#include "mesh_driver_art.h"
#include "character_asset.h"
#include "driver_projection_asset.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh driver art line %d: %s\n", __LINE__, #c);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
enum
{
    GRAPHIC_BYTES = 32 + 64 + 3 * 2048 + 32
};
typedef struct
{
    uint8_t manifest[24], other[24], graphic[GRAPHIC_BYTES], projection[44], motion;
    PortableAssetSectionView sections[6];
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
    const uint32_t manifest[6] = {0x31524843u, 17, 1, 91, 1, 2};
    const uint32_t graphic[8] = {0x32524143u, 3 * 2048, 16, 64, 64, 999, 3, 64};
    for (unsigned i = 0; i < 6; ++i)
        word(f->manifest + i * 4, manifest[i]);
    memcpy(f->other, f->manifest, sizeof f->other);
    word(f->other + 4, 18);
    for (unsigned i = 0; i < 8; ++i)
        word(f->graphic + i * 4, graphic[i]);
    for (unsigned i = 0; i < 64; ++i)
        f->graphic[32 + i] = (uint8_t)(i % 3);
    f->graphic[32] = 2;
    memset(f->graphic + 96, 0x22, 2048);
    memset(f->graphic + 96 + 2048, 0x33, 2048);
    memset(f->graphic + 96 + 4096, 0x44, 2048);
    f->graphic[96] = 0x10;
    f->graphic[96 + 9 * 32] = 0x43;
    uint8_t *palette = f->graphic + GRAPHIC_BYTES - 32;
    half(palette, 0x7fff); /* Palette0 white must nevertheless be transparent. */
    half(palette + 2, 0);  /* Color1 is opaque black. */
    half(palette + 4, 31);
    half(palette + 6, 31u << 5u);
    half(palette + 8, 31u << 10u);
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
    f->sections[0] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST,
                                                .content_id = 17,
                                                .version = 1,
                                                .bytes = f->manifest,
                                                .size = 24};
    f->sections[1] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST,
                                                .content_id = 18,
                                                .version = 1,
                                                .bytes = f->other,
                                                .size = 24};
    f->sections[2] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,
                                                .content_id = 91,
                                                .version = 4,
                                                .bytes = f->graphic,
                                                .size = GRAPHIC_BYTES};
    f->sections[3] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_MOTION_TABLES,
                                                .content_id = 1,
                                                .version = 1,
                                                .bytes = &f->motion,
                                                .size = 1};
    f->sections[4] =
        (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES,
                                   .content_id = 1,
                                   .version = 1,
                                   .bytes = f->projection,
                                   .size = 44};
    f->pack = (PortableAssetPackView){.resolved_sections = f->sections, .section_count = 5};
}

static int decoded_cells(void)
{
    Fixture f;
    fixture(&f);
    PortableMeshDriverArt *owner = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    const PortableRenderMesh *red = portable_mesh_driver_art_frame(owner, 0, 0);
    const PortableRenderMesh *mirror = portable_mesh_driver_art_frame(owner, 0, 1);
    const PortableRenderMesh *blue = portable_mesh_driver_art_view(owner, 0);
    CHECK(red && mirror && blue && red->vertex_count == 4 && red->index_count == 6);
    CHECK(red != mirror && red->materials == mirror->materials);
    const PortableNativeImage *image = &red->materials[0].texture;
    CHECK(portable_native_image_pixel(image, 0, 0) == 0);
    CHECK(portable_native_image_pixel(image, 1, 0) == UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(image, 2, 0) == UINT32_C(0xffff0000));
    CHECK(portable_native_image_pixel(image, 8, 8) == UINT32_C(0xff00ff00));
    CHECK(portable_native_image_pixel(image, 9, 8) == UINT32_C(0xff0000ff));
    CHECK(portable_native_image_pixel(&blue->materials[0].texture, 0, 0) == UINT32_C(0xff0000ff));
    CHECK(
        portable_native_image_pixel(&portable_mesh_driver_art_view(owner, 1)->materials[0].texture,
                                    0, 0) == UINT32_C(0xff00ff00));
    CHECK(portable_mesh_driver_art_view(owner, 127) == portable_mesh_driver_art_frame(owner, 2, 1));
    CHECK(red->vertices[0].uv[0] == 0 && mirror->vertices[0].uv[0] == 1);
    CHECK(red->vertices[2].uv[0] == 1 && mirror->vertices[2].uv[0] == 0);
    CHECK(fabs(red->vertices[0].position[0] + 1.2) < 1e-12);
    CHECK(fabs(red->vertices[1].position[0] - 2.0) < 1e-12);
    CHECK(fabs(red->vertices[0].position[2] + .45) < 1e-12);
    CHECK(fabs(red->vertices[2].position[2] - 2.75) < 1e-12);
    CHECK(red->materials[0].flags == (4u | 16u) && red->materials[0].alpha_cutoff_q16 == 1);
    CHECK(!portable_mesh_driver_art_frame(owner, 3, 0));
    CHECK(!portable_mesh_driver_art_frame(owner, 0, -1));
    CHECK(!portable_mesh_driver_art_frame(owner, 0, 2));
    CHECK(!portable_mesh_driver_art_view(owner, 128) && !portable_mesh_driver_art_view(NULL, 0));
    /* Graphics payload's donor999 and manifest class2 never replace identity17.
     * The cache owns bytes even after the source is changed or released. */
    memset(f.graphic, 0, sizeof f.graphic);
    memset(f.projection, 0, sizeof f.projection);
    CHECK(portable_native_image_pixel(image, 2, 0) == UINT32_C(0xffff0000));
    CHECK(portable_mesh_driver_art_frame(owner, 0, 0) == red);
    PortableMeshDriverArt *saved = owner;
    CHECK(!portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0) && owner == saved);
    CHECK(portable_native_image_pixel(image, 1, 0) == UINT32_C(0xff000000));
    portable_mesh_driver_art_close(owner);
    return 0;
}

static void frame_pixel(uint8_t *tiles, unsigned x, unsigned y, unsigned color)
{
    const size_t offset = ((y / 8u) * 8u + x / 8u) * 32u + (y & 7u) * 4u + (x & 7u) / 2u;
    const unsigned shift = (x & 1u) * 4u;
    tiles[offset] = (uint8_t)((tiles[offset] & ~(15u << shift)) | (color << shift));
}

static int opaque_ground_edges(void)
{
    Fixture f;
    fixture(&f);
    memset(f.graphic + 96, 0, 3 * PORTABLE_CHARACTER_FRAME_BYTES);
    /* Opaque black is support, palette0 white is not. Each cell retains its
     * complete64x64 DPR quad despite different transparent bottom padding. */
    frame_pixel(f.graphic + 96, 7, 41, 1);
    frame_pixel(f.graphic + 96 + PORTABLE_CHARACTER_FRAME_BYTES, 11, 58, 3);
    PortableMeshDriverArt *owner = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    const PortableRenderMesh *mesh = portable_mesh_driver_art_frame(owner, 0, 0);
    const PortableRenderMesh *mirror = portable_mesh_driver_art_frame(owner, 0, 1);
    PortableRenderVertex vertices[4], mirrored_vertices[4];
    uint8_t pixels[64 * 64 * 4];
    memcpy(vertices, mesh->vertices, sizeof vertices);
    memcpy(mirrored_vertices, mirror->vertices, sizeof mirrored_vertices);
    memcpy(pixels, mesh->materials[0].texture.pixels, sizeof pixels);
    const double expected[3] = {.65, -.2, 0};
    for (unsigned angle = 0; angle < 128; ++angle)
    {
        const unsigned index = angle > 63 ? 127u - angle : angle;
        const unsigned frame = f.graphic[32 + index];
        double z = 19;
        CHECK(portable_mesh_driver_art_ground_anchor(owner, (uint8_t)angle, &z));
        CHECK(fabs(z - expected[frame]) < 1e-12);
        const PortableRenderMesh *view = portable_mesh_driver_art_view(owner, (uint8_t)angle);
        CHECK(view == portable_mesh_driver_art_frame(owner, frame, angle > 63));
        CHECK(fabs(view->vertices[0].position[2] + .45) < 1e-12);
        CHECK(fabs(view->vertices[2].position[2] - 2.75) < 1e-12);
    }
    CHECK(!memcmp(vertices, mesh->vertices, sizeof vertices));
    CHECK(!memcmp(mirrored_vertices, mirror->vertices, sizeof mirrored_vertices));
    CHECK(!memcmp(pixels, mesh->materials[0].texture.pixels, sizeof pixels));
    CHECK(portable_native_image_pixel(&mesh->materials[0].texture, 7, 41) == UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(&mesh->materials[0].texture, 7, 42) == 0);
    double z = 17.25;
    CHECK(!portable_mesh_driver_art_ground_anchor(NULL, 0, &z) && z == 17.25);
    CHECK(!portable_mesh_driver_art_ground_anchor(owner, 128, &z) && z == 17.25);
    CHECK(!portable_mesh_driver_art_ground_anchor(owner, 255, &z) && z == 17.25);
    CHECK(!portable_mesh_driver_art_ground_anchor(owner, 0, NULL));
    /* Results remain owned/cached after source bytes cease to exist. */
    memset(f.graphic, 0, sizeof f.graphic);
    memset(f.projection, 0, sizeof f.projection);
    CHECK(portable_mesh_driver_art_ground_anchor(owner, 3, &z) && fabs(z - .65) < 1e-12);
    CHECK(!memcmp(pixels, mesh->materials[0].texture.pixels, sizeof pixels));
    portable_mesh_driver_art_close(owner);
    return 0;
}

static int imported_effect_palettes(void)
{
    Fixture f;
    fixture(&f);
    enum { TILES = PORTABLE_CHARACTER_FULL_FRAME_COUNT * PORTABLE_CHARACTER_FRAME_BYTES,
           BYTES = 32 + 64 + TILES + 16 * 16 * 2 };
    uint8_t *graphic = calloc(1, BYTES);
    CHECK(graphic);
    memcpy(graphic, f.graphic, 96 + 3 * 2048);
    word(graphic + 4, TILES);
    word(graphic + 8, 256);
    word(graphic + 24, PORTABLE_CHARACTER_FULL_FRAME_COUNT);
    uint8_t *palette = graphic + 96 + TILES;
    memcpy(palette, f.graphic + GRAPHIC_BYTES - 32, 32);
    /* Burn banks replace body colors3..15, retaining base wheels0..2. */
    half(palette + (5 * 16 + 1) * 2, 0x7fff);
    half(palette + (5 * 16 + 2) * 2, 0x7fff);
    half(palette + (5 * 16 + 3) * 2, 31u << 10);
    half(palette + (6 * 16 + 3) * 2, 0x7fff);
    /* Every body index has a distinct authored RGB555 bank7 value. Its wheel
     * entries deliberately disagree with base: the shared composer must ignore them. */
    for (unsigned color = 0; color < 16; ++color)
        half(palette + (7 * 16 + color) * 2,
             (uint16_t)(color | ((31u-color) << 5u) | (color << 10u)));
    for (unsigned x = 0; x < 13; ++x)
    {
        const size_t offset = 96 + ((16u / 8u) * 8u + x / 8u) * 32u + (x & 7u) / 2u;
        const unsigned shift = (x & 1u) * 4u;
        graphic[offset] = (uint8_t)((graphic[offset] & ~(15u << shift)) | ((x + 3u) << shift));
    }
    f.sections[2].bytes = graphic;
    f.sections[2].size = BYTES;
    f.sections[2].version = 3;
    PortableMeshDriverArt *owner = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    const PortableRenderMesh *base = portable_mesh_driver_art_view(owner, 1);
    const PortableRenderMesh *dark = portable_mesh_driver_art_palette_view(owner, 1, 5);
    const PortableRenderMesh *restore = portable_mesh_driver_art_palette_view(owner, 1, 6);
    const PortableRenderMesh *boo = portable_mesh_driver_art_palette_view(owner, 1, 7);
    CHECK(base && dark && restore && boo && dark != base && restore != dark && boo != restore);
    CHECK(portable_native_image_pixel(&base->materials[0].texture, 0, 0) == UINT32_C(0xff00ff00));
    CHECK(portable_native_image_pixel(&dark->materials[0].texture, 0, 0) == UINT32_C(0xff0000ff));
    CHECK(portable_native_image_pixel(&restore->materials[0].texture, 0, 0) == UINT32_C(0xffffffff));
    const PortableRenderMesh *rear = portable_mesh_driver_art_palette_view(owner, 3, 5);
    CHECK(rear && portable_native_image_pixel(&rear->materials[0].texture, 0, 0) == 0);
    CHECK(portable_native_image_pixel(&rear->materials[0].texture, 1, 0) == UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(&rear->materials[0].texture, 2, 0) == UINT32_C(0xffff0000));
    CHECK(portable_mesh_driver_art_palette_view(owner, 1, 0) == base);
    const PortableRenderMesh *mirror = portable_mesh_driver_art_palette_view(owner, 126, 5);
    CHECK(mirror && mirror->materials == dark->materials && mirror->vertices[0].uv[0] == 1);
    CHECK(!portable_mesh_driver_art_palette_view(owner, 128, 5));
    CHECK(!portable_mesh_driver_art_palette_view(owner, 1, 4));
    CHECK(!portable_mesh_driver_art_palette_view(NULL, 1, 5));
    CHECK(!portable_mesh_driver_art_palette_view(owner, 1, 8));
    CHECK(!portable_mesh_driver_art_palette_view(owner, 128, 7));
    const PortableRenderMesh *boo_rear = portable_mesh_driver_art_palette_view(owner, 3, 7);
    CHECK(boo_rear && portable_native_image_pixel(&boo_rear->materials[0].texture, 0, 0) == 0);
    CHECK(portable_native_image_pixel(&boo_rear->materials[0].texture, 1, 0) == UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(&boo_rear->materials[0].texture, 2, 0) == UINT32_C(0xffff0000));
    for (unsigned color = 3; color < 16; ++color)
    {
        const unsigned r = color, g = 31u-color, b = color;
        const uint32_t expected = UINT32_C(0xff000000) | (((r << 3u) | (r >> 2u)) << 16u) |
            (((g << 3u) | (g >> 2u)) << 8u) | ((b << 3u) | (b >> 2u));
        CHECK(portable_native_image_pixel(&boo_rear->materials[0].texture, color-3u, 16) == expected);
    }
    const PortableRenderMesh *boo_mirror = portable_mesh_driver_art_palette_view(owner, 126, 7);
    CHECK(boo_mirror && boo_mirror->materials == boo->materials && boo_mirror->vertices[0].uv[0] == 1);
    /* Palette composition and horizontal mirrors never move the cached lowest
     * alpha edge, even when nonzero body indices are recolored opaque black. */
    for (unsigned angle = 0; angle < 128; ++angle)
    {
        double anchor = 99;
        CHECK(portable_mesh_driver_art_ground_anchor(owner, (uint8_t)angle, &anchor));
        CHECK(fabs(anchor + .45) < 1e-12);
        const uint8_t banks[] = {0, 5, 6, 7};
        for (unsigned bank = 0; bank < sizeof banks / sizeof banks[0]; ++bank)
        {
            const PortableRenderMesh *view =
                portable_mesh_driver_art_palette_view(owner, (uint8_t)angle, banks[bank]);
            CHECK(view && view->vertices[0].position[2] == base->vertices[0].position[2]);
            unsigned opaque = 0;
            for (unsigned x = 0; x < 64; ++x)
                opaque |= portable_native_image_pixel(&view->materials[0].texture, x, 63) >> 24;
            CHECK(opaque);
        }
    }
    /* An authored payload with banks0..6 retains byte-identical ordinary/burn
     * cells and advertises no bank7. Existing owners keep their immutable data. */
    word(graphic + 8, 7 * 16);
    f.sections[2].version = 4;
    f.sections[2].size = BYTES - 512 + 7 * 32;
    PortableMeshDriverArt *partial = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &partial, NULL, 0));
    CHECK(!portable_mesh_driver_art_palette_view(partial, 1, 7));
    for (unsigned bank = 0; bank < 3; ++bank)
    {
        const unsigned selection = bank ? bank + 4 : 0;
        const PortableNativeImage *full_image = &portable_mesh_driver_art_palette_view(owner, 1, (uint8_t)selection)->materials[0].texture;
        const PortableNativeImage *partial_image = &portable_mesh_driver_art_palette_view(partial, 1, (uint8_t)selection)->materials[0].texture;
        CHECK(!memcmp(full_image->pixels, partial_image->pixels, 64 * 64 * 4));
    }
    portable_mesh_driver_art_close(partial);
    word(graphic + 8, 256);
    f.sections[2].version = 3;f.sections[2].size = BYTES;
    memset(graphic, 0, BYTES);
    CHECK(portable_native_image_pixel(&dark->materials[0].texture, 0, 0) == UINT32_C(0xff0000ff));
    CHECK(portable_mesh_driver_art_view(owner, 1) == base &&
        portable_mesh_driver_art_palette_view(owner, 1, 7) == boo);
    CHECK(portable_native_image_pixel(&boo_rear->materials[0].texture, 0, 16) == UINT32_C(0xff18e718));
    PortableMeshDriverArt *saved = owner;
    CHECK(!portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0) && owner == saved);
    CHECK(portable_mesh_driver_art_palette_view(owner, 1, 7) == boo &&
        portable_native_image_pixel(&boo_rear->materials[0].texture, 0, 16) == UINT32_C(0xff18e718));
    portable_mesh_driver_art_close(owner);
    free(graphic);
    fixture(&f);
    owner = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    CHECK(!portable_mesh_driver_art_palette_view(owner, 1, 5) &&
        !portable_mesh_driver_art_palette_view(owner, 1, 6) &&
        !portable_mesh_driver_art_palette_view(owner, 1, 7));
    CHECK(portable_mesh_driver_art_palette_view(owner, 1, 0) == portable_mesh_driver_art_view(owner, 1));
    portable_mesh_driver_art_close(owner);
    return 0;
}

static int identity_and_rollback(void)
{
    Fixture f;
    fixture(&f);
    PortableMeshDriverArt *owner = NULL, *other = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    CHECK(portable_mesh_driver_art_open(&f.pack, 18, &other, NULL, 0));
    const PortableRenderMesh *first = portable_mesh_driver_art_view(owner, 0);
    CHECK(portable_native_image_pixel(&first->materials[0].texture, 0, 0) ==
          portable_native_image_pixel(
              &portable_mesh_driver_art_view(other, 0)->materials[0].texture, 0, 0));
    PortableMeshDriverArt *saved = owner;
    CHECK(!portable_mesh_driver_art_open(&f.pack, 999, &owner, NULL, 0) && owner == saved);
    /* A manifest's exact graphics reference is mandatory, not its class donor. */
    word(f.manifest + 12, 92);
    CHECK(!portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0) && owner == saved);
    word(f.manifest + 12, 91);
    f.graphic[32] = 3; /* Invalid authored v4 directional reference. */
    CHECK(!portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0) && owner == saved);
    f.graphic[32] = 2;
    f.sections[2].size--;
    CHECK(!portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0) && owner == saved);
    f.sections[2].size++;
    f.projection[16] = 2;
    CHECK(!portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0) && owner == saved);
    CHECK(portable_mesh_driver_art_view(owner, 0) == first);
    portable_mesh_driver_art_close(other);
    portable_mesh_driver_art_close(owner);
    return 0;
}

static int angle_selection(void)
{
    const PortableRenderPose pose = {.forward = {0, -1, 0}, .up = {0, 0, 1}};
    PortableRenderView view = {.position = {0, 5, 2}, .forward = {0, -1, 0}};
    uint8_t angle = 55;
    CHECK(portable_mesh_driver_art_angle(&pose, &view, &angle) && angle == 0);
    view.position[1] = -5;
    CHECK(portable_mesh_driver_art_angle(&pose, &view, &angle) && angle == 64);
    view.position[0] = 5;
    view.position[1] = 0;
    CHECK(portable_mesh_driver_art_angle(&pose, &view, &angle) && angle == 96);
    view.position[0] = -5;
    CHECK(portable_mesh_driver_art_angle(&pose, &view, &angle) && angle == 32);
    view.position[0] = 0;
    CHECK(portable_mesh_driver_art_angle(&pose, &view, &angle) && angle == 0);
    const PortableRenderPose before = pose;
    const PortableRenderView view_before = view;
    CHECK(portable_mesh_driver_art_angle(&pose, &view, &angle));
    CHECK(!memcmp(&before, &pose, sizeof pose) && !memcmp(&view_before, &view, sizeof view));
    view.position[0] = NAN;
    angle = 55;
    CHECK(!portable_mesh_driver_art_angle(&pose, &view, &angle) && angle == 55);
    CHECK(!portable_mesh_driver_art_angle(NULL, &view, &angle) && angle == 55);
    return 0;
}

static int projection_decoder(void)
{
    Fixture f;
    fixture(&f);
    PortableDriverProjectionRules out;
    CHECK(portable_driver_projection_asset_open(&f.sections[4], &out));
    for (unsigned i = 0; i < 3; ++i)
        CHECK(out.rows[i].max_distance == 511 + i * 512 && out.rows[i].size == (64u >> i) &&
              out.rows[i].offset_x == -24 && out.rows[i].offset_y == -55 &&
              out.rows[i].affine_double_size);
    f.projection[8 + 12] = 1;
    f.projection[9 + 12] = 0;
    CHECK(!portable_driver_projection_asset_open(&f.sections[4], &out));
    fixture(&f);
    f.projection[8 + 2] = 32;
    CHECK(!portable_driver_projection_asset_open(&f.sections[4], &out));
    CHECK(!portable_driver_projection_asset_open(NULL, &out));
    CHECK(!portable_driver_projection_asset_open(&f.sections[4], NULL));
    return 0;
}

static int stock_and_legacy_frames(void)
{
    Fixture f;
    fixture(&f);
    enum
    {
        STOCK_BYTES = 32 + 64 + 34 * 2048 + 25 * (512 + 128) + 512
    };
    static uint8_t stock[STOCK_BYTES];
    const uint32_t header[8] = {0x33524143u, 34 * 2048, 256, 64, 64, 7, 34, 64};
    for (unsigned i = 0; i < 8; ++i)
        word(stock + i * 4, header[i]);
    memset(stock + 32, 24, 64);
    memset(stock + 96, 0x22, 34 * 2048);
    memset(stock + 96 + 33 * 2048, 0x44, 2048);
    half(stock + STOCK_BYTES - 512 + 4, 31u << 5u);
    half(stock + STOCK_BYTES - 512 + 8, 31u << 10u);
    f.sections[2].bytes = stock;
    f.sections[2].size = STOCK_BYTES;
    f.sections[2].version = 3;
    PortableMeshDriverArt *owner = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    CHECK(portable_mesh_driver_art_view(owner, 0) == portable_mesh_driver_art_frame(owner, 24, 0));
    CHECK(
        portable_native_image_pixel(&portable_mesh_driver_art_view(owner, 0)->materials[0].texture,
                                    0, 0) == UINT32_C(0xff00ff00));
    CHECK(portable_native_image_pixel(
              &portable_mesh_driver_art_frame(owner, 33, 0)->materials[0].texture, 0, 0) ==
          UINT32_C(0xff0000ff));
    CHECK(!portable_mesh_driver_art_frame(owner, 34, 0));
    portable_mesh_driver_art_close(owner);
    owner = NULL;
    uint8_t legacy[32 + 2048 + 32] = {0};
    word(legacy, 0x31524143u);
    word(legacy + 4, 2048);
    word(legacy + 8, 16);
    word(legacy + 12, 64);
    word(legacy + 16, 64);
    word(legacy + 20, 200);
    word(legacy + 24, 19);
    legacy[32] = 0x22;
    half(legacy + sizeof legacy - 32 + 4, 31);
    f.sections[2].bytes = legacy;
    f.sections[2].size = sizeof legacy;
    f.sections[2].version = 1;
    CHECK(portable_mesh_driver_art_open(&f.pack, 17, &owner, NULL, 0));
    const PortableRenderMesh *mesh = portable_mesh_driver_art_view(owner, 0);
    CHECK(mesh == portable_mesh_driver_art_view(owner, 63));
    CHECK(portable_native_image_pixel(&mesh->materials[0].texture, 0, 0) == UINT32_C(0xffff0000));
    CHECK(!portable_mesh_driver_art_frame(owner, 1, 0));
    portable_mesh_driver_art_close(owner);
    return 0;
}

static int selected_source_poses(void)
{
    Fixture f;
    fixture(&f);
    PortableMeshDriverArt *owner = NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack,17,&owner,NULL,0));
    CHECK(!portable_mesh_driver_art_rules(owner));
    PortableCharacterViewPose pose = {.angle=0,.mirror=1};
    CHECK(portable_mesh_driver_art_pose_view(owner,&pose,0)==portable_mesh_driver_art_frame(owner,2,1));
    CHECK(portable_mesh_driver_art_view(owner,0)==portable_mesh_driver_art_frame(owner,2,0));
    CHECK(!portable_mesh_driver_art_pose_view(owner,&pose,5));
    pose.angle=63;
    CHECK(portable_mesh_driver_art_pose_view(owner,&pose,0)==portable_mesh_driver_art_frame(owner,0,1));
    pose.angle=0;
    pose.frame_override=1;
    CHECK(portable_mesh_driver_art_pose_view(owner,&pose,0)==portable_mesh_driver_art_frame(owner,1,1));
    double anchor=999,expected;
    CHECK(portable_mesh_driver_art_ground_anchor(owner,1,&expected));
    CHECK(portable_mesh_driver_art_pose_ground_anchor(owner,&pose,&anchor) && anchor==expected);
    pose.frame_override=3;
    CHECK(!portable_mesh_driver_art_pose_view(owner,&pose,0));
    anchor=999;
    CHECK(!portable_mesh_driver_art_pose_ground_anchor(owner,&pose,&anchor) && anchor==999);
    pose.frame_override=0;pose.mirror=2;
    CHECK(!portable_mesh_driver_art_pose_view(owner,&pose,0));
    pose.mirror=0;pose.angle=128;
    CHECK(!portable_mesh_driver_art_pose_view(owner,&pose,0));
    uint8_t rules[38]={0};
    word(rules,UINT32_C(0x31525644));word(rules+4,15);
    memset(rules+8,7,15);memset(rules+23,9,15);
    f.sections[5]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_DRIVER_VIEW_RULES,
        .content_id=1,.version=1,.bytes=rules,.size=sizeof rules};
    f.pack.section_count=6;
    CHECK(portable_mesh_driver_art_open(&f.pack,17,&owner,NULL,0));
    const PortableCharacterViewRules *owned=portable_mesh_driver_art_rules(owner);
    CHECK(owned && owned->response_limit[14]==7 && owned->speed_limit[0]==9);
    rules[8]=19;
    PortableMeshDriverArt *before=owner;
    CHECK(!portable_mesh_driver_art_open(&f.pack,17,&owner,NULL,0) && owner==before);
    CHECK(owned->response_limit[0]==7);
    CHECK(!portable_mesh_driver_art_rules(NULL));
    CHECK(!portable_mesh_driver_art_pose_view(NULL,&pose,0));
    CHECK(!portable_mesh_driver_art_pose_ground_anchor(owner,NULL,&anchor));
    portable_mesh_driver_art_close(owner);
    return 0;
}

static int cached_opaque_pose_bounds(void)
{
    Fixture f;fixture(&f);
    memset(f.graphic+96,0,3u*2048u);
    /* Two nonzero indexed texels bound frame0, including opaque black. */
    for (unsigned i=0;i<2u;++i)
    {
        const unsigned x=i?10u:3u,y=i?20u:4u;
        const size_t offset=((y/8u)*8u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        f.graphic[96+offset]|=(uint8_t)(1u<<((x&1u)*4u));
    }
    PortableMeshDriverArt *owner=NULL;
    CHECK(portable_mesh_driver_art_open(&f.pack,17,&owner,NULL,0));
    PortableCharacterViewPose pose={.angle=3}; /* table slot0 */
    double bounds[6],anchor;
    CHECK(portable_mesh_driver_art_pose_bounds(owner,&pose,bounds));
    CHECK(fabs(bounds[0]-(-24+3)*.05)<1e-12 && fabs(bounds[3]-(-24+11)*.05)<1e-12);
    CHECK(fabs(bounds[2]-(-(-55+21)*.05))<1e-12);
    CHECK(fabs(bounds[5]-(-(-55+4)*.05))<1e-12 && bounds[1]==0 && bounds[4]==0);
    CHECK(portable_mesh_driver_art_pose_ground_anchor(owner,&pose,&anchor) && anchor==bounds[2]);
    const double old_min=bounds[0],old_max=bounds[3],sum=(-24-24+64)*.05;
    pose.mirror=1;
    CHECK(portable_mesh_driver_art_pose_bounds(owner,&pose,bounds));
    CHECK(fabs(bounds[0]-(sum-old_max))<1e-12 && fabs(bounds[3]-(sum-old_min))<1e-12);
    pose.frame_override=1; /* Valid fully transparent frame: no phantom extent. */
    CHECK(portable_mesh_driver_art_pose_bounds(owner,&pose,bounds));
    for(unsigned i=0;i<6u;++i)CHECK(bounds[i]==0);
    memset(f.graphic,0,sizeof f.graphic);
    pose.frame_override=0;pose.mirror=0;
    CHECK(portable_mesh_driver_art_pose_bounds(owner,&pose,bounds) && bounds[0]==old_min);
    double before[6];memcpy(before,bounds,sizeof before);
    pose.frame_override=3;
    CHECK(!portable_mesh_driver_art_pose_bounds(owner,&pose,bounds) && !memcmp(before,bounds,sizeof before));
    CHECK(!portable_mesh_driver_art_pose_bounds(NULL,&pose,bounds));
    CHECK(!portable_mesh_driver_art_pose_bounds(owner,NULL,bounds));
    CHECK(!portable_mesh_driver_art_pose_bounds(owner,&pose,NULL));
    portable_mesh_driver_art_close(owner);return 0;
}

/* A KSP1 v2 set: two pitch rows (0 and 1 in every view), two tumble frames
 * (2, 3) once per hit and effect data. Frame f is filled with blue 16(f+1). */
enum
{
    KSP_SIZE = 64 + 4 + 2 * 64 * 2 + 4 + 256 * 4 + 4 * 64
};
static void sprite_set(uint8_t *b)
{
    memset(b, 0, KSP_SIZE);
    const uint32_t head[7] = {0x3150534Bu, 8u | 8u << 16, 4, 1, 256, 3277, 0};
    for (unsigned i = 0; i < 7; ++i)
        word(b + i * 4, head[i]);
    b[28] = 2, b[29] = 2, b[30] = 1, b[31] = PORTABLE_DRIVER_SPRITE_FILTERED;
    word(b + 32, 0x70), word(b + 36, 0x707000), word(b + 40, 0x700000), word(b + 44, 0x7000);
    b[48] = 2, b[49] = 0x70, b[50] = 0x60;
    half(b + 64, 0x8000), half(b + 66, 1000);
    for (unsigned i = 0; i < 64; ++i)
        half(b + 68 + i * 2, 0), half(b + 68 + 128 + i * 2, 1);
    half(b + 324, 2), half(b + 326, 3);
    for (unsigned c = 1; c <= 4; ++c)
        word(b + 328 + c * 4, 0xff000000u | c * 16u);
    for (unsigned f = 0; f < 4; ++f)
        memset(b + 328 + 1024 + f * 64, (int)(f + 1), 64);
}
static int shown_frame(const PortableMeshDriverArt *owner)
{
    const PortableCharacterViewPose pose = {0};
    const PortableRenderMesh *mesh = portable_mesh_driver_art_pose_view(owner, &pose, 0);
    return mesh && mesh->material_count ? mesh->materials[0].texture.pixels[0] / 16 - 1 : -1;
}
static int sprite_rows_and_tumble(void)
{
    static uint8_t bytes[KSP_SIZE];
    sprite_set(bytes);
    PortableAssetSectionView section = {.kind = PORTABLE_ASSET_SECTION_DRIVER_SPRITES, .content_id = 17,
                                        .version = 2, .bytes = bytes, .size = KSP_SIZE};
    PortableDriverSprites view;
    CHECK(portable_driver_sprites_open(&section, &view) && view.row_count == 2 && view.tumble_count == 2);
    CHECK(view.star_add[1] == 0x707000 && view.star_ticks == 2 && view.ghost_alpha == 0x70 && view.boo_alpha == 0x60);
    CHECK(portable_driver_sprites_row(&view, -32768) == 0 && portable_driver_sprites_row(&view, 999) == 0 &&
          portable_driver_sprites_row(&view, 1000) == 1 && portable_driver_sprites_row(&view, 32767) == 1);
    PortableAssetPackView pack = {.resolved_sections = &section, .section_count = 1};
    PortableMeshDriverArt *owner = NULL;
    CHECK(portable_mesh_driver_art_open(&pack, 17, &owner, NULL, 0));
    CHECK(portable_mesh_driver_art_sprites(owner) && shown_frame(owner) == 0);
    const double row1 = 1000.0 / 65536.0 * 2.0 * 3.14159265358979323846;
    CHECK(!portable_mesh_driver_art_set_pose(owner, row1 + 1e-6, -1) && shown_frame(owner) == 1);
    CHECK(!portable_mesh_driver_art_set_pose(owner, row1 - 1e-3, -1) && shown_frame(owner) == 0);
    CHECK(portable_mesh_driver_art_set_pose(owner, 0, 0.25) && shown_frame(owner) == 2);
    CHECK(portable_mesh_driver_art_set_pose(owner, 0, 0.75) && shown_frame(owner) == 3);
    const PortableCharacterViewPose pose = {0};
    const PortableRenderMesh *mesh = portable_mesh_driver_art_pose_view(owner, &pose, 0);
    CHECK(mesh->materials[0].filter == PORTABLE_RENDER_FILTER_THREE_POINT);
    portable_mesh_driver_art_close(owner);
    /* Rows must start at -32768 and ascend; tumble frames must exist; spare bytes stay zero. */
    const struct { unsigned at; uint8_t value; } bad[] = {{65, 0}, {66, 0x00}, {324, 9}, {51, 1}, {31, 2}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i)
    {
        sprite_set(bytes);
        bytes[bad[i].at] = bad[i].value;
        if (bad[i].at == 66) half(bytes + 66, 0x8000);
        CHECK(!portable_driver_sprites_open(&section, &view));
    }
    return 0;
}

int main(void)
{
    CHECK(!decoded_cells() && !opaque_ground_edges() && !imported_effect_palettes() &&
          !identity_and_rollback() && !angle_selection() && !projection_decoder() &&
          !stock_and_legacy_frames() && !selected_source_poses() && !cached_opaque_pose_bounds() &&
          !sprite_rows_and_tumble());
    puts(
        "Mesh driver art: PASS (exact identity, source pixels, directional mirrors, foot anchors, rollback, "
        "KSP1 pitch rows and tumble frames)");
    return 0;
}
