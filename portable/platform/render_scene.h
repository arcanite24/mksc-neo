#ifndef PORTABLE_RENDER_SCENE_H
#define PORTABLE_RENDER_SCENE_H

#include "course_3d.h"
#include "scene_3d.h"

/* Geometry remains double precision for the CPU adapter. GPU backends convert
 * immutable resource arrays once; per-frame transforms never own content. */
typedef struct
{
    double position[3], normal[3], uv[2];
} PortableRenderVertex;
/* Looping texture animation of one material, one step per animation tick.
 * Matrices map mesh UVs: u'=m[0]u+m[2]v+m[4], v'=m[1]u+m[3]v+m[5]; a single
 * matrix is static. Pattern keys ascend from frame 0 and replace the texture. */
typedef struct
{
    uint32_t matrix_frames, matrix_count; /* count is 1 or matrix_frames */
    const double *matrices;               /* 6 per matrix */
    uint32_t key_frames, key_count;
    const uint32_t *key_starts;
    const PortableNativeImage *key_textures;
} PortableRenderTextureAnimation;
typedef struct
{
    uint32_t argb, flags; /* argb: the diffuse colour (and alpha) of LIT materials */
    int32_t alpha_cutoff_q16;
    PortableNativeImage texture;
    const PortableRenderTextureAnimation *animation; /* NULL: static; GPU/GLES only */
    /* LIT terms: shade = emission + light * (ambient + diffuse * max(0, N.L)). */
    uint32_t ambient_argb, emission_argb;
    uint32_t filter; /* PORTABLE_RENDER_FILTER_*; GPU/GLES only */
} PortableRenderMaterial;
enum
{
    /* Runtime-only material: linearly filtered color multiplies the depth-tested
     * destination without writing depth (soft contact shadows). Course content
     * cannot select it; the frozen CPU fallback omits these runs. */
    PORTABLE_RENDER_MATERIAL_MULTIPLY = 64u,
    /* Runtime-only material: linearly filtered color adds to the depth-tested
     * destination without writing depth (glows, sparks, flames). Course content
     * cannot select it; the frozen CPU fallback omits these runs too. */
    PORTABLE_RENDER_MATERIAL_ADDITIVE = 128u,
    PORTABLE_RENDER_MATERIAL_BLENDED = PORTABLE_RENDER_MATERIAL_MULTIPLY | PORTABLE_RENDER_MATERIAL_ADDITIVE,
    /* Course alpha blending (MATS flag 32): straight alpha over the depth-tested
     * destination without writing depth, after the mesh's other runs. */
    PORTABLE_RENDER_MATERIAL_ALPHA_BLEND = 32u,
    /* Camera-anchored backdrop (material property mksc:sky): drawn first in
     * mesh order around the eye, without depth, past the far plane. */
    PORTABLE_RENDER_MATERIAL_BACKGROUND = 256u,
    /* Vertex-normal lighting by the frame's light (GPU/GLES only; the frozen
     * CPU fallback draws these unlit). */
    PORTABLE_RENDER_MATERIAL_LIT = 512u,
    /* Fades into the frame's fog (GPU/GLES only). */
    PORTABLE_RENDER_MATERIAL_FOG = 1024u,
    /* With BACKGROUND (material property mksc:sky-world): the backdrop keeps
     * its world placement instead of following the eye. */
    PORTABLE_RENDER_MATERIAL_WORLD_SKY = 2048u
};
enum
{
    PORTABLE_RENDER_FILTER_NEAREST = 0u,
    PORTABLE_RENDER_FILTER_BILINEAR = 1u,
    PORTABLE_RENDER_FILTER_THREE_POINT = 2u /* N64 three-texel interpolation */
};
enum
{
    PORTABLE_RENDER_FOG_LINEAR = 0u,  /* weight rises linearly with view depth */
    PORTABLE_RENDER_FOG_EASE_IN = 1u, /* t^2 (DS fog table mode 1) */
    PORTABLE_RENDER_FOG_EASE_OUT = 2u, /* 1-(1-t)^2 (DS fog table mode 2) */
    PORTABLE_RENDER_FOG_RECIPROCAL = 3u /* linear in 1/depth, as N64 depth-buffer fog */
};
/* Scene light and fog (GPU/GLES only). Without any light colour lit
 * materials draw unlit; a zero light direction leaves them their ambient and
 * emission terms. With light_camera set the light travels along each view's
 * forward axis turned light_camera_turns about world up (positive turns move
 * +X toward +Y), as the DS's camera light. */
typedef struct
{
    double light_direction[3]; /* world unit vector toward the light */
    uint32_t light_argb;       /* light colour of instances without their own */
    uint32_t light_camera;
    double light_camera_turns;
    uint32_t fog_argb; /* alpha: strongest fog weight; zero disables fog */
    double fog_near, fog_far; /* view depth in metres, near < far */
    uint32_t fog_curve;       /* PORTABLE_RENDER_FOG_* */
} PortableRenderEnvironment;
typedef struct
{
    uint32_t first_index, index_count, material;
} PortableRenderRun;
typedef struct
{
    const PortableRenderVertex *vertices;
    const uint32_t *indices;
    const PortableRenderMaterial *materials;
    const PortableRenderRun *runs;
    uint32_t vertex_count, index_count, material_count, run_count;
    const uint32_t *vertex_argb; /* NULL: white; otherwise multiplies texels (GPU/GLES only) */
    uint32_t revision; /* changes whenever the vertices change in place (skinned models) */
} PortableRenderMesh;
typedef struct
{
    PortableRenderMesh mesh;
    void *storage;
    const PortableAssetPackView *pack;
    uint32_t course_id;
} PortableRenderResources;
typedef struct
{
    const PortableRenderMesh *mesh;
    double transform[12]; /* Row-major world_from_local affine 3x4. */
    uint32_t tint_argb;   /* Multiplicative channels; identity is 0xffffffff. */
    /* Optional viewport-relative vertical interval [begin,end)/extent.
     * All zeros mean the entire view. Integer edges partition adjacent source
     * rows exactly at any physical size; only GPU/GLES draw these intervals. */
    uint32_t clip_y_begin, clip_y_end, clip_y_extent;
    uint32_t flags;
    double billboard_scale[3]; /* Explicit signed local scales for either billboard mode. */
    uint32_t hidden_views;     /* Low four bits hide matching view indices; zero shows all. */
    double uv_offset[2];       /* GPU/GLES only; zero preserves immutable mesh UVs. */
    uint32_t light_argb;       /* Lit materials' light colour; zero uses the frame's. */
    uint32_t add_argb;         /* RGB added after the tint (colour effects); zero adds nothing. */
} PortableRenderInstance;
enum
{
    PORTABLE_RENDER_INSTANCE_BILLBOARD_Z = 1u,
    /* Column 2 supplies the supporting up direction, independent of scale. */
    PORTABLE_RENDER_INSTANCE_BILLBOARD_UP = 2u
};
typedef struct
{
    size_t x, y, width, height; /* Physical half-open, disjoint viewports. */
    double position[3], right[3], up[3], forward[3];
    PortableScene3DProjection projection; /* Physical coordinates local to this viewport. */
    uint32_t clear_argb;
} PortableRenderView;
enum
{
    PORTABLE_RENDER_VIEW_LIMIT = 4,
    PORTABLE_RENDER_INSTANCE_LIMIT = 4112,
    PORTABLE_RENDER_TARGET_EXTENT_LIMIT = 16384,
    PORTABLE_RENDER_TARGET_PIXEL_LIMIT = 16777216
};
typedef struct
{
    const PortableRenderInstance *instances;
    size_t instance_count;
    PortableRenderView views[PORTABLE_RENDER_VIEW_LIMIT];
    size_t view_count;
    uint32_t animation_tick; /* Advances material texture animations. */
    uint32_t animation_rewind_q16; /* 0=current; 65536=previous accepted tick. */
    PortableRenderEnvironment environment;
} PortableRenderFrame;
/* Shader light/fog uniforms of one run: light (local direction toward the
 * light, w lit), lit_base, lit_diffuse, fog colour and fog (near, far, curve,
 * filter), 20 floats. Backdrops take neither light nor fog. */
void portable_render_paint_environment(const PortableRenderFrame *frame, const PortableRenderView *view,
                                       const PortableRenderInstance *instance,
                                       const PortableRenderMaterial *material, int backdrop,
                                       float out[20]);
typedef struct
{
    PortableScene3D panes[PORTABLE_RENDER_VIEW_LIMIT];
} PortableRenderCpu;

/* Zero initialize owners. Failed preparation preserves the previous resources;
 * successful preparation replaces them. Pack/image bytes stay borrowed until
 * close AND until backend uploads have stopped borrowing that generation. */
int portable_render_resources_open_course(PortableRenderResources *resources,
                                          const PortableCourse3DView *course, char *error,
                                          size_t error_capacity);
/* The course as one race sees it (portable_course_3d_presence): faces of
 * materials whose mksc:present-in excludes the race are left out. */
int portable_render_resources_open_course_present(PortableRenderResources *resources,
                                                  const PortableCourse3DView *course, uint32_t presence,
                                                  char *error, size_t error_capacity);
void portable_render_resources_close(PortableRenderResources *resources);
/* The UV matrix and texture of a material at an animation tick; identity and
 * the material texture when static. Backends call this per run. */
void portable_render_material_at(const PortableRenderMaterial *material, uint32_t tick, double uv[6],
                                 const PortableNativeImage **texture);
void portable_render_instance_identity(PortableRenderInstance *instance,
                                       const PortableRenderMesh *mesh);
/* Fractional UV matrices only; texture-pattern keys remain on the accepted tick. */
void portable_render_material_sample(const PortableRenderMaterial *material, uint32_t tick,
    uint32_t rewind_q16, double uv[6], const PortableNativeImage **texture);
/* Invalid masks/indices are not visible; valid frames have at most four views. */
int portable_render_instance_visible(const PortableRenderInstance *source, size_t view_index);
/* Return physical rows relative to the view; a valid interval may be empty at
 * small output sizes. Invalid arguments preserve both outputs. */
int portable_render_instance_clip_rows(const PortableRenderInstance *, const PortableRenderView *,
                                       size_t *first, size_t *count);
/* Copy for one view. Z billboards remain world-upright; UP billboards use
 * normalized source column 2 and camera-right projected into its tangent plane.
 * A parallel camera-right chooses a deterministic tangent. Modes are exclusive.
 * Billboards preserve the supplied foot translation and explicit scales.
 * The resolved UP copy is ordinary camera-specific affine geometry (flags 0);
 * retain the immutable source to resolve another camera.
 * Aliasing is allowed; failure preserves output. */
int portable_render_instance_for_view(const PortableRenderInstance *source,
                                      const PortableRenderView *view, PortableRenderInstance *out);

/* Select distinct source art cells using the unsigned heading half-turn.
 * Source yaw0 looks toward-Y; view.right supplies camera yaw. The exact
 * forward edge selects0, the opposite edge1. Vertical right defaults to0.
 * Presentation only; failure preserves side. */
int portable_render_view_art_side(const PortableRenderView *, uint16_t heading, unsigned *side);
/* Resolve a directional instance independently for each camera, reflecting local
 * X within its anchored bounds when facing points along camera-right (XOR turn).
 * Facing is an engine-XY presentation snapshot. half_turn selects the source
 * unsigned half-turn boundary instead of a symmetric side test. Failure preserves output. */
int portable_render_instance_face_for_view(const PortableRenderInstance *source,
                                           const PortableRenderView *view, int32_t facing_x,
                                           int32_t facing_y, uint8_t turn, uint8_t half_turn,
                                           double local_min_plus_max_x,
                                           PortableRenderInstance *out);

/* Reflect local Z about the model's anchored bounds center. The caller caches
 * local minZ+maxZ; resources are borrowed and never rescanned here. Invalid
 * flags/scales/transforms, nonfinite arithmetic or world overflow preserve input. */
int portable_render_instance_flip_vertical(PortableRenderInstance *instance,
                                           double local_min_plus_max_z);

/* Native output is the quality floor; internal_scale requests at least
 * 160*scale samples per viewport row. Oversized outputs retain their aspect
 * within the bounded GPU target budget. Failure preserves both outputs. */
int portable_render_world_extent(size_t output_width, size_t output_height, unsigned internal_scale,
                                 unsigned viewport_rows, size_t *width, size_t *height, char *error,
                                 size_t capacity);
/* Full descriptor validation before any framebuffer writes. */
/* GPU/GLES immutable upload proof. Return 1 only for an already validated,
 * successfully uploaded exact vertices/indices pointer+count key; 0 deep-checks
 * unknown geometry, -1 rejects a changed cached mesh descriptor. Every call
 * still validates headers/materials/runs and all instance/view state. Forget
 * uploads before modifying geometry or reusing its storage. No proof is saved
 * by this function; NULL callback is the ordinary deep validator. */
typedef int (*PortableRenderGeometryKnown)(const void *, const PortableRenderMesh *);
int portable_render_frame_valid_geometry(const PortableRenderFrame *, size_t, size_t,
                                         PortableRenderGeometryKnown, const void *, char *, size_t);
int portable_render_frame_valid(const PortableRenderFrame *frame, size_t target_width,
                                size_t target_height, char *error, size_t error_capacity);
/* Read-only scene submission; writes only viewport pixels and owned CPU depth.
 * No target/frame/content pointer remains borrowed when this call returns.
 * Kernel, ordered material runs, cutout and winding semantics stay unchanged. */
int portable_render_cpu_draw(PortableRenderCpu *cpu, const PortableRenderFrame *scene,
                             const PortableSoftwareFrame *target, char *error,
                             size_t error_capacity);
void portable_render_cpu_close(PortableRenderCpu *cpu);

#endif
