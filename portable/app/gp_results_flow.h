#ifndef PORTABLE_GP_RESULTS_FLOW_H
#define PORTABLE_GP_RESULTS_FLOW_H

#include <stdint.h>

typedef enum
{
    PORTABLE_GP_RESULTS_OK = 0,
    PORTABLE_GP_RESULTS_INVALID_ARGUMENT,
    PORTABLE_GP_RESULTS_STATE_UNSUPPORTED
} PortableGpResultsStatus;

/* Source lap board holds through0x21c ticks. Short Transitions permits an
 * explicit confirm earlier; the natural standings/retry route stays intact. */
int portable_gp_results_advance_allowed(uint32_t result_ticks,uint8_t short_transitions);

#define PORTABLE_GP_RESULTS_MAX_ROWS 6u /* five laps and the total */
#define PORTABLE_GP_RESULTS_EXIT_BAND_COUNT 5u
#define PORTABLE_GP_RESULTS_RECORD_ROW_COUNT 6u
#define PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT 9u
#define PORTABLE_GP_RESULTS_MENU_OPTION_COUNT 5u

typedef struct
{
    uint8_t racer_index;
    uint8_t x;
    uint8_t y;
} PortableGpResultsRowCommand;

typedef struct
{
    uint8_t source_y;
    uint8_t row_count;
    int8_t destination_x;
} PortableGpResultsExitBandCommand;

typedef struct
{
    uint32_t flags;
    uint16_t time_centiseconds;
    uint8_t emphasized;
    uint32_t roster_driver_id; /* 0: stock art; native records retain the roster identity. */
} PortableGpResultsRecord;

typedef struct
{
    uint8_t display_row;
    uint8_t identity;
    uint16_t time_centiseconds;
    uint8_t emphasized;
    uint32_t roster_driver_id;
} PortableGpResultsRecordRowCommand;

typedef struct
{
    uint8_t source_y;
    uint8_t row_count;
    uint8_t destination_x;
} PortableGpResultsSlideBandCommand;

typedef struct
{
    uint32_t total_ticks;
    uint32_t state_counter;
    uint8_t state;
    uint8_t records_changed;
    uint8_t background_copy_pending;
    uint8_t background_clear_pending;
    uint8_t reveal_pending;
    uint8_t reveal_progress;
    uint8_t row_count;
    PortableGpResultsRowCommand rows[PORTABLE_GP_RESULTS_MAX_ROWS];
    uint8_t header_x;
    uint8_t header_y;
    uint8_t header_variant;
    uint8_t song_pending;
    uint16_t song_id;
    uint8_t result_delay;
    uint8_t exit_pending;
    uint8_t exit_code;
    uint8_t exit_wipe_pending;
    uint8_t exit_wipe_progress;
    uint8_t exit_band_count;
    PortableGpResultsExitBandCommand
        exit_bands[PORTABLE_GP_RESULTS_EXIT_BAND_COUNT];
    uint8_t advance_skip_pending;
    uint8_t followup_graphics_pending;
    uint8_t followup_map_pending;
    uint8_t scroll_map_pending;
    uint8_t track_name_pending;
    uint8_t followup_object_pending;
    uint8_t record_rows_pending;
    uint8_t record_row_count;
    PortableGpResultsRecordRowCommand
        record_rows[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT];
    uint16_t scroll_x;
    uint16_t scroll_y;
    uint8_t record_slide_pending;
    uint8_t record_slide_progress;
    uint8_t slide_band_count;
    PortableGpResultsSlideBandCommand
        slide_bands[PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT];
    uint8_t map_dirty_pending;
    uint8_t menu_reveal_pending;
    uint8_t menu_reveal_progress;
    uint8_t menu_setup_graphics_pending;
    uint8_t menu_setup_map_pending;
    uint8_t menu_setup_scroll_map_pending;
    uint8_t menu_palette_pending;
    uint8_t menu_highlight_pending;
    uint8_t menu_highlight_x;
    uint8_t menu_highlight_y;
    uint8_t menu_highlight_width;
    uint8_t menu_highlight_height;
    uint8_t menu_highlight_palette_bank;
    uint8_t menu_selection;
    uint8_t menu_previous_selection;
    uint8_t menu_cursor_pending;
    uint8_t menu_selection_changed;
    uint8_t menu_selection_highlight_pending;
    uint8_t menu_disabled_pending;
    uint8_t menu_navigation_song_pending;
    uint8_t menu_confirm_song_pending;
    uint16_t menu_navigation_song_id;
    uint16_t menu_confirm_song_id;
    uint8_t menu_action_code;
    uint8_t action_dispatch_pending;
    uint8_t action_dispatch_code;
    uint8_t scene_request_pending;
    uint32_t scene_request_value;
} PortableGpResultsFlow;

/* 1E7C8 cases3..11: GP eight-racer time board before point transfer.
 * States12/13 hand off to the existing standings owner, exactly once. */
typedef struct {
    uint32_t ticks;
    uint16_t counter;
    uint8_t state,progress,subcounter,visible,skip_transfer;
    uint8_t row_x[8],row_delay[8];
} PortableGpTimeBoardFlow;
/* Quick Run2123C cases4..10: delayed rows, redraw, then five window steps and a full redraw. */
typedef struct {
    uint8_t state,counter,window_progress,row_x[8],row_delay[8];
} PortableQuickResultsOpening;
void portable_quick_results_opening_init(PortableQuickResultsOpening *opening);
PortableGpResultsStatus portable_quick_results_opening_tick(
    PortableQuickResultsOpening *opening,uint8_t confirm);

void portable_gp_time_board_init(PortableGpTimeBoardFlow *flow);
int portable_gp_time_board_can_confirm(const PortableGpTimeBoardFlow *flow);
PortableGpResultsStatus portable_gp_time_board_tick(PortableGpTimeBoardFlow *flow,uint8_t confirm);

void portable_gp_results_flow_init(PortableGpResultsFlow *flow);
PortableGpResultsStatus portable_gp_results_flow_tick_entry(
    PortableGpResultsFlow *flow, uint8_t cup_record_changed,
    uint8_t course_record_changed);
PortableGpResultsStatus portable_gp_results_flow_tick_reveal(
    PortableGpResultsFlow *flow, uint8_t course_in_cup); /* 801E248 passes the lap count */
/* GP 1E248 stage3: hold through clock540 without TT result audio. */
PortableGpResultsStatus portable_gp_results_flow_tick_lap_hold(PortableGpResultsFlow *flow);

PortableGpResultsStatus portable_gp_results_flow_tick_hold(
    PortableGpResultsFlow *flow, uint16_t race_mode_flags);
PortableGpResultsStatus portable_gp_results_flow_tick_exit_wipe(
    PortableGpResultsFlow *flow, uint8_t advance_triggered);
PortableGpResultsStatus portable_gp_results_flow_tick_followup_setup(
    PortableGpResultsFlow *flow);
PortableGpResultsStatus portable_gp_results_flow_tick_record_setup(
    PortableGpResultsFlow *flow,
    const PortableGpResultsRecord records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT],
    const uint8_t record_identities[8]);
PortableGpResultsStatus portable_gp_results_flow_tick_record_slide(
    PortableGpResultsFlow *flow, uint8_t advance_triggered);
PortableGpResultsStatus portable_gp_results_flow_tick_menu_reveal(
    PortableGpResultsFlow *flow, uint8_t advance_triggered);
PortableGpResultsStatus portable_gp_results_flow_tick_menu_setup(
    PortableGpResultsFlow *flow, uint16_t race_flags);
PortableGpResultsStatus portable_gp_results_flow_tick_menu_record_setup(
    PortableGpResultsFlow *flow);
PortableGpResultsStatus portable_gp_results_flow_tick_menu_selection(
    PortableGpResultsFlow *flow, uint16_t repeat_keys,
    uint16_t triggered_keys, uint16_t race_flags, const uint8_t option_codes[5]);
PortableGpResultsStatus portable_gp_results_flow_tick_action_dispatch(
    PortableGpResultsFlow *flow);

#endif
