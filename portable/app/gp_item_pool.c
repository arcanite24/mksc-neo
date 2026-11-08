#include "gp_race_roster.h"

const PortableGpGreenShell *portable_gp_green_actor(const PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_GREEN_ACTOR_SLOTS)return NULL;
    return actor<8u?&roster->racers[actor].green_shell:&roster->green_pool[actor-8u].shell;
}

PortableGpGreenShell *portable_gp_green_actor_mutable(PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_GREEN_ACTOR_SLOTS)return NULL;
    return actor<8u?&roster->racers[actor].green_shell:&roster->green_pool[actor-8u].shell;
}

size_t portable_gp_green_actor_owner(const PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_GREEN_ACTOR_SLOTS)return PORTABLE_GP_RACER_COUNT;
    return actor<8u?actor:roster->green_pool[actor-8u].owner;
}

const PortableGpRedShell *portable_gp_red_actor(const PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_RED_ACTOR_SLOTS)return NULL;
    return actor<8u?&roster->racers[actor].red_shell:&roster->red_pool[actor-8u].shell;
}

PortableGpRedShell *portable_gp_red_actor_mutable(PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_RED_ACTOR_SLOTS)return NULL;
    return actor<8u?&roster->racers[actor].red_shell:&roster->red_pool[actor-8u].shell;
}

size_t portable_gp_red_actor_owner(const PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_RED_ACTOR_SLOTS)return PORTABLE_GP_RACER_COUNT;
    return actor<8u?actor:roster->red_pool[actor-8u].owner;
}

const PortableGpBanana *portable_gp_banana_actor(const PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_BANANA_ACTOR_SLOTS)return NULL;
    return actor<8u?&roster->racers[actor].banana:&roster->banana_pool[actor-8u].banana;
}

PortableGpBanana *portable_gp_banana_actor_mutable(PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_BANANA_ACTOR_SLOTS)return NULL;
    return actor<8u?&roster->racers[actor].banana:&roster->banana_pool[actor-8u].banana;
}

size_t portable_gp_banana_actor_owner(const PortableGpRaceRoster *roster,size_t actor)
{
    if(roster==NULL || actor>=PORTABLE_GP_BANANA_ACTOR_SLOTS)return PORTABLE_GP_RACER_COUNT;
    return actor<8u?actor:roster->banana_pool[actor-8u].owner;
}

unsigned portable_gp_item_pool_count(const PortableGpRaceRoster *roster)
{
    unsigned count=0u;
    for(unsigned i=0u;i<PORTABLE_GP_GREEN_ACTOR_SLOTS;++i)count+=portable_gp_green_actor(roster,i)->occupied!=0u;
    for(unsigned i=0u;i<PORTABLE_GP_RED_ACTOR_SLOTS;++i)count+=portable_gp_red_actor(roster,i)->occupied!=0u;
    for(unsigned i=0u;i<PORTABLE_GP_BANANA_ACTOR_SLOTS;++i)count+=portable_gp_banana_actor(roster,i)->occupied!=0u;
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i)count+=roster->mushroom_pool[i].occupied!=0u;
    return count;
}

unsigned portable_gp_green_actor_order(const PortableGpRaceRoster *roster,
    uint8_t actors[PORTABLE_GP_GREEN_ACTOR_SLOTS])
{
    unsigned count=0u;
    for(unsigned actor=0u;actor<PORTABLE_GP_GREEN_ACTOR_SLOTS;++actor){
        const PortableGpGreenShell *shell=portable_gp_green_actor(roster,actor);
        if(!shell->occupied)continue;
        unsigned j=count++;
        while(j){
            const uint32_t prior=portable_gp_green_actor(roster,actors[j-1u])->creation_order;
            if(shell->creation_order==prior || (uint32_t)(shell->creation_order-prior)>=UINT32_C(0x80000000))break;
            actors[j]=actors[j-1u];--j;
        }
        actors[j]=(uint8_t)actor;
    }
    return count;
}

unsigned portable_gp_red_actor_order(const PortableGpRaceRoster *roster,
    uint8_t actors[PORTABLE_GP_RED_ACTOR_SLOTS])
{
    unsigned count=0u;
    for(unsigned actor=0u;actor<PORTABLE_GP_RED_ACTOR_SLOTS;++actor){
        const PortableGpRedShell *shell=portable_gp_red_actor(roster,actor);
        if(!shell->occupied)continue;
        unsigned j=count++;
        while(j){
            const uint32_t prior=portable_gp_red_actor(roster,actors[j-1u])->creation_order;
            if(shell->creation_order==prior || (uint32_t)(shell->creation_order-prior)>=UINT32_C(0x80000000))break;
            actors[j]=actors[j-1u];--j;
        }
        actors[j]=(uint8_t)actor;
    }
    return count;
}

unsigned portable_gp_banana_actor_order(const PortableGpRaceRoster *roster,
    uint8_t actors[PORTABLE_GP_BANANA_ACTOR_SLOTS])
{
    unsigned count=0u;
    for(unsigned actor=0u;actor<PORTABLE_GP_BANANA_ACTOR_SLOTS;++actor){
        const PortableGpBanana *banana=portable_gp_banana_actor(roster,actor);
        if(!banana->occupied)continue;
        unsigned j=count++;
        while(j){
            const uint32_t prior=portable_gp_banana_actor(roster,actors[j-1u])->creation_order;
            if(banana->creation_order==prior || (uint32_t)(banana->creation_order-prior)>=UINT32_C(0x80000000))break;
            actors[j]=actors[j-1u];--j;
        }
        actors[j]=(uint8_t)actor;
    }
    return count;
}

/*4F7AC takes a free node, or reuses the oldest live node when the16 slots
 * are full. 478E8 retires a still-owned orbit slot before reusing its node. */
void portable_gp_item_pool_reserve(PortableGpRaceRoster *roster)
{
    for(;;){
        unsigned count=0u;uint32_t oldest=0u;
        uint8_t *occupied=NULL;uint16_t *flags=NULL;
        PortableGpGreenShell *retiring_green=NULL;PortableGpRedShell *retiring_red=NULL;size_t owner=0u;
        for(unsigned i=0u;i<PORTABLE_GP_GREEN_ACTOR_SLOTS+PORTABLE_GP_RED_ACTOR_SLOTS+PORTABLE_GP_BANANA_ACTOR_SLOTS+PORTABLE_GP_ITEM_POOL_CAPACITY;++i){
            uint8_t *live;uint16_t *visible;uint32_t order;
            if(i<PORTABLE_GP_GREEN_ACTOR_SLOTS){
                PortableGpGreenShell *s=portable_gp_green_actor_mutable(roster,i);
                live=&s->occupied;visible=&s->flags_34;order=s->creation_order;
            }else if(i<PORTABLE_GP_GREEN_ACTOR_SLOTS+PORTABLE_GP_RED_ACTOR_SLOTS){
                PortableGpRedShell *s=portable_gp_red_actor_mutable(roster,i-PORTABLE_GP_GREEN_ACTOR_SLOTS);
                live=&s->occupied;visible=&s->flags_34;order=s->creation_order;
            }else if(i<PORTABLE_GP_GREEN_ACTOR_SLOTS+PORTABLE_GP_RED_ACTOR_SLOTS+PORTABLE_GP_BANANA_ACTOR_SLOTS){
                PortableGpBanana *s=portable_gp_banana_actor_mutable(roster,i-PORTABLE_GP_GREEN_ACTOR_SLOTS-PORTABLE_GP_RED_ACTOR_SLOTS);
                live=&s->occupied;visible=&s->flags_34;order=s->creation_order;
            }else{
                PortableGpMushroomActor *s=&roster->mushroom_pool[i-PORTABLE_GP_GREEN_ACTOR_SLOTS-PORTABLE_GP_RED_ACTOR_SLOTS-PORTABLE_GP_BANANA_ACTOR_SLOTS];
                live=&s->occupied;visible=&s->flags_34;order=s->creation_order;
            }
            if(!*live)continue;
            ++count;
            if(occupied==NULL || (order!=oldest && (uint32_t)(oldest-order)<UINT32_C(0x80000000))){
                occupied=live;flags=visible;oldest=order;
                retiring_green=i<PORTABLE_GP_GREEN_ACTOR_SLOTS?portable_gp_green_actor_mutable(roster,i):NULL;
                retiring_red=i>=PORTABLE_GP_GREEN_ACTOR_SLOTS && i<PORTABLE_GP_GREEN_ACTOR_SLOTS+PORTABLE_GP_RED_ACTOR_SLOTS?
                    portable_gp_red_actor_mutable(roster,i-PORTABLE_GP_GREEN_ACTOR_SLOTS):NULL;
                if(retiring_green)owner=portable_gp_green_actor_owner(roster,i);
                else if(retiring_red)owner=portable_gp_red_actor_owner(roster,i-PORTABLE_GP_GREEN_ACTOR_SLOTS);
            }
        }
        if(count<PORTABLE_GP_ITEM_POOL_CAPACITY)return;
        if((retiring_green && retiring_green->retire_held_slot) || (retiring_red && retiring_red->retire_held_slot)){
            roster->racers[owner].source_flags&=UINT32_C(0xfffeffff);
            --roster->racers[owner].item_use_state_17c;
        }
        *occupied=0u;*flags=0u;
    }
}

void portable_gp_item_pool_retain_green(PortableGpRaceRoster *roster,size_t owner)
{
    PortableGpGreenShell *shell=&roster->racers[owner].green_shell;
    if(!shell->occupied)return;
    /* reserve has made room for the new actor, so there is always a free
     * retained slot, even if every other live actor belongs to this owner. */
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i)if(!roster->green_pool[i].shell.occupied){
        roster->green_pool[i].shell=*shell;roster->green_pool[i].owner=(uint8_t)owner;return;
    }
}

void portable_gp_item_pool_retain_red(PortableGpRaceRoster *roster,size_t owner)
{
    PortableGpRedShell *shell=&roster->racers[owner].red_shell;
    if(!shell->occupied)return;
    /* reserve has made room for the new actor, so there is always a free
     * retained slot, even if every other live actor belongs to this owner. */
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i)if(!roster->red_pool[i].shell.occupied){
        roster->red_pool[i].shell=*shell;roster->red_pool[i].owner=(uint8_t)owner;return;
    }
}

void portable_gp_item_pool_retain_banana(PortableGpRaceRoster *roster,size_t owner)
{
    PortableGpBanana *banana=&roster->racers[owner].banana;
    if(!banana->occupied)return;
    /* reserve has made room for the new actor, so there is always a free
     * retained slot, even if every other live actor belongs to this owner. */
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i)if(!roster->banana_pool[i].banana.occupied){
        roster->banana_pool[i].banana=*banana;roster->banana_pool[i].owner=(uint8_t)owner;return;
    }
}

void portable_gp_item_pool_construct_mushroom(PortableGpRaceRoster *roster,
    size_t owner,uint16_t lifetime,uint8_t golden)
{
    /* Even48340/48370/482E0 allocate through7FD8 before immediately freeing
     * the node. A full pool must therefore still retire its oldest actor. */
    portable_gp_item_pool_reserve(roster);
    const uint32_t order=++roster->item_uses;
    if(lifetime==0u)return;
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i){
        PortableGpMushroomActor *actor=&roster->mushroom_pool[i];
        if(actor->occupied)continue;
        *actor=(PortableGpMushroomActor){.creation_order=order,
            .ticks_remaining=lifetime,.flags_34=2u,.owner=(uint8_t)owner,
            .golden=golden,.occupied=1u};
        return;
    }
}
