#ifndef PORTABLE_MESH_RACER_H
#define PORTABLE_MESH_RACER_H

#include "mesh_kart.h"
#include "course_3d.h"
#include <stddef.h>
#include <stdint.h>
/* Engine surface for a decoded SURF row, cannon fields included. */
void portable_mesh_surface_from_course(const PortableCourse3DSurface *row, PortableMeshSurface *out);

enum
{
    PORTABLE_MESH_RACER_COUNTDOWN,
    PORTABLE_MESH_RACER_RACING,
    PORTABLE_MESH_RACER_FINISHED
};
typedef struct
{
    PortableMeshKartState kart;
    uint32_t tick, race_ticks, phase, countdown_ticks, lap, checkpoint, checkpoint_count;
    uint32_t last_respawn, respawn_grace, finish_ticks;
    int32_t previous_position_q16[3];
    uint32_t crossing_armed;
} PortableMeshRacerState;

typedef struct
{
    const PortableCourse3DView *course;
    const PortableMeshKartRules *rules;
    int32_t fall_limit_q16;
    uint32_t spawn_grid, mode; /* Immutable configuration, outside state codecs. */
    PortableMeshRacerState state;
    /* Borrowed immutable collision snapshot; NULL keeps the validated linear adapter. */
    const PortableMeshCourse *geometry;
} PortableMeshRacer;

typedef struct
{
    PortableMeshKartEvents events;
    uint32_t finish_fraction_q30; /* UINT32_MAX unless this turn finishes. */
    uint8_t publish, contact;
} PortableMeshRacerTurn;

/* Pure finite gate geometry for already decoded content. Forward/backward
 * admission matches the race owner; misses/invalid arguments preserve time.
 * This does not accept checkpoints, arm laps or mutate a racer. */
int portable_mesh_racer_gate_crossing(const PortableCourse3DCheckpoint *gate,
    const int32_t before_q16[3], const int32_t after_q16[3], int64_t *time_q30);

PortableMeshStatus portable_mesh_racer_init(PortableMeshRacer *racer,
                                            const PortableCourse3DView *course,
                                            const PortableMeshKartRules *rules, uint32_t grid,
                                            uint32_t mode, char *error, size_t capacity);
/* Phases operate on the owner's staging copy. Move all racers, resolve stable
 * pairs, then publish all racers before committing any shared world state. */
PortableMeshStatus portable_mesh_racer_move(PortableMeshRacer *racer, PortableMeshKartInput input,
                                            PortableMeshRacerTurn *turn, char *error,
                                            size_t capacity);
/* Optional immutable dynamics; no item timers or pointers enter racer state.
 * Failure preserves the owner and turn; existing move supplies NULL. */
PortableMeshStatus portable_mesh_racer_move_effects(PortableMeshRacer *racer,
    PortableMeshKartInput input, const PortableMeshKartEffects *effects,
    PortableMeshRacerTurn *turn, char *error, size_t capacity);
/* Authored GRID recovery for a Battle Bomb's accepted detonation timer;
 * other modes retain safe-point recovery. Transactional, with no forged input. */
PortableMeshStatus portable_mesh_racer_respawn(PortableMeshRacer *, char *error, size_t capacity);
PortableMeshStatus portable_mesh_racer_publish(PortableMeshRacer *racer,
                                               PortableMeshRacerTurn *turn, char *error,
                                               size_t capacity);

#endif
