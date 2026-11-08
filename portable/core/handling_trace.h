#ifndef PORTABLE_HANDLING_TRACE_H
#define PORTABLE_HANDLING_TRACE_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t frame;
    uint16_t control_word;
    int32_t position_x;
    int32_t position_y;
    int32_t speed_q16;
    int16_t angle;
    int32_t acceleration_q16;
} PortableHandlingTraceSample;

typedef enum
{
    PORTABLE_HANDLING_TRACE_FIELD_NONE = 0,
    PORTABLE_HANDLING_TRACE_FIELD_FRAME,
    PORTABLE_HANDLING_TRACE_FIELD_CONTROL_WORD,
    PORTABLE_HANDLING_TRACE_FIELD_POSITION_X,
    PORTABLE_HANDLING_TRACE_FIELD_POSITION_Y,
    PORTABLE_HANDLING_TRACE_FIELD_SPEED_Q16,
    PORTABLE_HANDLING_TRACE_FIELD_ANGLE,
    PORTABLE_HANDLING_TRACE_FIELD_ACCELERATION_Q16,
    PORTABLE_HANDLING_TRACE_FIELD_SAMPLE_COUNT
} PortableHandlingTraceField;

typedef struct
{
    size_t sample_index;
    uint32_t frame;
    PortableHandlingTraceField field;
    uint64_t expected_value;
    uint64_t actual_value;
} PortableHandlingTraceDifference;

typedef enum
{
    PORTABLE_HANDLING_TRACE_MATCH = 0,
    PORTABLE_HANDLING_TRACE_DIFFERENT,
    PORTABLE_HANDLING_TRACE_INVALID
} PortableHandlingTraceResult;

PortableHandlingTraceResult portable_handling_trace_compare(
    const PortableHandlingTraceSample *expected, size_t expected_count,
    const PortableHandlingTraceSample *actual, size_t actual_count,
    PortableHandlingTraceDifference *difference);

const char *portable_handling_trace_field_name(PortableHandlingTraceField field);

#endif
