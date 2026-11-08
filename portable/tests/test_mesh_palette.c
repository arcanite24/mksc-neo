#include "mesh_palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            fprintf(stderr, "mesh palette:%d: %s\n", __LINE__, #value);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct
{
    uint8_t bytes[20000];
    size_t size, ambient_offset, lanes_offset, frames_offset, patches_offset, initial_offset;
    uint32_t ambient_size, data_size;
} Fixture;
static void put(uint8_t *p, uint32_t value)
{
    for (unsigned b = 0; b < 4; ++b)
        p[b] = (uint8_t)(value >> (b * 8));
}
static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void patch(uint8_t *p, unsigned target, unsigned offset, unsigned count, unsigned source)
{
    p[0] = (uint8_t)target;
    put16(p + 2, offset);
    put16(p + 4, count);
    put(p + 8, source);
}
static void fixture(Fixture *f, unsigned timer_frames, unsigned reverse, unsigned every_call)
{
    memset(f, 0, sizeof *f);
    const unsigned lanes = timer_frames ? 2 : 1, frames = 64 + timer_frames,
                   patches = 192 + timer_frames;
    f->ambient_offset = 304 + 2 * 32;
    const unsigned payload_offset = 32 + lanes * 32 + frames * 8 + (patches + 1) * 12;
    f->data_size = 256 + 64 * 12 + timer_frames * 2 + 4;
    f->ambient_size = payload_offset + f->data_size;
    f->size = f->ambient_offset + f->ambient_size + 8;
    const uint32_t header[] = {
        UINT32_C(0x3141504d), 1, (uint32_t)f->size, 41, 7, 2, f->ambient_size, 8, 0, 0, 0, 0};
    for (unsigned i = 0; i < 12; ++i)
        put(f->bytes + i * 4, header[i]);
    for (unsigned i = 0; i < 64; ++i)
    {
        put16(f->bytes + 48 + i * 2, 100 + i);
        put16(f->bytes + 176 + i * 2, 200 + i);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        uint8_t *b = f->bytes + 304 + i * 32;
        put(b, i ? 101 : 97);
        put(b + 4, 11 + i);
        put(b + 8, i);
        put(b + 12, 2);
        put(b + 16, 2);
        put(b + 20, i * 4);
        put(b + 24, 4);
    }
    uint8_t *a = f->bytes + f->ambient_offset;
    const uint32_t ah[] = {UINT32_C(0x31424d41), 1, lanes, frames, patches, 1, f->data_size, 0};
    for (unsigned i = 0; i < 8; ++i)
        put(a + i * 4, ah[i]);
    f->lanes_offset = f->ambient_offset + 32;
    uint8_t *sun = a + 32 + (reverse ? 32 : 0);
    sun[0] = PORTABLE_AMBIENT_PRE_PROFILE;
    sun[1] = PORTABLE_AMBIENT_ON_ENTRY;
    sun[2] = PORTABLE_AMBIENT_SUNSET;
    put16(sun + 6, 64);
    put16(sun + 8, 0);
    if (timer_frames)
    {
        uint8_t *timer = a + 32 + (reverse ? 0 : 32);
        timer[0] = PORTABLE_AMBIENT_POST_DRIVER;
        timer[1] = (uint8_t)(every_call ? PORTABLE_AMBIENT_EVERY_CALL : PORTABLE_AMBIENT_ON_ENTRY);
        timer[2] = PORTABLE_AMBIENT_TIMER;
        put16(timer + 4, 64);
        put16(timer + 6, timer_frames);
        put16(timer + 8, 0);
    }
    f->frames_offset = f->ambient_offset + 32 + lanes * 32;
    f->patches_offset = f->frames_offset + frames * 8;
    f->initial_offset = f->patches_offset + patches * 12;
    uint8_t *data = a + payload_offset;
    for (unsigned i = 0; i < 128; ++i)
        put16(data + i * 2, 500 + i);
    for (unsigned frame = 0; frame < 64; ++frame)
    {
        uint8_t *fr = f->bytes + f->frames_offset + frame * 8;
        put16(fr, 4);
        put16(fr + 2, frame * 3);
        fr[4] = 3;
        for (unsigned lane = 0; lane < 3; ++lane)
        {
            const unsigned at = 256 + frame * 12 + lane * 4;
            patch(f->bytes + f->patches_offset + (frame * 3 + lane) * 12,
                  lane == 2 ? PORTABLE_AMBIENT_SKY : PORTABLE_AMBIENT_ROAD, lane == 1 ? 48 : 0, 2,
                  at);
            put16(data + at, 1000 * (lane + 1) + frame);
            put16(data + at + 2, 1000 * (lane + 1) + frame + 1);
        }
    }
    for (unsigned frame = 0; frame < timer_frames; ++frame)
    {
        uint8_t *fr = f->bytes + f->frames_offset + (64 + frame) * 8;
        put16(fr, 2 + frame % 2);
        put16(fr + 2, 192 + frame);
        fr[4] = 1;
        const unsigned at = 256 + 64 * 12 + frame * 2;
        patch(f->bytes + f->patches_offset + (192 + frame) * 12, PORTABLE_AMBIENT_ROAD,
              timer_frames == 4 ? 60 + frame % 2 : 49, 1, at);
        put16(data + at, 4000 + frame);
    }
    patch(f->bytes + f->initial_offset, PORTABLE_AMBIENT_OBJECT, 3, 2, f->data_size - 4);
    put16(data + f->data_size - 4, 777);
    put16(data + f->data_size - 2, 778);
    const uint8_t indices[8] = {0, 1, 63, 255, 2, 3, 4, 255};
    memcpy(f->bytes + f->size - 8, indices, 8);
}
static PortableAmbientRules rules(const PortableMeshPaletteConfig *c)
{
    return (PortableAmbientRules){.lanes = c->lanes,
                                  .frames = c->frames,
                                  .patches = c->patches,
                                  .init_patches = c->initial,
                                  .data = c->data,
                                  .lane_count = c->lane_count,
                                  .frame_count = c->frame_count,
                                  .patch_count = c->patch_count,
                                  .init_patch_count = c->init_patch_count,
                                  .data_size = c->data_size,
                                  .object_seed_offset = c->object_seed_offset};
}
static int source_turns(void)
{
    for (unsigned reverse = 0; reverse < 2; ++reverse)
        for (unsigned every = 0; every < 2; ++every)
        {
            Fixture f;
            fixture(&f, 2, reverse, every);
            PortableMeshPaletteConfig *c = malloc(sizeof *c), *copy = malloc(sizeof *copy);
            CHECK(c && copy);
            char error[192];
            CHECK(portable_mesh_palette_decode(f.bytes, f.size, 41, c, error, sizeof error));
            CHECK(c->variant_count > 65 && c->variant_count <= 512 && !error[0]);
            *copy = *c;
            memset(c, 0xa5, sizeof *c);
            free(c);
            c = copy;
            PortableMeshPalette w, initial;
            CHECK(portable_mesh_palette_init(c, &w, error, sizeof error));
            initial = w;
            CHECK(w.ambient.snapshot.object[3] == 777 && w.ambient.snapshot.object[4] == 778);
            const PortableAmbientRules r = rules(c);
            PortableAmbientState source;
            CHECK(portable_ambient_init(&source, &r, &c->seed) == 1);
            unsigned timer_last = 0, sunset_last = 0;
            for (unsigned frame = 0; frame < 64; ++frame)
                for (unsigned counter = 0; counter < 4; ++counter)
                {
                    const PortableAmbientInputs in = {
                        .clock = frame * 4 + counter,
                        .sky_phase_before = (uint8_t)(frame < 32 ? 1 : 4),
                        .sky_phase_after = (uint8_t)(frame == 31   ? 2
                                                     : frame == 63 ? 5
                                                     : frame < 32  ? 1
                                                                   : 4),
                        .sky_elapsed_before = (int32_t)frame,
                        .sky_counter_before = (int32_t)counter};
                    const uint16_t old_sky = w.ambient.snapshot.sky[0];
                    CHECK(portable_ambient_tick(&source, &r, PORTABLE_AMBIENT_PRE_PROFILE, &in,
                                                NULL, 0) == 1);
                    CHECK(portable_ambient_tick(&source, &r, PORTABLE_AMBIENT_POST_DRIVER, &in,
                                                NULL, 0) == 1);
                    CHECK(portable_mesh_palette_config_valid(c));
                    CHECK(portable_mesh_palette_state_valid(c, &w) ==
                          portable_mesh_palette_valid(c, &w));
                    PortableMeshPalette composed;
                    CHECK(portable_mesh_palette_tick_validated_config(c, &composed, &w, &in, NULL,
                                                                      0, error, sizeof error));
                    CHECK(portable_mesh_palette_tick(c, &w, &w, &in, NULL, 0, error, sizeof error));
                    CHECK(!memcmp(&composed, &w, sizeof w));
                    CHECK(!memcmp(&w.ambient, &source, sizeof source));
                    CHECK(portable_mesh_palette_valid(c, &w));
                    CHECK(w.ambient.snapshot.road[32] == 132 && w.ambient.snapshot.sky[55] == 255);
                    if (counter < 3)
                        CHECK(w.ambient.snapshot.sky[0] == old_sky);
                    else
                    {
                        CHECK((unsigned)w.ambient.snapshot.sky[0] == 3000u + frame &&
                              (unsigned)w.ambient.snapshot.road[0] == 1000u + frame);
                        timer_last |= w.ambient.snapshot.road[49] >= 4000;
                        sunset_last |= (unsigned)w.ambient.snapshot.road[49] == 2001u + frame;
                    }
                    const uint8_t *pair = portable_mesh_palette_variant(c, w.variant);
                    CHECK(pair && ((unsigned)pair[0] | (unsigned)pair[1] << 8) ==
                                      w.ambient.snapshot.road[0]);
                }
            CHECK(timer_last && (every || sunset_last));
            const uint64_t hash = portable_mesh_palette_checksum(c, &w);
            CHECK(hash && hash != portable_mesh_palette_checksum(c, &initial));
            const PortableMeshPalette saved = w;
            PortableAmbientInputs paused = {.paused = 1};
            CHECK(portable_mesh_palette_tick(c, &w, &w, &paused, NULL, 0, error, sizeof error));
            CHECK(!memcmp(&w, &saved, sizeof w));
            CHECK(portable_mesh_palette_init(c, &w, error, sizeof error) &&
                  !memcmp(&w, &initial, sizeof w));
            PortableMeshPaletteBinding binding;
            const uint8_t *pixels = NULL;
            CHECK(portable_mesh_palette_binding(c, 0, &binding, &pixels));
            CHECK(binding.kind == 97 && binding.bank == 0 && binding.width == 2 &&
                  pixels[3] == 255);
            const PortableMeshPaletteBinding saved_binding = binding;
            const uint8_t *saved_pixels = pixels;
            CHECK(!portable_mesh_palette_binding(c, 2, &binding, &pixels));
            CHECK(!memcmp(&binding, &saved_binding, sizeof binding) && pixels == saved_pixels);
            CHECK(!portable_mesh_palette_variant(c, c->variant_count));
            free(c);
        }
    return 0;
}
static int malformed(void)
{
    Fixture base;
    fixture(&base, 2, 0, 0);
    PortableMeshPaletteConfig *c = malloc(sizeof *c), *saved = malloc(sizeof *saved);
    CHECK(c && saved);
    char error[192];
    CHECK(portable_mesh_palette_decode(base.bytes, base.size, 41, c, error, sizeof error));
    *saved = *c;
    for (unsigned bad = 0; bad < 24; ++bad)
    {
        Fixture f = base;
        switch (bad)
        {
        case 0:
            put(f.bytes, 0);
            break;
        case 1:
            put(f.bytes + 4, 2);
            break;
        case 2:
            put(f.bytes + 8, (uint32_t)f.size - 1);
            break;
        case 3:
            put(f.bytes + 12, 0);
            break;
        case 4:
            put(f.bytes + 16, 0);
            break;
        case 5:
            put(f.bytes + 20, 129);
            break;
        case 6:
            put(f.bytes + 24, f.ambient_size - 1);
            break;
        case 7:
            put(f.bytes + 28, PORTABLE_MESH_PALETTE_INDEX_BYTES + 1u);
            break;
        case 8:
            put(f.bytes + 32, 1);
            break;
        case 9:
            put(f.bytes + 304, 96);
            break;
        case 10:
            put(f.bytes + 308, 0);
            break;
        case 11:
            put(f.bytes + 312, 2);
            break;
        case 12:
            put(f.bytes + 316, 1025);
            break;
        case 13:
            put(f.bytes + 324, 1);
            break;
        case 14:
            put(f.bytes + 328, 3);
            break;
        case 15:
            put(f.bytes + 332, 1);
            break;
        case 16:
            put(f.bytes + 336, 97);
            put(f.bytes + 340, 10);
            break;
        case 17:
            f.bytes[f.size - 1] = 64;
            break;
        case 18:
            f.bytes[f.lanes_offset + 1] = 2;
            break;
        case 19:
            f.bytes[f.lanes_offset + 2] = PORTABLE_AMBIENT_STORM;
            break;
        case 20:
            f.bytes[f.lanes_offset + 32 + 3] = PORTABLE_AMBIENT_MANUAL;
            break;
        case 21:
            f.bytes[f.patches_offset] = PORTABLE_AMBIENT_OBJECT;
            break;
        case 22:
            f.bytes[f.initial_offset] = PORTABLE_AMBIENT_SKY_TILES;
            break;
        default:
            put16(f.bytes + f.frames_offset, 0);
            break;
        }
        CHECK(!portable_mesh_palette_decode(f.bytes, f.size, 41, c, error, sizeof error));
        CHECK(strstr(error, "profile=41 MPA1") && !memcmp(c, saved, sizeof *c));
    }
    CHECK(!portable_mesh_palette_decode(NULL, base.size, 41, c, error, sizeof error));
    CHECK(!portable_mesh_palette_decode(base.bytes, base.size - 1, 41, c, error, sizeof error));
    Fixture many;
    fixture(&many, 4, 0, 0);
    CHECK(!portable_mesh_palette_decode(many.bytes, many.size, 41, c, error, sizeof error));
    CHECK(strstr(error, "budget") && !memcmp(c, saved, sizeof *c));
    PortableMeshPalette w, out;
    CHECK(portable_mesh_palette_init(c, &w, error, sizeof error));
    memset(&out, 0xa5, sizeof out);
    const PortableMeshPalette before_out = out, snapshot = w;
    PortableAmbientInputs in = {.sky_counter_before = 4};
    CHECK(!portable_mesh_palette_tick(c, &out, &w, &in, NULL, 0, error, sizeof error));
    CHECK(!memcmp(&out, &before_out, sizeof out) && !memcmp(&w, &snapshot, sizeof w));
    /* Public entries revalidate caller-owned config on every call, even when
     * the preceding call admitted the same pointer/profile/revision. */
    const uint16_t duration = c->frames[0].duration;
    c->frames[0].duration = 0;
    CHECK(!portable_mesh_palette_valid(c, &w));
    CHECK(!portable_mesh_palette_tick(c, &out, &w, &(PortableAmbientInputs){0}, NULL, 0, error,
                                      sizeof error));
    CHECK(!memcmp(&out, &before_out, sizeof out) && !memcmp(&w, &snapshot, sizeof w));
    c->frames[0].duration = duration;
    CHECK(portable_mesh_palette_valid(c, &w));
    PortableMeshPalette corrupt = w;
    corrupt.ambient.snapshot.road[0] ^= 1u;
    CHECK(!portable_mesh_palette_state_valid(c, &corrupt));
    CHECK(!portable_mesh_palette_valid(c, &corrupt));
    w.variant = UINT32_MAX;
    CHECK(!portable_mesh_palette_tick(c, &out, &w, &(PortableAmbientInputs){0}, NULL, 0, error,
                                      sizeof error));
    CHECK(!memcmp(&out, &before_out, sizeof out));
    free(saved);
    free(c);
    return 0;
}
static int neutral(void)
{
    PortableMeshPaletteConfig *c = calloc(1, sizeof *c);
    CHECK(c);
    PortableMeshPalette w;
    CHECK(portable_mesh_palette_init(c, &w, NULL, 0));
    const uint64_t seed = UINT64_C(0x123456789abcdef0);
    CHECK(portable_mesh_palette_hash(&w, seed) == seed);
    const PortableMeshPalette before = w;
    CHECK(portable_mesh_palette_tick(c, &w, &w, NULL, NULL, 0, NULL, 0));
    CHECK(!memcmp(&w, &before, sizeof w));
    CHECK(!portable_mesh_palette_variant(c, 0));
    free(c);
    return 0;
}
int main(void)
{
    CHECK(!source_turns() && !malformed() && !neutral());
    puts(
        "mesh palette: PASS source64 frames, timer overlap, immutable variants, copied config, bounds and rollback");
    return 0;
}
