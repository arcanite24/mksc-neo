#ifndef PORTABLE_BOO_MOTION_H
#define PORTABLE_BOO_MOTION_H
#include "boo.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_boo_motion_tick(PortableBooCourse *course,
    const int32_t *sine,size_t sine_count,uint32_t course_id,const char *mode)
{
    if(course->count>PORTABLE_BOO_CAPACITY){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s boo count=%zu unsupported\n",
            course_id,mode,course->count);return 0;
    }
    for(size_t i=course->count;i!=0u;--i){
        if(!portable_boo_advance(&course->actors[i-1u],sine,sine_count)){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s boo=%zu state=%" PRIu32 " unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].state);return 0;
        }
    }
    return 1;
}
#endif
