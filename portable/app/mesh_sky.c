#include "mesh_sky.h"
#include "model_asset.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t signed_word(const uint8_t *p)
{
    uint32_t v = word(p);
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}
static int fail(char *error, size_t cap, uint32_t id, const char *reason)
{
    if (error && cap)
        snprintf(error, cap, "3D sky profile=%u MSK1: %s", id, reason);
    return 0;
}
static int controller_valid(unsigned controller)
{
    return controller <= 7;
}

static int profile_valid(const PortableMeshSkyConfig *c)
{
    if (!c || c->enabled > 1)
        return 0;
    if (!c->enabled)
        return !c->profile_id && !c->revision && !c->render_mask && !c->entry_delay;
    if (!c->profile_id || c->profile_id == UINT32_MAX || !c->revision || !c->render_mask ||
        c->render_mask > 7 || c->entry_delay > 1 || !portable_sky_motion_rules_valid(&c->rules) ||
        !controller_valid(c->rules.controller) || (c->render_mask & ~(uint32_t)c->rules.layer_mask))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        if (c->render_mask & (1u << i))
        {
            if (!c->logical_width[i] || c->logical_width[i] > 4096 || c->radius_q16[i] <= 0 ||
                c->radius_q16[i] > 2048 * 65536)
                return 0;
        }
        else if (c->logical_width[i] || c->radius_q16[i])
            return 0;
    }
    for (unsigned i = 0; i < 1024; ++i)
        if (c->sine[i] < -32768 || c->sine[i] > 32768)
            return 0;
    return 1;
}

static int needs_progress(const PortableMeshSkyConfig *c)
{
    return c->enabled &&
           (c->rules.controller == 2 || c->rules.controller == 4 || c->rules.controller == 5);
}
int portable_mesh_sky_config_valid(const PortableMeshSkyConfig *c)
{
    return profile_valid(c) && portable_mesh_sky_progress_config_valid(&c->progress) &&
           c->progress.enabled == (uint32_t)needs_progress(c) &&
           portable_mesh_palette_config_valid(&c->palette) &&
           c->palette.enabled == (uint32_t)(c->enabled && c->rules.controller == 4);
}

int portable_mesh_sky_decode(const uint8_t *bytes, size_t size, uint32_t id,
                             PortableMeshSkyConfig *out, char *error, size_t cap)
{
    if (!bytes || !out || size != PORTABLE_MESH_SKY_PROFILE_SIZE || memcmp(bytes, "MSK1", 4) ||
        word(bytes + 4) != 1 || word(bytes + 8) != PORTABLE_MESH_SKY_PROFILE_SIZE ||
        word(bytes + 12) != id || word(bytes + 28) != 1024)
        return fail(error, cap, id, "invalid header");
    int32_t r[23];
    for (unsigned i = 0; i < 23; ++i)
        r[i] = signed_word(bytes + 32 + 4 * i);
    if (!controller_valid((unsigned)r[0]))
    {
        if (error && cap)
            snprintf(error, cap, "3D sky profile=%u MSK1: unsupported controller=%d", id,
                     (int)r[0]);
        return 0;
    }
    const int32_t minimum[23] = {0,      0,      0,      0,      0,      0,      0,  0,
                                 -32768, -32768, -32768, -32768, -32768, -32768, -1, 0,
                                 0,      -32768, 0,      0,      0,      0,      0};
    const int32_t maximum[23] = {7,     15,    15,    15,    7,     1,     16,  16,
                                 32767, 32767, 32767, 32767, 32767, 32767, 159, 2,
                                 15,    32767, 2,     3,     2,     2,     2};
    for (unsigned i = 0; i < 23; ++i)
        if (r[i] < minimum[i] || r[i] > maximum[i])
            return fail(error, cap, id, "invalid rule field");
    PortableMeshSkyConfig c = {.enabled = 1,
                               .profile_id = id,
                               .revision = word(bytes + 16),
                               .render_mask = word(bytes + 20),
                               .entry_delay = word(bytes + 24)};
    c.rules.controller = (uint8_t)r[0];
    c.rules.layer_mask = (uint8_t)r[4];
    c.rules.blend_mode = (uint8_t)r[5];
    c.rules.blend_a = (uint8_t)r[6];
    c.rules.blend_b = (uint8_t)r[7];
    c.rules.split_y = (int16_t)r[14];
    c.rules.split_layer = (uint8_t)r[15];
    c.rules.split_yaw_shift = (uint8_t)r[16];
    c.rules.split_x_bias = (int16_t)r[17];
    c.rules.lightning_hide_layer = (uint8_t)r[18];
    c.rules.road_mode = (uint8_t)r[19];
    for (unsigned i = 0; i < 3; ++i)
    {
        c.rules.yaw_shift[i] = (uint8_t)r[1 + i];
        c.rules.x_bias[i] = (int16_t)r[8 + i];
        c.rules.y_bias[i] = (int16_t)r[11 + i];
        c.rules.layer_source[i] = (uint8_t)r[20 + i];
        c.logical_width[i] = word(bytes + 124 + 8 * i);
        c.radius_q16[i] = signed_word(bytes + 128 + 8 * i);
    }
    for (unsigned i = 0; i < 1024; ++i)
        c.sine[i] = signed_word(bytes + 148 + 4 * i);
    if (!profile_valid(&c))
        return fail(error, cap, id, "invalid identity, layer bounds or sine");
    *out = c;
    if (error && cap)
        error[0] = 0;
    return 1;
}

static int binding_error(char *error, size_t cap, uint32_t course, uint32_t profile,
                         uint32_t object, const char *reason)
{
    if (error && cap)
        snprintf(error, cap, "3D course=%u sky profile=%u object=%u: %s", course, profile, object,
                 reason);
    return 0;
}
int portable_mesh_sky_config_open(const PortableCourse3DView *course, PortableMeshSkyConfig *out,
                                  PortableMeshSkyBindings *bindings, char *error, size_t cap)
{
    PortableMeshSkyConfig c = {0};
    PortableMeshSkyBindings b = {{UINT32_MAX, UINT32_MAX, UINT32_MAX}};
    PortableCourse3DMeta meta;
    PortableCourse3DProperty p;
    const uint32_t id = course ? course->id : 0;
    if (!course || !out || !bindings || !portable_course_3d_meta(course, &meta))
        return binding_error(error, cap, id, 0, UINT32_MAX, "invalid configuration or META");
    uint32_t profile = 0;
    if (portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                         "mksc:sky-motion", &p))
    {
        PortableAssetSectionView section;
        if (p.type != PORTABLE_COURSE_3D_PROPERTY_ASSET ||
            p.value.asset.kind != PORTABLE_ASSET_SECTION_MESH_SKY_RULES)
            return binding_error(error, cap, id, 0, UINT32_MAX, "expected MSK1 asset");
        profile = p.value.asset.id;
        if (!course->pack ||
            portable_asset_pack_find_section(course->pack, PORTABLE_ASSET_SECTION_MESH_SKY_RULES,
                                             profile, &section) != PORTABLE_ASSET_PACK_OK ||
            section.version != 1 || section.size != PORTABLE_MESH_SKY_PROFILE_SIZE)
            return binding_error(error, cap, id, profile, UINT32_MAX,
                                 "missing/unsupported MSK1 asset");
        char reason[160];
        if (!portable_mesh_sky_decode(section.bytes, (size_t)section.size, profile, &c, reason,
                                      sizeof reason))
            return binding_error(error, cap, id, profile, UINT32_MAX, reason);
    }
    if (portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                         "mksc:palette-motion", &p))
    {
        if (!c.enabled || c.rules.controller != PORTABLE_SKY_MOTION_SUNSET)
            return binding_error(error, cap, id, profile, UINT32_MAX,
                                 "orphan palette-motion without sunset sky");
        if (p.type != PORTABLE_COURSE_3D_PROPERTY_ASSET ||
            p.value.asset.kind != PORTABLE_MESH_PALETTE_RULES_KIND)
            return binding_error(error, cap, id, profile, UINT32_MAX, "expected MPA1 asset");
        PortableAssetSectionView section;
        if (!course->pack ||
            portable_asset_pack_find_section(course->pack, PORTABLE_MESH_PALETTE_RULES_KIND,
                                             p.value.asset.id,
                                             &section) != PORTABLE_ASSET_PACK_OK ||
            section.version != 1)
            return binding_error(error, cap, id, profile, UINT32_MAX,
                                 "missing/unsupported MPA1 asset");
        char reason[192];
        if (!portable_mesh_palette_decode(section.bytes, (size_t)section.size, p.value.asset.id,
                                          &c.palette, reason, sizeof reason))
            return binding_error(error, cap, id, profile, UINT32_MAX, reason);
    }
    if (c.enabled && c.rules.controller == PORTABLE_SKY_MOTION_SUNSET && !c.palette.enabled)
        return binding_error(error, cap, id, profile, UINT32_MAX,
                             "sunset sky requires palette-motion");
    const PortableCourse3DTable *objects =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
    if (objects &&
        (objects->count > 4096 || objects->stride != 64 || (objects->count && !objects->bytes)))
        return binding_error(error, cap, id, profile, UINT32_MAX, "invalid OBJS");
    uint32_t seen = 0;
    for (uint32_t i = 0; objects && i < objects->count; ++i)
    {
        const uint8_t *row = objects->bytes + 64 * i;
        const uint32_t first = word(row + 56), count = word(row + 60);
        if (!portable_course_3d_property_find(course, first, count, "mksc:sky-layer", &p))
            continue;
        if (!c.enabled || p.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || p.value.integer < 0 ||
            p.value.integer > 2)
            return binding_error(error, cap, id, profile, i, "unbound/invalid logical layer");
        const unsigned layer = (unsigned)p.value.integer;
        if (!(c.render_mask & (1u << layer)) || (seen & (1u << layer)))
            return binding_error(error, cap, id, profile, i, "disabled/duplicate logical layer");
        PortableModelView model;
        if (word(row + 4) != PORTABLE_ASSET_SECTION_MODEL_MESH || word(row + 8) == UINT32_MAX ||
            !course->pack || !portable_model_mesh_open(course->pack, word(row + 8), &model))
            return binding_error(error, cap, id, profile, i,
                                 "missing/unsupported ordinary sky model");
        if (portable_course_3d_property_find(course, first, count, "mksc:controller", &p))
            return binding_error(error, cap, id, profile, i,
                                 "sky layer cannot own an actor controller");
        if (portable_course_3d_property_find(course, first, count, "mksc:billboard", &p) &&
            (p.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN || p.value.boolean))
            return binding_error(error, cap, id, profile, i, "sky model cannot be a billboard");
        b.object_indices[layer] = i;
        seen |= 1u << layer;
    }
    if (seen != c.render_mask)
        return binding_error(error, cap, id, profile, UINT32_MAX, "missing logical sky model");
    if (needs_progress(&c))
    {
        char reason[192];
        if (!portable_mesh_sky_progress_open(course, &c.progress, reason, sizeof reason))
            return binding_error(error, cap, id, profile, UINT32_MAX, reason);
        if (!c.progress.enabled)
            return binding_error(error, cap, id, profile, UINT32_MAX,
                                 "missing source progress provenance");
    }
    if (!portable_mesh_sky_config_valid(&c))
        return binding_error(error, cap, id, profile, UINT32_MAX,
                             "invalid bound sky configuration");
    *out = c;
    *bindings = b;
    if (error && cap)
        error[0] = 0;
    return 1;
}

static int sunset_valid(const PortableSkyMotion *m)
{
    if (m->phase > 6 || m->transition_clock || m->transition_counter < 0 ||
        m->transition_counter > 3 || m->horizon_y < 32 || m->horizon_y > 184 ||
        m->horizon_y_q16 < 32 * 65536 || m->horizon_y_q16 > 184 * 65536 ||
        m->horizon_y != portable_sky_motion_floor(m->horizon_y_q16, 16))
        return 0;
    switch (m->phase)
    {
    case 0:
        return !m->elapsed && !m->transition_counter && m->horizon_y == 184 &&
               m->horizon_y_q16 == 184 * 65536;
    case 1:
        return m->elapsed <= 32 && m->horizon_y >= 144 && m->horizon_y_q16 >= 144 * 65536;
    case 2:
        return !m->elapsed && !m->transition_counter && m->horizon_y == 144 &&
               m->horizon_y_q16 == 144 * 65536;
    case 3:
        return m->elapsed <= 128 && !m->transition_counter && m->horizon_y >= 48 &&
               m->horizon_y <= 144 && m->horizon_y_q16 >= 48 * 65536 &&
               m->horizon_y_q16 <= 144 * 65536;
    case 4:
        return m->elapsed >= 32 && m->elapsed <= 64 && m->horizon_y == 48 &&
               m->horizon_y_q16 == 48 * 65536;
    case 5:
        return m->elapsed <= 64 && !m->transition_counter && m->horizon_y <= 48 &&
               m->horizon_y_q16 <= 48 * 65536;
    default:
        return m->elapsed == 65 && !m->transition_counter && m->horizon_y == 32 &&
               m->horizon_y_q16 == 32 * 65536;
    }
}
static int motion_valid(const PortableMeshSkyConfig *c, const PortableSkyMotion *m)
{
    if (m->initialized != 1 || m->elapsed < 0 || m->elapsed > 7201 || m->prior_vertical_offset ||
        m->prior_vertical_offset_q16)
        return 0;
    if (c->rules.controller == PORTABLE_SKY_MOTION_SUNSET)
    {
        if (!sunset_valid(m))
            return 0;
    }
    else if (m->transition_counter ||
             m->phase > (c->rules.controller == 2                                 ? 2
                         : (c->rules.controller == 3 || c->rules.controller == 5) ? 1
                                                                                  : 0) ||
             (c->rules.controller != 2 && m->transition_clock) || m->horizon_y != 184 ||
             m->horizon_y_q16 != 184 * 65536)
        return 0;
    if (c->rules.controller == 2 && ((m->phase < 2 && (m->elapsed > 1024 || m->transition_clock)) ||
                                     (m->phase == 2 && m->elapsed != 1025)))
        return 0;
    if (c->rules.controller == 5 && m->elapsed)
        return 0;
    const PortableSkyMotionSnapshot *s = &m->snapshot;
    const int split = c->rules.controller == PORTABLE_SKY_MOTION_SUNSET && m->phase > 4;
    if (s->initialized != 1 || s->precise != 1 || s->draw_road_line_valid != 1 ||
        s->road_line != 74 || s->draw_road_line != 74 || s->draw_road_line_q16 != 74 * 65536 ||
        s->layer_mask != c->rules.layer_mask || s->blend_mode != c->rules.blend_mode ||
        s->blend_a != c->rules.blend_a || s->blend_b != c->rules.blend_b ||
        s->road_mode != c->rules.road_mode || s->split_y != (split ? 24 : c->rules.split_y) ||
        s->split_layer != (split ? 2 : c->rules.split_layer) || s->split_vertical != split ||
        s->split_scroll_y != (split ? 46 : 0) || s->split_scroll_y_q16 != (split ? 46 * 65536 : 0))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (s->layer_source[i] != c->rules.layer_source[i])
            return 0;
    return 1;
}

int portable_mesh_sky_state_valid(const PortableMeshSkyConfig *c, const PortableMeshSky *w)
{
    if (!c || !w || w->enabled != c->enabled || w->progress_enabled != c->progress.enabled ||
        w->racer_count > 8 || !portable_mesh_palette_state_valid(&c->palette, &w->palette))
        return 0;
    if (!w->enabled)
        return !w->clock && !w->profile_id && !w->revision && !w->motion.initialized &&
               !w->racer_count;
    if (w->profile_id != c->profile_id || w->revision != c->revision ||
        !motion_valid(c, &w->motion))
        return 0;
    if (!w->progress_enabled)
        return !w->racer_count;
    for (unsigned i = 0; i < 8; ++i)
        if (!portable_mesh_sky_progress_valid(&c->progress, &w->progress[i]) ||
            (i && !motion_valid(c, &w->other_motion[i - 1])))
            return 0;
    return 1;
}
int portable_mesh_sky_valid(const PortableMeshSkyConfig *c, const PortableMeshSky *w)
{
    return portable_mesh_sky_config_valid(c) && portable_mesh_sky_state_valid(c, w);
}
static PortableSkyMotionInputs inputs(const PortableMeshSkyConfig *c, uint32_t clock)
{
    const PortableSkyMotionInputs in = {.clock = clock,
                                        .scene_flags_7fe = (uint16_t)(c->entry_delay ? 256 : 0),
                                        .precise_vertical_offset = 1};
    return in;
}
int portable_mesh_sky_init(const PortableMeshSkyConfig *c, PortableMeshSky *out)
{
    if (!out || !portable_mesh_sky_config_valid(c))
        return 0;
    PortableMeshSky w = {0};
    if (c->enabled)
    {
        w.enabled = 1;
        w.profile_id = c->profile_id;
        w.revision = c->revision;
        const PortableSkyMotionInputs in = inputs(c, 0);
        if (portable_sky_motion_init(&w.motion, &c->rules, &in, c->sine, 1024) != 1)
            return 0;
        if (c->progress.enabled)
        {
            w.progress_enabled = 1;
            for (unsigned i = 0; i < 8; ++i)
            {
                if (!portable_mesh_sky_progress_init(&c->progress, &w.progress[i]))
                    return 0;
                if (i)
                    w.other_motion[i - 1] = w.motion;
            }
        }
    }
    if (!portable_mesh_palette_init(&c->palette, &w.palette, NULL, 0))
        return 0;
    if (!portable_mesh_sky_state_valid(c, &w))
        return 0;
    *out = w;
    return 1;
}
int portable_mesh_sky_advance_racers(const PortableMeshSkyConfig *c, PortableMeshSky *out,
                                     const PortableMeshSky *before,
                                     const PortableMeshSkyProgressInput *poses, unsigned count,
                                     unsigned leader)
{
    if (!out || !portable_mesh_sky_valid(c, before))
        return 0;
    PortableMeshSky next = *before;
    if (c->enabled)
    {
        ++next.clock;
        PortableSkyMotionInputs in = inputs(c, next.clock);
        if (c->progress.enabled)
        {
            if (!poses || !count || count > 8 || leader >= count ||
                (before->racer_count && before->racer_count != count))
                return 0;
            next.racer_count = count;
            for (unsigned i = 0; i < count; ++i)
                if (!portable_mesh_sky_progress_update(&c->progress, &next.progress[i],
                                                       &before->progress[i], &poses[i]))
                    return 0;
            in.leader_progress = next.progress[leader].high_water;
            in.leader_route_index = next.progress[leader].route_index;
            in.leader_route_wrap = next.progress[leader].route_wrap;
            for (unsigned i = 0; i < count; ++i)
            {
                in.selected_progress = next.progress[i].high_water;
                PortableSkyMotion *motion = i ? &next.other_motion[i - 1] : &next.motion;
                if (portable_sky_motion_tick(motion, &c->rules, &in, c->sine, 1024) != 1)
                    return 0;
            }
        }
        else if (portable_sky_motion_tick(&next.motion, &c->rules, &in, c->sine, 1024) != 1)
            return 0;
    }
    if (c->palette.enabled)
    {
        const PortableAmbientInputs ambient = {
            .clock = next.clock,
            .selected_progress = next.progress[0].high_water,
            .leader_progress = next.progress[leader].high_water,
            .leader_route_wrap = next.progress[leader].route_wrap,
            .sky_phase_before = before->motion.phase,
            .sky_phase_after = next.motion.phase,
            .sky_elapsed_before = before->motion.elapsed,
            .sky_counter_before = before->motion.transition_counter};
        if (!portable_mesh_palette_tick_validated_config(
                &c->palette, &next.palette, &before->palette, &ambient, c->sine, 1024, NULL, 0))
            return 0;
    }
    if (!portable_mesh_sky_state_valid(c, &next))
        return 0;
    *out = next;
    return 1;
}
int portable_mesh_sky_advance(const PortableMeshSkyConfig *c, PortableMeshSky *out,
                              const PortableMeshSky *before)
{
    return portable_mesh_sky_advance_racers(c, out, before, NULL, 0, 0);
}
int portable_mesh_sky_snapshot_racer(const PortableMeshSkyConfig *c, const PortableMeshSky *w,
                                     unsigned racer, uint16_t yaw, PortableSkyMotionSnapshot *out)
{
    if (!out || !portable_mesh_sky_valid(c, w) || !c->enabled || racer >= 8 ||
        (w->progress_enabled && w->racer_count && racer >= w->racer_count))
        return 0;
    PortableSkyMotionSnapshot snapshot =
        (w->progress_enabled && racer) ? w->other_motion[racer - 1].snapshot : w->motion.snapshot;
    const int32_t angle = portable_sky_motion_signed16(yaw);
    for (unsigned i = 0; i < 3; ++i)
    {
        const int multiplier = c->rules.controller == 2 ? (i == 0 ? 0 : i == 1 ? 2 : 1) : 1;
        snapshot.scroll_x[i] = portable_sky_motion_wrap16(
            (int32_t)snapshot.scroll_x[i] +
            multiplier * (angle / (INT32_C(1) << c->rules.yaw_shift[i])));
        snapshot.scroll_x_q16[i] = portable_sky_motion_wrap_q16(
            (int64_t)snapshot.scroll_x_q16[i] +
            multiplier * ((int64_t)angle * 65536 / (INT64_C(1) << c->rules.yaw_shift[i])));
    }
    snapshot.split_scroll_x = portable_sky_motion_wrap16(
        (int32_t)snapshot.split_scroll_x + angle / (INT32_C(1) << c->rules.split_yaw_shift));
    snapshot.split_scroll_x_q16 = portable_sky_motion_wrap_q16(
        (int64_t)snapshot.split_scroll_x_q16 +
        (int64_t)angle * 65536 / (INT64_C(1) << c->rules.split_yaw_shift));
    *out = snapshot;
    return 1;
}
int portable_mesh_sky_snapshot(const PortableMeshSkyConfig *c, const PortableMeshSky *w,
                               uint16_t yaw, PortableSkyMotionSnapshot *out)
{
    return portable_mesh_sky_snapshot_racer(c, w, 0, yaw, out);
}
static void hash_word(uint64_t *hash, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        *hash ^= (value >> (8 * i)) & 255u;
        *hash *= UINT64_C(1099511628211);
    }
}
static uint64_t hash_motion(const PortableSkyMotion *m, uint64_t hash)
{
#define H(v) hash_word(&hash, (uint32_t)(v))
    H(m->initialized);
    H(m->phase);
    H(m->prior_vertical_offset);
    H(m->prior_vertical_offset_q16);
    H(m->elapsed);
    H(m->transition_counter);
    H(m->horizon_y);
    H(m->horizon_y_q16);
    H(m->transition_clock);
    const PortableSkyMotionSnapshot *s = &m->snapshot;
    for (unsigned i = 0; i < 3; ++i)
    {
        H(m->raw_scroll_y[i]);
        H(m->raw_scroll_y_q16[i]);
        H(s->scroll_x[i]);
        H(s->scroll_y[i]);
        H(s->scroll_x_q16[i]);
        H(s->scroll_y_q16[i]);
        H(s->layer_source[i]);
    }
    H(s->layer_mask);
    H(s->blend_mode);
    H(s->blend_a);
    H(s->blend_b);
    H(s->split_y);
    H(s->split_layer);
    H(s->split_scroll_x);
    H(s->split_scroll_y);
    H(s->split_vertical);
    H(s->split_scroll_x_q16);
    H(s->split_scroll_y_q16);
    H(s->road_line);
    H(s->road_mode);
    H(s->initialized);
    H(s->precise);
    H(s->draw_road_line);
    H(s->draw_road_line_q16);
    H(s->draw_road_line_valid);
#undef H
    return hash;
}
uint64_t portable_mesh_sky_hash(const PortableMeshSky *w, uint64_t hash)
{
    if (!w || !w->enabled)
        return hash;
#define H(v) hash_word(&hash, (uint32_t)(v))
    H(UINT32_C(0x31594b53));
    H(w->profile_id);
    H(w->revision);
    H(w->clock);
    hash = hash_motion(&w->motion, hash);
    if (w->progress_enabled)
    {
        H(UINT32_C(0x31504753));
        H(w->racer_count);
        for (unsigned i = 0; i < 8; ++i)
        {
            H(w->progress[i].high_water);
            H(w->progress[i].route_index);
            H(w->progress[i].route_wrap);
            if (i)
                hash = hash_motion(&w->other_motion[i - 1], hash);
        }
    }
#undef H
    return portable_mesh_palette_hash(&w->palette, hash);
}

uint64_t portable_mesh_sky_checksum(const PortableMeshSkyConfig *c, const PortableMeshSky *w)
{
    if (!portable_mesh_sky_valid(c, w) || !c->enabled)
        return 0;
    return portable_mesh_sky_hash(w, UINT64_C(14695981039346656037));
}
