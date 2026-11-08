#include "ui_canvas.h"
#include "ui_font.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "canvas alpha line %d: %s\n", __LINE__, #condition);                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int source_over(void)
{
    uint32_t pixels[2];
    UiCanvas canvas;
    memset(&canvas, 0xff, sizeof canvas);
    ui_canvas_init_transparent(&canvas, pixels, 2, 1, 0);
    CHECK(canvas.premultiplied_alpha && canvas.stride == 2);
    ui_canvas_clear(&canvas, 0x00ffffffu);
    CHECK(pixels[0] == 0u && pixels[1] == 0u);
    ui_blend_pixel(&canvas, 0, 0, 0x80ff0000u, 1.0f);
    CHECK(pixels[0] == 0x80800000u);
    ui_blend_pixel(&canvas, 0, 0, 0x800000ffu, 1.0f);
    CHECK(pixels[0] == 0xc0400080u);
    ui_blend_pixel(&canvas, 0, 0, 0x4000ff00u, 1.0f);
    CHECK(pixels[0] == 0xd0304060u);
    ui_blend_pixel(&canvas, 1, 0, 0x80ff0000u, 0.5f);
    CHECK(pixels[1] == 0x40400000u);
    ui_canvas_clear(&canvas, 0x80402010u);
    CHECK(pixels[0] == 0x80201008u);
    ui_blend_pixel(&canvas, 0, 0, 0x00ffffffu, 1.0f);
    CHECK(pixels[0] == 0x80201008u);
    ui_blend_pixel(&canvas, 0, 0, 0xff123456u, 1.0f);
    CHECK(pixels[0] == 0xff123456u);

    /* Reusing a transparent canvas through the old initializer resets mode. */
    ui_canvas_init(&canvas, pixels, 2, 1, 2);
    CHECK(!canvas.premultiplied_alpha);
    ui_canvas_clear(&canvas, 0x00123456u);
    CHECK(pixels[0] == 0xff123456u);
    ui_canvas_clear(&canvas, 0u);
    ui_blend_pixel(&canvas, 0, 0, 0x80ff0000u, 1.0f);
    CHECK(pixels[0] == 0xff800000u);
    ui_blend_pixel(&canvas, 0, 0, 0x800000ffu, 1.0f);
    CHECK(pixels[0] == 0xff400080u);
    return 0;
}

static int span_polygon_image(void)
{
    enum
    {
        WIDTH = 8,
        HEIGHT = 6
    };
    uint32_t pixels[4][WIDTH * HEIGHT];
    UiCanvas canvas[4];
    const uint32_t source = 0x80ff0000u;
    const UiImage image = {&source, 1, 1};
    const UiPaint paint = ui_paint_solid(source);
    const UiPoint rectangle[4] = {{1, 1}, {7, 1}, {7, 5}, {1, 5}};
    for (unsigned i = 0; i < 4u; ++i)
    {
        ui_canvas_init_transparent(&canvas[i], pixels[i], WIDTH, HEIGHT, WIDTH);
        ui_canvas_clear(&canvas[i], 0x800000ffu);
    }
    for (int y = 1; y < 5; ++y)
        for (int x = 1; x < 7; ++x)
            ui_blend_pixel(&canvas[0], x, y, source, 1.0f);
    /* Polygon and rounded-box interiors use optimized constant-color spans. */
    ui_fill_convex(&canvas[1], rectangle, 4, &paint, 1.0f, 1.0f);
    ui_fill_round_rect(&canvas[2], 1, 1, 6, 4, 0, 0, &paint, 1.0f, 1.0f);
    ui_draw_image(&canvas[3], &image, 1, 1, 6, 4, 1.0f, UI_IMAGE_NEAREST, 0xffffffffu, 0);
    CHECK(pixels[0][WIDTH + 1] == 0xc0800040u);
    for (unsigned i = 1; i < 4u; ++i)
        CHECK(!memcmp(pixels[0], pixels[i], sizeof pixels[0]));
    return 0;
}

static int image_alpha(void)
{
    uint32_t pixel;
    UiCanvas canvas;
    ui_canvas_init_transparent(&canvas, &pixel, 1, 1, 1);
    const uint32_t green = 0x8000ff00u;
    const UiImage image = {&green, 1, 1};
    ui_canvas_clear(&canvas, 0u);
    ui_draw_image(&canvas, &image, 0, 0, 1, 1, 1, UI_IMAGE_NEAREST, 0x80ffffffu, 0);
    CHECK(pixel == 0x40004000u);
    ui_canvas_clear(&canvas, 0u);
    ui_draw_image(&canvas, &image, 0, 0, 1, 1, 1, UI_IMAGE_SILHOUETTE, 0x80008000u, 0);
    CHECK(pixel == 0x40002000u);

    /* Transparent green must not bleed into a bilinear red edge. */
    const uint32_t edge[2] = {0xffff0000u, 0x0000ff00u};
    const UiImage edge_image = {edge, 2, 1};
    ui_canvas_clear(&canvas, 0u);
    ui_draw_image(&canvas, &edge_image, 0, 0, 1, 1, 1, UI_IMAGE_SMOOTH, 0xffffffffu, 0);
    CHECK(pixel == 0x80800000u);
    return 0;
}

static int clipping_and_tint(void)
{
    enum
    {
        WIDTH = 6,
        HEIGHT = 5,
        STRIDE = 8,
        GUARDS = 2
    };
    uint32_t storage[STRIDE * HEIGHT + GUARDS * 2];
    for (size_t i = 0; i < sizeof storage / sizeof *storage; ++i)
        storage[i] = 0xdeadbeefu;
    uint32_t *pixels = storage + GUARDS;
    UiCanvas canvas;
    ui_canvas_init_transparent(&canvas, pixels, WIDTH, HEIGHT, STRIDE);
    ui_canvas_clear(&canvas, 0u);
    ui_canvas_clip(&canvas, 2, 1, 4, 3);
    ui_fill_rect(&canvas, -10, -10, 30, 30, 0x80ff0000u, 1);
    ui_tint_rect(&canvas, -10, -10, 30, 30, 0x000000ffu, 0.5f);
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < STRIDE; ++x)
        {
            const uint32_t expected = x >= WIDTH                           ? 0xdeadbeefu
                                      : x >= 2 && x < 4 && y >= 1 && y < 3 ? 0x80400040u
                                                                           : 0u;
            CHECK(pixels[y * STRIDE + x] == expected);
        }
    CHECK(storage[0] == 0xdeadbeefu && storage[1] == 0xdeadbeefu);
    CHECK(storage[STRIDE * HEIGHT + GUARDS] == 0xdeadbeefu);
    CHECK(storage[STRIDE * HEIGHT + GUARDS + 1] == 0xdeadbeefu);
    ui_canvas_unclip(&canvas);
    ui_tint_rect(&canvas, 0, 0, WIDTH, HEIGHT, 0xffffffffu, 1.0f);
    CHECK(pixels[0] == 0u && pixels[STRIDE + 2] == 0x80808080u);
    return 0;
}

static void draw_primitives(UiCanvas *canvas)
{
    const UiPaint paint = ui_paint_linear(0, 0, 0x40ff0000u, 40, 24, 0xc00000ffu);
    const UiPoint polygon[3] = {{2, 2}, {35, 3}, {18, 21}};
    const uint32_t texels[4] = {0x80ff0000u, 0x0000ff00u, 0xff0000ffu, 0x40ffffffu};
    const UiImage image = {texels, 2, 2};
    ui_fill_convex(canvas, polygon, 3, &paint, 2, 0.7f);
    ui_fill_round_rect(canvas, 1, 2, 30, 20, 4, 0.2f, &paint, 1, 0.6f);
    ui_fill_chevron_bar(canvas, 0, 5, 36, 12, 5, &paint, 1, 0.4f);
    ui_fill_ellipse(canvas, 20, 12, 16, 9, &paint, 1, 0.5f);
    ui_fill_circle(canvas, 8, 8, 6, &paint, 1, 0.5f);
    ui_stroke_circle(canvas, 20, 12, 9, 2, 0x8044ff22u, 0.8f);
    ui_stroke_round_rect(canvas, 3, 3, 32, 18, 4, 0.1f, 2, 0x8033aaffu, 0.8f);
    ui_fill_rect(canvas, 4, 4, 5, 6, 0x808899aau, 0.6f);
    ui_draw_image(canvas, &image, 3, 3, 15, 13, 0.7f, UI_IMAGE_SMOOTH, 0x80aaccffu, 3);
    ui_draw_image_region(canvas, &image, 0, 0, 1, 2, 19, 4, 12, 14, 0.8f,
                         UI_IMAGE_NEAREST | UI_IMAGE_GRAY, 0xffffffffu, 0);
    ui_tint_rect(canvas, 0, 0, 40, 24, 0x00224466u, 0.3f);
}

static int opaque_equivalence(void)
{
    uint32_t pixels[2][40 * 24];
    UiCanvas canvas[2];
    ui_canvas_init(&canvas[0], pixels[0], 40, 24, 40);
    ui_canvas_init_transparent(&canvas[1], pixels[1], 40, 24, 40);
    for (unsigned i = 0; i < 2u; ++i)
    {
        ui_canvas_clear(&canvas[i], 0xff123456u);
        draw_primitives(&canvas[i]);
    }
    CHECK(!memcmp(pixels[0], pixels[1], sizeof pixels[0]));
    return 0;
}

static int font_source_over(void)
{
    enum
    {
        WIDTH = 96,
        HEIGHT = 80,
        STRIDE = 101
    };
    const uint32_t guard = UINT32_C(0x12345678), background = UINT32_C(0xff406080);
    uint32_t storage[3][STRIDE * HEIGHT + 2];
    CHECK(ui_font_ready());
    for (unsigned face = 0; face < UI_FONT_COUNT; ++face)
        for (unsigned passes = 1; passes <= 3u; ++passes)
        {
            UiCanvas canvases[3];
            for (unsigned mode = 0; mode < 3u; ++mode)
            {
                for (size_t i = 0; i < STRIDE * HEIGHT + 2u; ++i)
                    storage[mode][i] = guard;
                if (mode == 0u)
                    ui_canvas_init(&canvases[mode], storage[mode] + 1, WIDTH, HEIGHT, STRIDE);
                else
                    ui_canvas_init_transparent(&canvases[mode], storage[mode] + 1, WIDTH, HEIGHT,
                                               STRIDE);
                ui_canvas_clear(&canvases[mode], mode == 1u ? 0u : background);
                ui_canvas_clip(&canvases[mode], 14, 12, 70, 62);
            }
            UiTextStyle style = ui_text_style((UiFontFace)face, 42.0f, UINT32_C(0xffe0a035));
            if (passes >= 2u)
            {
                style.outline = 2.25f;
                style.outline_color = UINT32_C(0xc010b8e8);
            }
            if (passes == 3u)
            {
                style.shadow_color = UINT32_C(0x80000080);
                style.shadow_x = 1.375f;
                style.shadow_y = 2.625f;
                style.shadow_blur = 4.25f;
            }
            const UiTextStyle before = style;
            for (unsigned mode = 0; mode < 3u; ++mode)
                ui_text_draw(&canvases[mode], &style, 10.375f, 55.625f, UI_ALIGN_LEFT, "R", 1.0f);
            CHECK(!memcmp(&before, &style, sizeof style));
            size_t partial = 0, opaque = 0, clipped = 0;
            for (int y = 0; y < HEIGHT; ++y)
                for (int x = 0; x < STRIDE; ++x)
                {
                    const size_t index = 1u + (size_t)y * STRIDE + (size_t)x;
                    if (x >= WIDTH)
                    {
                        for (unsigned mode = 0; mode < 3u; ++mode)
                            CHECK(storage[mode][index] == guard);
                        continue;
                    }
                    const uint32_t overlay = storage[1][index], direct = storage[0][index];
                    const unsigned alpha = overlay >> 24u;
                    CHECK(((overlay >> 16u) & 255u) <= alpha && ((overlay >> 8u) & 255u) <= alpha &&
                          (overlay & 255u) <= alpha);
                    partial += alpha > 0u && alpha < 255u;
                    opaque += alpha == 255u;
                    CHECK(storage[2][index] ==
                          direct); /* Opaque destination arithmetic stays exact. */
                    if (x < 14 || x >= 70 || y < 12 || y >= 62)
                    {
                        CHECK(overlay == 0u && direct == background);
                        ++clipped;
                    }
                    for (unsigned shift = 0; shift < 24u; shift += 8u)
                    {
                        const unsigned composed =
                            ((overlay >> shift) & 255u) +
                            ((((background >> shift) & 255u) * (255u - alpha) + 127u) / 255u);
                        const unsigned reference = (direct >> shift) & 255u;
                        const unsigned difference =
                            composed > reference ? composed - reference : reference - composed;
                        /* One final composite groups up to three separately
                         * rounded source-over passes. Each can add <=1 byte
                         * of rounding difference; no glyph shapes differ. */
                        CHECK(difference <= passes);
                    }
                }
            CHECK(partial > 20u && opaque > 10u && clipped > 100u);
            for (unsigned mode = 0; mode < 3u; ++mode)
                CHECK(storage[mode][0] == guard && storage[mode][STRIDE * HEIGHT + 1u] == guard);
        }
    ui_font_release();
    return 0;
}

int main(void)
{
    CHECK(!source_over());
    CHECK(!span_polygon_image());
    CHECK(!image_alpha());
    CHECK(!clipping_and_tint());
    CHECK(!opaque_equivalence());
    CHECK(!font_source_over());
    puts("UI canvas alpha: PASS");
    return 0;
}
