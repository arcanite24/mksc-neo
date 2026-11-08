#ifndef PORTABLE_MK64_INTERNAL_H
#define PORTABLE_MK64_INTERNAL_H
#include "rom_import_internal.h"
#include "mk64_import.h"

#define MK64_COURSE_ID(index) (0x4E363400u + (index) + 1u)
#define MK64_CUP_ID(index) (0x4E364300u + (index) + 1u)
#define MK64_COURSE_TABLE 0x122390u
#define MK64_TEXTURE_BASE 0x641F70u
/* Kind 102 profile the imported race courses drive with, and the kind 103
 * item rules every course and arena uses. */
#define MK64_PHYSICS_ID 0x4E363450u
#define MK64_ITEMS_ID 0x4E363449u /* Grand Prix odds */
#define MK64_ITEMS_VS_ID 0x4E363456u
#define MK64_ITEMS_BATTLE_ID 0x4E363442u

typedef struct Mk64CourseSource {
    const char *name;
    uint32_t index, cup, slot, surfaces;
    uint32_t collision_lists[5]; /* arenas use explicit lists, not TrackSections */
    uint8_t battle, arena_surface;
    uint8_t opaque; /* course renderer leaves G_RM_AA_ZB_OPA_SURF set, not TEX_EDGE */
    uint8_t grid_on_axis; /* spawn_players.c centres the grid on x = 0, not path point 0 */
    uint8_t water;        /* water_level is the course's constant fall height */
    int16_t water_level;
    uint32_t item_boxes;  /* segment-6 ActorSpawnData list (actors.c spawn_all_item_boxes) */
    uint8_t scenery;      /* convert the placed scenery mk64_object.c lists for this index */
    /* Arenas (spawn_players.c 3P/4P battle): starts at distance d on the axes, height h;
     * ring faces them along Big Donut's ring instead of the centre. */
    int16_t arena_distance, arena_height;
    uint8_t arena_ring;
} Mk64CourseSource;

/* Kind-96 meshes of placed scenery and their kind-97 materials. */
#define MK64_OBJECT_MESH_ID(index, slot) (0x4E500000u | ((index) + 1u) << 8 | (slot))
#define MK64_OBJECT_MATERIAL_ID(index, slot, material) (0x4E600000u | ((index) + 1u) << 12 | (slot) << 4 | (material))
#define MK64_COMMON_DATA 0x132B50u /* segment 13, MIO0 */
#define MK64_COMMON_DATA_END 0x145470u

/* 17.6 source units per metre (see mk64_course.c), in Q16 metres. */
static inline int32_t mk64_metres(int32_t v) { return (int32_t)((int64_t)v * 65536 * 5 / 88); }
/* Most racers' collision sphere, tenths of a source unit
 * (gKartBoundingBoxSizeTable); META mksc:kart-scale is its share of the
 * stock kart's 0.5 m radius. */
#define MK64_KART_SPHERE_TENTHS 55

uint32_t mk64_be16(const uint8_t *p);
uint32_t mk64_be32(const uint8_t *p);
int32_t mk64_i16(const uint8_t *p);
RiBuf *mk64_normalize(RiCtx *ctx, RiData data);
RiBuf *mk64_mio0(RiCtx *ctx, RiData data);
/* Also the block's end: blocks packed back to back start 4-aligned after it. */
RiBuf *mk64_mio0_block(RiCtx *ctx, RiData data, size_t *end);
/* The next 4-aligned MIO0 block after the one at `at`, ROM offset. */
size_t mk64_mio0_next(RiCtx *ctx, size_t at);
/* Packed segment 7 -> bounded F3DEX data for conversion, not a hardware layer. */
RiBuf *mk64_display_lists(RiCtx *ctx, RiData packed, uint32_t expected_size);
void mk64_course_convert(RiCtx *ctx, const Mk64CourseSource *source);

/* A course's RSP segments while converting: 3 actor textures, 5 course
 * textures, 6 course data, 7 expanded lists, 13 common data. */
typedef struct Mk64Segments {
    RiData segment[16];
} Mk64Segments;
/* Segment 3 as init_actors_and_load_textures fills it for course index. */
RiData mk64_actor_textures(RiCtx *ctx, uint32_t index);
/* Height of the floor under at (Q16 metres), 0 when none. */
typedef int (*Mk64Floor)(void *context, const int32_t at[3], int32_t *out);
struct MkdsModel;
/* Placed scenery as OBJS rows over kind-96 meshes (mk64_object.c). */
/* race: path 0 (engine Q16 metres) for traffic, NULL on arenas. */
void mk64_objects(struct MkdsModel *course, const Mk64Segments *segments, uint32_t index, Mk64Floor floor,
                  Mk64Floor surface,
                  void *context, const int32_t (*race)[3], uint32_t race_count);
/* The eight racers: KSP1 sprites, DRV3 rows and portraits (mk64_driver.c),
 * numbered in their stock donors' grid order (Mario 0 .. Bowser 7). */
/* Clear of the DS import's driver IDs (200-212), so both games compose. */
#define MK64_DRIVER_ID(index) (220u + (index))
#define MK64_PORTRAIT_ID(index) (0x4E700000u | (index))
void mk64_drivers(RiCtx *ctx);
/* Kind-116 effect set (kart shadow and particle art) and its materials. */
#define MK64_FX_SET_ID 0x4E364658u
#define MK64_FX_MATERIAL_ID(n) (0x4E465800u + (n))
void mk64_effects(RiCtx *ctx);
/* Item, item box and shard meshes from segment 13 and the shell frames, with
 * one BND1 row (PORTABLE_MODEL_ITEM) per model and item rules profile;
 * returns the row count (mk64_object.c). */
uint32_t mk64_item_art(RiCtx *ctx, const uint32_t *profiles, size_t profile_count, RiBuf *bindings);
/* Audio (mk64_sound.c): kind-115 N64 songs (portable/audio/n64_sequence.h),
 * one per used sequence with its bank, and the kind-114 sound set every
 * course races with. START_SONG arguments carry 0x4000; effects are
 * 0x2000 | bank << 8 | sound of the effect sequence (song 1). */
#define MK64_SONG_KIND 115u
#define MK64_SONG_ID(sequence) ((sequence) + 1u)
#define MK64_SONG(sequence) (0x4000u | MK64_SONG_ID(sequence))
#define MK64_EFFECT(bank, sound) (0x6000u | (uint32_t)(bank) << 8 | (uint32_t)(sound))
#define MK64_SOUND_SET_ID 0x4E363453u
void mk64_sounds(RiCtx *ctx);
/* The song ID of course index's music (META music, kind 115). */
uint32_t mk64_course_song(uint32_t index);
/* The shared modern kart profile and item rules, once per pack. */
void mk64_shared_sections(RiCtx *ctx);
#endif
