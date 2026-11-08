#include "mesh_palette_scene.h"
#include "model_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "palette scene:%d: %s\n", __LINE__, #x);                               \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static void put(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}
static void half(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static uint32_t color(unsigned c)
{
    unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return UINT32_C(0xff000000) | (r * 8 + r / 4) << 16 | (g * 8 + g / 4) << 8 | (b * 8 + b / 4);
}
static void image(uint8_t *p, unsigned width, unsigned height, const uint8_t *indices,
                  const uint16_t seed[64])
{
    size_t size = 32 + (size_t)width * height * 4;
    memset(p, 0, size);
    memcpy(p, "MKSCIMG1", 8);
    half(p + 8, 32);
    half(p + 10, 1);
    half(p + 12, width);
    half(p + 14, height);
    put(p + 24, 32);
    put(p + 28, (uint32_t)size);
    for (size_t i = 0; i < (size_t)width * height; ++i)
        put(p + 32 + i * 4, indices[i] == 255 ? 0 : color(seed[indices[i]]));
}
static const uint8_t ambient_data[256] = {0};
static void config(PortableMeshPaletteConfig *c, const uint8_t *planes, unsigned bindings,
                   unsigned variants)
{
    memset(c, 0, sizeof *c);
    c->enabled = c->profile_id = c->revision = 1;
    c->binding_count = bindings;
    c->variant_count = variants;
    c->indices = planes;
    c->index_bytes = bindings * 3;
    c->data = ambient_data;
    c->data_size = sizeof ambient_data;
    c->lane_count = 1;
    c->frame_count = 64;
    c->timer_lane = UINT16_MAX;
    c->lanes[0] = (PortableAmbientLane){.stage = PORTABLE_AMBIENT_PRE_PROFILE,
                                        .controller = PORTABLE_AMBIENT_SUNSET,
                                        .frame_count = 64};
    for (unsigned i = 0; i < 64; ++i)
        c->frames[i].duration = 1;
    c->initial_snapshot.initialized = 1;
    c->seed.road[1] = c->seed.road[2] = 31;
    c->seed.sky[1] = c->seed.sky[2] = 31u << 5;
    for (unsigned i = 0; i < bindings; ++i)
        c->bindings[i] = (PortableMeshPaletteBinding){.kind = i ? 101 : 97,
                                                      .id = 11 + i,
                                                      .bank = i ? 1 : 0,
                                                      .width = 3,
                                                      .height = 1,
                                                      .index_offset = i * 3,
                                                      .index_size = 3};
    for (unsigned v = 0; v < variants; ++v)
        for (unsigned i = 0; i < 64; ++i)
        {
            half(c->variants[v] + i * 2, c->seed.road[i]);
            half(c->variants[v] + 128 + i * 2, c->seed.sky[i]);
        }
    if (variants > 1)
        half(c->variants[1] + 4, 31u << 10);
    if (variants > 2)
        half(c->variants[2] + 126, 1234); /* Unused index must not duplicate uploads. */
}
static const PortableRenderVertex vertices[3] = {0};
static const uint32_t indices[3] = {0, 1, 2};
static const PortableRenderRun run = {0, 3, 0};
static PortableRenderMesh mesh(const PortableRenderMaterial *materials, unsigned count)
{
    return (PortableRenderMesh){.vertices = vertices,
                                .indices = indices,
                                .runs = &run,
                                .vertex_count = 3,
                                .index_count = 3,
                                .run_count = 1,
                                .materials = materials,
                                .material_count = count};
}
static int ordinary_views(void)
{
    PortableMeshPaletteConfig *c = malloc(sizeof *c);
    CHECK(c);
    const uint8_t planes[] = {1, 2, 255, 1, 2, 255};
    config(c, planes, 2, 3);
    CHECK(portable_mesh_palette_config_valid(c));
    uint8_t material[60] = {0}, texture[44];
    put(material, UINT32_C(0x314c544d));
    put(material + 4, UINT32_C(0xff123456));
    put(material + 8, 1);
    put(material + 12, 44);
    image(material + 16, 3, 1, planes, c->seed.road);
    image(texture, 3, 1, planes + 3, c->seed.sky);
    PortableAssetSectionView sections[2] = {
        {.kind = 97, .content_id = 11, .version = 1, .bytes = material, .size = sizeof material},
        {.kind = 101,
         .content_id = 12,
         .version = 1,
         .flags = 3,
         .bytes = texture,
         .size = sizeof texture}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 2};
    PortableModelMaterial decoded;
    CHECK(portable_model_material_open(&pack, 11, &decoded));
    PortableRenderMaterial paints[3] = {
        {.argb = decoded.argb, .flags = 21, .alpha_cutoff_q16 = 32768, .texture = decoded.texture},
        {.argb = UINT32_MAX, .flags = 4},
        {.argb = UINT32_C(0xff112233), .flags = 4}};
    CHECK(portable_native_image_open(texture, sizeof texture, &paints[1].texture));
    const PortableRenderMaterial original[3] = {paints[0], paints[1], paints[2]};
    PortableRenderMesh base = mesh(paints, 3), unaffected = mesh(&paints[2], 1);
    char error[192];
    PortableMeshPaletteScene *scene =
        portable_mesh_palette_scene_create(c, &pack, error, sizeof error);
    CHECK(scene);
    PortableMeshPaletteSceneStats stats = portable_mesh_palette_scene_stats(scene);
    CHECK(stats.unique_textures == 3 && stats.expanded_rgba_bytes == 36 && !stats.mesh_wrappers);
    const PortableRenderMesh *a =
        portable_mesh_palette_scene_mesh(scene, &base, 0, error, sizeof error);
    const PortableRenderMesh *b =
        portable_mesh_palette_scene_mesh(scene, &base, 1, error, sizeof error);
    CHECK(a && b && a != b && a != &base && b != &base);
    CHECK(portable_mesh_palette_scene_mesh(scene, &base, 2, error, sizeof error) == a);
    CHECK(portable_mesh_palette_scene_mesh(scene, &base, 1, error, sizeof error) == b);
    CHECK(a->vertices == base.vertices && a->indices == base.indices && a->runs == base.runs &&
          a->vertex_count == base.vertex_count && a->index_count == base.index_count);
    CHECK(a->materials[0].argb == paints[0].argb && a->materials[0].flags == paints[0].flags &&
          a->materials[0].alpha_cutoff_q16 == paints[0].alpha_cutoff_q16);
    CHECK(portable_native_image_pixel(&a->materials[0].texture, 0, 0) == UINT32_C(0xffff0000) &&
          portable_native_image_pixel(&a->materials[0].texture, 1, 0) == UINT32_C(0xffff0000));
    CHECK(portable_native_image_pixel(&b->materials[0].texture, 0, 0) == UINT32_C(0xffff0000) &&
          portable_native_image_pixel(&b->materials[0].texture, 1, 0) == UINT32_C(0xff0000ff));
    CHECK(!portable_native_image_pixel(&b->materials[0].texture, 2, 0));
    CHECK(a->materials[1].texture.palette == b->materials[1].texture.palette);
    CHECK(portable_native_image_pixel(&b->materials[1].texture, 0, 0) == UINT32_C(0xff00ff00));
    CHECK(portable_mesh_palette_scene_mesh(scene, &unaffected, 0, error, sizeof error) ==
          &unaffected);
    stats = portable_mesh_palette_scene_stats(scene);
    CHECK(stats.mesh_wrappers == 2);
    CHECK(!portable_mesh_palette_scene_mesh(scene, &base, 3, error, sizeof error));
    PortableRenderMaterial bad_paint = paints[0];
    bad_paint.texture.width = 2;
    PortableRenderMesh bad = mesh(&bad_paint, 1);
    CHECK(!portable_mesh_palette_scene_mesh(scene, &bad, 0, error, sizeof error));
    CHECK(portable_mesh_palette_scene_stats(scene).mesh_wrappers == 2 &&
          portable_mesh_palette_scene_mesh(scene, &base, 1, error, sizeof error) == b);
    CHECK(!memcmp(paints, original, sizeof paints) &&
          !memcmp(planes, (uint8_t[]){1, 2, 255, 1, 2, 255}, 6));
    portable_mesh_palette_scene_close(scene);
    /* Model a GP candidate's embedded config/terrain moving into the live
     * host. Previously prepared wrapper addresses must survive the old host. */
    PortableMeshPaletteConfig *old_config = malloc(sizeof *old_config);
    PortableMeshPaletteConfig *moved_config = malloc(sizeof *moved_config);
    PortableRenderMesh *old_terrain = malloc(sizeof *old_terrain);
    CHECK(old_config && moved_config && old_terrain);
    memcpy(old_config, c, sizeof *c);
    memcpy(moved_config, c, sizeof *c);
    *old_terrain = base;
    PortableRenderMesh moved_terrain = *old_terrain;
    scene = portable_mesh_palette_scene_create(old_config, &pack, error, sizeof error);
    CHECK(scene);
    a = portable_mesh_palette_scene_mesh(scene, old_terrain, 0, error, sizeof error);
    b = portable_mesh_palette_scene_mesh(scene, old_terrain, 1, error, sizeof error);
    CHECK(a && b);
    stats = portable_mesh_palette_scene_stats(scene);
    ++moved_config->revision;
    CHECK(!portable_mesh_palette_scene_relocate(scene, moved_config, old_terrain, &moved_terrain,
                                                error, sizeof error));
    --moved_config->revision;
    --moved_terrain.index_count;
    CHECK(!portable_mesh_palette_scene_relocate(scene, moved_config, old_terrain, &moved_terrain,
                                                error, sizeof error));
    ++moved_terrain.index_count;
    CHECK(portable_mesh_palette_scene_mesh(scene, old_terrain, 1, error, sizeof error) == b &&
          portable_mesh_palette_scene_stats(scene).mesh_wrappers == stats.mesh_wrappers);
    CHECK(portable_mesh_palette_scene_relocate(scene, moved_config, old_terrain, &moved_terrain,
                                               error, sizeof error));
    memset(old_config, 0xa5, sizeof *old_config);
    memset(old_terrain, 0xa5, sizeof *old_terrain);
    free(old_config);
    free(old_terrain);
    CHECK(portable_mesh_palette_scene_mesh(scene, &moved_terrain, 0, error, sizeof error) == a &&
          portable_mesh_palette_scene_mesh(scene, &moved_terrain, 1, error, sizeof error) == b &&
          portable_mesh_palette_scene_stats(scene).mesh_wrappers == stats.mesh_wrappers);
    CHECK(portable_native_image_pixel(&b->materials[0].texture, 1, 0) == UINT32_C(0xff0000ff));
    CHECK(!portable_mesh_palette_scene_relocate(NULL, moved_config, &base, &base, error,
                                                sizeof error));
    portable_mesh_palette_scene_close(scene);
    free(moved_config);
    /* Actual asset overrides must not be silently replaced by source colors. */
    for (unsigned fault = 0; fault < 4; ++fault)
    {
        const uint8_t saved = material[48];
        const uint16_t width = c->bindings[1].width;
        const uint32_t id = sections[1].content_id;
        if (fault == 0)
            sections[1].content_id = 99;
        if (fault == 1)
        {
            c->bindings[1].width = 1;
            c->bindings[1].height = 3;
        }
        if (fault == 2)
            material[48] ^= 1;
        if (fault == 3)
            put(texture + 40, UINT32_C(0xff000000)); /* Indexed255 requires transparent. */
        CHECK(!portable_mesh_palette_scene_create(c, &pack, error, sizeof error));
        CHECK(strstr(error, "profile=1"));
        sections[1].content_id = id;
        c->bindings[1].width = width;
        c->bindings[1].height = 1;
        material[48] = saved;
        put(texture + 40, 0);
    }
    const PortableModelMaterial retained = decoded;
    material[0] ^= 1;
    CHECK(!portable_model_material_open(&pack, 11, &decoded) &&
          !memcmp(&retained, &decoded, sizeof decoded));
    material[0] ^= 1;
    /* Preparing another base must admit its complete variant table atomically. */
    scene = portable_mesh_palette_scene_create(c, &pack, error, sizeof error);
    CHECK(scene);
    PortableRenderMesh *many = calloc(1025, sizeof *many);
    CHECK(many);
    for (unsigned i = 0; i < 1023; ++i)
    {
        many[i] = base;
        CHECK(portable_mesh_palette_scene_mesh(scene, &many[i], 0, error, sizeof error));
    }
    PortableRenderMesh sky_only = mesh(&paints[1], 1);
    CHECK(portable_mesh_palette_scene_mesh(scene, &sky_only, 0, error, sizeof error));
    CHECK(portable_mesh_palette_scene_stats(scene).mesh_wrappers == 2047);
    many[1023] = base;
    CHECK(!portable_mesh_palette_scene_mesh(scene, &many[1023], 0, error, sizeof error));
    CHECK(strstr(error, "wrapper budget") &&
          portable_mesh_palette_scene_stats(scene).mesh_wrappers == 2047);
    /* A failed two-wrapper admission must leave room for one valid wrapper. */
    many[1024] = sky_only;
    CHECK(portable_mesh_palette_scene_mesh(scene, &many[1024], 0, error, sizeof error));
    CHECK(portable_mesh_palette_scene_stats(scene).mesh_wrappers == 2048);
    CHECK(portable_mesh_palette_scene_mesh(scene, &many[0], 1, error, sizeof error));
    portable_mesh_palette_scene_close(scene);
    free(many);
    memset(c, 0, sizeof *c);
    CHECK(portable_mesh_palette_scene_relocate(NULL, c, NULL, NULL, error, sizeof error));
    scene = portable_mesh_palette_scene_create(c, &pack, error, sizeof error);
    CHECK(scene && portable_mesh_palette_scene_mesh(scene, &base, 0, error, sizeof error) == &base);
    CHECK(!portable_mesh_palette_scene_stats(scene).unique_textures &&
          !portable_mesh_palette_scene_stats(scene).mesh_wrappers);
    CHECK(!portable_mesh_palette_scene_mesh(scene, &base, 1, error, sizeof error));
    portable_mesh_palette_scene_close(scene);
    free(c);
    return 0;
}
static int budgets(void)
{
    PortableMeshPaletteConfig *c = malloc(sizeof *c);
    CHECK(c);
    char error[192];
    uint8_t planes[15];
    memset(planes, 1, sizeof planes);
    config(c, planes, 5, 512);
    uint8_t images[5][44];
    PortableAssetSectionView sections[5];
    for (unsigned i = 0; i < 5; ++i)
    {
        c->bindings[i].kind = 101;
        c->bindings[i].bank = 0;
        image(images[i], 3, 1, planes + i * 3, c->seed.road);
        sections[i] = (PortableAssetSectionView){
            .kind = 101, .content_id = 11 + i, .version = 1, .bytes = images[i], .size = 44};
    }
    for (unsigned v = 0; v < 512; ++v)
        half(c->variants[v] + 2, 31 + v);
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 5};
    CHECK(portable_mesh_palette_config_valid(c));
    c->binding_count = 4;
    c->index_bytes = 12;
    pack.section_count = 4;
    PortableMeshPaletteScene *scene =
        portable_mesh_palette_scene_create(c, &pack, error, sizeof error);
    CHECK(scene && portable_mesh_palette_scene_stats(scene).unique_textures == 2048);
    portable_mesh_palette_scene_close(scene);
    c->binding_count = 5;
    c->index_bytes = 15;
    pack.section_count = 5;
    CHECK(!portable_mesh_palette_scene_create(c, &pack, error, sizeof error) &&
          strstr(error, "texture count"));
    const size_t pixels = 1024u * 1024u;
    uint8_t *large_plane = malloc(pixels), *large_image = malloc(32 + pixels * 4);
    CHECK(large_plane && large_image);
    memset(large_plane, 1, pixels);
    config(c, large_plane, 1, 17);
    c->bindings[0].kind = 101;
    c->bindings[0].width = c->bindings[0].height = 1024;
    c->bindings[0].index_size = c->index_bytes = (uint32_t)pixels;
    for (unsigned v = 0; v < 17; ++v)
        half(c->variants[v] + 2, 31 + v);
    image(large_image, 1024, 1024, large_plane, c->seed.road);
    sections[0] = (PortableAssetSectionView){
        .kind = 101, .content_id = 11, .version = 1, .bytes = large_image, .size = 32 + pixels * 4};
    pack.section_count = 1;
    c->variant_count = 16;
    scene = portable_mesh_palette_scene_create(c, &pack, error, sizeof error);
    CHECK(scene &&
          portable_mesh_palette_scene_stats(scene).expanded_rgba_bytes == 64u * 1024 * 1024);
    portable_mesh_palette_scene_close(scene);
    c->variant_count = 17;
    CHECK(!portable_mesh_palette_scene_create(c, &pack, error, sizeof error) &&
          strstr(error, "64 MiB"));
    free(large_plane);
    free(large_image);
    free(c);
    return 0;
}
int main(void)
{
    CHECK(!ordinary_views() && !budgets());
    puts(
        "mesh palette scene: PASS seed identity immutable indexed variants canonical budgets rollback");
    return 0;
}
