#include "test_support.h"

int run_driver_collision(void)
{
    int32_t sine_table[1024] = {0};
    PortableDriverPoseState pose = {
        .position_x = 0,
        .position_y = 0,
        .component_x = 0,
        .component_y = 0,
        .sampled_world_x = 0,
        .sampled_world_y = 0,
        .track_cell = 0,
        .track_cell_valid = 1,
    };
    PortableDriverCollisionState state = {0};
    PortableTrackObjectActorInit imported_object = {
        .kind = PORTABLE_TRACK_OBJECT_ACTOR_INIT_GENERIC_EXTENDED,
        .source_code = UINT32_C(0x000c0002),
        .source_actor_type = UINT16_C(0x1000),
        .size_x = 8,
        .size_y = 8,
        .size_z = 8,
        .source_flags = 1,
        .draw_scale = UINT16_C(0x100),
        .update_kind = PORTABLE_TRACK_OBJECT_UPDATE_8059E00,
        .collision_kind = PORTABLE_TRACK_OBJECT_COLLISION_8059E44,
    };

    if (portable_driver_collision_tick(&pose, &state, 0, 0, 7, 47) !=
            PORTABLE_DRIVER_COLLISION_OK ||
        state.list.active_count != 1 || state.list.passive_count != 1 ||
        state.callback_count != 2 || state.first_other_tag != 7)
        return 1;

    if (portable_driver_collision_tick(&pose, &state, 0x10001, 0, 7, 47) !=
            PORTABLE_DRIVER_COLLISION_OK || state.callback_count != 0)
        return 1;

    pose.position_x = INT32_C(0x03ac0000);
    pose.position_y = INT32_C(0x018c0000);
    if (portable_driver_collision_tick_imported(
            &pose, &state, &imported_object, pose.position_x,
            pose.position_y, 0x2a, 47) != PORTABLE_DRIVER_COLLISION_OK ||
        state.list.active_count != 1 || state.list.passive_count != 1 ||
        state.callback_count != 1 || state.first_other_tag != 0x2a ||
        state.imported_trigger_count != 1 ||
        state.imported_object.source.active != 1 ||
        state.imported_object.source.timer != 0x20 ||
        state.imported_object.source.draw_scale != 0xff00)
        return 1;

    sine_table[0x200] = 3;
    if (portable_driver_collision_update_imported(
            &state, sine_table, sizeof(sine_table) / sizeof(sine_table[0])) !=
            PORTABLE_DRIVER_COLLISION_OK ||
        state.imported_object.pos_z != 144 ||
        state.imported_object.source.timer != 0x1f ||
        state.imported_object.source.flags != 1)
        return 1;
    state.imported_object.source.timer = 0;
    sine_table[0] = -2;
    if (portable_driver_collision_update_imported(
            &state, sine_table, sizeof(sine_table) / sizeof(sine_table[0])) !=
            PORTABLE_DRIVER_COLLISION_OK ||
        state.imported_object.pos_z != -96 ||
        state.imported_object.source.timer != -1 ||
        state.imported_object.source.flags != 0 ||
        portable_driver_collision_update_imported(
            &state, sine_table, sizeof(sine_table) / sizeof(sine_table[0])) !=
            PORTABLE_DRIVER_COLLISION_OK)
        return 1;

    if (portable_driver_collision_update_imported(&state, sine_table, 1023) !=
            PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT ||
        portable_driver_collision_update_imported(NULL, sine_table,
                                                  sizeof(sine_table) /
                                                      sizeof(sine_table[0])) !=
            PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT)
        return 1;

    if (portable_driver_collision_tick(&pose, &state, 0, 0, 7, 0) !=
            PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT ||
        portable_driver_collision_tick(NULL, &state, 0, 0, 7, 47) !=
            PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT)
        return 1;
    imported_object.collision_kind = PORTABLE_TRACK_OBJECT_COLLISION_NONE;
    if (portable_driver_collision_tick_imported(
            &pose, &state, &imported_object, pose.position_x,
            pose.position_y, 0x2a, 47) !=
        PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT)
        return 1;

    puts("portable driver collision: PASS");
    return 0;
}

int run_terrain_query(void)
{
    uint8_t cells[3 * 4] = {
        10, 11, 12, 13,
        20, 21, 22, 23,
        30, 31, 32, 33,
    };
    PortableTerrainView terrain = {
        .cells = cells,
        .row_stride = 4,
        .width = 4,
        .height = 3,
    };
    PortableTerrainView before_error = terrain;
    uint8_t value = 0;

    if (portable_terrain_query8(&terrain, 0, 0, &value) !=
            PORTABLE_TERRAIN_QUERY_OK || value != 10 ||
        portable_terrain_query8(&terrain, 8, 8, &value) !=
            PORTABLE_TERRAIN_QUERY_OK || value != 21 ||
        portable_terrain_query8(&terrain, -1, -1, &value) !=
            PORTABLE_TERRAIN_QUERY_OK || value != 10 ||
        portable_terrain_query8(&terrain, -8, 0, &value) !=
            PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS ||
        portable_terrain_query8(&terrain, 32, 0, &value) !=
            PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS)
        return 1;

    terrain.row_stride = 3;
    if (portable_terrain_query8(&terrain, 24, 0, &value) !=
            PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS)
        return 1;
    {
        uint8_t original_row[128] = {0};
        PortableTerrainView original_shape = {
            .cells = original_row,
            .row_stride = 0,
            .width = 128,
            .height = 128,
        };

        original_row[0x6f] = 0x06;
        if (portable_terrain_query8(&original_shape, 0x378, 0x263,
                                    &value) != PORTABLE_TERRAIN_QUERY_OK ||
            value != 0x06)
            return 1;
    }
    if (portable_terrain_query8(NULL, 0, 0, &value) !=
            PORTABLE_TERRAIN_QUERY_INVALID_ARGUMENT ||
        portable_terrain_query8(&before_error, 0, 0, NULL) !=
            PORTABLE_TERRAIN_QUERY_INVALID_ARGUMENT)
        return 1;

    puts("portable terrain query: PASS");
    return 0;
}

int run_driver_track_state(void)
{
    uint8_t track_data[0x180] = {0};
    uint8_t raster_cells[4 * 4] = {0};
    uint8_t terrain_cells[4 * 4] = {0};
    PortableTrackAsset asset = {
        .data = track_data,
        .size = sizeof(track_data),
    };
    PortableTrackGeometryView geometry;
    PortableTrackRaster raster = {
        .cells = raster_cells,
        .capacity = sizeof(raster_cells),
        .width = 4,
        .height = 4,
    };
    PortableTerrainView terrain = {
        .cells = terrain_cells,
        .row_stride = 4,
        .width = 4,
        .height = 4,
    };
    PortableDriverTrackState state;

    write_le32(track_data + 0xCC, 0x100);
    track_data[4] = 2;
    track_data[0x103] = 0x08;
    track_data[0x104] = 0;
    track_data[0x108] = 0x34;
    track_data[0x109] = 0x12;
    track_data[0x10A] = 0x78;
    track_data[0x10B] = 0x56;
    raster_cells[1 * 4 + 1] = 0x42;
    terrain_cells[2 * 4 + 2] = 0x99;

    if (portable_track_geometry_init(&asset, 2, &geometry) !=
            PORTABLE_TRACK_GEOMETRY_OK ||
        portable_driver_track_state_sample(
            &geometry, &raster, &terrain, 1, 0, 16, 16, &state) !=
            PORTABLE_DRIVER_TRACK_STATE_OK)
        return 1;
    if (state.requested_checkpoint != 1 || state.selected_checkpoint != 0 ||
        state.checkpoint_row != 0 || state.checkpoint.x != 0x1234 ||
        state.checkpoint.y != 0x5678 || state.world_x != 16 ||
        state.world_y != 16 || state.track_cell != 0x42 ||
        state.terrain_cell != 0x99 || !state.track_cell_valid ||
        !state.terrain_cell_valid ||
        state.track_query_status != PORTABLE_TRACK_RASTER_OK ||
        state.terrain_query_status != PORTABLE_TERRAIN_QUERY_OK)
        return 1;

    if (portable_driver_track_state_sample(
            &geometry, &raster, &terrain, 0, 0, 64, 16, &state) !=
            PORTABLE_DRIVER_TRACK_STATE_TRACK_QUERY_ERROR ||
        state.track_cell_valid || state.terrain_cell_valid ||
        state.track_query_status != PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS ||
        state.terrain_query_status != PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS)
        return 1;
    if (portable_driver_track_state_sample(
            NULL, &raster, &terrain, 0, 0, 0, 0, &state) !=
            PORTABLE_DRIVER_TRACK_STATE_INVALID_ARGUMENT ||
        portable_driver_track_state_sample(
            &geometry, &raster, NULL, 0, 0, 0, 0, &state) !=
            PORTABLE_DRIVER_TRACK_STATE_INVALID_ARGUMENT)
        return 1;

    /* Source sentinel picks the preceding row, and heading comes from the
     * raster's target at that row (including its finish-band high bit). */
    const PortableRacePoint recovery_route[3] = {{8, 8}, {24, 8}, {24, 24}};
    const uint16_t angles[1] = {0};
    PortableDriverRecoveryTarget target = {0};
    raster_cells[1] = UINT8_C(0x80);
    if (!portable_driver_recovery_target(recovery_route, 3u, 2u, &raster,
            angles, 1u, &target) || target.checkpoint_index != 1u ||
        target.position.x != 24 || target.position.y != 8 ||
        target.heading != UINT16_C(0xc000))
        return 1;
    const PortableDriverRecoveryTarget saved = target;
    if (portable_driver_recovery_target(recovery_route, 3u, 3u, &raster,
            angles, 1u, &target) ||
        target.checkpoint_index != saved.checkpoint_index ||
        target.position.x != saved.position.x || target.heading != saved.heading)
        return 1;
    raster_cells[1] = 127u;
    if (portable_driver_recovery_target(recovery_route, 3u, 1u, &raster,
            angles, 1u, &target))
        return 1;

    puts("portable driver track state: PASS");
    return 0;
}

int run_driver_material(void)
{
    uint8_t asset_data[0x300] = {0};
    uint8_t raster_cells[4 * 4] = {0};
    uint8_t terrain_cells[4 * 4] = {0};
    PortableTrackAsset asset = {
        .data = asset_data,
        .size = sizeof(asset_data),
    };
    PortableTrackMaterialView material;
    PortableTrackRaster raster = {
        .cells = raster_cells,
        .capacity = sizeof(raster_cells),
        .width = 4,
        .height = 4,
    };
    PortableTerrainView terrain = {
        .cells = terrain_cells,
        .row_stride = 4,
        .width = 4,
        .height = 4,
    };
    PortableDriverMaterialState state;

    write_le32(asset_data + 0x88, 0x200);
    asset_data[0x200 + 0x17] = 0x5a;
    terrain_cells[2 * 4 + 2] = 0x17;
    raster_cells[1 * 4 + 1] = 0x42;
    if (portable_track_material_init(&asset, &material) !=
            PORTABLE_TRACK_MATERIAL_OK ||
        material.source_offset != 0x200 || material.count != 0x100 ||
        portable_track_material_query(&material, 0x17, &state.material_flags) !=
            0 ||
        state.material_flags != 0x5a ||
        portable_driver_material_sample(
            &material, &raster, &terrain, INT32_C(0x00100000),
            INT32_C(0x00100000), &state) != PORTABLE_DRIVER_MATERIAL_OK)
        return 1;
    if (!state.in_bounds || state.world_x != 16 || state.world_y != 16 ||
        state.terrain_index != 0x17 || state.material_flags != 0x5a ||
        state.track_cell != 0x42 || state.off_track ||
        state.material_effect_candidate || !state.terrain_index_valid ||
        !state.material_flags_valid || !state.track_cell_valid)
        return 1;

    raster_cells[1 * 4 + 1] = 0x7f;
    if (portable_driver_material_sample(
            &material, &raster, &terrain, INT32_C(0x00100000),
            INT32_C(0x00100000), &state) != PORTABLE_DRIVER_MATERIAL_OK ||
        !state.off_track)
        return 1;

    if (portable_driver_material_sample(
            &material, &raster, &terrain, INT32_C(0x00200000), 0, &state) !=
            PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS ||
        state.in_bounds || state.material_flags != 0x26 ||
        !state.material_flags_valid || !state.material_effect_candidate ||
        state.terrain_index_valid || state.track_cell_valid)
        return 1;

    if (portable_driver_material_sample(
            NULL, &raster, &terrain, 0, 0, &state) !=
            PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT ||
        portable_track_material_init(NULL, &material) !=
            PORTABLE_TRACK_MATERIAL_INVALID_ARGUMENT ||
        portable_track_material_query(NULL, 0, &state.material_flags) != 1)
        return 1;

    /* All destructible classes mutate exactly the struck 8-unit cell;
     * only 84 suppresses reflection when +6A is nonzero. */
    for (unsigned flags = 0x80u; flags <= 0x90u; ++flags)
    {
        PortableDriverMaterialState wall = {.in_bounds = 1u, .world_x = 17,
            .world_y = 9, .material_flags = (uint8_t)flags};
        for (unsigned protected = 0; protected < 2u; ++protected)
        {
            memset(terrain_cells, 0x55, sizeof terrain_cells);
            uint8_t reflect;
            size_t changed;
            if (portable_driver_wall_material_contact(&wall, &terrain,
                    terrain_cells, sizeof terrain_cells, 8u, (uint16_t)protected,
                    &reflect, &changed) != 0 ||
                reflect != !(flags == 0x84u && protected) ||
                changed != (flags >= 0x82u && flags <= 0x8fu ? 6u : SIZE_MAX))
                return 1;
            for (size_t cell = 0; cell < sizeof terrain_cells; ++cell)
                if (terrain_cells[cell] != (cell == changed ? 8u : 0x55u)) return 1;
        }
    }
    puts("portable driver material: PASS");
    return 0;
}

int run_driver_track_response(void)
{
    PortableDriverMaterialState material = {
        .in_bounds = 1,
        .track_cell = 0x03,
        .track_cell_valid = 1,
    };
    PortableDriverTrackResponseState state = {0};

    if (portable_driver_track_response_tick(&material, &state) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_OK ||
        state.flags != PORTABLE_DRIVER_TRACK_RESPONSE_TRANSITION ||
        state.source_flags != 0 ||
        state.surface_cell != 0x03 || !state.surface_cell_valid ||
        !state.surface_cell_changed)
        return 1;
    if (portable_driver_track_response_tick(&material, &state) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_OK ||
        state.flags != 0 || state.source_flags != 0 ||
        state.surface_cell_changed)
        return 1;

    material.track_cell = 0x83;
    if (portable_driver_track_response_tick(&material, &state) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_OK ||
        state.flags != 0 || state.source_flags != 0 ||
        state.surface_cell != 0x03 || state.surface_cell_changed)
        return 1;

    material.track_cell = 0x7f;
    material.off_track = 1;
    if (portable_driver_track_response_tick(&material, &state) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_OK ||
        (state.flags & PORTABLE_DRIVER_TRACK_RESPONSE_OFF_TRACK) == 0 ||
        state.source_flags != PORTABLE_DRIVER_TRACK_RESPONSE_OFF_TRACK ||
        state.surface_cell != 0x7f || !state.surface_cell_changed)
        return 1;

    material.in_bounds = 0;
    material.off_track = 0;
    state.flags = 0x4;
    if (portable_driver_track_response_tick(&material, &state) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_OK ||
        (state.flags & PORTABLE_DRIVER_TRACK_RESPONSE_BOUNDARY) == 0 ||
        state.source_flags != PORTABLE_DRIVER_TRACK_RESPONSE_BOUNDARY ||
        (state.flags & PORTABLE_DRIVER_TRACK_RESPONSE_TRANSITION) != 0 ||
        (state.flags & PORTABLE_DRIVER_TRACK_RESPONSE_OFF_TRACK) != 0)
        return 1;

    if (portable_driver_track_response_tick(NULL, &state) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_INVALID_ARGUMENT ||
        portable_driver_track_response_tick(&material, NULL) !=
            PORTABLE_DRIVER_TRACK_RESPONSE_INVALID_ARGUMENT)
        return 1;

    puts("portable driver track response: PASS");
    return 0;
}

int run_driver_terrain_effect(void)
{
    PortableDriverMaterialState material = {
        .in_bounds = 1,
        .world_x = 17,
        .world_y = 16,
        .material_flags = 0x80,
        .material_flags_valid = 1,
    };
    PortableDriverTerrainEffectState state = {
        .cell_key = 0x0201,
        .cell_key_valid = 1,
    };

    if (portable_driver_terrain_effect_tick(&material, 0, &state) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
        state.previous_cell_key != 0x0201 || state.cell_key != 0x0202 ||
        state.cell_delta != 1 || !state.cell_changed ||
        !state.effect_active || !state.effect_dispatch_candidate ||
        state.effect_dispatch_delta != 0 || state.height_gated ||
        state.bounds_gated)
        return 1;

    material.world_x = 16;
    if (portable_driver_terrain_effect_tick(&material, 0, &state) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
        state.previous_cell_key != 0x0202 || state.cell_key != 0x0202 ||
        state.cell_delta != 0 || state.cell_changed ||
        state.effect_dispatch_delta != -254)
        return 1;

    material.world_x = 9;
    if (portable_driver_terrain_effect_tick(&material, 0, &state) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
        state.cell_key != 0x0201 || state.cell_delta != -1 ||
        state.effect_dispatch_delta != 4)
        return 1;

    state.cell_key = 0x1234;
    material.world_x = 24;
    if (portable_driver_terrain_effect_tick(&material, 0x00050000, &state) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_HEIGHT_LIMIT ||
        !state.height_gated || state.cell_key != 0x1234 ||
        state.cell_changed || state.effect_active)
        return 1;

    material.in_bounds = 0;
    if (portable_driver_terrain_effect_tick(&material, 0, &state) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_OUT_OF_BOUNDS ||
        !state.bounds_gated || state.cell_key != 0x1234 ||
        state.cell_changed)
        return 1;

    if (portable_driver_terrain_effect_tick(NULL, 0, &state) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT ||
        portable_driver_terrain_effect_tick(&material, 0, NULL) !=
            PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT)
        return 1;

    {
        static const int32_t x_offsets[8] = {0, 8, 0, -8, 0, -8, 0, 8};
        static const int32_t y_offsets[8] = {0, -8, 0, -8, 0, 8, 0, 8};
        static uint8_t flags[256];
        static uint8_t terrain_cells[5 * 5];
        PortableTerrainEffectTableView tables = {
            .x_offsets = x_offsets,
            .x_count = 8,
            .y_offsets = y_offsets,
            .y_count = 8,
        };
        PortableTrackMaterialView track_material = {
            .flags = flags,
            .count = sizeof(flags),
        };
        PortableTerrainView terrain = {
            .cells = terrain_cells,
            .row_stride = 5,
            .width = 5,
            .height = 5,
        };
        PortableDriverMaterialState dispatch_material = {
            .world_x = 16,
            .world_y = 16,
            .material_flags_valid = 1,
        };
        PortableDriverTerrainEffectDispatch dispatch;
        int32_t vector_x;
        int32_t vector_y;

        memset(flags, 0, sizeof(flags));
        memset(terrain_cells, 0, sizeof(terrain_cells));
        flags[1] = 0x80;
        flags[2] = 0x80;
        terrain_cells[2u * 5u + 3u] = 1; /* mode 1, x neighbor */
        terrain_cells[1u * 5u + 2u] = 2; /* mode 1, y neighbor */
        vector_x = 5;
        vector_y = -7;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, &track_material, &terrain, &tables, 1,
                &vector_x, &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
            dispatch.x_neighbor_flags != 0x80 ||
            dispatch.y_neighbor_flags != 0x80 || dispatch.orientation != 3 ||
            vector_x != -5 || vector_y != 7 || !dispatch.vector_mutated)
            return 1;

        flags[1] = 0;
        flags[2] = 0x80;
        terrain_cells[2u * 5u + 1u] = 3; /* mode 3, x neighbor */
        flags[3] = 0;
        vector_x = 5;
        vector_y = -7;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, &track_material, &terrain, &tables, 3,
                &vector_x, &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
            dispatch.orientation != 1 || vector_x != -5 || vector_y != -7)
            return 1;

        flags[1] = 0x80;
        flags[2] = 0;
        terrain_cells[2u * 5u + 1u] = 1; /* mode 5, x neighbor */
        terrain_cells[3u * 5u + 2u] = 4; /* mode 5, y neighbor */
        flags[4] = 0;
        vector_x = 5;
        vector_y = -7;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, &track_material, &terrain, &tables, 5,
                &vector_x, &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
            dispatch.orientation != 2 || vector_x != 5 || vector_y != 7)
            return 1;

        flags[1] = 0;
        flags[2] = 0;
        terrain_cells[2u * 5u + 3u] = 3; /* mode 7, x neighbor */
        terrain_cells[3u * 5u + 2u] = 4; /* mode 7, y neighbor */
        vector_x = 5;
        vector_y = -7;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, &track_material, &terrain, &tables, 7,
                &vector_x, &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
            dispatch.orientation != 0 || vector_x != -5 || vector_y != 7)
            return 1;

        vector_x = INT32_MIN;
        vector_y = INT32_MAX;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, NULL, NULL, &tables, 0, &vector_x,
                &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
            vector_x != INT32_MIN || vector_y != INT32_MAX)
            return 1;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, NULL, NULL, &tables, 9, &vector_x,
                &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_OK ||
            vector_x != INT32_MIN || vector_y != INT32_MAX)
            return 1;
        if (portable_driver_terrain_effect_dispatch(
                &dispatch_material, NULL, NULL, &tables, 1, &vector_x,
                &vector_y, &dispatch) !=
                PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT)
            return 1;
    }

    puts("portable driver terrain effect: PASS");
    return 0;
}

int run_driver_vector_effect(void)
{
    static uint8_t source_rom[PORTABLE_SOURCE_VECTOR_Y_OFFSET +
                              PORTABLE_SOURCE_VECTOR_BYTES];
    static int16_t alternate_x[PORTABLE_SOURCE_VECTOR_COUNT];
    static int16_t alternate_y[PORTABLE_SOURCE_VECTOR_COUNT];
    static int32_t normal_response_entries[
        PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT];
    static const int32_t x_offsets[8] = {0, 8, 0, -8, 0, -8, 0, 8};
    static const int32_t y_offsets[8] = {0, -8, 0, -8, 0, 8, 0, 8};
    static uint8_t flags[256];
    static uint8_t terrain_cells[8 * 8];
    const PortableTerrainEffectTableView tables = {
        .x_offsets = x_offsets,
        .x_count = 8,
        .y_offsets = y_offsets,
        .y_count = 8,
    };
    const PortableTrackMaterialView track_material = {
        .flags = flags,
        .count = sizeof(flags),
    };
    const PortableTerrainView terrain = {
        .cells = terrain_cells,
        .row_stride = 8,
        .width = 8,
        .height = 8,
    };
    PortableDriverMaterialState material = {
        .in_bounds = 1,
        .world_x = 16,
        .world_y = 16,
    };
    PortableDriverVectorEffectResult result;
    PortableDriverVectorCollisionResult collision_response;
    PortableDriverNormalCollisionResult normal_collision_response;
    PortableDriverVectorTriggerResult trigger;
    PortableDriverVectorScheduleResult schedule;
    uint16_t source_angle;
    PortableSourceVectorTableBuffers source_buffers = {
        .x_components = alternate_x,
        .x_capacity = PORTABLE_SOURCE_VECTOR_COUNT,
        .y_components = alternate_y,
        .y_capacity = PORTABLE_SOURCE_VECTOR_COUNT,
    };
    PortableSourceVectorTableView source_view;
    PortableSourceNormalResponseTableBuffers normal_response_buffers = {
        .entries = normal_response_entries,
        .capacity = PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT,
    };
    PortableSourceNormalResponseTableView normal_response_table;
    PortableSourceNormalResponseResult normal_response;
    int16_t component_x;
    int16_t component_y;

    /* Original sub_8040570 at Peach GP frame 2,022: object center
     * (812,292), driver anchor (816,290), prior vector (-482,-390),
     * +0x7a=0xdb8d. Source sub_802EC38 returns 0x2d1c; ROM sine
     * samples at indices 180/76 are 29269/14732. */
    {
        int32_t sine[1024] = {0};
        PortableDriverObjectContactResult object_contact;

        sine[180] = 29269;
        sine[76] = 14732;
        if (portable_driver_object_contact_resultant(
                816, 290, 812, 292, -482, -390, UINT16_C(0xdb8d),
                sine, 1024u, &object_contact) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
            object_contact.impact_angle != UINT16_C(0x2d1c) ||
            object_contact.response_angle != UINT16_C(0x2d1c) ||
            object_contact.resultant_x != 101 ||
            object_contact.resultant_y != -366 ||
            portable_driver_object_contact_resultant(
                816, 290, 812, 292, -482, -390, UINT16_C(0xdb8d),
                sine, 1023u, &object_contact) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
            return 1;
    }

    /* 4073C ignores prior velocity/heading and pushes along the current
     * object-to-kart direction, including signed-angle truncation. */
    {
        int32_t sine[1024] = {0};
        PortableDriverObjectContactResult contact;
        sine[180] = 29269;
        sine[76] = 14732;
        if (portable_driver_object_contact_escape(
                816, 290, 812, 292, sine, 1024u, &contact) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
            contact.impact_angle != UINT16_C(0x2d1c) ||
            contact.resultant_x != 457 || contact.resultant_y != -230)
            return 1;
        sine[0] = 0;
        sine[256] = 32768;
        if (portable_driver_object_contact_escape(
                4, 0, 4, 4, sine, 1024u, &contact) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
            contact.resultant_x != 0 || contact.resultant_y != -512 ||
            portable_driver_object_contact_escape(
                4, 0, 4, 4, sine, 1023u, &contact) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
            return 1;
    }

    memset(source_rom, 0, sizeof(source_rom));
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_X_OFFSET, 0x0100u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_X_OFFSET + 2u, 0x0100u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_X_OFFSET + 4u, 0xff00u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_X_OFFSET + 6u, 0xff00u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_Y_OFFSET, 0xff00u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_Y_OFFSET + 2u, 0x0100u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_Y_OFFSET + 4u, 0x0100u);
    write_le16(source_rom + PORTABLE_SOURCE_VECTOR_Y_OFFSET + 6u, 0xff00u);
    if (portable_source_vector_tables_build(
            source_rom, sizeof(source_rom), &source_buffers, &source_view) !=
            PORTABLE_SOURCE_VECTOR_TABLES_OK ||
        source_view.x_components != alternate_x ||
        source_view.y_components != alternate_y ||
        alternate_x[0] != 256 || alternate_x[2] != -256 ||
        alternate_y[0] != -256 || alternate_y[3] != -256 ||
        portable_source_vector_tables_build(
            source_rom, PORTABLE_SOURCE_VECTOR_Y_OFFSET, &source_buffers,
            &source_view) != PORTABLE_SOURCE_VECTOR_TABLES_OUT_OF_BOUNDS ||
        portable_source_vector_tables_build(
            NULL, sizeof(source_rom), &source_buffers, &source_view) !=
            PORTABLE_SOURCE_VECTOR_TABLES_INVALID_ARGUMENT)
        return 1;

    {
        static const int32_t response_entries[
            PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT] = {
                -4, -8, -16, -24, -36, -56, -64, -85};
        size_t response_index;
        for (response_index = 0;
             response_index < PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT;
             ++response_index)
            write_le32(source_rom + PORTABLE_SOURCE_NORMAL_RESPONSE_OFFSET +
                           response_index * 4u,
                       (uint32_t)response_entries[response_index]);
    }
    if (portable_source_normal_response_table_build(
            source_rom, sizeof(source_rom), &normal_response_buffers,
            &normal_response_table) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response_table.entries != normal_response_entries ||
        normal_response_table.count != PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT ||
        normal_response_entries[0] != -4 ||
        normal_response_entries[7] != -85 ||
        portable_source_normal_response_table_build(
            source_rom, PORTABLE_SOURCE_NORMAL_RESPONSE_OFFSET +
                            PORTABLE_SOURCE_NORMAL_RESPONSE_BYTES - 1u,
            &normal_response_buffers, &normal_response_table) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OUT_OF_BOUNDS ||
        portable_source_normal_response_table_build(
            NULL, sizeof(source_rom), &normal_response_buffers,
            &normal_response_table) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_INVALID_ARGUMENT)
        return 1;

    if (portable_source_normal_response_produce(
            UINT8_C(0x0b), INT16_C(0x0100), &normal_response_table,
            &normal_response) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response.next_status != UINT8_C(0x0b) ||
        normal_response.timer_cleared != 0 ||
        normal_response.output_written != 1 ||
        normal_response.table_index != 0 ||
        normal_response.output_q16 != -INT32_C(0x00040000) ||
        portable_source_normal_response_produce(
            UINT8_C(0x0b), 0, &normal_response_table, &normal_response) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response.next_status != UINT8_C(0x0e) ||
        normal_response.timer_cleared != 1 || normal_response.output_written != 0 ||
        portable_source_normal_response_produce(
            UINT8_C(0x0b), INT16_C(0x3fff), &normal_response_table,
            &normal_response) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response.table_index != 7 ||
        normal_response.output_q16 != -INT32_C(0x00550000) ||
        portable_source_normal_response_produce(
            UINT8_C(0x03), INT16_C(0x0100), &normal_response_table,
            &normal_response) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response.next_status != UINT8_C(0x03) ||
        normal_response.output_written != 0 ||
        portable_source_normal_response_produce(
            UINT8_C(0x0b), INT16_C(0x0100),
            &(PortableSourceNormalResponseTableView){
                .entries = normal_response_entries,
                .count = PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT - 1u,
            },
            &normal_response) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_CAPACITY ||
        portable_source_normal_response_produce(
            UINT8_C(0x0b), INT16_C(0x0100), NULL, &normal_response) !=
            PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_INVALID_ARGUMENT)
        return 1;

    /* GP A+Left frame 2065: 8E34 changes +0x80 from 0x0aa0 to 0x06a0
     * before DAEC selects the next acceleration. The captured output is
     * -4, whereas selecting from the stale angle would produce -8. */
    if (portable_source_normal_response_produce(
            UINT8_C(0x0b), INT16_C(0x06a0), &normal_response_table,
            &normal_response) != PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response.table_index != 0u ||
        normal_response.output_q16 != -INT32_C(0x00040000) ||
        portable_source_normal_response_produce(
            UINT8_C(0x0b), INT16_C(0x0aa0), &normal_response_table,
            &normal_response) != PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK ||
        normal_response.table_index != 1u ||
        normal_response.output_q16 != -INT32_C(0x00080000))
        return 1;

    component_x = 5;
    component_y = 7;
    if (portable_driver_vector_effect_apply_alternate(
            UINT16_C(0x4000), 7, alternate_x, 4, alternate_y, 4,
            &component_x, &component_y, &result) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        component_x != 256 || component_y != 256 || result.mode != 7 ||
        result.alternate_state_branch != 1 ||
        result.source_direction_index != 1 || result.contact_word_5a != 0 ||
        !result.component_mutated ||
        portable_driver_vector_effect_apply_alternate(
            0, 0, alternate_x, 3, alternate_y, 4, &component_x, &component_y,
            &result) != PORTABLE_DRIVER_VECTOR_EFFECT_QUERY_ERROR ||
        portable_driver_vector_effect_apply_alternate(
            0, 0, alternate_x, 4, alternate_y, 4, NULL, &component_y,
            &result) != PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
        return 1;

    if (portable_driver_vector_effect_apply_collision(
            12, -7, UINT16_C(0x1234), UINT16_C(0xff00),
            UINT32_C(0xdeadbfff), UINT16_C(0xaaaa), UINT8_C(0x40),
            &collision_response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        collision_response.source_flags != UINT32_C(0xdeadafff) ||
        collision_response.angle_delta != UINT16_C(0x1334) ||
        collision_response.timer_value != UINT8_C(0x0b) ||
        collision_response.response_applied != 1 ||
        portable_driver_vector_effect_apply_collision(
            0, 0, UINT16_C(0x1234), UINT16_C(0xff00),
            UINT32_C(0x00001001), UINT16_C(0xfff0), UINT8_C(0x09),
            &collision_response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        collision_response.source_flags != UINT32_C(0x00000001) ||
        collision_response.angle_delta != UINT16_C(0xfff0) ||
        collision_response.timer_value != UINT8_C(0x09) ||
        collision_response.response_applied != 0 ||
        portable_driver_vector_effect_apply_collision(
            1, 0, 0, 0, 0, 0, 0, NULL) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
        return 1;

    /* Live Peach capture at source frame 12310: a nonzero resultant vector
     * leaves flags 0xc400 after the source 0x1000 clear and arms timer 0x0b;
     * the captured angle delta is zero for this response. */
    if (portable_driver_vector_effect_apply_collision(
            -264, 253, UINT16_C(0x5e02), UINT16_C(0x5e02),
            UINT32_C(0x0000d400), 0, 0, &collision_response) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        collision_response.source_flags != UINT32_C(0x0000c400) ||
        collision_response.angle_delta != 0 ||
        collision_response.timer_value != UINT8_C(0x0b) ||
        collision_response.response_applied != 1)
        return 1;

    if (portable_driver_vector_effect_source_angle(1, 0, &source_angle) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        source_angle != UINT16_C(0x4000) ||
        portable_driver_vector_effect_source_angle(0, 1, &source_angle) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        source_angle != UINT16_C(0x8000) ||
        portable_driver_vector_effect_source_angle(-1, 0, &source_angle) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        source_angle != UINT16_C(0xc000) ||
        portable_driver_vector_effect_source_angle(0, -1, &source_angle) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        source_angle != 0 ||
        portable_driver_vector_effect_source_angle(-287, -90, &source_angle) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        source_angle != UINT16_C(0xcc61) ||
        portable_driver_vector_effect_source_angle(-161, 374, &source_angle) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        source_angle != UINT16_C(0x9090) ||
        portable_driver_vector_effect_source_angle(1, 0, NULL) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
        return 1;

    /* sub_803285C/sub_802EAEC at the captured Peach A+Left frame 1894:
     * source world position (807,724), route cell (110,80), LUT[516]. */
    {
        uint16_t track_lut[517] = {0};

        track_lut[516] = UINT16_C(0x2000);
        if (portable_driver_track_direction_source_angle(
                INT32_C(807) << 16, INT32_C(724) << 16,
                INT32_C(880), INT32_C(640), track_lut, 517u,
                &source_angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
            source_angle != UINT16_C(0x2000))
            return 1;
    }

    /* Live Peach frame 7386 follows sub_803AD6C's normal branch. */
    if (portable_driver_vector_effect_apply_normal_collision(
            -254, 258, UINT16_C(0xa0e3), UINT16_C(0x57e7),
            UINT32_C(0x0000d400), UINT16_C(0xa0e3), 0, 512,
            INT32_C(0x02530000), UINT8_C(0x00), UINT8_C(0x40),
            &normal_collision_response) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        normal_collision_response.source_flags != UINT32_C(0x0000c400) ||
        normal_collision_response.angle_history_7a != UINT16_C(0xa0e3) ||
        normal_collision_response.angle_delta_80 != UINT16_C(0x48fc) ||
        normal_collision_response.resistance_92 != 256 ||
        normal_collision_response.magnitude_state_bc !=
            INT32_C(0x01020000) ||
        normal_collision_response.timer_value_84 != UINT8_C(0x0b) ||
        normal_collision_response.response_timer_7e != UINT8_C(0x0b) ||
        normal_collision_response.response_applied != 1 ||
        portable_driver_vector_effect_apply_normal_collision(
            0, 0, UINT16_C(0x1234), UINT16_C(0x2222),
            UINT32_C(0x00001001), UINT16_C(0xaaaa), UINT16_C(0xbbbb), -1,
            -1, UINT8_C(0x09), UINT8_C(0x04),
            &normal_collision_response) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        normal_collision_response.source_flags != UINT32_C(0x00000001) ||
        normal_collision_response.angle_history_7a != UINT16_C(0x2222) ||
        normal_collision_response.angle_delta_80 != 0 ||
        normal_collision_response.resistance_92 != 256 ||
        normal_collision_response.magnitude_state_bc != 0 ||
        normal_collision_response.timer_value_84 != UINT8_C(0x09) ||
        normal_collision_response.response_timer_7e != UINT8_C(0x04) ||
        normal_collision_response.response_applied != 0 ||
        portable_driver_vector_effect_apply_normal_collision(
            1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
        return 1;

    /* Exact automatic sub_803AD6C capture: source ArcTan2 produces 0x9090
     * from (-161,374), then the normal response stores delta 0x29a5. */
    if (portable_driver_vector_effect_apply_normal_collision_source_angle(
            -161, 374, UINT16_C(0x66eb), UINT32_C(0x0000d400),
            UINT16_C(0x66eb), 0, INT16_C(0x00e0),
            INT32_C(0x02028000), UINT8_C(0x00), UINT8_C(0x00),
            &normal_collision_response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        normal_collision_response.source_flags != UINT32_C(0x0000c400) ||
        normal_collision_response.angle_history_7a != UINT16_C(0x66eb) ||
        normal_collision_response.angle_delta_80 != UINT16_C(0x29a5) ||
        normal_collision_response.response_angle_7a != UINT16_C(0x9090) ||
        normal_collision_response.base_heading_7c != UINT16_C(0x66eb) ||
        normal_collision_response.resistance_92 != INT16_C(0x0100) ||
        normal_collision_response.magnitude_state_bc !=
            INT32_C(0x01760000) ||
        normal_collision_response.timer_value_84 != UINT8_C(0x0b) ||
        normal_collision_response.response_timer_7e != UINT8_C(0x0b) ||
        normal_collision_response.response_applied != 1)
        return 1;

    /* P102 source field-order captures: +0x7a is the resultant angle, +0x7c
     * is the base heading and +0x80 is their fixed-width halfword delta. */
    if (portable_driver_vector_effect_apply_normal_collision(
            -628, 129, UINT16_C(0x492c), UINT16_C(0x616c),
            UINT32_C(0x0000c400), UINT16_C(0x492c), UINT16_C(0xe320),
            INT16_C(0x0180),
            INT32_C(0x02828000), UINT8_C(0x00), UINT8_C(0x01),
            &normal_collision_response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        normal_collision_response.response_angle_7a != UINT16_C(0x492c) ||
        normal_collision_response.base_heading_7c != UINT16_C(0x616c) ||
        normal_collision_response.angle_delta_80 != UINT16_C(0xe7c0) ||
        portable_driver_vector_effect_apply_normal_collision(
            -314, 120, UINT16_C(0xb360), UINT16_C(0x63ac),
            UINT32_C(0x0000c400), UINT16_C(0xb360), UINT16_C(0xe320),
            INT16_C(0x0100),
            INT32_C(0x013a0000), UINT8_C(0x0b), UINT8_C(0x0b),
            &normal_collision_response) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        normal_collision_response.response_angle_7a != UINT16_C(0xb360) ||
        normal_collision_response.base_heading_7c != UINT16_C(0x63ac) ||
        normal_collision_response.angle_delta_80 != UINT16_C(0x4fb4))
        return 1;

    memset(flags, 0, sizeof(flags));
    memset(terrain_cells, 0, sizeof(terrain_cells));

    component_x = 0x1234;
    component_y = -0x2233;
    if (portable_driver_vector_effect_apply(
            NULL, NULL, NULL, NULL, 0, &component_x, &component_y,
            &result) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        component_x != (int16_t)-0x1234 || component_y != -0x2233 ||
        result.orientation != 1 || !result.component_mutated ||
        result.contact_word_5a != 0xc007)
        return 1;

    if (portable_driver_vector_effect_source_trigger(
            UINT8_C(0x80), UINT16_C(0x1200), UINT16_C(0x1201), 1, 0, 0,
            &trigger) != PORTABLE_DRIVER_VECTOR_TRIGGER_OK ||
        trigger.candidate != 1 || trigger.mode != 0 ||
        trigger.cell_delta != 1 ||
        portable_driver_vector_effect_source_trigger(
            UINT8_C(0x80), UINT16_C(0x1201), UINT16_C(0x1200), 1, 0, 0,
            &trigger) != PORTABLE_DRIVER_VECTOR_TRIGGER_OK ||
        trigger.mode != 4 ||
        portable_driver_vector_effect_source_trigger(
            UINT8_C(0x80), UINT16_C(0x0871), UINT16_C(0x0771), 1, 0, 0,
            &trigger) != PORTABLE_DRIVER_VECTOR_TRIGGER_OK ||
        trigger.cell_delta != -256 || trigger.mode != 6 ||
        portable_driver_vector_effect_source_trigger(
            UINT8_C(0x03), UINT16_C(0x1200), UINT16_C(0x1201), 1, 0, 0,
            &trigger) != PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE ||
        portable_driver_vector_effect_source_trigger(
            UINT8_C(0x80), UINT16_C(0x1200), UINT16_C(0x1201), 1,
            INT32_C(0x00040001), 0, &trigger) !=
            PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE ||
        portable_driver_vector_effect_source_trigger(
            UINT8_C(0x80), UINT16_C(0x1200), UINT16_C(0x1201), 1, 0,
            UINT16_C(0x4000), &trigger) !=
            PORTABLE_DRIVER_VECTOR_TRIGGER_OK ||
        !trigger.alternate_state_branch ||
        portable_driver_vector_effect_source_trigger(
            UINT8_C(0x80), UINT16_C(0x1200), UINT16_C(0x1200), 1, 0, 0,
            &trigger) != PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE ||
        !trigger.contact || trigger.candidate)
        return 1;

    /* sub_803B550 arms state+0x60 on a successful sub_803B2B4 result and
     * consumes that arm on the next no-candidate result to schedule the
     * normal +0x1000 response. */
    if (portable_driver_vector_effect_source_schedule_normal_collision(
            0, 1, &schedule) != PORTABLE_DRIVER_VECTOR_SCHEDULER_OK ||
        schedule.next_source_state != UINT16_C(0x8001) ||
        schedule.normal_collision_scheduled || schedule.alternate_state_branch ||
        portable_driver_vector_effect_source_schedule_normal_collision(
            UINT16_C(0x8001), 0, &schedule) !=
            PORTABLE_DRIVER_VECTOR_SCHEDULER_OK ||
        schedule.next_source_state != 0 ||
        schedule.normal_collision_scheduled != 1 ||
        portable_driver_vector_effect_source_schedule_normal_collision(
            UINT16_C(0x8007), 1, &schedule) !=
            PORTABLE_DRIVER_VECTOR_SCHEDULER_OK ||
        schedule.next_source_state != UINT16_C(0xc008) ||
        portable_driver_vector_effect_source_schedule_normal_collision(
            UINT16_C(0x4000), 1, &schedule) !=
            PORTABLE_DRIVER_VECTOR_SCHEDULER_OK ||
        schedule.next_source_state != UINT16_C(0x4000) ||
        !schedule.alternate_state_branch || schedule.normal_collision_scheduled ||
        portable_driver_vector_effect_source_schedule_normal_collision(
            UINT16_C(0x4000), 0, &schedule) !=
            PORTABLE_DRIVER_VECTOR_SCHEDULER_OK ||
        schedule.next_source_state != 0 || schedule.normal_collision_scheduled ||
        portable_driver_vector_effect_source_schedule_normal_collision(
            0, 2, &schedule) !=
            PORTABLE_DRIVER_VECTOR_SCHEDULER_INVALID_RESULT ||
        portable_driver_vector_effect_source_schedule_normal_collision(
            0, 0, NULL) !=
            PORTABLE_DRIVER_VECTOR_SCHEDULER_INVALID_ARGUMENT)
        return 1;

    component_x = 0x1234;
    component_y = -0x2233;
    if (portable_driver_vector_effect_apply(
            NULL, NULL, NULL, NULL, 2, &component_x, &component_y,
            &result) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        component_x != 0x1234 || component_y != (int16_t)0x2233 ||
        result.orientation != 2)
        return 1;

    /* Mode 1 sees the source high-bit material on both neighbors. */
    flags[1] = 0x80;
    flags[2] = 0x80;
    terrain_cells[2u * 8u + 3u] = 1; /* (24,16), x neighbor. */
    terrain_cells[1u * 8u + 2u] = 2; /* (16,8), y neighbor. */
    component_x = 5;
    component_y = -7;
    if (portable_driver_vector_effect_apply(
            &material, &track_material, &terrain, &tables, 1,
            &component_x, &component_y, &result) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        result.x_neighbor_flags != 0x80 || result.y_neighbor_flags != 0x80 ||
        result.orientation != 3 || result.source_direction_index != 2u ||
        component_x != -5 || component_y != 7)
        return 1;

    /* Mode 3 has only the y neighbor high-bit set and therefore flips x. */
    memset(flags, 0, sizeof(flags));
    memset(terrain_cells, 0, sizeof(terrain_cells));
    flags[2] = 0x80;
    terrain_cells[2u * 8u + 1u] = 3; /* (8,16), x neighbor. */
    terrain_cells[1u * 8u + 2u] = 2; /* (16,8), y neighbor. */
    component_x = 5;
    component_y = -7;
    if (portable_driver_vector_effect_apply(
            &material, &track_material, &terrain, &tables, 3,
            &component_x, &component_y, &result) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        result.orientation != 1 || result.source_direction_index != 1u ||
        component_x != -5 || component_y != -7)
        return 1;

    component_x = INT16_MIN;
    component_y = INT16_MIN;
    if (portable_driver_vector_effect_apply(
            NULL, NULL, NULL, NULL, 4, &component_x, &component_y,
            &result) != PORTABLE_DRIVER_VECTOR_EFFECT_OK ||
        component_x != INT16_MIN || component_y != INT16_MIN)
        return 1;

    component_x = 5;
    component_y = 7;
    if (portable_driver_vector_effect_apply(
            &material, &track_material, &terrain, &tables, 8,
            &component_x, &component_y, &result) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_UNSUPPORTED_MODE ||
        component_x != 5 || component_y != 7 ||
        portable_driver_vector_effect_apply(
            &material, NULL, &terrain, &tables, 1, &component_x,
            &component_y, &result) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT ||
        portable_driver_vector_effect_apply(
            &material, &track_material, &terrain, &tables, 1, NULL,
            &component_y, &result) !=
            PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT)
        return 1;

    puts("portable driver vector effect: PASS");
    return 0;
}

int run_driver_angle_target(void)
{
    PortableDriverAngleTargetResult result;

    if (portable_driver_angle_target_update(
            UINT16_C(0x1234), UINT16_C(0x2345), 0, NULL) !=
            PORTABLE_DRIVER_ANGLE_TARGET_INVALID_ARGUMENT)
        return 1;

    /* The source casts the halfword subtraction to signed 16 bits. */
    if (portable_driver_angle_target_update(
            UINT16_C(0x27ff), UINT16_C(0x2000), 0, &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_OK ||
        result.difference != INT16_C(0x07ff) ||
        result.next_current_angle_56 != UINT16_C(0x27ff) ||
        result.next_target_angle_7a != UINT16_C(0x27ff) ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_SNAP_CURRENT)
        return 1;

    if (portable_driver_angle_target_update(
            UINT16_C(0x1801), UINT16_C(0x2000), 0, &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_OK ||
        result.difference != -INT16_C(0x07ff) ||
        result.next_target_angle_7a != UINT16_C(0x1801) ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_SNAP_CURRENT)
        return 1;

    /* Exact threshold crossings step the target by signed0x600. */
    if (portable_driver_angle_target_update(
            UINT16_C(0x2800), UINT16_C(0x2000), 0, &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_OK ||
        result.difference != INT16_C(0x0800) ||
        result.next_target_angle_7a != UINT16_C(0x2600) ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_RETARGET_FAR ||
        portable_driver_angle_target_update(
            UINT16_C(0x1800), UINT16_C(0x2000), 0, &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_OK ||
        result.difference != -INT16_C(0x0800) ||
        result.next_target_angle_7a != UINT16_C(0x1a00) ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_RETARGET_FAR)
        return 1;

    /* Unsigned subtraction wraps before the source signed-halfword cast. */
    if (portable_driver_angle_target_update(
            UINT16_C(0x0100), UINT16_C(0xff00), 0, &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_OK ||
        result.difference != INT16_C(0x0200) ||
        result.next_target_angle_7a != UINT16_C(0x0100) ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_SNAP_CURRENT)
        return 1;

    /* sub_803BA98 owns this branch; the pure helper must not invent its
     * current-angle mutation. */
    if (portable_driver_angle_target_update(
            UINT16_C(0x1234), UINT16_C(0x2345), UINT16_C(0x0200),
            &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_UNSUPPORTED_STATE_CALLBACK ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_STATE_CALLBACK ||
        result.next_current_angle_56 != UINT16_C(0x1234) ||
        result.next_target_angle_7a != UINT16_C(0x2345))
        return 1;

    /* sub_803BB48 owns the equal-angle branch. */
    if (portable_driver_angle_target_update(
            UINT16_C(0x4567), UINT16_C(0x4567), 0, &result) !=
            PORTABLE_DRIVER_ANGLE_TARGET_UNSUPPORTED_EQUAL_CALLBACK ||
        result.branch != PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_EQUAL_CALLBACK ||
        result.difference != 0 ||
        result.next_target_angle_7a != UINT16_C(0x4567))
        return 1;

    puts("portable driver angle target: PASS");
    return 0;
}

int run_driver_angle_callbacks(void)
{
    static uint8_t table[PORTABLE_DRIVER_ANGLE_TARGET_TABLE_BYTES];
    PortableDriverAngleLimitResult limit_result;
    PortableDriverAngleTargetCallbackResult target_result;

    if (portable_driver_angle_limit_callback(
            UINT16_C(0x0200), UINT16_C(0x0100), UINT16_C(0x0100), NULL) !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_INVALID_ARGUMENT)
        return 1;

    if (portable_driver_angle_limit_callback(
            UINT16_C(0x0200), UINT16_C(0x0100), UINT16_C(0x0100),
            &limit_result) != PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        limit_result.amount != INT32_C(0x0200) ||
        limit_result.position_after_amount != INT32_C(0x0300) ||
        limit_result.next_position_82 != UINT16_C(0x0300) ||
        limit_result.next_limit_b6 != UINT16_C(0x01c0) ||
        limit_result.branch != PORTABLE_DRIVER_ANGLE_LIMIT_DECREMENT)
        return 1;

    if (portable_driver_angle_limit_callback(
            UINT16_C(0x0040), 0, UINT16_C(0x0500), &limit_result) !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        limit_result.amount != -INT32_C(0x0040) ||
        limit_result.position_after_amount != INT32_C(0x04c0) ||
        limit_result.next_position_82 != UINT16_C(0x0040) ||
        limit_result.next_limit_b6 != UINT16_C(0x0040) ||
        limit_result.branch != PORTABLE_DRIVER_ANGLE_LIMIT_POSITION_UPDATE)
        return 1;

    /* A low signed limit crosses the positive +/-0x480 boundary and resets
     * the source response fields. */
    if (portable_driver_angle_limit_callback(
            UINT16_C(0x0080), UINT16_C(0xdfad), 0, &limit_result) !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        limit_result.position_after_amount != INT32_C(0x0080) ||
        limit_result.next_position_82 != 0 ||
        limit_result.next_flags_b4 != UINT16_C(0x44ad) ||
        limit_result.next_angle_delta_80 != 0 ||
        limit_result.next_timer_84 != 0 ||
        limit_result.next_response_status_7e != 0 ||
        limit_result.branch != PORTABLE_DRIVER_ANGLE_LIMIT_BOUNDARY_RESET)
        return 1;

    if (portable_driver_angle_limit_callback(
        UINT16_C(0x0080), 0, 0, &limit_result) !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        limit_result.amount != -INT32_C(0x0080) ||
        limit_result.position_after_amount != -INT32_C(0x0080) ||
        limit_result.next_position_82 != 0 ||
        limit_result.branch != PORTABLE_DRIVER_ANGLE_LIMIT_BOUNDARY_RESET)
        return 1;

    /* sub_803BA98 clamps the applied amount but still decrements the raw
     * source limit by 0x40 on its high-limit branch. */
    if (portable_driver_angle_limit_callback(
            UINT16_C(0x2000), UINT16_C(0x0100), 0, &limit_result) !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        limit_result.amount != INT32_C(0x1000) ||
        limit_result.position_after_amount != INT32_C(0x1000) ||
        limit_result.next_position_82 != UINT16_C(0x1000) ||
        limit_result.next_limit_b6 != UINT16_C(0x1fc0) ||
        limit_result.branch != PORTABLE_DRIVER_ANGLE_LIMIT_DECREMENT)
        return 1;

    memset(table, 0, sizeof(table));
    table[PORTABLE_DRIVER_ANGLE_TARGET_TABLE_OFFSET + 3u * 8u + 7u] = 0x12;
    table[PORTABLE_DRIVER_ANGLE_TARGET_TABLE_OFFSET + 4u * 8u + 7u] =
        0x09;

    if (portable_driver_angle_target_callback(
        UINT16_C(0x1200), UINT16_C(0x1000), 3, table, sizeof(table),
            &target_result) != PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        target_result.difference != INT16_C(0x0200) ||
        target_result.table_index != 31u || target_result.table_value != 0x12 ||
        target_result.next_angle_7a != UINT16_C(0x1120) ||
        target_result.next_angle_56 != UINT16_C(0x1120) ||
        target_result.branch != PORTABLE_DRIVER_ANGLE_TARGET_TABLE_POSITIVE)
        return 1;

    if (portable_driver_angle_target_callback(
            UINT16_C(0x1000), UINT16_C(0x1800), 4, table, sizeof(table),
            &target_result) != PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        target_result.difference != -INT16_C(0x0800) ||
        target_result.table_index != 39u || target_result.table_value != 0x09 ||
        target_result.next_angle_7a != UINT16_C(0x1770) ||
        target_result.next_angle_56 != UINT16_C(0x1770) ||
        target_result.branch != PORTABLE_DRIVER_ANGLE_TARGET_TABLE_NEGATIVE)
        return 1;

    /* Both near-angle thresholds snap to +0xca and do not require a table. */
    if (portable_driver_angle_target_callback(
            UINT16_C(0x11ff), UINT16_C(0x1000), 0, NULL, 0,
            &target_result) != PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        target_result.next_angle_7a != UINT16_C(0x11ff) ||
        target_result.next_angle_56 != UINT16_C(0x11ff) ||
        target_result.branch != PORTABLE_DRIVER_ANGLE_TARGET_SNAP ||
        portable_driver_angle_target_callback(
            UINT16_C(0x1000), UINT16_C(0x1200), 0, NULL, 0,
            &target_result) != PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        target_result.difference != -INT16_C(0x0200) ||
        target_result.branch != PORTABLE_DRIVER_ANGLE_TARGET_SNAP)
        return 1;

    if (portable_driver_angle_target_callback(
            UINT16_C(0x1800), UINT16_C(0x1000), 0, NULL,
            PORTABLE_DRIVER_ANGLE_TARGET_TABLE_BYTES - 1u, &target_result) !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_TABLE_TOO_SMALL ||
        portable_driver_angle_target_callback(
            UINT16_C(0x1800), UINT16_C(0x1000), 0, table, sizeof(table),
            NULL) != PORTABLE_DRIVER_ANGLE_CALLBACK_INVALID_ARGUMENT)
        return 1;

    puts("portable driver angle callbacks: PASS");
    return 0;
}

int run_driver_angle_composition(void)
{
    static uint8_t table[PORTABLE_DRIVER_ANGLE_TARGET_TABLE_BYTES];
    PortableDriverAngleCompositionInput input = {0};
    PortableDriverAngleCompositionResult result;

    if (portable_driver_angle_compose(NULL, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_INVALID_ARGUMENT ||
        portable_driver_angle_compose(&input, NULL) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_INVALID_ARGUMENT)
        return 1;

    /* The state-callback branch applies BA98 first, then BC00 adds the
     * updated +0x82 to the pre-callback +0x7a and stores a halfword. */
    input = (PortableDriverAngleCompositionInput){
        .current_angle_56 = UINT16_C(0x1000),
        .target_angle_7a = UINT16_C(0x2000),
        .source_flags_b4 = UINT16_C(0x0300),
        .limit_b6 = UINT16_C(0x0200),
        .position_82 = UINT16_C(0x0100),
        .angle_delta_80 = UINT16_C(0x1234),
        .timer_84 = 0x0b,
        .response_status_7e = 0x5a,
    };
    if (portable_driver_angle_compose(&input, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_OK ||
        result.branch != PORTABLE_DRIVER_ANGLE_COMPOSITION_STATE_CALLBACK ||
        result.callback_status != PORTABLE_DRIVER_ANGLE_CALLBACK_OK ||
        result.next_current_angle_56 != UINT16_C(0x2300) ||
        result.next_target_angle_7a != UINT16_C(0x2000) ||
        result.next_flags_b4 != UINT16_C(0x0300) ||
        result.next_limit_b6 != UINT16_C(0x01c0) ||
        result.next_position_82 != UINT16_C(0x0300) ||
        result.next_angle_delta_80 != UINT16_C(0x1234) ||
        result.next_timer_84 != 0x0b ||
        result.next_response_status_7e != 0x5a)
        return 1;

    /* A boundary reset owns the three callback-cleared fields and the
     * masked flag write. */
    input.limit_b6 = UINT16_C(0x0080);
    input.source_flags_b4 = UINT16_C(0xdfad);
    input.position_82 = 0;
    if (portable_driver_angle_compose(&input, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_OK ||
        result.next_current_angle_56 != UINT16_C(0x2000) ||
        result.next_flags_b4 != UINT16_C(0x44ad) ||
        result.next_limit_b6 != UINT16_C(0x0080) ||
        result.next_position_82 != 0 ||
        result.next_angle_delta_80 != 0 || result.next_timer_84 != 0 ||
        result.next_response_status_7e != 0)
        return 1;

    /* This is the bounded P072 caller shape. The table value is deliberately
     * supplied by the caller at the P071 contract offset. */
    memset(table, 0, sizeof(table));
    table[PORTABLE_DRIVER_ANGLE_TARGET_TABLE_OFFSET + 31u] = 0x10;
    input = (PortableDriverAngleCompositionInput){
        .current_angle_56 = UINT16_C(0xe100),
        .target_angle_7a = UINT16_C(0xe100),
        .source_flags_b4 = 0,
        .target_angle_ca = UINT16_C(0xea00),
        .table_nibble_9a = 3,
        .target_table = table,
        .target_table_count = sizeof(table),
    };
    if (portable_driver_angle_compose(&input, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_OK ||
        result.branch != PORTABLE_DRIVER_ANGLE_COMPOSITION_EQUAL_CALLBACK ||
        result.target_callback.difference != INT16_C(0x0900) ||
        result.target_callback.table_index != 31u ||
        result.target_callback.table_value != 0x10 ||
        result.next_current_angle_56 != UINT16_C(0xe200) ||
        result.next_target_angle_7a != UINT16_C(0xe200))
        return 1;

    /* P070's ordinary branches remain pass-through for +0x56 and callback
     * fields while updating only +0x7a. */
    input = (PortableDriverAngleCompositionInput){
        .current_angle_56 = UINT16_C(0x1100),
        .target_angle_7a = UINT16_C(0x1000),
        .source_flags_b4 = 0,
        .angle_delta_80 = UINT16_C(0x2222),
        .timer_84 = 2,
        .response_status_7e = 3,
    };
    if (portable_driver_angle_compose(&input, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_OK ||
        result.branch != PORTABLE_DRIVER_ANGLE_COMPOSITION_SNAP_CURRENT ||
        result.next_current_angle_56 != UINT16_C(0x1100) ||
        result.next_target_angle_7a != UINT16_C(0x1100) ||
        result.next_angle_delta_80 != UINT16_C(0x2222) ||
        result.next_timer_84 != 2 || result.next_response_status_7e != 3)
        return 1;

    input.current_angle_56 = UINT16_C(0x2800);
    input.target_angle_7a = UINT16_C(0x2000);
    if (portable_driver_angle_compose(&input, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_OK ||
        result.branch != PORTABLE_DRIVER_ANGLE_COMPOSITION_RETARGET_FAR ||
        result.next_current_angle_56 != UINT16_C(0x2800) ||
        result.next_target_angle_7a != UINT16_C(0x2600))
        return 1;

    /* A malformed equal-angle table is reported by the delegated callback,
     * with the result retaining the caller's uncommitted state. */
    input.current_angle_56 = UINT16_C(0x4000);
    input.target_angle_7a = UINT16_C(0x4000);
    input.target_angle_ca = UINT16_C(0x5000);
    input.target_table = NULL;
    input.target_table_count = 0;
    if (portable_driver_angle_compose(&input, &result) !=
            PORTABLE_DRIVER_ANGLE_COMPOSITION_TARGET_CALLBACK_ERROR ||
        result.branch != PORTABLE_DRIVER_ANGLE_COMPOSITION_EQUAL_CALLBACK ||
        result.callback_status !=
            PORTABLE_DRIVER_ANGLE_CALLBACK_TABLE_TOO_SMALL ||
        result.next_current_angle_56 != UINT16_C(0x4000) ||
        result.next_target_angle_7a != UINT16_C(0x4000))
        return 1;

    puts("portable driver angle composition: PASS");
    return 0;
}

typedef struct
{
    uint8_t trace[16];
    size_t count;
} CollisionTrace;

static void record_collision(PortableCollisionActor *actor,
                             PortableCollisionActor *other,
                             void *user)
{
    CollisionTrace *trace = user;

    if (trace->count < sizeof(trace->trace))
        trace->trace[trace->count++] = actor->tag;
    (void)other;
}



int run_collision(void)
{
    PortableCollisionList list = {0};
    CollisionTrace trace = {0};
    PortableCollisionActor active = {
        .pos_x = 0, .pos_y = 0, .pos_z = 0,
        .size_x = 2, .size_y = 2, .size_z = 2,
        .checkpoint_id = 1, .tag = 1, .collide = record_collision,
        .user = &trace,
    };
    PortableCollisionActor active_two = {
        .pos_x = 0, .pos_y = 0, .pos_z = 0,
        .size_x = 2, .size_y = 2, .size_z = 2,
        .checkpoint_id = 3, .tag = 2, .collide = record_collision,
        .user = &trace,
    };
    PortableCollisionActor passive = {
        .pos_x = 0, .pos_y = 0, .pos_z = 0,
        .size_x = 2, .size_y = 2, .size_z = 2,
        .checkpoint_id = 3, .tag = 3, .collide = record_collision,
        .user = &trace,
    };
    PortableCollisionActor far = passive;

    far.user = &trace;
    far.tag = 4;
    far.checkpoint_id = 0;
    far.pos_x = 0x10001;
    if (portable_collision_add_active(&list, &active) !=
            PORTABLE_COLLISION_OK ||
        portable_collision_add_active(&list, &active_two) !=
            PORTABLE_COLLISION_OK ||
        portable_collision_add_passive(&list, &passive) !=
            PORTABLE_COLLISION_OK ||
        portable_collision_add_passive(&list, &far) !=
            PORTABLE_COLLISION_OK)
        return 1;
    if (portable_collision_collide(&list, 4, PORTABLE_COLLISION_FAST) !=
        PORTABLE_COLLISION_OK)
        return 1;
    if (trace.count != 6 || trace.trace[0] != 1 || trace.trace[1] != 2 ||
        trace.trace[2] != 1 || trace.trace[3] != 3 ||
        trace.trace[4] != 2 || trace.trace[5] != 3)
        return 1;
    if (list.active[0] != &active || list.active[1] != &active_two)
        return 1;

    trace.count = 0;
    if (portable_collision_collide(&list, 4, PORTABLE_COLLISION_FULL) !=
        PORTABLE_COLLISION_OK)
        return 1;
    if (trace.count != 6 || trace.trace[0] != 1 || trace.trace[1] != 2 ||
        trace.trace[2] != 1 || trace.trace[3] != 3 ||
        trace.trace[4] != 2 || trace.trace[5] != 3)
        return 1;

    trace.count = 0;
    active.filter_flags = 2;
    if (portable_collision_collide(&list, 4, PORTABLE_COLLISION_FULL) !=
            PORTABLE_COLLISION_OK || trace.count != 2 ||
        trace.trace[0] != 2 || trace.trace[1] != 3)
        return 1;
    active.filter_flags = 0;
    while (list.active_count < PORTABLE_COLLISION_MAX_ACTIVE)
    {
        if (portable_collision_add_active(&list, &active) !=
            PORTABLE_COLLISION_OK)
            return 1;
    }
    if (portable_collision_add_active(&list, &active) !=
            PORTABLE_COLLISION_CAPACITY_EXCEEDED ||
        portable_collision_add_passive(&list, NULL) !=
            PORTABLE_COLLISION_INVALID_ARGUMENT)
        return 1;

    portable_collision_clear(&list);
    active.checkpoint_id = 5;
    if (portable_collision_add_active(&list, &active) !=
            PORTABLE_COLLISION_OK ||
        portable_collision_collide(&list, 4, PORTABLE_COLLISION_FAST) !=
            PORTABLE_COLLISION_INVALID_CHECKPOINT)
        return 1;

    puts("portable collision: PASS");
    return 0;
}
