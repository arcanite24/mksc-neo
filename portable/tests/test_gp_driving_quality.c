#include "gp_driving_quality.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

static PortableGpDrivingQualityRace base_race(void)
{
    static PortableGpDrivingQualityRules rules;
    for(unsigned row=0u;row<3u;++row){
        rules.speed_target[row]=200;
        for(unsigned rank=0u;rank<4u;++rank)rules.rank_bonus[row][rank]=7;
    }
    for(unsigned driver=0u;driver<8u;++driver)rules.driver_bonus[driver]=3;
    PortableGpDrivingQualityRace race = {.rules=&rules};
    race.finish_rank = 0u;
    race.speed_class = 0u;
    race.driver_class = 0u;
    race.transition_countdown = 3u;
    race.route_count = 47u;
    race.race_ticks = 7000u;
    return race;
}

int main(void)
{
    PortableGpDrivingQuality quality;
    PortableGpDrivingQuality unchanged;
    PortableGpDrivingQualityRace race = base_race();
    int16_t score = 0;

    portable_gp_driving_quality_init(&quality);
    CHECK(portable_gp_driving_quality_tick(
              &quality, UINT16_C(0x0100), 0u, 0u, UINT8_C(0x5a)) ==
          PORTABLE_GP_DRIVING_QUALITY_OK);
    CHECK(portable_gp_driving_quality_tick(
              &quality, UINT16_C(0x0100), 0u, UINT16_C(0xc000), 0u) ==
          PORTABLE_GP_DRIVING_QUALITY_OK);
    CHECK(quality.counter_0c == 1u && quality.counter_10 == 1u &&
          quality.counter_20 == 1u);
    CHECK(portable_gp_driving_quality_record_item_use(
              &quality, UINT16_C(0x1003), UINT16_C(0x0100),
              UINT32_C(0x20)) == PORTABLE_GP_DRIVING_QUALITY_OK);
    CHECK(portable_gp_driving_quality_record_item_use(
              &quality, UINT16_C(0x200a), UINT16_C(0x0100), 0u) ==
          PORTABLE_GP_DRIVING_QUALITY_OK);
    CHECK(portable_gp_driving_quality_record_item_use(
              &quality, UINT16_C(0x100c), UINT16_C(0x0100), 0u) ==
          PORTABLE_GP_DRIVING_QUALITY_OK);
    CHECK(quality.event_14 == 1u && quality.event_15 == 1u &&
          quality.event_16 == 1u);
    CHECK(portable_gp_driving_quality_record_event(
              &quality, PORTABLE_GP_DRIVING_EVENT_0A,
              UINT16_C(0x0100), UINT32_C(0x20)) ==
          PORTABLE_GP_DRIVING_QUALITY_OK && quality.count_0a == 0u);
    CHECK(portable_gp_driving_quality_record_event(
              &quality, PORTABLE_GP_DRIVING_EVENT_1A,
              UINT16_C(0x0100), 0u) == PORTABLE_GP_DRIVING_QUALITY_OK &&
          quality.event_1a == 1u);
    CHECK(portable_gp_driving_quality_record_event(
              &quality, PORTABLE_GP_DRIVING_EVENT_1A,
              UINT16_C(0x2200), 0u) == PORTABLE_GP_DRIVING_QUALITY_OK &&
          quality.event_1a == 1u);
    quality.event_24 = UINT8_MAX;
    CHECK(portable_gp_driving_quality_record_event(
              &quality, PORTABLE_GP_DRIVING_EVENT_24,
              UINT16_C(0x0100), 0u) == PORTABLE_GP_DRIVING_QUALITY_OK &&
          quality.event_24 == 0u);
    CHECK(portable_gp_driving_quality_record_item_use(
              &quality, UINT16_C(0x1000), UINT16_C(0x0100), 0u) ==
          PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT);
    CHECK(portable_gp_driving_quality_record_event(
              &quality, (PortableGpDrivingQualityEvent)0,
              UINT16_C(0x0100), 0u) ==
          PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT);
    CHECK(portable_gp_driving_quality_tick(
              &quality, UINT16_C(0x2200), 0u, 0u, UINT8_C(0xff)) ==
          PORTABLE_GP_DRIVING_QUALITY_OK && quality.counter_0c == 1u);
    CHECK(portable_gp_driving_quality_tick(
              &quality, UINT16_C(0x0100), UINT32_C(0x20), 0u,
              UINT8_C(0xff)) == PORTABLE_GP_DRIVING_QUALITY_OK &&
          quality.counter_20 == 1u);
    portable_gp_driving_quality_init(&quality);
    CHECK(portable_gp_driving_quality_finish_race(&quality, &race, &score) ==
          PORTABLE_GP_DRIVING_QUALITY_OK);
    /* Authored rules:7 rank + ((200*47-7000)/8)=300 time +3 class. */
    CHECK(score == 310 && quality.samples[0] == 310 &&
          quality.average_score == 0);

    quality.count_0a = 2u;
    quality.counter_0c = 9u;
    quality.counter_10 = 3u;
    quality.event_14 = 1u;
    quality.event_15 = 1u;
    quality.event_16 = 1u;
    quality.event_17 = 1u;
    quality.event_18 = 1u;
    quality.event_19 = 1u;
    quality.event_1a = 1u;
    quality.event_1b = 1u;
    quality.event_1c = 1u;
    quality.event_24 = 1u;
    quality.counter_20 = 11u;
    race.course_linear_index = 1u;
    race.standings_gap_class = 2u;
    race.finish_rank = 2u;
    race.speed_class = 2u;
    race.driver_class = 2u;
    race.retro_course = 1u;
    race.transition_countdown = 2u;
    race.coins = 7u;
    race.route_count = 35u;
    race.race_ticks = 4900u;
    CHECK(portable_gp_driving_quality_finish_race(&quality, &race, &score) ==
          PORTABLE_GP_DRIVING_QUALITY_OK);
    CHECK(score == 285 && quality.samples[1] == 285);

    portable_gp_driving_quality_init(&quality);
    for (uint8_t slot = 0u; slot < 4u; ++slot)
    {
        portable_gp_driving_quality_begin_race(&quality);
        race = base_race();
        race.course_linear_index = slot;
        race.race_ticks = (uint32_t)(7000u + slot * 800u);
        CHECK(portable_gp_driving_quality_finish_race(
                  &quality, &race, &score) == PORTABLE_GP_DRIVING_QUALITY_OK);
    }
    CHECK(quality.samples[0] == 310 && quality.samples[1] == 210 &&
          quality.samples[2] == 110 && quality.samples[3] == 10 &&
          quality.average_score == 160);

    quality.count_0a = 9u;
    portable_gp_driving_quality_begin_race(&quality);
    CHECK(quality.samples[0] == 310 && quality.samples[3] == 10 &&
          quality.average_score == 0 && quality.count_0a == 0u);

    /* Source caps the five race-time fields' sum at 0xea5f. */
    portable_gp_driving_quality_init(&quality);
    race = base_race();
    race.race_ticks = UINT32_MAX;
    CHECK(portable_gp_driving_quality_finish_race(&quality, &race, &score) ==
          PORTABLE_GP_DRIVING_QUALITY_OK && score == 10);

    /* Source signed averaging truncates negative totals toward zero. */
    portable_gp_driving_quality_init(&quality);
    quality.samples[0] = -1;
    quality.samples[1] = -2;
    quality.samples[2] = -3;
    race = base_race();
    race.course_linear_index = 3u;
    race.route_count = 1u;
    race.race_ticks = UINT32_C(0xea5f);
    quality.counter_10 = 1u;
    CHECK(portable_gp_driving_quality_finish_race(&quality, &race, &score) ==
          PORTABLE_GP_DRIVING_QUALITY_OK && score == 8 &&
          quality.average_score == 0);

    unchanged = quality;
    race.course_linear_index = 4u;
    CHECK(portable_gp_driving_quality_finish_race(&quality, &race, &score) ==
          PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT &&
          memcmp(&quality, &unchanged, sizeof quality) == 0);
    CHECK(portable_gp_driving_quality_finish_race(NULL, &race, &score) ==
          PORTABLE_GP_DRIVING_QUALITY_INVALID_ARGUMENT);
    return 0;
}
