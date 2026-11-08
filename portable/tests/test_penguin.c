#include "penguin.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"penguin line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    int32_t sine[1024]={0};
    for(unsigned i=0;i<=512u;++i)sine[i]=(int32_t)(i<=256u?i:512u-i)*128;
    PortablePenguin a,b;
    CHECK(!portable_penguin_init(&a,0,0,0u,2u));
    CHECK(portable_penguin_init(&a,100*65536,200*65536,2u,0u));
    CHECK(portable_penguin_init(&b,100*65536,200*65536,2u,1u) && !memcmp(&a,&b,sizeof a));
    for(unsigned tick=0;tick<84u;++tick){
        const unsigned t=tick%42u;
        CHECK(portable_penguin_advance(&a,sine,1024u));
        CHECK(a.timer==(int32_t)((t+1u)%42u));
        CHECK(a.art==(t<6u?0u:t<14u?1u:t<20u?2u:t<28u?3u:t<34u?4u:5u));
        CHECK(a.flags==3u && a.height_q16==0 && a.affine_y_q8==256);
    }
    b=a;
    CHECK(!portable_penguin_contact(&a,0x2001u,0,0));
    CHECK(!portable_penguin_contact(&a,0x1001u,0,1));
    CHECK(!portable_penguin_contact(&a,0x2001u,1,1) && !memcmp(&a,&b,sizeof a));
    CHECK(portable_penguin_contact(&a,0x2001u,0,1));
    CHECK(a.timer==32 && a.flags==1u && a.affine_y_q8==-256 && a.art==5u);
    for(int timer=32;timer>=0;--timer){
        CHECK(portable_penguin_advance(&a,sine,1024u));
        CHECK(a.timer==timer-1 && a.height_q16==sine[timer*16]*48 && a.art==5u);
        CHECK(a.flags==(timer?1u:0u));
    }
    b=a;CHECK(portable_penguin_advance(&a,sine,1024u) && !memcmp(&a,&b,sizeof a));
    a.flags=1u;a.timer=16;sine[256]=INT32_MAX;
    CHECK(portable_penguin_advance(&a,sine,1024u) && a.height_q16==-48);
    a.timer=33;CHECK(!portable_penguin_advance(&a,sine,1024u));
    a.flags=3u;a.timer=-1;CHECK(!portable_penguin_advance(&a,sine,1024u));
    CHECK(!portable_penguin_advance(&a,sine,512u));
    PortablePenguinCourse course;
    CHECK(portable_penguin_course_init(&course,&b,1u));
    CHECK(course.initial==&b && course.count==1u && !memcmp(&course.actors[0],&b,sizeof b));
    return 0;
}
