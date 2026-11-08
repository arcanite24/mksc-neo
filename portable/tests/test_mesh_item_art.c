#include "mesh_item_art.h"
#include "hud_asset.h"
#include "item_box_asset.h"
#include "race_particle_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh item art line %d: %s\n", __LINE__, #c);                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define HUD_BYTES                                                                                  \
    (44u + 4096u + 96u * 2u + 20u * 2u + 60u + 6u * 2u + 2u * 2u + 6u * 2u + 1792u + 16u * 2u +    \
     17u * 2u)
#define BOX_BYTES (24u + 4096u + 16u * 2u)
#define EFFECT_BYTES (32u + 5u * 8u + 8192u + 80u * 2u + 304u)
static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8u);
}
static void w32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}
static void hud_payload(uint8_t *b)
{
    memset(b, 0, HUD_BYTES);
    const uint32_t words[11] = {0x32445548u, 4096, 96, 20, 60, 6, 2, 6, 1792, 16, 17};
    for (unsigned i = 0; i < 11; ++i)
        w32(b + i * 4u, words[i]);
    uint8_t *palette = b + 44 + 4096;
    w16(palette + (64 + 1) * 2, 0x03e0);
    w16(palette + (80 + 1) * 2, 0x001f);
    uint8_t *tiles = palette + 96 * 2 + 20 * 2 + 60 + 6 * 2 + 2 * 2 + 6 * 2;
    memset(tiles, 0x11, 1792);
    for (unsigned tile = 0; tile < 56; ++tile)
        tiles[tile * 32] = 0x10;
    uint8_t *cells = tiles + 1792;
    w16(cells + 5 * 2, 0x70);
    w16(cells + 6 * 2, 0x71);
    w16(cells + 9 * 2, 0x72);
    w16(cells + 10 * 2, 0x73);
    uint8_t *selectors = cells + 32;
    for (unsigned i = 0; i < 17; ++i)
        w16(selectors + i * 2, (uint16_t)(i >= 15 ? 0xf000 : 0xe000));
}
static void box_payload(uint8_t *b)
{
    memset(b, 0, BOX_BYTES);
    const uint32_t words[6] = {0x31584249u, 4096, 16, 8, 32, 32};
    for (unsigned i = 0; i < 6; ++i)
        w32(b + i * 4u, words[i]);
    for (unsigned frame = 0; frame < 8; ++frame)
    {
        const unsigned color = frame % 2u + 1u;
        memset(b + 24 + frame * 512, (int)(color | (color << 4u)), 512);
        b[24 + frame * 512] = (uint8_t)(color << 4u);
        b[24 + frame * 512 + 1] = 0x64;
    }
    w16(b + 24 + 4096 + 2, 31);
    w16(b + 24 + 4096 + 4, 0x03e0);
}
static void effect_payload(uint8_t *b)
{
    memset(b, 0, EFFECT_BYTES);
    const uint32_t words[8] = {0x31435846u, 8192, 80, 5, 0x100, 3, 304, 0};
    for (unsigned i = 0; i < 8; ++i)
        w32(b + i * 4u, words[i]);
    for (unsigned i = 0; i < 5; ++i)
    {
        w32(b + 32 + i * 8, i * 32);
        w16(b + 36 + i * 8, 32);
        w16(b + 38 + i * 8, 1);
    }
    uint8_t *tiles = b + 72, *palette = tiles + 8192, *cells = palette + 160;
    for (unsigned bank = 0; bank < 5; ++bank)
    {
        w16(palette + (bank * 16 + 1) * 2, 31);
        w16(palette + (bank * 16 + 2) * 2, 0x03e0);
        w16(palette + (bank * 16 + 3) * 2, 0x7c00);
    }
    for (unsigned orientation = 0; orientation < 18; ++orientation)
    {
        for (unsigned tile = 0; tile < 4; ++tile)
        {
            const unsigned color = (orientation + tile) % 3u + 1u;
            memset(tiles + (orientation * 4 + tile) * 32, (int)(color | (color << 4u)), 32);
        }
        tiles[orientation * 128] &= 0xf0u;
        uint8_t *cell = cells + 160 + orientation * 8;
        w16(cell, 1);
        cell[2] = (uint8_t)(-13 + (int)(orientation % 3u));
        w16(cell + 4, (uint16_t)(0x4000u | (orientation & 1u ? 0x1000u : 0)));
        w16(cell + 6, (uint16_t)(0x100u + orientation * 4u + ((3u + orientation / 6u) << 12u)));
    }
    /* Banana uses source tile138/bank4; opaque black must survive baking. */
    memset(tiles + 56 * 32, 0x11, 4 * 32);
    tiles[56 * 32] = 0x10;
    w16(palette + (16 + 1) * 2, 0);
}
int main(void)
{
    uint8_t *hud = malloc(HUD_BYTES), *box = malloc(BOX_BYTES), *effect = malloc(EFFECT_BYTES);
    CHECK(hud && box && effect);
    hud_payload(hud);
    box_payload(box);
    effect_payload(effect);
    PortableAssetSectionView sections[3] = {{.kind = PORTABLE_ASSET_SECTION_HUD_GRAPHICS,
                                             .content_id = 1,
                                             .version = 2,
                                             .bytes = hud,
                                             .size = HUD_BYTES},
                                            {.kind = PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS,
                                             .content_id = 1,
                                             .version = 1,
                                             .bytes = box,
                                             .size = BOX_BYTES},
                                            {.kind = PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,
                                             .content_id = 1,
                                             .version = 2,
                                             .bytes = effect,
                                             .size = EFFECT_BYTES}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 3};
    PortableMeshItemArt *art = NULL;
    char error[256];
    CHECK(portable_mesh_item_art_open(&pack, &art, error, sizeof error));
    const PortableNativeImage *green = portable_mesh_item_art_hud(art, 0x1000),
                              *red = portable_mesh_item_art_hud(art, 0x1002);
    CHECK(green && red && green->width == 16 && green->height == 16);
    CHECK(portable_native_image_pixel(green, 0, 0) == 0 &&
          portable_native_image_pixel(green, 1, 0) == 0xff00ff00u &&
          portable_native_image_pixel(red, 1, 0) == 0xffff0000u);
    CHECK(
        portable_native_image_pixel(portable_mesh_item_art_hud(art, 0x1001), 1, 0) == 0xff00ff00u &&
        portable_native_image_pixel(portable_mesh_item_art_hud(art, 0x1003), 1, 0) == 0xffff0000u);
    CHECK(!portable_mesh_item_art_hud(art, 0x1006) && !portable_mesh_item_art_hud(art, 0x100d) &&
          !portable_mesh_item_art_hud(art, 0) && !portable_mesh_item_art_hud(art, 0x2000));
    CHECK(portable_mesh_item_art_actor(art, 0x100b, 0) == portable_mesh_item_art_hud(art, 0x100b) &&
          !portable_mesh_item_art_actor(art, 0x100b, 1));
    const PortableNativeImage *a = portable_mesh_item_art_actor(art, 0x1000, 0),
                              *mirrored = portable_mesh_item_art_actor(art, 0x1000, 1);
    CHECK(a && mirrored && a->anchor_x == 8 && a->anchor_y == 13 && mirrored->anchor_y == 12);
    CHECK(portable_native_image_pixel(a, 0, 0) == 0 &&
          portable_native_image_pixel(a, 1, 0) == 0xffff0000u &&
          portable_native_image_pixel(mirrored, 0, 0) == 0xff0000ffu);
    for (unsigned i = 0; i < 6; ++i)
        CHECK(portable_mesh_item_art_actor(art, 0x1002, i) &&
              portable_mesh_item_art_actor(art, 0x1004, i));
    CHECK(!portable_mesh_item_art_actor(art, 0x1000, 6) &&
          !portable_mesh_item_art_actor(art, 0x1006, 0));
    const PortableNativeImage *banana = portable_mesh_item_art_actor(art, 0x1005, 0);
    CHECK(banana && banana->anchor_y == 13 && portable_native_image_pixel(banana, 0, 0) == 0 &&
          portable_native_image_pixel(banana, 1, 0) == 0xff000000u);
    const PortableNativeImage *b0 = portable_mesh_item_art_box(art, 0),
                              *b4 = portable_mesh_item_art_box(art, 4),
                              *b32 = portable_mesh_item_art_box(art, 32);
    CHECK(b0 && b0->width == 32 && b0->height == 32 && b0->anchor_y == 30 &&
          portable_native_image_pixel(b0, 0, 0) == 0 &&
          portable_native_image_pixel(b0, 1, 0) == 0xffff0000u &&
          portable_native_image_pixel(b4, 1, 0) == 0xff00ff00u &&
          portable_native_image_pixel(b0, 3, 0) != portable_native_image_pixel(b32, 3, 0) &&
          portable_mesh_item_art_box(art, 128) == b0);
    PortableMeshItemArt *before = art;
    /* Failed late source-cell validation cannot retire the prior image owner. */
    effect[EFFECT_BYTES - 2] = 0xff;
    effect[EFFECT_BYTES - 1] = 0xff;
    CHECK(!portable_mesh_item_art_open(&pack, &art, error, sizeof error) && art == before &&
          strstr(error, "payload") && portable_native_image_pixel(green, 1, 0) == 0xff00ff00u);
    effect_payload(effect);
    sections[0].size--;
    CHECK(!portable_mesh_item_art_open(&pack, &art, error, sizeof error) && art == before);
    sections[0].size++;
    sections[2].version = 1;
    CHECK(!portable_mesh_item_art_open(&pack, &art, error, sizeof error) && art == before);
    /* Cache images own their bytes, independently of all decoder/pack storage. */
    memset(hud, 0, HUD_BYTES);
    memset(box, 0, BOX_BYTES);
    memset(effect, 0, EFFECT_BYTES);
    free(hud);
    free(box);
    free(effect);
    CHECK(portable_native_image_pixel(green, 1, 0) == 0xff00ff00u &&
          portable_native_image_pixel(banana, 1, 0) == 0xff000000u &&
          portable_native_image_pixel(b0, 1, 0) == 0xffff0000u);
    portable_mesh_item_art_close(art);
    portable_mesh_item_art_close(NULL);
    puts("Mesh item artwork: PASS source palettes orientations transparency owned rollback");
    return 0;
}
