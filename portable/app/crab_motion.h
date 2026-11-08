#ifndef PORTABLE_CRAB_MOTION_H
#define PORTABLE_CRAB_MOTION_H
#include "crab.h"
#include "diagnostic.h"
#include <inttypes.h>

static inline int portable_crab_motion_tick(PortableCrabCourse *course,
    const int32_t *sine, uint32_t course_id, const char *mode)
{
    if (course->count>PORTABLE_CRAB_CAPACITY) {
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s crab count=%zu unsupported\n",
            course_id,mode,course->count);
        return 0;
    }
    for (size_t i=0;i<course->count;++i) {
        if (!portable_crab_advance(&course->actors[i],sine)) {
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s crab=%zu variant=%u timer=%" PRId32 " unsupported\n",
                course_id,mode,i,(unsigned)course->actors[i].variant,course->actors[i].timer);
            return 0;
        }
    }
    return 1;
}
#endif
