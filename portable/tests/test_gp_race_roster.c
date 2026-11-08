#include "gp_roster_fixture.h"
#include "gp_race_roster.h"
#include "gp_roster_math.h"
#include "kart_motion.h"
#include "battle_navigation.h"
#include "driver_vector_effect.h"
#include "item_rules_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#condition); \
    return __LINE__; \
} } while (0)

static PortableGpRouletteRules fixture_roulette;
static PortableGpRosterStatus fixture_roster_init(PortableGpRaceRoster *roster,
    uint32_t course,uint8_t driver)
{
    fixture_roulette=test_roulette_rules();
    const PortableGpRosterStatus status=test_roster_init(roster,course,driver);
    if(status==PORTABLE_GP_ROSTER_OK){roster->roulette_rules=&fixture_roulette;roster->hazard_sound_rules=test_hazard_rules();}
    return status;
}
#define portable_gp_race_roster_init fixture_roster_init
static PortableGpRosterStatus fixture_roster_init_for_rules(PortableGpRaceRoster *roster,
    const PortableRaceRulesView *rules,uint8_t driver)
{
    const PortableGpRosterStatus status=test_roster_init_for_rules(roster,rules,driver);
    if(status==PORTABLE_GP_ROSTER_OK){roster->roulette_rules=test_roulette_profile();roster->hazard_sound_rules=test_hazard_rules();}
    return status;
}
#define portable_gp_race_roster_init_for_rules fixture_roster_init_for_rules

typedef struct {
    uint32_t before_serial[PORTABLE_GP_RACER_COUNT];
    unsigned count;
} AfterDriverFixture;

static PortableGpRosterStatus check_after_driver_order(void *context,
    PortableGpRaceRoster *roster,size_t index)
{
    AfterDriverFixture *fixture=context;
    if(index!=fixture->count+1u || roster->racers[index].is_player ||
       roster->racers[index].last_update_serial<=fixture->before_serial[index])
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    /* A batch callback after the entire roster would fail this assertion. */
    if(index+1u<portable_gp_race_roster_count(roster) &&
       roster->racers[index+1u].last_update_serial!=fixture->before_serial[index+1u])
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    ++fixture->count;
    return PORTABLE_GP_ROSTER_OK;
}

static int test_reserved_item(void)
{
    PortableGpRaceRoster roster;
    CHECK(portable_gp_race_roster_init(&roster,24u,0u)==0);
    PortableGpRacer *r=&roster.racers[0];
    r->is_player=1u;
    r->item_inventory.held_item_17a=0x1006u;
    r->control=0u;r->source_flags=0u;r->item_use_state_17c=0u;
    CHECK(portable_gp_race_roster_use_reserved_item(NULL,0u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(portable_gp_race_roster_use_reserved_item(&roster,8u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    for(unsigned gate=0u;gate<4u;++gate){
        r->control=gate==0u?0u:0x80u;
        r->source_flags=gate==1u?0x800u:gate==2u?0x10000u:0u;
        r->item_use_state_17c=(uint8_t)(gate==3u);
        CHECK(portable_gp_race_roster_use_reserved_item(&roster,0u)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
        CHECK(r->item_inventory.held_item_17a==0x1006u && roster.item_uses==0u);
    }
    r->source_flags=0u;r->item_use_state_17c=0u;
    for(unsigned alias=0u;alias<2u;++alias){
        r->item_inventory.held_item_17a=(uint16_t)(alias?0x2006u:0x1006u);
        r->item_inventory.preview_item=r->item_inventory.settled_item=0x1006u;
        const uint16_t response=r->item_response_flags_b2;
        CHECK(portable_gp_race_roster_use_reserved_item(&roster,0u)==0);
        CHECK(r->item_inventory.held_item_17a==0u && r->item_inventory.preview_item==0u &&
            r->item_inventory.settled_item==0u && !r->item_inventory.active);
        CHECK(r->item_response_flags_b2==response &&
            portable_gp_item_pool_count(&roster)==alias+1u && roster.player_reserved_uses==alias+1u);
    }
    CHECK(portable_gp_race_roster_tick_mushroom_actors(&roster,0u)==0);
    CHECK(portable_gp_item_pool_count(&roster)==2u && roster.mushroom_pool[0].ticks_remaining==0u);
    memset(roster.mushroom_pool,0,sizeof roster.mushroom_pool);
    for(unsigned i=0u;i<16u;++i)
        roster.green_pool[i].shell=(PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,
            .creation_order=i+1u,.flags_34=3u};
    roster.item_uses=16u;r->item_inventory.held_item_17a=0x1006u;
    CHECK(portable_gp_race_roster_use_reserved_item(&roster,0u)==0);
    CHECK(portable_gp_item_pool_count(&roster)==16u && !roster.green_pool[0].shell.occupied &&
        roster.green_pool[1].shell.occupied && roster.item_uses==17u);
    /* Battle's native CPU dispatcher shares the player effect; GP CPU odds
     * continue rejecting the source's unsupported item callback. */
    roster.race_mode_flags_7da=0x800u;roster.racer_count=4u;
    roster.cpu_items_enabled=1u;roster.battle_combat=(PortableBattleCombatRules){128u,4096u,48u};
    r=&roster.racers[1];r->is_player=0u;r->source_flags=0u;
    r->control=0x4000u;r->item_use_state_17c=0u;r->item_inventory.held_item_17a=0x1006u;
    roster.cpu_item_clock=7u;int32_t sine[1024]={0};
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
    CHECK(r->item_inventory.held_item_17a==0u && r->control==0x4000u && roster.cpu_reserved_uses==1u);
    return 0;
}

static int test_battle_navigation(void)
{
    static PortableBattleNavWorkspace work;
    uint8_t cells[16u*16u],flags[256];
    memset(cells,0,sizeof cells);memset(flags,0x40,sizeof flags);flags[1]=0x80u;
    PortableTerrainView terrain={cells,16u,16u,16u};
    PortableTrackMaterialView material={.flags=flags,.count=256u};
    PortableBattleAiRules rules={256u,128u,0x1000u,0x3000u,6u,64u};
    CHECK(portable_battle_ai_rules_valid(&rules));
    for (unsigned y=0u;y<16u;++y) cells[y*16u+8u]=1u;
    cells[12u*16u+8u]=cells[13u*16u+8u]=0u;
    const PortableBattleNavPoint start={24,24},goals[2]={{104,24},{104,104}};
    PortableBattleNavRoute route,repeated;
    CHECK(portable_battle_navigation_route(&work,&terrain,&material,&rules,start,goals,2u,&route));
    CHECK(route.reachable && route.waypoint.x<64 && route.waypoint.y>24);
    CHECK(portable_battle_navigation_route(&work,&terrain,&material,&rules,start,goals,2u,&repeated));
    CHECK(route.waypoint.x==repeated.waypoint.x && route.waypoint.y==repeated.waypoint.y &&
        route.goal==repeated.goal);
    cells[12u*16u+8u]=cells[13u*16u+8u]=1u;
    CHECK(portable_battle_navigation_route(&work,&terrain,&material,&rules,start,goals,2u,&route));
    CHECK(!route.reachable);
    /* Destroyed walls become immediately traversable; no stale graph. */
    for (unsigned y=0u;y<16u;++y) cells[y*16u+8u]=0u;
    CHECK(portable_battle_navigation_route(&work,&terrain,&material,&rules,start,goals,1u,&route));
    CHECK(route.reachable && route.waypoint.x>start.x && route.waypoint.y==start.y);
    rules.clearance=0u;
    CHECK(!portable_battle_navigation_route(&work,&terrain,&material,&rules,start,goals,1u,&route));
    rules.clearance=8u;CHECK(!portable_battle_ai_rules_valid(&rules));
    rules.clearance=6u;rules.turn_angle=rules.stop_angle;CHECK(!portable_battle_ai_rules_valid(&rules));
    return 0;
}

static int test_battle_red_release(void)
{
    PortableGpRaceRoster roster;
    int32_t sine[1024]={0};sine[256]=32768;
    CHECK(portable_gp_race_roster_init(&roster,24u,0u)==0);
    roster.race_mode_flags_7da=0x800u;roster.racer_count=4u;
    PortableGpRacer *r=&roster.racers[1];
    r->rank_flags_ba=6u;r->source_flags=0x2000u;r->item_use_state_17c=1u;
    r->shell_item_18c=PORTABLE_GP_RED_SHELL_ITEM;
    r->red_shell=(PortableGpRedShell){.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_RED_SHELL_KIND,
        .phase=PORTABLE_GP_RED_SHELL_ACTIVE};
    CHECK(portable_gp_race_roster_release_red_shell_forward_target(&roster,1u,sine,1024u)==0);
    CHECK(r->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET &&
        !r->red_shell.target_valid && r->rank_flags_ba==6u && r->item_use_state_17c==0u);
    return 0;
}

static int test_battle_cpu_combat(void)
{
    PortableGpRaceRoster roster;
    int32_t sine[1024]={0};sine[256]=65536;sine[768]=-65536;
    CHECK(portable_gp_race_roster_init(&roster,24u,0u)==0);
    roster.race_mode_flags_7da=0x800u;roster.racer_count=4u;
    roster.cpu_items_enabled=1u;roster.battle_combat=(PortableBattleCombatRules){128u,4096u,48u};
    for (unsigned i=0u;i<4u;++i) {
        roster.racers[i].position_x_q16=1000*65536;
        roster.racers[i].position_y_q16=1000*65536;
        roster.racers[i].balloons_remaining=3u;
        roster.racers[i].world_height_q16=0;
        roster.racers[i].lightning_state_78=0u;
        roster.racers[i].contact_status_66=0;
        roster.racers[i].hit_cue_flags_58=0u;
    }
    PortableGpRacer *r=&roster.racers[1],*target=&roster.racers[0];
    r->source_flags=0x2000u;r->heading=0u;r->is_player=0u;
    r->position_x_q16=128*65536;r->position_y_q16=128*65536;
    r->item_inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;
    roster.cpu_item_clock=7u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
    CHECK(roster.cpu_red_uses==1u && r->item_inventory.held_item_17a==0u);
    CHECK(portable_gp_race_roster_tick_red_shell_attached(&roster,1u,sine,1024u)==0);
    target->position_x_q16=128*65536;target->position_y_q16=100*65536;
    /* Empty inventory must still evaluate an attached weapon after its hold. */
    r->item_decision_timer_18e=0u;
    target->hit_cue_flags_58=0x8000u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
    CHECK(r->item_decision_190&1u);
    target->hit_cue_flags_58=0u;target->position_x_q16=156*65536;target->position_y_q16=128*65536;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
    CHECK(r->item_decision_190&1u);
    target->position_x_q16=128*65536;target->position_y_q16=-1*65536;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
    CHECK(r->item_decision_190&1u);
    target->position_y_q16=100*65536;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
    CHECK(!(r->item_decision_190&1u) && r->item_decision_timer_18e==48u);
    CHECK(portable_gp_race_roster_tick_red_shell_held_active(&roster,1u,sine,1024u)==0);
    CHECK(r->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET);
    target->source_flags=0x8400u;target->rank_flags_ba=0u;
    CHECK(portable_gp_race_roster_resolve_shell_victim(&roster,0u,
        target->position_x_q16,target->position_y_q16,0,0u,0u,0u,0u,sine,1024u)==0);
    CHECK((target->source_flags&0x400u)!=0u && (target->hit_state_flags_74&3u)==3u);
    for (unsigned red=0u;red<2u;++red) {
        CHECK(portable_gp_race_roster_init(&roster,24u,0u)==0);
        roster.race_mode_flags_7da=0x800u;roster.racer_count=4u;
        roster.cpu_items_enabled=1u;roster.battle_combat=(PortableBattleCombatRules){128u,4096u,48u};
        roster.shell_orbit_delay_valid=1u;memset(roster.shell_orbit_delay,0,sizeof roster.shell_orbit_delay);
        for (unsigned i=0u;i<4u;++i) {
            roster.racers[i].balloons_remaining=3u;roster.racers[i].world_height_q16=0;
            roster.racers[i].lightning_state_78=0u;roster.racers[i].contact_status_66=0;
            roster.racers[i].hit_cue_flags_58=0u;
            roster.racers[i].position_x_q16=1000*65536;roster.racers[i].position_y_q16=1000*65536;
        }
        r=&roster.racers[1];target=&roster.racers[0];
        r->source_flags=0x2000u;r->heading=0u;r->is_player=0u;
        r->position_x_q16=128*65536;r->position_y_q16=128*65536;
        target->position_x_q16=128*65536;target->position_y_q16=100*65536;
        r->item_inventory.held_item_17a=(uint16_t)(red?PORTABLE_GP_TRIPLE_RED_SHELL_ITEM:PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM);
        roster.cpu_item_clock=7u;
        CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
        PortableGpRedShellWorld world={.sine=sine,.sine_count=1024u};
        PortableGpRedShellRoute route={0};
        for (unsigned actor=0u;actor<PORTABLE_GP_GREEN_ACTOR_SLOTS;++actor) {
            if (red && portable_gp_red_actor(&roster,actor)->occupied)
                CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,actor,&world,&route)==0);
            if (!red && portable_gp_green_actor(&roster,actor)->occupied)
                CHECK(portable_gp_race_roster_tick_green_shell_orbit(&roster,actor,sine,1024u)==0);
        }
        r->item_decision_timer_18e=0u;
        CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,1u,0u,sine,1024u)==0);
        unsigned released=0u;
        for (unsigned actor=0u;actor<PORTABLE_GP_GREEN_ACTOR_SLOTS;++actor) {
            if (red && portable_gp_red_actor(&roster,actor)->occupied) {
                CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,actor,&world,&route)==0);
                released+=portable_gp_red_actor(&roster,actor)->phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET;
            }
            if (!red && portable_gp_green_actor(&roster,actor)->occupied) {
                CHECK(portable_gp_race_roster_tick_green_shell_orbit(&roster,actor,sine,1024u)==0);
                released+=portable_gp_green_actor(&roster,actor)->phase==PORTABLE_GP_GREEN_SHELL_RELEASED;
            }
        }
        CHECK(released==1u && (r->item_decision_190&1u)!=0u);
    }
    return 0;
}

static int test_battle_balloons(void)
{
    /* CC80: inclusive period, seven-bit phase, contact-bit preservation,
     * negative-speed ASR and end-on signed response selection. */
    uint8_t walk=1u;
    for (unsigned tick=1u;tick<=20u;++tick) {
        portable_battle_bomb_walk_tick(&walk,0);
        CHECK(walk==(uint8_t)((tick<<1u)|1u));
    }
    portable_battle_bomb_walk_tick(&walk,0);CHECK(walk==1u);
    CHECK(portable_battle_bomb_walk_period(-1)==21);
    CHECK(portable_battle_bomb_walk_period(256*65536)==18);
    CHECK(portable_battle_bomb_walk_frame(0u,0,1u,0u)==1u);
    CHECK(portable_battle_bomb_walk_frame(10u,0,1u,0u)==0u);
    CHECK(portable_battle_bomb_walk_frame(10u,0,65u,0u)==1u);
    CHECK(portable_battle_bomb_walk_frame(10u,0,0u,0u)==1u);
    CHECK(portable_battle_bomb_walk_frame(10u,0,63u,0x8000u)==0u);
    walk=255u;portable_battle_bomb_walk_tick(&walk,INT32_MIN);CHECK(walk==1u);
    walk=7u;portable_battle_bomb_walk_tick(&walk,INT32_MAX);CHECK(walk==1u);
    PortableGpRaceRoster roster;
    PortableBattleBalloonEvent event=PORTABLE_BATTLE_BALLOON_NONE;
    CHECK(portable_gp_race_roster_init(&roster,24u,0u)==PORTABLE_GP_ROSTER_OK);
    PortableGpRacer *r=&roster.racers[0];
    r->hit_state_flags_74=3u;
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==
          PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    CHECK(r->balloons_remaining==3u && r->hit_state_flags_74==3u);
    roster.race_mode_flags_7da=0x800u;
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==0);
    CHECK(event==PORTABLE_BATTLE_BALLOON_POP && r->balloons_remaining==2u &&
          r->hit_state_flags_74==0x8002u);
    r->hit_state_flags_74|=1u;
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==0);
    CHECK(event==PORTABLE_BATTLE_BALLOON_NONE && r->balloons_remaining==2u &&
          r->hit_state_flags_74==3u);
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==0);
    CHECK(event==PORTABLE_BATTLE_BALLOON_POP && r->balloons_remaining==1u);
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==0);
    r->hit_state_flags_74|=1u;
    /* High scene bits do not change the source low-nibble state gate. */
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,0x84u,&event)==0);
    CHECK(event==PORTABLE_BATTLE_BALLOON_NONE && r->balloons_remaining==1u &&
          r->hit_state_flags_74==2u);
    r->hit_state_flags_74|=1u;
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,0x83u,&event)==0);
    CHECK(event==PORTABLE_BATTLE_BALLOON_ELIMINATED && r->balloons_remaining==0u);
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==0);
    r->hit_state_flags_74|=1u;
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,&event)==0);
    CHECK(event==PORTABLE_BATTLE_BALLOON_NONE && r->balloons_remaining==0u &&
          r->hit_state_flags_74==2u);
    CHECK(portable_battle_race_roster_tick_balloons(NULL,0u,3u,&event)==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(portable_battle_race_roster_tick_balloons(&roster,8u,3u,&event)==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(portable_battle_race_roster_tick_balloons(&roster,0u,3u,NULL)==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    /* A complete round uses these same health writes for every racer,
     * then the recovered ranking and 180-tick result transition. */
    roster.race_phase_7d4=3u;
    for(unsigned owner=1u;owner<7u;++owner){
        for(unsigned hit=0u;hit<3u;++hit){
            roster.racers[owner].hit_state_flags_74|=1u;
            CHECK(portable_battle_race_roster_tick_balloons(&roster,owner,3u,&event)==0);
            CHECK(event==(hit==2u?PORTABLE_BATTLE_BALLOON_ELIMINATED:
                                  PORTABLE_BATTLE_BALLOON_POP));
            CHECK(portable_battle_race_roster_tick_balloons(&roster,owner,3u,&event)==0);
        }
    }
    CHECK(portable_battle_race_roster_tick_finish_transition(&roster)==0);
    CHECK(roster.standings[0]==7u && roster.finish_player_rank!=0u &&
          roster.finish_countdown==180u && roster.race_phase_7d4==4u);
    for(unsigned tick=0u;tick<181u;++tick)
        CHECK(portable_battle_race_roster_tick_finish_transition(&roster)==0);
    CHECK(roster.race_phase_7d4==5u);
    return 0;
}

static int test_live_item_pool(void)
{
    PortableGpRaceRoster roster={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&roster);int32_t sine[1024]={0};sine[256]=32768;
    PortableGpRacer *owner=&roster.racers[0];owner->control=0x80u;
    owner->green_shell=(PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=0x8401u,
        .phase=PORTABLE_GP_GREEN_SHELL_RELEASED,.flags_34=3u,.creation_order=5u,
        .position_x_q16=44*65536,.velocity_x_q16=123,.lifetime_38=8u};
    owner->item_inventory.held_item_17a=0x1000u;roster.item_uses=5u;
    CHECK(portable_gp_race_roster_use_green_shell(&roster,0u,sine,1024u)==0);
    CHECK(owner->green_shell.phase==PORTABLE_GP_GREEN_SHELL_ATTACHED &&
        roster.green_pool[0].shell.position_x_q16==44*65536 &&
        roster.green_pool[0].shell.velocity_x_q16==123 && roster.green_pool[0].owner==0u);
    CHECK(portable_gp_item_pool_count(&roster)==2u);
    owner->item_inventory.held_item_17a=0x1000u;
    const uint64_t before=portable_gp_race_roster_identity(&roster);
    CHECK(portable_gp_race_roster_use_green_shell(&roster,0u,sine,1024u)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    CHECK(portable_gp_race_roster_identity(&roster)==before);
    memset(&roster,0,sizeof roster);test_item_roster_profiles(&roster);
    for(unsigned i=0u;i<16u;++i){
        roster.green_pool[i].owner=3u;
        roster.green_pool[i].shell=(PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=0x8401u,
            .phase=PORTABLE_GP_GREEN_SHELL_RELEASED,.flags_34=3u,
            .creation_order=UINT32_MAX-7u+i};
    }
    /* The original allocator retires the oldest, including a still-live
     * actor; storage indices and wrapping creation serials are independent. */
    portable_gp_item_pool_reserve(&roster);
    CHECK(portable_gp_item_pool_count(&roster)==15u && !roster.green_pool[0].shell.occupied &&
        roster.green_pool[1].shell.occupied && !roster.green_pool[0].shell.flags_34);
    roster.racers[2].red_shell.occupied=1u;roster.racers[2].red_shell.creation_order=UINT32_MAX-8u;
    portable_gp_item_pool_reserve(&roster);
    CHECK(!roster.racers[2].red_shell.occupied && portable_gp_item_pool_count(&roster)==15u);
    CHECK(portable_gp_green_actor_owner(&roster,9u)==3u &&
        portable_gp_green_actor(&roster,9u)==&roster.green_pool[1].shell);
    const uint64_t identity=portable_gp_race_roster_identity(&roster);
    roster.green_pool[1].shell.vertical_velocity_q16++;
    CHECK(portable_gp_race_roster_identity(&roster)!=identity);
    roster.racers[2].banana.occupied=1u;roster.racers[2].banana.creation_order=12u;
    roster.green_pool[1].shell.retire_held_slot=1u;
    roster.racers[3].item_use_state_17c=3u;roster.racers[3].source_flags=0x10000u;
    portable_gp_item_pool_reserve(&roster);
    CHECK(!roster.green_pool[1].shell.occupied && roster.racers[3].item_use_state_17c==2u &&
        !(roster.racers[3].source_flags&0x10000u));
    uint8_t actors[PORTABLE_GP_GREEN_ACTOR_SLOTS];
    CHECK(portable_gp_green_actor_order(&roster,actors)==14u && actors[0]==23u && actors[13]==10u);
    return 0;
}

static int test_retained_red_pool(void)
{
    PortableGpRaceRoster roster={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&roster);int32_t sine[1024]={0};sine[256]=32768;
    PortableGpRacer *owner=&roster.racers[3];owner->control=0x80u;
    owner->item_inventory.held_item_17a=PORTABLE_GP_SPINY_SHELL_ITEM;
    CHECK(portable_gp_race_roster_use_red_shell(&roster,3u,sine,1024u)==0 &&
        portable_gp_race_roster_tick_red_shell_attached(&roster,3u,sine,1024u)==0);
    owner->control=0u;
    CHECK(portable_gp_race_roster_release_red_shell_forward_leader(&roster,3u,sine,1024u)==0);
    owner->red_shell.target_valid=1u;owner->red_shell.target_racer_index=7u;
    owner->red_shell.velocity_x_q16=123;owner->source_flags&=~0x10000u;
    owner->control=0x80u;owner->item_inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;
    CHECK(portable_gp_race_roster_use_red_shell(&roster,3u,sine,1024u)==0 &&
        portable_gp_item_pool_count(&roster)==2u && roster.red_pool[0].owner==3u &&
        roster.red_pool[0].shell.kind_10==PORTABLE_GP_SPINY_SHELL_KIND &&
        roster.red_pool[0].shell.target_racer_index==7u && roster.red_pool[0].shell.velocity_x_q16==123);
    owner->item_inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;
    const uint64_t before=portable_gp_race_roster_identity(&roster);
    CHECK(portable_gp_race_roster_use_red_shell(&roster,3u,sine,1024u)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE &&
        portable_gp_race_roster_identity(&roster)==before);
    roster.red_pool[0].shell.target_racer_index=6u;
    CHECK(portable_gp_race_roster_identity(&roster)!=before);
    CHECK(portable_gp_red_actor_owner(&roster,8u)==3u &&
        portable_gp_red_actor(&roster,8u)==&roster.red_pool[0].shell);
    memset(&roster,0,sizeof roster);test_item_roster_profiles(&roster);
    for(unsigned i=0u;i<16u;++i){
        roster.red_pool[i].owner=4u;
        roster.red_pool[i].shell=(PortableGpRedShell){.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.flags_34=3u,
            .creation_order=UINT32_MAX-7u+i,.kind_10=PORTABLE_GP_RED_SHELL_KIND};
    }
    portable_gp_item_pool_reserve(&roster);
    CHECK(portable_gp_item_pool_count(&roster)==15u && !roster.red_pool[0].shell.occupied);
    roster.racers[1].green_shell.occupied=1u;roster.racers[1].green_shell.creation_order=UINT32_MAX-8u;
    portable_gp_item_pool_reserve(&roster);
    CHECK(!roster.racers[1].green_shell.occupied && portable_gp_item_pool_count(&roster)==15u);
    uint8_t actors[PORTABLE_GP_RED_ACTOR_SLOTS];
    CHECK(portable_gp_red_actor_order(&roster,actors)==15u && actors[0]==23u && actors[14]==9u);
    roster.racers[1].banana.occupied=1u;roster.racers[1].banana.creation_order=12u;
    roster.red_pool[1].shell.retire_held_slot=1u;
    roster.racers[4].item_use_state_17c=3u;roster.racers[4].source_flags=0x10000u;
    portable_gp_item_pool_reserve(&roster);
    CHECK(!roster.red_pool[1].shell.occupied && roster.racers[4].item_use_state_17c==2u &&
        !(roster.racers[4].source_flags&0x10000u));
    return 0;
}

static int test_retained_banana_pool(void)
{
    PortableGpRaceRoster roster={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&roster);int32_t sine[1024]={0};sine[256]=32768;
    PortableGpRacer *owner=&roster.racers[3];owner->control=0x80u;
    for(unsigned use=0u;use<20u;++use){
        owner->item_inventory.held_item_17a=PORTABLE_GP_BANANA_ITEM;
        CHECK(portable_gp_race_roster_use_banana(&roster,3u,sine,1024u)==0);
        CHECK(portable_gp_item_pool_count(&roster)==(use<16u?use+1u:16u));
        owner->banana.phase=PORTABLE_GP_BANANA_DROPPED;
        owner->banana.collision_active=1u;owner->banana.position_x_q16=(int32_t)use*65536;
        owner->shell_item_18c=0u;
    }
    uint8_t actors[PORTABLE_GP_BANANA_ACTOR_SLOTS];
    CHECK(portable_gp_banana_actor_order(&roster,actors)==16u);
    CHECK(actors[0]==3u && portable_gp_banana_actor(&roster,actors[15])->creation_order==5u);
    CHECK(portable_gp_banana_actor_owner(&roster,actors[15])==3u);
    const uint64_t identity=portable_gp_race_roster_identity(&roster);
    portable_gp_banana_actor_mutable(&roster,actors[15])->vertical_velocity_q16++;
    CHECK(portable_gp_race_roster_identity(&roster)!=identity);
    /* Filling the shared pool with bananas also makes a shell reuse the
     * oldest banana. An ineligible held use never retires anything. */
    owner->banana.phase=PORTABLE_GP_BANANA_HELD;
    owner->item_inventory.held_item_17a=PORTABLE_GP_BANANA_ITEM;
    const uint64_t held=portable_gp_race_roster_identity(&roster);
    CHECK(portable_gp_race_roster_use_banana(&roster,3u,sine,1024u)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE &&
        portable_gp_race_roster_identity(&roster)==held);
    owner->item_inventory.held_item_17a=PORTABLE_GP_GREEN_SHELL_ITEM;
    CHECK(portable_gp_race_roster_use_green_shell(&roster,3u,sine,1024u)==0 &&
        portable_gp_item_pool_count(&roster)==16u &&
        !portable_gp_banana_actor(&roster,actors[15])->occupied);
    return 0;
}

static int test_triple_red_roster(void)
{
    int32_t sine[1024]={0};sine[256]=32768;
    const uint8_t delays[3]={0u,2u,4u};
    const PortableGpRedShellWorld world={.sine=sine,.sine_count=1024u};
    const PortableGpRedShellRoute route={0};
    for(unsigned leader=0u;leader<2u;++leader){
        PortableGpRaceRoster roster={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&roster);roster.shell_orbit_delay_valid=1u;
        memcpy(roster.shell_orbit_delay[1],delays,3u);
        for(unsigned i=0u;i<8u;++i)roster.racers[i].rank_flags_ba=(uint16_t)(((i+(leader?0u:7u))%8u)*2u);
        PortableGpRacer *owner=&roster.racers[0];owner->control=0x80u;
        owner->item_inventory.held_item_17a=PORTABLE_GP_TRIPLE_RED_SHELL_ITEM;
        CHECK(portable_gp_race_roster_use_red_shell(&roster,0u,sine,1024u)==0 &&
            owner->item_use_state_17c==3u && roster.triple_red_uses==1u && portable_gp_item_pool_count(&roster)==3u);
        owner->control=0u;
        for(unsigned tick=0u;tick<5u;++tick){
            uint8_t actors[PORTABLE_GP_RED_ACTOR_SLOTS];
            const unsigned count=portable_gp_red_actor_order(&roster,actors);
            for(unsigned turn=0u;turn<count;++turn)
                CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,actors[turn],&world,&route)==0);
        }
        CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_ORBITING &&
            roster.red_pool[0].shell.phase==PORTABLE_GP_RED_SHELL_ORBITING && owner->red_shell.orbit_audio_pending);
        const uint64_t before=portable_gp_race_roster_identity(&roster);
        CHECK(portable_gp_race_roster_release_red_shell_forward_leader(&roster,0u,NULL,0u)!=0 &&
            portable_gp_race_roster_identity(&roster)==before);
        owner->control=0x4c0u;owner->release_angle_offset=0x4000;owner->contact_heading_56=0x4000u;
        owner->source_flags=0x2000u;owner->item_decision_190=2u;
        CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,0u,&world,&route)==0 &&
            owner->item_use_state_17c==2u && !owner->red_shell.retire_held_slot &&
            owner->red_shell.angle_26==0x4000u && owner->red_shell.motion_audio_pending==123u &&
            owner->item_decision_190==2u);
        CHECK(owner->red_shell.target_valid==!leader &&
            owner->red_shell.target_racer_index==(leader?UINT8_MAX:7u));
        CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,8u,&world,&route)==0 &&
            roster.red_pool[0].shell.phase==PORTABLE_GP_RED_SHELL_ORBITING && owner->item_use_state_17c==2u);
        owner->source_flags=0x800u;
        CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,8u,&world,&route)==0 && owner->item_use_state_17c==2u);
        owner->source_flags=0u;
        CHECK(portable_gp_race_roster_tick_red_shell_actor(&roster,8u,&world,&route)==0 &&
            owner->item_use_state_17c==1u && !roster.red_pool[0].shell.retire_held_slot);
    }
    return 0;
}

static int test_cpu_projectile_decisions(int type)
{
    const int green=type==1,red=type>=2;
    int32_t sine[1024]={0};sine[256]=32768;
    PortableGpRaceRoster roster={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&roster);
    roster.cpu_items_enabled=1u;roster.cpu_item_clock=256u;
    roster.cpu_banana_release_valid=1u;roster.cpu_green_release_valid=1u;roster.cpu_red_release_valid=1u;
    const uint8_t red_decisions[2][4]={{2,0,0,0},{3,3,3,1}};
    memcpy(roster.cpu_red_release,red_decisions,sizeof red_decisions);
    const uint8_t green_decisions[2][4]={{2,0,0,0},{3,1,3,3}};
    memcpy(roster.cpu_green_release,green_decisions,sizeof green_decisions);
    const uint8_t decisions[2][4]={{0,0,0,2},{3,1,3,3}};
    memcpy(roster.cpu_banana_release,decisions,sizeof decisions);
    for(unsigned i=0u;i<8u;++i){
        roster.standings[i]=(uint8_t)i;roster.racers[i].rank_flags_ba=(uint16_t)(i*2u);
        roster.racers[i].source_flags=i?0x2000u:0x8000u;
        roster.racers[i].route_wrap_91=1u;
    }
    PortableGpRacer *cpu=&roster.racers[1];
    cpu->item_inventory.held_item_17a=green?0x1000u:red?(type==3?0x1004u:0x1002u):0x1003u; /* CPU triple red becomes banana. */
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    if(green){
        CHECK(cpu->green_shell.phase==PORTABLE_GP_GREEN_SHELL_ATTACHED);
        CHECK(portable_gp_race_roster_tick_green_shell_attached(&roster,1u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(cpu->shell_item_18c==0x1000u && roster.cpu_green_uses==1u);
    }else if(red){
        CHECK(cpu->red_shell.phase==PORTABLE_GP_RED_SHELL_ATTACHED);
        CHECK(portable_gp_race_roster_tick_red_shell_attached(&roster,1u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(cpu->shell_item_18c==(type==3?0x1004u:0x1002u) &&
              (type==3?roster.cpu_spiny_uses:roster.cpu_red_uses)==1u);
    }else CHECK(cpu->banana.phase==PORTABLE_GP_BANANA_HELD && cpu->shell_item_18c==0x1005u && roster.cpu_banana_uses==1u);
    CHECK(cpu->item_inventory.held_item_17a==0u && cpu->item_decision_190==1u);
    CHECK(cpu->item_decision_timer_18e==48u && roster.cpu_item_cooldown_c62==510u);
    /*42AE4 exits on empty17A. New roulette permits held decisions despite
     * use cooldown, lap exclusion, finish20 and the unrelated scene slot. */
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_timer_18e==48u);
    cpu->item_inventory.held_item_17a=1u;cpu->source_flags|=0x20u;
    roster.cpu_item_clock=17u;
    for(unsigned i=0u;i<48u;++i)
        CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_timer_18e==0u && cpu->item_decision_190==1u);
    roster.racers[0].route_index=2u;cpu->route_index=0u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_190==0u); /* Human ahead, gap2: throw. */
    roster.racers[0].route_index=3u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_190==((green||red)?0u:1u)); /* Green throws at gap3; banana holds. */
    roster.standings[0]=1u;roster.standings[1]=0u;cpu->rank_flags_ba=0u;
    cpu->route_index=4u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_190==(red?3u:2u)); /* Red holds at gap1; other items drop. */
    cpu->route_index=3u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_190==3u); /* Gap0: hold backward. */
    cpu->route_wrap_91=0u;cpu->item_decision_190=0xa3u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->item_decision_190==0xa3u); /* Negative wrapped lap gap retains decision. */
    /* Red defaults to a backward hold at ranks6/7 even with a non-adjacent
     * human ahead. Green and banana keep their forward hold in that case. */
    const uint8_t order[8]={2u,3u,4u,0u,5u,6u,1u,7u};
    memcpy(roster.standings,order,8u);cpu->rank_flags_ba=12u;
    cpu->progress_ae=0;cpu->route_wrap_91=1u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK((cpu->item_decision_190&3u)==(red?3u:1u));
    return 0;
}

/* Mod scripts see a CPU's activation: a replacement is constructed in its
 * place, zero keeps the item, and a GP CPU cannot be handed an item its
 * own code never uses (that keeps the original). */
static size_t script_racer;
static uint16_t script_seen,script_reply;
static uint16_t script_item_use(void *context,size_t racer,uint16_t item)
{
    (void)context;script_racer=racer;script_seen=item;
    return script_reply==0xffffu?item:script_reply;
}
static void cpu_script_roster(PortableGpRaceRoster *roster,uint16_t held,const PortableRaceScriptHooks *hooks)
{
    *roster=(PortableGpRaceRoster){0};
    test_item_roster_profiles(roster);
    roster->cpu_items_enabled=1u;roster->cpu_item_clock=256u;
    roster->cpu_banana_release_valid=roster->cpu_green_release_valid=roster->cpu_red_release_valid=1u;
    for(unsigned i=0u;i<8u;++i){
        roster->standings[i]=(uint8_t)i;roster->racers[i].rank_flags_ba=(uint16_t)(i*2u);
        roster->racers[i].source_flags=i?0x2000u:0x8000u;
        roster->racers[i].route_wrap_91=1u;
    }
    roster->racers[1].item_inventory.held_item_17a=held;
    roster->script_hooks=hooks;
    script_racer=99u;script_seen=0u;
}
static int test_cpu_item_scripts(void)
{
    int32_t sine[1024]={0};sine[256]=32768;
    const PortableRaceScriptHooks hooks={NULL,NULL,script_item_use};
    PortableGpRaceRoster roster;
    PortableGpRacer *cpu=&roster.racers[1];
    /* Green shell thrown as a banana. */
    cpu_script_roster(&roster,0x1000u,&hooks);script_reply=0x1005u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(script_racer==1u && script_seen==0x1000u);
    CHECK(cpu->banana.phase==PORTABLE_GP_BANANA_HELD && cpu->shell_item_18c==0x1005u &&
          roster.cpu_banana_uses==1u && roster.cpu_green_uses==0u && !cpu->green_shell.occupied);
    CHECK(cpu->item_decision_timer_18e==48u && roster.cpu_item_cooldown_c62==510u);
    /* Cancelled: the CPU keeps its shell and no cooldown starts. */
    cpu_script_roster(&roster,0x1000u,&hooks);script_reply=0u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(script_seen==0x1000u && cpu->item_inventory.held_item_17a==0x1000u &&
          !cpu->green_shell.occupied && roster.cpu_item_cooldown_c62==0u && roster.cpu_green_uses==0u);
    /* A triple mushroom is not a GP CPU item: the green shell goes out. */
    cpu_script_roster(&roster,0x1000u,&hooks);script_reply=0x1008u;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(cpu->green_shell.phase==PORTABLE_GP_GREEN_SHELL_ATTACHED && roster.cpu_green_uses==1u);
    /* A mushroom becomes a star through the shared effect path. */
    cpu_script_roster(&roster,0x1007u,&hooks);script_reply=0x100au;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(script_seen==0x1007u && roster.cpu_star_uses==1u && roster.cpu_mushroom_uses==0u &&
          cpu->lightning_status_6a>0 && cpu->item_inventory.held_item_17a==0u);
    /* An observing hook leaves the original activation untouched. */
    cpu_script_roster(&roster,0x1007u,&hooks);script_reply=0xffffu;
    CHECK(portable_gp_race_roster_use_cpu_item(&roster,1u,10u,3u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(roster.cpu_mushroom_uses==1u && (cpu->item_response_flags_b2&0x8000u)!=0u &&
          roster.cpu_item_cooldown_c62==510u);
    return 0;
}

static int test_cpu_affine_expiry(void)
{
    static const PortableDriverAffineRow landing[]={{248,264,1}};
    static const PortableDriverAffineRow expiry[]={{378,382,1}};
    static const PortableDriverAffineRow hold[]={{220,380,110}};
    static const PortableDriverAffineRow lift[]={{220,380,18}};
    static const PortableDriverAffineRow rebound[]={{220,380,112},{256,256,1}};
    const PortableDriverAffineRules rules={.tables={
        {1,1,landing},{4,1,expiry},{5,1,hold},{6,1,lift},{7,2,rebound}}};
    PortableGpRaceRoster roster;
    CHECK(portable_gp_race_roster_init(&roster,24u,0u)==PORTABLE_GP_ROSTER_OK);
    PortableGpRacer *cpu=&roster.racers[1];
    cpu->affine_rules=&rules;cpu->lightning_timer_68=1u;
    CHECK(portable_driver_affine_request(&cpu->affine,&rules,7u)==1);
    CHECK(portable_gp_race_roster_tick_lightning_timer(&roster,1u)==PORTABLE_GP_ROSTER_OK &&
        cpu->lightning_timer_68==0u && cpu->affine.mode==4u);
    CHECK(portable_driver_affine_tick(&cpu->affine,&rules)==0 &&
        cpu->affine.inverse_x_q8==378u && cpu->affine.inverse_y_q8==382u);
    cpu->affine.pending_mode=5u;cpu->lightning_timer_68=1u;
    CHECK(portable_gp_race_roster_tick_lightning_timer(&roster,1u)==PORTABLE_GP_ROSTER_OK &&
        cpu->affine.mode==5u && cpu->affine.remaining==110u);
    return 0;
}

static int test_all_racer_contacts(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    for(unsigned driver=0u;driver<8u;++driver){
        PortableGpRaceRoster base={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules(),.racer_count=2u,.race_mode_flags_7da=0x100u,.course_id=28u};test_item_roster_profiles(&base);
        base.standings[0]=0u;base.standings[1]=1u;
        for(unsigned i=0u;i<2u;++i)base.racers[i]=(PortableGpRacer){.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),.red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),.shell_orbit_rules=test_shell_orbit_rules(),
            .is_player=(uint8_t)(i==0u),.driver_class=(uint8_t)driver,.source_flags=i?0x2000u:0x8000u,
            .hit_cue_flags_58=0x40u,.coin_count_178=5u,.route_index=3u,
            .position_x_q16=(100+(int32_t)i*5)*65536,.position_y_q16=100*65536,
            .contact_center_x=(int16_t)(100+i*5),.contact_center_y=100,.component_x=i?-100:200};
        for(unsigned scenario=0u;scenario<7u;++scenario){
            PortableGpRaceRoster roster=base;PortableGpKartContactReport report;
            if(scenario==1u)++roster.racers[1].position_x_q16;
            if(scenario==2u)roster.racers[1].banana_hit_timer_38=1u;
            if(scenario==3u)roster.racers[1].banana_hit_timer_38=2u;
            if(scenario==4u)roster.racers[1].lightning_state_78=2u;
            if(scenario==5u)roster.racers[1].world_height_q16=0x50000;
            if(scenario==6u)roster.racers[1].contact_status_66=1;
            CHECK(portable_gp_race_roster_collide_karts(&roster,8u,99u,sine,1024u,&report)==0);
            const unsigned accepted=scenario==0u||scenario==2u;
            CHECK(roster.contact_events==accepted && report.response[0]==(accepted?PORTABLE_GP_KART_CONTACT_PUSH:0u));
            if(accepted)CHECK(roster.racers[0].banana_hit_timer_38==8u && roster.racers[1].banana_hit_timer_38==8u &&
                roster.racers[0].coin_count_178==4u && roster.racers[1].coin_count_178==5u &&
                roster.racers[0].coin_loss_event_count==1u && roster.racers[0].coin_loss_events[0].clock==99u &&
                roster.racers[0].component_x!=200 && roster.racers[1].component_x!=-100);
        }
        /* F450's bump precedes E92C damage immunity. Both normal and
         * protected/size variants retain audio without a physical event. */
        for(unsigned variant=0u;variant<2u;++variant){
            PortableGpRaceRoster immune=base;PortableGpKartContactReport report;
            immune.racers[0].hit_cue_flags_58=variant?0u:0x40u;
            immune.racers[1].hit_cue_flags_58=variant?0x40u:0x8000u;
            immune.racers[0].lightning_status_6a=1;
            immune.racers[0].speed_q16=-3*65536;immune.racers[1].speed_q16=-2*65536;
            CHECK(portable_gp_race_roster_collide_karts(&immune,8u,99u,sine,1024u,&report)==0);
            CHECK(report.bump[0] && report.bump_speed_q16[0]==-2*65536 && !report.response[0] &&
                !immune.racers[0].kart_contact_events && !immune.contact_events &&
                immune.racers[0].coin_count_178==5u);
        }
        {
            PortableGpRaceRoster finished=base;PortableGpKartContactReport report;
            finished.racers[0].source_flags|=0x20u;
            CHECK(portable_gp_race_roster_collide_karts(&finished,8u,99u,sine,1024u,&report)==0);
            CHECK(!report.bump[0] && report.response[0]==PORTABLE_GP_KART_CONTACT_PUSH &&
                finished.racers[0].kart_contact_events==1u && finished.racers[0].coin_count_178==5u);
        }
        /* Local0/1 both participate; guard updates prevent reverse callback
         * and a third coincident racer from resolving the same pair again. */
        base.race_mode_flags_7da=0x400u;base.racer_count=3u;
        base.racers[1].is_player=1u;base.racers[1].source_flags=0x8000u;
        base.racers[2]=base.racers[1];base.racers[2].position_x_q16=base.racers[0].position_x_q16;
        PortableGpKartContactReport report;
        CHECK(portable_gp_race_roster_collide_karts(&base,8u,100u,sine,1024u,&report)==0 && base.contact_events==1u &&
            report.response[0]==1u && report.response[1]==1u && !report.response[2] &&
            base.racers[0].coin_count_178==4u && base.racers[1].coin_count_178==4u && base.racers[2].coin_count_178==5u);
    }
    return 0;
}

/* Exercise E92C through the production pair dispatcher, including its
 * reverse callback and the GP/VS distinction in local coin ownership. */
static PortableGpRaceRoster size_contact_fixture(unsigned local_vs, unsigned normal)
{
    PortableGpRaceRoster r = {.racer_count = 2u,
                              .course_id = 28u,
                              .race_mode_flags_7da = (uint16_t)(local_vs ? 0x400u : 0x100u)};
    test_item_roster_profiles(&r);
    r.roulette_rules = test_roulette_profile();
    r.hazard_sound_rules = test_hazard_rules();
    for (unsigned i = 0u; i < 2u; ++i)
    {
        r.standings[i] = (uint8_t)i;
        r.racers[i] =
            (PortableGpRacer){.is_player = (uint8_t)(local_vs || i == 0u),
                              .source_flags = local_vs || i == 0u ? 0x8000u : 0x2000u,
                              .hit_cue_flags_58 = (uint16_t)(0x40u | (i == normal ? 0x10u : 0u)),
                              .rank_flags_ba = (uint16_t)(i * 2u),
                              .coin_count_178 = 9u,
                              .route_index = 3u,
                              .position_x_q16 = (100 + (int32_t)i * 4) * 65536,
                              .position_y_q16 = 100 * 65536,
                              .contact_center_x = (int16_t)(100 + i * 4),
                              .contact_center_y = 100,
                              .heading = 0x4000u};
    }
    return r;
}

static int test_lightning_size_contacts(void)
{
    int32_t sine[1024] = {0};
    sine[256] = 32768;
    sine[768] = -32768;
    for (unsigned vs = 0u; vs < 2u; ++vs)
        for (unsigned normal = 0u; normal < 2u; ++normal)
        {
            const unsigned small = 1u - normal;
            PortableGpRaceRoster r = size_contact_fixture(vs, normal);
            PortableGpKartContactReport report;
            CHECK(portable_gp_race_roster_collide_karts(&r, 8u, 101u, sine, 1024u, &report) == 0);
            const PortableGpRacer *v = &r.racers[small], *h = &r.racers[normal];
            CHECK(r.contact_events == 1u && h->kart_contact_events == 1u &&
                  v->kart_contact_events == 1u);
            CHECK(report.response[small] == PORTABLE_GP_KART_CONTACT_HIT &&
                  !report.response[normal]);
            CHECK(v->banana_hit_timer_38 == 8u && v->kart_contact_state_5a == 0x8007u &&
                  (v->source_flags & 0x1000u) && v->hit_state_flags_74 == 3u &&
                  v->mushroom_response_flags_b4 == 0x300u && v->hit_response_b6 == 0x1000u &&
                  abs(v->component_x) + abs(v->component_y) == 512 &&
                  (!v->component_x || !v->component_y) && !v->flattened_64);
            CHECK(!h->banana_hit_timer_38 && !h->hit_response_b6 && !h->component_x &&
                  !h->component_y && h->coin_count_178 == 9u && !h->coin_loss_event_count);
            CHECK(v->coin_count_178 == (v->is_player ? 5u : 9u) &&
                  v->coin_loss_event_count == (v->is_player ? 1u : 0u));
            if (v->is_player)
                CHECK(v->coin_loss_events[0].cause == 1u && v->coin_loss_events[0].loss == 4u &&
                      v->coin_loss_events[0].clock == 101u);
            CHECK(r.pending_voice_count == 1u && r.pending_voices[0].racer == small &&
                  r.pending_voices[0].ticks == 16u && !r.pending_voices[0].taunt);
            const PortableGpRaceRoster accepted = r;
            CHECK(portable_gp_race_roster_collide_karts(&r, 8u, 101u, sine, 1024u, &report) == 0);
            CHECK(!memcmp(&r, &accepted, sizeof r)); /* No second hit/loss/voice. */
            for (unsigned who = 0u; who < 2u; ++who)
                for (unsigned guard = 0u; guard < 7u; ++guard)
                {
                    r = size_contact_fixture(vs, normal);
                    PortableGpRacer *g = &r.racers[who];
                    if (guard == 0u)
                        g->banana_hit_timer_38 = 2u;
                    if (guard == 1u)
                        g->contact_status_60 = 1u;
                    if (guard == 2u)
                        g->contact_status_66 = -1;
                    if (guard == 3u)
                        g->lightning_state_78 = 2u;
                    if (guard == 4u)
                        g->world_height_q16 = 0x50000;
                    /* Inclusive boundaries: timer1, hop1 and height4ffff remain
             * eligible, with both centers at the same contact height. */
                    if (guard >= 5u)
                    {
                        for (unsigned i = 0u; i < 2u; ++i)
                        {
                            r.racers[i].banana_hit_timer_38 = 1u;
                            r.racers[i].lightning_state_78 = 1u;
                            r.racers[i].world_height_q16 = guard == 5u ? 0x4ffff : 0;
                        }
                    }
                    CHECK(portable_gp_race_roster_collide_karts(&r, 8u, 100u, sine, 1024u,
                                                                &report) == 0);
                    CHECK(r.contact_events == (guard >= 5u ? 1u : 0u));
                    CHECK(r.pending_voice_count == (guard >= 5u ? 1u : 0u));
                    if (guard < 5u)
                        CHECK(r.racers[small].coin_count_178 == 9u &&
                              !r.racers[small].coin_loss_event_count &&
                              !r.racers[small].hit_response_b6);
                }
            for (unsigned scenario = 0u; scenario < 5u; ++scenario)
            {
                r = size_contact_fixture(vs, normal);
                if (scenario == 0u)
                    r.racers[small].hit_cue_flags_58 |= 0x8000u; /* Small Star wins. */
                if (scenario == 1u)
                { /* Small boost wins over ordinary normal size. */
                    r.racers[normal].hit_cue_flags_58 &= UINT16_C(0xffbf);
                }
                if (scenario == 2u)
                    for (unsigned i = 0u; i < 2u; ++i)
                    {
                        r.racers[i].hit_cue_flags_58 |= 0x8000u;
                        r.racers[i].lightning_status_6a = 100;
                    }
                if (scenario >= 3u)
                    for (unsigned i = 0u; i < 2u; ++i)
                        r.racers[i].hit_cue_flags_58 =
                            (uint16_t)(0x40u | (scenario == 4u ? 0x10u : 0u));
                CHECK(portable_gp_race_roster_collide_karts(&r, 8u, 100u, sine, 1024u, &report) ==
                          0 &&
                      r.contact_events == 1u);
                if (scenario < 2u)
                    CHECK(r.racers[normal].hit_response_b6 == 0x1000u &&
                          r.racers[normal].banana_hit_timer_38 == 8u &&
                          !r.racers[small].banana_hit_timer_38);
                else
                    CHECK(report.response[0] == PORTABLE_GP_KART_CONTACT_PUSH &&
                          report.response[1] == PORTABLE_GP_KART_CONTACT_PUSH &&
                          !r.racers[0].hit_response_b6 && !r.racers[1].hit_response_b6 &&
                          r.racers[0].banana_hit_timer_38 == 8u &&
                          r.racers[1].banana_hit_timer_38 == 8u);
            }
        }
    return 0;
}

static int test_strong_hit_signed_sine_index(void)
{
    int32_t sine[1024] = {0};
    /* Distinct adjacent samples expose truncation versus arithmetic floor.
     * These are authored lookup values, not copies of the source sine table. */
    sine[507] = 960;
    sine[508] = 768;
    sine[773] = -32704;
    sine[772] = -16384;
    sine[1020] = -768;
    sine[1019] = -960;
    sine[260] = 32704;
    sine[261] = 16384;
    const uint16_t headings[2] = {0x3ee6u, 0x3ec5u};
    for (unsigned heading = 0u; heading < 2u; ++heading) {
        for (unsigned west = 0u; west < 2u; ++west) {
            const PortableGpRacer hitter = {.position_x_q16 = 0x046fb600,
                .position_y_q16 = 0x04707800, .heading = headings[heading],
                .hit_cue_flags_58 = 0x8000u};
            PortableGpRacer victim = {.position_x_q16 = west ? 0x046ca600 : 0x0472c600,
                .position_y_q16 = 0x04710900, .source_flags = 0x2808u,
                .heading = 0x4000u, .source_heading = 0x4000u,
                .speed_q16 = 0x03690000, .component_x = 873,
                .mushroom_response_flags_b4 = 0x8400u, .mushroom_mode_84 = 1u,
                .lightning_state_78 = 1u, .world_height_q16 = 0x48d00};
            const int32_t x = victim.position_x_q16, y = victim.position_y_q16;
            /* East selects 0x7ee6/0x7ec5 -> index507. West selects
             * 0xfee6/0xfec5: signed -282/-315 divided by64 -> -4, index1020. */
            CHECK(portable_gp_race_roster_begin_kart_hit(&victim, &hitter, 4897u,
                NULL, 0u, sine, 1024u) == PORTABLE_GP_ROSTER_OK);
            CHECK(victim.component_x == (west ? -12 : 15));
            CHECK(victim.component_y == (west ? -511 : 511));
            CHECK(victim.position_x_q16 == x && victim.position_y_q16 == y &&
                victim.speed_q16 == 0x03690000 && victim.heading == 0x4000u &&
                victim.source_heading == 0x4000u);
            CHECK(victim.banana_hit_timer_38 == 8u &&
                victim.kart_contact_state_5a == 0x8007u &&
                victim.mushroom_response_flags_b4 == 0x8700u &&
                victim.source_flags == 0x3808u);
        }
    }
    return 0;
}

static int test_coin_loss_requests(void)
{
    PortableGpRacer player={.is_player=1u,.source_flags=0x8000u,.coin_count_178=9u};
    const uint8_t expected_loss[3]={4u,4u,1u},remaining[3]={5u,1u,0u};
    for(unsigned hit=0u;hit<3u;++hit){
        player.banana_hit_timer_38=0u;
        CHECK(portable_gp_race_roster_begin_kart_hit(&player,NULL,hit,NULL,0u,NULL,0u)==0);
        CHECK(player.coin_loss_event_count==hit+1u && player.coin_count_178==remaining[hit] &&
            player.coin_loss_events[hit].cause==1u && player.coin_loss_events[hit].loss==expected_loss[hit] &&
            player.coin_loss_events[hit].remaining==remaining[hit] && player.coin_loss_events[hit].clock==hit);
    }
    player.banana_hit_timer_38=0u;
    CHECK(portable_gp_race_roster_begin_kart_hit(&player,NULL,3u,NULL,0u,NULL,0u)==0 && player.coin_loss_event_count==3u);
    player=(PortableGpRacer){.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),.red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),.shell_orbit_rules=test_shell_orbit_rules(),.is_player=1u,.source_flags=0x8000u,.coin_count_178=99u};
    for(unsigned hit=0u;hit<9u;++hit){
        player.banana_hit_timer_38=0u;
        CHECK(portable_gp_race_roster_begin_kart_hit(&player,NULL,hit,NULL,0u,NULL,0u)==0);
    }
    CHECK(player.coin_loss_event_count==8u && player.coin_loss_event_overflow && player.coin_count_178==63u);
    return 0;
}

/* F8C8's bomb role is independent of the selected handling character.
 * Exercise the item callback as well as the later victim guard. */
static int banana_bomb_fixture(PortableGpRaceRoster *r, unsigned held)
{
    CHECK(portable_gp_race_roster_init(r, 24u, 0u) == PORTABLE_GP_ROSTER_OK);
    r->race_mode_flags_7da = 0x800u;
    r->racer_count = 4u;
    PortableGpRacer *v = &r->racers[1];
    v->is_player = 1u;
    v->driver_class = 7u;
    v->source_flags = 0x8000u;
    v->coin_count_178 = 9u;
    v->balloons_remaining = 0u;
    v->world_height_q16 = 0;
    v->lightning_state_78 = 0u;
    v->contact_status_60 = 0u;
    v->contact_status_66 = 0;
    v->arena_start_x = 123;
    v->arena_start_y = 456;
    v->arena_start_heading = 0x3456u;
    v->speed_q16 = 65536;
    v->acceleration_q16 = 123;
    v->hit_spin_82 = 91;
    CHECK(
        portable_battle_elimination_tick(&v->elimination, PORTABLE_BATTLE_BALLOON_ELIMINATED, 1u));
    CHECK(v->elimination.revenge_bomb);
    r->racers[0].source_flags |= 0x10000u;
    r->racers[0].shell_item_18c = held ? PORTABLE_GP_BANANA_ITEM : 0u;
    r->racers[0].banana = (PortableGpBanana){
        .rules = test_banana_rules(),
        .occupied = 1u,
        .kind_10 = PORTABLE_GP_BANANA_KIND,
        .phase = (uint8_t)(held ? PORTABLE_GP_BANANA_HELD : PORTABLE_GP_BANANA_DROPPED),
        .collision_active = 1u,
        .flags_34 = 3u};
    return 0;
}

static int test_banana_revenge_bomb(void)
{
    PortableGpRaceRoster r;
    for (unsigned held = 0u; held < 2u; ++held)
        for (unsigned scenario = 0u; scenario < 12u; ++scenario)
        {
            CHECK(banana_bomb_fixture(&r, held) == 0);
            PortableGpRacer *v = &r.racers[1];
            PortableGpBanana *banana = &r.racers[0].banana;
            if (scenario == 1u)
                v->contact_status_60 = 1u;
            if (scenario == 2u)
                banana->collision_state_50 = 1u;
            if (scenario == 3u)
                v->lightning_state_78 = 2u;
            if (scenario == 4u)
                v->world_height_q16 = 0x42000;
            if (scenario == 5u)
                banana->position_z_q16 = 0x42000;
            if (scenario == 6u)
                v->contact_status_66 = 1;
            if (scenario == 7u)
                v->contact_status_66 = -1;
            if (scenario == 8u)
                v->lightning_status_6a = 1;
            if (scenario == 9u)
            {
                v->world_height_q16 = 0x41fff;
                banana->position_z_q16 = 0x41fff;
                v->lightning_state_78 = 1u;
            }
            if (scenario == 10u)
            {
                v->world_height_q16 = -1;
                banana->position_z_q16 = -1;
            }
            if (scenario == 11u)
                v->banana_hit_timer_38 = 2u; /* Not an F8C8 damage guard. */
            const PortableGpRacer before = *v;
            const PortableGpBanana before_banana = *banana;
            const uint32_t owner_flags = r.racers[0].source_flags;
            CHECK(portable_gp_race_roster_resolve_banana_kart_hit_with_context(
                      &r, 0u, 1u, 19u, 180u) == PORTABLE_GP_ROSTER_OK);
            const unsigned detonate = scenario == 0u || scenario >= 9u;
            if (detonate)
            {
                CHECK(v->elimination.revenge_bomb && !v->balloons_remaining &&
                      v->lightning_state_78 == 9u && v->mushroom_mode_84 == 2u &&
                      v->recovery_timer_9c == 60u && v->world_height_q16 == 0x100 &&
                      v->recovery_target_x_9e == 123 && v->recovery_target_y_a0 == 456 &&
                      v->recovery_target_heading_a2 == 0x3456u);
                CHECK(v->kart_contact_state_5a == 0x8007u && v->banana_hit_timer_38 == 8u &&
                      (v->source_flags & 0x1800u) == 0x1800u && !v->speed_q16 &&
                      !v->acceleration_q16 && !v->hit_spin_82);
            }
            else if (scenario == 8u)
            {
                CHECK(v->lightning_state_78 == before.lightning_state_78 &&
                      v->world_height_q16 == before.world_height_q16 &&
                      v->source_flags == before.source_flags && !v->kart_contact_state_5a &&
                      !v->banana_hit_timer_38 && !v->recovery_timer_9c &&
                      v->banana_reaction_audio_pending == 2u && v->banana_reaction_volume == 180u);
            }
            else
                CHECK(!memcmp(v, &before, sizeof before));
            CHECK(!r.banana_hits && v->coin_count_178 == 9u && !v->coin_loss_event_count &&
                  !v->mushroom_response_flags_b4 && !v->hit_state_flags_74 &&
                  !v->banana_special_entry_audio_pending);
            if (scenario == 6u || scenario == 7u)
            {
                /* The outer Boo bypass precedes the item callback entirely. */
                CHECK(!memcmp(banana, &before_banana, sizeof before_banana) &&
                      r.racers[0].source_flags == owner_flags &&
                      r.racers[0].shell_item_18c == (held ? PORTABLE_GP_BANANA_ITEM : 0u));
            }
            else
            {
                CHECK(banana->phase == PORTABLE_GP_BANANA_DESTROYING && banana->occupied &&
                      banana->collision_result == PORTABLE_GP_BANANA_COLLISION_DESTROYING &&
                      !(r.racers[0].source_flags & 0x10000u) && !r.racers[0].shell_item_18c);
                const PortableGpRacer accepted = *v;
                CHECK(portable_gp_race_roster_resolve_banana_kart_hit_with_context(
                          &r, 0u, 1u, 19u, 180u) == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
                CHECK(!memcmp(v, &accepted, sizeof accepted));
                for (unsigned tick = 0u; tick < 256u && banana->occupied; ++tick)
                    CHECK(portable_gp_race_roster_tick_banana_destroying(&r, 0u) == 0);
                CHECK(!banana->occupied && banana->phase == PORTABLE_GP_BANANA_DESTROYED);
            }
        }
    /* An ordinary kart with the same handling class still takes the banana
     * response; class bits must not manufacture an eliminated bomb role. */
    CHECK(banana_bomb_fixture(&r, 0u) == 0);
    r.racers[1].elimination = (PortableBattleEliminationState){0};
    r.racers[1].balloons_remaining = 3u;
    CHECK(portable_gp_race_roster_resolve_banana_kart_hit_with_context(&r, 0u, 1u, 20u, 180u) == 0);
    CHECK(r.banana_hits == 1u && r.racers[1].banana_hit_timer_38 == 3u &&
          r.racers[1].kart_contact_state_5a == 7u && !r.racers[1].recovery_timer_9c &&
          r.racers[1].lightning_state_78 == 0u &&
          (r.racers[1].mushroom_response_flags_b4 & 0x1000u) != 0u &&
          !r.racers[1].elimination.revenge_bomb);
    return 0;
}

static int test_contact_transaction_rollback(void)
{
    PortableGpRaceRoster *owners = calloc(2u, sizeof *owners);
    CHECK(owners != NULL);
    PortableGpRaceRoster *roster = &owners[0], *before = &owners[1];
    int32_t sine[1024] = {0};
    sine[256] = 32768;
    roster->racer_count = 4u;
    roster->race_mode_flags_7da = UINT16_C(0x800);
    roster->player_bump_pending = 1u;
    roster->player_bump_speed_q16 = 12345;
    roster->contact_events = 17u;
    roster->racers[1].elimination.revenge_bomb = 1u;
    roster->racers[1].speed_q16 = 24680;
    roster->racers[1].source_flags = UINT32_C(0x8000);
    roster->racers[2].rank_flags_ba = 8u;
    roster->mushroom_pool[3].creation_order = 987u;
    uint8_t contacted = 99u;
    *before = *roster;
    /* Detonation has already cleared speed and entered mode9 when an
     * invalid victim rank is discovered; neither entry may escape. */
    CHECK(portable_battle_race_roster_bomb_contact(roster, 1u, 2u, 11u,
              sine, 1024u, &contacted) == PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(contacted == 0u && memcmp(roster, before, sizeof *roster) == 0);
    roster->racers[2].rank_flags_ba = 2u;
    roster->standings[0] = 99u;
    *before = *roster;
    CHECK(portable_battle_race_roster_bomb_contact(roster, 1u, 2u, 11u,
              sine, 1024u, &contacted) == PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(contacted == 0u && memcmp(roster, before, sizeof *roster) == 0);
    /* The successful pair sees the already detonated bomb in its rank
     * lookup, commits both owners and leaves unrelated actor pools intact. */
    roster->standings[0] = 1u;
    CHECK(portable_battle_race_roster_bomb_contact(roster, 1u, 2u, 11u,
              sine, 1024u, &contacted) == PORTABLE_GP_ROSTER_OK);
    CHECK(contacted == 1u && roster->contact_events == 18u &&
          roster->racers[1].speed_q16 == 0 && roster->racers[1].lightning_state_78 == 9u &&
          roster->racers[2].hit_response_b6 == UINT16_C(0x1000) &&
          roster->racers[2].banana_hit_timer_38 == 8u &&
          roster->mushroom_pool[3].creation_order == 987u &&
          roster->player_bump_pending == 1u && roster->player_bump_speed_q16 == 12345);
    free(owners);
    return 0;
}

int main(void)
{
    CHECK(test_contact_transaction_rollback() == 0);
    CHECK(test_banana_revenge_bomb() == 0);
    CHECK(fixed_to_int(-1) == 0 && fixed_to_int(-65535) == 0 && fixed_to_int(-65536) == -1 &&
          fixed_to_int(-65537) == -1 && fixed_to_int(INT32_MIN) == -32768 &&
          fixed_to_int(INT32_MAX) == 32767);
    CHECK(test_strong_hit_signed_sine_index()==0);
    CHECK(test_coin_loss_requests()==0);
    CHECK(test_all_racer_contacts()==0);
    CHECK(test_lightning_size_contacts()==0);
    CHECK(test_cpu_affine_expiry()==0);
    CHECK(test_reserved_item()==0);
    CHECK(test_battle_navigation()==0);
    CHECK(test_battle_red_release()==0);
    CHECK(test_battle_cpu_combat()==0);
    CHECK(test_battle_balloons()==0);
    CHECK(test_live_item_pool()==0);
    CHECK(test_retained_red_pool()==0);
    CHECK(test_triple_red_roster()==0);
    CHECK(test_retained_banana_pool()==0);
    CHECK(test_cpu_projectile_decisions(0)==0);
    CHECK(test_cpu_projectile_decisions(1)==0);
    CHECK(test_cpu_projectile_decisions(2)==0);
    CHECK(test_cpu_projectile_decisions(3)==0);
    CHECK(test_cpu_item_scripts()==0);
    /* F8C8 damage guards are independent of445CC actor disposal. The
     * local driver receives the same live component words as CPU motion. */
    for(unsigned guard=0u;guard<6u;++guard){
        PortableGpRaceRoster hit;
        int32_t sine[1024]={0};sine[256]=65536;sine[768]=-65536;
        CHECK(portable_gp_race_roster_init(&hit,24u,0u)==PORTABLE_GP_ROSTER_OK);
        PortableGpRacer *victim=&hit.racers[0];
        victim->coin_count_178=9u;victim->banana_hit_timer_38=0u;
        victim->world_height_q16=guard==3u?0x42000:0;
        victim->lightning_status_6a=guard==1u?20:0;
        victim->contact_status_60=guard==2u?1u:0u;
        victim->lightning_state_78=guard==4u?2u:0u;
        victim->contact_status_66=guard==5u?20:0;
        hit.racers[1].red_shell=(PortableGpRedShell){.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),
            .kind_10=PORTABLE_GP_RED_SHELL_KIND,.occupied=1u,.collision_active=1u,
            .phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET,.speed_byte_71=100u,
            .position_x_q16=victim->position_x_q16-65536,
            .position_y_q16=victim->position_y_q16};
        CHECK(portable_gp_race_roster_resolve_red_shell_kart_hit(
            &hit,1u,0u,0u,0u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        if(guard==0u){
            CHECK(hit.red_shell_hits==1u && victim->coin_count_178==5u &&
                  victim->banana_hit_timer_38==8u &&
                  (victim->component_x!=0||victim->component_y!=0) &&
                  (victim->source_flags&0x1000u)!=0u &&
                  (victim->mushroom_response_flags_b4&0x200u)!=0u);
        }else{
            CHECK(hit.red_shell_hits==0u && victim->coin_count_178==9u &&
                  victim->banana_hit_timer_38==0u &&
                  (victim->mushroom_response_flags_b4&0x200u)==0u);
        }
        CHECK(hit.racers[1].red_shell.collision_active==(guard==3u||guard==5u));
        hit.race_mode_flags_7da=0x800u;
        PortableBattleBalloonEvent balloon;
        CHECK(portable_battle_race_roster_tick_balloons(&hit,0u,3u,&balloon)==0);
        CHECK(victim->balloons_remaining==(guard==0u?2u:3u) &&
              balloon==(guard==0u?PORTABLE_BATTLE_BALLOON_POP:PORTABLE_BATTLE_BALLOON_NONE));
        PortableGpRedShellAudioEvent event;
        const PortableGpRosterStatus audio=portable_gp_race_roster_take_red_shell_hit_audio(
            &hit,0u,0u,256u,&event);
        if(guard<=1u)CHECK(audio==PORTABLE_GP_ROSTER_OK && event.song_id==(guard?124u:132u));
        else CHECK(audio==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    }
    {
        /* 59FAC/48C2C: a Broken Pier Boo haunts whoever touched it. */
        PortableGpRaceRoster haunt;
        CHECK(portable_gp_race_roster_init(&haunt,24u,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_haunt(&haunt,8u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
        CHECK(portable_gp_race_roster_haunt(&haunt,5u)==PORTABLE_GP_ROSTER_OK);
        CHECK(haunt.boo_attacks[0].occupied && haunt.boo_attacks[0].target==5u && !haunt.boo_attacks[0].phase);
        int32_t sine[1024]={0};
        for(unsigned i=0u;i<test_boo_rules()->orbit_ticks;++i)
            CHECK(portable_gp_race_roster_tick_boo_attacks(&haunt,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(haunt.racers[5].item_lock_ac==test_boo_rules()->lock_ticks && haunt.racers[0].item_lock_ac==0u);
        for(unsigned i=1u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i)
            CHECK(portable_gp_race_roster_haunt(&haunt,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_haunt(&haunt,1u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    }
    {
        PortableGpRaceRoster boo;
        CHECK(portable_gp_race_roster_init(&boo,24u,0u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned i=0u;i<8u;++i)boo.standings[i]=(uint8_t)i;
        boo.racers[3].is_player=1u;boo.racers[3].control=0x80u;
        boo.racers[3].item_inventory.held_item_17a=0x100bu;
        boo.racers[0].item_inventory.held_item_17a=1u;
        boo.racers[1].item_inventory.held_item_17a=2u;
        boo.racers[2].item_inventory.held_item_17a=0x100eu;
        CHECK(portable_gp_race_roster_use_boo(&boo,3u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.racers[3].contact_status_66==test_boo_rules()->invisibility_ticks &&
            boo.racers[3].item_inventory.held_item_17a==0x1009u &&
            boo.racers[2].item_inventory.held_item_17a==0u &&
            boo.boo_attacks[0].target==0u && boo.boo_thefts==1u);
        CHECK(boo.item_hud.remaining==0u);
        int32_t sine[1024]={0};sine[0]=0;sine[256]=65536;sine[768]=-65536;
        for(unsigned i=0u;i<test_boo_rules()->orbit_ticks;++i){
            CHECK(portable_gp_race_roster_tick_boo_attacks(&boo,sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(boo.boo_attacks[0].radius==i+1u && boo.boo_attacks[0].timer==test_boo_rules()->orbit_ticks-1u-i);
            if(i+1u<test_boo_rules()->orbit_ticks)CHECK(boo.racers[0].item_lock_ac==0u);
        }
        CHECK(portable_boo_attack_music_transition(1u,2u)==2);
        CHECK(portable_boo_attack_music_transition(1u,6u)==0);
        CHECK(portable_boo_attack_music_transition(2u,3u)==0);
        CHECK(portable_boo_attack_music_transition(3u,4u)==-2);
        CHECK(portable_boo_attack_music_transition(3u,6u)==-2);
        CHECK(portable_boo_attack_music_transition(6u,7u)==0);
        CHECK(boo.boo_attacks[0].phase==2u && boo.racers[0].item_lock_ac==test_boo_rules()->lock_ticks &&
            boo.boo_attacks[0].z_q16==0x60000);
        /* Local AC needs fresh presses; holding or releasing A retains it. */
        CHECK(portable_gp_race_roster_tick_boo_timers(&boo,0u,0u)==PORTABLE_GP_ROSTER_OK &&
            boo.racers[0].item_lock_ac==test_boo_rules()->lock_ticks);
        for(unsigned i=0u;i<(test_boo_rules()->lock_ticks+9u)/10u;++i)
            CHECK(portable_gp_race_roster_tick_boo_timers(&boo,0u,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.racers[0].item_lock_ac==0u);
        CHECK(portable_gp_race_roster_tick_boo_attacks(&boo,sine,1024u)==PORTABLE_GP_ROSTER_OK &&
            boo.boo_attacks[0].phase==4u);
        for(unsigned i=0u;i<test_boo_rules()->orbit_ticks;++i)
            CHECK(portable_gp_race_roster_tick_boo_attacks(&boo,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(!boo.boo_attacks[0].occupied && !boo.boo_attacks[0].visible);
        for(unsigned i=0u;i<test_boo_rules()->lock_ticks;++i)
            CHECK(portable_gp_race_roster_tick_protection(&boo,3u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.racers[3].contact_status_66==0);
        /* Star repels the approach without installing AC, then throws the
         * inverted sprite on source velocity21000/gravity3000. */
        boo.racers[3].item_inventory.held_item_17a=0x100bu;
        boo.racers[0].lightning_status_6a=5;
        CHECK(portable_gp_race_roster_use_boo(&boo,3u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned i=0u;i<test_boo_rules()->orbit_ticks;++i)
            CHECK(portable_gp_race_roster_tick_boo_attacks(&boo,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.boo_attacks[0].phase==6u && boo.racers[0].item_lock_ac==0u);
        CHECK(portable_gp_race_roster_tick_boo_attacks(&boo,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.boo_attacks[0].phase==7u && boo.boo_attacks[0].affine_y_q8==-256 &&
            boo.boo_attacks[0].z_q16==0x60000+(int32_t)test_boo_rules()->repel_velocity_q16 && boo.boo_attacks[0].vz_q16==(int32_t)(test_boo_rules()->repel_velocity_q16-test_boo_rules()->repel_gravity_q16));
        for(unsigned i=0u;i<60u;++i)
            CHECK(portable_gp_race_roster_tick_boo_attacks(&boo,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(!boo.boo_attacks[0].occupied);
        /* A leading user still steals but creates no attack. No eligible
         * inventory leaves the slot empty and retains the immunity. */
        for(unsigned i=0u;i<8u;++i)boo.racers[i].item_inventory=(PortableGpItemInventory){0};
        boo.racers[0].control=0x80u;boo.racers[0].item_inventory.held_item_17a=0x100bu;
        CHECK(portable_gp_race_roster_use_boo(&boo,0u)==PORTABLE_GP_ROSTER_OK &&
            boo.racers[0].item_inventory.held_item_17a==0u && boo.racers[0].contact_status_66==test_boo_rules()->invisibility_ticks &&
            !boo.boo_attacks[0].occupied);
        CHECK(boo.item_hud.current==0u && boo.item_hud.previous==0x100bu && boo.item_hud.remaining==160u);
        boo.racers[0].item_inventory.held_item_17a=0x1007u;
        boo.racers[3].item_inventory.held_item_17a=0x100bu;
        CHECK(portable_gp_race_roster_use_boo(&boo,3u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.item_hud.current==2u && boo.item_hud.previous==0x1007u && boo.item_hud.remaining==160u);
        /* Ghosts reject the contact before any shell/banana is destroyed. */
        CHECK(portable_gp_race_roster_resolve_green_shell_kart_hit(&boo,1u,0u,0u,0u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_resolve_red_shell_kart_hit(&boo,1u,0u,0u,0u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_resolve_banana_kart_hit(&boo,1u,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boo.green_shell_hits==0u && boo.red_shell_hits==0u && boo.racers[0].banana_hit_timer_38==0u);
        /* Original CPU slot/rank decision constructs Boo, then420E2
         * discards a stolen non-Boo. Immunity and the attack remain. */
        PortableGpRaceRoster cpu;
        CHECK(portable_gp_race_roster_init(&cpu,24u,0u)==PORTABLE_GP_ROSTER_OK);
        const uint8_t order[8]={2u,1u,3u,0u,4u,5u,6u,7u};
        memcpy(cpu.standings,order,sizeof order);
        cpu.cpu_items_enabled=1u;cpu.cpu_item_engine=0u;cpu.cpu_item_clock=768u;
        cpu.racers[3].source_flags=0x2000u;cpu.racers[3].rank_flags_ba=4u;
        cpu.racers[3].route_index=5u;cpu.racers[3].route_wrap_91=0u;
        cpu.racers[3].item_inventory.held_item_17a=0x100bu;
        cpu.racers[2].item_inventory.held_item_17a=0x1007u;
        CHECK(portable_gp_race_roster_use_cpu_item(&cpu,3u,8u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(cpu.boo_uses==1u && cpu.boo_thefts==1u && cpu.cpu_item_cooldown_c62==510u &&
            cpu.racers[3].item_inventory.held_item_17a==0u && cpu.racers[3].contact_status_66==test_boo_rules()->invisibility_ticks &&
            cpu.boo_attacks[0].target==2u);
        cpu.racers[2].item_lock_ac=test_boo_rules()->lock_ticks;
        CHECK(portable_gp_race_roster_tick_boo_timers(&cpu,2u,0u)==PORTABLE_GP_ROSTER_OK &&
            cpu.racers[2].item_lock_ac==test_boo_rules()->lock_ticks-1u);
        /* Star acquired during orbit clears AC and enters the same escape. */
        cpu.boo_attacks[0].phase=3u;cpu.racers[2].lightning_status_6a=10;
        CHECK(portable_gp_race_roster_tick_boo_timers(&cpu,2u,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_tick_boo_attacks(&cpu,sine,1024u)==PORTABLE_GP_ROSTER_OK &&
            cpu.racers[2].item_lock_ac==0u && cpu.boo_attacks[0].phase==6u);
        /* Negative sine products use truncation, including the signed angle
         * wrap at8000; x/y must consult different table entries. */
        PortableBooAttack actor={.occupied=1u,.phase=3u,.angle=0x7a00u,.radius=1u};
        PortableBooAttackTarget target={.lock_ticks=1u};
        PortableBooRules numeric_rules=*test_boo_rules();numeric_rules.orbit_step=1536u;
        sine[768]=-17;sine[512]=-33;
        CHECK(portable_boo_attack_tick(&actor,&target,sine,1024u,&numeric_rules));
        CHECK(actor.x_q16==-3 && actor.y_q16==-6);
    }
    {
        const PortableBooRules custom={90u,60u,16u,3072u,2048u,65536u,16384u};
        PortableGpRaceRoster arena={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&arena);arena.racer_count=4u;arena.boo_rules=&custom;
        for(unsigned i=0u;i<8u;++i)arena.standings[i]=i<4u?(uint8_t)i:255u;
        arena.racers[0].is_player=1u;
        arena.racers[1].item_inventory.held_item_17a=0x1007u;
        arena.racers[3].item_inventory.held_item_17a=0x100bu;
        arena.racers[4].item_inventory.held_item_17a=0x100au; /* Unused storage. */
        CHECK(portable_gp_race_roster_use_boo(&arena,3u)==PORTABLE_GP_ROSTER_OK);
        CHECK(arena.racers[3].contact_status_66==90&&arena.racers[3].item_inventory.held_item_17a==0x1007u&&arena.boo_thefts==1u);
        int32_t sine[1024]={0};
        for(unsigned tick=0u;tick<16u;++tick)CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(arena.boo_attacks[0].phase==2u&&arena.boo_attacks[0].radius==16u&&arena.boo_attacks[0].angle==49152u&&arena.racers[0].item_lock_ac==60u);
        CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK&&arena.boo_attacks[0].bob_angle==2048u);
        for(unsigned tick=0u;tick<6u;++tick)CHECK(portable_gp_race_roster_tick_boo_timers(&arena,0u,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(arena.racers[0].item_lock_ac==0u);
        CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK&&arena.boo_attacks[0].phase==4u);
        for(unsigned tick=0u;tick<16u;++tick)CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(!arena.boo_attacks[0].occupied&&arena.boo_attacks[0].radius==0u);
        for(unsigned tick=0u;tick<90u;++tick)CHECK(portable_gp_race_roster_tick_protection(&arena,3u)==PORTABLE_GP_ROSTER_OK);
        CHECK(arena.racers[3].contact_status_66==0);
        arena.racers[3].item_inventory.held_item_17a=0x100bu;arena.racers[0].lightning_status_6a=1;
        CHECK(portable_gp_race_roster_use_boo(&arena,3u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned tick=0u;tick<16u;++tick)CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(arena.boo_attacks[0].phase==6u&&arena.racers[0].item_lock_ac==0u);
        CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK&&arena.boo_attacks[0].phase==7u&&arena.boo_attacks[0].z_q16==0x70000&&arena.boo_attacks[0].vz_q16==0xc000);
        for(unsigned tick=0u;tick<40u;++tick)CHECK(portable_gp_race_roster_tick_boo_attacks(&arena,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(!arena.boo_attacks[0].occupied);
        for(unsigned i=0u;i<4u;++i)arena.racers[i].item_inventory=(PortableGpItemInventory){0};
        arena.racers[0].control=0x80u;arena.racers[0].item_inventory.held_item_17a=0x100bu;
        CHECK(portable_gp_race_roster_use_boo(&arena,0u)==PORTABLE_GP_ROSTER_OK&&arena.racers[0].item_inventory.held_item_17a==0u&&arena.racers[4].item_inventory.held_item_17a==0x100au);
        CHECK(portable_gp_race_roster_use_boo(&arena,4u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    }
    {
        /* Paired9904 source call: prior35A0000 and angleE00 produce
         * horizontal3270000/vertical289 before8200 adds30000. */
        int32_t sine[1024]={0};sine[56]=11039;sine[200]=30852;
        PortableGpRacer jump={.speed_q16=0x035a0042,.acceleration_q16=0x30000,
            .source_flags=0x2000,.source_material_85=0x10};
        CHECK(portable_gp_race_roster_terrain_jump(&jump,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(jump.speed_q16==0x03270042&&jump.vertical_velocity_q8==289&&
              jump.world_height_q16==0x28000&&jump.lightning_state_78==1u&&
              jump.mushroom_mode_84==1u&&jump.source_flags==0x2800u&&
              jump.mushroom_response_flags_b4==0x8000u);
        PortableKartMotionState motion={.speed_q16=jump.speed_q16,
            .acceleration_q16=jump.acceleration_q16};
        CHECK(portable_kart_motion_tick(&motion,sine,1024u)==PORTABLE_KART_MOTION_OK);
        CHECK(motion.speed_q16==0x032a0042);
        jump.source_material_85=0x40u;
        CHECK(portable_gp_race_roster_terrain_jump(&jump,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(jump.vertical_velocity_q8==263&&jump.world_height_q16==0x38700);
        jump.world_height_q16=256;jump.vertical_velocity_q8=0;
        CHECK(portable_gp_race_roster_terrain_jump(&jump,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(jump.world_height_q16==0&&jump.vertical_velocity_q8==0&&
            jump.lightning_state_78==0&&jump.mushroom_mode_84==0&&
            jump.terrain_jump_landed&&jump.source_flags==0x2800u&&
            jump.mushroom_response_flags_b4==0);
        jump.source_material_85=0x48u;
        CHECK(portable_gp_race_roster_terrain_jump(&jump,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(!jump.terrain_jump_landed&&jump.source_flags==0x2000u);
        /* A198 floors the stored word BEFORE9904. The floor discards its
         * old fraction; an already qualifying word retains its fraction.
         * Zero-route CPU3 then adds its preceding C0000 through8200. */
        static const int32_t entries[]={0x02d90042,0x02dfffff,0x02e00000,0x02e00042};
        for(size_t i=0;i<sizeof entries/sizeof entries[0];++i){
            PortableGpRacer floor={.speed_q16=entries[i],.acceleration_q16=0xc0000,
                .source_flags=0x2000,.source_material_85=0x10};
            CHECK(portable_gp_race_roster_terrain_jump(&floor,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
            CHECK(floor.speed_q16==(i==3u?0x02b40042:0x02b40000)&&
                  floor.vertical_velocity_q8==247&&floor.world_height_q16==0x28000);
            PortableKartMotionState after={.speed_q16=floor.speed_q16,
                .acceleration_q16=floor.acceleration_q16};
            CHECK(portable_kart_motion_tick(&after,sine,1024u)==PORTABLE_KART_MOTION_OK);
            CHECK(after.speed_q16==(i==3u?0x02c00042:0x02c00000));
        }
        /* A198 signed reverse floor and content-owned forward cap. The
         * cap replaces the stored word before9904, including its fraction. */
        PortableGpRacer reverse={.speed_q16=-1,.source_flags=0x200cu,
            .source_material_85=0x10u,.mushroom_mode_84=11u};
        CHECK(portable_gp_race_roster_terrain_jump(&reverse,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(reverse.speed_q16==-INT32_C(0x02b40000)&&
            reverse.vertical_velocity_q8==247&&reverse.source_flags==0x280cu&&
            reverse.mushroom_mode_84==1u);
        PortableGpRacer capped={.speed_q16=0x04000042,.source_flags=0x2000u,
            .source_material_85=0x10u};
        CHECK(portable_gp_race_roster_terrain_jump(&capped,sine,1024u,0x03a00000)==PORTABLE_GP_ROSTER_OK);
        CHECK(capped.speed_q16==INT32_C(0x03690000)&&capped.vertical_velocity_q8==312);
        /* CPU ground nibble12 always jumps, including low speed. Nibble14
         * keeps its256 threshold and small height; neither needs a ramp. */
        PortableGpRacer small={.speed_q16=0,.source_flags=0x2000u,
            .source_material_85=0x1cu};
        CHECK(portable_gp_race_roster_terrain_jump(&small,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(small.speed_q16==0x01000000&&small.vertical_velocity_q8==256&&
            small.lightning_state_78==1u&&small.mushroom_mode_84==1u);
        small=(PortableGpRacer){.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),.red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),.shell_orbit_rules=test_shell_orbit_rules(),.speed_q16=0x00ffffff,.source_flags=0x2000u,
            .source_material_85=0x1eu};
        CHECK(portable_gp_race_roster_terrain_jump(&small,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(small.lightning_state_78==0u);
        ++small.speed_q16;
        CHECK(portable_gp_race_roster_terrain_jump(&small,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(small.world_height_q16==256&&small.vertical_velocity_q8==128&&small.lightning_state_78==1u);
        small.mushroom_mode_84=8u;small.world_height_q16=256;small.vertical_velocity_q8=0;
        CHECK(portable_gp_race_roster_terrain_jump(&small,sine,1024u,0)==PORTABLE_GP_ROSTER_OK);
        CHECK(small.terrain_jump_landed&&small.mushroom_mode_84==8u);

    }
    {
        /* Row zero is the rescue lane, independent of the CPU's alternate
         * target row. The target cell points north, so no LUT sample is needed. */
        const PortableRacePoint rescue_route[]={{32,32},{32,16},{48,16}};
        uint8_t cells[16];memset(cells,1,sizeof cells);
        const PortableTrackRaster raster={.cells=cells,.capacity=16,.width=4,.height=4};
        const PortableRaceRulesView rules={.route=rescue_route,.route_count=3,
            .roster_rules=test_roster_rules()};
        const uint16_t angles[]={0};
        const uint16_t thresholds[]={592,288,496,560,576,656};
        const PortableTrackMaterialView material={.protected_hop_thresholds=thresholds,
            .protected_hop_threshold_count=6};
        int32_t sine[1024]={0};sine[32]=6393;sine[224]=32138;
        static const uint8_t hazards[]={0x20,0x22,0x24,0x26};
        for(size_t h=0;h<sizeof hazards;++h){
            PortableGpRacer racer={.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),
                .red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),
                .shell_orbit_rules=test_shell_orbit_rules(),.source_flags=0x2000,.source_material_85=hazards[h],
                .position_x_q16=40*65536,.position_y_q16=38*65536,
                .recovery_contact_34=3,.speed_q16=0x01000000,
                .acceleration_q16=0x30000,.hit_spin_82=100,.hit_counter_88=200,
                .route_row_191=7,.lightning_timer_68=60,
                .lightning_status_6a=100,.hit_cue_flags_58=0x805c,
                .mushroom_response_flags_b4=0xffff};
            CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                angles,1)==PORTABLE_GP_ROSTER_OK);
            CHECK(racer.recovery_material==hazards[h]&&racer.speed_q16==0&&
                racer.acceleration_q16==0&&racer.lightning_timer_68==0&&
                racer.hit_counter_88==0&&racer.hit_spin_82==0&&
                racer.lightning_status_6a==4&&racer.mushroom_response_flags_b4==0x4000);
            CHECK(racer.fall_audio_serial==(hazards[h]==0x20?1u:0u));
            if(hazards[h]==0x20){
                CHECK(racer.fall_audio_variant==0u);
                CHECK(racer.lightning_state_78==2&&racer.recovery_timer_9c==60&&
                    racer.world_height_q16==256&&racer.recovery_contact_34==1);
                for(unsigned tick=0;tick<60;++tick)
                    CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                        angles,1)==PORTABLE_GP_ROSTER_OK);
                CHECK(racer.fall_audio_serial==1u);
                CHECK(racer.lightning_state_78==2&&racer.recovery_timer_9c==0&&
                    racer.world_height_q16==256); /* 9B38 alone does not run C4C0 sinking. */
            }else if(hazards[h]==0x22){
                CHECK(racer.lightning_state_78==4&&racer.recovery_timer_9c==144);
                for(unsigned tick=0;tick<144;++tick)
                    CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                        angles,1)==PORTABLE_GP_ROSTER_OK);
                CHECK(racer.lightning_state_78==4&&racer.recovery_timer_9c==0);
            }
            if(hazards[h]==0x20||hazards[h]==0x22)
                CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                    angles,1)==PORTABLE_GP_ROSTER_OK);
            CHECK(racer.recovery_target_x_9e==32&&racer.recovery_target_y_a0==32&&
                racer.recovery_target_heading_a2==0);
            if(racer.lightning_state_78!=6)
                CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                    angles,1)==PORTABLE_GP_ROSTER_OK);
            CHECK(racer.lightning_state_78==6);
            racer.source_material_85=0x40; /* Changing material must retain the owner. */
            CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                angles,1)==PORTABLE_GP_ROSTER_OK);
            CHECK(racer.position_x_q16==37*65536&&racer.position_y_q16==38*65536);
            unsigned ticks=0;
            while(racer.lightning_state_78!=0&&ticks++<120)
                CHECK(portable_gp_race_roster_terrain_tick(&racer,&rules,&raster,&material,sine,1024,0,
                    angles,1)==PORTABLE_GP_ROSTER_OK);
            CHECK(ticks<120&&racer.position_x_q16==32*65536&&
                racer.position_y_q16==32*65536&&racer.world_height_q16==0&&
                racer.recovery_material==0&&racer.mushroom_mode_84==0&&
                racer.recovery_contact_34==3&&(racer.hit_state_flags_74&1));
        }
        PortableGpRacer water={.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),
                .red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),
                .shell_orbit_rules=test_shell_orbit_rules(),.source_flags=0x2000,.source_material_85=0x22,
            .speed_q16=0x035a0042};
        CHECK(portable_gp_race_roster_terrain_tick(&water,&rules,&raster,&material,sine,1024,0,
            angles,1)==PORTABLE_GP_ROSTER_OK);
        CHECK(water.lightning_state_78==1&&water.mushroom_mode_84==1&&
            water.recovery_material==0&&water.vertical_velocity_q8>0);
        PortableGpRacer protected={.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),
                .red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),
                .shell_orbit_rules=test_shell_orbit_rules(),.source_flags=0x2000,.source_material_85=0x28};
        CHECK(portable_gp_race_roster_terrain_tick(&protected,&rules,&raster,&material,sine,1024,0,
            angles,1)==PORTABLE_GP_ROSTER_OK);
        /* CPU28 uses9AA4's friction/hop branch, never the void owner. */
        CHECK(protected.recovery_material==0&&protected.lightning_state_78==1&&
            protected.vertical_velocity_q8==224&&protected.mushroom_mode_84==0);
        for(unsigned selector=0;selector<16;++selector){
            for(unsigned boundary=0;boundary<3;++boundary){
                const int32_t limit=selector>9?thresholds[selector-10]*65536:100*65536;
                PortableGpRacer hop={.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),
                .red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),
                .shell_orbit_rules=test_shell_orbit_rules(),.source_flags=0x2000,.source_material_85=0x40u,
                    .mushroom_mode_84=8u};
                hop.source_material_85=(uint8_t)(0x40u+selector*2u);
                CHECK(portable_gp_race_roster_terrain_tick(&hop,&rules,&raster,&material,sine,1024,0,
                    angles,1)==PORTABLE_GP_ROSTER_OK);
                CHECK(hop.source_surface_selector_86==selector);
                hop.source_material_85=0x28u;hop.speed_q16=limit+(int32_t)boundary-1;
                const int32_t prior=hop.speed_q16;
                CHECK(portable_gp_race_roster_terrain_tick(&hop,&rules,&raster,&material,sine,1024,0,
                    angles,1)==PORTABLE_GP_ROSTER_OK);
                CHECK(hop.speed_q16==prior-(selector>9&&boundary>0?0x400000:0)&&
                    hop.lightning_state_78==1&&hop.vertical_velocity_q8==224&&
                    hop.source_flags==0x2800&&hop.mushroom_mode_84==8u);
                hop.source_material_85=0x40; /* Airborne samples cannot reset selector. */
                CHECK(portable_gp_race_roster_terrain_tick(&hop,&rules,&raster,&material,sine,1024,0,
                    angles,1)==PORTABLE_GP_ROSTER_OK);
                CHECK(hop.source_surface_selector_86==selector&&hop.vertical_velocity_q8==198);
            }
        }
        protected=(PortableGpRacer){.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),.red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),.shell_orbit_rules=test_shell_orbit_rules(),.source_flags=0x2000,.source_material_85=0x28,
            .source_surface_selector_86=10,.speed_q16=0x3000000};
        const PortableGpRacer missing_before=protected;
        CHECK(portable_gp_race_roster_terrain_tick(&protected,&rules,&raster,NULL,sine,1024,0,
            angles,1)==PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE);
        CHECK(memcmp(&protected,&missing_before,sizeof protected)==0);
        PortableGpRacer invalid={.mushroom_rules=test_mushroom_rules(),.green_shell_rules=test_green_shell_rules(),
                .red_shell_rules=test_red_shell_rules(),.banana_rules=test_banana_rules(),
                .shell_orbit_rules=test_shell_orbit_rules(),.source_flags=0x2000,.source_material_85=0x24,
            .route_index=4,.speed_q16=100};
        const PortableGpRacer before=invalid;
        CHECK(portable_gp_race_roster_terrain_tick(&invalid,&rules,&raster,&material,sine,1024,0,
            angles,1)==PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED);
        CHECK(memcmp(&before,&invalid,sizeof invalid)==0);
    }
    {
        /* B98C/B7B8 is a geometric line crossing inside high-half cells,
         * including FF. Checkpoint index wrap alone never awards a lap. */
        uint8_t cells[128u*128u];
        const PortableTrackRaster progress_raster={cells,sizeof cells,128,128,0,0};
        const PortableRacePoint points[2]={{64,100},{64,80}};
        uint8_t modes[2]={0,0},rows[40];memset(rows,1,sizeof rows);
        PortableRaceRulesView progress_rules={.route=points,.route_count=2u,
            .ai_modes=modes,.ai_mode_count=2u,.finish_y_q16=100*65536,.lap_count=3u,
            .gp_target_table=rows,.gp_target_table_count=40u};
        test_roster_rules_attach(&progress_rules);
        const PortableGpRacer entry={.position_x_q16=64*65536,.position_y_q16=99*65536,
            .contact_center_y=101,.route_index=1u,.route_wrap_91=127u,
            .max_progress_c6=0x7f01u,.source_flags=0x20c0u};
        for(unsigned high=0u;high<4u;++high)
        {
            const uint8_t raw[4]={0u,0x80u,0x81u,0xffu};
            memset(cells,raw[high],sizeof cells);
            PortableGpRacer racer=entry;
            CHECK(portable_gp_race_roster_track_progress(&racer,&progress_rules,
                &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(racer.route_wrap_91==(high?128u:127u));
            CHECK(racer.route_index==0u && racer.max_progress_c6==(high?0x8000u:0x7f01u));
            CHECK(racer.route_row_191==0u);
            CHECK((racer.source_flags&0xc0u)==(high?0u:0xc0u));
            CHECK((racer.source_flags&3u)==(high==3u?2u:0u));
        }
        memset(cells,0x80,sizeof cells);
        PortableGpRacer carry=entry;carry.lightning_state_78=6u;
        CHECK(portable_gp_race_roster_track_progress(&carry,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(carry.route_wrap_91==127u && (carry.source_flags&0xc0u)==0xc0u);
        carry=entry;carry.lightning_state_78=7u;
        CHECK(portable_gp_race_roster_track_progress(&carry,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK && carry.route_wrap_91==128u);
        PortableGpRacer lap=entry;
        lap.route_wrap_91=128u;lap.max_progress_c6=0x8000u;
        CHECK(portable_gp_race_roster_track_progress(&lap,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(lap.route_wrap_91==129u && lap.max_progress_c6==0x8100u && lap.route_row_191==1u);
        /* Reverse/forward oscillation changes91 but cannot repeat a cue or row selection. */
        lap.contact_center_y=99;lap.position_y_q16=100*65536;
        CHECK(portable_gp_race_roster_track_progress(&lap,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK && lap.route_wrap_91==128u);
        rows[1]=2u;lap.lap_marker_a4=0u;
        lap.contact_center_y=100;lap.position_y_q16=99*65536;
        CHECK(portable_gp_race_roster_track_progress(&lap,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(lap.route_wrap_91==129u && lap.route_row_191==1u && lap.lap_marker_a4==0u);
        lap.route_wrap_91=130u;lap.max_progress_c6=0x8200u;
        lap.hit_spin_82=9;lap.hit_angle_80=7;lap.hit_state_7e=13;
        CHECK(portable_gp_race_roster_track_progress(&lap,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(lap.route_wrap_91==131u && (lap.source_flags&0x20u) && lap.route_row_191==0u &&
            lap.hit_spin_82==0 && lap.hit_angle_80==0u && lap.hit_state_7e==0u);
        /* The current signed high half floors negative fractions; F0 truncates. */
        PortableGpRacer negative=entry;negative.contact_center_y=0;negative.position_y_q16=-1;
        progress_rules.finish_y_q16=0;
        CHECK(portable_gp_race_roster_track_progress(&negative,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK && negative.route_wrap_91==128u);
        progress_rules.finish_y_q16=100*65536;
        PortableGpRacer outside=entry;outside.position_x_q16=-16*65536;
        CHECK(portable_gp_race_roster_track_progress(&outside,&progress_rules,
            &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(outside.route_wrap_91==127u && outside.route_index==1u && (outside.source_flags&3u)==1u);
        memset(cells,0,sizeof cells);modes[0]=0x80u;
        for(unsigned above=0u;above<2u;++above)
        {
            PortableGpRacer airborne=entry;airborne.world_height_q16=4*65536+(int32_t)above;
            CHECK(portable_gp_race_roster_track_progress(&airborne,&progress_rules,
                &progress_raster,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(airborne.route_index==(above?1u:0u) && airborne.acceleration_mode_19b==0x80u &&
                (airborne.source_flags&3u)==(above?2u:0u) && airborne.route_wrap_91==127u);
        }
    }
    uint8_t expected_classes[PORTABLE_GP_RACER_COUNT];
    for(unsigned i=0u;i<PORTABLE_GP_RACER_COUNT;++i)
        expected_classes[i]=test_roster_rules()->rival_classes[7u][7u-i];
    static const uint8_t expected_route[PORTABLE_GP_RACER_COUNT] = {
        0u,45u,45u,45u,45u,46u,46u,46u
    };
    /* Authored grid(888,618) and checkpoint byte/2 truncation give
     * deltas (1,-8),(-1,-6),(1,-5),(-1,-3),(1,-7),(-1,-6),(1,-4).
     * ArcTan2 plus quarter turn, narrowed to the source high byte. */
    static const uint16_t expected_angle[PORTABLE_GP_RACER_COUNT] = {
        0u,0x0500u,0xf900u,0x0800u,0xf200u,0x0500u,0xf900u,0x0900u
    };
    static const uint8_t expected_acceleration_mode[PORTABLE_GP_RACER_COUNT] = {
        0u,2u,2u,0u,0u,0u,0u,0u
    };
    static const PortableRacePoint route[47] = {
        [45] = {880,640}, [46] = {888,552}
    };
    static const uint8_t route_ai_modes[47] = {
        [45] = 3u, [46] = 2u
    };
    uint8_t raster_cells[128u * 128u];
    PortableTrackRaster raster = {
        raster_cells, sizeof raster_cells, 128u, 128u, 0u, 0u
    };
    PortableRaceRulesView rules = {
        .route = route, .route_count = 47u,
        .compatibility = {.course_id = 24u}
    };
    test_roster_rules_attach(&rules);
    int32_t zero_sine[1024] = {0};
    int32_t contact_sine[1024] = {0};
    int32_t shell_sine[1024] = {0};
    PortableGpRaceRoster roster;
    PortableGpRaceRoster generic;
    PortableGpRaceRoster before;
    PortableGpRaceRoster repeat;
    PortableGpRaceRoster no_item;
    PortableGpRaceRoster lightning;
    PortableGpRaceRoster standings;
    PortableGpRaceRoster finish;
    PortableGpRaceRoster all;
    PortableGpRaceRoster all_repeat;
    uint64_t initial_identity;

    /* Normal3285C uses the supplied EAEC table and selected route row;
     * BC84 outside-route steering instead retains90 and uses row zero. */
    for(unsigned path=0u;path<5u;++path)
    {
        const PortableRacePoint base[2]={{80,80},{160,160}};
        const PortableRacePoint alternate[2]={{48,80},{80,80}};
        PortableRaceRulesView direction_rules={.route=base,.route_count=2u,
            .gp_route_rows=alternate,.gp_route_row_count=1u,
            .finish_y_q16=64*65536,.lap_count=3u,
            .compatibility={.course_id=29u}};
        test_roster_rules_attach(&direction_rules);
        uint16_t angles[1]={0x1234u};
        PortableGpRaceRoster direction;
        memset(raster_cells,path==3u?0x81:path==2u||path==4u?127:0,sizeof raster_cells);
        CHECK(portable_gp_race_roster_init_for_rules(&direction,&direction_rules,7u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned racer=1u;racer<8u;++racer)
        {
            direction.racers[racer].position_x_q16=64*65536;
            direction.racers[racer].position_y_q16=64*65536;
            direction.racers[racer].decision_valid=1u;
            direction.racers[racer].route_row_191=path==4u?255u:path!=0u;
            if (path==3u) direction.racers[racer].component_y=-256;
        }
        AfterDriverFixture after_fixture={0};
        for(unsigned i=0u;i<8u;++i)after_fixture.before_serial[i]=direction.racers[i].last_update_serial;
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&direction,&direction_rules,
            &raster,zero_sine,1024u,NULL,NULL,NULL,0u,0,angles,1u,NULL,
            check_after_driver_order,&after_fixture)==PORTABLE_GP_ROSTER_OK);
        CHECK(after_fixture.count==7u);
        for(unsigned racer=1u;racer<8u;++racer)
        {
            CHECK(direction.racers[racer].route_angle==
                (path==0u||path==3u?0x6d00u:path==1u?0x9200u:0x6000u));
            if (path==3u)
                CHECK(direction.racers[racer].route_index==0u &&
                      direction.racers[racer].route_wrap_91==128u);
        }
    }
    /* Imported CPU tables own every handling class and the fifth catch-up
     * row. A custom table changes both steering and speed decisions in the
     * same all-racer tick used by GP, independently of the selected driver. */
    for(unsigned overspeed=0u;overspeed<2u;++overspeed)
    {
        const PortableRacePoint points[2]={{80,80},{80,80}};
        PortableGpCpuTuning tuning={0};
        for (unsigned driver = 0u; driver < 8u; ++driver)
        {
            tuning.acceleration[driver][0] = driver % 3u == 0u   ? 0u
                                             : driver % 3u == 1u ? UINT8_MAX
                                                                 : 1u;
            tuning.target_speeds[driver][4][2] = 1u;
        }
        tuning.heading_steps[4][7] = 31u;
        PortableRaceRulesView data_rules = {.route = points,
                                            .route_count = 2u,
                                            .cpu_tuning = &tuning,
                                            .compatibility = {.course_id = 29u}};
        test_roster_rules_attach(&data_rules);
        PortableGpRaceRoster data_roster;
        memset(raster_cells,0,sizeof raster_cells);
        CHECK(portable_gp_race_roster_init_for_rules(&data_roster,&data_rules,7u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned racer=1u;racer<8u;++racer)
        {
            PortableGpRacer *kart=&data_roster.racers[racer];
            kart->position_x_q16=64*65536;
            kart->position_y_q16=64*65536;
            kart->decision_valid=1u;
            kart->heading=0u;
            kart->route_angle=0x4000u;
            kart->rank_row_9a=0x84u;
            kart->acceleration_mode_19b=2u;
            kart->coin_count_178=0u;
            kart->speed_target_bias_19d=0u;
            kart->speed_q16=overspeed?100*65536:0;
            kart->acceleration_q16=overspeed?0x1234:0;
        }
        /* Low-half B98C selects mode2 from this course's table. */
        const uint8_t modes[2]={2u,2u};
        data_rules.ai_modes=modes;data_rules.ai_mode_count=2u;
        {
            PortableGpRaceRoster decisions = data_roster;
            CHECK(portable_gp_race_roster_decide_initial(&decisions, &data_rules, &raster) ==
                  PORTABLE_GP_ROSTER_OK);
            for (unsigned racer = 1u; racer < 8u; ++racer)
                CHECK(decisions.racers[racer].acceleration_q16 ==
                      (int32_t)tuning.acceleration[decisions.racers[racer].driver_class][0] *
                          INT32_C(4096));
        }
        CHECK(portable_gp_race_roster_tick_initial_all(&data_roster, &data_rules, &raster,
                                                       zero_sine, 1024u) == PORTABLE_GP_ROSTER_OK);
        for (unsigned racer = 1u; racer < 8u; ++racer)
        {
            const PortableGpRacer *kart=&data_roster.racers[racer];
            CHECK(kart->heading==31u*16u);
            CHECK(kart->acceleration_q16 ==
                  (overspeed
                       ? (int32_t)data_rules.source_auto_deceleration_table[(100u - 16u) >> 6u] *
                                 65536 +
                             0x1234
                       : (int32_t)tuning.acceleration[kart->driver_class][0] * 4096));
        }
        data_roster.racers[1].rank_row_9a = 5u;
        CHECK(portable_gp_race_roster_tick_initial_all(&data_roster,&data_rules,
            &raster,zero_sine,1024u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    }
    {
        /* A pending CPU hit must still enter each terrain rescue owner.
         * Once the sampled cell is safe, the same roster pass carries and
         * lowers the victim back into ordinary driving. */
        const PortableRacePoint points[2]={{64,32},{64,32}};
        PortableRaceRulesView hit_rules={.route=points,.route_count=2u,
            .lap_count=3u,.compatibility={.course_id=43u}};
        test_roster_rules_attach(&hit_rules);
        uint8_t ground[256u*256u]={0},flags[256];
        const PortableTerrainView terrain={ground,256u,256u,256u};
        const PortableTrackMaterialView material={.flags=flags,.count=256u};
        const uint16_t angles[0x4000]={0};
        const uint8_t hazards[4]={0x20,0x22,0x24,0x26};
        const uint8_t modes[4]={2u,4u,3u,5u};
        memset(raster_cells,0,sizeof raster_cells);
        for (unsigned kind=0u;kind<4u;++kind) {
            PortableGpRaceRoster hit;
            CHECK(portable_gp_race_roster_init_for_rules(&hit,&hit_rules,7u)==PORTABLE_GP_ROSTER_OK);
            for (unsigned index=1u;index<8u;++index) {
                hit.racers[index].position_x_q16=64*65536;
                hit.racers[index].position_y_q16=64*65536;
                hit.racers[index].decision_valid=1u;
                hit.racers[index].speed_q16=0;
                hit.racers[index].acceleration_q16=0;
            }
            PortableGpRacer *victim=&hit.racers[1];
            victim->source_flags=UINT32_C(0x3000);
            victim->mushroom_response_flags_b4=UINT16_C(0x0200);
            victim->hit_response_b6=UINT16_C(0x2000);
            victim->banana_hit_timer_38=8u;
            victim->kart_contact_state_5a=UINT16_C(0x8007);
            memset(flags,hazards[kind],sizeof flags);
            CHECK(portable_gp_race_roster_tick_initial_all_terrain(&hit,&hit_rules,&raster,
                zero_sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
            CHECK(victim->lightning_state_78==modes[kind] &&
                  victim->recovery_material==hazards[kind] &&
                  (victim->mushroom_response_flags_b4&UINT16_C(0x0200))==0u);
            memset(flags,0x48,sizeof flags);
            for (unsigned tick=0u;tick<400u && victim->lightning_state_78!=0u;++tick)
                CHECK(portable_gp_race_roster_tick_initial_all_terrain(&hit,&hit_rules,&raster,
                    zero_sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
            CHECK(victim->lightning_state_78==0u && victim->recovery_material==0u &&
                  victim->world_height_q16==0);
        }
    }
    {
        /* Occupied tiles use integrated XY but the preceding EE/F0 anchor.
         * Exercise the real CPU pass, including timer-one escape, protection,
         * guard, airborne height and a prop removed by an earlier CPU turn. */
        const PortableRacePoint points[2]={{64,32},{64,32}};
        PortableRaceRulesView prop_rules={.route=points,.route_count=2u,
            .lap_count=3u,.compatibility={.course_id=43u}};
        test_roster_rules_attach(&prop_rules);
        uint8_t ground[256u*256u]={0},flags[256];memset(flags,0x90,sizeof flags);
        const PortableTerrainView terrain={ground,256u,256u,256u};
        const PortableTrackMaterialView material={.flags=flags,.count=256u};
        const uint16_t angles[0x4000]={0};
        int32_t sine[1024];
        for(unsigned i=0u;i<1024u;++i){
            const int32_t phase=(int32_t)(i%512u);
            sine[i]=(phase<=256?phase:512-phase)*128*(i<512u?1:-1);
        }
        memset(raster_cells,0,sizeof raster_cells);
        for(unsigned family=0u;family<2u;++family)
        for(unsigned scenario=0u;scenario<12u;++scenario){
            PortableGpRaceRoster hit;
            CHECK(portable_gp_race_roster_init_for_rules(&hit,&prop_rules,7u)==0);
            PortableCourseProps props={.count=1u};
            props.actors[0]=(PortableCourseProp){.x_q16=68*65536,.y_q16=68*65536,
                .source_code=family?0x860000u:0xc0000u,.flags=1u,.affine_x_q8=256,.affine_y_q8=256};
            for(unsigned i=1u;i<8u;++i){
                hit.racers[i].position_x_q16=64*65536;
                hit.racers[i].position_y_q16=64*65536;
                hit.racers[i].decision_valid=1u;
                hit.racers[i].speed_q16=0;
                hit.racers[i].acceleration_q16=0;
                hit.racers[i].contact_status_66=2;
            }
            PortableGpRacer *victim=&hit.racers[1];
            victim->contact_status_66=0;
            victim->component_x=256;victim->component_y=128;
            victim->heading=victim->source_heading=victim->contact_heading_56=0x4000u;
            victim->banana_hit_timer_38=(uint16_t)(scenario==1u?1u:scenario==4u?2u:0u);
            victim->contact_status_66=(int16_t)(scenario==3u?2:0);
            victim->world_height_q16=scenario==5u?0x40001:0;
            if(scenario==2u || scenario==6u)victim->lightning_status_6a=20;
            if(scenario==7u)props.actors[0].state=1u;
            if(scenario==8u){victim->lightning_state_78=1u;victim->mushroom_mode_84=1u;
                victim->world_height_q16=65536;victim->vertical_velocity_q8=512;}
            if(scenario==9u){victim->mushroom_response_flags_b4=0x0200u;
                victim->hit_response_b6=0x2000u;}
            if(scenario==10u)victim->speed_q16=-65536;
            if(scenario==11u)victim->source_flags&=~UINT32_C(0x2000);
            if(scenario==6u)hit.racers[2].contact_status_66=0;
            PortableDriverObjectContactResult expected;
            CHECK((scenario==1u?portable_driver_object_contact_escape(65,64,68,68,
                sine,1024u,&expected):portable_driver_object_contact_resultant(64,64,
                68,68,256,128,0x4000u,sine,1024u,&expected))==0);
            CHECK(portable_gp_race_roster_tick_initial_all_terrain(&hit,&prop_rules,&raster,
                sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,&props,NULL,NULL)==0);
            CHECK(victim->position_x_q16==65*65536 && victim->position_y_q16==64*65536+32768);
            CHECK(props.actors[0].contact_phase==(family && (scenario==0u || scenario>=8u)));
            if(scenario<=1u || scenario>=8u){
                CHECK(victim->component_x==expected.resultant_x && victim->component_y==expected.resultant_y);
                CHECK(victim->banana_hit_timer_38==7u && victim->kart_contact_state_5a==0x8007u);
                CHECK((victim->source_flags&0x4000u)!=0u && !props.actors[0].state);
                if(scenario<=1u){
                    CHECK(victim->mushroom_mode_84==11u && victim->hit_state_7e==0u);
                    CHECK(victim->speed_q16==0 && !(victim->source_flags&0x1000u));
                    uint16_t angle;CHECK(portable_driver_vector_effect_source_angle(
                        expected.resultant_x,expected.resultant_y,&angle)==0);
                    CHECK((uint16_t)victim->hit_spin_82==(uint16_t)(angle-0x4000u));
                }
                if(scenario==8u)CHECK(victim->lightning_state_78==1u && victim->mushroom_mode_84==1u);
                if(scenario==9u)CHECK(victim->mushroom_mode_84==10u && victim->hit_state_7e==13u);
                if(scenario==10u)CHECK(victim->mushroom_mode_84==11u && victim->speed_q16==-65536);
                if(scenario==11u)CHECK(victim->mushroom_mode_84==11u &&
                    victim->hit_state_7e==11u && victim->speed_q16>0);
            }else{
                CHECK(victim->kart_contact_state_5a!=0x8007u);
                CHECK(props.actors[0].state==(scenario==2u || scenario==6u || scenario==7u));
                if(scenario==2u || scenario==6u)CHECK(props.actors[0].timer==32);
                if(scenario==6u)CHECK(hit.racers[2].kart_contact_state_5a!=0x8007u);
            }
        }
    }
    {
        /* Surface turbo goes through the real all-CPU pass: A2B4 refreshes
         * 34 before8200, then BD24 decrements once and installs imported acceleration while
         * retaining C0's fraction. Landing and expiry skip normal AI accel. */
        const PortableRacePoint points[2]={{64,32},{64,32}};
        PortableRaceRulesView boost_rules={.route=points,.route_count=2u,
            .lap_count=3u,.compatibility={.course_id=43u}};
        test_roster_rules_attach(&boost_rules);
        uint8_t ground[256u*256u]={0},flags[256];memset(flags,0x16,sizeof flags);
        const PortableTerrainView terrain={ground,256u,256u,256u};
        const PortableTrackMaterialView material={.flags=flags,.count=256u};
        PortableGpRaceRoster turbo;
        memset(raster_cells,0,sizeof raster_cells);
        CHECK(portable_gp_race_roster_init_for_rules(&turbo,&boost_rules,7u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned racer=1u;racer<8u;++racer)
        {
            PortableGpRacer *kart=&turbo.racers[racer];
            kart->position_x_q16=64*65536;kart->position_y_q16=64*65536;
            kart->decision_valid=1u;kart->speed_q16=20*65536;
            kart->acceleration_q16=0x1234;kart->hit_state_7e=0u;
        }
        for(unsigned tick=0u;tick<2u;++tick)
        {
            CHECK(portable_gp_race_roster_tick_initial_all_terrain(&turbo,&boost_rules,
                &raster,zero_sine,1024u,&terrain,&material,NULL,0u,0,NULL,0u,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
            for(unsigned racer=1u;racer<8u;++racer)
            {
                const PortableGpRacer *kart=&turbo.racers[racer];
                CHECK(kart->mushroom_mode_84==8u && kart->mushroom_timer_cc==33u &&
                    kart->hit_state_7e==14u && kart->lightning_state_78==0u &&
                    kart->acceleration_q16==(int32_t)test_mushroom_rules()->acceleration_units*65536+0x1234 &&
                    (kart->mushroom_response_flags_b4&0x80u)!=0u);
            }
        }
        memset(flags,0x40,sizeof flags);
        turbo.racers[1].mushroom_timer_cc=1u;
        turbo.racers[2].speed_q16=(int32_t)test_mushroom_rules()->max_speed_q16+65536;
        turbo.racers[2].acceleration_q16=0u;
        turbo.racers[3].lightning_state_78=1u;
        turbo.racers[3].world_height_q16=256;
        turbo.racers[3].vertical_velocity_q8=0;
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&turbo,&boost_rules,
            &raster,zero_sine,1024u,&terrain,&material,NULL,0u,0,NULL,0u,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
        CHECK(turbo.racers[1].mushroom_mode_84==0u && turbo.racers[1].mushroom_timer_cc==0u &&
            turbo.racers[1].acceleration_q16==(int32_t)test_mushroom_rules()->acceleration_units*65536+0x1234 &&
            (turbo.racers[1].mushroom_response_flags_b4&0xc0u)==0u);
        CHECK(turbo.racers[2].speed_q16==(int32_t)test_mushroom_rules()->max_speed_q16 &&
            turbo.racers[2].acceleration_q16==0u && turbo.racers[2].mushroom_timer_cc==32u);
        CHECK(turbo.racers[3].terrain_jump_landed && turbo.racers[3].mushroom_mode_84==8u &&
            turbo.racers[3].mushroom_timer_cc==32u);
        /* An airborne strip and the source status gate cannot start turbo. */
        memset(flags,0x16,sizeof flags);
        turbo.racers[4].mushroom_mode_84=0u;turbo.racers[4].hit_state_7e=5u;
        turbo.racers[5].mushroom_mode_84=0u;turbo.racers[5].lightning_state_78=1u;
        turbo.racers[5].world_height_q16=65536;turbo.racers[5].vertical_velocity_q8=0;
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&turbo,&boost_rules,
            &raster,zero_sine,1024u,&terrain,&material,NULL,0u,0,NULL,0u,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
        CHECK(turbo.racers[4].mushroom_mode_84==0u && turbo.racers[4].hit_state_7e==5u);
        CHECK(turbo.racers[5].mushroom_mode_84==0u && turbo.racers[5].lightning_state_78==1u);
    }
    for(unsigned x_leg=0u;x_leg<2u;++x_leg)
    {
        /* Carry Y crosses into FF after the first B98C sample. A8EE's
         * second sample sets outside flags before BC84, retaining checkpoint
         * zero and suppressing a finish crossing while mode6 owns motion. */
        const PortableRacePoint points[2]={{80,80},{96,96}};
        const PortableRacePoint sentinel[1]={{48,80}};
        PortableRaceRulesView carry_rules={.route=points,.route_count=2u,
            .gp_sentinel_targets=sentinel,.gp_sentinel_target_mask=1u,
            .finish_y_q16=62*65536,.lap_count=3u,.compatibility={.course_id=29u}};
        test_roster_rules_attach(&carry_rules);
        uint8_t ground[256u*256u]={0},flags[256];memset(flags,0x40,sizeof flags);
        const PortableTerrainView carry_terrain={ground,256u,256u,256u};
        const PortableTrackMaterialView carry_material={.flags=flags,.count=256u};
        memset(raster_cells,0,sizeof raster_cells);
        if (x_leg)
            for(unsigned y=0u;y<128u;++y) raster_cells[y*128u+3u]=0xff;
        else memset(raster_cells+3u*128u,0xff,128u);
        PortableGpRaceRoster carry;
        CHECK(portable_gp_race_roster_init_for_rules(&carry,&carry_rules,7u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned racer=1u;racer<8u;++racer)
        {
            PortableGpRacer *kart=&carry.racers[racer];
            kart->position_x_q16=64*65536;kart->position_y_q16=64*65536;
            kart->decision_valid=1u;kart->source_flags=0x2800u;
            kart->lightning_state_78=6u;kart->mushroom_mode_84=2u;
            kart->world_height_q16=7*65536;kart->recovery_material=0x20u;
            kart->recovery_target_x_9e=x_leg?32:64;kart->recovery_target_y_a0=x_leg?64:32;
            kart->route_wrap_91=128u;kart->max_progress_c6=0x8000u;
        }
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&carry,&carry_rules,
            &raster,zero_sine,1024u,&carry_terrain,&carry_material,NULL,0u,0,NULL,0u,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
        for(unsigned racer=1u;racer<8u;++racer)
        {
            const PortableGpRacer *kart=&carry.racers[racer];
            CHECK(kart->position_x_q16==(x_leg?61:64)*65536 &&
                kart->position_y_q16==(x_leg?64:61)*65536 &&
                kart->recovery_checkpoint_sample==!x_leg &&
                (kart->source_flags&3u)==(x_leg?0u:2u) && kart->route_index==0u &&
                kart->route_wrap_91==128u && kart->max_progress_c6==0x8000u);
            if (x_leg) CHECK(kart->route_angle==0x8000u);
        }
        if (x_leg)
        {
            carry.racers[1].position_x_q16=64*65536;
            carry_rules.gp_sentinel_target_mask=0u;
            CHECK(portable_gp_race_roster_tick_initial_all_terrain(&carry,&carry_rules,
                &raster,zero_sine,1024u,&carry_terrain,&carry_material,NULL,0u,0,NULL,0u,NULL,NULL,NULL)==
                PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED);
        }
    }
    CHECK(portable_gp_race_roster_init(&roster, 24u, 7u) ==
          PORTABLE_GP_ROSTER_OK);
    {
        PortableRaceRulesView shy_rules = rules;
        shy_rules.compatibility.course_id = 25u;
        shy_rules.start_x_q16 = INT32_C(1752) << 16u;
        shy_rules.start_y_q16 = INT32_C(1220) << 16u;
        shy_rules.start_heading = UINT16_C(0x2400);
        shy_rules.start_route_index = 52u;
        CHECK(portable_gp_race_roster_init_for_rules(
                  &generic, &shy_rules, 7u) == PORTABLE_GP_ROSTER_OK);
        CHECK(generic.course_id == 25u &&
              generic.racers[0].position_x_q16 ==
                  (INT32_C(1772) << 16u) &&
              generic.racers[0].position_y_q16 ==
                  (INT32_C(1396) << 16u) &&
              generic.racers[7].position_x_q16 ==
                  (INT32_C(1732) << 16u) &&
              generic.racers[7].position_y_q16 ==
                  (INT32_C(1228) << 16u));
        for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
            CHECK(generic.racers[index].driver_class ==
                      expected_classes[index] &&
                  generic.racers[index].heading == UINT16_C(0x2400) &&
                  generic.racers[index].source_heading == UINT16_C(0x2400));
        PortableGpKartContactReport contacts;
        CHECK(portable_gp_race_roster_collide_karts(&generic,127u,0u,zero_sine,1024u,&contacts)==PORTABLE_GP_ROSTER_OK);
        CHECK(!contacts.response[0]);
    }
    contact_sine[256]=INT32_C(32768);
    CHECK(roster.course_id == 24u && roster.authoritative_ticks == 0u);
    CHECK(roster.racers[0].max_progress_c6 == UINT16_C(0x7f2c));
    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        CHECK(roster.racers[index].driver_class == expected_classes[index]);
        CHECK(roster.racers[index].grid_slot == 14u - index * 2u);
        CHECK(roster.racers[index].source_flags ==
              (index == 0u ? 0x8000u : 0x2000u));
        CHECK(roster.racers[index].is_player == (index == 0u));
        CHECK(roster.racers[index].coin_count_178 == 5u);
        CHECK(roster.racers[index].acceleration_mode_19b ==
              expected_acceleration_mode[index]);
    }
    standings=roster;
    standings.racers[0].route_wrap_91=UINT8_C(128);
    standings.racers[0].route_index=0u;
    CHECK(portable_gp_race_roster_update_standings(&standings,0u)==
          PORTABLE_GP_ROSTER_OK);
    {
        static const uint8_t expected[PORTABLE_GP_RACER_COUNT]=
            {0u,7u,6u,5u,4u,3u,2u,1u};
        CHECK(memcmp(standings.standings,expected,sizeof expected)==0);
        for(size_t rank=0u;rank<PORTABLE_GP_RACER_COUNT;++rank)
            CHECK(standings.racers[expected[rank]].rank_flags_ba==rank*2u);
    }
    standings=roster;
    for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index)
        standings.racers[index].balloons_remaining=0u;
    standings.racers[0].balloons_remaining=3u;
    standings.racers[3].balloons_remaining=3u;
    CHECK(portable_gp_race_roster_update_standings(&standings,1u)==
          PORTABLE_GP_ROSTER_OK);
    {
        static const uint8_t expected[PORTABLE_GP_RACER_COUNT]=
            {3u,7u,6u,5u,4u,0u,2u,1u};
        CHECK(memcmp(standings.standings,expected,sizeof expected)==0);
    }
    finish=roster;
    finish.race_phase_7d4=UINT8_C(0x83);
    finish.race_mode_flags_7da=UINT16_C(0x2200);
    CHECK(portable_gp_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    finish.racers[0].source_flags|=UINT32_C(0x20);
    for(size_t index=1u;index<PORTABLE_GP_RACER_COUNT;++index)
        finish.racers[index].source_flags|=UINT32_C(0x40);
    CHECK(portable_gp_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(finish.race_phase_7d4==UINT8_C(0x84)&&
          finish.finish_countdown==0u&&finish.finish_player_rank==7u);
    CHECK((finish.racers[0].source_flags&UINT32_C(0x20))!=0u);
    for(size_t index=1u;index<PORTABLE_GP_RACER_COUNT;++index)
        CHECK((finish.racers[index].source_flags&UINT32_C(0x2070))==
              UINT32_C(0x2070));
    CHECK(portable_gp_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_OK&&finish.race_phase_7d4==UINT8_C(0x85));
    finish.race_phase_7d4=3u;
    finish.race_mode_flags_7da=UINT16_C(0x0800);
    CHECK(portable_gp_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    CHECK(portable_gp_race_roster_tick_finish_transition(NULL)==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    finish=roster;
    finish.race_phase_7d4=3u;
    finish.race_mode_flags_7da=UINT16_C(0x0800);
    CHECK(portable_battle_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index){
        finish.racers[index].balloons_remaining=0u;
        finish.racers[index].speed_q16=(int32_t)(index+1u)<<16u;
    }
    finish.racers[0].balloons_remaining=1u;
    CHECK(portable_battle_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(finish.race_phase_7d4==4u&&finish.finish_countdown==180u&&
          finish.finish_result_delay_be4==8u&&finish.finish_player_rank==0u&&
          finish.finish_overlay_variant==0u&&finish.finish_banner_occupied&&
          !finish.finish_banner_visible&&finish.finish_banner_y==63u&&
          finish.finish_banner_affine_scale==0x100u);
    for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index)
        CHECK((finish.racers[index].source_flags&UINT32_C(0x08b8))==
                  UINT32_C(0x08b8)&&finish.racers[index].speed_q16==0);
    for(size_t tick=0u;tick<180u;++tick)
        CHECK(portable_battle_race_roster_tick_finish_transition(&finish)==
              PORTABLE_GP_ROSTER_OK);
    CHECK(finish.finish_countdown==0u&&finish.race_phase_7d4==4u);
    CHECK(portable_battle_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(finish.race_phase_7d4==5u);
    CHECK(portable_battle_race_roster_tick_finish_banner(
              &finish,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(finish.finish_banner_visible&&finish.finish_banner_stage==1u&&
          finish.finish_banner_affine_scale==0x100u);
    for(size_t tick=0u;tick<33u;++tick)
        CHECK(portable_battle_race_roster_tick_finish_banner(
                  &finish,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(finish.finish_banner_stage==2u&&
          finish.finish_banner_hold_counter==120u&&
          finish.finish_banner_affine_scale==0x100u);
    for(size_t tick=0u;tick<120u;++tick)
        CHECK(portable_battle_race_roster_tick_finish_banner(
                  &finish,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(finish.finish_banner_stage==2u&&
          finish.finish_banner_hold_counter==0u);
    CHECK(portable_battle_race_roster_tick_finish_banner(
              &finish,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(finish.finish_banner_stage==3u&&finish.finish_banner_value==0u);
    for(size_t tick=0u;tick<33u;++tick)
        CHECK(portable_battle_race_roster_tick_finish_banner(
                  &finish,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(!finish.finish_banner_occupied&&!finish.finish_banner_visible&&
          finish.finish_banner_affine_scale==0x200u);
    CHECK(portable_battle_race_roster_tick_finish_banner(
              &finish,zero_sine,1024u)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    CHECK(portable_battle_race_roster_tick_finish_banner(
              NULL,zero_sine,1024u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    finish=roster;
    finish.race_phase_7d4=3u;
    finish.race_mode_flags_7da=UINT16_C(0x0c00);
    CHECK(portable_battle_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    finish.race_mode_flags_7da=UINT16_C(0x0800);
    for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index)
        finish.racers[index].balloons_remaining=0u;
    finish.racers[7].balloons_remaining=1u;
    CHECK(portable_battle_race_roster_tick_finish_transition(&finish)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(finish.finish_player_rank==7u&&finish.finish_overlay_variant==1u);
    CHECK(portable_battle_race_roster_tick_finish_transition(NULL)==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    memset(raster_cells, 0x7f, sizeof raster_cells);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        const int32_t x = roster.racers[index].position_x_q16 >> 16;
        const int32_t y = roster.racers[index].position_y_q16 >> 16;
        raster_cells[(size_t)(y / 16) * 128u + (size_t)(x / 16)] =
            index < 5u ? 45u : 0xaeu;
    }
    CHECK(portable_gp_race_roster_decide_initial(&roster, &rules, &raster) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.rank_cursor == 4u && roster.racers[3].rank_row_9a == 1u);
    CHECK(roster.racers[0].decision_valid == 0u &&
          roster.racers[0].control == 0u);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        CHECK(roster.racers[index].decision_valid == 1u);
        CHECK(roster.racers[index].route_index == expected_route[index]);
        CHECK(roster.racers[index].control == 0x8000u);
        CHECK(roster.racers[index].route_angle == expected_angle[index]);
        CHECK(roster.racers[index].acceleration_q16 ==
              (int32_t)rules.cpu_tuning->acceleration[expected_classes[index]][0]*4096);
    }
    /* 41F18 gives protection precedence over shrink/input lock: pacing
     * depends on an earlier local driver or the next rank's checkpoint. */
    for (unsigned branch=0u;branch<4u;++branch) {
        PortableGpRaceRoster pacing=roster;
        pacing.rank_cursor=3u;
        pacing.racers[0].rank_flags_ba=14u;
        for (unsigned i=1u;i<8u;++i) pacing.racers[i].rank_flags_ba=(uint16_t)((i-1u)*2u);
        pacing.racers[3].lightning_status_6a=100;
        pacing.racers[3].item_lock_ac=10u;
        pacing.racers[3].lightning_timer_68=100u;
        if (branch==1u) {
            pacing.racers[4].position_x_q16=pacing.racers[5].position_x_q16;
            pacing.racers[4].position_y_q16=pacing.racers[5].position_y_q16;
        }
        if (branch==2u) {
            const uint16_t rank=pacing.racers[1].rank_flags_ba;
            pacing.racers[1].rank_flags_ba=pacing.racers[0].rank_flags_ba;
            pacing.racers[0].rank_flags_ba=rank;
        }
        if (branch==3u) pacing.racers[3].source_flags|=UINT32_C(0x20);
        CHECK(portable_gp_race_roster_decide_initial(&pacing,&rules,&raster)==PORTABLE_GP_ROSTER_OK);
        CHECK(pacing.racers[3].rank_row_9a==(branch==0u?0u:branch==1u?2u:branch==2u?4u:3u));
    }
    CHECK(portable_gp_race_roster_resolve_initial_contact(&roster,47u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.contact_events == 1u &&
          roster.racers[1].contact_mode_6e == 8u &&
          roster.racers[1].contact_phase_6f == 2u);
    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
        if (index != 1u)
            CHECK(roster.racers[index].contact_mode_6e == 0u &&
                  roster.racers[index].contact_phase_6f == 0u);
    CHECK(portable_gp_race_roster_resolve_initial_contact(&roster,47u) ==
          PORTABLE_GP_ROSTER_OK && roster.contact_events == 1u);
    CHECK(portable_gp_race_roster_collect_coin(&roster,0u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].coin_count_178 == 6u && roster.coin_events == 1u);
    CHECK(portable_gp_race_roster_begin_item_box(
              &roster,7u,PORTABLE_GP_ITEM_BOX_KIND,0u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[7].item_inventory.held_item_17a == 1u &&
          roster.racers[7].item_inventory.roulette_index_70 == 30 &&
          roster.item_box_events == 1u);
    for (unsigned tick = 0u; tick < 31u; ++tick)
        CHECK(portable_gp_race_roster_tick_item(
                  &roster,7u,0u,0x1000u) == PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[7].item_inventory.held_item_17a == 0x1000u &&
          portable_gp_item_hud_value(&roster.racers[7].item_inventory) ==
              0x1000u && roster.item_settlements == 1u);
    CHECK(portable_gp_race_roster_init(&no_item,24u,7u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_begin_item_box(
              &no_item,4u,PORTABLE_GP_ITEM_BOX_KIND,0u) ==
          PORTABLE_GP_ROSTER_OK);
    for (unsigned tick = 0u; tick < 31u; ++tick)
        CHECK(portable_gp_race_roster_tick_item(
                  &no_item,4u,0u,0u) == PORTABLE_GP_ROSTER_OK);
    CHECK(no_item.racers[4].item_inventory.held_item_17a == 0u &&
          no_item.racers[4].item_inventory.active == 0u &&
          no_item.item_settlements == 1u);
    CHECK(portable_gp_race_roster_tick_item(
              &no_item,0u,0u,0u) == PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(portable_gp_race_roster_init(&lightning,24u,7u)==
          PORTABLE_GP_ROSTER_OK);
    {
        for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index){
            uint32_t weight=0u;
            const uint16_t flags=test_roster_rules()->contact_flags[lightning.racers[index].driver_class];
            const uint32_t expected_weight=fixture_roulette.contact_weight[flags&7u][(flags&0x10u)?0u:1u]+
                (index==0u?12u*lightning.racers[index].coin_count_178:0u);
            CHECK(lightning.racers[index].hit_cue_flags_58==flags);
            CHECK(portable_gp_race_roster_contact_weight(
                      &lightning,index,&weight)==PORTABLE_GP_ROSTER_OK&&
                  weight==expected_weight);
        }
        /* The original Thumb code adds 12 per player coin; this matters
         * at the source-paired third Peach kart contact with six coins. */
        lightning.racers[0].coin_count_178=6u;
        {
            uint32_t weight=0u;
            CHECK(portable_gp_race_roster_contact_weight(
                      &lightning,0u,&weight)==PORTABLE_GP_ROSTER_OK&&
                  weight==fixture_roulette.contact_weight[lightning.racers[0].hit_cue_flags_58&7u][0]+72u);
        }
        lightning.racers[0].coin_count_178=5u;
    }
    {
        PortableGpKartContactReport contacts;
        lightning.racers[0].position_x_q16=INT32_C(0x038c0000);
        lightning.racers[0].position_y_q16=INT32_C(0x02d65b00);
        lightning.racers[0].component_x=0;
        lightning.racers[0].component_y=-575;
        lightning.racers[2].position_x_q16=INT32_C(0x03885e00);
        lightning.racers[2].position_y_q16=INT32_C(0x02d25a00);
        lightning.racers[2].component_x=-40;
        lightning.racers[2].component_y=-179;
        lightning.racers[2].heading=UINT16_C(0xf700);
        /* Nearby actors share the source collision checkpoint; the old
         * opening-only probe left this rival at its unrelated grid index0. */
        lightning.racers[2].route_index=lightning.racers[0].route_index;
        /* The response consumes the player's prior pre-motion center. */
        lightning.racers[0].contact_center_x=908;
        lightning.racers[0].contact_center_y=727;
        lightning.racers[2].contact_center_x=900;
        lightning.racers[2].contact_center_y=727;
        /* The horizontal center axis is +quarter turn: Q15 sine1/cosine0.
         * Authored masses154/16 give local normal(-80*16)/170 = -7;
         * the unequal-class separation rule installs rival normal -7-512.
         * Tangential components -575/-179 are unchanged. */
        CHECK(portable_gp_race_roster_collide_karts(&lightning,47u,0u,contact_sine,1024u,&contacts)==
                  PORTABLE_GP_ROSTER_OK&&contacts.response[0]==PORTABLE_GP_KART_CONTACT_PUSH);
        CHECK(lightning.racers[0].component_x==-7&&
              lightning.racers[0].component_y==-575&&
              lightning.racers[2].component_x==-519&&
              lightning.racers[2].component_y==-179&&
              lightning.racers[2].position_x_q16==INT32_C(0x03885e00)&&
              lightning.racers[0].coin_count_178==4u&&
              lightning.racers[2].coin_count_178==5u&&
              lightning.racers[0].banana_hit_timer_38==8u&&
              lightning.racers[2].banana_hit_timer_38==8u&&
              lightning.racers[0].kart_contact_state_5a==UINT16_C(0x8007)&&
              lightning.racers[2].kart_contact_state_5a==UINT16_C(0x8007));
    }
    lightning.racers[0].control=UINT16_C(0x0080);
    lightning.racers[0].item_response_flags_b2=UINT16_C(0x0040);
    lightning.racers[0].item_inventory.held_item_17a=UINT16_C(0x100c);
    lightning.racers[0].item_inventory.preview_item=UINT16_C(0x100c);
    lightning.racers[0].item_inventory.settled_item=UINT16_C(0x100c);
    lightning.racers[0].item_inventory.active=1u;
    lightning.racers[1].lightning_state_78=1u;
    lightning.racers[1].speed_q16=INT32_C(0x01230000);
    lightning.racers[2].lightning_state_78=2u;
    lightning.racers[3].lightning_status_6a=1;
    CHECK(portable_gp_race_roster_use_lightning(&lightning,0u)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(lightning.racers[0].item_inventory.held_item_17a==0u&&
          lightning.racers[0].item_response_flags_b2==0u&&
          lightning.lightning_duration_c82==test_item_status_rules()->lightning_ticks&&
          lightning.lightning_state_80d==2u&&
          lightning.lightning_audio_pending&&lightning.item_uses==1u&&
          lightning.lightning_events==1u);
    CHECK(lightning.racers[1].mushroom_response_flags_b4==
              UINT16_C(0x0300)&&
          lightning.racers[1].hit_response_b6==UINT16_C(0x1000)&&
          lightning.racers[1].hit_state_flags_74==UINT16_C(3)&&
          lightning.racers[1].speed_q16==0&&
          lightning.racers[1].lightning_timer_68==test_item_status_rules()->lightning_ticks);
    CHECK(lightning.racers[2].mushroom_response_flags_b4==0u&&
          lightning.racers[2].hit_response_b6==0u&&
          lightning.racers[2].lightning_timer_68==0u&&
          lightning.racers[3].mushroom_response_flags_b4==0u&&
          lightning.racers[3].lightning_timer_68==0u);
    CHECK(lightning.racers[4].mushroom_response_flags_b4==
              UINT16_C(0x0300)&&
          lightning.racers[4].hit_response_b6==UINT16_C(0x1000)&&
          lightning.racers[4].hit_state_flags_74==UINT16_C(3)&&
          lightning.racers[4].lightning_timer_68==test_item_status_rules()->lightning_ticks);
    {
        PortableGpLightningAudioEvent event={0};
        CHECK(portable_gp_race_roster_take_lightning_audio(
                  &lightning,&event)==PORTABLE_GP_ROSTER_OK);
        CHECK(event.song_id==126u&&event.player==10u&&
              !lightning.lightning_audio_pending);
    }
    CHECK(portable_gp_race_roster_use_lightning(&lightning,0u)==
          PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    {
        uint32_t weight=0u;
        CHECK(portable_gp_race_roster_contact_weight(
                  &lightning,1u,&weight)==PORTABLE_GP_ROSTER_OK&&weight==fixture_roulette.contact_weight[lightning.racers[1].hit_cue_flags_58&7u][0]);
    }
    CHECK(portable_gp_race_roster_tick_lightning_timer(&lightning,1u)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(lightning.racers[1].lightning_timer_68==test_item_status_rules()->lightning_ticks-1u&&
          (lightning.racers[1].hit_cue_flags_58&UINT16_C(0x0010))==0u);
    {
        uint32_t weight=0u;
        uint16_t affine_scale=0u;
        CHECK(portable_gp_race_roster_contact_weight(
                  &lightning,1u,&weight)==PORTABLE_GP_ROSTER_OK&&weight==fixture_roulette.contact_weight[lightning.racers[1].hit_cue_flags_58&7u][1]&&
              portable_gp_race_roster_lightning_affine_scale(
                  &lightning,1u,UINT16_C(0x0100),&affine_scale)==
                  PORTABLE_GP_ROSTER_OK&&affine_scale==UINT16_C(0x0180));
    }
    for(unsigned tick=1u;tick<test_item_status_rules()->lightning_ticks;++tick)
        CHECK(portable_gp_race_roster_tick_lightning_timer(&lightning,1u)==
              PORTABLE_GP_ROSTER_OK);
    CHECK(lightning.racers[1].lightning_timer_68==0u&&
          (lightning.racers[1].hit_cue_flags_58&UINT16_C(0x0010))!=0u);
    {
        uint16_t affine_scale=0u;
        CHECK(portable_gp_race_roster_lightning_affine_scale(
                  &lightning,1u,UINT16_C(0x0100),&affine_scale)==
                  PORTABLE_GP_ROSTER_OK&&affine_scale==UINT16_C(0x0100));
    }
    {
        PortableGpRaceRoster ai_lightning={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&ai_lightning);
        CHECK(portable_gp_race_roster_init(&ai_lightning,24u,7u)==
              PORTABLE_GP_ROSTER_OK);
        ai_lightning.racers[1].control=UINT16_C(0x0080);
        ai_lightning.racers[1].item_inventory.held_item_17a=
            UINT16_C(0x100c);
        CHECK(portable_gp_race_roster_use_lightning(&ai_lightning,1u)==
              PORTABLE_GP_ROSTER_OK);
        CHECK(ai_lightning.racers[0].lightning_timer_68==test_item_status_rules()->lightning_ticks&&
              ai_lightning.lightning_audio_pending&&
              ai_lightning.lightning_effect.occupied&&
              !ai_lightning.lightning_effect.visible&&
              ai_lightning.lightning_effect.target_index==0u&&
              ai_lightning.lightning_effect_events==1u);
        for(unsigned tick=0u;tick<56u;++tick){
            const int8_t frame=fixture_roulette.lightning_slots[tick/4u];
            CHECK(portable_gp_race_roster_tick_lightning_effect(
                      &ai_lightning)==PORTABLE_GP_ROSTER_OK);
            CHECK(ai_lightning.lightning_effect.script_tick==tick+1u&&
                  ai_lightning.lightning_effect.occupied&&
                  ai_lightning.lightning_effect.visible==(frame>=0));
            if(frame>=0)
                CHECK(ai_lightning.lightning_effect.frame_index==
                      (uint8_t)frame);
        }
        CHECK(portable_gp_race_roster_tick_lightning_effect(&ai_lightning)==
              PORTABLE_GP_ROSTER_OK);
        CHECK(!ai_lightning.lightning_effect.occupied&&
              !ai_lightning.lightning_effect.visible&&
              portable_gp_race_roster_tick_lightning_effect(&ai_lightning)==
                  PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    }
    {
        uint32_t weight=0u;
        CHECK(portable_gp_race_roster_contact_weight(
                  &lightning,1u,&weight)==PORTABLE_GP_ROSTER_OK&&weight==fixture_roulette.contact_weight[lightning.racers[1].hit_cue_flags_58&7u][0]);
        lightning.racers[1].hit_cue_flags_58=UINT16_C(0x0027);
        lightning.racers[1].lightning_status_6a=1;
        CHECK(portable_gp_race_roster_contact_weight(
                  &lightning,1u,&weight)==PORTABLE_GP_ROSTER_OK&&
              weight==UINT32_C(0x00020000));
        lightning.racers[1].hit_cue_flags_58=UINT16_C(0x0007);
        lightning.racers[1].lightning_status_6a=0;
        CHECK(portable_gp_race_roster_contact_weight(
                  &lightning,1u,&weight)==PORTABLE_GP_ROSTER_OK&&weight==1u);
        CHECK(portable_gp_race_roster_contact_weight(
                  NULL,1u,&weight)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    }
    lightning.racers[1].lightning_timer_68=UINT16_C(0xffff);
    lightning.racers[1].hit_cue_flags_58=0u;
    CHECK(portable_gp_race_roster_tick_lightning_timer(&lightning,1u)==
          PORTABLE_GP_ROSTER_OK);
    CHECK(lightning.racers[1].lightning_timer_68==0u&&
          (lightning.racers[1].hit_cue_flags_58&UINT16_C(0x0010))!=0u);
    roster.racers[7].control |= UINT16_C(0x0080);
    roster.racers[7].source_heading = 0u;
    shell_sine[0] = INT32_C(0x00010000);
    shell_sine[0x100] = INT32_C(0x00008000);
    CHECK(portable_gp_race_roster_use_green_shell(
              &roster,7u,shell_sine,1024u) == PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[7].item_inventory.held_item_17a == 0u &&
          roster.racers[7].green_shell.kind_10 == 0x8401u &&
          roster.racers[7].green_shell.phase ==
              PORTABLE_GP_GREEN_SHELL_ATTACHED &&
          roster.racers[7].item_use_state_17c == 1u && roster.item_uses == 1u);
    CHECK(portable_gp_race_roster_tick_green_shell_attached(
              &roster,7u,shell_sine,1024u) == PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[7].green_shell.phase ==
              PORTABLE_GP_GREEN_SHELL_ACTIVE &&
          roster.racers[7].green_shell.flags_34 == 3u &&
          roster.racers[7].shell_item_18c == 0x1000u);
    {
    const PortableGpRacer green_victim_before = roster.racers[1];
    roster.racers[7].control &= UINT16_C(0xffbf);
    CHECK(portable_gp_race_roster_release_green_shell(
              &roster,7u,shell_sine,1024u) == PORTABLE_GP_ROSTER_OK);
    roster.racers[7].green_shell.position_x_q16 = INT32_C(0x02000000);
    roster.racers[7].green_shell.position_y_q16 = INT32_C(0x02000000);
    roster.racers[7].green_shell.state_71 = 48u;
    roster.racers[1].position_x_q16 = INT32_C(0x02010000);
    roster.racers[1].position_y_q16 = INT32_C(0x02000000);
    CHECK(portable_gp_race_roster_resolve_green_shell_kart_hit(
              &roster,7u,1u,0u,0u,shell_sine,1024u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[7].green_shell.phase ==
              PORTABLE_GP_GREEN_SHELL_DESTROYING &&
          roster.green_shell_hits == 1u &&
          roster.racers[1].red_shell_hit_audio_pending &&
          (roster.racers[1].mushroom_response_flags_b4 &
              UINT16_C(0x0200)) != 0u);
    CHECK(portable_gp_race_roster_tick_green_shell_destroying(
              &roster,7u) == PORTABLE_GP_ROSTER_OK &&
          roster.racers[7].green_shell.position_z_q16 > 0);
    roster.racers[1] = green_victim_before;
    }
    uint8_t banana_ground[256u*256u]={0};
    const uint8_t banana_flags[256]={0x40u};
    const PortableTerrainView banana_terrain={banana_ground,256u,256u,256u};
    const PortableTrackMaterialView banana_material={.flags=banana_flags,.count=256u};
    const PortableGpBananaWorld banana_world={.material=&banana_material,.raster=&raster,.terrain=&banana_terrain};
    roster.racers[0].item_inventory.held_item_17a = UINT16_C(0x1005);
    roster.racers[0].item_inventory.settled_item = UINT16_C(0x1005);
    roster.racers[0].control = UINT16_C(0x00c0);
    CHECK(portable_gp_race_roster_use_banana(
              &roster,0u,shell_sine,1024u) == PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_tick_banana(
              &roster,0u,&banana_world,shell_sine,1024u) == PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].banana.kind_10 == UINT16_C(0x8001) &&
          roster.racers[0].banana.phase == PORTABLE_GP_BANANA_HELD &&
          roster.racers[0].banana.attachment_ticks_4c == 1u &&
          roster.racers[0].shell_item_18c == UINT16_C(0x1005) &&
          roster.item_uses == 2u);
    roster.racers[0].control &= UINT16_C(0xffbf);
    CHECK(portable_gp_race_roster_tick_banana(
              &roster,0u,&banana_world,shell_sine,1024u) == PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].banana.phase == PORTABLE_GP_BANANA_DROPPED &&
          roster.racers[0].banana.position_z_q16 == 0 &&
          roster.racers[0].banana.collision_active == 1u &&
          roster.racers[0].shell_item_18c == 0u);
    {
        const PortableGpRacer banana_owner=roster.racers[0];
        const PortableGpRacer victim=roster.racers[1];
        roster.racers[1].source_flags&=UINT32_C(0xffffdfff);
        roster.racers[1].is_player=1u;
        roster.racers[1].speed_q16=INT32_C(0x01a60000);
        CHECK(portable_gp_race_roster_resolve_banana_kart_hit(
                  &roster,0u,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].banana_hit_timer_38==3u&&
              roster.racers[1].mushroom_response_flags_b4==
                  UINT16_C(0x1000)&&
              roster.racers[0].banana.collision_result==
                  PORTABLE_GP_BANANA_COLLISION_DESTROYING);
        {
            PortableGpBananaAudioEvent event={0};
            CHECK(portable_gp_race_roster_take_banana_reaction_audio(
                      &roster,1u,0u,&event)==PORTABLE_GP_ROSTER_OK);
            CHECK(event.song_id==135u&&event.player==6u&&event.volume==218u&&
                  event.pitch==-90&&
                  !roster.racers[1].banana_reaction_audio_pending);
        }
        for(unsigned tick=0u;tick<77u;++tick)
            CHECK(portable_gp_race_roster_tick_banana_hit_response(
                      &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_spin_82==512&&
              roster.racers[1].banana_spin_timer_c8==0u&&
              roster.racers[1].banana_ground_timer_b8==0u&&
              roster.racers[1].mushroom_response_flags_b4==
                  UINT16_C(0x0408));
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==0u&&
              roster.racers[1].hit_spin_82==288);
        roster.racers[1].hit_spin_82=-512;
        roster.racers[1].mushroom_response_flags_b4=UINT16_C(0x1000);
        for(unsigned tick=0u;tick<77u;++tick)
            CHECK(portable_gp_race_roster_tick_banana_hit_response(
                      &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_spin_82==-1024);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==0u&&
              roster.racers[1].hit_spin_82==-800);
        roster.racers[1].hit_spin_82=0;
        roster.racers[1].mushroom_response_flags_b4=UINT16_C(0x1002);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_spin_82==0&&
              roster.racers[1].banana_spin_timer_c8==60u&&
              roster.racers[1].banana_ground_timer_b8==59u&&
              roster.racers[1].mushroom_response_flags_b4==
                  UINT16_C(0x040a));
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==0u&&
              roster.racers[1].hit_spin_82==0);
        roster.racers[1].hit_state_7e=14u;
        roster.racers[1].hit_angle_80=UINT16_C(0x3fff);
        roster.racers[1].hit_spin_82=512;
        roster.racers[1].mushroom_mode_84=0u;
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_spin_82==288&&
              roster.racers[1].mushroom_mode_84==0u);
        roster.racers[1].hit_angle_80=UINT16_C(0x4000);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==11u&&
              roster.racers[1].hit_spin_82==64&&
              roster.racers[1].mushroom_mode_84==11u);
        roster.racers[1].speed_q16=INT32_C(0x00100000);
        roster.racers[1].hit_ground_flags_96=0u;
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==11u&&
              roster.racers[1].hit_angle_80==UINT16_C(0x4000)&&
              roster.racers[1].hit_spin_82==64&&
              roster.racers[1].mushroom_mode_84==11u);
        roster.racers[1].speed_q16=INT32_C(0x000fffff);
        roster.racers[1].hit_ground_flags_96=0u;
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_angle_80==0u&&
              roster.racers[1].hit_spin_82==64&&
              roster.racers[1].mushroom_mode_84==0u);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==0u&&
              roster.racers[1].hit_spin_82==0);
        roster.racers[1].hit_state_7e=11u;
        roster.racers[1].mushroom_mode_84=11u;
        roster.racers[1].hit_ground_flags_96=UINT16_C(0x8000);
        roster.racers[1].speed_q16=INT32_C(0x00ffffff);
        roster.racers[1].hit_angle_80=UINT16_C(0x4000);
        roster.racers[1].hit_spin_82=INT16_C(0x0500);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==11u&&
              roster.racers[1].hit_angle_80==0u&&
              roster.racers[1].hit_spin_82==INT16_C(0x0500)&&
              roster.racers[1].mushroom_mode_84==11u);
        roster.racers[1].speed_q16=INT32_C(0x01000000);
        roster.racers[1].hit_angle_80=UINT16_C(0x0500);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==11u&&
              roster.racers[1].hit_angle_80==UINT16_C(0x0100)&&
              roster.racers[1].hit_spin_82==INT16_C(0x0100)&&
              roster.racers[1].mushroom_mode_84==11u);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_angle_80==0u&&
              roster.racers[1].hit_spin_82==0&&
              roster.racers[1].mushroom_mode_84==0u);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==0u);
        roster.racers[1].hit_state_7e=11u;
        roster.racers[1].mushroom_mode_84=11u;
        roster.racers[1].hit_angle_80=UINT16_C(0xfb00);
        roster.racers[1].hit_spin_82=-INT16_C(0x0500);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==11u&&
              roster.racers[1].hit_angle_80==UINT16_C(0xff00)&&
              roster.racers[1].hit_spin_82==-INT16_C(0x0100));
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_angle_80==0u&&
              roster.racers[1].hit_spin_82==0&&
              roster.racers[1].mushroom_mode_84==0u);
        roster.racers[1].hit_state_7e=11u;
        roster.racers[1].mushroom_mode_84=11u;
        roster.racers[1].hit_ground_flags_96=UINT16_C(0x4000);
        roster.racers[1].speed_q16=INT32_C(0x000fffff);
        roster.racers[1].hit_angle_80=UINT16_C(0x4000);
        CHECK(portable_gp_race_roster_tick_banana_hit_response(
                  &roster,1u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].hit_state_7e==14u&&
              roster.racers[1].hit_angle_80==0u&&
              roster.racers[1].mushroom_mode_84==0u);
        for(unsigned tick=0u;tick<14u;++tick)
            CHECK(portable_gp_race_roster_tick_banana_destroying(
                      &roster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(!roster.racers[0].banana.occupied&&
              roster.racers[0].banana.phase==PORTABLE_GP_BANANA_DESTROYED);
        roster.racers[0]=banana_owner;
        roster.racers[1]=victim;
        roster.racers[1].source_flags|=UINT32_C(0x00002000);
        roster.racers[1].is_player=1u;
        roster.racers[1].mushroom_response_flags_b4=0u;
        roster.racers[1].hit_response_b6=0u;
        roster.racers[1].hit_state_flags_74=0u;
        roster.racers[1].component_x=123;
        roster.racers[1].component_y=-456;
        CHECK(portable_gp_race_roster_resolve_banana_kart_hit_with_context(
                  &roster,0u,1u,0u,UINT16_C(0x0100))==
                      PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].banana_hit_timer_38==3u&&
              roster.racers[1].mushroom_response_flags_b4==UINT16_C(0x0200)&&
              roster.racers[1].hit_response_b6==UINT16_C(0x2000)&&
              roster.racers[1].hit_state_flags_74==UINT16_C(3)&&
              roster.racers[1].component_x==0&&
              roster.racers[1].component_y==0&&
              !roster.racers[1].banana_reaction_audio_pending&&
              roster.racers[1].banana_special_entry_audio_pending);
        {
            PortableGpBananaAudioEvent event={0};
            CHECK(portable_gp_race_roster_take_banana_special_entry_audio(
                      &roster,1u,0u,&event)==PORTABLE_GP_ROSTER_OK);
            CHECK(event.song_id==162u&&event.player==5u&&
                  event.volume==UINT16_C(0x0100)&&event.pitch==0&&
                  !roster.racers[1].banana_special_entry_audio_pending);
        }
        {
            PortableGpBananaAudioEvent event={0};
            CHECK(portable_gp_race_roster_take_banana_special_class_audio(
                      &roster,1u,0u,&event)==PORTABLE_GP_ROSTER_OK);
            CHECK(event.song_id==test_hazard_rules()->impact_voices[portable_gp_racer_voice_class(&roster.racers[1])][0][(roster.racers[1].hit_cue_flags_58&0x10u)?0u:1u]&&event.player==2u&&
                  event.volume==UINT16_C(0x0100)&&event.pitch==0);
        }
        for(unsigned tick=0u;tick<14u;++tick)
            CHECK(portable_gp_race_roster_tick_banana_destroying(
                      &roster,0u)==PORTABLE_GP_ROSTER_OK);
        roster.racers[0]=banana_owner;
        roster.racers[1].is_player=0u;
        roster.racers[1].banana_hit_timer_38=0u;
        roster.racers[1].rank_flags_ba=2u;
        roster.racers[1].source_flags|=UINT32_C(0x00008000);
        CHECK(portable_gp_race_roster_resolve_banana_kart_hit_with_context(
                  &roster,0u,1u,1u,UINT16_C(0x0020))==
                      PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[1].mushroom_response_flags_b4==UINT16_C(0x0300)&&
              roster.racers[1].hit_response_b6==UINT16_C(0x1000)&&
              roster.racers[1].hit_state_flags_74==UINT16_C(3));
        {
            PortableGpBananaAudioEvent event={0};
            CHECK(portable_gp_race_roster_take_banana_special_class_audio(
                      &roster,1u,0u,&event)==PORTABLE_GP_ROSTER_OK);
            CHECK(event.song_id==test_hazard_rules()->impact_voices[portable_gp_racer_voice_class(&roster.racers[1])][1][(roster.racers[1].hit_cue_flags_58&0x10u)?0u:1u]&&event.player==4u&&
                  event.volume==UINT16_C(0x0040)&&event.pitch==0);
        }
        for(unsigned tick=0u;tick<14u;++tick)
            CHECK(portable_gp_race_roster_tick_banana_destroying(
                      &roster,0u)==PORTABLE_GP_ROSTER_OK);
        roster.racers[1]=victim;
    }
    roster.racers[0].item_inventory.held_item_17a=UINT16_C(0x1002);
    roster.racers[0].item_inventory.settled_item=UINT16_C(0x1002);
    roster.racers[0].control|=UINT16_C(0x0040);
    CHECK(portable_gp_race_roster_use_red_shell(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_tick_red_shell_attached(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_tick_red_shell_held_active(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].red_shell.kind_10==UINT16_C(0x8402)&&
          roster.racers[0].red_shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE&&
          roster.racers[0].shell_item_18c==UINT16_C(0x1002)&&
          roster.item_uses==3u);
    roster.racers[0].control&=UINT16_C(0xffbf);
    roster.racers[0].control|=UINT16_C(0x0400);
    CHECK(portable_gp_race_roster_release_red_shell_backward(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].red_shell.phase==
              PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD&&
          roster.racers[0].red_shell.angle_26==UINT16_C(0x8000)&&
          roster.racers[0].red_shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks&&
          roster.racers[0].shell_item_18c==0u&&
          roster.racers[0].item_use_state_17c==0u);
    memset(&roster.racers[0].red_shell,0,sizeof roster.racers[0].red_shell);
    roster.racers[0].source_flags&=UINT32_C(0xfffeffff);
    roster.racers[0].control=UINT16_C(0x00c0);
    roster.racers[0].rank_flags_ba=0u;
    roster.racers[0].item_inventory.held_item_17a=UINT16_C(0x1002);
    roster.racers[0].item_inventory.settled_item=UINT16_C(0x1002);
    CHECK(portable_gp_race_roster_use_red_shell(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_tick_red_shell_attached(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    roster.racers[0].control&=UINT16_C(0xffbf);
    CHECK(portable_gp_race_roster_release_red_shell_forward_leader(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].red_shell.phase==
              PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET&&
          roster.racers[0].red_shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u&&
          roster.racers[0].red_shell.target_valid==0u&&
          roster.item_uses==4u);
    CHECK(portable_gp_red_shell_tick_forward_no_target_clear(
              &roster.racers[0].red_shell,1u,shell_sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(roster.racers[0].red_shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration&&
          roster.racers[0].red_shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks-1u);
    memset(&roster.racers[0].red_shell,0,sizeof roster.racers[0].red_shell);
    roster.racers[0].source_flags&=UINT32_C(0xfffeffff);
    roster.racers[0].control=UINT16_C(0x00c0);
    roster.racers[0].rank_flags_ba=14u;
    roster.racers[0].item_inventory.held_item_17a=UINT16_C(0x1002);
    roster.racers[0].item_inventory.settled_item=UINT16_C(0x1002);
    CHECK(portable_gp_race_roster_use_red_shell(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(portable_gp_race_roster_tick_red_shell_attached(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    roster.racers[0].control&=UINT16_C(0xffbf);
    CHECK(portable_gp_race_roster_release_red_shell_forward_target(
              &roster,0u,shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].red_shell.phase==
              PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET&&
          roster.racers[0].red_shell.target_valid==1u&&
          roster.racers[0].red_shell.target_racer_index==1u&&
          roster.item_uses==5u);
    CHECK(portable_gp_red_shell_tick_forward_target_far_clear(
              &roster.racers[0].red_shell,roster.racers[1].position_x_q16,
              roster.racers[1].position_y_q16,1u,shell_sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(roster.racers[0].red_shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks-1u&&
          roster.racers[0].red_shell.animation_tick_4c==1u);
    {
        const int32_t target_x=roster.racers[1].position_x_q16;
        const int32_t target_y=roster.racers[1].position_y_q16;
        const uint32_t target_flags=roster.racers[1].source_flags;
        roster.racers[1].position_x_q16=(int32_t)(
            (uint32_t)roster.racers[0].red_shell.position_x_q16+
            UINT32_C(0x000a0000));
        roster.racers[1].position_y_q16=
            roster.racers[0].red_shell.position_y_q16;
        roster.racers[1].source_flags=0u;
        CHECK(portable_gp_red_shell_tick_forward_target_close_clear(
                  &roster.racers[0].red_shell,roster.racers[1].position_x_q16,
                  roster.racers[1].position_y_q16,roster.racers[1].world_height_q16,
                  roster.racers[1].source_flags,1u,shell_sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
        CHECK(roster.racers[0].red_shell.angle_26==UINT16_C(0x4000)&&
              roster.racers[0].red_shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u+2u*test_red_shell_rules()->acceleration&&
              roster.racers[0].red_shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks-2u);
        {
            const PortableGpRedShell flight=roster.racers[0].red_shell;
            const uint32_t owner_flags=roster.racers[0].source_flags;
            const PortableGpRacer victim_before=roster.racers[1];
            PortableGpRedShellAudioEvent event={0};
            roster.racers[1].position_x_q16=(int32_t)(
                (uint32_t)flight.position_x_q16+UINT32_C(0x000a0000));
            roster.racers[1].position_y_q16=flight.position_y_q16;
            CHECK(portable_gp_race_roster_resolve_red_shell_kart_hit(
                      &roster,0u,1u,0u,0u,shell_sine,1024u)==
                  PORTABLE_GP_ROSTER_OK);
            CHECK(roster.racers[0].red_shell.collision_result==
                      PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING&&
                  (roster.racers[0].source_flags&UINT32_C(0x10000))==0u&&
                  roster.racers[1].mushroom_response_flags_b4==
                      UINT16_C(0x0200)&&
                  roster.racers[1].hit_response_b6==UINT16_C(0x2000)&&
                  roster.racers[1].hit_state_flags_74==3u&&
                  roster.racers[1].component_x==(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+2u*test_red_shell_rules()->acceleration)*16&&
                  roster.racers[1].component_y==-(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+2u*test_red_shell_rules()->acceleration)*8&&
                  roster.racers[1].red_shell_hit_audio_pending&&
                  roster.red_shell_hits==1u);
            CHECK(portable_gp_race_roster_take_red_shell_hit_audio(
                      &roster,1u,0u,0x40u,&event)==PORTABLE_GP_ROSTER_OK);
            CHECK(event.song_id==132u&&event.player==4u&&event.volume==0x40u&&
                  !roster.racers[1].red_shell_hit_audio_pending);
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(
                      &roster.racers[1],shell_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(roster.racers[1].hit_state_7e==13u &&
                  roster.racers[1].mushroom_mode_84==10u &&
                  roster.racers[1].banana_hit_timer_38==7u);
            roster.racers[0].red_shell=flight;
            roster.racers[0].source_flags=owner_flags;
            roster.racers[1]=victim_before;
            roster.red_shell_hits=0u;
        }
        for(uint8_t floor=1u;floor<=test_red_shell_rules()->route_budget;++floor)
            CHECK(portable_gp_red_shell_tick_target_route_floor(
                      &roster.racers[0].red_shell,floor)==PORTABLE_GP_RED_SHELL_OK);
        CHECK(roster.racers[0].red_shell.phase==
                  PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE&&
              roster.racers[0].red_shell.collision_active==0u);
        /* z(n)=n*v-n*(n-1)*g/2; disposal occurs at first negative z. */
        unsigned destruction_ticks=1u;
        while((int64_t)destruction_ticks*test_red_shell_rules()->destruction_velocity_q16-
            (int64_t)destruction_ticks*(destruction_ticks-1u)*test_red_shell_rules()->destruction_gravity_q16/2>=0)
            ++destruction_ticks;
        for(unsigned tick=0u;tick<destruction_ticks;++tick)
            CHECK(portable_gp_race_roster_tick_red_shell_destroying(
                      &roster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(roster.racers[0].red_shell.phase==
                  PORTABLE_GP_RED_SHELL_DESTROYED&&
              !roster.racers[0].red_shell.occupied);
        roster.racers[1].position_x_q16=target_x;
        roster.racers[1].position_y_q16=target_y;
        roster.racers[1].source_flags=target_flags;
    }
    roster.racers[0].coin_count_178 = 99u;
    CHECK(portable_gp_race_roster_collect_coin(&roster,0u) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.racers[0].coin_count_178 == 99u && roster.coin_events == 1u);
    roster.racers[0].coin_count_178 = 5u;
    roster.racers[1].contact_mode_6e = 0u;
    roster.racers[1].contact_phase_6f = 0u;
    roster.contact_events = 0u;
    roster.coin_events = 0u;
    memset(&roster.racers[7].item_inventory,0,
           sizeof roster.racers[7].item_inventory);
    memset(&roster.racers[7].green_shell,0,
           sizeof roster.racers[7].green_shell);
    memset(&roster.racers[0].banana,0,sizeof roster.racers[0].banana);
    memset(&roster.racers[0].red_shell,0,sizeof roster.racers[0].red_shell);
    roster.racers[7].control &= UINT16_C(0xff7f);
    roster.racers[0].control = 0u;
    roster.racers[7].shell_item_18c = 0u;
    roster.racers[7].item_use_state_17c = 0u;
    roster.racers[0].shell_item_18c = 0u;
    roster.racers[0].item_use_state_17c = 0u;
    roster.racers[0].source_flags &= UINT32_C(0xfffeffff);
    roster.item_box_events = 0u;
    roster.item_settlements = 0u;
    roster.item_uses = 0u;
    roster.green_shell_hits = 0u;
    roster.banana_hits = 0u;
    initial_identity = portable_gp_race_roster_identity(&roster);
    before = roster;
    CHECK(portable_gp_race_roster_tick_stationary(&roster) ==
          PORTABLE_GP_ROSTER_OK);
    CHECK(roster.authoritative_ticks == 1u && roster.update_serial == 8u);
    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        CHECK(roster.racers[index].last_update_serial == index + 1u);
        CHECK(roster.racers[index].position_x_q16 ==
              before.racers[index].position_x_q16);
        CHECK(roster.racers[index].position_y_q16 ==
              before.racers[index].position_y_q16);
    }
    CHECK(portable_gp_race_roster_identity(&roster) != initial_identity);
    all = before;
    /* Control the row-selector input independently of stock pacing tables:
     * input lock selects row3 for all CPUs throughout these eight ticks.
     * One complete selector cycle must update every CPU exactly once. */
    for(unsigned i=1u;i<PORTABLE_GP_RACER_COUNT;++i){
        all.racers[i].lightning_status_6a=0;
        all.racers[i].item_lock_ac=100u;
    }
    all_repeat = all;
    for (size_t tick = 0u; tick < 8u; ++tick)
    {
        CHECK(portable_gp_race_roster_tick_initial_all(
                  &all, &rules, &raster, zero_sine, 1024u) ==
              PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_tick_initial_all(
                  &all_repeat, &rules, &raster, zero_sine, 1024u) ==
              PORTABLE_GP_ROSTER_OK);
    }
    CHECK(all.authoritative_ticks == 8u && all.update_serial == 64u &&
          all.rank_cursor == 4u);
    CHECK(memcmp(&all, &all_repeat, sizeof all) == 0);
    {
        /* sub_803B7B8's first completed Peach lap: 0x80 -> 0x81
         * updates +0xc6 and sets driver +0x48 bit 4 and +0xa4 bit 8. */
        PortableGpRaceRoster lap = before;
        PortableRaceRulesView lap_rules = rules;
        lap_rules.lap_count = 3u;
        lap.racers[0].route_index = 46u;
        lap.racers[0].source_flags = UINT32_C(0x8000);
        portable_gp_race_roster_apply_route_wrap(
            &lap.racers[0], &lap_rules, UINT8_C(128));
        CHECK(lap.racers[0].route_wrap_91 == UINT8_C(128) &&
              lap.racers[0].max_progress_c6 == UINT16_C(0x8000) &&
              lap.racers[0].source_flags == UINT32_C(0x8000) &&
              lap.racers[0].lap_marker_a4 == 0u);
        portable_gp_race_roster_apply_route_wrap(
            &lap.racers[0], &lap_rules, UINT8_C(129));
        CHECK(lap.racers[0].route_wrap_91 == UINT8_C(129) &&
              lap.racers[0].max_progress_c6 == UINT16_C(0x8100) &&
              lap.racers[0].source_flags == UINT32_C(0x8004) &&
              lap.racers[0].lap_marker_a4 == UINT16_C(0x0100));
        portable_gp_race_roster_apply_route_wrap(
            &lap.racers[0], &lap_rules, UINT8_C(129));
        CHECK(lap.racers[0].max_progress_c6 == UINT16_C(0x8100) &&
              lap.racers[0].source_flags == UINT32_C(0x8004) &&
              lap.racers[0].lap_marker_a4 == UINT16_C(0x0100));
        /* The session consumes that one-frame cue before the next wrap. */
        lap.racers[0].lap_marker_a4 = 0u;
        portable_gp_race_roster_apply_route_wrap(
            &lap.racers[0], &lap_rules, UINT8_C(130));
        CHECK(lap.racers[0].route_wrap_91 == UINT8_C(130) &&
              lap.racers[0].max_progress_c6 == UINT16_C(0x8200) &&
              lap.racers[0].source_flags == UINT32_C(0x800c) &&
              lap.racers[0].lap_marker_a4 == UINT16_C(0x0080));
        /* The first rival reaches +0xc6=0x8300 at source frame 5729;
         * sub_803B7B8 marks it finished before the next row update. */
        lap.racers[1].source_flags = UINT32_C(0x200c);
        lap.racers[1].route_wrap_91 = UINT8_C(130);
        lap.racers[1].max_progress_c6 = UINT16_C(0x8200);
        portable_gp_race_roster_apply_route_wrap(
            &lap.racers[1], &lap_rules, UINT8_C(131));
        CHECK(lap.racers[1].max_progress_c6 == UINT16_C(0x8300) &&
              lap.racers[1].source_flags == UINT32_C(0x203c));
        portable_gp_race_roster_apply_route_wrap(
            &lap.racers[1], &lap_rules, UINT8_C(132));
        CHECK(lap.racers[1].route_wrap_91 == UINT8_C(131) &&
              lap.racers[1].max_progress_c6 == UINT16_C(0x8300));
    }
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        CHECK(all.racers[index].motion_active == 1u);
        CHECK(all.racers[index].rank_row_9a == 3u);
        CHECK(all.racers[index].last_update_serial == 56u + index + 1u);
    }
    {
        PortableGpRaceRoster last_place = before;
        last_place.rank_cursor = 1u;
        last_place.standings[6] = 0u;
        last_place.standings[7] = 1u;
        last_place.racers[0].rank_flags_ba = 12u;
        last_place.racers[1].rank_flags_ba = 14u;
        /* Keep the post-motion standings order stable for the row producer. */
        last_place.racers[0].route_index = 46u;
        last_place.racers[1].route_index = 45u;
        last_place.racers[1].rank_row_9a = UINT8_C(0x82);
        CHECK(portable_gp_race_roster_tick_initial_all(
                  &last_place, &rules, &raster, zero_sine, 1024u) ==
              PORTABLE_GP_ROSTER_OK);
        CHECK(last_place.racers[1].rank_row_9a == 2u);
    }
    for (unsigned effect=0u;effect<2u;++effect) {
        PortableGpRaceRoster slowed=before;
        slowed.rank_cursor=1u;
        if (effect==0u) slowed.racers[1].lightning_timer_68=100u;
        else slowed.racers[1].item_lock_ac=20u;
        CHECK(portable_gp_race_roster_tick_initial_all(
                  &slowed,&rules,&raster,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(slowed.racers[1].rank_row_9a==3u);
    }
    {
        PortableRaceRulesView ai_rules = rules;
        ai_rules.ai_modes = route_ai_modes;
        ai_rules.ai_mode_count = 47u;
        all = before;
        CHECK(portable_gp_race_roster_tick_initial_all(
                  &all, &ai_rules, &raster, zero_sine, 1024u) ==
              PORTABLE_GP_ROSTER_OK);
        CHECK(all.racers[1].route_index == 45u &&
              all.racers[1].acceleration_mode_19b == 3u);
        CHECK(all.racers[5].route_index == 46u &&
              all.racers[5].acceleration_mode_19b == 0u);
        uint8_t mode_rows[3u*47u];
        memset(mode_rows,0,sizeof mode_rows);
        memset(mode_rows+47u,2,47u);
        memset(mode_rows+94u,3,47u);
        ai_rules.ai_modes=mode_rows;
        ai_rules.ai_mode_count=sizeof mode_rows;
        all=before;
        all.ai_mode_row_c64=1u;
        CHECK(portable_gp_race_roster_tick_initial_all(
            &all,&ai_rules,&raster,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(all.racers[1].acceleration_mode_19b==2u);
        CHECK(all.racers[5].acceleration_mode_19b==0u);
        all=before;
        all.ai_mode_row_c64=2u;
        CHECK(portable_gp_race_roster_tick_initial_all(
            &all,&ai_rules,&raster,zero_sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(all.racers[1].acceleration_mode_19b==3u);
        all=before;
        all.ai_mode_row_c64=1u;
        ai_rules.ai_mode_count=47u;
        CHECK(portable_gp_race_roster_tick_initial_all(
            &all,&ai_rules,&raster,zero_sine,1024u)==PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED);
    }
    {
        /* Real roster composition must dispatch9B38 even while a kart
         * contact suppresses8200. The sink timer is not field88 or field38. */
        uint8_t terrain_cells[256u*256u]={0};
        uint8_t materials[256];memset(materials,0x22,sizeof materials);
        const PortableTerrainView terrain={terrain_cells,256,256,256};
        const PortableTrackMaterialView material={.flags=materials,.count=256};
        uint8_t rescue_cells[128u*128u];memset(rescue_cells,45,sizeof rescue_cells);
        PortableTrackRaster rescue_raster=raster;rescue_raster.cells=rescue_cells;
        const uint16_t angles[0x4000]={0};
        PortableGpRaceRoster sink=before;
        sink.racers[1].speed_q16=0x1000000;
        sink.racers[1].component_x=0;sink.racers[1].component_y=0;
        sink.racers[1].kart_contact_state_5a=0x8007;
        sink.racers[1].banana_hit_timer_38=10;
        sink.racers[1].source_flags|=0x4000;
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&sink,&rules,&rescue_raster,
            zero_sine,1024,&terrain,&material,NULL,0,0,angles,0x4000,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
        CHECK(sink.racers[1].lightning_state_78==4&&sink.racers[1].recovery_timer_9c==144&&
            sink.racers[1].banana_hit_timer_38==9&&sink.racers[1].component_x==0&&
            sink.racers[1].component_y==0&&sink.racers[1].acceleration_q16==65536);
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&sink,&rules,&rescue_raster,
            zero_sine,1024,&terrain,&material,NULL,0,0,angles,0x4000,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
        CHECK(sink.racers[1].recovery_timer_9c==143&&sink.racers[1].banana_hit_timer_38==8);
        /* A finite-map miss enters bounds recovery using the retained +90. */
        PortableGpRaceRoster bounds=before;
        bounds.racers[1].position_x_q16=-16*65536;
        bounds.racers[1].component_x=0;bounds.racers[1].component_y=0;
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&bounds,&rules,&rescue_raster,
            zero_sine,1024,&terrain,&material,NULL,0,0,angles,0x4000,NULL,NULL,NULL)==PORTABLE_GP_ROSTER_OK);
        CHECK(bounds.racers[1].lightning_state_78==5&&bounds.racers[1].recovery_material==0x26&&
            (bounds.racers[1].source_flags&3)==1&&bounds.racers[1].route_index==before.racers[1].route_index);
    }
    {
        /* The whole CPU turn owns76 independently of9B38's material/timer.
         * All seven autonomous racers share the prefix; the already-ticked
         * selected racer must not receive a second prefix from this pass. */
        uint8_t ground[256u*256u]={0},materials[256];memset(materials,0x20,sizeof materials);
        const PortableTerrainView terrain={ground,256u,256u,256u};
        const PortableTrackMaterialView material={.flags=materials,.count=256u};
        uint8_t checkpoints[128u*128u];memset(checkpoints,45,sizeof checkpoints);
        PortableTrackRaster fall_raster=raster;fall_raster.cells=checkpoints;
        const uint16_t angles[0x4000]={0};
        PortableGpRaceRoster falling=before;
        falling.racers[0].fall_frames_76=77u;
        for(unsigned i=1u;i<8u;++i){
            PortableGpRacer *r=&falling.racers[i];
            r->component_x=r->component_y=0;r->speed_q16=r->acceleration_q16=0;
            r->lightning_state_78=2u;r->mushroom_mode_84=2u;r->recovery_material=0x20u;
            r->recovery_timer_9c=60u;r->fall_frames_76=(uint16_t)(13u+(i-1u)%4u);
        }
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&falling,&rules,&fall_raster,
            zero_sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,NULL,NULL,NULL)==0);
        CHECK(falling.racers[0].fall_frames_76==77u);
        for(unsigned i=1u;i<8u;++i){
            const PortableGpRacer *r=&falling.racers[i];
            const unsigned start=13u+(i-1u)%4u,expected=start<=14u?start+1u:start;
            CHECK(r->fall_frames_76==expected && r->world_height_q16==-(int32_t)expected*32768 &&
                r->recovery_timer_9c==59u && r->lightning_state_78==2u);
            CHECK(r->hit_spin_82==0); /* Late8298 clears the prefix's82. */
        }
        /* Entry2 writes terminal76 before9B38 changes78 to retrieval6.
         * The changed material must not prevent clearing it next turn. */
        for(unsigned i=1u;i<8u;++i){
            falling.racers[i].fall_frames_76=14u;falling.racers[i].recovery_timer_9c=0u;
        }
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&falling,&rules,&fall_raster,
            zero_sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,NULL,NULL,NULL)==0);
        for(unsigned i=1u;i<8u;++i)
            CHECK(falling.racers[i].fall_frames_76==15u && falling.racers[i].lightning_state_78==6u &&
                portable_driver_hidden(6u,falling.racers[i].world_height_q16,15u,0u));
        memset(materials,0x40,sizeof materials);
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&falling,&rules,&fall_raster,
            zero_sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,NULL,NULL,NULL)==0);
        for(unsigned i=1u;i<8u;++i)CHECK(!falling.racers[i].fall_frames_76);
        /* Normal road with no terrain adapter still clears stale76. */
        PortableGpRaceRoster ordinary=before;
        for(unsigned i=1u;i<8u;++i)ordinary.racers[i].fall_frames_76=15u;
        CHECK(portable_gp_race_roster_tick_initial_all(&ordinary,&rules,&raster,zero_sine,1024u)==0);
        for(unsigned i=1u;i<8u;++i)CHECK(!ordinary.racers[i].fall_frames_76);
        /* Pending-hit C4C0 takes a separate B550 path but the same prefix. */
        PortableGpRaceRoster pending=before;
        pending.racers[1].mushroom_response_flags_b4=0x0200u;
        pending.racers[1].fall_frames_76=15u;
        CHECK(portable_gp_race_roster_tick_initial_all_terrain(&pending,&rules,&fall_raster,
            zero_sine,1024u,&terrain,&material,NULL,0u,0,angles,0x4000u,NULL,NULL,NULL)==0);
        CHECK(!pending.racers[1].fall_frames_76);
    }
    {
        /* B98C BA04/BA64 preserve +90 for both outside-route sentinels.
         * Seven CPUs must keep moving without wrapping a lap, selecting an
         * AI mode from index127, or losing their previous steering target. */
        uint8_t boundary_cells[128u * 128u];
        PortableTrackRaster boundary_raster = raster;
        boundary_raster.cells = boundary_cells;
        PortableRaceRulesView boundary_rules = rules;
        boundary_rules.ai_modes = route_ai_modes;
        boundary_rules.ai_mode_count = 47u;
        for (unsigned high = 0u; high < 2u; ++high)
        {
            memset(boundary_cells, high ? 255 : 127, sizeof boundary_cells);
            PortableGpRaceRoster outside = before;
            for (unsigned racer = 1u; racer < 8u; ++racer)
            {
                outside.racers[racer].source_flags |= UINT32_C(3);
                outside.racers[racer].acceleration_mode_19b = 3u;
            }
            CHECK(portable_gp_race_roster_tick_initial_all(&outside,
                &boundary_rules, &boundary_raster, zero_sine, 1024u) ==
                PORTABLE_GP_ROSTER_OK);
            for (unsigned racer = 1u; racer < 8u; ++racer)
            {
                CHECK(outside.racers[racer].route_index == before.racers[racer].route_index);
                CHECK(outside.racers[racer].route_wrap_91 == before.racers[racer].route_wrap_91);
                CHECK((outside.racers[racer].source_flags & 3u) == 2u);
                CHECK(outside.racers[racer].acceleration_mode_19b == 0u);
            }
            memset(boundary_cells, 45, sizeof boundary_cells);
            CHECK(portable_gp_race_roster_tick_initial_all(&outside,
                &boundary_rules, &boundary_raster, zero_sine, 1024u) ==
                PORTABLE_GP_ROSTER_OK);
            CHECK((outside.racers[1].source_flags & 3u) == 0u);
            CHECK(outside.racers[1].acceleration_mode_19b == 3u);
        }
        /* Malformed ordinary indices still reject the content. */
        memset(boundary_cells, 100, sizeof boundary_cells);
        PortableGpRaceRoster malformed = before;
        CHECK(portable_gp_race_roster_tick_initial_all(&malformed,
            &boundary_rules, &boundary_raster, zero_sine, 1024u) ==
            PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED);
    }
    {
        /* 41F18's rank distance uses the current course's checkpoint
         * count. Across Shy Guy Beach's53->0 wrap, the rank2 rival is
         * one checkpoint behind rank1, not five ahead (Peach's47). */
        PortableGpRaceRoster wrap_rows=before;
        PortableRacePoint beach_route[53]={0};
        uint8_t beach_cells[128u*128u];
        memset(beach_cells,52,sizeof beach_cells);
        const size_t rival7_cell=
            (size_t)(wrap_rows.racers[7].position_y_q16/0x10000/16)*128u+
            (size_t)(wrap_rows.racers[7].position_x_q16/0x10000/16);
        CHECK(rival7_cell<sizeof beach_cells);
        beach_cells[rival7_cell]=0u;
        PortableTrackRaster beach_raster=raster;
        beach_raster.cells=beach_cells;
        PortableRaceRulesView beach_rules=rules;
        beach_rules.route=beach_route;
        beach_rules.route_count=53u;
        wrap_rows.rank_cursor=6u;
        static const uint8_t order[8]={0u,7u,6u,5u,4u,3u,2u,1u};
        for(unsigned rank=0u;rank<8u;++rank){
            wrap_rows.standings[rank]=order[rank];
            wrap_rows.racers[order[rank]].rank_flags_ba=(uint16_t)(rank*2u);
        }
        for(unsigned racer=0u;racer<8u;++racer)
            wrap_rows.racers[racer].route_wrap_91=127u;
        wrap_rows.racers[0].route_wrap_91=128u;
        wrap_rows.racers[0].route_index=0u;
        wrap_rows.racers[7].route_wrap_91=128u;
        CHECK(portable_gp_race_roster_decide_initial(
            &wrap_rows,&beach_rules,&beach_raster)==PORTABLE_GP_ROSTER_OK);
        CHECK(wrap_rows.racers[7].route_index==0u);
        CHECK(wrap_rows.racers[6].route_index==52u);
        CHECK(wrap_rows.racers[6].rank_row_9a==1u);
        /* 41B78's nearby player-ahead branch returns1 immediately.
         * Falling through to generic progress comparison would emit41. */
        wrap_rows.rank_cursor=7u;
        wrap_rows.racers[0].progress_ae=4;
        wrap_rows.racers[7].progress_ae=9;
        CHECK(portable_gp_race_roster_decide_initial(
            &wrap_rows,&beach_rules,&beach_raster)==PORTABLE_GP_ROSTER_OK);
        CHECK(wrap_rows.racers[7].rank_row_9a==1u);
    }
    CHECK(portable_gp_race_roster_tick_initial_all(
              NULL, &rules, &raster, zero_sine, 1024u) ==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(portable_gp_race_roster_tick_initial_all(
              &all, &rules, &raster, NULL, 1024u) ==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    CHECK(portable_gp_race_roster_init(&repeat, 25u, 7u) ==
          PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE);
    CHECK(portable_gp_race_roster_init(&repeat, 24u, 8u) ==
          PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    {
        PortableGpRaceRoster launch={.roster_rules=test_roster_rules(),.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&launch);
        launch.racers[0].route_index=52u;
        launch.racers[0].contact_axis_1e=140;
        launch.racers[0].contact_axis_20=120;
        launch.racers[0].contact_height_24=256u;
        launch.racers[7].route_index=52u;
        launch.racers[7].contact_axis_1e=182;
        launch.racers[7].contact_axis_20=369;
        CHECK(portable_gp_race_roster_resolve_player_contact(&launch,60u,0u)==
              PORTABLE_GP_ROSTER_OK);
        CHECK(launch.contact_events==1u&&
              launch.racers[0].contact_mode_6e==8u&&
              launch.racers[0].contact_phase_6f==2u);
        /* Authored script demonstrates that frame IDs, per-frame timing and
         * reset cooldown are content-owned, independently of the donor art. */
        CHECK(portable_gp_race_roster_tick_contact_animation(&launch.racers[0],0u,test_roster_rules())==0);
        CHECK(launch.racers[0].contact_frame_6c==21u &&
              launch.racers[0].contact_mode_6e==0x18u && launch.racers[0].contact_phase_6f==(test_roster_rules()->contact_script[1][1]<<1u));
        unsigned active_remaining=0u;
        for(unsigned row=1u;row<6u;++row)active_remaining+=test_roster_rules()->contact_script[row][1];
        for(unsigned i=1u;i<active_remaining;++i)
            CHECK(portable_gp_race_roster_tick_contact_animation(&launch.racers[0],0u,test_roster_rules())==0);
        CHECK((launch.racers[0].contact_mode_6e&15u)==8u);
        CHECK(portable_gp_race_roster_tick_contact_animation(&launch.racers[0],0u,test_roster_rules())==0);
        CHECK(launch.racers[0].contact_mode_6e==10u && launch.racers[0].contact_phase_6f==(test_roster_rules()->contact_script[7][1]<<1u));
        launch.racers[0].contact_height_24=900u;
        const unsigned cooldown_ticks=test_roster_rules()->contact_script[7][1]+active_remaining;
        for(unsigned i=1u;i<cooldown_ticks;++i){
            CHECK(portable_gp_race_roster_tick_contact_animation(&launch.racers[0],12u,test_roster_rules())==0);
            CHECK((launch.racers[0].contact_mode_6e&15u)==10u);
        }
        CHECK(portable_gp_race_roster_tick_contact_animation(&launch.racers[0],12u,test_roster_rules())==0);
        CHECK(launch.racers[0].contact_mode_6e==0u && launch.racers[0].contact_phase_6f==0u);
        launch.racers[0].contact_height_24=256u;
        launch.racers[0].contact_mode_6e=0u;
        launch.racers[7].contact_axis_1e=196;
        CHECK(portable_gp_race_roster_resolve_player_contact(&launch,60u,0u)==
              PORTABLE_GP_ROSTER_OK&&launch.racers[0].contact_mode_6e==0u);
        /* Original zero route rejects the nearest rival while its own
         * CA78 view is neither0 nor127, then starts mode9 once aligned. */
        launch.racers[7].contact_axis_1e=130;
        launch.racers[7].contact_height_24=256u;
        launch.racers[7].contact_heading_56=UINT16_C(0xfc00);
        CHECK(portable_gp_race_roster_resolve_player_contact(&launch,60u,0u)==
              PORTABLE_GP_ROSTER_OK&&launch.racers[7].contact_mode_6e==0u);
        launch.racers[7].contact_heading_56=0u;
        CHECK(portable_gp_race_roster_resolve_player_contact(&launch,60u,0u)==
              PORTABLE_GP_ROSTER_OK&&launch.racers[7].contact_mode_6e==9u);
        CHECK(portable_gp_race_roster_tick_contact_animation(
                  &launch.racers[7],0u,test_roster_rules())==PORTABLE_GP_ROSTER_OK);
        CHECK(launch.racers[7].contact_mode_6e==0x19u&&
              launch.racers[7].contact_phase_6f==(test_roster_rules()->contact_script[1][1]<<1u)&&
              launch.racers[7].contact_flags_6d==1u);
        /* AAF0 branches to AC24 before consuming the countdown when
         * current heading leaves the seam. Original zero rival7 +1707
         * retains frame27/flip while entering phase0 cooldown. The authored
         * reset duration below replaces the source asset's count63. */
        launch.racers[7].contact_mode_6e=0x39u;
        launch.racers[7].contact_phase_6f=0x1cu;
        launch.racers[7].contact_frame_6c=27u;
        CHECK(portable_gp_race_roster_tick_contact_animation(
                  &launch.racers[7],1u,test_roster_rules())==PORTABLE_GP_ROSTER_OK);
        CHECK(launch.racers[7].contact_mode_6e==10u&&
              launch.racers[7].contact_phase_6f==(test_roster_rules()->contact_script[7][1]<<1u)&&
              launch.racers[7].contact_frame_6c==27u&&
              launch.racers[7].contact_flags_6d==1u);
        CHECK(portable_gp_race_roster_tick_contact_animation(
                  &launch.racers[7],12u,test_roster_rules())==PORTABLE_GP_ROSTER_OK&&
              launch.racers[7].contact_phase_6f==((test_roster_rules()->contact_script[7][1]-1u)<<1u));
        /* Height is unsigned; 511 remains eligible, 512 cancels.
         * Both seam views0/127 are accepted. These boundary branches
         * are source/unit coverage, not a claim of original route proof. */
        launch.racers[7].contact_mode_6e=0x18u;
        launch.racers[7].contact_phase_6f=8u;
        launch.racers[7].contact_height_24=511u;
        CHECK(portable_gp_race_roster_tick_contact_animation(
                  &launch.racers[7],127u,test_roster_rules())==PORTABLE_GP_ROSTER_OK&&
              launch.racers[7].contact_mode_6e==0x18u&&
              launch.racers[7].contact_phase_6f==(test_roster_rules()->contact_script[1][1]<<1u));
        launch.racers[7].contact_height_24=512u;
        CHECK(portable_gp_race_roster_tick_contact_animation(
                  &launch.racers[7],0u,test_roster_rules())==PORTABLE_GP_ROSTER_OK&&
              launch.racers[7].contact_mode_6e==10u&&
              launch.racers[7].contact_phase_6f==(test_roster_rules()->contact_script[7][1]<<1u)&&
              launch.racers[7].contact_frame_6c==27u&&
              launch.racers[7].contact_flags_6d==1u);
    }
    {
        PortableGpRaceRoster cup_setup;
        static const uint8_t points[8]={9u,0u,0u,0u,0u,1u,3u,6u};
        static const int16_t expected[8]={4,0,0,0,2,5,7,9};
        static const int16_t initial_progress_expected[8]={4,0,0,0,3,5,7,9};
        CHECK(portable_gp_race_roster_init(&cup_setup,24u,7u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_roster_apply_cup_ai_setup(
            &cup_setup,points,1u,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(cup_setup.ai_mode_row_c64==1u);
        for(unsigned i=0u;i<8u;++i)CHECK(cup_setup.racers[i].progress_ae==expected[i]);
        const uint64_t row_one_identity=portable_gp_race_roster_identity(&cup_setup);
        cup_setup.ai_mode_row_c64=0u;
        CHECK(portable_gp_race_roster_identity(&cup_setup)!=row_one_identity);
        static const int gaps[6]={-9,0,2,3,8,9};
        static const uint8_t rows[6]={0u,0u,0u,1u,1u,2u};
        for(unsigned i=0u;i<6u;++i){
            uint8_t boundary_points[8]={20u,20u,0u,0u,0u,0u,0u,0u};
            boundary_points[0]=(uint8_t)(20+gaps[i]);
            CHECK(portable_gp_race_roster_apply_cup_ai_setup(
                &cup_setup,boundary_points,1u,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(cup_setup.ai_mode_row_c64==rows[i]);
            CHECK(portable_gp_race_roster_apply_cup_ai_setup(
                &cup_setup,boundary_points,4u,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(cup_setup.ai_mode_row_c64==0u);
        }
        /* Old setup uses grid order, independently of points. */
        CHECK(portable_gp_race_roster_apply_cup_ai_setup(
            &cup_setup,points,0u,0u)==PORTABLE_GP_ROSTER_OK);
        for(unsigned i=0u;i<8u;++i)CHECK(cup_setup.racers[i].progress_ae==initial_progress_expected[i]);
    }
    {
        /* Decisions observe first-grid AE even after a later-course grid is
         * installed. Clearing AE would erase this behind-rival gap branch. */
        PortableGpRaceRoster initialized,derived,missing;
        CHECK(test_roster_init(&initialized,24u,7u)==0);
        derived=initialized;
        for(unsigned i=0u;i<8u;++i)derived.racers[i].progress_ae=0;
        const uint8_t points[8]={0};
        CHECK(portable_gp_race_roster_apply_cup_ai_setup(&derived,points,0u,0u)==0);
        for(unsigned i=0u;i<8u;++i)
            CHECK(initialized.racers[i].progress_ae==derived.racers[i].progress_ae);
        const uint8_t reordered[8]={7u,6u,5u,3u,2u,1u,4u,0u};
        PortableGpGridRow grid[8];
        for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpGridRow){(int32_t)(80u+i)*65536,80*65536,0u};
        CHECK(portable_gp_race_roster_apply_course_grid(&initialized,grid,reordered)==0);
        CHECK(portable_gp_race_roster_apply_course_grid(&derived,grid,reordered)==0);
        missing=initialized;
        for(unsigned i=0u;i<8u;++i)missing.racers[i].progress_ae=0;
        uint8_t cells[128u*128u]={0};
        const PortableTrackRaster own_raster={cells,sizeof cells,128u,128u,0u,0u};
        CHECK(portable_gp_race_roster_decide_initial(&initialized,&rules,&own_raster)==0);
        CHECK(portable_gp_race_roster_decide_initial(&derived,&rules,&own_raster)==0);
        CHECK(portable_gp_race_roster_decide_initial(&missing,&rules,&own_raster)==0);
        CHECK(initialized.racers[3].rank_row_9a==0x82u &&
              derived.racers[3].rank_row_9a==initialized.racers[3].rank_row_9a &&
              missing.racers[3].rank_row_9a!=initialized.racers[3].rank_row_9a);
    }
    /* Star owns a separate protection clock; activation does not erase
     * Lightning shrink or borrow the mushroom response clock. */
    for(unsigned preset=0u;preset<2u;++preset) {
        const PortableItemStatusRules custom={90u,60u,120u};
        PortableGpRaceRoster star = {.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&star);
        star.item_status_rules=preset?&custom:test_item_status_rules();
        for (unsigned owner=0u; owner<2u; ++owner) {
            PortableGpRacer *r = &star.racers[owner];
            r->source_flags = owner==0u ? UINT32_C(0x8000) : UINT32_C(0x2000);
            r->is_player = (uint8_t)(owner==0u);
            r->item_inventory.held_item_17a = UINT16_C(0x100a);
            if (owner==0u) {
                CHECK(portable_gp_race_roster_use_star(&star,owner)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
                r->control = UINT16_C(0x0080);
            }
            r->effect_state_72 = UINT16_MAX;
            r->hit_state_flags_74 = UINT16_MAX;
            r->contact_status_66 = 50;
            r->item_lock_ac = 20u;
            r->lightning_timer_68 = 100u;
            r->mushroom_timer_cc = 17u;
            CHECK(portable_gp_race_roster_use_star(&star,owner)==PORTABLE_GP_ROSTER_OK);
            CHECK(r->lightning_status_6a==(owner==0u?(preset?90:(int)test_item_status_rules()->star_player_ticks):(preset?60:(int)test_item_status_rules()->star_cpu_ticks)));
            CHECK(r->hit_cue_flags_58==UINT16_C(0x8000));
            CHECK(r->contact_status_66==0 && r->effect_state_72==0u &&
                  r->hit_state_flags_74==UINT16_MAX && r->item_lock_ac==0u);
            CHECK(r->item_inventory.held_item_17a==0u);
            CHECK(r->item_response_flags_b2==(owner==0u?UINT16_C(0x2000):0u));
            CHECK(r->lightning_timer_68==100u && r->mushroom_timer_cc==17u);
            const unsigned duration=owner==0u?(preset?90u:test_item_status_rules()->star_player_ticks):(preset?60u:test_item_status_rules()->star_cpu_ticks);
            r->mushroom_response_flags_b4=UINT16_MAX;
            r->mushroom_mode_84=8u;
            r->lightning_state_78=1u;
            for (unsigned tick=1u;tick<duration;++tick) {
                CHECK(portable_gp_race_roster_tick_protection(&star,owner)==PORTABLE_GP_ROSTER_OK);
                CHECK(r->lightning_status_6a==(int16_t)(duration-tick));
                CHECK((r->hit_cue_flags_58&UINT16_C(0x8000))!=0u);
            }
            CHECK(portable_gp_race_roster_tick_protection(&star,owner)==PORTABLE_GP_ROSTER_OK);
            CHECK(r->lightning_status_6a==0 && r->hit_cue_flags_58==0u);
            CHECK(r->mushroom_response_flags_b4==UINT16_C(0x4000) && r->mushroom_mode_84==1u);
            CHECK(r->lightning_timer_68==100u && r->mushroom_timer_cc==17u);
        }
        star.racers[0].lightning_status_6a=INT16_MIN;
        CHECK(portable_gp_race_roster_tick_protection(&star,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(star.racers[0].lightning_status_6a==INT16_MAX);
        CHECK(portable_gp_race_roster_use_star(NULL,0u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
        CHECK(portable_gp_race_roster_tick_protection(&star,8u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    }
    /* CPU 42AE4 schedules by actor index and engine, then42A28 uses rank
     * neighbors. Consumption queues the ground request instead of input. */
    {
        PortableGpRaceRoster cpu={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules(),.cpu_items_enabled=1u};test_item_roster_profiles(&cpu);
        for (unsigned i=0u;i<8u;++i) {
            cpu.standings[i]=(uint8_t)i;
            cpu.racers[i].source_flags=i==0u?UINT32_C(0x8000):UINT32_C(0x2000);
            cpu.racers[i].is_player=(uint8_t)(i==0u);
            cpu.racers[i].rank_flags_ba=(uint16_t)(i*2u);
            cpu.racers[i].route_wrap_91=1u;
            cpu.racers[i].route_index=2u;
            cpu.racers[i].item_inventory.held_item_17a=UINT16_C(0x1007);
        }
        for (unsigned engine=0u;engine<3u;++engine) {
            PortableGpRaceRoster scheduled=cpu;
            scheduled.cpu_item_engine=(uint8_t)engine;
            scheduled.cpu_item_clock=(256u>>engine)-1u;
            CHECK(portable_gp_race_roster_use_cpu_item(&scheduled,1u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(scheduled.item_uses==0u);
            ++scheduled.cpu_item_clock;
            CHECK(portable_gp_race_roster_use_cpu_item(&scheduled,1u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(scheduled.cpu_mushroom_uses==1u && scheduled.item_uses==1u &&
                  scheduled.racers[1].item_inventory.held_item_17a==0u &&
                  scheduled.racers[1].item_response_flags_b2==UINT16_C(0x8000) &&
                  scheduled.racers[1].control==0u && scheduled.racers[1].mushroom_mode_84==0u);
            CHECK(scheduled.cpu_item_cooldown_c62==(engine==0u?510u:engine==1u?270u:150u));
            PortableGpRaceRoster star=cpu;
            star.cpu_item_engine=(uint8_t)engine;
            star.cpu_item_clock=256u>>engine;
            star.racers[1].item_inventory.held_item_17a=UINT16_C(0x100a);
            star.racers[1].item_use_state_17c=1u; /* local-only gate */
            CHECK(portable_gp_race_roster_use_cpu_item(&star,1u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(star.cpu_star_uses==1u && star.item_uses==1u &&
                  star.racers[1].lightning_status_6a==test_item_status_rules()->star_cpu_ticks && star.racers[1].control==0u &&
                  star.racers[1].item_inventory.held_item_17a==0u &&
                  star.cpu_item_cooldown_c62==scheduled.cpu_item_cooldown_c62);
            scheduled.cpu_item_clock=2u*(256u>>engine);
            CHECK(portable_gp_race_roster_use_cpu_item(&scheduled,2u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(scheduled.item_uses==1u);
        }
        for (unsigned guard=0u;guard<5u;++guard) {
            PortableGpRaceRoster held=cpu;
            held.cpu_item_clock=256u;
            if (guard==0u) held.racers[1].route_wrap_91=0u;
            if (guard==1u) {held.racers[1].route_wrap_91=2u;held.racers[1].route_index=5u;}
            if (guard==2u) held.racers[1].source_flags|=UINT32_C(0x800);
            if (guard==3u) held.racers[1].source_flags|=UINT32_C(0x20);
            if (guard==4u) held.race_mode_flags_7da=1u;
            CHECK(portable_gp_race_roster_use_cpu_item(&held,1u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(held.item_uses==0u && held.racers[1].item_inventory.held_item_17a==UINT16_C(0x1007));
        }
        for (unsigned engine=0u;engine<3u;++engine) {
            PortableGpRaceRoster hit=cpu;
            hit.cpu_item_engine=(uint8_t)engine;
            hit.cpu_item_clock=256u>>engine;
            hit.racers[1].item_inventory.held_item_17a=UINT16_C(0x1001);
            CHECK(portable_gp_race_roster_use_cpu_item(&hit,1u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
            CHECK(hit.lightning_events==1u && hit.racers[0].lightning_timer_68==test_item_status_rules()->lightning_ticks &&
                  hit.racers[1].control==0u && hit.lightning_audio_pending &&
                  hit.cpu_item_cooldown_c62==(engine==0u?510u:engine==1u?270u:150u));
        }
        {
            PortableGpRaceRoster hit=cpu;
            hit.cpu_item_clock=2u*256u;
            hit.racers[2].item_inventory.held_item_17a=UINT16_C(0x100c);
            hit.racers[2].progress_ae=2;
            CHECK(portable_gp_race_roster_use_cpu_item(&hit,2u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK &&
                  hit.lightning_events==0u); /* lightning omits42A28's progress-difference shortcut */
            hit.racers[2].progress_ae=3;
            CHECK(portable_gp_race_roster_use_cpu_item(&hit,2u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK &&
                  hit.lightning_events==1u);
        }
        cpu.cpu_item_clock=2u*256u;
        CHECK(portable_gp_race_roster_use_cpu_item(&cpu,2u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(cpu.item_uses==0u); /* no nearby local neighbor and equal progress */
        cpu.racers[2].progress_ae=2;
        CHECK(portable_gp_race_roster_use_cpu_item(&cpu,2u,10u,3u,NULL,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(cpu.item_uses==1u);
        int32_t boost_sine[1024]={0};
        uint8_t boost_cells[4]={0};
        const PortableTrackRaster boost_raster={.cells=boost_cells,.width=2u,.height=2u,.capacity=4u};
        const PortableRaceRulesView boost_rules={0};
        PortableGpRacer *boost=&cpu.racers[2];
        boost->source_material_85=UINT8_C(0x40);
        boost->lightning_state_78=1u;boost->world_height_q16=1;
        boost->vertical_velocity_q8=-1;
        CHECK(portable_gp_race_roster_terrain_tick(boost,&boost_rules,&boost_raster,NULL,
            boost_sine,1024u,0,NULL,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boost->lightning_state_78==0u && boost->item_response_flags_b2==UINT16_C(0x8000));
        CHECK(portable_gp_race_roster_terrain_tick(boost,&boost_rules,&boost_raster,NULL,
            boost_sine,1024u,0,NULL,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(boost->mushroom_mode_84==8u && boost->mushroom_timer_cc==test_mushroom_rules()->boost_ticks &&
              boost->hit_state_7e==14u && boost->item_response_flags_b2==0u);
    }
    /* E92C's protected driver damages only the unprotected rival, even
     * when the hitter is local or the victim is in ordinary mode (58 bit40). */
    {
        int32_t hit_sine[1024]={0};
        hit_sine[0]=0; hit_sine[256]=32768;
        PortableGpRaceRoster contacts={.roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules()};test_item_roster_profiles(&contacts);
        for (unsigned owner=0u;owner<8u;++owner) {
            contacts.standings[owner]=(uint8_t)owner;
            contacts.racers[owner].source_flags=owner==0u?UINT32_C(0x8000):UINT32_C(0x2000);
            contacts.racers[owner].is_player=(uint8_t)(owner==0u);
            contacts.racers[owner].rank_flags_ba=(uint16_t)(owner*2u);
            contacts.racers[owner].hit_cue_flags_58=UINT16_C(0x0040);
            contacts.racers[owner].position_x_q16=(int32_t)owner*65536;
        }
        for (unsigned hitter=0u;hitter<2u;++hitter) {
            PortableGpRaceRoster hit=contacts;
            hit.racers[hitter].hit_cue_flags_58|=UINT16_C(0x8000);
            hit.racers[hitter].lightning_status_6a=100;
            const PortableGpRacer before_hitter=hit.racers[hitter];
            CHECK(portable_gp_race_roster_resolve_protected_kart_contact(
                &hit,hitter,2u,1u,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(memcmp(&before_hitter,&hit.racers[hitter],sizeof before_hitter)==0);
            CHECK(hit.contact_events==1u && hit.racers[2].banana_hit_timer_38==8u);
            CHECK(hit.racers[2].kart_contact_state_5a==UINT16_C(0x8007));
            CHECK(hit.racers[2].hit_response_b6==UINT16_C(0x1000));
            CHECK(hit.racers[2].mushroom_response_flags_b4==UINT16_C(0x0300));
            CHECK(hit.racers[2].hit_state_flags_74==3u && hit.racers[2].source_flags==UINT32_C(0x3000));
            if(hitter==0u){
                /* An airborne banana's owner lock survives an unrelated
                 * lightning/strong response; motion must not reject it. */
                PortableGpRacer locked=hit.racers[2];locked.source_flags|=0x10000u;
                CHECK(portable_gp_race_roster_cpu_pending_hit_motion(&locked,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
                CHECK((locked.source_flags&0x10000u)!=0u && locked.mushroom_mode_84==10u);
            }
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(&hit.racers[2],hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(hit.racers[2].mushroom_mode_84==10u && hit.racers[2].hit_response_b6==UINT16_C(0x0fc0));
            for (unsigned tick=0u;tick<200u && hit.racers[2].mushroom_mode_84!=0u;++tick)
                CHECK(portable_gp_race_roster_cpu_pending_hit_motion(&hit.racers[2],hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(hit.racers[2].mushroom_mode_84==0u && hit.racers[2].hit_spin_82==0 &&
                  hit.racers[2].hit_state_7e==0u && (hit.racers[2].mushroom_response_flags_b4&0x0200u)==0u);
        }
        /* A58C keeps the pending impulse while airborne and on the landing
         * tick. Ground-entry 9B38 consumes it on the following tick. */
        {
            PortableGpRaceRoster air=contacts;
            air.racers[0].hit_cue_flags_58|=UINT16_C(0x8000);
            PortableGpRacer *victim=&air.racers[2];
            victim->source_flags|=UINT32_C(0x800);
            victim->lightning_state_78=1u;
            victim->mushroom_mode_84=1u;
            victim->world_height_q16=INT32_C(0x10000);
            victim->vertical_velocity_q8=-50;
            victim->mushroom_response_flags_b4=UINT16_C(0x8000);
            CHECK(portable_gp_race_roster_resolve_protected_kart_contact(&air,0u,2u,1u,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(victim,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(victim->world_height_q16==INT32_C(0xb400) && victim->vertical_velocity_q8==-76 &&
                  victim->lightning_state_78==1u && victim->mushroom_mode_84==1u &&
                  (victim->source_flags&UINT32_C(0x1000))!=0u && victim->hit_state_7e==0u);
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(victim,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(victim->world_height_q16==INT32_C(0x4e00));
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(victim,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(victim->world_height_q16==0 && victim->lightning_state_78==0u &&
                  victim->terrain_jump_landed && victim->mushroom_mode_84==0u &&
                  (victim->source_flags&UINT32_C(0x1000))!=0u &&
                  (victim->mushroom_response_flags_b4&UINT16_C(0x8000))==0u);
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(victim,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(victim->mushroom_mode_84==10u && victim->hit_state_7e==13u &&
                  (victim->source_flags&UINT32_C(0x1800))==0u);
        }
        /* ROM BA98 settles on a signed halfword crossing, including values
         * outside the local driver's narrower 3956C dead zone. */
        for (unsigned side=0u;side<2u;++side) {
            PortableGpRacer settling=contacts.racers[2];
            settling.mushroom_mode_84=10u;
            settling.hit_spin_82=side==0u?400:-400;
            settling.mushroom_response_flags_b4=UINT16_C(0x0200);
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(&settling,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(settling.mushroom_mode_84==0u && settling.hit_spin_82==0 &&
                  settling.mushroom_response_flags_b4==UINT16_C(0x400));
        }
        {
            PortableGpRacer wrapping=contacts.racers[2];
            wrapping.mushroom_mode_84=10u;
            wrapping.hit_spin_82=32700;
            wrapping.hit_response_b6=100u;
            wrapping.mushroom_response_flags_b4=UINT16_C(0x0300);
            CHECK(portable_gp_race_roster_cpu_pending_hit_motion(&wrapping,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(wrapping.hit_spin_82==-31584 && wrapping.mushroom_mode_84==10u);
        }
        for (unsigned guard=1u;guard<6u;++guard) {
            PortableGpRaceRoster hit=contacts;
            hit.racers[1].hit_cue_flags_58|=UINT16_C(0x8000);
            if (guard==1u) hit.racers[2].contact_status_66=1;
            if (guard==2u) hit.racers[2].world_height_q16=INT32_C(0x50000);
            if (guard==3u) hit.racers[2].banana_hit_timer_38=2u;
            if (guard==4u) hit.racers[1].contact_status_60=1u;
            if (guard==5u) hit.racers[1].lightning_state_78=2u;
            const PortableGpRaceRoster before_hit=hit;
            CHECK(portable_gp_race_roster_resolve_protected_kart_contact(&hit,1u,2u,0u,hit_sine,1024u)==
                  PORTABLE_GP_ROSTER_OK);
            CHECK(memcmp(&hit,&before_hit,sizeof hit)==0);
        }
        /* F0E8 uses the elastic push for two stars. Equal cue words use
         * F32C's sequential signed midpoints, including negative odd halves. */
        for (unsigned local=0u;local<2u;++local) {
            for (unsigned odd=0u;odd<2u;++odd) {
                PortableGpRaceRoster push=contacts;
                const unsigned first=local?0u:1u;
                PortableGpRacer *a=&push.racers[first],*b=&push.racers[2];
                a->hit_cue_flags_58=b->hit_cue_flags_58=UINT16_C(0x8040);
                a->lightning_status_6a=b->lightning_status_6a=100;
                a->contact_center_x=0; b->contact_center_x=0;
                a->contact_center_y=0; b->contact_center_y=1;
                a->heading=b->heading=UINT16_C(0x4000);
                a->component_y=odd?-1:0; b->component_y=0;
                CHECK(portable_gp_race_roster_resolve_protected_kart_contact(
                    &push,first,2u,0u,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
                CHECK(push.contact_events==1u && a->banana_hit_timer_38==8u &&
                      b->banana_hit_timer_38==8u && a->kart_contact_state_5a==UINT16_C(0x8007));
                CHECK(a->lightning_status_6a==100 && b->lightning_status_6a==100 &&
                      a->hit_response_b6==0u && b->hit_response_b6==0u);
                CHECK(a->component_y==-256-(int)odd && b->component_y==128-(int)odd &&
                      a->component_x==0 && b->component_x==0);
                const PortableGpRaceRoster before_push=push;
                CHECK(portable_gp_race_roster_resolve_protected_kart_contact(
                    &push,first,2u,0u,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
                CHECK(memcmp(&push,&before_push,sizeof push)==0);
            }
        }
        for (unsigned finished=0u;finished<2u;++finished) {
            PortableGpRaceRoster push=contacts;
            push.racers[0].hit_cue_flags_58=push.racers[2].hit_cue_flags_58=UINT16_C(0x8040);
            push.racers[0].coin_count_178=push.racers[2].coin_count_178=5u;
            push.racers[0].coin_loss_event_count=push.racers[2].coin_loss_event_count=0u;
            if (finished) push.racers[0].source_flags|=UINT32_C(0x20);
            CHECK(portable_gp_race_roster_resolve_protected_kart_contact(
                &push,0u,2u,0u,hit_sine,1024u)==PORTABLE_GP_ROSTER_OK);
            CHECK(push.racers[0].coin_count_178==(finished?5u:4u) &&
                  push.racers[2].coin_count_178==5u);
            CHECK(push.racers[0].coin_loss_event_count==(finished?0u:1u));
            if(!finished)CHECK(push.racers[0].coin_loss_events[0].cause==3u &&
                push.racers[0].coin_loss_events[0].loss==1u && push.racers[0].coin_loss_events[0].remaining==4u);
            CHECK(!push.racers[2].coin_loss_event_count);
        }
        CHECK(portable_gp_race_roster_resolve_protected_kart_contact(&contacts,1u,2u,0u,hit_sine,1024u)==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE);
    }
    {
        /* D00C runs for the selected local, while AA6C gates the recipient
         * independently. Projected +1e is ground Y; +20 is body X. */
        PortableGpRaceRoster contacts={.roster_rules=test_roster_rules(),
            .roulette_rules=test_roulette_profile(),.hazard_sound_rules=test_hazard_rules(),
            .racer_count=3u};test_item_roster_profiles(&contacts);
        uint8_t views[PORTABLE_GP_RACER_COUNT]={0};
        PortableGpRacer *local=&contacts.racers[2];
        PortableGpRacer *other=&contacts.racers[0];
        local->route_index=other->route_index=5u;
        local->contact_axis_1e=140;local->contact_axis_20=120;
        local->contact_height_24=256u;local->contact_flags_6d=3u;
        other->contact_axis_1e=152;other->contact_axis_20=150;
        other->contact_height_24=256u;
        views[2]=5u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(!contacts.contact_events && !local->contact_mode_6e);
        views[2]=127u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(contacts.contact_events==1u && local->contact_mode_6e==8u &&
              local->contact_phase_6f==2u && local->contact_flags_6d==1u);
        local->contact_mode_6e=local->contact_phase_6f=0;
        other->contact_axis_1e=130;views[0]=3u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(contacts.contact_events==1u && !other->contact_mode_6e);
        views[0]=0u;other->contact_height_24=512u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(contacts.contact_events==1u && !other->contact_mode_6e);
        other->contact_height_24=511u;other->elimination.revenge_bomb=1u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(!other->contact_mode_6e);
        other->elimination.revenge_bomb=0u;local->elimination.revenge_bomb=1u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(!other->contact_mode_6e);
        local->elimination.revenge_bomb=0u;
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK);
        CHECK(contacts.contact_events==2u && other->contact_mode_6e==9u &&
              other->contact_flags_6d==1u);
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,views)==PORTABLE_GP_ROSTER_OK &&
              contacts.contact_events==2u);
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,3u,10u,views)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
        CHECK(portable_gp_race_roster_resolve_view_contacts(&contacts,2u,10u,NULL)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
    }
    puts("GP race roster: PASS drivers=8 source decisions=7 rank rows=7 moving=7 grid=Peach isolated");
    return 0;
}
