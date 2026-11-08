#ifndef PORTABLE_TIME_TRIAL_GHOST_LIBRARY_H
#define PORTABLE_TIME_TRIAL_GHOST_LIBRARY_H

#include "time_trial_ghost_store.h"

typedef enum
{
    PORTABLE_GHOST_LIBRARY_AVAILABLE = 0,
    PORTABLE_GHOST_LIBRARY_REUSE,
    PORTABLE_GHOST_LIBRARY_FULL,
    PORTABLE_GHOST_LIBRARY_IO_ERROR
} PortableGhostLibraryStatus;

typedef struct
{
    unsigned used;
    uint32_t courses[PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS];
} PortableGhostLibraryInventory;

/* CE3C/DC60/14C9C: reuse this course, otherwise allocate one of ten slots.
 * Native slots are per-course files; received ghosts (source 10/11) are separate.
 * Existing malformed files remain occupied until explicitly erased. */
PortableGhostLibraryStatus portable_time_trial_ghost_library_query(
    const char *directory,uint32_t course_id,PortableGhostLibraryInventory *out);
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_library_save(
    const char *directory,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

#endif
