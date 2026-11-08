#include "gp_roster_fixture.h"
#include <stdint.h>
#include <stdio.h>

#include "gp_race_roster.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, \
            #condition); return 1; } } while (0)

int main(void)
{
    PortableGpRaceRoster roster;
    PortableGpRacer *player;

    CHECK(test_roster_init(&roster,24u,7u) ==
          PORTABLE_GP_ROSTER_OK);
    player = &roster.racers[0];
    player->item_inventory.held_item_17a = UINT16_C(0x1008);
    player->item_inventory.settled_item = UINT16_C(0x1008);
    player->control = UINT16_C(0x00c0);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(player->item_inventory.held_item_17a == UINT16_C(0x100f) &&
          player->item_inventory.settled_item == UINT16_C(0x100f) &&
          player->item_response_flags_b2 == UINT16_C(0x8000) &&
          player->mushroom_mode_84 == 0u && roster.item_uses == 1u);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_OK);
    CHECK(player->item_inventory.held_item_17a == UINT16_C(0x1007));
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_OK);
    CHECK(player->item_inventory.held_item_17a == 0u && roster.item_uses == 3u && roster.triple_mushroom_uses==1u);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);

    player->item_inventory.held_item_17a = UINT16_C(0x1009);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_OK);
    CHECK(player->item_inventory.held_item_17a == UINT16_C(0x100e) &&
          player->golden_mushroom_ticks == test_mushroom_rules()->golden_actor_ticks && roster.golden_mushroom_uses==1u);
    for (unsigned tick=0u; tick<(test_mushroom_rules()->golden_actor_ticks-1u); ++tick) {
        CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u) == PORTABLE_GP_ROSTER_OK);
        CHECK(player->item_inventory.held_item_17a == UINT16_C(0x100e) &&
              player->golden_mushroom_ticks == (test_mushroom_rules()->golden_actor_ticks-1u)-tick);
    }
    CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u) == PORTABLE_GP_ROSTER_OK);
    CHECK(player->item_inventory.held_item_17a == 0u &&
          player->item_inventory.settled_item == 0u && player->golden_mushroom_ticks == 0u && roster.golden_mushroom_uses==1u);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    for (unsigned gate=0u;gate<4u;++gate) {
        player->item_inventory.held_item_17a=UINT16_C(0x1007);
        player->source_flags=gate==0u?UINT32_C(0x800):gate==1u?UINT32_C(0x10000):0u;
        player->item_use_state_17c=(uint8_t)(gate==2u);
        player->control=gate==3u?0u:UINT16_C(0x80);
        CHECK(portable_gp_race_roster_use_mushroom(&roster,0u) == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
        CHECK(player->item_inventory.held_item_17a==UINT16_C(0x1007));
    }
    /* Single mushrooms occupy a source node for30 actor turns; the first
     * two triple uses allocate and immediately return theirs. */
    CHECK(test_roster_init(&roster,24u,7u)==PORTABLE_GP_ROSTER_OK);
    player=&roster.racers[0];player->control=0x80u;
    player->item_inventory.held_item_17a=0x1008u;
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_item_pool_count(&roster)==0u);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_item_pool_count(&roster)==0u);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_item_pool_count(&roster)==1u);
    for(unsigned tick=0u;tick+1u<test_mushroom_rules()->single_actor_ticks;++tick){
        CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_item_pool_count(&roster)==1u);
    }
    CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_item_pool_count(&roster)==0u);

    /* A transient triple constructor still evicts the oldest shared shell. */
    CHECK(test_roster_init(&roster,24u,7u)==PORTABLE_GP_ROSTER_OK);
    player=&roster.racers[0];player->control=0x80u;
    for(unsigned i=0u;i<16u;++i){
        roster.green_pool[i].shell.occupied=1u;
        roster.green_pool[i].shell.flags_34=3u;
        roster.green_pool[i].shell.creation_order=i+1u;
    }
    roster.item_uses=16u;player->item_inventory.held_item_17a=0x1008u;
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(!roster.green_pool[0].shell.occupied && portable_gp_item_pool_count(&roster)==15u);
    player->item_inventory.held_item_17a=0x1009u;
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_item_pool_count(&roster)==16u);
    /* Golden repeat neither resets nor allocates a second lasting timer. */
    CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(player->golden_mushroom_ticks==(test_mushroom_rules()->golden_actor_ticks-1u) && portable_gp_item_pool_count(&roster)==15u);
    CHECK(!roster.green_pool[1].shell.occupied);

    /* Reusing a golden node cancels its expiry callback without clearing the
     * owner's inventory; normal expiry clears even replacement inventory. */
    CHECK(test_roster_init(&roster,24u,7u)==PORTABLE_GP_ROSTER_OK);
    player=&roster.racers[0];player->control=0x80u;
    player->item_inventory.held_item_17a=0x1009u;
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    for(unsigned i=0u;i<15u;++i)
        portable_gp_item_pool_construct_mushroom(&roster,1u,30u,0u);
    portable_gp_item_pool_construct_mushroom(&roster,1u,30u,0u);
    CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(player->golden_mushroom_ticks==0u && player->item_inventory.held_item_17a==0x100eu);
    player->item_inventory.held_item_17a=0x1009u;
    CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    player->item_inventory.held_item_17a=0x100au;
    for(unsigned tick=0u;tick<test_mushroom_rules()->golden_actor_ticks;++tick)
        CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==PORTABLE_GP_ROSTER_OK);
    CHECK(player->item_inventory.held_item_17a==0u);
    {
        PortableMushroomRules rules={3u,2u,4u,7u,1000u*65536u};
        CHECK(test_roster_init(&roster,24u,7u)==PORTABLE_GP_ROSTER_OK);
        player=&roster.racers[0];player->mushroom_rules=&rules;player->control=0x80u;
        player->item_inventory.held_item_17a=0x1007u;
        CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==PORTABLE_GP_ROSTER_OK&&portable_gp_item_pool_count(&roster)==1u);
        CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==0&&portable_gp_item_pool_count(&roster)==1u);
        CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==0&&portable_gp_item_pool_count(&roster)==0u);
        player->item_inventory.held_item_17a=0x1009u;
        CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==0&&player->golden_mushroom_ticks==4u);
        for(unsigned tick=0u;tick<3u;++tick){
            CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==0&&player->golden_mushroom_ticks==3u-tick);
            CHECK(player->item_inventory.held_item_17a==0x100eu);
        }
        CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==0&&player->item_inventory.held_item_17a==0u);
        rules.single_actor_ticks=0u;player->item_inventory.held_item_17a=0x1007u;
        CHECK(portable_gp_race_roster_use_mushroom(&roster,0u)==0&&portable_gp_item_pool_count(&roster)==0u);
    }
    puts("GP mushroom: PASS triple successors, shared request, timed inventory and input gates");
    return 0;
}
