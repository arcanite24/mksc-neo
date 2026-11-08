#include "audio_sample_asset.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

PortableAudioSampleStatus portable_audio_sample_open(
    PortableAudioSampleView *view, const uint8_t *data, size_t size)
{
    static const uint8_t magic[8] = {'M','K','S','C','S','M','P','1'};
    PortableAudioSampleView candidate;
    uint32_t data_offset, total_size;
    size_t expected_size;
    if (view == NULL || data == NULL)
        return PORTABLE_AUDIO_SAMPLE_INVALID_ARGUMENT;
    if (size < PORTABLE_AUDIO_SAMPLE_HEADER_SIZE)
        return PORTABLE_AUDIO_SAMPLE_BAD_SIZE;
    if (memcmp(data, magic, sizeof magic) != 0)
        return PORTABLE_AUDIO_SAMPLE_BAD_MAGIC;
    if (read_u16(data + 8u) != PORTABLE_AUDIO_SAMPLE_HEADER_SIZE ||
        read_u16(data + 10u) != 1u)
        return PORTABLE_AUDIO_SAMPLE_BAD_VERSION;
    candidate.sample_id = read_u16(data + 12u);
    candidate.encoding = data[14];
    candidate.flags = data[15];
    candidate.frequency_q10 = read_u32(data + 16u);
    candidate.sample_count = read_u32(data + 20u);
    candidate.loop_start = read_u32(data + 24u);
    data_offset = read_u32(data + 28u);
    total_size = read_u32(data + 32u);
    if (read_u32(data + 36u) != 0u ||
        data_offset != PORTABLE_AUDIO_SAMPLE_HEADER_SIZE || total_size != size)
        return PORTABLE_AUDIO_SAMPLE_BAD_SIZE;
    if ((candidate.flags & ~PORTABLE_AUDIO_SAMPLE_LOOP) != 0u)
        return PORTABLE_AUDIO_SAMPLE_BAD_FLAGS;
    if (candidate.sample_count == 0u)
        return PORTABLE_AUDIO_SAMPLE_BAD_SIZE;
    if (candidate.encoding == PORTABLE_AUDIO_SAMPLE_PCM_S8) {
        if (candidate.frequency_q10 < 1024u)
            return PORTABLE_AUDIO_SAMPLE_BAD_RATE;
        expected_size = candidate.sample_count;
    } else if (candidate.encoding == PORTABLE_AUDIO_SAMPLE_PSG_WAVE4) {
        if (candidate.frequency_q10 != 0u || candidate.sample_count != 32u)
            return candidate.frequency_q10 != 0u ? PORTABLE_AUDIO_SAMPLE_BAD_RATE :
                PORTABLE_AUDIO_SAMPLE_BAD_SIZE;
        expected_size = 16u;
    } else {
        return PORTABLE_AUDIO_SAMPLE_BAD_ENCODING;
    }
    if (size - data_offset != expected_size)
        return PORTABLE_AUDIO_SAMPLE_BAD_SIZE;
    if ((candidate.flags & PORTABLE_AUDIO_SAMPLE_LOOP) != 0u) {
        if (candidate.loop_start >= candidate.sample_count)
            return PORTABLE_AUDIO_SAMPLE_BAD_LOOP;
    } else if (candidate.loop_start != 0u) {
        return PORTABLE_AUDIO_SAMPLE_BAD_LOOP;
    }
    candidate.data = data + data_offset;
    candidate.data_size = expected_size;
    *view = candidate;
    return PORTABLE_AUDIO_SAMPLE_OK;
}

const char *portable_audio_sample_status_name(PortableAudioSampleStatus status)
{
    static const char *const names[] = {
        "ok", "invalid argument", "bad magic", "bad version", "bad size",
        "bad encoding", "bad flags", "bad rate", "bad loop"
    };
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] :
        "unknown status";
}
