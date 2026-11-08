#ifndef PORTABLE_NATIVE_IMAGE_H
#define PORTABLE_NATIVE_IMAGE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *pixels;
    uint16_t width, height;
    int16_t anchor_x, anchor_y;
    const uint8_t *palette;
    uint16_t palette_count, palette_base;
} PortableNativeImage;

/* MKSCIMG1 stores tightly packed little-endian ARGB pixels, never host pointers. */
int portable_native_image_open(const uint8_t *bytes, size_t size,
                               PortableNativeImage *image);
uint32_t portable_native_image_pixel(const PortableNativeImage *image,
                                     size_t x, size_t y);
/* Colors are RGB555 indexed by the original OBJ palette position. */
uint32_t portable_native_image_pixel_palette(const PortableNativeImage *image,
    size_t x, size_t y, const uint16_t *colors, size_t count);
/* A NULL mask overrides all supplied colors; zero mask entries retain art variants. */
uint32_t portable_native_image_pixel_palette_masked(const PortableNativeImage *image,
    size_t x, size_t y, const uint16_t *colors, size_t count, const uint8_t *dirty);
typedef struct {
    const uint8_t *entries;
    uint32_t duration;
    uint16_t count;
    uint8_t loop;
} PortableNativeAnimation;
int portable_native_animation_open(const uint8_t *bytes, size_t size,
                                   PortableNativeAnimation *animation);
uint32_t portable_native_animation_frame(const PortableNativeAnimation *animation,
                                         uint32_t tick);
#endif
