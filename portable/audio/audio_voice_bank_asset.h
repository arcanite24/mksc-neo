#ifndef PORTABLE_AUDIO_VOICE_BANK_ASSET_H
#define PORTABLE_AUDIO_VOICE_BANK_ASSET_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_VOICE_BANK_HEADER_SIZE 40u
#define PORTABLE_AUDIO_VOICE_RECORD_SIZE 20u
#define PORTABLE_AUDIO_VOICE_MAPPING_SIZE 8u

typedef enum
{
    PORTABLE_AUDIO_VOICE_PCM = 1,
    PORTABLE_AUDIO_VOICE_PSG_SQUARE,
    PORTABLE_AUDIO_VOICE_PSG_WAVE,
    PORTABLE_AUDIO_VOICE_PSG_NOISE,
    PORTABLE_AUDIO_VOICE_KEY_SPLIT,
    PORTABLE_AUDIO_VOICE_DRUM
} PortableAudioVoiceSourceKind;

typedef enum
{
    PORTABLE_AUDIO_VOICE_OK = 0,
    PORTABLE_AUDIO_VOICE_INVALID_ARGUMENT,
    PORTABLE_AUDIO_VOICE_BAD_MAGIC,
    PORTABLE_AUDIO_VOICE_BAD_VERSION,
    PORTABLE_AUDIO_VOICE_BAD_SIZE,
    PORTABLE_AUDIO_VOICE_BAD_SOURCE,
    PORTABLE_AUDIO_VOICE_BAD_MAPPING
} PortableAudioVoiceStatus;

typedef struct
{
    const uint8_t *data;
    size_t size;
    uint16_t bank_id;
    uint16_t voice_count;
    uint32_t mapping_count;
    uint32_t voices_offset;
    uint32_t mappings_offset;
} PortableAudioVoiceBankView;

typedef struct
{
    uint8_t original_type;
    uint8_t key;
    uint8_t length;
    uint8_t pan_sweep;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
    uint8_t source_kind;
    uint32_t source_id;
    uint16_t mapping_first;
    uint16_t mapping_count;
} PortableAudioVoice;

typedef struct
{
    uint8_t key_start;
    uint8_t key_end;
    uint16_t voice_index;
    uint8_t tone_key;
    uint8_t pan;
} PortableAudioVoiceMapping;

PortableAudioVoiceStatus portable_audio_voice_bank_open(
    PortableAudioVoiceBankView *view, const uint8_t *data, size_t size);
PortableAudioVoiceStatus portable_audio_voice_bank_voice(
    const PortableAudioVoiceBankView *view, uint16_t index,
    PortableAudioVoice *voice);
PortableAudioVoiceStatus portable_audio_voice_bank_mapping(
    const PortableAudioVoiceBankView *view, uint32_t index,
    PortableAudioVoiceMapping *mapping);
PortableAudioVoiceStatus portable_audio_voice_bank_resolve(
    const PortableAudioVoiceBankView *view, uint16_t index, uint8_t key,
    PortableAudioVoice *voice, uint16_t *resolved_index);
const char *portable_audio_voice_status_name(PortableAudioVoiceStatus status);

#endif
