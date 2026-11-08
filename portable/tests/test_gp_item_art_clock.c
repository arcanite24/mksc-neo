#include "gp_item_art_clock.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"item art clock failed line %d: %s\n",__LINE__,#x); \
    return 1; } } while(0)

int main(void)
{
    const PortableItemArtIntroRules rules={{3u,7u,11u,13u,17u,19u,23u,29u}};
    PortableGpItemArtClock clock={0},before=clock;
    CHECK(!portable_gp_item_art_clock_begin_direct(&clock,0u,&rules));
    CHECK(!portable_gp_item_art_clock_begin_direct(&clock,201u,&rules));
    CHECK(memcmp(&clock,&before,sizeof clock)==0);
    CHECK(!portable_gp_item_art_clock_begin_direct(&clock,180u,NULL));
    PortableItemArtIntroRules invalid=rules;invalid.waits[3]=0u;
    CHECK(!portable_gp_item_art_clock_begin_direct(&clock,180u,&invalid));
    invalid.waits[3]=8193u;
    CHECK(!portable_gp_item_art_clock_begin_direct(&clock,180u,&invalid));
    CHECK(memcmp(&clock,&before,sizeof clock)==0);
    /* Authored script waits sum122. Instruction callbacks add399;
     * retaining180 countdown turns therefore omits341 callbacks. */
    CHECK(portable_gp_item_art_clock_begin_direct(&clock,180u,&rules));
    CHECK(clock.calls==341u&&clock.pending==0u);
    CHECK(!portable_gp_item_art_clock_begin_direct(&clock,180u,&rules));
    before=clock;
    CHECK(!portable_gp_item_art_clock_complete(&clock));
    CHECK(memcmp(&clock,&before,sizeof clock)==0);
    CHECK(portable_gp_item_art_clock_tick(&clock,0x81u));
    CHECK(clock.calls==342u);
    CHECK(portable_gp_item_art_clock_tick(&clock,0xc1u));
    CHECK(clock.calls==342u);
    CHECK(portable_gp_item_art_clock_suspend_next(&clock));
    before=clock;
    CHECK(!portable_gp_item_art_clock_suspend_next(&clock));
    CHECK(!portable_gp_item_art_clock_complete(&clock));
    CHECK(memcmp(&clock,&before,sizeof clock)==0);
    CHECK(portable_gp_item_art_clock_tick(&clock,0x84u));
    CHECK(clock.calls==342u&&clock.pending==2u);
    before=clock;
    CHECK(!portable_gp_item_art_clock_tick(&clock,0x84u));
    CHECK(memcmp(&clock,&before,sizeof clock)==0);
    CHECK(portable_gp_item_art_clock_complete(&clock));
    CHECK(clock.calls==343u&&clock.pending==0u);
    clock.calls=UINT32_MAX;
    CHECK(portable_gp_item_art_clock_tick(&clock,0x83u));
    CHECK(clock.calls==0u);
    CHECK(portable_gp_item_art_clock_suspend_next(&clock));
    clock.calls=UINT32_MAX;
    CHECK(portable_gp_item_art_clock_tick(&clock,0x84u));
    CHECK(portable_gp_item_art_clock_complete(&clock));
    CHECK(clock.calls==0u&&clock.pending==0u);
    CHECK(!portable_gp_item_art_clock_begin_direct(NULL,180u,&rules));
    CHECK(!portable_gp_item_art_clock_suspend_next(NULL));
    CHECK(!portable_gp_item_art_clock_tick(NULL,0u));
    CHECK(!portable_gp_item_art_clock_complete(NULL));
    return 0;
}
