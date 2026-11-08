#include "recovered_race_smoke.h"

#include "recovered_time_trial_session.h"
#include "time_trial_ghost_playback.h"
#include "driver_vector_effect.h"
#include "route_geometry.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    RECOVERED_SMOKE_MAX_TICKS = 60000u,
    RECOVERED_SMOKE_STALL_TICKS = 10000u,
};

uint16_t portable_recovered_modern_drift_aid(uint16_t buttons,int enabled,
    const PortableDriverPilotState *driver,const PortableModernDriftRules *rules)
{
    if(!enabled || driver==NULL || rules==NULL)return buttons;
    const unsigned tier=driver->charged_exit_count%3u;
    buttons &= (uint16_t)~PORTABLE_INPUT_HOP;
    if(driver->modern_drift.boost_ticks==0u &&
       driver->modern_drift.charge_ticks<rules->charge_ticks[tier] &&
       (buttons&(PORTABLE_INPUT_LEFT|PORTABLE_INPUT_RIGHT))!=0u)
        buttons |= PORTABLE_INPUT_HOP;
    return buttons;
}

uint16_t portable_recovered_modern_air_aid(uint16_t buttons,int enabled,
    const PortableDriverPilotState *driver,const PortableModernDriftRules *rules)
{
    if(!enabled || driver==NULL || rules==NULL || rules->air.launch_mask==0u ||
        (enabled==2 && driver->charged_exit_count!=0u))return buttons;
    buttons &= (uint16_t)~PORTABLE_INPUT_HOP;
    if(driver->hop.mode==0u && driver->modern_air.boost_ticks==0u &&
        driver->input.speed_target_q16>0 && (int64_t)driver->motion.speed_q16*256 >=
        (int64_t)driver->input.speed_target_q16*rules->air.minimum_speed_ratio_256)
        buttons |= PORTABLE_INPUT_HOP;
    else if(driver->hop.mode==1u && driver->modern_air.armed && !driver->modern_air.tricked &&
        driver->modern_air.air_ticks>=rules->air.minimum_air_ticks && !driver->modern_air.previous_hop)
        buttons |= PORTABLE_INPUT_HOP;
    return buttons;
}

uint16_t portable_recovered_race_smoke_control(const PortableRaceRuntime *runtime)
{
    const PortableDriverPilotState *driver = runtime->recovered_driver;
    /* Source targets already point ahead of their raster band. Follow the
     * accepted checkpoint's target, as BC84 does; adding one skips a turn.
     * The geometric route keeps its explicit next-point cursor. */
    const PortableRacePoint target =
        runtime->session
            .route[runtime->session.progress_kind == PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1
                       ? (runtime->session.checkpoint_raw & UINT8_C(0x7f))
                       : runtime->session.next_route_index];
    const uint16_t target_angle =
        portable_route_bearing(target.x - driver->pose.position_x / INT32_C(65536),
                               target.y - driver->pose.position_y / INT32_C(65536));
    int32_t delta = (int32_t)target_angle - (uint16_t)driver->motion.angle;
    const uint32_t speed = driver->motion.speed_q16 < 0 ? 0u - (uint32_t)driver->motion.speed_q16
                                                        : (uint32_t)driver->motion.speed_q16;
    uint16_t result = UINT16_C(0x8000);

    if (delta > 32767)
        delta -= 65536;
    if (delta < -32768)
        delta += 65536;
    /* Response q8 contributes response/8 heading units per tick and decays
     * gradually. Countersteer before the bearing crosses the target: sixteen
     * ticks of the current response is a short input-only anticipation window.
     * Without it the aid oscillates across narrow ramps at lower speeds. */
    int32_t steering_delta=delta-driver->steering.response_q8*2;
    if (steering_delta>32767) steering_delta-=65536;
    if (steering_delta< -32768) steering_delta+=65536;
    if (steering_delta > 250)
        result |= UINT16_C(0x0100);
    if (steering_delta < -250)
        result |= UINT16_C(0x0200);
    /* Slow hairpins retain A below256 to enter the source A+B turn. */
    if (delta > 7000 || delta < -7000)
    {
        result |= UINT16_C(0x4000);
        if (speed > UINT32_C(0x01000000))
            result &= (uint16_t)~UINT16_C(0x8000);
    }
    /* 38FE4 keeps an airborne turn active under braking with R above448.
     * Avoid adding ground hops; this aid changes input only. */
    if ((result & UINT16_C(0x4000)) != 0u && driver->hop.mode == 1u &&
        speed >= UINT32_C(0x01c00000))
        result |= UINT16_C(0x0010);
    return result;
}

static void capture_report(PortableRecoveredSmokeReport *report,
                           PortableRecoveredSmokeStatus status,
                           const PortableRaceRuntime *runtime,
                           uint32_t scheduled_ticks,
                           uint32_t last_progress_tick)
{
    const PortableDriverPilotState *driver = runtime->recovered_driver;
    *report = (PortableRecoveredSmokeReport){
        .status = status,
        .runtime_status = status == PORTABLE_RECOVERED_SMOKE_RUNTIME_ERROR
            ? PORTABLE_RACE_RUNTIME_DRIVER_ERROR : PORTABLE_RACE_RUNTIME_OK,
        .driver_status = runtime->last_driver_status,
        .scheduled_ticks = scheduled_ticks,
        .race_ticks = portable_race_session_total_ticks(&runtime->session),
        .last_progress_tick = last_progress_tick,
        .laps_completed = runtime->session.laps_completed,
        .angle = (uint16_t)driver->motion.angle,
        .route_index = (uint32_t)runtime->session.next_route_index,
        .position_x = driver->pose.position_x,
        .position_y = driver->pose.position_y,
        .speed_q16 = driver->motion.speed_q16,
        .track_cell_valid = driver->pose.track_cell_valid,
        .checkpoint_raw = runtime->session.checkpoint_raw,
        .lap_raw = runtime->session.lap_raw,
        .finish_armed = runtime->session.finish_armed,
        .checkpoint_next_index = runtime->session.checkpoint_next_index,
        .max_progress_raw = runtime->session.max_progress_raw,
    };
}

static PortableRecoveredSmokeStatus recovered_race_smoke_run(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    PortableRecoveredSmokeReport *report,
    uint8_t *out_ghost)
{
    PortableRecoveredRaceView race;
    PortableRecoveredTimeTrialSession *time_trial;
    size_t last_route_index;
    uint32_t last_progress_tick = 0u;
    FILE *trace = NULL;
    const char *trace_path = getenv("MKSC_RECOVERED_SMOKE_TRACE_PATH");

    if (source == NULL || driver == NULL ||
        report == NULL)
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    *report = (PortableRecoveredSmokeReport){0};
    race = *source;
    race.initial_driver = *driver;
    time_trial = malloc(sizeof *time_trial);
    if (time_trial == NULL)
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    if (portable_recovered_time_trial_init(time_trial, &race) !=
        PORTABLE_RECOVERED_TIME_TRIAL_OK) {
        free(time_trial);
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    }
    if (trace_path != NULL && trace_path[0] != '\0')
    {
        trace = fopen(trace_path, "wx");
        if (trace == NULL || fprintf(trace,
                "tick,phase,x,y,speed,heading,flags,route,lap,checkpoint,material,timer,input,hop,height,driver_mode,response,contact_state,contact_direction,hold_timer,component_x,component_y,wall_mutations,wall_cell\n") < 0)
        {
            if (trace != NULL)
                fclose(trace);
            free(time_trial);
            return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
        }
    }

    last_route_index = time_trial->runtime.session.next_route_index;
    for (uint32_t tick = 0u; tick < RECOVERED_SMOKE_MAX_TICKS; ++tick)
    {
        PortableRaceTickDisposition disposition;
        const uint16_t input =
            time_trial->runtime.session.phase == PORTABLE_RACE_PHASE_RACING
                ? portable_recovered_race_smoke_control(&time_trial->runtime)
                : 0u;
        const PortableRecoveredTimeTrialStatus session_status =
            portable_recovered_time_trial_tick_source(
                time_trial, input, &disposition);

        if (trace != NULL)
        {
            const PortableDriverPilotState *current =
                time_trial->runtime.recovered_driver;
            if (fprintf(trace, "%u,%u,%d,%d,%d,%u,%u,%zu,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u,%u,%u,%d,%d,%u,%u\n",
                        tick + 1u, time_trial->runtime.session.phase,
                        current->pose.position_x, current->pose.position_y,
                        current->motion.speed_q16,
                        (uint16_t)current->motion.angle,
                        current->steering.response_flags,
                        time_trial->runtime.session.next_route_index,
                        time_trial->runtime.session.laps_completed,
                        time_trial->runtime.session.checkpoint_raw,
                        current->source_material_85,
                        current->normal_collision_timer_38, input, current->hop.mode,
                        current->pose.position_z, current->steering.driver_mode,
                        current->steering.response_status, current->vector_effect_source_state,
                        current->vector_effect_source_direction, current->normal_collision_timer_62,
                        current->motion.component_x, current->motion.component_y,
                        current->wall_mutation_count, current->wall_mutation_cell) < 0)
            {
                fclose(trace);
                free(time_trial);
                return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
            }
        }

        if (session_status != PORTABLE_RECOVERED_TIME_TRIAL_OK)
        {
            fprintf(stderr, "course %u Time Trial tick %u: session=%u driver=%d hop=%u material=%02x requests=%04x Lakitu=%u/%u\n",
                time_trial->runtime.session.compatibility.course_id, tick,
                (unsigned)session_status, time_trial->runtime.last_driver_status,
                time_trial->driver.hop.mode, time_trial->driver.source_material_85,
                time_trial->driver.hop.recovery_requests, time_trial->hazard_lakitu.phase,
                (unsigned)time_trial->hazard_lakitu.callback);

            capture_report(report, PORTABLE_RECOVERED_SMOKE_RUNTIME_ERROR,
                           &time_trial->runtime, tick + 1u,
                           last_progress_tick);
            report->runtime_status = time_trial->last_runtime_status;
            *driver = time_trial->driver;
            if (trace != NULL)
                fclose(trace);
            free(time_trial);
            return report->status;
        }
        if (time_trial->runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED)
        {
            capture_report(report, PORTABLE_RECOVERED_SMOKE_OK,
                           &time_trial->runtime, tick + 1u,
                           last_progress_tick);
            if (out_ghost != NULL &&
                !portable_recovered_time_trial_ghost_candidate(
                    time_trial, out_ghost))
            {
                capture_report(report,
                               PORTABLE_RECOVERED_SMOKE_RUNTIME_ERROR,
                               &time_trial->runtime, tick + 1u,
                               last_progress_tick);
                *driver = time_trial->driver;
                if (trace != NULL)
                    fclose(trace);
                free(time_trial);
                return report->status;
            }
            *driver = time_trial->driver;
            if (trace != NULL)
                fclose(trace);
            free(time_trial);
            return report->status;
        }
        if (time_trial->runtime.session.next_route_index != last_route_index)
        {
            last_route_index = time_trial->runtime.session.next_route_index;
            last_progress_tick = tick;
        }
        if (tick - last_progress_tick >= RECOVERED_SMOKE_STALL_TICKS)
        {
            capture_report(report, PORTABLE_RECOVERED_SMOKE_PROGRESS_BLOCK,
                           &time_trial->runtime, tick + 1u,
                           last_progress_tick);
            *driver = time_trial->driver;
            if (trace != NULL)
                fclose(trace);
            free(time_trial);
            return report->status;
        }
    }
    capture_report(report, PORTABLE_RECOVERED_SMOKE_TIMEOUT,
                   &time_trial->runtime, RECOVERED_SMOKE_MAX_TICKS,
                   last_progress_tick);
    *driver = time_trial->driver;
    if (trace != NULL)
        fclose(trace);
    free(time_trial);
    return report->status;
}

PortableRecoveredSmokeStatus portable_recovered_race_smoke_run(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    PortableRecoveredSmokeReport *report)
{
    return recovered_race_smoke_run(
        source, driver, report, NULL);
}

PortableRecoveredSmokeStatus portable_recovered_race_smoke_run_with_ghost(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    PortableRecoveredSmokeReport *report,
    uint8_t out_ghost[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    if (out_ghost == NULL)
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    return recovered_race_smoke_run(
        source, driver, report, out_ghost);
}

PortableRecoveredSmokeStatus portable_recovered_race_ghost_playback_smoke(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    const uint8_t ghost[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    PortableRecoveredSmokeReport *report)
{
    PortableRecoveredRaceView race;
    PortableTimeTrialGhostPlayback *playback;
    uint32_t last_progress_tick = 0u;
    size_t last_route_index;

    if (source == NULL || driver == NULL ||
        ghost == NULL || report == NULL)
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    race = *source;
    race.initial_driver = *driver;
    playback = malloc(sizeof *playback);
    if (playback == NULL)
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    if (portable_time_trial_ghost_playback_init(
            playback, &race, ghost) !=
        PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK) {
        free(playback);
        return PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT;
    }
    last_route_index = playback->session.runtime.session.next_route_index;

    for (uint32_t tick = 0u; tick < RECOVERED_SMOKE_MAX_TICKS; ++tick)
    {
        PortableRaceTickDisposition disposition;
        const PortableTimeTrialGhostPlaybackStatus status =
            portable_time_trial_ghost_playback_tick(playback, &disposition);
        if (status != PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK)
        {
            capture_report(report, PORTABLE_RECOVERED_SMOKE_RUNTIME_ERROR,
                           &playback->session.runtime, tick + 1u,
                           last_progress_tick);
            *driver = playback->session.driver;
            free(playback);
            return report->status;
        }
        if (playback->session.runtime.session.phase ==
            PORTABLE_RACE_PHASE_FINISHED)
        {
            capture_report(report, PORTABLE_RECOVERED_SMOKE_OK,
                           &playback->session.runtime, tick + 1u,
                           last_progress_tick);
            *driver = playback->session.driver;
            free(playback);
            return report->status;
        }
        if (playback->session.runtime.session.next_route_index !=
            last_route_index)
        {
            last_route_index =
                playback->session.runtime.session.next_route_index;
            last_progress_tick = tick;
        }
        if (tick - last_progress_tick >= RECOVERED_SMOKE_STALL_TICKS)
        {
            capture_report(report, PORTABLE_RECOVERED_SMOKE_PROGRESS_BLOCK,
                           &playback->session.runtime, tick + 1u,
                           last_progress_tick);
            *driver = playback->session.driver;
            free(playback);
            return report->status;
        }
    }
    capture_report(report, PORTABLE_RECOVERED_SMOKE_TIMEOUT,
                   &playback->session.runtime, RECOVERED_SMOKE_MAX_TICKS,
                   last_progress_tick);
    *driver = playback->session.driver;
    free(playback);
    return report->status;
}

const char *portable_recovered_smoke_status_name(
    PortableRecoveredSmokeStatus status)
{
    static const char *const names[] = {
        "ok", "invalid_argument", "runtime_error", "progress_block", "timeout"
    };
    return (unsigned)status < sizeof names / sizeof names[0]
        ? names[status] : "invalid_status";
}

uint16_t portable_gp_race_smoke_control(const PortableGpRaceSession *session,uint32_t ticks)
{
    if(session==NULL)return PORTABLE_INPUT_NONE;
    /* Holding A through the lights enters 40E8C's over-rev penalty. Wait for
     * GO instead of making every automated cup start with a slow launch. */
    if(session->runtime.session.phase==PORTABLE_RACE_PHASE_COUNTDOWN)
        return PORTABLE_INPUT_NONE;
    const uint16_t source=portable_recovered_race_smoke_control(&session->runtime);
    uint16_t buttons=0u;
    if(source&0x8000u)buttons|=PORTABLE_INPUT_ACCELERATE;
    if(source&0x4000u)buttons|=PORTABLE_INPUT_BRAKE;
    if(source&0x0200u)buttons|=PORTABLE_INPUT_LEFT;
    if(source&0x0100u)buttons|=PORTABLE_INPUT_RIGHT;
    if(source&0x0010u)buttons|=PORTABLE_INPUT_HOP;
    const uint16_t held=session->roster.racers[session->local_index].item_inventory.held_item_17a;
    if ((session->roster.racers[session->local_index].item_use_state_17c || held==UINT16_C(0x1001) || held==UINT16_C(0x1003) || held==UINT16_C(0x1000) || held==UINT16_C(0x1002) || held==UINT16_C(0x1004) || held==UINT16_C(0x1005) || held==UINT16_C(0x1006) || held==UINT16_C(0x100a) || held==UINT16_C(0x100b) || held==UINT16_C(0x100c) ||
     (held>=UINT16_C(0x1007) && held<=UINT16_C(0x1009)) ||
     held==UINT16_C(0x100e) || held==UINT16_C(0x100f)) &&
        (ticks&1u)==0u)
        buttons|=PORTABLE_INPUT_ITEM;
    if(session->roster.racers[session->local_index].banana.phase==PORTABLE_GP_BANANA_HELD &&
       !(buttons&PORTABLE_INPUT_ITEM) && (session->roster.item_uses&1u))
        buttons|=PORTABLE_INPUT_UP;
    return buttons;
}

uint16_t portable_battle_local_aid(const PortableGpRaceSession *session,
    PortableBattleNavWorkspace *work,uint32_t ticks)
{
    if(session==NULL||work==NULL||session->runtime.session.phase!=PORTABLE_RACE_PHASE_RACING)
        return PORTABLE_INPUT_NONE;
    const size_t self=session->local_index;
    const PortableGpRacer *me=&session->roster.racers[self];
    if(me->balloons_remaining==0u||(me->source_flags&UINT32_C(0x20))!=0u)return PORTABLE_INPUT_NONE;
    /* Like the CPU intent: fetch a box while empty-handed, then hunt. */
    const int armed=me->item_inventory.held_item_17a!=0u||me->item_use_state_17c!=0u||
        me->shell_item_18c!=0u||me->item_inventory.active;
    PortableBattleNavPoint goals[PORTABLE_BATTLE_NAV_GOALS];
    size_t count=0u;
    const PortableBattleNavPoint start={me->position_x_q16/65536,me->position_y_q16/65536};
    for(size_t i=0u;!armed&&i<session->item_box_count&&count<PORTABLE_BATTLE_NAV_GOALS;++i){
        const PortableGpItemBoxState *box=&session->item_boxes[i];
        if(box->active)
            goals[count++]=(PortableBattleNavPoint){box->placement.position_x_q16/65536,box->placement.position_y_q16/65536};
    }
    int64_t best=INT64_MAX;PortableBattleNavPoint rival=start;
    for(size_t i=0u;i<portable_gp_race_roster_count(&session->roster);++i){
        const PortableGpRacer *other=&session->roster.racers[i];
        if(i==self||other->balloons_remaining==0u)continue;
        const PortableBattleNavPoint at={other->position_x_q16/65536,other->position_y_q16/65536};
        const int64_t dx=at.x-start.x,dy=at.y-start.y;
        if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;rival=at;}
        if((armed||count==0u)&&count<PORTABLE_BATTLE_NAV_GOALS)goals[count++]=at;
    }
    PortableBattleNavPoint target=armed?rival:count!=0u?goals[0]:start;
    uint16_t buttons=PORTABLE_INPUT_ACCELERATE;
    const PortableDriverPilotDependencies *deps=&session->player_dependencies;
    PortableBattleNavRoute route={0};
    if(count!=0u&&session->battle_item_rules!=NULL&&deps->terrain!=NULL&&
       portable_battle_navigation_route(work,deps->terrain,deps->track_material,
           &session->battle_item_rules->ai,start,goals,count,&route)&&route.reachable)
        target=route.waypoint;
    uint16_t angle,difference=0x8000u;
    if(portable_driver_vector_effect_source_angle((int16_t)(target.x-start.x),
           (int16_t)(target.y-start.y),&angle)==PORTABLE_DRIVER_VECTOR_EFFECT_OK){
        difference=(uint16_t)(angle-(uint16_t)session->player.motion.angle);
        if(difference>=0x0600u&&difference<0x8000u)buttons|=PORTABLE_INPUT_RIGHT;
        else if(difference>=0x8000u&&difference<=0xfa00u)buttons|=PORTABLE_INPUT_LEFT;
    }
    /* Fire when lined up on a nearby rival, or eventually anyway. */
    uint16_t aim=0x8000u;
    if(portable_driver_vector_effect_source_angle((int16_t)(rival.x-start.x),(int16_t)(rival.y-start.y),&aim)==
       PORTABLE_DRIVER_VECTOR_EFFECT_OK)aim=(uint16_t)(aim-(uint16_t)session->player.motion.angle);
    const int lined=(aim<0x0800u||aim>0xf800u)&&best<INT64_C(120)*120;
    if(armed&&(ticks&1u)==0u&&(lined||(ticks&0x1ffu)==0u))buttons|=PORTABLE_INPUT_ITEM;
    return buttons;
}
