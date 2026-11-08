#ifndef PORTABLE_RENDER_MODEL_H
#define PORTABLE_RENDER_MODEL_H

#include "model_asset.h"
#include "mesh_motion.h"
#include "render_pose.h"
#include "render_scene.h"

/* Geometry is copied into the existing resource owner; texture pixels remain
 * borrowed from pack. Models retain source triangle order and draw both sides.
 * Failure leaves the previous resources intact. Close with the shared close. */
/* Default lit-model terms until a course supplies its own (DS kartcrsmat's
 * usual row: ambient 7/31, emission 8/31). */
#define PORTABLE_RENDER_MODEL_AMBIENT 0xff393939u
#define PORTABLE_RENDER_MODEL_EMISSION 0xff424242u
int portable_render_resources_open_model(PortableRenderResources *resources,
                                         const PortableModelBinding *binding,
                                         const PortableAssetPackView *pack, char *error,
                                         size_t error_capacity);

/* A model's rig swaps (kind 109) animate its materials by the frame's
 * animation tick. portable_render_model_skin poses the opened rest vertices
 * at a clip frame into out (rest->vertex_count rows); callers draw them as a
 * copy of the rest mesh whose revision changes with each new pose. */
int portable_render_model_skin(const PortableRenderMesh *rest, const PortableModelRig *rig, uint32_t clip,
                               uint32_t frame, PortableRenderVertex *out);
/* Fractional baked clip sample. Loops blend the last frame into the first;
 * nonloops clamp. Rest geometry and accepted clocks are never modified. */
int portable_render_model_skin_sample(const PortableRenderMesh *rest, const PortableModelRig *rig,
                                      uint32_t clip, double frame, PortableRenderVertex *out);
/* The same with a caller's row-major 3x4 transform per node (procedural
 * poses: rolling tires). */
int portable_render_model_skin_nodes(const PortableRenderMesh *rest, const PortableModelRig *rig,
                                     const double (*matrices)[12], PortableRenderVertex *out);

/* Compose (local-pivot)*bindingScale*metres_per_unit with an SI pose at its
 * authored foot. R2 driver/scenery coordinates are original 2D source units;
 * they are not implicitly metres. Callers choose an explicit conversion.
 * Models use +X right/-Y forward/+Z up. The shared box pose's horizontal
 * columns are both negated (a proper rotation); negative scale is preserved.
 * Clearance is metres; the shared pose helper normalizes the terrain frame.
 * All output is preserved on failure; output/input aliasing is permitted. */
int portable_render_model_transform(double out[12], const PortableModelBinding *binding,
                                    const PortableRenderPose *pose, double clearance,
                                    double metres_per_unit);

/* As above, composing positive finite local body ratios before pivot subtraction.
 * This keeps the authored pivot seated during shrink and nonuniform squash.
 * Ratios must be in (0,32768]; failure preserves output. */
int portable_render_model_transform_scaled(double out[12], const PortableModelBinding *binding,
                                           const PortableRenderPose *pose, double clearance,
                                           double metres_per_unit, const double body_scale[3]);

/* OBJ local units already have their authored conversion in original. Compose
 * original * (bindingScale * (vertex-pivot)), without driver unit conversion.
 * A NULL binding copies the original fallback exactly. Transactional and alias-safe. */
int portable_render_model_local_transform(double out[12], const double original[12],
                                          const PortableModelBinding *binding);

/* Apply the collision motion to an authored world-from-local transform.
 * Rotates the complete basis and translation about the shared world pivot;
 * preserves model-local offsets and nonuniform scales. */
void portable_render_model_motion(const PortableMeshMotion *motion, uint32_t tick, double transform[12]);

#endif
