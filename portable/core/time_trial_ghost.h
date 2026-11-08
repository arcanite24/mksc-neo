#ifndef PORTABLE_TIME_TRIAL_GHOST_H
#define PORTABLE_TIME_TRIAL_GHOST_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS = 10,
    PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE = 0x1000,
    PORTABLE_TIME_TRIAL_GHOST_DRIVER_OFFSET = 0x08,
    /* Native extension in the source record's spare byte: roster driver ID,
     * zero for the class's stock driver. */
    PORTABLE_TIME_TRIAL_GHOST_ROSTER_DRIVER_OFFSET = 0x09,
    PORTABLE_TIME_TRIAL_GHOST_TIME_OFFSET = 0x0a,
    PORTABLE_TIME_TRIAL_GHOST_COURSE_OFFSET = 0x11,
    PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET = 0x28,
    PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE =
        PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE -
        PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
    PORTABLE_TIME_TRIAL_GHOST_SOURCE_CAPACITY = 0x0fd6,
    PORTABLE_TIME_TRIAL_GHOST_MAX_RUN = 0xfe,
};

typedef struct
{
    uint8_t *payload;
    size_t payload_size;
    size_t cursor;
    int32_t remaining;
    uint32_t position;
    int exhausted;
} PortableTimeTrialGhostEncoder;

typedef struct
{
    const uint8_t *payload;
    size_t payload_size;
    size_t cursor;
    uint32_t remaining;
    int ended;
} PortableTimeTrialGhostDecoder;

void portable_time_trial_ghost_record_init(
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t driver_class,
    uint8_t course_id);

uint8_t portable_time_trial_ghost_record_driver(const uint8_t *record);
uint8_t portable_time_trial_ghost_record_roster_driver(const uint8_t *record);
void portable_time_trial_ghost_record_set_roster_driver(uint8_t *record,
                                                        uint8_t driver_id);
uint8_t portable_time_trial_ghost_record_course(const uint8_t *record);
uint16_t portable_time_trial_ghost_record_time(const uint8_t *record);
void portable_time_trial_ghost_record_set_time(uint8_t *record,
                                               uint16_t total_ticks);

int portable_time_trial_ghost_should_replace(const uint8_t *current_record,
                                             const uint8_t *candidate_record);

int portable_time_trial_ghost_encoder_init(PortableTimeTrialGhostEncoder *state,
                                           uint8_t *payload,
                                           size_t payload_size);
int portable_time_trial_ghost_encoder_append(PortableTimeTrialGhostEncoder *state,
                                             uint16_t control_word);

int portable_time_trial_ghost_decoder_init(PortableTimeTrialGhostDecoder *state,
                                           const uint8_t *payload,
                                           size_t payload_size);
int portable_time_trial_ghost_decoder_next(PortableTimeTrialGhostDecoder *state,
                                           uint16_t *out_control_word);

#endif
