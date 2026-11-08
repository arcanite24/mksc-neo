#ifndef PORTABLE_SPATIAL_AUDIO_H
#define PORTABLE_SPATIAL_AUDIO_H
#include <stdint.h>
#include <limits.h>
/* 804CD28: wrapping position differences/squares, truncation toward zero,
 * signed range comparison and wrapping halfword result. Camera coordinates
 * are caller-owned values; no original scene pointer or backend is involved. */
static inline int32_t portable_spatial_signed(uint32_t bits)
{
    return bits <= INT32_MAX ? (int32_t)bits :
        -INT32_C(1) - (int32_t)(UINT32_MAX - bits);
}
static inline uint16_t portable_spatial_audio_volume(
    uint32_t x, uint32_t y, uint32_t camera_x, uint32_t camera_y)
{
    const int32_t dx = portable_spatial_signed(x - camera_x) / 65536;
    const int32_t dy = portable_spatial_signed(y - camera_y) / 65536;
    const int32_t distance = portable_spatial_signed(
        (uint32_t)dx * (uint32_t)dx + (uint32_t)dy * (uint32_t)dy);
    return distance > INT32_C(0x3ffff) ? 0u :
        (uint16_t)(INT32_C(256) - distance / 1024);
}
#endif
