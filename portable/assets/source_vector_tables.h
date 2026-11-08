#ifndef MKSC_PORTABLE_SOURCE_VECTOR_TABLES_H
#define MKSC_PORTABLE_SOURCE_VECTOR_TABLES_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_SOURCE_VECTOR_TABLES_OK = 0,
    PORTABLE_SOURCE_VECTOR_TABLES_INVALID_ARGUMENT = 1,
    PORTABLE_SOURCE_VECTOR_TABLES_OUT_OF_BOUNDS = 2,
    PORTABLE_SOURCE_VECTOR_TABLES_CAPACITY = 3,
};

enum
{
    /* _080EC770 and _080EC778 in the exact ROM. */
    PORTABLE_SOURCE_VECTOR_X_OFFSET = 0x0ec770,
    PORTABLE_SOURCE_VECTOR_Y_OFFSET = 0x0ec778,
    PORTABLE_SOURCE_VECTOR_COUNT = 4,
    PORTABLE_SOURCE_VECTOR_BYTES = PORTABLE_SOURCE_VECTOR_COUNT * 2,
};

typedef struct
{
    int16_t *x_components;
    size_t x_capacity;
    int16_t *y_components;
    size_t y_capacity;
} PortableSourceVectorTableBuffers;

typedef struct
{
    const int16_t *x_components;
    size_t x_count;
    const int16_t *y_components;
    size_t y_count;
} PortableSourceVectorTableView;

/* Load sub_803B550's alternate direct component-write tables. */
int portable_source_vector_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceVectorTableBuffers *buffers,
    PortableSourceVectorTableView *view);

#endif
