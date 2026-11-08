#ifndef MKSC_PORTABLE_TERRAIN_EFFECT_TABLES_H
#define MKSC_PORTABLE_TERRAIN_EFFECT_TABLES_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_TERRAIN_EFFECT_TABLES_OK = 0,
    PORTABLE_TERRAIN_EFFECT_TABLES_INVALID_ARGUMENT = 1,
    PORTABLE_TERRAIN_EFFECT_TABLES_OUT_OF_BOUNDS = 2,
    PORTABLE_TERRAIN_EFFECT_TABLES_CAPACITY = 3,
};

enum
{
    /* ROM tables at 0x080EDA74 and 0x080EDA94. */
    PORTABLE_TERRAIN_EFFECT_X_OFFSET = 0x0eda74,
    PORTABLE_TERRAIN_EFFECT_Y_OFFSET = 0x0eda94,
    PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT = 8,
    PORTABLE_TERRAIN_EFFECT_OFFSET_BYTES =
        PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT * 4,
};

typedef struct
{
    int32_t *x_offsets;
    size_t x_capacity;
    int32_t *y_offsets;
    size_t y_capacity;
} PortableTerrainEffectTableBuffers;

typedef struct
{
    const int32_t *x_offsets;
    size_t x_count;
    const int32_t *y_offsets;
    size_t y_count;
} PortableTerrainEffectTableView;

/* Load sub_8044920's signed neighbor offsets from the exact local ROM. */
int portable_terrain_effect_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableTerrainEffectTableBuffers *buffers,
    PortableTerrainEffectTableView *view);

#endif
