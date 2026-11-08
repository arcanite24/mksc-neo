#ifndef PORTABLE_THWOMP_SOUND_H
#define PORTABLE_THWOMP_SOUND_H
#include "thwomp.h"
#include "spatial_audio.h"
/* 540A0/5442C/54888/54D50/551D0/5563C: landing song384.
 * The host owns sound enable/volume settings, never actor motion. */
typedef struct {
    uint32_t serial;
    uint16_t volumes[PORTABLE_THWOMP_CAPACITY];
    uint8_t count;
} PortableThwompSounds;
static inline uint16_t portable_thwomp_landing_volume(const PortableThwomp *actor,
    int32_t listener_x,int32_t listener_y,uint32_t listener_flags)
{
    if(listener_flags&UINT32_C(0x20))return 0u;
    const uint16_t volume=portable_spatial_audio_volume(
        (uint32_t)actor->x_q16,(uint32_t)actor->y_q16,
        (uint32_t)listener_x,(uint32_t)listener_y);
    return volume>24u?volume:0u;
}
#endif
