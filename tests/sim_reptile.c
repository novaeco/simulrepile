#include "reptile_logic.h"
#include "game_mode.h"
#include <stdio.h>
#include <time.h>

static const char *stage_to_str(reptile_growth_stage_t stage) {
  switch (stage) {
  case REPTILE_GROWTH_HATCHLING:
    return "hatchling";
  case REPTILE_GROWTH_JUVENILE:
    return "juvenile";
  case REPTILE_GROWTH_ADULT:
    return "adult";
  case REPTILE_GROWTH_SENIOR:
    return "senior";
  default:
    return "unknown";
  }
}

int main(void) {
  reptile_facility_t facility;
  game_mode_set(GAME_MODE_SIMULATION);
  reptile_facility_init(&facility, true, "test_slot", GAME_MODE_SIMULATION);

  reptile_facility_metrics_t metrics;
  reptile_facility_compute_metrics(&facility, &metrics);
  printf("Initial occupied=%u free=%u cash=%.2f€\n", metrics.occupied,
         metrics.free_slots, facility.economy.cash_cents / 100.0);

  for (int i = 0; i < 6 * 60; ++i) {
    reptile_facility_tick(&facility, 1000);
  }

  const terrarium_t *t0 = reptile_facility_get_terrarium_const(&facility, 0);
  printf("T01 growth=%.1f%% stage=%s income=%.2f€/j incident=%d\n",
         t0->growth * 100.0f, stage_to_str(t0->stage),
         t0->revenue_cents_per_day / 100.0f, t0->incident);

  reptile_certificate_t cert = {
      .valid = true,
      .issue_date = time(NULL),
      .expiry_date = time(NULL) + 365L * 24L * 3600L,
  };
  snprintf(cert.id, sizeof(cert.id), "AUTO-%ld", (long)cert.issue_date);
  snprintf(cert.authority, sizeof(cert.authority), "DDPP test");
  reptile_terrarium_add_certificate(reptile_facility_get_terrarium(&facility, 0),
                                    &cert);

  for (int i = 0; i < 4 * 60; ++i) {
    reptile_facility_tick(&facility, 1000);
  }
  printf("After certification alerts=%u compliance=%u\n",
         facility.alerts_active, facility.compliance_alerts);

  facility.terrariums[0].certificate_count = 0;
  for (int i = 0; i < 8 * 60; ++i) {
    reptile_facility_tick(&facility, 1000);
  }
  printf("Compliance incidents=%u total fines=%.2f€\n",
         facility.compliance_alerts, facility.economy.fines_cents / 100.0);

  reptile_inventory_add_feed(&facility, 20);
  reptile_inventory_add_water(&facility, 40);
  printf("Stocks feed=%u water=%uL cash=%.2f€\n",
         facility.inventory.feeders, facility.inventory.water_reserve_l,
         facility.economy.cash_cents / 100.0);

  reptile_facility_save(&facility);

  reptile_facility_t loaded;
  reptile_facility_init(&loaded, true, "test_slot", GAME_MODE_SIMULATION);
  if (reptile_facility_load(&loaded) == ESP_OK) {
    printf("Loaded slot=%s mature=%u average_growth=%.1f%%\n", loaded.slot,
           loaded.mature_count, loaded.average_growth * 100.0f);
  } else {
    printf("Failed to load saved state\n");
  }

  return 0;
}
