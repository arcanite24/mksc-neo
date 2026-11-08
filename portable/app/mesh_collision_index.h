#ifndef PORTABLE_MESH_COLLISION_INDEX_H
#define PORTABLE_MESH_COLLISION_INDEX_H
#include "course_3d.h"
#include "mesh_kart.h"
#include "mesh_motion.h"
/* Immutable decoded collision snapshot. Creation validates every row before
 * publication; afterward the source view/pack may be released or replaced.
 * Generic callback courses retain their existing linear validation semantics.
 * No index, allocation or host pointer enters a simulation codec/checksum. */
typedef struct PortableMeshCollisionIndex PortableMeshCollisionIndex;
PortableMeshCollisionIndex *portable_mesh_collision_index_create(const PortableCourse3DView *,
    char *error, size_t capacity);
/* The same for one race (portable_course_3d_presence): COLT rows whose SURF
 * mksc:present-in excludes it keep their IDs but are never queried. */
PortableMeshCollisionIndex *portable_mesh_collision_index_create_present(const PortableCourse3DView *,
    uint32_t presence, char *error, size_t capacity);
const PortableMeshCourse *portable_mesh_collision_index_course(const PortableMeshCollisionIndex *);
void portable_mesh_collision_index_close(PortableMeshCollisionIndex *);
/* COLT rows owned by an OBJS row with mksc:motion hold its rest pose and
 * move with it. The course at `tick` is built in caller storage; any other
 * course (or one without moving rows) is returned unchanged. */
enum { PORTABLE_MESH_COLLISION_MOTION_LIMIT=64 };
typedef struct
{
    PortableMeshCourse course;
    const PortableMeshCollisionIndex *index;
    PortableMeshPose now[PORTABLE_MESH_COLLISION_MOTION_LIMIT], before[PORTABLE_MESH_COLLISION_MOTION_LIMIT];
} PortableMeshCollisionPose;
const PortableMeshCourse *portable_mesh_collision_index_at(const PortableMeshCourse *, uint32_t tick,
                                                           PortableMeshCollisionPose *storage);
/* Contact spheres of hazard OBJS rows (mksc:hazard-radius, optional
 * mksc:hazard-center) posed at `tick`; returns their count, 0 for a course
 * that is no index. A row with mksc:hazard-period is live only while
 * tick % period < mksc:hazard-active; dormant ones report radius 0.
 * Optional `reactions` names each row's PORTABLE_MESH_HAZARD_* reaction:
 * boolean mksc:spin-bumper rows push and spin anti-gravity karts; string
 * mksc:hazard-reaction "spin", "tumble" or "squash" adds that status to the
 * knock (absent or "bump": the knock alone). */
enum { PORTABLE_MESH_COLLISION_HAZARD_LIMIT=64 };
uint32_t portable_mesh_collision_hazards(const PortableMeshCourse *, uint32_t tick,
                                         int32_t centers_q16[][3], int32_t radii_q16[], uint8_t reactions[]);
/* The same, seen from `probe` (a kart's centre): a row with vector
 * mksc:hazard-box (positive half extents along its rest x, y (forward) and
 * z axes, Q16 metres, within its hazard radius) reports the box point
 * nearest the probe as a 1 cm sphere, so boxes strike like spheres. Rows
 * whose mksc:present-in excludes the index's race are not hazards. */
uint32_t portable_mesh_collision_hazards_near(const PortableMeshCourse *, uint32_t tick, const int32_t probe_q16[3],
                                              int32_t centers_q16[][3], int32_t radii_q16[], uint8_t reactions[]);
/* OBJS row motion: 1 moving (out set), 0 static, -1 invalid declaration. */
int portable_mesh_collision_object_motion(const PortableCourse3DView *, uint32_t object, PortableMeshMotion *out);
#endif
