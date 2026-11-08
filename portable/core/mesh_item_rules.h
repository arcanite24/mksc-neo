#ifndef PORTABLE_MESH_ITEM_RULES_H
#define PORTABLE_MESH_ITEM_RULES_H
#include "item_roulette.h"
#include "mesh_kart_rules.h"
#include <stdint.h>

/* MIR1/v1 stores SI metres, seconds-based rates and ratios as Q16. Durations
 * count accepted fixed ticks. This list is the explicit LE scalar wire order;
 * neither structure padding nor source/GBA speed units are serialized. */
#define PORTABLE_MESH_ITEM_FIELDS(X)                                                               \
    X(actors_capacity, 1, 64)                                                                      \
    X(actors_radius_q16, 3277, 524288)                                                             \
    X(actors_lifetime_ticks, 1, 65535)                                                             \
    X(actors_owner_grace_ticks, 0, 65535)                                                          \
    X(actors_release_lock_ticks, 1, 32767)                                                         \
    X(actors_hold_distance_q16, 0, 524288)                                                         \
    X(actors_hold_height_q16, 0, 524288)                                                           \
    X(actors_orbit_radius_q16, 1, 524288)                                                          \
    X(actors_orbit_turn_rate_q16, 1, 262144)                                                       \
    X(actors_orbit_delay_0, 0, 32767)                                                              \
    X(actors_orbit_delay_1, 0, 32767)                                                              \
    X(actors_orbit_delay_2, 0, 32767)                                                              \
    X(shell_speed_q16, 1, 16777216)                                                                \
    X(shell_launch_bonus_q16, 0, 16777216)                                                         \
    X(shell_acceleration_q16, 0, 67108864)                                                         \
    X(shell_restitution_q16, 0, 65536)                                                             \
    X(shell_max_bounces, 1, 64)                                                                    \
    X(shell_homing_turn_rate_q16, 1, 262144)                                                       \
    X(shell_homing_range_q16, 1, 268435456)                                                        \
    X(shell_ground_probe_q16, 1, 524288)                                                           \
    X(shell_ground_snap_q16, 0, 524288)                                                            \
    X(shell_trap_arm_ticks, 1, 32767)                                                              \
    X(shell_trap_lifetime_ticks, 1, 65535)                                                         \
    X(shell_trap_lock_ticks, 1, 32767)                                                             \
    X(banana_throw_speed_q16, 1, 16777216)                                                         \
    X(banana_throw_up_speed_q16, 0, 16777216)                                                      \
    X(banana_gravity_q16, 1, 67108864)                                                             \
    X(hit_spin_ticks, 1, 32767)                                                                    \
    X(hit_invulnerability_ticks, 1, 32767)                                                         \
    X(hit_speed_ratio_q16, 0, 65536)                                                               \
    X(hit_impulse_q16, 0, 16777216)                                                                \
    X(mushroom_boost_ticks, 1, 8192)                                                               \
    X(mushroom_acceleration_q16, 0, 67108864)                                                      \
    X(mushroom_cap_ratio_q16, 65536, 262144)                                                       \
    X(mushroom_golden_ticks, 1, 65535)                                                             \
    X(star_player_ticks, 1, 32767)                                                                 \
    X(star_cpu_ticks, 1, 32767)                                                                    \
    X(star_acceleration_q16, 0, 67108864)                                                          \
    X(star_cap_ratio_q16, 65536, 262144)                                                           \
    X(lightning_duration_ticks, 48, 32767)                                                         \
    X(lightning_speed_ratio_q16, 3277, 65536)                                                      \
    X(lightning_scale_ratio_q16, 3277, 65536)                                                      \
    X(boo_invisibility_ticks, 1, 32767)                                                            \
    X(boo_theft_lock_ticks, 1, 32767)                                                              \
    X(cpu_decision_ticks, 1, 32767)                                                                \
    X(cpu_hold_ticks, 1, 32767)                                                                    \
    X(cpu_use_distance_q16, 1, 268435456)                                                          \
    X(cpu_backward_distance_q16, 0, 268435456)

enum
{
    PORTABLE_MESH_ITEM_RULES_KIND = 103,
    PORTABLE_MESH_ITEM_RULES_VERSION = 1,
    PORTABLE_MESH_ITEM_RANKS = 8,
    PORTABLE_MESH_ITEM_WEIGHT_CAPACITY = 16,
    PORTABLE_MESH_ITEM_MAX_PROFILES = 256,
#define MESH_ITEM_COUNT(name, minimum, maximum) +1
    PORTABLE_MESH_ITEM_SCALAR_WORDS = 0 PORTABLE_MESH_ITEM_FIELDS(MESH_ITEM_COUNT),
#undef MESH_ITEM_COUNT
    PORTABLE_MESH_ITEM_RULES_BYTES = 32 + PORTABLE_MESH_ITEM_SCALAR_WORDS * 4 + (10 + 27) * 4 +
        2 * 8 * 16 * 4
};
typedef struct
{
    uint32_t profile_id, revision;
#define MESH_ITEM_MEMBER(name, minimum, maximum) int32_t name;
    PORTABLE_MESH_ITEM_FIELDS(MESH_ITEM_MEMBER)
#undef MESH_ITEM_MEMBER
    PortableGpRouletteRules roulette; /* Only preview/timeline have mesh meaning. */
    PortableGpItemWeight player_weights[8][16], cpu_weights[8][16];
} PortableMeshItemRules;
/* Engine item IDs: the Super Circuit set in its order, then the items it
 * lacks, which other games' rules award. */
enum
{
    PORTABLE_MESH_ITEM_GREEN_SHELL = 0x1000,
    PORTABLE_MESH_ITEM_TRIPLE_GREEN = 0x1001,
    PORTABLE_MESH_ITEM_RED_SHELL = 0x1002,
    PORTABLE_MESH_ITEM_TRIPLE_RED = 0x1003,
    PORTABLE_MESH_ITEM_SPINY_SHELL = 0x1004,
    PORTABLE_MESH_ITEM_SINGLE_BANANA = 0x1005,
    PORTABLE_MESH_ITEM_MUSHROOM = 0x1007,
    PORTABLE_MESH_ITEM_TRIPLE_MUSHROOM = 0x1008,
    PORTABLE_MESH_ITEM_GOLDEN_MUSHROOM = 0x1009,
    PORTABLE_MESH_ITEM_STAR = 0x100a,
    PORTABLE_MESH_ITEM_BOO = 0x100b,
    PORTABLE_MESH_ITEM_LIGHTNING = 0x100c,
    PORTABLE_MESH_ITEM_DOUBLE_MUSHROOM = 0x100f, /* the triple's successor; MK64 awards it */
    PORTABLE_MESH_ITEM_FAKE_BOX = 0x1010,
    PORTABLE_MESH_ITEM_BOMB = 0x1011,
    PORTABLE_MESH_ITEM_TRIPLE_BANANAS = 0x1012,
    PORTABLE_MESH_ITEM_BULLET = 0x1013,
    PORTABLE_MESH_ITEM_BLOOPER = 0x1014,
    PORTABLE_MESH_ITEM_BANANA_BUNCH = 0x1015,
    PORTABLE_MESH_ITEM_LAST = 0x1015
};
/* Engine constants of those items (the MIR1/v1 wire format has no fields for
 * them yet): Bob-omb fuse after release, blast duration and radius; Bullet
 * Bill ride; Blooper ink on the racers ahead. */
enum
{
    PORTABLE_MESH_BOMB_FUSE_TICKS = 150,
    PORTABLE_MESH_BOMB_BLAST_TICKS = 40,
    PORTABLE_MESH_BOMB_BLAST_RADIUS_Q16 = 5 * 65536,
    PORTABLE_MESH_BULLET_TICKS = 420,
    PORTABLE_MESH_INK_TICKS = 300
};
/* Award tables use canonical inventory IDs only. The golden successor is a
 * valid preview/state, never a fresh roulette award;100d has no handler. */
static inline int portable_mesh_item_id_valid(uint32_t item, int preview)
{
    return (item >= 0x1000u && item <= 0x100cu) || item == 0x100fu ||
           (item >= 0x1010u && item <= PORTABLE_MESH_ITEM_LAST) || (preview && item == 0x100eu);
}
static inline int portable_mesh_item_rules_valid(const PortableMeshItemRules *rules)
{
    if (!rules || !rules->profile_id || rules->profile_id == UINT32_MAX || !rules->revision)
        return 0;
#define MESH_ITEM_CHECK(name, minimum, maximum)                                                    \
    if (rules->name < minimum || rules->name > maximum)                                            \
        return 0;
    PORTABLE_MESH_ITEM_FIELDS(MESH_ITEM_CHECK)
#undef MESH_ITEM_CHECK
    if (rules->actors_release_lock_ticks > rules->shell_trap_arm_ticks ||
        rules->shell_trap_lock_ticks > rules->shell_trap_lifetime_ticks ||
        rules->shell_ground_snap_q16 > rules->shell_ground_probe_q16)
        return 0;
    for (unsigned i = 0; i < 10u; ++i)
        if (!portable_mesh_item_id_valid(rules->roulette.preview[i], 1))
            return 0;
    for (unsigned i = 0; i < 27u; ++i)
        if (!rules->roulette.timeline[i] || rules->roulette.timeline[i] > 32767u)
            return 0;
    for (unsigned domain = 0; domain < 2u; ++domain)
        for (unsigned rank = 0; rank < 8u; ++rank)
        {
            const PortableGpItemWeight *row =
                domain ? rules->cpu_weights[rank] : rules->player_weights[rank];
            uint32_t total = 0, seen = 0;
            int ended = 0;
            for (unsigned i = 0; i < 16u; ++i)
            {
                if (!row[i].item)
                {
                    if (row[i].weight)
                        return 0;
                    ended = 1;
                    continue;
                }
                if (ended || !portable_mesh_item_id_valid(row[i].item, 0) || !row[i].weight)
                    return 0;
                const uint32_t bit = UINT32_C(1) << (row[i].item - 0x1000u);
                if (seen & bit)
                    return 0;
                seen |= bit;
                total += row[i].weight;
                if (total > 65535u)
                    return 0;
            }
            if (!total)
                return 0;
        }
    return 1;
}
static inline int portable_mesh_item_rules_fit_profile(const PortableMeshItemRules *items,
                                                       const PortableMeshKartRules *kart)
{
    return items && kart &&
           (int64_t)kart->forward_speed_q16 * items->mushroom_cap_ratio_q16 <=
               INT64_C(16777216) * 65536 &&
           (int64_t)kart->forward_speed_q16 * items->star_cap_ratio_q16 <=
               INT64_C(16777216) * 65536;
}
#endif
