#ifndef PORTABLE_AUDIO_TUNING_H
#define PORTABLE_AUDIO_TUNING_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_TUNING_BYTES 132u

typedef struct {
    uint32_t pcm_frequency[12];
    int16_t psg_frequency[12];
    uint8_t noise[60];
} PortableAudioTuning;

static inline int portable_audio_tuning_open(PortableAudioTuning *out,
    const uint8_t *bytes, size_t size)
{
    if (out == NULL || bytes == NULL || size != PORTABLE_AUDIO_TUNING_BYTES)
        return 0;
    for (unsigned i = 0; i < 12u; ++i) {
        const uint8_t *p = bytes + i * 4u;
        out->pcm_frequency[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
            (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        const uint16_t value = (uint16_t)((uint16_t)bytes[48u+i*2u] |
            (uint16_t)bytes[49u+i*2u] << 8);
        out->psg_frequency[i] = (int16_t)(value < 32768u ?
            (int32_t)value : (int32_t)value - 65536);
        if (out->pcm_frequency[i] == 0u ||
            (i != 0u && out->pcm_frequency[i] <= out->pcm_frequency[i-1u]) ||
            out->psg_frequency[i] < -2047 || out->psg_frequency[i] >= 0)
            return 0;
    }
    for (unsigned i = 0; i < 60u; ++i) {
        out->noise[i] = bytes[72u+i];
        if ((out->noise[i] & 8u) != 0u || (out->noise[i] >> 4) > 13u)
            return 0;
    }
    return 1;
}
#endif
