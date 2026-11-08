#ifndef MKSC_PORTABLE_SOURCE_B550_TABLES_H
#define MKSC_PORTABLE_SOURCE_B550_TABLES_H

#include <stddef.h>
#include <stdint.h>

enum
{
    /* _080EC780 and _080EC788 in the exact ROM. */
    PORTABLE_SOURCE_B550_X_OFFSET = 0x0ec780,
    PORTABLE_SOURCE_B550_Y_OFFSET = 0x0ec788,
    PORTABLE_SOURCE_B550_COUNT = 4,
    PORTABLE_SOURCE_B550_BYTES = PORTABLE_SOURCE_B550_COUNT * 2,
};

enum
{
    PORTABLE_SOURCE_B550_TABLES_OK = 0,
    PORTABLE_SOURCE_B550_TABLES_INVALID_ARGUMENT = 1,
    PORTABLE_SOURCE_B550_TABLES_OUT_OF_BOUNDS = 2,
    PORTABLE_SOURCE_B550_TABLES_CAPACITY = 3,
};

typedef struct
{
    int16_t *x_components;
    size_t x_capacity;
    int16_t *y_components;
    size_t y_capacity;
} PortableSourceB550TableBuffers;

typedef struct
{
    const int16_t *x_components;
    size_t x_count;
    const int16_t *y_components;
    size_t y_count;
} PortableSourceB550TableView;

/* Load sub_803B550's normal low-speed component tables. */
int portable_source_b550_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceB550TableBuffers *buffers,
    PortableSourceB550TableView *view);

#endif
