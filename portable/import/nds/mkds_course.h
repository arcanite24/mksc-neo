#ifndef PORTABLE_MKDS_COURSE_H
#define PORTABLE_MKDS_COURSE_H

/* One Mario Kart DS course folder into 3D course sections: C3D1 (render mesh
 * with vertex colours, alpha and texture animation, the camera-anchored sky,
 * and preview-only gameplay placeholders), its C3C1 catalog row and a TXB1
 * texture bundle. See ARCHITECTURE "Imported game content". */

#include "nds_fs.h"

typedef struct MkdsCourseSource
{
    const char *folder, *name, *cup_name;
    uint32_t index;     /* position in the importer's course table */
    uint32_t battle;    /* battle stage: Battle mode only */
    uint32_t cup, slot; /* DS cup and slot */
    uint32_t music;     /* kind-112 content ID of its theme, or 0 */
} MkdsCourseSource;

/* Content IDs keep the generated mod clear of authored mods' small IDs. */
#define MKDS_COURSE_ID(index) (0x4D4B4400u + (index) + 1u)
#define MKDS_CUP_ID(cup) (0x4D4B4300u + (cup) + 1u)
#define MKDS_BATTLE_CUP_ID 0x4D4B4342u
#define MKDS_TEXTURE_ID(index, texture) (0x4D000000u | ((index) + 1u) << 12 | (texture))
#define MKDS_TEXTURE_LIMIT 4096u
/* Kind-96 meshes of moving MapObj models and their kind-97 materials. */
#define MKDS_OBJECT_MESH_ID(index, model) (0x4D500000u | ((index) + 1u) << 8 | (model))
#define MKDS_OBJECT_MATERIAL_ID(index, model, slot) (0x4D600000u | ((index) + 1u) << 12 | (model) << 4 | (slot))
/* Kind 102 KPH2 profile the imported courses race with. */
#define MKDS_PHYSICS_ID 0x4D4B4450u

/* Adds the shared physics profile (the default modern profile) under `id`. */
void mkds_physics_section(RiCtx *ctx, uint32_t id);
/* The MIR1 item rules every course races with (mkds_rules.h). */
#define MKDS_ITEMS_ID 0x4D4B4449u
#define MKDS_ITEMS_VS_ID 0x4D4B4456u
#define MKDS_ITEMS_BATTLE_ID 0x4D4B4442u

/* Adds the course's sections; fails naming the folder and the unsupported
 * feature. rom is the cartridge filesystem. */
void mkds_course_convert(RiCtx *ctx, const NdsFs *rom, const MkdsCourseSource *source);

#endif
