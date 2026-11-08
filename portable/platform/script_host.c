#include "script_host.h"
#include "gp_race_roster.h"
#include "text_glyph.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HOOK_RACE_START, HOOK_LAP, HOOK_ITEM_USE, HOOK_FINISH, HOOK_RACE_END, HOOK_TICK, HOOK_COUNT
};
static const char *const hook_names[HOOK_COUNT]={
    "on_race_start","on_lap","on_item_use","on_finish","on_race_end","on_tick"
};
static const char *const item_names[]={
    "green_shell","triple_green_shell","red_shell","triple_red_shell","spiny_shell",
    "banana","reserved","mushroom","triple_mushroom","golden_mushroom","star","boo","lightning"
};

typedef struct ScriptMod {
    struct PortableScriptHost *host;
    char id[64];
    char *source;
    size_t size;
    lua_State *state;
    size_t memory;
    uint64_t random_state;
    uint32_t budget;
    uint8_t broken;   /* failed to compile: off for the whole session */
    uint8_t disabled; /* failed at run time: off for the rest of this race */
    uint8_t tripped;
    uint8_t loading;
    uint8_t hooks;    /* bit per HOOK_* defined as a global function */
} ScriptMod;

struct PortableScriptHost {
    ScriptMod mods[32];
    size_t count;
    PortableRaceScriptHooks hooks;
    /* The race being scripted: exactly one of these while a race runs. */
    PortableGpRaceSession *session;
    PortableRecoveredTimeTrialSession *time_trial;
    PortableScriptMode mode;
    uint32_t course_id;
    uint8_t engine_class;
    uint8_t started,ended,in_item_use;
    uint8_t laps[PORTABLE_GP_RACER_COUNT];
    uint8_t finished[PORTABLE_GP_RACER_COUNT];
    uint32_t ticks;
    char message[PORTABLE_SCRIPT_MESSAGE_CAPACITY+1u];
    uint16_t message_ticks;
};

uint16_t portable_script_item_id(const char *name)
{
    if(name==NULL)return 0u;
    for(size_t i=0u;i<sizeof item_names/sizeof item_names[0];++i)
        if(strcmp(name,item_names[i])==0)return (uint16_t)(0x1000u+i);
    return 0u;
}
const char *portable_script_item_name(uint16_t item)
{
    if(item>=0x1000u&&item<0x1000u+sizeof item_names/sizeof item_names[0])return item_names[item-0x1000u];
    if(item==0x100eu)return "mushroom";
    if(item==0x100fu)return "double_mushroom";
    return NULL;
}

/* ---- per-mod allocator, instruction budget and RNG --------------------- */

static void *script_alloc(void *user,void *pointer,size_t old_size,size_t new_size)
{
    ScriptMod *mod=user;
    const size_t old=pointer!=NULL?old_size:0u;
    if(new_size==0u){free(pointer);mod->memory-=old;return NULL;}
    if(new_size>old&&new_size-old>PORTABLE_SCRIPT_MEMORY_LIMIT-mod->memory)return NULL;
    void *next=realloc(pointer,new_size);
    if(next!=NULL)mod->memory=mod->memory-old+new_size;
    return next;
}
static int racing(const PortableScriptHost *host){return host->session!=NULL||host->time_trial!=NULL;}
static ScriptMod *state_mod(lua_State *state){return *(ScriptMod **)lua_getextraspace(state);}
/* The race API exists only inside hooks, never while a script's main chunk runs. */
static ScriptMod *race_mod(lua_State *state)
{
    ScriptMod *mod=state_mod(state);
    if(mod->loading||!racing(mod->host))luaL_error(state,"race API is only available inside hooks");
    return mod;
}
static void count_hook(lua_State *state,lua_Debug *debug)
{
    (void)debug;
    ScriptMod *mod=state_mod(state);
    if(!mod->tripped&&mod->budget>1000u){mod->budget-=1000u;return;}
    /* Once tripped, every instruction errors, so pcall cannot keep a runaway
     * loop alive: each catching frame fails again on its next instruction. */
    if(!mod->tripped){mod->tripped=1u;lua_sethook(state,count_hook,LUA_MASKCOUNT,1);}
    luaL_error(state,"instruction budget exceeded");
}
static void arm_budget(ScriptMod *mod,uint32_t budget)
{
    mod->budget=budget;mod->tripped=0u;
    lua_sethook(mod->state,count_hook,LUA_MASKCOUNT,1000);
}
static uint64_t splitmix(uint64_t *state)
{
    uint64_t z=(*state+=UINT64_C(0x9e3779b97f4a7c15));
    z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
    return z^(z>>31);
}

/* ---- race API ----------------------------------------------------------- */

/* One kart as scripts see it, read from either race stack. */
typedef struct {
    int32_t x_q16,y_q16,speed_q16;
    uint16_t item;
    unsigned lap,place;
    uint8_t player,finished,driver;
} ScriptKart;

static const PortableRaceSession *race_session(const PortableScriptHost *host)
{
    return host->time_trial!=NULL?&host->time_trial->runtime.session:&host->session->runtime.session;
}
/* A local pilot can finish before the shared VS race ends. */
static PortableRacePhase script_phase(const PortableScriptHost *host)
{
    const PortableRacePhase phase=race_session(host)->phase;
    if(host->session!=NULL&&host->session->local_count>1u){
        if(portable_gp_race_session_locals_finished(host->session))return PORTABLE_RACE_PHASE_FINISHED;
        return phase==PORTABLE_RACE_PHASE_COUNTDOWN?phase:PORTABLE_RACE_PHASE_RACING;
    }
    return phase;
}
static size_t racer_count(const PortableScriptHost *host)
{
    if(host->time_trial!=NULL)return 1u;
    const size_t count=portable_gp_race_roster_count(&host->session->roster);
    return count<PORTABLE_GP_RACER_COUNT?count:PORTABLE_GP_RACER_COUNT;
}
static unsigned lap_count(const PortableScriptHost *host)
{
    return host->time_trial!=NULL?host->time_trial->runtime.session.lap_count:host->session->rules.lap_count;
}
static void read_kart(const PortableScriptHost *host,size_t index,ScriptKart *kart)
{
    if(host->time_trial!=NULL){
        /* Time Trial's slot is its mushroom count. */
        static const uint16_t mushrooms[4]={0u,0x100eu,0x100fu,0x1008u};
        const PortableRecoveredTimeTrialSession *trial=host->time_trial;
        const PortableRaceSession *race=&trial->runtime.session;
        *kart=(ScriptKart){trial->driver.pose.position_x,trial->driver.pose.position_y,
            trial->driver.motion.speed_q16,
            trial->mushrooms_remaining<=3u?mushrooms[trial->mushrooms_remaining]:0u,
            race->laps_completed<race->lap_count?race->laps_completed:race->lap_count,1u,1u,
            (uint8_t)(race->phase==PORTABLE_RACE_PHASE_FINISHED),
            (uint8_t)race->compatibility.character_class};
        return;
    }
    const PortableGpRaceSession *session=host->session;
    const PortableGpRacer *racer=&session->roster.racers[index];
    const unsigned wrap=racer->max_progress_c6>>8u;
    unsigned laps=wrap>=0x80u?wrap-0x80u:0u;
    if(laps>session->rules.lap_count)laps=session->rules.lap_count;
    *kart=(ScriptKart){racer->position_x_q16,racer->position_y_q16,racer->speed_q16,
        racer->item_inventory.active?0u:racer->item_inventory.held_item_17a,laps,
        (racer->rank_flags_ba>>1u)+1u,(uint8_t)(index<(session->local_count>1u?session->local_count:1u)),
        (uint8_t)((racer->source_flags&UINT32_C(0x20))!=0u),racer->driver_class};
}
static void push_kart(lua_State *state,const PortableScriptHost *host,size_t index)
{
    ScriptKart kart;
    read_kart(host,index,&kart);
    lua_createtable(state,0,10);
    lua_pushinteger(state,(lua_Integer)index+1);lua_setfield(state,-2,"index");
    lua_pushboolean(state,kart.player);lua_setfield(state,-2,"player");
    lua_pushinteger(state,kart.x_q16/65536);lua_setfield(state,-2,"x");
    lua_pushinteger(state,kart.y_q16/65536);lua_setfield(state,-2,"y");
    lua_pushnumber(state,(lua_Number)kart.speed_q16/65536.0);lua_setfield(state,-2,"speed");
    const char *item=portable_script_item_name(kart.item);
    if(item!=NULL){lua_pushstring(state,item);lua_setfield(state,-2,"item");}
    lua_pushinteger(state,kart.lap);lua_setfield(state,-2,"lap");
    lua_pushinteger(state,kart.place);lua_setfield(state,-2,"place");
    lua_pushboolean(state,kart.finished);lua_setfield(state,-2,"finished");
    lua_pushinteger(state,kart.driver);lua_setfield(state,-2,"driver");
}
static int api_random(lua_State *state)
{
    ScriptMod *mod=race_mod(state);
    lua_Integer low=1,high;
    if(lua_gettop(state)>=2){low=luaL_checkinteger(state,1);high=luaL_checkinteger(state,2);}
    else high=luaL_checkinteger(state,1);
    luaL_argcheck(state,low>=-INT32_MAX&&high<=INT32_MAX&&low<=high,1,"bounds must be ordered 32-bit integers");
    const uint64_t span=(uint64_t)(high-low)+1u;
    const uint64_t sample=splitmix(&mod->random_state)>>32u;
    lua_pushinteger(state,low+(lua_Integer)((sample*span)>>32u));
    return 1;
}
static int api_message(lua_State *state)
{
    ScriptMod *mod=race_mod(state);
    size_t length;const char *text=luaL_checklstring(state,1,&length);
    const lua_Integer ticks=luaL_optinteger(state,2,120);
    luaL_argcheck(state,ticks>=0&&ticks<=PORTABLE_SCRIPT_MESSAGE_MAX_TICKS,2,"ticks must be 0..600");
    PortableScriptHost *host=mod->host;
    size_t out=0u;
    for(size_t i=0u;i<length&&out<PORTABLE_SCRIPT_MESSAGE_CAPACITY;++i){
        char c=text[i];
        if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
        host->message[out++]=c==' '||portable_text_character(c)?c:'?';
    }
    host->message[out]='\0';
    host->message_ticks=(uint16_t)ticks;
    return 0;
}
static int api_kart(lua_State *state)
{
    const PortableScriptHost *host=race_mod(state)->host;
    const lua_Integer index=luaL_checkinteger(state,1);
    if(index<1||(size_t)index>racer_count(host)){lua_pushnil(state);return 1;}
    push_kart(state,host,(size_t)index-1u);
    return 1;
}
static int api_give_item(lua_State *state)
{
    PortableScriptHost *host=race_mod(state)->host;
    const lua_Integer kart=luaL_checkinteger(state,1);
    const uint16_t item=portable_script_item_id(luaL_checkstring(state,2));
    luaL_argcheck(state,item!=0u,2,"unknown item name");
    /* Grants are local-only and never mutate selection inside an item-use hook. */
    int granted=0;
    if(!host->in_item_use&&host->session!=NULL){
        PortableGpRaceSession *session=host->session;
        const size_t count=session->local_count>1u?session->local_count:1u;
        if(kart>=1&&(size_t)kart<=count&&
           !(session->local_count>1u&&(portable_gp_race_session_locals_finished(session)||
               session->vs_retired[(size_t)kart-1u]))){
            const uint8_t previous=session->local_index;
            if(portable_gp_race_session_select_local(session,(uint8_t)(kart-1))==PORTABLE_GP_RACE_SESSION_OK){
                granted=portable_gp_race_session_grant_player_item(session,item)==PORTABLE_GP_RACE_SESSION_OK;
                if(portable_gp_race_session_select_local(session,previous)!=PORTABLE_GP_RACE_SESSION_OK)
                    return luaL_error(state,"could not restore local kart selection");
            }
        }
    }
    lua_pushboolean(state,granted);
    return 1;
}
static int api_print(lua_State *state)
{
    const ScriptMod *mod=state_mod(state);
    printf("[%s]",mod->id);
    const int count=lua_gettop(state);
    for(int i=1;i<=count;++i){printf(" %s",luaL_tolstring(state,i,NULL));lua_pop(state,1);}
    printf("\n");fflush(stdout);
    return 0;
}
/* Raw writes: a script's metatables cannot run code outside protection. */
static void set_integer(lua_State *state,const char *key,lua_Integer value)
{lua_pushstring(state,key);lua_pushinteger(state,value);lua_rawset(state,-3);}
static void set_string(lua_State *state,const char *key,const char *value)
{lua_pushstring(state,key);lua_pushstring(state,value);lua_rawset(state,-3);}
static void push_race(ScriptMod *mod)
{
    static const char *const modes[]={"gp","quick_run","battle","time_trial","vs"};
    static const char *const phases[]={"countdown","racing","finished"};
    const PortableScriptHost *host=mod->host;
    lua_State *state=mod->state;
    lua_getfield(state,LUA_REGISTRYINDEX,"mksc.race");
    set_integer(state,"tick",host->ticks);
    const unsigned phase=(unsigned)script_phase(host);
    set_string(state,"phase",phase>=1u&&phase<=3u?phases[phase-1u]:"countdown");
    set_integer(state,"laps",lap_count(host));
    set_integer(state,"course",host->course_id);
    set_string(state,"mode",modes[host->mode]);
    set_integer(state,"class",50*(host->engine_class+1));
    set_integer(state,"karts",(lua_Integer)racer_count(host));
}

/* ---- state lifecycle ----------------------------------------------------- */

static void arm_budget(ScriptMod *mod,uint32_t budget);
static void close_mod(ScriptMod *mod)
{
    /* Finalizers run on close; the budget still bounds them. */
    if(mod->state!=NULL){arm_budget(mod,PORTABLE_SCRIPT_HOOK_BUDGET);lua_close(mod->state);}
    mod->state=NULL;mod->memory=0u;mod->hooks=0u;
}
static void report(ScriptMod *mod,const char *where,const char *detail)
{
    fprintf(stderr,"mod %s: %s: %s; script disabled%s\n",mod->id,where,
        detail!=NULL?detail:"error",mod->broken?"":" for this race");
}
/* Restricted global table: base subset, string, table and math without
 * random; no io/os/package/debug/coroutine, no load/dofile/loadfile/require. */
static int open_sandbox(lua_State *state)
{
    luaL_requiref(state,LUA_GNAME,luaopen_base,1);
    luaL_requiref(state,LUA_STRLIBNAME,luaopen_string,1);
    luaL_requiref(state,LUA_TABLIBNAME,luaopen_table,1);
    luaL_requiref(state,LUA_MATHLIBNAME,luaopen_math,1);
    lua_settop(state,0);
    static const char *const removed[]={"dofile","loadfile","load","require","collectgarbage","warn","print"};
    for(size_t i=0u;i<sizeof removed/sizeof removed[0];++i){lua_pushnil(state);lua_setglobal(state,removed[i]);}
    lua_getglobal(state,"math");
    lua_pushnil(state);lua_setfield(state,-2,"random");
    lua_pushnil(state);lua_setfield(state,-2,"randomseed");
    lua_pop(state,1);
    lua_getglobal(state,"string");
    lua_pushnil(state);lua_setfield(state,-2,"dump");
    lua_pop(state,1);
    lua_pushcfunction(state,api_print);lua_setglobal(state,"print");
    static const luaL_Reg race[]={
        {"random",api_random},{"message",api_message},{"kart",api_kart},
        {"give_item",api_give_item},{NULL,NULL}
    };
    luaL_newlib(state,race);
    lua_pushvalue(state,-1);
    lua_setfield(state,LUA_REGISTRYINDEX,"mksc.race");
    lua_setglobal(state,"race");
    return 0;
}
static int open_mod(ScriptMod *mod)
{
    close_mod(mod);
    mod->state=lua_newstate(script_alloc,mod);
    if(mod->state==NULL){report(mod,"load","out of memory");return 0;}
    *(ScriptMod **)lua_getextraspace(mod->state)=mod;
    arm_budget(mod,PORTABLE_SCRIPT_LOAD_BUDGET);
    lua_pushcfunction(mod->state,open_sandbox);
    if(lua_pcall(mod->state,0,0,0)!=LUA_OK){report(mod,"load",lua_tostring(mod->state,-1));close_mod(mod);return 0;}
    char chunk[96];snprintf(chunk,sizeof chunk,"=%s",mod->id);
    /* Text only: precompiled bytecode is not a safe input. */
    mod->loading=1u;
    const int loaded=luaL_loadbufferx(mod->state,mod->source,mod->size,chunk,"t")==LUA_OK&&
        lua_pcall(mod->state,0,0,0)==LUA_OK&&!mod->tripped;
    mod->loading=0u;
    if(!loaded){
        report(mod,"load",mod->tripped?"instruction budget exceeded":lua_tostring(mod->state,-1));
        close_mod(mod);return 0;
    }
    lua_pushglobaltable(mod->state);
    for(unsigned hook=0u;hook<HOOK_COUNT;++hook){
        lua_pushstring(mod->state,hook_names[hook]);
        if(lua_rawget(mod->state,-2)==LUA_TFUNCTION)mod->hooks|=(uint8_t)(1u<<hook);
        lua_pop(mod->state,1);
    }
    lua_settop(mod->state,0);
    return 1;
}

PortableScriptHost *portable_script_host_create(const PortableScriptSource *scripts,size_t count)
{
    if(count>32u||(count!=0u&&scripts==NULL))return NULL;
    PortableScriptHost *host=calloc(1u,sizeof *host);
    if(host==NULL)return NULL;
    for(size_t i=0u;i<count;++i){
        ScriptMod *mod=&host->mods[host->count++];
        mod->host=host;
        snprintf(mod->id,sizeof mod->id,"%s",scripts[i].id!=NULL?scripts[i].id:"?");
        mod->source=malloc(scripts[i].size!=0u?scripts[i].size:1u);
        if(mod->source==NULL){portable_script_host_free(host);return NULL;}
        if(scripts[i].size!=0u)memcpy(mod->source,scripts[i].source,scripts[i].size);
        mod->size=scripts[i].size;
        mod->broken=1u;
        if(open_mod(mod))mod->broken=0u;
        close_mod(mod);
    }
    return host;
}
void portable_script_host_free(PortableScriptHost *host)
{
    if(host==NULL)return;
    portable_script_host_end_race(host);
    for(size_t i=0u;i<host->count;++i)free(host->mods[i].source);
    free(host);
}
size_t portable_script_host_active_count(const PortableScriptHost *host)
{
    size_t count=0u;
    if(host!=NULL)for(size_t i=0u;i<host->count;++i)count+=!host->mods[i].broken;
    return count;
}

const char *portable_script_host_failed_id(const PortableScriptHost *host)
{
    if(host!=NULL)
        for(size_t i=0u;i<host->count;++i)
            if(host->mods[i].broken)return host->mods[i].id;
    return NULL;
}

size_t portable_script_host_live_count(const PortableScriptHost *host)
{
    size_t count=0u;
    if(host!=NULL)for(size_t i=0u;i<host->count;++i)count+=host->mods[i].state!=NULL&&!host->mods[i].disabled;
    return count;
}

/* ---- hook dispatch ------------------------------------------------------- */

typedef struct { ScriptMod *mod; unsigned hook; size_t racer; lua_Integer value; uint16_t item; } HookCall;
/* Runs protected: fills race, looks the hook up and calls it with its
 * arguments; a missing hook is a no-op returning nil. */
static int run_hook(lua_State *state)
{
    const HookCall *call=lua_touserdata(state,1);
    lua_settop(state,0);
    push_race(call->mod);
    lua_pushglobaltable(state);
    lua_pushstring(state,hook_names[call->hook]);
    if(lua_rawget(state,-2)!=LUA_TFUNCTION)return 0;
    if(call->hook==HOOK_RACE_START||call->hook==HOOK_RACE_END||call->hook==HOOK_TICK){
        lua_pushvalue(state,1);lua_call(state,1,1);
    }else{
        push_kart(state,call->mod->host,call->racer);
        if(call->hook==HOOK_ITEM_USE)lua_pushstring(state,portable_script_item_name(call->item));
        else lua_pushinteger(state,call->value);
        lua_call(state,2,1);
    }
    return 1;
}
/* Calls one hook in load order on every live mod; on_item_use threads the
 * chosen item through, and a cancel stops the chain. */
static uint16_t call_hook(PortableScriptHost *host,unsigned hook,size_t racer,lua_Integer value,uint16_t item)
{
    for(size_t i=0u;i<host->count;++i){
        ScriptMod *mod=&host->mods[i];
        if(mod->state==NULL||mod->disabled||(mod->hooks&(1u<<hook))==0u)continue;
        lua_State *state=mod->state;
        HookCall call={mod,hook,racer,value,item};
        lua_settop(state,0);
        lua_pushcfunction(state,run_hook);
        lua_pushlightuserdata(state,&call);
        arm_budget(mod,PORTABLE_SCRIPT_HOOK_BUDGET);
        const int status=lua_pcall(state,1,1,0);
        const char *problem=NULL;
        if(status!=LUA_OK||mod->tripped)
            problem=mod->tripped?"instruction budget exceeded":lua_tostring(state,-1);
        else if(hook==HOOK_ITEM_USE){
            if(lua_isboolean(state,-1)&&!lua_toboolean(state,-1))item=0u;
            else if(lua_type(state,-1)==LUA_TSTRING){
                const uint16_t chosen=portable_script_item_id(lua_tostring(state,-1));
                if(chosen==0u)problem="returned an unknown item name";
                else item=chosen;
            }
        }
        if(problem!=NULL){
            report(mod,hook_names[hook],problem);
            mod->disabled=1u;close_mod(mod);
            continue;
        }
        lua_settop(state,0);
        if(hook==HOOK_ITEM_USE&&item==0u)break;
    }
    return item;
}
static uint16_t hook_item_use(void *context,size_t racer,uint16_t item)
{
    PortableScriptHost *host=context;
    if(!racing(host)||racer>=racer_count(host)||portable_script_item_name(item)==NULL)return item;
    host->in_item_use=1u;
    const uint16_t chosen=call_hook(host,HOOK_ITEM_USE,racer,0,item);
    host->in_item_use=0u;
    return chosen;
}
static void hook_after_tick(void *context)
{
    PortableScriptHost *host=context;
    if(!racing(host))return;
    ++host->ticks;
    if(host->message_ticks!=0u&&--host->message_ticks==0u)host->message[0]='\0';
    const PortableRacePhase phase=script_phase(host);
    if(!host->started&&phase!=PORTABLE_RACE_PHASE_COUNTDOWN){
        host->started=1u;call_hook(host,HOOK_RACE_START,0u,0,0u);
    }
    const size_t count=racer_count(host);
    ScriptKart kart;
    for(size_t racer=0u;racer<count&&lap_count(host)!=0u;++racer){
        read_kart(host,racer,&kart);
        while(host->laps[racer]<kart.lap)call_hook(host,HOOK_LAP,racer,++host->laps[racer],0u);
    }
    for(size_t racer=0u;racer<count;++racer){
        read_kart(host,racer,&kart);
        if(kart.finished&&!host->finished[racer])call_hook(host,HOOK_FINISH,racer,kart.place,0u);
        host->finished[racer]=kart.finished;
    }
    if(!host->ended&&phase==PORTABLE_RACE_PHASE_FINISHED){
        host->ended=1u;call_hook(host,HOOK_RACE_END,0u,0,0u);
    }
    call_hook(host,HOOK_TICK,0u,0,0u);
}

/* Fresh states for every compiled mod; NULL hooks when none is live. */
static const PortableRaceScriptHooks *begin_scripts(PortableScriptHost *host,uint64_t seed)
{
    size_t live=0u;
    for(size_t i=0u;i<host->count;++i){
        ScriptMod *mod=&host->mods[i];
        mod->disabled=0u;
        if(mod->broken)continue;
        uint64_t stream=seed;
        for(const char *c=mod->id;*c!='\0';++c)stream=(stream^(uint8_t)*c)*UINT64_C(0x100000001b3);
        mod->random_state=stream;
        if(!open_mod(mod)){mod->disabled=1u;continue;}
        ++live;
    }
    const size_t count=racer_count(host);
    ScriptKart kart;
    for(size_t racer=0u;racer<count;++racer){
        read_kart(host,racer,&kart);
        host->laps[racer]=(uint8_t)kart.lap;
        host->finished[racer]=kart.finished;
    }
    if(live==0u){host->session=NULL;host->time_trial=NULL;return NULL;}
    host->hooks=(PortableRaceScriptHooks){host,hook_after_tick,hook_item_use};
    return &host->hooks;
}

int portable_script_host_begin_race(PortableScriptHost *host,PortableGpRaceSession *session,
    PortableScriptMode mode,uint32_t course_id,uint8_t engine_class)
{
    if(session==NULL||(mode!=PORTABLE_SCRIPT_MODE_GP&&mode!=PORTABLE_SCRIPT_MODE_QUICK_RUN&&
        mode!=PORTABLE_SCRIPT_MODE_BATTLE&&mode!=PORTABLE_SCRIPT_MODE_VS))return 0;
    session->script_hooks=NULL;
    if(host==NULL)return 1;
    portable_script_host_end_race(host);
    host->session=session;host->mode=mode;host->course_id=course_id;host->engine_class=engine_class;
    const uint64_t seed=((uint64_t)course_id<<32u)^((uint64_t)engine_class<<24u)^((uint64_t)mode<<16u)^
        session->source_rng_state^((uint64_t)session->roster.racers[0].driver_class<<40u);
    session->script_hooks=begin_scripts(host,seed);
    return 1;
}
int portable_script_host_begin_time_trial(PortableScriptHost *host,
    PortableRecoveredTimeTrialSession *session,uint32_t course_id)
{
    if(session==NULL)return 0;
    session->script_hooks=NULL;
    if(host==NULL)return 1;
    portable_script_host_end_race(host);
    host->time_trial=session;host->mode=PORTABLE_SCRIPT_MODE_TIME_TRIAL;
    host->course_id=course_id;host->engine_class=2u;
    /* No game RNG in Time Trial: each attempt draws the same numbers. */
    const uint64_t seed=((uint64_t)course_id<<32u)^((uint64_t)2u<<24u)^
        ((uint64_t)PORTABLE_SCRIPT_MODE_TIME_TRIAL<<16u)^
        ((uint64_t)session->runtime.session.compatibility.character_class<<40u);
    session->script_hooks=begin_scripts(host,seed);
    return 1;
}
void portable_script_host_end_race(PortableScriptHost *host)
{
    if(host==NULL)return;
    for(size_t i=0u;i<host->count;++i)close_mod(&host->mods[i]);
    host->session=NULL;host->time_trial=NULL;host->started=host->ended=host->in_item_use=0u;host->ticks=0u;
    host->message[0]='\0';host->message_ticks=0u;
    memset(host->laps,0,sizeof host->laps);memset(host->finished,0,sizeof host->finished);
}
const char *portable_script_host_message(const PortableScriptHost *host)
{
    return host!=NULL&&host->message_ticks!=0u&&host->message[0]!='\0'?host->message:NULL;
}
