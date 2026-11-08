#include "time_trial_ghost.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void portable_time_trial_ghost_record_init(
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t driver_class,
    uint8_t course_id)
{
    if (record == NULL)
        return;
    memset(record, 0, PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    record[PORTABLE_TIME_TRIAL_GHOST_DRIVER_OFFSET] =
        (uint8_t)(driver_class & 7u);
    record[PORTABLE_TIME_TRIAL_GHOST_COURSE_OFFSET] = course_id;
}

uint8_t portable_time_trial_ghost_record_driver(const uint8_t *record)
{
    return record == NULL ? 0u :
        (uint8_t)(record[PORTABLE_TIME_TRIAL_GHOST_DRIVER_OFFSET] & 7u);
}

uint8_t portable_time_trial_ghost_record_roster_driver(const uint8_t *record)
{
    return record == NULL ? 0u :
        record[PORTABLE_TIME_TRIAL_GHOST_ROSTER_DRIVER_OFFSET];
}

void portable_time_trial_ghost_record_set_roster_driver(uint8_t *record,
                                                        uint8_t driver_id)
{
    if (record != NULL)
        record[PORTABLE_TIME_TRIAL_GHOST_ROSTER_DRIVER_OFFSET] = driver_id;
}

uint8_t portable_time_trial_ghost_record_course(const uint8_t *record)
{
    return record == NULL ? 0u :
        record[PORTABLE_TIME_TRIAL_GHOST_COURSE_OFFSET];
}

uint16_t portable_time_trial_ghost_record_time(const uint8_t *record)
{
    return record == NULL ? 0u :
        read_le16(record + PORTABLE_TIME_TRIAL_GHOST_TIME_OFFSET);
}

void portable_time_trial_ghost_record_set_time(uint8_t *record,
                                               uint16_t total_ticks)
{
    if (record != NULL)
        write_le16(record + PORTABLE_TIME_TRIAL_GHOST_TIME_OFFSET, total_ticks);
}

int portable_time_trial_ghost_should_replace(const uint8_t *current_record,
                                             const uint8_t *candidate_record)
{
    uint16_t current_time;
    uint16_t candidate_time;

    if (current_record == NULL || candidate_record == NULL)
        return 0;
    candidate_time = portable_time_trial_ghost_record_time(candidate_record);
    if (candidate_time == 0u)
        return 0;
    current_time = portable_time_trial_ghost_record_time(current_record);
    return current_time == 0u ||
        portable_time_trial_ghost_record_course(current_record) !=
            portable_time_trial_ghost_record_course(candidate_record) ||
        candidate_time < current_time;
}

int portable_time_trial_ghost_encoder_init(PortableTimeTrialGhostEncoder *state,
                                           uint8_t *payload,
                                           size_t payload_size)
{
    if (state == NULL || payload == NULL ||
        payload_size < PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE)
        return 0;
    memset(state, 0, sizeof *state);
    state->payload = payload;
    state->payload_size = payload_size;
    state->remaining = PORTABLE_TIME_TRIAL_GHOST_SOURCE_CAPACITY - 6;
    memset(payload, 0, 5u);
    payload[5] = 0xffu;
    return 1;
}

int portable_time_trial_ghost_encoder_append(PortableTimeTrialGhostEncoder *state,
                                             uint16_t control_word)
{
    uint8_t *current;
    uint8_t count;

    if (state == NULL || state->payload == NULL || state->exhausted ||
        state->remaining <= 0 || state->cursor + 3u > state->payload_size)
        return 0;
    current = state->payload + state->cursor;
    count = current[2];
    ++state->position;
    if (count <= 0xfdu && read_le16(current) == control_word)
    {
        current[2] = (uint8_t)(count + 1u);
        return 1;
    }
    if (state->cursor + 6u > state->payload_size)
    {
        state->exhausted = 1;
        return 0;
    }
    state->cursor += 3u;
    current = state->payload + state->cursor;
    write_le16(current, control_word);
    current[2] = 1u;
    current[3] = 0u;
    current[4] = 0u;
    current[5] = 0xffu;
    state->remaining -= 3;
    if (state->remaining <= 0)
        state->exhausted = 1;
    return 1;
}

int portable_time_trial_ghost_decoder_init(PortableTimeTrialGhostDecoder *state,
                                           const uint8_t *payload,
                                           size_t payload_size)
{
    if (state == NULL || payload == NULL || payload_size < 3u)
        return 0;
    memset(state, 0, sizeof *state);
    state->payload = payload;
    state->payload_size = payload_size;
    state->remaining = payload[2];
    return 1;
}

int portable_time_trial_ghost_decoder_next(PortableTimeTrialGhostDecoder *state,
                                           uint16_t *out_control_word)
{
    const uint8_t *run;

    if (state == NULL || out_control_word == NULL || state->payload == NULL ||
        state->ended || state->cursor + 3u > state->payload_size)
        return 0;
    run = state->payload + state->cursor;
    if (run[2] == 0xffu)
    {
        state->ended = 1;
        return 0;
    }
    if (state->remaining == 0u)
    {
        state->cursor += 3u;
        if (state->cursor + 3u > state->payload_size)
        {
            state->ended = 1;
            return 0;
        }
        run = state->payload + state->cursor;
        if (run[2] == 0xffu)
        {
            state->ended = 1;
            return 0;
        }
        state->remaining = run[2];
    }
    if (state->remaining == 0u)
    {
        state->ended = 1;
        return 0;
    }
    --state->remaining;
    *out_control_word = read_le16(run);
    return 1;
}
