#ifndef PORTABLE_THUNDERCLOUD_MOTION_H
#define PORTABLE_THUNDERCLOUD_MOTION_H
#include "thundercloud.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_thundercloud_motion_tick(PortableThundercloudCourse *course,
    uint32_t course_id,const char *mode)
{
    if(course->count>PORTABLE_THUNDERCLOUD_CAPACITY)return 0;
    for(size_t i=course->count;i!=0u;--i)
        if(!portable_thundercloud_advance(&course->actors[i-1u],course->palette_phase)){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s thundercloud=%zu state=%d timer=%d unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].state,course->actors[i-1u].timer);return 0;
        }
    return 1;
}
#endif
