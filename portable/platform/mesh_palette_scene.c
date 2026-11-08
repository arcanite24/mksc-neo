#include "mesh_palette_scene.h"
#include "model_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PaletteColors
{
    uint8_t bytes[128];
    struct PaletteColors *next;
} PaletteColors;
typedef struct
{
    const uint8_t *source_pixels, *plane;
    uint32_t width, height;
    uint64_t used;
    const uint8_t *variants[PORTABLE_MESH_PALETTE_VARIANTS];
    PaletteColors *colors;
} PaletteImage;
typedef struct PaletteMeshVariant
{
    PortableRenderMesh mesh;
    PortableRenderMaterial *materials;
    struct PaletteMeshVariant *next;
} PaletteMeshVariant;
typedef struct PaletteMesh
{
    const PortableRenderMesh *base;
    const PortableRenderMesh **variants;
    PaletteMeshVariant *owned;
    struct PaletteMesh *next;
} PaletteMesh;
struct PortableMeshPaletteScene
{
    const PortableMeshPaletteConfig *config;
    PaletteImage images[PORTABLE_MESH_PALETTE_BINDINGS];
    PaletteMesh *meshes;
    PortableMeshPaletteSceneStats stats;
};
static int fail(char *error, size_t cap, const PortableMeshPaletteConfig *config,
                const char *reason)
{
    if (error && cap)
        snprintf(error, cap, "3D palette scene profile=%u: %s", config ? config->profile_id : 0,
                 reason);
    return 0;
}
static uint32_t rgb555(uint16_t color)
{
    const unsigned r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
    return UINT32_C(0xff000000) | (r * 8 + r / 4) << 16 | (g * 8 + g / 4) << 8 | (b * 8 + b / 4);
}
static int binding_fail(char *error, size_t cap, const PortableMeshPaletteConfig *cfg,
                        unsigned index, const PortableMeshPaletteBinding *b, const char *reason)
{
    char detail[160];
    snprintf(detail, sizeof detail, "binding=%u kind=%u id=%u: %s", index, b->kind, b->id, reason);
    return fail(error, cap, cfg, detail);
}
static void mesh_close(PaletteMesh *mesh)
{
    if (!mesh)
        return;
    while (mesh->owned)
    {
        PaletteMeshVariant *v = mesh->owned;
        mesh->owned = v->next;
        free(v->materials);
        free(v);
    }
    free(mesh->variants);
    free(mesh);
}
void portable_mesh_palette_scene_close(PortableMeshPaletteScene *scene)
{
    if (!scene)
        return;
    for (unsigned i = 0; i < PORTABLE_MESH_PALETTE_BINDINGS; ++i)
        while (scene->images[i].colors)
        {
            PaletteColors *c = scene->images[i].colors;
            scene->images[i].colors = c->next;
            free(c);
        }
    while (scene->meshes)
    {
        PaletteMesh *mesh = scene->meshes;
        scene->meshes = mesh->next;
        mesh_close(mesh);
    }
    free(scene);
}
static int open_image(const PortableAssetPackView *pack, const PortableMeshPaletteBinding *b,
                      PortableNativeImage *image)
{
    if (b->kind == PORTABLE_ASSET_SECTION_MODEL_MATERIAL)
    {
        PortableModelMaterial material;
        if (!portable_model_material_open(pack, b->id, &material) || !material.texture.pixels)
            return 0;
        *image = material.texture;
        return 1;
    }
    PortableAssetSectionView section;
    return b->kind == PORTABLE_ASSET_SECTION_COURSE_3D_TEXTURE &&
           portable_asset_pack_find_section(pack, b->kind, b->id, &section) ==
               PORTABLE_ASSET_PACK_OK &&
           section.version == 1 && section.bytes && section.size >= 32 &&
           section.size <= SIZE_MAX && !memcmp(section.bytes, "MKSCIMG1", 8) &&
           portable_native_image_open(section.bytes, (size_t)section.size, image);
}
static int prepare_image(PortableMeshPaletteScene *scene, unsigned index,
                         const PortableAssetPackView *pack, char *error, size_t cap)
{
    const PortableMeshPaletteConfig *cfg = scene->config;
    PortableMeshPaletteBinding b = {0};
    const uint8_t *plane;
    PortableNativeImage original;
    if (!portable_mesh_palette_binding(cfg, index, &b, &plane) || !open_image(pack, &b, &original))
        return binding_fail(error, cap, cfg, index, &b,
                            "missing or unsupported bound IMG1/material asset");
    if (original.width != b.width || original.height != b.height)
        return binding_fail(error, cap, cfg, index, &b,
                            "bound image dimensions differ from indexed plane");
    PaletteImage *image = &scene->images[index];
    image->source_pixels = original.pixels;
    image->plane = plane;
    image->width = b.width;
    image->height = b.height;
    const uint16_t *seed = b.bank ? cfg->seed.sky : cfg->seed.road;
    for (uint32_t pixel = 0; pixel < b.index_size; ++pixel)
    {
        const unsigned color = plane[pixel];
        if (color != 255 && color >= 64)
            return binding_fail(error, cap, cfg, index, &b,
                                "indexed plane contains an invalid palette index");
        const uint32_t expected = color == 255 ? 0 : rgb555(seed[color]);
        if (portable_native_image_pixel(&original, pixel % b.width, pixel / b.width) != expected)
            return binding_fail(error, cap, cfg, index, &b,
                                "seed pixel mismatch; asset override needs matching palette data");
        if (color != 255)
            image->used |= UINT64_C(1) << color;
    }
    /* Lookup is deliberately by original pixel identity, never RGB equality.
     * Ambiguous aliases would select a different logical index plane. */
    for (unsigned i = 0; i < index; ++i)
        if (scene->images[i].source_pixels == image->source_pixels)
            return fail(error, cap, cfg, "duplicate bound source pixel identity");
    for (unsigned variant = 0; variant < cfg->variant_count; ++variant)
    {
        uint8_t colors[128] = {0};
        const uint8_t *pair = cfg->variants[variant] + b.bank * 128;
        for (unsigned color = 0; color < 64; ++color)
            if (image->used & (UINT64_C(1) << color))
            {
                memcpy(colors + color * 2, pair + color * 2, 2);
                colors[color * 2 + 1] &= 127; /* RGB555 ignores bit15. */
            }
        PaletteColors *found = image->colors;
        while (found && memcmp(found->bytes, colors, sizeof colors))
            found = found->next;
        if (!found)
        {
            const size_t bytes = (size_t)b.width * b.height * 4;
            if (scene->stats.unique_textures == PORTABLE_MESH_PALETTE_SCENE_TEXTURES ||
                bytes > PORTABLE_MESH_PALETTE_SCENE_BYTES - scene->stats.expanded_rgba_bytes)
                return fail(error, cap, cfg, "expanded texture count/64 MiB budget exceeded");
            found = malloc(sizeof *found);
            if (!found)
                return fail(error, cap, cfg, "palette allocation failed");
            memcpy(found->bytes, colors, sizeof colors);
            found->next = image->colors;
            image->colors = found;
            ++scene->stats.unique_textures;
            scene->stats.expanded_rgba_bytes += bytes;
        }
        image->variants[variant] = found->bytes;
    }
    return 1;
}
PortableMeshPaletteScene *portable_mesh_palette_scene_create(const PortableMeshPaletteConfig *cfg,
                                                             const PortableAssetPackView *pack,
                                                             char *error, size_t cap)
{
    if (!portable_mesh_palette_config_valid(cfg) || !pack)
    {
        fail(error, cap, cfg, "invalid configuration or pack");
        return NULL;
    }
    PortableMeshPaletteScene *scene = calloc(1, sizeof *scene);
    if (!scene)
    {
        fail(error, cap, cfg, "owner allocation failed");
        return NULL;
    }
    scene->config = cfg;
    for (unsigned i = 0; i < cfg->binding_count; ++i)
        if (!prepare_image(scene, i, pack, error, cap))
        {
            portable_mesh_palette_scene_close(scene);
            return NULL;
        }
    if (error && cap)
        error[0] = 0;
    return scene;
}
static int material_equal(const PortableRenderMaterial *a, const PortableRenderMaterial *b)
{
    const PortableNativeImage *x = &a->texture, *y = &b->texture;
    return a->argb == b->argb && a->flags == b->flags &&
           a->alpha_cutoff_q16 == b->alpha_cutoff_q16 && x->pixels == y->pixels &&
           x->palette == y->palette && x->width == y->width && x->height == y->height &&
           x->anchor_x == y->anchor_x && x->anchor_y == y->anchor_y &&
           x->palette_count == y->palette_count && x->palette_base == y->palette_base;
}
int portable_mesh_palette_scene_relocate(PortableMeshPaletteScene *scene,
                                         const PortableMeshPaletteConfig *config,
                                         const PortableRenderMesh *old_base,
                                         const PortableRenderMesh *new_base, char *error,
                                         size_t cap)
{
    if (!config || !portable_mesh_palette_config_valid(config) || (!scene && config->enabled))
        return fail(error, cap, config, "invalid relocation config");
    if (!scene)
    {
        if (error && cap)
            error[0] = 0;
        return 1;
    }
    if (memcmp(scene->config, config, sizeof *config) || !old_base || !new_base ||
        old_base->vertices != new_base->vertices || old_base->indices != new_base->indices ||
        old_base->materials != new_base->materials || old_base->runs != new_base->runs ||
        old_base->vertex_count != new_base->vertex_count ||
        old_base->index_count != new_base->index_count ||
        old_base->material_count != new_base->material_count ||
        old_base->run_count != new_base->run_count)
        return fail(error, cap, config, "relocation owners differ");
    PaletteMesh *found = NULL;
    for (PaletteMesh *m = scene->meshes; m; m = m->next)
    {
        if (m->base == old_base)
            found = m;
        if (old_base != new_base && m->base == new_base)
            return fail(error, cap, config, "relocation base already prepared");
    }
    /* Unaffected bases have no cache entry and require only the config rebind. */
    if (found)
        found->base = new_base;
    scene->config = config;
    if (error && cap)
        error[0] = 0;
    return 1;
}
const PortableRenderMesh *portable_mesh_palette_scene_mesh(PortableMeshPaletteScene *scene,
                                                           const PortableRenderMesh *base,
                                                           unsigned variant, char *error,
                                                           size_t cap)
{
    if (!scene || !base || !base->vertices || !base->indices || !base->runs ||
        !base->vertex_count || base->vertex_count > 131072 || !base->index_count ||
        base->index_count > 262144u * 3u || base->index_count % 3u || !base->run_count ||
        base->run_count > 262144 || !base->materials || !base->material_count ||
        base->material_count > 256 ||
        (scene->config->enabled ? variant >= scene->config->variant_count : variant != 0))
    {
        fail(error, cap, scene ? scene->config : NULL, "invalid mesh or variant");
        return NULL;
    }
    for (uint32_t i = 0; i < base->run_count; ++i)
        if (base->runs[i].material >= base->material_count ||
            base->runs[i].first_index > base->index_count ||
            base->runs[i].index_count > base->index_count - base->runs[i].first_index)
        {
            fail(error, cap, scene->config, "invalid mesh material run");
            return NULL;
        }
    for (PaletteMesh *mesh = scene->meshes; mesh; mesh = mesh->next)
        if (mesh->base == base)
        {
            if (error && cap)
                error[0] = 0;
            return mesh->variants[variant];
        }
    int binding[256], affected = 0;
    for (unsigned material = 0; material < base->material_count; ++material)
    {
        binding[material] = -1;
        for (unsigned i = 0; i < scene->config->binding_count; ++i)
            if (base->materials[material].texture.pixels == scene->images[i].source_pixels)
            {
                binding[material] = (int)i;
                affected = 1;
                if (base->materials[material].texture.width != scene->images[i].width ||
                    base->materials[material].texture.height != scene->images[i].height ||
                    base->materials[material].texture.palette ||
                    base->materials[material].texture.palette_count ||
                    base->materials[material].texture.palette_base)
                {
                    fail(error, cap, scene->config,
                         "mesh image view dimensions differ from binding");
                    return NULL;
                }
            }
    }
    if (!affected)
    {
        if (error && cap)
            error[0] = 0;
        return base;
    }
    PaletteMesh *prepared = calloc(1, sizeof *prepared);
    size_t added = 0;
    if (prepared)
        prepared->variants = calloc(scene->config->variant_count, sizeof *prepared->variants);
    if (!prepared || !prepared->variants)
        goto allocation_failed;
    prepared->base = base;
    for (unsigned v = 0; v < scene->config->variant_count; ++v)
    {
        PortableRenderMaterial *materials = malloc(base->material_count * sizeof *materials);
        if (!materials)
            goto allocation_failed;
        memcpy(materials, base->materials, base->material_count * sizeof *materials);
        for (unsigned i = 0; i < base->material_count; ++i)
            if (binding[i] >= 0)
            {
                const PaletteImage *image = &scene->images[binding[i]];
                materials[i].texture.pixels = image->plane;
                materials[i].texture.palette = image->variants[v];
                materials[i].texture.palette_count = 64;
                materials[i].texture.palette_base = 0;
            }
        PaletteMeshVariant *found = prepared->owned;
        while (found)
        {
            unsigned i = 0;
            while (i < base->material_count && material_equal(&materials[i], &found->materials[i]))
                ++i;
            if (i == base->material_count)
                break;
            found = found->next;
        }
        if (found)
            free(materials);
        else
        {
            if (scene->stats.mesh_wrappers + added == PORTABLE_MESH_PALETTE_SCENE_MESHES)
            {
                free(materials);
                mesh_close(prepared);
                fail(error, cap, scene->config, "aggregate mesh wrapper budget exceeded");
                return NULL;
            }
            found = malloc(sizeof *found);
            if (!found)
            {
                free(materials);
                goto allocation_failed;
            }
            found->mesh = *base;
            found->materials = materials;
            found->mesh.materials = materials;
            found->next = prepared->owned;
            prepared->owned = found;
            ++added;
        }
        prepared->variants[v] = &found->mesh;
    }
    prepared->next = scene->meshes;
    scene->meshes = prepared;
    scene->stats.mesh_wrappers += added;
    if (error && cap)
        error[0] = 0;
    return prepared->variants[variant];
allocation_failed:
    mesh_close(prepared);
    fail(error, cap, scene->config, "mesh variant allocation failed");
    return NULL;
}
PortableMeshPaletteSceneStats
portable_mesh_palette_scene_stats(const PortableMeshPaletteScene *scene)
{
    return scene ? scene->stats : (PortableMeshPaletteSceneStats){0};
}
