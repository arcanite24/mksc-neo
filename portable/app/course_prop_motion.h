#ifndef PORTABLE_COURSE_PROP_MOTION_H
#define PORTABLE_COURSE_PROP_MOTION_H
#include "course_prop.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_course_prop_motion_tick(PortableCourseProps *course,
    const int32_t *sine,size_t sine_count,uint32_t course_id,const char *mode,
    const PortableCoursePropListener *listener)
{
    if(portable_course_props_advance_heard(course,sine,sine_count,listener))return 1;
    portable_diagnostic_printf("course=%" PRIu32 " mode=%s common prop count=%zu unsupported\n",
        course_id,mode,course->count);
    for(size_t i=0;i<course->count && i<PORTABLE_COURSE_PROP_CAPACITY;++i){
        const PortableCourseProp *a=&course->actors[i];
        if(a->flags&1u)
            portable_diagnostic_printf("prop=%zu code=%" PRIx32 " state=%u timer=%" PRId32 " patrol_cycle=%u stride=%u/%u\n",
                i,a->source_code,a->state,a->timer,a->mechakoopa.cycle,
                a->mechakoopa.animation_phase,a->mechakoopa.animation_ticks);
    }
    return 0;
}
#endif
