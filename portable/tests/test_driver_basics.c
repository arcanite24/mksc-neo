#include "test_support.h"

int run_smoke(void)
{
    const uint32_t low = 0x3412u;
    const uint32_t high = 0xABCDu;
    const uint32_t combined = low | (high << 16);

    if (combined != UINT32_C(0xABCD3412))
        return 1;

    puts("portable smoke: PASS");
    return 0;
}

int run_driver_tick(void)
{
    uint8_t track[0x300] = {0};
    PortableTrackAsset asset = {
        .data = track,
        .size = sizeof(track),
    };
    PortableTrackGeometryView geometry;
    PortableDriverTickState state = {
        .checkpoint_index = 2,
        .heading = 0x1100,
    };
    PortableDriverTickState before_error;
    const size_t table_base = 0x120;
    const size_t record_one = table_base + (1u << 3);

    track[0xCC] = 0x00;
    track[0xCD] = 0x01;
    track[0x100 + 3] = 0x20;
    track[0x100 + 4] = 0x00;
    track[table_base + 0] = 0x00;
    track[table_base + 1] = 0x40;
    track[table_base + 2] = 0x00;
    track[table_base + 3] = 0xE0;
    track[record_one + 0] = 0x00;
    track[record_one + 1] = 0x80;
    track[record_one + 2] = 0x00;
    track[record_one + 3] = 0x20;

    if (portable_track_geometry_init(&asset, 3, &geometry) !=
        PORTABLE_TRACK_GEOMETRY_OK)
        return 1;
    if (portable_driver_tick(&geometry, &state, 0x1000) !=
        PORTABLE_DRIVER_TICK_CHANGED)
        return 1;
    if (state.target_x != -4 || state.target_y != 1 ||
        state.target_heading != 0x1000 || state.heading != 0x0FC0 ||
        state.heading_copy != 0x0FC0 ||
        state.heading_target_copy != 0x0FC0)
        return 1;

    if (portable_driver_tick(&geometry, &state, 0x1000) !=
        PORTABLE_DRIVER_TICK_STABLE)
        return 1;
    if (state.heading != 0x0FC0 || state.heading_copy != 0x0FC0 ||
        state.heading_target_copy != 0x0FC0)
        return 1;

    if (portable_driver_tick(&geometry, &state, 0x1300) !=
        PORTABLE_DRIVER_TICK_CHANGED)
        return 1;
    if (state.heading != 0x1100 || state.heading_copy != 0x1100 ||
        state.heading_target_copy != 0x1100)
        return 1;

    before_error = state;
    state.checkpoint_index = 0xFF;
    if (portable_driver_tick(&geometry, &state, 0x1300) !=
        PORTABLE_DRIVER_TICK_GEOMETRY_ERROR)
        return 1;
    state.checkpoint_index = before_error.checkpoint_index;
    if (memcmp(&state, &before_error, sizeof(state)) != 0)
        return 1;

    puts("portable driver tick: PASS");
    return 0;
}

int run_driver_pose(void)
{
    uint8_t cells[4 * 4];
    PortableTrackRaster raster = {
        .cells = cells,
        .capacity = sizeof(cells),
        .width = 4,
        .height = 4,
    };
    PortableDriverPoseState state = {
        .position_x = 15 * INT32_C(0x10000),
        .position_y = 31 * INT32_C(0x10000),
        .component_x = 256,
        .component_y = 256,
    };
    PortableDriverPoseState repeat = state;
    PortableDriverPoseState before_error;
    PortableTrackRaster invalid_raster = raster;

    memset(cells, 0x7f, sizeof(cells));
    cells[2 * 4 + 1] = 0x22;
    if (portable_driver_pose_tick(&raster, &state) !=
            PORTABLE_DRIVER_POSE_OK ||
        state.position_x != 16 * INT32_C(0x10000) ||
        state.position_y != 32 * INT32_C(0x10000) ||
        state.sampled_world_x != 16 || state.sampled_world_y != 32 ||
        state.track_cell != 0x22 || state.track_cell_valid != 1)
        return 1;

    repeat.position_x = 15 * INT32_C(0x10000);
    repeat.position_y = 31 * INT32_C(0x10000);
    if (portable_driver_pose_tick(&raster, &repeat) !=
            PORTABLE_DRIVER_POSE_OK ||
        memcmp(&state, &repeat, sizeof(state)) != 0)
        return 1;

    state.position_x = -INT32_C(0x100000);
    state.position_y = 0;
    state.component_x = 0;
    state.component_y = 0;
    state.track_cell_valid = 1;
    if (portable_driver_pose_tick(&raster, &state) !=
            PORTABLE_DRIVER_POSE_TRACK_OUT_OF_BOUNDS ||
        state.sampled_world_x != -16 || state.sampled_world_y != 0 ||
        state.track_cell_valid != 0)
        return 1;

    before_error = state;
    invalid_raster.cells = NULL;
    if (portable_driver_pose_tick(&invalid_raster, &state) !=
            PORTABLE_DRIVER_POSE_INVALID_ARGUMENT ||
        memcmp(&state, &before_error, sizeof(state)) != 0 ||
        portable_driver_pose_tick(&raster, NULL) !=
            PORTABLE_DRIVER_POSE_INVALID_ARGUMENT)
        return 1;

    puts("portable driver pose: PASS");
    return 0;
}

int run_kart_motion(void)
{
    int32_t sine_table[1024] = {0};
    PortableKartMotionState state;
    PortableKartMotionState before;

    sine_table[1] = 0x4000;
    sine_table[0xff] = -0x4000;
    state.speed_q16 = 0x20000;
    state.acceleration_q16 = 0;
    state.angle = 64;
    state.component_x = 0;
    state.component_y = 0;
    if (portable_kart_motion_tick(&state, sine_table, 1024) !=
            PORTABLE_KART_MOTION_OK ||
        state.speed_q16 != 0x20000 || state.component_x != 1 ||
        state.component_y != 1)
        return 1;

    sine_table[1023] = 0x4000;
    sine_table[257] = 0x4000;
    state.speed_q16 = -0x20000;
    state.acceleration_q16 = 0;
    state.angle = -65;
    if (portable_kart_motion_tick(&state, sine_table, 1024) !=
            PORTABLE_KART_MOTION_OK ||
        state.speed_q16 != -0x20000 || state.component_x != -1 ||
        state.component_y != 1)
        return 1;

    sine_table[0] = 0x4000;
    state.speed_q16 = 0x07dfffff;
    state.acceleration_q16 = 2;
    state.angle = 0;
    if (portable_kart_motion_tick(&state, sine_table, 1024) !=
            PORTABLE_KART_MOTION_OK ||
        state.speed_q16 != 0x07e00000 || state.component_x != 1008)
        return 1;

    {
        /* Both sine indices retain the source's negative-angle bias before
         * arithmetic shift, including either signed-angle limit. */
        static const struct
        {
            int16_t angle;
            uint16_t x_index, y_index;
        } cases[] = {
            {INT16_MIN, 512u, 768u}, {-65, 1023u, 257u},      {-64, 1023u, 257u},
            {-63, 0u, 256u},         {INT16_MAX, 511u, 769u},
        };
        for (size_t index = 0u; index < sizeof cases / sizeof cases[0]; ++index)
        {
            memset(sine_table, 0, sizeof sine_table);
            sine_table[cases[index].x_index] = 32768;
            sine_table[cases[index].y_index] = -32768;
            state = (PortableKartMotionState){.speed_q16 = INT32_MIN, .angle = cases[index].angle};
            if (portable_kart_motion_tick(&state, sine_table, 1024u) != PORTABLE_KART_MOTION_OK ||
                state.speed_q16 != INT32_MIN || state.component_x != INT16_MIN ||
                state.component_y != INT16_MIN)
                return 1;
        }
    }
    before = state;
    if (portable_kart_motion_tick(&state, sine_table, 1023) !=
            PORTABLE_KART_MOTION_TABLE_TOO_SMALL ||
        memcmp(&state, &before, sizeof(state)) != 0 ||
        portable_kart_motion_tick(NULL, sine_table, 1024) !=
            PORTABLE_KART_MOTION_INVALID_ARGUMENT ||
        portable_kart_motion_tick(&state, NULL, 1024) != PORTABLE_KART_MOTION_INVALID_ARGUMENT)
        return 1;

    puts("portable kart motion: PASS");
    return 0;
}

int run_driver_input(void)
{
    PortableDriverInputState state = {
        .base_speed_q16 = INT32_C(0x00123456),
        .speed_parameter = 5,
        .state_flags = 0,
        .control_word = 0,
        .speed_target_q16 = 0,
    };
    PortableDriverInputState before_error;

    if (portable_driver_control_word_from_gba_keys(
            UINT16_C(0x0373)) != UINT16_C(0xcb50))
        return 1;
    if (portable_driver_control_word_from_gba_keys(0) != 0)
        return 1;
    if (portable_driver_control_word_from_gba_keys(UINT16_C(0x0001)) !=
        UINT16_C(0x8000))
        return 1;
    if (portable_driver_control_word_from_gba_keys(UINT16_C(0x0002)) !=
        UINT16_C(0x4000))
        return 1;
    if (portable_driver_control_word_from_gba_keys(UINT16_C(0x0010)) !=
        UINT16_C(0x0100))
        return 1;
    if (portable_driver_control_word_from_gba_keys(UINT16_C(0x0020)) !=
        UINT16_C(0x0200))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_ACCELERATE) != UINT16_C(0x8000))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_BRAKE) != UINT16_C(0x4000))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_RIGHT) != UINT16_C(0x0100))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_LEFT) != UINT16_C(0x0200))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_HOP) != UINT16_C(0x0010))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_UP) != UINT16_C(0x0800))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_DOWN) != UINT16_C(0x0400))
        return 1;
    if (portable_driver_control_word_from_semantic_buttons(
            PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_LEFT) !=
        UINT16_C(0x8200))
        return 1;

    /* 5 is the exact branch boundary where the offset cancels to zero. */
    if (portable_driver_input_tick(&state, UINT16_C(0x0800)) !=
            PORTABLE_DRIVER_INPUT_OK ||
        state.control_word != UINT16_C(0x0800) ||
        state.speed_target_q16 != INT32_C(0x00123456))
        return 1;

    state.speed_parameter = 0x10;
    if (portable_driver_input_tick(&state, UINT16_C(0x0200)) !=
            PORTABLE_DRIVER_INPUT_OK ||
        state.control_word != UINT16_C(0x0200) ||
        state.speed_target_q16 != INT32_C(0x00283456))
        return 1;

    state.speed_parameter = 4;
    if (portable_driver_input_tick(&state, UINT16_C(0x0400)) !=
            PORTABLE_DRIVER_INPUT_OK ||
        state.speed_target_q16 != INT32_C(0x00143456))
        return 1;

    state.base_speed_q16 = INT32_C(0x7fffffff);
    if (portable_driver_input_tick(&state, 0) != PORTABLE_DRIVER_INPUT_OK ||
        state.speed_target_q16 != -INT32_C(0x7ffe0001))
        return 1;

    state.base_speed_q16 = INT32_C(0x00123456);
    state.speed_parameter = 0x24;
    if (portable_driver_input_tick(&state, 0) != PORTABLE_DRIVER_INPUT_OK ||
        state.speed_target_q16 != INT32_C(0x00483456))
        return 1;

    state.speed_parameter = 0x41;
    if (portable_driver_input_tick(&state, 0) != PORTABLE_DRIVER_INPUT_OK ||
        state.speed_target_q16 != state.base_speed_q16)
        return 1;

    before_error = state;
    state.state_flags = 1;
    if (portable_driver_input_tick(&state, UINT16_C(0xffff)) !=
            PORTABLE_DRIVER_INPUT_UNSUPPORTED_FLAGS ||
        state.base_speed_q16 != before_error.base_speed_q16 ||
        state.speed_parameter != before_error.speed_parameter ||
        state.control_word != before_error.control_word ||
        state.speed_target_q16 != before_error.speed_target_q16 ||
        portable_driver_input_tick(NULL, 0) !=
            PORTABLE_DRIVER_INPUT_INVALID_ARGUMENT)
        return 1;

    puts("portable driver input: PASS");
    return 0;
}

int run_input_script(void)
{
    PortableInputFrame frames[8] = {0};
    size_t count = 0;

    if (portable_input_script_parse(
            ".ABlrLR", frames, 8, &count) !=
            PORTABLE_INPUT_SCRIPT_OK ||
        count != 7u ||
        frames[0].buttons != PORTABLE_INPUT_NONE ||
        frames[1].buttons != PORTABLE_INPUT_ACCELERATE ||
        frames[1].source_control_word != UINT16_C(0x8000) ||
        frames[2].buttons != PORTABLE_INPUT_BRAKE ||
        frames[2].source_control_word != UINT16_C(0x4000) ||
        frames[3].buttons != PORTABLE_INPUT_LEFT ||
        frames[3].source_control_word != UINT16_C(0x0200) ||
        frames[4].buttons != PORTABLE_INPUT_RIGHT ||
        frames[4].source_control_word != UINT16_C(0x0100) ||
        frames[5].buttons !=
            (PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_LEFT) ||
        frames[5].source_control_word != UINT16_C(0x8200) ||
        frames[6].buttons !=
            (PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_RIGHT) ||
        frames[6].source_control_word != UINT16_C(0x8100))
        return 1;

    if (portable_input_script_parse("A?", frames, 8, &count) !=
            PORTABLE_INPUT_SCRIPT_INVALID_TOKEN ||
        portable_input_script_parse("AAAA", frames, 3, &count) !=
            PORTABLE_INPUT_SCRIPT_CAPACITY ||
        portable_input_script_parse(NULL, frames, 8, &count) !=
            PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT ||
        portable_input_script_parse("A", NULL, 1, &count) !=
            PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT ||
        portable_input_script_parse("", NULL, 0, &count) !=
            PORTABLE_INPUT_SCRIPT_OK || count != 0u)
        return 1;

    /* Admission replaces only explicitly covered preturns, including neutral
     * input, and remains identical when Retry samples an earlier tick again. */
    PortableMeshInputSegments segments;
    memset(&segments, 0x5a, sizeof segments);
    if (portable_mesh_input_segments_parse("180:2:-32767:1;184:1:0:0;185:3:32767:5", &segments) !=
            PORTABLE_INPUT_SCRIPT_OK || segments.count != 3u)
        return 1;
    const PortableMeshInputSegments saved = segments;
    const uint32_t ticks[] = {179, 180, 181, 182, 183, 184, 185, 187, 188, 180};
    const int admitted[] = {0, 1, 1, 0, 0, 1, 1, 1, 0, 1};
    for (size_t i = 0; i < sizeof ticks / sizeof ticks[0]; ++i)
    {
        PortableMeshKartInput input = {.steering = 123, .buttons = PORTABLE_MESH_BRAKE};
        const PortableMeshKartInput previous = input;
        if (portable_mesh_input_segments_sample(&segments, ticks[i], &input) != admitted[i])
            return 1;
        if (!admitted[i])
        {
            if (memcmp(&input, &previous, sizeof input))
                return 1;
        }
        else if (ticks[i] == 184u)
        {
            if (input.steering || input.buttons)
                return 1;
        }
        else if (ticks[i] < 182u)
        {
            if (input.steering != -32767 || input.buttons != PORTABLE_MESH_ACCELERATE)
                return 1;
        }
        else if (input.steering != 32767 || input.buttons !=
                    (PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP))
            return 1;
        if (memcmp(&segments, &saved, sizeof saved))
            return 1;
    }
    /* A scoped segment affects only the chosen ordinary cup ordinal, including
     * neutral input and Retry; skipped races preserve the pilot's controls. */
    uint32_t selected_race = 99u;
    if (portable_mesh_input_race_parse(NULL, &selected_race) != PORTABLE_INPUT_SCRIPT_OK ||
        selected_race != UINT32_MAX)
        return 1;
    for (uint32_t race = 0; race < 4u; ++race)
    {
        char ordinal[] = {(char)('0' + race), 0};
        if (portable_mesh_input_race_parse(ordinal, &selected_race) != PORTABLE_INPUT_SCRIPT_OK ||
            selected_race != race)
            return 1;
        for (uint32_t current = 0; current < 4u; ++current)
        {
            PortableMeshKartInput scoped = {.steering = 123, .buttons = PORTABLE_MESH_BRAKE};
            const PortableMeshKartInput prior = scoped;
            if (portable_mesh_input_segments_sample_race(&segments, selected_race, current,
                    184u, &scoped) != (race == current))
                return 1;
            if (race == current ? (scoped.steering || scoped.buttons) :
                    memcmp(&scoped, &prior, sizeof scoped))
                return 1;
        }
    }
    static const char *const bad_races[] = {"", "4", "-1", "01", "1x", " 1", "4294967296"};
    for (size_t i = 0; i < sizeof bad_races / sizeof bad_races[0]; ++i)
        if (portable_mesh_input_race_parse(bad_races[i], &selected_race) == PORTABLE_INPUT_SCRIPT_OK ||
            selected_race != 3u)
            return 1;
    if (portable_mesh_input_race_parse("0", NULL) != PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT)
        return 1;
    PortableMeshKartInput scoped = {.steering = 123, .buttons = PORTABLE_MESH_BRAKE};
    const PortableMeshKartInput prior = scoped;
    if (portable_mesh_input_segments_sample_race(&segments, 0u, UINT32_MAX, 180u, &scoped) != -1 ||
        portable_mesh_input_segments_sample_race(&segments, 4u, 0u, 180u, &scoped) != -1 ||
        portable_mesh_input_segments_sample_race(&segments, UINT32_MAX, 4u, 180u, &scoped) != -1 ||
        memcmp(&scoped, &prior, sizeof scoped))
        return 1;
    /* Default scope also keeps ordinary TT sampling and time rollback intact. */
    if (portable_mesh_input_segments_sample_race(&segments, UINT32_MAX, UINT32_MAX, 180u, &scoped) != 1 ||
        scoped.steering != -32767 || scoped.buttons != PORTABLE_MESH_ACCELERATE ||
        portable_mesh_input_segments_sample_race(&segments, 1u, 1u, 180u, &scoped) != 1 ||
        memcmp(&segments, &saved, sizeof saved))
        return 1;
    static const char *const invalid[] = {
        "", "180:0:0:1", "180:4097:0:1", "180:4096:0:1;5000:1:0:0",
        "180:2:0:1;181:1:0:1", "181:1:0:1;180:1:0:1", "180:1:-32768:1",
        "180:1:32768:1", "180:1:0:8", "180:1:0:-1", "180:1:+1:1",
        "4294967295:1:0:1", "4294967296:1:0:1", "180:1:0:1;", "180:1:0:1x",
        "180:1:0", "180:1::1", "180:1:0:1;;181:1:0:0", " 180:1:0:1"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        if (portable_mesh_input_segments_parse(invalid[i], &segments) == PORTABLE_INPUT_SCRIPT_OK ||
            memcmp(&segments, &saved, sizeof saved))
            return 1;
    char maximum[1024] = {0};
    size_t used = 0;
    for (unsigned i = 0; i < 33u; ++i)
    {
        used += (size_t)snprintf(maximum + used, sizeof maximum - used, "%s%u:1:0:1",
                                i ? ";" : "", 180u + i);
        if (i == 31u)
        {
            if (portable_mesh_input_segments_parse(maximum, &segments) != PORTABLE_INPUT_SCRIPT_OK ||
                segments.count != 32u)
                return 1;
        }
    }
    const PortableMeshInputSegments capacity_saved = segments;
    if (portable_mesh_input_segments_parse(maximum, &segments) != PORTABLE_INPUT_SCRIPT_CAPACITY ||
        memcmp(&segments, &capacity_saved, sizeof segments))
        return 1;
    if (portable_mesh_input_segments_parse("4294967294:1:0:7", &segments) != PORTABLE_INPUT_SCRIPT_OK)
        return 1;
    PortableMeshKartInput input = {0};
    if (portable_mesh_input_segments_sample(&segments, UINT32_MAX - 1u, &input) != 1 ||
        input.buttons != 7u || portable_mesh_input_segments_sample(&segments, UINT32_MAX, &input) != 0)
        return 1;
    const PortableMeshKartInput unchanged = input;
    segments.segments[0].input.buttons = 8u;
    if (portable_mesh_input_segments_sample(&segments, 0, &input) != -1 ||
        memcmp(&input, &unchanged, sizeof input) ||
        portable_mesh_input_segments_parse(NULL, &segments) != PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT ||
        portable_mesh_input_segments_parse("180:1:0:1", NULL) != PORTABLE_INPUT_SCRIPT_INVALID_ARGUMENT)
        return 1;

    puts("portable input script: PASS");
    return 0;
}

int run_driver_acceleration(void)
{
    int16_t table[64] = {0};
    PortableDriverAccelerationState state = {
        .speed_q16 = INT32_C(0x05000000),
        .acceleration_q16 = INT32_C(0x12345678),
        .control_word = UINT16_C(0x0800),
        .driver_mode = 0,
        .character_class = 3,
    };
    PortableDriverAccelerationState before_error;
    size_t index = 3u * 8u + 5u;

    table[index] = -12;
    if (portable_driver_acceleration_tick(&state, table, 64) !=
            PORTABLE_DRIVER_ACCELERATION_OK ||
        state.acceleration_q16 != -INT32_C(0x000ba988) ||
        state.control_word != UINT16_C(0x0800))
        return 1;

    /* Every normal mode shares the same source table branch. */
    state.driver_mode = 4;
    state.speed_q16 = INT32_C(0x00000000);
    state.acceleration_q16 = INT32_C(0x0000abcd);
    state.character_class = 0xff;
    table[56] = 4;
    if (portable_driver_acceleration_tick(&state, table, 64) !=
            PORTABLE_DRIVER_ACCELERATION_OK ||
        state.acceleration_q16 != INT32_C(0x0004abcd))
        return 1;

    before_error = state;
    if (portable_driver_acceleration_tick(&state, table, 63) !=
            PORTABLE_DRIVER_ACCELERATION_TABLE_TOO_SMALL ||
        memcmp(&state, &before_error, sizeof(state)) != 0)
        return 1;
    state.driver_mode = 5;
    before_error = state;
    if (portable_driver_acceleration_tick(&state, table, 64) !=
            PORTABLE_DRIVER_ACCELERATION_UNSUPPORTED_MODE ||
        memcmp(&state, &before_error, sizeof(state)) != 0)
        return 1;
    state.driver_mode = 0;
    state.control_word = UINT16_C(0x4000);
    before_error = state;
    if (portable_driver_acceleration_tick(&state, table, 64) !=
            PORTABLE_DRIVER_ACCELERATION_UNSUPPORTED_CONTROL ||
        memcmp(&state, &before_error, sizeof(state)) != 0)
        return 1;
    if (portable_driver_acceleration_tick(NULL, table, 64) !=
            PORTABLE_DRIVER_ACCELERATION_INVALID_ARGUMENT ||
        portable_driver_acceleration_tick(&state, NULL, 64) !=
            PORTABLE_DRIVER_ACCELERATION_INVALID_ARGUMENT)
        return 1;

    puts("portable driver acceleration: PASS");
    return 0;
}

int run_driver_forward_acceleration(void)
{
    int16_t global_curve[8] = {0};
    int16_t local_table[16] = {0};
    PortableDriverForwardAccelerationState state = {
        .speed_q16 = 0,
        .response_q8 = 0,
        .state_flags = 0,
        .acceleration_q16 = 0,
    };
    PortableDriverForwardAccelerationState before_error;

    local_table[0] = 0x0900;
    if (portable_driver_forward_acceleration_tick(
            &state, global_curve, 8, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_OK ||
        state.response_q8 != 0 || state.acceleration_q16 != 0x00090000)
    {
        return 1;
    }

    /* The negative product path adds 0xffff before the arithmetic shift. */
    state.speed_q16 = 0;
    state.response_q8 = -32768;
    global_curve[0] = 32;
    local_table[0] = 0x0a00;
    if (portable_driver_forward_acceleration_tick(
            &state, global_curve, 8, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_OK ||
        state.response_q8 != INT16_C(32752) ||
        state.acceleration_q16 != 0x000a0000)
    {
        return 1;
    }

    /* The source's bit-0 branch emits the fixed 0xc000 acceleration. */
    state.state_flags = 1;
    if (portable_driver_forward_acceleration_tick(
            &state, global_curve, 8, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_OK ||
        state.acceleration_q16 != 0x0000c000)
    {
        return 1;
    }

    state.state_flags = 0;
    state.speed_q16 = INT32_C(0x03400000);
    local_table[13] = -0x1200;
    if (portable_driver_forward_acceleration_tick(
            &state, global_curve, 8, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_OK ||
        state.acceleration_q16 != -INT32_C(0x00120000))
    {
        return 1;
    }

    /* P170: the muted source A+Right fallback writer at speed 0x027e8000
     * preserves the response and selects local table entry 9 (0x0f00). */
    state.speed_q16 = INT32_C(0x027e8000);
    state.response_q8 = INT16_C(0x0900);
    state.state_flags = UINT16_C(0x0024);
    state.acceleration_q16 = INT32_C(0xffe80000);
    global_curve[0] = 0;
    local_table[9] = INT16_C(0x0f00);
    if (portable_driver_forward_acceleration_tick(
            &state, global_curve, 8, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_OK ||
        state.response_q8 != INT16_C(0x0900) ||
        state.acceleration_q16 != INT32_C(0x000f0000))
    {
        return 1;
    }

    before_error = state;
    if (portable_driver_forward_acceleration_tick(
            &state, global_curve, 7, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_GLOBAL_TABLE_TOO_SMALL ||
        memcmp(&state, &before_error, sizeof(state)) != 0 ||
        portable_driver_forward_acceleration_tick(
            &state, global_curve, 8, local_table, 15) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_LOCAL_TABLE_TOO_SMALL ||
        memcmp(&state, &before_error, sizeof(state)) != 0 ||
        portable_driver_forward_acceleration_tick(
            NULL, global_curve, 8, local_table, 16) !=
            PORTABLE_DRIVER_FORWARD_ACCELERATION_INVALID_ARGUMENT)
        return 1;

    puts("portable driver forward acceleration: PASS");
    return 0;
}
