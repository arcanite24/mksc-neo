#include "ui_art.h"

#include <stdlib.h>
#include <string.h>

#include "native_image.h"

enum { ART_SLOTS = 256 };

typedef struct
{
    const uint8_t *pack_bytes;
    const void *pack_sections; /* Mod composition shares base bytes. */
    uint32_t id;
    int smooth;
    uint64_t used;
    uint32_t *pixels;
    UiImage image;
} ArtEntry;

struct UiArtCache
{
    ArtEntry entries[ART_SLOTS];
    uint64_t clock;
};

UiArtCache *ui_art_create(void)
{
    return calloc(1u, sizeof(UiArtCache));
}

void ui_art_clear(UiArtCache *cache)
{
    if (cache == NULL) return;
    for (unsigned i = 0u; i < ART_SLOTS; ++i) free(cache->entries[i].pixels);
    memset(cache,0,sizeof *cache);
}

void ui_art_destroy(UiArtCache *cache)
{
    ui_art_clear(cache);
    free(cache);
}

static int find_graphic(const PortableAssetPackView *pack, uint32_t id, PortableAssetSectionView *out)
{
    return pack != NULL && id != 0u &&
           portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS, id, out) ==
               PORTABLE_ASSET_PACK_OK;
}

/* One EPX pass: each source pixel becomes a 2x2 block whose corners take a
 * neighbor's color where two neighbors agree, rounding diagonal steps. */
static uint32_t *epx(const uint32_t *source, int width, int height)
{
    uint32_t *out = malloc((size_t)width * (size_t)height * 4u * sizeof *out);
    if (out == NULL) return NULL;
    const int out_width = width * 2;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const uint32_t p = source[y * width + x];
            const uint32_t a = y > 0 ? source[(y - 1) * width + x] : p;
            const uint32_t b = x + 1 < width ? source[y * width + x + 1] : p;
            const uint32_t c = x > 0 ? source[y * width + x - 1] : p;
            const uint32_t d = y + 1 < height ? source[(y + 1) * width + x] : p;
            uint32_t *block = out + (size_t)(y * 2) * (size_t)out_width + (size_t)(x * 2);
            block[0] = c == a && c != d && a != b ? a : p;
            block[1] = a == b && a != c && b != d ? b : p;
            block[out_width] = d == c && d != b && c != a ? c : p;
            block[out_width + 1] = b == d && b != a && d != c ? d : p;
        }
    return out;
}

static int decode(ArtEntry *entry, const PortableAssetSectionView *section, int smooth)
{
    PortableNativeImage native;
    if (!portable_native_image_open(section->bytes, section->size, &native)) return 0;
    const int width = native.width, height = native.height;
    uint32_t *pixels = malloc((size_t)width * (size_t)height * sizeof *pixels);
    if (pixels == NULL) return 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint32_t color = portable_native_image_pixel(&native, (size_t)x, (size_t)y);
            /* Transparent pixels compare equal for EPX regardless of RGB. */
            if ((color >> 24) == 0u) color = 0u;
            pixels[y * width + x] = color;
        }
    int out_width = width, out_height = height;
    for (int pass = 0; smooth && pass < 2; ++pass)
    {
        uint32_t *larger = epx(pixels, out_width, out_height);
        if (larger == NULL) break;
        free(pixels);
        pixels = larger;
        out_width *= 2;
        out_height *= 2;
    }
    entry->pixels = pixels;
    entry->image.pixels = pixels;
    entry->image.width = out_width;
    entry->image.height = out_height;
    return 1;
}

const UiImage *ui_art_image(UiArtCache *cache, const PortableAssetPackView *pack, uint32_t id,
                            int smooth)
{
    if (cache == NULL || pack == NULL || id == 0u) return NULL;
    ++cache->clock;
    ArtEntry *oldest = &cache->entries[0];
    for (unsigned i = 0u; i < ART_SLOTS; ++i)
    {
        ArtEntry *entry = &cache->entries[i];
        if (entry->pixels != NULL && entry->id == id && entry->smooth == smooth &&
            entry->pack_bytes == pack->bytes && entry->pack_sections == pack->resolved_sections)
        {
            entry->used = cache->clock;
            return &entry->image;
        }
        if (entry->used < oldest->used) oldest = entry;
    }
    PortableAssetSectionView section;
    if (!find_graphic(pack, id, &section)) return NULL;
    free(oldest->pixels);
    memset(oldest, 0, sizeof *oldest);
    if (!decode(oldest, &section, smooth)) return NULL;
    oldest->pack_bytes = pack->bytes;
    oldest->pack_sections = pack->resolved_sections;
    oldest->id = id;
    oldest->smooth = smooth;
    oldest->used = cache->clock;
    return &oldest->image;
}

uint32_t ui_art_animation_frame(const PortableAssetPackView *pack, uint32_t id, uint32_t tick)
{
    PortableAssetSectionView section;
    if (!find_graphic(pack, id, &section)) return 0u;
    PortableNativeAnimation animation;
    if (!portable_native_animation_open(section.bytes, section.size, &animation)) return id;
    return portable_native_animation_frame(&animation, tick);
}

uint32_t ui_art_animation_duration(const PortableAssetPackView *pack, uint32_t id)
{
    PortableAssetSectionView section;
    PortableNativeAnimation animation;
    if (!find_graphic(pack, id, &section) ||
        !portable_native_animation_open(section.bytes, section.size, &animation))
        return 0u;
    return animation.duration;
}

uint32_t ui_art_average_color(const UiImage *image)
{
    if (image == NULL) return 0xff808080u;
    uint64_t r = 0u, g = 0u, b = 0u, n = 0u;
    for (int i = 0; i < image->width * image->height; ++i)
    {
        const uint32_t p = image->pixels[i];
        if ((p >> 24) < 128u) continue;
        r += (p >> 16) & 255u;
        g += (p >> 8) & 255u;
        b += p & 255u;
        ++n;
    }
    if (n == 0u) return 0xff808080u;
    return 0xff000000u | (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
}
