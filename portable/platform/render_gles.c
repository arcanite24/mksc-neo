#include "render_gles.h"
#include "shaders/scene_shaders.h"

/* Use SDL's portable GL types; ES3 entry points are loaded from its context. */
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#include <SDL3/SDL_opengles2.h>
#undef SDL_USE_BUILTIN_OPENGL_DEFINITIONS

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    GLES_MAJOR_VERSION = 0x821b,
    GLES_VERTEX_ARRAY_BINDING = 0x85b5,
    GLES_READ_FRAMEBUFFER = 0x8ca8,
    GLES_DRAW_FRAMEBUFFER = 0x8ca9,
    GLES_READ_FRAMEBUFFER_BINDING = 0x8caa,
    GLES_DRAW_FRAMEBUFFER_BINDING = 0x8ca6,
    GLES_RGBA8 = 0x8058,
    GLES_DEPTH_COMPONENT32F = 0x8cac,
    GLES_UNIFORM_BUFFER = 0x8a11,
    GLES_UNIFORM_BUFFER_BINDING = 0x8a28,
    GLES_UNIFORM_BUFFER_START = 0x8a29,
    GLES_UNIFORM_BUFFER_SIZE = 0x8a2a,
    GLES_UNIFORM_BLOCK_DATA_SIZE = 0x8a40,
    GLES_SAMPLER_BINDING = 0x8919,
    GLES_PIXEL_PACK_BUFFER = 0x88eb,
    GLES_PIXEL_UNPACK_BUFFER = 0x88ec,
    GLES_PIXEL_PACK_BUFFER_BINDING = 0x88ed,
    GLES_PIXEL_UNPACK_BUFFER_BINDING = 0x88ef,
    GLES_PACK_ROW_LENGTH = 0x0d02,
    GLES_PACK_SKIP_ROWS = 0x0d03,
    GLES_PACK_SKIP_PIXELS = 0x0d04,
    GLES_UNPACK_ROW_LENGTH = 0x0cf2,
    GLES_UNPACK_SKIP_ROWS = 0x0cf3,
    GLES_UNPACK_SKIP_PIXELS = 0x0cf4,
    GLES_RASTERIZER_DISCARD = 0x8c89,
    GLES_CONTEXT_LOST = 0x0507
};

#define GLES_FUNCTIONS(X) \
    X(GLenum, GetError, (void)) \
    X(void, Finish, (void)) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GetIntegeri_v, (GLenum, GLuint, GLint *)) \
    X(void, GetInteger64i_v, (GLenum, GLuint, int64_t *)) \
    X(void, GetBooleanv, (GLenum, GLboolean *)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, ClearDepthf, (GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, BlendEquationSeparate, (GLenum, GLenum)) \
    X(void, DepthRangef, (GLfloat, GLfloat)) \
    X(void, CullFace, (GLenum)) \
    X(void, FrontFace, (GLenum)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, GenSamplers, (GLsizei, GLuint *)) \
    X(void, DeleteSamplers, (GLsizei, const GLuint *)) \
    X(void, BindSampler, (GLuint, GLuint)) \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(void, BindBufferBase, (GLenum, GLuint, GLuint)) \
    X(void, BindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLuint, GetUniformBlockIndex, (GLuint, const GLchar *)) \
    X(void, GetActiveUniformBlockiv, (GLuint, GLuint, GLenum, GLint *)) \
    X(void, UniformBlockBinding, (GLuint, GLuint, GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint *)) \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))

typedef struct
{
#define DECLARE_GL(result, name, arguments) result (GL_APIENTRY *name) arguments;
    GLES_FUNCTIONS(DECLARE_GL)
#undef DECLARE_GL
} GlesFunctions;

typedef struct
{
    float position[3], uv[2];
    uint8_t color[4]; /* RGBA */
    float normal[3];
} GlesVertex;
typedef struct
{
    uint32_t base_argb, tint_argb, cutoff_q16, alpha_mode;
    float uv_offset[2];
    uint32_t padding[2];
    float uv_u[4], uv_v[4];
    float environment[20]; /* portable_render_paint_environment */
} GlesPaint;
_Static_assert(sizeof(GlesVertex) == PORTABLE_SCENE_SHADER_VERTEX_STRIDE, "GLES vertex layout");
_Static_assert(sizeof(GlesPaint) == PORTABLE_SCENE_SHADER_FRAGMENT_UNIFORM_BYTES, "GLES material layout");

typedef struct GlesTexture
{
    PortableNativeImage image;
    GLuint name;
    uint64_t used; /* last frame that drew it */
    struct GlesTexture *next;
} GlesTexture;
typedef struct GlesGeometry
{
    const PortableRenderVertex *source_vertices;
    const uint32_t *source_indices;
    uint32_t vertex_count, index_count, revision;
    GLuint vertices, indices;
    uint64_t used;
    struct GlesGeometry *next;
} GlesGeometry;
typedef struct GlesMesh
{
    const PortableRenderMesh *source;
    GlesGeometry *geometry;
    uint32_t material_count;
    GlesTexture **textures; /* NULL draws the shared white texture */
    uint64_t used;
    struct GlesMesh *next;
} GlesMesh;

struct PortableRenderGles
{
    SDL_Renderer *renderer;
    SDL_GLContext context;
    GlesFunctions gl;
    GLuint program, vao, uniforms[2], samplers[5], white; /* Nearest wraps, linear clamp. */
    GLuint framebuffer, color, depth;
    SDL_Texture *wrapper;
    size_t width, height, mesh_count, texture_count, geometry_count;
    uint64_t frame;
    GlesMesh *meshes;
    GlesGeometry *geometry;
    GlesTexture *textures;
    uint8_t *readback;
    size_t readback_capacity;
    int invalidated, resources_ready;
};

static const GLenum saved_capabilities[] = {GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND,
    GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_POLYGON_OFFSET_FILL,
    GL_SAMPLE_ALPHA_TO_COVERAGE, GLES_RASTERIZER_DISCARD, GL_DITHER};
static const GLenum pixel_stores[] = {GL_PACK_ALIGNMENT, GLES_PACK_ROW_LENGTH,
    GLES_PACK_SKIP_ROWS, GLES_PACK_SKIP_PIXELS, GL_UNPACK_ALIGNMENT,
    GLES_UNPACK_ROW_LENGTH, GLES_UNPACK_SKIP_ROWS, GLES_UNPACK_SKIP_PIXELS};

/* SDL owns the context. A private VAO isolates every vertex attribute/EBO;
 * explicit state capture covers GL state SDL does not invalidate or rebuild. */
typedef struct
{
    GLint draw_framebuffer, read_framebuffer, renderbuffer, program, vao;
    GLint array_buffer, uniform_buffer, pack_buffer, unpack_buffer;
    GLint uniform_bindings[2], texture, sampler, active_texture;
    int64_t uniform_offsets[2], uniform_sizes[2];
    GLint viewport[4], scissor[4], depth_func, cull_face, front_face;
    GLint blend_func[4], blend_equation[2];
    GLint pixel_stores[8];
    GLfloat clear_color[4], clear_depth, depth_range[2];
    GLboolean enabled[9], color_mask[4], depth_mask;
} GlesState;

static int failure(char *error, size_t capacity, const char *action)
{
    if (error != NULL && capacity != 0u)
        snprintf(error, capacity, "GLES scene: %s: %s", action, SDL_GetError());
    return 0;
}

static int gl_complete(PortableRenderGles *gles, const char *action)
{
    const GLenum code = gles->gl.GetError();
    if (code == GL_NO_ERROR)
        return 1;
    if (code == GLES_CONTEXT_LOST)
        gles->invalidated = 1;
    SDL_SetError("%s (GL error 0x%04x)", action, (unsigned)code);
    return 0;
}

static int load_functions(PortableRenderGles *gles)
{
#define LOAD_GL(result, name, arguments) \
    do { \
        SDL_FunctionPointer address = SDL_GL_GetProcAddress("gl" #name); \
        if (address == NULL) return SDL_SetError("missing ES3 function gl" #name), 0; \
        _Static_assert(sizeof address == sizeof gles->gl.name, "GL function pointer width"); \
        memcpy(&gles->gl.name, &address, sizeof address); \
    } while (0);
    GLES_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
    GLint major = 0;
    gles->gl.GetIntegerv(GLES_MAJOR_VERSION, &major);
    if (major < 3)
        return SDL_SetError("OpenGL ES 3 context required"), 0;
    return gl_complete(gles, "ES3 context");
}

static void clear_meshes(PortableRenderGles *gles, int delete_names)
{
    while (gles->meshes != NULL)
    {
        GlesMesh *entry = gles->meshes;
        gles->meshes = entry->next;
        free(entry->textures);
        free(entry);
    }
    while (gles->geometry != NULL)
    {
        GlesGeometry *entry = gles->geometry;
        gles->geometry = entry->next;
        if (delete_names)
        {
            gles->gl.DeleteBuffers(1, &entry->vertices);
            gles->gl.DeleteBuffers(1, &entry->indices);
        }
        free(entry);
    }
    while (gles->textures != NULL)
    {
        GlesTexture *entry = gles->textures;
        gles->textures = entry->next;
        if (delete_names) gles->gl.DeleteTextures(1, &entry->name);
        free(entry);
    }
    gles->mesh_count = gles->texture_count = gles->geometry_count = 0u;
}

static void release_names(PortableRenderGles *gles, int delete_names)
{
    clear_meshes(gles, delete_names);
    if (gles->wrapper != NULL) SDL_DestroyTexture(gles->wrapper);
    if (delete_names)
    {
        gles->gl.DeleteFramebuffers(1, &gles->framebuffer);
        gles->gl.DeleteRenderbuffers(1, &gles->depth);
        gles->gl.DeleteTextures(1, &gles->color);
        gles->gl.DeleteTextures(1, &gles->white);
        gles->gl.DeleteBuffers(2, gles->uniforms);
        gles->gl.DeleteSamplers(5, gles->samplers);
        gles->gl.DeleteVertexArrays(1, &gles->vao);
        if (gles->program != 0u) gles->gl.DeleteProgram(gles->program);
    }
    gles->wrapper = NULL;
    gles->framebuffer = gles->depth = gles->color = gles->white = 0u;
    gles->program = gles->vao = 0u;
    gles->resources_ready = 0;
    memset(gles->uniforms, 0, sizeof gles->uniforms);
    memset(gles->samplers, 0, sizeof gles->samplers);
    gles->width = gles->height = 0u;
}

static void save_state(PortableRenderGles *gles, GlesState *state)
{
    const GlesFunctions *gl = &gles->gl;
    gl->GetIntegerv(GLES_DRAW_FRAMEBUFFER_BINDING, &state->draw_framebuffer);
    gl->GetIntegerv(GLES_READ_FRAMEBUFFER_BINDING, &state->read_framebuffer);
    gl->GetIntegerv(GL_RENDERBUFFER_BINDING, &state->renderbuffer);
    gl->GetIntegerv(GL_CURRENT_PROGRAM, &state->program);
    gl->GetIntegerv(GLES_VERTEX_ARRAY_BINDING, &state->vao);
    gl->GetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
    gl->GetIntegerv(GLES_UNIFORM_BUFFER_BINDING, &state->uniform_buffer);
    gl->GetIntegerv(GLES_PIXEL_PACK_BUFFER_BINDING, &state->pack_buffer);
    gl->GetIntegerv(GLES_PIXEL_UNPACK_BUFFER_BINDING, &state->unpack_buffer);
    for (GLuint i = 0u; i < 2u; ++i)
    {
        gl->GetIntegeri_v(GLES_UNIFORM_BUFFER_BINDING, i, &state->uniform_bindings[i]);
        gl->GetInteger64i_v(GLES_UNIFORM_BUFFER_START, i, &state->uniform_offsets[i]);
        gl->GetInteger64i_v(GLES_UNIFORM_BUFFER_SIZE, i, &state->uniform_sizes[i]);
    }
    gl->GetIntegerv(GL_ACTIVE_TEXTURE, &state->active_texture);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->GetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture);
    gl->GetIntegerv(GLES_SAMPLER_BINDING, &state->sampler);
    gl->GetIntegerv(GL_VIEWPORT, state->viewport);
    gl->GetIntegerv(GL_SCISSOR_BOX, state->scissor);
    gl->GetIntegerv(GL_DEPTH_FUNC, &state->depth_func);
    gl->GetIntegerv(GL_CULL_FACE_MODE, &state->cull_face);
    gl->GetIntegerv(GL_FRONT_FACE, &state->front_face);
    static const GLenum blend_queries[6] = {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA,
                                            GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB,
                                            GL_BLEND_EQUATION_ALPHA};
    for (size_t i = 0u; i < 4u; ++i)
        gl->GetIntegerv(blend_queries[i], &state->blend_func[i]);
    for (size_t i = 0u; i < 2u; ++i)
        gl->GetIntegerv(blend_queries[4u + i], &state->blend_equation[i]);
    gl->GetBooleanv(GL_COLOR_WRITEMASK, state->color_mask);
    gl->GetBooleanv(GL_DEPTH_WRITEMASK, &state->depth_mask);
    gl->GetFloatv(GL_COLOR_CLEAR_VALUE, state->clear_color);
    gl->GetFloatv(GL_DEPTH_CLEAR_VALUE, &state->clear_depth);
    gl->GetFloatv(GL_DEPTH_RANGE, state->depth_range);
    for (size_t i = 0u; i < 9u; ++i)
        state->enabled[i] = gl->IsEnabled(saved_capabilities[i]);
    for (size_t i = 0u; i < 8u; ++i)
        gl->GetIntegerv(pixel_stores[i], &state->pixel_stores[i]);
}

static int restore_state(PortableRenderGles *gles, const GlesState *state)
{
    const GlesFunctions *gl = &gles->gl;
    gl->BindFramebuffer(GLES_DRAW_FRAMEBUFFER, (GLuint)state->draw_framebuffer);
    gl->BindFramebuffer(GLES_READ_FRAMEBUFFER, (GLuint)state->read_framebuffer);
    gl->BindRenderbuffer(GL_RENDERBUFFER, (GLuint)state->renderbuffer);
    gl->BindVertexArray((GLuint)state->vao);
    gl->BindBuffer(GL_ARRAY_BUFFER, (GLuint)state->array_buffer);
    for (GLuint i = 0u; i < 2u; ++i)
    {
        if (state->uniform_bindings[i] != 0 && state->uniform_sizes[i] > 0)
            gl->BindBufferRange(GLES_UNIFORM_BUFFER, i, (GLuint)state->uniform_bindings[i],
                                (GLintptr)state->uniform_offsets[i], (GLsizeiptr)state->uniform_sizes[i]);
        else gl->BindBufferBase(GLES_UNIFORM_BUFFER, i, (GLuint)state->uniform_bindings[i]);
    }
    gl->BindBuffer(GLES_UNIFORM_BUFFER, (GLuint)state->uniform_buffer);
    gl->BindBuffer(GLES_PIXEL_PACK_BUFFER, (GLuint)state->pack_buffer);
    gl->BindBuffer(GLES_PIXEL_UNPACK_BUFFER, (GLuint)state->unpack_buffer);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, (GLuint)state->texture);
    gl->BindSampler(0u, (GLuint)state->sampler);
    gl->ActiveTexture((GLenum)state->active_texture);
    gl->UseProgram((GLuint)state->program);
    gl->Viewport(state->viewport[0], state->viewport[1], state->viewport[2], state->viewport[3]);
    gl->Scissor(state->scissor[0], state->scissor[1], state->scissor[2], state->scissor[3]);
    gl->DepthFunc((GLenum)state->depth_func);
    gl->CullFace((GLenum)state->cull_face);
    gl->FrontFace((GLenum)state->front_face);
    gl->BlendFuncSeparate((GLenum)state->blend_func[0], (GLenum)state->blend_func[1],
                          (GLenum)state->blend_func[2], (GLenum)state->blend_func[3]);
    gl->BlendEquationSeparate((GLenum)state->blend_equation[0], (GLenum)state->blend_equation[1]);
    gl->DepthRangef(state->depth_range[0], state->depth_range[1]);
    gl->ColorMask(state->color_mask[0], state->color_mask[1], state->color_mask[2], state->color_mask[3]);
    gl->DepthMask(state->depth_mask);
    gl->ClearColor(state->clear_color[0], state->clear_color[1], state->clear_color[2], state->clear_color[3]);
    gl->ClearDepthf(state->clear_depth);
    for (size_t i = 0u; i < 9u; ++i)
    {
        if (state->enabled[i]) gl->Enable(saved_capabilities[i]);
        else gl->Disable(saved_capabilities[i]);
    }
    for (size_t i = 0u; i < 8u; ++i) gl->PixelStorei(pixel_stores[i], state->pixel_stores[i]);
    /* Restore the arbitrary GL state above, then invalidate SDL's own cache. */
    const int restored = gl_complete(gles, "restore compositor state");
    const int flushed = SDL_FlushRenderer(gles->renderer);
    return restored && flushed;
}

static int begin_boundary(PortableRenderGles *gles, GlesState *state)
{
    if (!SDL_FlushRenderer(gles->renderer)) return 0;
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    if (context == NULL) return SDL_SetError("SDL renderer has no current GLES context"), 0;
    if (gles->invalidated || context != gles->context)
    {
        /* Names from a lost context must never be deleted in a new context:
         * its unrelated objects may already have reused those numeric names. */
        release_names(gles, 0);
        gles->context = context;
        gles->invalidated = 0;
        if (!load_functions(gles)) { gles->invalidated = 1; return 0; }
    }
    memset(state, 0, sizeof *state);
    save_state(gles, state);
    if (!gl_complete(gles, "capture compositor state"))
    {
        if (!gles->invalidated) restore_state(gles, state);
        return 0;
    }
    return 1;
}

static GLuint compile_shader(PortableRenderGles *gles, GLenum stage,
                              const PortableSceneShaderCode *code)
{
    const GLuint shader = gles->gl.CreateShader(stage);
    if (shader == 0u)
    {
        if (gl_complete(gles, "allocate shader")) SDL_SetError("shader allocation failed");
        return 0u;
    }
    const GLchar *source = (const GLchar *)code->bytes;
    const GLint length = (GLint)code->size;
    gles->gl.ShaderSource(shader, 1, &source, &length);
    gles->gl.CompileShader(shader);
    GLint compiled = 0;
    gles->gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[512] = {0};
        gles->gl.GetShaderInfoLog(shader, (GLsizei)sizeof log, NULL, log);
        SDL_SetError("%s shader: %s", stage == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        gles->gl.DeleteShader(shader);
        return 0u;
    }
    return shader;
}

static int create_program(PortableRenderGles *gles)
{
    const GlesFunctions *gl = &gles->gl;
    GLuint vertex = compile_shader(gles, GL_VERTEX_SHADER, &portable_scene_vertex_glsl_code);
    GLuint fragment = vertex != 0u ? compile_shader(gles, GL_FRAGMENT_SHADER,
                                                    &portable_scene_fragment_glsl_code) : 0u;
    if (fragment != 0u)
    {
        gles->program = gl->CreateProgram();
        if (gles->program != 0u)
        {
            gl->AttachShader(gles->program, vertex);
            gl->AttachShader(gles->program, fragment);
            gl->LinkProgram(gles->program);
        }
        else if (gl_complete(gles, "allocate shader program")) SDL_SetError("program allocation failed");
    }
    if (vertex != 0u) gl->DeleteShader(vertex);
    if (fragment != 0u) gl->DeleteShader(fragment);
    if (fragment == 0u || gles->program == 0u) return 0;
    GLint linked = 0;
    gl->GetProgramiv(gles->program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512] = {0};
        gl->GetProgramInfoLog(gles->program, (GLsizei)sizeof log, NULL, log);
        return SDL_SetError("shader link: %s", log), 0;
    }
    const char *blocks[2] = {PORTABLE_SCENE_SHADER_GLES_VERTEX_BLOCK,
                              PORTABLE_SCENE_SHADER_GLES_FRAGMENT_BLOCK};
    const GLint sizes[2] = {PORTABLE_SCENE_SHADER_VERTEX_UNIFORM_BYTES, PORTABLE_SCENE_SHADER_FRAGMENT_UNIFORM_BYTES};
    gl->UseProgram(gles->program);
    for (GLuint i = 0u; i < 2u; ++i)
    {
        const GLuint block = gl->GetUniformBlockIndex(gles->program, blocks[i]);
        GLint size = 0;
        if (block != UINT32_MAX) gl->GetActiveUniformBlockiv(gles->program, block,
                                                           GLES_UNIFORM_BLOCK_DATA_SIZE, &size);
        if (block == UINT32_MAX || size != sizes[i])
            return SDL_SetError("uniform block %s layout: expected %d, got %d", blocks[i], sizes[i], size), 0;
        gl->UniformBlockBinding(gles->program, block, i);
    }
    const GLint sampler = gl->GetUniformLocation(gles->program, PORTABLE_SCENE_SHADER_GLES_SAMPLER);
    if (sampler < 0) return SDL_SetError("scene sampler unavailable"), 0;
    gl->Uniform1i(sampler, 0);
    return gl_complete(gles, "shader program");
}

static void texture_parameters(const GlesFunctions *gl)
{
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static int create_resources(PortableRenderGles *gles)
{
    if (gles->resources_ready) return 1;
    const GlesFunctions *gl = &gles->gl;
    if (!create_program(gles)) { release_names(gles, !gles->invalidated); return 0; }
    gl->GenVertexArrays(1, &gles->vao);
    gl->GenBuffers(2, gles->uniforms);
    for (GLuint i = 0u; i < 2u; ++i)
    {
        gl->BindBuffer(GLES_UNIFORM_BUFFER, gles->uniforms[i]);
        gl->BufferData(GLES_UNIFORM_BUFFER, i ? PORTABLE_SCENE_SHADER_FRAGMENT_UNIFORM_BYTES
                                              : PORTABLE_SCENE_SHADER_VERTEX_UNIFORM_BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    gl->GenSamplers(5, gles->samplers);
    for (unsigned i = 0u; i < 5u; ++i)
    {
        const GLint filter = i == 4u ? GL_LINEAR : GL_NEAREST;
        gl->SamplerParameteri(gles->samplers[i], GL_TEXTURE_MIN_FILTER, filter);
        gl->SamplerParameteri(gles->samplers[i], GL_TEXTURE_MAG_FILTER, filter);
        gl->SamplerParameteri(gles->samplers[i], GL_TEXTURE_WRAP_S, i & 1u ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        gl->SamplerParameteri(gles->samplers[i], GL_TEXTURE_WRAP_T, i & 2u ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    }
    gl->BindBuffer(GLES_PIXEL_UNPACK_BUFFER, 0u);
    for (unsigned i = 4u; i < 8u; ++i) gl->PixelStorei(pixel_stores[i], i == 4u ? 1 : 0);
    const uint8_t white[4] = {255u, 255u, 255u, 255u};
    gl->GenTextures(1, &gles->white);
    gl->BindTexture(GL_TEXTURE_2D, gles->white);
    texture_parameters(gl);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GLES_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    const int valid = gl_complete(gles, "scene resources");
    if (!valid) { release_names(gles, !gles->invalidated); return 0; }
    gles->resources_ready = 1;
    return 1;
}

PortableRenderGles *portable_render_gles_create(SDL_Renderer *renderer, char *error, size_t capacity)
{
    const char *name = renderer != NULL ? SDL_GetRendererName(renderer) : NULL;
    if (name == NULL || strcmp(name, "opengles2") != 0)
    {
        SDL_SetError("opengles2 SDL renderer required");
        failure(error, capacity, "context");
        return NULL;
    }
    PortableRenderGles *gles = calloc(1u, sizeof *gles);
    if (gles == NULL)
    {
        SDL_SetError("owner allocation failed");
        failure(error, capacity, "allocation");
        return NULL;
    }
    gles->renderer = renderer;
    GlesState state;
    if (!begin_boundary(gles, &state))
    {
        failure(error, capacity, "context");
        free(gles);
        return NULL;
    }
    int valid = create_resources(gles);
    if (!valid) failure(error, capacity, "create resources");
    if (!restore_state(gles, &state) && valid)
    {
        valid = 0;
        failure(error, capacity, "restore compositor");
    }
    if (!valid)
    {
        portable_render_gles_close(gles);
        return NULL;
    }
    if (error != NULL && capacity != 0u) error[0] = '\0';
    return gles;
}

/* Looks up or uploads an image and marks it drawn this frame; *found is NULL
 * for the shared white texture. */
static GLuint texture_entry(PortableRenderGles *gles, const PortableNativeImage *image, GlesTexture **found)
{
    *found = NULL;
    if (image->pixels == NULL) return gles->white;
    for (GlesTexture *entry = gles->textures; entry != NULL; entry = entry->next)
        if (entry->image.pixels == image->pixels && entry->image.palette == image->palette &&
            entry->image.width == image->width && entry->image.height == image->height &&
            entry->image.palette_count == image->palette_count && entry->image.palette_base == image->palette_base)
        {
            entry->used = gles->frame;
            *found = entry;
            return entry->name;
        }
    if (gles->texture_count >= 4096u) return SDL_SetError("texture cache capacity"), 0u;
    GlesTexture *entry = calloc(1u, sizeof *entry);
    const size_t bytes = (size_t)image->width * image->height * 4u;
    uint8_t *pixels = malloc(bytes);
    if (entry == NULL || pixels == NULL)
    {
        free(entry); free(pixels);
        return SDL_SetError("texture upload allocation"), 0u;
    }
    for (size_t y = 0u; y < image->height; ++y)
        for (size_t x = 0u; x < image->width; ++x)
        {
            const uint32_t color = portable_native_image_pixel(image, x, y);
            uint8_t *pixel = pixels + (y * image->width + x) * 4u;
            pixel[0] = (uint8_t)(color >> 16u); pixel[1] = (uint8_t)(color >> 8u);
            pixel[2] = (uint8_t)color; pixel[3] = (uint8_t)(color >> 24u);
        }
    const GlesFunctions *gl = &gles->gl;
    gl->GenTextures(1, &entry->name);
    gl->BindTexture(GL_TEXTURE_2D, entry->name);
    texture_parameters(gl);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GLES_RGBA8, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    if (!gl_complete(gles, "texture upload"))
    {
        gl->DeleteTextures(1, &entry->name); free(entry); return 0u;
    }
    entry->image = *image; entry->used = gles->frame; entry->next = gles->textures; gles->textures = entry;
    ++gles->texture_count;
    *found = entry;
    return entry->name;
}

static GLuint cached_texture(PortableRenderGles *gles, const PortableNativeImage *image)
{
    GlesTexture *found;
    return texture_entry(gles, image, &found);
}

static GlesVertex *gles_vertices(const PortableRenderMesh *mesh)
{
    GlesVertex *vertices = malloc((size_t)mesh->vertex_count * sizeof(GlesVertex));
    if (!vertices)
        return SDL_SetError("geometry upload allocation"), NULL;
    for (uint32_t i = 0; i < mesh->vertex_count; ++i)
    {
        for (unsigned axis = 0; axis < 3; ++axis)
            vertices[i].position[axis] = (float)mesh->vertices[i].position[axis];
        for (unsigned axis = 0; axis < 2; ++axis)
            vertices[i].uv[axis] = (float)mesh->vertices[i].uv[axis];
        for (unsigned axis = 0; axis < 3; ++axis)
            vertices[i].normal[axis] = (float)mesh->vertices[i].normal[axis];
        const uint32_t argb = mesh->vertex_argb ? mesh->vertex_argb[i] : UINT32_MAX;
        const uint8_t rgba[4] = {(uint8_t)(argb >> 16u), (uint8_t)(argb >> 8u), (uint8_t)argb,
                                 (uint8_t)(argb >> 24u)};
        memcpy(vertices[i].color, rgba, sizeof rgba);
    }
    return vertices;
}

/* A new revision re-uploads the vertices of a mesh skinned in place. */
static GlesGeometry *cached_geometry(PortableRenderGles *gles, const PortableRenderMesh *mesh)
{
    for (GlesGeometry *entry = gles->geometry; entry; entry = entry->next)
        if (entry->source_vertices == mesh->vertices && entry->source_indices == mesh->indices &&
            entry->vertex_count == mesh->vertex_count && entry->index_count == mesh->index_count)
        {
            if (entry->revision != mesh->revision)
            {
                GlesVertex *vertices = gles_vertices(mesh);
                if (!vertices)
                    return NULL;
                const GlesFunctions *gl = &gles->gl;
                gl->BindBuffer(GL_ARRAY_BUFFER, entry->vertices);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mesh->vertex_count * sizeof(GlesVertex)), vertices,
                               GL_DYNAMIC_DRAW);
                free(vertices);
                if (!gl_complete(gles, "geometry update"))
                    return NULL;
                entry->revision = mesh->revision;
            }
            entry->used = gles->frame;
            return entry;
        }
    if (gles->geometry_count >= PORTABLE_RENDER_INSTANCE_LIMIT)
        return SDL_SetError("geometry cache capacity"), NULL;
    const size_t vertex_bytes = (size_t)mesh->vertex_count * sizeof(GlesVertex);
    const size_t index_bytes = (size_t)mesh->index_count * sizeof(uint32_t);
    if (vertex_bytes / sizeof(GlesVertex) != mesh->vertex_count ||
        index_bytes / sizeof(uint32_t) != mesh->index_count || vertex_bytes > (size_t)PTRDIFF_MAX ||
        index_bytes > (size_t)PTRDIFF_MAX || mesh->index_count > (uint32_t)INT_MAX)
        return SDL_SetError("mesh buffer capacity"), NULL;
    GlesGeometry *entry = calloc(1u, sizeof *entry);
    GlesVertex *vertices = entry ? gles_vertices(mesh) : NULL;
    if (!entry || !vertices)
    {
        free(entry);
        return SDL_SetError("geometry upload allocation"), NULL;
    }
    /* Buffer storage is target-independent. ARRAY uploads leave the borrowed
     * compositor VAO's element binding intact. */
    const GlesFunctions *gl = &gles->gl;
    gl->GenBuffers(1, &entry->vertices);
    gl->GenBuffers(1, &entry->indices);
    gl->BindBuffer(GL_ARRAY_BUFFER, entry->vertices);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertex_bytes, vertices, GL_STATIC_DRAW);
    gl->BindBuffer(GL_ARRAY_BUFFER, entry->indices);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)index_bytes, mesh->indices, GL_STATIC_DRAW);
    free(vertices);
    if (!gl_complete(gles, "geometry upload"))
    {
        gl->DeleteBuffers(1, &entry->vertices);
        gl->DeleteBuffers(1, &entry->indices);
        free(entry);
        return NULL;
    }
    entry->source_vertices = mesh->vertices;
    entry->source_indices = mesh->indices;
    entry->vertex_count = mesh->vertex_count;
    entry->index_count = mesh->index_count;
    entry->revision = mesh->revision;
    entry->used = gles->frame;
    entry->next = gles->geometry;
    gles->geometry = entry;
    ++gles->geometry_count;
    return entry;
}

static int geometry_known(const void *context, const PortableRenderMesh *mesh)
{
    const PortableRenderGles *owner = context;
    if (owner->invalidated || SDL_GL_GetCurrentContext() != owner->context) return 0;
    for (const GlesMesh *entry = owner->meshes; entry; entry = entry->next)
        if (entry->source == mesh)
            return entry->geometry->source_vertices == mesh->vertices &&
                entry->geometry->source_indices == mesh->indices &&
                entry->geometry->vertex_count == mesh->vertex_count &&
                entry->geometry->index_count == mesh->index_count &&
                entry->material_count == mesh->material_count ?
                (entry->geometry->revision == mesh->revision ? 1 : 0) : -1;
    for (const GlesGeometry *entry = owner->geometry; entry; entry = entry->next)
        if (entry->source_vertices == mesh->vertices && entry->source_indices == mesh->indices &&
            entry->vertex_count == mesh->vertex_count && entry->index_count == mesh->index_count)
            return entry->revision == mesh->revision;
    return 0;
}

/* Pattern frames upload with their mesh, never inside a draw. */
static int cached_animation_keys(PortableRenderGles *gles, const PortableRenderMesh *mesh, uint32_t material)
{
    const PortableRenderTextureAnimation *animation = mesh->materials[material].animation;
    for (uint32_t key = 0; animation && key < animation->key_count; ++key)
        if (cached_texture(gles, &animation->key_textures[key]) == 0u)
            return 0;
    return 1;
}

/* Frees cache entries last drawn before frame `keep`; returns how many went.
 * A kept mesh marked its geometry and textures, so they stay with it. */
static size_t evict_stale(PortableRenderGles *gles, uint64_t keep)
{
    const size_t before = gles->mesh_count + gles->geometry_count + gles->texture_count;
    const GlesFunctions *gl = &gles->gl;
    for (GlesMesh **link = &gles->meshes; *link;)
    {
        GlesMesh *entry = *link;
        if (entry->used >= keep) { link = &entry->next; continue; }
        *link = entry->next;
        free(entry->textures);
        free(entry);
        --gles->mesh_count;
    }
    for (GlesGeometry **link = &gles->geometry; *link;)
    {
        GlesGeometry *entry = *link;
        if (entry->used >= keep) { link = &entry->next; continue; }
        *link = entry->next;
        gl->DeleteBuffers(1, &entry->vertices);
        gl->DeleteBuffers(1, &entry->indices);
        free(entry);
        --gles->geometry_count;
    }
    for (GlesTexture **link = &gles->textures; *link;)
    {
        GlesTexture *entry = *link;
        if (entry->used >= keep) { link = &entry->next; continue; }
        *link = entry->next;
        gl->DeleteTextures(1, &entry->name);
        free(entry);
        --gles->texture_count;
    }
    return before - (gles->mesh_count + gles->geometry_count + gles->texture_count);
}

static GlesMesh *cached_mesh(PortableRenderGles *gles, const PortableRenderMesh *mesh)
{
    for (GlesMesh *entry = gles->meshes; entry; entry = entry->next)
        if (entry->source == mesh)
        {
            if (entry->geometry->revision != mesh->revision && !cached_geometry(gles, mesh))
                return NULL;
            entry->used = entry->geometry->used = gles->frame;
            for (uint32_t i = 0; i < entry->material_count; ++i)
            {
                if (entry->textures[i])
                    entry->textures[i]->used = gles->frame;
                if (!cached_animation_keys(gles, mesh, i))
                    return NULL;
            }
            return entry;
        }
    if (gles->mesh_count >= PORTABLE_RENDER_INSTANCE_LIMIT)
        return SDL_SetError("mesh cache capacity"), NULL;
    GlesMesh *entry = calloc(1u, sizeof *entry);
    if (!entry) return SDL_SetError("mesh upload allocation"), NULL;
    entry->textures = calloc(mesh->material_count, sizeof *entry->textures);
    int valid = entry->textures != NULL;
    for (uint32_t i = 0; valid && i < mesh->material_count; ++i)
        valid = texture_entry(gles, &mesh->materials[i].texture, &entry->textures[i]) != 0u &&
                cached_animation_keys(gles, mesh, i);
    if (valid) entry->geometry = cached_geometry(gles, mesh);
    if (!entry->geometry)
    {
        free(entry->textures);
        free(entry);
        return NULL;
    }
    entry->source = mesh;
    entry->material_count = mesh->material_count;
    entry->used = gles->frame;
    entry->next = gles->meshes;
    gles->meshes = entry;
    ++gles->mesh_count;
    return entry;
}

static int resize_target(PortableRenderGles *gles, size_t width, size_t height, GlesState *state)
{
    if (width == gles->width && height == gles->height && gles->wrapper != NULL) return 1;
    const GlesFunctions *gl = &gles->gl;
    GLint texture_limit = 0, depth_limit = 0;
    gl->GetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    gl->GetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &depth_limit);
    if (texture_limit <= 0 || depth_limit <= 0 || width > (size_t)texture_limit ||
        height > (size_t)texture_limit || width > (size_t)depth_limit || height > (size_t)depth_limit)
        return SDL_SetError("target exceeds GLES device limits"), 0;
    GLuint color = 0u, depth = 0u, framebuffer = 0u;
    gl->GenTextures(1, &color); gl->BindTexture(GL_TEXTURE_2D, color); texture_parameters(gl);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GLES_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->GenRenderbuffers(1, &depth); gl->BindRenderbuffer(GL_RENDERBUFFER, depth);
    gl->RenderbufferStorage(GL_RENDERBUFFER, GLES_DEPTH_COMPONENT32F, (GLsizei)width, (GLsizei)height);
    gl->GenFramebuffers(1, &framebuffer); gl->BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    gl->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    int valid = gl->CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!valid) SDL_SetError("offscreen framebuffer incomplete");
    if (valid) valid = gl_complete(gles, "target allocation");
    SDL_Texture *wrapper = NULL;
    if (valid)
    {
        SDL_PropertiesID props = SDL_CreateProperties();
        if (props != 0u)
        {
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER, color);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, (Sint64)width);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, (Sint64)height);
            wrapper = SDL_CreateTextureWithProperties(gles->renderer, props);
            SDL_DestroyProperties(props);
        }
        valid = wrapper != NULL && SDL_SetTextureBlendMode(wrapper, SDL_BLENDMODE_NONE) &&
                SDL_SetTextureScaleMode(wrapper, SDL_SCALEMODE_NEAREST);
    }
    if (!valid)
    {
        if (wrapper != NULL) SDL_DestroyTexture(wrapper);
        gl->DeleteFramebuffers(1, &framebuffer); gl->DeleteRenderbuffers(1, &depth); gl->DeleteTextures(1, &color);
        return 0;
    }
    /* The previous wrapper may have been SDL's last sampled texture. Do not
     * restore a deleted name; SDL's cache will rebind the new wrapper. */
    if ((GLuint)state->texture == gles->color && gles->color != 0u) state->texture = 0;
    if (gles->wrapper != NULL) SDL_DestroyTexture(gles->wrapper);
    gl->DeleteFramebuffers(1, &gles->framebuffer); gl->DeleteRenderbuffers(1, &gles->depth); gl->DeleteTextures(1, &gles->color);
    gles->wrapper = wrapper; gles->color = color; gles->depth = depth; gles->framebuffer = framebuffer;
    gles->width = width; gles->height = height;
    return 1;
}

static void matrix(float out[16], const PortableRenderView *view, const PortableRenderInstance *instance)
{
    double camera[3][4];
    const double *basis[3] = {view->right, view->up, view->forward};
    for (unsigned row = 0u; row < 3u; ++row)
        for (unsigned column = 0u; column < 4u; ++column)
        {
            camera[row][column] = 0.0;
            for (unsigned axis = 0u; axis < 3u; ++axis)
                camera[row][column] += basis[row][axis] * (instance->transform[axis * 4u + column] -
                                                          (column == 3u ? view->position[axis] : 0.0));
        }
    const PortableScene3DProjection *p = &view->projection;
    const double sx = 2.0 * p->focal_x / (double)view->width, sy = 2.0 * p->focal_y / (double)view->height;
    const double ox = 2.0 * p->center_x / (double)view->width - 1.0, oy = 1.0 - 2.0 * p->center_y / (double)view->height;
    const double depth = p->far_z / (p->far_z - p->near_z);
    for (unsigned column = 0u; column < 4u; ++column)
    {
        out[column * 4u] = (float)(sx * camera[0][column] + ox * camera[2][column]);
        /* SDL samples texture v=0 at its logical top. Render that top into
         * GL's bottom row, avoiding a separate flip/composition pass. */
        out[column * 4u + 1u] = (float)-(sy * camera[1][column] + oy * camera[2][column]);
        out[column * 4u + 2u] = (float)(depth * camera[2][column] - (column == 3u ? depth * p->near_z : 0.0));
        out[column * 4u + 3u] = (float)camera[2][column];
    }
}

SDL_Texture *portable_render_gles_draw(PortableRenderGles *gles, const PortableRenderFrame *frame,
                                      size_t width, size_t height, char *error, size_t capacity)
{
    if (gles == NULL || width == 0u || height == 0u || width > PORTABLE_RENDER_TARGET_EXTENT_LIMIT ||
        height > PORTABLE_RENDER_TARGET_EXTENT_LIMIT || width > PORTABLE_RENDER_TARGET_PIXEL_LIMIT / height)
    {
        SDL_SetError("target capacity"); failure(error, capacity, "target"); return NULL;
    }
    if (!portable_render_frame_valid_geometry(frame, width, height, geometry_known, gles, error, capacity)) return NULL;
    GlesState state;
    if (!begin_boundary(gles, &state)) { failure(error, capacity, "compositor boundary"); return NULL; }
    const GlesFunctions *gl = &gles->gl;
    GLint viewport_limits[2] = {0, 0};
    gl->GetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport_limits);
    int valid = viewport_limits[0] > 0 && viewport_limits[1] > 0;
    for (size_t i = 0u; valid && i < frame->view_count; ++i)
        valid = frame->views[i].width <= (size_t)viewport_limits[0] &&
                frame->views[i].height <= (size_t)viewport_limits[1];
    if (!valid) SDL_SetError("viewport exceeds GLES device limits");
    if (valid) valid = create_resources(gles);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindBuffer(GLES_PIXEL_UNPACK_BUFFER, 0u);
    for (unsigned i = 4u; i < 8u; ++i) gl->PixelStorei(pixel_stores[i], i == 4u ? 1 : 0);
    ++gles->frame;
    for (size_t i = 0u, evictions = 0u; valid && i < frame->instance_count; ++i)
        if (cached_mesh(gles, frame->instances[i].mesh) == NULL)
        {
            /* Sprite frames and palette variants accumulate past the caches
             * over a race: drop what went undrawn for a second, then anything
             * this frame has not drawn yet, and retry. */
            const uint64_t recent = gles->frame > 60u ? gles->frame - 60u : 0u;
            if (evictions++ < 2u && (evict_stale(gles, recent) || evict_stale(gles, gles->frame)))
                --i;
            else
                valid = 0;
        }
    if (valid) valid = resize_target(gles, width, height, &state);
    if (!valid)
    {
        failure(error, capacity, "prepare frame"); restore_state(gles, &state); return NULL;
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER, gles->framebuffer);
    gl->BindVertexArray(gles->vao);
    gl->UseProgram(gles->program);
    gl->EnableVertexAttribArray(0u); gl->EnableVertexAttribArray(1u); gl->EnableVertexAttribArray(2u);
    gl->EnableVertexAttribArray(3u);
    gl->BindBufferBase(GLES_UNIFORM_BUFFER, 0u, gles->uniforms[0]);
    gl->BindBufferBase(GLES_UNIFORM_BUFFER, 1u, gles->uniforms[1]);
    gl->Disable(GL_BLEND); gl->Disable(GL_STENCIL_TEST); gl->Disable(GL_POLYGON_OFFSET_FILL);
    gl->Disable(GL_SAMPLE_ALPHA_TO_COVERAGE); gl->Disable(GLES_RASTERIZER_DISCARD); gl->Disable(GL_DITHER);
    gl->Disable(GL_SCISSOR_TEST); gl->Enable(GL_DEPTH_TEST);
    gl->DepthFunc(GL_LEQUAL); gl->DepthMask(GL_TRUE); gl->DepthRangef(0.0f, 1.0f);
    gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    /* Only multiply, additive and alpha-blended materials enable blending; each run sets its factors. */
    gl->BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    gl->BlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    gl->CullFace(GL_BACK);
    /* Mirroring Y reverses the GPU backend's clockwise front faces. */
    gl->FrontFace(GL_CCW);
    gl->ClearColor(0, 0, 0, 1); gl->ClearDepthf(1.0f); gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->Enable(GL_SCISSOR_TEST);
    for (size_t pane = 0u; pane < frame->view_count; ++pane)
    {
        const PortableRenderView *view = &frame->views[pane];
        gl->Viewport((GLint)view->x, (GLint)view->y, (GLsizei)view->width, (GLsizei)view->height);
        gl->Scissor((GLint)view->x, (GLint)view->y, (GLsizei)view->width, (GLsizei)view->height);
        gl->ClearColor((float)((view->clear_argb >> 16u) & 255u) / 255.0f,
                        (float)((view->clear_argb >> 8u) & 255u) / 255.0f,
                        (float)(view->clear_argb & 255u) / 255.0f, 1.0f);
        gl->DepthMask(GL_TRUE); /* A prior pane may end on a multiply run. */
        gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        /* Backdrops, then every other run in mesh order, then course alpha
         * blending, matching the GPU backend. */
        for (unsigned stage = 0u; stage < 3u; ++stage)
        for (size_t i = 0u; i < frame->instance_count; ++i)
        {
            if (!portable_render_instance_visible(&frame->instances[i], pane))
                continue;
            PortableRenderInstance oriented = frame->instances[i];
            portable_render_instance_for_view(&frame->instances[i], view, &oriented);
            const PortableRenderInstance *instance = &oriented;
            size_t clip_first = 0, clip_count = 0;
            if (!portable_render_instance_clip_rows(instance, view, &clip_first, &clip_count) ||
                !clip_count)
                continue; /* Invalid intervals fail frame preflight. */
            gl->Scissor((GLint)view->x, (GLint)(view->y + clip_first),
                        (GLsizei)view->width, (GLsizei)clip_count);
            GlesMesh *cached = cached_mesh(gles, instance->mesh);
            float transform[16], world_transform[16];
            if (stage == 0u)
            {
                /* Backdrops sit around the eye unless world-placed, past the far plane. */
                PortableRenderView backdrop = *view;
                PortableRenderInstance anchored = *instance;
                backdrop.projection.far_z = 65535.0;
                matrix(world_transform, &backdrop, instance);
                for (unsigned axis = 0u; axis < 3u; ++axis)
                    anchored.transform[axis * 4u + 3u] = view->position[axis];
                matrix(transform, &backdrop, &anchored);
            }
            else
                matrix(transform, view, instance);
            int bound = 0;
            for (uint32_t run_index = 0u; run_index < instance->mesh->run_count; ++run_index)
            {
                const PortableRenderRun *run = &instance->mesh->runs[run_index];
                const PortableRenderMaterial *material = &instance->mesh->materials[run->material];
                const int background = (material->flags & PORTABLE_RENDER_MATERIAL_BACKGROUND) != 0u;
                const int blend = (material->flags & PORTABLE_RENDER_MATERIAL_ALPHA_BLEND) != 0u;
                if ((background ? 0u : blend ? 2u : 1u) != stage)
                    continue;
                if (!bound)
                {
                    gl->BindBuffer(GLES_UNIFORM_BUFFER, gles->uniforms[0]);
                    gl->BufferSubData(GLES_UNIFORM_BUFFER, 0, sizeof transform, transform);
                    gl->BindBuffer(GL_ARRAY_BUFFER, cached->geometry->vertices);
                    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, cached->geometry->indices);
                    gl->VertexAttribPointer(0u, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(GlesVertex), (const void *)(uintptr_t)0u);
                    gl->VertexAttribPointer(1u, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(GlesVertex), (const void *)(uintptr_t)12u);
                    gl->VertexAttribPointer(2u, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)sizeof(GlesVertex), (const void *)(uintptr_t)20u);
                    gl->VertexAttribPointer(3u, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(GlesVertex), (const void *)(uintptr_t)24u);
                    bound = 1;
                }
                if (background)
                {
                    gl->BindBuffer(GLES_UNIFORM_BUFFER, gles->uniforms[0]);
                    gl->BufferSubData(GLES_UNIFORM_BUFFER, 0, sizeof transform,
                                      material->flags & PORTABLE_RENDER_MATERIAL_WORLD_SKY ? world_transform : transform);
                }
                GlesPaint paint = {.base_argb = material->argb, .tint_argb = instance->tint_argb,
                    .uv_offset = {(float)instance->uv_offset[0], (float)instance->uv_offset[1]},
                    .cutoff_q16 = (uint32_t)material->alpha_cutoff_q16,
                    .alpha_mode = blend ? 2u : (material->flags & 16u) != 0u,
                    .uv_u = {1, 0, 0, 0}, .uv_v = {0, 1, 0, 0}};
                paint.padding[1] = instance->add_argb & 0x00ffffffu;
                portable_render_paint_environment(frame, view, instance, material, stage == 0u, paint.environment);
                if (paint.environment[3] > 0.0f) /* lit: the diffuse lives in the light terms */
                    paint.base_argb |= 0x00ffffffu;
                GLuint image = cached->textures[run->material] ? cached->textures[run->material]->name : gles->white;
                if (material->animation)
                {
                    double uv[6];
                    const PortableNativeImage *source;
                    portable_render_material_sample(material, frame->animation_tick, frame->animation_rewind_q16, uv, &source);
                    const float u[4] = {(float)uv[0], (float)uv[2], (float)uv[4], 0.0f};
                    const float v[4] = {(float)uv[1], (float)uv[3], (float)uv[5], 0.0f};
                    memcpy(paint.uv_u, u, sizeof u);
                    memcpy(paint.uv_v, v, sizeof v);
                    image = cached_texture(gles, source); /* uploaded with the mesh */
                }
                const int multiply = (material->flags & PORTABLE_RENDER_MATERIAL_MULTIPLY) != 0u;
                const int additive = (material->flags & PORTABLE_RENDER_MATERIAL_ADDITIVE) != 0u;
                if (multiply || additive || (material->flags & 4u)) gl->Disable(GL_CULL_FACE); else gl->Enable(GL_CULL_FACE);
                if (background) gl->Disable(GL_DEPTH_TEST); else gl->Enable(GL_DEPTH_TEST);
                if (multiply || additive || blend)
                {
                    gl->Enable(GL_BLEND); gl->DepthMask(GL_FALSE);
                    gl->BlendFuncSeparate(blend ? GL_SRC_ALPHA : multiply ? GL_DST_COLOR : GL_ONE,
                                          blend ? GL_ONE_MINUS_SRC_ALPHA : multiply ? GL_ZERO : GL_ONE,
                                          GL_ZERO, GL_ONE);
                }
                else { gl->Disable(GL_BLEND); gl->DepthMask(background ? GL_FALSE : GL_TRUE); }
                gl->BindTexture(GL_TEXTURE_2D, image);
                gl->BindSampler(0u, gles->samplers[multiply || additive ? 4u : material->flags & 3u]);
                gl->BindBuffer(GLES_UNIFORM_BUFFER, gles->uniforms[1]);
                gl->BufferSubData(GLES_UNIFORM_BUFFER, 0, sizeof paint, &paint);
                gl->DrawElements(GL_TRIANGLES, (GLsizei)run->index_count, GL_UNSIGNED_INT,
                                  (const void *)(uintptr_t)((size_t)run->first_index * sizeof(uint32_t)));
            }
        }
    }
    valid = gl_complete(gles, "draw frame");
    if (!valid) failure(error, capacity, "draw");
    if (!restore_state(gles, &state) && valid)
    {
        valid = 0;
        failure(error, capacity, "restore compositor");
    }
    if (!valid) return NULL;
    if (error != NULL && capacity != 0u) error[0] = '\0';
    return gles->wrapper;
}

int portable_render_gles_wait_idle(PortableRenderGles *gles, char *error, size_t capacity)
{
    if (gles == NULL)
    {
        SDL_SetError("null GLES backend");
        return failure(error, capacity, "wait");
    }
    GlesState state;
    if (!begin_boundary(gles, &state))
        return failure(error, capacity, "wait compositor boundary");
    gles->gl.Finish();
    int valid = gl_complete(gles, "wait for completed scene/compositor work");
    if (!valid)
        failure(error, capacity, "wait");
    if (!restore_state(gles, &state) && valid)
    {
        valid = 0;
        failure(error, capacity, "restore wait state");
    }
    if (valid && error != NULL && capacity != 0u)
        error[0] = '\0';
    return valid;
}

int portable_render_gles_read_pixels(PortableRenderGles *gles, uint32_t *pixels, size_t stride,
                                     char *error, size_t capacity)
{
    if (gles == NULL || pixels == NULL || gles->wrapper == NULL || stride < gles->width ||
        (gles->height > 1u && stride > (SIZE_MAX / sizeof *pixels - gles->width) / (gles->height - 1u)))
    {
        SDL_SetError("invalid capture destination"); return failure(error, capacity, "capture");
    }
    GlesState state;
    if (!begin_boundary(gles, &state)) return failure(error, capacity, "capture boundary");
    int valid = gles->framebuffer != 0u;
    if (!valid) SDL_SetError("context reset requires a new frame before capture");
    const size_t bytes = gles->width * gles->height * 4u;
    if (valid && bytes > gles->readback_capacity)
    {
        uint8_t *readback = realloc(gles->readback, bytes);
        valid = readback != NULL;
        if (valid) { gles->readback = readback; gles->readback_capacity = bytes; }
        else SDL_SetError("capture allocation failed");
    }
    if (valid)
    {
        const GlesFunctions *gl = &gles->gl;
        gl->BindFramebuffer(GLES_READ_FRAMEBUFFER, gles->framebuffer);
        gl->BindBuffer(GLES_PIXEL_PACK_BUFFER, 0u);
        for (unsigned i = 0u; i < 4u; ++i) gl->PixelStorei(pixel_stores[i], i == 0u ? 1 : 0);
        gl->ReadPixels(0, 0, (GLsizei)gles->width, (GLsizei)gles->height, GL_RGBA, GL_UNSIGNED_BYTE, gles->readback);
        valid = gl_complete(gles, "capture pixels");
    }
    if (!valid) failure(error, capacity, "capture");
    if (!restore_state(gles, &state) && valid)
    {
        valid = 0;
        failure(error, capacity, "restore capture state");
    }
    if (!valid) return 0;
    /* Projection already places logical top in GL row zero, matching the
     * SDL texture wrapper. Captures use that identical orientation. */
    for (size_t y = 0u; y < gles->height; ++y)
        for (size_t x = 0u; x < gles->width; ++x)
        {
            const uint8_t *pixel = gles->readback + (y * gles->width + x) * 4u;
            pixels[y * stride + x] = ((uint32_t)pixel[3] << 24u) | ((uint32_t)pixel[0] << 16u) |
                                     ((uint32_t)pixel[1] << 8u) | pixel[2];
        }
    if (error != NULL && capacity != 0u) error[0] = '\0';
    return 1;
}

size_t portable_render_gles_geometry_count(const PortableRenderGles *gles)
{
    return gles ? gles->geometry_count : 0u;
}

void portable_render_gles_forget_meshes(PortableRenderGles *gles)
{
    if (gles == NULL) return;
    GlesState state;
    if (begin_boundary(gles, &state))
    {
        clear_meshes(gles, 1);
        restore_state(gles, &state);
    }
    else clear_meshes(gles, 0);
}

void portable_render_gles_invalidate(PortableRenderGles *gles)
{
    if (gles != NULL) gles->invalidated = 1;
}

void portable_render_gles_close(PortableRenderGles *gles)
{
    if (gles == NULL) return;
    GlesState state;
    if (begin_boundary(gles, &state))
    {
        if ((GLuint)state.texture == gles->color) state.texture = 0;
        release_names(gles, 1);
        restore_state(gles, &state);
    }
    else release_names(gles, 0);
    free(gles->readback);
    free(gles);
}
