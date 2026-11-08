#ifndef PORTABLE_GP_RESULTS_RENDERER_H
#define PORTABLE_GP_RESULTS_RENDERER_H

#include <stdint.h>

#include "gp_results_asset.h"
#include "gp_minimap_marker_asset.h"
#include "gp_results_flow.h"
#include "software_frame.h"

/* Frozen HUD art only, in native coordinates. Transparent pixels reveal the
 * live race, including after a source zero-cell copy masks board art. */
#define PORTABLE_LAP_BOARD_HUD_TRANSPARENT UINT32_C(0x01000000)
typedef struct {
    int32_t x_q16,y_q16;
    uint32_t source_flags;
    uint8_t class_flags; /* Original driver4C; independent of mod content ID. */
    uint8_t selected;
} PortableRaceMinimapMarker;
int portable_gp_results_render_race_markers(const PortableGpMinimapMarkerAssetView *asset,
    const PortableRaceMinimapMarker *markers,uint8_t count,uint8_t coordinate_shift,
    uint32_t marker_clock,PortableSoftwareFrame *frame);
typedef struct { uint32_t pixels[240u*160u]; } PortableLapBoardHud;
/* Pass the full race frame: board centers, departing HUD retains edge anchors.
 * A NULL HUD draws just the board without legacy static background copies. */
int portable_gp_results_render_laps_with_hud(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,const uint16_t *times_centiseconds,
    uint8_t lap_count,const PortableLapBoardHud *hud,PortableSoftwareFrame *frame);

/* The lap board: three laps use DB7B8 two-row cells, other counts DB7C8 one-row cells. */
int portable_gp_results_render_laps(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,const uint16_t *times_centiseconds,
    uint8_t lap_count,PortableSoftwareFrame *frame);

int portable_gp_results_render(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    const uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS],
    uint8_t highlighted_row,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_rank(
    const PortableGpResultsAssetView *asset,
    uint8_t player_rank,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_rank_with_colors(
    const PortableGpResultsAssetView *asset,
    uint8_t player_rank,
    const uint8_t *rank_colors,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_coins(
    const PortableGpResultsAssetView *asset,
    uint8_t coins,
    PortableSoftwareFrame *frame);
/* The source result actor uses two or three priority-1 OBJ cells. */
int portable_gp_results_render_lakitu_record(
    const PortableGpResultsAssetView *asset,
    const uint8_t record[PORTABLE_GP_RESULTS_LAKITU_FRAME_BYTES],
    PortableSoftwareFrame *frame);
int portable_gp_results_render_minimap_markers(
    const PortableGpMinimapMarkerAssetView *asset,
    const int32_t x_q16[8],
    const int32_t y_q16[8],
    uint32_t result_tick,
    uint8_t rivals_use_bank7,
    const uint32_t source_flags[8],
    PortableSoftwareFrame *frame);
/* TT shares the source marker art with GP, but submits only its active racers. */
int portable_gp_results_render_minimap_markers_count(
    const PortableGpMinimapMarkerAssetView *asset,const int32_t x_q16[8],
    const int32_t y_q16[8],uint8_t count,uint32_t result_tick,
    uint8_t rivals_use_bank7,const uint32_t source_flags[8],PortableSoftwareFrame *frame);
int portable_gp_results_render_exit_wipe(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    const uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS],
    uint8_t course_in_cup,
    uint8_t highlighted_row,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_followup_setup(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_record_setup(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_record_slide(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_menu_reveal(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_menu_setup(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_menu_selection(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    uint16_t race_flags,
    uint32_t frame_counter,
    PortableSoftwareFrame *frame);
/* The Quick Run 2123C action window over an already drawn board: DB6FC's five
 * rows, cursor, pulsing selection and the greyed REPLAY row. */
/* 2123C's palette E is shared by the selected board row and action window. */
int portable_gp_results_menu_colors(const PortableGpResultsAssetView *asset,
    uint32_t frame_counter,uint16_t colors[16]);
int portable_gp_results_render_action_window_open(
    const PortableGpResultsAssetView *asset,uint8_t progress,uint8_t replay_disabled,
    PortableSoftwareFrame *frame);
int portable_gp_results_render_action_window(
    const PortableGpResultsAssetView *asset,uint8_t selection,uint8_t replay_disabled,
    uint32_t frame_counter,PortableSoftwareFrame *frame);
int portable_gp_results_render_terminal(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    uint16_t race_flags,
    uint32_t frame_counter,
    PortableSoftwareFrame *frame);

#endif
