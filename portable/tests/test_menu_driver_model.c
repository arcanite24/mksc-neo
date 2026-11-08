#include "menu_driver_model.h"
#include "model_asset.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "menu driver model line %d: %s\n", __LINE__, #c);                      \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (i * 8u));
}

typedef struct
{
    uint8_t red[16], blue[16];
    uint8_t driver_mesh[16 + 4 + 4 * 20 + 2 * 8], kart_mesh[16 + 4 + 4 * 20 + 2 * 8];
    uint8_t bindings[8 + 44], driver_row[64], kart_row[80 + 16];
    PortableAssetSectionView sections[7];
    PortableAssetPackView pack;
} Fixture;

static void material(uint8_t *p, uint32_t argb)
{
    memcpy(p, "MTL1", 4);
    put32(p + 4, argb);
}

/* A one-material quad: corners in mesh units (Q0), two triangles. */
static void quad(uint8_t *p, uint32_t material_id, const int32_t corners[4][3])
{
    memcpy(p, "MSH1", 4);
    put32(p + 4, 4);
    put32(p + 8, 2);
    put32(p + 12, 1);
    put32(p + 16, material_id);
    uint8_t *vertex = p + 20;
    for (unsigned i = 0; i < 4u; ++i, vertex += 20)
        for (unsigned axis = 0; axis < 3u; ++axis)
            put32(vertex + axis * 4u, (uint32_t)(corners[i][axis] * 65536));
    static const uint16_t triangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2u; ++t)
        for (unsigned c = 0; c < 3u; ++c)
        {
            vertex[t * 8u + c * 2u] = (uint8_t)triangles[t][c];
            vertex[t * 8u + c * 2u + 1u] = 0;
        }
}

static void fixture(Fixture *f)
{
    memset(f, 0, sizeof *f);
    material(f->red, 0xffff0000u);
    material(f->blue, 0xff0000ffu);
    /* An upright driver card on a flat kart deck. */
    const int32_t card[4][3] = {{-10, 0, 0}, {10, 0, 0}, {10, 0, 20}, {-10, 0, 20}};
    const int32_t deck[4][3] = {{-15, -20, 0}, {15, -20, 0}, {15, 20, 0}, {-15, 20, 0}};
    quad(f->driver_mesh, 1, card);
    quad(f->kart_mesh, 2, deck);
    memcpy(f->bindings, "BND1", 4);
    put32(f->bindings + 4, 1);
    const uint32_t binding[11] = {PORTABLE_MODEL_DRIVER, 200, 0, 0, 10, 65536, 65536, 65536, 0, 0, 0};
    for (unsigned i = 0; i < 11u; ++i)
        put32(f->bindings + 8 + i * 4u, binding[i]);
    memcpy(f->driver_row, "DRV3", 4);
    const uint32_t driver[7] = {1, PORTABLE_DRIVER_3D_SELECTABLE, 65536, 65536, 65536, 65536, 0};
    for (unsigned i = 0; i < 7u; ++i)
        put32(f->driver_row + 4 + i * 4u, driver[i]);
    memcpy(f->driver_row + 32, "Card", 4);
    memcpy(f->kart_row, "KRT3", 4);
    const uint32_t kart[11] = {200, PORTABLE_KART_3D_SELECTABLE | PORTABLE_KART_3D_OWNER_DEFAULT, 11, 0, 0, 0,
                               65536, 65536, 65536, 65536, 1};
    for (unsigned i = 0; i < 11u; ++i)
        put32(f->kart_row + 4 + i * 4u, kart[i]);
    memcpy(f->kart_row + 48, "Deck", 4);
    put32(f->kart_row + 80, 200);
    put32(f->kart_row + 92, 2u * 65536u); /* Seat two units above the deck. */
    const PortableAssetSectionView sections[7] = {
        {.kind = PORTABLE_ASSET_SECTION_MODEL_MATERIAL, .content_id = 1, .version = 1, .bytes = f->red, .size = 16},
        {.kind = PORTABLE_ASSET_SECTION_MODEL_MATERIAL, .content_id = 2, .version = 1, .bytes = f->blue, .size = 16},
        {.kind = PORTABLE_ASSET_SECTION_MODEL_MESH, .content_id = 10, .version = 1, .bytes = f->driver_mesh,
         .size = sizeof f->driver_mesh},
        {.kind = PORTABLE_ASSET_SECTION_MODEL_MESH, .content_id = 11, .version = 1, .bytes = f->kart_mesh,
         .size = sizeof f->kart_mesh},
        {.kind = PORTABLE_ASSET_SECTION_MODEL_BINDINGS, .content_id = 1, .version = 1, .bytes = f->bindings,
         .size = sizeof f->bindings},
        {.kind = PORTABLE_ASSET_SECTION_DRIVER_3D, .content_id = 200, .version = 1, .bytes = f->driver_row,
         .size = sizeof f->driver_row},
        {.kind = PORTABLE_ASSET_SECTION_KART_3D, .content_id = 300, .version = 1, .bytes = f->kart_row,
         .size = sizeof f->kart_row}};
    memcpy(f->sections, sections, sizeof sections);
    f->pack = (PortableAssetPackView){.resolved_sections = f->sections, .section_count = 7};
}

static void count_colors(const UiImage *image, unsigned *red, unsigned *blue, unsigned *clear)
{
    *red = *blue = *clear = 0u;
    for (int i = 0; i < image->width * image->height; ++i)
    {
        const uint32_t color = image->pixels[i];
        if ((color >> 24) == 0u) ++*clear;
        else if ((color >> 24) == 255u && color == 0xffff0000u) ++*red;
        else if ((color >> 24) == 255u && color == 0xff0000ffu) ++*blue;
    }
}

int main(void)
{
    static Fixture f;
    fixture(&f);
    PortableMenuDriverModel *model = portable_menu_driver_model_create();
    CHECK(model != NULL);
    PortableMenuDriverModelPose pose = {.driver_id = 200, .size = 64, .yaw = 0.0};
    UiImage image;
    CHECK(portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    CHECK(image.width == 64 && image.height == 64 && image.pixels != NULL);
    unsigned red, blue, clear;
    count_colors(&image, &red, &blue, &clear);
    /* Driver and kart both draw, on a transparent background. */
    CHECK(red > 100u && blue > 100u && clear > 1000u);
    CHECK((image.pixels[0] >> 24) == 0u && (image.pixels[64 * 64 - 1] >> 24) == 0u);
    /* The driver sits on the seat above the deck: red reaches higher rows. */
    int red_top = 64, blue_top = 64;
    for (int i = 0; i < 64 * 64; ++i)
    {
        if (image.pixels[i] == 0xffff0000u && i / 64 < red_top) red_top = i / 64;
        if (image.pixels[i] == 0xff0000ffu && i / 64 < blue_top) blue_top = i / 64;
    }
    CHECK(red_top < blue_top);
    static uint32_t front[64 * 64];
    memcpy(front, image.pixels, sizeof front);
    /* Turning changes the picture; an explicit kart matches its default. */
    pose.yaw = 1.2;
    CHECK(portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    CHECK(memcmp(front, image.pixels, sizeof front) != 0);
    pose.yaw = 0.0;
    pose.kart_id = 300;
    CHECK(portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    CHECK(memcmp(front, image.pixels, sizeof front) == 0);
    /* Missing drivers and karts fail (once reported) so menus keep fallback art. */
    pose.kart_id = 301;
    CHECK(!portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    CHECK(!portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    pose = (PortableMenuDriverModelPose){.driver_id = 201, .size = 64};
    CHECK(!portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    pose.driver_id = 200;
    pose.size = 4;
    CHECK(!portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    /* Forgetting the pack's content reopens it on the next render. */
    portable_menu_driver_model_forget(model);
    pose.size = 64;
    CHECK(portable_menu_driver_model_render(model, &f.pack, &pose, &image));
    CHECK(memcmp(front, image.pixels, sizeof front) == 0);
    /* Classic frames composite at their physical scale around the centre. */
    static uint32_t pixels[240 * 2 * 160 * 2];
    for (size_t i = 0; i < sizeof pixels / sizeof pixels[0]; ++i) pixels[i] = 0xff102030u;
    PortableSoftwareFrame frame = {.pixels = pixels, .width = 240, .height = 160, .scale = 2};
    pose.size = 32;
    CHECK(portable_menu_driver_model_draw_frame(model, &f.pack, &pose, &frame, 72, 80));
    unsigned changed = 0u, outside = 0u;
    for (size_t y = 0; y < 320u; ++y)
        for (size_t x = 0; x < 480u; ++x)
            if (pixels[y * 480u + x] != 0xff102030u)
            {
                ++changed;
                if (x < 112u || x >= 176u || y < 128u || y >= 192u) ++outside;
            }
    CHECK(changed > 200u && outside == 0u);
    portable_menu_driver_model_destroy(model);
    puts("menu driver model: PASS");
    return 0;
}
