#include "lava_bubble.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"lava bubble failed line %d\n",__LINE__);return 1;} } while(0)
int main(void)
{
    int32_t sine[1024]={0};
    sine[504]=804;sine[256]=16384;sine[248]=16364;
    PortableLavaBubble actor;
    CHECK(portable_lava_bubble_init(&actor,100*65536,200*65536,4u,1u));
    CHECK(actor.timer==100&&actor.period==100&&actor.flags==0u);
    for(unsigned i=0;i<99u;++i)CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==1u&&actor.timer==1&&actor.flags==0u);
    CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==2u&&actor.timer==64&&actor.flags==0u);
    CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==3u&&actor.timer==63&&actor.flags==3u&&
          actor.height_q16==38592&&actor.art==0u);
    for(unsigned i=0;i<31u;++i)CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.timer==32&&actor.height_q16==786432&&actor.art==0u);
    CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.timer==31&&actor.height_q16==785472&&actor.art==1u);
    for(unsigned i=0;i<31u;++i)CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==0u&&actor.timer==100&&actor.height_q16==0&&actor.flags==3u);
    CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==1u&&actor.timer==99&&actor.flags==0u);
    PortableLavaBubble before=actor;
    portable_lava_bubble_contact(&actor,0x2001u,1,1,128,-256);
    CHECK(memcmp(&actor,&before,sizeof actor)==0);
    portable_lava_bubble_contact(&actor,0x2001u,0,0,128,-256);
    CHECK(memcmp(&actor,&before,sizeof actor)==0);
    actor.flags=3u;
    portable_lava_bubble_contact(&actor,0x2001u,0,1,128,-256);
    CHECK(actor.phase==4u&&actor.velocity_z_q16==0x30000);
    CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==5u&&actor.flags==1u&&actor.x_q16==100*65536+128&&
          actor.y_q16==200*65536-256&&actor.height_q16==0x30000);
    unsigned ticks=0u;
    while(actor.phase!=6u&&ticks++<100u)CHECK(portable_lava_bubble_advance(&actor,sine));
    CHECK(actor.phase==6u&&actor.flags==0u&&actor.height_q16<0&&ticks<100u);
    before=actor;
    CHECK(!portable_lava_bubble_init(&actor,0,0,0u,13u));
    CHECK(memcmp(&actor,&before,sizeof actor)==0);
    CHECK(portable_lava_bubble_init(&actor,0,0,0u,0u));
    CHECK(actor.timer==0&&actor.period==90);
    CHECK(portable_lava_bubble_advance(&actor,sine)&&actor.phase==2u&&actor.timer==64);
    PortableLavaBubbleCourse course;
    CHECK(portable_lava_bubble_course_init(&course,&actor,1u));
    CHECK(portable_lava_bubble_course_advance(&course,sine));
    CHECK(course.actors[0].phase==3u&&actor.phase==2u);
    CHECK(portable_lava_bubble_course_init(&course,course.initial,course.count));
    CHECK(course.actors[0].phase==2u&&course.actors[0].timer==64);
    puts("lava bubble: PASS");return 0;
}
