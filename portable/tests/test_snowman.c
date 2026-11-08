#include "snowman.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"snowman line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    PortableSnowman a;
    CHECK(!portable_snowman_init(&a,0,0,0u,1u));
    CHECK(portable_snowman_init(&a,100*65536,200*65536,2u,0u));
    PortableSnowman before=a;
    CHECK(!portable_snowman_contact(&a,0x2001u,1) && !memcmp(&a,&before,sizeof a));
    CHECK(!portable_snowman_contact(&a,0x1001u,0));
    CHECK(portable_snowman_contact(&a,0x2001u,0) && a.state==32);
    CHECK(portable_snowman_advance(&a) && a.state==33 && a.timer==0 && a.flags==3u);
    unsigned art=0u;
    for(unsigned timer=0;timer<=34u;++timer){
        CHECK(portable_snowman_advance(&a));
        if((timer&3u)==2u)art=(timer&7u)==2u;
        CHECK(a.timer==(int32_t)timer+1 && a.art==art && (a.flags&2u));
        CHECK((a.flags&1u)==((timer&3u)<2u?0u:1u));
        CHECK(a.state==(timer==34u?34:33));
    }
    CHECK(portable_snowman_contact(&a,0x2001u,0) && a.state==48);
    CHECK(portable_snowman_advance(&a) && a.state==49 && a.timer==0 && a.flags==1u && a.art==1u);
    for(unsigned timer=1;timer<=40u;++timer){
        CHECK(portable_snowman_advance(&a));
        CHECK(a.timer==(int32_t)timer && a.flags==(timer<40u && (timer&4u)?1u:0u));
        CHECK(a.state==(timer==40u?34:49));
    }
    before=a;CHECK(portable_snowman_advance(&a) && !memcmp(&a,&before,sizeof a));
    /* A second hit can interrupt the first blink while the actor is hidden. */
    CHECK(portable_snowman_init(&a,0,0,0u,0u));
    CHECK(portable_snowman_contact(&a,0x2001u,0));
    CHECK(portable_snowman_advance(&a) && portable_snowman_advance(&a) && a.flags==2u);
    CHECK(portable_snowman_contact(&a,0x2001u,0) && portable_snowman_advance(&a) && a.flags==0u);
    a.state=99;CHECK(!portable_snowman_advance(&a));
    a.state=33;a.timer=INT32_MAX;
    CHECK(portable_snowman_advance(&a) && a.timer==INT32_MIN);
    PortableSnowmanCourse course;
    CHECK(portable_snowman_course_init(&course,&before,1u));
    CHECK(course.initial==&before && course.count==1u && !memcmp(&course.actors[0],&before,sizeof before));
    return 0;
}
