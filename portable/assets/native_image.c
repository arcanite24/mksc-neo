#include "native_image.h"
#include <string.h>

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int16_t signed16(uint16_t value)
{
    return (int16_t)(value < 0x8000u ? (int32_t)value : (int32_t)value - 65536);
}
int portable_native_image_open(const uint8_t *bytes, size_t size, PortableNativeImage *image)
{
    PortableNativeImage candidate = {0};
    if (bytes == NULL || image == NULL || size < 32u || u16(bytes + 8u) != 32u ||
        u32(bytes + 24u) != 32u || u32(bytes + 28u) != size)
        return 0;
    const int indexed = memcmp(bytes, "MKSCIMG2", 8u) == 0 && u16(bytes + 10u) == 2u;
    if (!indexed &&
        (memcmp(bytes, "MKSCIMG1", 8u) != 0 || u16(bytes + 10u) != 1u || u32(bytes + 20u) != 0u))
        return 0;
    candidate.width = u16(bytes + 12u);
    candidate.height = u16(bytes + 14u);
    if (!candidate.width || !candidate.height || candidate.width > 4096u ||
        candidate.height > 4096u)
        return 0;
    const size_t pixels = (size_t)candidate.width * candidate.height;
    if (indexed)
    {
        candidate.palette_count = u16(bytes + 20u);
        candidate.palette_base = u16(bytes + 22u);
        if (!candidate.palette_count || candidate.palette_count > 128u ||
            candidate.palette_base + candidate.palette_count > 128u ||
            size != 32u + pixels + candidate.palette_count * 2u)
            return 0;
        candidate.palette = bytes + 32u + pixels;
        for (size_t i = 0; i < pixels; ++i)
        {
            const unsigned index = bytes[32u + i];
            if (index != 255u &&
                (index < candidate.palette_base ||
                 index >= (unsigned)candidate.palette_base + candidate.palette_count))
                return 0;
        }
    }
    else if (size != 32u + pixels * 4u)
        return 0;
    candidate.anchor_x = signed16(u16(bytes + 16u));
    candidate.anchor_y = signed16(u16(bytes + 18u));
    candidate.pixels = bytes + 32u;
    *image = candidate;
    return 1;
}
uint32_t portable_native_image_pixel_palette_masked(const PortableNativeImage *image, size_t x,
                                                    size_t y, const uint16_t *colors, size_t count,
                                                    const uint8_t *dirty)
{
    if (!image || !image->pixels || x >= image->width || y >= image->height)
        return 0u;
    const size_t offset = y * image->width + x;
    if (!image->palette)
        return u32(image->pixels + offset * 4u);
    const unsigned index = image->pixels[offset];
    if (index == 255u)
        return 0u;
    const uint16_t color = colors && index < count && (!dirty || dirty[index])
                               ? colors[index]
                               : u16(image->palette + (index - image->palette_base) * 2u);
    const unsigned r = color & 31u, g = (color >> 5u) & 31u, b = (color >> 10u) & 31u;
    return 0xff000000u | (r * 8u + r / 4u) << 16u | (g * 8u + g / 4u) << 8u | (b * 8u + b / 4u);
}
uint32_t portable_native_image_pixel_palette(const PortableNativeImage *image, size_t x, size_t y,
                                             const uint16_t *colors, size_t count)
{
    return portable_native_image_pixel_palette_masked(image, x, y, colors, count, NULL);
}
uint32_t portable_native_image_pixel(const PortableNativeImage *image, size_t x, size_t y)
{
    return portable_native_image_pixel_palette(image, x, y, NULL, 0u);
}

int portable_native_animation_open(const uint8_t *bytes, size_t size,
                                   PortableNativeAnimation *animation)
{
    PortableNativeAnimation candidate;
    uint64_t duration = 0u;
    if (bytes == NULL || animation == NULL || size < 32u || memcmp(bytes, "MKSCANI1", 8u) != 0 ||
        u16(bytes + 8u) != 32u || u16(bytes + 10u) != 1u || u16(bytes + 14u) > 1u ||
        u32(bytes + 20u) != 32u || u32(bytes + 24u) != size || u32(bytes + 28u) != 0u)
        return 0;
    candidate.count = u16(bytes + 12u);
    if (candidate.count == 0u || candidate.count > 4096u ||
        size != 32u + (size_t)candidate.count * 8u)
        return 0;
    candidate.entries = bytes + 32u;
    candidate.loop = (uint8_t)u16(bytes + 14u);
    candidate.duration = u32(bytes + 16u);
    for (unsigned index = 0u; index < candidate.count; ++index)
    {
        uint32_t ticks = u32(candidate.entries + index * 8u + 4u);
        if (ticks == 0u)
            return 0;
        duration += ticks;
    }
    if (duration != candidate.duration || duration > UINT32_MAX)
        return 0;
    *animation = candidate;
    return 1;
}
uint32_t portable_native_animation_frame(const PortableNativeAnimation *animation, uint32_t tick)
{
    if (animation == NULL || animation->entries == NULL || animation->duration == 0u ||
        animation->count == 0u)
        return 0u;
    if (animation->loop)
        tick %= animation->duration;
    else if (tick >= animation->duration)
        tick = animation->duration - 1u;
    for (unsigned index = 0u; index < animation->count; ++index)
    {
        const uint8_t *entry = animation->entries + index * 8u;
        uint32_t ticks = u32(entry + 4u);
        if (tick < ticks)
            return u32(entry);
        tick -= ticks;
    }
    return 0u;
}
