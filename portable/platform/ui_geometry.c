#include "ui_geometry.h"
#include "ui_gles_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These budgets include queued geometry and persistent source keys. Exhaustion
 * rejects the whole prepared frame; it never changes the canvas reference. */
enum
{
    GEOMETRY_COMMANDS = 8192,
    GEOMETRY_VERTICES = 262144,
    GEOMETRY_IMAGES = 1024,
    RESOLVED_SLOTS = 8192,
    GEOMETRY_CACHE_BYTES = 16 * 1024 * 1024,
    CONTOUR_POINTS = 512,
    RAMP_WIDTH = 256
};
static const float pi = 3.14159265358979323846f;

typedef struct
{
    SDL_Texture *texture;
    unsigned char *key;
    size_t key_size, owned;
    uint64_t hash, used;
} GeometryImage;

/* Recorder payloads are immutable throughout one prepare. This hint table
 * avoids rebuilding full persistent keys for repeated commands in that frame;
 * it is cleared before the next list, so the first source still hashes and
 * compares all bytes even when an arena address is reused. */
typedef struct
{
    const void *source;
    uint32_t parameters[8];
    SDL_Texture *texture;
} GeometryResolved;

typedef struct
{
    SDL_Texture *texture;
    SDL_Rect clip;
    SDL_BlendMode blend;
    SDL_ScaleMode sampling;
    size_t first, count;
    int glyph_width, glyph_height;
    float glyph_scale, glyph_outline, glyph_softness, glyph_alpha;
    uint32_t glyph_color;
} GeometryDraw;

typedef struct
{
    SDL_Texture *texture;
    float tx, ty, origin, low, span, alpha;
    SDL_FColor color;
} GeometryPaint;

typedef struct
{
    UiDrawKind kind;
    float cx, cy, hw, hh, radius, skew, feather, stroke;
    const UiPoint *points;
    int point_count;
    float orientation;
    float edges[160][3];
    unsigned edge_count;
} GeometryShape;

struct UiGeometry
{
    SDL_Renderer *renderer;
    UiGlesFont *font;
    SDL_Vertex *vertices, *transformed;
    GeometryDraw *draws;
    size_t vertex_count, vertex_capacity, draw_count, cache_bytes, uploaded;
    uint64_t frame;
    int width, height, prepared;
    unsigned image_count;
    int profile_enabled;
    uint64_t profile_prepares, profile_draws, profile_prepare_ticks, profile_draw_ticks;
    uint64_t profile_shape_ticks, profile_glyph_ticks, profile_image_ticks;
    uint64_t profile_vertices, profile_batches, profile_resolved_hits, profile_cache_hits,
        profile_uploads, profile_upload_bytes;
    GeometryResolved resolved[RESOLVED_SLOTS];
    GeometryImage images[GEOMETRY_IMAGES];
};

static float clamp_unit(float value)
{
    return fmaxf(0.0f, fminf(1.0f, value));
}
static unsigned byte_value(float value)
{
    return (unsigned)fmaxf(0.0f, fminf(255.0f, floorf(value + .5f)));
}
static uint32_t premultiply(uint32_t color)
{
    const unsigned a = color >> 24;
    return (a << 24) | (((((color >> 16) & 255u) * a + 127u) / 255u) << 16) |
           (((((color >> 8) & 255u) * a + 127u) / 255u) << 8) | ((color & 255u) * a + 127u) / 255u;
}
static SDL_FColor vertex_color(uint32_t color, float alpha)
{
    const float a = (float)(color >> 24) / 255.0f * clamp_unit(alpha);
    return (SDL_FColor){(float)((color >> 16) & 255u) / 255.0f * a,
                        (float)((color >> 8) & 255u) / 255.0f * a,
                        (float)(color & 255u) / 255.0f * a, a};
}
static int failure(char *error, size_t capacity, const char *action)
{
    if (error != NULL && capacity != 0u)
        snprintf(error, capacity, "Geometry UI: %s: %s", action, SDL_GetError());
    return 0;
}
static uint64_t hash_bytes(const void *data, size_t size)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}
static void retire(UiGeometry *g, GeometryImage *image)
{
    if (image->texture != NULL)
        SDL_DestroyTexture(image->texture);
    free(image->key);
    g->cache_bytes -= image->owned;
    *image = (GeometryImage){0};
}
static SDL_Texture *cached(UiGeometry *g, const void *key, size_t size, uint64_t hash)
{
    for (unsigned i = 0; i < g->image_count; ++i)
    {
        GeometryImage *image = &g->images[i];
        if (image->texture != NULL && image->hash == hash && image->key_size == size &&
            memcmp(image->key, key, size) == 0)
        {
            image->used = g->frame;
            if (g->profile_enabled)
                ++g->profile_cache_hits;
            return image->texture;
        }
    }
    return NULL;
}
static GeometryResolved *resolved_slot(UiGeometry *g, const void *source,
                                       const uint32_t parameters[8])
{
    const uint64_t hash =
        hash_bytes(parameters, 8u * sizeof *parameters) ^ (uint64_t)(uintptr_t)source;
    unsigned index = (unsigned)((hash ^ (hash >> 32)) & (RESOLVED_SLOTS - 1u));
    for (unsigned i = 0; i < RESOLVED_SLOTS; ++i)
    {
        GeometryResolved *slot = &g->resolved[index];
        if (slot->source == NULL ||
            (slot->source == source &&
             memcmp(slot->parameters, parameters, sizeof slot->parameters) == 0))
            return slot;
        index = (index + 1u) & (RESOLVED_SLOTS - 1u);
    }
    SDL_SetError("frame exhausts resolved texture hints");
    return NULL;
}
static void remember_resolved(GeometryResolved *slot, const void *source,
                              const uint32_t parameters[8], SDL_Texture *texture)
{
    slot->source = source;
    memcpy(slot->parameters, parameters, sizeof slot->parameters);
    slot->texture = texture;
}
/* Takes the key only on success. No texture used by this frame can be evicted. */
static SDL_Texture *upload(UiGeometry *g, unsigned char *key, size_t key_size, uint64_t hash,
                           const uint32_t *pixels, int width, int height)
{
    const size_t bytes = (size_t)width * (size_t)height * 4u;
    if (key_size > GEOMETRY_CACHE_BYTES || bytes > GEOMETRY_CACHE_BYTES - key_size)
    {
        SDL_SetError("texture exceeds geometry cache budget");
        return NULL;
    }
    const size_t owned = key_size + bytes;
    GeometryImage *slot = NULL;
    for (;;)
    {
        slot = NULL;
        GeometryImage *oldest = NULL;
        for (unsigned i = 0; i < g->image_count; ++i)
        {
            GeometryImage *image = &g->images[i];
            if (image->texture == NULL)
            {
                if (slot == NULL)
                    slot = image;
            }
            else if (image->used != g->frame && (oldest == NULL || image->used < oldest->used))
                oldest = image;
        }
        if (slot == NULL && g->image_count < GEOMETRY_IMAGES)
            slot = &g->images[g->image_count];
        if (slot != NULL && g->cache_bytes <= GEOMETRY_CACHE_BYTES - owned)
            break;
        if (oldest == NULL)
        {
            SDL_SetError("frame exhausts geometry texture cache");
            return NULL;
        }
        retire(g, oldest);
    }
    SDL_Texture *texture = SDL_CreateTexture(g->renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture == NULL)
        return NULL;
    if (!SDL_UpdateTexture(texture, NULL, pixels, width * 4) ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED))
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    if (slot == &g->images[g->image_count])
        ++g->image_count;
    *slot = (GeometryImage){texture, key, key_size, owned, hash, g->frame};
    g->cache_bytes += owned;
    g->uploaded += bytes;
    if (g->profile_enabled)
    {
        ++g->profile_uploads;
        g->profile_upload_bytes += bytes;
    }
    return texture;
}
static int reserve_vertices(UiGeometry *g, size_t count)
{
    if (count > GEOMETRY_VERTICES - g->vertex_count)
    {
        SDL_SetError("frame exhausts geometry vertex budget");
        return 0;
    }
    const size_t needed = g->vertex_count + count;
    if (needed <= g->vertex_capacity)
        return 1;
    size_t capacity = g->vertex_capacity != 0 ? g->vertex_capacity : 4096u;
    while (capacity < needed)
        capacity *= 2u;
    if (capacity > GEOMETRY_VERTICES)
        capacity = GEOMETRY_VERTICES;
    /* Both buffers must grow before publication, keeping allocation failure
     * from invalidating the previous owned storage. */
    SDL_Vertex *vertices = malloc(capacity * sizeof *vertices);
    SDL_Vertex *transformed = malloc(capacity * sizeof *transformed);
    if (vertices == NULL || transformed == NULL)
    {
        free(vertices);
        free(transformed);
        return 0;
    }
    if (g->vertex_count != 0u)
        memcpy(vertices, g->vertices, g->vertex_count * sizeof *vertices);
    free(g->vertices);
    free(g->transformed);
    g->vertices = vertices;
    g->transformed = transformed;
    g->vertex_capacity = capacity;
    return 1;
}
static int triangle(UiGeometry *g, SDL_Vertex a, SDL_Vertex b, SDL_Vertex c)
{
    if (!reserve_vertices(g, 3u))
        return 0;
    g->vertices[g->vertex_count++] = a;
    g->vertices[g->vertex_count++] = b;
    g->vertices[g->vertex_count++] = c;
    return 1;
}
static int quadrilateral(UiGeometry *g, SDL_Vertex a, SDL_Vertex b, SDL_Vertex c, SDL_Vertex d)
{
    return triangle(g, a, b, c) && triangle(g, a, c, d);
}
static GeometryDraw *begin_draw(UiGeometry *g, const UiDrawCommand *command, SDL_Texture *texture,
                                SDL_BlendMode blend, SDL_ScaleMode sampling)
{
    if (g->draw_count == GEOMETRY_COMMANDS)
    {
        SDL_SetError("geometry draw budget exceeded");
        return NULL;
    }
    GeometryDraw *draw = &g->draws[g->draw_count++];
    const int left = command->clip_x0 < 0          ? 0
                     : command->clip_x0 > g->width ? g->width
                                                   : command->clip_x0;
    const int top = command->clip_y0 < 0           ? 0
                    : command->clip_y0 > g->height ? g->height
                                                   : command->clip_y0;
    const int right = command->clip_x1 < left       ? left
                      : command->clip_x1 > g->width ? g->width
                                                    : command->clip_x1;
    const int bottom = command->clip_y1 < top         ? top
                       : command->clip_y1 > g->height ? g->height
                                                      : command->clip_y1;
    *draw = (GeometryDraw){.texture = texture,
                           .clip = {left, top, right - left, bottom - top},
                           .blend = blend,
                           .sampling = sampling,
                           .first = g->vertex_count};
    return draw;
}
static void end_draw(UiGeometry *g, GeometryDraw *draw)
{
    draw->count = g->vertex_count - draw->first;
}
static SDL_Vertex painted(const GeometryPaint *paint, UiPoint p, float coverage)
{
    SDL_FColor color = paint->color;
    const float weight = clamp_unit(coverage) * paint->alpha;
    color.r *= weight;
    color.g *= weight;
    color.b *= weight;
    color.a *= weight;
    float t = paint->span > 0
                  ? (p.x * paint->tx + p.y * paint->ty - paint->origin - paint->low) / paint->span
                  : 0;
    return (SDL_Vertex){{p.x, p.y}, color, {(t * 255.0f + .5f) / 256.0f, .5f}};
}
static uint32_t mix_paint(const UiPaint *paint, float t)
{
    if (paint->count == 1u || t <= paint->positions[0])
        return paint->colors[0];
    for (unsigned i = 1; i < paint->count; ++i)
        if (t <= paint->positions[i])
        {
            const float span = paint->positions[i] - paint->positions[i - 1u];
            return ui_mix(paint->colors[i - 1u], paint->colors[i],
                          span > 1e-6f ? (t - paint->positions[i - 1u]) / span : 1.0f);
        }
    return paint->colors[paint->count - 1u];
}
static int prepare_paint(UiGeometry *g, const UiPaint *source, float alpha, GeometryPaint *paint)
{
    *paint = (GeometryPaint){.alpha = clamp_unit(alpha), .color = {1, 1, 1, 1}};
    if (source->count == 0u || source->count > 4u)
        return 0;
    const float axis[] = {source->x0, source->y0, source->x1, source->y1};
    for (unsigned i = 0; i < 4u; ++i)
        if (!isfinite(axis[i]) || fabsf(axis[i]) > 1e6f)
            return 0;
    for (unsigned i = 0; i < source->count; ++i)
        if (!isfinite(source->positions[i]) || fabsf(source->positions[i]) > 1e6f ||
            (i != 0u && source->positions[i] < source->positions[i - 1u]))
            return 0;
    const float dx = source->x1 - source->x0, dy = source->y1 - source->y0;
    const float squared = dx * dx + dy * dy;
    if (source->count == 1u || squared <= 1e-6f)
    {
        paint->color = vertex_color(source->colors[0], 1.0f);
        return 1;
    }
    paint->tx = dx / squared;
    paint->ty = dy / squared;
    paint->origin = source->x0 * paint->tx + source->y0 * paint->ty;
    paint->low = fminf(0, source->positions[0]);
    paint->span = fmaxf(1, source->positions[source->count - 1u]) - paint->low;
    uint32_t key_words[10] = {1u, source->count};
    memcpy(key_words + 2, source->colors, 4u * sizeof(uint32_t));
    memcpy(key_words + 6, source->positions, 4u * sizeof(float));
    const uint64_t hash = hash_bytes(key_words, sizeof key_words);
    paint->texture = cached(g, key_words, sizeof key_words, hash);
    if (paint->texture != NULL)
        return 1;
    unsigned char *key = malloc(sizeof key_words);
    if (key == NULL)
        return 0;
    memcpy(key, key_words, sizeof key_words);
    uint32_t pixels[RAMP_WIDTH];
    for (unsigned i = 0; i < RAMP_WIDTH; ++i)
        pixels[i] = premultiply(mix_paint(source, paint->low + paint->span * (float)i / 255.0f));
    paint->texture = upload(g, key, sizeof key_words, hash, pixels, RAMP_WIDTH, 1);
    if (paint->texture == NULL)
        free(key);
    return paint->texture != NULL;
}

static float shape_distance(const GeometryShape *shape, UiPoint point)
{
    float x = point.x - shape->cx, y = point.y - shape->cy;
    if (shape->kind == UI_DRAW_CONVEX)
    {
        float distance = -1e30f;
        for (unsigned i = 0; i < shape->edge_count; ++i)
            distance = fmaxf(distance, shape->edges[i][0] * point.x + shape->edges[i][1] * point.y -
                                           shape->edges[i][2]);
        return distance;
    }
    if (shape->kind == UI_DRAW_ELLIPSE)
    {
        const float k0 = hypotf(x / shape->hw, y / shape->hh);
        const float k1 = hypotf(x / (shape->hw * shape->hw), y / (shape->hh * shape->hh));
        return k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -fminf(shape->hw, shape->hh);
    }
    if (shape->kind == UI_DRAW_STROKE_CIRCLE)
        return hypotf(x, y) - shape->hw;
    x += shape->skew * y;
    const float qx = fabsf(x) - shape->hw + shape->radius;
    const float qy = fabsf(y) - shape->hh + shape->radius;
    return hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - shape->radius;
}
static float shape_coverage(const GeometryShape *shape, UiPoint point)
{
    const float d = shape_distance(shape, point);
    if (shape->kind == UI_DRAW_STROKE_CIRCLE)
        return clamp_unit(.5f - fabsf(d + shape->stroke * .5f) + shape->stroke * .5f);
    if (shape->kind == UI_DRAW_STROKE_ROUND_RECT)
        return clamp_unit(.5f - d) * clamp_unit(.5f + d + shape->stroke);
    return clamp_unit(.5f - d / shape->feather);
}
/* Convex implicit contours share radial samples. Collapsed inner contours
 * become center vertices rather than overlapping fans or inverted AA rings. */
static UiPoint contour_point(const GeometryShape *shape, float angle, float distance)
{
    const UiPoint center = {shape->cx, shape->cy};
    if (shape_distance(shape, center) >= distance)
        return center;
    const float dx = cosf(angle), dy = sinf(angle);
    float radius;
    if (shape->kind == UI_DRAW_CONVEX)
    {
        radius = 1e30f;
        for (unsigned i = 0; i < shape->edge_count; ++i)
        {
            const float *edge = shape->edges[i];
            const float denominator = edge[0] * dx + edge[1] * dy;
            if (denominator > 1e-8f)
                radius =
                    fminf(radius, (distance + edge[2] - edge[0] * center.x - edge[1] * center.y) /
                                      denominator);
        }
    }
    else if (shape->kind == UI_DRAW_STROKE_CIRCLE)
        radius = shape->hw + distance;
    else if (shape->kind == UI_DRAW_ELLIPSE)
    {
        const float x = dx / shape->hw, y = dy / shape->hh;
        const float k = hypotf(x, y), l = hypotf(x / shape->hw, y / shape->hh);
        radius = (distance * l + k) / (k * k);
    }
    else
    {
        const float x = fabsf(dx + shape->skew * dy), y = fabsf(dy);
        const float hw = shape->hw + distance, hh = shape->hh + distance;
        const float rounded = shape->radius + distance;
        radius = fminf(x > 1e-8f ? hw / x : 1e30f, y > 1e-8f ? hh / y : 1e30f);
        const float core_x = shape->hw - shape->radius, core_y = shape->hh - shape->radius;
        if (rounded > 0 && radius * x > core_x && radius * y > core_y)
        {
            const float a = x * x + y * y, projection = x * core_x + y * core_y;
            const float discriminant = projection * projection -
                                       a * (core_x * core_x + core_y * core_y - rounded * rounded);
            radius = (projection + sqrtf(fmaxf(discriminant, 0))) / a;
        }
    }
    radius = fmaxf(radius, 0);
    return (UiPoint){center.x + dx * radius, center.y + dy * radius};
}
static float chord_error(UiPoint a, UiPoint b, UiPoint middle)
{
    const float dx = b.x - a.x, dy = b.y - a.y, squared = dx * dx + dy * dy;
    const float t = squared > 1e-12f
                        ? clamp_unit(((middle.x - a.x) * dx + (middle.y - a.y) * dy) / squared)
                        : 0;
    return hypotf(middle.x - (a.x + t * dx), middle.y - (a.y + t * dy));
}
static int add_angles(const GeometryShape *shape, const float *levels, unsigned level_count,
                      float a, float b, unsigned depth, float *angles, unsigned *count)
{
    const float middle = (a + b) * .5f;
    float error = 0;
    for (unsigned i = 0; i < level_count; ++i)
        error = fmaxf(error, chord_error(contour_point(shape, a, levels[i]),
                                         contour_point(shape, b, levels[i]),
                                         contour_point(shape, middle, levels[i])));
    if (error > .20f)
    {
        if (depth == 10u || *count >= CONTOUR_POINTS - 2u)
        {
            SDL_SetError(
                "adaptive contour kind=%d center=(%.6g,%.6g) half=(%.6g,%.6g) "
                "radius=%.6g skew=%.6g feather=%.6g stroke=%.6g depth=%u points=%u error=%.6g",
                (int)shape->kind, (double)shape->cx, (double)shape->cy, (double)shape->hw,
                (double)shape->hh, (double)shape->radius, (double)shape->skew,
                (double)shape->feather, (double)shape->stroke, depth, *count, (double)error);
            return 0;
        }
        return add_angles(shape, levels, level_count, a, middle, depth + 1u, angles, count) &&
               add_angles(shape, levels, level_count, middle, b, depth + 1u, angles, count);
    }
    if (*count == CONTOUR_POINTS)
        return 0;
    angles[(*count)++] = a;
    return 1;
}
/* A sharp convex corner is a radial discontinuity in the derivative. Put
 * those exact plane intersections into the sampling partition before testing
 * chord error; a wide footer must not spend its depth budget finding a corner. */
static int insert_angle(float *seeds, unsigned *count, float angle)
{
    if (!isfinite(angle))
        return 0;
    if (angle < 0)
        angle += 2.0f * pi;
    if (angle >= 2.0f * pi)
        angle = 0;
    unsigned at = 0;
    while (at < *count && seeds[at] < angle)
        ++at;
    if ((at < *count && fabsf(seeds[at] - angle) < 1e-6f) ||
        (at > 0u && fabsf(seeds[at - 1u] - angle) < 1e-6f))
        return 1;
    if (*count == CONTOUR_POINTS)
    {
        SDL_SetError("convex contour seed budget exceeded");
        return 0;
    }
    memmove(seeds + at + 1u, seeds + at, (*count - at) * sizeof *seeds);
    seeds[at] = angle;
    ++*count;
    return 1;
}
static int contour_seeds(const GeometryShape *shape, const float *levels, unsigned level_count,
                         float *seeds, unsigned *count)
{
    *count = 0;
    for (unsigned i = 0; i < 16u; ++i)
        if (!insert_angle(seeds, count, (float)i * pi / 8.0f))
            return 0;
    if (shape->kind != UI_DRAW_CONVEX)
        return 1;
    for (unsigned level = 0; level < level_count; ++level)
    {
        if (shape_distance(shape, (UiPoint){shape->cx, shape->cy}) >= levels[level])
            continue;
        for (unsigned i = 0; i < shape->edge_count; ++i)
        {
            const float *a = shape->edges[i], *b = shape->edges[(i + 1u) % shape->edge_count];
            const float determinant = a[0] * b[1] - a[1] * b[0];
            if (!isfinite(determinant))
                return 0;
            if (fabsf(determinant) < 1e-8f)
                continue;
            const float ca = levels[level] + a[2] - a[0] * shape->cx - a[1] * shape->cy;
            const float cb = levels[level] + b[2] - b[0] * shape->cx - b[1] * shape->cy;
            const float x = (ca * b[1] - a[1] * cb) / determinant;
            const float y = (a[0] * cb - ca * b[0]) / determinant;
            if (!isfinite(x) || !isfinite(y))
            {
                SDL_SetError("nonfinite convex contour intersection");
                return 0;
            }
            int inside = 1;
            for (unsigned edge = 0; edge < shape->edge_count; ++edge)
            {
                const float *n = shape->edges[edge];
                const float limit = levels[level] + n[2] - n[0] * shape->cx - n[1] * shape->cy;
                if (n[0] * x + n[1] * y > limit + .005f)
                {
                    inside = 0;
                    break;
                }
            }
            if (inside && !insert_angle(seeds, count, atan2f(y, x)))
                return 0;
        }
    }
    return 1;
}
static SDL_Vertex shape_vertex(const GeometryShape *shape, const GeometryPaint *paint,
                               UiPoint point, const UiDrawCommand *image)
{
    float coverage = shape_coverage(shape, point);
    SDL_Vertex vertex = painted(paint, point, coverage);
    if (image != NULL)
    {
        const float x = point.x - image->data.image.x, y = point.y - image->data.image.y;
        const float w = image->data.image.w, h = image->data.image.h;
        const float edge = clamp_unit(x + .5f) * clamp_unit(w - x + .5f) * clamp_unit(y + .5f) *
                           clamp_unit(h - y + .5f);
        if (image->data.image.radius <= 0)
            coverage = edge;
        else
            coverage *= edge;
        vertex.color = (SDL_FColor){coverage * paint->alpha, coverage * paint->alpha,
                                    coverage * paint->alpha, coverage * paint->alpha};
        const UiImage *source = image->data.image.source;
        vertex.tex_coord = (SDL_FPoint){
            (image->data.image.sx + x / w * image->data.image.sw) / (float)source->width,
            (image->data.image.sy + y / h * image->data.image.sh) / (float)source->height};
    }
    return vertex;
}
static int shape_mesh(UiGeometry *g, const GeometryShape *shape, const GeometryPaint *paint,
                      const UiDrawCommand *image)
{
    float levels[10] = {-shape->feather * .5f, 0, shape->feather * .5f};
    unsigned level_count = 3u;
    /* Rounded image edges multiply rectangular and rounded coverage. Extra
     * AA bands keep that quadratic response within about one alpha byte. */
    if (image != NULL)
    {
        level_count = 9u;
        for (unsigned i = 0; i < level_count; ++i)
            levels[i] = -.5f + (float)i * .125f;
    }
    if (shape->kind == UI_DRAW_STROKE_CIRCLE || shape->kind == UI_DRAW_STROKE_ROUND_RECT)
    {
        levels[0] = -shape->stroke - .5f;
        levels[1] = -shape->stroke + .5f;
        levels[2] = -.5f;
        levels[3] = .5f;
        levels[4] = -shape->stroke * .5f;
        level_count = 5u;
    }
    for (unsigned i = 1; i < level_count; ++i)
    {
        const float value = levels[i];
        unsigned j = i;
        while (j != 0u && levels[j - 1u] > value)
        {
            levels[j] = levels[j - 1u];
            --j;
        }
        levels[j] = value;
    }
    float seeds[CONTOUR_POINTS], angles[CONTOUR_POINTS];
    unsigned seed_count = 0, count = 0;
    if (!contour_seeds(shape, levels, level_count, seeds, &seed_count))
        return 0;
    for (unsigned i = 0; i < seed_count; ++i)
        if (!add_angles(shape, levels, level_count, seeds[i],
                        i + 1u < seed_count ? seeds[i + 1u] : 2.0f * pi, 0, angles, &count))
            return 0;
    const UiPoint center = {shape->cx, shape->cy};
    const SDL_Vertex center_vertex = shape_vertex(shape, paint, center, image);
    for (unsigned i = 0; i < count; ++i)
    {
        const float a = angles[i], b = angles[(i + 1u) % count];
        SDL_Vertex previous_a =
            shape_vertex(shape, paint, contour_point(shape, a, levels[0]), image);
        SDL_Vertex previous_b =
            shape_vertex(shape, paint, contour_point(shape, b, levels[0]), image);
        if (!triangle(g, center_vertex, previous_a, previous_b))
            return 0;
        for (unsigned level = 1; level < level_count; ++level)
        {
            SDL_Vertex next_a =
                shape_vertex(shape, paint, contour_point(shape, a, levels[level]), image);
            SDL_Vertex next_b =
                shape_vertex(shape, paint, contour_point(shape, b, levels[level]), image);
            if (!quadrilateral(g, previous_a, next_a, next_b, previous_b))
                return 0;
            previous_a = next_a;
            previous_b = next_b;
        }
    }
    return 1;
}

static SDL_Texture *prepare_image(UiGeometry *g, const UiDrawCommand *command)
{
    const UiImage *image = command->data.image.source;
    if (image == NULL || image->pixels == NULL || image->width <= 0 || image->height <= 0 ||
        image->width > 4096 || image->height > 4096)
        return NULL;
    const size_t pixels = (size_t)image->width * (size_t)image->height;
    if (pixels > (GEOMETRY_CACHE_BYTES - 20u) / 8u)
        return NULL;
    const size_t bytes = pixels * 4u, key_size = bytes + 20u;
    const uint32_t parameters[8] = {2u, (uint32_t)image->width, (uint32_t)image->height,
                                    command->data.image.flags &
                                        (UI_IMAGE_GRAY | UI_IMAGE_SILHOUETTE),
                                    command->data.image.tint};
    GeometryResolved *resolved = resolved_slot(g, image->pixels, parameters);
    if (resolved == NULL)
        return NULL;
    if (resolved->texture != NULL)
    {
        if (g->profile_enabled)
            ++g->profile_resolved_hits;
        return resolved->texture;
    }
    unsigned char *key = malloc(key_size);
    if (key == NULL)
        return NULL;
    const uint32_t words[5] = {parameters[0], parameters[1], parameters[2], parameters[3],
                               parameters[4]};
    memcpy(key, words, sizeof words);
    memcpy(key + sizeof words, image->pixels, bytes);
    const uint64_t hash = hash_bytes(key, key_size);
    SDL_Texture *texture = cached(g, key, key_size, hash);
    if (texture != NULL)
    {
        remember_resolved(resolved, image->pixels, parameters, texture);
        free(key);
        return texture;
    }
    uint32_t *converted = malloc(bytes);
    if (converted == NULL)
    {
        free(key);
        return NULL;
    }
    const uint32_t tint = command->data.image.tint;
    for (size_t i = 0; i < pixels; ++i)
    {
        const uint32_t color = image->pixels[i];
        float r = (float)((color >> 16) & 255u), green = (float)((color >> 8) & 255u),
              b = (float)(color & 255u);
        if (command->data.image.flags & UI_IMAGE_SILHOUETTE)
        {
            r = (float)((tint >> 16) & 255u);
            green = (float)((tint >> 8) & 255u);
            b = (float)(tint & 255u);
        }
        else
        {
            if (command->data.image.flags & UI_IMAGE_GRAY)
                r = green = b = (r * .30f + green * .59f + b * .11f) * .55f + 30.0f;
            r *= (float)((tint >> 16) & 255u) / 255.0f;
            green *= (float)((tint >> 8) & 255u) / 255.0f;
            b *= (float)(tint & 255u) / 255.0f;
        }
        const unsigned alpha = ((color >> 24) * (tint >> 24) + 127u) / 255u;
        converted[i] =
            premultiply((alpha << 24) | ((uint32_t)r << 16) | ((uint32_t)green << 8) | (uint32_t)b);
    }
    texture = upload(g, key, key_size, hash, converted, image->width, image->height);
    if (texture == NULL)
        free(key);
    free(converted);
    if (texture != NULL)
        remember_resolved(resolved, image->pixels, parameters, texture);
    return texture;
}
static float glyph_sample(const UiDrawCommand *command, float u, float v)
{
    const int width = command->data.glyph.width, height = command->data.glyph.height;
    if (!isfinite(u) || !isfinite(v) || u < -.5f || v < -.5f || u > (float)width + .5f ||
        v > (float)height + .5f)
        return 0;
    const float x = u - .5f, y = v - .5f;
    const int x0 = (int)floorf(x), y0 = (int)floorf(y);
    const float fx = x - (float)x0, fy = y - (float)y0;
    float value = 0;
    for (int yy = 0; yy < 2; ++yy)
        for (int xx = 0; xx < 2; ++xx)
        {
            const int sx = x0 + xx, sy = y0 + yy;
            if (sx >= 0 && sy >= 0 && sx < width && sy < height)
                value += (float)command->data.glyph.field[(size_t)sy * (size_t)width + (size_t)sx] *
                         (xx != 0 ? fx : 1.0f - fx) * (yy != 0 ? fy : 1.0f - fy);
        }
    return value;
}
static int glyph_coverage_mesh(UiGeometry *g, const UiDrawCommand *command)
{
    const int width = command->data.glyph.width, height = command->data.glyph.height;
    const float scale = command->data.glyph.scale;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || scale <= 0 ||
        command->data.glyph.softness <= 0)
        return 0;
    const float x = floorf(command->data.glyph.x), y = floorf(command->data.glyph.y);
    const float phase_x = command->data.glyph.x - x, phase_y = command->data.glyph.y - y;
    const float physical_w = ceilf((float)width * scale + phase_x),
                physical_h = ceilf((float)height * scale + phase_y);
    if (physical_w <= 0 || physical_h <= 0 || physical_w > 2048 || physical_h > 2048)
        return 0;
    const int w = (int)physical_w, h = (int)physical_h;
    const size_t source_size = (size_t)width * (size_t)height, key_size = 32u + source_size;
    if (key_size > GEOMETRY_CACHE_BYTES ||
        (size_t)w * (size_t)h > (GEOMETRY_CACHE_BYTES - key_size) / 4u)
        return 0;
    unsigned char *key = malloc(key_size);
    if (key == NULL)
        return 0;
    uint32_t words[8] = {6u, (uint32_t)width, (uint32_t)height, 0, 0, 0, 0, 0};
    memcpy(words + 3, &phase_x, sizeof phase_x);
    memcpy(words + 4, &phase_y, sizeof phase_y);
    memcpy(words + 5, &scale, sizeof scale);
    memcpy(words + 6, &command->data.glyph.outline, sizeof(float));
    memcpy(words + 7, &command->data.glyph.softness, sizeof(float));
    memcpy(key, words, sizeof words);
    memcpy(key + sizeof words, command->data.glyph.field, source_size);
    const uint64_t hash = hash_bytes(key, key_size);
    SDL_Texture *texture = cached(g, key, key_size, hash);
    if (texture != NULL)
        free(key);
    else
    {
        uint32_t *pixels = malloc((size_t)w * (size_t)h * sizeof *pixels);
        if (pixels == NULL)
        {
            free(key);
            return 0;
        }
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
            {
                const float value = glyph_sample(command, ((float)xx + .5f - phase_x) / scale,
                                                 ((float)yy + .5f - phase_y) / scale);
                const float distance = (value - 128.0f) * (12.0f / 128.0f) * scale;
                const unsigned a =
                    value > 0 ? byte_value(clamp_unit((distance + command->data.glyph.outline) /
                                                          command->data.glyph.softness +
                                                      .5f) *
                                           255.0f)
                              : 0u;
                pixels[(size_t)yy * (size_t)w + (size_t)xx] = a * UINT32_C(0x01010101);
            }
        texture = upload(g, key, key_size, hash, pixels, w, h);
        free(pixels);
        if (texture == NULL)
        {
            free(key);
            return 0;
        }
    }
    GeometryDraw *draw =
        begin_draw(g, command, texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED, SDL_SCALEMODE_NEAREST);
    if (draw == NULL)
        return 0;
    const SDL_FColor color = vertex_color(command->data.glyph.color, command->data.glyph.alpha);
    const int ok = quadrilateral(g, (SDL_Vertex){{x, y}, color, {0, 0}},
                                 (SDL_Vertex){{x + (float)w, y}, color, {1, 0}},
                                 (SDL_Vertex){{x + (float)w, y + (float)h}, color, {1, 1}},
                                 (SDL_Vertex){{x, y + (float)h}, color, {0, 1}});
    end_draw(g, draw);
    return ok;
}
/* SDF cache entries depend only on the authored low-resolution field. Scale,
 * outline, softness and fractional placement stay in the shader, so a moving
 * title cannot multiply coverage textures or churn the current-frame budget. */
static int glyph_sdf_mesh(UiGeometry *g, const UiDrawCommand *command)
{
    const int width = command->data.glyph.width, height = command->data.glyph.height;
    const float scale = command->data.glyph.scale;
    if (width <= 0 || height <= 0 || width > 2046 || height > 2046 || !isfinite(scale) ||
        scale <= 0 || !isfinite(command->data.glyph.softness) || command->data.glyph.softness <= 0 ||
        !isfinite(command->data.glyph.outline) || !isfinite(command->data.glyph.alpha))
        return SDL_SetError("invalid SDF glyph extent or style"), 0;
    const uint32_t words[8] = {5u, (uint32_t)width, (uint32_t)height, 0, 0, 0, 0, 0};
    GeometryResolved *resolved = resolved_slot(g, command->data.glyph.field, words);
    if (resolved == NULL)
        return 0;
    SDL_Texture *texture = resolved->texture;
    if (texture != NULL)
    {
        if (g->profile_enabled)
            ++g->profile_resolved_hits;
    }
    else
    {
        const size_t source_size = (size_t)width * (size_t)height;
        const size_t key_size = sizeof words + source_size;
        unsigned char *key = malloc(key_size);
        if (key == NULL)
            return 0;
        memcpy(key, words, sizeof words);
        memcpy(key + sizeof words, command->data.glyph.field, source_size);
        const uint64_t hash = hash_bytes(key, key_size);
        texture = cached(g, key, key_size, hash);
        if (texture != NULL)
            free(key);
        else
        {
            const size_t stride = (size_t)width + 2u;
            uint32_t *pixels = calloc(stride * ((size_t)height + 2u), sizeof *pixels);
            if (pixels == NULL)
            {
                free(key);
                return 0;
            }
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    pixels[((size_t)y + 1u) * stride + (size_t)x + 1u] =
                        UINT32_C(0xff000000) |
                        (uint32_t)command->data.glyph.field[(size_t)y * (size_t)width + (size_t)x] *
                            UINT32_C(0x00010101);
            texture = upload(g, key, key_size, hash, pixels, width + 2, height + 2);
            free(pixels);
            if (texture == NULL)
            {
                free(key);
                return 0;
            }
        }
        remember_resolved(resolved, command->data.glyph.field, words, texture);
    }
    if (SDL_GetNumberProperty(SDL_GetTextureProperties(texture),
                              SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0) == 0)
        return SDL_SetError("SDF texture has no GLES binding"), 0;
    GeometryDraw *draw =
        begin_draw(g, command, texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED, SDL_SCALEMODE_NEAREST);
    if (draw == NULL)
        return 0;
    draw->glyph_width = width;
    draw->glyph_height = height;
    draw->glyph_scale = scale;
    draw->glyph_outline = command->data.glyph.outline;
    draw->glyph_softness = command->data.glyph.softness;
    draw->glyph_alpha = command->data.glyph.alpha;
    draw->glyph_color = command->data.glyph.color;
    const float gx = command->data.glyph.x, gy = command->data.glyph.y;
    const float x0 = floorf(gx), y0 = floorf(gy);
    const float x1 = ceilf(gx + (float)width * scale), y1 = ceilf(gy + (float)height * scale);
    const SDL_FColor unused = {1, 1, 1, 1};
    const float u0 = (x0 - gx) / scale, v0 = (y0 - gy) / scale;
    const float u1 = (x1 - gx) / scale, v1 = (y1 - gy) / scale;
    const int valid = quadrilateral(
        g, (SDL_Vertex){{x0, y0}, unused, {u0, v0}}, (SDL_Vertex){{x1, y0}, unused, {u1, v0}},
        (SDL_Vertex){{x1, y1}, unused, {u1, v1}}, (SDL_Vertex){{x0, y1}, unused, {u0, v1}});
    end_draw(g, draw);
    return valid;
}
static int glyph_mesh(UiGeometry *g, const UiDrawCommand *command)
{
    return g->font != NULL ? glyph_sdf_mesh(g, command) : glyph_coverage_mesh(g, command);
}
static SDL_Vertex plain_vertex(float x, float y, SDL_FColor color)
{
    return (SDL_Vertex){{x, y}, color, {0, 0}};
}
static int rectangle_mesh(UiGeometry *g, float x, float y, float w, float h, SDL_FColor color)
{
    if (w <= 0 || h <= 0)
        return 1;
    return quadrilateral(g, plain_vertex(x, y, color), plain_vertex(x + w, y, color),
                         plain_vertex(x + w, y + h, color), plain_vertex(x, y + h, color));
}
/* The checker is three GPU layers: the vertical base, nonoverlapping rotated
 * cells, and concentric additive light bands. No window-sized bitmap exists.
 * Byte filtering and CPU incremental-float boundaries remain approximations. */
static SDL_Vertex checker_vertex(float x, float y, float ca, float sa, float u, float v, float lift)
{
    return plain_vertex(ca * x - sa * y + u, sa * x + ca * y + v,
                        (SDL_FColor){lift / 255.0f, lift * .8f / 255.0f, lift * .4f / 255.0f, 1});
}
static int checker_mesh(UiGeometry *g, const UiDrawCommand *command)
{
    UiDrawCommand full = *command;
    full.clip_x0 = full.clip_y0 = 0;
    full.clip_x1 = g->width;
    full.clip_y1 = g->height;
    GeometryDraw *draw = begin_draw(g, &full, NULL, SDL_BLENDMODE_NONE, SDL_SCALEMODE_LINEAR);
    if (draw == NULL)
        return 0;
    const float w = (float)g->width, h = (float)g->height;
    const SDL_FColor top = {52.0f / 255.0f, 190.0f / 255.0f, 246.0f / 255.0f, 1};
    const SDL_FColor bottom = {10.0f / 255.0f, 104.0f / 255.0f, 206.0f / 255.0f, 1};
    if (!quadrilateral(g, plain_vertex(0, 0, top), plain_vertex(w, 0, top),
                       plain_vertex(w, h, bottom), plain_vertex(0, h, bottom)))
        return 0;
    end_draw(g, draw);
    draw = begin_draw(g, &full, NULL, SDL_BLENDMODE_ADD, SDL_SCALEMODE_LINEAR);
    if (draw == NULL)
        return 0;
    const float cell = 168.0f * command->data.checker.scale;
    if (!isfinite(cell) || cell <= 0)
    {
        SDL_SetError("invalid checker cell extent");
        return 0;
    }
    const float ca = cosf(-.33f), sa = sinf(-.33f);
    /* Reduce each axis by an even number of cells before integer bounds. */
    const double travel =
        (double)command->data.checker.seconds * 26.0 * (double)command->data.checker.scale;
    const float scroll_u = (float)fmod(travel, (double)cell * 2.0);
    const float scroll_v = (float)fmod(travel * .35, (double)cell * 2.0);
    float low_u = 1e30f, high_u = -1e30f, low_v = 1e30f, high_v = -1e30f;
    for (unsigned i = 0; i < 4u; ++i)
    {
        const float x = (i & 1u) ? w : 0, y = (i & 2u) ? h : 0;
        const float u = (ca * x + sa * y + scroll_u) / cell,
                    v = (-sa * x + ca * y + scroll_v) / cell;
        low_u = fminf(low_u, u);
        high_u = fmaxf(high_u, u);
        low_v = fminf(low_v, v);
        high_v = fmaxf(high_v, v);
    }
    if (high_u - low_u > 128 || high_v - low_v > 128)
    {
        SDL_SetError("checker cell budget exceeded");
        return 0;
    }
    const int x0 = (int)floorf(low_u), x1 = (int)ceilf(high_u), y0 = (int)floorf(low_v),
              y1 = (int)ceilf(high_v);
    if ((size_t)(x1 - x0) * (size_t)(y1 - y0) > 4096u)
        return 0;
    const float ox = -ca * scroll_u + sa * scroll_v, oy = -sa * scroll_u - ca * scroll_v;
    const float band = fminf(2.0f, cell * .5f);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
        {
            const float left = (float)xx * cell, top_y = (float)yy * cell;
            const float shade = ((xx + yy) & 1) != 0 ? 14.0f : 0.0f;
            const float xs[4] = {left, left + band, left + cell - band, left + cell};
            const float ys[4] = {top_y, top_y + band, top_y + cell - band, top_y + cell};
            for (unsigned row = 0; row < 3u; ++row)
                for (unsigned col = 0; col < 3u; ++col)
                {
                    SDL_Vertex vertices[4];
                    const unsigned xi[4] = {col, col + 1u, col + 1u, col},
                                   yi[4] = {row, row, row + 1u, row + 1u};
                    for (unsigned i = 0; i < 4u; ++i)
                    {
                        const int edge = xi[i] == 0u || xi[i] == 3u || yi[i] == 0u || yi[i] == 3u;
                        vertices[i] = checker_vertex(xs[xi[i]], ys[yi[i]], ca, sa, ox, oy,
                                                     edge ? 7.0f : shade);
                    }
                    if (!quadrilateral(g, vertices[0], vertices[1], vertices[2], vertices[3]))
                        return 0;
                }
        }
    const float cx = w * .18f, cy = h * -.1f, radius = fmaxf(w, h) * 1.1f;
    for (unsigned ring = 0; ring < 16u; ++ring)
        for (unsigned segment = 0; segment < 64u; ++segment)
        {
            const float r0 = (float)ring / 16.0f, r1 = (float)(ring + 1u) / 16.0f;
            const float a = (float)segment * pi / 32.0f, b = (float)(segment + 1u) * pi / 32.0f;
            const SDL_FColor c0 = {(1 - r0 * r0) * 34.0f / 255.0f, (1 - r0 * r0) * 27.2f / 255.0f,
                                   (1 - r0 * r0) * 13.6f / 255.0f, 1};
            const SDL_FColor c1 = {(1 - r1 * r1) * 34.0f / 255.0f, (1 - r1 * r1) * 27.2f / 255.0f,
                                   (1 - r1 * r1) * 13.6f / 255.0f, 1};
            if (!quadrilateral(
                    g, plain_vertex(cx + cosf(a) * r0 * radius, cy + sinf(a) * r0 * radius, c0),
                    plain_vertex(cx + cosf(a) * r1 * radius, cy + sinf(a) * r1 * radius, c1),
                    plain_vertex(cx + cosf(b) * r1 * radius, cy + sinf(b) * r1 * radius, c1),
                    plain_vertex(cx + cosf(b) * r0 * radius, cy + sinf(b) * r0 * radius, c0)))
                return 0;
        }
    end_draw(g, draw);
    return 1;
}

static int command_mesh(UiGeometry *g, const UiDrawCommand *command)
{
    if (command->kind == UI_DRAW_CHECKER)
        return checker_mesh(g, command);
    if (command->kind == UI_DRAW_GLYPH)
    {
        const uint64_t start = g->profile_enabled ? SDL_GetPerformanceCounter() : 0;
        const int valid = glyph_mesh(g, command);
        if (g->profile_enabled)
            g->profile_glyph_ticks += SDL_GetPerformanceCounter() - start;
        return valid;
    }
    if (command->kind == UI_DRAW_CLEAR || command->kind == UI_DRAW_RECT ||
        command->kind == UI_DRAW_TINT)
    {
        uint32_t color;
        float alpha = 1, x = 0, y = 0, w = (float)g->width, h = (float)g->height;
        SDL_BlendMode blend = SDL_BLENDMODE_BLEND_PREMULTIPLIED;
        UiDrawCommand clip = *command;
        if (command->kind == UI_DRAW_CLEAR)
        {
            color =
                command->data.clear | (command->premultiplied_alpha ? 0u : UINT32_C(0xff000000));
            blend = SDL_BLENDMODE_NONE;
            clip.clip_x0 = clip.clip_y0 = 0;
            clip.clip_x1 = g->width;
            clip.clip_y1 = g->height;
        }
        else if (command->kind == UI_DRAW_RECT)
        {
            x = floorf(command->data.rect.x + .5f);
            y = floorf(command->data.rect.y + .5f);
            w = floorf(command->data.rect.x + command->data.rect.w + .5f) - x;
            h = floorf(command->data.rect.y + command->data.rect.h + .5f) - y;
            color = command->data.rect.color;
            alpha = command->data.rect.alpha;
        }
        else
        {
            x = (float)command->data.tint.x0;
            y = (float)command->data.tint.y0;
            w = (float)command->data.tint.x1 - x;
            h = (float)command->data.tint.y1 - y;
            color = command->data.tint.color | UINT32_C(0xff000000);
            alpha = command->data.tint.amount;
            blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA,
                                               SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                               SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                               SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        }
        GeometryDraw *draw = begin_draw(g, &clip, NULL, blend, SDL_SCALEMODE_LINEAR);
        if (draw == NULL)
            return 0;
        const int valid = rectangle_mesh(g, x, y, w, h, vertex_color(color, alpha));
        end_draw(g, draw);
        return valid;
    }
    GeometryShape shape = {.kind = command->kind, .feather = 1};
    GeometryPaint paint = {.alpha = 1, .color = {1, 1, 1, 1}};
    UiPaint solid;
    const UiPaint *source = NULL;
    float alpha = 1;
    const UiDrawCommand *image = NULL;
    switch (command->kind)
    {
    case UI_DRAW_CONVEX:
    {
        shape.points = command->data.convex.points;
        shape.point_count = command->data.convex.count;
        shape.feather = fmaxf(1, command->data.convex.feather);
        source = &command->data.convex.paint;
        alpha = command->data.convex.alpha;
        float area = 0;
        for (int i = 0; i < shape.point_count; ++i)
        {
            const UiPoint a = shape.points[i], b = shape.points[(i + 1) % shape.point_count];
            shape.cx += a.x / (float)shape.point_count;
            shape.cy += a.y / (float)shape.point_count;
            area += a.x * b.y - b.x * a.y;
        }
        if (fabsf(area) < 1e-4f)
            return 1;
        shape.orientation = area > 0 ? 1.0f : -1.0f;
        for (int i = 0; i < shape.point_count; ++i)
        {
            const UiPoint a = shape.points[i], b = shape.points[(i + 1) % shape.point_count];
            const float ex = b.x - a.x, ey = b.y - a.y, length = hypotf(ex, ey);
            if (length > 1e-5f)
            {
                float *edge = shape.edges[shape.edge_count++];
                edge[0] = shape.orientation * ey / length;
                edge[1] = -shape.orientation * ex / length;
                edge[2] = edge[0] * a.x + edge[1] * a.y;
            }
        }
        if (shape.edge_count < 3u)
            return 1;
        for (int i = 0; i < shape.point_count; ++i)
        {
            shape.hw = fmaxf(shape.hw, fabsf(shape.points[i].x - shape.cx));
            shape.hh = fmaxf(shape.hh, fabsf(shape.points[i].y - shape.cy));
        }
        if (shape_distance(&shape, (UiPoint){shape.cx, shape.cy}) >= 0)
        {
            SDL_SetError("unsupported nonconvex contour");
            return 0;
        }
        break;
    }
    case UI_DRAW_ROUND_RECT:
    case UI_DRAW_STROKE_ROUND_RECT:
        shape.hw = command->data.box.w * .5f;
        shape.hh = command->data.box.h * .5f;
        shape.cx = command->data.box.x + shape.hw;
        shape.cy = command->data.box.y + shape.hh;
        shape.radius = fmaxf(0, fminf(command->data.box.radius, fminf(shape.hw, shape.hh)));
        shape.skew = command->data.box.skew;
        shape.feather = fmaxf(1, command->data.box.feather);
        shape.stroke = command->data.box.stroke;
        source = &command->data.box.paint;
        alpha = command->data.box.alpha;
        break;
    case UI_DRAW_ELLIPSE:
    case UI_DRAW_STROKE_CIRCLE:
        shape.cx = command->data.ellipse.cx;
        shape.cy = command->data.ellipse.cy;
        shape.hw = command->data.ellipse.rx;
        shape.hh = command->data.ellipse.ry;
        shape.feather = fmaxf(1, command->data.ellipse.feather);
        shape.stroke = command->data.ellipse.stroke;
        source = &command->data.ellipse.paint;
        alpha = command->data.ellipse.alpha;
        break;
    case UI_DRAW_IMAGE:
        image = command;
        shape.kind = UI_DRAW_ROUND_RECT;
        shape.hw = command->data.image.w * .5f;
        shape.hh = command->data.image.h * .5f;
        shape.cx = command->data.image.x + shape.hw;
        shape.cy = command->data.image.y + shape.hh;
        shape.radius = fmaxf(0, fminf(command->data.image.radius, fminf(shape.hw, shape.hh)));
        const uint64_t image_start = g->profile_enabled ? SDL_GetPerformanceCounter() : 0;
        paint.texture = prepare_image(g, command);
        if (g->profile_enabled)
            g->profile_image_ticks += SDL_GetPerformanceCounter() - image_start;
        paint.alpha = clamp_unit(command->data.image.alpha);
        if (paint.texture == NULL)
            return 0;
        break;
    default:
        SDL_SetError("unknown geometry command");
        return 0;
    }
    if (shape.hw <= 0 || shape.hh <= 0 || alpha <= 0)
        return 1;
    if ((shape.kind == UI_DRAW_STROKE_CIRCLE || shape.kind == UI_DRAW_STROKE_ROUND_RECT) &&
        shape.stroke <= 0)
        return 1;
    if (source == NULL && image == NULL)
    {
        solid = ui_paint_solid(UINT32_MAX);
        source = &solid;
    }
    if (source != NULL && !prepare_paint(g, source, alpha, &paint))
        return 0;
    const SDL_ScaleMode sampling = image != NULL && (image->data.image.flags & UI_IMAGE_NEAREST)
                                       ? SDL_SCALEMODE_NEAREST
                                       : SDL_SCALEMODE_LINEAR;
    GeometryDraw *draw =
        begin_draw(g, command, paint.texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED, sampling);
    if (draw == NULL)
        return 0;
    const uint64_t shape_start = g->profile_enabled ? SDL_GetPerformanceCounter() : 0;
    const int valid = shape_mesh(g, &shape, &paint, image);
    if (g->profile_enabled)
        g->profile_shape_ticks += SDL_GetPerformanceCounter() - shape_start;
    end_draw(g, draw);
    return valid;
}

UiGeometry *ui_geometry_create(SDL_Renderer *renderer, char *error, size_t capacity)
{
    const char *name = renderer != NULL ? SDL_GetRendererName(renderer) : NULL;
    if (name == NULL || strcmp(name, "software") == 0)
    {
        SDL_SetError("accelerated SDL geometry renderer required");
        failure(error, capacity, "create");
        return NULL;
    }
    UiGeometry *g = calloc(1, sizeof *g);
    if (g == NULL)
        return NULL;
    g->renderer = renderer;
    if (strcmp(name, "opengles2") == 0)
    {
        g->font = ui_gles_font_create(renderer);
        if (g->font == NULL)
        {
            free(g);
            return NULL;
        }
    }
    const char *profile = getenv("MKSC_UI_GEOMETRY_PROFILE");
    g->profile_enabled = profile != NULL && strcmp(profile, "1") == 0;
    g->draws = calloc(GEOMETRY_COMMANDS, sizeof *g->draws);
    if (g->draws == NULL)
    {
        ui_gles_font_close(g->font);
        free(g);
        return NULL;
    }
    return g;
}
int ui_geometry_prepare(UiGeometry *g, const UiDrawList *list, int width, int height, char *error,
                        size_t capacity)
{
    if (g == NULL)
        return 0;
    const uint64_t profile_start = g->profile_enabled ? SDL_GetPerformanceCounter() : 0;
    g->prepared = 0;
    g->vertex_count = g->draw_count = 0;
    g->uploaded = 0;
    memset(g->resolved, 0, sizeof g->resolved);
    if (list == NULL || width <= 0 || height <= 0 || width > 65536 || height > 65536 ||
        ui_draw_list_error(list)[0] != '\0')
    {
        SDL_SetError("invalid or failed draw list");
        return failure(error, capacity, "preflight");
    }
    /* Submission before eviction keeps prior queued commands from borrowing
     * textures or data that this preparation is about to replace. */
    if (!SDL_FlushRenderer(g->renderer))
        return failure(error, capacity, "frame boundary");
    g->width = width;
    g->height = height;
    if (++g->frame == 0)
    {
        ui_geometry_forget_images(g);
        g->frame = 1;
    }
    const size_t count = ui_draw_list_count(list);
    if (count > 4096u)
        return failure(error, capacity, "command budget");
    for (size_t i = 0; i < count; ++i)
    {
        const UiDrawCommand *command = ui_draw_list_command(list, i);
        if (command == NULL || !command_mesh(g, command))
            return failure(error, capacity, "command preflight");
    }
    for (size_t i = 0; i < g->vertex_count; ++i)
    {
        const SDL_Vertex *v = &g->vertices[i];
        const float fields[] = {v->position.x, v->position.y, v->tex_coord.x, v->tex_coord.y,
                                v->color.r,    v->color.g,    v->color.b,     v->color.a};
        for (unsigned j = 0; j < 8u; ++j)
            if (!isfinite(fields[j]) || fabsf(fields[j]) > 1e7f)
            {
                SDL_SetError("geometry contains unbounded coordinates");
                return failure(error, capacity, "numeric preflight");
            }
    }
    /* Validate all required blend modes without queueing any target writes. */
    SDL_BlendMode previous;
    if (!SDL_GetRenderDrawBlendMode(g->renderer, &previous))
        return failure(error, capacity, "blend preflight");
    int valid = 1;
    for (size_t i = 0; valid && i < g->draw_count; ++i)
    {
        GeometryDraw *draw = &g->draws[i];
        valid = draw->texture != NULL ? SDL_SetTextureBlendMode(draw->texture, draw->blend)
                                      : SDL_SetRenderDrawBlendMode(g->renderer, draw->blend);
    }
    const int restored = SDL_SetRenderDrawBlendMode(g->renderer, previous);
    if (!valid || !restored)
        return failure(error, capacity, "blend preflight");
    if (g->font != NULL &&
        !ui_gles_font_prepare(g->font, g->vertices, g->vertex_count, error, capacity))
        return 0;
    g->prepared = 1;
    if (g->profile_enabled)
    {
        ++g->profile_prepares;
        g->profile_prepare_ticks += SDL_GetPerformanceCounter() - profile_start;
        g->profile_vertices += g->vertex_count;
        g->profile_batches += g->draw_count;
    }
    return 1;
}
int ui_geometry_draw(UiGeometry *g, const SDL_FRect *target, char *error, size_t capacity)
{
    if (g == NULL || !g->prepared || target == NULL || !isfinite(target->x) ||
        !isfinite(target->y) || !isfinite(target->w) || !isfinite(target->h) ||
        fabsf(target->x) > 65536 || fabsf(target->y) > 65536 || target->w <= 0 || target->h <= 0 ||
        target->w > 65536 || target->h > 65536)
        return 0;
    const uint64_t profile_start = g->profile_enabled ? SDL_GetPerformanceCounter() : 0;
    SDL_Rect viewport, clip;
    SDL_BlendMode blend;
    float old_sx, old_sy;
    SDL_TextureAddressMode u, v;
    const int clipped = SDL_RenderClipEnabled(g->renderer);
    if (!SDL_GetRenderViewport(g->renderer, &viewport) ||
        !SDL_GetRenderClipRect(g->renderer, &clip) ||
        !SDL_GetRenderDrawBlendMode(g->renderer, &blend) ||
        !SDL_GetRenderScale(g->renderer, &old_sx, &old_sy) ||
        !SDL_GetRenderTextureAddressMode(g->renderer, &u, &v))
        return failure(error, capacity, "save renderer state");
    int valid = SDL_SetRenderScale(g->renderer, 1, 1) && SDL_SetRenderViewport(g->renderer, NULL) &&
                SDL_SetRenderTextureAddressMode(g->renderer, SDL_TEXTURE_ADDRESS_CLAMP,
                                                SDL_TEXTURE_ADDRESS_CLAMP);
    const float sx = target->w / (float)g->width, sy = target->h / (float)g->height;
    for (size_t i = 0; i < g->vertex_count; ++i)
    {
        g->transformed[i] = g->vertices[i];
        g->transformed[i].position.x = target->x + g->vertices[i].position.x * sx;
        g->transformed[i].position.y = target->y + g->vertices[i].position.y * sy;
    }
    for (size_t i = 0; valid && i < g->draw_count; ++i)
    {
        const GeometryDraw *draw = &g->draws[i];
        if (draw->count == 0 || draw->clip.w <= 0 || draw->clip.h <= 0)
            continue;
        if (draw->glyph_width != 0)
        {
            if (!ui_gles_font_begin(g->font, target, g->width, g->height, error, capacity))
            {
                valid = 0;
                break;
            }
            /* All consecutive font passes share one flush/state boundary;
             * outline, shadow and fill retain their recorded painter order. */
            do
            {
                draw = &g->draws[i];
                if (draw->count != 0 && draw->clip.w > 0 && draw->clip.h > 0)
                    valid = ui_gles_font_draw(g->font, draw->texture, draw->first, draw->count,
                                              &draw->clip, draw->glyph_width, draw->glyph_height,
                                              draw->glyph_scale, draw->glyph_outline,
                                              draw->glyph_softness, draw->glyph_color,
                                              draw->glyph_alpha);
                if (!valid || i + 1u == g->draw_count || g->draws[i + 1u].glyph_width == 0)
                    break;
                ++i;
            } while (1);
            const int font_restored = ui_gles_font_end(g->font);
            valid = valid && font_restored;
            continue;
        }
        const int left = (int)floorf(target->x + (float)draw->clip.x * sx),
                  top = (int)floorf(target->y + (float)draw->clip.y * sy);
        const SDL_Rect bounds = {
            left, top,
            (int)ceilf(target->x + ((float)draw->clip.x + (float)draw->clip.w) * sx) - left,
            (int)ceilf(target->y + ((float)draw->clip.y + (float)draw->clip.h) * sy) - top};
        valid = SDL_SetRenderClipRect(g->renderer, &bounds);
        if (valid && draw->texture != NULL)
            valid = SDL_SetTextureBlendMode(draw->texture, draw->blend) &&
                    SDL_SetTextureScaleMode(draw->texture, draw->sampling);
        else if (valid)
            valid = SDL_SetRenderDrawBlendMode(g->renderer, draw->blend);
        if (valid)
            valid = SDL_RenderGeometry(g->renderer, draw->texture, g->transformed + draw->first,
                                       (int)draw->count, NULL, 0);
    }
    const int scale_restored = SDL_SetRenderScale(g->renderer, old_sx, old_sy);
    const int viewport_restored = SDL_SetRenderViewport(g->renderer, &viewport);
    const int clip_restored = SDL_SetRenderClipRect(g->renderer, clipped ? &clip : NULL);
    const int blend_restored = SDL_SetRenderDrawBlendMode(g->renderer, blend);
    const int address_restored = SDL_SetRenderTextureAddressMode(g->renderer, u, v);
    g->prepared = 0;
    if (g->profile_enabled)
    {
        ++g->profile_draws;
        g->profile_draw_ticks += SDL_GetPerformanceCounter() - profile_start;
    }
    if (!valid || !scale_restored || !viewport_restored || !clip_restored || !blend_restored ||
        !address_restored)
        return failure(error, capacity, "geometry submission");
    return 1;
}
void ui_geometry_forget_images(UiGeometry *g)
{
    if (g == NULL)
        return;
    (void)SDL_FlushRenderer(g->renderer);
    for (unsigned i = 0; i < g->image_count; ++i)
        retire(g, &g->images[i]);
    g->image_count = 0;
    ui_gles_font_forget(g->font);
    g->prepared = 0;
}
size_t ui_geometry_uploaded_bytes(const UiGeometry *g)
{
    return g != NULL ? g->uploaded : 0u;
}
void ui_geometry_close(UiGeometry *g)
{
    if (g == NULL)
        return;
    if (g->profile_enabled && g->profile_prepares != 0u)
    {
        const double factor =
            1000.0 / (double)SDL_GetPerformanceFrequency() / (double)g->profile_prepares;
        const double draw_factor =
            g->profile_draws != 0u
                ? 1000.0 / (double)SDL_GetPerformanceFrequency() / (double)g->profile_draws
                : 0;
        fprintf(
            stderr,
            "Geometry UI profile: prepared=%llu drawn=%llu mean_prepare_ms=%.3f mean_shape_ms=%.3f mean_glyph_ms=%.3f mean_image_ms=%.3f mean_draw_ms=%.3f mean_vertices=%.1f mean_batches=%.1f resolved_hits=%llu persistent_hits=%llu uploads=%llu mean_uploaded_bytes=%.1f\n",
            (unsigned long long)g->profile_prepares, (unsigned long long)g->profile_draws,
            (double)g->profile_prepare_ticks * factor, (double)g->profile_shape_ticks * factor,
            (double)g->profile_glyph_ticks * factor, (double)g->profile_image_ticks * factor,
            (double)g->profile_draw_ticks * draw_factor,
            (double)g->profile_vertices / (double)g->profile_prepares,
            (double)g->profile_batches / (double)g->profile_prepares,
            (unsigned long long)g->profile_resolved_hits, (unsigned long long)g->profile_cache_hits,
            (unsigned long long)g->profile_uploads,
            (double)g->profile_upload_bytes / (double)g->profile_prepares);
    }
    ui_geometry_forget_images(g);
    ui_gles_font_close(g->font);
    free(g->vertices);
    free(g->transformed);
    free(g->draws);
    free(g);
}
