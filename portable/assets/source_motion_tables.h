#ifndef MKSC_PORTABLE_SOURCE_MOTION_TABLES_H
#define MKSC_PORTABLE_SOURCE_MOTION_TABLES_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_SOURCE_MOTION_TABLES_OK = 0,
    PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT = 1,
    PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS = 2,
    PORTABLE_SOURCE_MOTION_TABLES_CAPACITY = 3,
};

enum
{
    PORTABLE_SOURCE_SINE_OFFSET = 0x0e645c,
    PORTABLE_SOURCE_SINE_COUNT = 1024,
    PORTABLE_SOURCE_SINE_BYTES = PORTABLE_SOURCE_SINE_COUNT * 4,
    PORTABLE_SOURCE_AUTO_ACCELERATION_OFFSET = 0x0ebec8,
    PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT = 64,
    PORTABLE_SOURCE_AUTO_ACCELERATION_BYTES =
        PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT * 2,
    PORTABLE_SOURCE_BRAKE_OFFSET = 0x0ebea8,
    PORTABLE_SOURCE_BRAKE_COUNT = 8,
    PORTABLE_SOURCE_BRAKE_BYTES = PORTABLE_SOURCE_BRAKE_COUNT * 2,
    PORTABLE_SOURCE_BRAKE_ACCELERATE_OFFSET = 0x0ebeb8,
    PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT = 8,
    PORTABLE_SOURCE_BRAKE_ACCELERATE_BYTES =
        PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT * 2,
    PORTABLE_SOURCE_FORWARD_RESPONSE_OFFSET = 0x0ec1d8,
    PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT = 8,
    PORTABLE_SOURCE_FORWARD_RESPONSE_BYTES =
        PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT * 2,
    PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET = 0x0ec0d8,
    PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_COUNT = 8,
    PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE = 0x20,
    PORTABLE_SOURCE_STEERING_RESPONSE_SLOT_COUNT = 4,
    PORTABLE_SOURCE_STEERING_RESPONSE_COUNT = 4,
    PORTABLE_SOURCE_STEERING_RESPONSE_BYTES =
        PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_COUNT *
        PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE,
    PORTABLE_SOURCE_STEERING_DELTA_OFFSET = 0x0ec71c,
    PORTABLE_SOURCE_STEERING_DELTA_COUNT = 10,
    PORTABLE_SOURCE_STEERING_DELTA_BYTES =
        PORTABLE_SOURCE_STEERING_DELTA_COUNT * 2,
    PORTABLE_SOURCE_LOCAL_TABLE_OFFSET = 0x0ebf78,
    PORTABLE_SOURCE_LOCAL_TABLE_CLASS_COUNT = 8,
    PORTABLE_SOURCE_LOCAL_TABLE_CLASS_STRIDE = 0x20,
    PORTABLE_SOURCE_LOCAL_TABLE_COUNT = 16,
    PORTABLE_SOURCE_LOCAL_TABLE_BYTES =
        PORTABLE_SOURCE_LOCAL_TABLE_CLASS_COUNT *
        PORTABLE_SOURCE_LOCAL_TABLE_CLASS_STRIDE,
    PORTABLE_SOURCE_FORWARD_THRESHOLD_OFFSET = 0x0ec078,
    PORTABLE_SOURCE_FORWARD_THRESHOLD_CLASS_COUNT = 8,
    PORTABLE_SOURCE_FORWARD_THRESHOLD_SOURCE_COUNT = 6,
    PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT = 16,
    PORTABLE_SOURCE_FORWARD_THRESHOLD_BYTES =
        PORTABLE_SOURCE_FORWARD_THRESHOLD_CLASS_COUNT *
        PORTABLE_SOURCE_FORWARD_THRESHOLD_SOURCE_COUNT * 2,
};

typedef struct
{
    int32_t *sine;
    size_t sine_capacity;
    int16_t *auto_acceleration;
    size_t auto_acceleration_capacity;
    int16_t *brake;
    size_t brake_capacity;
    int16_t *brake_accelerate;
    size_t brake_accelerate_capacity;
    int16_t *forward_response;
    size_t forward_response_capacity;
    int16_t *steering_delta;
    size_t steering_delta_capacity;
} PortableSourceMotionTableBuffers;

typedef struct
{
    const int32_t *sine;
    size_t sine_count;
    const int16_t *auto_acceleration;
    size_t auto_acceleration_count;
    const int16_t *brake;
    size_t brake_count;
    const int16_t *brake_accelerate;
    size_t brake_accelerate_count;
    const int16_t *forward_response;
    size_t forward_response_count;
    const int16_t *steering_delta;
    size_t steering_delta_count;
} PortableSourceMotionTableView;

/* Load source-owned ROM tables into caller-owned native storage. */
int portable_source_motion_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceMotionTableBuffers *buffers,
    PortableSourceMotionTableView *view);

/* Reproduce sub_803C1BC's class/selector local response table. */
int portable_source_local_table_build(
    const uint8_t *rom,
    size_t rom_size,
    uint8_t character_class,
    uint8_t selector,
    int16_t *destination,
    size_t destination_capacity);

int portable_source_forward_thresholds_build(
    const uint8_t *rom, size_t rom_size, uint8_t character_class,
    uint8_t race_class, int16_t *destination, size_t destination_capacity);

/* Load the four signed response parameters used by sub_8038AFC. */
int portable_source_steering_response_build(
    const uint8_t *rom,
    size_t rom_size,
    uint8_t character_class,
    uint8_t response_slot,
    int16_t *destination,
    size_t destination_capacity);

#endif
