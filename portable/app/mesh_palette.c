#include "mesh_palette.h"
#include "ambient_asset.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t half(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static int fail(char *error, size_t cap, uint32_t id, const char *reason)
{
    if (error && cap)
        snprintf(error, cap, "3D palette profile=%u MPA1: %s", id, reason);
    return 0;
}
static PortableAmbientRules rules_view(const PortableMeshPaletteConfig *c)
{
    return (PortableAmbientRules){.lanes = c->lanes,
                                  .frames = c->frames,
                                  .patches = c->patches,
                                  .init_patches = c->initial,
                                  .data = c->data,
                                  .lane_count = c->lane_count,
                                  .frame_count = c->frame_count,
                                  .patch_count = c->patch_count,
                                  .init_patch_count = c->init_patch_count,
                                  .data_size = c->data_size,
                                  .object_seed_offset = c->object_seed_offset};
}
static int supported_rules(const PortableMeshPaletteConfig *c)
{
    if (c->lane_count < 1 || c->lane_count > 2 || c->sunset_lane >= c->lane_count ||
        (c->timer_lane != UINT16_MAX && c->timer_lane >= c->lane_count))
        return 0;
    unsigned sunsets = 0, timers = 0;
    const PortableAmbientRules r = rules_view(c);
    if (!portable_ambient_rules_valid(&r))
        return 0;
    for (unsigned i = 0; i < c->lane_count; ++i)
    {
        const PortableAmbientLane *l = &c->lanes[i];
        if (l->trigger != PORTABLE_AMBIENT_AUTOSTART)
            return 0;
        if (l->controller == PORTABLE_AMBIENT_SUNSET && l->stage == PORTABLE_AMBIENT_PRE_PROFILE &&
            l->frame_count == 64 && i == c->sunset_lane)
            ++sunsets;
        else if (l->controller == PORTABLE_AMBIENT_TIMER &&
                 l->stage == PORTABLE_AMBIENT_POST_DRIVER && i == c->timer_lane)
            ++timers;
        else
            return 0;
    }
    for (unsigned i = 0; i < c->patch_count; ++i)
        if (c->patches[i].target > PORTABLE_AMBIENT_SKY)
            return 0;
    for (unsigned i = 0; i < c->init_patch_count; ++i)
        if (c->initial[i].target > PORTABLE_AMBIENT_OBJECT)
            return 0;
    return sunsets == 1 && timers == (unsigned)(c->timer_lane != UINT16_MAX) &&
           sunsets + timers == c->lane_count;
}
static int bindings_valid(const PortableMeshPaletteConfig *c)
{
    uint32_t used = 0;
    for (unsigned i = 0; i < c->binding_count; ++i)
    {
        const PortableMeshPaletteBinding *b = &c->bindings[i];
        if ((b->kind != 97 && b->kind != 101) || !b->id || b->id == UINT32_MAX || b->bank > 1 ||
            !b->width || b->width > 1024 || !b->height || b->height > 1024 ||
            b->index_size != b->width * b->height || b->index_offset != used ||
            used > c->index_bytes || b->index_size > c->index_bytes - used ||
            (i && (b->kind < c->bindings[i - 1].kind ||
                   (b->kind == c->bindings[i - 1].kind && b->id <= c->bindings[i - 1].id))))
            return 0;
        used += b->index_size;
    }
    return used == c->index_bytes;
}
int portable_mesh_palette_config_valid(const PortableMeshPaletteConfig *c)
{
    if (!c || c->enabled > 1)
        return 0;
    if (!c->enabled)
        return !c->profile_id && !c->revision && !c->binding_count && !c->variant_count &&
               !c->index_bytes && !c->data_size && !c->lane_count && !c->frame_count &&
               !c->patch_count && !c->init_patch_count && !c->data && !c->indices;
    return c->profile_id && c->profile_id != UINT32_MAX && c->revision && c->binding_count &&
           c->binding_count <= PORTABLE_MESH_PALETTE_BINDINGS && c->variant_count &&
           c->variant_count <= PORTABLE_MESH_PALETTE_VARIANTS && c->index_bytes &&
           c->index_bytes <= PORTABLE_MESH_PALETTE_INDEX_BYTES && c->indices && c->data &&
           c->initial_snapshot.initialized == 1 && bindings_valid(c) && supported_rules(c);
}
static void snapshot_pair(const PortableAmbientSnapshot *s, uint8_t out[256])
{
    for (unsigned i = 0; i < 128; ++i)
    {
        const uint16_t color = i < 64 ? s->road[i] : s->sky[i - 64];
        out[i * 2] = (uint8_t)color;
        out[i * 2 + 1] = (uint8_t)(color >> 8);
    }
}
static void apply_frame(const PortableMeshPaletteConfig *c, unsigned index, uint8_t pair[256])
{
    const PortableAmbientFrame *frame = &c->frames[index];
    for (unsigned i = 0; i < frame->patch_count; ++i)
    {
        const PortableAmbientPatch *p = &c->patches[frame->first_patch + i];
        memcpy(pair + (p->target == PORTABLE_AMBIENT_SKY ? 128u : 0u) + p->offset * 2u,
               c->data + p->data_offset, p->count * 2u);
    }
}
static int add_variant(PortableMeshPaletteConfig *c, const uint8_t pair[256], unsigned *index)
{
    for (unsigned i = 0; i < c->variant_count; ++i)
        if (!memcmp(c->variants[i], pair, 256))
        {
            *index = i;
            return 1;
        }
    if (c->variant_count == PORTABLE_MESH_PALETTE_VARIANTS)
        return 0;
    *index = c->variant_count++;
    memcpy(c->variants[*index], pair, 256);
    return 1;
}
/* At each successive sunset prefix, close over timer writes before emitting
 * the next sunset frame. This preserves either write order, retained partial
 * writes and repeated timer frames. Every loop is bounded by wire limits and
 * the global512-pair cap. No source timer chronology is approximated at tick. */
static int enumerate(PortableMeshPaletteConfig *c)
{
    uint16_t current[512], next[512];
    unsigned count = 1;
    uint8_t pair[256];
    snapshot_pair(&c->initial_snapshot, pair);
    unsigned index;
    if (!add_variant(c, pair, &index))
        return 0;
    current[0] = (uint16_t)index;
    const PortableAmbientLane *sunset = &c->lanes[c->sunset_lane];
    for (unsigned prefix = 0; prefix <= 64; ++prefix)
    {
        uint8_t seen[512] = {0};
        for (unsigned i = 0; i < count; ++i)
            seen[current[i]] = 1;
        if (c->timer_lane != UINT16_MAX)
        {
            const PortableAmbientLane *timer = &c->lanes[c->timer_lane];
            for (unsigned at = 0; at < count; ++at)
                for (unsigned frame = 0; frame < timer->frame_count; ++frame)
                {
                    memcpy(pair, c->variants[current[at]], 256);
                    apply_frame(c, timer->first_frame + frame, pair);
                    if (!add_variant(c, pair, &index))
                        return 0;
                    if (!seen[index])
                    {
                        seen[index] = 1;
                        current[count++] = (uint16_t)index;
                    }
                }
        }
        if (prefix == 64)
            break;
        unsigned next_count = 0;
        memset(seen, 0, sizeof seen);
        for (unsigned i = 0; i < count; ++i)
        {
            memcpy(pair, c->variants[current[i]], 256);
            apply_frame(c, sunset->first_frame + prefix, pair);
            if (!add_variant(c, pair, &index))
                return 0;
            if (!seen[index])
            {
                seen[index] = 1;
                next[next_count++] = (uint16_t)index;
            }
        }
        count = next_count;
        memcpy(current, next, count * sizeof current[0]);
    }
    return 1;
}
int portable_mesh_palette_decode(const uint8_t *bytes, size_t size, uint32_t id,
                                 PortableMeshPaletteConfig *out, char *error, size_t cap)
{
    if (!bytes || !out || size < 304 || size > UINT32_MAX || word(bytes) != UINT32_C(0x3141504d) ||
        word(bytes + 4) != 1 || word(bytes + 8) != size || word(bytes + 12) != id || !id ||
        id == UINT32_MAX || !word(bytes + 16) || !word(bytes + 20) || word(bytes + 20) > 128 ||
        !word(bytes + 28) || word(bytes + 28) > PORTABLE_MESH_PALETTE_INDEX_BYTES ||
        word(bytes + 32) || word(bytes + 36) || word(bytes + 40) || word(bytes + 44))
        return fail(error, cap, id, "invalid header");
    const uint32_t bindings = word(bytes + 20), ambient_size = word(bytes + 24),
                   index_bytes = word(bytes + 28);
    const uint64_t ambient_offset = 304u + (uint64_t)bindings * 32u;
    if (ambient_offset + ambient_size + index_bytes != size)
        return fail(error, cap, id, "invalid payload sizes");
    PortableMeshPaletteConfig *c = calloc(1, sizeof *c);
    if (!c)
        return fail(error, cap, id, "configuration allocation failed");
    c->enabled = 1;
    c->profile_id = id;
    c->revision = word(bytes + 16);
    c->binding_count = bindings;
    c->index_bytes = index_bytes;
    c->indices = bytes + (size_t)ambient_offset + ambient_size;
    const char *reason = "invalid bindings or indexed planes";
    uint32_t used = 0;
    for (unsigned i = 0; i < bindings; ++i)
    {
        const uint8_t *p = bytes + 304 + i * 32;
        PortableMeshPaletteBinding b = {word(p),      word(p + 4),  word(p + 8), word(p + 12),
                                        word(p + 16), word(p + 20), word(p + 24)};
        if ((b.kind != 97 && b.kind != 101) || !b.id || b.id == UINT32_MAX || b.bank > 1 ||
            !b.width || b.width > 1024 || !b.height || b.height > 1024 ||
            b.index_size != b.width * b.height || b.index_offset != used || word(p + 28) ||
            b.index_size > index_bytes - used ||
            (i && (b.kind < c->bindings[i - 1].kind ||
                   (b.kind == c->bindings[i - 1].kind && b.id <= c->bindings[i - 1].id))))
            goto failed;
        c->bindings[i] = b;
        used += b.index_size;
    }
    if (used != index_bytes)
        goto failed;
    for (unsigned i = 0; i < index_bytes; ++i)
        if (c->indices[i] > 63 && c->indices[i] != 255)
            goto failed;
    PortableAmbientAssetStorage storage;
    const PortableAssetSectionView section = {.kind = 84,
                                              .content_id = id,
                                              .version = 1,
                                              .bytes = bytes + (size_t)ambient_offset,
                                              .size = ambient_size};
    reason = "invalid or unsupported AMB1 lanes/patches";
    if (!portable_ambient_asset_open(&section, &storage) || storage.rules.lane_count > 2)
        goto failed;
    c->lane_count = storage.rules.lane_count;
    c->frame_count = storage.rules.frame_count;
    c->patch_count = storage.rules.patch_count;
    c->init_patch_count = storage.rules.init_patch_count;
    c->data_size = storage.rules.data_size;
    c->object_seed_offset = storage.rules.object_seed_offset;
    c->data = storage.rules.data;
    c->timer_lane = UINT16_MAX;
    memcpy(c->lanes, storage.lanes, c->lane_count * sizeof c->lanes[0]);
    memcpy(c->frames, storage.frames, sizeof c->frames);
    memcpy(c->patches, storage.patches, sizeof c->patches);
    memcpy(c->initial, storage.initial, sizeof c->initial);
    c->seed = storage.seed;
    for (unsigned i = 0; i < 64; ++i)
    {
        c->seed.road[i] = half(bytes + 48 + i * 2);
        c->seed.sky[i] = half(bytes + 176 + i * 2);
    }
    for (unsigned i = 0; i < c->lane_count; ++i)
        if (c->lanes[i].controller == PORTABLE_AMBIENT_SUNSET)
            c->sunset_lane = (uint16_t)i;
        else
            c->timer_lane = (uint16_t)i;
    if (!supported_rules(c))
        goto failed;
    const PortableAmbientRules r = rules_view(c);
    PortableAmbientState initial;
    if (portable_ambient_init(&initial, &r, &c->seed) != 1)
        goto failed;
    c->initial_snapshot = initial.snapshot;
    reason = "immutable palette variant budget exceeded";
    if (!enumerate(c))
        goto failed;
    if (!portable_mesh_palette_config_valid(c))
        goto failed;
    *out = *c;
    free(c);
    if (error && cap)
        error[0] = 0;
    return 1;
failed:
    free(c);
    return fail(error, cap, id, reason);
}
static int zero_lane(const PortableAmbientLaneState *s)
{
    return !s->lane && !s->frame && !s->counter && !s->creation_order && !s->control_counter &&
           !s->active && !s->pending && !s->started && !s->controller_phase;
}
static int lane_valid(const PortableMeshPaletteConfig *c, const PortableAmbientLaneState *s,
                      unsigned lane)
{
    const PortableAmbientLane *l = &c->lanes[lane];
    return s->lane == lane && s->frame < l->frame_count &&
           s->creation_order == (uint32_t)c->lane_count - lane && !s->control_counter &&
           s->active <= 1 && !s->pending && s->started <= 1 && !s->controller_phase &&
           (lane != c->sunset_lane || (s->active && s->counter == l->initial_counter));
}
int portable_mesh_palette_state_valid(const PortableMeshPaletteConfig *c,
                                      const PortableMeshPalette *w)
{
    if (!c || !w || w->enabled != c->enabled)
        return 0;
    const PortableAmbientState *a = &w->ambient;
    if (!c->enabled)
    {
        if (w->profile_id || w->revision || w->variant || a->initialized || a->in_tick ||
            a->next_creation_order || a->post_serial || a->snapshot.initialized ||
            a->snapshot.image_patch_count)
            return 0;
        for (unsigned i = 0; i < 16; ++i)
            if (!zero_lane(&a->lanes[i]) || !zero_lane(&a->profile_lanes[i]))
                return 0;
        for (unsigned i = 0; i < 64; ++i)
            if (a->snapshot.road[i] || a->snapshot.sky[i])
                return 0;
        for (unsigned i = 0; i < 128; ++i)
            if (a->snapshot.object[i] || a->snapshot.object_dirty[i])
                return 0;
        for (unsigned i = 0; i < 32; ++i)
            if (a->snapshot.image_patch_ids[i])
                return 0;
        return 1;
    }
    if (w->profile_id != c->profile_id || w->revision != c->revision ||
        w->variant >= c->variant_count || a->initialized != 1 || a->in_tick ||
        a->snapshot.initialized != 1 || a->snapshot.image_patch_count ||
        a->next_creation_order != (uint32_t)c->lane_count + 1u)
        return 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        if (i == c->sunset_lane)
        {
            if (!lane_valid(c, &a->profile_lanes[i], i))
                return 0;
        }
        else if (!zero_lane(&a->profile_lanes[i]))
            return 0;
        if (i == 0 && c->timer_lane != UINT16_MAX)
        {
            if (!lane_valid(c, &a->lanes[0], c->timer_lane))
                return 0;
        }
        else if (!zero_lane(&a->lanes[i]))
            return 0;
    }
    for (unsigned i = 0; i < 128; ++i)
        if (a->snapshot.object[i] != c->initial_snapshot.object[i] ||
            a->snapshot.object_dirty[i] != c->initial_snapshot.object_dirty[i])
            return 0;
    for (unsigned i = 0; i < 32; ++i)
        if (a->snapshot.image_patch_ids[i])
            return 0;
    uint8_t pair[256];
    snapshot_pair(&a->snapshot, pair);
    return !memcmp(pair, c->variants[w->variant], 256);
}
int portable_mesh_palette_valid(const PortableMeshPaletteConfig *c, const PortableMeshPalette *w)
{
    return portable_mesh_palette_config_valid(c) && portable_mesh_palette_state_valid(c, w);
}
static int resolve_variant(const PortableMeshPaletteConfig *c, PortableMeshPalette *w)
{
    uint8_t pair[256];
    snapshot_pair(&w->ambient.snapshot, pair);
    for (unsigned i = 0; i < c->variant_count; ++i)
        if (!memcmp(pair, c->variants[i], 256))
        {
            w->variant = i;
            return 1;
        }
    return 0;
}
int portable_mesh_palette_init(const PortableMeshPaletteConfig *c, PortableMeshPalette *out,
                               char *error, size_t cap)
{
    if (!out || !portable_mesh_palette_config_valid(c))
        return fail(error, cap, c ? c->profile_id : 0, "invalid initialization arguments");
    PortableMeshPalette w = {0};
    if (c->enabled)
    {
        const PortableAmbientRules r = rules_view(c);
        w.enabled = 1;
        w.profile_id = c->profile_id;
        w.revision = c->revision;
        if (portable_ambient_init(&w.ambient, &r, &c->seed) != 1 || !resolve_variant(c, &w))
            return fail(error, cap, c->profile_id, "initial palette is not enumerated");
    }
    if (!portable_mesh_palette_state_valid(c, &w))
        return fail(error, cap, c->profile_id, "invalid initial owner");
    *out = w;
    if (error && cap)
        error[0] = 0;
    return 1;
}
int portable_mesh_palette_tick_validated_config(const PortableMeshPaletteConfig *c,
                                                PortableMeshPalette *out,
                                                const PortableMeshPalette *before,
                                                const PortableAmbientInputs *in,
                                                const int32_t *sine, size_t sine_count, char *error,
                                                size_t cap)
{
    if (!out || !portable_mesh_palette_state_valid(c, before) ||
        (c->enabled &&
         (!in || in->paused > 1 || in->source_gate > 1 || in->sky_phase_before > 6 ||
          in->sky_phase_after > 6 || in->sky_elapsed_before < 0 || in->sky_elapsed_before > 128 ||
          in->sky_counter_before < 0 || in->sky_counter_before > 3)))
        return fail(error, cap, c ? c->profile_id : 0, "invalid tick arguments or owner");
    PortableMeshPalette next = *before;
    if (c->enabled)
    {
        const PortableAmbientRules r = rules_view(c);
        if (portable_ambient_tick(&next.ambient, &r, PORTABLE_AMBIENT_PRE_PROFILE, in, sine,
                                  sine_count) != 1 ||
            portable_ambient_tick(&next.ambient, &r, PORTABLE_AMBIENT_POST_DRIVER, in, sine,
                                  sine_count) != 1 ||
            !resolve_variant(c, &next) || !portable_mesh_palette_state_valid(c, &next))
            return fail(error, cap, c->profile_id, "unrepresented or invalid palette after tick");
    }
    *out = next;
    if (error && cap)
        error[0] = 0;
    return 1;
}
int portable_mesh_palette_tick(const PortableMeshPaletteConfig *c, PortableMeshPalette *out,
                               const PortableMeshPalette *before, const PortableAmbientInputs *in,
                               const int32_t *sine, size_t sine_count, char *error, size_t cap)
{
    if (!portable_mesh_palette_config_valid(c))
        return fail(error, cap, c ? c->profile_id : 0, "invalid tick arguments or owner");
    return portable_mesh_palette_tick_validated_config(c, out, before, in, sine, sine_count, error,
                                                       cap);
}
const uint8_t *portable_mesh_palette_variant(const PortableMeshPaletteConfig *c, unsigned variant)
{
    return portable_mesh_palette_config_valid(c) && c->enabled && variant < c->variant_count
               ? c->variants[variant]
               : NULL;
}
int portable_mesh_palette_binding(const PortableMeshPaletteConfig *c, unsigned index,
                                  PortableMeshPaletteBinding *out, const uint8_t **indices)
{
    if (!out || !indices || !portable_mesh_palette_config_valid(c) || !c->enabled ||
        index >= c->binding_count)
        return 0;
    *out = c->bindings[index];
    *indices = c->indices + c->bindings[index].index_offset;
    return 1;
}
static void hash_value(uint64_t *h, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
    {
        *h ^= (uint8_t)(value >> (i * 8));
        *h *= UINT64_C(1099511628211);
    }
}
uint64_t portable_mesh_palette_hash(const PortableMeshPalette *w, uint64_t h)
{
    if (!w || !w->enabled)
        return h;
#define H(value, bytes) hash_value(&h, (uint32_t)(value), bytes)
    H(UINT32_C(0x3141504d), 4);
    H(w->enabled, 4);
    H(w->profile_id, 4);
    H(w->revision, 4);
    H(w->variant, 4);
    const PortableAmbientState *a = &w->ambient;
    H(a->initialized, 1);
    H(a->in_tick, 1);
    H(a->next_creation_order, 4);
    H(a->post_serial, 4);
    for (unsigned group = 0; group < 2; ++group)
        for (unsigned i = 0; i < 16; ++i)
        {
            const PortableAmbientLaneState *l = group ? &a->profile_lanes[i] : &a->lanes[i];
            H(l->lane, 2);
            H(l->frame, 2);
            H(l->counter, 4);
            H(l->creation_order, 4);
            H(l->control_counter, 4);
            H(l->active, 1);
            H(l->pending, 1);
            H(l->started, 1);
            H(l->controller_phase, 1);
        }
    const PortableAmbientSnapshot *s = &a->snapshot;
    for (unsigned i = 0; i < 64; ++i)
    {
        H(s->road[i], 2);
        H(s->sky[i], 2);
    }
    for (unsigned i = 0; i < 128; ++i)
    {
        H(s->object[i], 2);
        H(s->object_dirty[i], 1);
    }
    H(s->image_patch_count, 1);
    H(s->initialized, 1);
    for (unsigned i = 0; i < 32; ++i)
        H(s->image_patch_ids[i], 2);
#undef H
    return h;
}
uint64_t portable_mesh_palette_checksum(const PortableMeshPaletteConfig *c,
                                        const PortableMeshPalette *w)
{
    return portable_mesh_palette_valid(c, w)
               ? portable_mesh_palette_hash(w, UINT64_C(14695981039346656037))
               : 0;
}
