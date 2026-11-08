#ifndef PORTABLE_ROM_IMPORT_AUDIO_H
#define PORTABLE_ROM_IMPORT_AUDIO_H

/* Shared state of the audio importer (tools/portable/audio_rom.py RomAudio)
 * and the entry points of the sequence and voice-bank normalizers. */

#include "rom_import_internal.h"

/* export_audio_voice_bank.py voice kinds. */
enum { RI_VOICE_PCM = 1, RI_VOICE_PSG_SQUARE, RI_VOICE_PSG_WAVE, RI_VOICE_PSG_NOISE,
       RI_VOICE_KEY_SPLIT, RI_VOICE_DRUM };

typedef struct RiAudioSample {
    uint32_t kind;
    uint32_t address;
    uint32_t id;
} RiAudioSample;

typedef struct RiAudio {
    RiCtx *ctx;
    RiBuf *banks;      /* uint32_t, sorted unique (RomAudio.banks) */
    RiBuf *boundaries; /* uint32_t, sorted unique (RomAudio.boundaries) */
    RiBuf *samples;    /* RiAudioSample in dict insertion order (RomAudio.samples) */
} RiAudio;

/* RomAudio.tone_count; may be negative like Python's floor division. */
int64_t ri_audio_tone_count(RiAudio *audio, uint32_t address);
/* RomAudio.sample_id */
uint32_t ri_audio_sample_id(RiAudio *audio, uint32_t kind, uint32_t address);
/* RomBankFlattener(rom).export(address, bank_id) */
RiBuf *ri_audio_bank(RiAudio *audio, uint32_t address, uint32_t bank_id);
/* export_audio_sequence.export_sequence(rom, header, rom.pointer, ...) */
RiBuf *ri_audio_sequence(RiCtx *ctx, size_t header, uint32_t song_id, uint32_t bank_id);

#endif
