#include "gp_item_art_clock.h"

#include <stddef.h>

int portable_gp_item_art_clock_begin_direct(PortableGpItemArtClock *clock,
    uint16_t retained_countdown,const PortableItemArtIntroRules *rules)
{
    if(clock==NULL||clock->calls!=0u||clock->pending!=0u||
       retained_countdown==0u||retained_countdown>200u||
       !portable_item_art_intro_rules_valid(rules))return 0;
    /* 527E0 completes on callback257; request installation, 50BE4's
     * instruction-owned140-turn hold and phase2 installation add142. */
    uint32_t callbacks=257u+1u+140u+1u;
    for(unsigned i=0u;i<PORTABLE_ITEM_ART_INTRO_WAIT_COUNT;++i)
        callbacks+=rules->waits[i];
    clock->calls=callbacks-retained_countdown;
    return 1;
}

int portable_gp_item_art_clock_suspend_next(PortableGpItemArtClock *clock)
{
    if(clock==NULL||clock->pending!=0u)return 0;
    clock->pending=1u;
    return 1;
}

int portable_gp_item_art_clock_tick(PortableGpItemArtClock *clock,
    uint8_t scene_mode)
{
    if(clock==NULL||clock->pending>1u)return 0;
    /* 4389C's3618 bit40 branch skips the entire art/palette callback. */
    if(scene_mode&UINT8_C(0x40))return 1;
    if(clock->pending==1u){clock->pending=2u;return 1;}
    ++clock->calls; /* Defined unsigned wrap, as in the original u32 store. */
    return 1;
}

int portable_gp_item_art_clock_complete(PortableGpItemArtClock *clock)
{
    if(clock==NULL||clock->pending!=2u)return 0;
    ++clock->calls;
    clock->pending=0u;
    return 1;
}
