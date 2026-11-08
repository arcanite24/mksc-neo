#include "race_runtime.h"

#include <string.h>

#include "driver_material.h"
#include "driver_angle_callbacks.h"

PortableRaceRuntimeStatus portable_race_runtime_init_recovered(
    PortableRaceRuntime *runtime, const PortableRaceRulesView *rules,
    PortableDriverPilotState *driver,
    const PortableDriverPilotDependencies *dependencies)
{
    PortableRaceRuntime next = {0};

    if (runtime == NULL || rules == NULL || driver == NULL ||
        dependencies == NULL || dependencies->raster == NULL)
        return PORTABLE_RACE_RUNTIME_INVALID_ARGUMENT;
    if (rules->compatibility.handling_profile_id !=
        PORTABLE_HANDLING_RECOVERED_V1)
        return PORTABLE_RACE_RUNTIME_INCOMPATIBLE_PROFILE;
    if (driver->pose.position_x != rules->start_x_q16 ||
        driver->pose.position_y != rules->start_y_q16 ||
        (uint16_t)driver->motion.angle != rules->start_heading)
        return PORTABLE_RACE_RUNTIME_INCOMPATIBLE_PROFILE;
    if (!portable_race_session_init(&next.session, rules))
        return PORTABLE_RACE_RUNTIME_SESSION_ERROR;
    next.profile = PORTABLE_HANDLING_RECOVERED_V1;
    next.recovered_driver = driver;
    next.recovered_initial_state = *driver;
    next.recovered_dependencies = dependencies;
    next.source_checkpoint_modes = rules->ai_modes;
    next.source_checkpoint_mode_count = rules->ai_mode_count;
    next.finished_inputs=(PortableDriverFinishedInputs){
        .alternate_route=rules->gp_route_rows,
        .alternate_route_count=rules->gp_route_row_count?rules->route_count:0u,
        .ai_modes=rules->ai_modes,.ai_mode_count=rules->ai_mode_count};
    *runtime = next;
    return PORTABLE_RACE_RUNTIME_OK;
}

/* Legacy minimal motion fixtures/packs lack the autonomous source tables.
 * Full imported motion profiles supply them through the content layer. */
static int finished_owner_available(const PortableRaceRuntime *runtime)
{
    const PortableDriverPilotDependencies *d=runtime->recovered_dependencies;
    return d && runtime->recovered_driver && !runtime->recovered_driver->battle_enabled &&
        d->source_angle_target_table &&
        d->source_angle_target_table_count>=PORTABLE_DRIVER_ANGLE_TARGET_TABLE_BYTES &&
        d->source_track_route && d->source_track_route_count &&
        d->source_track_angle_lut && d->source_track_angle_lut_count &&
        d->source_auto_deceleration_table && d->source_auto_deceleration_count>=8u;
}

static int observe_source_progress(void *context,
                                   PortableRaceMotionObservation *motion)
{
    PortableRaceRuntime *runtime = context;
    const size_t checkpoint = motion->track_cell & UINT8_C(0x7f);
    if (motion->track_cell_valid && checkpoint != UINT8_C(0x7f) &&
        (motion->track_cell & UINT8_C(0x80)) == 0u &&
        runtime->source_checkpoint_modes != NULL)
    {
        const size_t slot = (size_t)runtime->source_checkpoint_mode_row *
                            runtime->session.route_count + checkpoint;
        if (slot >= runtime->source_checkpoint_mode_count)
            return 0;
        motion->checkpoint_mode = runtime->source_checkpoint_modes[slot];
    }
    const int was_finished=runtime->session.phase==PORTABLE_RACE_PHASE_FINISHED;
    if (!(was_finished?
        portable_race_session_update_finished_checkpoint(&runtime->session,motion):
        (motion->checkpoint_resample ?
         portable_race_session_resample_checkpoint(&runtime->session,motion) :
         portable_race_session_commit_motion(&runtime->session,motion))))return 0;
    if(!was_finished && runtime->session.phase==PORTABLE_RACE_PHASE_FINISHED)
        portable_driver_pilot_retire_selected(runtime->recovered_driver);
    /* B98C writes +19B even when the height gate retains the old +90.
     * C1BC's later acceleration lookup consumes the current mode. */
    runtime->recovered_driver->source_speed_variant_19b = motion->checkpoint_mode;
    /* 9828's same-tick rescue selection reads the newly accepted +90. */
    runtime->recovered_driver->source_route_index_90 =
        runtime->session.checkpoint_raw & UINT8_C(0x7f);
    return 1;
}

PortableRaceRuntimeStatus portable_race_runtime_tick(
    PortableRaceRuntime *runtime, uint16_t source_control_word,
    PortableRaceTickDisposition *disposition)
{
    PortableRaceTickDisposition tick;
    PortableRaceMotionObservation motion = {0};
    PortableDriverMaterialState material = {0};
    int material_status = PORTABLE_DRIVER_MATERIAL_OK;
    int driver_status;

    if (runtime == NULL || disposition == NULL ||
        runtime->profile != PORTABLE_HANDLING_RECOVERED_V1 ||
        runtime->recovered_driver == NULL ||
        runtime->recovered_dependencies == NULL)
        return PORTABLE_RACE_RUNTIME_INVALID_ARGUMENT;
    tick = portable_race_session_begin_tick(&runtime->session);
    runtime->boundary_exit = 0u;
    runtime->water_entry = 0u;
    *disposition = tick;
    if (tick == PORTABLE_RACE_TICK_INVALID)
        return PORTABLE_RACE_RUNTIME_SESSION_ERROR;
    const int finished=finished_owner_available(runtime) && tick==PORTABLE_RACE_TICK_FINISHED &&
        (runtime->recovered_driver->steering.state_flags&UINT32_C(0x2000))!=0u;
    if(tick!=PORTABLE_RACE_TICK_ADVANCE_MOTION && !finished)
        return PORTABLE_RACE_RUNTIME_OK;

    motion.old_x_q16 = runtime->recovered_driver->pose.position_x;
    motion.old_y_q16 = runtime->recovered_driver->pose.position_y;
    PortableDriverPilotDependencies dependencies = *runtime->recovered_dependencies;
    dependencies.course_props=runtime->course_props;
    dependencies.piranhas=runtime->piranhas;
    const int source_progress = runtime->session.progress_kind !=
                                PORTABLE_RACE_PROGRESS_DEMO_V1;
    if (source_progress)
    {
        dependencies.observe_progress = observe_source_progress;
        dependencies.progress_context = runtime;
    }
    runtime->finished_inputs.ai_mode_row=runtime->source_checkpoint_mode_row;
    driver_status = finished?
        portable_driver_pilot_finished_tick(runtime->recovered_driver,&dependencies,&runtime->finished_inputs):
        portable_driver_pilot_tick_with_finished(runtime->recovered_driver,source_control_word,
            &dependencies,finished_owner_available(runtime)?&runtime->finished_inputs:NULL);
    runtime->last_driver_status = driver_status;
    if (driver_status == PORTABLE_DRIVER_PILOT_PROGRESS_ERROR)
        return PORTABLE_RACE_RUNTIME_SESSION_ERROR;
    if (driver_status != PORTABLE_DRIVER_PILOT_OK)
        return PORTABLE_RACE_RUNTIME_DRIVER_ERROR;
    motion.new_x_q16 = runtime->recovered_driver->pose.position_x;
    motion.new_y_q16 = runtime->recovered_driver->pose.position_y;
    motion.track_cell = 0u;
    motion.track_cell_valid = 0u;

    if (runtime->recovered_dependencies->track_material != NULL &&
        runtime->recovered_dependencies->terrain != NULL)
    {
        material_status = portable_driver_material_sample(
            runtime->recovered_dependencies->track_material,
            runtime->recovered_dependencies->raster,
            runtime->recovered_dependencies->terrain,
            motion.new_x_q16, motion.new_y_q16, &material);
        runtime->last_material_status = material_status;
        if (material_status == PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS)
        {
            /* Native hazards own the finite-map return as well as26 tiles.
             * Airborne drivers land before the ground-only entry dispatch. */
            motion.drivable = 0u;
            runtime->boundary_exit = (uint8_t)!runtime->recovered_driver->source_hazard_enabled;
        }
        else if (material_status != PORTABLE_DRIVER_MATERIAL_OK)
            return PORTABLE_RACE_RUNTIME_MATERIAL_ERROR;
        else
        {
            runtime->last_material_flags = material.material_flags;
            runtime->last_material_flags_valid =
                material.material_flags_valid;
            /* Airborne native22 skips ground entry but retains ownership.
             * Only legacy drivers hand water to an application adapter. */
            runtime->water_entry = (uint8_t)(
                material.material_flags_valid &&
                material.material_flags == UINT8_C(0x22) &&
                !runtime->recovered_driver->source_terrain22_enabled &&
                !runtime->recovered_driver->source_hazard_kind);
            motion.drivable = (uint8_t)(!material.off_track &&
                                        !runtime->water_entry &&
                                        !runtime->recovered_driver->source_hazard_kind &&
                                        !(runtime->recovered_driver->source_terrain22_active &&
                                          runtime->recovered_driver->hop.mode >= 3u));
            motion.track_cell = material.track_cell;
            motion.track_cell_valid = material.track_cell_valid;
        }
    }
    else
        motion.drivable = runtime->recovered_driver->pose.track_cell_valid;

    if (!source_progress && !finished &&
        !portable_race_session_commit_motion(&runtime->session, &motion))
        return PORTABLE_RACE_RUNTIME_SESSION_ERROR;
    return PORTABLE_RACE_RUNTIME_OK;
}

void portable_race_runtime_restart(PortableRaceRuntime *runtime)
{
    if (runtime == NULL || runtime->profile != PORTABLE_HANDLING_RECOVERED_V1 ||
        runtime->recovered_driver == NULL)
        return;
    const PortableDriverPilotDependencies *dependencies = runtime->recovered_dependencies;
    if (dependencies != NULL && dependencies->owned_terrain_cells != NULL &&
        dependencies->initial_terrain_cells != NULL)
        memcpy(dependencies->owned_terrain_cells, dependencies->initial_terrain_cells,
               dependencies->owned_terrain_capacity);
    *runtime->recovered_driver = runtime->recovered_initial_state;
    runtime->last_driver_status = PORTABLE_DRIVER_PILOT_OK;
    runtime->last_material_status = PORTABLE_DRIVER_MATERIAL_OK;
    runtime->boundary_exit = 0u;
    runtime->water_entry = 0u;
    runtime->last_material_flags = 0u;
    runtime->last_material_flags_valid = 0u;
    portable_race_session_restart(&runtime->session);
}
