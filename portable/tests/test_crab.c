#include "crab.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"crab failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    int32_t sine[1024]={0};
    PortableCrab actor;
    const uint16_t variants[]={0u,1u,0x8000u,0x8001u,0x8002u};
    for(size_t v=0;v<sizeof variants/sizeof variants[0];++v){
        CHECK(portable_crab_init(&actor,100*65536,100*65536,2u,variants[v]));
        const int vertical=variants[v]==1u || variants[v]==0x8001u;
        CHECK(actor.velocity_x_q16==(vertical?0:0x8000));
        CHECK(actor.velocity_y_q16==(vertical?0x8000:0));
        for(unsigned t=0;t<380u;++t){
            const int32_t old_x=actor.x_q16,old_y=actor.y_q16;
            const int moving=t<150u || (t>=190u && t<340u);
            const int32_t step=moving?(t<150u?0x8000:-0x8000):0;
            CHECK(portable_crab_advance(&actor,sine));
            CHECK(actor.x_q16==old_x+(vertical?0:step));
            CHECK(actor.y_q16==old_y+(vertical?step:0));
            if(t==4u)CHECK(actor.art==(variants[v]>=0x8000u?2u:0u));
            if(t==5u)CHECK(actor.art==(variants[v]>=0x8000u?3u:1u));
            if(t==11u)CHECK(actor.art==(variants[v]>=0x8000u?2u:0u));
        }
        CHECK(actor.timer==0 && actor.x_q16==100*65536 && actor.y_q16==100*65536);
        PortableCrab unchanged=actor;
        portable_crab_contact(&actor,0x2001u,1,1);
        CHECK(memcmp(&actor,&unchanged,sizeof actor)==0);
        portable_crab_contact(&actor,0x2001u,0,0);
        CHECK(memcmp(&actor,&unchanged,sizeof actor)==0);
        portable_crab_contact(&actor,0x1000u,0,1);
        CHECK(memcmp(&actor,&unchanged,sizeof actor)==0);
        portable_crab_contact(&actor,0x2001u,0,1);
        CHECK(actor.flags==1u && actor.timer==32 && actor.flip_y);
        for(int t=32;t>=0;--t){
            sine[t*16]=t*100;
            CHECK(portable_crab_advance(&actor,sine));
            CHECK(actor.height_q16==t*4800);
            CHECK(actor.x_q16==unchanged.x_q16 && actor.y_q16==unchanged.y_q16);
            CHECK((actor.flags&1u)==(unsigned)(t!=0));
        }
        CHECK(actor.timer==-1 && actor.flags==0u);
        CHECK(portable_crab_advance(&actor,sine));
    }
    CHECK(!portable_crab_init(&actor,0,0,0u,2u));
    CHECK(portable_crab_init(&actor,0,0,0u,0u));
    actor.timer=380;CHECK(!portable_crab_advance(&actor,sine));
    return 0;
}
