#include "mesh_items_state.h"
static void word(uint64_t *hash, uint32_t value)
{
    for (unsigned byte = 0; byte < 4u; ++byte)
    {
        *hash ^= (value >> (byte * 8u)) & 255u;
        *hash *= UINT64_C(1099511628211);
    }
}
uint64_t portable_mesh_items_checksum(const PortableMeshItems *w)
{
    if (!w)
        return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
#define ADD(value) word(&hash, (uint32_t)(value))
    ADD(UINT32_C(0x3153494d)); /* MIS1, independent of the 44-word kart state. */
    ADD(w->tick);
    ADD(w->serial);
    ADD(w->prepared);
    ADD(w->inventory.seed);
    ADD(w->inventory.count);
    ADD(w->inventory.human_mask);
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_RACERS; ++i)
    {
        const PortableMeshItemRacer *r = &w->inventory.racers[i];
        ADD(r->held_item); ADD(r->preview_item); ADD(r->settled_item);
        ADD(r->roulette_index); ADD(r->delay); ADD(r->timeline_index);
        ADD(r->cpu_choice_delay); ADD(r->previous_item_pressed); ADD(r->active);
    }
    ADD(w->status.count);
    ADD(w->status.human_mask);
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_STATUS_AUXILIARIES; ++i)
    {
        ADD(w->status.auxiliaries[i].ticks);
        ADD(w->status.auxiliaries[i].owner);
    }
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_RACERS; ++i)
    {
        const PortableMeshItemRacerStatus *s = &w->status.racers[i];
        ADD(s->mushroom_ticks); ADD(s->golden_ticks); ADD(s->star_ticks); ADD(s->lightning_ticks);
        ADD(s->spin_ticks); ADD(s->hit_grace_ticks); ADD(s->boo_invisible_ticks); ADD(s->boo_lock_ticks);
        ADD(s->pending_mushroom);
    }
    ADD(w->boxes.count);
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_BOX_CAPACITY; ++i)
        ADD(w->boxes.cooldowns[i]);
    for (unsigned i = 0; i < PORTABLE_MESH_ITEMS_ACTORS; ++i)
    {
        const PortableMeshItemActor *a = &w->actors[i];
        for (unsigned axis = 0; axis < 3u; ++axis) ADD(a->motion.position_q16[axis]);
        for (unsigned axis = 0; axis < 3u; ++axis) ADD(a->motion.velocity_q16[axis]);
        for (unsigned axis = 0; axis < 3u; ++axis) ADD(a->motion.support_normal_q30[axis]);
        ADD(a->motion.contact_triangle); ADD(a->motion.contact_surface);
        ADD(a->motion.respawn_index); ADD(a->motion.bounce_count);
        ADD(a->motion.mode); ADD(a->motion.grounded);
        ADD(a->route.course_id); ADD(a->route.route); ADD(a->route.from_node); ADD(a->route.to_node);
        ADD(a->route.selected_edge); ADD(a->route.progress_q16); ADD(a->route.transition_count);
        for (unsigned axis = 0; axis < 3u; ++axis) ADD(a->route.previous_position_q16[axis]);
        ADD(a->route.initialized); ADD(a->route.direct);
        ADD(a->serial); ADD(a->checkpoint); ADD(a->item); ADD(a->lifetime);
        ADD(a->grace); ADD(a->delay); ADD(a->angle); ADD(a->phase);
        ADD(a->owner); ADD(a->target); ADD(a->haunted);
    }
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_RACERS; ++i)
    {
        for (unsigned axis = 0; axis < 3u; ++axis) ADD(w->before_q16[i][axis]);
        ADD(w->controls[i].item_pressed); ADD(w->controls[i].aim_backward);
        ADD(w->controls[i].aim_forward); ADD(w->controls[i].recovering);
        ADD(w->release_lock[i]); ADD(w->cpu_wait[i]); ADD(w->cpu_hold[i]);
        ADD(w->previous_item[i]); ADD(w->standings[i]);
    }
#undef ADD
    return hash;
}
