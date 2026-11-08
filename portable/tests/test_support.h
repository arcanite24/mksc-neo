#ifndef PORTABLE_TEST_SUPPORT_H
#define PORTABLE_TEST_SUPPORT_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "collision.h"
#include "camera_state.h"
#include "camera_projection.h"
#include "camera_affine.h"
#include "track_asset.h"
#include "track_object_asset.h"
#include "track_object_art.h"
#include "track_object_decoder.h"
#include "track_raster.h"
#include "driver_tick.h"
#include "driver_pose.h"
#include "driver_input.h"
#include "driver_acceleration.h"
#include "driver_forward_acceleration.h"
#include "driver_b550_resultant.h"
#include "driver_pilot.h"
#include "driver_collision.h"
#include "driver_material.h"
#include "driver_track_response.h"
#include "driver_terrain_effect.h"
#include "driver_vector_effect.h"
#include "driver_angle_target.h"
#include "driver_angle_callbacks.h"
#include "driver_angle_composition.h"
#include "driver_track_state.h"
#include "terrain_query.h"
#include "kart_motion.h"
#include "track_geometry.h"
#include "track_y.h"
#include "terrain_asset.h"
#include "sky_asset.h"
#include "character_asset.h"
#include "track_tileset_asset.h"
#include "road_palette_asset.h"
#include "source_motion_tables.h"
#include "source_normal_response_table.h"
#include "source_vector_tables.h"
#include "source_b550_tables.h"
#include "terrain_effect_tables.h"
#include "road_map_asset.h"
#include "minimap_asset.h"
#include "hud_asset.h"
#include "sky_renderer.h"
#include "character_renderer.h"
#include "track_object_renderer.h"
#include "road_renderer.h"
#include "hud_renderer.h"
#include "input_script.h"

#include "driver_response_fixture.h"

static inline uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static inline size_t build_literal_lz77(uint8_t *destination,
                                 size_t capacity,
                                 size_t output_size,
                                 uint8_t value)
{
    size_t source = 4;
    size_t produced = 0;

    if (destination == NULL || capacity < 4 || output_size == 0 ||
        output_size > 0xffffffu)
        return 0;
    destination[0] = 0x10;
    destination[1] = (uint8_t)output_size;
    destination[2] = (uint8_t)(output_size >> 8);
    destination[3] = (uint8_t)(output_size >> 16);
    while (produced < output_size)
    {
        unsigned bit;

        if (source >= capacity)
            return 0;
        destination[source++] = 0;
        for (bit = 0; bit < 8 && produced < output_size; ++bit)
        {
            if (source >= capacity)
                return 0;
            destination[source++] = value;
            ++produced;
        }
    }
    return source;
}

int run_smoke(void);
int run_track_y(void);
int run_track_import(void);
int run_track_object_asset(void);
int run_track_object_decoder(void);
int run_track_raster(void);
int run_driver_tick(void);
int run_driver_pose(void);
int run_kart_motion(void);
int run_driver_input(void);
int run_input_script(void);
int run_driver_acceleration(void);
int run_driver_forward_acceleration(void);
int run_driver_pilot(void);
int run_driver_collision(void);
int run_terrain_query(void);
int run_terrain_asset(void);
int run_track_object_art(void);
int run_sky_asset(void);
int run_sky_motion(void);
int run_ambient(void);
int run_character_view(void);
int run_finish_camera(void);
int run_snow_weather(void);
int run_course_prop_random(void);
int run_character_asset(void);
int run_track_tileset(void);
int run_road_map(void);
int run_road_palette(void);
int run_source_motion_tables(void);
int run_source_b550_tables(void);
int run_road_renderer(void);
int run_minimap_asset(void);
int run_hud_renderer(void);
int run_driver_track_state(void);
int run_driver_material(void);
int run_driver_track_response(void);
int run_driver_terrain_effect(void);
int run_driver_vector_effect(void);
int run_driver_angle_target(void);
int run_driver_angle_callbacks(void);
int run_driver_angle_composition(void);
int run_terrain_effect_tables(void);
int run_collision(void);
int run_camera(void);
int run_camera_projection(void);
int run_camera_affine(void);

#endif
