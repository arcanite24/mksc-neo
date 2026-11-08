#include "ui_draw_list.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    COMMAND_MAX = 4096,
    BYTE_MAX = 16 * 1024 * 1024
};

typedef struct
{
    UiDrawKind kind;
    int width, height;
    uint64_t hash;
    uintptr_t borrowed_address;
    const void *data;
    const UiImage *image;
} OwnedSource;

struct UiDrawList
{
    UiCanvasSink sink;
    size_t limit, capacity, used, base, count, sources;
    UiDrawCommand *commands;
    OwnedSource *source;
    char error[96];
};

/* MSVC's C library does not expose max_align_t. Align to the actual arena
 * payloads, including both metadata arrays, rather than a host-library type. */
typedef union
{
    UiDrawCommand command;
    OwnedSource source;
    UiImage image;
    UiPoint point;
    uint32_t pixel;
} PayloadAlignment;

static size_t aligned(size_t value)
{
    const size_t alignment = _Alignof(PayloadAlignment);
    return value + (alignment - value % alignment) % alignment;
}

static int fail(UiDrawList *list, const char *reason)
{
    list->sink.failed = 1u;
    if (list->error[0] == '\0')
    {
        strncpy(list->error, reason, sizeof list->error - 1u);
        list->error[sizeof list->error - 1u] = '\0';
    }
    return 0;
}

static void *reserve(UiDrawList *list, size_t size)
{
    const size_t start = aligned(list->used);
    if (start > list->capacity || size > list->capacity - start)
    {
        fail(list, "UI draw payload byte limit exceeded");
        return NULL;
    }
    void *result = (unsigned char *)list + start;
    list->used = start + size;
    return result;
}

static uint64_t content_hash(const void *data, size_t size)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static const OwnedSource *own_source(UiDrawList *list, UiDrawKind kind, const void *data, int width,
                                     int height)
{
    const size_t element = kind == UI_DRAW_IMAGE ? sizeof(uint32_t) : 1u;
    if (data == NULL || width <= 0 || height <= 0 ||
        (size_t)width > list->capacity / element / (size_t)height)
    {
        fail(list, "Invalid or oversized UI draw source");
        return NULL;
    }
    const size_t size = (size_t)width * (size_t)height * element;
    const uintptr_t address = (uintptr_t)data;
    /* Address identity only selects a candidate. Compare the current live
     * bytes with our copy: Art may have freed and reused this exact address. */
    for (size_t i = 0; i < list->sources; ++i)
    {
        const OwnedSource *source = &list->source[i];
        if (source->kind == kind && source->width == width && source->height == height &&
            source->borrowed_address == address && memcmp(source->data, data, size) == 0)
            return source;
    }
    const uint64_t hash = content_hash(data, size);
    for (size_t i = 0; i < list->sources; ++i)
    {
        const OwnedSource *source = &list->source[i];
        if (source->kind == kind && source->width == width && source->height == height &&
            source->hash == hash && memcmp(source->data, data, size) == 0)
            return source;
    }
    void *copy = reserve(list, size);
    if (copy == NULL)
        return NULL;
    memcpy(copy, data, size);
    UiImage *image = NULL;
    if (kind == UI_DRAW_IMAGE)
    {
        image = reserve(list, sizeof *image);
        if (image == NULL)
            return NULL;
        *image = (UiImage){copy, width, height};
    }
    OwnedSource *source = &list->source[list->sources++];
    *source = (OwnedSource){kind, width, height, hash, address, copy, image};
    return source;
}

static int finite_values(const float *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (!isfinite(values[i]))
            return 0;
    return 1;
}

static int paint_valid(const UiPaint *paint)
{
    return paint->count > 0u && paint->count <= UI_PAINT_STOPS &&
           finite_values(paint->positions, paint->count) &&
           finite_values((float[]){paint->x0, paint->y0, paint->x1, paint->y1}, 4u);
}

static int geometry_valid(const UiDrawCommand *command)
{
    switch (command->kind)
    {
    case UI_DRAW_CONVEX:
        if (command->data.convex.count < 3 || command->data.convex.count > 160 ||
            command->data.convex.points == NULL || !paint_valid(&command->data.convex.paint) ||
            !isfinite(command->data.convex.feather) || !isfinite(command->data.convex.alpha))
            return 0;
        for (int i = 0; i < command->data.convex.count; ++i)
            if (!isfinite(command->data.convex.points[i].x) ||
                !isfinite(command->data.convex.points[i].y))
                return 0;
        return 1;
    case UI_DRAW_ROUND_RECT:
    case UI_DRAW_STROKE_ROUND_RECT:
        return paint_valid(&command->data.box.paint) &&
               finite_values((float[]){command->data.box.x, command->data.box.y,
                                       command->data.box.w, command->data.box.h,
                                       command->data.box.radius, command->data.box.skew,
                                       command->data.box.feather, command->data.box.alpha,
                                       command->data.box.stroke},
                             9u);
    case UI_DRAW_ELLIPSE:
    case UI_DRAW_STROKE_CIRCLE:
        return paint_valid(&command->data.ellipse.paint) &&
               finite_values((float[]){command->data.ellipse.cx, command->data.ellipse.cy,
                                       command->data.ellipse.rx, command->data.ellipse.ry,
                                       command->data.ellipse.feather, command->data.ellipse.alpha,
                                       command->data.ellipse.stroke},
                             7u);
    case UI_DRAW_RECT:
        return finite_values((float[]){command->data.rect.x, command->data.rect.y,
                                       command->data.rect.w, command->data.rect.h,
                                       command->data.rect.alpha},
                             5u);
    case UI_DRAW_IMAGE:
        return !(command->data.image.flags &
                 ~(unsigned)(UI_IMAGE_NEAREST | UI_IMAGE_GRAY | UI_IMAGE_SILHOUETTE)) &&
               finite_values((float[]){command->data.image.sx, command->data.image.sy,
                                       command->data.image.sw, command->data.image.sh,
                                       command->data.image.x, command->data.image.y,
                                       command->data.image.w, command->data.image.h,
                                       command->data.image.alpha, command->data.image.radius},
                             10u);
    case UI_DRAW_TINT:
        return isfinite(command->data.tint.amount);
    case UI_DRAW_GLYPH:
        return command->data.glyph.scale > 0.0f &&
               finite_values((float[]){command->data.glyph.x, command->data.glyph.y,
                                       command->data.glyph.scale, command->data.glyph.outline,
                                       command->data.glyph.softness, command->data.glyph.alpha},
                             6u);
    case UI_DRAW_CHECKER:
        return command->data.checker.scale > 0.0f && isfinite(command->data.checker.scale) &&
               isfinite(command->data.checker.seconds);
    case UI_DRAW_CLEAR:
        return 1;
    }
    return 0;
}

static int record(void *owner, const UiDrawCommand *input)
{
    UiDrawList *list = owner;
    if (list->sink.failed)
        return 0;
    if (input == NULL || (unsigned)input->kind > UI_DRAW_CHECKER || input->clip_x0 < 0 ||
        input->clip_y0 < 0 || input->clip_x1 < input->clip_x0 || input->clip_y1 < input->clip_y0 ||
        input->premultiplied_alpha > 1u)
        return fail(list, "Invalid UI draw command");
    if (!geometry_valid(input))
        return fail(list, "Invalid UI draw geometry");
    if (list->count == list->limit)
        return fail(list, "UI draw command limit exceeded");
    UiDrawCommand command = *input;
    if (command.kind == UI_DRAW_CONVEX)
    {
        const int count = command.data.convex.count;
        if (count < 3 || count > 160 || command.data.convex.points == NULL)
            return fail(list, "Invalid UI convex points");
        const size_t size = (size_t)count * sizeof(UiPoint);
        void *copy = reserve(list, size);
        if (copy == NULL)
            return 0;
        memcpy(copy, command.data.convex.points, size);
        command.data.convex.points = copy;
    }
    else if (command.kind == UI_DRAW_IMAGE || command.kind == UI_DRAW_GLYPH)
    {
        const int image = command.kind == UI_DRAW_IMAGE;
        const UiImage *original = image ? command.data.image.source : NULL;
        if (image && original == NULL)
            return fail(list, "Missing UI image descriptor");
        const OwnedSource *source = own_source(
            list, command.kind, image ? (const void *)original->pixels : command.data.glyph.field,
            image ? original->width : command.data.glyph.width,
            image ? original->height : command.data.glyph.height);
        if (source == NULL)
            return 0;
        if (image)
            command.data.image.source = source->image;
        else
            command.data.glyph.field = source->data;
    }
    list->commands[list->count++] = command;
    return 1;
}

UiDrawList *ui_draw_list_create(size_t command_limit, size_t byte_limit)
{
    if (command_limit == 0u || command_limit > COMMAND_MAX || byte_limit > BYTE_MAX)
        return NULL;
    const size_t commands = aligned(sizeof(UiDrawList));
    const size_t sources = aligned(commands + command_limit * sizeof(UiDrawCommand));
    const size_t base = aligned(sources + command_limit * sizeof(OwnedSource));
    if (byte_limit < base)
        return NULL;
    UiDrawList *list = malloc(byte_limit);
    if (list == NULL)
        return NULL;
    memset(list, 0, sizeof *list);
    list->limit = command_limit;
    list->capacity = byte_limit;
    list->base = base;
    list->used = base;
    list->commands = (UiDrawCommand *)((unsigned char *)list + commands);
    list->source = (OwnedSource *)((unsigned char *)list + sources);
    list->sink = (UiCanvasSink){list, record, 0u};
    return list;
}

void ui_draw_list_reset(UiDrawList *list)
{
    if (list == NULL)
        return;
    list->used = list->base;
    list->count = list->sources = 0u;
    list->sink.failed = 0u;
    list->error[0] = '\0';
}

void ui_draw_list_close(UiDrawList *list)
{
    free(list);
}
UiCanvasSink *ui_draw_list_sink(UiDrawList *list)
{
    return list ? &list->sink : NULL;
}
size_t ui_draw_list_count(const UiDrawList *list)
{
    return list && !list->sink.failed ? list->count : 0u;
}
size_t ui_draw_list_bytes(const UiDrawList *list)
{
    return list ? list->used : 0u;
}
const UiDrawCommand *ui_draw_list_command(const UiDrawList *list, size_t index)
{
    return index < ui_draw_list_count(list) ? &list->commands[index] : NULL;
}
const char *ui_draw_list_error(const UiDrawList *list)
{
    return list ? list->error : "Missing UI draw list";
}
