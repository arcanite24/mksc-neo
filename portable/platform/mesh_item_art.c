#include "mesh_item_art.h"
#include "hud_asset.h"
#include "item_box_asset.h"
#include "race_particle_asset.h"
#include "hud_renderer.h"
#include "green_shell_renderer.h"
#include "red_shell_renderer.h"
#include "banana_renderer.h"
#include "item_box_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ICON_PIXELS 256u
#define BOX_PIXELS 1024u
#define OWNED_BYTES ((16u * ICON_PIXELS + 128u * BOX_PIXELS + 19u * ICON_PIXELS) * 4u)
#define UNTOUCHED UINT32_C(0x01000000)
struct PortableMeshItemArt
{
    PortableNativeImage hud[16], box[128], shell[3][6], banana;
    uint8_t *pixels;
};
typedef struct
{
    uint8_t tiles[PORTABLE_HUD_CHAR_BYTES], clock[PORTABLE_HUD_CLOCK_STEP_COUNT],
        items[PORTABLE_HUD_ITEM_TILE_BYTES];
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT], digits[PORTABLE_HUD_DIGIT_COUNT],
        left[PORTABLE_HUD_LEFT_CELL_COUNT], slash[PORTABLE_HUD_SLASH_CELL_COUNT],
        right[PORTABLE_HUD_RIGHT_CELL_COUNT], cells[PORTABLE_HUD_ITEM_CELL_COUNT],
        selectors[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    uint8_t box_tiles[PORTABLE_ITEM_BOX_TILE_BYTES];
    uint16_t box_palette[PORTABLE_ITEM_BOX_PALETTE_COUNT];
    PortableRaceParticleStorage effects;
} DecodeStorage;
static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity)
        snprintf(error, capacity, "3D item artwork: %s", message);
    return 0;
}
void portable_mesh_item_art_close(PortableMeshItemArt *owner)
{
    if (!owner)
        return;
    free(owner->pixels);
    free(owner);
}
static void image(PortableMeshItemArt *owner, size_t *offset, PortableNativeImage *out,
                  unsigned width, unsigned height, int anchor_x, int anchor_y, const uint32_t *argb)
{
    uint8_t *bytes = owner->pixels + *offset;
    for (size_t i = 0; i < (size_t)width * height; ++i)
        for (unsigned byte = 0; byte < 4u; ++byte)
            bytes[i * 4u + byte] = (uint8_t)(argb[i] >> (byte * 8u));
    *out = (PortableNativeImage){.pixels = bytes,
                                 .width = (uint16_t)width,
                                 .height = (uint16_t)height,
                                 .anchor_x = (int16_t)anchor_x,
                                 .anchor_y = (int16_t)anchor_y};
    *offset += (size_t)width * height * 4u;
}
static uint32_t hud_color(uint16_t value)
{
    const uint32_t r = value & 31u, g = (value >> 5u) & 31u, b = (value >> 10u) & 31u;
    return UINT32_C(0xff000000) | (((r << 3u) | (r >> 2u)) << 16u) |
           (((g << 3u) | (g >> 2u)) << 8u) | ((b << 3u) | (b >> 2u));
}
static int icon(const PortableHudAssetView *hud, uint16_t item, uint32_t pixels[ICON_PIXELS])
{
    PortableHudGpItemGraphic graphic;
    if (!portable_hud_gp_item_graphic(hud, item, &graphic))
        return 0;
    for (unsigned y = 0; y < 16u; ++y)
        for (unsigned x = 0; x < 16u; ++x)
        {
            const uint16_t cell = hud->item_cells[(y / 8u + 1u) * 4u + x / 8u + 1u];
            const size_t tile = cell & 1023u;
            const unsigned sx = (cell & 0x400u) ? 7u - (x & 7u) : (x & 7u),
                           sy = (cell & 0x800u) ? 7u - (y & 7u) : (y & 7u);
            const uint8_t *source;
            if (tile >= 0x70u && tile <= 0x73u)
                source =
                    hud->item_tiles + (size_t)graphic.physical_page * 128u + (tile - 0x70u) * 32u;
            else
            {
                if (tile * 32u + 32u > hud->tile_bytes)
                    return 0;
                source = hud->tiles + tile * 32u;
            }
            const unsigned color = (source[sy * 4u + sx / 2u] >> ((sx & 1u) * 4u)) & 15u;
            const unsigned bank = graphic.palette_selector >> 12u;
            const size_t palette = bank >= 10u ? (bank - 10u) * 16u + color : 0u;
            if (palette >= hud->palette_count)
                return 0;
            pixels[y * 16u + x] = color ? hud_color(hud->palette[palette]) : 0u;
        }
    return 1;
}
static int bake_actor(const PortableRaceParticleAssetView *effect, unsigned kind,
                      unsigned orientation, uint32_t pixels[BOX_PIXELS], int *anchor_y)
{
    *anchor_y = 13;
    if (kind < 3u)
    {
        const uint8_t value =
            effect->cells[PORTABLE_DRIFT_SPARK_CELL_BYTES + (kind * 6u + orientation) * 8u + 2u];
        const int offset = value < 128u ? value : (int)value - 256;
        *anchor_y = -offset;
    }
    for (unsigned i = 0; i < ICON_PIXELS; ++i)
        pixels[i] = UNTOUCHED;
    PortableSoftwareFrame frame;
    if (!portable_software_frame_init(&frame, pixels, 16, 16))
        return 0;
    int ok =
        kind == 0u
            ? portable_green_shell_render(effect, &frame, 8, *anchor_y, (uint8_t)orientation, 256)
        : kind == 1u
            ? portable_red_shell_render(effect, &frame, 8, *anchor_y, (uint8_t)orientation, 256)
        : kind == 2u
            ? portable_spiny_shell_render(effect, &frame, 8, *anchor_y, (uint8_t)orientation, 256)
            : portable_banana_render(effect, &frame, 8, *anchor_y, 256);
    /* Existing native samplers write RGB, including opaque palette black.
     * A sentinel distinguishes untouched transparent texels from RGB zero. */
    for (unsigned i = 0; i < ICON_PIXELS; ++i)
        pixels[i] = pixels[i] == UNTOUCHED ? 0u : pixels[i] | UINT32_C(0xff000000);
    return ok;
}
int portable_mesh_item_art_open(const PortableAssetPackView *pack, PortableMeshItemArt **owner,
                                char *error, size_t capacity)
{
    if (!pack || !owner)
        return fail(error, capacity, "invalid owner/pack");
    PortableAssetSectionView hs, bs, es;
    if (portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_HUD_GRAPHICS, 1, &hs) !=
            PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS, 1, &bs) !=
            PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 1,
                                         &es) != PORTABLE_ASSET_PACK_OK)
        return fail(error, capacity, "missing global HUD/box/effect graphics ID1");
    DecodeStorage *storage = calloc(1, sizeof *storage);
    PortableMeshItemArt *next = calloc(1, sizeof *next);
    if (!storage || !next)
    {
        free(storage);
        free(next);
        return fail(error, capacity, "allocation failed");
    }
    next->pixels = calloc(1, OWNED_BYTES);
    if (!next->pixels)
    {
        free(storage);
        portable_mesh_item_art_close(next);
        return fail(error, capacity, "image allocation failed");
    }
    const PortableHudAssetBuffers buffers = {storage->tiles,     sizeof storage->tiles,
                                             storage->palette,   96,
                                             storage->digits,    20,
                                             storage->clock,     60,
                                             storage->left,      6,
                                             storage->slash,     2,
                                             storage->right,     6,
                                             storage->items,     sizeof storage->items,
                                             storage->cells,     16,
                                             storage->selectors, 17};
    PortableHudAssetView hud = {0};
    PortableItemBoxAssetView box = {0};
    PortableRaceParticleAssetView effect = {0};
    int ok =
        portable_hud_asset_open(&hs, &buffers, &hud) &&
        portable_item_box_asset_open(&bs, storage->box_tiles, sizeof storage->box_tiles,
                                     storage->box_palette, 16, &box) &&
        portable_race_particle_asset_open(&es, &storage->effects, PORTABLE_DRIFT_SPARK_FRAME_COUNT,
                                          PORTABLE_DRIFT_SPARK_CELL_BYTES, &effect);
    if (!ok || hud.item_tile_bytes != PORTABLE_HUD_ITEM_TILE_BYTES || hud.item_cell_count != 16u ||
        hud.item_palette_count != 17u || effect.cell_bytes != 304u)
        goto invalid;
    uint32_t pixels[BOX_PIXELS];
    size_t offset = 0;
    for (unsigned i = 0; i < 16u; ++i)
    {
        memset(pixels, 0, sizeof pixels);
        if (i != 6u && i != 13u && !icon(&hud, (uint16_t)(0x1000u + i), pixels))
            goto invalid;
        image(next, &offset, &next->hud[i], 16, 16, 8, 8, pixels);
    }
    for (unsigned tick = 0; tick < 128u; ++tick)
    {
        for (unsigned i = 0; i < BOX_PIXELS; ++i)
            pixels[i] = UNTOUCHED;
        PortableSoftwareFrame frame;
        if (!portable_software_frame_init(&frame, pixels, 32, 32) ||
            !portable_item_box_render(&box, &frame, 16, 30, tick))
            goto invalid;
        for (unsigned i = 0; i < BOX_PIXELS; ++i)
            pixels[i] = pixels[i] == UNTOUCHED ? 0u : pixels[i] | UINT32_C(0xff000000);
        image(next, &offset, &next->box[tick], 32, 32, 16, 30, pixels);
    }
    for (unsigned kind = 0; kind < 3u; ++kind)
        for (unsigned direction = 0; direction < 6u; ++direction)
        {
            int anchor;
            if (!bake_actor(&effect, kind, direction, pixels, &anchor))
                goto invalid;
            image(next, &offset, &next->shell[kind][direction], 16, 16, 8, anchor, pixels);
        }
    int anchor;
    if (!bake_actor(&effect, 3, 0, pixels, &anchor))
        goto invalid;
    image(next, &offset, &next->banana, 16, 16, 8, anchor, pixels);
    free(storage);
    portable_mesh_item_art_close(*owner);
    *owner = next;
    if (error && capacity)
        error[0] = '\0';
    return 1;
invalid:
    free(storage);
    portable_mesh_item_art_close(next);
    return fail(error, capacity, "invalid HUD/box/effect payload or source cell bounds");
}
const PortableNativeImage *portable_mesh_item_art_hud(const PortableMeshItemArt *owner,
                                                      uint16_t item)
{
    if (!owner || item < 0x1000u || item > 0x100fu || item == 0x1006u || item == 0x100du)
        return NULL;
    return &owner->hud[item - 0x1000u];
}
const PortableNativeImage *portable_mesh_item_art_box(const PortableMeshItemArt *owner,
                                                      uint32_t tick)
{
    return owner ? &owner->box[tick & 127u] : NULL;
}
const PortableNativeImage *portable_mesh_item_art_actor(const PortableMeshItemArt *owner,
                                                        uint16_t item, unsigned direction)
{
    if (!owner)
        return NULL;
    if (item == 0x100bu)
        return direction == 0 ? portable_mesh_item_art_hud(owner, item) : NULL;
    if (item == 0x1005u)
        return direction == 0 ? &owner->banana : NULL;
    if (direction >= 6u)
        return NULL;
    if (item == 0x1000u || item == 0x1001u)
        return &owner->shell[0][direction];
    if (item == 0x1002u || item == 0x1003u)
        return &owner->shell[1][direction];
    return item == 0x1004u ? &owner->shell[2][direction] : NULL;
}
