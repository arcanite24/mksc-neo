#include "mesh_battle_art.h"
#include "character_asset.h"
#include "driver_projection_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BOMB_FRAMES = 16,
    CELL = 64,
    BOMB_PIXEL_BYTES = BOMB_FRAMES * CELL * CELL * 4
};
typedef struct
{
    PortableRenderVertex vertices[2][4];
    PortableRenderMaterial material;
    PortableRenderRun run;
    PortableRenderMesh meshes[2];
} BattleCell;
struct PortableMeshBattleArt
{
    BattleCell bombs[BOMB_FRAMES], balloon;
    uint8_t slots[64];
    uint8_t *pixels;
};
static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

static int fail(char *error, size_t capacity, const char *state)
{
    if (error && capacity)
        snprintf(error, capacity, "3D Battle artwork: %s", state);
    return 0;
}
static void pixel(uint8_t *out, uint32_t color)
{
    for (unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(color >> (i * 8));
}
static uint32_t color(uint16_t rgb)
{
    const uint32_t r = rgb & 31u, g = (rgb >> 5u) & 31u, b = (rgb >> 10u) & 31u;
    return UINT32_C(0xff000000) | (((r << 3u) | (r >> 2u)) << 16u) |
           (((g << 3u) | (g >> 2u)) << 8u) | ((b << 3u) | (b >> 2u));
}
static void cell(BattleCell *out, uint8_t *pixels, unsigned width, unsigned height, int x, int y)
{
    const double left = x * .05, right = (x + (int)width) * .05;
    const double top = -y * .05, bottom = -(y + (int)height) * .05;
    out->material = (PortableRenderMaterial){.argb = UINT32_MAX,
                                             .flags = 4u | 16u,
                                             .alpha_cutoff_q16 = 1,
                                             .texture = {.pixels = pixels,
                                                         .width = (uint16_t)width,
                                                         .height = (uint16_t)height,
                                                         .anchor_x = (int16_t)x,
                                                         .anchor_y = (int16_t)y}};
    out->run = (PortableRenderRun){0, 6, 0};
    for (unsigned mirror = 0; mirror < 2; ++mirror)
    {
        const double u0 = mirror ? 1 : 0, u1 = mirror ? 0 : 1;
        out->vertices[mirror][0] =
            (PortableRenderVertex){.position = {left, 0, bottom}, .uv = {u0, 1}};
        out->vertices[mirror][1] =
            (PortableRenderVertex){.position = {right, 0, bottom}, .uv = {u1, 1}};
        out->vertices[mirror][2] =
            (PortableRenderVertex){.position = {right, 0, top}, .uv = {u1, 0}};
        out->vertices[mirror][3] =
            (PortableRenderVertex){.position = {left, 0, top}, .uv = {u0, 0}};
        out->meshes[mirror] = (PortableRenderMesh){.vertices = out->vertices[mirror],
                                                   .indices = indices,
                                                   .materials = &out->material,
                                                   .runs = &out->run,
                                                   .vertex_count = 4,
                                                   .index_count = 6,
                                                   .material_count = 1,
                                                   .run_count = 1};
    }
}

void portable_mesh_battle_art_close(PortableMeshBattleArt *owner)
{
    if (owner)
    {
        free(owner->pixels);
        free(owner);
    }
}

int portable_mesh_battle_art_open(const PortableAssetPackView *pack, PortableMeshBattleArt **owner,
                                  char *error, size_t capacity)
{
    PortableAssetSectionView graphic, image, projection;
    PortableDriverProjectionRules feet;
    PortableNativeImage balloon;
    if (!pack || !owner)
        return fail(error, capacity, "invalid owner/pack");
    if (portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS, 9,
                                         &graphic) != PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 4,
                                         &image) != PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES, 1,
                                         &projection) != PORTABLE_ASSET_PACK_OK)
        return fail(error, capacity, "missing global Bomb/balloon/DPR1");
    if (!portable_driver_projection_asset_open(&projection, &feet) || image.version != 1 ||
        !portable_native_image_open(image.bytes, image.size, &balloon) || balloon.width > 64 ||
        balloon.height > 64 || balloon.anchor_x < -64 || balloon.anchor_x > 64 ||
        balloon.anchor_y < -64 || balloon.anchor_y > 64)
        return fail(error, capacity, "invalid balloon/DPR1 payload");
    uint16_t palette[256];
    PortableCharacterAssetView bomb;
    if (portable_character_asset_open_borrowed(&graphic, palette, 256, &bomb) !=
            PORTABLE_CHARACTER_ASSET_OK ||
        bomb.width != CELL || bomb.height != CELL || bomb.direction_frame_count != BOMB_FRAMES ||
        !bomb.view_frame_slots || bomb.view_frame_slot_count != 64)
        return fail(error, capacity, "invalid Bomb v4 sixteen-frame payload");
    for (unsigned i = 0; i < 64; ++i)
        if ((bomb.view_frame_slots[i] & 1u) || bomb.view_frame_slots[i] >= BOMB_FRAMES - 1u)
            return fail(error, capacity, "Bomb direction slot must be even 0..14");
    PortableMeshBattleArt *next = calloc(1, sizeof *next);
    if (!next)
        return fail(error, capacity, "allocation failed");
    next->pixels = malloc(BOMB_PIXEL_BYTES + (size_t)balloon.width * balloon.height * 4u);
    if (!next->pixels)
    {
        portable_mesh_battle_art_close(next);
        return fail(error, capacity, "pixel allocation failed");
    }
    memcpy(next->slots, bomb.view_frame_slots, sizeof next->slots);
    for (unsigned frame = 0; frame < BOMB_FRAMES; ++frame)
    {
        const uint8_t *tiles = bomb.direction_tiles + frame * PORTABLE_CHARACTER_FRAME_BYTES;
        uint8_t *pixels = next->pixels + frame * CELL * CELL * 4u;
        for (unsigned y = 0; y < CELL; ++y)
            for (unsigned x = 0; x < CELL; ++x)
            {
                const size_t offset =
                    ((y / 8u) * 8u + x / 8u) * 32u + (y & 7u) * 4u + (x & 7u) / 2u;
                const unsigned index = (tiles[offset] >> ((x & 1u) * 4u)) & 15u;
                pixel(pixels + ((size_t)y * CELL + x) * 4u, index ? color(palette[index]) : 0);
            }
        cell(&next->bombs[frame], pixels, CELL, CELL, feet.rows[0].offset_x, feet.rows[0].offset_y);
    }
    uint8_t *pixels = next->pixels + BOMB_PIXEL_BYTES;
    for (unsigned y = 0; y < balloon.height; ++y)
        for (unsigned x = 0; x < balloon.width; ++x)
            pixel(pixels + ((size_t)y * balloon.width + x) * 4u,
                  portable_native_image_pixel(&balloon, x, y));
    cell(&next->balloon, pixels, balloon.width, balloon.height, balloon.anchor_x, balloon.anchor_y);
    portable_mesh_battle_art_close(*owner);
    *owner = next;
    return 1;
}

const PortableRenderMesh *portable_mesh_battle_art_bomb_view(const PortableMeshBattleArt *owner,
                                                             uint8_t angle, unsigned walk_frame)
{
    if (!owner || angle > 127 || walk_frame > 1)
        return NULL;
    const unsigned index = angle > 63 ? 127u - angle : angle;
    return &owner->bombs[owner->slots[index] + walk_frame].meshes[angle > 63];
}
const PortableRenderMesh *portable_mesh_battle_art_balloon(const PortableMeshBattleArt *owner)
{
    return owner ? &owner->balloon.meshes[0] : NULL;
}
