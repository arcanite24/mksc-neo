#include "mesh_battle_art.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh Battle art line %d: %s\n", __LINE__, #c);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
enum
{
    BOMB_BYTES = 32 + 64 + 16 * (2048 + 512 + 128) + 512,
    BALLOON_BYTES = 32 + 6 + 4
};
typedef struct
{
    uint8_t bomb[BOMB_BYTES], balloon[BALLOON_BYTES], projection[44];
    PortableAssetSectionView sections[3];
    PortableAssetPackView pack;
} Fixture;
static void word(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8));
}
static void half(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}
static void fixture(Fixture *f)
{
    memset(f, 0, sizeof *f);
    const uint32_t header[8] = {0x33524143u, 16 * 2048, 256, 64, 64, 8, 16, 64};
    for (unsigned i = 0; i < 8; ++i)
        word(f->bomb + i * 4, header[i]);
    for (unsigned i = 0; i < 64; ++i)
        f->bomb[32 + i] = (uint8_t)((i % 8) * 2);
    f->bomb[32] = 14;
    memset(f->bomb + 96, 0x22, 16 * 2048);
    memset(f->bomb + 96 + 2048, 0x33, 2048);
    memset(f->bomb + 96 + 14 * 2048, 0x44, 2048);
    memset(f->bomb + 96 + 15 * 2048, 0x55, 2048);
    f->bomb[96 + 14 * 2048] = 0x10;
    uint8_t *palette = f->bomb + BOMB_BYTES - 512;
    half(palette, 0x7fff);
    half(palette + 4, 31);
    half(palette + 6, 31u << 5u);
    half(palette + 8, 31u << 10u);
    half(palette + 10, 31u | (31u << 5u));
    /* Indexed native source cell, with nonzero palette base and transparent255. */
    memcpy(f->balloon, "MKSCIMG2", 8);
    half(f->balloon + 8, 32);
    half(f->balloon + 10, 2);
    half(f->balloon + 12, 3);
    half(f->balloon + 14, 2);
    half(f->balloon + 16, (uint16_t)(int16_t)-2);
    half(f->balloon + 18, (uint16_t)(int16_t)-5);
    half(f->balloon + 20, 2);
    half(f->balloon + 22, 96);
    word(f->balloon + 24, 32);
    word(f->balloon + 28, BALLOON_BYTES);
    const uint8_t pixels[6] = {96, 97, 255, 97, 255, 96};
    memcpy(f->balloon + 32, pixels, sizeof pixels);
    half(f->balloon + BALLOON_BYTES - 2, 31u << 5u);
    word(f->projection, 0x31525044u);
    word(f->projection + 4, 3);
    for (unsigned i = 0; i < 3; ++i)
    {
        uint8_t *row = f->projection + 8 + i * 12;
        half(row, (uint16_t)(511 + i * 512));
        half(row + 2, (uint16_t)(64u >> i));
        half(row + 4, (uint16_t)(int16_t)-24);
        half(row + 6, (uint16_t)(int16_t)-55);
        row[8] = 1;
    }
    f->sections[0] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,
                                                .content_id = 9,
                                                .version = 4,
                                                .bytes = f->bomb,
                                                .size = BOMB_BYTES};
    f->sections[1] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,
                                                .content_id = 4,
                                                .version = 1,
                                                .bytes = f->balloon,
                                                .size = BALLOON_BYTES};
    f->sections[2] =
        (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES,
                                   .content_id = 1,
                                   .version = 1,
                                   .bytes = f->projection,
                                   .size = 44};
    f->pack = (PortableAssetPackView){.resolved_sections = f->sections, .section_count = 3};
}

static int source_pixels_and_frames(void)
{
    Fixture f;
    fixture(&f);
    PortableMeshBattleArt *owner = NULL;
    /* No character manifest, motion, or 2D course exists in this pack. */
    CHECK(portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0));
    const PortableRenderMesh *bomb = portable_mesh_battle_art_bomb_view(owner, 0, 0);
    const PortableRenderMesh *step = portable_mesh_battle_art_bomb_view(owner, 0, 1);
    const PortableRenderMesh *mirror = portable_mesh_battle_art_bomb_view(owner, 127, 1);
    CHECK(bomb && step && mirror && bomb != step && step != mirror);
    CHECK(portable_native_image_pixel(&bomb->materials[0].texture, 0, 0) == 0);
    CHECK(portable_native_image_pixel(&bomb->materials[0].texture, 1, 0) == UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(&bomb->materials[0].texture, 2, 0) == UINT32_C(0xff0000ff));
    CHECK(portable_native_image_pixel(&step->materials[0].texture, 0, 0) == UINT32_C(0xffffff00));
    CHECK(step->materials == mirror->materials && step->vertices[0].uv[0] == 0 &&
          mirror->vertices[0].uv[0] == 1);
    CHECK(portable_native_image_pixel(
              &portable_mesh_battle_art_bomb_view(owner, 1, 0)->materials[0].texture, 0, 0) ==
          UINT32_C(0xffff0000));
    CHECK(fabs(bomb->vertices[0].position[0] + 1.2) < 1e-12 &&
          fabs(bomb->vertices[0].position[2] + .45) < 1e-12 &&
          fabs(bomb->vertices[2].position[2] - 2.75) < 1e-12);
    CHECK(bomb->materials[0].flags == (4u | 16u) && bomb->materials[0].alpha_cutoff_q16 == 1);
    CHECK(!portable_mesh_battle_art_bomb_view(owner, 128, 0));
    CHECK(!portable_mesh_battle_art_bomb_view(owner, 0, 2));
    CHECK(!portable_mesh_battle_art_bomb_view(NULL, 0, 0));
    const PortableRenderMesh *balloon = portable_mesh_battle_art_balloon(owner);
    CHECK(balloon && !portable_mesh_battle_art_balloon(NULL));
    const PortableNativeImage *image = &balloon->materials[0].texture;
    CHECK(image->width == 3 && image->height == 2 && image->anchor_x == -2 &&
          image->anchor_y == -5);
    CHECK(portable_native_image_pixel(image, 0, 0) == UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(image, 1, 0) == UINT32_C(0xff00ff00));
    CHECK(portable_native_image_pixel(image, 2, 0) == 0);
    CHECK(fabs(balloon->vertices[0].position[0] + .10) < 1e-12 &&
          fabs(balloon->vertices[2].position[0] - .05) < 1e-12 &&
          fabs(balloon->vertices[0].position[2] - .15) < 1e-12 &&
          fabs(balloon->vertices[2].position[2] - .25) < 1e-12);
    const PortableRenderMesh before = *bomb;
    memset(f.bomb, 0, sizeof f.bomb);
    memset(f.balloon, 0, sizeof f.balloon);
    memset(f.projection, 0, sizeof f.projection);
    CHECK(!memcmp(&before, bomb, sizeof before));
    CHECK(portable_mesh_battle_art_bomb_view(owner, 0, 0) == bomb);
    CHECK(portable_native_image_pixel(&step->materials[0].texture, 0, 0) == UINT32_C(0xffffff00));
    CHECK(portable_native_image_pixel(image, 1, 0) == UINT32_C(0xff00ff00));
    portable_mesh_battle_art_close(owner);
    portable_mesh_battle_art_close(NULL);
    return 0;
}

static int malformed_preserves_owner(void)
{
    Fixture f;
    fixture(&f);
    PortableMeshBattleArt *owner = NULL;
    char error[128];
    CHECK(portable_mesh_battle_art_open(&f.pack, &owner, error, sizeof error));
    PortableMeshBattleArt *saved = owner;
    const PortableRenderMesh *mesh = portable_mesh_battle_art_bomb_view(owner, 0, 1);
    f.bomb[32] = 15;
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, error, sizeof error) && owner == saved &&
          strstr(error, "even"));
    f.bomb[32] = 14;
    f.sections[0].version = 3;
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    f.sections[0].version = 4;
    f.sections[0].size--;
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    f.sections[0].size++;
    word(f.bomb + 24, 14);
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    word(f.bomb + 24, 16);
    f.balloon[32] = 95;
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    f.balloon[32] = 96;
    half(f.balloon + 16, 65);
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    half(f.balloon + 16, (uint16_t)(int16_t)-2);
    f.projection[16] = 2;
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    f.projection[16] = 1;
    f.sections[1].content_id = 5;
    CHECK(!portable_mesh_battle_art_open(&f.pack, &owner, NULL, 0) && owner == saved);
    CHECK(!portable_mesh_battle_art_open(NULL, &owner, NULL, 0) && owner == saved);
    CHECK(!portable_mesh_battle_art_open(&f.pack, NULL, NULL, 0));
    CHECK(portable_mesh_battle_art_bomb_view(owner, 0, 1) == mesh &&
          portable_native_image_pixel(&mesh->materials[0].texture, 0, 0) == UINT32_C(0xffffff00));
    portable_mesh_battle_art_close(owner);
    return 0;
}

int main(void)
{
    CHECK(!source_pixels_and_frames() && !malformed_preserves_owner());
    puts("Mesh Battle art: PASS (global roles, source pixels, walking mirrors, anchors, rollback)");
    return 0;
}
