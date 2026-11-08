#ifndef PORTABLE_N64_SEQUENCE_H
#define PORTABLE_N64_SEQUENCE_H

#include "audio_pcm_mixer.h"

#include <stddef.h>
#include <stdint.h>

/* Nintendo 64 sequenced audio (the Mario Kart 64 sound driver, the "EU"
 * revision of the SM64 one) as one self-contained kind-115 section: an M64
 * sequence and the instrument bank it plays, with VADPCM samples. A native
 * sequencer runs the sequence, channel and layer scripts at the original
 * tatum rate (three audio updates per game tick), with the original ADSR,
 * vibrato, portamento, note stealing and a single delay-line reverb.
 *
 * Section "MKSCN64S" v1, little endian:
 *   0  magic[8]  8 header size u16 (72)  10 version u16 (1)
 *   12 player u8 (default session slot)  13 variation s8 (seq_getvariation;
 *      -1 for music, 0 for the one-player sound-effect sequence)
 *   14 follow u16: START_SONG argument started on the same slot when this
 *      song ends (MK64 chains a finish fanfare into its results), 0 none
 *   16 effect channels u8[8]: per sound bank, first channel << 4 | count
 *      (0: the bank has no channel; only a sound-effect sequence has any)
 *   24 sequence offset u32, size u32 (big-endian M64 bytes)
 *   32 instrument offset u32, count u32 (32 bytes: present u8, normal range
 *      low u8, high u8, release rate u8, envelope u32, then the low, normal
 *      and high sounds: sample u16 (0xffff none), zero u16, tuning f32)
 *   40 drum offset u32, count u32 (16 bytes: present u8, release rate u8,
 *      pan u8, zero u8, envelope u32, sample u16, zero u16, tuning f32)
 *   48 sample offset u32, count u32 (28 bytes: VADPCM data offset u32, size
 *      u32, book offset u32, loop start u32, loop end u32, loop count u32
 *      (0 plays once, else loops forever), loop state offset u32)
 *   56 data offset u32, size u32
 *   64 bank players u8[8]: the session slot each sound bank's effects use
 *      when started without one.
 * Envelope, book, loop-state and sample offsets are relative to the data
 * block, whose bytes keep the ROM's big-endian layouts: envelopes are s16
 * delay/argument pairs, books are s32 order (2), s32 predictor count and
 * s16 coefficients, loop states are s16[16]. */
#define PORTABLE_N64_SEQUENCE_HEADER_SIZE 72u
#define PORTABLE_N64_CHANNELS 16u
#define PORTABLE_N64_LAYERS 4u
#define PORTABLE_N64_NOTES 24u
#define PORTABLE_N64_STACK 4u
#define PORTABLE_N64_PATCHES 64u
#define PORTABLE_N64_SOUND_BANKS 8u
#define PORTABLE_N64_REVERB_SAMPLES 1004u /* 0x28 * 64 frames at 26800 Hz, at the session rate */

typedef struct
{
    const uint8_t *bytes;
    uint32_t size;
    uint8_t player;
    int8_t variation;
    uint16_t follow;
    uint8_t effect_channels[PORTABLE_N64_SOUND_BANKS], bank_players[PORTABLE_N64_SOUND_BANKS];
    uint32_t sequence_offset, sequence_size;
    uint32_t instrument_offset, instrument_count;
    uint32_t drum_offset, drum_count;
    uint32_t sample_offset, sample_count;
    uint32_t data_offset, data_size;
} PortableN64SequenceView;

typedef struct
{
    uint32_t pc, stack[PORTABLE_N64_STACK];
    uint8_t depth, loops[PORTABLE_N64_STACK];
} PortableN64Script;

/* An envelope is a section offset; a bank sound is (sample, tuning). */
typedef struct
{
    uint32_t envelope;
    uint8_t release;
    uint8_t sustain;
} PortableN64Adsr;

typedef struct
{
    float extent, cur, speed;
    uint8_t mode;
} PortableN64Portamento;

typedef struct
{
    PortableN64Script script;
    PortableN64Adsr adsr;
    PortableN64Portamento portamento;
    float freq_scale, velocity_square, note_freq_scale, note_velocity;
    uint16_t delay, duration, play_percentage, short_default;
    int16_t instrument; /* bank instrument, -1 none */
    uint16_t sample;    /* the sounding sound's sample, 0xffff none (synthetic or missing) */
    uint32_t sound;     /* identity of the bank sound, for continuous notes */
    float tuning;
    int8_t transposition;
    int8_t note; /* the note it owns, -1 none */
    uint8_t allocated, enabled, finished, stop_something, continuous, ignore_drum_pan, status;
    uint16_t portamento_time;
    uint8_t properties_init, inst_or_wave, pan, note_pan, note_duration, portamento_target;
} PortableN64Layer;

typedef struct
{
    PortableN64Script script;
    PortableN64Adsr adsr;
    PortableN64Layer layers[PORTABLE_N64_LAYERS];
    float volume, volume_scale, freq_scale, applied_volume;
    uint32_t dyn_table;
    uint16_t delay, vibrato_rate_target, vibrato_rate_start, vibrato_extent_target, vibrato_extent_start;
    uint16_t vibrato_rate_delay, vibrato_extent_delay, vibrato_delay;
    int16_t instrument; /* -1 none */
    int8_t io[8], value, transposition;
    uint8_t valid, enabled, finished, stop_script, has_instrument, inst_or_wave, large_notes, changes;
    uint8_t new_pan, pan_weight, priority, alloc_policy, mute_behavior, reverb, book_offset;
    uint16_t pan;
} PortableN64Channel;

typedef struct
{
    float current, target, velocity, fade_out, sustain;
    uint32_t envelope;
    int16_t delay;
    uint8_t state, action, index;
} PortableN64AdsrState;

typedef struct
{
    float extent, rate;
    uint32_t time;
    uint16_t delay, extent_timer, rate_timer;
    uint8_t active, channel;
} PortableN64Vibrato;

typedef struct
{
    PortableN64AdsrState adsr;
    PortableN64Vibrato vibrato;
    PortableN64Portamento portamento;
    float vibrato_scale, portamento_scale;
    float attr_freq_scale, attr_velocity;
    uint64_t position; /* source samples, 16.16 fixed point */
    uint32_t sample_index, frame;
    int32_t order; /* place in its driver note list */
    int16_t decoded[16], previous, current;
    int16_t parent, prev_parent, wanted_parent; /* layer index (channel * 4 + layer), -1 none */
    int32_t volume_left, volume_right, target_left, target_right; /* Q16 */
    uint32_t step; /* 16.16 source samples per output sample */
    uint16_t sample;
    uint8_t priority, list, attr_pan, attr_reverb, reverb, enabled, needs_init, finished;
    uint8_t synthetic, wave, wave_block, ended;
} PortableN64Note;

typedef enum
{
    PORTABLE_N64_OK = 0,
    PORTABLE_N64_INVALID_ARGUMENT,
    PORTABLE_N64_BAD_SECTION,
    PORTABLE_N64_BAD_COMMAND,
    PORTABLE_N64_BAD_ADDRESS,
    PORTABLE_N64_RUNAWAY_SCRIPT
} PortableN64Status;

typedef struct
{
    PortableN64SequenceView song;
    PortableN64Script script;
    PortableN64Channel channels[PORTABLE_N64_CHANNELS];
    PortableN64Note notes[PORTABLE_N64_NOTES];
    uint16_t patch_at[PORTABLE_N64_PATCHES]; /* sequence bytes the script rewrote */
    uint8_t patch_value[PORTABLE_N64_PATCHES];
    int16_t reverb_left[PORTABLE_N64_REVERB_SAMPLES], reverb_right[PORTABLE_N64_REVERB_SAMPLES];
    uint32_t short_velocity, short_duration; /* section offsets, 0: the defaults */
    float fade_volume, fade_scale, fade_velocity, applied_fade, mute_scale;
    uint32_t failed_at;
    int32_t value, back_order, front_order;
    uint32_t tempo_acc;
    uint16_t tempo, delay, fade_frames, fade_timer;
    uint16_t tempo_scale, volume; /* 256 = unity */
    int16_t pitch;                /* 1/256 semitone */
    uint16_t reverb_at;
    uint8_t patch_count, enabled, active, paused, muted, mute_behavior, recalculate, state;
    int8_t transposition, variation;
    uint8_t effect_channel, effect_sound, effect_pending; /* a one-shot sound-effect request */
    PortableN64Status status;
} PortableN64Player;

PortableN64Status portable_n64_sequence_open(PortableN64SequenceView *view, const uint8_t *bytes, size_t size);
/* Starts the sequence; it stays active until it ends and its notes die. */
void portable_n64_player_start(PortableN64Player *player, const PortableN64SequenceView *song);
/* Starts a sound-effect sequence and requests sound `sound` of `bank` on the
 * bank's first channel, as the game's sound manager does (IO port 0 = 1,
 * port 4 = sound, port 3 = full volume); the song ends when that channel
 * reports the sound done (port 7 = 0x80) and its notes have died. */
PortableN64Status portable_n64_player_start_effect(PortableN64Player *player, const PortableN64SequenceView *song,
                                                   uint8_t bank, uint8_t sound);
void portable_n64_player_stop(PortableN64Player *player);
int portable_n64_player_playing(const PortableN64Player *player);
/* Advances one game tick (three audio updates) and adds its notes into the
 * stereo frame. */
PortableN64Status portable_n64_player_mix(PortableN64Player *player,
                                          int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
/* Decodes one 9-byte VADPCM frame into 16 samples with an order-2 book
 * (big-endian s16[predictors][2][8]); history holds the previous frame's
 * last two samples and is updated. */
void portable_n64_vadpcm_frame(const uint8_t frame[9], const uint8_t *book, uint32_t predictors,
                               int16_t history[2], int16_t out[16]);
const char *portable_n64_status_name(PortableN64Status status);

#endif
