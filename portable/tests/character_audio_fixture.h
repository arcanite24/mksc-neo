#ifndef PORTABLE_TEST_CHARACTER_AUDIO_FIXTURE_H
#define PORTABLE_TEST_CHARACTER_AUDIO_FIXTURE_H
#include "../core/character_audio.h"

static inline PortableCharacterAudioRules character_audio_fixture(void)
{
    PortableCharacterAudioRules rules={0};
    for(unsigned i=0;i<8u;++i){
        rules.lock_voices[i]=(uint16_t)(100u+i);
        rules.menu_revs[i]=(PortableCharacterRevCue){(uint16_t)(140u+i),(uint16_t)(300u+i)};
        rules.launch_revs[i]=(PortableCharacterRevCue){(uint16_t)(160u+i),(uint16_t)(400u+i)};
        rules.race_horns[i]=(uint16_t)(220u+i);
        rules.ceremony_voices[i]=(uint16_t)(240u+i);
    }
    for(unsigned i=0;i<9u;++i)rules.menu_horns[i]=(uint16_t)(200u+i);
    rules.menu_horns[4]=0u;
    return rules;
}

static inline void character_audio_fixture_bytes(uint8_t out[130])
{
    const PortableCharacterAudioRules rules=character_audio_fixture();
    unsigned at=0;
#define CUE_WORD(value) do { const uint16_t v=(value);out[at++]=(uint8_t)v;out[at++]=(uint8_t)(v>>8); } while(0)
    for(unsigned i=0;i<8u;++i)CUE_WORD(rules.lock_voices[i]);
    for(unsigned table=0;table<2u;++table)for(unsigned i=0;i<8u;++i){
        const PortableCharacterRevCue cue=table?rules.launch_revs[i]:rules.menu_revs[i];
        CUE_WORD(cue.song);CUE_WORD(cue.volume);
    }
    for(unsigned i=0;i<9u;++i)CUE_WORD(rules.menu_horns[i]);
    for(unsigned i=0;i<8u;++i)CUE_WORD(rules.race_horns[i]);
    for(unsigned i=0;i<8u;++i)CUE_WORD(rules.ceremony_voices[i]);
#undef CUE_WORD
}
#endif
