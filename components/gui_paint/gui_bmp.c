/*****************************************************************************
* | File        :   BMP_APP.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                   The bmp picture is read from the SD card and drawn into the buffer
*
*----------------
* |     This version:   V1.0
* | Date        :   2024-12-06
* | Info        :   Basic version
*
******************************************************************************/
#include "gui_bmp.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline uint16_t rgb_from_palette(const RGBQUAD *palette, size_t palette_size, uint8_t index)
{
    if (palette == NULL || index >= palette_size) {
        return (index & 0x01u) ? 0xFFFFu : 0x0000u;
    }
    return RGB(palette[index].rgbRed, palette[index].rgbGreen, palette[index].rgbBlue);
}

static uint16_t extract_pixel_color(const uint8_t *row_data,
                                    int col,
                                    int bit_count,
                                    const BMPINF *bmp_info,
                                    const RGBQUAD *palette,
                                    size_t palette_size)
{
    switch (bit_count) {
        case 1: {
            int byte_offset = col / 8;
            int bit_offset = 7 - (col & 0x07);
            uint8_t index = (row_data[byte_offset] >> bit_offset) & 0x01u;
            return rgb_from_palette(palette, palette_size, index);
        }
        case 4: {
            int byte_offset = col / 2;
            bool high_nibble = ((col & 0x01) == 0);
            uint8_t raw = row_data[byte_offset];
            uint8_t index = high_nibble ? ((raw >> 4) & 0x0Fu) : (raw & 0x0Fu);
            return rgb_from_palette(palette, palette_size, index);
        }
        case 8: {
            uint8_t index = row_data[col];
            return rgb_from_palette(palette, palette_size, index);
        }
        case 16: {
            size_t offset = (size_t)col * 2u;
            uint16_t pixel = (uint16_t)row_data[offset] | ((uint16_t)row_data[offset + 1u] << 8);
            if (bmp_info->bCompression == 3u || bmp_info->bInfoSize >= 0x38u) {
                return pixel; // RGB565
            }
            uint16_t r5 = (pixel >> 10) & 0x1Fu;
            uint16_t g5 = (pixel >> 5) & 0x1Fu;
            uint16_t b5 = pixel & 0x1Fu;
            uint16_t g6 = (uint16_t)((g5 * 0x3Fu) / 0x1Fu);
            return (uint16_t)((r5 << 11) | (g6 << 5) | (b5 << 0));
        }
        case 24: {
            size_t offset = (size_t)col * 3u;
            uint8_t blue = row_data[offset];
            uint8_t green = row_data[offset + 1u];
            uint8_t red = row_data[offset + 2u];
            return RGB(red, green, blue);
        }
        case 32: {
            size_t offset = (size_t)col * 4u;
            uint8_t blue = row_data[offset];
            uint8_t green = row_data[offset + 1u];
            uint8_t red = row_data[offset + 2u];
            return RGB(red, green, blue);
        }
        default:
            Debug("Unsupported BMP depth: %d\n", bit_count);
            break;
    }
    return 0;
}

UBYTE GUI_ReadBmp(UWORD Xstart, UWORD Ystart, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        Debug("Cannot open the file: %s\n", path);
        return 0;
    }

    BMPFILEHEADER file_header;
    if (fread(&file_header, sizeof(file_header), 1, fp) != 1) {
        Debug("Failed to read BMP file header: %s\n", path);
        fclose(fp);
        return 0;
    }

    if (file_header.bType != 0x4D42u) {
        Debug("Invalid BMP signature in %s\n", path);
        fclose(fp);
        return 0;
    }

    BMPINF info_header;
    if (fread(&info_header, sizeof(info_header), 1, fp) != 1) {
        Debug("Failed to read BMP info header: %s\n", path);
        fclose(fp);
        return 0;
    }

    int32_t width = (int32_t)info_header.bWidth;
    int32_t height_raw = (int32_t)info_header.bHeight;
    bool top_down = height_raw < 0;
    int32_t height = top_down ? -height_raw : height_raw;

    if (width <= 0 || height <= 0) {
        Debug("Unsupported BMP dimensions %d x %d in %s\n", width, height_raw, path);
        fclose(fp);
        return 0;
    }

    size_t palette_entries = 0;
    if (info_header.bBitCount <= 8) {
        uint32_t used = (uint32_t)info_header.bClrUsed;
        if (used == 0u) {
            used = 1u << info_header.bBitCount;
        }
        if (used > 256u) {
            Debug("Palette too large (%u entries) in %s\n", used, path);
            fclose(fp);
            return 0;
        }
        palette_entries = used;
    }

    RGBQUAD palette[256];
    if (palette_entries > 0) {
        size_t header_bytes = (size_t)info_header.bInfoSize;
        if (header_bytes < sizeof(info_header)) {
            header_bytes = sizeof(info_header);
        }
        long palette_offset = (long)sizeof(BMPFILEHEADER) + (long)header_bytes;
        if (fseek(fp, palette_offset, SEEK_SET) != 0) {
            Debug("Failed to seek palette for %s\n", path);
            fclose(fp);
            return 0;
        }
        if (fread(palette, sizeof(RGBQUAD), palette_entries, fp) != palette_entries) {
            Debug("Failed to read palette for %s\n", path);
            fclose(fp);
            return 0;
        }
    }

    if (fseek(fp, (long)file_header.bOffset, SEEK_SET) != 0) {
        Debug("Failed to seek pixel data for %s\n", path);
        fclose(fp);
        return 0;
    }

    size_t row_bytes = ((size_t)width * info_header.bBitCount + 31u) / 32u * 4u;
    uint8_t *row_buffer = (uint8_t *)malloc(row_bytes);
    if (row_buffer == NULL) {
        Debug("Memory allocation failed (%zu bytes) for %s\n", row_bytes, path);
        fclose(fp);
        return 0;
    }

    for (int32_t row = 0; row < height; ++row) {
        if (fread(row_buffer, 1, row_bytes, fp) != row_bytes) {
            Debug("Incomplete BMP row %d in %s\n", row, path);
            free(row_buffer);
            fclose(fp);
            return 0;
        }

        int32_t dst_row = top_down ? row : (height - 1 - row);
        for (int32_t col = 0; col < width; ++col) {
            uint16_t color = extract_pixel_color(row_buffer, col, info_header.bBitCount,
                                                 &info_header, palette_entries ? palette : NULL,
                                                 palette_entries);
            Paint_SetPixel((UWORD)(col + Xstart), (UWORD)(dst_row + Ystart), color);
        }
    }

    free(row_buffer);
    fclose(fp);
    return 1;
}
