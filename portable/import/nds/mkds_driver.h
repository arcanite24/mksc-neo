#ifndef PORTABLE_MKDS_DRIVER_H
#define PORTABLE_MKDS_DRIVER_H

/* Mario Kart DS drivers and karts into the optional 3D model sections. A
 * driver is a kind-96 mesh at its own origin with kind-97 materials, a
 * kind-109 rig (drive, spin, win and lose clips and the blink) and a kind-110
 * 3D driver row; the importer binds every driver mesh with one kind-98
 * overlay. A kart is a mesh (body and tires) with a kind-111 row holding its
 * stats and every driver's seat. See ARCHITECTURE "Imported game content". */

#include "nds_fs.h"

typedef struct MkdsDriverSource
{
    const char *folder, *code, *name;
    uint32_t index;        /* position in the importer's driver table */
    uint32_t ds_id;        /* the game's character number: kart palette row, seat and weight */
    uint32_t driver_id;    /* our 3D-only driver ID */
    uint32_t donor;        /* stock driver lending voice and 2D fallbacks */
    uint32_t main_archive; /* the player model ships in KartModelMain */
} MkdsDriverSource;

typedef struct MkdsKartSource
{
    uint32_t index;                       /* kartoffsetdata row: character number * 3 + variant */
    const char *name;                     /* the game's menu name */
    const char *kart_folder, *kart_code;  /* the owner's own karts in KartModelMain */
    const MkdsDriverSource *owner;        /* the character the kart belongs to */
    const MkdsDriverSource *drivers;      /* everyone it seats */
    size_t driver_count;
} MkdsKartSource;

#define MKDS_KART_COUNT 37u
#define MKDS_KART_SEATS 16u

#define MKDS_DRIVER_MESH_ID(index) (0x4D4B4D00u + (index) + 1u)
#define MKDS_DRIVER_MATERIAL_ID(index, slot) (0x4D4B6D00u | ((index) + 1u) << 4 | (slot))
#define MKDS_KART_ID(index) (0x4D4B4B00u + (index) + 1u) /* kind-111 row and its mesh */
#define MKDS_KART_MATERIAL_ID(index, slot) (0x4D4C0000u + ((index) + 1u) * 16u + (slot))

/* Adds the driver's sections and appends its kind-98 binding row (44 bytes)
 * to bindings. Fails naming the driver and the unsupported feature. */
void mkds_driver_convert(RiCtx *ctx, const NdsFs *rom, const MkdsDriverSource *source, RiBuf *bindings);
/* Adds the kart's mesh, materials and kind-111 row. */
void mkds_kart_convert(RiCtx *ctx, const NdsFs *rom, const MkdsKartSource *source);
/* Adds the kart's shadow material and appends its 32-byte FXS1 shadow row. */
void mkds_kart_shadow(RiCtx *ctx, const NdsFs *rom, const MkdsKartSource *source, RiBuf *rows);

#endif
