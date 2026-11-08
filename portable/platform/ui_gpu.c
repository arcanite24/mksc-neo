#include "startup_trace.h"
#include "ui_gpu.h"
#include "ui_gpu_types.h"
#include "shaders/ui_shaders.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GPU_UI_COMMANDS = 4096, GPU_UI_IMAGES = 1024, GPU_UI_IMAGE_BYTES = 16 * 1024 * 1024 };

typedef struct
{
    uint64_t hash, used;
    unsigned char *source;
    size_t bytes;
    int width, height, glyph;
    SDL_Texture *texture;
} UiGpuImage;

typedef struct
{
    const void *source;
    int width, height, glyph;
    SDL_Texture *texture;
} UiGpuResolvedSource;

typedef struct
{
    SDL_GPURenderState *state;
    SDL_Texture *texture;
    SDL_FRect quad;
    SDL_Rect clip;
    SDL_BlendMode blend;
    SDL_ScaleMode sampling;
} UiGpuDraw;

struct UiGpu
{
    SDL_Renderer *renderer;
    SDL_GPUDevice *device;
    SDL_GPUShader *shader;
    UiGpuDraw *draws;
    size_t count, state_count, image_count, image_bytes, uploaded;
    int width, height, prepared;
    uint64_t frame;
    UiGpuImage images[GPU_UI_IMAGES];
    /* Recorder payloads are immutable only during this prepare. Pointer keys
     * never survive into another command list, even if its arena is reused. */
    UiGpuResolvedSource resolved[GPU_UI_COMMANDS + 1];
    size_t resolved_count;
};

static int ui_gpu_failure(char *error, size_t capacity, const char *action)
{
    if (error != NULL && capacity != 0u)
        snprintf(error, capacity, "GPU UI: %s: %s", action, SDL_GetError());
    return 0;
}

static uint64_t bytes_hash(const unsigned char *bytes, size_t count)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0u; i < count; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static unsigned rounded_byte_product(unsigned value, unsigned alpha)
{
    return (value * alpha + 127u) / 255u;
}

static void retire_image(UiGpu *gpu, UiGpuImage *image)
{
    if (image->texture != NULL) SDL_DestroyTexture(image->texture);
    free(image->source);
    gpu->image_bytes -= image->bytes * (image->glyph ? 5u : 2u);
    *image = (UiGpuImage){0};
}

static SDL_Texture *cached_image_texture(UiGpu *gpu, const void *source, int width, int height, int glyph)
{
    if (source == NULL || width <= 0 || height <= 0 || width > 4096 || height > 4096)
    {
        SDL_SetError("invalid image dimensions");
        return NULL;
    }
    const size_t pixels = (size_t)width * (size_t)height;
    const size_t bytes = pixels * (glyph ? 1u : 4u);
    const size_t owned = bytes + pixels * 4u;
    if (owned > GPU_UI_IMAGE_BYTES)
    {
        SDL_SetError("image exceeds bounded GPU UI cache");
        return NULL;
    }
    const uint64_t hash = bytes_hash(source, bytes);
    for (size_t i = 0u; i < gpu->image_count; ++i)
    {
        UiGpuImage *image = &gpu->images[i];
        if (image->texture != NULL && image->width == width && image->height == height &&
            image->glyph == glyph && image->hash == hash && !memcmp(image->source, source, bytes))
        {
            image->used = gpu->frame;
            return image->texture;
        }
    }
    UiGpuImage *slot = NULL;
    for (;;)
    {
        UiGpuImage *oldest = NULL;
        slot = NULL;
        for (size_t i = 0u; i < gpu->image_count; ++i)
        {
            UiGpuImage *image = &gpu->images[i];
            if (image->texture == NULL)
            {
                if (slot == NULL) slot = image;
            }
            else if (image->used != gpu->frame && (oldest == NULL || image->used < oldest->used)) oldest = image;
        }
        if (slot == NULL && gpu->image_count < GPU_UI_IMAGES) slot = &gpu->images[gpu->image_count];
        if (slot != NULL && gpu->image_bytes <= GPU_UI_IMAGE_BYTES - owned) break;
        if (oldest == NULL)
        {
            SDL_SetError("frame exhausts bounded GPU UI image cache");
            return NULL;
        }
        retire_image(gpu, oldest);
    }
    unsigned char *copy = malloc(bytes);
    uint32_t *converted = malloc(pixels * sizeof *converted);
    SDL_Texture *texture = copy != NULL && converted != NULL
        ? SDL_CreateTexture(gpu->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height)
        : NULL;
    if (texture == NULL)
    {
        free(copy);
        free(converted);
        return NULL;
    }
    memcpy(copy, source, bytes);
    for (size_t i = 0u; i < pixels; ++i)
    {
        if (glyph) converted[i] = UINT32_C(0xff000000) | ((uint32_t)copy[i] << 16);
        else
        {
            uint32_t color;
            memcpy(&color, copy + i * 4u, sizeof color);
            const unsigned alpha = color >> 24;
            converted[i] = (alpha << 24) |
                (rounded_byte_product((color >> 16) & 255u, alpha) << 16) |
                (rounded_byte_product((color >> 8) & 255u, alpha) << 8) |
                rounded_byte_product(color & 255u, alpha);
        }
    }
    const uint64_t upload_started = portable_startup_clock();
    const int valid = SDL_UpdateTexture(texture, NULL, converted, width * 4);
    portable_startup_phase("UI texture upload", upload_started);
    free(converted);
    if (!valid)
    {
        SDL_DestroyTexture(texture);
        free(copy);
        return NULL;
    }
    *slot = (UiGpuImage){hash, gpu->frame, copy, bytes, width, height, glyph, texture};
    if (slot == &gpu->images[gpu->image_count]) ++gpu->image_count;
    gpu->image_bytes += owned;
    gpu->uploaded += pixels * 4u;
    return texture;
}

static SDL_Texture *image_texture(UiGpu *gpu, const void *source, int width, int height, int glyph)
{
    for (size_t i = 0u; i < gpu->resolved_count; ++i)
    {
        const UiGpuResolvedSource *resolved = &gpu->resolved[i];
        if (resolved->source == source && resolved->width == width && resolved->height == height &&
            resolved->glyph == glyph)
            return resolved->texture;
    }
    if (gpu->resolved_count == GPU_UI_COMMANDS + 1u)
    {
        SDL_SetError("frame exhausts bounded GPU UI source resolution table");
        return NULL;
    }
    /* The first occurrence still proves persistent cache identity using the
     * content hash and byte comparison. Its texture is pinned for this frame. */
    SDL_Texture *texture = cached_image_texture(gpu, source, width, height, glyph);
    if (texture != NULL)
        gpu->resolved[gpu->resolved_count++] =
            (UiGpuResolvedSource){source, width, height, glyph, texture};
    return texture;
}

static void paint_color(float out[4], uint32_t argb)
{
    out[0] = (float)((argb >> 16) & 255u);
    out[1] = (float)((argb >> 8) & 255u);
    out[2] = (float)(argb & 255u);
    out[3] = (float)(argb >> 24);
}

static int prepare_paint(UiGpuUniforms *uniforms, const UiPaint *paint)
{
    if (paint->count == 0u || paint->count > UI_PAINT_STOPS) return 0;
    const float dx = paint->x1 - paint->x0, dy = paint->y1 - paint->y0;
    const float squared = dx * dx + dy * dy;
    uniforms->paint_axis[0] = squared > 1e-12f ? dx / squared : 0.0f;
    uniforms->paint_axis[1] = squared > 1e-12f ? dy / squared : 0.0f;
    uniforms->paint_axis[2] = paint->x0 * uniforms->paint_axis[0] + paint->y0 * uniforms->paint_axis[1];
    uniforms->paint_axis[3] = (float)paint->count;
    for (unsigned i = 0u; i < paint->count; ++i)
    {
        uniforms->paint_positions[i] = paint->positions[i];
        paint_color(uniforms->paint_colors[i], paint->colors[i]);
    }
    return 1;
}

static void set_rectangle(float rectangle[4], float x, float y, float w, float h)
{
    rectangle[0] = x; rectangle[1] = y; rectangle[2] = w; rectangle[3] = h;
}

static int prepare_command(UiGpu *gpu, const UiDrawCommand *command, UiGpuDraw *draw,
                            UiGpuUniforms *u)
{
    const UiPaint *paint = NULL;
    UiPaint solid = ui_paint_solid(UINT32_MAX);
    SDL_FRect quad = {0, 0, (float)gpu->width, (float)gpu->height};
    draw->clip = (SDL_Rect){command->clip_x0, command->clip_y0,
        command->clip_x1 - command->clip_x0, command->clip_y1 - command->clip_y0};
    draw->blend = SDL_BLENDMODE_BLEND_PREMULTIPLIED;
    draw->sampling = SDL_SCALEMODE_LINEAR;
    u->effect[0] = 1.0f;
    u->effect[3] = (float)command->kind;
    switch (command->kind)
    {
    case UI_DRAW_CLEAR:
        solid = ui_paint_solid(command->data.clear |
            (command->premultiplied_alpha ? 0u : UINT32_C(0xff000000)));
        draw->blend = SDL_BLENDMODE_NONE;
        draw->clip = (SDL_Rect){0, 0, gpu->width, gpu->height};
        break;
    case UI_DRAW_CHECKER:
        u->auxiliary[0] = (float)gpu->width;
        u->auxiliary[1] = (float)gpu->height;
        u->auxiliary[2] = command->data.checker.scale;
        u->auxiliary[3] = command->data.checker.seconds;
        draw->blend = SDL_BLENDMODE_NONE;
        draw->clip = (SDL_Rect){0, 0, gpu->width, gpu->height};
        break;
    case UI_DRAW_ROUND_RECT:
    case UI_DRAW_STROKE_ROUND_RECT:
    {
        const float w = command->data.box.w, h = command->data.box.h;
        const float feather = fmaxf(1.0f, command->data.box.feather);
        const float lean = fabsf(command->data.box.skew) * h * 0.5f;
        set_rectangle(u->rectangle, command->data.box.x, command->data.box.y, w, h);
        u->shape[0] = fmaxf(0.0f, fminf(command->data.box.radius, fminf(w, h) * 0.5f));
        u->shape[1] = command->data.box.skew;
        u->shape[2] = feather;
        u->shape[3] = command->data.box.stroke;
        u->effect[0] = command->data.box.alpha;
        paint = &command->data.box.paint;
        quad = (SDL_FRect){u->rectangle[0] - lean - feather, u->rectangle[1] - feather,
            w + lean * 2.0f + feather * 2.0f, h + feather * 2.0f};
        break;
    }
    case UI_DRAW_ELLIPSE:
    case UI_DRAW_STROKE_CIRCLE:
    {
        const float cx = command->data.ellipse.cx, cy = command->data.ellipse.cy;
        const float rx = command->data.ellipse.rx, ry = command->data.ellipse.ry;
        const float feather = fmaxf(1.0f, command->data.ellipse.feather);
        set_rectangle(u->rectangle, cx, cy, rx, ry);
        u->shape[2] = feather;
        u->shape[3] = command->data.ellipse.stroke;
        u->effect[0] = command->data.ellipse.alpha;
        paint = &command->data.ellipse.paint;
        quad = (SDL_FRect){cx - rx - feather - 1, cy - ry - feather - 1,
            (rx + feather + 1) * 2, (ry + feather + 1) * 2};
        break;
    }
    case UI_DRAW_CONVEX:
    {
        const int count = command->data.convex.count;
        if (count < 3 || count > 160 || command->data.convex.points == NULL) return 0;
        const UiPoint *points = command->data.convex.points;
        float area = 0, left = points[0].x, right = left, top = points[0].y, bottom = top;
        for (int i = 0; i < count; ++i)
        {
            const UiPoint a = points[i], b = points[(i + 1) % count];
            area += a.x * b.y - b.x * a.y;
            left = fminf(left, a.x); right = fmaxf(right, a.x);
            top = fminf(top, a.y); bottom = fmaxf(bottom, a.y);
        }
        const float orientation = area > 0 ? 1.0f : -1.0f;
        unsigned edges = 0u;
        for (int i = 0; i < count; ++i)
        {
            const UiPoint a = points[i], b = points[(i + 1) % count];
            const float ex = b.x - a.x, ey = b.y - a.y, length = sqrtf(ex * ex + ey * ey);
            if (length < 1e-5f) continue;
            u->edges[edges][0] = orientation * ey / length;
            u->edges[edges][1] = -orientation * ex / length;
            u->edges[edges][2] = u->edges[edges][0] * a.x + u->edges[edges][1] * a.y;
            ++edges;
        }
        u->effect[2] = (float)edges;
        u->effect[0] = fabsf(area) < 1e-4f || edges < 3u ? 0.0f : command->data.convex.alpha;
        u->shape[2] = fmaxf(1.0f, command->data.convex.feather);
        paint = &command->data.convex.paint;
        quad = (SDL_FRect){left - u->shape[2], top - u->shape[2],
            right - left + 2 * u->shape[2], bottom - top + 2 * u->shape[2]};
        break;
    }
    case UI_DRAW_RECT:
        quad = (SDL_FRect){floorf(command->data.rect.x + .5f), floorf(command->data.rect.y + .5f), 0, 0};
        quad.w = floorf(command->data.rect.x + command->data.rect.w + .5f) - quad.x;
        quad.h = floorf(command->data.rect.y + command->data.rect.h + .5f) - quad.y;
        solid = ui_paint_solid(command->data.rect.color);
        u->effect[0] = command->data.rect.alpha;
        set_rectangle(u->rectangle, quad.x, quad.y, quad.w, quad.h);
        break;
    case UI_DRAW_TINT:
        quad = (SDL_FRect){(float)command->data.tint.x0, (float)command->data.tint.y0,
            (float)command->data.tint.x1 - (float)command->data.tint.x0,
            (float)command->data.tint.y1 - (float)command->data.tint.y0};
        solid = ui_paint_solid(command->data.tint.color | UINT32_C(0xff000000));
        u->effect[0] = command->data.tint.amount;
        set_rectangle(u->rectangle, quad.x, quad.y, quad.w, quad.h);
        draw->blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        break;
    case UI_DRAW_IMAGE:
    {
        const UiImage *image = command->data.image.source;
        if (image == NULL) return 0;
        draw->texture = image_texture(gpu, image->pixels, image->width, image->height, 0);
        if (draw->texture == NULL) return 0;
        set_rectangle(u->rectangle, command->data.image.x, command->data.image.y,
            command->data.image.w, command->data.image.h);
        u->shape[0] = fmaxf(0.0f, fminf(command->data.image.radius,
            fminf(command->data.image.w, command->data.image.h) * .5f));
        u->effect[0] = command->data.image.alpha;
        u->effect[1] = (float)command->data.image.flags;
        draw->sampling = command->data.image.flags & UI_IMAGE_NEAREST
            ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
        set_rectangle(u->source, command->data.image.sx / (float)image->width,
            command->data.image.sy / (float)image->height,
            command->data.image.sw / (float)image->width, command->data.image.sh / (float)image->height);
        solid = ui_paint_solid(command->data.image.tint);
        quad = (SDL_FRect){floorf(u->rectangle[0]), floorf(u->rectangle[1]), 0, 0};
        quad.w = ceilf(u->rectangle[0] + u->rectangle[2]) - quad.x;
        quad.h = ceilf(u->rectangle[1] + u->rectangle[3]) - quad.y;
        break;
    }
    case UI_DRAW_GLYPH:
        draw->texture = image_texture(gpu, command->data.glyph.field,
            command->data.glyph.width, command->data.glyph.height, 1);
        if (draw->texture == NULL) return 0;
        set_rectangle(u->rectangle, command->data.glyph.x, command->data.glyph.y,
            (float)command->data.glyph.width * command->data.glyph.scale,
            (float)command->data.glyph.height * command->data.glyph.scale);
        u->auxiliary[0] = command->data.glyph.scale;
        u->auxiliary[1] = command->data.glyph.outline;
        u->auxiliary[2] = command->data.glyph.softness;
        u->effect[0] = command->data.glyph.alpha;
        solid = ui_paint_solid(command->data.glyph.color);
        quad = (SDL_FRect){floorf(u->rectangle[0]), floorf(u->rectangle[1]), 0, 0};
        quad.w = ceilf(u->rectangle[0] + u->rectangle[2]) - quad.x;
        quad.h = ceilf(u->rectangle[1] + u->rectangle[3]) - quad.y;
        break;
    default: return 0;
    }
    if (!prepare_paint(u, paint != NULL ? paint : &solid)) return 0;
    set_rectangle(u->quad, quad.x, quad.y, quad.w, quad.h);
    draw->quad = quad;
    const float *values = (const float *)u;
    for (size_t i = 0u; i < sizeof *u / sizeof *values; ++i) if (!isfinite(values[i])) return 0;
    return 1;
}

UiGpu *ui_gpu_create(SDL_Renderer *renderer, char *error, size_t capacity)
{
    SDL_GPUDevice *device = renderer != NULL ? SDL_GetGPURendererDevice(renderer) : NULL;
    if (device == NULL)
    {
        SDL_SetError("renderer has no shared SDL_GPU device");
        ui_gpu_failure(error, capacity, "device");
        return NULL;
    }
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat format = 0;
    const PortableUiShaderCode *code = NULL;
    if (formats & SDL_GPU_SHADERFORMAT_MSL)
    {
        format = SDL_GPU_SHADERFORMAT_MSL;
        code = &portable_ui_fragment_msl_code;
    }
    else if (formats & SDL_GPU_SHADERFORMAT_DXIL)
    {
        format = SDL_GPU_SHADERFORMAT_DXIL;
        code = &portable_ui_fragment_dxil_code;
    }
    else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
    {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        code = &portable_ui_fragment_spirv_code;
    }
    if (code == NULL)
    {
        SDL_SetError("no authored UI shader format");
        ui_gpu_failure(error, capacity, "shader");
        return NULL;
    }
    UiGpu *gpu = calloc(1u, sizeof *gpu);
    if (gpu == NULL) return NULL;
    gpu->renderer = renderer;
    gpu->device = device;
    gpu->draws = calloc(GPU_UI_COMMANDS, sizeof *gpu->draws);
    const SDL_GPUShaderCreateInfo info = {.code_size = code->size, .code = code->bytes,
        .entrypoint = code->entrypoint, .format = format, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1u, .num_uniform_buffers = 1u};
    if (gpu->draws != NULL) gpu->shader = SDL_CreateGPUShader(device, &info);
    if (gpu->shader == NULL)
    {
        ui_gpu_failure(error, capacity, "create shader");
        ui_gpu_close(gpu);
        return NULL;
    }
    return gpu;
}

int ui_gpu_prepare(UiGpu *gpu, const UiDrawList *list, int width, int height,
                   char *error, size_t capacity)
{
    if (gpu == NULL) return 0;
    gpu->prepared = 0;
    gpu->resolved_count = 0u;
    gpu->count = 0u;
    gpu->uploaded = 0u;
    if (list == NULL || ui_draw_list_error(list)[0] != '\0' || ui_draw_list_count(list) == 0u ||
        width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        ui_draw_list_count(list) > GPU_UI_COMMANDS)
    {
        SDL_SetError("invalid or incomplete UI command list");
        return ui_gpu_failure(error, capacity, "preflight");
    }
    /* Retiring or updating states must not mutate any still-queued SDL draws.
     * This submits queued compositor work; it never waits for the GPU. */
    if (!SDL_FlushRenderer(gpu->renderer)) return ui_gpu_failure(error, capacity, "frame boundary");
    gpu->width = width;
    gpu->height = height;
    if (++gpu->frame == 0u)
    {
        ui_gpu_forget_images(gpu);
        gpu->frame = 1u;
    }
    const uint32_t white = UINT32_MAX;
    SDL_Texture *white_texture = image_texture(gpu, &white, 1, 1, 0);
    if (white_texture == NULL) return ui_gpu_failure(error, capacity, "white image");
    const size_t count = ui_draw_list_count(list);
    for (size_t i = 0u; i < count; ++i)
    {
        UiGpuDraw *draw = &gpu->draws[i];
        draw->texture = white_texture;
        UiGpuUniforms uniforms = {0};
        const UiDrawCommand *command = ui_draw_list_command(list, i);
        if (command == NULL || !prepare_command(gpu, command, draw, &uniforms))
            return ui_gpu_failure(error, capacity, "command resources");
        if (draw->state == NULL)
        {
            const SDL_GPURenderStateCreateInfo info = {.fragment_shader = gpu->shader};
            draw->state = SDL_CreateGPURenderState(gpu->renderer, &info);
            if (draw->state == NULL) return ui_gpu_failure(error, capacity, "draw state");
            gpu->state_count = i + 1u;
        }
        if (!SDL_SetGPURenderStateFragmentUniforms(draw->state, 0u, &uniforms, sizeof uniforms))
            return ui_gpu_failure(error, capacity, "draw uniforms");
        ++gpu->count;
    }
    gpu->prepared = 1;
    return 1;
}

int ui_gpu_draw(UiGpu *gpu, const SDL_FRect *target, char *error, size_t capacity)
{
    if (gpu == NULL || !gpu->prepared || target == NULL || !isfinite(target->x) ||
        !isfinite(target->y) || !isfinite(target->w) || !isfinite(target->h) ||
        fabsf(target->x) > 65536 || fabsf(target->y) > 65536 ||
        target->w <= 0 || target->h <= 0 || target->w > 65536 || target->h > 65536)
        return 0;
    SDL_Renderer *renderer = gpu->renderer;
    SDL_Rect previous_clip, previous_viewport;
    float previous_sx, previous_sy;
    SDL_TextureAddressMode previous_u, previous_v;
    const int had_clip = SDL_RenderClipEnabled(renderer);
    if (!SDL_GetRenderClipRect(renderer, &previous_clip) ||
        !SDL_GetRenderViewport(renderer, &previous_viewport) ||
        !SDL_GetRenderScale(renderer, &previous_sx, &previous_sy) ||
        !SDL_GetRenderTextureAddressMode(renderer, &previous_u, &previous_v))
        return ui_gpu_failure(error, capacity, "save compositor state");
    int valid = SDL_SetRenderScale(renderer, 1, 1) && SDL_SetRenderViewport(renderer, NULL) &&
        SDL_SetRenderTextureAddressMode(renderer, SDL_TEXTURE_ADDRESS_CLAMP, SDL_TEXTURE_ADDRESS_CLAMP);
    const float sx = target->w / (float)gpu->width, sy = target->h / (float)gpu->height;
    static const int indices[6] = {0, 1, 2, 0, 2, 3};
    for (size_t i = 0u; valid && i < gpu->count; ++i)
    {
        UiGpuDraw *draw = &gpu->draws[i];
        if (draw->quad.w <= 0 || draw->quad.h <= 0 || draw->clip.w <= 0 || draw->clip.h <= 0) continue;
        const float x0 = target->x + draw->quad.x * sx, y0 = target->y + draw->quad.y * sy;
        const float x1 = x0 + draw->quad.w * sx, y1 = y0 + draw->quad.h * sy;
        const SDL_Vertex vertices[4] = {
            {{x0, y0}, {1, 1, 1, 1}, {0, 0}}, {{x1, y0}, {1, 1, 1, 1}, {1, 0}},
            {{x1, y1}, {1, 1, 1, 1}, {1, 1}}, {{x0, y1}, {1, 1, 1, 1}, {0, 1}}};
        const int left = (int)floorf(target->x + (float)draw->clip.x * sx);
        const int top = (int)floorf(target->y + (float)draw->clip.y * sy);
        const SDL_Rect clip = {left, top,
            (int)ceilf(target->x + ((float)draw->clip.x + (float)draw->clip.w) * sx) - left,
            (int)ceilf(target->y + ((float)draw->clip.y + (float)draw->clip.h) * sy) - top};
        valid = SDL_SetRenderClipRect(renderer, &clip) &&
            SDL_SetTextureScaleMode(draw->texture, draw->sampling) &&
            SDL_SetTextureBlendMode(draw->texture, draw->blend) &&
            SDL_SetGPURenderState(renderer, draw->state) &&
            SDL_RenderGeometry(renderer, draw->texture, vertices, 4, indices, 6);
    }
    const int cleared = SDL_SetGPURenderState(renderer, NULL);
    const int scale_restored = SDL_SetRenderScale(renderer, previous_sx, previous_sy);
    const int viewport_restored = SDL_SetRenderViewport(renderer, &previous_viewport);
    const int clip_restored = SDL_SetRenderClipRect(renderer, had_clip ? &previous_clip : NULL);
    const int address_restored = SDL_SetRenderTextureAddressMode(renderer, previous_u, previous_v);
    gpu->prepared = 0;
    if (!valid || !cleared || !scale_restored || !viewport_restored || !clip_restored || !address_restored)
        return ui_gpu_failure(error, capacity, "geometry submission");
    return 1;
}

void ui_gpu_forget_images(UiGpu *gpu)
{
    if (gpu == NULL) return;
    (void)SDL_FlushRenderer(gpu->renderer);
    for (unsigned i = 0u; i < GPU_UI_IMAGES; ++i) retire_image(gpu, &gpu->images[i]);
    gpu->image_count = 0u;
    gpu->resolved_count = 0u;
    gpu->prepared = 0;
}

size_t ui_gpu_uploaded_bytes(const UiGpu *gpu)
{
    return gpu != NULL ? gpu->uploaded : 0u;
}

void ui_gpu_close(UiGpu *gpu)
{
    if (gpu == NULL) return;
    (void)SDL_SetGPURenderState(gpu->renderer, NULL);
    (void)SDL_FlushRenderer(gpu->renderer);
    if (gpu->draws != NULL)
        for (size_t i = 0u; i < gpu->state_count; ++i)
            if (gpu->draws[i].state != NULL) SDL_DestroyGPURenderState(gpu->draws[i].state);
    ui_gpu_forget_images(gpu);
    if (gpu->shader != NULL) SDL_ReleaseGPUShader(gpu->device, gpu->shader);
    free(gpu->draws);
    free(gpu);
}
