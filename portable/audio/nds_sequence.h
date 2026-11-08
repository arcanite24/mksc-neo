#ifndef PORTABLE_NDS_SEQUENCE_H
#define PORTABLE_NDS_SEQUENCE_H

#include "audio_pcm_mixer.h"

#include <stddef.h>
#include <stdint.h>

/* Nintendo DS sequenced music (SSEQ with its SBNK instruments and SWAR waves)
 * as one self-contained kind-112 section, played by a native sequencer with
 * DS envelopes, LFO, sweep/portamento and PCM8/PCM16/IMA-ADPCM/PSG voices.
 *
 * Section "MKSCNSQ1" v1 or v2, little endian:
 *   0  magic[8]   8 header size u16 (48)  10 version u16 (1 or 2)
 *   12 volume u8 (0..127, the SDAT player volume)  13 player u8
 *   14 follow u16: the kind-112 section started on the same player when this
 *      song ends (a finish fanfare's results theme), 0 none
 *   16 sequence offset u32, size u32 (SSEQ command bytes; jumps are relative)
 *   24 program offset u32, count u32 (4 bytes: first region u16, region count u16)
 *   32 region offset u32, count u32 (12 bytes: low key, high key, kind
 *      1 PCM / 2 PSG square / 3 noise, base key, attack, decay, sustain,
 *      release, pan, zero, wave index (PCM) or duty 0..6 (PSG) u16)
 *   40 wave offset u32, count u32 (24 bytes: encoding 0 PCM8 / 1 PCM16 /
 *      2 ADPCM, loop u8, zero u16, timer u16, zero u16, loop start u32 and
 *      sample count u32 in samples, data offset u32, data size u32; ADPCM data
 *      starts with its 4-byte predictor/index header).
 * v2 (header size 52) adds 48 start u32: where track 0 begins within the
 * sequence bytes (a sound-effect archive entry inside its shared block). */
#define PORTABLE_NDS_SEQUENCE_HEADER_SIZE 48u
#define PORTABLE_NDS_SEQUENCE_HEADER_SIZE_V2 52u
#define PORTABLE_NDS_TRACKS 16u
#define PORTABLE_NDS_CHANNELS 16u
#define PORTABLE_NDS_CALL_DEPTH 3u

typedef struct
{
    const uint8_t *bytes;
    uint32_t size;
    uint8_t volume, player;
    uint16_t follow;
    uint32_t sequence_offset, sequence_size;
    uint32_t program_offset, program_count;
    uint32_t region_offset, region_count;
    uint32_t wave_offset, wave_count;
    uint32_t start;
} PortableNdsSequenceView;

typedef struct
{
    uint32_t position, returns[PORTABLE_NDS_CALL_DEPTH], loop_start[PORTABLE_NDS_CALL_DEPTH];
    int32_t wait;
    uint16_t program, mod_delay;
    int16_t sweep_pitch;
    uint8_t active, depth, loop_count[PORTABLE_NDS_CALL_DEPTH], note_wait, tie, porta, compare;
    uint8_t note_finish_wait; /* a 0-length note: the track waits until its channels stop */
    uint8_t volume, expression, pan, priority, bend_range, porta_key, porta_time;
    int8_t transpose, bend;
    uint8_t attack, decay, sustain, release; /* 0xff: the instrument's */
    uint8_t mod_depth, mod_speed, mod_type, mod_range;
    uint8_t tie_channel;
} PortableNdsTrack;

typedef struct
{
    const uint8_t *data;
    uint32_t sample_count, loop_start, index, sweep_length, sweep_counter;
    uint64_t fraction, step;
    int32_t amplitude, sustain_level, length, predictor, step_index, loop_predictor, loop_step_index;
    int32_t gain_left, gain_right;
    uint16_t decay_rate, release_rate, timer, lfo_counter, lfo_delay_counter, mod_delay, noise;
    int16_t previous, current, sweep_pitch;
    uint8_t active, kind, encoding, loop, ended, loop_saved, track, state, key, base_key, velocity;
    uint8_t pan, priority, attack_rate, duty, phase, mod_depth, mod_speed, mod_type, mod_range;
} PortableNdsChannel;

typedef enum
{
    PORTABLE_NDS_OK = 0,
    PORTABLE_NDS_INVALID_ARGUMENT,
    PORTABLE_NDS_BAD_SECTION,
    PORTABLE_NDS_BAD_COMMAND,
    PORTABLE_NDS_BAD_JUMP,
    PORTABLE_NDS_RUNAWAY_TRACK
} PortableNdsStatus;

typedef struct
{
    PortableNdsSequenceView song;
    PortableNdsTrack tracks[PORTABLE_NDS_TRACKS];
    PortableNdsChannel channels[PORTABLE_NDS_CHANNELS];
    int16_t variables[32];
    uint64_t clock;
    uint32_t random, tempo_counter, failed_at;
    uint16_t tempo, tempo_scale, volume; /* tempo_scale and volume: 256 = unity */
    int16_t pitch;                       /* 1/256 semitone, as SET_PITCH */
    uint8_t master_volume, active, paused;
    PortableNdsStatus status;
} PortableNdsPlayer;

PortableNdsStatus portable_nds_sequence_open(PortableNdsSequenceView *view, const uint8_t *bytes, size_t size);
/* Starts the song from its first track; a song stays active until every
 * track has ended and every voice has died away. */
void portable_nds_player_start(PortableNdsPlayer *player, const PortableNdsSequenceView *song);
void portable_nds_player_stop(PortableNdsPlayer *player);
int portable_nds_player_playing(const PortableNdsPlayer *player);
/* Advances one game tick and adds its voices into the stereo frame. */
PortableNdsStatus portable_nds_player_mix(PortableNdsPlayer *player,
                                          int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
const char *portable_nds_status_name(PortableNdsStatus status);

#endif
