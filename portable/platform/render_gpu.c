#include "render_gpu.h"
#include "shaders/scene_shaders.h"
#include "shaders/hd2d_shaders.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    float position[3], uv[2];
    uint8_t color[4]; /* RGBA */
    float normal[3];
} GpuVertex;
typedef struct
{
    uint32_t base_argb, tint_argb, cutoff_q16, alpha_mode;
    float uv_offset[2];
    uint32_t padding[2];
    float uv_u[4], uv_v[4];
    float environment[20]; /* portable_render_paint_environment */
} GpuPaint;
_Static_assert(sizeof(GpuVertex) == PORTABLE_SCENE_SHADER_VERTEX_STRIDE, "scene shader vertex layout");
_Static_assert(sizeof(GpuPaint) == PORTABLE_SCENE_SHADER_FRAGMENT_UNIFORM_BYTES, "scene shader paint layout");

/* Pipelines: culled and two-sided opaque, the depth-free clear quad (also
 * two-sided backgrounds), multiply, additive, culled and two-sided alpha
 * blending, then culled opaque and both blended backgrounds. */
enum
{
    PIPELINE_CULLED,
    PIPELINE_TWO_SIDED,
    PIPELINE_BACKDROP,
    PIPELINE_MULTIPLY,
    PIPELINE_ADDITIVE,
    PIPELINE_BLEND,
    PIPELINE_BLEND_TWO_SIDED,
    PIPELINE_BACKDROP_CULLED,
    PIPELINE_BACKDROP_BLEND,
    PIPELINE_BACKDROP_BLEND_TWO_SIDED,
    PIPELINE_COUNT
};

typedef struct GpuTextureCache
{
    PortableNativeImage image;
    SDL_GPUTexture *texture;
    uint64_t used; /* last frame that drew it */
    struct GpuTextureCache *next;
} GpuTextureCache;
typedef struct GpuGeometryCache
{
    const PortableRenderVertex *source_vertices;
    const uint32_t *source_indices;
    uint32_t vertex_count, index_count, revision;
    SDL_GPUBuffer *vertices, *indices;
    uint64_t used;
    struct GpuGeometryCache *next;
} GpuGeometryCache;
typedef struct GpuMeshCache
{
    const PortableRenderMesh *source;
    GpuGeometryCache *geometry;
    uint32_t material_count;
    GpuTextureCache **textures; /* NULL draws the shared white texture */
    uint64_t used;
    struct GpuMeshCache *next;
} GpuMeshCache;

struct PortableRenderGpu
{
    SDL_Renderer *renderer;
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipelines[PIPELINE_COUNT];
    SDL_GPUSampler *samplers[5]; /* Nearest wrap modes, then linear clamp. */
    SDL_GPUTexture *color, *depth, *white, *scene;
    SDL_GPUGraphicsPipeline *finish;
    float hd2d[6]; /* DOF, bloom, AO, grade, haze, billboard lighting. */
    int post_target;
    SDL_Texture *wrapper;
    SDL_GPUBuffer *sky_vertices, *sky_indices;
    SDL_GPUTransferBuffer *download;
    size_t width, height, download_capacity;
    GpuMeshCache *meshes;
    GpuGeometryCache *geometry;
    GpuTextureCache *textures;
    size_t mesh_count, texture_count, geometry_count;
    uint64_t frame;
};

static int failure(char *error, size_t capacity, const char *action)
{
    if (error != NULL && capacity != 0u)
        snprintf(error, capacity, "GPU scene: %s: %s", action, SDL_GetError());
    return 0;
}

static SDL_GPUBuffer *upload_buffer(PortableRenderGpu *gpu, SDL_GPUBufferUsageFlags usage,
                                    const void *bytes, Uint32 size)
{
    SDL_GPUBufferCreateInfo info = {.usage = usage, .size = size};
    SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(gpu->device, &info);
    SDL_GPUTransferBufferCreateInfo transfer_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                     .size = size};
    SDL_GPUTransferBuffer *transfer =
        buffer != NULL ? SDL_CreateGPUTransferBuffer(gpu->device, &transfer_info) : NULL;
    void *mapped = transfer != NULL ? SDL_MapGPUTransferBuffer(gpu->device, transfer, false) : NULL;
    SDL_GPUCommandBuffer *commands = NULL;
    int valid = 0;
    if (mapped != NULL)
    {
        memcpy(mapped, bytes, size);
        SDL_UnmapGPUTransferBuffer(gpu->device, transfer);
        commands = SDL_AcquireGPUCommandBuffer(gpu->device);
        SDL_GPUCopyPass *copy = commands != NULL ? SDL_BeginGPUCopyPass(commands) : NULL;
        if (copy != NULL)
        {
            const SDL_GPUTransferBufferLocation source = {.transfer_buffer = transfer};
            const SDL_GPUBufferRegion destination = {.buffer = buffer, .size = size};
            SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            valid = SDL_SubmitGPUCommandBuffer(commands);
            commands = NULL;
        }
    }
    if (commands != NULL)
        SDL_CancelGPUCommandBuffer(commands);
    if (transfer != NULL)
        SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
    if (!valid && buffer != NULL)
        SDL_ReleaseGPUBuffer(gpu->device, buffer);
    return valid ? buffer : NULL;
}

static SDL_GPUTexture *upload_image(PortableRenderGpu *gpu, const PortableNativeImage *image)
{
    const Uint32 width = image != NULL ? (Uint32)image->width : 1u;
    const Uint32 height = image != NULL ? (Uint32)image->height : 1u;
    /* Tight rows are portable across SDL's GPU upload implementations. SDL
     * aligns a temporary copy for D3D12 when needed; each asset uploads once. */
    const Uint32 pitch = width;
    const uint64_t byte_count = (uint64_t)pitch * height * 4u;
    if (width == 0u || height == 0u || byte_count > UINT32_MAX)
        return NULL;
    SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                     .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                     .width = width,
                                     .height = height,
                                     .layer_count_or_depth = 1u,
                                     .num_levels = 1u,
                                     .sample_count = SDL_GPU_SAMPLECOUNT_1};
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu->device, &info);
    SDL_GPUTransferBufferCreateInfo transfer_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                     .size = (Uint32)byte_count};
    SDL_GPUTransferBuffer *transfer =
        texture != NULL ? SDL_CreateGPUTransferBuffer(gpu->device, &transfer_info) : NULL;
    uint8_t *mapped =
        transfer != NULL ? SDL_MapGPUTransferBuffer(gpu->device, transfer, false) : NULL;
    SDL_GPUCommandBuffer *commands = NULL;
    int valid = 0;
    if (mapped != NULL)
    {
        memset(mapped, 0, (size_t)byte_count);
        for (Uint32 y = 0u; y < height; ++y)
            for (Uint32 x = 0u; x < width; ++x)
            {
                const uint32_t color =
                    image != NULL ? portable_native_image_pixel(image, x, y) : UINT32_MAX;
                uint8_t *pixel = mapped + ((size_t)y * pitch + x) * 4u;
                pixel[0] = (uint8_t)(color >> 16u);
                pixel[1] = (uint8_t)(color >> 8u);
                pixel[2] = (uint8_t)color;
                pixel[3] = (uint8_t)(color >> 24u);
            }
        SDL_UnmapGPUTransferBuffer(gpu->device, transfer);
        commands = SDL_AcquireGPUCommandBuffer(gpu->device);
        SDL_GPUCopyPass *copy = commands != NULL ? SDL_BeginGPUCopyPass(commands) : NULL;
        if (copy != NULL)
        {
            const SDL_GPUTextureTransferInfo source = {
                .transfer_buffer = transfer, .pixels_per_row = pitch, .rows_per_layer = height};
            const SDL_GPUTextureRegion destination = {
                .texture = texture, .w = width, .h = height, .d = 1u};
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            valid = SDL_SubmitGPUCommandBuffer(commands);
            commands = NULL;
        }
    }
    if (commands != NULL)
        SDL_CancelGPUCommandBuffer(commands);
    if (transfer != NULL)
        SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
    if (!valid && texture != NULL)
        SDL_ReleaseGPUTexture(gpu->device, texture);
    return valid ? texture : NULL;
}

static SDL_GPUShader *create_shader(PortableRenderGpu *gpu, SDL_GPUShaderStage stage, int finish)
{
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(gpu->device);
    const int vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    const PortableSceneShaderCode *code = NULL;
    SDL_GPUShaderFormat format = 0u;
    if (supported & SDL_GPU_SHADERFORMAT_MSL)
    {
        format = SDL_GPU_SHADERFORMAT_MSL;
        code = vertex ? &portable_scene_vertex_msl_code : finish ? &portable_hd2d_fragment_msl_code : &portable_scene_fragment_msl_code;
    }
    else if (supported & SDL_GPU_SHADERFORMAT_DXIL)
    {
        format = SDL_GPU_SHADERFORMAT_DXIL;
        code = vertex ? &portable_scene_vertex_dxil_code : finish ? &portable_hd2d_fragment_dxil_code : &portable_scene_fragment_dxil_code;
    }
    else if (supported & SDL_GPU_SHADERFORMAT_SPIRV)
    {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        code = vertex ? &portable_scene_vertex_spirv_code : finish ? &portable_hd2d_fragment_spirv_code : &portable_scene_fragment_spirv_code;
    }
    if (code == NULL)
    {
        SDL_SetError("no authored shader format supported");
        return NULL;
    }
    const SDL_GPUShaderCreateInfo info = {.code_size = code->size,
                                          .code = code->bytes,
                                          .entrypoint = code->entrypoint,
                                          .format = format,
                                          .stage = stage,
                                          .num_samplers = vertex ? 0u : finish ? 2u : 1u,
                                          .num_uniform_buffers = 1u};
    return SDL_CreateGPUShader(gpu->device, &info);
}

static int post_enabled(const PortableRenderGpu *gpu)
{
    return gpu->hd2d[0] || gpu->hd2d[1] || gpu->hd2d[2] || gpu->hd2d[3] || gpu->hd2d[4];
}

void portable_render_gpu_set_hd2d(PortableRenderGpu *gpu, const PortableSettings *settings)
{
    if (!gpu) return;
    gpu->hd2d[0] = settings && settings->hd2d_dof;
    gpu->hd2d[1] = settings && settings->hd2d_bloom;
    gpu->hd2d[2] = settings && settings->hd2d_ao;
    gpu->hd2d[3] = settings && settings->hd2d_grade;
    gpu->hd2d[4] = settings && settings->hd2d_fog;
    gpu->hd2d[5] = settings && settings->hd2d_light;
}

static int create_finish(PortableRenderGpu *gpu)
{
    if (gpu->finish) return 1;
    SDL_GPUShader *vertex = create_shader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, 0);
    SDL_GPUShader *fragment = create_shader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
    if (vertex && fragment)
    {
        const SDL_GPUVertexBufferDescription buffer = {.slot=0, .pitch=sizeof(GpuVertex), .input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX};
        const SDL_GPUVertexAttribute attributes[] = {
            {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset=0},
            {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset=12},
            {.location=2, .format=SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset=20},
            {.location=3, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset=24}};
        const SDL_GPUColorTargetDescription color = {.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
        const SDL_GPUGraphicsPipelineCreateInfo info = {
            .vertex_shader=vertex, .fragment_shader=fragment,
            .vertex_input_state={.vertex_buffer_descriptions=&buffer, .num_vertex_buffers=1,
                .vertex_attributes=attributes, .num_vertex_attributes=4},
            .primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state={.fill_mode=SDL_GPU_FILLMODE_FILL, .cull_mode=SDL_GPU_CULLMODE_NONE},
            .multisample_state={.sample_count=SDL_GPU_SAMPLECOUNT_1},
            .target_info={.color_target_descriptions=&color, .num_color_targets=1}};
        gpu->finish = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    }
    if (vertex) SDL_ReleaseGPUShader(gpu->device, vertex);
    if (fragment) SDL_ReleaseGPUShader(gpu->device, fragment);
    return gpu->finish != NULL;
}

static int create_pipelines(PortableRenderGpu *gpu)
{
    SDL_GPUShader *vertex = create_shader(gpu, SDL_GPU_SHADERSTAGE_VERTEX, 0);
    SDL_GPUShader *fragment =
        vertex != NULL ? create_shader(gpu, SDL_GPU_SHADERSTAGE_FRAGMENT, 0) : NULL;
    if (fragment == NULL)
    {
        if (vertex != NULL)
            SDL_ReleaseGPUShader(gpu->device, vertex);
        return 0;
    }
    const SDL_GPUVertexBufferDescription buffer = {
        .slot = 0u, .pitch = sizeof(GpuVertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
    const SDL_GPUVertexAttribute attributes[4] = {{.location = 0u,
                                                   .buffer_slot = 0u,
                                                   .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                                   .offset = 0u},
                                                  {.location = 1u,
                                                   .buffer_slot = 0u,
                                                   .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                                   .offset = 12u},
                                                  {.location = 2u,
                                                   .buffer_slot = 0u,
                                                   .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                                                   .offset = 20u},
                                                  {.location = 3u,
                                                   .buffer_slot = 0u,
                                                   .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                                   .offset = 24u}};
    const SDL_GPUColorTargetDescription color = {.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
    SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .vertex_input_state = {.vertex_buffer_descriptions = &buffer,
                               .num_vertex_buffers = 1u,
                               .vertex_attributes = attributes,
                               .num_vertex_attributes = 4u},
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL,
                             .cull_mode = SDL_GPU_CULLMODE_BACK,
                             .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
                             .enable_depth_clip = true},
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
        .depth_stencil_state = {.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
                                .enable_depth_test = true,
                                .enable_depth_write = true},
        .target_info = {.color_target_descriptions = &color,
                        .num_color_targets = 1u,
                        .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                        .has_depth_stencil_target = true}};
    gpu->pipelines[PIPELINE_CULLED] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    /* Backdrops ignore depth entirely; they are drawn first, in mesh order. */
    info.depth_stencil_state.enable_depth_test = false;
    info.depth_stencil_state.enable_depth_write = false;
    gpu->pipelines[PIPELINE_BACKDROP_CULLED] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    /* Straight alpha over the destination, keeping its alpha. */
    const SDL_GPUColorTargetDescription blend = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                        .color_blend_op = SDL_GPU_BLENDOP_ADD,
                        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                        .enable_blend = true}};
    info.target_info.color_target_descriptions = &blend;
    gpu->pipelines[PIPELINE_BACKDROP_BLEND] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    info.depth_stencil_state.enable_depth_test = true;
    gpu->pipelines[PIPELINE_BLEND] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    gpu->pipelines[PIPELINE_BLEND_TWO_SIDED] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    info.depth_stencil_state.enable_depth_test = false;
    gpu->pipelines[PIPELINE_BACKDROP_BLEND_TWO_SIDED] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    info.target_info.color_target_descriptions = &color;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    gpu->pipelines[PIPELINE_TWO_SIDED] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    info.depth_stencil_state.enable_depth_write = false;
    /* The shader's opaque color scales the destination; alpha stays intact. */
    const SDL_GPUColorTargetDescription multiply = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR,
                        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                        .color_blend_op = SDL_GPU_BLENDOP_ADD,
                        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                        .enable_blend = true}};
    info.target_info.color_target_descriptions = &multiply;
    gpu->pipelines[PIPELINE_MULTIPLY] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    /* Additive glows: the shader's opaque color adds to the destination. */
    const SDL_GPUColorTargetDescription additive = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .color_blend_op = SDL_GPU_BLENDOP_ADD,
                        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                        .enable_blend = true}};
    info.target_info.color_target_descriptions = &additive;
    gpu->pipelines[PIPELINE_ADDITIVE] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    info.target_info.color_target_descriptions = &color;
    info.depth_stencil_state.enable_depth_test = false;
    gpu->pipelines[PIPELINE_BACKDROP] = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    SDL_ReleaseGPUShader(gpu->device, vertex);
    SDL_ReleaseGPUShader(gpu->device, fragment);
    for (unsigned i = 0u; i < PIPELINE_COUNT; ++i)
        if (gpu->pipelines[i] == NULL)
            return 0;
    return 1;
}

PortableRenderGpu *portable_render_gpu_create(SDL_Renderer *renderer, char *error, size_t capacity)
{
    SDL_GPUDevice *device = renderer != NULL ? SDL_GetGPURendererDevice(renderer) : NULL;
    if (device == NULL)
    {
        failure(error, capacity, "SDL renderer has no shared GPU device");
        return NULL;
    }
    PortableRenderGpu *gpu = calloc(1u, sizeof *gpu);
    if (gpu == NULL)
    {
        failure(error, capacity, "owner allocation");
        return NULL;
    }
    gpu->renderer = renderer;
    gpu->device = device;
    int valid = create_pipelines(gpu);
    for (unsigned i = 0u; valid && i < 5u; ++i)
    {
        const SDL_GPUFilter filter = i == 4u ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        const SDL_GPUSamplerCreateInfo info = {
            .min_filter = filter,
            .mag_filter = filter,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = i & 1u ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                                     : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = i & 2u ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                                     : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};
        gpu->samplers[i] = SDL_CreateGPUSampler(device, &info);
        valid = gpu->samplers[i] != NULL;
    }
    static const GpuVertex sky[4] = {{{-1, -1, 0}, {0, 0}, {255, 255, 255, 255}, {0, 0, 0}},
                                     {{1, -1, 0}, {0, 0}, {255, 255, 255, 255}, {0, 0, 0}},
                                     {{1, 1, 0}, {0, 0}, {255, 255, 255, 255}, {0, 0, 0}},
                                     {{-1, 1, 0}, {0, 0}, {255, 255, 255, 255}, {0, 0, 0}}};
    static const uint32_t indices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    if (valid)
        gpu->white = upload_image(gpu, NULL);
    if (gpu->white != NULL)
        gpu->sky_vertices = upload_buffer(gpu, SDL_GPU_BUFFERUSAGE_VERTEX, sky, sizeof sky);
    if (gpu->sky_vertices != NULL)
        gpu->sky_indices = upload_buffer(gpu, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof indices);
    if (gpu->sky_indices == NULL)
    {
        failure(error, capacity, "create scene resources");
        portable_render_gpu_close(gpu);
        return NULL;
    }
    if (error != NULL && capacity != 0u)
        error[0] = '\0';
    return gpu;
}

/* Looks up or uploads an image and marks it drawn this frame; *found is NULL
 * for the shared white texture. */
static SDL_GPUTexture *texture_entry(PortableRenderGpu *gpu, const PortableNativeImage *image,
                                     GpuTextureCache **found)
{
    *found = NULL;
    if (image->pixels == NULL)
        return gpu->white;
    for (GpuTextureCache *entry = gpu->textures; entry != NULL; entry = entry->next)
        if (entry->image.pixels == image->pixels && entry->image.width == image->width &&
            entry->image.height == image->height && entry->image.palette == image->palette &&
            entry->image.palette_count == image->palette_count &&
            entry->image.palette_base == image->palette_base)
        {
            entry->used = gpu->frame;
            *found = entry;
            return entry->texture;
        }
    if (gpu->texture_count >= 4096u)
    {
        SDL_SetError("texture cache capacity");
        return NULL;
    }
    GpuTextureCache *entry = calloc(1u, sizeof *entry);
    if (entry == NULL)
        return NULL;
    entry->texture = upload_image(gpu, image);
    if (entry->texture == NULL)
    {
        free(entry);
        return NULL;
    }
    entry->image = *image;
    entry->used = gpu->frame;
    entry->next = gpu->textures;
    gpu->textures = entry;
    ++gpu->texture_count;
    *found = entry;
    return entry->texture;
}

static SDL_GPUTexture *cached_texture(PortableRenderGpu *gpu, const PortableNativeImage *image)
{
    GpuTextureCache *found;
    return texture_entry(gpu, image, &found);
}

static SDL_GPUBuffer *upload_vertices(PortableRenderGpu *gpu, const PortableRenderMesh *mesh)
{
    GpuVertex *vertices = calloc(mesh->vertex_count, sizeof *vertices);
    if (!vertices)
        return SDL_SetError("geometry upload allocation"), NULL;
    for (uint32_t i = 0; i < mesh->vertex_count; ++i)
    {
        for (unsigned j = 0; j < 3; ++j)
            vertices[i].position[j] = (float)mesh->vertices[i].position[j];
        for (unsigned j = 0; j < 2; ++j)
            vertices[i].uv[j] = (float)mesh->vertices[i].uv[j];
        for (unsigned j = 0; j < 3; ++j)
            vertices[i].normal[j] = (float)mesh->vertices[i].normal[j];
        const uint32_t argb = mesh->vertex_argb ? mesh->vertex_argb[i] : UINT32_MAX;
        const uint8_t rgba[4] = {(uint8_t)(argb >> 16u), (uint8_t)(argb >> 8u), (uint8_t)argb,
                                 (uint8_t)(argb >> 24u)};
        memcpy(vertices[i].color, rgba, sizeof rgba);
    }
    SDL_GPUBuffer *buffer = upload_buffer(gpu, SDL_GPU_BUFFERUSAGE_VERTEX, vertices,
                                          mesh->vertex_count * (Uint32)sizeof *vertices);
    free(vertices);
    return buffer;
}

/* Material variants borrow the same arrays. Run lists and material identity
 * do not affect uploaded vertex/index buffers; a new revision re-uploads the
 * vertices of a mesh skinned in place. */
static GpuGeometryCache *cached_geometry(PortableRenderGpu *gpu, const PortableRenderMesh *mesh)
{
    for (GpuGeometryCache *entry = gpu->geometry; entry; entry = entry->next)
        if (entry->source_vertices == mesh->vertices && entry->source_indices == mesh->indices &&
            entry->vertex_count == mesh->vertex_count && entry->index_count == mesh->index_count)
        {
            if (entry->revision != mesh->revision)
            {
                SDL_GPUBuffer *vertices = upload_vertices(gpu, mesh);
                if (!vertices)
                    return NULL;
                SDL_ReleaseGPUBuffer(gpu->device, entry->vertices);
                entry->vertices = vertices;
                entry->revision = mesh->revision;
            }
            entry->used = gpu->frame;
            return entry;
        }
    if (gpu->geometry_count >= PORTABLE_RENDER_INSTANCE_LIMIT)
        return SDL_SetError("geometry cache capacity"), NULL;
    GpuGeometryCache *entry = calloc(1u, sizeof *entry);
    if (!entry)
        return SDL_SetError("geometry upload allocation"), NULL;
    entry->vertices = upload_vertices(gpu, mesh);
    if (entry->vertices)
        entry->indices = upload_buffer(gpu, SDL_GPU_BUFFERUSAGE_INDEX, mesh->indices,
                                      mesh->index_count * (Uint32)sizeof *mesh->indices);
    if (!entry->indices)
    {
        if (entry->vertices)
            SDL_ReleaseGPUBuffer(gpu->device, entry->vertices);
        free(entry);
        return NULL;
    }
    entry->source_vertices = mesh->vertices;
    entry->source_indices = mesh->indices;
    entry->vertex_count = mesh->vertex_count;
    entry->index_count = mesh->index_count;
    entry->revision = mesh->revision;
    entry->used = gpu->frame;
    entry->next = gpu->geometry;
    gpu->geometry = entry;
    ++gpu->geometry_count;
    return entry;
}

static int geometry_known(const void *context, const PortableRenderMesh *mesh)
{
    const PortableRenderGpu *owner = context;
    for (const GpuMeshCache *entry = owner->meshes; entry; entry = entry->next)
        if (entry->source == mesh)
            return entry->geometry->source_vertices == mesh->vertices &&
                entry->geometry->source_indices == mesh->indices &&
                entry->geometry->vertex_count == mesh->vertex_count &&
                entry->geometry->index_count == mesh->index_count &&
                entry->material_count == mesh->material_count ?
                (entry->geometry->revision == mesh->revision ? 1 : 0) : -1;
    for (const GpuGeometryCache *entry = owner->geometry; entry; entry = entry->next)
        if (entry->source_vertices == mesh->vertices && entry->source_indices == mesh->indices &&
            entry->vertex_count == mesh->vertex_count && entry->index_count == mesh->index_count)
            return entry->revision == mesh->revision;
    return 0;
}

/* Pattern frames upload with their mesh, never inside a render pass. */
static int cached_animation_keys(PortableRenderGpu *gpu, const PortableRenderMesh *mesh, uint32_t material)
{
    const PortableRenderTextureAnimation *animation = mesh->materials[material].animation;
    for (uint32_t key = 0; animation && key < animation->key_count; ++key)
        if (cached_texture(gpu, &animation->key_textures[key]) == NULL)
            return 0;
    return 1;
}

static GpuMeshCache *cached_mesh(PortableRenderGpu *gpu, const PortableRenderMesh *mesh)
{
    for (GpuMeshCache *entry = gpu->meshes; entry; entry = entry->next)
        if (entry->source == mesh)
        {
            if (entry->geometry->revision != mesh->revision && !cached_geometry(gpu, mesh))
                return NULL;
            entry->used = entry->geometry->used = gpu->frame;
            for (uint32_t i = 0; i < entry->material_count; ++i)
            {
                if (entry->textures[i])
                    entry->textures[i]->used = gpu->frame;
                if (!cached_animation_keys(gpu, mesh, i))
                    return NULL;
            }
            return entry;
        }
    if (gpu->mesh_count >= PORTABLE_RENDER_INSTANCE_LIMIT)
        return SDL_SetError("mesh cache capacity"), NULL;
    GpuMeshCache *entry = calloc(1u, sizeof *entry);
    if (!entry)
        return SDL_SetError("mesh upload allocation"), NULL;
    entry->textures = calloc(mesh->material_count, sizeof *entry->textures);
    int valid = entry->textures != NULL;
    for (uint32_t i = 0; valid && i < mesh->material_count; ++i)
        valid = texture_entry(gpu, &mesh->materials[i].texture, &entry->textures[i]) != NULL &&
                cached_animation_keys(gpu, mesh, i);
    if (valid)
        entry->geometry = cached_geometry(gpu, mesh);
    if (!entry->geometry)
    {
        free(entry->textures);
        free(entry);
        return NULL;
    }
    entry->source = mesh;
    entry->material_count = mesh->material_count;
    entry->used = gpu->frame;
    entry->next = gpu->meshes;
    gpu->meshes = entry;
    ++gpu->mesh_count;
    return entry;
}

/* Frees cache entries last drawn before frame `keep`; returns how many went.
 * A kept mesh marked its geometry and textures, so they stay with it. */
static size_t evict_stale(PortableRenderGpu *gpu, uint64_t keep)
{
    const size_t before = gpu->mesh_count + gpu->geometry_count + gpu->texture_count;
    for (GpuMeshCache **link = &gpu->meshes; *link;)
    {
        GpuMeshCache *entry = *link;
        if (entry->used >= keep) { link = &entry->next; continue; }
        *link = entry->next;
        free(entry->textures);
        free(entry);
        --gpu->mesh_count;
    }
    for (GpuGeometryCache **link = &gpu->geometry; *link;)
    {
        GpuGeometryCache *entry = *link;
        if (entry->used >= keep) { link = &entry->next; continue; }
        *link = entry->next;
        SDL_ReleaseGPUBuffer(gpu->device, entry->vertices);
        SDL_ReleaseGPUBuffer(gpu->device, entry->indices);
        free(entry);
        --gpu->geometry_count;
    }
    for (GpuTextureCache **link = &gpu->textures; *link;)
    {
        GpuTextureCache *entry = *link;
        if (entry->used >= keep) { link = &entry->next; continue; }
        *link = entry->next;
        SDL_ReleaseGPUTexture(gpu->device, entry->texture);
        free(entry);
        --gpu->texture_count;
    }
    return before - (gpu->mesh_count + gpu->geometry_count + gpu->texture_count);
}

static int resize_target(PortableRenderGpu *gpu, size_t width, size_t height)
{
    const int post = post_enabled(gpu);
    if (post && !create_finish(gpu)) return 0;
    if (width == gpu->width && height == gpu->height && gpu->wrapper != NULL && post == gpu->post_target)
        return 1;
    SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                     .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                              SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                     .width = (Uint32)width,
                                     .height = (Uint32)height,
                                     .layer_count_or_depth = 1u,
                                     .num_levels = 1u,
                                     .sample_count = SDL_GPU_SAMPLECOUNT_1};
    SDL_GPUTexture *color = SDL_CreateGPUTexture(gpu->device, &info);
    SDL_GPUTexture *scene = post && color ? SDL_CreateGPUTexture(gpu->device, &info) : NULL;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | (post ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0);
    SDL_GPUTexture *depth = color != NULL ? SDL_CreateGPUTexture(gpu->device, &info) : NULL;
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, color);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                          SDL_PIXELFORMAT_RGBA32);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, (Sint64)width);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, (Sint64)height);
    SDL_Texture *wrapper =
        depth != NULL && (!post || scene) ? SDL_CreateTextureWithProperties(gpu->renderer, properties) : NULL;
    SDL_DestroyProperties(properties);
    if (wrapper == NULL)
    {
        if (scene) SDL_ReleaseGPUTexture(gpu->device, scene);
        if (color != NULL)
            SDL_ReleaseGPUTexture(gpu->device, color);
        if (depth != NULL)
            SDL_ReleaseGPUTexture(gpu->device, depth);
        return 0;
    }
    if (!SDL_SetTextureBlendMode(wrapper, SDL_BLENDMODE_NONE))
    {
        SDL_DestroyTexture(wrapper);
        if (scene) SDL_ReleaseGPUTexture(gpu->device, scene);
        SDL_ReleaseGPUTexture(gpu->device, color);
        SDL_ReleaseGPUTexture(gpu->device, depth);
        return 0;
    }
    if (gpu->wrapper != NULL)
        SDL_DestroyTexture(gpu->wrapper);
    if (gpu->color != NULL)
        SDL_ReleaseGPUTexture(gpu->device, gpu->color);
    if (gpu->depth != NULL)
        SDL_ReleaseGPUTexture(gpu->device, gpu->depth);
    if (gpu->scene) SDL_ReleaseGPUTexture(gpu->device, gpu->scene);
    gpu->scene = scene;
    gpu->post_target = post;
    gpu->wrapper = wrapper;
    gpu->color = color;
    gpu->depth = depth;
    gpu->width = width;
    gpu->height = height;
    return 1;
}

static void matrix(float out[16], const PortableRenderView *view,
                   const PortableRenderInstance *instance)
{
    double camera[3][4];
    const double *basis[3] = {view->right, view->up, view->forward};
    for (unsigned row = 0u; row < 3u; ++row)
        for (unsigned column = 0u; column < 4u; ++column)
        {
            camera[row][column] = 0.0;
            for (unsigned axis = 0u; axis < 3u; ++axis)
                camera[row][column] +=
                    basis[row][axis] * (instance->transform[axis * 4u + column] -
                                        (column == 3u ? view->position[axis] : 0.0));
        }
    const PortableScene3DProjection *projection = &view->projection;
    const double sx = 2.0 * projection->focal_x / (double)view->width;
    const double sy = 2.0 * projection->focal_y / (double)view->height;
    const double ox = 2.0 * projection->center_x / (double)view->width - 1.0;
    const double oy = 1.0 - 2.0 * projection->center_y / (double)view->height;
    const double depth = projection->far_z / (projection->far_z - projection->near_z);
    for (unsigned column = 0u; column < 4u; ++column)
    {
        out[column * 4u] = (float)(sx * camera[0][column] + ox * camera[2][column]);
        out[column * 4u + 1u] = (float)(sy * camera[1][column] + oy * camera[2][column]);
        out[column * 4u + 2u] =
            (float)(depth * camera[2][column] - (column == 3u ? depth * projection->near_z : 0.0));
        out[column * 4u + 3u] = (float)camera[2][column];
    }
}

static GpuPaint paint(uint32_t base, uint32_t tint, int32_t cutoff, uint32_t flags)
{
    const GpuPaint result = {.base_argb = base,
                             .tint_argb = tint,
                             .cutoff_q16 = (uint32_t)cutoff,
                             .alpha_mode = flags & PORTABLE_RENDER_MATERIAL_ALPHA_BLEND ? 2u
                                                                                        : (flags & 16u) != 0u,
                             .uv_u = {1, 0, 0, 0},
                             .uv_v = {0, 1, 0, 0}};
    return result;
}

/* Backdrops first, then every other run in mesh order (stock content draws
 * only here), then course alpha blending over the finished depth buffer. */
enum
{
    PASS_BACKDROP,
    PASS_SCENE,
    PASS_BLEND,
    PASS_COUNT
};

static unsigned run_pass(uint32_t flags)
{
    return flags & PORTABLE_RENDER_MATERIAL_BACKGROUND    ? PASS_BACKDROP
           : flags & PORTABLE_RENDER_MATERIAL_ALPHA_BLEND ? PASS_BLEND
                                                         : PASS_SCENE;
}

static unsigned run_pipeline(uint32_t flags)
{
    const int two_sided = (flags & 4u) != 0u;
    if (flags & PORTABLE_RENDER_MATERIAL_MULTIPLY)
        return PIPELINE_MULTIPLY;
    if (flags & PORTABLE_RENDER_MATERIAL_ADDITIVE)
        return PIPELINE_ADDITIVE;
    if (flags & PORTABLE_RENDER_MATERIAL_BACKGROUND)
        return flags & PORTABLE_RENDER_MATERIAL_ALPHA_BLEND
                   ? (two_sided ? PIPELINE_BACKDROP_BLEND_TWO_SIDED : PIPELINE_BACKDROP_BLEND)
                   : (two_sided ? PIPELINE_BACKDROP : PIPELINE_BACKDROP_CULLED);
    if (flags & PORTABLE_RENDER_MATERIAL_ALPHA_BLEND)
        return two_sided ? PIPELINE_BLEND_TWO_SIDED : PIPELINE_BLEND;
    return two_sided ? PIPELINE_TWO_SIDED : PIPELINE_CULLED;
}

/* Backdrops sit around the eye (the camera's translation is dropped) unless
 * world-placed; either way the far plane widens so the whole backdrop
 * survives clipping. */
static void backdrop_view(const PortableRenderView *view, const PortableRenderInstance *instance, int world,
                          PortableRenderView *out_view, PortableRenderInstance *out_instance)
{
    *out_view = *view;
    *out_instance = *instance;
    for (unsigned axis = 0u; axis < 3u && !world; ++axis)
        out_instance->transform[axis * 4u + 3u] = view->position[axis];
    out_view->projection.far_z = 65535.0;
}

SDL_Texture *portable_render_gpu_draw(PortableRenderGpu *gpu, const PortableRenderFrame *frame,
                                      size_t width, size_t height, char *error, size_t capacity)
{
    if (gpu == NULL || width == 0u || height == 0u || width > PORTABLE_RENDER_TARGET_EXTENT_LIMIT ||
        height > PORTABLE_RENDER_TARGET_EXTENT_LIMIT ||
        width > PORTABLE_RENDER_TARGET_PIXEL_LIMIT / height)
    {
        SDL_SetError("target capacity");
        failure(error, capacity, "target");
        return NULL;
    }
    if (!portable_render_frame_valid_geometry(frame, width, height, geometry_known, gpu, error, capacity))
        return NULL;
    if (!SDL_FlushRenderer(gpu->renderer))
    {
        failure(error, capacity, "compositor boundary");
        return NULL;
    }
    ++gpu->frame;
    for (size_t i = 0u, evictions = 0u; i < frame->instance_count; ++i)
        if (cached_mesh(gpu, frame->instances[i].mesh) == NULL)
        {
            /* Sprite frames and palette variants accumulate past the caches
             * over a race: drop what went undrawn for a second, then anything
             * this frame has not drawn yet, and retry. */
            const uint64_t recent = gpu->frame > 60u ? gpu->frame - 60u : 0u;
            if (evictions++ < 2u && (evict_stale(gpu, recent) || evict_stale(gpu, gpu->frame)))
            {
                --i;
                continue;
            }
            failure(error, capacity, "upload mesh");
            return NULL;
        }
    if (!resize_target(gpu, width, height))
    {
        failure(error, capacity, "resize target");
        return NULL;
    }
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(gpu->device);
    const SDL_GPUColorTargetInfo color = {.texture = gpu->post_target ? gpu->scene : gpu->color,
                                          .clear_color = {0, 0, 0, 1},
                                          .load_op = SDL_GPU_LOADOP_CLEAR,
                                          .store_op = SDL_GPU_STOREOP_STORE};
    const SDL_GPUDepthStencilTargetInfo depth = {.texture = gpu->depth,
                                                 .clear_depth = 1.0f,
                                                 .load_op = SDL_GPU_LOADOP_CLEAR,
                                                 .store_op = gpu->post_target ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE,
                                                 .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
                                                 .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE};
    SDL_GPURenderPass *pass =
        commands != NULL ? SDL_BeginGPURenderPass(commands, &color, 1u, &depth) : NULL;
    if (pass == NULL)
    {
        if (commands != NULL)
            SDL_CancelGPUCommandBuffer(commands);
        failure(error, capacity, "begin pass");
        return NULL;
    }
    for (size_t view_index = 0u; view_index < frame->view_count; ++view_index)
    {
        const PortableRenderView *view = &frame->views[view_index];
        const SDL_GPUViewport viewport = {(float)view->x,      (float)view->y, (float)view->width,
                                          (float)view->height, 0.0f,           1.0f};
        const SDL_Rect scissor = {(int)view->x, (int)view->y, (int)view->width, (int)view->height};
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &scissor);
        const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const GpuPaint sky = paint(view->clear_argb, UINT32_MAX, 0, 0u);
        const SDL_GPUBufferBinding sky_vertex = {.buffer = gpu->sky_vertices};
        const SDL_GPUBufferBinding sky_index = {.buffer = gpu->sky_indices};
        const SDL_GPUTextureSamplerBinding sky_texture = {.texture = gpu->white,
                                                          .sampler = gpu->samplers[0]};
        SDL_BindGPUGraphicsPipeline(pass, gpu->pipelines[PIPELINE_BACKDROP]);
        SDL_PushGPUVertexUniformData(commands, 0u, identity, sizeof identity);
        SDL_PushGPUFragmentUniformData(commands, 0u, &sky, sizeof sky);
        SDL_BindGPUVertexBuffers(pass, 0u, &sky_vertex, 1u);
        SDL_BindGPUIndexBuffer(pass, &sky_index, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_BindGPUFragmentSamplers(pass, 0u, &sky_texture, 1u);
        SDL_DrawGPUIndexedPrimitives(pass, 6u, 1u, 0u, 0, 0u);
        for (unsigned stage = 0u; stage < PASS_COUNT; ++stage)
        for (size_t i = 0u; i < frame->instance_count; ++i)
        {
            if (!portable_render_instance_visible(&frame->instances[i], view_index))
                continue;
            PortableRenderInstance oriented = frame->instances[i];
            portable_render_instance_for_view(&frame->instances[i], view, &oriented);
            const PortableRenderInstance *instance = &oriented;
            size_t clip_first = 0, clip_count = 0;
            if (!portable_render_instance_clip_rows(instance, view, &clip_first, &clip_count) ||
                !clip_count)
                continue; /* Frame preflight has already rejected invalid intervals. */
            const SDL_Rect instance_scissor = {(int)view->x, (int)(view->y + clip_first),
                                               (int)view->width, (int)clip_count};
            SDL_SetGPUScissor(pass, &instance_scissor);
            GpuMeshCache *cached = cached_mesh(gpu, instance->mesh);
            const SDL_GPUBufferBinding vertex_binding = {.buffer = cached->geometry->vertices};
            const SDL_GPUBufferBinding index_binding = {.buffer = cached->geometry->indices};
            float transform[16], world_transform[16];
            if (stage == PASS_BACKDROP)
            {
                PortableRenderView backdrop;
                PortableRenderInstance anchored;
                backdrop_view(view, instance, 0, &backdrop, &anchored);
                matrix(transform, &backdrop, &anchored);
                backdrop_view(view, instance, 1, &backdrop, &anchored);
                matrix(world_transform, &backdrop, &anchored);
            }
            else
                matrix(transform, view, instance);
            int bound = 0;
            for (uint32_t run_index = 0u; run_index < instance->mesh->run_count; ++run_index)
            {
                const PortableRenderRun *run = &instance->mesh->runs[run_index];
                const PortableRenderMaterial *material = &instance->mesh->materials[run->material];
                if (run_pass(material->flags) != stage)
                    continue;
                if (stage == PASS_BACKDROP)
                    SDL_PushGPUVertexUniformData(commands, 0u,
                                                 material->flags & PORTABLE_RENDER_MATERIAL_WORLD_SKY ? world_transform
                                                                                                      : transform,
                                                 sizeof transform);
                if (!bound)
                {
                    if (stage != PASS_BACKDROP)
                        SDL_PushGPUVertexUniformData(commands, 0u, transform, sizeof transform);
                    SDL_BindGPUVertexBuffers(pass, 0u, &vertex_binding, 1u);
                    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    bound = 1;
                }
                GpuPaint material_paint = paint(material->argb, instance->tint_argb,
                                                      material->alpha_cutoff_q16, material->flags);
                portable_render_paint_environment(frame, view, instance, material, stage == PASS_BACKDROP,
                                                  material_paint.environment);
                if (material_paint.environment[3] > 0.0f) /* lit: the diffuse lives in the light terms */
                    material_paint.base_argb |= 0x00ffffffu;
                for (unsigned axis = 0; axis < 2u; ++axis)
                    material_paint.uv_offset[axis] = (float)instance->uv_offset[axis];
                SDL_GPUTexture *image =
                    cached->textures[run->material] ? cached->textures[run->material]->texture : gpu->white;
                if (material->animation)
                {
                    double uv[6];
                    const PortableNativeImage *source;
                    portable_render_material_sample(material, frame->animation_tick, frame->animation_rewind_q16, uv, &source);
                    const float u[4] = {(float)uv[0], (float)uv[2], (float)uv[4], 0.0f};
                    const float v[4] = {(float)uv[1], (float)uv[3], (float)uv[5], 0.0f};
                    memcpy(material_paint.uv_u, u, sizeof u);
                    memcpy(material_paint.uv_v, v, sizeof v);
                    image = cached_texture(gpu, source); /* uploaded with the mesh */
                }
                material_paint.padding[0] = (uint32_t)(gpu->hd2d[5] && stage != PASS_BACKDROP &&
                    !(material->flags & PORTABLE_RENDER_MATERIAL_BLENDED) &&
                    (instance->flags & (PORTABLE_RENDER_INSTANCE_BILLBOARD_Z | PORTABLE_RENDER_INSTANCE_BILLBOARD_UP)));
                material_paint.padding[1] = instance->add_argb & 0x00ffffffu;
                const int multiply = (material->flags & PORTABLE_RENDER_MATERIAL_MULTIPLY) != 0u;
                const int additive = (material->flags & PORTABLE_RENDER_MATERIAL_ADDITIVE) != 0u;
                const SDL_GPUTextureSamplerBinding texture = {
                    .texture = image,
                    .sampler = gpu->samplers[multiply || additive ? 4u : material->flags & 3u]};
                SDL_BindGPUGraphicsPipeline(pass, gpu->pipelines[run_pipeline(material->flags)]);
                SDL_PushGPUFragmentUniformData(commands, 0u, &material_paint,
                                               sizeof material_paint);
                SDL_BindGPUFragmentSamplers(pass, 0u, &texture, 1u);
                SDL_DrawGPUIndexedPrimitives(pass, run->index_count, 1u, run->first_index, 0, 0u);
            }
        }
    }
    SDL_EndGPURenderPass(pass);
    if (gpu->post_target)
    {
        const SDL_GPUColorTargetInfo output = {.texture=gpu->color, .load_op=SDL_GPU_LOADOP_CLEAR,
            .store_op=SDL_GPU_STOREOP_STORE, .clear_color={0,0,0,1}};
        pass = SDL_BeginGPURenderPass(commands, &output, 1, NULL);
        if (!pass) { SDL_CancelGPUCommandBuffer(commands); failure(error, capacity, "HD-2D pass"); return NULL; }
        SDL_BindGPUGraphicsPipeline(pass, gpu->finish);
        const SDL_GPUBufferBinding vertices = {.buffer=gpu->sky_vertices}, indices = {.buffer=gpu->sky_indices};
        SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
        SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        SDL_PushGPUVertexUniformData(commands, 0, identity, sizeof identity);
        const SDL_GPUTextureSamplerBinding textures[] = {
            {.texture=gpu->scene, .sampler=gpu->samplers[4]},
            {.texture=gpu->depth, .sampler=gpu->samplers[0]}};
        SDL_BindGPUFragmentSamplers(pass, 0, textures, 2);
        for (size_t i=0; i<frame->view_count; ++i)
        {
            const PortableRenderView *view = &frame->views[i];
            const SDL_GPUViewport viewport = {(float)view->x, (float)view->y, (float)view->width, (float)view->height, 0, 1};
            const SDL_Rect scissor = {(int)view->x, (int)view->y, (int)view->width, (int)view->height};
            SDL_SetGPUViewport(pass, &viewport); SDL_SetGPUScissor(pass, &scissor);
            const float uniforms[16] = {
                (float)view->x, (float)view->y, (float)view->width, (float)view->height,
                1.0f/(float)width, 1.0f/(float)height, (float)view->projection.near_z, (float)view->projection.far_z,
                gpu->hd2d[0], gpu->hd2d[1], gpu->hd2d[2], gpu->hd2d[3],
                gpu->hd2d[4], ((view->clear_argb>>16)&255)/255.0f, ((view->clear_argb>>8)&255)/255.0f, (view->clear_argb&255)/255.0f};
            SDL_PushGPUFragmentUniformData(commands, 0, uniforms, sizeof uniforms);
            SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
        }
        SDL_EndGPURenderPass(pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(commands))
    {
        failure(error, capacity, "submit frame");
        return NULL;
    }
    if (error != NULL && capacity != 0u)
        error[0] = '\0';
    return gpu->wrapper;
}

int portable_render_gpu_wait_idle(PortableRenderGpu *gpu, char *error, size_t capacity)
{
    if (gpu == NULL)
    {
        SDL_SetError("null GPU backend");
        return failure(error, capacity, "wait");
    }
    if (SDL_GetRenderWindow(gpu->renderer) != NULL &&
        SDL_GetRenderTarget(gpu->renderer) != NULL)
    {
        SDL_SetError("window-backed offscreen compositor requires a submission boundary");
        return failure(error, capacity, "wait");
    }
    if (!SDL_FlushRenderer(gpu->renderer) || !SDL_WaitForGPUIdle(gpu->device))
        return failure(error, capacity, "wait for completed scene/compositor work");
    if (error != NULL && capacity != 0u)
        error[0] = '\0';
    return 1;
}

int portable_render_gpu_read_pixels(PortableRenderGpu *gpu, uint32_t *pixels, size_t stride,
                                    char *error, size_t capacity)
{
    if (gpu == NULL || gpu->color == NULL || pixels == NULL || stride < gpu->width ||
        (gpu->height > 1u && stride > (SIZE_MAX - gpu->width) / (gpu->height - 1u)))
    {
        SDL_SetError("invalid capture destination");
        return failure(error, capacity, "capture");
    }
    const Uint32 pitch = ((Uint32)gpu->width + 63u) & ~63u;
    const size_t bytes = (size_t)pitch * gpu->height * 4u;
    if (bytes > gpu->download_capacity)
    {
        const SDL_GPUTransferBufferCreateInfo info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                                                      .size = (Uint32)bytes};
        SDL_GPUTransferBuffer *download = SDL_CreateGPUTransferBuffer(gpu->device, &info);
        if (download == NULL)
            return failure(error, capacity, "capture allocation");
        if (gpu->download != NULL)
            SDL_ReleaseGPUTransferBuffer(gpu->device, gpu->download);
        gpu->download = download;
        gpu->download_capacity = bytes;
    }
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(gpu->device);
    SDL_GPUCopyPass *copy = commands != NULL ? SDL_BeginGPUCopyPass(commands) : NULL;
    if (copy == NULL)
    {
        if (commands != NULL)
            SDL_CancelGPUCommandBuffer(commands);
        return failure(error, capacity, "capture pass");
    }
    const SDL_GPUTextureRegion source = {
        .texture = gpu->color, .w = (Uint32)gpu->width, .h = (Uint32)gpu->height, .d = 1u};
    const SDL_GPUTextureTransferInfo destination = {.transfer_buffer = gpu->download,
                                                    .pixels_per_row = pitch,
                                                    .rows_per_layer = (Uint32)gpu->height};
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (fence == NULL)
        return failure(error, capacity, "capture fence");
    const bool complete = SDL_WaitForGPUFences(gpu->device, true, &fence, 1u);
    SDL_ReleaseGPUFence(gpu->device, fence);
    if (!complete)
        return failure(error, capacity, "capture wait");
    const uint8_t *mapped = SDL_MapGPUTransferBuffer(gpu->device, gpu->download, false);
    if (mapped == NULL)
        return failure(error, capacity, "capture map");
    for (size_t y = 0u; y < gpu->height; ++y)
        for (size_t x = 0u; x < gpu->width; ++x)
        {
            const uint8_t *pixel = mapped + (y * pitch + x) * 4u;
            pixels[y * stride + x] = ((uint32_t)pixel[3] << 24u) | ((uint32_t)pixel[0] << 16u) |
                                     ((uint32_t)pixel[1] << 8u) | pixel[2];
        }
    SDL_UnmapGPUTransferBuffer(gpu->device, gpu->download);
    return 1;
}

size_t portable_render_gpu_geometry_count(const PortableRenderGpu *gpu)
{
    return gpu ? gpu->geometry_count : 0u;
}

void portable_render_gpu_forget_meshes(PortableRenderGpu *gpu)
{
    if (gpu == NULL)
        return;
    while (gpu->meshes != NULL)
    {
        GpuMeshCache *entry = gpu->meshes;
        gpu->meshes = entry->next;
        free(entry->textures);
        free(entry);
    }
    while (gpu->geometry != NULL)
    {
        GpuGeometryCache *entry = gpu->geometry;
        gpu->geometry = entry->next;
        SDL_ReleaseGPUBuffer(gpu->device, entry->vertices);
        SDL_ReleaseGPUBuffer(gpu->device, entry->indices);
        free(entry);
    }
    while (gpu->textures != NULL)
    {
        GpuTextureCache *entry = gpu->textures;
        gpu->textures = entry->next;
        SDL_ReleaseGPUTexture(gpu->device, entry->texture);
        free(entry);
    }
    gpu->mesh_count = gpu->texture_count = gpu->geometry_count = 0u;
}

void portable_render_gpu_close(PortableRenderGpu *gpu)
{
    if (gpu == NULL)
        return;
    portable_render_gpu_forget_meshes(gpu);
    if (gpu->finish) SDL_ReleaseGPUGraphicsPipeline(gpu->device, gpu->finish);
    if (gpu->scene) SDL_ReleaseGPUTexture(gpu->device, gpu->scene);
    if (gpu->wrapper != NULL)
        SDL_DestroyTexture(gpu->wrapper);
    if (gpu->color != NULL)
        SDL_ReleaseGPUTexture(gpu->device, gpu->color);
    if (gpu->depth != NULL)
        SDL_ReleaseGPUTexture(gpu->device, gpu->depth);
    if (gpu->white != NULL)
        SDL_ReleaseGPUTexture(gpu->device, gpu->white);
    if (gpu->sky_vertices != NULL)
        SDL_ReleaseGPUBuffer(gpu->device, gpu->sky_vertices);
    if (gpu->sky_indices != NULL)
        SDL_ReleaseGPUBuffer(gpu->device, gpu->sky_indices);
    if (gpu->download != NULL)
        SDL_ReleaseGPUTransferBuffer(gpu->device, gpu->download);
    for (unsigned i = 0u; i < PIPELINE_COUNT; ++i)
        if (gpu->pipelines[i] != NULL)
            SDL_ReleaseGPUGraphicsPipeline(gpu->device, gpu->pipelines[i]);
    for (unsigned i = 0u; i < 5u; ++i)
        if (gpu->samplers[i] != NULL)
            SDL_ReleaseGPUSampler(gpu->device, gpu->samplers[i]);
    free(gpu);
}
