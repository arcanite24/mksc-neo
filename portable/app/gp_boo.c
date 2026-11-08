#include "gp_race_roster.h"
static void set_item(PortableGpRacer *r,uint16_t item)
{
    r->item_inventory=(PortableGpItemInventory){.rules=r->item_inventory.rules,.held_item_17a=item,
        .preview_item=item,.settled_item=item,.player_owned=r->is_player};
}
PortableGpRosterStatus portable_gp_race_roster_use_boo(PortableGpRaceRoster *r,size_t index)
{
    if(!r||index>=portable_gp_race_roster_count(r))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const PortableBooRules *rules=(r->boo_rules);
    if(!portable_boo_rules_valid(rules))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *owner=&r->racers[index];
    if(owner->item_inventory.held_item_17a!=0x100bu ||
       (owner->is_player && ((owner->control&0x80u)==0u ||
        (owner->source_flags&0x10800u)!=0u || owner->item_use_state_17c!=0u)))
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    unsigned seen=0u;
    for(unsigned rank=0u;rank<portable_gp_race_roster_count(r);++rank){
        if(r->standings[rank]>=portable_gp_race_roster_count(r) || (seen&(1u<<r->standings[rank])))
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        seen|=1u<<r->standings[rank];
    }
    PortableBooAttack *actor=NULL;
    if(r->standings[0]!=index){
        for(unsigned i=0u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i)
            if(!r->boo_attacks[i].occupied){actor=&r->boo_attacks[i];break;}
        if(!actor)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    }
    /* 48694 clears the user's Boo before scanning rank order, so another
     * Boo can be stolen without selecting the just-consumed inventory. */
    set_item(owner,0u);owner->contact_status_66=(int16_t)rules->invisibility_ticks;
    for(unsigned rank=0u;rank<portable_gp_race_roster_count(r);++rank){
        const unsigned victim=r->standings[rank];
        if(victim==index)continue;
        uint16_t item=r->racers[victim].item_inventory.held_item_17a;
        if(item<=2u)continue;
        if(item==0x100eu)item=0x1009u;
        if(victim==r->local_racer)
            portable_gp_item_hud_transition(&r->item_hud,item,2u);
        set_item(owner,item);set_item(&r->racers[victim],0u);++r->boo_thefts;break;
    }
    if(index==r->local_racer)
        portable_gp_item_hud_transition(&r->item_hud,0x100bu,owner->item_inventory.held_item_17a);
    if(actor)*actor=(PortableBooAttack){.target=r->standings[0],.occupied=1u};
    ++r->boo_uses;++r->item_uses;
    return PORTABLE_GP_ROSTER_OK;
}
PortableGpRosterStatus portable_gp_race_roster_haunt(PortableGpRaceRoster *r,size_t index)
{
    if(!r||index>=portable_gp_race_roster_count(r))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for(unsigned i=0u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i)
        if(!r->boo_attacks[i].occupied){
            r->boo_attacks[i]=(PortableBooAttack){.target=(uint8_t)index,.occupied=1u};
            return PORTABLE_GP_ROSTER_OK;
        }
    return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
}
PortableGpRosterStatus portable_gp_race_roster_tick_boo_attacks(PortableGpRaceRoster *r,
    const int32_t *sine,size_t count)
{
    if(!r||!sine||count<1024u)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    for(unsigned i=0u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i){
        PortableBooAttack *a=&r->boo_attacks[i];
        if(!a->occupied)continue;
        if(a->target>=portable_gp_race_roster_count(r))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        PortableGpRacer *victim=&r->racers[a->target];
        PortableBooAttackTarget target={.x_q16=victim->position_x_q16,
            .y_q16=victim->position_y_q16,.z_q16=victim->world_height_q16,
            .component_x=victim->component_x,.component_y=victim->component_y,
            .heading=victim->heading,.protection=victim->lightning_status_6a,
            .lock_ticks=victim->item_lock_ac};
        if(!portable_boo_attack_tick(a,&target,sine,count,r->boo_rules))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        victim->item_lock_ac=target.lock_ticks;
    }
    return PORTABLE_GP_ROSTER_OK;
}
PortableGpRosterStatus portable_gp_race_roster_tick_boo_timers(PortableGpRaceRoster *r,
    size_t index,uint8_t accelerate_pressed)
{
    if(!r||index>=portable_gp_race_roster_count(r))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *d=&r->racers[index];
    if(d->item_lock_ac!=0u){
        if(d->lightning_status_6a!=0 || (d->is_player && (int16_t)d->hit_response_b6>255))
            d->item_lock_ac=0u;
        else if(!d->is_player)--d->item_lock_ac;
        else if(accelerate_pressed)d->item_lock_ac=d->item_lock_ac>9u?
            (uint16_t)(d->item_lock_ac-10u):0u;
    }
    return PORTABLE_GP_ROSTER_OK;
}
