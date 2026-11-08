#ifndef PORTABLE_TIME_TRIAL_GHOST_SAVE_H
#define PORTABLE_TIME_TRIAL_GHOST_SAVE_H
#include <stdint.h>
#include "time_trial_ghost.h"

typedef enum
{
    PORTABLE_GHOST_SAVE_READY = 0,
    PORTABLE_GHOST_SAVE_UNAVAILABLE,
    PORTABLE_GHOST_SAVE_FULL,
    PORTABLE_GHOST_SAVE_FAILED,
    PORTABLE_GHOST_SAVE_SAVED
} PortableGhostSaveStatus;
typedef enum
{
    PORTABLE_GHOST_SAVE_ACTION_NONE = 0,
    PORTABLE_GHOST_SAVE_ACTION_WRITE,
    PORTABLE_GHOST_SAVE_ACTION_ERASE,
    PORTABLE_GHOST_SAVE_ACTION_BACK
} PortableGhostSaveAction;
typedef struct
{
    PortableGhostSaveStatus status;
    unsigned used_slots;
    uint8_t prompt;
    uint8_t yes;
    uint8_t replaces_course;
    uint8_t full_list;
    uint8_t erase_cursor;
    uint8_t erase_failed;
} PortableGhostSaveMenu;

/* Called with fresh edges, after results have latched the finished recording. */
PortableGhostSaveAction portable_time_trial_ghost_save_tick(
    PortableGhostSaveMenu *menu,uint16_t triggered);
#endif
