#include "time_trial_ghost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "time trial ghost failed line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

static int test_source_capture(void)
{
    static const uint8_t expected[][15] = {
        {0,0,0xfe, 0,0,0xfe, 0,0,0x16, 0,0x80,1, 0,0,0xff},
        {0,0,0xfe, 0,0,0xfe, 0,0,0x16, 0,0x80,2, 0,0,0xff},
        {0,0,0xfe, 0,0,0xfe, 0,0,0x16, 0,0x80,3, 0,0,0xff},
        {0,0,0xfe, 0,0,0xfe, 0,0,0x16, 0,0x80,4, 0,0,0xff},
    };
    uint8_t payload[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE] = {0};
    PortableTimeTrialGhostEncoder encoder;

    CHECK(portable_time_trial_ghost_encoder_init(
        &encoder, payload, sizeof payload));
    for (unsigned index = 0; index < 530u; ++index)
        CHECK(portable_time_trial_ghost_encoder_append(&encoder, 0u));
    for (unsigned index = 0; index < 4u; ++index)
    {
        CHECK(portable_time_trial_ghost_encoder_append(&encoder, 0x8000u));
        CHECK(memcmp(payload, expected[index], sizeof expected[index]) == 0);
    }
    CHECK(encoder.position == 534u);
    return 0;
}

static int test_round_trip_and_run_split(void)
{
    uint8_t payload[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE] = {0};
    PortableTimeTrialGhostEncoder encoder;
    PortableTimeTrialGhostDecoder decoder;
    uint16_t value;

    CHECK(portable_time_trial_ghost_encoder_init(
        &encoder, payload, sizeof payload));
    for (unsigned index = 0; index < 255u; ++index)
        CHECK(portable_time_trial_ghost_encoder_append(&encoder, 0x8123u));
    CHECK(payload[2] == 0u);
    CHECK(payload[3] == 0x23u && payload[4] == 0x81u && payload[5] == 0xfeu);
    CHECK(payload[6] == 0x23u && payload[7] == 0x81u && payload[8] == 1u);
    CHECK(payload[11] == 0xffu);
    CHECK(portable_time_trial_ghost_decoder_init(
        &decoder, payload, sizeof payload));
    for (unsigned index = 0; index < 255u; ++index)
    {
        CHECK(portable_time_trial_ghost_decoder_next(&decoder, &value));
        CHECK(value == 0x8123u);
    }
    CHECK(!portable_time_trial_ghost_decoder_next(&decoder, &value));
    return 0;
}

static int test_capacity_and_malformed(void)
{
    uint8_t payload[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE] = {0};
    PortableTimeTrialGhostEncoder encoder;
    PortableTimeTrialGhostDecoder decoder;
    uint16_t value;
    unsigned accepted = 0u;

    CHECK(portable_time_trial_ghost_encoder_init(
        &encoder, payload, sizeof payload));
    while (portable_time_trial_ghost_encoder_append(
               &encoder, (uint16_t)(accepted & 1u ? 0x4000u : 0x8000u)))
        ++accepted;
    CHECK(accepted == 1350u);
    CHECK(encoder.exhausted);
    CHECK(encoder.cursor == 4050u);
    CHECK(payload[4055] == 0xffu);

    CHECK(portable_time_trial_ghost_decoder_init(&decoder, payload, 2u) == 0);
    CHECK(portable_time_trial_ghost_decoder_init(&decoder, payload, 3u));
    CHECK(!portable_time_trial_ghost_decoder_next(&decoder, &value));
    payload[0] = 0x34u;
    payload[1] = 0x12u;
    payload[2] = 1u;
    CHECK(portable_time_trial_ghost_decoder_init(&decoder, payload, 3u));
    CHECK(portable_time_trial_ghost_decoder_next(&decoder, &value));
    CHECK(value == 0x1234u);
    CHECK(!portable_time_trial_ghost_decoder_next(&decoder, &value));
    return 0;
}

static int test_record_policy(void)
{
    uint8_t current[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];

    portable_time_trial_ghost_record_init(current, 1u, 24u);
    portable_time_trial_ghost_record_init(candidate, 6u, 24u);
    portable_time_trial_ghost_record_set_time(candidate, 800u);
    CHECK(portable_time_trial_ghost_record_driver(candidate) == 6u);
    CHECK(portable_time_trial_ghost_record_course(candidate) == 24u);
    CHECK(portable_time_trial_ghost_record_time(candidate) == 800u);
    CHECK(portable_time_trial_ghost_should_replace(current, candidate));

    portable_time_trial_ghost_record_set_time(current, 800u);
    CHECK(!portable_time_trial_ghost_should_replace(current, candidate));
    portable_time_trial_ghost_record_set_time(candidate, 801u);
    CHECK(!portable_time_trial_ghost_should_replace(current, candidate));
    portable_time_trial_ghost_record_set_time(candidate, 799u);
    CHECK(portable_time_trial_ghost_should_replace(current, candidate));
    candidate[PORTABLE_TIME_TRIAL_GHOST_COURSE_OFFSET] = 25u;
    portable_time_trial_ghost_record_set_time(candidate, 900u);
    CHECK(portable_time_trial_ghost_should_replace(current, candidate));
    portable_time_trial_ghost_record_set_time(candidate, 0u);
    CHECK(!portable_time_trial_ghost_should_replace(current, candidate));
    return 0;
}

static int print_capture_prefixes(void)
{
    uint8_t payload[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE] = {0};
    PortableTimeTrialGhostEncoder encoder;

    CHECK(portable_time_trial_ghost_encoder_init(
        &encoder, payload, sizeof payload));
    for (unsigned index = 0; index < 530u; ++index)
        CHECK(portable_time_trial_ghost_encoder_append(&encoder, 0u));
    for (unsigned index = 0; index < 4u; ++index)
    {
        CHECK(portable_time_trial_ghost_encoder_append(&encoder, 0x8000u));
        for (unsigned byte = 0; byte < 15u; ++byte)
            printf("%02x", payload[byte]);
        putchar('\n');
    }
    return 0;
}

static int parse_size(const char *text, size_t limit, size_t *out)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);

    if (end == text || *end != '\0' || value > limit)
        return 0;
    *out = (size_t)value;
    return 1;
}

static int print_decoder_state_rows(int argc, char **argv)
{
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    PortableTimeTrialGhostDecoder decoder;
    size_t cursor;
    size_t remaining;
    size_t count;
    FILE *file;
    int read_ok;

    if (argc != 6 ||
        !parse_size(argv[3], PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE - 3u,
                    &cursor) ||
        !parse_size(argv[4], UINT32_MAX, &remaining) ||
        !parse_size(argv[5], 1000u, &count) || count == 0u ||
        (file = fopen(argv[2], "rb")) == NULL)
        return 2;
    read_ok = fread(record, 1u, sizeof record, file) == sizeof record &&
              fgetc(file) == EOF;
    if (fclose(file) != 0)
        read_ok = 0;
    if (!read_ok ||
        !portable_time_trial_ghost_decoder_init(
            &decoder, record + PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
            PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE))
        return 2;
    decoder.cursor = cursor;
    decoder.remaining = (uint32_t)remaining;
    for (size_t index = 0u; index < count; ++index)
    {
        const size_t before_cursor = decoder.cursor;
        const uint32_t before_remaining = decoder.remaining;
        uint16_t value = 0u;
        const int available = portable_time_trial_ghost_decoder_next(
            &decoder, &value);

        printf("%zu %u %d %u %zu %u\n", before_cursor,
               (unsigned)before_remaining, available, (unsigned)value,
               decoder.cursor, (unsigned)decoder.remaining);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--capture-prefixes") == 0)
        return print_capture_prefixes();
    if (argc >= 2 && strcmp(argv[1], "--decoder-state-rows") == 0)
        return print_decoder_state_rows(argc, argv);
    CHECK(argc == 1);
    CHECK(test_source_capture() == 0);
    CHECK(test_round_trip_and_run_split() == 0);
    CHECK(test_capacity_and_malformed() == 0);
    CHECK(test_record_policy() == 0);
    puts("portable time trial ghost: PASS");
    return 0;
}
