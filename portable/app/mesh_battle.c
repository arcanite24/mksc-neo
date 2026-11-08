#include "mesh_battle.h"
#include <stdio.h>

static int fail(char *error, size_t capacity, uint32_t tick, const char *state)
{
    if (error && capacity)
        snprintf(error, capacity, "3D Battle tick=%u state=%s", (unsigned)tick, state);
    return 0;
}

static int config_valid(const PortableMeshBattleConfig *config)
{
    return config && config->racer_count >= 2 && config->racer_count <= 8 &&
           config->starting_balloons >= 1 && config->starting_balloons <= 3 &&
           config->elimination_ticks >= 1 && config->elimination_ticks <= 127;
}

static int valid(const PortableMeshBattle *battle)
{
    if (!battle || !config_valid(&battle->config) || battle->tick_valid > 1 || battle->finished > 1)
        return 0;
    unsigned seen = 0, survivors = 0;
    for (unsigned i = 0; i < battle->config.racer_count; ++i)
    {
        const PortableBattleEliminationState *role = &battle->elimination[i];
        if ((battle->hit_flags[i] & ~UINT16_C(0x8001)) ||
            battle->balloons[i] > battle->config.starting_balloons || role->revenge_bomb > 1 ||
            role->remaining_ticks > battle->config.elimination_ticks ||
            (role->revenge_bomb && role->remaining_ticks) ||
            (battle->balloons[i] && (role->revenge_bomb || role->remaining_ticks)) ||
            (!battle->balloons[i] && !role->revenge_bomb && !role->remaining_ticks))
            return 0;
        survivors += battle->balloons[i] != 0;
        const unsigned racer = battle->standings[i];
        if (racer >= battle->config.racer_count || (seen & (1u << racer)) ||
            battle->ranks[racer] != i + 1u ||
            (i && battle->balloons[battle->standings[i - 1u]] < battle->balloons[racer]))
            return 0;
        seen |= 1u << racer;
    }
    return battle->finished == (survivors <= 1);
}

int portable_mesh_battle_init(PortableMeshBattle *out, const PortableMeshBattleConfig *config,
                              char *error, size_t capacity)
{
    if (!out || !config_valid(config))
        return fail(error, capacity, 0, "invalid configuration");
    PortableMeshBattle next = {.config = *config};
    for (unsigned i = 0; i < config->racer_count; ++i)
    {
        next.balloons[i] = config->starting_balloons;
        next.standings[i] = (uint8_t)i;
        next.ranks[i] = (uint8_t)(i + 1u);
    }
    *out = next;
    return 1;
}

static void standings(PortableMeshBattle *battle)
{
    /* Insertion moves strictly greater counts only, preserving the preceding
     * accepted order for every tie, including a simultaneous final loss. */
    for (unsigned i = 1; i < battle->config.racer_count; ++i)
    {
        const uint8_t racer = battle->standings[i];
        unsigned j = i;
        while (j && battle->balloons[racer] > battle->balloons[battle->standings[j - 1u]])
        {
            battle->standings[j] = battle->standings[j - 1u];
            --j;
        }
        battle->standings[j] = racer;
    }
    for (unsigned i = 0; i < battle->config.racer_count; ++i)
        battle->ranks[battle->standings[i]] = (uint8_t)(i + 1u);
}

int portable_mesh_battle_step(PortableMeshBattle *owner, uint32_t accepted_tick,
                              uint32_t damage_mask, PortableMeshBattleEvents *events, char *error,
                              size_t capacity)
{
    if (!events || !valid(owner) || (damage_mask >> owner->config.racer_count))
        return fail(error, capacity, accepted_tick, "invalid owner/damage/output");
    PortableMeshBattleEvents out = {0};
    if (owner->finished || (owner->tick_valid && owner->accepted_tick == accepted_tick))
    {
        *events = out;
        return 1;
    }
    if (owner->tick_valid &&
        (owner->accepted_tick == UINT32_MAX || accepted_tick != owner->accepted_tick + 1u))
        return fail(error, capacity, accepted_tick, "stale or skipped live tick");
    PortableMeshBattle next = *owner;
    PortableBattleBalloonEvent transitions[PORTABLE_MESH_BATTLE_RACERS] = {0};
    unsigned survivors = 0;
    for (unsigned i = 0; i < next.config.racer_count; ++i)
    {
        if (damage_mask & (1u << i))
            next.hit_flags[i] |= 1u;
        transitions[i] = portable_battle_balloon_tick(&next.hit_flags[i], &next.balloons[i], 0);
        if (transitions[i] != PORTABLE_BATTLE_BALLOON_NONE)
            out.popped_mask |= 1u << i;
        if (transitions[i] == PORTABLE_BATTLE_BALLOON_ELIMINATED)
            out.eliminated_mask |= 1u << i;
        survivors += next.balloons[i] != 0;
    }
    /* Role changes follow the complete C444 pass, never one racer's move. */
    for (unsigned i = 0; i < next.config.racer_count; ++i)
    {
        const uint8_t was_bomb = next.elimination[i].revenge_bomb;
        if (!portable_battle_elimination_tick(&next.elimination[i], transitions[i],
                                              next.config.elimination_ticks))
            return fail(error, capacity, accepted_tick, "invalid elimination duration");
        if (!was_bomb && next.elimination[i].revenge_bomb)
            out.bomb_mask |= 1u << i;
    }
    standings(&next);
    next.accepted_tick = accepted_tick;
    next.tick_valid = 1;
    next.finished = survivors <= 1;
    *owner = next;
    *events = out;
    return 1;
}
