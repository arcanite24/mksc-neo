#ifndef PORTABLE_GP_RACE_RESULTS_H
#define PORTABLE_GP_RACE_RESULTS_H

#include "gp_race_session.h"
#include "gp_results_session.h"

typedef enum
{
    PORTABLE_GP_RACE_RESULTS_OK = 0,
    PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT,
    PORTABLE_GP_RACE_RESULTS_NOT_READY
} PortableGpRaceResultsStatus;

/* 2123C rows are frozen in current rank order. D7B8 estimates unfinished
 * times from remaining checkpoints; finished times sum DAC4's five splits. */
typedef struct {
    uint16_t centiseconds;
    uint8_t racer_index, driver_class, estimated;
} PortableQuickRunResultRow;
typedef struct {
    PortableQuickRunResultRow rows[PORTABLE_GP_RACER_COUNT];
    uint8_t engine_class;
} PortableQuickRunResults;
PortableGpRaceResultsStatus portable_quick_run_results_build(
    const PortableGpRaceSession *race, PortableQuickRunResults *results);

typedef struct {
    uint8_t racer_index,driver_class,balloons,rank;
} PortableBattleResultRow;
typedef struct {
    PortableBattleResultRow rows[4];
    uint8_t count;
} PortableBattleResults;
PortableGpRaceResultsStatus portable_battle_results_build(
    const PortableGpRaceSession *race,PortableBattleResults *results);

/* VS (0x400) lists every local: finishers in standings order with their own
 * finish clocks, then the racer the 4A878 branch stopped short. */
typedef struct {
    uint32_t centiseconds;
    uint8_t racer_index,driver_class,retired;
} PortableVsResultRow;
typedef struct {
    PortableVsResultRow rows[PORTABLE_GP_MAX_LOCALS];
    uint8_t count;
} PortableVsResults;
PortableGpRaceResultsStatus portable_vs_results_build(
    const PortableGpRaceSession *race,PortableVsResults *results);

PortableGpRaceResultsStatus portable_gp_race_results_config(
    const PortableGpRaceSession *race,
    PortableGpResultsSessionConfig *config);

#endif
