#ifndef PORTABLE_GP_RESULTS_SESSION_H
#define PORTABLE_GP_RESULTS_SESSION_H

#include <stdint.h>

#include "gp_results_flow.h"

#define PORTABLE_GP_RESULTS_SESSION_MAX_SONGS 2u

typedef enum
{
    PORTABLE_GP_RESULTS_SESSION_OK = 0,
    PORTABLE_GP_RESULTS_SESSION_COMPLETE,
    PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT,
    PORTABLE_GP_RESULTS_SESSION_FLOW_ERROR
} PortableGpResultsSessionStatus;

typedef enum
{
    PORTABLE_GP_RESULTS_PRESENT_RACE = 0,
    PORTABLE_GP_RESULTS_PRESENT_SCOREBOARD,
    PORTABLE_GP_RESULTS_PRESENT_EXIT_WIPE,
    PORTABLE_GP_RESULTS_PRESENT_FOLLOWUP,
    PORTABLE_GP_RESULTS_PRESENT_RECORDS,
    PORTABLE_GP_RESULTS_PRESENT_RECORD_SLIDE,
    PORTABLE_GP_RESULTS_PRESENT_MENU_REVEAL,
    PORTABLE_GP_RESULTS_PRESENT_MENU_SETUP,
    PORTABLE_GP_RESULTS_PRESENT_MENU_RECORDS,
    PORTABLE_GP_RESULTS_PRESENT_MENU_SELECTION,
    PORTABLE_GP_RESULTS_PRESENT_TERMINAL
} PortableGpResultsPresentation;

typedef struct
{
    const uint8_t *record_identities,*menu_option_codes; /* Borrowed content maps. */
    uint16_t race_flags;
    uint8_t course_in_cup;
    uint8_t highlighted_row;
    uint8_t cup_record_changed;
    uint8_t course_record_changed;
    uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS];
    PortableGpResultsRecord records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT];
} PortableGpResultsSessionConfig;

typedef struct
{
    PortableGpResultsFlow flow;
    PortableGpResultsSessionConfig config;
    PortableGpResultsPresentation presentation;
    uint32_t presentation_frame;
    uint8_t song_count;
    uint16_t song_ids[PORTABLE_GP_RESULTS_SESSION_MAX_SONGS];
    uint8_t complete;
    uint8_t next_mode;
    uint8_t scene_request_pending;
    uint32_t scene_request_value;
} PortableGpResultsSession;

PortableGpResultsSessionStatus portable_gp_results_session_init(
    PortableGpResultsSession *session,
    const PortableGpResultsSessionConfig *config);
PortableGpResultsSessionStatus portable_gp_results_session_tick(
    PortableGpResultsSession *session, uint16_t repeat_keys,
    uint16_t triggered_keys);

#endif
