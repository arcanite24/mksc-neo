#ifndef PORTABLE_ITEM_ART_INTRO_RULES_H
#define PORTABLE_ITEM_ART_INTRO_RULES_H
#include <stdint.h>

enum { PORTABLE_ITEM_ART_INTRO_WAIT_COUNT=8u };
typedef struct { uint16_t waits[PORTABLE_ITEM_ART_INTRO_WAIT_COUNT]; } PortableItemArtIntroRules;
static inline int portable_item_art_intro_rules_valid(const PortableItemArtIntroRules *rules)
{
    if(rules==0)return 0;
    for(unsigned i=0u;i<PORTABLE_ITEM_ART_INTRO_WAIT_COUNT;++i)
        if(rules->waits[i]==0u || rules->waits[i]>8192u)return 0;
    return 1;
}
#endif
