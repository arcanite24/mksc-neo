#ifndef PORTABLE_PENGUIN_MOTION_H
#define PORTABLE_PENGUIN_MOTION_H
#include "penguin.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_penguin_motion_tick(PortablePenguinCourse *course,
    const int32_t *sine,size_t sine_count,uint32_t course_id,const char *mode)
{
    if(course->count>PORTABLE_PENGUIN_CAPACITY){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s penguin count=%zu unsupported\n",
            course_id,mode,course->count);return 0;
    }
    for(size_t i=course->count;i!=0u;--i){
        if(!portable_penguin_advance(&course->actors[i-1u],sine,sine_count)){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s penguin=%zu timer=%" PRId32 " unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].timer);return 0;
        }
    }
    return 1;
}
#endif
