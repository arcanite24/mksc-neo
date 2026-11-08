#include "mesh_battle.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh Battle line %d: %s\n", __LINE__, #c);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int latch_and_finish(void)
{
    PortableMeshBattle battle;
    const PortableMeshBattleConfig config = {2, 3, 1};
    PortableMeshBattleEvents events;
    CHECK(portable_mesh_battle_init(&battle, &config, NULL, 0));
    CHECK(battle.balloons[0] == 3 && battle.balloons[1] == 3 && !battle.finished);
    CHECK(portable_mesh_battle_step(&battle, 10, 1, &events, NULL, 0));
    CHECK(events.popped_mask == 1 && !events.eliminated_mask && !events.bomb_mask);
    CHECK(battle.balloons[0] == 2 && battle.hit_flags[0] == 0x8000);
    CHECK(battle.standings[0] == 1 && battle.ranks[0] == 2);
    PortableMeshBattle saved = battle;
    CHECK(portable_mesh_battle_step(&battle, 10, 3, &events, NULL, 0));
    CHECK(!events.popped_mask && !events.eliminated_mask && !events.bomb_mask &&
          !memcmp(&saved, &battle, sizeof battle));
    /* A second actual hit queues during latch clearing, then pops NEXT tick,
     * even with no new damage request. It is neither lost nor applied twice. */
    CHECK(portable_mesh_battle_step(&battle, 11, 1, &events, NULL, 0));
    CHECK(!events.popped_mask && battle.balloons[0] == 2 && battle.hit_flags[0] == 1);
    CHECK(portable_mesh_battle_step(&battle, 12, 0, &events, NULL, 0));
    CHECK(events.popped_mask == 1 && battle.balloons[0] == 1 && battle.hit_flags[0] == 0x8000);
    CHECK(portable_mesh_battle_step(&battle, 13, 0, &events, NULL, 0));
    CHECK(!events.popped_mask && battle.hit_flags[0] == 0);
    CHECK(portable_mesh_battle_step(&battle, 14, 1, &events, NULL, 0));
    CHECK(events.popped_mask == 1 && events.eliminated_mask == 1 && events.bomb_mask == 1);
    CHECK(!battle.balloons[0] && battle.elimination[0].revenge_bomb && battle.finished);
    CHECK(battle.standings[0] == 1 && battle.ranks[1] == 1);
    saved = battle;
    CHECK(portable_mesh_battle_step(&battle, 15, 3, &events, NULL, 0));
    CHECK(!events.popped_mask && !events.eliminated_mask && !events.bomb_mask &&
          !memcmp(&saved, &battle, sizeof battle));
    return 0;
}

static int tied_standings(void)
{
    PortableMeshBattle battle;
    const PortableMeshBattleConfig config = {3, 2, 1};
    PortableMeshBattleEvents events;
    CHECK(portable_mesh_battle_init(&battle, &config, NULL, 0));
    CHECK(portable_mesh_battle_step(&battle, 0, 1, &events, NULL, 0));
    CHECK(battle.standings[0] == 1 && battle.standings[1] == 2 && battle.standings[2] == 0);
    CHECK(portable_mesh_battle_step(&battle, 1, 2, &events, NULL, 0));
    CHECK(battle.standings[0] == 2 && battle.standings[1] == 1 && battle.standings[2] == 0);
    CHECK(portable_mesh_battle_step(&battle, 2, 4, &events, NULL, 0));
    CHECK(battle.balloons[0] == 1 && battle.balloons[1] == 1 && battle.balloons[2] == 1);
    CHECK(battle.standings[0] == 2 && battle.standings[1] == 1 && battle.standings[2] == 0);
    CHECK(portable_mesh_battle_step(&battle, 3, 0, &events, NULL, 0));
    CHECK(portable_mesh_battle_step(&battle, 4, 7, &events, NULL, 0));
    CHECK(battle.finished && events.popped_mask == 7 && events.eliminated_mask == 7 &&
          events.bomb_mask == 7);
    CHECK(battle.standings[0] == 2 && battle.standings[1] == 1 && battle.standings[2] == 0);
    CHECK(battle.ranks[2] == 1 && battle.ranks[1] == 2 && battle.ranks[0] == 3);
    return 0;
}

static int roles_after_all_racers(void)
{
    PortableMeshBattle battle;
    PortableMeshBattleEvents events;
    const PortableMeshBattleConfig slow = {8, 1, 3};
    CHECK(portable_mesh_battle_init(&battle, &slow, NULL, 0));
    CHECK(portable_mesh_battle_step(&battle, 0, 0x81, &events, NULL, 0));
    CHECK(events.eliminated_mask == 0x81 && !events.bomb_mask && !battle.finished);
    CHECK(battle.elimination[0].remaining_ticks == 2 && battle.elimination[7].remaining_ticks == 2);
    CHECK(portable_mesh_battle_step(&battle, 1, 0, &events, NULL, 0));
    CHECK(!events.bomb_mask && battle.elimination[0].remaining_ticks == 1 &&
          battle.elimination[7].remaining_ticks == 1);
    CHECK(portable_mesh_battle_step(&battle, 2, 0, &events, NULL, 0));
    CHECK(events.bomb_mask == 0x81 && !events.popped_mask && !events.eliminated_mask);
    CHECK(battle.elimination[0].revenge_bomb && battle.elimination[7].revenge_bomb);
    /* Already eliminated racers cannot emit another POP/ELIM/BOMB transition. */
    CHECK(portable_mesh_battle_step(&battle, 3, 0x81, &events, NULL, 0));
    CHECK(!events.popped_mask && !events.eliminated_mask && !events.bomb_mask);
    const PortableMeshBattleConfig four = {4, 1, 1};
    CHECK(portable_mesh_battle_init(&battle, &four, NULL, 0));
    CHECK(portable_mesh_battle_step(&battle, 0, 7, &events, NULL, 0));
    CHECK(battle.finished && battle.standings[0] == 3 && battle.ranks[3] == 1 &&
          battle.elimination[0].revenge_bomb && battle.elimination[1].revenge_bomb &&
          battle.elimination[2].revenge_bomb && !battle.elimination[3].revenge_bomb);
    return 0;
}

static int invalid_is_transactional(void)
{
    PortableMeshBattle battle;
    const PortableMeshBattleConfig config = {2, 3, 1};
    PortableMeshBattleEvents events = {12, 34, 56};
    const PortableMeshBattleEvents events_before = events;
    CHECK(portable_mesh_battle_init(&battle, &config, NULL, 0));
    PortableMeshBattle before = battle;
    const PortableMeshBattleConfig invalid[] = {{1, 3, 1}, {9, 3, 1}, {2, 0, 1},
                                                {2, 4, 1}, {2, 3, 0}, {2, 3, 128}};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        CHECK(!portable_mesh_battle_init(&battle, &invalid[i], NULL, 0) &&
              !memcmp(&before, &battle, sizeof battle));
    CHECK(!portable_mesh_battle_init(&battle, NULL, NULL, 0));
    CHECK(!portable_mesh_battle_step(&battle, 0, 4, &events, NULL, 0) &&
          !memcmp(&events, &events_before, sizeof events) &&
          !memcmp(&battle, &before, sizeof battle));
    CHECK(!portable_mesh_battle_step(NULL, 0, 0, &events, NULL, 0));
    CHECK(!portable_mesh_battle_step(&battle, 0, 0, NULL, NULL, 0));
    CHECK(portable_mesh_battle_step(&battle, 100, 0, &events, NULL, 0));
    before = battle;
    events = events_before;
    CHECK(!portable_mesh_battle_step(&battle, 99, 0, &events, NULL, 0) &&
          !memcmp(&before, &battle, sizeof battle) &&
          !memcmp(&events, &events_before, sizeof events));
    CHECK(!portable_mesh_battle_step(&battle, 102, 0, &events, NULL, 0) &&
          !memcmp(&before, &battle, sizeof battle) &&
          !memcmp(&events, &events_before, sizeof events));
    battle.standings[1] = battle.standings[0];
    before = battle;
    CHECK(!portable_mesh_battle_step(&battle, 101, 0, &events, NULL, 0) &&
          !memcmp(&before, &battle, sizeof battle) &&
          !memcmp(&events, &events_before, sizeof events));
    CHECK(portable_mesh_battle_init(&battle, &config, NULL, 0));
    CHECK(portable_mesh_battle_step(&battle, UINT32_MAX, 0, &events, NULL, 0));
    before = battle;
    events = events_before;
    CHECK(!portable_mesh_battle_step(&battle, 0, 0, &events, NULL, 0) &&
          !memcmp(&before, &battle, sizeof battle) &&
          !memcmp(&events, &events_before, sizeof events));
    return 0;
}

int main(void)
{
    CHECK(!latch_and_finish() && !tied_standings() && !roles_after_all_racers() &&
          !invalid_is_transactional());
    puts("Mesh Battle: PASS (queued damage, roles, stable ties, frozen results, rollback)");
    return 0;
}
