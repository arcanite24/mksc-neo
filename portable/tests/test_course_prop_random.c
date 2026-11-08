#include "course_prop.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"course prop RNG line %d: %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct { unsigned draws; } PropRandom;
static uint32_t next_random(void *context)
{
    static const uint32_t values[]={0x7fffu,0x04abu,0x2345u};
    PropRandom *random=context;
    return values[random->draws++%3u];
}

int run_course_prop_random(void)
{
    static const uint32_t codes[]={0xa30000u,0x010000u,0xa30000u,0xa00000u,0xa30000u};
    PortableCourseProp initial[5];
    for(unsigned i=0u;i<5u;++i){
        CHECK(portable_course_prop_init(&initial[i],(100+(int32_t)i)*65536,120*65536,codes[i],256u,(uint8_t)i));
        initial[i].placement_order=(uint8_t)(4u-i);
    }
    CHECK(!initial[0].pier_boo.phase && !initial[2].pier_boo.phase && !initial[4].pier_boo.phase);
    const PortableCourseProp authored[5]={initial[0],initial[1],initial[2],initial[3],initial[4]};
    PropRandom random={0};PortableCourseProps course;
    CHECK(portable_course_props_init_with_random(&course,initial,5u,next_random,&random));
    CHECK(random.draws==3u && course.initial==initial && course.count==5u);
    CHECK(course.actors[0].pier_boo.phase==0x7c00u && course.actors[2].pier_boo.phase==0x0400u &&
        course.actors[4].pier_boo.phase==0x2000u);
    CHECK(!memcmp(authored,initial,sizeof initial));
    for(unsigned i=0u;i<5u;++i)
        CHECK(course.actors[i].x_q16==initial[i].x_q16 && course.actors[i].placement_order==4u-i);
    /* Rebuilding from authored placements and a reset owner reproduces phases,
     * regardless of animation/removal in the preceding race. */
    const PortableCourseProps seeded=course;
    course.actors[0].pier_boo.phase=0x1400u;course.actors[2].state=8u;
    random.draws=0u;
    CHECK(portable_course_props_init_with_random(&course,course.initial,course.count,next_random,&random));
    CHECK(random.draws==3u && !memcmp(&seeded,&course,sizeof course));
    random.draws=0u;
    CHECK(portable_course_props_init_with_random(&course,NULL,0u,next_random,&random));
    CHECK(!random.draws && !course.count);
    CHECK(portable_course_props_init_with_random(&course,initial+1u,1u,next_random,&random));
    CHECK(!random.draws && course.count==1u);
    CHECK(!portable_course_props_init_with_random(&course,NULL,1u,next_random,&random));
    CHECK(!portable_course_props_init_with_random(&course,initial,PORTABLE_COURSE_PROP_CAPACITY+1u,next_random,&random));
    CHECK(!random.draws);
    CHECK(portable_course_props_init(&course,initial,5u));
    CHECK(!course.actors[0].pier_boo.phase && !course.actors[2].pier_boo.phase && !course.actors[4].pier_boo.phase);
    return 0;
}
