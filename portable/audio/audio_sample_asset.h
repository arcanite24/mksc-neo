#ifndef PORTABLE_AUDIO_SAMPLE_ASSET_H
#define PORTABLE_AUDIO_SAMPLE_ASSET_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_SAMPLE_HEADER_SIZE 40u

typedef enum
{
    PORTABLE_AUDIO_SAMPLE_PCM_S8 = 1,
    PORTABLE_AUDIO_SAMPLE_PSG_WAVE4
} PortableAudioSampleEncoding;

enum
{
    PORTABLE_AUDIO_SAMPLE_LOOP = 1u << 0
};

typedef enum
{
    PORTABLE_AUDIO_SAMPLE_OK = 0,
    PORTABLE_AUDIO_SAMPLE_INVALID_ARGUMENT,
    PORTABLE_AUDIO_SAMPLE_BAD_MAGIC,
    PORTABLE_AUDIO_SAMPLE_BAD_VERSION,
    PORTABLE_AUDIO_SAMPLE_BAD_SIZE,
    PORTABLE_AUDIO_SAMPLE_BAD_ENCODING,
    PORTABLE_AUDIO_SAMPLE_BAD_FLAGS,
    PORTABLE_AUDIO_SAMPLE_BAD_RATE,
    PORTABLE_AUDIO_SAMPLE_BAD_LOOP
} PortableAudioSampleStatus;

typedef struct
{
    const uint8_t *data;
    size_t data_size;
    uint16_t sample_id;
    uint8_t encoding;
    uint8_t flags;
    uint32_t frequency_q10;
    uint32_t sample_count;
    uint32_t loop_start;
} PortableAudioSampleView;

PortableAudioSampleStatus portable_audio_sample_open(
    PortableAudioSampleView *view, const uint8_t *data, size_t size);
const char *portable_audio_sample_status_name(PortableAudioSampleStatus status);

#endif
