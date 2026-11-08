#ifndef PORTABLE_CHARACTER_AUDIO_H
#define PORTABLE_CHARACTER_AUDIO_H

#include <stdint.h>

typedef struct { uint16_t song,volume; } PortableCharacterRevCue;
typedef struct {
    uint16_t lock_voices[8];
    PortableCharacterRevCue menu_revs[8],launch_revs[8];
    uint16_t menu_horns[9],race_horns[8],ceremony_voices[8];
} PortableCharacterAudioRules;

static inline int portable_character_audio_rules_valid(const PortableCharacterAudioRules *rules)
{
    if(rules==0)return 0;
    for(unsigned i=0;i<8u;++i){
        if(rules->lock_voices[i]==0u||rules->lock_voices[i]>=403u||
            rules->menu_revs[i].song==0u||rules->menu_revs[i].song>=403u||rules->menu_revs[i].volume>1023u||
            rules->launch_revs[i].song==0u||rules->launch_revs[i].song>=403u||rules->launch_revs[i].volume>1023u||
            rules->race_horns[i]==0u||rules->race_horns[i]>=403u||
            rules->ceremony_voices[i]==0u||rules->ceremony_voices[i]>=403u)return 0;
    }
    for(unsigned i=0;i<9u;++i)if(rules->menu_horns[i]>=403u)return 0;
    return 1;
}
#endif
