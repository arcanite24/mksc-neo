#include "gp_item_inventory.h"
#include "gp_item_hud.h"
#include "item_rules_fixture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void)
{
    const PortableGpRouletteRules roulette=test_roulette_rules();
    /* D4B4 emits six award pulses, ten theft pulses, and freezes at finish. */
    PortableGpItemHud hud={0};
    portable_gp_item_hud_transition(&hud,0u,0x1005u);
    unsigned awards=0u;
    for(unsigned t=0u;t<160u;++t){
        const uint16_t cue=portable_gp_item_hud_tick(&hud,0u);
        if(cue){CHECK(cue==104u && t%16u==0u && t<96u);++awards;}
        CHECK(hud.displayed_item==((t%16u)<8u?0x1005u:0u));
    }
    CHECK(awards==6u && hud.remaining==0u);
    CHECK(portable_gp_item_hud_tick(&hud,0u)==0u && hud.displayed_item==0x1005u);
    portable_gp_item_hud_transition(&hud,0u,0x1008u);
    portable_gp_item_hud_consume(&hud);
    CHECK(hud.current==0x100fu && hud.previous==0x1008u);
    for(unsigned t=0u;t<160u;++t){
        CHECK(portable_gp_item_hud_tick(&hud,0u)==0u);
        CHECK(hud.displayed_item==((t%16u)<8u?0x100fu:0x1008u));
    }
    portable_gp_item_hud_consume(&hud);
    CHECK(hud.current==0x1007u && hud.previous==0x100fu);
    portable_gp_item_hud_consume(&hud);
    CHECK(hud.remaining==0u && hud.displayed_item==0u);
    const uint16_t old_items[]={0x100bu,0x100bu,0x100bu,0x1007u};
    const uint16_t new_items[]={0x100au,0x100bu,0u,2u};
    for(unsigned i=0u;i<4u;++i){
        portable_gp_item_hud_transition(&hud,old_items[i],new_items[i]);
        PortableGpItemHud frozen=hud;
        CHECK(portable_gp_item_hud_tick(&hud,1u)==0u && memcmp(&hud,&frozen,sizeof hud)==0);
        unsigned pulses=0u;
        for(unsigned t=0u;t<160u;++t){
            uint16_t cue=portable_gp_item_hud_tick(&hud,0u);
            if(cue){CHECK(t%16u==8u && cue==(i==3u?131u:130u));++pulses;}
            if(t%16u==8u)CHECK(hud.displayed_item==(i==3u?0x1007u:i==1u?0u:0x100bu));
        }
        CHECK(pulses==(i==2u?0u:10u));
        CHECK(portable_gp_item_hud_tick(&hud,0u)==0u);
        CHECK(hud.displayed_item==(i==3u?0u:new_items[i]));
    }
    static const PortableGpItemWeight table[] = {
        {0x1005u,7u},{0x1000u,3u},{0x1007u,2u},{0x1002u,1u},
        {0x1001u,4u},{0u,0xffu}
    };
    /* Every boundary chooses its content record; leader selection retains
     * the extra coin-flip RNG sample before the ordinary weighted draw. */
    PortableGpPlayerItemRules rules={0};
    for(unsigned i=0u;i<11u;++i){
        rules.table[i][0]=(PortableGpItemWeight){(uint16_t)(0x1000u+i),1u};
        rules.table[i][1]=(PortableGpItemWeight){0u,255u};
    }
    static const uint32_t gaps[]={0u,4u,5u,8u,9u,12u,13u,16u,17u,20u,21u,24u,25u,28u,29u,32u,33u,UINT32_MAX};
    for(unsigned i=0u;i<sizeof gaps/sizeof gaps[0];++i){
        uint32_t random=123u,expected=random;
        (void)portable_gp_item_random(&expected);
        CHECK(portable_gp_item_select_player(&rules,&roulette,0u,gaps[i],8,&random)==(uint16_t)(0x1002u+i/2u));
        CHECK(random==expected);
    }
    for(uint32_t seed=0u;seed<64u;++seed){
        uint32_t random=seed,expected=seed;
        const unsigned selected=(portable_gp_item_random(&expected)&1u)?0u:1u;
        (void)portable_gp_item_random(&expected);
        CHECK(portable_gp_item_select_player(&rules,&roulette,1u,0u,8,&random)==0x1000u+selected);
        CHECK(random==expected);
    }
    PortableGpItemInventory opponent = {0};
    PortableGpItemInventory player = {.rules=&roulette};
    PortableGpItemInventory repeat = {.rules=&roulette};
    uint32_t random_a = UINT32_C(0xb3ffdb81);
    uint32_t random_b = random_a;
    uint16_t selected_a;
    uint16_t selected_b;

    CHECK(portable_gp_item_begin(&opponent, 0x8800u, 0x2000u, 0u, 0u) ==
          PORTABLE_GP_ITEM_OK);
    CHECK(opponent.held_item_17a == 1u && opponent.active == 1u &&
          opponent.roulette_index_70 == 30);
    for (unsigned tick = 0u; tick < 30u; ++tick)
        CHECK(portable_gp_item_tick(&opponent,0u,0x1000u) ==
              PORTABLE_GP_ITEM_ACTIVE);
    CHECK(opponent.roulette_index_70 == 0 && opponent.held_item_17a == 1u);
    CHECK(portable_gp_item_tick(&opponent,0u,0x1000u) ==
          PORTABLE_GP_ITEM_OK);
    CHECK(opponent.held_item_17a == 0x1000u && opponent.settled_item == 0x1000u &&
          portable_gp_item_hud_value(&opponent) == 0x1000u);
    CHECK(portable_gp_item_begin(&opponent,0x8800u,0x2000u,0u,0u) ==
          PORTABLE_GP_ITEM_INELIGIBLE);

    CHECK(portable_gp_item_begin(&player,0x8800u,0x8000u,1u,7u) ==
          PORTABLE_GP_ITEM_OK);
    CHECK(portable_gp_item_tick(&player,0u,0x1005u) == PORTABLE_GP_ITEM_ACTIVE);
    CHECK(portable_gp_item_hud_value(&player) == 0x1007u);
    CHECK(portable_gp_item_tick(&player,1u,0x1005u) == PORTABLE_GP_ITEM_ACTIVE);
    for (unsigned tick = 0u; tick < 64u && player.active; ++tick)
    {
        PortableGpItemStatus status =
            portable_gp_item_tick(&player,1u,0x1005u);
        CHECK(status == (player.active ? PORTABLE_GP_ITEM_ACTIVE
                                       : PORTABLE_GP_ITEM_OK));
    }
    CHECK(player.active == 0u);
    CHECK(portable_gp_item_hud_value(&player) == 0x1005u);

    /* All authored durations are consumed, including the final three. */
    memset(&repeat, 0, sizeof repeat);
    repeat.rules=&roulette;
    CHECK(portable_gp_item_begin(&repeat,0x8800u,0x8000u,1u,0u) ==
          PORTABLE_GP_ITEM_OK);
    {
        unsigned ticks = 0u;
        while (repeat.active && ticks < 200u)
        {
            const PortableGpItemStatus status=portable_gp_item_tick(&repeat,0u,0x100au);
            CHECK(status == (repeat.active ? PORTABLE_GP_ITEM_ACTIVE
                                         : PORTABLE_GP_ITEM_OK));
            ++ticks;
        }
        CHECK(ticks == 56u && repeat.timeline_index_78 == 27 &&
              repeat.held_item_17a == 0x100au);
    }

    selected_a = portable_gp_item_select_weighted(
        table,sizeof table/sizeof table[0],3,&random_a);
    selected_b = portable_gp_item_select_weighted(
        table,sizeof table/sizeof table[0],3,&random_b);
    CHECK(selected_a == selected_b && random_a == random_b && selected_a != 0u);
    CHECK(portable_gp_item_strength(0x1000u) == 1u &&
          portable_gp_item_strength(0x1001u) == 3u &&
          portable_gp_item_strength(0x1007u) == 0u);
    {
        uint32_t random=77u;
        CHECK(portable_gp_item_select_player(NULL,&roulette,1u,0u,8,&random)==0u && random==77u);
        CHECK(portable_gp_item_select_player(&rules,NULL,1u,0u,8,&random)==0u && random==77u);
        PortableGpItemInventory missing={.active=1u,.player_owned=1u,.held_item_17a=1u};
        CHECK(portable_gp_item_tick(&missing,0u,0x1007u)==PORTABLE_GP_ITEM_INVALID_ARGUMENT);
    }
    {
        typedef struct
        {
            uint16_t item;
            uint32_t primary;
            uint32_t secondary;
            uint16_t successor;
            PortableGpItemInventoryAction action;
        } ExpectedDispatch;
        static const ExpectedDispatch expected[] = {
            {0x1000u,0x08047368u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1001u,0x080473fcu,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1002u,0x080474ccu,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1003u,0x08047560u,0x08043310u,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1004u,0x08047630u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1005u,0x08048090u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1006u,0x08048134u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1007u,0x08048294u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x1008u,0x08048340u,0,0x100fu,PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR},
            {0x1009u,0x080482b8u,0,0x100eu,PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR},
            {0x100au,0x080485dcu,0x08043344u,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x100bu,0x08048694u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x100cu,0x080483a0u,0x08043378u,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x100du,0x08048090u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x100eu,0x080482e0u,0,0,PORTABLE_GP_ITEM_INVENTORY_KEEP},
            {0x100fu,0x08048370u,0,0x1007u,PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR},
            {0x2000u,0x08047368u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2001u,0x080473fcu,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2002u,0x080474ccu,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2003u,0x08047560u,0x08043310u,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2004u,0x08047630u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2005u,0x08048090u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2006u,0x08048134u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2007u,0x08048294u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2008u,0x08048090u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x2009u,0x08048090u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x200au,0x080485dcu,0x08043344u,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x200bu,0x08048090u,0,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
            {0x200cu,0x080483a0u,0x08043378u,0,PORTABLE_GP_ITEM_INVENTORY_CLEAR},
        };
        PortableGpItemUseDispatch dispatch;
        for (size_t index = 0u; index < sizeof expected / sizeof expected[0];
             ++index)
        {
            CHECK(portable_gp_item_use_dispatch(expected[index].item,
                                                &dispatch));
            CHECK(dispatch.primary_callback == expected[index].primary &&
                  dispatch.secondary_callback == expected[index].secondary &&
                  dispatch.successor_item == expected[index].successor &&
                  dispatch.inventory_action == expected[index].action);
        }
        CHECK(!portable_gp_item_use_dispatch(0u,&dispatch));
        CHECK(!portable_gp_item_use_dispatch(1u,&dispatch));
        CHECK(!portable_gp_item_use_dispatch(0x1000u,NULL));
        CHECK(portable_gp_item_use_dispatch(0x2222u,&dispatch) &&
              dispatch.primary_callback == 0x08048090u &&
              dispatch.inventory_action == PORTABLE_GP_ITEM_INVENTORY_CLEAR);
    }
    memset(&repeat,0,sizeof repeat);
    CHECK(portable_gp_item_begin(&repeat,0x8800u,0x20u,0u,0u) ==
          PORTABLE_GP_ITEM_INELIGIBLE);
    CHECK(portable_gp_item_begin(&repeat,0x1000u,0u,0u,0u) ==
          PORTABLE_GP_ITEM_INVALID_ARGUMENT);
    {
        const PortableBattleItemRules battle={.table={
            {{0x1007u,7u},{0u,255u}},
            {{0x1002u,9u},{0x100au,2u},{0x1005u,4u},{0x1000u,6u},{0u,255u}},
            {{0x1002u,11u},{0x100au,3u},{0x1005u,7u},{0x1000u,13u},{0u,255u}}},
            .box_hidden_ticks=120u};
        for (unsigned balloons=0u; balloons<4u; ++balloons) {
            const uint16_t total=balloons==0u?7u:balloons==1u?21u:34u;
            unsigned counts[13]={0};
            for (unsigned seed=0u; seed<32768u; ++seed) {
                uint32_t state=seed, expected_state=seed;
                int32_t roll=portable_gp_item_random(&expected_state)%total;
                const PortableGpItemWeight *row=battle.table[balloons>1u?2u:balloons];
                unsigned entry=0u;
                while ((roll-=row[entry].weight)>=0) ++entry;
                const uint16_t selected=portable_gp_item_select_battle(&battle,(uint8_t)balloons,8,&state);
                CHECK(selected==row[entry].item && state==expected_state);
                ++counts[selected-0x1000u];
                state=seed;
                CHECK(portable_gp_item_select_battle(&battle,(uint8_t)balloons,0,&state)==
                    (balloons==0u?0x1007u:0x100au));
                CHECK(state==expected_state);
            }
            CHECK(counts[balloons==0u?7u:2u]!=0u);
            CHECK(counts[1]==0u && counts[3]==0u && counts[9]==0u && counts[11]==0u && counts[12]==0u);
        }
        CHECK(portable_gp_item_select_battle(NULL,3u,8,NULL)==0u);
    }
    puts("GP item inventory: PASS source roulette timing, PRNG, weighted selection and HUD slot");
    return 0;
}
