#include "source_motion_tables.h"

#include <stdint.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int32_t signed32_from_bits(uint32_t bits)
{
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

static int16_t signed16_from_bits(uint16_t bits)
{
    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)(-((int32_t)(UINT16_C(0x10000) - bits)));
}

static int source_range_valid(size_t rom_size,
                              size_t offset,
                              size_t byte_count)
{
    return offset <= rom_size && byte_count <= rom_size - offset;
}

static int32_t source_arithmetic_shift_right_one(int32_t value)
{
    if (value >= 0)
        return value / 2;
    return -(((-value) + 1) / 2);
}

int portable_source_motion_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceMotionTableBuffers *buffers,
    PortableSourceMotionTableView *view)
{
    size_t index;

    if (rom == NULL || buffers == NULL || view == NULL ||
        buffers->sine == NULL || buffers->auto_acceleration == NULL ||
        buffers->brake == NULL || buffers->brake_accelerate == NULL ||
        buffers->forward_response == NULL || buffers->steering_delta == NULL)
        return PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT;
    if (buffers->sine_capacity < PORTABLE_SOURCE_SINE_COUNT ||
        buffers->auto_acceleration_capacity <
            PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT ||
        buffers->brake_capacity < PORTABLE_SOURCE_BRAKE_COUNT ||
        buffers->brake_accelerate_capacity <
            PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT ||
        buffers->forward_response_capacity <
            PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT ||
        buffers->steering_delta_capacity <
            PORTABLE_SOURCE_STEERING_DELTA_COUNT)
        return PORTABLE_SOURCE_MOTION_TABLES_CAPACITY;
    if (!source_range_valid(rom_size, PORTABLE_SOURCE_SINE_OFFSET,
                            PORTABLE_SOURCE_SINE_BYTES) ||
        !source_range_valid(rom_size,
                            PORTABLE_SOURCE_AUTO_ACCELERATION_OFFSET,
                            PORTABLE_SOURCE_AUTO_ACCELERATION_BYTES) ||
        !source_range_valid(rom_size, PORTABLE_SOURCE_BRAKE_OFFSET,
                            PORTABLE_SOURCE_BRAKE_BYTES) ||
        !source_range_valid(rom_size, PORTABLE_SOURCE_BRAKE_ACCELERATE_OFFSET,
                            PORTABLE_SOURCE_BRAKE_ACCELERATE_BYTES) ||
        !source_range_valid(rom_size,
                            PORTABLE_SOURCE_FORWARD_RESPONSE_OFFSET,
                            PORTABLE_SOURCE_FORWARD_RESPONSE_BYTES) ||
        !source_range_valid(rom_size, PORTABLE_SOURCE_STEERING_DELTA_OFFSET,
                            PORTABLE_SOURCE_STEERING_DELTA_BYTES))
        return PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS;

    for (index = 0; index < PORTABLE_SOURCE_SINE_COUNT; ++index)
    {
        buffers->sine[index] = signed32_from_bits(read_le32(
            rom + PORTABLE_SOURCE_SINE_OFFSET + index * 4u));
    }
    for (index = 0; index < PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT;
         ++index)
    {
        buffers->auto_acceleration[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_AUTO_ACCELERATION_OFFSET + index * 2u));
    }
    for (index = 0; index < PORTABLE_SOURCE_BRAKE_COUNT; ++index)
    {
        buffers->brake[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_BRAKE_OFFSET + index * 2u));
        buffers->brake_accelerate[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_BRAKE_ACCELERATE_OFFSET + index * 2u));
    }
    for (index = 0; index < PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT; ++index)
    {
        buffers->forward_response[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_FORWARD_RESPONSE_OFFSET + index * 2u));
    }
    for (index = 0; index < PORTABLE_SOURCE_STEERING_DELTA_COUNT; ++index)
    {
        buffers->steering_delta[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_STEERING_DELTA_OFFSET + index * 2u));
    }

    view->sine = buffers->sine;
    view->sine_count = PORTABLE_SOURCE_SINE_COUNT;
    view->auto_acceleration = buffers->auto_acceleration;
    view->auto_acceleration_count = PORTABLE_SOURCE_AUTO_ACCELERATION_COUNT;
    view->brake = buffers->brake;
    view->brake_count = PORTABLE_SOURCE_BRAKE_COUNT;
    view->brake_accelerate = buffers->brake_accelerate;
    view->brake_accelerate_count = PORTABLE_SOURCE_BRAKE_ACCELERATE_COUNT;
    view->forward_response = buffers->forward_response;
    view->forward_response_count = PORTABLE_SOURCE_FORWARD_RESPONSE_COUNT;
    view->steering_delta = buffers->steering_delta;
    view->steering_delta_count = PORTABLE_SOURCE_STEERING_DELTA_COUNT;
    return PORTABLE_SOURCE_MOTION_TABLES_OK;
}

int portable_source_local_table_build(
    const uint8_t *rom,
    size_t rom_size,
    uint8_t character_class,
    uint8_t selector,
    int16_t *destination,
    size_t destination_capacity)
{
    size_t index;
    const size_t class_index = character_class & 7u;

    if (rom == NULL || destination == NULL)
        return PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT;
    if (destination_capacity < PORTABLE_SOURCE_LOCAL_TABLE_COUNT)
        return PORTABLE_SOURCE_MOTION_TABLES_CAPACITY;
    if (!source_range_valid(rom_size, PORTABLE_SOURCE_LOCAL_TABLE_OFFSET,
                            PORTABLE_SOURCE_LOCAL_TABLE_BYTES))
        return PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS;

    for (index = 0; index < PORTABLE_SOURCE_LOCAL_TABLE_COUNT; ++index)
    {
        const size_t offset = PORTABLE_SOURCE_LOCAL_TABLE_OFFSET +
                              class_index *
                                  PORTABLE_SOURCE_LOCAL_TABLE_CLASS_STRIDE +
                              index * sizeof(uint16_t);
        const int32_t source_value = signed16_from_bits(read_le16(rom + offset));
        int32_t value = source_value * 16;

        if (selector > 1u)
        {
            value += source_arithmetic_shift_right_one(value);
            if (value <= 0x31)
                value = 0xe0;
        }
        destination[index] = (int16_t)value;
    }
    return PORTABLE_SOURCE_MOTION_TABLES_OK;
}

int portable_source_forward_thresholds_build(
    const uint8_t *rom, size_t rom_size, uint8_t character_class,
    uint8_t race_class, int16_t *destination, size_t destination_capacity)
{
    if(rom==NULL||destination==NULL)
        return PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT;
    if(destination_capacity<PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT)
        return PORTABLE_SOURCE_MOTION_TABLES_CAPACITY;
    if(!source_range_valid(rom_size,PORTABLE_SOURCE_FORWARD_THRESHOLD_OFFSET,
                           PORTABLE_SOURCE_FORWARD_THRESHOLD_BYTES))
        return PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS;
    for (size_t index = 0; index < 10; index++)
        destination[index] = -1;
    for(size_t index=10;index<PORTABLE_SOURCE_FORWARD_THRESHOLD_COUNT;index++){
        size_t source=(character_class&7u)*
            PORTABLE_SOURCE_FORWARD_THRESHOLD_SOURCE_COUNT+(index-10u);
        int32_t value=signed16_from_bits(read_le16(
            rom+PORTABLE_SOURCE_FORWARD_THRESHOLD_OFFSET+source*2u))*16;
        if(race_class==0)value-=0x80;
        else if(race_class==2||race_class==3)value+=0xa0;
        destination[index]=(int16_t)value;
    }
    return PORTABLE_SOURCE_MOTION_TABLES_OK;
}

int portable_source_steering_response_build(
    const uint8_t *rom,
    size_t rom_size,
    uint8_t character_class,
    uint8_t response_slot,
    int16_t *destination,
    size_t destination_capacity)
{
    size_t index;
    const size_t class_index = character_class & 7u;

    if (rom == NULL || destination == NULL)
        return PORTABLE_SOURCE_MOTION_TABLES_INVALID_ARGUMENT;
    if (destination_capacity < PORTABLE_SOURCE_STEERING_RESPONSE_COUNT ||
        response_slot >= PORTABLE_SOURCE_STEERING_RESPONSE_SLOT_COUNT)
        return PORTABLE_SOURCE_MOTION_TABLES_CAPACITY;
    if (!source_range_valid(rom_size,
                            PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET,
                            PORTABLE_SOURCE_STEERING_RESPONSE_BYTES))
        return PORTABLE_SOURCE_MOTION_TABLES_OUT_OF_BOUNDS;

    for (index = 0; index < PORTABLE_SOURCE_STEERING_RESPONSE_COUNT;
         ++index)
    {
        const size_t offset = PORTABLE_SOURCE_STEERING_RESPONSE_OFFSET +
                              class_index *
                                  PORTABLE_SOURCE_STEERING_RESPONSE_CLASS_STRIDE +
                              (size_t)response_slot * 8u + index * 2u;

        destination[index] = signed16_from_bits(read_le16(rom + offset));
    }
    return PORTABLE_SOURCE_MOTION_TABLES_OK;
}
