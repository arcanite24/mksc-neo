#include "rom_import_frontend.h"

#include <string.h>

/* Port of tools/portable/ui_rom.py title_backgrounds and title_sections:
 * US title_loadGraphics (08000A34) and title_main (08000E6C) assets. */

#define TITLE_BACKGROUND 1u
#define TITLE_LABEL 0x20000u
#define TITLE_SELECTED_LABEL 0x20100u
#define TITLE_FRAME 0x20200u
#define TITLE_UP 0x20201u
#define TITLE_DOWN 0x20202u
#define TITLE_LINK_FRAME 0x20203u
#define TITLE_BACKGROUND_FRAME 0x20300u
#define TITLE_NIGHT_BACKGROUND 0x20400u
#define TITLE_SUNSET_BACKGROUND 0x20500u
#define TITLE_NIGHT_TWINKLE 0x20600u /* +1..7: star colour rows 1..7 over row 0 */

#define SCREEN_W 240u
#define SCREEN_H 160u
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)
#define MAP_SIZE 256u /* a 32x32-cell tile map is 256 pixels square */

typedef enum TitleVariant { VARIANT_NORMAL, VARIANT_SUNSET, VARIANT_NIGHT } TitleVariant;

/* Layers shared by every compose() call of one variant. */
typedef struct TitleArt {
    RiBuf *sky_tiles;
    RiBuf *sky_map;
    RiBuf *panorama_map;
    RiBuf *logo; /* 256x256 tile-map pixels */
} TitleArt;

/* title_backgrounds.compose: backdrop colour, sky, panorama, logo. */
static RiBuf *compose(RiCtx *ctx, const TitleArt *art, const RiBuf *palette)
{
    RiBuf *sky = ri_tile_map(ctx, ri_view(art->sky_tiles), ri_view(palette), ri_view(art->sky_map), 32, 32, 4);
    RiBuf *panorama = ri_tile_map(ctx, ri_view(art->sky_tiles), ri_view(palette),
                                  ri_view(art->panorama_map), 32, 32, 8);
    const RiBuf *layers[3] = {sky, panorama, art->logo};
    static const uint32_t y_origins[3] = {80, 96, 0};
    uint32_t backdrop = ri_color(ri_get_u16(ctx, ri_view(palette), 0));
    RiBuf *base = ri_buf_zero(ctx, SCREEN_PIXELS * 4);
    for (size_t i = 0; i < SCREEN_PIXELS; ++i)
        ri_set_pixel(base, i, backdrop);
    for (size_t layer = 0; layer < 3; ++layer)
        for (uint32_t y = 0; y < SCREEN_H; ++y)
            for (uint32_t x = 0; x < SCREEN_W; ++x) {
                uint32_t pixel = ri_pixel(layers[layer], ((y + y_origins[layer]) % MAP_SIZE) * MAP_SIZE + x % MAP_SIZE);
                if (pixel) ri_set_pixel(base, y * SCREEN_W + x, pixel);
            }
    ri_buf_free(ctx, sky);
    ri_buf_free(ctx, panorama);
    return base;
}

static RiBuf *title_palette(RiCtx *ctx, TitleVariant variant)
{
    static const size_t variant_palettes[3] = {0xCDAE4, 0xCDB78, 0xCDC0C};
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_place(ctx, palette, 0, ri_view(ri_fe_lz(ctx, variant_palettes[variant])));
    ri_fe_place(ctx, palette, 128, ri_view(ri_fe_lz(ctx, 0xCD9A0)));
    if (variant == VARIANT_SUNSET) {
        ri_fe_splice(ctx, palette, 128, 160, ri_view(ri_fe_lz(ctx, 0xCDA9C)));
        ri_fe_splice(ctx, palette, 224, 256, ri_view(ri_fe_lz(ctx, 0xCDAC4)));
    } else if (variant == VARIANT_NIGHT) {
        ri_fe_splice(ctx, palette, 128, 160, ri_fe_rom_slice(ctx, 0xD5CA0, 0xD5CC0));
        ri_fe_splice(ctx, palette, 224, 256, ri_fe_rom_slice(ctx, 0xD5CC0, 0xD5CE0));
    }
    return palette;
}

/* title_backgrounds: flag frames for the normal, sunset or night title
 * palette and sky; night also yields seven star-twinkle overlays. Emits the
 * sections in title_sections order. */
static void title_backgrounds(RiCtx *ctx, TitleVariant variant, uint32_t first)
{
    static const size_t driver_art[4] = {0xD0DE0, 0xD1D20, 0xD2BA8, 0xD3A90};
    RiBuf *palette = title_palette(ctx, variant);
    RiBuf *logo_tiles = ri_fe_lz(ctx, 0xCDCA0);
    TitleArt art;
    art.sky_tiles = ri_fe_lz(ctx, 0xCEBB4);
    ri_put_buf(ctx, art.sky_tiles, ri_fe_lz(ctx, 0xD0060));
    /* The settled title replaces the cloud art with its driver panorama. */
    RiBuf *drivers = ri_buf(ctx);
    for (size_t i = 0; i < 4; ++i)
        ri_put_buf(ctx, drivers, ri_fe_lz(ctx, driver_art[i]));
    ri_fe_place(ctx, art.sky_tiles, 0x2800, ri_view(drivers));
    RiBuf *blank = ri_buf_zero(ctx, 64);
    ri_fe_splice(ctx, art.sky_tiles, 0, 64, ri_view(blank));
    art.sky_map = ri_fe_lz(ctx, variant == VARIANT_NIGHT ? 0xD53D8 : 0xD4E10);
    art.panorama_map = ri_fe_lz(ctx, 0xD58F8);
    art.logo = ri_tile_map(ctx, ri_view(logo_tiles), ri_view(palette), ri_view(ri_fe_lz(ctx, 0xD4794)), 32, 32, 4);

    RiBuf *base = compose(ctx, &art, palette);
    RiBuf *flagged = ri_buf_zero(ctx, SCREEN_PIXELS); /* membership mask */
    RiBuf *flag_map = ri_fe_lz(ctx, 0xD455C);
    for (size_t phase = 0; phase < 5; ++phase) {
        size_t address = ri_fe_pointer(ctx, 0xD8E6C + phase * 4);
        RiBuf *flag_tiles = ri_buf_copy(ctx, logo_tiles->data, logo_tiles->size);
        if (flag_tiles->size < 0x2000) ri_resize(ctx, flag_tiles, 0x2000);
        ri_put_buf(ctx, flag_tiles, ri_fe_lz(ctx, address));
        RiBuf *flag = ri_tile_map(ctx, ri_view(flag_tiles), ri_view(palette), ri_view(flag_map), 32, 32, 4);
        RiBuf *pixels = ri_buf_copy(ctx, base->data, base->size);
        for (uint32_t y = 0; y < SCREEN_H; ++y)
            for (uint32_t x = 0; x < SCREEN_W; ++x) {
                uint32_t source = ri_pixel(flag, ((y + 32) % MAP_SIZE) * MAP_SIZE + x);
                if (!source || ri_pixel(art.logo, y * MAP_SIZE + x)) continue;
                size_t index = y * SCREEN_W + x;
                flagged->data[index] = 1;
                uint32_t target = ri_pixel(pixels, index), value = 0xFF000000u;
                for (int shift = 0; shift <= 16; shift += 8)
                    value |= (((source >> shift & 255) * 10 + (target >> shift & 255) * 6) / 16) << shift;
                ri_set_pixel(pixels, index, value);
            }
        uint32_t image_id = phase == 0 ? first
                            : (variant == VARIANT_NORMAL ? TITLE_BACKGROUND_FRAME : first) + (uint32_t)phase;
        ri_section(ctx, 15, image_id, 1, ri_image_asset(ctx, SCREEN_W, SCREEN_H, pixels, 0, 0));
        ri_buf_free(ctx, pixels);
        ri_buf_free(ctx, flag);
        ri_buf_free(ctx, flag_tiles);
    }
    if (variant != VARIANT_NIGHT) return;
    /* Stars cycle bank 7 colours 10..13 through eight rows every 9 frames. */
    for (uint32_t row = 1; row < 8; ++row) {
        RiBuf *shifted = ri_buf_copy(ctx, palette->data, palette->size);
        size_t start = 0xD5CC0 + (size_t)row * 32;
        ri_fe_splice(ctx, shifted, 224 + 20, 224 + 28, ri_fe_rom_slice(ctx, start + 20, start + 28));
        RiBuf *pixels = compose(ctx, &art, shifted);
        RiBuf *keep = ri_buf_zero(ctx, SCREEN_PIXELS);
        uint32_t x0 = SCREEN_W, y0 = SCREEN_H, x1 = 0, y1 = 0;
        for (uint32_t i = 0; i < SCREEN_PIXELS; ++i) {
            if (ri_pixel(base, i) == ri_pixel(pixels, i) || flagged->data[i]) continue;
            uint32_t x = i % SCREEN_W, y = i / SCREEN_W;
            keep->data[i] = 1;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x + 1 > x1) x1 = x + 1;
            if (y + 1 > y1) y1 = y + 1;
        }
        if (!x1) { /* no change: Python falls back to changed = [0] */
            keep->data[0] = 1;
            x0 = y0 = 0;
            x1 = y1 = 1;
        }
        uint32_t width = x1 - x0, height = y1 - y0;
        RiBuf *crop = ri_buf_zero(ctx, (size_t)width * height * 4);
        for (uint32_t y = y0; y < y1; ++y)
            for (uint32_t x = x0; x < x1; ++x)
                if (keep->data[y * SCREEN_W + x])
                    ri_set_pixel(crop, (y - y0) * width + (x - x0), ri_pixel(pixels, y * SCREEN_W + x));
        ri_section(ctx, 15, TITLE_NIGHT_TWINKLE + row, 1,
                   ri_image_asset(ctx, width, height, crop, (int32_t)x0, (int32_t)y0));
        ri_buf_free(ctx, crop);
        ri_buf_free(ctx, keep);
        ri_buf_free(ctx, pixels);
        ri_buf_free(ctx, shifted);
    }
}

void ri_fe_title_presentation_sections(RiCtx *ctx)
{
    /* Reuse the stock compositor verbatim: resolved flat title overrides can
     * be compared with these sections before enabling the companion layers. */
    title_backgrounds(ctx, VARIANT_NORMAL, TITLE_BACKGROUND);
    title_backgrounds(ctx, VARIANT_SUNSET, TITLE_SUNSET_BACKGROUND);
    title_backgrounds(ctx, VARIANT_NIGHT, TITLE_NIGHT_BACKGROUND);

    /* title_loadGraphics initially loads the cloud scene. Its settled title
     * later replaces these tiles and the cloud map with the driver panorama;
     * retain the original scene here, without any logo or driver artwork. */
    RiBuf *tiles = ri_fe_lz(ctx, 0xCEBB4);
    ri_put_buf(ctx, tiles, ri_fe_lz(ctx, 0xD0060));
    RiBuf *cloud_map = ri_fe_lz(ctx, 0xD49FC);
    for (unsigned variant = VARIANT_NORMAL; variant <= VARIANT_NIGHT; ++variant) {
        RiBuf *palette = title_palette(ctx, (TitleVariant)variant);
        RiBuf *sky_map = ri_fe_lz(ctx, variant == VARIANT_NIGHT ? 0xD53D8 : 0xD4E10);
        RiBuf *sky = ri_tile_map(ctx, ri_view(tiles), ri_view(palette),
            ri_view(sky_map), 32, 32, 4);
        RiBuf *clouds = ri_tile_map(ctx, ri_view(tiles), ri_view(palette),
            ri_view(cloud_map), 32, 64, 4);
        /* The bottom layer owns the source backdrop; clouds keep transparent
         * texels so their scrolling reveals the sky rather than black. */
        const uint32_t backdrop = ri_color(ri_get_u16(ctx, ri_view(palette), 0));
        for (size_t pixel = 0; pixel < MAP_SIZE * MAP_SIZE; ++pixel)
            if (!ri_pixel(sky, pixel)) ri_set_pixel(sky, pixel, backdrop);
        const unsigned sky_frames = variant == VARIANT_NIGHT ? 8u : 1u;
        for (unsigned row = 1u; row < sky_frames; ++row) {
            RiBuf *shifted = ri_buf_copy(ctx, palette->data, palette->size);
            const size_t start = 0xD5CC0u + (size_t)row * 32u;
            ri_fe_splice(ctx, shifted, 224u + 20u, 224u + 28u,
                ri_fe_rom_slice(ctx, start + 20u, start + 28u));
            RiBuf *frame = ri_tile_map(ctx, ri_view(tiles), ri_view(shifted),
                ri_view(sky_map), 32, 32, 4);
            for (size_t pixel = 0; pixel < MAP_SIZE * MAP_SIZE; ++pixel)
                if (!ri_pixel(frame, pixel)) ri_set_pixel(frame, pixel, backdrop);
            ri_put_buf(ctx, sky, frame);
            ri_buf_free(ctx, frame);
            ri_buf_free(ctx, shifted);
        }
        ri_section(ctx, 15, 0x20900u + variant * 2u, 1,
            ri_image_asset(ctx, MAP_SIZE, MAP_SIZE * sky_frames, sky, 0, 0));
        ri_section(ctx, 15, 0x20901u + variant * 2u, 1,
            ri_image_asset(ctx, MAP_SIZE, MAP_SIZE * 2u, clouds, 0, 0));
        ri_buf_free(ctx, clouds);
        ri_buf_free(ctx, sky);
        ri_buf_free(ctx, sky_map);
        ri_buf_free(ctx, palette);
    }
    uint8_t source_sha1[20];
    mksc_sha1(ctx->rom.data, ctx->rom.size, source_sha1);
    RiBuf *metadata = ri_buf(ctx);
    ri_put(ctx, metadata, "MKSCWUI1", 8);
    ri_u32(ctx, metadata, 1);
    ri_put(ctx, metadata, source_sha1, sizeof source_sha1);
    ri_section(ctx, 15, 0x209ffu, 1, metadata);
}

/* Packed vertical sprite strips retain source cell geometry and palette
 * identities; the renderer selects a row from its immutable tick. */
static RiBuf *title_palette_atlas(RiCtx *ctx,size_t cell,const RiBuf *tiles,
    const RiBuf *base_palette,const RiBuf *cycle,unsigned count,int highlight)
{
    RiBuf *pixels = ri_buf(ctx);
    uint32_t width = 0, height = 0;
    int ax = 0, ay = 0;
    for(unsigned row=0;row<count;++row){
        RiBuf *palette=ri_buf_copy(ctx,base_palette->data,base_palette->size);
        if(highlight){
            for(unsigned i=0;i<4u;++i){
                size_t source=0xcae70u+((i+((~row)&3u))&3u)*2u;
                ri_fe_splice(ctx,palette,(175u-i)*2u,(176u-i)*2u,ri_fe_rom_slice(ctx,source,source+2u));
            }
        }else ri_fe_splice(ctx,palette,304u,320u,(RiData){cycle->data+row*32u+16u,16u});
        RiBuf *image=ri_cell_image(ctx,ctx->rom,cell,ri_view(tiles),ri_view(palette));
        uint32_t w=ri_le16(image->data+12u),h=ri_le16(image->data+14u);
        if (!row)
        {
            width = w;
            height = h;
            ax = (int16_t)ri_le16(image->data + 16u);
            ay = (int16_t)ri_le16(image->data + 18u);
        }
        else if(w!=width||h!=height)ri_fail(ctx,"title palette atlas geometry changed");
        ri_put(ctx,pixels,image->data+32u,(size_t)w*h*4u);
    }
    return ri_image_asset(ctx,width,height*count,pixels,ax,ay);
}

void ri_fe_title_sections(RiCtx *ctx)
{
    static const size_t object_uploads[7][2] = {
        {0xCAE78, 0x1400}, {0xCB62C, 0x2900}, {0xCBA10, 0x3E00}, {0xCC1F0, 0x5300},
        {0xCC748, 0x6800}, {0xCCCEC, 0x6C00}, {0xCC8F4, 0x7400}};
    static const uint32_t label_tables[2][2] = {{TITLE_LABEL, 0xD8E4C}, {TITLE_SELECTED_LABEL, 0xD8E34}};
    static const uint32_t cells[6][2] = {{TITLE_FRAME, 0xD8B14}, {TITLE_UP, 0xD8BAC}, {TITLE_DOWN, 0xD8BA4},
                                         {TITLE_LINK_FRAME, 0xD8C44}, {0x20700u,0xD8DDCu},{0x20701u,0xD8B9Cu}};
    if (ctx->rom.size != 0x400000 || memcmp(ctx->rom.data + 0xAC, "AMKE", 4) != 0)
        ri_fail(ctx, "title import requires the US ROM");
    title_backgrounds(ctx, VARIANT_NORMAL, TITLE_BACKGROUND);
    title_backgrounds(ctx, VARIANT_SUNSET, TITLE_SUNSET_BACKGROUND);
    title_backgrounds(ctx, VARIANT_NIGHT, TITLE_NIGHT_BACKGROUND);
    RiBuf *object_tiles = ri_buf_zero(ctx, 0x8000);
    for (size_t i = 0; i < 7; ++i)
        ri_fe_place(ctx, object_tiles, object_uploads[i][1], ri_view(ri_fe_lz(ctx, object_uploads[i][0])));
    RiBuf *object_palette = ri_fe_lz(ctx, 0xCAC64);
    for (uint32_t index = 0; index < 3; ++index) {
        uint32_t option = ri_get_u8(ctx, ctx->rom, 0xD8E64 + index);
        for (size_t t = 0; t < 2; ++t) {
            size_t address = ri_fe_pointer(ctx, label_tables[t][1] + (size_t)option * 4);
            ri_section(ctx, 15, label_tables[t][0] + index, 1,
                       ri_cell_image(ctx, ctx->rom, address, ri_view(object_tiles), ri_view(object_palette)));
        }
    }
    RiBuf *cycle=ri_fe_lz(ctx,0xcadc4u);
    if(cycle->size!=512u)ri_fail(ctx,"title palette cycle size is invalid");
    static const size_t chrome_cells[4]={0xd8b14u,0xd8bacu,0xd8ba4u,0xd8c44u};
    for(unsigned i=0;i<4u;++i)
        ri_section(
            ctx, 15, 0x20800u + i, 1,
            title_palette_atlas(ctx, chrome_cells[i], object_tiles, object_palette, cycle, 16u, 0));
    for(unsigned i=0;i<3u;++i){
        unsigned option=ri_get_u8(ctx,ctx->rom,0xd8e64u+i);
        ri_section(ctx, 15, 0x20810u + i, 1,
                   title_palette_atlas(ctx, ri_fe_pointer(ctx, 0xd8e34u + option * 4u),
                                       object_tiles, object_palette, cycle, 4u, 1));
    }
    for (size_t i = 0; i < 6; ++i)
        ri_section(ctx, 15, cells[i][0], 1,
                   ri_cell_image(ctx, ctx->rom, cells[i][1], ri_view(object_tiles), ri_view(object_palette)));
}
