#ifndef PORTABLE_MOUSE_MOTION_H
#define PORTABLE_MOUSE_MOTION_H
#include "mouse.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_mouse_motion_tick(PortableMouseCourse *course,const int32_t *sine,
    size_t sine_count,uint32_t course_id,const char *mode)
{
    if(course->count>PORTABLE_MOUSE_CAPACITY){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s mouse count=%zu unsupported\n",course_id,mode,course->count);return 0;
    }
    for(size_t i=course->count;i!=0u;--i)
        if(!portable_mouse_advance(&course->actors[i-1u],sine,sine_count)){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s mouse=%zu variant=%u timer=%d flags=%u unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].variant,course->actors[i-1u].timer,course->actors[i-1u].flags);return 0;
        }
    return 1;
}
#endif
