#ifndef PORTABLE_SEAGULL_MOTION_H
#define PORTABLE_SEAGULL_MOTION_H
#include "seagull.h"
#include "diagnostic.h"
#include <inttypes.h>

static inline int portable_seagull_motion_tick(PortableSeagullCourse *course,
    const int32_t *sine, uint32_t course_id, const char *mode)
{
    if (course->count>PORTABLE_SEAGULL_CAPACITY) {
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s seagull count=%zu unsupported\n",
            course_id,mode,course->count);
        return 0;
    }
    for (size_t i=0;i<course->count;++i) {
        if (!portable_seagull_advance(&course->actors[i],sine)) {
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s seagull=%zu state=%u timer=%" PRId32 " unsupported\n",
                course_id,mode,i,(unsigned)course->actors[i].state,course->actors[i].timer);
            return 0;
        }
    }
    return 1;
}
#endif
