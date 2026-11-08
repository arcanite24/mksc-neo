#include "ambient.h"

static uint16_t portable_ambient_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

const PortableAmbientPatch *portable_ambient_patch(const PortableAmbientRules *rules, uint16_t id)
{
    const unsigned index = id & 0x7fffu;
    if (id & 0x8000u)
        return index < rules->init_patch_count ? &rules->init_patches[index] : NULL;
    return index < rules->patch_count ? &rules->patches[index] : NULL;
}

static int portable_ambient_patch_valid(const PortableAmbientRules *r,
                                        const PortableAmbientPatch *p)
{
    if (p->target > PORTABLE_AMBIENT_BACK_MAP || !p->count)
        return 0;
    const uint32_t limit = p->target < 2u    ? 64u
                           : p->target == 2u ? 128u
                           : p->target == 3u ? 0x4000u
                                             : 0x1000u;
    const uint32_t bytes = p->target < 3u ? (uint32_t)p->count * 2u : p->count;
    return (uint32_t)p->offset + p->count <= limit &&
           (uint64_t)p->data_offset + bytes <= r->data_size;
}

int portable_ambient_rules_valid(const PortableAmbientRules *r)
{
    if (!r || r->lane_count > 16u || r->frame_count > 256u || r->patch_count > 768u ||
        r->init_patch_count > 32u || r->data_size > 262144u || (r->lane_count && !r->lanes) ||
        (r->frame_count && !r->frames) || (r->patch_count && !r->patches) ||
        (r->init_patch_count && !r->init_patches) || (r->data_size && !r->data))
        return 0;
    for (unsigned i = 0; i < r->patch_count; ++i)
        if (!portable_ambient_patch_valid(r, &r->patches[i]))
            return 0;
    for (unsigned i = 0; i < r->init_patch_count; ++i)
        if (!portable_ambient_patch_valid(r, &r->init_patches[i]))
            return 0;
    for (unsigned i = 0; i < r->frame_count; ++i)
    {
        const PortableAmbientFrame *f = &r->frames[i];
        if (!f->duration || f->patch_count > 3u ||
            (uint64_t)f->first_patch + f->patch_count > r->patch_count)
            return 0;
    }
    for (unsigned i = 0; i < r->lane_count; ++i)
    {
        const PortableAmbientLane *l = &r->lanes[i];
        if (l->stage > 1u || l->write_policy > 1u || l->controller > 5u || l->trigger > 1u ||
            !l->frame_count || (uint32_t)l->first_frame + l->frame_count > r->frame_count ||
            l->initial_frame >= l->frame_count ||
            (l->loop_frame != UINT16_MAX && l->loop_frame >= l->frame_count))
            return 0;
        if ((l->controller == PORTABLE_AMBIENT_TIMER && l->params[0] > 1u) ||
            (l->controller == PORTABLE_AMBIENT_SUNSET && l->frame_count < 64u) ||
            (l->controller == PORTABLE_AMBIENT_DIRECTION_TILES && l->frame_count < 18u) ||
            (l->controller == PORTABLE_AMBIENT_FINISH_BACKDROP && l->frame_count < 41u) ||
            (l->controller == PORTABLE_AMBIENT_RAINBOW && l->frame_count < 18u))
            return 0;
    }
    for (unsigned i = 0; i < r->lane_count; ++i)
        if (r->lanes[i].controller == PORTABLE_AMBIENT_STORM)
            for (unsigned j = 0; j < 4u; ++j)
                if (r->lanes[i].params[j] >= r->lane_count ||
                    r->lanes[r->lanes[i].params[j]].stage != PORTABLE_AMBIENT_POST_DRIVER)
                    return 0;
    return 1;
}

static int portable_ambient_apply(PortableAmbientState *s, const PortableAmbientRules *r,
                                  uint16_t id)
{
    const PortableAmbientPatch *p = portable_ambient_patch(r, id);
    if (!p)
        return -1;
    if (p->target < 3u)
    {
        uint16_t *colors = p->target == 0u   ? s->snapshot.road
                           : p->target == 1u ? s->snapshot.sky
                                             : s->snapshot.object;
        for (unsigned i = 0; i < p->count; ++i)
        {
            colors[p->offset + i] = portable_ambient_read_u16(r->data + p->data_offset + i * 2u);
            if (p->target == PORTABLE_AMBIENT_OBJECT)
                s->snapshot.object_dirty[p->offset + i] = 1u;
        }
    }
    else
    {
        /* Exact-region replacements move last, preserving overlapping writes. */
        unsigned count = s->snapshot.image_patch_count;
        for (unsigned i = 0; i < count; ++i)
        {
            const PortableAmbientPatch *old =
                portable_ambient_patch(r, s->snapshot.image_patch_ids[i]);
            if (old && old->target == p->target && old->offset == p->offset &&
                old->count == p->count)
            {
                for (unsigned j = i + 1u; j < count; ++j)
                    s->snapshot.image_patch_ids[j - 1u] = s->snapshot.image_patch_ids[j];
                --count;
                s->snapshot.image_patch_ids[count] = 0;
                break;
            }
        }
        if (count >= PORTABLE_AMBIENT_IMAGE_PATCHES)
            return -1;
        s->snapshot.image_patch_ids[count] = id;
        s->snapshot.image_patch_count = (uint8_t)(count + 1u);
    }
    return 1;
}

static PortableAmbientLaneState portable_ambient_lane_initial(const PortableAmbientLane *lane,
                                                              uint16_t id, uint32_t order)
{
    return (PortableAmbientLaneState){.lane = id,
                                      .frame = lane->initial_frame,
                                      .counter = lane->initial_counter,
                                      .creation_order = order,
                                      .active = 1u};
}

int portable_ambient_init(PortableAmbientState *s, const PortableAmbientRules *r,
                          const PortableAmbientSeed *seed)
{
    if (!r)
        return 0;
    if (!s || !seed || !portable_ambient_rules_valid(r))
        return -1;
    *s = (PortableAmbientState){.initialized = 1u};
    for (unsigned i = 0; i < 64u; ++i)
    {
        s->snapshot.road[i] = seed->road[i];
        s->snapshot.sky[i] = seed->sky[i];
    }
    for (unsigned i = 0; i < 128u; ++i)
        s->snapshot.object[i] = seed->object[i];
    s->snapshot.initialized = 1u;
    unsigned slot = 0;
    for (unsigned i = 0; i < r->lane_count; ++i)
    {
        const PortableAmbientLane *lane = &r->lanes[i];
        if (lane->trigger == PORTABLE_AMBIENT_MANUAL)
            continue;
        /* The imported table already records source execution order. */
        const PortableAmbientLaneState initial =
            portable_ambient_lane_initial(lane, (uint16_t)i, (uint32_t)r->lane_count - i);
        if (lane->stage == PORTABLE_AMBIENT_PRE_PROFILE)
            s->profile_lanes[i] = initial;
        else
            s->lanes[slot++] = initial;
    }
    s->next_creation_order = (uint32_t)r->lane_count + 1u;
    for (unsigned i = 0; i < r->init_patch_count; ++i)
        if (portable_ambient_apply(s, r, (uint16_t)(0x8000u | i)) < 0)
            return -1;
    return 1;
}

int portable_ambient_spawn(PortableAmbientState *s, const PortableAmbientRules *r, uint16_t lane_id)
{
    if (!r)
        return 0;
    if (!s || !s->initialized || lane_id >= r->lane_count)
        return -1;
    const PortableAmbientLane *lane = &r->lanes[lane_id];
    if (lane->stage == PORTABLE_AMBIENT_PRE_PROFILE)
    {
        s->profile_lanes[lane_id] = portable_ambient_lane_initial(lane, lane_id, 0);
        return 1;
    }
    unsigned slot = 0;
    for (; slot < 16u; ++slot)
        if (!s->lanes[slot].active)
            break;
    if (slot == 16u)
    {
        slot = 0;
        for (unsigned i = 1; i < 16u; ++i)
            if (s->lanes[i].creation_order < s->lanes[slot].creation_order)
                slot = i;
    }
    if (s->next_creation_order == UINT32_MAX)
    {
        /* Keep head/tail order deterministic across the serialized counter wrap. */
        uint32_t old[16];
        for (unsigned i = 0; i < 16u; ++i)
            old[i] = s->lanes[i].creation_order;
        uint32_t maximum = 0;
        for (unsigned i = 0; i < 16u; ++i)
            if (s->lanes[i].active)
            {
                uint32_t rank = 1;
                for (unsigned j = 0; j < 16u; ++j)
                    if (s->lanes[j].active && old[j] < old[i])
                        ++rank;
                s->lanes[i].creation_order = rank;
                if (rank > maximum)
                    maximum = rank;
            }
        s->next_creation_order = maximum + 1u;
    }
    s->lanes[slot] = portable_ambient_lane_initial(lane, lane_id, s->next_creation_order++);
    s->lanes[slot].pending = s->in_tick;
    return 1;
}

static int portable_ambient_emit(PortableAmbientState *s, const PortableAmbientRules *r,
                                 const PortableAmbientLane *lane, uint16_t frame)
{
    if (frame >= lane->frame_count)
        return -1;
    const PortableAmbientFrame *f = &r->frames[lane->first_frame + frame];
    for (unsigned i = 0; i < f->patch_count; ++i)
        if (portable_ambient_apply(s, r, (uint16_t)(f->first_patch + i)) < 0)
            return -1;
    return 1;
}

static int portable_ambient_next_frame(PortableAmbientLaneState *s, const PortableAmbientLane *lane)
{
    if (++s->frame < lane->frame_count)
        return 1;
    if (lane->loop_frame == UINT16_MAX)
    {
        s->active = 0;
        return 0;
    }
    s->frame = lane->loop_frame;
    return 1;
}

static int portable_ambient_lane_tick(PortableAmbientState *owner, const PortableAmbientRules *r,
                                      PortableAmbientLaneState *s, const PortableAmbientInputs *in)
{
    const PortableAmbientLane *lane = &r->lanes[s->lane];
    int emit = 0;
    switch (lane->controller)
    {
    case PORTABLE_AMBIENT_TIMER:
        if (lane->params[0])
        {
            const uint16_t duration = r->frames[lane->first_frame + s->frame].duration;
            const int changed = ++s->counter >= duration;
            if (changed)
            {
                s->counter = 0;
                if (!portable_ambient_next_frame(s, lane))
                    return 1;
            }
            emit = !s->started || changed || lane->write_policy == PORTABLE_AMBIENT_EVERY_CALL;
        }
        else
        {
            if (!s->started)
                emit = 1;
            else if (s->counter)
                --s->counter;
            else
            {
                if (!portable_ambient_next_frame(s, lane))
                    return 1;
                emit = 1;
            }
            if (emit)
                s->counter = r->frames[lane->first_frame + s->frame].duration - 1u;
            if (lane->write_policy == PORTABLE_AMBIENT_EVERY_CALL)
                emit = 1;
        }
        break;
    case PORTABLE_AMBIENT_SUNSET:
        if ((in->sky_phase_before == 1u && in->sky_elapsed_before >= 0 &&
             in->sky_elapsed_before <= 31) ||
            (in->sky_phase_before == 4u && in->sky_elapsed_before >= 32 &&
             in->sky_elapsed_before <= 63))
        {
            if (in->sky_counter_before == 3)
            {
                s->frame = (uint16_t)in->sky_elapsed_before;
                emit = 1;
            }
        }
        break;
    case PORTABLE_AMBIENT_DIRECTION_TILES:
    {
        const int32_t threshold = in->selected_base_speed_q16 / 3;
        if (!in->selected_speed_q16 || (in->selected_flags & 0x20u))
            s->controller_phase = 0;
        else if (s->controller_phase == 0u)
        {
            if (in->selected_speed_q16 > threshold)
            {
                s->control_counter = 6;
                s->controller_phase = 1;
            }
        }
        else if (s->controller_phase == 1u)
        {
            if (--s->control_counter <= 0)
            {
                s->controller_phase = 2;
                if (in->selected_speed_q16 < threshold)
                    s->controller_phase = 0;
            }
        }
        else if (in->selected_speed_q16 < threshold)
        {
            s->control_counter = 3;
            s->controller_phase = 1;
        }
        if (++s->counter > 1u)
        {
            s->counter = 0;
            s->frame = (uint16_t)(((s->frame % 6u) + 1u) % 6u + s->controller_phase * 6u);
            emit = 1;
        }
        break;
    }
    case PORTABLE_AMBIENT_FINISH_BACKDROP:
        switch (s->controller_phase)
        {
        case 0:
            if ((in->race_phase_7d4 & 15u) == 3u)
                s->controller_phase = 1;
            break;
        case 1:
            s->frame = (uint16_t)(s->control_counter / 16);
            emit = 1;
            if (++s->control_counter > 512)
            {
                s->control_counter = 0;
                s->controller_phase = 2;
            }
            break;
        case 2:
            s->frame = (uint16_t)(33u + ((unsigned)s->control_counter / 64u & 7u));
            emit = 1;
            if ((in->leader_route_wrap & 15u) == 2u)
                s->controller_phase = 3;
            ++s->control_counter;
            break;
        case 3:
            s->frame = (uint16_t)(33u + ((unsigned)s->control_counter / 64u & 7u));
            emit = 1;
            if (((unsigned)s->control_counter & 511u) == 0u)
            {
                s->control_counter = 0;
                s->controller_phase = 4;
            }
            else
                ++s->control_counter;
            break;
        case 4:
            s->frame = (uint16_t)((512 - s->control_counter) / 16);
            emit = 1;
            if (++s->control_counter > 512)
                s->controller_phase = 5;
            break;
        default:
            break;
        }
        break;
    case PORTABLE_AMBIENT_RAINBOW:
        if (s->controller_phase == 0u)
        {
            if ((int32_t)(in->leader_progress >> 8u) - 127 > 1)
                s->controller_phase = 1;
        }
        else if (s->controller_phase == 1u)
        {
            if (++s->control_counter > 60)
            {
                s->control_counter = 0;
                s->controller_phase = 2;
                s->frame = 0;
                emit = 1;
            }
        }
        else if (s->controller_phase == 2u)
        {
            s->frame = (uint16_t)s->control_counter;
            emit = 1;
            if (++s->control_counter > 16)
                s->controller_phase = 3;
        }
        else
        {
            s->frame = 17;
            emit = 1;
        }
        break;
    case PORTABLE_AMBIENT_STORM:
        if (in->scene_mode_80d == 2u)
            break;
        if (++s->counter > 125u)
        {
            s->counter = 0;
            const unsigned event = in->clock & 7u;
            if (event < 6u && portable_ambient_spawn(owner, r, (uint16_t)lane->params[3]) < 0)
                return -1;
            if ((event < 4u || event >= 6u) &&
                portable_ambient_spawn(owner, r, (uint16_t)lane->params[2]) < 0)
                return -1;
            if (portable_ambient_spawn(owner, r, (uint16_t)lane->params[0]) < 0 ||
                portable_ambient_spawn(owner, r, (uint16_t)lane->params[1]) < 0)
                return -1;
        }
        break;
    default:
        return -1;
    }
    s->started = 1u;
    return emit ? portable_ambient_emit(owner, r, lane, s->frame) : 1;
}

int portable_ambient_tick(PortableAmbientState *s, const PortableAmbientRules *r, uint8_t stage,
                          const PortableAmbientInputs *in, const int32_t *sine, size_t sine_count)
{
    (void)sine;
    (void)sine_count; /* Imported frames contain resolved RGB5 values. */
    if (!r)
        return 0;
    if (!s || !in || !s->initialized || stage > 1u)
        return -1;
    if (in->paused || (stage == PORTABLE_AMBIENT_PRE_PROFILE && in->source_gate))
        return 1;
    if (stage == PORTABLE_AMBIENT_PRE_PROFILE)
    {
        for (unsigned i = 0; i < r->lane_count; ++i)
            if (s->profile_lanes[i].active &&
                portable_ambient_lane_tick(s, r, &s->profile_lanes[i], in) < 0)
                return -1;
        return 1;
    }
    ++s->post_serial;
    uint8_t order[16];
    unsigned count = 0;
    for (unsigned i = 0; i < 16u; ++i)
    {
        if (!s->lanes[i].active)
            continue;
        s->lanes[i].pending = 0;
        unsigned at = count++;
        while (at && s->lanes[order[at - 1u]].creation_order < s->lanes[i].creation_order)
        {
            order[at] = order[at - 1u];
            --at;
        }
        order[at] = (uint8_t)i;
    }
    s->in_tick = 1u;
    for (unsigned i = 0; i < count; ++i)
    {
        PortableAmbientLaneState *lane = &s->lanes[order[i]];
        if (lane->active && !lane->pending && portable_ambient_lane_tick(s, r, lane, in) < 0)
        {
            s->in_tick = 0;
            return -1;
        }
    }
    s->in_tick = 0;
    return 1;
}

static uint32_t portable_ambient_digest_value(uint32_t hash, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
    {
        hash ^= (value >> (i * 8u)) & 255u;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

uint32_t portable_ambient_digest(const PortableAmbientState *s)
{
    if (!s)
        return 0;
    uint32_t h = UINT32_C(2166136261);
#define AMBIENT_HASH(v) h = portable_ambient_digest_value(h, (uint32_t)(v))
    AMBIENT_HASH(s->initialized);
    AMBIENT_HASH(s->in_tick);
    AMBIENT_HASH(s->next_creation_order);
    AMBIENT_HASH(s->post_serial);
    for (unsigned group = 0; group < 2u; ++group)
        for (unsigned i = 0; i < 16u; ++i)
        {
            const PortableAmbientLaneState *l = group ? &s->profile_lanes[i] : &s->lanes[i];
            AMBIENT_HASH(l->lane);
            AMBIENT_HASH(l->frame);
            AMBIENT_HASH(l->counter);
            AMBIENT_HASH(l->creation_order);
            AMBIENT_HASH(l->control_counter);
            AMBIENT_HASH(l->active);
            AMBIENT_HASH(l->pending);
            AMBIENT_HASH(l->started);
            AMBIENT_HASH(l->controller_phase);
        }
    for (unsigned i = 0; i < 64u; ++i)
    {
        AMBIENT_HASH(s->snapshot.road[i]);
        AMBIENT_HASH(s->snapshot.sky[i]);
    }
    for (unsigned i = 0; i < 128u; ++i)
    {
        AMBIENT_HASH(s->snapshot.object[i]);
        AMBIENT_HASH(s->snapshot.object_dirty[i]);
    }
    AMBIENT_HASH(s->snapshot.image_patch_count);
    AMBIENT_HASH(s->snapshot.initialized);
    for (unsigned i = 0; i < 32u; ++i)
        AMBIENT_HASH(s->snapshot.image_patch_ids[i]);
#undef AMBIENT_HASH
    return h;
}
