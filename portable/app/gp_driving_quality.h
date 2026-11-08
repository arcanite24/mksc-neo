#ifndef PORTABLE_GP_DRIVING_QUALITY_H
#define PORTABLE_GP_DRIVING_QUALITY_H

#include <stdint.h>
#include "gp_scoring.h"

#define PORTABLE_GP_DRIVING_SAMPLE_COUNT 4u

typedef enum
{
    PORTABLE_GP_DRIVING_QUALITY_OK = 0,
    PORTABLE_GP_DRIVING_QUALITY_INVALID_ARGUMENT,
    PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT
} PortableGpDrivingQualityStatus;

/* Byte offsets from source state +0xc8c.  Keep the offset in the public name
 * until every caller has a qualified player-visible label. */
typedef enum
{
    PORTABLE_GP_DRIVING_EVENT_0A = 0x0a,
    PORTABLE_GP_DRIVING_EVENT_14 = 0x14,
    PORTABLE_GP_DRIVING_EVENT_15 = 0x15,
    PORTABLE_GP_DRIVING_EVENT_16 = 0x16,
    PORTABLE_GP_DRIVING_EVENT_17 = 0x17,
    PORTABLE_GP_DRIVING_EVENT_18 = 0x18,
    PORTABLE_GP_DRIVING_EVENT_19 = 0x19,
    PORTABLE_GP_DRIVING_EVENT_1A = 0x1a,
    PORTABLE_GP_DRIVING_EVENT_1B = 0x1b,
    PORTABLE_GP_DRIVING_EVENT_1C = 0x1c,
    PORTABLE_GP_DRIVING_EVENT_24 = 0x24
} PortableGpDrivingQualityEvent;

/* Source state +0xc8c..+0xcb0. Names retain their source offsets until the
 * remaining event call sites have player-visible identities. */
typedef struct
{
    int16_t samples[PORTABLE_GP_DRIVING_SAMPLE_COUNT];
    int16_t average_score;
    uint8_t count_0a;
    uint32_t counter_0c;
    uint32_t counter_10;
    uint8_t event_14;
    uint8_t event_15;
    uint8_t event_16;
    uint8_t event_17;
    uint8_t event_18;
    uint8_t event_19;
    uint8_t event_1a;
    uint8_t event_1b;
    uint8_t event_1c;
    uint32_t counter_20;
    uint8_t event_24;
} PortableGpDrivingQuality;

typedef struct
{
    const PortableGpDrivingQualityRules *rules;
    uint8_t course_linear_index;
    uint8_t standings_gap_class;
    uint8_t finish_rank;
    uint8_t speed_class;
    uint8_t driver_class;
    uint8_t retro_course;
    uint8_t transition_countdown;
    uint16_t coins;
    uint16_t route_count;
    uint32_t race_ticks;
} PortableGpDrivingQualityRace;

void portable_gp_driving_quality_init(PortableGpDrivingQuality *quality);
void portable_gp_driving_quality_begin_race(PortableGpDrivingQuality *quality);
PortableGpDrivingQualityStatus portable_gp_driving_quality_tick(
    PortableGpDrivingQuality *quality, uint16_t race_mode_flags,
    uint32_t driver_flags, uint16_t control_word, uint8_t material_85);
/* The caller must own the active-player identity test made by the source
 * helpers.  This function reproduces their mode/finished-player gates and
 * byte-width increment only. */
PortableGpDrivingQualityStatus portable_gp_driving_quality_record_event(
    PortableGpDrivingQuality *quality, PortableGpDrivingQualityEvent event,
    uint16_t race_mode_flags, uint32_t driver_flags);
/* sub_8037B30 dispatches only these three item IDs to score callbacks. */
PortableGpDrivingQualityStatus portable_gp_driving_quality_record_item_use(
    PortableGpDrivingQuality *quality, uint16_t item,
    uint16_t race_mode_flags, uint32_t driver_flags);
PortableGpDrivingQualityStatus portable_gp_driving_quality_finish_race(
    PortableGpDrivingQuality *quality,
    const PortableGpDrivingQualityRace *race,
    int16_t *sample_score);

#endif
