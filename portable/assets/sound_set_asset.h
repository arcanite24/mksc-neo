#ifndef MKSC_PORTABLE_SOUND_SET_ASSET_H
#define MKSC_PORTABLE_SOUND_SET_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "asset_pack.h"

/* Kind 114 MKSCSND1: the sounds a content platform plays in place of the
 * stock ones while racing its courses (META integer `mksc:sound-set` names
 * the section). 24-byte header (magic[8], u16 header size 24, u16 version 1,
 * 2 or 3, u16 cue count, u16 voice count, u16 engine pitch range in 1/256
 * semitone at top speed (0: the stock range), s16 engine pitch at rest (v2+;
 * zero in v1), u16 final-lap music tempo (v3, 256 = unity, 0: the stock
 * 125%; zero before v3), u16 zero), then cue rows (u16 cue, u16 song: kind-82
 * ID, a kind-112 ID | 0x8000 or a kind-115 song | 0x4000
 * (audio_command_stream.h); 0 is silence) and voice rows (u32 driver ID,
 * u16 boost voice song, u16 struck voice song; v2 adds u16 engine song, 0 for
 * the set's engine cues, and u16 zero; v3 then adds
 * PORTABLE_SOUND_VOICE_EVENTS u16 songs, PortableSoundVoiceEvent order, 0
 * for none).
 * A cue below 0x8000 is the stock song it replaces; the cues below have no
 * stock song and play only when a set provides them. Surface cues carry the
 * floor's SURF `mksc:sound-surface` class (1..63) in their low six bits. */
enum
{
    PORTABLE_SOUND_CUE_ENGINE_IDLE = 0xff01u,
    PORTABLE_SOUND_CUE_ENGINE_COAST = 0xff02u,
    PORTABLE_SOUND_CUE_ENGINE_THROTTLE = 0xff03u,
    PORTABLE_SOUND_CUE_DRIFT = 0xff04u,        /* held while a drift lasts */
    PORTABLE_SOUND_CUE_SPARK_FIRST = 0xff05u,  /* the drift's first mini-turbo tier */
    PORTABLE_SOUND_CUE_SPARK_SECOND = 0xff06u, /* its second and later tiers */
    PORTABLE_SOUND_CUE_LAP = 0xff07u,          /* replaces the stock lap cue */
    PORTABLE_SOUND_CUE_SURFACE_DRIFT = 0xfc00u, /* | class: drift on that floor, else CUE_DRIFT */
    PORTABLE_SOUND_CUE_SURFACE_LAND = 0xfc40u,  /* | class: landing on it, else the stock landing */
    PORTABLE_SOUND_CUE_SURFACE_RUN = 0xfc80u,   /* | class: held while driving over it */
};
#define PORTABLE_SOUND_SURFACE_CLASSES 64u
/* v3 per-driver voices beyond boost and struck. */
typedef enum
{
    PORTABLE_SOUND_VOICE_START_BOOST,   /* a rocket start */
    PORTABLE_SOUND_VOICE_HEAVY_HIT,     /* tumbled by a shell or bomb, crushed */
    PORTABLE_SOUND_VOICE_ATTACK,        /* the racer's item hits a rival */
    PORTABLE_SOUND_VOICE_OVERTAKE,      /* the racer moves up a place */
    PORTABLE_SOUND_VOICE_FINISH_FIRST,  /* finishes first */
    PORTABLE_SOUND_VOICE_FINISH_PLACED, /* finishes on the podium */
    PORTABLE_SOUND_VOICE_FINISH_LOST,   /* finishes off it */
    PORTABLE_SOUND_VOICE_BATTLE_WIN,
    PORTABLE_SOUND_VOICE_BATTLE_LOSE,
    PORTABLE_SOUND_VOICE_RESERVED,
    PORTABLE_SOUND_VOICE_EVENTS
} PortableSoundVoiceEvent;
#define PORTABLE_SOUND_SET_MAGIC "MKSCSND1"
#define PORTABLE_SOUND_SET_ROW_LIMIT 512u

typedef struct
{
    const uint8_t *cues, *voices;
    uint32_t cue_count, voice_count, voice_size;
    uint16_t engine_pitch_range;
    int16_t engine_pitch_base;
    uint16_t final_lap_tempo; /* 256 = unity; 0 keeps the stock tempo */
} PortableSoundSet;

/* 1 with a valid section; the view borrows the section bytes. */
int portable_sound_set_open(const PortableAssetSectionView *section, PortableSoundSet *out);
int portable_sound_set_find(const PortableAssetPackView *pack, uint32_t id, PortableSoundSet *out);
/* 1 when the set replaces `cue`, with its song in *song (0: silence). */
int portable_sound_set_cue(const PortableSoundSet *set, uint16_t cue, uint16_t *song);
/* 1 when the set voices `driver_id`. */
int portable_sound_set_voice(const PortableSoundSet *set, uint32_t driver_id, uint16_t *boost, uint16_t *struck);
/* 1 when the set gives `driver_id` its own engine song. */
int portable_sound_set_engine(const PortableSoundSet *set, uint32_t driver_id, uint16_t *engine);
/* 1 when a v3 set gives `driver_id` a voice for `event` (nonzero song). */
int portable_sound_set_voice_event(const PortableSoundSet *set, uint32_t driver_id, PortableSoundVoiceEvent event,
                                   uint16_t *song);

#endif
