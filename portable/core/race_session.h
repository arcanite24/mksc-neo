#ifndef PORTABLE_RACE_SESSION_H
#define PORTABLE_RACE_SESSION_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_RACE_MAX_LAPS 5u
#define PORTABLE_RACE_DETERMINISTIC_DIGEST_BYTES 32u

typedef struct
{
    int32_t x;
    int32_t y;
} PortableRacePoint;

typedef enum
{
    PORTABLE_RACE_PROGRESS_DEMO_V1 = 1,
    PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1 = 2,
    PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1 = 3
} PortableRaceProgressKind;

typedef enum
{
    PORTABLE_FINISH_NORTHBOUND_Y = 1,
    PORTABLE_FINISH_SOURCE_Y_CROSSING = 2,
    PORTABLE_FINISH_NONE = 3
} PortableFinishGateKind;

typedef struct
{
    uint32_t course_id;
    uint32_t course_revision;
    uint32_t rules_id;
    uint32_t rules_revision;
    uint32_t handling_profile_id;
    uint32_t handling_revision;
    uint32_t character_class;
    uint32_t state_schema_version;
    uint8_t deterministic_digest[PORTABLE_RACE_DETERMINISTIC_DIGEST_BYTES];
    /* Roster driver manifest ID (1..255) sharing character_class's seat, or
     * zero for the class's stock driver. Saves and ghosts keep it beside the
     * class; it is not part of course compatibility. */
    uint32_t roster_driver_id;
} PortableRaceCompatibilityId;

/* One source engine profile: eight handling classes, five rubber-band rows.
 * Content decodes EC300's byte tables; simulation never stores ROM offsets. */
typedef struct
{
    uint8_t acceleration[8][16];
    uint8_t target_speeds[8][5][4];
    uint8_t heading_steps[5][8];
} PortableGpCpuTuning;

typedef struct {
    uint8_t rival_classes[8][8];
    uint16_t contact_flags[8];
    int32_t recovery_acceleration[8];
    uint8_t pacing_progress[3][3][4],pacing_route[3][4],pacing_last_place[12];
    uint8_t contact_script[8][2];
} PortableDriverRosterRules;

typedef struct
{
    /* Route points use integer world units. Poses and gates use Q16.16. */
    const PortableRacePoint *route;
    size_t route_count;
    /* Row-major32B50 modes. Count includes every available mode row;
     * steering's optional checkpoint rows below use a separate selector. */
    const uint8_t *ai_modes;
    size_t ai_mode_count;
    /* Optional source checkpoint rows, stored row-major after row zero. */
    const PortableRacePoint *gp_route_rows;
    size_t gp_route_row_count;
    const uint8_t *gp_target_table;
    size_t gp_target_table_count;
    const PortableGpCpuTuning *cpu_tuning;
    const PortableDriverRosterRules *roster_rules;
    const int16_t *source_auto_deceleration_table;
    size_t source_auto_deceleration_count;
    const PortableRacePoint *gp_sentinel_targets;
    uint8_t gp_sentinel_target_mask;
    int32_t start_x_q16;
    int32_t start_y_q16;
    uint16_t start_heading;
    uint16_t start_route_index;
    uint8_t start_checkpoint_raw;
    PortableRaceProgressKind progress_kind;
    PortableFinishGateKind finish_kind;
    int32_t finish_y_q16;
    uint16_t lap_count;
    uint16_t countdown_ticks;
    uint16_t rescue_penalty_ticks;
    /* Player roulette strength budget (RUL1 v2); 0 selects the default 8. */
    uint8_t item_strength_threshold;
    PortableRaceCompatibilityId compatibility;
} PortableRaceRulesView;

typedef struct
{
    int32_t old_x_q16;
    int32_t old_y_q16;
    int32_t new_x_q16;
    int32_t new_y_q16;
    uint8_t drivable;
    uint8_t track_cell;
    uint8_t track_cell_valid;
    /* B98C samples before the driver's hop/Lakitu update. */
    int32_t height_q16;
    uint8_t hop_mode;
    uint8_t checkpoint_mode;
    uint8_t checkpoint_resample; /* A8EE carry sample; no second motion commit */
} PortableRaceMotionObservation;

typedef enum
{
    PORTABLE_RACE_PHASE_COUNTDOWN = 1,
    PORTABLE_RACE_PHASE_RACING,
    PORTABLE_RACE_PHASE_FINISHED
} PortableRacePhase;

typedef enum
{
    PORTABLE_RACE_TICK_INVALID = 0,
    PORTABLE_RACE_TICK_COUNTDOWN,
    PORTABLE_RACE_TICK_ADVANCE_MOTION,
    PORTABLE_RACE_TICK_FINISHED
} PortableRaceTickDisposition;

typedef struct
{
    const PortableRacePoint *route;
    size_t route_count;
    int32_t start_x_q16;
    int32_t start_y_q16;
    uint16_t start_heading;
    uint16_t start_route_index;
    uint8_t start_checkpoint_raw;
    PortableRaceProgressKind progress_kind;
    PortableFinishGateKind finish_kind;
    int32_t finish_y_q16;
    uint16_t lap_count;
    uint16_t countdown_ticks;
    uint16_t rescue_penalty_ticks;
    PortableRaceCompatibilityId compatibility;

    size_t next_route_index;
    PortableRacePhase phase;
    uint16_t laps_completed;
    uint16_t countdown_remaining;
    uint8_t lap_started;
    uint8_t finish_armed;
    uint8_t motion_pending;
    uint8_t checkpoint_raw;
    uint8_t lap_raw;
    uint16_t max_progress_raw;
    uint16_t checkpoint_next_index;
    uint32_t elapsed_ticks;
    uint32_t lap_origin_ticks;
    uint32_t lap_ticks[PORTABLE_RACE_MAX_LAPS];
    uint32_t rescue_count;
    int32_t rescue_x_q16;
    int32_t rescue_y_q16;
    uint16_t rescue_heading;
} PortableRaceSession;

int portable_race_session_init(PortableRaceSession *session,
                               const PortableRaceRulesView *rules);
void portable_race_session_restart(PortableRaceSession *session);
PortableRaceTickDisposition portable_race_session_begin_tick(
    PortableRaceSession *session);
int portable_race_session_commit_motion(
    PortableRaceSession *session,
    const PortableRaceMotionObservation *motion);
/* A8EE carry sample after the ordinary motion commit; no clock/centerline step. */
int portable_race_session_resample_checkpoint(PortableRaceSession *session,
    const PortableRaceMotionObservation *motion);
/* Finished drivers retain B98C checkpoints for autopilot/recovery, without
 * advancing lap records, race timing or the host's route-distance aid. */
int portable_race_session_update_finished_checkpoint(PortableRaceSession *session,
    const PortableRaceMotionObservation *motion);
int portable_race_session_apply_rescue(PortableRaceSession *session);
uint32_t portable_race_session_total_ticks(
    const PortableRaceSession *session);
uint32_t portable_race_session_current_lap_ticks(
    const PortableRaceSession *session);

#endif
