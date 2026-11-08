#include "mesh_driver_art.h"
#include "character_asset.h"
#include "character_view_asset.h"
#include "content_registry.h"
#include "driver_projection_asset.h"
#include "driver_sprite_asset.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    CELL = 64,
    PIXEL_BYTES = CELL * CELL * 4
};
typedef struct
{
    PortableRenderVertex vertices[2][4];
    PortableRenderMaterial material, blend_material;
    PortableRenderRun run;
    PortableRenderMesh meshes[2], blend[2]; /* cutout and translucent, by mirror */
} DriverFrame;
struct PortableMeshDriverArt
{
    /* frame_count per phase; KSP1 sprites keep one copy per wheel phase. */
    DriverFrame *frames;
    DriverFrame *palette_frames[3];
    double *ground_anchors;
    double (*opaque_bounds)[6];
    uint16_t slots[PORTABLE_DRIVER_SPRITE_ROWS][PORTABLE_CHARACTER_VIEW_SLOT_COUNT];
    uint8_t *pixels;
    uint32_t frame_count, phase_count, phase, row, tumble_frame;
    double phase_distance;
    PortableCharacterViewRules view_rules;
    uint8_t has_view_rules;
    /* KSP1: the owned section and its view; frames decode on first use. */
    uint8_t *sprite_bytes;
    PortableDriverSprites sprites;
    int32_t frame_offset[2]; /* frame quad origin, texels */
    double frame_scale;
};

static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

static int fail(char *error, size_t capacity, uint32_t driver, const char *state)
{
    if (error && capacity)
        snprintf(error, capacity, "3D driver=%u source art: %s", (unsigned)driver, state);
    return 0;
}

static void write_pixel(uint8_t *out, uint32_t color)
{
    for (unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(color >> (i * 8));
}

static uint32_t rgb(uint16_t color)
{
    const uint32_t r = color & 31u, g = (color >> 5u) & 31u, b = (color >> 10u) & 31u;
    return UINT32_C(0xff000000) | (((r << 3u) | (r >> 2u)) << 16u) |
           (((g << 3u) | (g >> 2u)) << 8u) | ((b << 3u) | (b >> 2u));
}

static void frame_init(DriverFrame *frame, uint8_t *pixels, int32_t offset_x, int32_t offset_y, uint32_t width,
                       uint32_t height, double scale)
{
    const double left = offset_x * scale, right = (offset_x + (int32_t)width) * scale;
    const double top = -offset_y * scale, bottom = -(offset_y + (int32_t)height) * scale;
    frame->material = (PortableRenderMaterial){.argb = UINT32_MAX,
                                               .flags = 4u | 16u,
                                               .alpha_cutoff_q16 = 1,
                                               .texture = {.pixels = pixels,
                                                           .width = width,
                                                           .height = height,
                                                           .anchor_x = offset_x,
                                                           .anchor_y = offset_y}};
    /* Translucent copy (Boo, Time Trial ghosts) over the finished depth. */
    frame->blend_material = frame->material;
    frame->blend_material.flags = 4u | PORTABLE_RENDER_MATERIAL_ALPHA_BLEND;
    frame->run = (PortableRenderRun){0, 6, 0};
    for (unsigned mirror = 0; mirror < 2; ++mirror)
    {
        const double u0 = mirror ? 1 : 0, u1 = mirror ? 0 : 1;
        frame->vertices[mirror][0] =
            (PortableRenderVertex){.position = {left, 0, bottom}, .uv = {u0, 1}};
        frame->vertices[mirror][1] =
            (PortableRenderVertex){.position = {right, 0, bottom}, .uv = {u1, 1}};
        frame->vertices[mirror][2] =
            (PortableRenderVertex){.position = {right, 0, top}, .uv = {u1, 0}};
        frame->vertices[mirror][3] =
            (PortableRenderVertex){.position = {left, 0, top}, .uv = {u0, 0}};
        frame->meshes[mirror] = (PortableRenderMesh){.vertices = frame->vertices[mirror],
                                                     .indices = indices,
                                                     .materials = &frame->material,
                                                     .runs = &frame->run,
                                                     .vertex_count = 4,
                                                     .index_count = 6,
                                                     .material_count = 1,
                                                     .run_count = 1};
        frame->blend[mirror] = frame->meshes[mirror];
        frame->blend[mirror].materials = &frame->blend_material;
    }
}

void portable_mesh_driver_art_close(PortableMeshDriverArt *owner)
{
    if (owner)
    {
        for (unsigned bank = 0; bank < 3; ++bank)
            free(owner->palette_frames[bank]);
        if (owner->sprite_bytes && owner->frames)
            for (size_t i = 0; i < (size_t)owner->frame_count * owner->phase_count; ++i)
                free((void *)owner->frames[i].material.texture.pixels);
        free(owner->sprite_bytes);
        free(owner->frames);
        free(owner->ground_anchors);
        free(owner->opaque_bounds);
        free(owner->pixels);
        free(owner);
    }
}

/* Filtered frames: each clear texel next to opaque ones takes their mean
 * colour (still clear), twice, so the filter's reach shows no dark fringe. */
static void bleed(uint8_t *pixels, uint32_t width, uint32_t height)
{
    for (unsigned pass = 0; pass < 2u; ++pass)
    {
        uint8_t *known = calloc((size_t)width * height, 1u);
        if (!known) return;
        for (size_t i = 0; i < (size_t)width * height; ++i)
            known[i] = pixels[i * 4u + 3u] || pixels[i * 4u] || pixels[i * 4u + 1u] || pixels[i * 4u + 2u];
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                uint8_t *out = pixels + ((size_t)y * width + x) * 4u;
                if (known[(size_t)y * width + x]) continue;
                unsigned sum[3] = {0, 0, 0}, count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = (int)x + dx, ny = (int)y + dy;
                        if (nx < 0 || ny < 0 || nx >= (int)width || ny >= (int)height ||
                            !known[(size_t)ny * width + (size_t)nx]) continue;
                        const uint8_t *in = pixels + ((size_t)ny * width + (size_t)nx) * 4u;
                        for (unsigned c = 0; c < 3u; ++c) sum[c] += in[c];
                        ++count;
                    }
                if (count)
                    for (unsigned c = 0; c < 3u; ++c) out[c] = (uint8_t)(sum[c] / count);
            }
        free(known);
    }
}

/* A KSP1 frame in one wheel phase, decoded the first time it is drawn. */
static const DriverFrame *sprite_frame(const PortableMeshDriverArt *owner, uint32_t phase, uint32_t frame)
{
    DriverFrame *out = &owner->frames[(size_t)phase * owner->frame_count + frame];
    if (out->material.texture.pixels) return out;
    const PortableDriverSprites *sprites = &owner->sprites;
    uint8_t *pixels = malloc((size_t)sprites->width * sprites->height * 4u);
    if (!pixels) return NULL;
    for (uint32_t y = 0; y < sprites->height; ++y)
        for (uint32_t x = 0; x < sprites->width; ++x)
        {
            uint32_t color = portable_driver_sprites_texel(sprites, frame, phase, x, y);
            color = color >> 24 < 128u ? 0 : color | UINT32_C(0xff000000);
            write_pixel(pixels + ((size_t)y * sprites->width + x) * 4u, color);
        }
    const int filtered = (sprites->flags & PORTABLE_DRIVER_SPRITE_FILTERED) != 0;
    if (filtered) bleed(pixels, sprites->width, sprites->height);
    frame_init(out, pixels, owner->frame_offset[0], owner->frame_offset[1], sprites->width, sprites->height,
               owner->frame_scale);
    if (filtered)
    {
        out->material.filter = out->blend_material.filter = PORTABLE_RENDER_FILTER_THREE_POINT;
        out->material.alpha_cutoff_q16 = 32768;
    }
    return out;
}

/* A KSP1 sprite driver. Frames stand on their lowest opaque row, centred
 * across; their pixels decode on first use (MK64 sets hold ~300 frames in
 * four wheel phases, of which a race shows a few). */
static int open_sprites(const PortableAssetSectionView *section, uint32_t driver_id, const PortableAssetPackView *pack,
                        PortableMeshDriverArt **owner, char *error, size_t capacity)
{
    PortableMeshDriverArt *next = calloc(1, sizeof *next);
    if (next) next->sprite_bytes = malloc(section->size);
    PortableAssetSectionView copy = *section;
    if (next && next->sprite_bytes)
    {
        memcpy(next->sprite_bytes, section->bytes, section->size);
        copy.bytes = next->sprite_bytes;
    }
    if (!next || !next->sprite_bytes || !portable_driver_sprites_open(&copy, &next->sprites))
    {
        portable_mesh_driver_art_close(next);
        return fail(error, capacity, driver_id, next ? "KSP1 invalid" : "sprite allocation failed");
    }
    const PortableDriverSprites *sprites = &next->sprites;
    const uint32_t count = sprites->frame_count, phases = sprites->phase_count;
    next->frames = calloc((size_t)count * phases, sizeof *next->frames);
    next->ground_anchors = calloc(count, sizeof *next->ground_anchors);
    next->opaque_bounds = calloc(count, sizeof *next->opaque_bounds);
    if (!next->frames || !next->ground_anchors || !next->opaque_bounds)
    {
        portable_mesh_driver_art_close(next);
        return fail(error, capacity, driver_id, "sprite allocation failed");
    }
    PortableCharacterViewRules view_rules;
    const PortableCharacterViewRules *view_rules_out = NULL;
    if (!portable_character_view_asset_load(pack, &view_rules, &view_rules_out))
    {
        portable_mesh_driver_art_close(next);
        return fail(error, capacity, driver_id, "DVR1 invalid");
    }
    next->has_view_rules = (uint8_t)(view_rules_out != NULL);
    if (view_rules_out) next->view_rules = view_rules;
    next->frame_count = count;
    next->phase_count = phases;
    next->phase_distance = sprites->phase_distance_q16 / 65536.0;
    memcpy(next->slots, sprites->slots, sizeof next->slots);
    next->frame_scale = sprites->metres_per_pixel_q16 / 65536.0;
    next->frame_offset[0] = -(int32_t)sprites->width / 2;
    next->frame_offset[1] = -(int32_t)sprites->height;
    const double scale = next->frame_scale;
    const int32_t offset_x = next->frame_offset[0], offset_y = next->frame_offset[1];
    for (uint32_t frame = 0; frame < count; ++frame)
    {
        int first_x = (int)sprites->width, last_x = -1, first_y = (int)sprites->height, last_y = -1;
        for (uint32_t y = 0; y < sprites->height; ++y)
            for (uint32_t x = 0; x < sprites->width; ++x)
                if (portable_driver_sprites_texel(sprites, frame, 0, x, y) >> 24 >= 128u)
                {
                    if ((int)x < first_x) first_x = (int)x;
                    if ((int)x > last_x) last_x = (int)x;
                    if ((int)y < first_y) first_y = (int)y;
                    last_y = (int)y;
                }
        /* Quad corners exist before the pixels do (bounds, mirroring). */
        for (uint32_t phase = 0; phase < phases; ++phase)
            frame_init(&next->frames[(size_t)phase * count + frame], NULL, offset_x, offset_y, sprites->width,
                       sprites->height, scale);
        if (last_y >= 0)
        {
            next->ground_anchors[frame] = -(offset_y + last_y + 1) * scale;
            double *bounds = next->opaque_bounds[frame];
            bounds[0] = (offset_x + first_x) * scale;
            bounds[2] = next->ground_anchors[frame];
            bounds[3] = (offset_x + last_x + 1) * scale;
            bounds[5] = -(offset_y + first_y) * scale;
        }
    }
    portable_mesh_driver_art_close(*owner);
    *owner = next;
    return 1;
}

void portable_mesh_driver_art_set_wheel(PortableMeshDriverArt *owner, double distance)
{
    if (!owner) return;
    owner->phase = 0;
    if (owner->phase_count > 1u && owner->phase_distance > 0 && isfinite(distance))
    {
        const double step = floor(fabs(distance) / owner->phase_distance);
        owner->phase = (uint32_t)fmod(step, (double)owner->phase_count);
    }
}

int portable_mesh_driver_art_set_pose(PortableMeshDriverArt *owner, double pitch, double tumble)
{
    if (!owner) return 0;
    owner->row = 0;
    owner->tumble_frame = UINT32_MAX;
    if (!owner->sprite_bytes) return 0;
    const PortableDriverSprites *sprites = &owner->sprites;
    if (isfinite(pitch))
    {
        const double turns = fmax(-.5, fmin(.5, pitch / (2 * 3.14159265358979323846)));
        owner->row = portable_driver_sprites_row(sprites, (int32_t)lround(turns * 65536.0));
    }
    if (sprites->tumble_count && isfinite(tumble) && tumble >= 0 && tumble < 1)
    {
        const uint32_t step = (uint32_t)floor(tumble * sprites->tumble_cycles * sprites->tumble_count);
        owner->tumble_frame = sprites->tumble[step % sprites->tumble_count];
    }
    return owner->tumble_frame != UINT32_MAX;
}

const PortableDriverSprites *portable_mesh_driver_art_sprites(const PortableMeshDriverArt *owner)
{
    return owner && owner->sprite_bytes ? &owner->sprites : NULL;
}

static const DriverFrame *frame_at(const PortableMeshDriverArt *owner, uint32_t frame)
{
    if (owner->sprite_bytes) return sprite_frame(owner, owner->phase, frame);
    return &owner->frames[(size_t)owner->phase * owner->frame_count + frame];
}

int portable_mesh_driver_art_open(const PortableAssetPackView *pack, uint32_t driver_id,
                                  PortableMeshDriverArt **owner, char *error, size_t capacity)
{
    PortableAssetSectionView sprites;
    if (pack && owner &&
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_DRIVER_SPRITES, driver_id, &sprites) ==
            PORTABLE_ASSET_PACK_OK)
        return open_sprites(&sprites, driver_id, pack, owner, error, capacity);
    PortableCharacterContent character;
    PortableAssetSectionView graphic, projection;
    PortableDriverProjectionRules geometry;
    if (!pack || !owner ||
        portable_content_registry_character(pack, driver_id, &character) != PORTABLE_CONTENT_OK)
        return fail(error, capacity, driver_id, "manifest missing/invalid");
    if (portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,
                                         character.graphics, &graphic) != PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES, 1u,
                                         &projection) != PORTABLE_ASSET_PACK_OK ||
        !portable_driver_projection_asset_open(&projection, &geometry))
        return fail(error, capacity, driver_id, "graphics/DPR1 missing or invalid");
    PortableCharacterViewRules view_rules;
    const PortableCharacterViewRules *view_rules_out = NULL;
    if (!portable_character_view_asset_load(pack, &view_rules, &view_rules_out))
        return fail(error, capacity, driver_id, "DVR1 invalid");
    PortableCharacterPackedStorage *decoded = malloc(sizeof *decoded);
    PortableMeshDriverArt *next = calloc(1, sizeof *next);
    if (!decoded || !next)
    {
        free(decoded);
        portable_mesh_driver_art_close(next);
        return fail(error, capacity, driver_id, "allocation failed");
    }
    next->has_view_rules = (uint8_t)(view_rules_out != NULL);
    if (view_rules_out) next->view_rules = view_rules;
    PortableCharacterAssetView view;
    if (portable_character_asset_open_packed(&graphic, decoded, &view) !=
            PORTABLE_CHARACTER_ASSET_OK ||
        view.width != CELL || view.height != CELL || !view.direction_tiles || !view.palette ||
        view.palette_count < 16 || !view.direction_frame_count ||
        view.direction_frame_count > PORTABLE_CHARACTER_FULL_FRAME_COUNT ||
        view.direction_tile_bytes < view.direction_frame_count * PORTABLE_CHARACTER_FRAME_BYTES ||
        (view.view_frame_slots && view.view_frame_slot_count != 64))
    {
        free(decoded);
        portable_mesh_driver_art_close(next);
        return fail(error, capacity, driver_id, "character graphics payload unsupported/invalid");
    }
    next->frame_count = (uint32_t)view.direction_frame_count;
    next->phase_count = 1u;
    next->frames = calloc(next->frame_count, sizeof *next->frames);
    next->ground_anchors = calloc(next->frame_count, sizeof *next->ground_anchors);
    next->opaque_bounds = calloc(next->frame_count, sizeof *next->opaque_bounds);
    unsigned optional_banks = view.palette_banks && view.palette_bank_count > 5
                                  ? (unsigned)view.palette_bank_count - 5u : 0u;
    if (optional_banks > 3) optional_banks = 3;
    const unsigned banks = 1u + optional_banks;
    next->pixels = malloc((size_t)banks * next->frame_count * PIXEL_BYTES);
    int allocated = next->pixels != NULL && next->frames && next->ground_anchors && next->opaque_bounds;
    for (unsigned bank = 0; bank < optional_banks; ++bank)
    {
        next->palette_frames[bank] = calloc(next->frame_count, sizeof *next->palette_frames[bank]);
        allocated &= next->palette_frames[bank] != NULL;
    }
    if (!allocated)
    {
        free(decoded);
        portable_mesh_driver_art_close(next);
        return fail(error, capacity, driver_id, "pixel allocation failed");
    }
    if (view.view_frame_slots)
        for (unsigned i = 0; i < PORTABLE_CHARACTER_VIEW_SLOT_COUNT; ++i)
            next->slots[0][i] = view.view_frame_slots[i];
    /* Decode once. Index0 is transparent regardless of palette0, while a
     * nonzero index containing RGB555 zero is intentionally opaque black. */
    for (unsigned bank = 0; bank < banks; ++bank)
    {
        uint16_t palette[16];
        if (!portable_character_asset_palette(&view, (uint8_t)(bank ? bank + 4 : 0), palette))
        {
            free(decoded);
            portable_mesh_driver_art_close(next);
            return fail(error, capacity, driver_id, "optional palette invalid");
        }
        for (uint32_t slot = 0; slot < next->frame_count; ++slot)
        {
            int last_opaque_y = -1,first_opaque_y = CELL;
            int first_opaque_x = CELL,last_opaque_x = -1;
            uint8_t *pixels = next->pixels + ((size_t)bank * next->frame_count + slot) * PIXEL_BYTES;
            const uint8_t *tiles = view.direction_tiles + (size_t)slot * PORTABLE_CHARACTER_FRAME_BYTES;
            for (unsigned y = 0; y < CELL; ++y)
                for (unsigned x = 0; x < CELL; ++x)
                {
                    const size_t offset =
                        ((y / 8u) * 8u + x / 8u) * 32u + (y & 7u) * 4u + (x & 7u) / 2u;
                    const unsigned color = (tiles[offset] >> ((x & 1u) * 4u)) & 15u;
                    if (!bank && color)
                    {
                        last_opaque_y = (int)y;
                        if ((int)y<first_opaque_y) first_opaque_y=(int)y;
                        if ((int)x<first_opaque_x) first_opaque_x=(int)x;
                        if ((int)x>last_opaque_x) last_opaque_x=(int)x;
                    }
                    write_pixel(pixels + ((size_t)y * CELL + x) * 4u,
                                color ? rgb(palette[color]) : 0);
                }
            DriverFrame *frame = bank ? &next->palette_frames[bank - 1][slot] : &next->frames[slot];
            frame_init(frame, pixels, geometry.rows[0].offset_x, geometry.rows[0].offset_y, CELL, CELL, .05);
            if (!bank)
            {
                next->ground_anchors[slot] =
                    last_opaque_y < 0 ? 0 : -(geometry.rows[0].offset_y + last_opaque_y + 1) * .05;
                if (last_opaque_y>=0)
                {
                    double *bounds=next->opaque_bounds[slot];
                    bounds[0]=(geometry.rows[0].offset_x+first_opaque_x)*.05;
                    bounds[2]=next->ground_anchors[slot];
                    bounds[3]=(geometry.rows[0].offset_x+last_opaque_x+1)*.05;
                    bounds[5]=-(geometry.rows[0].offset_y+first_opaque_y)*.05;
                }
            }
        }
    }
    free(decoded);
    portable_mesh_driver_art_close(*owner);
    *owner = next;
    return 1;
}

const PortableRenderMesh *portable_mesh_driver_art_frame(const PortableMeshDriverArt *owner,
                                                         uint32_t frame, int mirror)
{
    if (!owner || frame >= owner->frame_count || (mirror != 0 && mirror != 1))
        return NULL;
    const DriverFrame *out = frame_at(owner, frame);
    return out ? &out->meshes[mirror] : NULL;
}

const PortableRenderMesh *portable_mesh_driver_art_view(const PortableMeshDriverArt *owner,
                                                        uint8_t angle)
{
    if (!owner || angle > 127)
        return NULL;
    const unsigned index = angle > 63 ? 127u - angle : angle;
    return portable_mesh_driver_art_frame(owner, owner->slots[owner->row][index], angle > 63);
}

const PortableRenderMesh *portable_mesh_driver_art_palette_view(const PortableMeshDriverArt *owner,
                                                                uint8_t angle, uint8_t bank)
{
    if (!bank) return portable_mesh_driver_art_view(owner, angle);
    if (!owner || angle > 127 || (bank < 5 || bank > 7) || !owner->palette_frames[bank-5])
        return NULL;
    const unsigned index = angle > 63 ? 127u - angle : angle;
    return &owner->palette_frames[bank-5][owner->slots[0][index]].meshes[angle > 63];
}

int portable_mesh_driver_art_ground_anchor(const PortableMeshDriverArt *owner, uint8_t angle,
                                            double *z)
{
    if (!owner || !z || angle > 127)
        return 0;
    const unsigned index = angle > 63 ? 127u - angle : angle;
    const unsigned frame = owner->slots[owner->row][index];
    if (frame >= owner->frame_count)
        return 0;
    *z = owner->ground_anchors[frame];
    return 1;
}

int portable_mesh_driver_art_angle(const PortableRenderPose *pose, const PortableRenderView *view,
                                   uint8_t *angle)
{
    if (!pose || !view || !angle)
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(pose->position[axis]) || !isfinite(pose->forward[axis]) ||
            !isfinite(view->position[axis]) || !isfinite(view->forward[axis]))
            return 0;
    const double fx = pose->forward[0], fy = pose->forward[1];
    double x = pose->position[0] - view->position[0], y = pose->position[1] - view->position[1];
    if (!isfinite(x) || !isfinite(y))
        return 0;
    if (hypot(x, y) < 1e-12)
    {
        x = view->forward[0];
        y = view->forward[1];
    }
    if (hypot(fx, fy) < 1e-12 || hypot(x, y) < 1e-12)
    {
        *angle = 0;
        return 1;
    }
    const double cross = fx * y - fy * x, dot = fx * x + fy * y;
    if (!isfinite(cross) || !isfinite(dot))
        return 0;
    double sector = atan2(cross, dot) * (64.0 / 3.14159265358979323846);
    if (sector < 0)
        sector += 128.0;
    *angle = (uint8_t)(sector >= 128.0 ? 0u : (unsigned)floor(sector));
    return 1;
}


const PortableCharacterViewRules *portable_mesh_driver_art_rules(const PortableMeshDriverArt *owner)
{
    return owner && owner->has_view_rules ? &owner->view_rules : NULL;
}

static int pose_frame(const PortableMeshDriverArt *owner, const PortableCharacterViewPose *pose,
                       uint32_t *frame)
{
    if (!owner || !pose || pose->angle > 127u || pose->mirror > 1u || !frame)
        return 0;
    const unsigned index = pose->angle > 63u ? 127u - pose->angle : pose->angle;
    const uint32_t slot = owner->sprite_bytes && owner->tumble_frame != UINT32_MAX ? owner->tumble_frame
                        : pose->frame_override ? pose->frame_override : owner->slots[owner->row][index];
    if (slot >= owner->frame_count) return 0;
    *frame = slot;
    return 1;
}

const PortableRenderMesh *portable_mesh_driver_art_pose_view(const PortableMeshDriverArt *owner,
    const PortableCharacterViewPose *pose, uint8_t bank)
{
    uint32_t frame;
    if (!pose_frame(owner, pose, &frame)) return NULL;
    if (!bank)
    {
        const DriverFrame *out = frame_at(owner, frame);
        return out ? &out->meshes[pose->mirror] : NULL;
    }
    if (bank < 5u || bank > 7u || !owner->palette_frames[bank - 5u]) return NULL;
    return &owner->palette_frames[bank - 5u][frame].meshes[pose->mirror];
}

const PortableRenderMesh *portable_mesh_driver_art_pose_blend(const PortableMeshDriverArt *owner,
    const PortableCharacterViewPose *pose)
{
    uint32_t frame;
    if (!pose_frame(owner, pose, &frame)) return NULL;
    const DriverFrame *out = frame_at(owner, frame);
    return out ? &out->blend[pose->mirror] : NULL;
}

int portable_mesh_driver_art_pose_ground_anchor(const PortableMeshDriverArt *owner,
    const PortableCharacterViewPose *pose, double *z)
{
    uint32_t frame;
    if (!z || !pose_frame(owner, pose, &frame)) return 0;
    *z = owner->ground_anchors[frame];
    return 1;
}


int portable_mesh_driver_art_pose_bounds(const PortableMeshDriverArt *owner,
    const PortableCharacterViewPose *pose, double bounds[6])
{
    uint32_t frame;
    if (!bounds || !pose_frame(owner,pose,&frame)) return 0;
    double result[6];
    memcpy(result,owner->opaque_bounds[frame],sizeof result);
    if (pose->mirror && result[3]>result[0])
    {
        const double sum=owner->frames[frame].vertices[0][0].position[0]+
                         owner->frames[frame].vertices[0][1].position[0];
        const double minimum=sum-result[3];
        result[3]=sum-result[0];
        result[0]=minimum;
    }
    memcpy(bounds,result,sizeof result);
    return 1;
}
