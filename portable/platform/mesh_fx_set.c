#include "mesh_fx_set.h"

#include "model_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    PortableRenderVertex vertices[4];
    PortableRenderMaterial material;
    PortableRenderMesh mesh;
} FxQuad;

struct PortableMeshFxSet
{
    PortableFxSet rows;
    PortableMeshShadowDecal shadows[PORTABLE_FX_SET_SHADOW_LIMIT];
    FxQuad frames[PORTABLE_FX_STYLE_SLOTS][PORTABLE_MESH_FX_FRAMES];
    PortableMeshFxStyle styles[PORTABLE_FX_STYLE_SLOTS];
    uint8_t *style_pixels[PORTABLE_FX_STYLE_SLOTS];
    PortableRenderRun run;
};

static const uint32_t quad_indices[6] = {0, 1, 2, 0, 2, 3};

static void put(uint8_t *pixel, uint32_t argb)
{
    for (unsigned byte = 0; byte < 4u; ++byte)
        pixel[byte] = (uint8_t)(argb >> (byte * 8u));
}

void portable_mesh_fx_set_close(PortableMeshFxSet *owner)
{
    if (!owner)
        return;
    for (unsigned i = 0; i < PORTABLE_FX_SET_SHADOW_LIMIT; ++i)
        portable_mesh_shadow_decal_close(&owner->shadows[i]);
    for (unsigned i = 0; i < PORTABLE_FX_STYLE_SLOTS; ++i)
        free(owner->style_pixels[i]);
    free(owner);
}

static int fail(char *error, size_t capacity, uint32_t id, const char *detail, uint32_t at)
{
    if (error && capacity)
        snprintf(error, capacity, "3D effect set %#x: %s %u", id, detail, at);
    return 0;
}

static void quad(PortableMeshFxSet *set, FxQuad *q, const PortableRenderVertex vertices[4])
{
    memcpy(q->vertices, vertices, sizeof q->vertices);
    q->mesh = (PortableRenderMesh){.vertices = q->vertices, .indices = quad_indices, .materials = &q->material,
                                   .runs = &set->run, .vertex_count = 4, .index_count = 6, .material_count = 1,
                                   .run_count = 1};
}

/* Darkness = texel alpha x material alpha; the multiply colour keeps the
 * material's hue where it is not black (1 - a + a x colour). */
int portable_mesh_shadow_decal_open(PortableMeshShadowDecal *decal, const PortableModelMaterial *material)
{
    const PortableNativeImage *image = material ? &material->texture : NULL;
    if (!decal || !image || !image->pixels || image->width < 2u || image->height < 2u)
        return 0;
    uint8_t *pixels = malloc((size_t)image->width * image->height * 4u);
    if (!pixels)
        return 0;
    const uint32_t tint = material->argb;
    for (uint32_t y = 0; y < image->height; ++y)
        for (uint32_t x = 0; x < image->width; ++x)
        {
            const uint32_t texel = portable_native_image_pixel(image, x, y);
            const uint32_t a = (texel >> 24) * (tint >> 24) / 255u;
            uint32_t out = UINT32_C(0xff000000);
            for (unsigned shift = 0; shift < 24u; shift += 8u)
            {
                const uint32_t colour = ((texel >> shift) & 255u) * ((tint >> shift) & 255u) / 255u;
                out |= (255u - a + a * colour / 255u) << shift;
            }
            put(pixels + ((size_t)y * image->width + x) * 4u, out);
        }
    *decal = (PortableMeshShadowDecal){
        .vertices = {{.position = {-1, -1, 0}, .uv = {0, 1}},
                     {.position = {1, -1, 0}, .uv = {1, 1}},
                     {.position = {1, 1, 0}, .uv = {1, 0}},
                     {.position = {-1, 1, 0}, .uv = {0, 0}}},
        .material = {.argb = UINT32_MAX, .flags = 4u | PORTABLE_RENDER_MATERIAL_MULTIPLY,
                     .texture = {.pixels = pixels, .width = image->width, .height = image->height}},
        .run = {0, 6, 0},
        .pixels = pixels};
    decal->mesh = (PortableRenderMesh){.vertices = decal->vertices, .indices = quad_indices,
                                       .materials = &decal->material, .runs = &decal->run, .vertex_count = 4,
                                       .index_count = 6, .material_count = 1, .run_count = 1};
    return 1;
}

void portable_mesh_shadow_decal_close(PortableMeshShadowDecal *decal)
{
    if (!decal)
        return;
    free(decal->pixels);
    *decal = (PortableMeshShadowDecal){0};
}

/* Additive art is premultiplied; intensity art (not COLOURED) becomes white
 * light or white coverage the particle's tint colours. */
static int bake_style(PortableMeshFxSet *set, const PortableFxStyle *row, const PortableModelMaterial *material)
{
    const PortableNativeImage *image = &material->texture;
    if (!image->pixels || !image->width || !image->height || image->width % row->frames)
        return 0;
    const size_t count = (size_t)image->width * image->height;
    uint8_t *pixels = malloc(count * 4u);
    if (!pixels)
        return 0;
    const int additive = (row->flags & PORTABLE_FX_STYLE_ADDITIVE) != 0u;
    const int coloured = (row->flags & PORTABLE_FX_STYLE_COLOURED) != 0u;
    for (uint32_t y = 0; y < image->height; ++y)
        for (uint32_t x = 0; x < image->width; ++x)
        {
            uint32_t texel = portable_native_image_pixel(image, x, y);
            const uint32_t a = (texel >> 24) * (material->argb >> 24) / 255u;
            uint32_t r = (texel >> 16) & 255u, g = (texel >> 8) & 255u, b = texel & 255u;
            if (!coloured)
            {
                const uint32_t i = r > g ? (r > b ? r : b) : (g > b ? g : b);
                r = g = b = i;
            }
            if (additive)
                texel = UINT32_C(0xff000000) | (r * a / 255u) << 16 | (g * a / 255u) << 8 | b * a / 255u;
            else if (!coloured)
                texel = (r * a / 255u) << 24 | 0xffffffu;
            else
                texel = a << 24 | r << 16 | g << 8 | b;
            put(pixels + ((size_t)y * image->width + x) * 4u, texel);
        }
    set->style_pixels[row->slot] = pixels;
    PortableMeshFxStyle *style = &set->styles[row->slot];
    style->frame_count = row->frames;
    style->flags = row->flags;
    for (uint32_t f = 0; f < row->frames; ++f)
    {
        FxQuad *q = &set->frames[row->slot][f];
        q->material = (PortableRenderMaterial){
            .argb = UINT32_MAX,
            .flags = 4u | (additive ? PORTABLE_RENDER_MATERIAL_ADDITIVE : PORTABLE_RENDER_MATERIAL_ALPHA_BLEND),
            .texture = {.pixels = pixels, .width = image->width, .height = image->height}};
        const double u0 = (double)f / row->frames, u1 = (double)(f + 1u) / row->frames;
        const PortableRenderVertex vertices[4] = {{.position = {-.5, 0, -.5}, .uv = {u0, 1}},
                                                  {.position = {.5, 0, -.5}, .uv = {u1, 1}},
                                                  {.position = {.5, 0, .5}, .uv = {u1, 0}},
                                                  {.position = {-.5, 0, .5}, .uv = {u0, 0}}};
        quad(set, q, vertices);
        style->frames[f] = &q->mesh;
    }
    return 1;
}

int portable_mesh_fx_set_open(const PortableAssetPackView *pack, uint32_t id, PortableMeshFxSet **owner,
                              char *error, size_t capacity)
{
    if (!pack || !owner)
        return fail(error, capacity, id, "invalid pack/owner", 0);
    if (!id)
    {
        portable_mesh_fx_set_close(*owner);
        *owner = NULL;
        return 1;
    }
    PortableMeshFxSet *next = calloc(1, sizeof *next);
    if (!next)
        return fail(error, capacity, id, "allocation", 0);
    next->run = (PortableRenderRun){0, 6, 0};
    if (!portable_fx_set_find(pack, id, &next->rows))
    {
        portable_mesh_fx_set_close(next);
        return fail(error, capacity, id, "missing or invalid FXS1 section", 0);
    }
    for (uint32_t i = 0; i < next->rows.shadow_count; ++i)
    {
        PortableModelMaterial material;
        if (!portable_model_material_open(pack, next->rows.shadows[i].material, &material) ||
            !portable_mesh_shadow_decal_open(&next->shadows[i], &material))
        {
            portable_mesh_fx_set_close(next);
            return fail(error, capacity, id, "shadow row has no texture", i);
        }
    }
    for (uint32_t i = 0; i < next->rows.style_count; ++i)
    {
        PortableModelMaterial material;
        if (!portable_model_material_open(pack, next->rows.styles[i].material, &material) ||
            !bake_style(next, &next->rows.styles[i], &material))
        {
            portable_mesh_fx_set_close(next);
            return fail(error, capacity, id, "style row has no strip texture", i);
        }
    }
    portable_mesh_fx_set_close(*owner);
    *owner = next;
    return 1;
}

const PortableFxSet *portable_mesh_fx_set_rows(const PortableMeshFxSet *owner)
{
    return owner ? &owner->rows : NULL;
}

int portable_mesh_fx_set_shadow(const PortableMeshFxSet *owner, uint32_t kart_id, uint32_t driver_id,
                                PortableMeshFxShadow *out)
{
    const PortableFxShadow *row = owner ? portable_fx_set_shadow(&owner->rows, kart_id, driver_id) : NULL;
    if (!row || !out)
        return 0;
    *out = (PortableMeshFxShadow){.mesh = &owner->shadows[row - owner->rows.shadows].mesh,
                                  .half_width = row->half_width_q16 / 65536.0,
                                  .half_length = row->half_length_q16 / 65536.0,
                                  .forward = row->forward_q16 / 65536.0,
                                  .vanish = row->vanish_q16 / 65536.0,
                                  .volume = (row->flags & PORTABLE_FX_SHADOW_VOLUME) != 0u};
    return 1;
}

const PortableMeshFxStyle *portable_mesh_fx_set_style(const PortableMeshFxSet *owner, unsigned slot)
{
    return owner && slot < PORTABLE_FX_STYLE_SLOTS && owner->styles[slot].frame_count ? &owner->styles[slot]
                                                                                       : NULL;
}
