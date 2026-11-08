#include "gp_driving_quality.h"

#include <stddef.h>
#include <string.h>

static int32_t divide_toward_zero(int32_t value, int32_t divisor)
{
    return value / divisor;
}

void portable_gp_driving_quality_init(PortableGpDrivingQuality *quality)
{
    if (quality != NULL)
        memset(quality, 0, sizeof *quality);
}

void portable_gp_driving_quality_begin_race(PortableGpDrivingQuality *quality)
{
    int16_t samples[PORTABLE_GP_DRIVING_SAMPLE_COUNT];

    if (quality == NULL)
        return;
    memcpy(samples, quality->samples, sizeof samples);
    memset(quality, 0, sizeof *quality);
    memcpy(quality->samples, samples, sizeof samples);
}

PortableGpDrivingQualityStatus portable_gp_driving_quality_tick(
    PortableGpDrivingQuality *quality, uint16_t race_mode_flags,
    uint32_t driver_flags, uint16_t control_word, uint8_t material_85)
{
    if (quality == NULL)
        return PORTABLE_GP_DRIVING_QUALITY_INVALID_ARGUMENT;
    if (race_mode_flags != UINT16_C(0x0100) ||
        (driver_flags & UINT32_C(0x20)) != 0u)
        return PORTABLE_GP_DRIVING_QUALITY_OK;
    if ((control_word & UINT16_C(0x8000)) == 0u &&
        quality->counter_0c <= UINT32_C(0xffff))
        ++quality->counter_0c;
    if ((control_word & UINT16_C(0x4000)) != 0u &&
        quality->counter_10 <= UINT32_C(0xffff))
        ++quality->counter_10;
    if (material_85 > UINT8_C(0x59) &&
        quality->counter_20 <= UINT32_C(0xffff))
        ++quality->counter_20;
    return PORTABLE_GP_DRIVING_QUALITY_OK;
}

PortableGpDrivingQualityStatus portable_gp_driving_quality_record_event(
    PortableGpDrivingQuality *quality, PortableGpDrivingQualityEvent event,
    uint16_t race_mode_flags, uint32_t driver_flags)
{
    uint8_t *counter = NULL;

    if (quality == NULL)
        return PORTABLE_GP_DRIVING_QUALITY_INVALID_ARGUMENT;
    switch (event)
    {
        case PORTABLE_GP_DRIVING_EVENT_0A: counter = &quality->count_0a; break;
        case PORTABLE_GP_DRIVING_EVENT_14: counter = &quality->event_14; break;
        case PORTABLE_GP_DRIVING_EVENT_15: counter = &quality->event_15; break;
        case PORTABLE_GP_DRIVING_EVENT_16: counter = &quality->event_16; break;
        case PORTABLE_GP_DRIVING_EVENT_17: counter = &quality->event_17; break;
        case PORTABLE_GP_DRIVING_EVENT_18: counter = &quality->event_18; break;
        case PORTABLE_GP_DRIVING_EVENT_19: counter = &quality->event_19; break;
        case PORTABLE_GP_DRIVING_EVENT_1A: counter = &quality->event_1a; break;
        case PORTABLE_GP_DRIVING_EVENT_1B: counter = &quality->event_1b; break;
        case PORTABLE_GP_DRIVING_EVENT_1C: counter = &quality->event_1c; break;
        case PORTABLE_GP_DRIVING_EVENT_24: counter = &quality->event_24; break;
        default: return PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT;
    }
    if (race_mode_flags != UINT16_C(0x0100))
        return PORTABLE_GP_DRIVING_QUALITY_OK;
    /* sub_8043310/44/78 count player item use without the +0x48 finished
     * test.  Every victim/response helper from +0x0a and +0x17 onward has it. */
    if (event != PORTABLE_GP_DRIVING_EVENT_14 &&
        event != PORTABLE_GP_DRIVING_EVENT_15 &&
        event != PORTABLE_GP_DRIVING_EVENT_16 &&
        (driver_flags & UINT32_C(0x20)) != 0u)
        return PORTABLE_GP_DRIVING_QUALITY_OK;
    ++*counter;
    return PORTABLE_GP_DRIVING_QUALITY_OK;
}

PortableGpDrivingQualityStatus portable_gp_driving_quality_record_item_use(
    PortableGpDrivingQuality *quality, uint16_t item,
    uint16_t race_mode_flags, uint32_t driver_flags)
{
    PortableGpDrivingQualityEvent event;

    switch (item)
    {
        case UINT16_C(0x1003): case UINT16_C(0x2003):
            event = PORTABLE_GP_DRIVING_EVENT_14; break;
        case UINT16_C(0x100a): case UINT16_C(0x200a):
            event = PORTABLE_GP_DRIVING_EVENT_15; break;
        case UINT16_C(0x100c): case UINT16_C(0x200c):
            event = PORTABLE_GP_DRIVING_EVENT_16; break;
        default:
            return quality == NULL
                ? PORTABLE_GP_DRIVING_QUALITY_INVALID_ARGUMENT
                : PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT;
    }
    return portable_gp_driving_quality_record_event(
        quality, event, race_mode_flags, driver_flags);
}

PortableGpDrivingQualityStatus portable_gp_driving_quality_finish_race(
    PortableGpDrivingQuality *quality,
    const PortableGpDrivingQualityRace *race,
    int16_t *sample_score)
{
    uint32_t source_ticks;
    int32_t time_bonus;
    int32_t event_penalty;
    int32_t event_bonus;
    int32_t score;
    uint8_t slot;

    if (quality == NULL || race == NULL || race->rules == NULL)
        return PORTABLE_GP_DRIVING_QUALITY_INVALID_ARGUMENT;
    if (race->course_linear_index >= PORTABLE_GP_DRIVING_SAMPLE_COUNT ||
        race->standings_gap_class >= 3u || race->speed_class >= 3u ||
        race->driver_class >= 8u || race->transition_countdown > 3u ||
        race->route_count == 0u || quality->counter_0c > UINT32_C(0x10000) ||
        quality->counter_10 > UINT32_C(0x10000) ||
        quality->counter_20 > UINT32_C(0x10000))
        return PORTABLE_GP_DRIVING_QUALITY_UNSUPPORTED_INPUT;

    source_ticks = race->race_ticks > UINT32_C(0xea5f)
        ? UINT32_C(0xea5f) : race->race_ticks;
    time_bonus = divide_toward_zero(
        race->rules->speed_target[race->speed_class] * race->route_count -
            (int32_t)source_ticks,
        8);
    if (time_bonus < 0)
        time_bonus = 0;

    event_penalty =
        (int32_t)quality->event_14 * 5 +
        (int32_t)quality->event_15 * 30 +
        (int32_t)quality->event_16 * 5 +
        (int32_t)quality->event_17 * 30 +
        (int32_t)quality->event_18 * 15 +
        (int32_t)quality->event_19 * 20 +
        (int32_t)quality->event_1a * 15;
    event_bonus =
        (int32_t)quality->event_1b * 25 +
        (int32_t)quality->event_1c * 15 +
        (int32_t)quality->event_24 * 15;

    score = race->retro_course != 0u ? 100 : 0;
    if (race->finish_rank < 4u)
        score += race->rules->rank_bonus[race->standings_gap_class]
                                  [race->finish_rank];
    score += (int32_t)race->coins * 4;
    score += (int32_t)quality->count_0a * 40;
    score += time_bonus;
    score += race->rules->driver_bonus[race->driver_class];
    score -= (int32_t)(quality->counter_0c >> 2u);
    score -= (int32_t)(quality->counter_10 << 1u);
    score -= event_penalty;
    score -= (int32_t)(3u - race->transition_countdown) * 120;
    score += event_bonus;
    score -= (int32_t)(quality->counter_20 >> 2u);

    slot = (uint8_t)(race->course_linear_index & 3u);
    const uint16_t bits=(uint16_t)(uint32_t)score;
    quality->samples[slot]=(int16_t)(bits<UINT16_C(0x8000)?(int32_t)bits:
        (int32_t)bits-INT32_C(65536));
    if (slot == 3u)
    {
        int32_t total = 0;
        for (size_t index = 0u;
             index < PORTABLE_GP_DRIVING_SAMPLE_COUNT; ++index)
            total += quality->samples[index];
        quality->average_score = (int16_t)divide_toward_zero(total, 4);
    }
    if (sample_score != NULL)
        *sample_score = quality->samples[slot];
    return PORTABLE_GP_DRIVING_QUALITY_OK;
}
