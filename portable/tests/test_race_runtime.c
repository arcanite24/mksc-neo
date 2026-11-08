#include "item_rules_fixture.h"
#include "driver_response_fixture.h"
#include "race_runtime.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "race runtime failed line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

int main(void)
{
    const PortableRacePoint route[2] = {{100, 100}, {100, 50}};
    uint8_t cells[4] = {0};
    int16_t auto_acceleration[64] = {0};
    int32_t sine[1024] = {0};
    const PortableTrackRaster raster = {
        .cells = cells, .capacity = 4u, .width = 2u, .height = 2u,
    };
    PortableDriverPilotDependencies dependencies = {.shell_orbit_rules=test_shell_orbit_rules(),.banana_rules=test_banana_rules(),.red_shell_rules=test_red_shell_rules(),.green_shell_rules=test_green_shell_rules(),.boo_rules=test_boo_rules(),.item_status_rules=test_item_status_rules(),.mushroom_rules=test_mushroom_rules(),
        .response_rules=portable_test_response_profile(),
        .auto_acceleration_table = auto_acceleration,
        .auto_acceleration_count = 64u,
        .sine_table = sine,
        .sine_count = 1024u,
        .raster = &raster,
    };
    PortableDriverPilotState driver = {0};
    PortableRaceRulesView rules = {
        .route = route,
        .route_count = 2u,
        .start_x_q16 = 0,
        .start_y_q16 = 0,
        .start_route_index = 1u,
        .progress_kind = PORTABLE_RACE_PROGRESS_DEMO_V1,
        .finish_kind = PORTABLE_FINISH_NORTHBOUND_Y,
        .finish_y_q16 = 100 * 65536,
        .lap_count = 3u,
        .countdown_ticks = 1u,
        .rescue_penalty_ticks = 180u,
    };
    PortableRaceRuntime runtime;
    PortableRaceTickDisposition disposition;

    rules.compatibility.handling_profile_id = PORTABLE_HANDLING_DEMO_V1;
    CHECK(portable_race_runtime_init_recovered(
              &runtime, &rules, &driver, &dependencies) ==
          PORTABLE_RACE_RUNTIME_INCOMPATIBLE_PROFILE);
    rules.compatibility.handling_profile_id = PORTABLE_HANDLING_RECOVERED_V1;
    driver.pose.position_x = 1;
    CHECK(portable_race_runtime_init_recovered(
              &runtime, &rules, &driver, &dependencies) ==
          PORTABLE_RACE_RUNTIME_INCOMPATIBLE_PROFILE);
    driver.pose.position_x = 0;
    driver.source_update_order = 1u;
    CHECK(portable_race_runtime_init_recovered(
              &runtime, &rules, &driver, &dependencies) ==
          PORTABLE_RACE_RUNTIME_OK);
    CHECK(runtime.profile == PORTABLE_HANDLING_RECOVERED_V1);
    CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
          PORTABLE_RACE_RUNTIME_OK);
    CHECK(disposition == PORTABLE_RACE_TICK_COUNTDOWN);
    CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
          PORTABLE_RACE_RUNTIME_OK);
    CHECK(disposition == PORTABLE_RACE_TICK_ADVANCE_MOTION &&
          driver.pose.position_x == 0 && driver.pose.position_y == 0 &&
          driver.motion.speed_q16 == 0 &&
          driver.motion.acceleration_q16 == 0 &&
          runtime.session.elapsed_ticks == 1u);
    driver.pose.position_x = 7;
    portable_race_runtime_restart(&runtime);
    CHECK(driver.pose.position_x == 0);
    CHECK(runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN);

    /* Source checkpoint modes observe entry height, before the hop lowers
     * it across4 units, and use the active source mode row. */
    {
        const uint8_t modes[4] = {0u, 0u, 0x80u, 0u};
        PortableDriverPilotState airborne = {0};
        PortableRaceRulesView source_rules = rules;
        source_rules.progress_kind = PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
        source_rules.finish_kind = PORTABLE_FINISH_SOURCE_Y_CROSSING;
        source_rules.start_checkpoint_raw = 0x81u;
        source_rules.ai_modes = modes;
        source_rules.ai_mode_count = sizeof modes;
        airborne.source_update_order = 1u;
        airborne.pose.position_z = 4 * 65536 + 1;
        airborne.hop.height_q16 = airborne.pose.position_z;
        airborne.hop.mode = 1u;
        airborne.hop.velocity_q8 = -1;
        CHECK(portable_race_runtime_init_recovered(&runtime, &source_rules,
                  &airborne, &dependencies) == PORTABLE_RACE_RUNTIME_OK);
        runtime.source_checkpoint_mode_row = 1u;
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_OK);
        CHECK(airborne.source_progress.height_q16 == 4 * 65536 + 1 &&
              airborne.pose.position_z <= 4 * 65536 &&
              runtime.session.checkpoint_raw == 0x81u &&
              airborne.source_route_index_90 == 1u &&
              airborne.source_speed_variant_19b == 0x80u &&
              (airborne.steering.state_flags & UINT32_C(2)) != 0u);
        runtime.source_checkpoint_mode_row = 0u;
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_OK);
        CHECK(runtime.session.checkpoint_raw == 0u &&
              airborne.source_route_index_90 == 0u);
        cells[0] = 0xffu;
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_OK);
        CHECK(runtime.session.checkpoint_raw == 0u &&
              (airborne.steering.state_flags & UINT32_C(2)) != 0u);
        cells[0] = 0u;
        runtime.source_checkpoint_mode_row = 3u;
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_SESSION_ERROR);
        CHECK(runtime.last_driver_status == PORTABLE_DRIVER_PILOT_PROGRESS_ERROR);
    }

    /* Lava entered on a raster boundary must retrieve from the checkpoint
     * accepted by B98C this tick, before39F74/9828 selects the return pose. */
    {
        const PortableRacePoint targets[3] = {{20,20}, {40,40}, {60,60}};
        uint8_t map[64] = {0}, ground[256] = {0}, flags[256] = {0x24};
        uint16_t angles[16384] = {0};
        const PortableTrackRaster lava_raster = {
            .cells=map, .capacity=64u, .width=8u, .height=8u};
        const PortableTerrainView lava_terrain = {
            .cells=ground, .row_stride=16u, .width=16u, .height=16u};
        const PortableTrackMaterialView lava_material = {.flags=flags, .count=256u};
        PortableDriverPilotDependencies lava_dependencies = dependencies;
        PortableDriverPilotState kart = {0};
        PortableRaceRulesView lava_rules = rules;
        map[1u * 8u + 1u] = 1u;
        lava_dependencies.raster = &lava_raster;
        lava_dependencies.terrain = &lava_terrain;
        lava_dependencies.track_material = &lava_material;
        lava_dependencies.source_track_route = targets;
        lava_dependencies.source_track_route_count = 3u;
        lava_dependencies.source_track_angle_lut = angles;
        lava_dependencies.source_track_angle_lut_count = 16384u;
        kart.source_update_order = 1u;
        kart.source_hazard_enabled = 1u;
        kart.pose.position_x = 15 * 65536;
        kart.pose.position_y = 20 * 65536;
        kart.pose.component_x = 512;
        lava_rules.route = targets;
        lava_rules.route_count = 3u;
        lava_rules.start_route_index = 0u;
        lava_rules.start_checkpoint_raw = 0x80u;
        lava_rules.start_x_q16 = kart.pose.position_x;
        lava_rules.start_y_q16 = kart.pose.position_y;
        lava_rules.progress_kind = PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
        lava_rules.finish_kind = PORTABLE_FINISH_SOURCE_Y_CROSSING;
        CHECK(portable_race_runtime_init_recovered(&runtime, &lava_rules,
                  &kart, &lava_dependencies) == PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime, 0u, &disposition) ==
              PORTABLE_RACE_RUNTIME_OK);
        CHECK(kart.hop.mode == 3u && kart.hop.retrieval_started &&
              runtime.session.checkpoint_raw == 1u && kart.source_route_index_90 == 1u &&
              kart.recovery_target_x_9e == 40u && kart.recovery_target_y_a0 == 40u);
    }

    /* A8EE's second B98C sample follows only the Y carry leg. It accepts
     * the moved-to checkpoint/sentinel without a second clock or lap commit. */
    for(unsigned path=0u;path<3u;++path)
    {
        uint8_t map[64]={0},ground[256]={0},flags[256]={0x40};
        map[1u*8u+1u]=path==0u?0xffu:path==1u?1u:0x81u;
        const PortableTrackRaster carry_raster={map,64u,8u,8u,0u,0u};
        const PortableTerrainView carry_terrain={ground,16u,16u,16u};
        const PortableTrackMaterialView carry_material={.flags=flags,.count=256u};
        PortableDriverPilotDependencies carry_dependencies=dependencies;
        carry_dependencies.raster=&carry_raster;
        carry_dependencies.terrain=&carry_terrain;
        carry_dependencies.track_material=&carry_material;
        PortableDriverPilotState kart={0};
        kart.source_update_order=1u;kart.source_hazard_enabled=1u;
        kart.source_hazard_kind=PORTABLE_DRIVER_HAZARD_VOID;
        kart.hop.mode=6u;kart.hop.height_q16=7*65536;
        kart.pose.position_z=7*65536;
        kart.pose.position_x=20*65536;kart.pose.position_y=32*65536;
        kart.recovery_target_x_9e=20;kart.recovery_target_y_a0=0;
        kart.steering.state_flags=0x8800u;
        PortableRaceRulesView carry_rules=rules;
        carry_rules.start_x_q16=kart.pose.position_x;
        carry_rules.start_y_q16=kart.pose.position_y;
        carry_rules.start_checkpoint_raw=0x80u;
        carry_rules.start_route_index=0u;
        carry_rules.progress_kind=PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
        carry_rules.finish_kind=PORTABLE_FINISH_SOURCE_Y_CROSSING;
        carry_rules.finish_y_q16=30*65536;
        CHECK(portable_race_runtime_init_recovered(&runtime,&carry_rules,
            &kart,&carry_dependencies)==PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime,0u,&disposition)==PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime,0u,&disposition)==PORTABLE_RACE_RUNTIME_OK);
        CHECK(kart.pose.position_y==29*65536 && kart.hop.recovery_checkpoint_sample &&
            kart.source_progress.checkpoint_resample && !runtime.session.motion_pending &&
            runtime.session.elapsed_ticks==1u && runtime.session.laps_completed==0u &&
            runtime.session.checkpoint_raw==(path==0u?0u:1u) &&
            kart.source_route_index_90==(path==0u?0u:1u) &&
            (kart.steering.state_flags&3u)==(path==0u?2u:0u));
    }

    for(unsigned native=0u;native<2u;++native)
    {
        /* An airborne driver over22 has not entered a ground water state.
         * Native ownership must still prevent a second legacy handoff. */
        uint8_t map[64]={0},ground[256]={0},flags[256]={0x22};
        const PortableTrackRaster water_raster={.cells=map,.capacity=64u,.width=8u,.height=8u};
        const PortableTerrainView terrain={.cells=ground,.row_stride=16u,.width=16u,.height=16u};
        const PortableTrackMaterialView material={.flags=flags,.count=256u};
        PortableDriverPilotDependencies water_dependencies=dependencies;
        water_dependencies.raster=&water_raster;water_dependencies.terrain=&terrain;
        water_dependencies.track_material=&material;
        PortableDriverPilotState kart={0};
        kart.source_update_order=1u;kart.source_terrain22_enabled=(uint8_t)native;
        kart.pose.position_x=80*65536;kart.pose.position_y=80*65536;
        kart.pose.position_z=6*65536;kart.hop.height_q16=kart.pose.position_z;
        kart.hop.mode=1u;
        PortableRaceRulesView water_rules=rules;
        water_rules.start_x_q16=kart.pose.position_x;water_rules.start_y_q16=kart.pose.position_y;
        CHECK(portable_race_runtime_init_recovered(&runtime,&water_rules,&kart,&water_dependencies)==
            PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime,0u,&disposition)==PORTABLE_RACE_RUNTIME_OK);
        CHECK(portable_race_runtime_tick(&runtime,0u,&disposition)==PORTABLE_RACE_RUNTIME_OK);
        CHECK(kart.hop.mode==1u && !kart.source_terrain22_active &&
            runtime.water_entry==!native && runtime.session.elapsed_ticks==1u);
    }

    /* A terrain response changes speed after8200 has created this turn's
     * components. The next pose must consume that incoming-speed vector,
     * including the mode4 countdown instead of a stationary rescue. */
    {
        uint8_t map[64]={0}, terrain_cells[256]={0}, material_flags[256]={0};
        const PortableTrackRaster terrain_raster={
            .cells=map,.capacity=64u,.width=8u,.height=8u};
        const PortableTerrainView terrain_view={
            .cells=terrain_cells,.row_stride=16u,.width=16u,.height=16u};
        const PortableTrackMaterialView material_view={
            .flags=material_flags,.count=256u};
        PortableDriverPilotDependencies terrain_dependencies=dependencies;
        PortableDriverPilotState kart={0};
        material_flags[0]=UINT8_C(0x22);
        sine[32]=8192; sine[224]=24576; sine[256]=32768;
        terrain_dependencies.raster=&terrain_raster;
        terrain_dependencies.terrain=&terrain_view;
        terrain_dependencies.track_material=&material_view;
        kart.source_update_order=1u;
        kart.source_terrain22_enabled=1u;
        kart.preserve_source_base_speed=1u;
        kart.input.base_speed_q16=600*65536;
        kart.pose.position_x=80*65536; kart.pose.position_y=80*65536;
        kart.pose.component_y=-5;
        kart.motion.speed_q16=600*65536;
        kart.motion.acceleration_q16=32*65536;
        kart.steering.state_flags=UINT32_C(0x8000);
        CHECK(portable_driver_pilot_tick(&kart,UINT16_C(0x8000),&terrain_dependencies)==
              PORTABLE_DRIVER_PILOT_OK);
        CHECK(kart.pose.position_y==80*65536-5*256 &&
              kart.motion.component_y==-632 && kart.pose.component_y==-632 &&
              kart.motion.speed_q16==354*65536 && kart.motion.acceleration_q16==0 &&
              kart.hop.mode==1u && kart.hop.velocity_q8==118);
        const int32_t prior_y=kart.pose.position_y;
        CHECK(portable_driver_pilot_tick(&kart,UINT16_C(0x8000),&terrain_dependencies)==
              PORTABLE_DRIVER_PILOT_OK);
        CHECK(kart.pose.position_y==prior_y-632*256 &&
              kart.motion.component_y==-354 && kart.pose.position_z==92*256);
        kart.hop.mode=4u; kart.hop.height_q16=0; kart.pose.position_z=0;
        kart.drift_charge.charge_counter=144u;
        kart.motion.speed_q16=7*65536;
        kart.motion.acceleration_q16=65536+UINT16_C(0x1234);
        kart.pose.component_y=-7;
        const int32_t recovery_y=kart.pose.position_y;
        CHECK(portable_driver_pilot_tick(&kart,UINT16_C(0x8000),&terrain_dependencies)==
              PORTABLE_DRIVER_PILOT_OK);
        CHECK(kart.drift_charge.charge_counter==143u &&
              kart.pose.position_y==recovery_y-7*256 &&
              kart.motion.component_y==-8 &&
              kart.motion.acceleration_q16==65536+UINT16_C(0x1234));
    }

    puts("portable race runtime: PASS");
    return 0;
}
