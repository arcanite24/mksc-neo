#ifndef PORTABLE_MKDS_SOUND_H
#define PORTABLE_MKDS_SOUND_H

/* Mario Kart DS music and sound effects: one SDAT sequence, or one entry of
 * a sequence archive (SSAR), with its SBNK instruments and the SWAR waves
 * they use becomes a self-contained kind-112 NDS sequence
 * (portable/audio/nds_sequence.h). */

#include "nds_fs.h"

#define MKDS_SOUND_SECTION_KIND 112u
/* Content ID of SDAT sequence n. */
#define MKDS_MUSIC_ID(sequence) ((sequence) + 1u)
/* Content ID of sequence-archive `archive` entry `index` (below 0x8000). */
#define MKDS_EFFECT_ID(archive, index) (0x1000u * ((archive) + 1u) + (index))
#define MKDS_SOUND_SET_KIND 114u
#define MKDS_SOUND_SET_ID 0x4D4B5353u

/* DS floor materials (CSfxFloorMat); SURF mksc:sound-surface is material + 1. */
#define MKDS_FLOOR_MATERIALS 19u
/* Adds the section for SDAT sequence `sequence`, chaining kind-112 song
 * `follow` (0 none) when it ends; fails naming the sequence and the
 * unsupported bank or wave feature. */
void mkds_sound_sequence(RiCtx *ctx, RiData sdat, uint32_t sequence, uint32_t follow);
/* Adds section `id` for SSAR `archive` entry `index` on player slot `player`:
 * only the commands it reaches and the programs they select are kept. */
void mkds_sound_effect(RiCtx *ctx, RiData sdat, uint32_t archive, uint32_t index, uint32_t player, uint32_t id);
/* Adds the kind-114 v3 sound set every DS course races with (META
 * mksc:sound-set), with the event sequences it names: DS engine, floor,
 * item and race effects and fanfares in place of the stock cues, and each
 * driver's own voices. */
void mkds_sound_set(RiCtx *ctx, RiData sdat, const uint32_t *driver_ids, const uint8_t *ds_ids, uint32_t drivers);

/* The positional sound of MapObj `object` on course folder `course`: SSAR 0
 * entry `effect` (kept by mkds_sound_set, so its song is
 * 0x8000 | MKDS_EFFECT_ID(0, effect)), heard within `radius` metres and
 * restarted every `period` ticks (0: when it ends). NULL when silent. */
typedef struct
{
    const char *course;
    uint16_t object, effect, radius, period;
} MkdsObjectSound;
const MkdsObjectSound *mkds_object_sound(const char *course, uint32_t object);

#endif
