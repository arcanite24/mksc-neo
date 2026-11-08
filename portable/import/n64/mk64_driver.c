#include "mk64_internal.h"
#include <string.h>

/* The eight racers as directional sprites (kind 113 KSP1) and selectable
 * 3D-only drivers (kind 110 DRV3) with a character-select portrait.
 *
 * The kart_textures segment (mk64.ld, ROM 0x145470) holds one file per
 * character in link order: 321 MIO0 frames of 64x64 CI8 packed back to back
 * (4-aligned), then 289 frames x 4 wheel phases of 64 RGBA16 wheel colours
 * (TLUT indices 192..255), then the 192 shared kart colours, padded to 16.
 * kart_dma.c gKartTextureTable0 names nine slope groups of 35 frames: group
 * g is frames 21g..21g+20 (seen from behind, 2.86 degrees apart) then 14
 * frames 9 degrees apart that groups share (rear_frames). player_controller.c
 * func_80029200 picks the group from the kart's pitch (slopeAccel, nose
 * down positive) and func_8002934C a frame from the view angle relative to
 * the kart, `angle` < 0x2880 ? angle / 0x208 : angle / 0x666 + 15,
 * mirroring past half a turn. Shell, bomb and star hits draw
 * gKartTextureTumbles (frames 289..320, no wheel colours of their own) by
 * an 8.8 counter that effects.c func_8008C62C steps 0xA0 a tick, twice.
 * (n64decomp/mk64 58cfcb0) */
#define KART_TEXTURES 0x145470u
#define KART_TEXTURES_END 0x63E278u
#define KART_FILE_FRAMES 321u
#define WHEEL_FRAMES 289u
#define SHARED_COLORS 192u
#define FRAME_SIDE 64u
#define GROUP_FRAMES 35u
#define FINE_FRAMES 21u
#define GROUPS 9u
#define TUMBLE_FIRST 289u
#define TUMBLE_FRAMES 32u
#define TUMBLE_CYCLES 2u
/* Frames a set holds: nine groups' fine frames, five shared rear sets, the tumble. */
#define SET_FRAMES (GROUPS * FINE_FRAMES + 5u * 14u + TUMBLE_FRAMES)

typedef struct Mk64Racer {
    const char *name;
    uint32_t donor;    /* Super Circuit driver lending voice and stats */
    uint32_t face;     /* character-select face 00: 64x64 RGBA16 MIO0 */
} Mk64Racer;
/* In kart_textures link order. */
static const Mk64Racer racers[8] = {
    {"Luigi 64", 2, 0x737494}, {"Mario 64", 1, 0x729A30}, {"Yoshi 64", 5, 0x75F1C4},
    {"Peach 64", 3, 0x744674}, {"Wario 64", 7, 0x779F00}, {"Toad 64", 4, 0x754900},
    {"DK 64", 6, 0x76A4EC},    {"Bowser 64", 8, 0x78D210},
};

static uint32_t argb16(uint32_t v)
{
    const uint32_t r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31;
    return (v & 1 ? 0xFF000000u : 0) | ((r << 3) | (r >> 2)) << 16 | ((g << 3) | (g >> 2)) << 8 | ((b << 3) | (b >> 2));
}

/* The group frame seen from half-circle view bin `bin` (512 angle units). */
static uint32_t group_frame(uint32_t bin)
{
    const uint32_t angle = bin * 512u + 256u;
    const uint32_t frame = angle < 0x2880u ? angle / 0x208u : angle / 0x666u + 15u;
    return frame < GROUP_FRAMES ? frame : GROUP_FRAMES - 1u;
}

/* First of the 14 rear frames each slope group shares (DECLARE_KART_GROUP). */
static const uint16_t rear_frames[GROUPS] = {195, 195, 215, 235, 235, 235, 255, 275, 275};
static const uint16_t rear_sets[5] = {195, 215, 235, 255, 275};

/* Source frame of a group's frame. */
static uint32_t source_frame(uint32_t group, uint32_t frame)
{
    return frame < FINE_FRAMES ? group * FINE_FRAMES + frame : rear_frames[group] + (frame - FINE_FRAMES);
}

/* KSP1 frame of a source frame: fine frames, then the rear sets, then the tumble. */
static uint32_t set_frame(uint32_t source)
{
    if (source < GROUPS * FINE_FRAMES) return source;
    if (source >= TUMBLE_FIRST) return GROUPS * FINE_FRAMES + 5u * 14u + (source - TUMBLE_FIRST);
    for (uint32_t set = 0; set < 5u; ++set)
        if (source >= rear_sets[set] && source < rear_sets[set] + 14u)
            return GROUPS * FINE_FRAMES + set * 14u + (source - rear_sets[set]);
    return UINT32_MAX;
}

static uint32_t set_source(uint32_t frame)
{
    if (frame < GROUPS * FINE_FRAMES) return frame;
    frame -= GROUPS * FINE_FRAMES;
    return frame < 5u * 14u ? rear_sets[frame / 14u] + frame % 14u : TUMBLE_FIRST + (frame - 5u * 14u);
}

/* Lowest nose-up pitch of each group (binary angle), from func_80029200:
 * group 8 - g for slopeAccel bands -10, -7, -3, -2, 2, 3, 7, 10 degrees. */
static const int16_t group_pitch[GROUPS] = {-32768, -1819, -1273, -545, -363, 364, 546, 1274, 1820};

/* Star (func_800235AC): PRIM colours added to the art two ticks each. */
static const uint32_t star_colours[4] = {0x000070u, 0x707000u, 0x700000u, 0x007000u};
#define STAR_TICKS 2u
#define GHOST_ALPHA 0x70u /* render_ghost */
#define BOO_ALPHA 0x60u   /* ALPHA_BOO_EFFECT */

static void portrait(RiCtx *ctx, const Mk64Racer *racer, uint32_t id)
{
    if (racer->face >= ctx->rom.size) ri_fail(ctx, "MK64 %s: portrait outside the cartridge", racer->name);
    RiBuf *face = mk64_mio0(ctx, ri_span(ctx, ctx->rom, racer->face, ctx->rom.size - racer->face));
    if (face->size != FRAME_SIDE * FRAME_SIDE * 2u) ri_fail(ctx, "MK64 %s: portrait is not 64x64 RGBA16", racer->name);
    RiBuf *pixels = ri_buf_zero(ctx, FRAME_SIDE * FRAME_SIDE * 4u);
    for (size_t i = 0; i < FRAME_SIDE * FRAME_SIDE; ++i) ri_set_pixel(pixels, i, argb16(mk64_be16(face->data + i * 2)));
    RiBuf *image = ri_image_asset(ctx, FRAME_SIDE, FRAME_SIDE, pixels, 0, 0);
    RiBuf *material = ri_buf(ctx);
    ri_put(ctx, material, "MTL1", 4);
    ri_words(ctx, material, 3, 0xFFFFFFFFu, 0u, (uint32_t)image->size);
    ri_put_buf(ctx, material, image);
    ri_section(ctx, 97, id, 1, material);
    ri_buf_free(ctx, image); ri_buf_free(ctx, pixels); ri_buf_free(ctx, face);
}

void mk64_drivers(RiCtx *ctx)
{
    size_t at = KART_TEXTURES;
    for (uint32_t index = 0; index < 8; ++index) {
        const Mk64Racer *racer = &racers[index];
        size_t frames[KART_FILE_FRAMES];
        for (uint32_t f = 0; f < KART_FILE_FRAMES; ++f) { frames[f] = at; at = mk64_mio0_next(ctx, at); }
        const size_t wheels = at, palette = wheels + (size_t)WHEEL_FRAMES * 4u * 0x80u;
        if (palette + SHARED_COLORS * 2u > KART_TEXTURES_END)
            ri_fail(ctx, "MK64 %s: kart frames overrun the kart_textures segment", racer->name);
        at = (palette + SHARED_COLORS * 2u + 15u) & ~(size_t)15u;

        RiBuf *out = ri_buf(ctx);
        ri_put(ctx, out, "KSP1", 4);
        ri_u16(ctx, out, FRAME_SIDE); ri_u16(ctx, out, FRAME_SIDE);
        /* MK64 draws every racer's 64-texel frame on an 18-unit quad
         * (D_800E51D0) at gCharacterSize 0.75: 13.5 source units. Courses
         * shrink karts to the source sphere (mksc:kart-scale, sphere / 0.5 m)
         * and the host draws kart art at 0.72 of its size, so the frame is
         * stored that much larger and lands at 13.5 units on the road. */
        const uint64_t frame_tenths = 135u, sphere_tenths = MK64_KART_SPHERE_TENTHS;
        ri_words(ctx, out, 6, SET_FRAMES, 4u, SHARED_COLORS,
                 (uint32_t)(65536u * frame_tenths * 100u / (FRAME_SIDE * sphere_tenths * 2u * 72u)),
                 (uint32_t)(65536 * 3 / 10), 0u);
        /* Version 2: nine pitch rows, the tumble twice per hit, filtered
         * like the N64 (render_kart's G_RM_AA_ZB_TEX_EDGE, bilinear). */
        out->data[out->size - 4] = GROUPS;
        out->data[out->size - 3] = TUMBLE_FRAMES;
        out->data[out->size - 2] = TUMBLE_CYCLES;
        out->data[out->size - 1] = 1u;
        for (uint32_t i = 0; i < 4u; ++i) ri_u32(ctx, out, star_colours[i]);
        ri_u8(ctx, out, STAR_TICKS); ri_u8(ctx, out, GHOST_ALPHA); ri_u8(ctx, out, BOO_ALPHA);
        ri_put_zero(ctx, out, 13);
        for (uint32_t group = 0; group < GROUPS; ++group) ri_u16(ctx, out, (uint16_t)group_pitch[group]);
        ri_put_zero(ctx, out, (4u - GROUPS * 2u % 4u) % 4u);
        for (uint32_t group = 0; group < GROUPS; ++group)
            for (uint32_t bin = 0; bin < 64; ++bin)
                ri_u16(ctx, out, (uint16_t)set_frame(source_frame(group, group_frame(bin))));
        for (uint32_t i = 0; i < TUMBLE_FRAMES; ++i) ri_u16(ctx, out, (uint16_t)set_frame(TUMBLE_FIRST + i));
        for (uint32_t i = 0; i < SHARED_COLORS; ++i) ri_u32(ctx, out, argb16(mk64_be16(ri_at(ctx, ctx->rom, palette + i * 2u, 2))));
        for (uint32_t frame = 0; frame < SET_FRAMES; ++frame) {
            const uint32_t source = set_source(frame);
            RiBuf *texels = mk64_mio0(ctx, ri_span(ctx, ctx->rom, frames[source], ctx->rom.size - frames[source]));
            if (texels->size != FRAME_SIDE * FRAME_SIDE) ri_fail(ctx, "MK64 %s: kart frame %u is not 64x64 CI8", racer->name, source);
            /* Stored frames turn the kart's nose to the left of the image as
             * the view swings off its tail, like the stock racers' unmirrored
             * view slots, so they copy across unflipped. */
            ri_put(ctx, out, texels->data, FRAME_SIDE * FRAME_SIDE);
            ri_buf_free(ctx, texels);
            /* Tumble frames show the wheel colours last loaded: the rear view's. */
            const uint32_t wheel = source < WHEEL_FRAMES ? source : source_frame(4u, 0u);
            for (uint32_t phase = 0; phase < 4; ++phase)
                for (uint32_t i = 0; i < 256u - SHARED_COLORS; ++i)
                    ri_u32(ctx, out, argb16(mk64_be16(ri_at(ctx, ctx->rom, wheels + (size_t)wheel * 0x200u + phase * 0x80u + i * 2u, 2))));
        }
        const uint32_t id = MK64_DRIVER_ID(racer->donor - 1u);
        ri_section(ctx, 113, id, 2, out);

        portrait(ctx, racer, MK64_PORTRAIT_ID(racer->donor - 1u));
        RiBuf *row = ri_buf(ctx);
        ri_put(ctx, row, "DRV3", 4);
        /* The donor's stock stats already set MK64's weight classes. */
        ri_words(ctx, row, 7, racer->donor, 1u, 65536u, 65536u, 65536u, 65536u, MK64_PORTRAIT_ID(racer->donor - 1u));
        char name[32] = {0};
        strncpy(name, racer->name, sizeof name - 1);
        ri_put(ctx, row, name, sizeof name);
        ri_section(ctx, 110, id, 1, row);
    }
}
