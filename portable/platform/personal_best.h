#ifndef PORTABLE_PERSONAL_BEST_H
#define PORTABLE_PERSONAL_BEST_H

#include <stdint.h>
#include "race_session.h"

typedef enum
{
    PORTABLE_PERSONAL_BEST_OK = 0,
    PORTABLE_PERSONAL_BEST_NOT_FOUND,
    PORTABLE_PERSONAL_BEST_INVALID,
    PORTABLE_PERSONAL_BEST_IO_ERROR,
    PORTABLE_PERSONAL_BEST_INCOMPATIBLE,
} PortablePersonalBestStatus;

typedef struct
{
    uint32_t ticks;
    uint32_t character_class;
    uint32_t roster_driver_id; /* 0: the class's stock driver; v5 rows only. */
} PortablePersonalBestRecord;

#define PORTABLE_PERSONAL_BEST_COUNT 5u

typedef struct
{
    PortablePersonalBestRecord races[PORTABLE_PERSONAL_BEST_COUNT];
    PortablePersonalBestRecord best_lap;
} PortablePersonalBestTable;

PortablePersonalBestStatus portable_personal_best_load_table(
    const char *path,const PortableRaceCompatibilityId *expected,
    PortablePersonalBestTable *out_table);
PortablePersonalBestStatus portable_personal_best_update_table(
    const char *path,const PortableRaceCompatibilityId *identity,
    uint32_t race_ticks,uint32_t lap_ticks,PortablePersonalBestTable *out_table,
    int *out_new_best);

PortablePersonalBestStatus portable_personal_best_load_record(
    const char *path,const PortableRaceCompatibilityId *expected,
    PortablePersonalBestRecord *out_record);
PortablePersonalBestStatus portable_personal_best_update_record(
    const char *path,const PortableRaceCompatibilityId *identity,
    uint32_t candidate_ticks,PortablePersonalBestRecord *out_record,
    int *out_updated);

PortablePersonalBestStatus portable_personal_best_load(
    const char *path,const PortableRaceCompatibilityId *expected,
    uint32_t *out_ticks);
PortablePersonalBestStatus portable_personal_best_update(
    const char *path,const PortableRaceCompatibilityId *identity,
    uint32_t candidate_ticks,uint32_t *out_best_ticks,int *out_updated);

#endif
