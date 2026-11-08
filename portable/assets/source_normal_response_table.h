#ifndef MKSC_PORTABLE_SOURCE_NORMAL_RESPONSE_TABLE_H
#define MKSC_PORTABLE_SOURCE_NORMAL_RESPONSE_TABLE_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK = 0,
    PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_INVALID_ARGUMENT = 1,
    PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OUT_OF_BOUNDS = 2,
    PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_CAPACITY = 3,
};

enum
{
    /* _080EC248, consumed by sub_803DAEC and the case-11 response path. */
    PORTABLE_SOURCE_NORMAL_RESPONSE_OFFSET = 0x0ec248,
    PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT = 8,
    PORTABLE_SOURCE_NORMAL_RESPONSE_BYTES =
        PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT * sizeof(int32_t),
};

typedef struct
{
    int32_t *entries;
    size_t capacity;
} PortableSourceNormalResponseTableBuffers;

typedef struct
{
    const int32_t *entries;
    size_t count;
} PortableSourceNormalResponseTableView;

typedef struct
{
    uint8_t next_status;
    uint8_t timer_cleared;
    uint8_t output_written;
    uint8_t table_index;
    int32_t output_q16;
} PortableSourceNormalResponseResult;

/* Load the exact source _080EC248 table into caller-owned storage. */
int portable_source_normal_response_table_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceNormalResponseTableBuffers *buffers,
    PortableSourceNormalResponseTableView *view);

/* Reproduce sub_803DAEC's status-11 velocity/table producer. */
int portable_source_normal_response_produce(
    uint8_t status,
    int16_t velocity,
    const PortableSourceNormalResponseTableView *table,
    PortableSourceNormalResponseResult *out);

#endif
