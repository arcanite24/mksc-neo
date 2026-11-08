#include "handling_trace.h"

#include <stdio.h>
#include <string.h>

static const PortableHandlingTraceSample reference[] = {
    {1u, UINT16_C(0x8000), INT32_C(0x03780000), INT32_C(0x02640000),
     0, 0, INT32_C(0x00090000)},
    {2u, UINT16_C(0x4000), INT32_C(0x03780000), INT32_C(0x02640000),
     INT32_C(0x00090000), 0, -INT32_C(0x00040000)},
};

static int expect_field(PortableHandlingTraceField expected_field,
                        PortableHandlingTraceSample changed)
{
    PortableHandlingTraceSample actual[2] = {reference[0], changed};
    PortableHandlingTraceDifference difference;

    return portable_handling_trace_compare(reference, 2u, actual, 2u,
                                            &difference) ==
               PORTABLE_HANDLING_TRACE_DIFFERENT &&
           difference.sample_index == 1u && difference.frame == 2u &&
           difference.field == expected_field;
}

static int run_tests(void)
{
    PortableHandlingTraceSample changed;
    PortableHandlingTraceSample extra[3] = {reference[0], reference[1],
                                            reference[1]};
    PortableHandlingTraceDifference difference;

    if (portable_handling_trace_compare(reference, 2u, reference, 2u,
                                        &difference) !=
        PORTABLE_HANDLING_TRACE_MATCH)
        return 0;

    changed = reference[1];
    ++changed.frame;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_FRAME, changed))
        return 0;
    changed = reference[1];
    changed.control_word ^= 1u;
    changed.position_x ^= 1;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_CONTROL_WORD, changed))
        return 0;
    changed = reference[1];
    changed.position_x ^= 1;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_POSITION_X, changed))
        return 0;
    changed = reference[1];
    changed.position_y ^= 1;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_POSITION_Y, changed))
        return 0;
    changed = reference[1];
    changed.speed_q16 ^= 1;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_SPEED_Q16, changed))
        return 0;
    changed = reference[1];
    changed.angle ^= 1;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_ANGLE, changed))
        return 0;
    changed = reference[1];
    changed.acceleration_q16 ^= 1;
    if (!expect_field(PORTABLE_HANDLING_TRACE_FIELD_ACCELERATION_Q16, changed))
        return 0;

    if (portable_handling_trace_compare(reference, 2u, reference, 1u,
                                        &difference) !=
            PORTABLE_HANDLING_TRACE_DIFFERENT ||
        difference.sample_index != 1u || difference.frame != 2u ||
        difference.field != PORTABLE_HANDLING_TRACE_FIELD_SAMPLE_COUNT ||
        difference.expected_value != 2u || difference.actual_value != 1u)
        return 0;
    extra[2].frame = 3u;
    if (portable_handling_trace_compare(reference, 2u, extra, 3u,
                                        &difference) !=
            PORTABLE_HANDLING_TRACE_DIFFERENT ||
        difference.sample_index != 2u || difference.frame != 3u ||
        difference.field != PORTABLE_HANDLING_TRACE_FIELD_SAMPLE_COUNT ||
        difference.expected_value != 2u || difference.actual_value != 3u)
        return 0;

    if (strcmp(portable_handling_trace_field_name(
                   PORTABLE_HANDLING_TRACE_FIELD_ACCELERATION_Q16),
               "acceleration_q16") != 0)
        return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--intentional-mismatch") == 0)
    {
        PortableHandlingTraceSample changed[2] = {reference[0], reference[1]};
        PortableHandlingTraceDifference difference;

        changed[1].speed_q16 ^= 1;
        if (portable_handling_trace_compare(reference, 2u, changed, 2u,
                                            &difference) ==
            PORTABLE_HANDLING_TRACE_DIFFERENT)
        {
            fprintf(stderr, "handling trace mismatch frame %u field %s\n",
                    difference.frame,
                    portable_handling_trace_field_name(difference.field));
            return 1;
        }
        return 0;
    }
    if (argc != 1 || !run_tests())
    {
        fputs("portable handling trace: FAIL\n", stderr);
        return 1;
    }
    puts("portable handling trace: PASS");
    return 0;
}
