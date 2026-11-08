#include "script_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"script host line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

static PortableScriptHost *create(const char *const *sources,size_t count)
{
    static const char *const ids[]={"first","second","third"};
    PortableScriptSource scripts[3];
    CHECK(count<=3u);
    for(size_t i=0u;i<count;++i)scripts[i]=(PortableScriptSource){ids[i],sources[i],strlen(sources[i])};
    PortableScriptHost *host=portable_script_host_create(scripts,count);
    CHECK(host!=NULL);
    return host;
}
static PortableGpRaceSession *grid(uint32_t rng)
{
    PortableGpRaceSession *session=calloc(1u,sizeof *session);
    CHECK(session!=NULL);
    session->runtime.session.phase=PORTABLE_RACE_PHASE_COUNTDOWN;
    session->rules.lap_count=3u;
    session->source_rng_state=rng;
    for(unsigned i=0u;i<PORTABLE_GP_RACER_COUNT;++i)session->roster.racers[i].rank_flags_ba=(uint16_t)(i<<1u);
    return session;
}
static void tick(PortableGpRaceSession *session)
{
    CHECK(session->script_hooks!=NULL);
    session->script_hooks->after_tick(session->script_hooks->context);
}
static const char *message(const PortableScriptHost *host)
{
    const char *text=portable_script_host_message(host);
    return text!=NULL?text:"";
}
/* Starts a race, runs one countdown tick and one racing tick. */
static void start(PortableScriptHost *host,PortableGpRaceSession *session,uint32_t course)
{
    CHECK(portable_script_host_begin_race(host,session,PORTABLE_SCRIPT_MODE_GP,course,0u));
    tick(session);
    session->runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    tick(session);
}

static void test_sandbox(void)
{
    const char *const sandbox[]={
        "function on_race_start(race)\n"
        "  local closed = io == nil and os == nil and require == nil and load == nil and dofile == nil\n"
        "    and loadfile == nil and debug == nil and package == nil and coroutine == nil and utf8 == nil\n"
        "    and collectgarbage == nil and math.random == nil and math.randomseed == nil and string.dump == nil\n"
        "  local open = type(string.format) == 'function' and type(table.sort) == 'function'\n"
        "    and type(math.floor) == 'function' and type(pcall) == 'function' and type(setmetatable) == 'function'\n"
        "  race.message(closed and open and 'sandbox ok' or 'sandbox open', 10)\n"
        "end\n"};
    PortableScriptHost *host=create(sandbox,1u);
    PortableGpRaceSession *session=grid(7u);
    start(host,session,24u);
    CHECK(strcmp(message(host),"SANDBOX OK")==0);
    portable_script_host_end_race(host);portable_script_host_free(host);

    /* Compile/load failures disable a script for the session; bytecode is
     * refused, and the race API does not exist outside hooks. */
    const char *const broken[]={"function (", "\x1bLua\x54\x00", "race.random(3)"};
    for(size_t i=0u;i<3u;++i){
        host=create(&broken[i],1u);
        CHECK(portable_script_host_active_count(host)==0u);
        CHECK(strcmp(portable_script_host_failed_id(host),"first")==0);
        CHECK(portable_script_host_begin_race(host,session,PORTABLE_SCRIPT_MODE_GP,24u,0u));
        CHECK(session->script_hooks==NULL);
        portable_script_host_free(host);
    }
    const char *const mixed[]={"local x = 1", "function (", "function ("};
    host=create(mixed,3u);
    CHECK(portable_script_host_active_count(host)==1u);
    CHECK(strcmp(portable_script_host_failed_id(host),"second")==0);
    portable_script_host_free(host);
    CHECK(portable_script_host_failed_id(NULL)==NULL);
    /* A script without hooks never attaches: the race is untouched. */
    const char *const quiet[]={"local x = 1"};
    host=create(quiet,1u);
    CHECK(portable_script_host_active_count(host)==1u);
    CHECK(portable_script_host_failed_id(host)==NULL);
    CHECK(portable_script_host_begin_race(host,session,PORTABLE_SCRIPT_MODE_GP,24u,0u));
    CHECK(session->script_hooks!=NULL);
    portable_script_host_free(host);
    free(session);
}

static void test_limits(void)
{
    const char *const runaway[]={
        "function on_tick(race) while true do end end",
        "function on_tick(race) while true do pcall(function() while true do end end) end end",
        "function on_tick(race) local s = string.rep('x', 8 * 1024 * 1024) end",
    };
    for(size_t i=0u;i<3u;++i){
        const char *const sources[]={runaway[i],"function on_tick(race) race.message('still here', 5) end"};
        PortableScriptHost *host=create(sources,2u);
        PortableGpRaceSession *session=grid(9u);
        start(host,session,24u);
        /* The offender is off for this race; later mods keep running. */
        CHECK(portable_script_host_active_count(host)==2u&&portable_script_host_live_count(host)==1u);
        CHECK(strcmp(message(host),"STILL HERE")==0);
        portable_script_host_free(host);free(session);
    }
}

static void test_order(void)
{
    const char *const sources[]={
        "local log = {}\n"
        "local function add(text) log[#log + 1] = text end\n"
        "function on_race_start(race) add('S' .. race.tick) end\n"
        "function on_lap(kart, lap) add('L' .. kart.index .. '-' .. lap) end\n"
        "function on_finish(kart, place) add('F' .. kart.index .. '-' .. place) end\n"
        "function on_race_end(race) add('E') race.message(table.concat(log, ' '), 50) end\n",
        "function on_race_end(race) race.message(race.message and 'overwritten' or '', 50) end\n"};
    PortableScriptHost *host=create(sources,1u);
    PortableGpRaceSession *session=grid(3u);
    start(host,session,24u);
    session->roster.racers[0].max_progress_c6=0x8100u;
    session->roster.racers[2].max_progress_c6=0x8100u;
    tick(session);
    session->roster.racers[0].max_progress_c6=0x8300u;
    session->roster.racers[0].source_flags|=0x20u;
    session->runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
    tick(session);
    CHECK(strcmp(message(host),"S2 L1-1 L3-1 L1-2 L1-3 F1-1 E")==0);
    portable_script_host_free(host);free(session);
    /* Load order: the later mod's on_race_end runs after the first one's. */
    host=create(sources,2u);session=grid(3u);
    start(host,session,24u);
    session->runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
    tick(session);
    CHECK(strcmp(message(host),"OVERWRITTEN")==0);
    portable_script_host_free(host);free(session);
}

static void test_random(void)
{
    const char *const draws=
        "function on_race_start(race)\n"
        "  local values = {}\n"
        "  for i = 1, 6 do values[i] = race.random(1000) end\n"
        "  values[7] = race.random(5, 6)\n"
        "  race.message(table.concat(values, ' '), 20)\n"
        "end\n";
    const char *const sources[]={draws,draws};
    char first[64],second[64];
    PortableScriptHost *host=create(sources,1u);
    PortableGpRaceSession *session=grid(0x797a9bu);
    start(host,session,24u);
    snprintf(first,sizeof first,"%s",message(host));
    start(host,session,24u);
    CHECK(strcmp(first,message(host))==0); /* same race, same stream */
    CHECK(session->source_rng_state==0x797a9bu); /* the game's RNG is untouched */
    start(host,session,25u);
    CHECK(strcmp(first,message(host))!=0);
    portable_script_host_free(host);
    /* Each mod draws its own stream: the second mod's message differs. */
    host=create(sources,2u);
    start(host,session,24u);
    snprintf(second,sizeof second,"%s",message(host));
    CHECK(strcmp(first,second)!=0);
    portable_script_host_free(host);free(session);
}

static void test_items(void)
{
    const char *const sources[]={
        "function on_race_start(race)\n"
        "  local given = race.give_item(1, 'mushroom')\n"
        "  local again = race.give_item(1, 'banana')\n"
        "  local rival = race.give_item(2, 'banana')\n"
        "  local kart = race.kart(1)\n"
        "  race.message((given and 'G' or 'g') .. (again and 'A' or 'a') .. (rival and 'R' or 'r') ..\n"
        "    ' ' .. tostring(kart.item) .. ' ' .. kart.place .. ' ' .. tostring(race.kart(9)), 50)\n"
        "end\n"
        "function on_item_use(kart, item)\n"
        "  if item == 'green_shell' then return 'banana' end\n"
        "  if item == 'spiny_shell' then return false end\n"
        "  if item == 'boo' then return race.give_item(1, 'star') and 'star' or 'boo' end\n"
        "  if item == 'star' then return 'rocket' end\n"
        "end\n"};
    PortableScriptHost *host=create(sources,1u);
    PortableGpRaceSession *session=grid(5u);
    start(host,session,24u);
    CHECK(strcmp(message(host),"GAR MUSHROOM 1 NIL")==0);
    CHECK(session->roster.racers[0].item_inventory.held_item_17a==0x1007u);
    const PortableRaceScriptHooks *hooks=session->script_hooks;
    CHECK(hooks->item_use(hooks->context,0u,0x1000u)==0x1005u);
    CHECK(hooks->item_use(hooks->context,0u,0x1004u)==0u);
    CHECK(hooks->item_use(hooks->context,0u,0x1007u)==0x1007u);
    CHECK(hooks->item_use(hooks->context,0u,0x100bu)==0x100bu); /* no grants mid-use */
    CHECK(hooks->item_use(hooks->context,0u,0x100au)==0x100au); /* bad name: disabled */
    CHECK(portable_script_host_live_count(host)==0u);
    CHECK(hooks->item_use(hooks->context,0u,0x1000u)==0x1000u);
    CHECK(portable_script_item_id("lightning")==0x100cu&&portable_script_item_id("rocket")==0u);
    portable_script_host_free(host);free(session);
}

/* Time Trial: one kart whose slot is the mushroom count, no grants, and
 * on_item_use may only keep or cancel. */
static void test_time_trial(void)
{
    const char *const sources[]={
        "function on_race_start(race)\n"
        "  local k = race.kart(1)\n"
        "  race.message((race.mode:gsub('_', ' ')) .. ' ' .. race.karts .. ' ' .. race.class .. ' ' ..\n"
        "    (k.item:gsub('_', ' ')) .. ' ' .. tostring(race.give_item(1, 'star')) .. ' ' ..\n"
        "    tostring(race.kart(2)) .. ' ' .. k.place, 60)\n"
        "end\n"
        "function on_lap(kart, lap) race.message('lap ' .. lap .. ' of ' .. race.laps, 60) end\n"
        "function on_finish(kart, place) race.message('finish ' .. place, 60) end\n"
        "function on_item_use(kart, item) if item == 'double_mushroom' then return false end end\n"};
    PortableScriptHost *host=create(sources,1u);
    PortableRecoveredTimeTrialSession *session=calloc(1u,sizeof *session);
    CHECK(session!=NULL);
    session->runtime.session.phase=PORTABLE_RACE_PHASE_COUNTDOWN;
    session->runtime.session.lap_count=3u;
    session->mushrooms_remaining=3u;
    CHECK(portable_script_host_begin_time_trial(host,session,7u));
    const PortableRaceScriptHooks *hooks=session->script_hooks;
    CHECK(hooks!=NULL);
    hooks->after_tick(hooks->context);
    session->runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    hooks->after_tick(hooks->context);
    CHECK(strcmp(message(host),"TIME TRIAL 1 150 TRIPLE MUSHROOM FALSE NIL 1")==0);
    CHECK(hooks->item_use(hooks->context,0u,0x1008u)==0x1008u);
    CHECK(hooks->item_use(hooks->context,0u,0x100fu)==0u);
    session->runtime.session.laps_completed=1u;
    hooks->after_tick(hooks->context);
    CHECK(strcmp(message(host),"LAP 1 OF 3")==0);
    session->runtime.session.laps_completed=3u;
    session->runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
    hooks->after_tick(hooks->context);
    CHECK(strcmp(message(host),"FINISH 1")==0);
    portable_script_host_end_race(host);
    CHECK(portable_script_host_message(host)==NULL);
    portable_script_host_free(host);free(session);
}

/* API-level local coverage: the session tick test separately verifies that
 * after_tick runs once after the shared world, rather than once per pilot. */
static PortableGpRaceSession *local_grid(void)
{
    PortableGpRaceSession *session=grid(123u);
    session->local_count=3u;
    session->roster.racer_count=3u;
    session->runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    for(unsigned k=0u;k<2u;++k)
        session->locals[k].runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    return session;
}
static void test_local_scripts(void)
{
    const char *const sources[]={
        "local calls, ends, first, used = 0, 0, nil, ''\n"
        "function on_race_start(race)\n"
        "  first = race.random(1, 1000000)\n"
        "  for i = 1, 3 do assert(race.kart(i).player); assert(race.give_item(i, 'mushroom')) end\n"
        "  assert(race.kart(4) == nil and not race.give_item(4, 'star'))\n"
        "  assert(not race.give_item(2, 'banana'))\n"
        "end\n"
        "function on_item_use(kart, item)\n"
        "  assert(kart.player and not race.give_item(3, 'star'))\n"
        "  used = used .. kart.index\n"
        "  if kart.index == 2 then return false end\n"
        "  return 'banana'\n"
        "end\n"
        "function on_race_end(race) assert(not race.give_item(2, 'star')); assert(not race.give_item(3, 'star')); ends = ends + 1 end\n"
        "function on_tick(race)\n"
        "  calls = calls + 1\n"
        "  if calls == 2 then assert(not race.give_item(3, 'star')) end\n"
        "  race.message(race.mode .. ' ' .. race.phase .. ' ' .. calls .. ' ' .. ends .. ' ' .. used .. ' ' .. first, 60)\n"
        "end\n"};
    for(unsigned battle=0u;battle<2u;++battle){
        PortableScriptHost *host=create(sources,1u);
        PortableGpRaceSession *session=local_grid();
        const PortableScriptMode mode=battle?PORTABLE_SCRIPT_MODE_BATTLE:PORTABLE_SCRIPT_MODE_VS;
        char initial[PORTABLE_SCRIPT_MESSAGE_CAPACITY+1u];
        CHECK(portable_script_host_begin_race(host,session,mode,24u,0u));
        tick(session);
        CHECK(portable_script_host_live_count(host)==1u);
        CHECK(session->local_index==0u&&session->roster.local_racer==0u);
        for(unsigned k=0u;k<3u;++k){
            CHECK(session->roster.racers[k].item_inventory.held_item_17a==0x1007u);
            CHECK(portable_gp_race_session_select_local(session,(uint8_t)k)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(session->roster.item_hud.current==0x1007u);
        }
        CHECK(portable_gp_race_session_select_local(session,0u)==PORTABLE_GP_RACE_SESSION_OK);
        snprintf(initial,sizeof initial,"%s",message(host));
        CHECK(strncmp(initial,battle?"BATTLE ":"VS ",battle?7u:3u)==0);
        const PortableRaceScriptHooks *hooks=session->script_hooks;
        for(unsigned k=0u;k<3u;++k)
            CHECK(hooks->item_use(hooks->context,k,0x1007u)==(k==1u?0u:0x1005u));
        /* P1 finishing does not end the shared race. */
        session->runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
        session->roster.racers[0].source_flags|=0x20u;
        session->roster.racers[2].item_inventory.held_item_17a=0u;
        session->vs_retired[2]=1u; /* Retired, but its pilot phase is still racing. */
        tick(session);
        CHECK(strstr(message(host),"RACING 2 0 123 ")!=NULL);
        CHECK(session->roster.racers[2].item_inventory.held_item_17a==0u);
        session->vs_retired[2]=0u; /* Shared completion independently rejects grants. */
        session->roster.race_phase_7d4=5u;
        session->roster.racers[1].source_flags|=0x20u;
        session->roster.racers[1].item_inventory.held_item_17a=0u;
        tick(session);
        CHECK(strstr(message(host),"FINISHED 3 1 123 ")!=NULL);
        tick(session);
        CHECK(strstr(message(host),"FINISHED 4 1 123 ")!=NULL);
        CHECK(portable_script_host_live_count(host)==1u);
        portable_script_host_end_race(host);
        free(session);
        session=local_grid();
        CHECK(portable_script_host_begin_race(host,session,mode,24u,0u));
        tick(session);
        CHECK(strcmp(initial,message(host))==0); /* Fresh local script/RNG state. */
        portable_script_host_free(host);free(session);
    }
}

int main(void)
{
    test_sandbox();
    test_limits();
    test_order();
    test_random();
    test_items();
    test_time_trial();
    test_local_scripts();
    puts("script host: PASS sandbox, budgets, memory cap, hook order, random streams, item hooks, time trial");
    return 0;
}
