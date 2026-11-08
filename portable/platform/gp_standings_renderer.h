#ifndef PORTABLE_GP_STANDINGS_RENDERER_H
#define PORTABLE_GP_STANDINGS_RENDERER_H

#include "gp_standings_session.h"
#include "gp_race_results.h"
#include "gp_standings_asset.h"
#include "gp_results_asset.h"
#include "gp_results_flow.h"
#include "software_frame.h"

int portable_gp_standings_render(
    const PortableGpStandingsAssetView *asset,
    const PortableGpStandingsSession *session,
    PortableSoftwareFrame *frame);
/* Draw transparent standings layers over a freshly rendered race scene. */
int portable_gp_standings_render_over(
    const PortableGpStandingsAssetView *asset,
    const PortableGpStandingsSession *session,PortableSoftwareFrame *frame);

/* The 2123C time board (Quick Run, VS and Battle): rank, driver name and a
 * value in the board's two-cell digits: a time, a balloon count or nothing. */
typedef enum {
    PORTABLE_TIME_BOARD_EMPTY,
    PORTABLE_TIME_BOARD_TIME,
    PORTABLE_TIME_BOARD_COUNT
} PortableTimeBoardValue;
typedef struct {
    uint32_t value; /* Centiseconds, or a count 0..9. */
    uint8_t racer_index,driver_class,display_rank,kind;
    uint8_t selected; /* Explicit identity; independent of display rank or seat number. */
} PortableTimeBoardRow;
/* GP1E7C8 board: source GPC/GPS header, staggered name/time rows and
 * time-to-points wipe over the live race. Quick uses its separate2123C art. */
int portable_gp_time_board_render(const PortableGpStandingsAssetView *asset,
    const PortableTimeBoardRow rows[8],const uint8_t points[8],const uint8_t awards[8],
    const PortableGpTimeBoardFlow *flow,uint8_t engine_class,PortableSoftwareFrame *frame);

int portable_time_board_render_slide(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results,const PortableTimeBoardRow *rows,unsigned count,
    uint8_t engine_class,const uint8_t row_x[8],PortableSoftwareFrame *frame);

int portable_time_board_render(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results,const PortableTimeBoardRow *rows,unsigned count,
    uint8_t engine_class,PortableSoftwareFrame *frame);

int portable_battle_board_render(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results_asset,const PortableBattleResults *results,
    uint8_t engine_class,PortableSoftwareFrame *frame);
int portable_vs_board_render(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results_asset,const PortableVsResults *results,
    uint8_t engine_class,PortableSoftwareFrame *frame);
#endif
