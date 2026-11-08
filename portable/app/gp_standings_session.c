#include "gp_standings_session.h"

#include <stddef.h>
#include <string.h>

PortableGpStandingsStatus portable_gp_standings_session_init(
    PortableGpStandingsSession *session, const PortableGpCupSession *cup)
{
    PortableGpStandingsSession next = {0};

    if (session == NULL || cup == NULL)
        return PORTABLE_GP_STANDINGS_INVALID_ARGUMENT;
    if (cup->stage != PORTABLE_GP_CUP_STAGE_STANDINGS ||
        (cup->next_mode != 8u && cup->next_mode != 7u) ||
        ((cup->race_mode_flags & UINT16_C(0x0f00)) != UINT16_C(0x0100) &&
         cup->race_mode_flags != UINT16_C(0x2200)))
        return PORTABLE_GP_STANDINGS_UNSUPPORTED_STATE;
    next.cup = *cup;
    next.phase = PORTABLE_GP_STANDINGS_TRANSFER;
    for (size_t racer = 0u; racer < PORTABLE_GP_RACER_COUNT; ++racer)
    {
        next.displayed_points[racer] = cup->rows[racer].points_before;
        next.remaining_award[racer] = cup->rows[racer].points_awarded;
    }
    *session = next;
    return PORTABLE_GP_STANDINGS_OK;
}

PortableGpStandingsStatus portable_gp_standings_session_tick(
    PortableGpStandingsSession *session, uint8_t confirm_pressed)
{
    if (session == NULL)
        return PORTABLE_GP_STANDINGS_INVALID_ARGUMENT;
    switch (session->phase)
    {
    case PORTABLE_GP_STANDINGS_TRANSFER:
        if (session->reverse_finish_cursor >= PORTABLE_GP_RACER_COUNT)
        {
            session->phase = PORTABLE_GP_STANDINGS_CONSOLIDATE;
            break;
        }
        else
        {
            const uint8_t finish_rank = (uint8_t)(
                PORTABLE_GP_RACER_COUNT - 1u - session->reverse_finish_cursor);
            const uint8_t racer = session->cup.race_order[finish_rank];
            if (racer >= PORTABLE_GP_RACER_COUNT)
                return PORTABLE_GP_STANDINGS_UNSUPPORTED_STATE;
            if (session->remaining_award[racer] == 0u)
            {
                ++session->reverse_finish_cursor;
                session->transfer_counter = 0u;
            }
            else
            {
                ++session->transfer_counter;
                if (session->transfer_counter == 3u)
                {
                    --session->remaining_award[racer];
                    ++session->displayed_points[racer];
                    session->transfer_counter = 0u;
                    if (racer == 0u)
                        session->player_point_cue_pending = 1u;
                }
            }
        }
        /* The source handles A/Start after the current state tick and enters
         * case 13. Case 14 performs the actual consolidation one tick later. */
        if (confirm_pressed != 0u)
        {
            session->reverse_finish_cursor = 0u;
            session->transfer_counter = 0u;
            session->phase = PORTABLE_GP_STANDINGS_CONSOLIDATE;
        }
        break;
    case PORTABLE_GP_STANDINGS_CONSOLIDATE:
        for (size_t racer = 0u; racer < PORTABLE_GP_RACER_COUNT; ++racer)
        {
            session->displayed_points[racer] = (uint8_t)(
                session->displayed_points[racer] +
                session->remaining_award[racer]);
            session->remaining_award[racer] = 0u;
        }
        session->hold_counter = 0u;
        session->phase = PORTABLE_GP_STANDINGS_HOLD;
        break;
    case PORTABLE_GP_STANDINGS_HOLD:
        ++session->hold_counter;
        if (session->hold_counter > 60u || (confirm_pressed && session->short_transitions)) {
            session->phase=PORTABLE_GP_STANDINGS_REORDER;
            session->reorder_stage=confirm_pressed?23u:16u;
            session->reorder_counter=session->reorder_progress=0u;
        }
        break;
    case PORTABLE_GP_STANDINGS_REORDER:
        switch(session->reorder_stage){
        case 16: /* alternating row wipe, three ticks per step */
            if(++session->reorder_counter>2u){
                session->reorder_counter=0u;
                if(session->reorder_progress>13u){session->reorder_stage=17u;session->reorder_progress=0u;session->reorder_counter=3u;}
                else ++session->reorder_progress;
            }
            break;
        case 17:case 21:
            if(++session->reorder_counter>2u){
                session->reorder_counter=0u;
                if(++session->reorder_progress>5u){
                    if(session->reorder_stage==17u){session->reorder_stage=18u;session->reorder_progress=0u;}
                    else{session->reorder_stage=22u;session->reorder_progress=4u;session->reorder_counter=3u;}
                }
            }
            break;
        case 18:
            if(session->reorder_progress++>15u){session->reorder_stage=19u;session->reorder_progress=0u;}
            break;
        case 19:session->sorted_order=1u;session->reorder_stage=20u;break;
        case 20:session->reorder_stage=21u;session->reorder_progress=session->reorder_counter=0u;break;
        case 22:
            if(++session->reorder_counter>2u){
                session->reorder_counter=0u;
                if(session->reorder_progress>17u){session->reorder_stage=23u;session->reorder_progress=0u;}
                else ++session->reorder_progress;
            }
            break;
        case 23:session->sorted_order=1u;session->reorder_stage=24u;break;
        case 24:session->phase=PORTABLE_GP_STANDINGS_SORTED_HOLD;session->hold_counter=0u;break;
        default:return PORTABLE_GP_STANDINGS_UNSUPPORTED_STATE;
        }
        /* Source A/Start routes every active wipe/slide to23, which sorts
         * and redraws the complete cumulative board before accepting exit. */
        if(confirm_pressed && session->phase==PORTABLE_GP_STANDINGS_REORDER && session->reorder_stage<23u){
            session->reorder_stage=23u;session->reorder_progress=session->reorder_counter=0u;
        }
        break;
    case PORTABLE_GP_STANDINGS_SORTED_HOLD:
        ++session->hold_counter;
        if(confirm_pressed)session->confirm_cue_pending=1u;
        if(confirm_pressed)session->phase=PORTABLE_GP_STANDINGS_COMPLETE;
        break;
    case PORTABLE_GP_STANDINGS_COMPLETE:
        break;
    default:
        return PORTABLE_GP_STANDINGS_UNSUPPORTED_STATE;
    }
    return PORTABLE_GP_STANDINGS_OK;
}

uint16_t portable_gp_standings_music(uint8_t player_finish_rank)
{
    return player_finish_rank == 0u ? 14u : player_finish_rank <= 3u ? 15u : 16u;
}

uint8_t portable_gp_standings_session_take_player_point_cue(
    PortableGpStandingsSession *session)
{
    uint8_t pending;

    if (session == NULL)
        return 0u;
    pending = session->player_point_cue_pending;
    session->player_point_cue_pending = 0u;
    return pending;
}
