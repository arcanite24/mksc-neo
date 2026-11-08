#include "mesh_item_scene.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh item scene line %d: %s\n", __LINE__, #c);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
/* Synthetic immutable image provider: source cache decoding/palettes have their
 * own tests; this target verifies the scene boundary without a ROM/2D course. */
struct PortableMeshItemArt
{
    PortableNativeImage images[148];
    int missing;
};
const PortableNativeImage *portable_mesh_item_art_box(const PortableMeshItemArt *art, uint32_t tick)
{
    return art && art->missing != (int)(tick & 127u) ? &art->images[tick & 127u] : NULL;
}
const PortableNativeImage *portable_mesh_item_art_actor(const PortableMeshItemArt *art,
                                                        uint16_t item, unsigned direction)
{
    if (!art || direction >= 6)
        return NULL;
    unsigned index;
    if (item == 0x1000 || item == 0x1001)
        index = 128 + direction;
    else if (item == 0x1002 || item == 0x1003)
        index = 134 + direction;
    else if (item == 0x1004)
        index = 140 + direction;
    else if (item == 0x1005 && !direction)
        index = 146;
    else if (item == 0x100b && !direction)
        index = 147;
    else
        return NULL;
    return art->missing != (int)index ? &art->images[index] : NULL;
}
static void w32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}
static PortableMeshItemRules rules(void)
{
    PortableMeshItemRules r = {.profile_id = 1, .revision = 1};
#define MINIMUM(name, lo, hi) r.name = lo;
    PORTABLE_MESH_ITEM_FIELDS(MINIMUM)
#undef MINIMUM
    r.actors_radius_q16 = Q / 2;
    for (unsigned i = 0; i < 10; ++i)
        r.roulette.preview[i] = 0x1000;
    for (unsigned i = 0; i < 27; ++i)
        r.roulette.timeline[i] = 1;
    for (unsigned i = 0; i < 8; ++i)
    {
        r.player_weights[i][0] = (PortableGpItemWeight){0x1000, 1};
        r.cpu_weights[i][0] = (PortableGpItemWeight){0x1000, 1};
    }
    return r;
}
static int visibility(void)
{
    PortableMeshItems *world = calloc(1, sizeof *world);
    CHECK(world);
    world->status.count = 8;
    CHECK(!portable_mesh_item_scene_kart_hidden_views(NULL, 0, 4));
    CHECK(!portable_mesh_item_scene_kart_hidden_views(world, 0, 4));
    const uint16_t timers[] = {0, 1, 2, 31, 32, 33, 342, 343, 344, 345, 360};
    for (unsigned racer = 0; racer < 8; ++racer)
        for (unsigned t = 0; t < sizeof timers / sizeof *timers; ++t)
            for (unsigned parity = 0; parity < 2; ++parity)
            {
                world->status.racers[racer].boo_invisible_ticks = timers[t];
                world->tick = 100u + parity;
                for (unsigned views = 1; views <= 4; ++views)
                {
                    const uint32_t mask = portable_mesh_item_scene_kart_hidden_views(world, racer, views);
                    for (unsigned view = 0; view < views; ++view)
                    {
                        const int hidden = timers[t] && (view == racer ? parity != 0
                            : (timers[t] >= 33 && timers[t] <= 343) || (timers[t] & 1u));
                        CHECK(((mask >> view) & 1u) == (unsigned)hidden);
                    }
                    CHECK(!(mask >> views));
                }
            }
    CHECK(!portable_mesh_item_scene_kart_hidden_views(world, 8, 4));
    CHECK(!portable_mesh_item_scene_kart_hidden_views(world, 0, 5));
    free(world);
    return 0;
}
int main(void)
{
    CHECK(!visibility());
    uint8_t texels[32 * 32 * 4];
    memset(texels, 0xff, sizeof texels);
    memset(texels, 0, 4);
    PortableMeshItemArt *art = calloc(1, sizeof *art);
    CHECK(art);
    art->missing = -1;
    for (unsigned i = 0; i < 148; ++i)
    {
        const uint16_t size = i < 128 ? 32 : 16;
        art->images[i] = (PortableNativeImage){.pixels = texels,
                                               .width = size,
                                               .height = size,
                                               .anchor_x = (int16_t)(size / 2),
                                               .anchor_y = (int16_t)(i < 128    ? 30
                                                                     : i == 147 ? 8
                                                                                : 13)};
    }
    PortableMeshItemScene *scene = NULL;
    char error[256];
    CHECK(portable_mesh_item_scene_open(art, &scene, error, sizeof error));
    uint8_t bytes[64] = {0};
    for (unsigned i = 0; i < 2; ++i)
    {
        uint8_t *row = bytes + i * 32;
        w32(row, (uint32_t)(i ? -Q : Q));
        w32(row + 4, (uint32_t)(i ? -2 * Q : 2 * Q));
        w32(row + 8, (i ? 4u : 3u) * Q);
        w32(row + 12, Q);
        w32(row + 20, 120);
    }
    const PortableCourse3DView course = {
        .id = 70000,
        .table_count = 1,
        .tables = {{PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'), 2, 32, bytes}}};
    PortableMeshItems *world = calloc(1, sizeof *world), *before = malloc(sizeof *before);
    CHECK(world && before);
    world->inventory.count = world->status.count = 2;
    world->inventory.human_mask = world->status.human_mask = 3;
    world->boxes.count = 2;
    world->boxes.cooldowns[1] = 60;
    world->tick = 4;
    const uint16_t items[4] = {0x1000, 0x1002, 0x100b, 0x1005};
    for (unsigned i = 0; i < 4; ++i)
    {
        world->actors[i].phase = (uint8_t)(i == 1   ? PORTABLE_MESH_ACTOR_ORBIT_PENDING
                                           : i == 2 ? PORTABLE_MESH_ACTOR_HAUNT
                                                    : PORTABLE_MESH_ACTOR_FLYING);
        world->actors[i].owner = (uint8_t)(i & 1u);
        world->actors[i].item = items[i];
        world->actors[i].serial = i + 1;
        world->actors[i].motion.position_q16[0] = (int32_t)(i + 2) * Q;
        world->actors[i].motion.position_q16[1] = (int32_t)(i + 3) * Q;
        world->actors[i].motion.position_q16[2] = (int32_t)(i + 4) * Q;
    }
    *before = *world;
    const PortableCourse3DView course_before = course;
    uint8_t original_bytes[64];
    memcpy(original_bytes, bytes, sizeof bytes);
    PortableRenderInstance output[8], untouched[8];
    memset(output, 0xa5, sizeof output);
    memcpy(untouched, output, sizeof output);
    PortableMeshItemRules r = rules();
    CHECK(portable_mesh_item_rules_valid(&r));
    size_t count = 1;
    CHECK(portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                          sizeof error) &&
          count == 5);
    PortableRenderInstance tt_output[8]; size_t tt_count = 0;
    CHECK(portable_mesh_item_scene_append(scene, &course, world, &r, 2u, tt_output, 8,
                                          &tt_count, error, sizeof error) && tt_count == 3);

    CHECK(!memcmp(output, untouched, sizeof output[0]) &&
          !memcmp(output + 5, untouched + 5, 3 * sizeof output[0]));
    CHECK(!memcmp(world, before, sizeof *world) && !memcmp(bytes, original_bytes, sizeof bytes) &&
          !memcmp(&course, &course_before, sizeof course));
    CHECK(output[1].transform[3] == 1 && output[1].transform[7] == 2 &&
          output[1].transform[11] == 3);
    CHECK(output[2].transform[3] == 2 && output[2].transform[7] == 3 &&
          output[2].transform[11] == 4);
    for (unsigned i = 1; i < 5; ++i)
    {
        CHECK(output[i].flags == PORTABLE_RENDER_INSTANCE_BILLBOARD_Z &&
              output[i].billboard_scale[0] == 1 && output[i].billboard_scale[1] == 1 &&
              output[i].billboard_scale[2] == 1);
        CHECK(output[i].mesh->vertex_count == 4 && output[i].mesh->index_count == 6 &&
              output[i].mesh->run_count == 1 && output[i].mesh->materials[0].flags == (4u | 16u) &&
              output[i].mesh->materials[0].alpha_cutoff_q16 == 1);
        CHECK(portable_native_image_pixel(&output[i].mesh->materials[0].texture, 0, 0) == 0 &&
              portable_native_image_pixel(&output[i].mesh->materials[0].texture, 1, 0) ==
                  UINT32_MAX);
    }
    /* Source anchors do not shift a collision-centered item: shell16px is1m,
     * box32px is2m. UV corners cover the complete native source rectangles. */
    CHECK(output[1].mesh->vertices[0].position[0] == -1 &&
          output[1].mesh->vertices[0].position[2] == 1 &&
          output[1].mesh->vertices[3].position[0] == 1 &&
          output[1].mesh->vertices[3].position[2] == -1);
    CHECK(output[2].mesh->vertices[0].position[0] == -.5 &&
          output[2].mesh->vertices[0].position[2] == .5 && output[2].mesh->vertices[3].uv[0] == 1 &&
          output[2].mesh->vertices[3].uv[1] == 1);
    PortableRenderView view = {.right = {1, 0, 0}};
    PortableRenderInstance facing, reverse;
    const PortableRenderInstance original = output[2];
    CHECK(portable_render_instance_for_view(&output[2], &view, &facing));
    view.right[0] = -1;
    CHECK(portable_render_instance_for_view(&output[2], &view, &reverse));
    CHECK(facing.transform[0] == 1 && reverse.transform[0] == -1 && facing.transform[3] == 2 &&
          reverse.transform[3] == 2 && facing.transform[7] == 3 && reverse.transform[11] == 4 &&
          facing.mesh == output[2].mesh && reverse.mesh == output[2].mesh &&
          !memcmp(&original, &output[2], sizeof original));
    const PortableRenderMesh *box_mesh = output[1].mesh, *shell_mesh = output[2].mesh;
    PortableRenderVertex vertices[4];
    memcpy(vertices, shell_mesh->vertices, sizeof vertices);
    count = 0;
    CHECK(portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                          sizeof error) &&
          count == 4 && output[0].mesh == box_mesh && output[1].mesh == shell_mesh);
    /* Distinct published animation picks a prebuilt immutable mesh; it never
     * edits the mesh still borrowed by an earlier completed frame. */
    world->tick = 6;
    count = 0;
    CHECK(portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                          sizeof error) &&
          output[0].mesh != box_mesh && output[1].mesh != shell_mesh &&
          !memcmp(vertices, shell_mesh->vertices, sizeof vertices));
    *world = *before;
    count = 1;
    memcpy(output, untouched, sizeof output);
    CHECK(!portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 4, &count, error,
                                           sizeof error) &&
          count == 1 && !memcmp(output, untouched, sizeof output) && strstr(error, "capacity"));
    w32(bytes + 32 + 8, PORTABLE_FIXED_3D_POSITION_LIMIT + 1u);
    CHECK(!portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                           sizeof error) &&
          count == 1 && !memcmp(output, untouched, sizeof output));
    memcpy(bytes, original_bytes, sizeof bytes);
    world->boxes.count = 1;
    CHECK(!portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                           sizeof error) &&
          count == 1 && !memcmp(output, untouched, sizeof output));
    *world = *before;
    world->actors[1].item = 0x1006;
    CHECK(!portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                           sizeof error) &&
          count == 1 && !memcmp(output, untouched, sizeof output));
    *world = *before;
    world->actors[0].motion.position_q16[2] = PORTABLE_FIXED_3D_POSITION_LIMIT;
    CHECK(!portable_mesh_item_scene_append(scene, &course, world, &r, 8u, output, 8, &count, error,
                                           sizeof error) &&
          count == 1 && !memcmp(output, untouched, sizeof output));
    PortableMeshItemScene *old = scene;
    art->missing = 147;
    CHECK(!portable_mesh_item_scene_open(art, &scene, error, sizeof error) && scene == old);
    portable_mesh_item_scene_close(scene);
    free(world);
    free(before);
    free(art);
    puts(
        "Mesh item scene: PASS immutable source quads XYZ cooldowns hidden validation transactional capacity");
    return 0;
}
