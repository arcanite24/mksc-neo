#ifndef PORTABLE_GP_STANDINGS_SESSION_H
#define PORTABLE_GP_STANDINGS_SESSION_H

#include <stdint.h>

#include "gp_cup_session.h"

typedef enum
{
    PORTABLE_GP_STANDINGS_OK = 0,
    PORTABLE_GP_STANDINGS_INVALID_ARGUMENT,
    PORTABLE_GP_STANDINGS_UNSUPPORTED_STATE
} PortableGpStandingsStatus;

typedef enum
{
    PORTABLE_GP_STANDINGS_TRANSFER = 0,
    PORTABLE_GP_STANDINGS_CONSOLIDATE,
    PORTABLE_GP_STANDINGS_HOLD,
    PORTABLE_GP_STANDINGS_COMPLETE,
    PORTABLE_GP_STANDINGS_REORDER,
    PORTABLE_GP_STANDINGS_SORTED_HOLD
} PortableGpStandingsPhase;

typedef struct
{
    PortableGpCupSession cup;
    uint8_t displayed_points[PORTABLE_GP_RACER_COUNT];
    uint8_t remaining_award[PORTABLE_GP_RACER_COUNT];
    uint8_t reverse_finish_cursor;
    uint8_t transfer_counter;
    uint8_t hold_counter;
    uint8_t player_point_cue_pending;
    uint8_t confirm_cue_pending; /* 801E7C8 state 25: A/Start plays 143 */
    uint8_t reorder_stage; /* source1E7C8 cases16..24 */
    uint8_t reorder_counter,reorder_progress;
    uint8_t sorted_order;
    uint8_t short_transitions;
    PortableGpStandingsPhase phase;
} PortableGpStandingsSession;

PortableGpStandingsStatus portable_gp_standings_session_init(
    PortableGpStandingsSession *session, const PortableGpCupSession *cup);
PortableGpStandingsStatus portable_gp_standings_session_tick(
    PortableGpStandingsSession *session, uint8_t confirm_pressed);
/* 801E7C8+0x1BA: standings music by the player's 0-based race rank. */
uint16_t portable_gp_standings_music(uint8_t player_finish_rank);
uint8_t portable_gp_standings_session_take_player_point_cue(
    PortableGpStandingsSession *session);

#endif
