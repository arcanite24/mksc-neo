#ifndef PORTABLE_SCENE_3D_H
#define PORTABLE_SCENE_3D_H

#include "software_frame.h"

/* Zero-initialize before first use. Depth is owned; frame is borrowed through
 * the next begin/close. Pixel coordinates and buffer dimensions are physical. */
typedef struct {
    uint32_t *depth;
    uint32_t *billboard_depth; /* Alias into owned allocation; source art layer. */
    size_t capacity, width, height;
    PortableSoftwareFrame *frame;
    size_t clip_top, clip_bottom; /* Physical half-open row interval. */
    uint8_t source_weight, destination_weight; /* RGB weights /16, saturated. */
    uint8_t fallback_reported; /* Host lifetime: suppress repeated diagnostics. */
    uint8_t model_pass; /* Models test/write geometry and object depth atomically. */
    uint8_t model_fallback_reported; /* Host lifetime, not reset by begin. */
    void *worker_pool; /* Owned, idle between synchronous quad submissions. */
    uint8_t worker_pool_attempted; /* Do not retry a failed lazy allocation. */
} PortableScene3D;

/* Camera space: positiveZ forward, positiveY up. Projection maps positiveY
 * toward decreasing physical screenY. UV units belong to the sampler. */
typedef struct { double x, y, z, u, v; } PortableScene3DVertex;
typedef struct {
    double focal_x, focal_y, center_x, center_y;
    double near_z, far_z;
} PortableScene3DProjection;
typedef uint32_t (*PortableScene3DSampler)(const void *userdata, double u, double v);

/* Failed begin leaves the previous target/buffer unchanged. Successful begin
 * clears depth only; the caller owns background color and framebuffer storage. */
int portable_scene_3d_begin(PortableScene3D *scene, PortableSoftwareFrame *frame);
void portable_scene_3d_close(PortableScene3D *scene);
/* Returns1 for valid geometry, including fully clipped/degenerate triangles.
 * Samplers return ARGB; alpha zero leaves both destination and depth untouched. */
int portable_scene_3d_triangle(PortableScene3D *scene,
    const PortableScene3DProjection *projection, const PortableScene3DVertex vertices[3],
    PortableScene3DSampler sampler, const void *userdata);
/* Ordered triangles (0,1,2), (0,2,3), optionally across disjoint row bands.
 * Sampler and userdata must be reentrant and immutable for this call; neither
 * may change the target. Both triangles are validated before any drawing.
 * Returns only after every worker has stopped borrowing the target/content. */
int portable_scene_3d_quad_readonly(PortableScene3D *scene,
    const PortableScene3DProjection *projection, const PortableScene3DVertex vertices[4],
    PortableScene3DSampler sampler, const void *userdata);
/* Same clipping/validation without fragments. A model preflights every
 * triangle before emitting any, so unsupported geometry cannot partly draw. */
int portable_scene_3d_triangle_validate(const PortableScene3D *scene,
    const PortableScene3DProjection *projection,const PortableScene3DVertex vertices[3]);
/* UINT32_MAX is the empty-depth sentinel. Smaller depths win; ties let the
 * later draw win, matching ordered world billboards and triangle submission. */
int portable_scene_3d_depth_store(PortableScene3D *scene, size_t x, size_t y,
    uint32_t depth_q16, uint32_t argb);

#endif
