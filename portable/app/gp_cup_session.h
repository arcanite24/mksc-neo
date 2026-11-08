#ifndef PORTABLE_GP_CUP_SESSION_H
#define PORTABLE_GP_CUP_SESSION_H

#include <stdint.h>

#include "gp_race_session.h"

typedef enum
{
    PORTABLE_GP_CUP_SESSION_OK = 0,
    PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT,
    PORTABLE_GP_CUP_SESSION_RACE_NOT_READY,
    PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE
} PortableGpCupSessionStatus;

typedef enum
{
    PORTABLE_GP_CUP_FINISH_NOT_READY = 0,
    PORTABLE_GP_CUP_FINISH_STANDINGS,
    PORTABLE_GP_CUP_FINISH_RETRY_MENU
} PortableGpCupFinishRoute;

typedef enum
{
    PORTABLE_GP_CUP_STAGE_RACE = 0,
    PORTABLE_GP_CUP_STAGE_STANDINGS,
    PORTABLE_GP_CUP_STAGE_NEXT_RACE
} PortableGpCupStage;

typedef struct
{
    uint8_t racer_index;
    uint8_t driver_class;
    uint8_t finish_rank;
    uint8_t standing_rank;
    uint8_t points_before;
    uint8_t points_awarded;
    uint8_t points_after;
} PortableGpStandingRow;

typedef struct
{
    uint32_t course_ids[4];
    uint8_t source_headers[4];
    uint8_t cup_index;
    uint8_t track_set;
} PortableGpCupDefinition;

typedef struct
{
    uint32_t course_id;
    uint32_t terrain_effect_counter,terrain_effect_phase;
    PortableGpItemArtClock item_art_clock;
    uint32_t marker_clock;
    uint32_t marker_display_clock;
    uint16_t race_mode_flags;
    uint8_t cup_index;
    uint8_t track_set;
    uint8_t course_in_cup;
    uint8_t course_header_index;
    uint8_t selected_driver_class;
    uint8_t engine_class;
    uint8_t retry_credits; /* Cup starts with three; Try Again consumes one. */
    uint8_t racer_driver_classes[PORTABLE_GP_RACER_COUNT];
    uint8_t race_order[PORTABLE_GP_RACER_COUNT];
    uint8_t cumulative_points[PORTABLE_GP_RACER_COUNT];
    uint8_t finishing_coins[4];
    uint8_t finishing_ranks[4],finishing_points[4];
    uint32_t course_ids[4];
    uint32_t race_centiseconds[4]; /* Original HUD clock, independent of wall time. */
    uint32_t race_ticks[4]; /* Frozen finish clocks, carried into the award panel. */
    uint8_t standings_order[PORTABLE_GP_RACER_COUNT];
    PortableGpDrivingQuality driving_quality;
    uint8_t driving_quality_sample_mask;
    PortableGpStandingRow rows[PORTABLE_GP_RACER_COUNT];
    uint8_t next_mode;
    PortableGpCupStage stage;
} PortableGpCupSession;

/* sub_801E7C8 state 1 sends the player in ranks 0-3 to the point-award
 * standings path; lower places continue to its retry/quit menu path. */
PortableGpCupFinishRoute portable_gp_cup_session_finish_route(
    const PortableGpRaceSession *completed_race);

/* sub_801E7C8 case 2 builds GP rows in finish order, awards the source
 * 9/6/3/1 table into each racer's byte at +0xc4, and later sorts cumulative
 * points. previous is NULL for race one and the prior next-race session for
 * later races. Courses 0-2 select mode 8; the fourth selects mode 7. */
PortableGpCupSessionStatus portable_gp_cup_session_begin_standings(
    const PortableGpRaceSession *completed_race,
    const PortableGpCupSession *previous,
    uint8_t cup_index, uint8_t course_in_cup, uint8_t course_header_index,
    uint8_t track_set,
    PortableGpCupSession *session);

/* play.c selection 8 calls sub_8001CA8. It increments the linear
 * course-in-cup index, resolves the next source header, and re-creates all
 * racers without changing their identities or cumulative points. */
PortableGpCupSessionStatus portable_gp_cup_session_advance(
    const PortableGpCupSession *standings,
    const PortableGpCupDefinition *definition,
    PortableGpCupSession *next_race);

/* Restore shared game state into an initialized native next-race grid.
 * Course-local actors/items/coins remain freshly constructed. */
PortableGpCupSessionStatus portable_gp_cup_session_resume_race(
    const PortableGpCupSession *next_race,PortableGpRaceSession *race);

#endif
