#include "ui_gles_font.h"

#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#include <SDL3/SDL_opengles2.h>
#undef SDL_USE_BUILTIN_OPENGL_DEFINITIONS

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ES2 only: no VAOs, uniform blocks, integer samplers or new context. */
#define FONT_FUNCTIONS(X)                                                                          \
    X(const GLubyte *, GetString, (GLenum))                                                        \
    X(GLenum, GetError, (void))                                                                    \
    X(void, GetIntegerv, (GLenum, GLint *))                                                        \
    X(void, GetBooleanv, (GLenum, GLboolean *))                                                    \
    X(GLboolean, IsEnabled, (GLenum))                                                              \
    X(void, Enable, (GLenum))                                                                      \
    X(void, Disable, (GLenum))                                                                     \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                            \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                             \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                               \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                   \
    X(void, BlendEquationSeparate, (GLenum, GLenum))                                               \
    X(void, ActiveTexture, (GLenum))                                                               \
    X(void, BindTexture, (GLenum, GLuint))                                                         \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                \
    X(void, GenBuffers, (GLsizei, GLuint *))                                                       \
    X(void, DeleteBuffers, (GLsizei, const GLuint *))                                              \
    X(void, BindBuffer, (GLenum, GLuint))                                                          \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                \
    X(void, GetVertexAttribiv, (GLuint, GLenum, GLint *))                                          \
    X(void, GetVertexAttribPointerv, (GLuint, GLenum, void **))                                    \
    X(void, EnableVertexAttribArray, (GLuint))                                                     \
    X(void, DisableVertexAttribArray, (GLuint))                                                    \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))        \
    X(GLuint, CreateShader, (GLenum))                                                              \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                 \
    X(void, CompileShader, (GLuint))                                                               \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                              \
    X(void, GetShaderPrecisionFormat, (GLenum, GLenum, GLint *, GLint *))                          \
    X(void, DeleteShader, (GLuint))                                                                \
    X(GLuint, CreateProgram, (void))                                                               \
    X(void, AttachShader, (GLuint, GLuint))                                                        \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *))                                  \
    X(void, LinkProgram, (GLuint))                                                                 \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *))                                               \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                             \
    X(void, DeleteProgram, (GLuint))                                                               \
    X(void, UseProgram, (GLuint))                                                                  \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *))                                         \
    X(void, Uniform1i, (GLint, GLint))                                                             \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))

typedef struct
{
#define DECLARE(result, name, args) result(GL_APIENTRY *name) args;
    FONT_FUNCTIONS(DECLARE)
#undef DECLARE
    void(GL_APIENTRY *BindSampler)(GLuint, GLuint);
} FontGl;

typedef struct
{
    GLint enabled, size, type, normalized, stride, buffer;
    void *pointer;
} FontAttribute;

typedef struct
{
    GLint program, buffer, active_texture, texture, sampler;
    GLint viewport[4], scissor[4], blend[6];
    GLboolean enabled[5], color_mask[4];
    FontAttribute attributes[2];
} FontState;

struct UiGlesFont
{
    SDL_Renderer *renderer;
    SDL_GLContext context;
    FontGl gl;
    GLuint program, vertices;
    GLint projection, placement, texture_size, style, color, sampler;
    size_t vertex_count;
    int active, target_texture, swap_color, samplers, output_w, output_h;
    float sx, sy, x, y;
    FontState state;
};

static const GLenum capabilities[] = {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_CULL_FACE,
                                      GL_STENCIL_TEST};
static const char vertex_source[] =
    "#version 100\nprecision highp float;\n"
    "attribute vec2 position; attribute vec2 field;\n"
    "uniform vec4 projection; uniform vec4 placement; varying highp vec2 uv;\n"
    "void main(){ vec2 p=placement.xy+position*placement.zw;"
    "gl_Position=vec4(p*projection.xy+projection.zw,0.0,1.0); uv=field; }\n";
static const char fragment_source[] =
    "#version 100\nprecision highp float;\n"
    "uniform highp sampler2D sdf; uniform vec4 texture_size; uniform vec4 style;"
    "uniform vec4 color; varying highp vec2 uv;\n"
    "float sample_byte(vec2 p){"
    "return texture2D(sdf,(p+vec2(1.5))/texture_size.xy).r*255.0; }\n"
    "void main(){"
    "if(any(lessThan(uv,vec2(-0.5)))||any(greaterThan(uv,texture_size.zw+vec2(0.5)))) discard;"
    "vec2 p=uv-vec2(0.5), i=floor(p), f=fract(p);"
    "float v=(1.0-f.x)*(1.0-f.y)*sample_byte(i)"
    "+f.x*(1.0-f.y)*sample_byte(i+vec2(1,0))"
    "+(1.0-f.x)*f.y*sample_byte(i+vec2(0,1))"
    "+f.x*f.y*sample_byte(i+vec2(1,1));"
    "if(v<=0.0) discard;"
    "float distance=(v-128.0)*(12.0/128.0)*style.x;"
    "float coverage=clamp((distance+style.y)/style.z+0.5,0.0,1.0);"
    "float a=floor(color.a*coverage+0.5)/255.0;"
    "gl_FragColor=vec4(color.rgb*(a/255.0),a); }\n";

static int fail(char *error, size_t capacity, const char *stage)
{
    if (error != NULL && capacity != 0u)
        snprintf(error, capacity, "GLES UI font: %s: %s", stage, SDL_GetError());
    return 0;
}
static int complete(UiGlesFont *f, const char *stage)
{
    GLenum error = f->gl.GetError();
    if (error == GL_NO_ERROR)
        return 1;
    SDL_SetError("%s GL error 0x%x", stage, (unsigned)error);
    return 0;
}
static int load(UiGlesFont *f)
{
#define LOAD(result, name, args)                                                                   \
    do                                                                                             \
    {                                                                                              \
        SDL_FunctionPointer address = SDL_GL_GetProcAddress("gl" #name);                           \
        if (address == NULL)                                                                       \
            return SDL_SetError("missing GLES2 gl" #name), 0;                                      \
        _Static_assert(sizeof address == sizeof f->gl.name, "GL function pointer width");          \
        memcpy(&f->gl.name, &address, sizeof address);                                             \
    } while (0);
    FONT_FUNCTIONS(LOAD)
#undef LOAD
    const char *version = (const char *)f->gl.GetString(GL_VERSION);
    const char *es = version != NULL ? strstr(version, "OpenGL ES ") : NULL;
    f->samplers = es != NULL && es[10] >= '3' && es[10] <= '9';
    if (f->samplers)
    {
        SDL_FunctionPointer address = SDL_GL_GetProcAddress("glBindSampler");
        if (address == NULL)
            return SDL_SetError("missing ES3 glBindSampler"), 0;
        memcpy(&f->gl.BindSampler, &address, sizeof address);
    }
    GLint range[2], precision = 0;
    f->gl.GetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    if (precision < 16)
        return SDL_SetError("highp fragment precision required for SDF coverage"), 0;
    return complete(f, "load functions");
}
static void save(UiGlesFont *f, FontState *s)
{
    const FontGl *gl = &f->gl;
    gl->GetIntegerv(GL_CURRENT_PROGRAM, &s->program);
    gl->GetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->buffer);
    gl->GetIntegerv(GL_ACTIVE_TEXTURE, &s->active_texture);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->GetIntegerv(GL_TEXTURE_BINDING_2D, &s->texture);
    s->sampler = 0;
    if (f->samplers)
        gl->GetIntegerv(0x8919 /* GL_SAMPLER_BINDING, ES3 only */, &s->sampler);
    gl->GetIntegerv(GL_VIEWPORT, s->viewport);
    gl->GetIntegerv(GL_SCISSOR_BOX, s->scissor);
    const GLenum blend_fields[] = {GL_BLEND_SRC_RGB,      GL_BLEND_DST_RGB,
                                   GL_BLEND_SRC_ALPHA,    GL_BLEND_DST_ALPHA,
                                   GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA};
    for (size_t i = 0; i < 6u; ++i)
        gl->GetIntegerv(blend_fields[i], &s->blend[i]);
    gl->GetBooleanv(GL_COLOR_WRITEMASK, s->color_mask);
    for (size_t i = 0; i < 5u; ++i)
        s->enabled[i] = gl->IsEnabled(capabilities[i]);
    for (GLuint i = 0; i < 2u; ++i)
    {
        FontAttribute *a = &s->attributes[i];
        gl->GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a->enabled);
        gl->GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a->size);
        gl->GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a->type);
        gl->GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a->normalized);
        gl->GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a->stride);
        gl->GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a->buffer);
        gl->GetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a->pointer);
    }
}
static int restore(UiGlesFont *f, const FontState *s)
{
    const FontGl *gl = &f->gl;
    for (GLuint i = 0; i < 2u; ++i)
    {
        const FontAttribute *a = &s->attributes[i];
        gl->BindBuffer(GL_ARRAY_BUFFER, (GLuint)a->buffer);
        gl->VertexAttribPointer(i, a->size, (GLenum)a->type, (GLboolean)a->normalized, a->stride,
                                a->pointer);
        if (a->enabled)
            gl->EnableVertexAttribArray(i);
        else
            gl->DisableVertexAttribArray(i);
    }
    gl->BindBuffer(GL_ARRAY_BUFFER, (GLuint)s->buffer);
    gl->UseProgram((GLuint)s->program);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, (GLuint)s->texture);
    if (f->samplers)
        gl->BindSampler(0u, (GLuint)s->sampler);
    gl->ActiveTexture((GLenum)s->active_texture);
    gl->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    gl->Scissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
    gl->BlendFuncSeparate((GLenum)s->blend[0], (GLenum)s->blend[1], (GLenum)s->blend[2],
                          (GLenum)s->blend[3]);
    gl->BlendEquationSeparate((GLenum)s->blend[4], (GLenum)s->blend[5]);
    gl->ColorMask(s->color_mask[0], s->color_mask[1], s->color_mask[2], s->color_mask[3]);
    for (size_t i = 0; i < 5u; ++i)
    {
        if (s->enabled[i])
            gl->Enable(capabilities[i]);
        else
            gl->Disable(capabilities[i]);
    }
    const int valid = complete(f, "restore SDL state");
    /* SDL's public flush also invalidates its cached GL state. */
    const int flushed = SDL_FlushRenderer(f->renderer);
    return valid && flushed;
}
static GLuint shader(UiGlesFont *f, GLenum type, const char *source)
{
    GLuint name = f->gl.CreateShader(type);
    if (name == 0u)
        return 0u;
    f->gl.ShaderSource(name, 1, &source, NULL);
    f->gl.CompileShader(name);
    GLint valid = 0;
    f->gl.GetShaderiv(name, GL_COMPILE_STATUS, &valid);
    if (valid)
        return name;
    char log[512] = {0};
    f->gl.GetShaderInfoLog(name, (GLsizei)sizeof log, NULL, log);
    SDL_SetError("SDF shader compile: %s", log);
    f->gl.DeleteShader(name);
    return 0u;
}
static int resources(UiGlesFont *f)
{
    if (f->program != 0u)
        return 1;
    GLuint vs = shader(f, GL_VERTEX_SHADER, vertex_source);
    if (vs == 0u)
        return 0;
    GLuint fs = shader(f, GL_FRAGMENT_SHADER, fragment_source);
    if (fs == 0u)
    {
        f->gl.DeleteShader(vs);
        return 0;
    }
    GLuint program = f->gl.CreateProgram();
    if (program != 0u)
    {
        f->gl.AttachShader(program, vs);
        f->gl.AttachShader(program, fs);
        f->gl.BindAttribLocation(program, 0u, "position");
        f->gl.BindAttribLocation(program, 1u, "field");
        f->gl.LinkProgram(program);
    }
    f->gl.DeleteShader(vs);
    f->gl.DeleteShader(fs);
    GLint linked = 0;
    if (program != 0u)
        f->gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512] = {0};
        if (program != 0u)
        {
            f->gl.GetProgramInfoLog(program, (GLsizei)sizeof log, NULL, log);
            f->gl.DeleteProgram(program);
        }
        return SDL_SetError("SDF shader link: %s", log), 0;
    }
    const char *names[] = {"projection", "placement", "texture_size", "style", "color", "sdf"};
    GLint locations[6];
    for (size_t i = 0; i < 6u; ++i)
    {
        locations[i] = f->gl.GetUniformLocation(program, names[i]);
        if (locations[i] < 0)
        {
            f->gl.DeleteProgram(program);
            return SDL_SetError("SDF shader missing uniform %s", names[i]), 0;
        }
    }
    GLuint vertices = 0;
    f->gl.GenBuffers(1, &vertices);
    if (vertices == 0u || !complete(f, "create SDF resources"))
    {
        if (vertices != 0u)
            f->gl.DeleteBuffers(1, &vertices);
        f->gl.DeleteProgram(program);
        return 0;
    }
    f->program = program;
    f->vertices = vertices;
    f->projection = locations[0];
    f->placement = locations[1];
    f->texture_size = locations[2];
    f->style = locations[3];
    f->color = locations[4];
    f->sampler = locations[5];
    return 1;
}
UiGlesFont *ui_gles_font_create(SDL_Renderer *renderer)
{
    const char *name = renderer != NULL ? SDL_GetRendererName(renderer) : NULL;
    if (name == NULL || strcmp(name, "opengles2") != 0)
        return NULL;
    UiGlesFont *f = calloc(1, sizeof *f);
    if (f != NULL)
        f->renderer = renderer;
    return f;
}
int ui_gles_font_prepare(UiGlesFont *f, const SDL_Vertex *vertices, size_t count, char *error,
                         size_t capacity)
{
    if (f == NULL || (count != 0u && vertices == NULL) || count > 262144u)
        return SDL_SetError("invalid glyph vertices"), fail(error, capacity, "prepare");
    f->vertex_count = 0u;
    if (!SDL_FlushRenderer(f->renderer))
        return fail(error, capacity, "flush");
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    if (context == NULL)
        return SDL_SetError("missing SDL GLES context"), fail(error, capacity, "prepare");
    if (context != f->context)
    {
        /* Context generations have independent names; never delete old names
         * in a replacement context which may have reused them. */
        f->program = f->vertices = 0u;
        f->context = NULL;
        if (!load(f))
            return fail(error, capacity, "load");
        f->context = context;
    }
    FontState state;
    save(f, &state);
    int valid = complete(f, "capture state") && resources(f);
    if (valid)
    {
        f->gl.BindBuffer(GL_ARRAY_BUFFER, f->vertices);
        f->gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof *vertices), vertices,
                         GL_STREAM_DRAW);
        valid = complete(f, "prepare glyph vertices");
    }
    const int restored = restore(f, &state);
    if (!valid || !restored)
        return fail(error, capacity, "prepare resources");
    f->vertex_count = count;
    return 1;
}
int ui_gles_font_begin(UiGlesFont *f, const SDL_FRect *target, int width, int height, char *error,
                       size_t capacity)
{
    if (f == NULL || f->active || f->program == 0u || width <= 0 || height <= 0 ||
        SDL_GL_GetCurrentContext() != f->context)
        return SDL_SetError("unprepared glyph context"), fail(error, capacity, "begin");
    if (!SDL_FlushRenderer(f->renderer) ||
        !SDL_GetCurrentRenderOutputSize(f->renderer, &f->output_w, &f->output_h))
        return fail(error, capacity, "flush target");
    save(f, &f->state);
    f->active = 1;
    SDL_Texture *target_texture = SDL_GetRenderTarget(f->renderer);
    f->target_texture = target_texture != NULL;
    const SDL_PixelFormat format =
        target_texture != NULL
            ? (SDL_PixelFormat)SDL_GetNumberProperty(SDL_GetTextureProperties(target_texture),
                                                     SDL_PROP_TEXTURE_FORMAT_NUMBER,
                                                     SDL_PIXELFORMAT_UNKNOWN)
            : SDL_PIXELFORMAT_UNKNOWN;
    /* SDL's GLES2 targets preserve their byte layout in the RGBA attachment;
     * its own geometry swaps R/B for BGRA/BGRX targets before fragment output. */
    f->swap_color = format == SDL_PIXELFORMAT_BGRA32 || format == SDL_PIXELFORMAT_BGRX32;
    f->sx = target->w / (float)width;
    f->sy = target->h / (float)height;
    f->x = target->x;
    f->y = target->y;
    const FontGl *gl = &f->gl;
    gl->Viewport(0, 0, f->output_w, f->output_h);
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->Disable(GL_STENCIL_TEST);
    gl->Enable(GL_BLEND);
    gl->Enable(GL_SCISSOR_TEST);
    gl->BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl->BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->UseProgram(f->program);
    gl->Uniform4f(f->projection, 2.0f / (float)f->output_w,
                  (f->target_texture ? 2.0f : -2.0f) / (float)f->output_h, -1.0f,
                  f->target_texture ? -1.0f : 1.0f);
    gl->Uniform4f(f->placement, f->x, f->y, f->sx, f->sy);
    gl->Uniform1i(f->sampler, 0);
    gl->ActiveTexture(GL_TEXTURE0);
    if (f->samplers)
        gl->BindSampler(0u, 0u);
    gl->BindBuffer(GL_ARRAY_BUFFER, f->vertices);
    gl->EnableVertexAttribArray(0u);
    gl->EnableVertexAttribArray(1u);
    gl->VertexAttribPointer(0u, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(SDL_Vertex),
                            (const void *)offsetof(SDL_Vertex, position));
    gl->VertexAttribPointer(1u, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(SDL_Vertex),
                            (const void *)offsetof(SDL_Vertex, tex_coord));
    if (complete(f, "begin glyph pass"))
        return 1;
    (void)ui_gles_font_end(f);
    return fail(error, capacity, "begin");
}
int ui_gles_font_draw(UiGlesFont *f, SDL_Texture *texture, size_t first, size_t count,
                      const SDL_Rect *clip, int width, int height, float scale, float outline,
                      float softness, uint32_t color, float alpha)
{
    if (f == NULL || !f->active || first > f->vertex_count || count > f->vertex_count - first)
        return 0;
    const GLuint name = (GLuint)SDL_GetNumberProperty(SDL_GetTextureProperties(texture),
                                                      SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
    if (name == 0u)
        return SDL_SetError("missing GLES SDF texture"), 0;
    int left = (int)floorf(f->x + (float)clip->x * f->sx);
    int top = (int)floorf(f->y + (float)clip->y * f->sy);
    int right = (int)ceilf(f->x + ((float)clip->x + (float)clip->w) * f->sx);
    int bottom = (int)ceilf(f->y + ((float)clip->y + (float)clip->h) * f->sy);
    const FontGl *gl = &f->gl;
    gl->Scissor(left, f->target_texture ? top : f->output_h - bottom, right - left, bottom - top);
    gl->BindTexture(GL_TEXTURE_2D, name);
    /* These cache-owned textures are sampled only by this pass. Manual
     * bilinear interpolation matches the byte SDF's zero border exactly. */
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->Uniform4f(f->texture_size, (float)width + 2.0f, (float)height + 2.0f, (float)width,
                  (float)height);
    gl->Uniform4f(f->style, scale, outline, softness, 0);
    const float red = (float)((color >> 16) & 255u), blue = (float)(color & 255u);
    gl->Uniform4f(f->color, f->swap_color ? blue : red, (float)((color >> 8) & 255u),
                  f->swap_color ? red : blue, (float)(color >> 24) * fmaxf(0, fminf(1, alpha)));
    gl->DrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
    return complete(f, "draw glyph");
}
int ui_gles_font_end(UiGlesFont *f)
{
    if (f == NULL || !f->active)
        return 1;
    f->active = 0;
    return restore(f, &f->state);
}
void ui_gles_font_forget(UiGlesFont *f)
{
    if (f == NULL)
        return;
    (void)ui_gles_font_end(f);
    if (f->context != NULL && f->context == SDL_GL_GetCurrentContext())
    {
        if (f->program != 0u)
            f->gl.DeleteProgram(f->program);
        if (f->vertices != 0u)
            f->gl.DeleteBuffers(1, &f->vertices);
    }
    f->program = f->vertices = 0u;
    f->vertex_count = 0u;
}
void ui_gles_font_close(UiGlesFont *f)
{
    if (f == NULL)
        return;
    ui_gles_font_forget(f);
    free(f);
}
