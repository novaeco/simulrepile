#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_rom_crc.h"
#include "esp_vfs_fat.h"
#include "unity.h"
#include "wear_levelling.h"

#include "persist/save_manager.h"
#include "persist/schema_version.h"

#define TEST_FS_MOUNT_POINT "/spiflash"
#define TEST_PARTITION_LABEL "storage"
#define TEST_SAVE_ROOT TEST_FS_MOUNT_POINT "/saves_ut"

#define TEST_ASSERT_ESP_OK(expr) TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, (expr), #expr " failed")

static bool s_fs_mounted;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static void mount_test_fs(void)
{
    if (s_fs_mounted) {
        return;
    }
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = 4096,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        TEST_FS_MOUNT_POINT, TEST_PARTITION_LABEL, &mount_config, &s_wl_handle);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Failed to mount FAT test partition");
    s_fs_mounted = true;
}

static void unmount_test_fs(void)
{
    if (!s_fs_mounted) {
        return;
    }
    esp_err_t err = esp_vfs_fat_spiflash_unmount_rw_wl(TEST_FS_MOUNT_POINT, s_wl_handle);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Failed to unmount FAT test partition");
    s_wl_handle = WL_INVALID_HANDLE;
    s_fs_mounted = false;
}

static void remove_tree(const char *path, bool remove_root)
{
    DIR *dir = opendir(path);
    if (!dir) {
        struct stat st = {0};
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            unlink(path);
        }
        return;
    }

    struct dirent *entry;
    char child_path[256];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        int written = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(child_path)) {
            continue;
        }
        struct stat st = {0};
        if (stat(child_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_tree(child_path, true);
        } else {
            unlink(child_path);
        }
    }
    closedir(dir);

    if (remove_root) {
        rmdir(path);
    }
}

static void reset_save_root(void)
{
    mount_test_fs();
    remove_tree(TEST_SAVE_ROOT, true);
    TEST_ASSERT_ESP_OK(save_manager_init(TEST_SAVE_ROOT));
}

static void finalize_save_root(void)
{
    remove_tree(TEST_SAVE_ROOT, true);
    unmount_test_fs();
}

static void build_slot_path(int slot_index, bool backup, char *out_path, size_t len)
{
    snprintf(out_path, len, "%s/slot%d%s.json", TEST_SAVE_ROOT, slot_index, backup ? ".bak" : "");
}

typedef struct __attribute__((packed)) {
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t payload_crc32;
    uint32_t payload_length;
    uint64_t saved_at_unix;
} disk_header_t;

TEST_CASE("save_manager saves and validates CRC metadata", "[persist][crc]")
{
    reset_save_root();

    const char *payload = "{\"terrarium\":0,\"name\":\"Test\"}";
    save_slot_t slot = {
        .meta = {
            .schema_version = SIMULREPILE_SAVE_VERSION,
            .flags = 0,
            .payload_length = strlen(payload),
        },
        .payload = (uint8_t *)payload,
    };

    TEST_ASSERT_ESP_OK(save_manager_save_slot(0, &slot, false));

    save_slot_status_t status[4];
    TEST_ASSERT_ESP_OK(save_manager_list_slots(status, 4));
    TEST_ASSERT_TRUE_MESSAGE(status[0].primary.exists, "Primary slot missing after save");
    TEST_ASSERT_TRUE_MESSAGE(status[0].primary.valid, "Primary slot invalid after save");
    TEST_ASSERT_EQUAL_UINT32(slot.meta.payload_length, status[0].primary.meta.payload_length);
    uint32_t expected_crc = esp_rom_crc32_le(0, slot.payload, slot.meta.payload_length);
    TEST_ASSERT_EQUAL_HEX32(expected_crc, status[0].primary.meta.crc32);

    save_slot_t loaded = {0};
    TEST_ASSERT_ESP_OK(save_manager_load_slot(0, &loaded));
    TEST_ASSERT_NOT_NULL(loaded.payload);
    TEST_ASSERT_EQUAL_STRING(payload, (const char *)loaded.payload);
    save_manager_free_slot(&loaded);

    TEST_ASSERT_ESP_OK(save_manager_delete_slot(0));
    finalize_save_root();
}

TEST_CASE("save_manager falls back to backup on CRC mismatch", "[persist][backup]")
{
    reset_save_root();

    const char *initial_payload = "{\"state\":\"v1\"}";
    save_slot_t slot = {
        .meta = {
            .schema_version = SIMULREPILE_SAVE_VERSION,
            .flags = 0,
            .payload_length = strlen(initial_payload),
        },
        .payload = (uint8_t *)initial_payload,
    };
    TEST_ASSERT_ESP_OK(save_manager_save_slot(1, &slot, false));

    const char *updated_payload = "{\"state\":\"v2\"}";
    slot.payload = (uint8_t *)updated_payload;
    slot.meta.payload_length = strlen(updated_payload);
    TEST_ASSERT_ESP_OK(save_manager_save_slot(1, &slot, true));

    char primary_path[128];
    build_slot_path(1, false, primary_path, sizeof(primary_path));
    FILE *f = fopen(primary_path, "r+b");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "Failed to reopen primary for corruption");

    disk_header_t header = {0};
    size_t read_bytes = fread(&header, 1, sizeof(header), f);
    TEST_ASSERT_EQUAL_UINT(sizeof(header), read_bytes);
    TEST_ASSERT_EQUAL_UINT32(strlen(updated_payload), header.payload_length);
    long payload_offset = (long)sizeof(header);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, payload_offset + (long)(header.payload_length - 1), SEEK_SET));
    int c = fgetc(f);
    TEST_ASSERT_NOT_EQUAL(-1, c);
    fseek(f, -1, SEEK_CUR);
    fputc((c == 'x') ? 'y' : 'x', f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    save_slot_t loaded = {0};
    esp_err_t load_err = save_manager_load_slot(1, &loaded);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, load_err, "Load should recover from backup");
    TEST_ASSERT_NOT_NULL(loaded.payload);
    TEST_ASSERT_EQUAL_STRING(initial_payload, (const char *)loaded.payload);
    save_manager_free_slot(&loaded);

    save_slot_status_t status = {0};
    esp_err_t validate_err = save_manager_validate_slot(1, true, &status);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_CRC, validate_err, "Validation must signal CRC mismatch");
    TEST_ASSERT_TRUE(status.backup.valid);
    TEST_ASSERT_TRUE(status.backup.exists);

    TEST_ASSERT_ESP_OK(save_manager_delete_slot(1));
    finalize_save_root();
}

TEST_CASE("save_manager delete removes primary and backup", "[persist][cleanup]")
{
    reset_save_root();

    const char *payload = "{\"slot\":2}";
    save_slot_t slot = {
        .meta = {
            .schema_version = SIMULREPILE_SAVE_VERSION,
            .payload_length = strlen(payload),
        },
        .payload = (uint8_t *)payload,
    };

    TEST_ASSERT_ESP_OK(save_manager_save_slot(2, &slot, false));
    TEST_ASSERT_ESP_OK(save_manager_save_slot(2, &slot, true));

    TEST_ASSERT_ESP_OK(save_manager_delete_slot(2));

    char path[128];
    struct stat st = {0};
    build_slot_path(2, false, path, sizeof(path));
    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
    build_slot_path(2, true, path, sizeof(path));
    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);

    finalize_save_root();
}
