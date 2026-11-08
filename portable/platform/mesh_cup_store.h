#ifndef PORTABLE_MESH_CUP_STORE_H
#define PORTABLE_MESH_CUP_STORE_H
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t cup_id, engine_class, driver_id, course_ids[4];
    uint8_t digest[32];
} PortableMeshCupIdentity;
typedef struct
{
    uint32_t standing_rank, points, finish_ticks[4], finish_ranks[4];
} PortableMeshCupRecord;
typedef enum
{
    PORTABLE_MESH_CUP_STORE_OK,
    PORTABLE_MESH_CUP_STORE_NOT_FOUND,
    PORTABLE_MESH_CUP_STORE_INVALID,
    PORTABLE_MESH_CUP_STORE_INCOMPATIBLE,
    PORTABLE_MESH_CUP_STORE_IO_ERROR
} PortableMeshCupStoreStatus;
/* Creates only mesh3d under an existing caller-owned data directory. */
int portable_mesh_cup_store_path(const char *directory, const PortableMeshCupIdentity *identity,
                                 char *out, size_t capacity);
/* Full identity/header validation precedes output publication. */
PortableMeshCupStoreStatus portable_mesh_cup_store_load(const char *path,
                                                        const PortableMeshCupIdentity *expected,
                                                        PortableMeshCupRecord *out);
/* Better rank, then more points, then lower total genuine finish time wins.
 * Failure preserves the old file and all caller outputs. */
PortableMeshCupStoreStatus portable_mesh_cup_store_update(const char *path,
                                                          const PortableMeshCupIdentity *identity,
                                                          const PortableMeshCupRecord *candidate,
                                                          PortableMeshCupRecord *out, int *updated);
const char *portable_mesh_cup_store_status_name(PortableMeshCupStoreStatus status);
#endif
