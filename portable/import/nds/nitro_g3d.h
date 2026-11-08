#ifndef PORTABLE_NITRO_G3D_H
#define PORTABLE_NITRO_G3D_H

/* Nitro G3D resources: models (BMD0), textures (BTX0 or a model's TEX0),
 * texture SRT animation (BTA0), texture pattern animation (BTP0) and
 * skeletal animation (BCA0). Layouts follow noclip's nns_g3d (MIT),
 * apicula's nsbmd_docs (0BSD) and MarioKartToolbox (MIT). Readers keep
 * borrowed views into the file; evaluation runs the model's render commands
 * and display lists in fixed point. Unsupported features fail naming the
 * file, model and command. */

#include "rom_import_internal.h"

#define NITRO_NAME_CAPACITY 17u
#define NITRO_MATRIX_STACK 32u

enum
{
    NITRO_TEXTURE_NONE = 0,
    NITRO_TEXTURE_A3I5 = 1,
    NITRO_TEXTURE_PALETTE4 = 2,
    NITRO_TEXTURE_PALETTE16 = 3,
    NITRO_TEXTURE_PALETTE256 = 4,
    NITRO_TEXTURE_COMPRESSED = 5,
    NITRO_TEXTURE_A5I3 = 6,
    NITRO_TEXTURE_DIRECT = 7,
};

typedef struct NitroTexture
{
    char name[NITRO_NAME_CAPACITY];
    uint32_t format, width, height, color0;
    RiData texels;  /* exactly the texel bytes of this image */
    RiData indices; /* 4x4 compressed only: per-block palette words */
} NitroTexture;

typedef struct NitroPalette
{
    char name[NITRO_NAME_CAPACITY];
    RiData colors; /* BGR555 words to the end of the palette block */
} NitroPalette;

typedef struct NitroTextures
{
    const char *file;
    NitroTexture *textures;
    size_t texture_count;
    NitroPalette *palettes;
    size_t palette_count;
} NitroTextures;

/* Texture matrix modes of the authoring tool (model header byte 0x16). */
enum
{
    NITRO_TEXMTX_MAYA = 0,
    NITRO_TEXMTX_SI3D = 1,
    NITRO_TEXMTX_MAX = 2,
    NITRO_TEXMTX_XSI = 3,
};

typedef struct NitroMaterial
{
    char name[NITRO_NAME_CAPACITY];
    char texture[NITRO_NAME_CAPACITY]; /* empty: untextured */
    char palette[NITRO_NAME_CAPACITY];
    uint32_t diffuse_ambient, specular_emission, polygon_attr, teximage_param;
    uint32_t width, height; /* texel extent the display lists' texcoords use */
    /* Static texture SRT in fx32 (sin/cos for rotation), identity when absent. */
    int32_t scale_s, scale_t, sin_r, cos_r, translate_s, translate_t;
} NitroMaterial;

typedef struct NitroModel
{
    char name[NITRO_NAME_CAPACITY];
    const char *file;
    RiData data;      /* the model block */
    RiData commands;  /* render commands (SBC) */
    uint32_t texmtx_mode, node_count, material_count, shape_count;
    int32_t up_scale; /* fx32 */
    RiData *nodes;    /* node descriptions */
    NitroMaterial *materials;
    RiData *shapes; /* display lists */
    char (*shape_names)[NITRO_NAME_CAPACITY];
} NitroModel;

typedef struct NitroModels
{
    const char *file;
    NitroModel *models;
    size_t count;
    int has_textures;
    NitroTextures textures; /* embedded TEX0 when has_textures */
} NitroModels;

NitroModels nitro_bmd(RiCtx *ctx, RiData file, const char *name);
NitroTextures nitro_btx(RiCtx *ctx, RiData file, const char *name);
const NitroTexture *nitro_find_texture(const NitroTextures *textures, const char *name);
const NitroPalette *nitro_find_palette(const NitroTextures *textures, const char *name);

/* Decodes one texture to width*height little-endian ARGB words (ri_pixel
 * order). palette may be NULL only for direct-colour textures. */
RiBuf *nitro_texture_argb(RiCtx *ctx, const NitroTextures *textures, const NitroTexture *texture,
                          const NitroPalette *palette);

#define NITRO_NO_NODE 0xFFu

/* One evaluated vertex in model space times the model's up scale, fx (Q12)
 * world units, with GX texcoords (Q4 texels) and BGR555 colour. local is the
 * GX position before its node's matrix: models that bind every vertex to one
 * node (no envelopes) pose it as nitro_model_pose(node) * local. */
typedef struct NitroVertex
{
    int32_t position[3];
    int32_t local[3];
    int32_t texcoord[2];
    int32_t normal[3]; /* Q9 unit normal from the display list, or zero */
    uint16_t color;    /* BGR555 */
    uint8_t has_normal;
    uint8_t node; /* node whose matrix placed it; NITRO_NO_NODE: identity */
} NitroVertex;

typedef struct NitroTriangle
{
    uint32_t vertices[3];
    uint32_t material;
} NitroTriangle;

/* Billboard nodes face the camera. A backdrop mesh (seen from near its
 * origin) bakes them facing the origin; any other mesh counts them, as a
 * static mesh cannot represent them. */
typedef struct NitroMesh
{
    RiBuf *vertices;  /* NitroVertex */
    RiBuf *triangles; /* NitroTriangle, counter-clockwise front faces */
    uint32_t vertex_count, triangle_count;
    uint32_t billboard_nodes;
} NitroMesh;

NitroMesh nitro_model_mesh(RiCtx *ctx, const NitroModel *model, int backdrop);

/* Skeletal animation (BCA0/JNT0): per node translation, rotation and scale
 * curves, evaluated at whole frames as MarioKartToolbox does. */
typedef struct NitroJointAnimation
{
    char name[NITRO_NAME_CAPACITY];
    const char *file;
    RiData block; /* the J\0AC animation */
    uint32_t frames, track_count;
} NitroJointAnimation;

/* The first animation of the file. */
NitroJointAnimation nitro_bca(RiCtx *ctx, RiData file, const char *name);

/* A node's model-space matrix times the up scale: r (row-major, column
 * vectors) and t in fx (Q12). */
typedef struct NitroNodeMatrix
{
    int32_t r[9], t[3];
    uint8_t evaluated; /* the render commands reached this node */
} NitroNodeMatrix;

/* Runs the render commands' matrix steps at a whole frame of animation
 * (NULL: the model's rest pose); out has model->node_count rows. Frames past
 * the end hold the last one. */
void nitro_model_pose(RiCtx *ctx, const NitroModel *model, const NitroJointAnimation *animation,
                      uint32_t frame, NitroNodeMatrix *out);

/* Texture SRT animation of one material: one row per frame, already
 * expanded from the stepped tracks with the same linear interpolation. */
typedef struct NitroSrtFrame
{
    int32_t scale_s, scale_t, sin_r, cos_r, translate_s, translate_t; /* fx32 */
} NitroSrtFrame;

typedef struct NitroSrtTrack
{
    char material[NITRO_NAME_CAPACITY];
    RiBuf *frames; /* NitroSrtFrame, duration rows */
} NitroSrtTrack;

typedef struct NitroSrt
{
    uint32_t duration, texmtx_mode;
    NitroSrtTrack *tracks;
    size_t count;
} NitroSrt;

NitroSrt nitro_bta(RiCtx *ctx, RiData file, const char *name);

typedef struct NitroPatternKey
{
    uint32_t frame;
    char texture[NITRO_NAME_CAPACITY];
    char palette[NITRO_NAME_CAPACITY];
} NitroPatternKey;

typedef struct NitroPatternTrack
{
    char material[NITRO_NAME_CAPACITY];
    NitroPatternKey *keys;
    size_t count;
} NitroPatternTrack;

typedef struct NitroPattern
{
    uint32_t duration;
    NitroPatternTrack *tracks;
    size_t count;
} NitroPattern;

/* The first pattern animation of the file. */
NitroPattern nitro_btp(RiCtx *ctx, RiData file, const char *name);

/* Texture matrix of a material, in normalized texture coordinates: u' = m[0]u +
 * m[2]v + m[4], v' = m[1]u + m[3]v + m[5], all Q16. Follows the authoring tool
 * mode; unsupported modes fail. */
void nitro_texture_matrix(RiCtx *ctx, uint32_t mode, const NitroSrtFrame *srt, int32_t out[6],
                          const char *what);

#endif
