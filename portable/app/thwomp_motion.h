#ifndef PORTABLE_THWOMP_MOTION_H
#define PORTABLE_THWOMP_MOTION_H

#include "thwomp_sound.h"
#include "diagnostic.h"
#include <inttypes.h>

/* Keep diagnostics outside deterministic core and name unsupported content. */
static inline int portable_thwomp_motion_tick(PortableThwompCourse *course,
    uint32_t course_id, const char *mode, int32_t listener_x,int32_t listener_y,
    uint32_t listener_flags,PortableThwompSounds *sounds)
{
    if (course->count > PORTABLE_THWOMP_CAPACITY) {
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s Thwomp count=%zu unsupported\n",
            course_id,mode,course->count);
        return 0;
    }
    sounds->count=0u;
    ++sounds->serial;
    for (size_t i=0; i<course->count; ++i) {
        int landed;
        if (!portable_thwomp_advance(&course->actors[i],&landed)) {
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s Thwomp=%zu variant=%u state=%u unsupported\n",
                course_id,mode,i,(unsigned)course->actors[i].variant,
                (unsigned)course->actors[i].state);
            return 0;
        }
        if(landed){
            const uint16_t volume=portable_thwomp_landing_volume(&course->actors[i],
                listener_x,listener_y,listener_flags);
            if(volume!=0u)sounds->volumes[sounds->count++]=volume;
        }
    }
    return 1;
}
#endif
