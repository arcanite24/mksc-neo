#ifndef PORTABLE_CANNONBALL_MOTION_H
#define PORTABLE_CANNONBALL_MOTION_H
#include "cannonball_sound.h"
#include "camera_projection.h"
#include "diagnostic.h"
#include <inttypes.h>

static inline int portable_cannonball_motion_tick(PortableCannonballCourse *course,
    uint32_t course_id, const char *mode,
    const PortableCannonballListener *listener,const int32_t *sine,size_t sine_count,
    PortableCannonballSounds *sounds)
{
    if (course->count>PORTABLE_CANNONBALL_CAPACITY) {
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s cannonball count=%zu unsupported\n",
            course_id,mode,course->count);
        return 0;
    }
    sounds->count=0u;
    ++sounds->serial;
    if(course->count!=0u && (sine==NULL || sine_count<1024u)){
        portable_diagnostic_printf("course=%" PRIu32 " mode=%s cannonball audio missing camera sine table\n",
            course_id,mode);
        return 0;
    }
    PortableCameraState camera;
    PortableCameraProjection projection;
    portable_camera_state_update(&camera,listener->x_q16,listener->y_q16,listener->heading);
    const int32_t signed_heading=listener->heading<=INT16_MAX ?
        (int32_t)listener->heading : (int32_t)listener->heading-65536;
    const unsigned angle=(unsigned)(signed_heading/64)&1023u;
    if(course->count!=0u && portable_camera_projection_build(&camera,
        sine[angle],sine[(256u-angle)&1023u],31414,9319,&projection)!=0)return 0;
    for (size_t i=0;i<course->count;++i) {
        const PortableCannonball *actor=&course->actors[i];
        PortableCannonballSound sound;
        PortableCameraProjectionResult projected;
        const int visible=actor->state+(actor->timer==0)==2 &&
            portable_camera_projection_project(&camera,&projection,actor->x_q16,
                actor->y_q16,actor->height_q16,&projected)==0;
        if(portable_cannonball_sound_request(actor,listener,visible,&sound))
            sounds->events[sounds->count++]=sound;
        if (!portable_cannonball_advance(&course->actors[i])) {
            portable_diagnostic_printf("course=%" PRIu32 " mode=%s cannonball=%zu state=%u timer=%" PRId32 " unsupported\n",
                course_id,mode,i,(unsigned)course->actors[i].state,course->actors[i].timer);
            return 0;
        }
    }
    return 1;
}
#endif
