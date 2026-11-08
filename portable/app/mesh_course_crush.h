#ifndef PORTABLE_MESH_COURSE_CRUSH_H
#define PORTABLE_MESH_COURSE_CRUSH_H
#include "driver_affine.h"
#include "mesh_racer.h"
typedef struct {
    PortableDriverAffineRow rows[5][32];
    uint8_t counts[5];
} PortableMeshCrushRules;
typedef struct {
    uint16_t contact_timer;
    PortableDriverAffine affine;
} PortableMeshCrushState;
int portable_mesh_course_crush_rules_open(const PortableCourse3DView *, PortableMeshCrushRules *, char *, size_t);
int portable_mesh_course_crush_rules_valid(const PortableMeshCrushRules *);
int portable_mesh_course_crush_valid(const PortableMeshCrushRules *, const PortableMeshCrushState *);
int portable_mesh_course_crush_admit(const PortableMeshCrushRules *, PortableMeshCrushState *, PortableMeshKartState *, char *, size_t);
int portable_mesh_course_thwomp_rebound(PortableMeshKartState *, int32_t, char *, size_t);
/* Accepted turn only. A release turn stays held; the following turn can drive.
 * Recovery clears all crush state. Affine recovery continues after control release. */
int portable_mesh_course_crush_tick(const PortableMeshCrushRules *, PortableMeshCrushState *,
    int recovering, int *held, int *cue, char *, size_t);
void portable_mesh_course_crush_hold(PortableMeshKartState *, PortableMeshKartEffects *);
#endif
