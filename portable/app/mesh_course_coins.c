#include "mesh_course_coins.h"
#include "fixed_3d.h"
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
static int fail(char *error, size_t cap, uint32_t course, const char *why)
{
    if (error && cap)
        snprintf(error, cap, "3D course=%u coins: %s", course, why);
    return 0;
}
static uint64_t placement_mask(unsigned count)
{
    return count == 64 ? UINT64_MAX : count ? ((UINT64_C(1) << count) - 1) : 0;
}
static int same_coin(const PortableMeshCourseCoin *a, const PortableMeshCourseCoin *b)
{
    return a->model_id == b->model_id &&
           !memcmp(a->position_q16, b->position_q16, sizeof a->position_q16) &&
           !memcmp(a->half_q16, b->half_q16, sizeof a->half_q16);
}
int portable_mesh_course_coins_config_valid(const PortableMeshCourseCoinsConfig *c)
{
    if (!c || c->enabled > 1 || c->placement_count > 64)
        return 0;
    if (!c->enabled)
        return !c->profile_id && !c->revision && !c->modes && !c->cap && !c->initial &&
               !c->placement_count;
    if (!c->profile_id || c->profile_id == UINT32_MAX || !c->revision || !c->modes ||
        (c->modes & ~31u) || !c->cap || c->cap > 99 || c->initial > c->cap)
        return 0;
    for (unsigned i = 0; i < 100; ++i)
        if (c->ratios_q16[i] < 3277 || c->ratios_q16[i] > 65536)
            return 0;
    for (unsigned i = 0; i < 5; ++i)
        if (c->loss[i] > 99)
            return 0;
    for (unsigned i = 0; i < c->placement_count; ++i)
    {
        const PortableMeshCourseCoin *p = &c->placements[i];
        if (!p->id || p->id > INT32_MAX || p->object_index >= 4096 || p->model_id == UINT32_MAX)
            return 0;
        for (unsigned j = 0; j < 3; ++j)
            if (p->position_q16[j] < -268435456 || p->position_q16[j] > 268435456 ||
                p->half_q16[j] <= 0 || p->half_q16[j] > 16 * 65536)
                return 0;
        for (unsigned j = 0; j < i; ++j)
            if (p->id == c->placements[j].id && !same_coin(p, &c->placements[j]))
                return 0;
    }
    return 1;
}
int portable_mesh_course_coins_config_open(const PortableCourse3DView *course,
                                           PortableMeshCourseCoinsConfig *out, char *error,
                                           size_t cap)
{
    if (!course || !out)
        return fail(error, cap, course ? course->id : 0, "invalid configuration arguments");
    PortableMeshCourseCoinsConfig c = {.course_id = course->id};
    PortableCourse3DMeta meta;
    PortableCourse3DProperty p;
    if (!portable_course_3d_meta(course, &meta))
        return fail(error, cap, course->id, "invalid META");
    if (portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                         "mksc:coin-rules", &p))
    {
        PortableAssetSectionView s;
        if (p.type != PORTABLE_COURSE_3D_PROPERTY_ASSET || p.value.asset.kind != 105 ||
            !course->pack ||
            portable_asset_pack_find_section(course->pack, 105, p.value.asset.id, &s) !=
                PORTABLE_ASSET_PACK_OK ||
            s.version != 1 || s.size != 452 || !s.bytes || memcmp(s.bytes, "MCR1", 4) ||
            word(s.bytes + 4) != 1 || word(s.bytes + 8) != 452 ||
            word(s.bytes + 12) != s.content_id)
            return fail(error, cap, course->id, "invalid MCR1 rules");
        c.enabled = 1;
        c.profile_id = word(s.bytes + 12);
        c.revision = word(s.bytes + 16);
        c.modes = word(s.bytes + 20);
        c.cap = word(s.bytes + 24);
        c.initial = word(s.bytes + 28);
        for (unsigned i = 0; i < 100; ++i)
        {
            uint32_t v = word(s.bytes + 32 + 4 * i);
            if (v > 65536)
                return fail(error, cap, course->id, "invalid MCR1 ratio");
            c.ratios_q16[i] = (int32_t)v;
        }
        for (unsigned i = 0; i < 5; ++i)
        {
            uint32_t v = word(s.bytes + 432 + 4 * i);
            if (v > 99)
                return fail(error, cap, course->id, "invalid MCR1 loss");
            c.loss[i] = (uint8_t)v;
        }
    }
    const PortableCourse3DTable *objects =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
    if (objects &&
        (objects->count > 4096 || objects->stride != 64 || (objects->count && !objects->bytes)))
        return fail(error, cap, course->id, "invalid OBJS");
    for (unsigned i = 0; objects && i < objects->count; ++i)
    {
        const uint8_t *row = objects->bytes + 64 * i;
        const uint32_t first = word(row + 56), count = word(row + 60);
        if (!portable_course_3d_property_find(course, first, count, "mksc:controller", &p) ||
            p.type != PORTABLE_COURSE_3D_PROPERTY_STRING || !p.value.string ||
            strcmp(p.value.string, "coins"))
        {
            if (word(row) == UINT32_C(0x20000001))
            {
                if (error && cap)
                    snprintf(error, cap, "3D course=%u object=%u coins: missing/invalid controller",
                             course->id, i);
                return 0;
            }
            continue;
        }
        const char *why = NULL;
        if (!c.enabled)
            why = "coin declaration without MCR1 rules";
        else if (c.placement_count == 64)
            why = "placement budget exceeded";
        else if (word(row) != UINT32_C(0x20000001) || word(row + 4) != 96 ||
                 word(row + 8) == UINT32_MAX)
            why = "invalid coin class/model";
        else if (!portable_course_3d_property_find(course, first, count, "mksc:controller-version",
                                                   &p) ||
                 p.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || p.value.integer != 1)
            why = "unsupported controller version";
        if (why)
        {
            if (error && cap)
                snprintf(error, cap, "3D course=%u object=%u coins: %s", course->id, i, why);
            return 0;
        }
        PortableMeshCourseCoin *coin = &c.placements[c.placement_count];
        coin->object_index = i;
        coin->model_id = word(row + 8);
        PortableAssetSectionView model;
        if (!course->pack || portable_asset_pack_find_section(course->pack, 96, coin->model_id,
                                                              &model) != PORTABLE_ASSET_PACK_OK)
            return fail(error, cap, course->id, "missing coin model");
        if (!portable_course_3d_property_find(course, first, count, "coin:id", &p) ||
            p.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || p.value.integer <= 0)
            return fail(error, cap, course->id, "invalid coin ID");
        coin->id = (uint32_t)p.value.integer;
        const char *keys[3] = {"coin:half-x", "coin:half-y", "coin:half-z"};
        for (unsigned j = 0; j < 3; ++j)
        {
            coin->position_q16[j] = signed_word(row + 12 + 4 * j);
            if (!portable_course_3d_property_find(course, first, count, keys[j], &p) ||
                p.type != PORTABLE_COURSE_3D_PROPERTY_FIXED || p.value.fixed_q16 <= 0 ||
                p.value.fixed_q16 > 16 * 65536)
                return fail(error, cap, course->id, "invalid coin half extent");
            coin->half_q16[j] = p.value.fixed_q16;
        }
        ++c.placement_count;
    }
    if (!portable_mesh_course_coins_config_valid(&c))
        return fail(error, cap, course->id, "invalid rules/placements or conflicting duplicate ID");
    *out = c;
    return 1;
}
int portable_mesh_course_coins_valid(const PortableMeshCourseCoinsConfig *c,
                                     const PortableMeshCourseCoins *w)
{
    if (!portable_mesh_course_coins_config_valid(c) || !w || w->enabled != c->enabled ||
        (w->available & ~placement_mask(c->placement_count)))
        return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (w->count[i] > (c->enabled ? c->cap : 0) ||
            w->contact_grace[i] > (c->enabled ? 8u : 0u) ||
            (c->enabled
                 ? w->previous_body_scale_q16[i] < 3277 || w->previous_body_scale_q16[i] > 65536
                 : w->previous_body_scale_q16[i] != 0))
            return 0;
    for (unsigned i = 0; i < c->placement_count; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (c->placements[i].id == c->placements[j].id &&
                ((w->available >> i) & 1) != ((w->available >> j) & 1))
                return 0;
    return 1;
}
int portable_mesh_course_coins_init(const PortableMeshCourseCoinsConfig *c,
                                    PortableMeshCourseCoins *out)
{
    if (!out || !portable_mesh_course_coins_config_valid(c))
        return 0;
    PortableMeshCourseCoins w = {.available = placement_mask(c->placement_count),
                                 .enabled = (uint8_t)c->enabled};
    if (c->enabled)
    {
        memset(w.count, (int)c->initial, sizeof w.count);
        for (unsigned i = 0; i < 8; ++i)
            w.previous_body_scale_q16[i] = 65536;
    }
    *out = w;
    return 1;
}
int portable_mesh_course_coins_advance(const PortableMeshCourseCoinsConfig *c,
                                       PortableMeshCourseCoins *w)
{
    if (!portable_mesh_course_coins_valid(c, w))
        return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (w->contact_grace[i])
            --w->contact_grace[i];
    return 1;
}
static int valid_mode(uint32_t mode)
{
    return mode && !(mode & ~31u) && !(mode & (mode - 1));
}
int portable_mesh_course_coins_effect(const PortableMeshCourseCoinsConfig *c,
                                      const PortableMeshCourseCoins *w, uint32_t mode,
                                      unsigned racer, PortableMeshKartEffects *out)
{
    if (!out || racer >= 8 || !valid_mode(mode) || !portable_mesh_course_coins_valid(c, w) ||
        out->speed_ratio_q16 < 3277 || out->speed_ratio_q16 > 65536 || out->boost_active > 1)
        return 0;
    if (c->enabled && (mode & c->modes) && !out->boost_active && !out->star &&
        out->speed_ratio_q16 > c->ratios_q16[w->count[racer]])
        out->speed_ratio_q16 = c->ratios_q16[w->count[racer]];
    return 1;
}
typedef struct
{
    int64_t n, d;
} Fraction;
static int compare(Fraction a, Fraction b)
{
    int64_t x = a.n * b.d, y = b.n * a.d;
    return x < y ? -1 : x > y;
}
/* Rational slab intervals retain exact earliest entry order, including a very
 * short diagonal corner crossing. Products stay below (2*4096m+16m)^2 Q32. */
static int swept(const int64_t from[3], const int64_t to[3], const PortableMeshCourseCoin *coin,
                 Fraction *entry)
{
    Fraction low = {0, 1}, high = {1, 1};
    for (unsigned j = 0; j < 3; ++j)
    {
        int64_t a = (int64_t)coin->position_q16[j] - coin->half_q16[j],
                b = (int64_t)coin->position_q16[j] + coin->half_q16[j], d = to[j] - from[j];
        if (!d)
        {
            if (from[j] < a || from[j] > b)
                return 0;
            continue;
        }
        Fraction begin, end;
        if (d > 0)
        {
            begin = (Fraction){a - from[j], d};
            end = (Fraction){b - from[j], d};
        }
        else
        {
            begin = (Fraction){from[j] - b, -d};
            end = (Fraction){from[j] - a, -d};
        }
        if (compare(begin, low) > 0)
            low = begin;
        if (compare(end, high) < 0)
            high = end;
        if (compare(low, high) > 0)
            return 0;
    }
    *entry = low;
    return 1;
}
static void losses(const PortableMeshCourseCoinsConfig *c, PortableMeshCourseCoins *w,
                   const uint8_t requests[5][8], unsigned count,
                   PortableMeshCourseCoinEvents *events)
{
    for (unsigned cause = 0; cause < 5; ++cause)
        for (unsigned racer = 0; racer < 8; ++racer)
        {
            if (racer >= count)
                break;
            if (!requests[cause][racer] ||
                (cause == PORTABLE_MESH_COIN_CONTACT && w->contact_grace[racer]))
                continue;
            unsigned amount =
                (cause == PORTABLE_MESH_COIN_CONTACT ? 1u : (unsigned)requests[cause][racer]) *
                c->loss[cause];
            if (amount > w->count[racer])
                amount = w->count[racer];
            if (amount)
            {
                w->count[racer] -= (uint8_t)amount;
                events->lost[racer] += (uint8_t)amount;
                events->causes[racer] |= (uint8_t)(1u << cause);
            }
            if (cause == PORTABLE_MESH_COIN_CONTACT)
                w->contact_grace[racer] = 8;
        }
}
int portable_mesh_course_coins_resolve(const PortableMeshCourseCoinsConfig *c,
                                       PortableMeshCourseCoins *world, uint32_t mode,
                                       const PortableMeshKartRules *rules, const int32_t scales[8],
                                       const PortableMeshRacerState *before,
                                       const PortableMeshRacerState *after, size_t count,
                                       uint8_t eligible,
                                       const PortableMeshCourseCoinRequests *requests,
                                       PortableMeshCourseCoinEvents *out, char *error, size_t cap)
{
    if (!out || !requests || !before || !after || !count || count > 8 || !valid_mode(mode) ||
        (eligible & ~((1u << count) - 1)) || !portable_mesh_course_coins_valid(c, world) ||
        !portable_mesh_kart_rules_valid(rules))
        return fail(error, cap, c ? c->course_id : 0, "invalid resolve owner");
    int64_t feet_before[8][3], feet_after[8][3];
    for (unsigned i = 0; i < count; ++i)
    {
        int32_t scale = scales ? scales[i] : 65536;
        if (scale < 3277 || scale > 65536 || !portable_mesh_kart_state_valid(&before[i].kart) ||
            !portable_mesh_kart_state_valid(&after[i].kart) ||
            before[i].phase > PORTABLE_MESH_RACER_FINISHED ||
            after[i].phase > PORTABLE_MESH_RACER_FINISHED)
            return fail(error, cap, c->course_id, "invalid racer/body scale");
        /* Match core kart_shape(): each authored dimension is truncated
         * independently before their ground clearance is added. */
        const int32_t previous = c->enabled ? world->previous_body_scale_q16[i] : 65536;
        const int64_t before_clearance =
            (int64_t)portable_fixed_3d_mul(rules->kart_radius_q16, previous) +
            portable_fixed_3d_mul(rules->ride_height_q16, previous);
        const int64_t after_clearance =
            (int64_t)portable_fixed_3d_mul(rules->kart_radius_q16, scale) +
            portable_fixed_3d_mul(rules->ride_height_q16, scale);
        for (unsigned j = 0; j < 3; ++j)
        {
            feet_before[i][j] = (int64_t)before[i].kart.position_q16[j] -
                                before_clearance * before[i].kart.up_q30[j] / 1073741824;
            feet_after[i][j] = (int64_t)after[i].kart.position_q16[j] -
                               after_clearance * after[i].kart.up_q30[j] / 1073741824;
        }
    }
    PortableMeshCourseCoins w = *world;
    PortableMeshCourseCoinEvents events = {0};
    if (c->enabled && (mode & c->modes))
    {
        memcpy(events.before, w.count, 8);
        losses(c, &w, requests->pre, (unsigned)count, &events);
        for (unsigned i = 0; i < count; ++i)
        {
            if (!(eligible & (1u << i)) || w.count[i] >= c->cap ||
                before[i].phase != PORTABLE_MESH_RACER_RACING ||
                after[i].phase != PORTABLE_MESH_RACER_RACING || !before[i].kart.grounded ||
                !after[i].kart.grounded || before[i].respawn_grace || after[i].respawn_grace ||
                before[i].kart.respawn_ticks || after[i].kart.respawn_ticks)
                continue;
            unsigned best = 64;
            Fraction earliest = {1, 1};
            for (unsigned j = 0; j < c->placement_count; ++j)
            {
                Fraction entry;
                if (!(w.available & (UINT64_C(1) << j)) ||
                    !swept(feet_before[i], feet_after[i], &c->placements[j], &entry))
                    continue;
                if (best == 64 || compare(entry, earliest) < 0)
                {
                    best = j;
                    earliest = entry;
                }
            }
            if (best == 64)
                continue;
            uint64_t removed = 0;
            for (unsigned j = 0; j < c->placement_count; ++j)
                if (c->placements[j].id == c->placements[best].id)
                    removed |= UINT64_C(1) << j;
            w.available &= ~removed;
            events.picked[i] = removed;
            ++w.count[i];
        }
        losses(c, &w, requests->post, (unsigned)count, &events);
        memcpy(events.after, w.count, 8);
    }
    if (c->enabled)
        for (unsigned i = 0; i < count; ++i)
            w.previous_body_scale_q16[i] = scales ? scales[i] : 65536;
    *world = w;
    *out = events;
    return 1;
}
static void hash_byte(uint64_t *h, uint8_t b)
{
    *h ^= b;
    *h *= UINT64_C(1099511628211);
}
uint64_t portable_mesh_course_coins_hash(const PortableMeshCourseCoins *w, uint64_t hash)
{
    if (!w || w->enabled > 1)
        return 0;
    if (!w->enabled)
    {
        if (w->available)
            return 0;
        for (unsigned i = 0; i < 8; ++i)
            if (w->count[i] || w->contact_grace[i] || w->previous_body_scale_q16[i])
                return 0;
        return hash;
    }
    for (unsigned i = 0; i < 8; ++i)
        if (w->count[i] > 99 || w->contact_grace[i] > 8 || w->previous_body_scale_q16[i] < 3277 ||
            w->previous_body_scale_q16[i] > 65536)
            return 0;
    const uint8_t tag[4] = {'C', 'O', 'I', '1'};
    for (unsigned i = 0; i < 4; ++i)
        hash_byte(&hash, tag[i]);
    hash_byte(&hash, w->enabled);
    for (unsigned i = 0; i < 8; ++i)
        hash_byte(&hash, (uint8_t)(w->available >> (8 * i)));
    for (unsigned i = 0; i < 8; ++i)
        hash_byte(&hash, w->count[i]);
    for (unsigned i = 0; i < 8; ++i)
        hash_byte(&hash, w->contact_grace[i]);
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned byte = 0; byte < 4; ++byte)
            hash_byte(&hash, (uint8_t)((uint32_t)w->previous_body_scale_q16[i] >> (8 * byte)));
    return hash;
}
