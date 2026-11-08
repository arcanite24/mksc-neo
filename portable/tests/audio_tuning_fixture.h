#ifndef PORTABLE_TEST_AUDIO_TUNING_FIXTURE_H
#define PORTABLE_TEST_AUDIO_TUNING_FIXTURE_H
#include "audio_tuning.h"

/* Authored profile; isolated reference pitches keep existing scalar DSP goldens.
 * This is not the source's twelve-note or sixty-register tuning table. */
static inline const PortableAudioTuning *audio_test_tuning(void)
{
    static PortableAudioTuning tuning;
    for (unsigned i=0; i<12u; ++i) {
        tuning.pcm_frequency[i]=UINT32_C(2147483648)+i*UINT32_C(160000000);
        tuning.psg_frequency[i]=(int16_t)(-2004+(int)i*80);
    }
    tuning.pcm_frequency[1]=UINT32_C(2275179671);
    tuning.pcm_frequency[9]=UINT32_C(3611622603);
    tuning.psg_frequency[1]=-1891;
    tuning.psg_frequency[2]=-1785;
    tuning.psg_frequency[8]=-1262;
    tuning.psg_frequency[9]=-1192;
    for (unsigned i=0; i<60u; ++i) tuning.noise[i]=(uint8_t)(i%8u);
    tuning.noise[39]=0x34u;
    return &tuning;
}

static inline void audio_test_tuning_bytes(uint8_t bytes[PORTABLE_AUDIO_TUNING_BYTES])
{
    const PortableAudioTuning *t=audio_test_tuning();
    for(unsigned i=0;i<12u;++i){
        for(unsigned b=0;b<4u;++b)bytes[i*4u+b]=(uint8_t)(t->pcm_frequency[i]>>(b*8u));
        bytes[48u+i*2u]=(uint8_t)t->psg_frequency[i];
        bytes[49u+i*2u]=(uint8_t)((uint16_t)t->psg_frequency[i]>>8);
    }
    for(unsigned i=0;i<60u;++i)bytes[72u+i]=t->noise[i];
}
#endif
