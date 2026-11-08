#ifndef PORTABLE_MKDS_MODEL_H
#define PORTABLE_MKDS_MODEL_H

/* Builds C3D1-style tables from Nitro models: rows, strings, properties,
 * welded vertices with colours, materials with texture SRT and pattern
 * animation, and the TXB1 texture bundle. Courses write the container;
 * drivers convert its materials and textures into model sections. */

#include "nds_fs.h"
#include "nitro_g3d.h"

/* The game draws course models 16 times larger than authored (collision and
 * map data use that scale); we map one of those units to 1/20 metre, which
 * gives Figure-8 Circuit a 786 m lap for 24 m/s modern karts. */
#define MKDS_MODEL_SCALE 16
#define MKDS_UNITS_PER_METRE 20

#define MKDS_ABSENT 0xFFFFFFFFu
#define MKDS_KIND_BUNDLE 108u
#define MKDS_KIND_MESH 96u
#define MKDS_KIND_MATERIAL 97u
#define MKDS_KIND_RIG 109u
#define MKDS_TEXTURES_PER_MODEL 128u

enum
{
    MKDS_MATERIAL_WRAP_U = 1,
    MKDS_MATERIAL_WRAP_V = 2,
    MKDS_MATERIAL_TWO_SIDED = 4,
    MKDS_MATERIAL_UNLIT = 8,
    MKDS_MATERIAL_MASK = 16,
    MKDS_MATERIAL_BLEND = 32,
    MKDS_MODEL_LIT = 4, /* kind-97 MTL1 flags (PORTABLE_MODEL_LIT/FOG) */
    MKDS_MODEL_FOG = 8,
    MKDS_PROPERTY_INTEGER = 0,
    MKDS_PROPERTY_FIXED = 1,
    MKDS_PROPERTY_BOOLEAN = 2,
    MKDS_PROPERTY_STRING = 3,
    MKDS_PROPERTY_ASSET = 4,
    MKDS_PROPERTY_VECTOR = 7,
};

/* Every table either container kind writes; the directory lists the
 * required ones and those with rows. */
enum
{
    MKDS_T_STRS, MKDS_T_META, MKDS_T_VERT, MKDS_T_VCOL, MKDS_T_TRIS, MKDS_T_MATS, MKDS_T_COLV,
    MKDS_T_COLT, MKDS_T_SURF, MKDS_T_CPNT, MKDS_T_CPED, MKDS_T_RTHD, MKDS_T_RTND, MKDS_T_RTED,
    MKDS_T_GRID, MKDS_T_PROP, MKDS_T_TXAN, MKDS_T_TXSR, MKDS_T_TXPT, MKDS_T_RESP, MKDS_T_ITEM, MKDS_T_CAMR,
    MKDS_T_OBJS, MKDS_T_ZONE, MKDS_T_COUNT
};

typedef struct MkdsTable
{
    char tag[5];
    uint32_t stride, count, required;
    RiBuf *bytes;
} MkdsTable;

typedef struct MkdsTexture
{
    const NitroTextures *set;
    const NitroTexture *texture;
    const NitroPalette *palette;
    uint32_t mirror, id, transparent, translucent;
    RiBuf *image;
} MkdsTexture;

/* Per converted material: how display-list texcoords become UVs. */
typedef struct MkdsMaterialPlan
{
    const NitroMaterial *source;
    int32_t uv[6]; /* static Q16 matrix, already including mirror halving */
    uint32_t mirror, skip, reverse, sky;
} MkdsMaterialPlan;

typedef struct MkdsModel
{
    RiCtx *ctx;
    char where[64];
    MkdsTable tables[MKDS_T_COUNT];
    MkdsTexture *textures;
    uint32_t texture_count, texture_base; /* texture IDs: texture_base | index */
    MkdsMaterialPlan *plans;
    uint32_t plan_capacity;
    uint32_t *weld[2]; /* open hash of VERT+VCOL and COLV rows */
    uint32_t weld_capacity[2];
    uint32_t sky_property;
    uint32_t fog; /* the course enables fog: polygon attribute bit 15 materials opt in */
    const struct MkdsPlacement *placement; /* non-NULL: geometry is a placed map object */
    RiBuf *vertex_nodes; /* non-NULL: each VERT row's skinning node byte; welding keeps nodes apart */
    uint32_t mesh_blend; /* mkds_mesh_write keeps translucent materials' blend flag */
} MkdsModel;

/* A map object's model-to-map transform: per-axis scale and rotation (Q12,
 * DS axes) then a map-unit (fx32) translation. */
typedef struct MkdsPlacement
{
    int64_t scale[3], r[9];
    int32_t translate[3];
} MkdsPlacement;

/* required lists the tables the container always carries; plans hold up to
 * material_limit materials. */
void mkds_model_init(MkdsModel *c, RiCtx *ctx, const char *where, uint32_t texture_base,
                     const int *required, size_t required_count, uint32_t material_limit);

void mkds_words(MkdsModel *c, int table, const uint32_t *values, size_t count);
uint32_t mkds_string(MkdsModel *c, const char *text);
uint32_t mkds_property(MkdsModel *c, const char *key, uint32_t type, uint32_t a, uint32_t b);
uint32_t mkds_property_vector(MkdsModel *c, const char *key, const int32_t value[3]);
int32_t mkds_round_div(int64_t value, int64_t divisor);
int64_t mkds_q16_mul(int64_t a, int64_t b);
/* fx32 map units (collision and map data) to Q16 metres. */
int32_t mkds_units(MkdsModel *c, int64_t map);
/* fx32 course model units (drawn MKDS_MODEL_SCALE times larger) to Q16 metres. */
int32_t mkds_metres(MkdsModel *c, int32_t world);
/* Weld table: identical encoded rows share one index; MKDS_ABSENT when new
 * (the caller then appends the row). which 0: VERT+VCOL, 1: COLV. */
uint32_t mkds_weld(MkdsModel *c, int which, const uint8_t *key, size_t size, int table);
void mkds_put_word(uint8_t *bytes, uint32_t value);
uint32_t mkds_rgb_word(uint32_t color);

/* A texture with an explicit palette (NULL for direct colour) joins the
 * bundle once per palette and mirror. */
MkdsTexture *mkds_texture(MkdsModel *c, const NitroTextures *set, const NitroTexture *texture,
                          const NitroPalette *palette, uint32_t mirror, const char *material);
/* Appends MATS rows (and TXAN/TXSR/TXPT for animated ones) for every
 * material of model; sky marks camera-anchored background materials. */
void mkds_materials(MkdsModel *c, const NitroModel *model, const NitroTextures *set, const NitroSrt *srt,
                    const NitroPattern *pattern, uint32_t sky);
/* Select one NSBTP appearance permanently; MKDS_ABSENT retains its timed loop.
 * Texture SRT remains animated independently of the pattern selection. */
void mkds_materials_at_frame(MkdsModel *c, const NitroModel *model, const NitroTextures *set,
                             const NitroSrt *srt, const NitroPattern *pattern, uint32_t sky,
                             uint32_t pattern_frame);

/* A render corner: Q16 metres in engine axes, Q16 UVs and RGBA. */
typedef struct MkdsCorner
{
    int32_t position[3], uv[2];
    int32_t normal[3]; /* Q30 display-list normal in engine axes when has_normal */
    uint8_t has_normal;
    uint32_t color;
    uint8_t node; /* the Nitro node that placed the vertex (NITRO_NO_NODE: none) */
} MkdsCorner;

/* UV and colour of a vertex under its material; position as course model. */
MkdsCorner mkds_corner(MkdsModel *c, const NitroVertex *vertex, const MkdsMaterialPlan *plan);
uint32_t mkds_render_vertex(MkdsModel *c, const MkdsCorner *corner, const int32_t normal[3]);
/* Course geometry: the model's triangles in world position; backdrop for sky models. */
/* Returns the camera-facing billboard nodes, which only placed objects may
 * have (drawn unrotated; the caller adds the crossing copy). */
uint32_t mkds_geometry(MkdsModel *c, const NitroModel *model, uint32_t first_material, int backdrop);

/* The container: {magic, version 1, size, id, 0, directory, count, 0 x9}. */
RiBuf *mkds_model_blob(MkdsModel *c, const char *magic, uint32_t id);
/* A plain ARGB image (width * height words) as one more bundle texture; its ID. */
uint32_t mkds_image(MkdsModel *c, const RiBuf *pixels, uint32_t width, uint32_t height);
/* An MTL1 material: ARGB tint, wrap flags and an optional plain image. */
RiBuf *mkds_material_section(MkdsModel *m, uint32_t argb, uint32_t flags, const RiBuf *image);
/* A kind-96 MSH1 mesh over 20-byte vertex rows (Q16 position, UV) and
 * 8-byte triangles (three u16 corners, u16 material), with one kind-97
 * MTL1 material per MATS row (IDs material_base | slot). */
void mkds_mesh_write(MkdsModel *m, uint32_t material_base, const RiBuf *vertices, uint32_t vertex_count,
                     const RiBuf *triangles, uint32_t triangle_count, uint32_t mesh_id);
/* TXB1 bundle of every texture the model used. */
RiBuf *mkds_model_bundle(MkdsModel *c);

typedef struct MkdsResources
{
    NitroModels models;
    NitroTextures textures;
    int has_srt, has_pattern;
    NitroSrt srt;
    NitroPattern pattern;
} MkdsResources;

/* stem.nsbmd with textures from stem.nsbtx (texture archive first, then the
 * archive) or embedded, and optional stem.nsbta/.nsbtp; 0 when the model is
 * absent. */
int mkds_resources(MkdsModel *c, const NdsFs *archive, const NdsFs *texture_archive, const char *stem,
                   MkdsResources *out);

/* A posed node relative to its rest pose, both model-space Nitro matrices:
 * r (Q12, DS axes) and t (fx, DS model units) with posed = (r, t) * rest. */
void mkds_pose_relative(MkdsModel *c, const NitroNodeMatrix *posed, const NitroNodeMatrix *rest, int64_t r[9],
                        int64_t t[3]);

/* A model's own kind-96 mesh (materials material_base | slot), in model-local
 * metres at course scale; its 20-byte vertex rows (Q16 position, UV) and
 * three-u16 + material triangles stay readable for collision passes. With an
 * animation the mesh keeps each vertex's node and gains a kind-109 RIG1 rig
 * whose one looping IDLE clip holds every NSBCA frame. A model that is one
 * camera-facing node (Goomba, Pokey, most item sprites) stays one plane in
 * the X-Z plane, flagged billboard; larger models cross their camera-facing
 * parts. blend keeps translucent materials blended (MTL1 flag 32). */
typedef struct MkdsObjectMesh
{
    uint32_t id, vertex_count, triangle_count, billboard;
    const uint8_t *vertices;
    const uint16_t *triangles;
} MkdsObjectMesh;
MkdsObjectMesh mkds_object_mesh(RiCtx *ctx, const char *where, const MkdsResources *model,
                                const NitroJointAnimation *animation, uint32_t mesh_id, uint32_t material_base,
                                int blend);

/* The plan of a one-material stencil shadow volume: what it darkens over a
 * level floor. A MKDS_SHADOW_TEXELS square of white texels whose alpha is
 * the covered share, rows from the volume's +z (top) to -z, columns from -x
 * (or +x when mirror_x) across its x/z bounds; low/high are its bounds in
 * model units. Fails naming `where` unless the volume reaches below 0. */
#define MKDS_SHADOW_TEXELS 64u
RiBuf *mkds_shadow_plan(RiCtx *ctx, const char *where, const NitroModel *model, int mirror_x, int32_t low[3],
                        int32_t high[3]);

#endif
