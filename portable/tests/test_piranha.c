#include "piranha.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"piranha line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    PortablePiranha a;uint16_t song;
    CHECK(!portable_piranha_init(&a,0,0,0u,1u));
    CHECK(portable_piranha_init(&a,100*65536,200*65536,2u,0u));
    CHECK(a.flags==2u && a.affine_x_q8==224u);
    CHECK(portable_piranha_advance(&a,0u,&song) && a.state==1 && !song);
    PortablePiranha before=a;
    CHECK(!portable_piranha_contact(&a,0x2001u,1,0,1));
    CHECK(!memcmp(&a,&before,sizeof a));
    CHECK(!portable_piranha_contact(&a,0x1001u,0,0,1));
    CHECK(portable_piranha_contact(&a,0x2001u,0,0,1));
    for(int tick=1;tick<=24;++tick){
        CHECK(portable_piranha_advance(&a,0u,&song));
        CHECK(song==(tick==9?378u:0u));
        CHECK(a.art==(tick<5?0u:tick<9?1u:tick<16?2u:tick<21?4u:3u));
        CHECK(a.state==(tick==24?0:17) && a.flags==1u);
    }
    CHECK(portable_piranha_advance(&a,0u,&song) && a.state==1 && a.flags==2u);
    /* 8040A44/8040C3C never clear the kart's +0x34 bit 2 (49790 still
     * registers it) and 57228 tests only family, +0x66 and +0x6a: a kart
     * held in capture mode 8 is bitten again once the plant re-arms. */
    {
        PortablePiranhaCourse course;PortableDriverCaptureActor actor;
        CHECK(portable_piranha_course_init(&course,&a,1u));
        CHECK(portable_course_capture_actors(&course,NULL,&actor,1u)==1u &&
              actor.flags_34==2u && actor.x_q16==a.x_q16);
    }
    CHECK(portable_piranha_contact(&a,0x2001u,0,0,1));
    for(int tick=1;tick<=9;++tick){CHECK(portable_piranha_advance(&a,0x20u,&song));CHECK(!song);}
    CHECK(portable_piranha_contact(&a,0x2001u,0,1,1));
    for(int tick=1;tick<=26;++tick){
        CHECK(portable_piranha_advance(&a,0u,&song) && !song);
        CHECK(a.height_q16==tick*0x30000-(tick*(tick-1)/2)*0x4000);
        CHECK(a.flip_y && a.affine_y_q8==256u && !(a.flags&2u));
        CHECK(a.state==(tick==26?243:241));
    }
    CHECK(!(a.flags&1u));before=a;
    CHECK(!portable_piranha_contact(&a,0x2001u,0,1,1));
    CHECK(portable_piranha_advance(&a,0u,&song) && !memcmp(&a,&before,sizeof a));
    CHECK(portable_piranha_init(&a,0,0,0u,0u));
    CHECK(portable_piranha_contact(&a,0x2001u,0,1,0));
    CHECK(portable_piranha_advance(&a,0u,&song) && a.flags==0u); /* protected hit keeps hidden art hidden */
    a.state=18;CHECK(!portable_piranha_advance(&a,0u,&song));
    return 0;
}
