#ifndef PORTABLE_MESH_TIME_TRIAL_STORE_H
#define PORTABLE_MESH_TIME_TRIAL_STORE_H

#include "mesh_time_trial_replay.h"

typedef enum
{
    PORTABLE_MESH_STORE_OK = 0,
    PORTABLE_MESH_STORE_NOT_FOUND,
    PORTABLE_MESH_STORE_INVALID,
    PORTABLE_MESH_STORE_INCOMPATIBLE,
    PORTABLE_MESH_STORE_IO_ERROR
} PortableMeshStoreStatus;
typedef struct
{
    uint32_t race_ticks, best_lap_ticks;
} PortableMeshPersonalBest;
/* Creates only the mesh3d subdirectory of the caller's existing data directory.
 * The full identity belongs to both the filename and the independently checked header. */
int portable_mesh_store_path(const char *directory,
                             const PortableMeshTimeTrialCompatibility *identity, int ghost,
                             char *out, size_t capacity);
/* Successful load transfers a bounded heap buffer; failure leaves outputs unchanged. */
PortableMeshStoreStatus portable_mesh_ghost_load(const char *path,
                                                 const PortableMeshTimeTrialCompatibility *expected,
                                                 uint8_t **out_bytes, size_t *out_size);
/* Input is a complete replay/ghost stream. Atomic publication uses ghost magic. */
PortableMeshStoreStatus portable_mesh_ghost_save(const char *path,
                                                 const PortableMeshTimeTrialCompatibility *expected,
                                                 const uint8_t *bytes, size_t size);
PortableMeshStoreStatus
portable_mesh_personal_best_load(const char *path,
                                 const PortableMeshTimeTrialCompatibility *expected,
                                 PortableMeshPersonalBest *out);
PortableMeshStoreStatus portable_mesh_personal_best_update(
    const char *path, const PortableMeshTimeTrialCompatibility *expected,
    const PortableMeshPersonalBest *candidate, PortableMeshPersonalBest *out, int *updated);
const char *portable_mesh_store_status_name(PortableMeshStoreStatus status);

#endif
