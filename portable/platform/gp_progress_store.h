#ifndef PORTABLE_GP_PROGRESS_STORE_H
#define PORTABLE_GP_PROGRESS_STORE_H

#include "gp_ceremony_session.h"
#include "mod_identity.h"

#include <stdint.h>

#define PORTABLE_GP_PROGRESS_CUP_COUNT 5u
#define PORTABLE_GP_PROGRESS_SPEED_COUNT 3u
#define PORTABLE_GP_PROGRESS_TRACK_SET_COUNT 2u

typedef enum
{
    PORTABLE_GP_PROGRESS_OK = 0,
    PORTABLE_GP_PROGRESS_NOT_FOUND,
    PORTABLE_GP_PROGRESS_INVALID_ARGUMENT,
    PORTABLE_GP_PROGRESS_BAD_FORMAT,
    PORTABLE_GP_PROGRESS_IO_ERROR
} PortableGpProgressStatus;

typedef struct
{
    int8_t grade[PORTABLE_GP_PROGRESS_TRACK_SET_COUNT]
                [PORTABLE_GP_PROGRESS_CUP_COUNT]
                [PORTABLE_GP_PROGRESS_SPEED_COUNT];
    uint8_t rating[PORTABLE_GP_PROGRESS_TRACK_SET_COUNT]
                  [PORTABLE_GP_PROGRESS_CUP_COUNT]
                  [PORTABLE_GP_PROGRESS_SPEED_COUNT];
    uint16_t unlock_flags; /* Set bit cup*3+class means Extra cup earned. */
} PortableGpProgress;

typedef struct
{
    uint8_t grade_changed;
    uint8_t rating_changed;
    uint8_t unlock_changed;
    uint8_t file_changed;
} PortableGpProgressUpdate;

/* Source DE68: four gold medals in one class unlock the fifth cup. */
uint8_t portable_gp_progress_cup_access(const PortableGpProgress *progress,uint8_t speed_class);

/* 1FC0 requires four base golds and the per-cup/class Extra earned bit. */
/* title_loadGraphics: 2 night when every GBA cup/class rating (save+0x46)
 * is best, else 1 sunset when every medal (save+0x28) is gold, else 0. */
uint8_t portable_gp_progress_title_variant(const PortableGpProgress *progress);
uint8_t portable_gp_progress_extra_cup_access(const PortableGpProgress *progress,uint8_t speed_class);

void portable_gp_progress_init(PortableGpProgress *progress);
PortableGpProgressStatus portable_gp_progress_apply_ceremony(
    PortableGpProgress *progress,const PortableGpCeremonySession *ceremony,
    uint8_t retro_tracks,uint8_t speed_class,
    PortableGpProgressUpdate *update);
PortableGpProgressStatus portable_gp_progress_load(
    const char *path,PortableGpProgress *progress);
/* Also returns the mod set recorded at the last save (empty for stock/v1). */
PortableGpProgressStatus portable_gp_progress_load_with_mods(
    const char *path,PortableGpProgress *progress,PortableModIdentity *mods);
/* Writes v1, or v2 with the current mod identity appended while mods are on. */
PortableGpProgressStatus portable_gp_progress_save(
    const char *path,const PortableGpProgress *progress);
/* The live GBA cup route uses the ceremony's engine class and shared file format. */
PortableGpProgressStatus portable_gp_progress_record_ceremony(
    const char *path, PortableGpCeremonySession *ceremony);
const char *portable_gp_progress_status_name(PortableGpProgressStatus status);

#endif
