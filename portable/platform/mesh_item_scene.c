#include "mesh_item_scene.h"
#include "fixed_3d.h"
#include "render_model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    BOXES = 128,
    SHELL_FIRST = 128,
    BANANA = 146,
    BOO = 147,
    QUADS = 148,
    RIG_FRAMES = 600,
    SHARDS_PER_BOX = 6,
    SHARD_TICKS = 36
};
typedef struct
{
    PortableRenderVertex vertices[4];
    PortableRenderMaterial material;
    PortableRenderRun run;
    PortableRenderMesh mesh;
} ItemQuad;
/* A content platform's model for one presentation slot; a rigged one loops
 * its IDLE clip, baked once per frame at open. */
typedef struct
{
    PortableRenderResources resources;
    PortableRenderMesh *frames;
    PortableRenderVertex *frame_vertices;
    uint32_t frame_count, flags;
    double scale, pivot[3], extent; /* extent: farthest rest vertex from the up axis */
    PortableNativeImage overlay;    /* material 0's texture, borrowed from the pack */
    uint8_t present;
} ItemModel;
struct PortableMeshItemScene
{
    ItemQuad quads[QUADS];
    ItemModel models[PORTABLE_MODEL_ITEM_MODELS];
    PortableNativeImage icons[PORTABLE_MODEL_ITEM_ICONS]; /* borrowed from the pack */
};
uint32_t portable_mesh_item_scene_kart_hidden_views(const PortableMeshItems *world, unsigned racer,
                                                   unsigned viewers)
{
    if (!world || racer >= world->status.count || viewers > PORTABLE_RENDER_VIEW_LIMIT)
        return 0;
    const uint16_t timer = world->status.racers[racer].boo_invisible_ticks;
    if (!timer)
        return 0;
    uint32_t hidden = 0;
    for (unsigned view = 0; view < viewers; ++view)
        if (racer == view ? (world->tick & 1u)
                         : (timer > 32u && timer <= 343u) || (timer & 1u))
            hidden |= UINT32_C(1) << view;
    return hidden;
}
static const uint32_t indices[6] = {0, 1, 2, 1, 3, 2};
static int fail(char *error, size_t capacity, uint32_t course, uint32_t row, const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "3D item scene course=%u row=%u: %s", course, row, reason);
    return 0;
}
static void close_models(PortableMeshItemScene *owner)
{
    memset(owner->icons, 0, sizeof owner->icons);
    for (unsigned slot = 0; slot < PORTABLE_MODEL_ITEM_MODELS; ++slot)
    {
        ItemModel *model = &owner->models[slot];
        portable_render_resources_close(&model->resources);
        free(model->frames);
        free(model->frame_vertices);
        memset(model, 0, sizeof *model);
    }
}
void portable_mesh_item_scene_close(PortableMeshItemScene *owner)
{
    if (!owner)
        return;
    close_models(owner);
    free(owner);
}
static int quad(ItemQuad *q, const PortableNativeImage *image, unsigned expected)
{
    if (!image || !image->pixels || image->width != expected || image->height != expected)
        return 0;
    /* Source anchors locate a2D draw origin, not the sphere center. Explicitly
     * remove the image-center offset in that same anchor coordinate space. */
    const double origin_x = -(double)image->anchor_x, origin_z = (double)image->anchor_y;
    const double center_x = origin_x + (double)image->width * .5;
    const double center_z = origin_z - (double)image->height * .5;
    const double xs[2] = {(origin_x - center_x) / 16.0,
                          (origin_x + image->width - center_x) / 16.0};
    const double zs[2] = {(origin_z - center_z) / 16.0,
                          (origin_z - image->height - center_z) / 16.0};
    for (unsigned v = 0; v < 4u; ++v)
        q->vertices[v] = (PortableRenderVertex){.position = {xs[v & 1u], 0, zs[(v >> 1u) & 1u]},
                                                .normal = {0, -1, 0},
                                                .uv = {(v & 1u) ? 1 : 0, (v & 2u) ? 1 : 0}};
    q->material = (PortableRenderMaterial){
        .argb = UINT32_MAX, .flags = 4u | 16u, .alpha_cutoff_q16 = 1, .texture = *image};
    q->run = (PortableRenderRun){0, 6, 0};
    q->mesh = (PortableRenderMesh){.vertices = q->vertices,
                                   .indices = indices,
                                   .materials = &q->material,
                                   .runs = &q->run,
                                   .vertex_count = 4,
                                   .index_count = 6,
                                   .material_count = 1,
                                   .run_count = 1};
    return 1;
}
int portable_mesh_item_scene_open(const PortableMeshItemArt *art, PortableMeshItemScene **owner,
                                  char *error, size_t capacity)
{
    if (!art || !owner)
        return fail(error, capacity, 0, 0, "invalid art/owner");
    PortableMeshItemScene *next = calloc(1, sizeof *next);
    if (!next)
        return fail(error, capacity, 0, 0, "allocation failed");
    for (unsigned i = 0; i < BOXES; ++i)
        if (!quad(&next->quads[i], portable_mesh_item_art_box(art, i), 32))
            goto invalid;
    const uint16_t ids[3] = {0x1000, 0x1002, 0x1004};
    for (unsigned kind = 0; kind < 3u; ++kind)
        for (unsigned i = 0; i < 6u; ++i)
            if (!quad(&next->quads[SHELL_FIRST + kind * 6u + i],
                      portable_mesh_item_art_actor(art, ids[kind], i), 16))
                goto invalid;
    if (!quad(&next->quads[BANANA], portable_mesh_item_art_actor(art, 0x1005, 0), 16) ||
        !quad(&next->quads[BOO], portable_mesh_item_art_actor(art, 0x100b, 0), 16))
        goto invalid;
    portable_mesh_item_scene_close(*owner);
    *owner = next;
    if (error && capacity)
        error[0] = 0;
    return 1;
invalid:
    free(next);
    return fail(error, capacity, 0, 0, "missing or invalid immutable item image");
}
static int open_model(ItemModel *model, const PortableModelBinding *binding,
                      const PortableAssetPackView *pack, double kart_scale, char *error, size_t capacity)
{
    if (!portable_render_resources_open_model(&model->resources, binding, pack, error, capacity))
        return 0;
    /* Model units are metres at the platform's course scale; the binding's
     * scale (uniform) and the course's kart scale (META mksc:kart-scale)
     * apply on top, so items stay in proportion to the karts. */
    model->scale = (double)binding->scale_q16[0] / 65536.0 * kart_scale;
    model->flags = binding->pose_override;
    if (binding->mesh.material_count)
        model->overlay = binding->mesh.materials[0].texture;
    for (unsigned axis = 0; axis < 3u; ++axis)
        model->pivot[axis] = (double)binding->pivot_q16[axis] / 65536.0;
    for (uint32_t v = 0; v < model->resources.mesh.vertex_count; ++v)
    {
        const double *p = model->resources.mesh.vertices[v].position;
        model->extent = fmax(model->extent, hypot(p[0], p[1]));
    }
    PortableModelRig rig;
    if (portable_model_rig_open(pack, binding->mesh_id, &binding->mesh, &rig))
    {
        const int clip = portable_model_rig_clip(&rig, PORTABLE_MODEL_CLIP_TAG('I', 'D', 'L', 'E'));
        const uint32_t frames = clip < 0 ? 0u : rig.clips[clip].frames;
        const PortableRenderMesh *rest = &model->resources.mesh;
        if (frames && frames <= RIG_FRAMES)
        {
            model->frames = calloc(frames, sizeof *model->frames);
            model->frame_vertices = calloc((size_t)frames * rest->vertex_count, sizeof *model->frame_vertices);
            if (!model->frames || !model->frame_vertices)
                return fail(error, capacity, 0, binding->mesh_id, "rig frame allocation failed");
            for (uint32_t frame = 0; frame < frames; ++frame)
            {
                PortableRenderVertex *vertices = model->frame_vertices + (size_t)frame * rest->vertex_count;
                if (!portable_render_model_skin(rest, &rig, (uint32_t)clip, frame, vertices))
                    return fail(error, capacity, 0, binding->mesh_id, "invalid item model rig");
                model->frames[frame] = *rest;
                model->frames[frame].vertices = vertices;
            }
            model->frame_count = frames;
        }
    }
    model->present = 1;
    return 1;
}
int portable_mesh_item_scene_open_models(PortableMeshItemScene *scene, const PortableAssetPackView *pack,
                                         uint32_t profile_id, double kart_scale, char *error, size_t capacity)
{
    if (!(kart_scale > 0))
        return fail(error, capacity, 0, profile_id, "invalid item kart scale");
    if (!scene || !pack)
        return fail(error, capacity, 0, profile_id, "invalid item model scene/pack");
    close_models(scene);
    PortableModelCatalog catalog = {0};
    if (!portable_model_catalog_open(&catalog, pack))
        return fail(error, capacity, 0, profile_id, "item model catalog");
    unsigned opened = 0;
    for (unsigned slot = 0; slot < PORTABLE_MODEL_ITEM_SLOTS; ++slot)
    {
        const PortableModelBinding *binding = portable_model_catalog_find_item(&catalog, profile_id, slot);
        if (!binding)
            continue;
        if (slot >= PORTABLE_MODEL_ITEM_ICON)
        {
            if (!binding->mesh.material_count || !binding->mesh.materials[0].texture.pixels)
            {
                close_models(scene);
                portable_model_catalog_close(&catalog);
                return fail(error, capacity, 0, profile_id, "item icon mesh has no texture");
            }
            scene->icons[slot - PORTABLE_MODEL_ITEM_ICON] = binding->mesh.materials[0].texture;
            ++opened;
            continue;
        }
        if (slot >= PORTABLE_MODEL_ITEM_MODELS)
            continue;
        if (!open_model(&scene->models[slot], binding, pack, kart_scale, error, capacity))
        {
            close_models(scene);
            portable_model_catalog_close(&catalog);
            return 0;
        }
        ++opened;
    }
    portable_model_catalog_close(&catalog);
    if (opened)
        printf("3D item models: profile=%u models=%u\n", profile_id, opened);
    if (error && capacity)
        error[0] = 0;
    return 1;
}
const PortableNativeImage *portable_mesh_item_scene_icon(const PortableMeshItemScene *scene, uint16_t item)
{
    if (!scene || item < 0x1000u || item - 0x1000u >= PORTABLE_MODEL_ITEM_ICONS)
        return NULL;
    const PortableNativeImage *icon = &scene->icons[item - 0x1000u];
    return icon->pixels ? icon : NULL;
}
static const ItemQuad *actor_quad(const PortableMeshItemScene *scene, uint16_t item, unsigned frame)
{
    switch (item)
    {
    case 0x1000:
    case 0x1001:
        return &scene->quads[SHELL_FIRST + frame];
    case 0x1002:
    case 0x1003:
        return &scene->quads[SHELL_FIRST + 6u + frame];
    case 0x1004:
        return &scene->quads[SHELL_FIRST + 12u + frame];
    case 0x1005:
        return &scene->quads[BANANA];
    case 0x100b:
        return &scene->quads[BOO];
    case PORTABLE_MESH_ITEM_FAKE_BOX:
        return &scene->quads[0];
    default:
        /* Dropped items the stock art lacks (Bob-omb, banana lines) fall back to the banana. */
        return item > PORTABLE_MESH_ITEM_FAKE_BOX && item <= PORTABLE_MESH_ITEM_LAST ? &scene->quads[BANANA] : NULL;
    }
}
/* The platform model drawn for an actor of item, if any: triples draw their
 * single item's model. */
static const ItemModel *actor_model(const PortableMeshItemScene *scene, uint16_t item)
{
    if (item == 0x1001 || item == 0x1003)
        --item;
    if (item < 0x1000 || item - 0x1000u >= 32u)
        return NULL;
    const ItemModel *model = &scene->models[item - 0x1000u];
    return model->present ? model : NULL;
}
static int bounds(const int32_t position[3], double extent)
{
    /* A cylindrical billboard may turn its horizontal extent onto either
     * world axis. Preflight that envelope before any caller writes. */
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        if (position[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            position[axis] > PORTABLE_FIXED_3D_POSITION_LIMIT)
            return 0;
        const double value = (double)position[axis] / 65536.0;
        if (value - extent < -4096.0 || value + extent > 4096.0)
            return 0;
    }
    return 1;
}
static void instance(PortableRenderInstance *out, const ItemQuad *q, const int32_t position[3],
                     double diameter)
{
    portable_render_instance_identity(out, &q->mesh);
    out->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        out->billboard_scale[axis] = diameter;
        out->transform[axis * 4u + 3u] = (double)position[axis] / 65536.0;
    }
}
/* A model instance at a world point (metres), turned yaw radians about up
 * and scaled by size times the model's own scale. */
static void model_instance(PortableRenderInstance *out, const ItemModel *model, uint32_t tick,
                           const double position[3], double yaw, double size)
{
    const PortableRenderMesh *mesh =
        model->frame_count ? &model->frames[tick % model->frame_count] : &model->resources.mesh;
    portable_render_instance_identity(out, mesh);
    const double scale = model->scale * size;
    /* The authored pivot lands on the world point. */
    double offset[3] = {-model->pivot[0] * scale, -model->pivot[1] * scale, -model->pivot[2] * scale};
    if (model->flags & PORTABLE_MODEL_ITEM_FLAG_BILLBOARD)
    {
        out->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
        for (unsigned axis = 0; axis < 3u; ++axis)
            out->billboard_scale[axis] = scale;
    }
    else
    {
        const double c = cos(yaw), s = sin(yaw);
        const double rows[12] = {c * scale, -s * scale, 0, 0, s * scale, c * scale, 0, 0, 0, 0, scale, 0};
        memcpy(out->transform, rows, sizeof rows);
        const double x = offset[0], y = offset[1];
        offset[0] = c * x - s * y;
        offset[1] = s * x + c * y;
    }
    for (unsigned axis = 0; axis < 3u; ++axis)
        out->transform[axis * 4u + 3u] = position[axis] + offset[axis];
}
int portable_mesh_item_scene_rider(const PortableMeshItemScene *scene, uint16_t item, const double position[3],
                                   double yaw, uint32_t tick, PortableRenderInstance *out)
{
    if (!scene || !position || !out || item < 0x1000u || item - 0x1000u >= PORTABLE_MODEL_ITEM_MODELS)
        return 0;
    const ItemModel *model = &scene->models[item - 0x1000u];
    if (!model->present)
        return 0;
    model_instance(out, model, tick, position, yaw, 1.0);
    return 1;
}
const PortableNativeImage *portable_mesh_item_scene_overlay(const PortableMeshItemScene *scene, unsigned slot)
{
    if (!scene || slot >= PORTABLE_MODEL_ITEM_MODELS || !scene->models[slot].present)
        return NULL;
    const PortableNativeImage *image = &scene->models[slot].overlay;
    return image->pixels ? image : NULL;
}
static void metres(const int32_t q16[3], double out[3])
{
    for (unsigned axis = 0; axis < 3u; ++axis)
        out[axis] = (double)q16[axis] / 65536.0;
}
static int box_shattering(const PortableMeshItemScene *scene, const PortableCourse3DItem *item, uint16_t cooldown)
{
    return scene->models[PORTABLE_MODEL_ITEM_BOX_SHARD].present && cooldown &&
           item->respawn_ticks >= cooldown && item->respawn_ticks - cooldown < SHARD_TICKS;
}
int portable_mesh_item_scene_append(const PortableMeshItemScene *scene,
                                    const PortableCourse3DView *course,
                                    const PortableMeshItems *world,
                                    const PortableMeshItemRules *rules, uint32_t mode, PortableRenderInstance *out,
                                    size_t capacity, size_t *count, char *error,
                                    size_t error_capacity)
{
    const uint32_t id = course ? course->id : 0;
    if (!scene || !course || !world || !portable_mesh_item_rules_valid(rules) || !out || !count ||
        *count > capacity || (mode != 1u && mode != 2u && mode != 4u && mode != 8u && mode != 16u) || !world->inventory.count || world->inventory.count > 8u ||
        world->status.count != world->inventory.count)
        return fail(error, error_capacity, id, 0, "invalid immutable scene inputs");
    const PortableCourse3DTable *table =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'));
    const uint32_t boxes = table ? table->count : 0;
    if (table && (table->stride != 32u || (boxes && !table->bytes)))
        return fail(error, error_capacity, id, 0, "invalid ITEM table storage");
    if (boxes > PORTABLE_MESH_ITEM_BOX_CAPACITY || world->boxes.count != boxes)
        return fail(error, error_capacity, id, 0, "ITEM/cooldown count mismatch");
    const double diameter = 2.0 * (double)rules->actors_radius_q16 / 65536.0;
    size_t needed = 0, shards = 0;
    for (uint32_t i = 0; i < boxes; ++i)
    {
        PortableCourse3DItem item;
        if (!portable_course_3d_item(course, i, &item) || !bounds(item.position_q16, diameter))
            return fail(error, error_capacity, id, i, "invalid ITEM placement or billboard bounds");
        if (world->boxes.cooldowns[i] > item.respawn_ticks)
            return fail(error, error_capacity, id, i, "invalid ITEM cooldown");
        if (mode != 2u && !world->boxes.cooldowns[i])
            ++needed;
        if (mode != 2u && box_shattering(scene, &item, world->boxes.cooldowns[i]) &&
            shards + SHARDS_PER_BOX <= PORTABLE_MESH_ITEM_SCENE_EFFECTS)
            shards += SHARDS_PER_BOX;
    }
    PortableMeshItemPose poses[PORTABLE_MESH_ITEMS_ACTORS];
    const size_t actors = portable_mesh_items_actor_poses(world, poses, PORTABLE_MESH_ITEMS_ACTORS);
    if (actors > PORTABLE_MESH_ITEMS_ACTORS)
        return fail(error, error_capacity, id, 0, "actor pose capacity exceeded");
    const unsigned animation = (world->tick / 2u) % 6u;
    for (size_t i = 0; i < actors; ++i)
    {
        const PortableMeshItemPose *p = &poses[i];
        if (!p->phase || p->phase > PORTABLE_MESH_ACTOR_BLAST ||
            p->owner >= world->inventory.count ||
            (!actor_model(scene, p->item) && !actor_quad(scene, p->item, animation)) ||
            !bounds(p->position_q16, diameter * .5))
            return fail(error, error_capacity, id, (uint32_t)i,
                        "invalid actor pose/artwork bounds");
        if (p->phase != PORTABLE_MESH_ACTOR_ORBIT_PENDING)
            ++needed;
    }
    if (needed + shards > capacity - *count)
        return fail(error, error_capacity, id, 0, "instance capacity exceeded");
    size_t written = *count;
    const ItemModel *box = scene->models[PORTABLE_MODEL_ITEM_BOX].present
                               ? &scene->models[PORTABLE_MODEL_ITEM_BOX] : NULL;
    const ItemModel *shard = &scene->models[PORTABLE_MODEL_ITEM_BOX_SHARD];
    size_t shard_budget = shards;
    for (uint32_t i = 0; i < boxes && mode != 2u; ++i)
    {
        /* Both passes read the same immutable typed content. All decoder
         * failure conditions were checked above, before any output write. */
        PortableCourse3DItem item = {0};
        (void)portable_course_3d_item(course, i, &item);
        const uint16_t cooldown = world->boxes.cooldowns[i];
        double at[3];
        metres(item.position_q16, at);
        if (!cooldown)
        {
            if (box)
                model_instance(&out[written++], box, world->tick + i * 7u, at, 0.0, 1.0);
            else
                instance(&out[written++], &scene->quads[world->tick & 127u], item.position_q16, diameter);
        }
        else if (box_shattering(scene, &item, cooldown) && shard_budget >= SHARDS_PER_BOX)
        {
            /* Collected: the pieces burst outward and fall (presentation of
             * the cooldown clock; no simulation state). */
            shard_budget -= SHARDS_PER_BOX;
            const double t = (double)(item.respawn_ticks - cooldown) / 59.7275;
            for (unsigned piece = 0; piece < SHARDS_PER_BOX; ++piece)
            {
                const double heading = (double)piece * (6.283185307179586 / SHARDS_PER_BOX) + i;
                const double speed = 2.5 + (double)(piece % 3u) * 0.5;
                const double rise = 3.0 + (double)((piece * 5u) % 3u);
                double p[3] = {at[0] + cos(heading) * speed * t, at[1] + sin(heading) * speed * t,
                               at[2] + rise * t - 4.9 * t * t};
                const double fade = 1.0 - (double)(item.respawn_ticks - cooldown) / SHARD_TICKS;
                model_instance(&out[written++], shard, world->tick, p, heading + t * 9.0, 0.6 + 0.4 * fade);
            }
        }
    }
    for (size_t i = 0; i < actors; ++i)
    {
        const PortableMeshItemPose *p = &poses[i];
        if (p->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING)
            continue;
        const ItemModel *model = actor_model(scene, p->item);
        if (p->phase == PORTABLE_MESH_ACTOR_BLAST && scene->models[PORTABLE_MODEL_ITEM_BLAST].present)
        {
            /* The blast swells over its first ticks, then holds until it fades. */
            const double age = (double)(PORTABLE_MESH_BOMB_BLAST_TICKS - p->delay);
            const double grow = fmin(1.0, (age + 1.0) / 8.0);
            double at[3];
            metres(p->position_q16, at);
            model_instance(&out[written++], &scene->models[PORTABLE_MODEL_ITEM_BLAST], world->tick, at,
                           (double)world->tick * 0.1,
                           /* World radius = scale * size * extent: the blast covers its physical radius. */
                           grow * (double)PORTABLE_MESH_BOMB_BLAST_RADIUS_Q16 / 65536.0 /
                               fmax(scene->models[PORTABLE_MODEL_ITEM_BLAST].extent *
                                        scene->models[PORTABLE_MODEL_ITEM_BLAST].scale, 0.01));
            continue;
        }
        if (!model)
        {
            instance(&out[written++], actor_quad(scene, p->item, animation), p->position_q16, diameter);
            continue;
        }
        double at[3];
        metres(p->position_q16, at);
        double yaw = atan2((double)p->velocity_q16[1], (double)p->velocity_q16[0]) + 1.5707963267948966;
        if (model->flags & PORTABLE_MODEL_ITEM_FLAG_SPIN)
            yaw = (double)world->tick * 0.2;
        model_instance(&out[written++], model, world->tick, at, yaw, 1.0);
    }
    *count = written;
    if (error && error_capacity)
        error[0] = 0;
    return 1;
}
