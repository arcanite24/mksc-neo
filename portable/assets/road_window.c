#include "road_map_asset.h"

static int32_t arithmetic_shift_right(int64_t value, unsigned shift)
{
    const int64_t divisor = INT64_C(1) << shift;

    if (value >= 0)
        return (int32_t)(value / divisor);
    return (int32_t)(-(((-value) + divisor - 1) / divisor));
}

static int32_t source_round_shift15(int64_t value)
{
    if (value < 0)
        value += 0x7fffu;
    return arithmetic_shift_right(value, 15);
}

PortableRoadMapStatus portable_road_map_asset_repack(
    const PortableRoadMapView *world,
    const PortableRoadMapBuffers *window,
    int32_t source_x_q16,
    int32_t source_y_q16,
    int16_t heading,
    const int32_t *sine_table,
    size_t sine_count,
    PortableRoadMapView *out_view)
{
    if(!world || !window || !out_view || !world->map || !window->map || !sine_table ||
       world==out_view)return PORTABLE_ROAD_MAP_INVALID_ARGUMENT;
    if(!world->width_tiles || !world->height_tiles ||
       world->height_tiles>SIZE_MAX/world->width_tiles ||
       world->width_tiles*world->height_tiles>world->map_bytes ||
       window->map_capacity<PORTABLE_ROAD_MAP_BYTES || sine_count<1024)
        return PORTABLE_ROAD_MAP_CAPACITY;
    uintptr_t src=(uintptr_t)world->map,dst=(uintptr_t)window->map;
    if((dst>=src && dst-src<world->map_bytes) ||
       (src>dst && src-dst<PORTABLE_ROAD_MAP_BYTES))
        return PORTABLE_ROAD_MAP_INVALID_ARGUMENT;
    /* Truncate signed division toward zero, then clear the low tile bit. */
    int32_t qx=(source_x_q16/524288)&~1;
    int32_t qy=(source_y_q16/524288)&~1;
    /* ROM 08033850..5a adds 0x40000000 before ASR16, wrapping at 32 bits:
     * equivalently add a quarter turn and reinterpret as signed 16-bit. */
    uint32_t bits=((uint32_t)(uint16_t)heading+0x4000u)&0xffffu;
    int32_t angle=bits<0x8000u?(int32_t)bits:(int32_t)bits-65536;
    int32_t index=(angle/64)&1023;
    int32_t cx=(qx-source_round_shift15((int64_t)56*sine_table[(256-index)&1023]))/2;
    int32_t cy=(qy-source_round_shift15((int64_t)56*sine_table[index]))/2;
    /* The source copies pairs of rows/columns. Express the same window as
     * ordinary world tile coordinates, retaining the 128-tile BG wrap. */
    for(int32_t y=(cy-32)*2;y<(cy+32)*2;y++){
        for(int32_t x=(cx-32)*2;x<(cx+32)*2;x++){
            uint8_t tile=0;
            if(x>=0 && y>=0 && (size_t)x<world->width_tiles && (size_t)y<world->height_tiles)
                tile=world->map[(size_t)y*world->width_tiles+(size_t)x];
            window->map[((uint32_t)y&127u)*128u+((uint32_t)x&127u)]=tile;
        }
    }
    *out_view=(PortableRoadMapView){window->map,PORTABLE_ROAD_MAP_BYTES,128,128,
        world->source_offset,world->source_offset_second};
    return PORTABLE_ROAD_MAP_OK;
}
