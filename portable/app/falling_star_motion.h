#ifndef PORTABLE_FALLING_STAR_MOTION_H
#define PORTABLE_FALLING_STAR_MOTION_H
#include "falling_star.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_falling_star_motion_tick(PortableFallingStarCourse *course,
    uint32_t course_id,const char *mode)
{
    if(course->count>PORTABLE_FALLING_STAR_CAPACITY){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s falling_star count=%zu unsupported\n",
            course_id,mode,course->count);return 0;
    }
    for(size_t i=course->count;i!=0u;--i)
        if(!portable_falling_star_advance(&course->actors[i-1u])){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s falling_star=%zu state=%d timer=%d unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].state,course->actors[i-1u].timer);return 0;
        }
    return 1;
}
#endif
