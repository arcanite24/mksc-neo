#ifndef PORTABLE_RECOVERED_RACE_SMOKE_H
#define PORTABLE_RECOVERED_RACE_SMOKE_H

#include <stdint.h>
#include "gp_race_session.h"
#include "battle_navigation.h"
#include "driver_input.h"

#include "race_runtime.h"
#include "recovered_pack.h"
#include "time_trial_ghost.h"

typedef enum
{
    PORTABLE_RECOVERED_SMOKE_OK = 0,
    PORTABLE_RECOVERED_SMOKE_INVALID_ARGUMENT,
    PORTABLE_RECOVERED_SMOKE_RUNTIME_ERROR,
    PORTABLE_RECOVERED_SMOKE_PROGRESS_BLOCK,
    PORTABLE_RECOVERED_SMOKE_TIMEOUT
} PortableRecoveredSmokeStatus;

typedef struct
{
    PortableRecoveredSmokeStatus status;
    PortableRaceRuntimeStatus runtime_status;
    int driver_status;
    uint32_t scheduled_ticks;
    uint32_t race_ticks;
    uint32_t last_progress_tick;
    uint16_t laps_completed;
    uint16_t angle;
    uint32_t route_index;
    int32_t position_x;
    int32_t position_y;
    int32_t speed_q16;
    uint8_t track_cell_valid;
    uint8_t checkpoint_raw;
    uint8_t lap_raw;
    uint8_t finish_armed;
    uint16_t checkpoint_next_index;
    uint16_t max_progress_raw;
} PortableRecoveredSmokeReport;

/* Deterministic route follower used only by unattended recovered-runtime
 * acceptance. Interactive gameplay always consumes sampled player input. */
/* Opt-in acceptance aid: hold ordinary B during water recovery rather than
 * steering to shore. It only filters sampled buttons and never edits drivers. */
static inline uint16_t portable_recovered_water_rescue_aid(uint16_t buttons,
    int enabled,const PortableDriverPilotState *driver)
{
    if(enabled && driver!=NULL && driver->source_terrain22_active &&
       (driver->hop.mode==3u||driver->hop.mode==4u||driver->hop.mode==6u||driver->hop.mode==7u))
        return PORTABLE_INPUT_BRAKE;
    return buttons;
}

/* A bounded ordinary A+steer segment exercises wall feedback before the
 * normal route follower resumes. direction:-1 left,+1 right,0 disabled. */
static inline uint16_t portable_recovered_wall_contact_aid(uint16_t buttons,
    int direction,const PortableRaceSession *race)
{
    if(direction && race && race->phase==PORTABLE_RACE_PHASE_RACING && race->elapsed_ticks<300u)
        return (uint16_t)(PORTABLE_INPUT_ACCELERATE | (direction<0?PORTABLE_INPUT_LEFT:PORTABLE_INPUT_RIGHT));
    return buttons;
}

uint16_t portable_recovered_race_smoke_control(
    const PortableRaceRuntime *runtime);

PortableRecoveredSmokeStatus portable_recovered_race_smoke_run(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    PortableRecoveredSmokeReport *report);

PortableRecoveredSmokeStatus portable_recovered_race_smoke_run_with_ghost(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    PortableRecoveredSmokeReport *report,
    uint8_t out_ghost[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

PortableRecoveredSmokeStatus portable_recovered_race_ghost_playback_smoke(
    const PortableRecoveredRaceView *source,
    PortableDriverPilotState *driver,
    const uint8_t ghost[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    PortableRecoveredSmokeReport *report);

const char *portable_recovered_smoke_status_name(
    PortableRecoveredSmokeStatus status);

uint16_t portable_gp_race_smoke_control(const PortableGpRaceSession *session,uint32_t ticks);
/* Optional ordinary-input exercise of selected drift tiers; no state writes. */
uint16_t portable_recovered_modern_air_aid(uint16_t buttons,int enabled,
    const PortableDriverPilotState *driver,const PortableModernDriftRules *rules);
uint16_t portable_recovered_modern_drift_aid(uint16_t buttons,int enabled,
    const PortableDriverPilotState *driver,const PortableModernDriftRules *rules);
/* Test aid for the selected Battle local: chase the nearest rival with
 * balloons along the native navigation grid and fire every held item. */
uint16_t portable_battle_local_aid(const PortableGpRaceSession *session,
    PortableBattleNavWorkspace *work,uint32_t ticks);

#endif
