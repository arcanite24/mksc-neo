#ifndef PORTABLE_MESH_QUERY_H
#define PORTABLE_MESH_QUERY_H
#include "mesh_kart.h"

/* Exact geometric primitives shared with kart collision. Inputs are nonnull,
 * Q16 metre positions in the existing +/-4096m domain. Normal is winding-
 * dependent Q30; a degenerate triangle returns 0 without publishing a normal.
 * inside/closest require a nondegenerate triangle and its valid unit normal.
 * inside tests the dominant-axis projection (including edges), not plane
 * distance. closest projects onto the face or searches edges 0,1,2 in order;
 * equal-distance candidates keep the first. Its output must not alias inputs.
 * These are not ray/sphere sweeps or arbitrary-gravity movement policies. */
int portable_mesh_query_normal(const PortableMeshTriangle *, int32_t normal_q30[3]);
int portable_mesh_query_inside(const PortableMeshTriangle *, const int32_t point_q16[3],
                               const int32_t normal_q30[3]);
void portable_mesh_query_closest(const PortableMeshTriangle *, const int32_t point_q16[3],
                                 const int32_t normal_q30[3], int32_t closest_q16[3]);
typedef enum
{
    PORTABLE_MESH_QUERY_INVALID = -1,
    PORTABLE_MESH_QUERY_MISS = 0,
    PORTABLE_MESH_QUERY_HIT = 1
} PortableMeshQueryStatus;
/* Conservative inclusive segment AABB, widened on every axis by extent.
 * Extent is 0..INT32_MAX; saturated bounds retain all representable geometry.
 * Invalid inputs preserve both outputs. Consumers add their rounding skin. */
int portable_mesh_query_bounds(const int32_t before_q16[3], const int32_t after_q16[3],
                               int64_t extent_q16, int32_t low_q16[3], int32_t high_q16[3]);
/* Validate indexed traversal status/order/range. Missing index scans EVERY row,
 * including far malformed rows, in the original order. Indexed owners must
 * prevalidate all immutable rows. MISS/INVALID preserve out. */
PortableMeshQueryStatus portable_mesh_query_next_triangle(const PortableMeshCourse *,
                                                          const int32_t low_q16[3],
                                                          const int32_t high_q16[3], uint32_t first,
                                                          uint32_t *out);
#define PORTABLE_MESH_QUERY_FRACTION_ONE UINT64_C(4294967296)
/* Pickup query, not terrain CCD. Positions/radius are Q16; positions may be
 * +/-8192m (including relative world coordinates), radius 1 LSB..8192m.
 * Inclusive sphere contact returns ceil(earliest entry * 2^32), 0..2^32.
 * Exact tangencies count even between representable fractions. MISS/INVALID
 * preserve fraction; no host-specific wide integer type is required. */
PortableMeshQueryStatus portable_mesh_query_segment_sphere(const int32_t before_q16[3],
                                                           const int32_t after_q16[3],
                                                           const int32_t center_q16[3],
                                                           int32_t radius_q16,
                                                           uint64_t *fraction_q32);
typedef struct
{
    uint32_t triangle, surface, type, respawn;
    int32_t normal_q30[3], center_q16[3], drop_q16;
} PortableMeshSupportHit;
/* Z-up sphere support only: positions/triangle vertices are +/-4096m Q16,
 * radius 1 LSB..4096m, max_drop 0..8192m, cutoff 1..1Q30. Select the nearest
 * usable upward-facing floor that requires no upward center correction.
 * Wall surfaces are skipped; hazard type is returned for caller policy.
 * Equal exact drops keep the first triangle. Integer snapped center stays
 * on/above the plane (less than 1 Q16 LSB clearance rounding). Full traversal
 * failure or MISS preserves hit, including when earlier triangles matched. */
PortableMeshQueryStatus portable_mesh_query_support(const PortableMeshCourse *,
                                                    const int32_t center_q16[3], int32_t radius_q16,
                                                    int32_t max_drop_q16, int32_t cutoff_q30,
                                                    PortableMeshSupportHit *hit);
typedef struct
{
    uint64_t fraction_q32;
    uint32_t triangle, surface, type, respawn;
    int32_t normal_q30[3], center_q16[3], point_q16[3];
} PortableMeshFaceHit;
/* Two-sided finite FACE crossings only, not edge/vertex CCD. All positions
 * and vertices are +/-4096m Q16, radius 1 LSB..4096m. Starting penetration
 * is skipped (use overlap); touching while approaching counts at fraction 0.
 * Entry is rounded upward to Q32; center/face point use Q16 projection
 * conventions. Earliest exact plane fraction wins, first triangle on ties.
 * Surface type is metadata, with no movement/hazard policy. Full traversal
 * failure or MISS preserves hit. */
PortableMeshQueryStatus portable_mesh_query_sweep_faces(const PortableMeshCourse *,
                                                        const int32_t before_q16[3],
                                                        const int32_t after_q16[3],
                                                        int32_t radius_q16,
                                                        PortableMeshFaceHit *hit);
typedef struct
{
    uint32_t triangle, surface, type, respawn;
    int32_t normal_q30[3], point_q16[3], penetration_q16;
    uint8_t face_interior;
} PortableMeshOverlapHit;
/* Endpoint overlap against one triangle, including edges and vertices. Same
 * position/radius domain as sweep_faces. Uses the existing closest primitive;
 * distance is rounded down, penetration may overestimate by <1 Q16 LSB.
 * Normal points from closest point toward center; zero distance retains the
 * authored winding normal. face_interior includes face boundaries. MISS or
 * INVALID preserves hit; inputs and borrowed course data are never modified. */
PortableMeshQueryStatus portable_mesh_query_overlap(const PortableMeshCourse *, uint32_t triangle,
                                                    const int32_t center_q16[3], int32_t radius_q16,
                                                    PortableMeshOverlapHit *hit);
typedef struct
{
    uint32_t triangle;
    int32_t point_q16[3];
} PortableMeshFloorWitness;
typedef struct
{
    PortableMeshFloorWitness witness;
    int32_t normal_q30[3];
    uint32_t surface, type, respawn;
} PortableMeshFloorTraceHit;
/* Z-up point-floor continuation, not sphere collision. The accepted start
 * triangle selects the deck; its point may be rounded above the plane by less
 * than one Q16 LSB, as in support(). Trace the entire straight XY segment.
 * Transfer requires a positive-length common 3D edge/subedge; vertex fans may
 * traverse such edges at one fraction, but point-only islands cannot connect.
 * Exact duplicate rows with equal metadata are supported. Other overlapping
 * reachable interiors are UNSUPPORTED. Wall/too-steep rows are not floors;
 * other surface types are metadata, as for support(). At most512 intersecting
 * floor rows are admitted (COLLISION_BUDGET). OUTSIDE means no connected floor
 * reaches the target. INVALID/UNSUPPORTED/OUTSIDE/budget preserve out. Indexed
 * owners must prevalidate immutable rows, like the other query APIs. */
PortableMeshStatus portable_mesh_query_floor_trace(const PortableMeshCourse *,
                                                   const PortableMeshFloorWitness *start,
                                                   const int32_t target_xy_q16[2],
                                                   int32_t cutoff_q30,
                                                   PortableMeshFloorTraceHit *out);
#endif
