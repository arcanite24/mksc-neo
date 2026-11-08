#include "gp_standings_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "GP standings session failed line %d: %s\n", \
            __LINE__, #condition); exit(1); \
} } while (0)

static PortableGpCupSession make_cup(void)
{
    PortableGpCupSession cup = {0};
    const uint8_t points[8] = {9u, 6u, 3u, 1u, 0u, 0u, 0u, 0u};
    cup.race_mode_flags = UINT16_C(0x2200);
    cup.next_mode = 8u;
    cup.stage = PORTABLE_GP_CUP_STAGE_STANDINGS;
    for (size_t rank = 0u; rank < 8u; ++rank)
    {
        cup.race_order[rank] = (uint8_t)rank;
        cup.standings_order[rank]=(uint8_t)(7u-rank);
        cup.rows[rank].standing_rank=(uint8_t)(7u-rank);
        cup.rows[rank].points_before = (uint8_t)rank;
        cup.rows[rank].points_awarded = points[rank];
        cup.rows[rank].points_after = (uint8_t)(rank + points[rank]);
    }
    return cup;
}

int main(void)
{
    PortableGpCupSession cup = make_cup();
    PortableGpStandingsSession session;
    unsigned ticks = 0u;
    unsigned cues = 0u;

    CHECK(portable_gp_standings_session_init(&session, &cup) ==
          PORTABLE_GP_STANDINGS_OK);
    while (session.phase == PORTABLE_GP_STANDINGS_TRANSFER)
    {
        CHECK(portable_gp_standings_session_tick(&session, 0u) ==
              PORTABLE_GP_STANDINGS_OK);
        cues += portable_gp_standings_session_take_player_point_cue(&session);
        CHECK(++ticks < 100u);
    }
    CHECK(ticks == 66u && cues == 9u &&
          session.phase == PORTABLE_GP_STANDINGS_CONSOLIDATE);
    CHECK(portable_gp_standings_session_tick(&session, 0u) ==
          PORTABLE_GP_STANDINGS_OK &&
          session.phase == PORTABLE_GP_STANDINGS_HOLD);
    for (size_t racer = 0u; racer < 8u; ++racer)
        CHECK(session.displayed_points[racer] == cup.rows[racer].points_after &&
              session.remaining_award[racer] == 0u);
    for (ticks = 0u; ticks < 60u; ++ticks)
        CHECK(portable_gp_standings_session_tick(&session, 0u) ==
              PORTABLE_GP_STANDINGS_OK &&
              session.phase == PORTABLE_GP_STANDINGS_HOLD);
    CHECK(portable_gp_standings_session_tick(&session, 0u) ==
          PORTABLE_GP_STANDINGS_OK &&
          session.phase == PORTABLE_GP_STANDINGS_REORDER &&
          session.reorder_stage==16u && !session.confirm_cue_pending);
    ticks=0u;
    while(session.phase==PORTABLE_GP_STANDINGS_REORDER){
        CHECK(portable_gp_standings_session_tick(&session,0u)==PORTABLE_GP_STANDINGS_OK);
        CHECK(++ticks<200u);
    }
    CHECK(ticks==143u && session.phase==PORTABLE_GP_STANDINGS_SORTED_HOLD && session.sorted_order);
    for(unsigned tick=0u;tick<100u;++tick)
        CHECK(portable_gp_standings_session_tick(&session,0u)==PORTABLE_GP_STANDINGS_OK &&
            session.phase==PORTABLE_GP_STANDINGS_SORTED_HOLD);
    CHECK(portable_gp_standings_session_tick(&session,1u)==PORTABLE_GP_STANDINGS_OK &&
        session.phase==PORTABLE_GP_STANDINGS_COMPLETE && session.confirm_cue_pending);

    CHECK(portable_gp_standings_session_init(&session, &cup) ==
          PORTABLE_GP_STANDINGS_OK);
    CHECK(portable_gp_standings_session_tick(&session, 1u) ==
          PORTABLE_GP_STANDINGS_OK &&
          session.phase == PORTABLE_GP_STANDINGS_CONSOLIDATE);
    CHECK(portable_gp_standings_session_tick(&session, 0u) ==
          PORTABLE_GP_STANDINGS_OK &&
          session.phase == PORTABLE_GP_STANDINGS_HOLD);
    for (size_t racer = 0u; racer < 8u; ++racer)
        CHECK(session.displayed_points[racer] == cup.rows[racer].points_after);
    /* Default case15 retains its61-tick hold; Short Transitions permits
     * early confirmation, but still visits23/24 to sort/redraw exactly once. */
    CHECK(portable_gp_standings_session_tick(&session,1u)==PORTABLE_GP_STANDINGS_OK &&
        session.phase==PORTABLE_GP_STANDINGS_HOLD);
    session.short_transitions=1u;
    CHECK(portable_gp_standings_session_tick(&session,1u)==PORTABLE_GP_STANDINGS_OK &&
        session.phase==PORTABLE_GP_STANDINGS_REORDER && session.reorder_stage==23u);
    CHECK(portable_gp_standings_session_tick(&session,0u)==PORTABLE_GP_STANDINGS_OK && session.sorted_order);
    CHECK(portable_gp_standings_session_tick(&session,0u)==PORTABLE_GP_STANDINGS_OK &&
        session.phase==PORTABLE_GP_STANDINGS_SORTED_HOLD);
    for(unsigned racer=0u;racer<8u;++racer)
        CHECK(session.displayed_points[racer]==cup.rows[racer].points_after && !session.remaining_award[racer]);
    CHECK(portable_gp_standings_session_tick(&session,1u)==PORTABLE_GP_STANDINGS_OK &&
        session.phase==PORTABLE_GP_STANDINGS_COMPLETE && session.confirm_cue_pending);
    CHECK(portable_gp_standings_music(0u) == 14u &&
          portable_gp_standings_music(1u) == 15u &&
          portable_gp_standings_music(3u) == 15u &&
          portable_gp_standings_music(4u) == 16u);

    cup.race_mode_flags = UINT16_C(0x0200);
    CHECK(portable_gp_standings_session_init(&session, &cup) ==
          PORTABLE_GP_STANDINGS_UNSUPPORTED_STATE);
    CHECK(portable_gp_standings_session_init(NULL, &cup) ==
          PORTABLE_GP_STANDINGS_INVALID_ARGUMENT);
    puts("portable GP standings session: PASS");
    return 0;
}
