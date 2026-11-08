#ifndef PORTABLE_CANNONBALL_SOUND_H
#define PORTABLE_CANNONBALL_SOUND_H
#include "cannonball.h"
#include "spatial_audio.h"

/* Presentation requests from 56440, consumed once by the audio host. Player
 * occupancy stays in the sequencer; it cannot change authoritative motion. */
typedef struct {
    uint16_t song_id,volume;
    uint8_t require_idle;
} PortableCannonballSound;
typedef struct {
    PortableCannonballSound events[PORTABLE_CANNONBALL_CAPACITY];
    uint32_t serial;
    uint8_t count;
} PortableCannonballSounds;
typedef struct {
    int32_t x_q16,y_q16;
    uint32_t driver_flags;
    uint16_t heading;
} PortableCannonballListener;

static inline int portable_cannonball_sound_request(const PortableCannonball *actor,
    const PortableCannonballListener *listener,int launch_projected,
    PortableCannonballSound *event)
{
    const int next_state=actor->state+(actor->timer==0);
    const int launch=next_state==2;
    const int explode=next_state==4;
    if((!launch && !explode) || (listener->driver_flags & UINT32_C(0x20)) ||
       (launch && !launch_projected))return 0;
    const uint16_t volume=portable_spatial_audio_volume(
        (uint32_t)actor->x_q16,(uint32_t)actor->y_q16,
        (uint32_t)listener->x_q16,(uint32_t)listener->y_q16);
    if(volume<=63u)return 0;
    *event=(PortableCannonballSound){.song_id=launch?382u:383u,
        .volume=volume,.require_idle=(uint8_t)launch};
    return 1;
}
#endif
