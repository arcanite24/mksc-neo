#ifndef PORTABLE_SNOWMAN_MOTION_H
#define PORTABLE_SNOWMAN_MOTION_H
#include "snowman.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_snowman_motion_tick(PortableSnowmanCourse *course,
    uint32_t course_id,const char *mode)
{
    if(course->count>PORTABLE_SNOWMAN_CAPACITY){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s snowman count=%zu unsupported\n",
            course_id,mode,course->count);return 0;
    }
    for(size_t i=course->count;i!=0u;--i){
        if(!portable_snowman_advance(&course->actors[i-1u])){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s snowman=%zu state=%" PRId32 " timer=%" PRId32 " unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].state,course->actors[i-1u].timer);return 0;
        }
    }
    return 1;
}
#endif
