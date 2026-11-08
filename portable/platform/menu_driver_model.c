#include "menu_driver_model.h"

#include "model_asset.h"
#include "render_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The race draws imported karts at .05 m per mesh unit and this body ratio
 * (course_3d_host.c KART_ART_SCALE); the preview keeps the same proportions. */
#define MODEL_METRES .05
#define MODEL_BODY .72
#define SUPERSAMPLE 2u
#define PITCH .30 /* Camera elevation, radians. */

typedef struct
{
    PortableRenderResources resources;
    PortableRenderMaterial *materials; /* Per-render copy: CPU draws static textures. */
    PortableRenderMesh mesh;
} ModelPart;

struct PortableMenuDriverModel
{
    const PortableAssetPackView *pack;
    uint32_t driver_id, requested_kart, kart_id;
    int state; /* 0 closed, 1 ready, -1 failed for this pack/driver/kart. */
    PortableModelCatalog catalog;
    PortableModelBinding driver; /* Pivot moved to the kart pivot minus the seat. */
    ModelPart parts[2];          /* Driver, kart body. */
    PortableModelRig rig;
    int rigged;
    PortableRenderVertex *posed;
    double seat[3];
    double low_z, high_z, radius; /* Turning bounds at yaw 0, metres. */
    PortableRenderCpu cpu;
    uint32_t *samples, *image;
    size_t sample_capacity, image_capacity;
};

PortableMenuDriverModel *portable_menu_driver_model_create(void)
{
    return calloc(1u, sizeof(PortableMenuDriverModel));
}

static void close_content(PortableMenuDriverModel *model)
{
    for (unsigned i = 0; i < 2u; ++i)
    {
        portable_render_resources_close(&model->parts[i].resources);
        free(model->parts[i].materials);
        model->parts[i] = (ModelPart){0};
    }
    portable_model_catalog_close(&model->catalog);
    free(model->posed);
    model->posed = NULL;
    model->rigged = 0;
    model->state = 0;
    model->pack = NULL;
    model->driver_id = model->requested_kart = model->kart_id = 0u;
}

void portable_menu_driver_model_forget(PortableMenuDriverModel *model)
{
    if (model != NULL) close_content(model);
}

void portable_menu_driver_model_destroy(PortableMenuDriverModel *model)
{
    if (model == NULL) return;
    close_content(model);
    portable_render_cpu_close(&model->cpu);
    free(model->samples);
    free(model->image);
    free(model);
}

static int open_part(ModelPart *part, const PortableModelBinding *binding, const PortableAssetPackView *pack,
                     char *error, size_t capacity)
{
    if (!portable_render_resources_open_model(&part->resources, binding, pack, error, capacity)) return 0;
    part->mesh = part->resources.mesh;
    part->materials = calloc(part->mesh.material_count, sizeof *part->materials);
    if (part->materials == NULL)
    {
        snprintf(error, capacity, "out of memory");
        return 0;
    }
    memcpy(part->materials, part->mesh.materials, part->mesh.material_count * sizeof *part->materials);
    part->mesh.materials = part->materials;
    return 1;
}

static void transform_point(const double transform[12], const double in[3], double out[3])
{
    for (unsigned row = 0; row < 3u; ++row)
        out[row] = transform[row * 4u] * in[0] + transform[row * 4u + 1u] * in[1] +
                   transform[row * 4u + 2u] * in[2] + transform[row * 4u + 3u];
}

/* Driver pose at the origin turned by yaw; the body shares it, moved off the seat. */
static int part_transforms(const PortableMenuDriverModel *model, double yaw, double driver[12], double body[12])
{
    const PortableRenderPose pose = {{0, 0, 0}, {-sin(yaw), cos(yaw), 0}, {0, 0, 1}};
    const double scale[3] = {MODEL_BODY, MODEL_BODY, MODEL_BODY};
    if (!portable_render_model_transform_scaled(driver, &model->driver, &pose, 0, MODEL_METRES, scale)) return 0;
    memcpy(body, driver, 12u * sizeof *body);
    for (unsigned row = 0; row < 3u; ++row)
        for (unsigned axis = 0; axis < 3u; ++axis)
            body[row * 4u + 3u] -= body[row * 4u + axis] * model->seat[axis];
    return 1;
}

static int open_content(PortableMenuDriverModel *model, const PortableAssetPackView *pack, uint32_t driver_id,
                        uint32_t kart_id, char *error, size_t capacity)
{
    if (!portable_model_catalog_open(&model->catalog, pack))
    {
        snprintf(error, capacity, "model catalog unavailable");
        return 0;
    }
    const PortableModelBinding *binding =
        portable_model_catalog_find(&model->catalog, PORTABLE_MODEL_DRIVER, driver_id, 0, 0);
    if (binding == NULL)
    {
        snprintf(error, capacity, "no driver model");
        return 0;
    }
    if (kart_id == 0u) kart_id = portable_kart_3d_default(pack, driver_id);
    PortableKart3D kart;
    int32_t seat[3];
    PortableModelBinding body = *binding;
    if (kart_id == 0u || !portable_kart_3d_find(pack, kart_id, &kart) || !portable_kart_3d_seat(&kart, driver_id, seat) ||
        !portable_model_mesh_open(pack, kart.mesh_id, &body.mesh))
    {
        snprintf(error, capacity, "kart %u has no seat or mesh", kart_id);
        return 0;
    }
    body.mesh_id = kart.mesh_id;
    memcpy(body.pivot_q16, kart.pivot_q16, sizeof body.pivot_q16);
    model->driver = *binding;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        model->driver.pivot_q16[axis] = kart.pivot_q16[axis] - seat[axis];
        model->seat[axis] = seat[axis] / 65536.0;
    }
    if (!open_part(&model->parts[0], &model->driver, pack, error, capacity) ||
        !open_part(&model->parts[1], &body, pack, error, capacity))
        return 0;
    const PortableRenderMesh *rest = &model->parts[0].resources.mesh;
    model->rigged = portable_model_rig_open(pack, model->driver.mesh_id, &model->driver.mesh, &model->rig);
    if (model->rigged && (model->posed = malloc(rest->vertex_count * sizeof *model->posed)) == NULL)
    {
        snprintf(error, capacity, "out of memory");
        return 0;
    }
    /* Fit the turning model once so the camera never pumps with yaw. */
    double transforms[2][12];
    if (!part_transforms(model, 0.0, transforms[0], transforms[1]))
    {
        snprintf(error, capacity, "invalid model transform");
        return 0;
    }
    model->low_z = HUGE_VAL;
    model->high_z = -HUGE_VAL;
    model->radius = 0.0;
    for (unsigned part = 0; part < 2u; ++part)
    {
        const PortableRenderMesh *mesh = &model->parts[part].resources.mesh;
        for (uint32_t i = 0; i < mesh->vertex_count; ++i)
        {
            double world[3];
            transform_point(transforms[part], mesh->vertices[i].position, world);
            model->low_z = fmin(model->low_z, world[2]);
            model->high_z = fmax(model->high_z, world[2]);
            model->radius = fmax(model->radius, hypot(world[0], world[1]));
        }
    }
    if (!(model->high_z > model->low_z) || !(model->radius > 0.0) || !isfinite(model->radius))
    {
        snprintf(error, capacity, "empty model bounds");
        return 0;
    }
    printf("Menu driver model: driver=%u kart=%u triangles=%u rigged=%d\n", driver_id, kart_id,
           (model->parts[0].mesh.index_count + model->parts[1].mesh.index_count) / 3u, model->rigged);
    model->kart_id = kart_id;
    return 1;
}

static int prepare(PortableMenuDriverModel *model, const PortableAssetPackView *pack, uint32_t driver_id,
                   uint32_t kart_id)
{
    if (model->state != 0 && model->pack == pack && model->driver_id == driver_id &&
        model->requested_kart == kart_id)
        return model->state > 0;
    close_content(model);
    char error[256] = "";
    const int ready = open_content(model, pack, driver_id, kart_id, error, sizeof error);
    if (!ready)
    {
        close_content(model);
        fprintf(stderr, "Menu driver model: driver=%u kart=%u: %s; showing its fallback art\n", driver_id,
                kart_id, error);
    }
    model->pack = pack;
    model->driver_id = driver_id;
    model->requested_kart = kart_id;
    model->state = ready ? 1 : -1;
    return ready;
}

/* The driver's drive clip at its centre frame (no steering), or the win clip
 * playing from the lock. Unrigged drivers draw their rest mesh. */
static void pose_driver(PortableMenuDriverModel *model, uint32_t tick, int celebrate)
{
    ModelPart *part = &model->parts[0];
    const PortableRenderMesh *rest = &part->resources.mesh;
    part->mesh.vertices = rest->vertices;
    if (!model->rigged) return;
    int clip = celebrate ? portable_model_rig_clip(&model->rig, PORTABLE_MODEL_CLIP_TAG('W', 'I', 'N', ' ')) : -1;
    double frame = 0.0;
    if (clip >= 0)
        frame = (double)tick;
    else
    {
        clip = portable_model_rig_clip(&model->rig, PORTABLE_MODEL_CLIP_TAG('D', 'R', 'I', 'V'));
        if (clip < 0) return;
        const PortableModelClip *drive = &model->rig.clips[clip];
        frame = drive->flags & PORTABLE_MODEL_CLIP_LOOP ? (double)tick : (drive->frames - 1u) * .5;
    }
    if (portable_render_model_skin_sample(rest, &model->rig, (uint32_t)clip, frame, model->posed))
        part->mesh.vertices = model->posed;
}

/* Rig material swaps (the blink) resolve to their texture at tick. */
static void resolve_materials(ModelPart *part, uint32_t tick)
{
    const PortableRenderMesh *rest = &part->resources.mesh;
    for (uint32_t i = 0; i < rest->material_count; ++i)
    {
        part->materials[i] = rest->materials[i];
        if (rest->materials[i].animation == NULL) continue;
        double uv[6];
        const PortableNativeImage *texture = NULL;
        portable_render_material_at(&rest->materials[i], tick, uv, &texture);
        if (texture != NULL) part->materials[i].texture = *texture;
        part->materials[i].animation = NULL;
    }
}

int portable_menu_driver_model_render(PortableMenuDriverModel *model, const PortableAssetPackView *pack,
                                      const PortableMenuDriverModelPose *pose, UiImage *out)
{
    if (model == NULL || pack == NULL || pose == NULL || out == NULL || pose->driver_id == 0u ||
        pose->size < 8u || pose->size > 1024u || !isfinite(pose->yaw) ||
        !prepare(model, pack, pose->driver_id, pose->kart_id))
        return 0;
    const size_t size = pose->size, edge = size * SUPERSAMPLE;
    if (model->sample_capacity < edge * edge)
    {
        uint32_t *samples = realloc(model->samples, edge * edge * sizeof *samples);
        if (samples == NULL) return 0;
        model->samples = samples;
        model->sample_capacity = edge * edge;
    }
    if (model->image_capacity < size * size)
    {
        uint32_t *image = realloc(model->image, size * size * sizeof *image);
        if (image == NULL) return 0;
        model->image = image;
        model->image_capacity = size * size;
    }
    pose_driver(model, pose->tick, pose->celebrate);
    resolve_materials(&model->parts[0], pose->tick);
    resolve_materials(&model->parts[1], pose->tick);
    PortableRenderInstance instances[2];
    for (unsigned part = 0; part < 2u; ++part)
        portable_render_instance_identity(&instances[part], &model->parts[part].mesh);
    if (!part_transforms(model, pose->yaw, instances[0].transform, instances[1].transform)) return 0;
    /* A pitched camera on +Y (the race's ceremony framing) that keeps the
     * whole turning model inside the square with a margin. */
    const double cosine = cos(PITCH), sine = sin(PITCH), focal = (double)edge * 1.6;
    const double target[3] = {0, 0, (model->low_z + model->high_z) * .5};
    const double half_height = (model->high_z - model->low_z) * .5 * cosine + model->radius * sine;
    const double distance = model->radius +
                            focal * fmax(model->radius / ((double)edge * .46), half_height / ((double)edge * .44));
    PortableRenderFrame frame = {.instances = instances, .instance_count = 2u, .view_count = 1u,
                                 .animation_tick = pose->tick};
    PortableRenderView *view = &frame.views[0];
    *view = (PortableRenderView){.width = edge, .height = edge, .clear_argb = 0u,
        .position = {target[0], target[1] + distance * cosine, target[2] + distance * sine},
        .right = {1, 0, 0}, .up = {0, -sine, cosine}, .forward = {0, -cosine, -sine},
        .projection = {focal, focal, (double)edge * .5, (double)edge * .5, .05, 4096.0}};
    PortableSoftwareFrame target_frame = {.pixels = model->samples, .width = edge, .height = edge};
    char error[160];
    if (!portable_render_cpu_draw(&model->cpu, &frame, &target_frame, error, sizeof error))
    {
        fprintf(stderr, "Menu driver model: driver=%u kart=%u: %s\n", pose->driver_id, model->kart_id, error);
        return 0;
    }
    /* Untouched depth is empty: resolve coverage into straight alpha. */
    const uint32_t *depth = model->cpu.panes[0].depth;
    for (size_t y = 0; y < size; ++y)
        for (size_t x = 0; x < size; ++x)
        {
            unsigned covered = 0u, red = 0u, green = 0u, blue = 0u;
            for (unsigned sy = 0; sy < SUPERSAMPLE; ++sy)
                for (unsigned sx = 0; sx < SUPERSAMPLE; ++sx)
                {
                    const size_t at = (y * SUPERSAMPLE + sy) * edge + x * SUPERSAMPLE + sx;
                    if (depth[at] == UINT32_MAX) continue;
                    const uint32_t color = model->samples[at];
                    red += (color >> 16) & 255u;
                    green += (color >> 8) & 255u;
                    blue += color & 255u;
                    ++covered;
                }
            model->image[y * size + x] =
                covered == 0u ? 0u
                              : (uint32_t)(covered * 255u / (SUPERSAMPLE * SUPERSAMPLE)) << 24 |
                                    (red / covered) << 16 | (green / covered) << 8 | blue / covered;
        }
    *out = (UiImage){model->image, (int)size, (int)size};
    return 1;
}

/* Composites a straight-alpha image over the native rectangle of a software
 * frame at its physical scale, sampling the nearest source pixel. */
static void blit(PortableSoftwareFrame *frame, const UiImage *image, long left, long top, long width, long height)
{
    const long scale = (long)portable_software_frame_scale(frame);
    const long frame_width = (long)frame->width * scale, frame_height = (long)frame->height * scale;
    const long x0 = left * scale, y0 = top * scale, w = width * scale, h = height * scale;
    for (long y = 0; y < h; ++y)
    {
        const long py = y0 + y;
        if (py < 0 || py >= frame_height) continue;
        const size_t sy = (size_t)(y * image->height / h);
        for (long x = 0; x < w; ++x)
        {
            const long px = x0 + x;
            if (px < 0 || px >= frame_width) continue;
            const uint32_t color = image->pixels[sy * (size_t)image->width + (size_t)(x * image->width / w)];
            const unsigned alpha = color >> 24;
            if (alpha == 0u) continue;
            uint32_t *pixel = &frame->pixels[(size_t)py * portable_software_frame_stride(frame) + (size_t)px];
            uint32_t mixed = 0xff000000u;
            for (unsigned shift = 0; shift <= 16u; shift += 8u)
            {
                const unsigned over = (color >> shift) & 255u, under = (*pixel >> shift) & 255u;
                mixed |= ((over * alpha + under * (255u - alpha) + 127u) / 255u) << shift;
            }
            *pixel = mixed;
        }
    }
}

int portable_menu_driver_model_draw_frame(PortableMenuDriverModel *model, const PortableAssetPackView *pack,
                                          const PortableMenuDriverModelPose *pose, PortableSoftwareFrame *frame,
                                          int center_x, int center_y)
{
    if (frame == NULL || frame->pixels == NULL || pose == NULL) return 0;
    const size_t scale = portable_software_frame_scale(frame);
    PortableMenuDriverModelPose physical = *pose;
    physical.size = pose->size * (unsigned)scale;
    UiImage image;
    if (!portable_menu_driver_model_render(model, pack, &physical, &image)) return 0;
    const long size = (long)pose->size;
    blit(frame, &image, (long)center_x - size / 2, (long)center_y - size / 2, size, size);
    return 1;
}

int portable_menu_driver_portrait_draw_frame(const PortableAssetPackView *pack, uint32_t driver_id,
                                             PortableSoftwareFrame *frame, int left, int top, int width,
                                             int height, uint32_t backdrop)
{
    PortableDriver3D row;
    PortableModelMaterial material;
    if (pack == NULL || frame == NULL || frame->pixels == NULL || width <= 0 || height <= 0 ||
        !portable_driver_3d_find(pack, driver_id, &row) || !portable_model_material_open(pack, row.portrait, &material) ||
        material.texture.pixels == NULL || material.texture.palette != NULL || material.texture.width == 0u ||
        material.texture.height == 0u)
        return 0;
    const uint32_t fill = backdrop;
    const UiImage solid = {&fill, 1, 1};
    if (backdrop >> 24) blit(frame, &solid, left, top, width, height);
    /* Fit the portrait's aspect inside the rectangle, centred. */
    const UiImage image = {(const uint32_t *)(const void *)material.texture.pixels, material.texture.width,
                           material.texture.height};
    long w = width, h = (long)width * image.height / image.width;
    if (h > height)
    {
        h = height;
        w = (long)height * image.width / image.height;
    }
    blit(frame, &image, left + (width - w) / 2, top + (height - h) / 2, w, h);
    return 1;
}
