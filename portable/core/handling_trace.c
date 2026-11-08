#include "handling_trace.h"

static PortableHandlingTraceResult set_difference(
    PortableHandlingTraceDifference *difference, size_t sample_index,
    uint32_t frame, PortableHandlingTraceField field, uint64_t expected_value,
    uint64_t actual_value)
{
    difference->sample_index = sample_index;
    difference->frame = frame;
    difference->field = field;
    difference->expected_value = expected_value;
    difference->actual_value = actual_value;
    return PORTABLE_HANDLING_TRACE_DIFFERENT;
}

PortableHandlingTraceResult portable_handling_trace_compare(
    const PortableHandlingTraceSample *expected, size_t expected_count,
    const PortableHandlingTraceSample *actual, size_t actual_count,
    PortableHandlingTraceDifference *difference)
{
    size_t shared_count;

    if (difference == NULL || (expected_count != 0u && expected == NULL) ||
        (actual_count != 0u && actual == NULL))
        return PORTABLE_HANDLING_TRACE_INVALID;

    *difference = (PortableHandlingTraceDifference){0};
    shared_count = expected_count < actual_count ? expected_count : actual_count;
    for (size_t index = 0; index < shared_count; ++index)
    {
        const PortableHandlingTraceSample *wanted = &expected[index];
        const PortableHandlingTraceSample *observed = &actual[index];
        const uint32_t frame = wanted->frame;

        if (wanted->frame != observed->frame)
            return set_difference(difference, index, frame,
                                  PORTABLE_HANDLING_TRACE_FIELD_FRAME,
                                  wanted->frame, observed->frame);
        if (wanted->control_word != observed->control_word)
            return set_difference(difference, index, frame,
                                  PORTABLE_HANDLING_TRACE_FIELD_CONTROL_WORD,
                                  wanted->control_word, observed->control_word);
        if (wanted->position_x != observed->position_x)
            return set_difference(difference, index, frame,
                                  PORTABLE_HANDLING_TRACE_FIELD_POSITION_X,
                                  (uint32_t)wanted->position_x,
                                  (uint32_t)observed->position_x);
        if (wanted->position_y != observed->position_y)
            return set_difference(difference, index, frame,
                                  PORTABLE_HANDLING_TRACE_FIELD_POSITION_Y,
                                  (uint32_t)wanted->position_y,
                                  (uint32_t)observed->position_y);
        if (wanted->speed_q16 != observed->speed_q16)
            return set_difference(difference, index, frame,
                                  PORTABLE_HANDLING_TRACE_FIELD_SPEED_Q16,
                                  (uint32_t)wanted->speed_q16,
                                  (uint32_t)observed->speed_q16);
        if (wanted->angle != observed->angle)
            return set_difference(difference, index, frame,
                                  PORTABLE_HANDLING_TRACE_FIELD_ANGLE,
                                  (uint16_t)wanted->angle,
                                  (uint16_t)observed->angle);
        if (wanted->acceleration_q16 != observed->acceleration_q16)
            return set_difference(
                difference, index, frame,
                PORTABLE_HANDLING_TRACE_FIELD_ACCELERATION_Q16,
                (uint32_t)wanted->acceleration_q16,
                (uint32_t)observed->acceleration_q16);
    }

    if (expected_count != actual_count)
    {
        const uint32_t frame = shared_count < expected_count
            ? expected[shared_count].frame
            : actual[shared_count].frame;
        return set_difference(difference, shared_count, frame,
                              PORTABLE_HANDLING_TRACE_FIELD_SAMPLE_COUNT,
                              expected_count, actual_count);
    }
    return PORTABLE_HANDLING_TRACE_MATCH;
}

const char *portable_handling_trace_field_name(PortableHandlingTraceField field)
{
    switch (field)
    {
    case PORTABLE_HANDLING_TRACE_FIELD_NONE:
        return "none";
    case PORTABLE_HANDLING_TRACE_FIELD_FRAME:
        return "frame";
    case PORTABLE_HANDLING_TRACE_FIELD_CONTROL_WORD:
        return "control_word";
    case PORTABLE_HANDLING_TRACE_FIELD_POSITION_X:
        return "position_x";
    case PORTABLE_HANDLING_TRACE_FIELD_POSITION_Y:
        return "position_y";
    case PORTABLE_HANDLING_TRACE_FIELD_SPEED_Q16:
        return "speed_q16";
    case PORTABLE_HANDLING_TRACE_FIELD_ANGLE:
        return "angle";
    case PORTABLE_HANDLING_TRACE_FIELD_ACCELERATION_Q16:
        return "acceleration_q16";
    case PORTABLE_HANDLING_TRACE_FIELD_SAMPLE_COUNT:
        return "sample_count";
    }
    return "invalid";
}
