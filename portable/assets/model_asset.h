#ifndef PORTABLE_MODEL_ASSET_H
#define PORTABLE_MODEL_ASSET_H

#include "asset_pack.h"
#include "native_image.h"

#define PORTABLE_MODEL_MAX_VERTICES 2048u
#define PORTABLE_MODEL_MAX_TRIANGLES 4096u
#define PORTABLE_MODEL_MAX_MATERIALS 16u
#define PORTABLE_MODEL_MAX_BINDINGS 256u
#define PORTABLE_MODEL_MAX_TEXTURE_EXTENT 1024u

enum
{
    PORTABLE_MODEL_DRIVER = 1,
    PORTABLE_MODEL_SCENERY = 2,
    /* Item presentation: target_id is the kind-103 item rules profile whose
     * races draw it; art_bank a PORTABLE_MODEL_ITEM_* slot; pose_override
     * PORTABLE_MODEL_ITEM_FLAG_* bits (not part of the lookup). */
    PORTABLE_MODEL_ITEM = 3,
    PORTABLE_MODEL_WRAP_U = 1,
    PORTABLE_MODEL_WRAP_V = 2,
    /* Lit by the course's model light (META mksc:model-* properties). */
    PORTABLE_MODEL_LIT = 4,
    /* Fades into the course fog. */
    PORTABLE_MODEL_FOG = 8,
    PORTABLE_MODEL_BLEND = 32, /* straight alpha blending (PORTABLE_RENDER_MATERIAL_ALPHA_BLEND) */
};

/* Item presentation slots (art_bank of PORTABLE_MODEL_ITEM bindings): an
 * item ID minus 0x1000 for that item held, trailing or thrown, then these. */
enum
{
    PORTABLE_MODEL_ITEM_BOX = 32,       /* item box; a RIG1 IDLE clip loops it */
    PORTABLE_MODEL_ITEM_BOX_SHARD = 33, /* one piece of a collected box */
    PORTABLE_MODEL_ITEM_BLAST = 34,     /* Bob-omb explosion */
    PORTABLE_MODEL_ITEM_INK = 35,       /* Blooper ink */
    PORTABLE_MODEL_ITEM_MODELS = 36,    /* slots below this are drawn in the world */
    /* HUD icon of item ID 0x1000 + n: material 0's texture of the slot's mesh. */
    PORTABLE_MODEL_ITEM_ICON = 40,
    PORTABLE_MODEL_ITEM_ICONS = 24,
    PORTABLE_MODEL_ITEM_SLOTS = PORTABLE_MODEL_ITEM_ICON + PORTABLE_MODEL_ITEM_ICONS,
    PORTABLE_MODEL_ITEM_FLAG_BILLBOARD = 1, /* camera-facing about the up axis */
    PORTABLE_MODEL_ITEM_FLAG_SPIN = 2,      /* turns about the up axis */
    PORTABLE_MODEL_ITEM_FLAGS = 3
};

typedef struct
{
    uint32_t argb;
    uint32_t flags;
    PortableNativeImage texture; /* Empty pixels select a solid material. */
} PortableModelMaterial;

typedef struct
{
    const uint8_t *vertices;  /* XYZUV: five signed little-endian Q16 words. */
    const uint8_t *triangles; /* Three u16 indices and a u16 material slot. */
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t material_count;
    PortableModelMaterial materials[PORTABLE_MODEL_MAX_MATERIALS];
} PortableModelView;

typedef struct
{
    uint32_t kind;
    uint32_t target_id;     /* Manifest driver ID or manifest course ID. */
    uint32_t art_bank;      /* Driver: zero; scenery: course-local art bank. */
    uint32_t pose_override; /* Exact source override, 0 ordinary, 1..31 special. */
    uint32_t mesh_id;
    int32_t scale_q16[3];
    int32_t pivot_q16[3];
    PortableModelView mesh;
} PortableModelBinding;

typedef struct PortableModelCatalog
{
    PortableModelBinding *bindings;
    size_t count;
} PortableModelCatalog;

/* Owned normalized v1 registry. Zero-initialize before prepare/close; do not
 * shallow-copy ownership. section.bytes aliases bytes, never this struct. */
typedef struct
{
    uint8_t *bytes;
    size_t size;
    PortableAssetSectionView section;
} PortableModelBindingOverlay;
/* Merge raw v2 rows over a nullable v1 predecessor. All old rows matching an
 * incoming four-word key are removed; incoming duplicates/invalid rows remain
 * verbatim for the final catalog's existing fallback policy. Invalid v1
 * envelopes inherit as empty; unsupported predecessor versions fail. Stable
 * numeric key order retains original ordinal among duplicate keys. No mesh
 * references are resolved here. Success replaces the old owner; any failure
 * preserves it. Inputs may borrow the old owner's bytes during this call. */
int portable_model_bindings_overlay_prepare(const PortableAssetSectionView *previous,
                                            const PortableAssetSectionView *overlay,
                                            PortableModelBindingOverlay *out, char *error,
                                            size_t capacity);
void portable_model_bindings_overlay_close(PortableModelBindingOverlay *);

/* Zero-initialize before first open. The pack and its payloads must outlive the
 * catalog. Invalid optional rows warn and fall back to billboard art. Missing or
 * invalid registries are an empty success; only bad arguments/allocation fail,
 * leaving the previous catalog intact. Successful open replaces the old owner. */
int portable_model_catalog_open(PortableModelCatalog *catalog, const PortableAssetPackView *pack);
void portable_model_catalog_close(PortableModelCatalog *catalog);
const PortableModelBinding *portable_model_catalog_find(const PortableModelCatalog *catalog,
                                                        uint32_t kind, uint32_t target_id,
                                                        uint32_t art_bank, uint32_t pose_override);
/* The PORTABLE_MODEL_ITEM binding of a profile's slot, whatever its flags. */
const PortableModelBinding *portable_model_catalog_find_item(const PortableModelCatalog *catalog,
                                                             uint32_t profile_id, uint32_t slot);
int portable_model_vertex(const PortableModelView *mesh, size_t index, int32_t xyz_q16[3],
                          int32_t uv_q16[2]);
int portable_model_triangle(const PortableModelView *mesh, size_t index, uint16_t indices[3],
                            uint16_t *material);
/* Optional kind109 RIG1 skeleton of a mesh (content ID = mesh ID): one
 * node per vertex (255 = static), clips of whole frames whose rows hold a
 * row-major 3x4 Q16 transform per node relative to the mesh's rest pose,
 * and material swaps that show another material slot for the last frames
 * of a looping period (a blink); swaps of one slot (one period, rising
 * starts) chain into a flipbook. See ARCHITECTURE "3D driver rigs". */
#define PORTABLE_MODEL_RIG_MAX_NODES 64u
#define PORTABLE_MODEL_RIG_MAX_CLIPS 16u
#define PORTABLE_MODEL_RIG_MAX_SWAPS 8u
#define PORTABLE_MODEL_RIG_STATIC 255u
enum
{
    PORTABLE_MODEL_CLIP_LOOP = 1
};
typedef struct
{
    uint32_t tag; /* four ASCII bytes, first in the low byte: DRIV, SPIN, WIN, LOSE */
    uint32_t frames, first_pose, flags;
} PortableModelClip;
typedef struct
{
    uint32_t slot, alternate, period, start;
} PortableModelSwap;
typedef struct
{
    uint32_t vertex_count, node_count, clip_count, pose_count, swap_count;
    const uint8_t *nodes, *poses;
    PortableModelClip clips[PORTABLE_MODEL_RIG_MAX_CLIPS];
    PortableModelSwap swaps[PORTABLE_MODEL_RIG_MAX_SWAPS];
} PortableModelRig;
#define PORTABLE_MODEL_CLIP_TAG(a, b, c, d) \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
/* 1 with a rig that fits mesh; 0 without one (a malformed rig warns). */
int portable_model_rig_open(const PortableAssetPackView *pack, uint32_t mesh_id,
                            const PortableModelView *mesh, PortableModelRig *out);
/* The clip with tag, or -1. */
int portable_model_rig_clip(const PortableModelRig *rig, uint32_t tag);
/* A node's Q16 transform at a clip frame (clamped to the last frame). */
void portable_model_rig_pose(const PortableModelRig *rig, uint32_t clip, uint32_t frame, uint32_t node,
                             int32_t out[12]);

/* Kind110 DRV3 rows (content ID = driver ID) describe drivers of the 3D
 * domain: the stock donor (1..8) whose voice and 2D fallbacks they use,
 * whether the grid lists them as 3D-only drivers, Q16 ratios applied to the
 * course's kart physics, a kind97 portrait material and a name. */
#define PORTABLE_DRIVER_3D_CAPACITY 64u
enum
{
    PORTABLE_DRIVER_3D_SELECTABLE = 1
};
typedef struct
{
    uint32_t id, donor, flags, portrait;
    int32_t mass_q16, speed_q16, acceleration_q16, handling_q16;
    char name[32];
} PortableDriver3D;
int portable_driver_3d_find(const PortableAssetPackView *pack, uint32_t id, PortableDriver3D *out);
/* Valid rows in ascending ID order; malformed rows warn and are skipped. */
size_t portable_driver_3d_list(const PortableAssetPackView *pack, PortableDriver3D *out, size_t capacity);

/* Kind111 KRT3 rows (content ID = kart ID) describe karts of the 3D domain:
 * a kind96 body mesh with its pivot, Q16 ratios applied to the course's kart
 * physics on top of the driver's, the driver whose kart it is (flag
 * OWNER_DEFAULT: the one that driver races by default), a name and each
 * driver's seat, an offset in mesh units from the kart origin to the driver
 * model's origin. Flag NO_SHADOW drops the host's footprint blob under the
 * kart (sources without one, such as Mario Kart DS). */
#define PORTABLE_KART_3D_CAPACITY 64u
#define PORTABLE_KART_3D_SEATS 16u
enum
{
    PORTABLE_KART_3D_SELECTABLE = 1,
    PORTABLE_KART_3D_OWNER_DEFAULT = 2,
    PORTABLE_KART_3D_NO_SHADOW = 4
};
typedef struct
{
    uint32_t id, owner, flags, mesh_id;
    int32_t pivot_q16[3];
    int32_t speed_q16, acceleration_q16, handling_q16, mass_q16;
    uint32_t seat_count;
    uint32_t seat_driver[PORTABLE_KART_3D_SEATS];
    int32_t seat_q16[PORTABLE_KART_3D_SEATS][3];
    char name[32];
} PortableKart3D;
int portable_kart_3d_find(const PortableAssetPackView *pack, uint32_t id, PortableKart3D *out);
/* Valid rows in ascending ID order; malformed rows warn and are skipped. */
size_t portable_kart_3d_list(const PortableAssetPackView *pack, PortableKart3D *out, size_t capacity);
/* The kart driver_id races when it chooses none; 0 when it has none. */
uint32_t portable_kart_3d_default(const PortableAssetPackView *pack, uint32_t driver_id);
/* driver_id's seat, else the owner's, else the first; 0 for a kart seating no one. */
int portable_kart_3d_seat(const PortableKart3D *kart, uint32_t driver_id, int32_t out[3]);

/* Direct kind96 mesh references (for placed course objects). Failure preserves out. */
int portable_model_mesh_open(const PortableAssetPackView *pack, uint32_t id,
                             PortableModelView *out);
/* Direct kind97 material, including its borrowed IMG1 pixels. Failure preserves out. */
int portable_model_material_open(const PortableAssetPackView *pack, uint32_t id,
                                 PortableModelMaterial *out);

#endif
