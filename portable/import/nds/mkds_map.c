#include "mkds_map.h"

#include <string.h>

/* ---- KCL ---------------------------------------------------------------- */

static void cross(const int64_t a[3], const int64_t b[3], int64_t out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static int64_t dot(const int64_t a[3], const int64_t b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int64_t shift_round(int64_t value, unsigned shift)
{
    const int64_t half = (int64_t)1 << (shift - 1);
    return value >= 0 ? (value + half) >> shift : -((-value + half) >> shift);
}

/* The corner that edge normal `edge` and the face normal span, `height` along
 * edge3 from the first corner (all Q12): a + c*height/dot(c, edge3). */
static int prism_corner(const int64_t a[3], const int64_t edge[3], const int64_t face[3],
                        const int64_t edge3[3], int64_t height, int32_t out[3])
{
    int64_t direction[3];
    cross(edge, face, direction);
    for (unsigned axis = 0; axis < 3; ++axis)
        direction[axis] = shift_round(direction[axis], 12);
    const int64_t divisor = dot(direction, edge3);
    if (divisor == 0)
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const int64_t value = a[axis] + direction[axis] * height * 4096 / divisor;
        if (value < INT32_MIN || value > INT32_MAX)
            return 0;
        out[axis] = (int32_t)value;
    }
    return 1;
}

MkdsKcl mkds_kcl(RiCtx *ctx, RiData file, const char *name)
{
    MkdsKcl out = {0};
    if (file.size < 0x3C)
        ri_fail(ctx, "%s: KCL header is truncated", name);
    const uint32_t positions = ri_get_u32(ctx, file, 0), normals = ri_get_u32(ctx, file, 4);
    const uint32_t prisms = ri_get_u32(ctx, file, 8) + 0x10u, octree = ri_get_u32(ctx, file, 12);
    if (positions < 0x3C || normals < positions || prisms < normals || octree < prisms || octree > file.size ||
        (octree - prisms) % 16)
        ri_fail(ctx, "%s: KCL section offsets are out of order", name);
    const size_t position_count = (normals - positions) / 12, normal_count = (prisms - normals) / 6;
    const size_t prism_count = (octree - prisms) / 16;
    if (prism_count == 0)
        ri_fail(ctx, "%s: KCL has no prisms", name);
    out.triangles = (MkdsKclTriangle *)(void *)ri_buf_zero(ctx, prism_count * sizeof(MkdsKclTriangle))->data;
    for (size_t index = 0; index < prism_count; ++index)
    {
        const size_t at = prisms + index * 16;
        const int64_t height = ri_get_i32(ctx, file, at);
        uint32_t indices[5];
        for (unsigned k = 0; k < 5; ++k)
            indices[k] = ri_get_u16(ctx, file, at + 4 + k * 2);
        const uint32_t attribute = ri_get_u16(ctx, file, at + 14);
        if (indices[0] >= position_count)
            ri_fail(ctx, "%s: prism %zu position %u is out of range", name, index, (unsigned)indices[0]);
        int64_t a[3], vectors[4][3]; /* face, edge1, edge2, edge3 */
        for (unsigned axis = 0; axis < 3; ++axis)
            a[axis] = ri_get_i32(ctx, file, positions + indices[0] * 12 + axis * 4);
        for (unsigned k = 0; k < 4; ++k)
        {
            if (indices[k + 1] >= normal_count)
                ri_fail(ctx, "%s: prism %zu normal %u is out of range", name, index, (unsigned)indices[k + 1]);
            for (unsigned axis = 0; axis < 3; ++axis)
                vectors[k][axis] = ri_get_i16(ctx, file, normals + indices[k + 1] * 6 + axis * 2);
        }
        MkdsKclTriangle *triangle = &out.triangles[out.count];
        int32_t b[3], c[3];
        if (height <= 0 || !prism_corner(a, vectors[2], vectors[0], vectors[3], height, b) ||
            !prism_corner(a, vectors[1], vectors[0], vectors[3], height, c))
            continue; /* flat prism: no surface */
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            triangle->vertex[0][axis] = (int32_t)a[axis];
            triangle->vertex[1][axis] = b[axis];
            triangle->vertex[2][axis] = c[axis];
            triangle->normal[axis] = (int32_t)vectors[0][axis];
        }
        int64_t ab[3], ac[3], area[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            ab[axis] = (int64_t)b[axis] - a[axis];
            ac[axis] = (int64_t)c[axis] - a[axis];
        }
        cross(ab, ac, area);
        if (area[0] == 0 && area[1] == 0 && area[2] == 0)
            continue;
        if (dot(area, vectors[0]) < 0)
        {
            memcpy(triangle->vertex[1], c, sizeof(c));
            memcpy(triangle->vertex[2], b, sizeof(b));
        }
        triangle->attribute = attribute;
        out.count++;
    }
    return out;
}

/* ---- NKMD --------------------------------------------------------------- */

typedef struct Section
{
    RiData entries;
    uint32_t count;
} Section;

static Section section(RiCtx *ctx, RiData file, const char *name, size_t at, size_t stride)
{
    Section out;
    out.count = ri_get_u32(ctx, file, at + 4);
    if (out.count > 4096)
        ri_fail(ctx, "%s: %.4s has %u entries", name, (const char *)ri_at(ctx, file, at, 4), (unsigned)out.count);
    out.entries = ri_span(ctx, file, at + 8, out.count * stride);
    return out;
}

static void vec(RiCtx *ctx, RiData data, size_t at, int32_t out[3])
{
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = ri_get_i32(ctx, data, at + axis * 4);
}

static void *entries(RiCtx *ctx, size_t count, size_t size)
{
    return ri_buf_zero(ctx, (count ? count : 1) * size)->data;
}

static MkdsPose *poses(RiCtx *ctx, Section s, int respawn)
{
    MkdsPose *out = (MkdsPose *)entries(ctx, s.count, sizeof(MkdsPose));
    const size_t stride = respawn ? 0x20 : 0x1C;
    for (uint32_t index = 0; index < s.count; ++index)
    {
        const size_t at = index * stride;
        vec(ctx, s.entries, at, out[index].position);
        vec(ctx, s.entries, at + 12, out[index].rotation);
        out[index].enemy_point = out[index].item_point = -1;
        if (respawn)
        {
            out[index].enemy_point = ri_get_i16(ctx, s.entries, at + 24);
            out[index].item_point = ri_get_i16(ctx, s.entries, at + 26);
            out[index].index = ri_get_i16(ctx, s.entries, at + 28);
        }
        else
            out[index].index = ri_get_i16(ctx, s.entries, at + 26);
    }
    return out;
}

static MkdsPath *paths(RiCtx *ctx, Section s, const char *name, const char *tag, size_t point_count)
{
    const unsigned links = memcmp(tag, "MEPA", 4) == 0 ? MKDS_PATH_LINKS : 3;
    MkdsPath *out = (MkdsPath *)entries(ctx, s.count, sizeof(MkdsPath));
    for (uint32_t index = 0; index < s.count; ++index)
    {
        const size_t at = index * (4 + 2 * links + (links == 3 ? 2 : 0));
        MkdsPath *path = &out[index];
        path->start = ri_get_u16(ctx, s.entries, at);
        path->length = ri_get_u16(ctx, s.entries, at + 2);
        for (unsigned k = 0; k < MKDS_PATH_LINKS; ++k)
            path->next[k] = path->previous[k] = -1;
        for (unsigned k = 0; k < links; ++k)
        {
            const int32_t next = (int8_t)ri_get_u8(ctx, s.entries, at + 4 + k);
            const int32_t previous = (int8_t)ri_get_u8(ctx, s.entries, at + 4 + links + k);
            const int32_t limit = links == 3 ? (int32_t)s.count : (int32_t)point_count;
            if (next >= limit || previous >= limit)
                ri_fail(ctx, "%s: %s path %u links to a missing path", name, tag, (unsigned)index);
            path->next[k] = next < 0 ? -1 : next;
            path->previous[k] = previous < 0 ? -1 : previous;
        }
        if (path->length == 0 || path->start + path->length > point_count)
            ri_fail(ctx, "%s: %s path %u spans points %u+%u of %zu", name, tag, (unsigned)index,
                    (unsigned)path->start, (unsigned)path->length, point_count);
    }
    return out;
}

static MkdsPoint *points(RiCtx *ctx, Section s, size_t stride)
{
    MkdsPoint *out = (MkdsPoint *)entries(ctx, s.count, sizeof(MkdsPoint));
    for (uint32_t index = 0; index < s.count; ++index)
    {
        vec(ctx, s.entries, index * stride, out[index].position);
        out[index].radius = ri_get_i32(ctx, s.entries, index * stride + 12);
    }
    return out;
}

MkdsMap mkds_nkm(RiCtx *ctx, RiData file, const char *name)
{
    static const struct
    {
        char tag[5];
        uint32_t stride; /* 0: not read here */
    } known[] = {
        {"OBJI", 0x3C}, {"PATH", 4}, {"POIT", 0x14}, {"STAG", 0}, {"KTPS", 0x1C}, {"KTPJ", 0x20}, {"KTP2", 0},
        {"KTPC", 0x1C},    {"KTPM", 0}, {"CPOI", 0x24}, {"CPAT", 12}, {"IPOI", 0x14}, {"IPAT", 12}, {"EPOI", 0x18},
        {"EPAT", 12},   {"MEPO", 0x18}, {"MEPA", 0x14}, {"AREA", 0}, {"CAME", 0x4C},
    };
    enum { OBJI, PATH, POIT, KTPS = 4, KTPJ, KTPC = 7, CPOI = 9, CPAT, IPOI, IPAT, EPOI, EPAT, MEPO, MEPA, CAME = 18, KNOWN = 19 };
    MkdsMap out;
    memset(&out, 0, sizeof(out));
    if (file.size < 8 || memcmp(ri_at(ctx, file, 0, 4), "NKMD", 4) != 0)
        ri_fail(ctx, "%s is not NKMD map data", name);
    out.version = ri_get_u16(ctx, file, 4);
    const uint32_t header = ri_get_u16(ctx, file, 6);
    if (out.version < 34 || header < 8 || header % 4 || header > file.size)
        ri_fail(ctx, "%s: NKMD version %u header %u is unsupported", name, (unsigned)out.version, (unsigned)header);
    Section found[KNOWN];
    memset(found, 0, sizeof(found));
    int present[KNOWN] = {0};
    out.laps = 3;
    for (unsigned light = 0; light < 4; ++light)
        out.stage.light_colors[light] = 0x7FFFu;
    for (uint32_t at = 8; at < header; at += 4)
    {
        const size_t offset = (size_t)header + ri_get_u32(ctx, file, at);
        const char *tag = (const char *)ri_at(ctx, file, offset, 8);
        unsigned kind = 0;
        while (kind < KNOWN && memcmp(tag, known[kind].tag, 4) != 0)
            ++kind;
        if (kind == KNOWN)
            ri_fail(ctx, "%s: unknown NKMD section %.4s", name, tag);
        if (present[kind]++)
            ri_fail(ctx, "%s: NKMD section %.4s repeats", name, tag);
        if (memcmp(tag, "STAG", 4) == 0)
        {
            out.laps = ri_get_u16(ctx, file, offset + 6);
            MkdsStage *stage = &out.stage;
            stage->course_id = ri_get_u16(ctx, file, offset + 4);
            stage->fog_enabled = ri_get_u8(ctx, file, offset + 9);
            stage->fog_mode = ri_get_u8(ctx, file, offset + 10);
            stage->fog_shift = ri_get_u8(ctx, file, offset + 11);
            stage->fog_distance = ri_get_i32(ctx, file, offset + 0x14);
            stage->fog_color = ri_get_u16(ctx, file, offset + 0x18) & 0x7FFFu;
            for (unsigned light = 0; light < 4; ++light)
                stage->light_colors[light] = ri_get_u16(ctx, file, offset + 0x1C + light * 2) & 0x7FFFu;
            if (stage->fog_mode > 2 || stage->fog_shift > 10)
                ri_fail(ctx, "%s: STAG fog mode %u shift %u is unsupported", name, (unsigned)stage->fog_mode,
                        (unsigned)stage->fog_shift);
        }
        else if (known[kind].stride)
            found[kind] = section(ctx, file, name, offset, known[kind].stride);
    }
    for (unsigned kind = 0; kind < KNOWN; ++kind)
        if (!present[kind])
            found[kind].count = 0; /* absent sections read as empty */

    const Section objects = found[OBJI];
    out.objects = (MkdsObject *)entries(ctx, objects.count, sizeof(MkdsObject));
    out.object_count = objects.count;
    for (uint32_t index = 0; index < objects.count; ++index)
    {
        const size_t at = index * 0x3C;
        MkdsObject *object = &out.objects[index];
        vec(ctx, objects.entries, at, object->position);
        vec(ctx, objects.entries, at + 12, object->rotation);
        vec(ctx, objects.entries, at + 24, object->scale);
        object->id = ri_get_u16(ctx, objects.entries, at + 36);
        object->route = ri_get_i16(ctx, objects.entries, at + 38);
        object->time_trial = ri_get_u16(ctx, objects.entries, at + 58);
        for (unsigned setting = 0; setting < 4; ++setting)
            object->settings[setting] = ri_get_u32(ctx, objects.entries, at + 40 + setting * 4);
    }
    out.starts = poses(ctx, found[KTPS], 0);
    out.start_count = found[KTPS].count;
    out.respawns = poses(ctx, found[KTPJ], 1);
    out.respawn_count = found[KTPJ].count;
    out.cannons = poses(ctx, found[KTPC], 0);
    out.cannon_count = found[KTPC].count;

    const Section checkpoints = found[CPOI];
    out.checkpoints = (MkdsCheckpoint *)entries(ctx, checkpoints.count, sizeof(MkdsCheckpoint));
    out.checkpoint_count = checkpoints.count;
    for (uint32_t index = 0; index < checkpoints.count; ++index)
    {
        const size_t at = index * 0x24;
        MkdsCheckpoint *checkpoint = &out.checkpoints[index];
        checkpoint->left[0] = ri_get_i32(ctx, checkpoints.entries, at);
        checkpoint->left[1] = ri_get_i32(ctx, checkpoints.entries, at + 4);
        checkpoint->right[0] = ri_get_i32(ctx, checkpoints.entries, at + 8);
        checkpoint->right[1] = ri_get_i32(ctx, checkpoints.entries, at + 12);
        checkpoint->goto_section = ri_get_i16(ctx, checkpoints.entries, at + 28);
        checkpoint->start_section = ri_get_i16(ctx, checkpoints.entries, at + 30);
        checkpoint->key = ri_get_i16(ctx, checkpoints.entries, at + 32);
        checkpoint->respawn = (int32_t)ri_get_u8(ctx, checkpoints.entries, at + 34);
        if ((size_t)checkpoint->respawn >= out.respawn_count)
            ri_fail(ctx, "%s: checkpoint %u respawn %d is missing", name, (unsigned)index, (int)checkpoint->respawn);
    }
    out.checkpoint_paths = paths(ctx, found[CPAT], name, "CPAT", out.checkpoint_count);
    out.checkpoint_path_count = found[CPAT].count;
    out.item_points = points(ctx, found[IPOI], 0x14);
    out.item_point_count = found[IPOI].count;
    out.item_paths = paths(ctx, found[IPAT], name, "IPAT", out.item_point_count);
    out.item_path_count = found[IPAT].count;
    const int battle = !present[EPOI] && present[MEPO];
    const Section enemy_points = found[battle ? MEPO : EPOI], enemy_paths = found[battle ? MEPA : EPAT];
    out.enemy_points = points(ctx, enemy_points, 0x18);
    out.enemy_point_count = enemy_points.count;
    out.enemy_paths = paths(ctx, enemy_paths, name, battle ? "MEPA" : "EPAT", out.enemy_point_count);
    out.enemy_path_count = enemy_paths.count;
    out.enemy_links_points = battle;

    out.rail_point_count = found[POIT].count;
    out.rail_points = (int32_t(*)[3])entries(ctx, found[POIT].count, sizeof(*out.rail_points));
    out.rail_durations = (int32_t *)entries(ctx, found[POIT].count, sizeof(*out.rail_durations));
    for (uint32_t index = 0; index < found[POIT].count; ++index)
    {
        vec(ctx, found[POIT].entries, index * 0x14, out.rail_points[index]);
        out.rail_durations[index] = ri_get_i16(ctx, found[POIT].entries, index * 0x14 + 14);
    }
    out.rail_count = found[PATH].count;
    out.rails = (MkdsRail *)entries(ctx, found[PATH].count, sizeof(MkdsRail));
    for (uint32_t index = 0, start = 0; index < found[PATH].count; ++index)
    {
        MkdsRail *rail = &out.rails[index];
        rail->start = start;
        rail->loop = ri_get_u8(ctx, found[PATH].entries, index * 4 + 1);
        rail->length = ri_get_u16(ctx, found[PATH].entries, index * 4 + 2);
        start += rail->length;
        if (rail->length == 0 || start > out.rail_point_count)
            ri_fail(ctx, "%s: PATH %u spans points %u+%u of %zu POIT", name, (unsigned)index, (unsigned)rail->start,
                    (unsigned)rail->length, out.rail_point_count);
    }
    out.camera_count = found[CAME].count;
    out.cameras = (MkdsCamera *)entries(ctx, found[CAME].count, sizeof(MkdsCamera));
    for (uint32_t index = 0; index < found[CAME].count; ++index)
    {
        const size_t at = index * 0x4C;
        MkdsCamera *camera = &out.cameras[index];
        vec(ctx, found[CAME].entries, at, camera->position);
        vec(ctx, found[CAME].entries, at + 0x18, camera->target[0]);
        vec(ctx, found[CAME].entries, at + 0x24, camera->target[1]);
        camera->fov[0] = ri_get_u16(ctx, found[CAME].entries, at + 0x30);
        camera->fov[1] = ri_get_u16(ctx, found[CAME].entries, at + 0x36);
        camera->type = ri_get_u16(ctx, found[CAME].entries, at + 0x3E);
        camera->rail = ri_get_i16(ctx, found[CAME].entries, at + 0x40);
        camera->duration = ri_get_u16(ctx, found[CAME].entries, at + 0x46);
        camera->next = ri_get_i16(ctx, found[CAME].entries, at + 0x48);
        camera->intro = ri_get_u8(ctx, found[CAME].entries, at + 0x4A);
        if (camera->rail >= (int32_t)out.rail_count || camera->next >= (int32_t)found[CAME].count)
            ri_fail(ctx, "%s: CAME %u names a missing PATH %d or camera %d", name, (unsigned)index, (int)camera->rail,
                    (int)camera->next);
    }
    return out;
}
