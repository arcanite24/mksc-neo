#include "gp_finish_portraits.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"finish portraits line %d: %s\n",__LINE__,#x);return 1; } } while (0)

int main(void)
{
    PortableGpFinishPortraits state;
    uint32_t flags[8]={0x20u,0u,0x20u};
    const uint8_t classes[8]={3u,1u,7u,0u,4u,2u,6u,5u};
    const uint8_t seats[8]={2u,0u,1u,3u,4u,5u,6u,7u};
    CHECK(portable_gp_finish_portraits_init(&state,8u,0u));
    CHECK(portable_gp_finish_portraits_tick(&state,flags,classes,seats));
    CHECK(state.rows[0].x_tile==34u&&state.rows[0].progress==1u);
    CHECK(state.rows[1].driver_class==-1);
    CHECK(state.rows[2].driver_class==7&&state.rows[2].delay==6u&&state.rows[2].progress==0u);
    /* A later finished rank remains registered but cannot advance through
     * an unfinished predecessor. The selected racer occupies its rank too. */
    flags[1]=0x20u;
    CHECK(portable_gp_finish_portraits_tick(&state,flags,classes,seats));
    CHECK(state.rows[1].racer_index==0u&&state.rows[1].delay==2u);
    CHECK(state.rows[2].delay==5u);
    for (unsigned i=0u;i<64u;++i)
        CHECK(portable_gp_finish_portraits_tick(&state,flags,classes,seats));
    CHECK(state.rows[0].x_tile==0u&&state.rows[1].x_tile==3u&&state.rows[2].x_tile==6u);
    for (unsigned i=3u;i<8u;++i) flags[i]=0x20u;
    for (unsigned i=0u;i<64u;++i)
        CHECK(portable_gp_finish_portraits_tick(&state,flags,classes,seats));
    CHECK(state.rows[3].x_tile==9u&&state.rows[4].x_tile==17u&&state.rows[7].x_tile==26u);
    const PortableGpFinishPortraits frozen=state;
    uint8_t bad[8];memcpy(bad,classes,sizeof bad);bad[7]=8u;
    CHECK(!portable_gp_finish_portraits_tick(&state,flags,bad,seats));
    CHECK(memcmp(&frozen,&state,sizeof state)==0);
    CHECK(portable_gp_finish_portraits_init(&state,8u,0u)&&state.rows[0].driver_class==-1);
    /* Original GP hold age220 has run19 callbacks: its five registered
     * portraits retain this exact cell position and three-call stagger. */
    for(unsigned i=0u;i<8u;++i)flags[i]=i<5u?0x20u:0u;
    for(unsigned i=0u;i<19u;++i)
        CHECK(portable_gp_finish_portraits_tick(&state,flags,classes,seats));
    const uint16_t source_x[5]={15u,21u,27u,34u,45u};
    const uint16_t source_progress[5]={19u,16u,13u,10u,7u};
    for(unsigned i=0u;i<5u;++i)
        CHECK(state.rows[i].x_tile==source_x[i]&&state.rows[i].progress==source_progress[i]);
    puts("finish portraits: PASS rank order, stagger, selected rank and exact slide");
    return 0;
}
