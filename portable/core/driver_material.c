#include "driver_material.h"

#include <stdint.h>

static int32_t world_floor_q16(int32_t value)
{
    const int64_t widened = value;

    if (widened >= 0)
        return (int32_t)(widened / INT64_C(0x10000));
    return (int32_t)(-((-widened + INT64_C(0xffff)) / INT64_C(0x10000)));
}

static int material_effect_candidate(uint8_t flags)
{
    return ((flags & UINT8_C(0x90)) == UINT8_C(0x80)) ||
           ((flags & UINT8_C(0x20)) != 0);
}

int portable_driver_material_sample(
    const PortableTrackMaterialView *material,
    const PortableTrackRaster *raster,
    const PortableTerrainView *terrain,
    int32_t position_x_q16,
    int32_t position_y_q16,
    PortableDriverMaterialState *out)
{
    int32_t world_x;
    int32_t world_y;
    int64_t max_x;
    int64_t max_y;
    uint8_t flags;

    if (material == NULL || raster == NULL || terrain == NULL || out == NULL ||
        material->flags == NULL || raster->cells == NULL ||
        terrain->cells == NULL || terrain->width == 0 ||
        terrain->height == 0)
        return PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT;

    world_x = world_floor_q16(position_x_q16);
    world_y = world_floor_q16(position_y_q16);
    max_x = (int64_t)terrain->width * 8 * 0x10000;
    max_y = (int64_t)terrain->height * 8 * 0x10000;

    *out = (PortableDriverMaterialState){
        .position_x_q16 = position_x_q16,
        .position_y_q16 = position_y_q16,
        .world_x = world_x,
        .world_y = world_y,
    };

    /* sub_8032C50 bounds field_2c/field_30 are 16-unit dimensions. */
    if (position_x_q16 < 0 || position_y_q16 < 0 ||
        (int64_t)position_x_q16 >= max_x ||
        (int64_t)position_y_q16 >= max_y)
    {
        out->material_flags = UINT8_C(0x26);
        out->material_flags_valid = 1;
        out->material_effect_candidate = 1;
        out->terrain_query_status = PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS;
        out->track_query_status = PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS;
        return PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS;
    }
    out->in_bounds = 1;

    out->terrain_query_status = portable_terrain_query8(
        terrain, world_x, world_y, &out->terrain_index);
    if (out->terrain_query_status != PORTABLE_TERRAIN_QUERY_OK)
        return PORTABLE_DRIVER_MATERIAL_TERRAIN_QUERY_ERROR;
    out->terrain_index_valid = 1;

    if (portable_track_material_query(material, out->terrain_index, &flags) !=
        0)
        return PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT;
    out->material_flags = flags;
    out->material_flags_valid = 1;
    out->material_effect_candidate =
        (uint8_t)material_effect_candidate(flags);

    out->track_query_status = portable_track_raster_query16(
        raster, world_x, world_y, &out->track_cell);
    if (out->track_query_status != PORTABLE_TRACK_RASTER_OK)
        return PORTABLE_DRIVER_MATERIAL_TRACK_QUERY_ERROR;
    out->track_cell_valid = 1;
    out->off_track =
        (uint8_t)((out->track_cell & UINT8_C(0x7f)) == UINT8_C(0x7f));
    return PORTABLE_DRIVER_MATERIAL_OK;
}

int portable_driver_wall_material_contact(
    const PortableDriverMaterialState *sample, const PortableTerrainView *terrain,
    uint8_t *owned_cells, size_t capacity, uint8_t replacement_tile,
    uint16_t invulnerability_timer, uint8_t *reflect, size_t *changed_cell)
{
    if (sample == NULL || terrain == NULL || reflect == NULL || changed_cell == NULL)
        return PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT;
    *reflect = 1u;
    *changed_cell = SIZE_MAX;
    if (sample->material_flags < 0x82u || sample->material_flags > 0x8fu)
        return PORTABLE_DRIVER_MATERIAL_OK;
    /* B2B4 calls B110 only after the finite-map and height gates. C division
     * retains the source truncation toward zero for the world/tile conversion. */
    if (!sample->in_bounds || sample->world_x < 0 || sample->world_y < 0 ||
        owned_cells == NULL || terrain->cells != owned_cells ||
        terrain->row_stride < terrain->width)
        return PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT;
    const size_t x = (size_t)(sample->world_x / 8);
    const size_t y = (size_t)(sample->world_y / 8);
    if (x >= terrain->width || y >= terrain->height ||
        y > (SIZE_MAX - x) / terrain->row_stride ||
        y * terrain->row_stride + x >= capacity)
        return PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS;
    *changed_cell = y * terrain->row_stride + x;
    owned_cells[*changed_cell] = replacement_tile;
    if (sample->material_flags == 0x84u && invulnerability_timer != 0u)
        *reflect = 0u;
    return PORTABLE_DRIVER_MATERIAL_OK;
}
