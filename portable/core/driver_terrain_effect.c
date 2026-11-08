#include "driver_terrain_effect.h"

#include <stdint.h>

static int material_effect_active(uint8_t flags)
{
    return (flags & UINT8_C(0x90)) == UINT8_C(0x80);
}

static int32_t source_effect_delta(int32_t cell_delta)
{
    if (cell_delta < 0)
    {
        const int32_t magnitude = -cell_delta - INT32_C(0xfa);

        return magnitude >= 0 ? magnitude : INT32_C(4);
    }

    if (cell_delta == 1)
        return 0;
    return cell_delta - INT32_C(1) - INT32_C(0xfd);
}

static int32_t negate_wrap32(int32_t value)
{
    return (int32_t)(UINT32_C(0) - (uint32_t)value);
}

static int add_source_offset(int32_t value,
                             int32_t offset,
                             int32_t *out_value)
{
    const int64_t widened = (int64_t)value + offset;

    if (widened < INT32_MIN || widened > INT32_MAX)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;
    *out_value = (int32_t)widened;
    return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;
}

static int query_material_flags(const PortableTrackMaterialView *material,
                                const PortableTerrainView *terrain,
                                int32_t world_x,
                                int32_t world_y,
                                uint8_t *out_flags)
{
    uint8_t terrain_index;

    if (portable_terrain_query8(terrain, world_x, world_y,
                                &terrain_index) != PORTABLE_TERRAIN_QUERY_OK)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;
    if (portable_track_material_query(material, terrain_index, out_flags) != 0)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;
    return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;
}

int portable_driver_terrain_effect_tick(
    const PortableDriverMaterialState *material,
    int32_t height_q16,
    PortableDriverTerrainEffectState *state)
{
    uint16_t x_cell;
    uint16_t y_cell;

    if (material == NULL || state == NULL)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT;

    state->previous_cell_key = state->cell_key;
    state->cell_changed = 0;
    state->effect_active = 0;
    state->effect_dispatch_candidate = 0;
    state->height_gated = 0;
    state->bounds_gated = 0;
    state->cell_delta = 0;
    state->effect_dispatch_delta = 0;

    /* sub_80449EC skips the map lookup when state+8 exceeds 0x4ffff. */
    if (height_q16 > INT32_C(0x0004ffff))
    {
        state->height_gated = 1;
        return PORTABLE_DRIVER_TERRAIN_EFFECT_HEIGHT_LIMIT;
    }
    if (!material->in_bounds)
    {
        state->bounds_gated = 1;
        return PORTABLE_DRIVER_TERRAIN_EFFECT_OUT_OF_BOUNDS;
    }
    if (!material->material_flags_valid)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT;

    /* sub_8032990's key is (world_y >> 3) << 8 | (world_x >> 3). */
    x_cell = (uint16_t)((uint32_t)material->world_x >> 3);
    y_cell = (uint16_t)((uint32_t)material->world_y >> 3);
    state->cell_key = (uint16_t)((uint16_t)(y_cell << 8) | x_cell);
    state->cell_key_valid = 1;
    state->cell_delta = (int32_t)state->cell_key -
                        (int32_t)state->previous_cell_key;
    state->cell_changed = (uint8_t)(state->cell_delta != 0);
    state->effect_active = (uint8_t)material_effect_active(
        material->material_flags);
    state->effect_dispatch_candidate = state->effect_active;
    if (state->effect_active)
        state->effect_dispatch_delta = source_effect_delta(state->cell_delta);
    return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;
}

int portable_driver_terrain_effect_dispatch(
    const PortableDriverMaterialState *material,
    const PortableTrackMaterialView *track_material,
    const PortableTerrainView *terrain,
    const PortableTerrainEffectTableView *tables,
    int32_t mode,
    int32_t *vector_x_q16,
    int32_t *vector_y_q16,
    PortableDriverTerrainEffectDispatch *out)
{
    uint8_t x_flags;
    uint8_t y_flags;
    int32_t x_neighbor;
    int32_t y_neighbor;
    uint8_t orientation;

    if (material == NULL || tables == NULL || vector_x_q16 == NULL ||
        vector_y_q16 == NULL || out == NULL || tables->x_offsets == NULL ||
        tables->y_offsets == NULL || tables->x_count < 8u ||
        tables->y_count < 8u)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT;

    *out = (PortableDriverTerrainEffectDispatch){
        .mode = mode,
    };

    /* sub_8044920 returns success without touching the vector for > 7. */
    if (mode < 0 || mode > 7)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;

    if (mode == 0 || mode == 4)
    {
        *vector_x_q16 = negate_wrap32(*vector_x_q16);
        out->vector_mutated = 1;
        return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;
    }
    if (mode == 2 || mode == 6)
    {
        *vector_y_q16 = negate_wrap32(*vector_y_q16);
        out->vector_mutated = 1;
        return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;
    }

    if (track_material == NULL || terrain == NULL ||
        !material->material_flags_valid)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT;

    if (add_source_offset(material->world_x, tables->x_offsets[mode],
                          &x_neighbor) !=
        PORTABLE_DRIVER_TERRAIN_EFFECT_OK)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;
    y_neighbor = material->world_y;
    if (query_material_flags(track_material, terrain, x_neighbor, y_neighbor,
                             &x_flags) != PORTABLE_DRIVER_TERRAIN_EFFECT_OK)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;

    x_neighbor = material->world_x;
    if (add_source_offset(material->world_y, tables->y_offsets[mode],
                          &y_neighbor) !=
        PORTABLE_DRIVER_TERRAIN_EFFECT_OK)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;
    if (query_material_flags(track_material, terrain, x_neighbor, y_neighbor,
                             &y_flags) != PORTABLE_DRIVER_TERRAIN_EFFECT_OK)
        return PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR;

    orientation = (uint8_t)(((x_flags & UINT8_C(0x80)) != 0 ? 2u : 0u) |
                            ((y_flags & UINT8_C(0x80)) != 0 ? 1u : 0u));
    out->x_neighbor_flags = x_flags;
    out->y_neighbor_flags = y_flags;
    out->orientation = orientation;
    switch (orientation)
    {
    case 0:
    case 3:
        *vector_x_q16 = negate_wrap32(*vector_x_q16);
        *vector_y_q16 = negate_wrap32(*vector_y_q16);
        out->vector_mutated = 1;
        break;
    case 2:
        *vector_y_q16 = negate_wrap32(*vector_y_q16);
        out->vector_mutated = 1;
        break;
    case 1:
        *vector_x_q16 = negate_wrap32(*vector_x_q16);
        out->vector_mutated = 1;
        break;
    default:
        break;
    }
    return PORTABLE_DRIVER_TERRAIN_EFFECT_OK;
}
