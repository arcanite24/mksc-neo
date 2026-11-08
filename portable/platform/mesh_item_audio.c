#include "mesh_item_audio.h"
#include "render_pose.h"
#include <stdio.h>
#include <string.h>

static int fail(char *error, size_t capacity, const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "3D item audio state: %s", reason);
    return 0;
}

static int position_valid(const int32_t p[3])
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (p[axis] < -268435456 || p[axis] > 268435456)
            return 0;
    return 1;
}

static int valid(const PortableMeshItemAudioView *v)
{
    if (!v || !v->world || !v->events || !v->racers || !v->mushroom_voices)
        return 0;
    const unsigned count = v->world->inventory.count;
    if (!count || count > 8 || v->world->prepared != 0 || v->world->status.count != count ||
        v->human_mask != v->world->inventory.human_mask ||
        v->human_mask != v->world->status.human_mask || v->primary_human >= count ||
        !(v->human_mask & (1u << v->primary_human)) || (v->human_mask >> count) ||
        v->events->count > PORTABLE_MESH_ITEMS_EVENTS || v->finished > 1 || v->final_lap > 1)
        return 0;
    if(v->actor_events &&
        (((v->actor_events->hit_mask | v->actor_events->protected_mask | v->actor_events->bump_mask |
           v->actor_events->kamek_touch_mask |
           v->actor_events->pier_boo_touch_mask | v->actor_events->pier_boo_protected_mask |
           v->actor_events->source_haunt_assigned_mask | v->actor_events->source_haunt_clock_mask |
           v->actor_events->source_haunt_music_start_mask | v->actor_events->source_haunt_music_release_mask |
           v->actor_events->observed_item_haunt_mask |
           v->actor_events->piranha_touch_mask | v->actor_events->piranha_bite_mask |
           v->actor_events->capture_mask | v->actor_events->launch_mask |
           v->actor_events->seagull_touch_mask | v->actor_events->lava_burn_mask |
           v->actor_events->thwomp_bump_mask | v->actor_events->thwomp_crush_mask |
           v->actor_events->thwomp_recover_mask | v->actor_events->cannonball_hit_mask |
           v->actor_events->cannonball_protected_mask | v->actor_events->boo_touch_mask |
           v->actor_events->rock_hit_mask | v->actor_events->rock_protected_mask |
           v->actor_events->teepee_touch_mask | v->actor_events->teepee_protected_mask |
           v->actor_events->teepee_release_mask | v->actor_events->falling_star_hit_mask |
           v->actor_events->falling_star_protected_mask | v->actor_events->thundercloud_hit_mask |
           v->actor_events->thundercloud_protected_mask | v->actor_events->common_prop_hit_mask |
           v->actor_events->common_prop_protected_mask | v->actor_events->common_prop_umbrella_open_mask |
           v->actor_events->common_prop_umbrella_close_mask | v->actor_events->mechakoopa_hit_mask |
           v->actor_events->mechakoopa_protected_mask | v->actor_events->mechakoopa_strong_mask |
           v->actor_events->kamek_protected_mask) >> count) ||
         (v->actor_events->hit_mask & v->actor_events->protected_mask) ||
         (v->actor_events->bump_mask & (v->actor_events->hit_mask | v->actor_events->protected_mask))))
        return 0;
    if (v->actor_events)
        for (unsigned i=0;i<8;++i)
        {
            const PortableMeshCourseCoinEvents *coins=&v->actor_events->coins;
            if (coins->before[i]>99 || coins->after[i]>99 || coins->lost[i]>100 ||
                (coins->causes[i] & ~31u) ||
                (i>=count && (coins->picked[i] || coins->lost[i] || coins->causes[i])))
                return 0;
        }
    if ((v->source_actor_config && v->source_actor_config->pier_boo_count) ||
        (v->source_actors && v->source_actors->pier_boo_count))
    {
        const PortableMeshCourseActorsConfig *config = v->source_actor_config;
        const PortableMeshCourseActors *actors = v->source_actors;
        if (!config || !actors || config->pier_boo_count > 64 ||
            config->pier_boo_count != actors->pier_boo_count ||
            (actors->source_haunt_lock_mask >> count)) return 0;
        for (unsigned slot = 0; slot < PORTABLE_BOO_ATTACK_CAPACITY; ++slot)
        {
            const PortableBooAttack *a = &actors->source_haunts[slot];
            if (a->occupied > 1 || a->visible > 1 || a->phase > 7 || a->frame > 1 ||
                (a->occupied && (a->target >= count ||
                    actors->source_haunt_source_indices[slot] >= config->pier_boo_count))) return 0;
        }
    }
    for (unsigned i = 0; i < count; ++i)
        if (v->donor[i] >= 8 || !position_valid(v->racers[i].kart.position_q16) ||
            v->world->inventory.racers[i].active > 1 ||
            (v->world->inventory.racers[i].held_item > 2 &&
             !portable_mesh_item_id_valid(v->world->inventory.racers[i].held_item, 1)))
            return 0;
    for (uint32_t i = 0; i < v->events->count; ++i)
    {
        const PortableMeshItemEvent *e = &v->events->values[i];
        if (e->kind > PORTABLE_MESH_EVENT_BOMB_CONTACT || e->racer >= count ||
            (e->target != UINT8_MAX && e->target >= count) ||
            (e->kind == PORTABLE_MESH_EVENT_BOMB_CONTACT && e->target == UINT8_MAX) ||
            (e->flags & ~3u) ||
            (e->actor != UINT32_MAX && e->actor >= PORTABLE_MESH_ITEMS_ACTORS))
            return 0;
    }
    return 1;
}

static int cue(PortableMeshItemAudioOutput *out, uint16_t song, uint8_t player, uint16_t volume)
{
    if (!volume)
        return 1;
    if (out->count == PORTABLE_MESH_ITEM_AUDIO_CUES || !song)
        return 0;
    out->cues[out->count++] = (PortableMeshItemAudioCue){song, volume, player, 0};
    return 1;
}

static uint16_t volume_at_mask(const PortableMeshItemAudioView *v,const int32_t p[3],int live_only,uint8_t mask)
{
    if (!position_valid(p))
        return UINT16_MAX;
    uint64_t nearest = UINT64_MAX;
    for (unsigned i = 0; i < v->world->inventory.count; ++i)
        if ((v->human_mask & mask & (1u << i)) &&
            (!live_only || v->racers[i].phase == PORTABLE_MESH_RACER_RACING))
        {
            uint64_t squared = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const int64_t delta = (int64_t)p[axis] - v->racers[i].kart.position_q16[axis];
                squared += (uint64_t)(delta * delta);
            }
            if (squared < nearest)
                nearest = squared;
        }
    /* Squared-distance linear fade avoids a floating-point/sqrt dependency.
     * World differences <=8192m keep three squared Q16 axes within uint64. */
    const uint64_t near = UINT64_C(16) << 32, far = UINT64_C(4096) << 32;
    if (nearest <= near)
        return 256;
    return nearest >= far ? 0 : (uint16_t)((far - nearest) * 256u / (far - near));
}

static uint16_t volume_at(const PortableMeshItemAudioView *v,const int32_t p[3],int live_only)
{
    return volume_at_mask(v,p,live_only,UINT8_MAX);
}

/* Source drawScale rejects depth/range, never lateral screen coordinates, and
 * projects the ground anchor rather than the falling art height. Adapt it to
 * the accepted modern chase frame and its .1..4096m depth range. Presentation
 * interpolation, viewport size and GPU occlusion cannot change this decision. */
static uint16_t rock_start_volume(const PortableMeshItemAudioView *v,const int32_t p[3],int32_t ground)
{
    uint8_t projected=0;
    for (unsigned i=0;i<v->world->inventory.count;++i)
        if ((v->human_mask & (1u<<i)) && v->racers[i].phase==PORTABLE_MESH_RACER_RACING)
        {
            PortableRenderPose pose;
            PortableRenderView camera={0};
            if (!portable_render_pose_from_kart(&pose,&v->racers[i].kart) ||
                !portable_render_pose_chase(&pose,5.0,3.0,-.30,&camera)) return UINT16_MAX;
            double depth=0;
            for (unsigned axis=0;axis<3;++axis)
                depth+=(((axis==2?ground:p[axis])/65536.0)-camera.position[axis])*camera.forward[axis];
            if (depth>=.10 && depth<=4096.0) projected|=(uint8_t)(1u<<i);
        }
    return volume_at_mask(v,p,1,projected);
}

static uint16_t volume(const PortableMeshItemAudioView *v, const PortableMeshItemEvent *event)
{
    const int32_t *p = v->racers[event->kind == PORTABLE_MESH_EVENT_BOMB_CONTACT
        ? event->target : event->racer].kart.position_q16;
    if (event->kind != PORTABLE_MESH_EVENT_BOMB_CONTACT &&
        event->actor != UINT32_MAX && event->serial &&
        v->world->actors[event->actor].serial == event->serial &&
        v->world->actors[event->actor].phase != PORTABLE_MESH_ACTOR_FREE)
        p = v->world->actors[event->actor].motion.position_q16;
    return volume_at(v,p,0);
}

static void music(const PortableMeshItemAudio *s, const PortableMeshItemAudioView *v,
                  PortableMeshItemAudioOutput *out)
{
    const PortableMeshItemRacerStatus *status = &v->world->status.racers[v->primary_human];
    out->music = (PortableRaceMusicView){.star_uses = s->star_uses,
                                         .lightning_uses = s->lightning_uses,
                                         .course_song = v->course_song,
                                         .star_active = status->star_ticks != 0,
                                         .lightning_active = status->lightning_ticks != 0,
                                         .boo_active = status->boo_invisible_ticks != 0,
                                         .finished = v->finished,
                                         .final_lap = v->final_lap};
    out->haunt_active = status->boo_lock_ticks != 0;
    if (v->source_actor_config && v->source_actor_config->pier_boo_count)
    {
        const unsigned bit = 1u << v->primary_human;
        out->haunt_active &= (v->source_actors->source_haunt_lock_mask & bit) == 0;
        for (unsigned slot = 0; slot < PORTABLE_BOO_ATTACK_CAPACITY; ++slot)
        {
            const PortableBooAttack *a = &v->source_actors->source_haunts[slot];
            if (a->occupied && a->target == v->primary_human && (a->phase == 2 || a->phase == 3))
                out->haunt_active = 1;
        }
    }
}

int portable_mesh_item_audio_step(PortableMeshItemAudio *owner, const PortableMeshItemAudioView *v,
                                  PortableMeshItemAudioOutput *output, char *error, size_t capacity)
{
    if (!owner || !output || !valid(v) || owner->initialized > 1 ||
        (owner->initialized && v->ordinal < owner->ordinal))
        return fail(error, capacity, "invalid accepted view/ordinal");
    PortableMeshItemAudio next = *owner;
    PortableMeshItemAudioOutput out = {0};
    if (owner->initialized && v->ordinal == owner->ordinal)
    {
        music(&next, v, &out);
        *output = out;
        return 1;
    }
    for (unsigned i = 0; i < v->world->inventory.count; ++i)
    {
        const uint16_t held = v->world->inventory.racers[i].held_item;
        if (next.initialized && next.held[i] != held)
        {
            if (next.held[i] == 1 && held > 2)
                portable_gp_item_hud_transition(&next.hud[i], 0, held);
            else if (next.held[i] == 0x1008 || next.held[i] == 0x100f)
                portable_gp_item_hud_consume(&next.hud[i]);
            else if (!held)
                next.hud[i] = (PortableGpItemHud){0};
        }
        next.held[i] = held;
    }
    for (uint32_t index = 0; index < v->events->count; ++index)
    {
        const PortableMeshItemEvent *e = &v->events->values[index];
        /* Items dropped behind like a banana (fake box, Bob-omb, banana lines)
         * sound like one. */
        PortableMeshItemEvent dropped = *e;
        if (e->item == PORTABLE_MESH_ITEM_FAKE_BOX || e->item == PORTABLE_MESH_ITEM_BOMB ||
            e->item == PORTABLE_MESH_ITEM_TRIPLE_BANANAS || e->item == PORTABLE_MESH_ITEM_BANANA_BUNCH)
        {
            dropped.item = PORTABLE_MESH_ITEM_SINGLE_BANANA;
            e = &dropped;
        }
        const int human = (v->human_mask & (1u << e->racer)) != 0;
        uint16_t song = 0, loudness = volume(v, e);
        uint8_t player = 4;
        if (loudness == UINT16_MAX)
            return fail(error, capacity, "actor position outside SI bounds");
        switch (e->kind)
        {
        case PORTABLE_MESH_EVENT_PICKUP:
            if (human && (!cue(&out, 121, 6, 256) || !cue(&out, 103, 7, 256)))
                return fail(error, capacity, "cue budget exhausted");
            break;
        case PORTABLE_MESH_EVENT_RELEASE:
            if (e->item == 0x1005)
                song = (e->flags & PORTABLE_MESH_ITEM_EVENT_FORWARD_THROW) ? 123 : 0;
            else if (e->item == 0x1002 && (e->flags & PORTABLE_MESH_ITEM_EVENT_BACKWARD))
            {
                song = 134;
                player = 6;
            }
            else
                song = 123;
            break;
        case PORTABLE_MESH_EVENT_ORBIT_READY:
            if (human)
            {
                song = 122;
                loudness = 256;
            }
            break;
        case PORTABLE_MESH_EVENT_LANDED:
            if (e->item == 0x1005)
                song = 122;
            break;
        case PORTABLE_MESH_EVENT_IMPACT:
            song = 124;
            break;
        case PORTABLE_MESH_EVENT_BOMB_CONTACT:
            /* E7F0 song386 uses its imported song-table player (+4), not the
             * generic shell channel. Its source is the detonated Bomb. */
            song = 386;
            player = PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER;
            break;
        case PORTABLE_MESH_EVENT_PROTECTED:
            if (e->item == 0x1000 || e->item == 0x1002 || e->item == 0x1004 || e->item == 0x1005)
                song = 124;
            break;
        case PORTABLE_MESH_EVENT_HIT:
            if (e->item == 0x1000 || e->item == 0x1002 || e->item == 0x1004 || e->item == 0x1005)
            {
                song = e->item == 0x1005 ? 135 : 132;
                player = e->item == 0x1005 ? 6 : 4;
            }
            break;
        case PORTABLE_MESH_EVENT_USE:
            if (e->item == 0x100b)
            {
                song = 129;
                player = 6;
            }
            else if (e->item == 0x100c)
            {
                ++next.lightning_uses;
                song = 126;
                player = 10;
            }
            else if (e->item == 0x100a && e->racer == v->primary_human)
                ++next.star_uses;
            if (human)
                loudness = 256;
            break;
        case PORTABLE_MESH_EVENT_THEFT:
            portable_gp_item_hud_transition(&next.hud[e->racer], 0x100b, e->item);
            if (e->target != UINT8_MAX)
                portable_gp_item_hud_transition(&next.hud[e->target], next.held[e->target], 2);
            break;
        case PORTABLE_MESH_EVENT_BOOST:
            if (human)
            {
                const unsigned small = v->world->status.racers[e->racer].lightning_ticks != 0;
                const uint16_t voice = v->mushroom_voices[v->donor[e->racer]][small];
                if (!voice || !cue(&out, voice, PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER, 256))
                    return fail(error, capacity, "missing imported mushroom voice/cue budget");
                song = 125;
                player = PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER;
                loudness = 256;
            }
            break;
        default:
            break; /* Expiry/destruction/haunt contain no invented generic cue. */
        }
        if (song && !cue(&out, song, player, loudness))
            return fail(error, capacity, "cue budget exhausted");
    }
    if (v->actor_events)
        for (unsigned i=0;i<v->world->inventory.count;++i)
            if ((v->human_mask & (1u<<i)) && v->actor_events->coins.picked[i])
                /* Duplicate source placement bits still describe one accepted
                 * pickup for this racer; losses contain no pickup cue. */
                if (!cue(&out,101,7,256))
                    return fail(error,capacity,"coin pickup cue budget");
    if(v->actor_events)
        for(unsigned i=0;i<v->world->inventory.count;++i)
        {
            const unsigned bit=1u<<i;
            if (!(v->human_mask & bit) &&
                ((v->actor_events->falling_star_hit_mask |
                  v->actor_events->falling_star_protected_mask | v->actor_events->thundercloud_hit_mask |
                  v->actor_events->thundercloud_protected_mask | v->actor_events->common_prop_hit_mask |
                  v->actor_events->common_prop_protected_mask | v->actor_events->mechakoopa_hit_mask |
                  v->actor_events->mechakoopa_protected_mask | v->actor_events->mechakoopa_strong_mask |
                  (v->actor_events->kamek_protected_mask | v->actor_events->pier_boo_touch_mask |
                   v->actor_events->pier_boo_protected_mask)) & bit))
                continue;
            const uint16_t song=(v->actor_events->pier_boo_touch_mask & bit)?380u:
                (v->actor_events->thundercloud_hit_mask & bit)?374u:
                (v->actor_events->cannonball_hit_mask & bit)?170u:
                ((v->actor_events->common_prop_hit_mask | v->actor_events->mechakoopa_hit_mask) & bit)?169u:
                ((v->actor_events->thwomp_bump_mask |
                v->actor_events->thwomp_crush_mask) & bit)?169u:
                (v->actor_events->lava_burn_mask & bit)?377u:
                ((v->actor_events->hit_mask | v->actor_events->bump_mask) & bit)?373u:
                (v->actor_events->protected_mask & bit)?160u:0u;
            if(!song)continue;
            /* Source controllers emit their contact cue at the accepted kart, with
             * the imported song's own player. No synthetic item event owner. */
            const PortableMeshItemEvent contact={.racer=(uint8_t)i,.actor=UINT32_MAX};
            const uint16_t loudness=volume(v,&contact);
            if(loudness==UINT16_MAX || !cue(&out,song,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,loudness))
                return fail(error,capacity,"source actor cue position/budget");
        }
    /* 40810 applies the contact response before invoking the umbrella owner. */
    if (v->actor_events)
        for (unsigned i=0;i<v->world->inventory.count;++i)
            if (v->human_mask & (1u<<i))
            {
                const uint16_t song=(v->actor_events->common_prop_umbrella_open_mask & (1u<<i))?376u:
                    (v->actor_events->common_prop_umbrella_close_mask & (1u<<i))?375u:0u;
                if(song && !cue(&out,song,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,256))
                    return fail(error,capacity,"umbrella cue budget");
            }
    if (v->actor_events)
        for (unsigned i = 0; i < v->world->inventory.count; ++i)
            if (v->human_mask & (1u << i))
            {
                const unsigned bit = 1u << i;
                const uint16_t songs[4] = {
                    (v->actor_events->piranha_touch_mask & bit) ? 373u : 0u,
                    (v->actor_events->piranha_bite_mask & bit) ? 378u : 0u,
                    (v->actor_events->capture_mask & bit) ? 162u : 0u,
                    (v->actor_events->launch_mask & bit) ? 381u : 0u};
                for (unsigned kind = 0; kind < 4; ++kind)
                    if (songs[kind] && !cue(&out, songs[kind],
                        kind == 3 ? 6u : PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER, 256))
                        return fail(error, capacity, "piranha cue budget exhausted");
            }
    if (v->actor_events)
        for (unsigned i = 0; i < v->world->inventory.count; ++i)
            if ((v->human_mask & v->actor_events->seagull_touch_mask) & (1u << i))
            {
                if (!cue(&out, 385, PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER, 256))
                    return fail(error, capacity, "seagull cue budget exhausted");
            }
    if (v->actor_events)
    {
        out.stop_164_mask = v->actor_events->thwomp_crush_mask & v->human_mask;
        if (v->impact_voices)
            for (unsigned i=0;i<v->world->inventory.count;++i)
                if ((v->actor_events->thwomp_crush_mask | v->actor_events->cannonball_hit_mask) & (1u<<i))
                {
                    const unsigned rival=!(v->human_mask & (1u<<i));
                    const unsigned small=v->world->status.racers[i].lightning_ticks!=0;
                    const uint16_t song=v->impact_voices[v->donor[i]][rival][small];
                    const PortableMeshItemEvent contact={.racer=(uint8_t)i,.actor=UINT32_MAX};
                    const uint16_t loudness=volume(v,&contact);
                    if (!song || loudness==UINT16_MAX ||
                        !cue(&out,song,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,loudness))
                        return fail(error,capacity,"Thwomp impact voice/position/budget");
                }
        for (unsigned i=0;i<v->world->inventory.count;++i)
            if ((v->actor_events->thwomp_recover_mask & v->human_mask) & (1u<<i))
                if (!cue(&out,133,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,256))
                    return fail(error,capacity,"Thwomp recovery cue budget");
        const uint64_t landed=v->actor_events->thwomp_landed;
        if (landed)
        {
            const PortableMeshCourseActorsConfig *config=v->source_actor_config;
            const PortableMeshCourseActors *actors=v->source_actors;
            if (!config || !actors || config->thwomp_count>64 ||
                actors->thwomp_count!=config->thwomp_count ||
                (config->thwomp_count<64 && (landed>>config->thwomp_count)))
                return fail(error,capacity,"Thwomp landing source owner/index");
            const unsigned total=portable_mesh_course_actors_config_count(config);
            if (!total || total<config->thwomp_count)
                return fail(error,capacity,"Thwomp landing source count");
            for (unsigned i=0;i<config->thwomp_count;++i)
                if (landed & (UINT64_C(1)<<i))
                {
                    int32_t position[3];
                    if (!portable_mesh_course_actors_pose(config,actors,total-config->pier_boo_count-config->kamek_count-config->mechakoopa_count-config->common_prop_count-config->thundercloud_count-config->falling_star_count-config->teepee_count-config->rock_count-config->boo_count-config->cannonball_count-config->thwomp_count+i,position))
                        return fail(error,capacity,"Thwomp landing source position");
                    const uint16_t loudness=volume_at(v,position,1);
                    if (loudness==UINT16_MAX || (loudness>24 &&
                        !cue(&out,384,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,loudness)))
                        return fail(error,capacity,"Thwomp landing cue position/budget");
                }
        }
    }
    if (v->actor_events)
    {
        const uint64_t masks[2] = {v->actor_events->cannonball_launched,
                                   v->actor_events->cannonball_exploded};
        const PortableMeshCourseActorsConfig *config = v->source_actor_config;
        const PortableMeshCourseActors *actors = v->source_actors;
        if (masks[0] || masks[1])
        {
            if (!config || !actors || config->cannonball_count > 64 ||
                actors->cannonball_count != config->cannonball_count ||
                (config->cannonball_count < 64 && ((masks[0] | masks[1]) >> config->cannonball_count)))
                return fail(error, capacity, "cannonball source owner/index");
            const unsigned total = portable_mesh_course_actors_config_count(config);
            if (total < config->cannonball_count) return fail(error, capacity, "cannonball source count");
            for (unsigned kind = 0; kind < 2; ++kind)
                for (unsigned i = 0; i < config->cannonball_count; ++i)
                    if (masks[kind] & (UINT64_C(1) << i))
                    {
                        int32_t position[3];
                        if (!portable_mesh_course_actors_pose(config, actors,
                                total - config->pier_boo_count - config->kamek_count - config->mechakoopa_count - config->common_prop_count - config->thundercloud_count - config->falling_star_count - config->teepee_count - config->rock_count - config->boo_count - config->cannonball_count + i, position))
                            return fail(error, capacity, "cannonball source position");
                        const uint16_t loudness = volume_at(v, position, 1);
                        if (loudness == UINT16_MAX) return fail(error, capacity, "cannonball cue position");
                        if (loudness > 63)
                        {
                            if (!cue(&out, kind ? 383 : 382, PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER, loudness))
                                return fail(error, capacity, "cannonball cue budget");
                            out.cues[out.count - 1].require_idle = (uint8_t)!kind;
                        }
                    }
        }
        for (unsigned i = 0; i < v->world->inventory.count; ++i)
            if (((v->actor_events->cannonball_hit_mask | v->actor_events->thundercloud_hit_mask) & v->human_mask) & (1u << i))
                if (!cue(&out, 162, PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER, 256))
                    return fail(error, capacity, "cannonball spin cue budget");
    }
    if (v->actor_events)
    {
        const PortableMeshCourseActorsConfig *config=v->source_actor_config;
        const PortableMeshCourseActors *actors=v->source_actors;
        const uint64_t *masks=v->actor_events->rock_cue_masks;
        if (masks[0] || masks[1] || masks[2] || masks[3])
        {
            if (!config || !actors || config->rock_count>64 ||
                actors->rock_count!=config->rock_count || (config->rock_count<64 &&
                ((masks[0]|masks[1]|masks[2]|masks[3])>>config->rock_count)))
                return fail(error,capacity,"rock source owner/index");
            const unsigned total=portable_mesh_course_actors_config_count(config);
            if (total<config->rock_count) return fail(error,capacity,"rock source count");
            for (unsigned kind=0;kind<4;++kind)
                for (unsigned i=0;i<config->rock_count;++i)
                    if (masks[kind] & (UINT64_C(1)<<i))
                    {
                        int32_t position[3];
                        if (!portable_mesh_course_actors_pose(config,actors,total-config->pier_boo_count-config->kamek_count-config->mechakoopa_count-config->common_prop_count-config->thundercloud_count-config->falling_star_count-config->teepee_count-config->rock_count+i,position))
                            return fail(error,capacity,"rock source position");
                        const uint16_t loudness=kind ? volume_at(v,position,1) :
                            rock_start_volume(v,position,config->rock_anchors_q16[i][2]);
                        if (loudness==UINT16_MAX) return fail(error,capacity,"rock cue position/camera");
                        if (loudness>63)
                        {
                            if (!cue(&out,(uint16_t)(386u+kind),PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,loudness))
                                return fail(error,capacity,"rock cue budget");
                            out.cues[out.count-1].require_idle=(uint8_t)!kind;
                        }
                    }
        }
    }
    if (v->actor_events)
        for (unsigned i=0;i<v->world->inventory.count;++i)
            if (v->human_mask & (1u<<i))
            {
                const uint16_t songs[3]={
                    (v->actor_events->teepee_touch_mask & (1u<<i)) ? 371u : 0u,
                    (v->actor_events->teepee_protected_mask & (1u<<i)) ? 160u : 0u,
                    (v->actor_events->teepee_release_mask & (1u<<i)) ? 372u : 0u};
                for (unsigned k=0;k<3;++k)
                    if (songs[k] && !cue(&out,songs[k],PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,256))
                        return fail(error,capacity,"teepee cue budget");
            }
    if (v->actor_events)
        for (unsigned i = 0; i < v->world->inventory.count; ++i)
            if ((v->actor_events->boo_touch_mask & v->human_mask) & (1u << i))
                if (!cue(&out,380,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,256))
                    return fail(error,capacity,"Boo cue budget");
    for (unsigned i = 0; i < v->world->inventory.count; ++i)
        if (v->human_mask & (1u << i))
        {
            const uint16_t song = portable_gp_item_hud_tick(&next.hud[i], v->finished);
            if (song && !cue(&out, song, 7, 256))
                return fail(error, capacity, "HUD cue budget exhausted");
        }
    if (v->source_actor_config && v->source_actor_config->thundercloud_count)
    {
        uint8_t active=0;
        for(unsigned i=0;i<v->world->inventory.count;++i)
        {
            if(v->world->status.racers[i].lightning_ticks)active|=(uint8_t)(1u<<i);
            else if(owner->initialized && (owner->cloud_lightning_active_mask & v->human_mask & (1u<<i)) &&
                    !v->finished && v->racers[i].phase==PORTABLE_MESH_RACER_RACING)
                if(!cue(&out,133,PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER,256))
                    return fail(error,capacity,"cloud lightning expiry cue budget");
        }
        next.cloud_lightning_active_mask=active;
    }
    else next.cloud_lightning_active_mask=0;
    next.initialized = 1;
    next.ordinal = v->ordinal;
    music(&next, v, &out);
    *owner = next;
    *output = out;
    return 1;
}
