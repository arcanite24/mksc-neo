#include "gp_results_session.h"

#include <stddef.h>
#include <string.h>

static PortableGpResultsSessionStatus result_status(
    PortableGpResultsStatus status)
{
    return status == PORTABLE_GP_RESULTS_OK ?
        PORTABLE_GP_RESULTS_SESSION_OK : PORTABLE_GP_RESULTS_SESSION_FLOW_ERROR;
}

static void add_song(PortableGpResultsSession *session, uint16_t song_id)
{
    if (session->song_count < PORTABLE_GP_RESULTS_SESSION_MAX_SONGS)
        session->song_ids[session->song_count++] = song_id;
}

PortableGpResultsSessionStatus portable_gp_results_session_init(
    PortableGpResultsSession *session,
    const PortableGpResultsSessionConfig *config)
{
    if (session == NULL || config == NULL || config->course_in_cup > 3u ||
        config->highlighted_row >= PORTABLE_GP_RESULTS_MAX_ROWS)
        return PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT;
    if(config->record_identities==NULL||config->menu_option_codes==NULL)
        return PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT;
    for(unsigned i=0u;i<8u;++i)if(config->record_identities[i]>7u)
        return PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT;
    for(unsigned i=0u;i<5u;++i)if(config->menu_option_codes[i]>4u)
        return PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT;
    memset(session, 0, sizeof *session);
    session->config = *config;
    session->presentation = PORTABLE_GP_RESULTS_PRESENT_RACE;
    portable_gp_results_flow_init(&session->flow);
    return PORTABLE_GP_RESULTS_SESSION_OK;
}

PortableGpResultsSessionStatus portable_gp_results_session_tick(
    PortableGpResultsSession *session, uint16_t repeat_keys,
    uint16_t triggered_keys)
{
    PortableGpResultsStatus status;
    uint8_t state;
    uint8_t advance;
    if (session == NULL)
        return PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT;
    if (session->complete != 0u)
        return PORTABLE_GP_RESULTS_SESSION_COMPLETE;

    session->song_count = 0u;
    session->scene_request_pending = 0u;
    ++session->presentation_frame;
    state = session->flow.state;
    advance = (uint8_t)((triggered_keys & UINT16_C(9)) != 0u);
    switch (state)
    {
    case 0u:
    case 1u:
        status = portable_gp_results_flow_tick_entry(
            &session->flow, session->config.cup_record_changed,
            session->config.course_record_changed);
        if (session->flow.state == 2u)
        {
            session->presentation = PORTABLE_GP_RESULTS_PRESENT_SCOREBOARD;
            session->presentation_frame = 0u;
        }
        break;
    case 2u:
        status = portable_gp_results_flow_tick_reveal(
            &session->flow, session->config.course_in_cup);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_SCOREBOARD;
        break;
    case 3u:
        status = portable_gp_results_flow_tick_hold(
            &session->flow, session->config.race_flags);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_SCOREBOARD;
        break;
    case 4u:
        status = portable_gp_results_flow_tick_exit_wipe(
            &session->flow, advance);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_EXIT_WIPE;
        break;
    case 5u:
        status = portable_gp_results_flow_tick_followup_setup(&session->flow);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_FOLLOWUP;
        session->presentation_frame = 0u;
        break;
    case 6u:
        status = portable_gp_results_flow_tick_record_setup(
            &session->flow, session->config.records,session->config.record_identities);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_RECORDS;
        session->presentation_frame = 0u;
        break;
    case 7u:
        status = portable_gp_results_flow_tick_record_slide(
            &session->flow, advance);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_RECORD_SLIDE;
        break;
    case 8u:
        status = portable_gp_results_flow_tick_menu_reveal(
            &session->flow, advance);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_MENU_REVEAL;
        break;
    case 9u:
        status = portable_gp_results_flow_tick_menu_setup(
            &session->flow, session->config.race_flags);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_MENU_SETUP;
        session->presentation_frame = 0u;
        break;
    case 10u:
        status = portable_gp_results_flow_tick_menu_record_setup(
            &session->flow);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_MENU_RECORDS;
        session->presentation_frame = 0u;
        break;
    case 11u:
        status = portable_gp_results_flow_tick_menu_selection(
            &session->flow, repeat_keys, triggered_keys,
            session->config.race_flags,session->config.menu_option_codes);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_MENU_SELECTION;
        break;
    case 12u:
        status = portable_gp_results_flow_tick_action_dispatch(&session->flow);
        session->presentation = PORTABLE_GP_RESULTS_PRESENT_TERMINAL;
        session->presentation_frame = 0u;
        break;
    default:
        return PORTABLE_GP_RESULTS_SESSION_FLOW_ERROR;
    }
    if (status != PORTABLE_GP_RESULTS_OK)
        return result_status(status);

    if (session->flow.song_pending != 0u)
        add_song(session, session->flow.song_id);
    if (session->flow.menu_navigation_song_pending != 0u)
        add_song(session, session->flow.menu_navigation_song_id);
    if (session->flow.menu_confirm_song_pending != 0u)
        add_song(session, session->flow.menu_confirm_song_id);
    if (session->flow.state == 13u)
    {
        session->complete = 1u;
        session->next_mode = session->flow.action_dispatch_code;
        session->scene_request_pending = session->flow.scene_request_pending;
        session->scene_request_value = session->flow.scene_request_value;
        return PORTABLE_GP_RESULTS_SESSION_COMPLETE;
    }
    return PORTABLE_GP_RESULTS_SESSION_OK;
}
