#include "mesh_items_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
int main(void)
{
    PortableMeshItems *a = calloc(1, sizeof *a), *b = malloc(sizeof *b);
    CHECK(a && b);
    CHECK(portable_mesh_items_checksum(NULL) == 0);
    const uint64_t zero = portable_mesh_items_checksum(a);
    /* Independent FNV-1a of MIS1 followed by 6905 zero LE words. */
    CHECK(zero == UINT64_C(0xb0fc6433f208a8a3));
#define FIELD(value) do { memcpy(b, a, sizeof *b); b->value = 1; \
    CHECK(portable_mesh_items_checksum(b) != zero); } while (0)
    FIELD(tick); FIELD(serial); FIELD(prepared);
    FIELD(inventory.seed); FIELD(inventory.count); FIELD(inventory.human_mask);
    FIELD(inventory.racers[7].held_item); FIELD(inventory.racers[7].preview_item);
    FIELD(inventory.racers[7].settled_item); FIELD(inventory.racers[7].roulette_index);
    FIELD(inventory.racers[7].delay); FIELD(inventory.racers[7].timeline_index);
    FIELD(inventory.racers[7].cpu_choice_delay); FIELD(inventory.racers[7].previous_item_pressed);
    FIELD(inventory.racers[7].active);
    FIELD(status.count); FIELD(status.human_mask); FIELD(status.auxiliaries[63].ticks);
    FIELD(status.auxiliaries[63].owner); FIELD(status.racers[7].mushroom_ticks);
    FIELD(status.racers[7].golden_ticks); FIELD(status.racers[7].star_ticks);
    FIELD(status.racers[7].lightning_ticks); FIELD(status.racers[7].spin_ticks);
    FIELD(status.racers[7].hit_grace_ticks); FIELD(status.racers[7].boo_invisible_ticks);
    FIELD(status.racers[7].boo_lock_ticks); FIELD(status.racers[7].pending_mushroom);
    FIELD(boxes.count); FIELD(boxes.cooldowns[4095]);
    FIELD(actors[63].motion.position_q16[2]); FIELD(actors[63].motion.velocity_q16[2]);
    FIELD(actors[63].motion.support_normal_q30[2]); FIELD(actors[63].motion.contact_triangle);
    FIELD(actors[63].motion.contact_surface); FIELD(actors[63].motion.respawn_index);
    FIELD(actors[63].motion.bounce_count); FIELD(actors[63].motion.mode); FIELD(actors[63].motion.grounded);
    FIELD(actors[63].route.course_id); FIELD(actors[63].route.route); FIELD(actors[63].route.from_node);
    FIELD(actors[63].route.to_node); FIELD(actors[63].route.selected_edge);
    FIELD(actors[63].route.progress_q16); FIELD(actors[63].route.transition_count);
    FIELD(actors[63].route.previous_position_q16[2]); FIELD(actors[63].route.initialized);
    FIELD(actors[63].route.direct); FIELD(actors[63].serial); FIELD(actors[63].checkpoint);
    FIELD(actors[63].item); FIELD(actors[63].lifetime); FIELD(actors[63].grace);
    FIELD(actors[63].delay); FIELD(actors[63].angle); FIELD(actors[63].phase);
    FIELD(actors[63].owner); FIELD(actors[63].target); FIELD(actors[63].haunted);
    FIELD(before_q16[7][2]); FIELD(controls[7].item_pressed); FIELD(controls[7].aim_backward);
    FIELD(controls[7].aim_forward); FIELD(controls[7].recovering);
    FIELD(release_lock[7]); FIELD(cpu_wait[7]); FIELD(cpu_hold[7]); FIELD(previous_item[7]);
    FIELD(standings[7]);
#undef FIELD
    /* Padding is deliberately absent, including tail padding in narrow structs. */
    memcpy(b, a, sizeof *b);
    const size_t tail = (size_t)((uint8_t *)&b->inventory.racers[0].active - (uint8_t *)b) + 1u;
    const size_t end = (size_t)((uint8_t *)&b->inventory.racers[1] - (uint8_t *)b);
    memset((uint8_t *)b + tail, 0xa5, end - tail);
    CHECK(portable_mesh_items_checksum(b) == zero);
    free(b); free(a);
    return 0;
}
