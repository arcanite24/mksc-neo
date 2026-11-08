#include "thwomp.h"
#include <stdio.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d\n",__LINE__);return 1;}}while(0)

int main(void)
{
    PortableThwomp actor;
    int landed;
    CHECK(portable_thwomp_init(&actor,1000*65536,1000*65536,4u,0u));
    CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(actor.state==1u && actor.timer==36 && actor.height_q16==0);
    for(unsigned i=0;i<36u;++i) CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(actor.state==2u && actor.timer==64 && actor.velocity_z_q16==0x8000);
    for(unsigned i=0;i<64u;++i) CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(actor.state==3u && actor.height_q16==32*65536);
    for(unsigned i=0;i<68u;++i) CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(actor.state==5u && actor.timer==8 && actor.velocity_z_q16==-0x40000);
    for(unsigned i=0;i<8u;++i) CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(landed && actor.state==6u && actor.height_q16==0 && actor.velocity_z_q16==0);
    actor.timer=29;
    CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(actor.height_q16==0x8000 && !landed); /* distinct stationary landing pulse */
    actor.timer=1;
    CHECK(portable_thwomp_advance(&actor,&landed));
    CHECK(actor.state==0u && actor.timer==32);

    /* Full source cycles return to their starting coordinates, with exact drift
       for fractional velocities (48*0x2d555 and 48*0x1aaaa). */
    for(unsigned variant=4;variant<12u;++variant) {
        CHECK(portable_thwomp_init(&actor,1000*65536,1000*65536,0u,variant));
        const unsigned phases=variant<=5u?8u:9u;
        const unsigned move=phases==8u?3u:4u;
        const unsigned cycles=portable_thwomp_normal_states(variant)/phases;
        for(unsigned cycle=0;cycle<cycles;++cycle) {
            actor.state=(uint8_t)(cycle*phases+move);actor.timer=48;
            const int32_t x=actor.x_q16,y=actor.y_q16;
            int dx=0,dy=0;
            if(variant<=5u||variant==7u||variant==8u) {
                if(cycle<2u)dx=cycle==0u?1:-1;else dy=cycle==2u?1:-1;
            } else if(variant==6u||variant==9u) {
                if(cycle<4u)dx=cycle<2u?1:-1;else dy=cycle==4u?1:-1;
            } else dx=cycle<(variant==10u?2u:3u)?1:-1;
            for(unsigned tick=0;tick<48u;++tick) CHECK(portable_thwomp_advance(&actor,&landed));
            CHECK(actor.x_q16==x+dx*48*actor.velocity_x_q16);
            CHECK(actor.y_q16==y+dy*48*actor.velocity_y_q16);
            CHECK(actor.timer==(phases==8u?64:variant==10u?24:variant==11u?16:32));
        }
        CHECK(actor.x_q16==1000*65536 && actor.y_q16==1000*65536);
        /* Advance complete normal cycles, including waits/rumble/rise/fall. */
        actor.state=0u;actor.timer=1;
        unsigned ticks=0,landings=0;
        do {
            CHECK(portable_thwomp_advance(&actor,&landed));
            landings+=(unsigned)landed;
            CHECK(actor.height_q16>=0 && actor.height_q16<=0x208000);
            CHECK(++ticks<3000u);
        } while(actor.state!=0u);
        CHECK(landings==cycles && actor.x_q16==1000*65536 && actor.y_q16==1000*65536);
    }
    for(unsigned variant=0;variant<12u;++variant) {
        CHECK(portable_thwomp_init(&actor,0,0,0u,variant));
        const unsigned state=actor.state;
        portable_thwomp_contact(&actor,0x2001u,1,1,128,-256);
        CHECK(actor.state==state);
        portable_thwomp_contact(&actor,0x2001u,0,1,128,-256);
        CHECK(actor.flags==1u && actor.velocity_x_q16==49152 && actor.velocity_y_q16==-98304);
        unsigned ticks=0;
        while(actor.flags&1u){CHECK(portable_thwomp_advance(&actor,&landed));CHECK(++ticks<100u);}
        CHECK(actor.height_q16<=0);
    }
    CHECK(!portable_thwomp_init(&actor,0,0,0u,12u));
    CHECK(portable_thwomp_init(&actor,0,0,0u,0u));actor.state=8u;
    CHECK(!portable_thwomp_advance(&actor,&landed));
    return 0;
}
