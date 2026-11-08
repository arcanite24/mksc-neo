#ifndef PORTABLE_PIRANHA_MOTION_H
#define PORTABLE_PIRANHA_MOTION_H
#include "piranha.h"
#include "diagnostic.h"
#include <inttypes.h>
static inline int portable_piranha_motion_tick(PortablePiranhaCourse *course,
    uint32_t driver_flags,uint32_t course_id,const char *mode,PortablePiranhaSounds *sounds)
{
    if(course->count>PORTABLE_PIRANHA_CAPACITY){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s piranha count=%zu unsupported\n",
            course_id,mode,course->count);return 0;
    }
    for(size_t i=course->count;i!=0u;--i){
        uint16_t event;
        if(!portable_piranha_advance(&course->actors[i-1u],driver_flags,&event)){
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s piranha=%zu state=%" PRId32 " timer=%" PRId32 " unsupported\n",
                course_id,mode,i-1u,course->actors[i-1u].state,course->actors[i-1u].timer);return 0;
        }
        if(event && sounds){
            if(sounds->count==PORTABLE_PIRANHA_CAPACITY)return 0;
            sounds->songs[sounds->count++]=event;
        }
    }
    return 1;
}
#endif
