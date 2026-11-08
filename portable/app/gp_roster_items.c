#include "gp_race_roster.h"
#include "gp_roster_math.h"
#include "kart_motion.h"
#include "driver_vector_effect.h"
#include "driver_steering.h"
#include <string.h>

PortableGpRosterStatus portable_gp_race_roster_collect_coin(
    PortableGpRaceRoster *roster, size_t racer_index)
{
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[racer_index];
    if (racer->coin_count_178 < 99u)
    {
        ++racer->coin_count_178;
        ++roster->coin_events;
    }
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_begin_item_box(
    PortableGpRaceRoster *roster, size_t racer_index,
    uint16_t item_box_kind, uint16_t roulette_seed)
{
    PortableGpRacer *racer;
    PortableGpItemStatus status;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[racer_index];
    racer->item_inventory.rules=roster->roulette_rules;
    status = portable_gp_item_begin(
        &racer->item_inventory,item_box_kind,racer->source_flags,
        racer->is_player,roulette_seed);
    if (status != PORTABLE_GP_ITEM_OK)
        return status == PORTABLE_GP_ITEM_INVALID_ARGUMENT
            ? PORTABLE_GP_ROSTER_INVALID_ARGUMENT
            : PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    ++roster->item_box_events;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_item(
    PortableGpRaceRoster *roster, size_t racer_index,
    uint8_t stop_pressed, uint16_t selected_item)
{
    PortableGpItemStatus status;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    status = portable_gp_item_tick(&roster->racers[racer_index].item_inventory,
                                   stop_pressed,selected_item);
    if (status == PORTABLE_GP_ITEM_OK) {
        ++roster->item_settlements;
        if(racer_index==roster->local_racer)
            portable_gp_item_hud_transition(&roster->item_hud,roster->item_hud.current,selected_item);
    } else if(racer_index==roster->local_racer && status==PORTABLE_GP_ITEM_ACTIVE)
        roster->item_hud.displayed_item=roster->racers[racer_index].item_inventory.preview_item;
    if (status == PORTABLE_GP_ITEM_INVALID_ARGUMENT ||
        status == PORTABLE_GP_ITEM_INELIGIBLE)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_use_star(
    PortableGpRaceRoster *roster, size_t racer_index)
{
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster) ||
        !portable_item_status_rules_valid((roster->item_status_rules)))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *owner = &roster->racers[racer_index];
    PortableGpItemUseDispatch dispatch;
    if (owner->item_inventory.held_item_17a != UINT16_C(0x100a) ||
        ((owner->source_flags & UINT32_C(0x8000)) != 0u &&
         ((owner->control & UINT16_C(0x0080)) == 0u ||
          (owner->source_flags & UINT32_C(0x00010800)) != 0u ||
          owner->item_use_state_17c != 0u)) ||
        !portable_gp_item_use_dispatch(UINT16_C(0x100a), &dispatch) ||
        dispatch.primary_callback != UINT32_C(0x080485dc))
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;

    /* 485DC: protection belongs to +6A, independently of Lightning's +68.
     * D950(actor,2) clears both bytes at +72 after its three masks. */
    owner->item_lock_ac = 0u;
    owner->hit_cue_flags_58 |= UINT16_C(0x8000);
    if(racer_index==0u)++roster->player_star_uses;
    owner->lightning_status_6a =
        (owner->source_flags & UINT32_C(0x8000)) != 0u ? (int16_t)(roster->item_status_rules)->star_player_ticks :
          (int16_t)(roster->item_status_rules)->star_cpu_ticks;
    owner->effect_state_72 = 0u;
    owner->contact_status_66 = 0;
    if ((owner->source_flags & UINT32_C(0x8000)) != 0u)
        owner->item_response_flags_b2 |= UINT16_C(0x2000);
    /* 7D6C consumes the item after the activation callback. */
    owner->item_inventory = (PortableGpItemInventory){0};
    ++roster->item_uses;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_protection(
    PortableGpRaceRoster *roster, size_t racer_index)
{
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer = &roster->racers[racer_index];
    if (racer->contact_status_66!=0) {
        const uint16_t next=(uint16_t)((uint16_t)racer->contact_status_66-1u);
        racer->contact_status_66=next<=INT16_MAX?(int16_t)next:
            (int16_t)(-1-(int32_t)(UINT16_MAX-next));
    }
    /* Both 7B30 and C4C0 decrement the halfword when its signed view is
     * nonzero, including negative values. Expiry retains only B4 bit4000. */
    if (racer->lightning_status_6a != 0) {
        const uint16_t next = (uint16_t)((uint16_t)racer->lightning_status_6a - 1u);
        racer->lightning_status_6a = next <= INT16_MAX ? (int16_t)next :
            (int16_t)(-1 - (int32_t)(UINT16_MAX - next));
        if (next != 0u)
            racer->hit_cue_flags_58 |= UINT16_C(0x8000);
        else {
            racer->mushroom_response_flags_b4 &= UINT16_C(0x4000);
            racer->hit_cue_flags_58 &= UINT16_C(0x7fff);
            racer->mushroom_mode_84 = racer->lightning_state_78;
        }
    }
    /* The local 7B30 branch also clears AC during protection or B6>255.
     * The generic C4C0 driver owns its independent AC dispatcher. */
    if ((racer->source_flags & UINT32_C(0x8000)) != 0u &&
        (racer->lightning_status_6a != 0 || (int16_t)racer->hit_response_b6 > 255))
        racer->item_lock_ac = 0u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_use_lightning(
    PortableGpRaceRoster *roster,size_t racer_index)
{
    PortableGpRacer *owner;
    PortableGpItemUseDispatch dispatch;
    if(roster==NULL||racer_index>=portable_gp_race_roster_count(roster)||
       !portable_item_status_rules_valid((roster->item_status_rules)))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    owner=&roster->racers[racer_index];
    if(owner->item_inventory.held_item_17a!=UINT16_C(0x100c)||
       (owner->is_player && (owner->control&UINT16_C(0x0080))==0u)||
       (owner->source_flags&UINT32_C(0x00000800))!=0u||
       (owner->is_player && (owner->item_use_state_17c!=0u||
        (owner->source_flags&UINT32_C(0x00010000))!=0u))||
       !portable_gp_item_use_dispatch(UINT16_C(0x100c),&dispatch)||
       dispatch.primary_callback!=UINT32_C(0x080483a0))
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;

    /* sub_8037D6C clears the item before sub_80483A0 applies Lightning's
     * immediate race-wide response. */
    owner->item_inventory.held_item_17a=PORTABLE_GP_ITEM_NONE;
    owner->item_inventory.preview_item=0u;
    owner->item_inventory.settled_item=0u;
    owner->item_inventory.active=0u;
    owner->item_response_flags_b2&=UINT16_C(0xffbf);
    roster->lightning_duration_c82=(roster->item_status_rules)->lightning_ticks;
    roster->lightning_state_80d=2u;
    roster->lightning_audio_pending=(uint8_t)(
        (roster->racers[0].source_flags&UINT32_C(0x20))==0u);

    for(size_t index=0u;index<portable_gp_race_roster_count(roster);++index){
        PortableGpRacer *victim=&roster->racers[index];
        if(index==racer_index||victim->lightning_status_6a!=0||
           victim->lightning_state_78==2u||
           victim->lightning_state_78==3u||
           victim->lightning_state_78==5u||
           victim->lightning_state_78==6u||
           victim->lightning_state_78==7u)
            continue;
        victim->mushroom_response_flags_b4|=UINT16_C(0x0300);
        victim->hit_response_b6=UINT16_C(0x1000);
        victim->hit_state_flags_74|=UINT16_C(3);
        if(victim->lightning_state_78==1u)
            victim->speed_q16=0;
        victim->lightning_timer_68=(roster->item_status_rules)->lightning_ticks;
        if(victim->is_player){
            victim->lightning_hit_pending=1u;
            roster->lightning_effect.script_tick=0u;
            roster->lightning_effect.target_index=(uint8_t)index;
            roster->lightning_effect.frame_index=0u;
            roster->lightning_effect.occupied=1u;
            roster->lightning_effect.visible=0u;
            ++roster->lightning_effect_events;
        }
    }
    ++roster->item_uses;
    ++roster->lightning_events;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_lightning_effect(
    PortableGpRaceRoster *roster)
{
    PortableGpLightningEffect *effect;
    uint16_t slot;
    int8_t frame;
    if(roster==NULL)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    effect=&roster->lightning_effect;
    if(!effect->occupied)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    if(effect->target_index>=PORTABLE_GP_RACER_COUNT||
       !roster->racers[effect->target_index].is_player)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(effect->script_tick>=56u){
        effect->occupied=0u;
        effect->visible=0u;
        return PORTABLE_GP_ROSTER_OK;
    }
    slot=(uint16_t)(effect->script_tick/4u);
    if(roster->roulette_rules==NULL)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    frame=roster->roulette_rules->lightning_slots[slot];
    effect->visible=(uint8_t)(frame>=0);
    if(frame>=0)effect->frame_index=(uint8_t)frame;
    ++effect->script_tick;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_take_lightning_audio(
    PortableGpRaceRoster *roster,PortableGpLightningAudioEvent *event)
{
    if(roster==NULL||event==NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(!roster->lightning_audio_pending)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    roster->lightning_audio_pending=0u;
    event->song_id=126u;
    event->player=10u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_lightning_timer(
    PortableGpRaceRoster *roster,size_t racer_index)
{
    PortableGpRacer *racer;
    int16_t timer;
    if(roster==NULL||racer_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[racer_index];
    timer=(int16_t)racer->lightning_timer_68;
    if(timer==1)(void)portable_driver_affine_request(&racer->affine,racer->affine_rules,4u);
    if(timer>0)
        timer=(int16_t)(uint16_t)(racer->lightning_timer_68-
                                  UINT16_C(1));
    if(timer<0)timer=0;
    racer->lightning_timer_68=(uint16_t)timer;
    if(timer==0)
        racer->hit_cue_flags_58|=UINT16_C(0x0010);
    else
        racer->hit_cue_flags_58&=UINT16_C(0xffef);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_contact_weight(
    const PortableGpRaceRoster *roster,size_t racer_index,uint32_t *weight)
{
    const PortableGpRacer *racer;
    uint8_t row;
    uint8_t small;
    if(roster==NULL||weight==NULL||racer_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[racer_index];
    if(racer->lightning_status_6a==0&&
       (racer->hit_cue_flags_58&UINT16_C(0x0040))==0u){
        *weight=UINT32_C(1);
        return PORTABLE_GP_ROSTER_OK;
    }
    if((racer->hit_cue_flags_58&UINT16_C(0x0020))!=0u){
        *weight=UINT32_C(0x00020000);
        return PORTABLE_GP_ROSTER_OK;
    }
    row=(uint8_t)(racer->hit_cue_flags_58&UINT16_C(7));
    small=(uint8_t)((racer->hit_cue_flags_58&UINT16_C(0x0010))==0u);
    if(roster->roulette_rules==NULL)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    *weight=roster->roulette_rules->contact_weight[row][small];
    /* The original Thumb sequence multiplies the local player's coin count
     * by three, then shifts left twice before adding the ROM weight. The
     * C transcription omits that final shift; see its codegen patch. */
    if ((racer->source_flags & UINT32_C(0x8000)) != 0u)
        *weight += (uint32_t)racer->coin_count_178 * 12u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_lightning_affine_scale(
    const PortableGpRaceRoster *roster,size_t racer_index,
    uint16_t normal_scale,uint16_t *affine_scale)
{
    if(roster==NULL||affine_scale==NULL||
       racer_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(roster->racers[racer_index].lightning_timer_68==0u)
        *affine_scale=normal_scale;
    else
        *affine_scale=(uint16_t)(((uint32_t)normal_scale*
                                  UINT32_C(0x180))/UINT32_C(0x100));
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpGreenShellOwner green_shell_owner(
    const PortableGpRacer *racer)
{
    PortableGpGreenShellOwner owner = {
        .rules=racer->green_shell_rules,.orbit_rules=racer->shell_orbit_rules,
        .position_x_q16 = racer->position_x_q16,
        .position_y_q16 = racer->position_y_q16,
        .position_z_q16 = racer->world_height_q16,
        .flags_48 = racer->source_flags,
        .control_96 = racer->control,
        .angle=(uint16_t)(((racer->source_flags&0x2000u)?racer->contact_heading_56:
            (uint16_t)(racer->release_angle_offset+racer->hit_spin_82))+0x8000u),
        .target_heading_56=racer->contact_heading_56,.response_status_7e=racer->hit_state_7e,
        .mode_78=racer->lightning_state_78,.decision_190=racer->item_decision_190,
        .shell_item_18c = racer->shell_item_18c,
        .speed_q16 = racer->speed_q16,
        .release_angle_offset=(racer->source_flags&0x2000u)?(int16_t)racer->contact_heading_56:racer->release_angle_offset,
        .floor_36 = racer->route_index,
        .item_use_state_17c = racer->item_use_state_17c,
    };
    return owner;
}

static void store_green_shell_owner(PortableGpRacer *racer,
                                    const PortableGpGreenShellOwner *owner)
{
    racer->source_flags = owner->flags_48;
    racer->item_decision_190=owner->decision_190;
    racer->shell_item_18c = owner->shell_item_18c;
    racer->item_use_state_17c = owner->item_use_state_17c;
}

static PortableGpRosterStatus use_triple_green_shell(PortableGpRaceRoster *roster,
    size_t racer_index,const int32_t *sine,size_t sine_count)
{
    if(!roster->shell_orbit_delay_valid)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[racer_index];
    PortableGpGreenShell shells[3];PortableGpItemInventory inventory=racer->item_inventory;
    PortableGpGreenShellOwner owner=green_shell_owner(racer);
    const PortableGpGreenShellStatus status=portable_gp_green_shell_use_triple(
        &inventory,&owner,shells,roster->shell_orbit_delay[0],sine,sine_count);
    if(status==PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_GREEN_SHELL_INELIGIBLE)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    for(unsigned i=0u;i<3u;++i){
        portable_gp_item_pool_reserve(roster);portable_gp_item_pool_retain_green(roster,racer_index);
        racer->green_shell=shells[i];racer->green_shell.creation_order=++roster->item_uses;
    }
    ++roster->triple_green_uses;
    racer->item_inventory=inventory;store_green_shell_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_orbit(
    PortableGpRaceRoster *roster,size_t actor,const int32_t *sine,size_t sine_count)
{
    if(roster==NULL || actor>=PORTABLE_GP_GREEN_ACTOR_SLOTS)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_green_actor_owner(roster,actor);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[owner_index];
    PortableGpGreenShellOwner owner=green_shell_owner(racer);
    const PortableGpGreenShellStatus status=portable_gp_green_shell_tick_orbit(
        &owner,portable_gp_green_actor_mutable(roster,actor),sine,sine_count);
    if(status==PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_GREEN_SHELL_INELIGIBLE)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_green_shell_owner(racer,&owner);
    if ((roster->race_mode_flags_7da&0x800u)!=0u && !racer->is_player &&
        portable_gp_green_actor(roster,actor)->phase==PORTABLE_GP_GREEN_SHELL_RELEASED) {
        racer->control&=UINT16_C(0xff7f);racer->item_decision_190|=1u;
    }
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_use_green_shell(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count)
{
    PortableGpGreenShellStatus status;
    PortableGpGreenShellOwner owner;
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[racer_index];
    if(racer->item_inventory.held_item_17a==PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM)
        return use_triple_green_shell(roster,racer_index,sine,sine_count);
    if(racer->green_shell.occupied && racer->green_shell.phase<=PORTABLE_GP_GREEN_SHELL_ACTIVE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    PortableGpGreenShell candidate={0};
    PortableGpItemInventory inventory=racer->item_inventory;
    owner = green_shell_owner(racer);
    status = portable_gp_green_shell_use(
        &inventory,&owner,&candidate,sine,sine_count);
    if (status == PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    portable_gp_item_pool_reserve(roster);
    portable_gp_item_pool_retain_green(roster,racer_index);
    racer->green_shell=candidate;racer->item_inventory=inventory;
    store_green_shell_owner(racer,&owner);
    racer->green_shell.creation_order=roster->item_uses+1u;
    ++roster->item_uses;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_attached(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count)
{
    PortableGpGreenShellStatus status;
    PortableGpGreenShellOwner owner;
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= PORTABLE_GP_GREEN_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_green_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[owner_index];
    PortableGpGreenShell *shell=portable_gp_green_actor_mutable(roster,racer_index);
    owner = green_shell_owner(racer);
    status = portable_gp_green_shell_tick_attached(
        &owner,shell,sine,sine_count);
    if (status == PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_green_shell_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_held_active(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count)
{
    PortableGpGreenShellStatus status;
    PortableGpGreenShellOwner owner;
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= PORTABLE_GP_GREEN_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_green_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[owner_index];
    PortableGpGreenShell *shell=portable_gp_green_actor_mutable(roster,racer_index);
    owner = green_shell_owner(racer);
    status = portable_gp_green_shell_tick_held_active(
        &owner,shell,sine,sine_count);
    if (status == PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_green_shell_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_release_green_shell(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count)
{
    PortableGpGreenShellStatus status;
    PortableGpGreenShellOwner owner;
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= PORTABLE_GP_GREEN_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_green_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[owner_index];
    PortableGpGreenShell *shell=portable_gp_green_actor_mutable(roster,racer_index);
    owner = green_shell_owner(racer);
    status = portable_gp_green_shell_release(
        &owner,shell,sine,sine_count);
    if (status == PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_green_shell_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_released(
    PortableGpRaceRoster *roster, size_t racer_index,const PortableGpGreenShellWorld *world)
{
    PortableGpGreenShellStatus status;
    PortableGpGreenShellOwner owner;
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= PORTABLE_GP_GREEN_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_green_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[owner_index];
    PortableGpGreenShell *shell=portable_gp_green_actor_mutable(roster,racer_index);
    owner = green_shell_owner(racer);
    status = portable_gp_green_shell_tick_released(
        &owner,shell,world);
    if (status == PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_green_shell_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_destroying(
    PortableGpRaceRoster *roster, size_t racer_index)
{
    PortableGpGreenShellStatus status;
    if (roster == NULL || racer_index >= PORTABLE_GP_GREEN_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    status = portable_gp_green_shell_tick_destroying(
        portable_gp_green_actor_mutable(roster,racer_index));
    if (status == PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpRedShellOwner red_shell_owner(const PortableGpRacer *racer)
{
    PortableGpRedShellOwner owner={
        .rules=racer->red_shell_rules,.orbit_rules=racer->shell_orbit_rules,
        .position_x_q16=racer->position_x_q16,
        .position_y_q16=racer->position_y_q16,
        .position_z_q16=racer->world_height_q16,
        .flags_48=racer->source_flags,.control_96=racer->control,
        .angle=(uint16_t)(((racer->source_flags&0x2000u)?racer->contact_heading_56:
            (uint16_t)(racer->release_angle_offset+racer->hit_spin_82))+0x8000u),
        .target_heading_56=racer->contact_heading_56,.response_status_7e=racer->hit_state_7e,
        .mode_78=racer->lightning_state_78,.decision_190=racer->item_decision_190,
        .shell_item_18c=racer->shell_item_18c,
        .release_angle_offset=(racer->source_flags&0x2000u)?(int16_t)racer->contact_heading_56:racer->release_angle_offset,
        .speed_q16=racer->speed_q16,.rank_flags_ba=racer->rank_flags_ba,
        .floor_36=racer->route_index,
        .item_use_state_17c=racer->item_use_state_17c,
    };return owner;
}

static void store_red_shell_owner(PortableGpRacer *racer,
                                  const PortableGpRedShellOwner *owner)
{
    racer->source_flags=owner->flags_48;
    racer->item_decision_190=owner->decision_190;
    racer->shell_item_18c=owner->shell_item_18c;
    racer->item_use_state_17c=owner->item_use_state_17c;
}

static PortableGpRosterStatus use_triple_red_shell(PortableGpRaceRoster *roster,
    size_t racer_index,const int32_t *sine,size_t sine_count)
{
    if(!roster->shell_orbit_delay_valid)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[racer_index];
    PortableGpRedShell shells[3];PortableGpItemInventory inventory=racer->item_inventory;
    PortableGpRedShellOwner owner=red_shell_owner(racer);
    const PortableGpRedShellStatus status=portable_gp_red_shell_use_triple(
        &inventory,&owner,shells,roster->shell_orbit_delay[1],sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    for(unsigned i=0u;i<3u;++i){
        portable_gp_item_pool_reserve(roster);portable_gp_item_pool_retain_red(roster,racer_index);
        racer->red_shell=shells[i];racer->red_shell.creation_order=++roster->item_uses;
    }
    ++roster->triple_red_uses;
    racer->item_inventory=inventory;store_red_shell_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_use_red_shell(
    PortableGpRaceRoster *roster,size_t racer_index,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRedShellStatus status;PortableGpRedShellOwner owner;
    PortableGpRacer *racer;
    if(roster==NULL||racer_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[racer_index];owner=red_shell_owner(racer);
    if(racer->item_inventory.held_item_17a==PORTABLE_GP_TRIPLE_RED_SHELL_ITEM)
        return use_triple_red_shell(roster,racer_index,sine,sine_count);
    if(racer->red_shell.occupied && racer->red_shell.phase<=PORTABLE_GP_RED_SHELL_ACTIVE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    PortableGpRedShell candidate={0};
    PortableGpItemInventory inventory=racer->item_inventory;
    status=portable_gp_red_shell_use(&inventory,&owner,
                                     &candidate,sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    portable_gp_item_pool_reserve(roster);
    portable_gp_item_pool_retain_red(roster,racer_index);
    racer->red_shell=candidate;racer->item_inventory=inventory;
    store_red_shell_owner(racer,&owner);
    racer->red_shell.creation_order=++roster->item_uses;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_attached(
    PortableGpRaceRoster *roster,size_t racer_index,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRedShellStatus status;PortableGpRedShellOwner owner;
    PortableGpRacer *racer;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,racer_index);owner=red_shell_owner(racer);
    status=portable_gp_red_shell_tick_attached(&owner,shell,
                                               sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_red_shell_owner(racer,&owner);return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_held_active(
    PortableGpRaceRoster *roster,size_t racer_index,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRedShellStatus status;PortableGpRedShellOwner owner;
    PortableGpRacer *racer;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,racer_index);owner=red_shell_owner(racer);
    status=portable_gp_red_shell_tick_held_active(&owner,shell,
                                                  sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_red_shell_owner(racer,&owner);
    if((owner.control_96&0x40u) || ((owner.flags_48&0x2000u) && (owner.decision_190&1u)))
        return PORTABLE_GP_ROSTER_OK;
    const int backward=(owner.flags_48&0x2000u)?(owner.decision_190&2u):(owner.control_96&0x400u);
    if(backward)return portable_gp_race_roster_release_red_shell_backward(roster,racer_index,sine,sine_count);
    if((owner.rank_flags_ba>>1u)==0u)
        return portable_gp_race_roster_release_red_shell_forward_leader(roster,racer_index,sine,sine_count);
    return portable_gp_race_roster_release_red_shell_forward_target(roster,racer_index,sine,sine_count);
}

PortableGpRosterStatus portable_gp_race_roster_release_red_shell_backward(
    PortableGpRaceRoster *roster,size_t racer_index,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRedShellStatus status;PortableGpRedShellOwner owner;
    PortableGpRacer *racer;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,racer_index);owner=red_shell_owner(racer);
    status=portable_gp_red_shell_release_backward(&owner,shell,
                                                  sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_red_shell_owner(racer,&owner);return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_release_red_shell_forward_leader(
    PortableGpRaceRoster *roster,size_t racer_index,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRedShellStatus status;PortableGpRedShellOwner owner;
    PortableGpRacer *racer;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,racer_index);owner=red_shell_owner(racer);
    if ((roster->race_mode_flags_7da&0x800u)!=0u) owner.rank_flags_ba=0u;
    PortableGpRedShell candidate=*shell;
    const uint8_t orbit_release=(uint8_t)(candidate.phase==PORTABLE_GP_RED_SHELL_ORBITING);
    if(orbit_release){
        candidate.phase=PORTABLE_GP_RED_SHELL_ACTIVE;
        owner.control_96&=UINT16_C(0xfbbf);owner.decision_190&=UINT8_C(0xfd);
    }
    status=portable_gp_red_shell_release_forward_leader(
        &owner,&candidate,sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    candidate.retire_held_slot=0u;*shell=candidate;
    if(orbit_release)owner.decision_190=racer->item_decision_190;
    store_red_shell_owner(racer,&owner);return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_release_red_shell_forward_target(
    PortableGpRaceRoster *roster,size_t racer_index,
    const int32_t *sine,size_t sine_count)
{
    /* 4676C clears7C in Battle irrespective of rank. Acquisition belongs
     * to the next44D4C callback, not GP's preceding-place selector. */
    if (roster!=NULL && (roster->race_mode_flags_7da&0x800u)!=0u)
        return portable_gp_race_roster_release_red_shell_forward_leader(roster,racer_index,sine,sine_count);
    PortableGpRedShellStatus status;PortableGpRedShellOwner owner;
    PortableGpRacer *racer;uint16_t target_rank;size_t target_index;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,racer_index);owner=red_shell_owner(racer);
    PortableGpRedShell candidate=*shell;
    const uint8_t orbit_release=(uint8_t)(candidate.phase==PORTABLE_GP_RED_SHELL_ORBITING);
    if(orbit_release){
        candidate.phase=PORTABLE_GP_RED_SHELL_ACTIVE;
        owner.control_96&=UINT16_C(0xfbbf);owner.decision_190&=UINT8_C(0xfd);
    }
    if((owner.rank_flags_ba>>1)==0u)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    target_rank=shell->kind_10==PORTABLE_GP_SPINY_SHELL_KIND?0u:
        (uint16_t)((owner.rank_flags_ba>>1)-1u);
    for(target_index=0u;target_index<PORTABLE_GP_RACER_COUNT;++target_index)
        if((roster->racers[target_index].rank_flags_ba>>1)==target_rank)break;
    if(target_index==PORTABLE_GP_RACER_COUNT)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    status=portable_gp_red_shell_release_forward_target(
        &owner,&candidate,(uint8_t)target_index,sine,sine_count);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    candidate.retire_held_slot=0u;*shell=candidate;
    if(orbit_release)owner.decision_190=racer->item_decision_190;
    store_red_shell_owner(racer,&owner);return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_actor(
    PortableGpRaceRoster *roster,size_t index,const PortableGpRedShellWorld *world,
    const PortableGpRedShellRoute *route)
{
    if(roster==NULL || index>=PORTABLE_GP_RED_ACTOR_SLOTS || world==NULL || route==NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *owner=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,index);
    switch(shell->phase){
    case PORTABLE_GP_RED_SHELL_ORBIT_PENDING:
    case PORTABLE_GP_RED_SHELL_ORBITING:{
        const uint8_t was_orbiting=(uint8_t)(shell->phase==PORTABLE_GP_RED_SHELL_ORBITING);
        const PortableGpRedShellOwner view=red_shell_owner(owner);
        const PortableGpRedShellStatus status=portable_gp_red_shell_tick_orbit(&view,shell,world->sine,world->sine_count);
        if(status!=PORTABLE_GP_RED_SHELL_OK)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        if(was_orbiting && (owner->control&0x80u) && !(owner->source_flags&0x10800u)) {
            const PortableGpRosterStatus released=(owner->rank_flags_ba>>1u)==0u?
                portable_gp_race_roster_release_red_shell_forward_leader(roster,index,world->sine,world->sine_count):
                portable_gp_race_roster_release_red_shell_forward_target(roster,index,world->sine,world->sine_count);
            if (released==PORTABLE_GP_ROSTER_OK && (roster->race_mode_flags_7da&0x800u)!=0u && !owner->is_player) {
                owner->control&=UINT16_C(0xff7f);owner->item_decision_190|=1u;
            }
            return released;
        }
        return PORTABLE_GP_ROSTER_OK;
    }
    case PORTABLE_GP_RED_SHELL_ATTACHED:
        return portable_gp_race_roster_tick_red_shell_attached(roster,index,world->sine,world->sine_count);
    case PORTABLE_GP_RED_SHELL_ACTIVE:
        return portable_gp_race_roster_tick_red_shell_held_active(roster,index,world->sine,world->sine_count);
    case PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE:
        return portable_gp_race_roster_tick_red_shell_destroying(roster,index);
    default:break;
    }
    if(!shell->occupied)return PORTABLE_GP_ROSTER_OK;
    PortableGpRedShellTarget targets[PORTABLE_GP_RACER_COUNT];
    for(unsigned i=0u;i<portable_gp_race_roster_count(roster);++i){
        const PortableGpRacer *r=&roster->racers[i];
        targets[i]=(PortableGpRedShellTarget){r->position_x_q16,r->position_y_q16,
            r->world_height_q16,r->source_flags,r->contact_status_66,r->route_index,(uint8_t)(r->rank_flags_ba>>1u)};
    }
    const int forward=shell->phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET ||
        shell->phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;
    PortableGpRedShellWorld actor_world=*world;
    actor_world.mode_flags=roster->race_mode_flags_7da;
    actor_world.owner_index=(uint8_t)owner_index;
    const size_t count=portable_gp_race_roster_count(roster);
    const PortableGpRedShellStatus status=forward?
        portable_gp_red_shell_tick_forward(shell,&owner->source_flags,&actor_world,route,targets,count):
        portable_gp_red_shell_tick_trap(shell,&owner->source_flags,&actor_world,targets,count);
    return status==PORTABLE_GP_RED_SHELL_OK?PORTABLE_GP_ROSTER_OK:
        status==PORTABLE_GP_RED_SHELL_INELIGIBLE?PORTABLE_GP_ROSTER_ITEM_INELIGIBLE:PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
}

PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_destroying(
    PortableGpRaceRoster *roster,size_t racer_index)
{
    PortableGpRedShellStatus status;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    status=portable_gp_red_shell_tick_destroying(
        portable_gp_red_actor_mutable(roster,racer_index));
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_collide_red_shell(
    PortableGpRaceRoster *roster,size_t racer_index,uint16_t other_kind,
    int32_t other_z_q16,int16_t other_field_66)
{
    PortableGpRedShellStatus status;PortableGpRacer *racer;
    if(roster==NULL||racer_index>=PORTABLE_GP_RED_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_red_actor_owner(roster,racer_index);
    if(owner_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer=&roster->racers[owner_index];
    PortableGpRedShell *shell=portable_gp_red_actor_mutable(roster,racer_index);
    if(shell->phase==PORTABLE_GP_RED_SHELL_ACTIVE || shell->phase==PORTABLE_GP_RED_SHELL_ORBITING){
        PortableGpRedShellOwner owner=red_shell_owner(racer);
        status=portable_gp_red_shell_collide_held(shell,&owner,
            other_kind,other_z_q16,other_field_66);
        store_red_shell_owner(racer,&owner);
    }else status=portable_gp_red_shell_collide(shell,
        &racer->source_flags,other_kind,other_z_q16,other_field_66);
    if(status==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    return PORTABLE_GP_ROSTER_OK;
}

static size_t hit_sine_index(uint16_t angle)
{
    int32_t signed_angle=(int16_t)angle;
    if(signed_angle<0)signed_angle+=INT32_C(0x3f);
    return (size_t)((signed_angle>>6)&INT32_C(0x3ff));
}

static int16_t hit_component(int32_t sine_value,uint16_t strength)
{
    int64_t product=(int64_t)sine_value*(int64_t)strength;
    if(product<0)product+=INT64_C(0x7fff);
    return (int16_t)(product>>15);
}

PortableGpRosterStatus portable_gp_race_roster_resolve_shell_victim(
    PortableGpRaceRoster *roster,size_t victim_index,
    int32_t shell_x_q16,int32_t shell_y_q16,int32_t shell_z_q16,
    uint16_t shell_state_50,uint8_t speed_byte,
    uint8_t alternate_race_state,uint8_t side_test_nonzero,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRacer *victim;int32_t delta_x,delta_y;
    uint16_t angle,strength;size_t index;
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster)||
       sine==NULL||sine_count<1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    victim=&roster->racers[victim_index];
    /* F8C8 performs the damage guard independently of445CC. A protected
     * kart destroys the shell but receives neither impulse nor coin loss. */
    if((victim->contact_status_60|shell_state_50)!=0u||shell_z_q16>0x41fff||
       victim->lightning_state_78>1u||
       victim->world_height_q16>0x41fff||victim->contact_status_66!=0)
        return PORTABLE_GP_ROSTER_OK;
    if(victim->lightning_status_6a!=0){
        victim->red_shell_hit_audio_pending=2u;
        return PORTABLE_GP_ROSTER_OK;
    }
    if (victim->elimination.revenge_bomb) {
        /* F8C8 writes the contact hold before E7F0, which does not launch
         * ordinary damage or restore balloons. The shell still shatters. */
        victim->kart_contact_state_5a=UINT16_C(0x8007);
        victim->source_flags|=UINT32_C(0x1000);
        victim->banana_hit_timer_38=8u;
        return portable_battle_race_roster_detonate_bomb(roster,victim_index);
    }
    const unsigned rank=victim->rank_flags_ba>>1u;
    if(rank>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    uint32_t ranked_flags[7];
    for(unsigned i=0u;i<rank;++i){
        if(roster->standings[i]>=8u)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        ranked_flags[i]=roster->racers[roster->standings[i]].source_flags;
    }
    const PortableGpRosterStatus damage=portable_gp_race_roster_begin_kart_hit(
        victim,NULL,alternate_race_state,ranked_flags,rank,sine,sine_count);
    if(damage!=PORTABLE_GP_ROSTER_OK)return damage;
    if(side_test_nonzero)victim->hit_response_b6=0x1000u;
    delta_x=fixed_to_int((int32_t)((uint32_t)victim->position_x_q16-
                                  (uint32_t)shell_x_q16));
    delta_y=fixed_to_int((int32_t)((uint32_t)victim->position_y_q16-
                                  (uint32_t)shell_y_q16));
    angle=(delta_x==0&&delta_y==0)?0u:
        portable_gp_red_shell_target_angle((int16_t)delta_x,(int16_t)delta_y);
    strength=(uint16_t)((uint16_t)speed_byte<<3);
    if(strength<=UINT16_C(0x0180))strength=UINT16_C(0x0180);
    else if(strength>UINT16_C(0x03ff))strength=UINT16_C(0x0400);
    if(strength!=UINT16_C(0x0180))
        angle=(uint16_t)(angle+(alternate_race_state?
                         UINT16_C(0x4000):UINT16_C(0xc000)));
    index=hit_sine_index(angle);
    victim->component_x=hit_component(sine[index],strength);
    victim->component_y=hit_component(
        -sine[(UINT16_C(0x0100)-(uint16_t)index)&UINT16_C(0x03ff)],strength);
    victim->red_shell_hit_audio_pending=1u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_resolve_green_shell_kart_hit(
    PortableGpRaceRoster *roster,size_t owner_index,size_t victim_index,
    uint8_t alternate_race_state,uint8_t side_test_nonzero,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRacer *owner;PortableGpGreenShell *shell;
    PortableGpGreenShellOwner owner_view;PortableGpGreenShellStatus status;
    PortableGpRosterStatus victim_status;
    if(roster==NULL||owner_index>=PORTABLE_GP_GREEN_ACTOR_SLOTS||
       victim_index>=portable_gp_race_roster_count(roster)||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(roster->racers[victim_index].contact_status_66!=0)
        return PORTABLE_GP_ROSTER_OK;
    shell=portable_gp_green_actor_mutable(roster,owner_index);
    const size_t racer_index=portable_gp_green_actor_owner(roster,owner_index);
    if(racer_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    owner=&roster->racers[racer_index];
    owner_view=green_shell_owner(owner);
    const uint16_t kind=(uint16_t)(roster->racers[victim_index].is_player?0x2001u:0x2002u);
    const int32_t height=roster->racers[victim_index].world_height_q16;
    if(shell->phase==PORTABLE_GP_GREEN_SHELL_ACTIVE || shell->phase==PORTABLE_GP_GREEN_SHELL_ORBITING)
        status=portable_gp_green_shell_collide_held(shell,&owner_view,kind,height,0);
    else status=portable_gp_green_shell_collide(shell,&owner_view.flags_48,
        kind,height,0,0u);
    if(status==PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_green_shell_owner(owner,&owner_view);
    if(shell->collision_result!=
       PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING)
        return PORTABLE_GP_ROSTER_OK;
    if(shell->collision_state_50!=0u)return PORTABLE_GP_ROSTER_OK;
    victim_status=portable_gp_race_roster_resolve_shell_victim(roster,victim_index,
        shell->position_x_q16,shell->position_y_q16,shell->position_z_q16,
        shell->collision_state_50,shell->state_71,
        alternate_race_state,side_test_nonzero,sine,sine_count);
    if(victim_status==PORTABLE_GP_ROSTER_OK &&
       roster->racers[victim_index].red_shell_hit_audio_pending==1u)++roster->green_shell_hits;
    return victim_status;
}

PortableGpRosterStatus portable_gp_race_roster_resolve_red_shell_kart_hit(
    PortableGpRaceRoster *roster,size_t owner_index,size_t victim_index,
    uint8_t alternate_race_state,uint8_t side_test_nonzero,
    const int32_t *sine,size_t sine_count)
{
    PortableGpRacer *owner;PortableGpRedShell *shell;
    PortableGpRosterStatus status;
    if(roster==NULL||owner_index>=PORTABLE_GP_RED_ACTOR_SLOTS||
       victim_index>=portable_gp_race_roster_count(roster)||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(roster->racers[victim_index].contact_status_66!=0)
        return PORTABLE_GP_ROSTER_OK;
    const size_t racer_index=portable_gp_red_actor_owner(roster,owner_index);
    if(racer_index>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    owner=&roster->racers[racer_index];
    shell=portable_gp_red_actor_mutable(roster,owner_index);
    PortableGpRacer *victim=&roster->racers[victim_index];
    const uint16_t kind=(uint16_t)(victim->is_player?0x2001u:0x2002u);
    if(shell->kind_10==PORTABLE_GP_SPINY_SHELL_KIND && shell->phase!=PORTABLE_GP_RED_SHELL_ACTIVE){
        const PortableGpRedShellStatus hit=portable_gp_spiny_shell_collide(shell,&owner->source_flags,
            kind,victim->world_height_q16,0,(uint8_t)(victim->rank_flags_ba>>1u),victim->lightning_status_6a);
        status=hit==PORTABLE_GP_RED_SHELL_OK?PORTABLE_GP_ROSTER_OK:PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    }else status=portable_gp_race_roster_collide_red_shell(roster,owner_index,kind,victim->world_height_q16,0);
    if(status!=PORTABLE_GP_ROSTER_OK)return status;
    if(shell->collision_result!=PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING &&
       !(shell->kind_10==PORTABLE_GP_SPINY_SHELL_KIND && shell->collision_result==PORTABLE_GP_RED_SHELL_COLLISION_IGNORED &&
         shell->position_z_q16<=0x41fff && victim->world_height_q16<=0x41fff))
        return PORTABLE_GP_ROSTER_OK;

    if(shell->kind_10==PORTABLE_GP_SPINY_SHELL_KIND){
        shell->velocity_x_q16=(victim->component_x>256?victim->component_x:256)*128;
        shell->velocity_y_q16=(victim->component_y>256?victim->component_y:256)*128;
        shell->lifetime_38=8u;
    }
    if(shell->collision_state_50!=0u)return PORTABLE_GP_ROSTER_OK;
    status=portable_gp_race_roster_resolve_shell_victim(roster,victim_index,
        shell->position_x_q16,shell->position_y_q16,shell->position_z_q16,
        shell->collision_state_50,shell->speed_byte_71,
        alternate_race_state,side_test_nonzero,sine,sine_count);
    if(status==PORTABLE_GP_ROSTER_OK &&
       roster->racers[victim_index].red_shell_hit_audio_pending==1u)++roster->red_shell_hits;
    return status;
}

PortableGpRosterStatus portable_gp_race_roster_take_red_shell_hit_audio(
    PortableGpRaceRoster *roster,size_t victim_index,uint8_t suppressed,
    uint16_t spatial_volume,PortableGpRedShellAudioEvent *event)
{
    PortableGpRacer *victim;
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster)||event==NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    victim=&roster->racers[victim_index];
    if(!victim->red_shell_hit_audio_pending)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    const uint8_t guarded=victim->red_shell_hit_audio_pending==2u;
    victim->red_shell_hit_audio_pending=0u;
    if(suppressed||spatial_volume==0u)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    event->song_id=guarded?124u:132u;event->player=4u;event->volume=spatial_volume;
    return PORTABLE_GP_ROSTER_OK;
}

static PortableGpBananaOwner banana_owner(const PortableGpRacer *racer)
{
    PortableGpBananaOwner owner = {
        .rules=racer->banana_rules,
        .position_x_q16 = racer->position_x_q16,
        .position_y_q16 = racer->position_y_q16,
        .position_z_q16 = racer->world_height_q16,
        .flags_48 = racer->source_flags,
        .control_96 = racer->control,
        .angle = (uint16_t)(((racer->source_flags&0x2000u)?racer->contact_heading_56:
            (uint16_t)(racer->release_angle_offset+racer->hit_spin_82))+UINT16_C(0x8000)),
        .release_angle=(racer->source_flags&0x2000u)?racer->contact_heading_56:(uint16_t)racer->release_angle_offset,
        .target_heading_56=racer->contact_heading_56,
        .response_status_7e=racer->hit_state_7e,.mode_78=racer->lightning_state_78,
        .decision_190=racer->item_decision_190,.route_row_191=racer->route_row_191,
        .shell_item_18c = racer->shell_item_18c,
        .floor_36 = racer->route_index,
        .item_use_state_17c = racer->item_use_state_17c,
    };
    return owner;
}

static void store_banana_owner(PortableGpRacer *racer,
                               const PortableGpBananaOwner *owner)
{
    racer->source_flags = owner->flags_48;
    racer->item_decision_190=owner->decision_190;
    racer->shell_item_18c = owner->shell_item_18c;
    racer->item_use_state_17c = owner->item_use_state_17c;
}

PortableGpRosterStatus portable_gp_race_roster_use_banana(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count)
{
    PortableGpBananaStatus status;
    PortableGpBananaOwner owner;
    PortableGpRacer *racer;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[racer_index];
    owner = banana_owner(racer);
    if(racer->banana.occupied && racer->banana.phase==PORTABLE_GP_BANANA_HELD)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    PortableGpBanana candidate={0};
    PortableGpItemInventory inventory=racer->item_inventory;
    status = portable_gp_banana_use(
        &inventory,&owner,&candidate,sine,sine_count);
    if (status == PORTABLE_GP_BANANA_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (status == PORTABLE_GP_BANANA_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    portable_gp_item_pool_reserve(roster);
    portable_gp_item_pool_retain_banana(roster,racer_index);
    racer->banana=candidate;racer->item_inventory=inventory;
    store_banana_owner(racer,&owner);
    racer->banana.creation_order=++roster->item_uses;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_banana(
    PortableGpRaceRoster *roster,size_t racer_index,const PortableGpBananaWorld *world,
    const int32_t *sine,size_t sine_count)
{
    if(roster==NULL || racer_index>=PORTABLE_GP_BANANA_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    const size_t owner_index=portable_gp_banana_actor_owner(roster,racer_index);
    if(owner_index>=8u)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[owner_index];
    PortableGpBananaOwner owner=banana_owner(racer);
    const PortableGpBananaStatus status=portable_gp_banana_tick(&owner,portable_gp_banana_actor_mutable(roster,racer_index),world,sine,sine_count);
    if(status==PORTABLE_GP_BANANA_INVALID_ARGUMENT)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_BANANA_INELIGIBLE)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    store_banana_owner(racer,&owner);
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_resolve_banana_kart_hit(
    PortableGpRaceRoster *roster,size_t owner_index,size_t victim_index)
{
    return portable_gp_race_roster_resolve_banana_kart_hit_with_context(
        roster,owner_index,victim_index,0u,UINT16_C(0x0100));
}

PortableGpRosterStatus portable_gp_race_roster_resolve_banana_kart_hit_with_context(
    PortableGpRaceRoster *roster,size_t owner_index,size_t victim_index,
    uint32_t scene_clock,uint16_t spatial_volume)
{
    if(roster==NULL||owner_index>=PORTABLE_GP_BANANA_ACTOR_SLOTS||
       victim_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(roster->racers[victim_index].contact_status_66!=0)return PORTABLE_GP_ROSTER_OK;
    const size_t semantic_owner=portable_gp_banana_actor_owner(roster,owner_index);
    if(semantic_owner>=8u)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *owner=&roster->racers[semantic_owner];
    PortableGpBanana *banana=portable_gp_banana_actor_mutable(roster,owner_index);
    const PortableGpRacer *victim=&roster->racers[victim_index];
    PortableGpBananaStatus status;
    if(banana->phase==PORTABLE_GP_BANANA_HELD){
        PortableGpBananaOwner view=banana_owner(owner);
        status=portable_gp_banana_collide_held(banana,&view,
            victim->is_player?UINT16_C(0x2001):UINT16_C(0x2002),victim->contact_status_66);
        store_banana_owner(owner,&view);
    }else status=portable_gp_banana_collide(banana,&owner->source_flags,
        victim->is_player?UINT16_C(0x2001):UINT16_C(0x2002),victim->contact_status_66);
    if(status==PORTABLE_GP_BANANA_INVALID_ARGUMENT)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_BANANA_INELIGIBLE)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    return portable_gp_race_roster_resolve_banana_victim(roster,victim_index,
        banana->position_z_q16,banana->collision_state_50,
        scene_clock,spatial_volume);
}

PortableGpRosterStatus portable_gp_race_roster_resolve_banana_victim(
    PortableGpRaceRoster *roster,size_t victim_index,int32_t item_height,
    uint16_t item_state_50,uint32_t scene_clock,uint16_t spatial_volume)
{
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *victim=&roster->racers[victim_index];
    if(victim->contact_status_60 || item_state_50 || victim->lightning_state_78>1u ||
       victim->world_height_q16>INT32_C(0x41fff) || item_height>INT32_C(0x41fff) ||
       victim->contact_status_66!=0)return PORTABLE_GP_ROSTER_OK;
    if(victim->lightning_status_6a!=0){
        victim->banana_reaction_audio_pending=2u;
        victim->banana_reaction_volume=spatial_volume;
        victim->banana_reaction_pitch=0;
        return PORTABLE_GP_ROSTER_OK;
    }
    if(victim->elimination.revenge_bomb){
        /* F8C8/F994 admits the contact hold, then E7F0 detonates the
         * typed Battle bomb before ordinary banana damage is dispatched. */
        victim->kart_contact_state_5a=UINT16_C(0x8007);
        victim->source_flags|=UINT32_C(0x1000);
        victim->banana_hit_timer_38=8u;
        return portable_battle_race_roster_detonate_bomb(roster,victim_index);
    }
    ++roster->banana_hits;
    /* sub_803F654 always sets +0x38 to three. Its source-flags 0x2000 route
     * uses sub_803E490's generic response entry, then clears both impulse
     * halfwords before returning. Audio from that route is a separate card. */
    if((victim->source_flags&UINT32_C(0x00002000))!=0u){
        const unsigned count=victim->rank_flags_ba>>1u;
        uint32_t ranked_flags[7];unsigned seen=0u;
        if(count>7u)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        for(unsigned rank=0u;rank<portable_gp_race_roster_count(roster);++rank){
            const unsigned index=roster->standings[rank];
            if(index>=8u || (seen&(1u<<index)))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            seen|=1u<<index;
            if(rank<count)ranked_flags[rank]=roster->racers[index].source_flags;
        }
        const PortableGpRosterStatus hit=portable_gp_race_roster_begin_kart_hit(
            victim,NULL,scene_clock,ranked_flags,count,NULL,0u);
        if(hit!=PORTABLE_GP_ROSTER_OK)return hit;
        victim->banana_hit_timer_38=3u;
        victim->kart_contact_state_5a=7u;
        victim->component_x=0;
        victim->component_y=0;
        if(victim->is_player&&
           (victim->source_flags&UINT32_C(0x00000020))==0u)
            victim->banana_special_entry_audio_pending=1u;
        if((roster->racers[0].source_flags&UINT32_C(0x00000020))==0u){
            const unsigned variant=(victim->hit_cue_flags_58&
                UINT16_C(0x0010))!=0u?0u:1u;
            if(roster->hazard_sound_rules==NULL)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
            victim->banana_special_class_song=roster->hazard_sound_rules->impact_voices
                [portable_gp_racer_voice_class(victim)][victim->is_player?0u:1u][variant];
            victim->banana_special_class_player=victim->is_player?2u:4u;
            victim->banana_special_class_volume=victim->is_player?
                UINT16_C(0x0100):(spatial_volume<UINT16_C(0x0040)?
                UINT16_C(0x0040):spatial_volume);
            victim->banana_special_class_audio_pending=1u;
        }
        return PORTABLE_GP_ROSTER_OK;
    }
    victim->banana_hit_timer_38=3u;
    victim->kart_contact_state_5a=7u;
    victim->source_flags|=UINT32_C(0x1000);
    /* The ordinary route enters banana response through field-b4 bit 0x1000. */
    victim->mushroom_response_flags_b4|=UINT16_C(0x1000);
    if(victim->is_player&&(victim->source_flags&UINT32_C(0x20))==0u){
        uint32_t magnitude=(uint32_t)victim->speed_q16;
        if(victim->speed_q16<0)magnitude=UINT32_C(0)-magnitude;
        victim->banana_reaction_pitch=(int16_t)(uint16_t)(
            ((magnitude<<6)>>22)-UINT32_C(0x0200));
        victim->banana_reaction_volume=(uint16_t)(
            ((magnitude>>20)&UINT32_C(0x3f))+UINT32_C(0x00c0));
        victim->banana_reaction_audio_pending=1u;
    }
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_take_banana_reaction_audio(
    PortableGpRaceRoster *roster,size_t victim_index,uint8_t suppressed,
    PortableGpBananaAudioEvent *event)
{
    PortableGpRacer *victim;
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster)||event==NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    victim=&roster->racers[victim_index];
    if(!victim->banana_reaction_audio_pending)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    const uint8_t protected_hit=victim->banana_reaction_audio_pending==2u;
    victim->banana_reaction_audio_pending=0u;
    if(suppressed || (protected_hit&&!victim->banana_reaction_volume))return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    event->song_id=protected_hit?124u:135u;event->player=protected_hit?4u:6u;
    event->volume=victim->banana_reaction_volume;
    event->pitch=victim->banana_reaction_pitch;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_take_banana_special_entry_audio(
    PortableGpRaceRoster *roster,size_t victim_index,uint8_t suppressed,
    PortableGpBananaAudioEvent *event)
{
    PortableGpRacer *victim;
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster)||event==NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    victim=&roster->racers[victim_index];
    if(!victim->banana_special_entry_audio_pending)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    victim->banana_special_entry_audio_pending=0u;
    if(suppressed)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    event->song_id=162u;event->player=5u;
    event->volume=UINT16_C(0x0100);event->pitch=0;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_take_banana_special_class_audio(
    PortableGpRaceRoster *roster,size_t victim_index,uint8_t suppressed,
    PortableGpBananaAudioEvent *event)
{
    PortableGpRacer *victim;
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster)||event==NULL)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    victim=&roster->racers[victim_index];
    if(!victim->banana_special_class_audio_pending)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    victim->banana_special_class_audio_pending=0u;
    if(suppressed)return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    event->song_id=victim->banana_special_class_song;
    event->player=victim->banana_special_class_player;
    event->volume=victim->banana_special_class_volume;event->pitch=0;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_banana_destroying(
    PortableGpRaceRoster *roster,size_t owner_index)
{
    PortableGpBananaStatus status;
    if(roster==NULL||owner_index>=PORTABLE_GP_BANANA_ACTOR_SLOTS)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    status=portable_gp_banana_tick_destroying(
        portable_gp_banana_actor_mutable(roster,owner_index));
    if(status==PORTABLE_GP_BANANA_INVALID_ARGUMENT)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if(status==PORTABLE_GP_BANANA_INELIGIBLE)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    return PORTABLE_GP_ROSTER_OK;
}

static int16_t settle_banana_hit_field_0400(int16_t value)
{
    uint16_t next=(uint16_t)value;
    if(value<0){
        next=(uint16_t)(next+UINT16_C(0x0400));
        return (int16_t)next>=0?0:(int16_t)next;
    }
    if(value>0){
        next=(uint16_t)(next-UINT16_C(0x0400));
        return (int16_t)next<0?0:(int16_t)next;
    }
    return 0;
}

PortableGpRosterStatus portable_gp_race_roster_tick_banana_hit_response(
    PortableGpRaceRoster *roster,size_t victim_index)
{
    PortableGpRacer *victim;uint16_t angle;
    if(roster==NULL||victim_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    victim=&roster->racers[victim_index];
    if((victim->mushroom_response_flags_b4&UINT16_C(0x1000))!=0u){
        /* sub_803A9AC's 0x1000 branch enters state 5/6 and clears both
         * response bits. This card owns the ordinary nonnegative state-6
         * route produced by the C28 entry. */
        if(victim->speed_q16>INT32_C(0x03800000))
            victim->speed_q16=INT32_C(0x03800000);
        victim->mushroom_mode_84=0u;
        victim->banana_spin_timer_c8=60u;
        victim->hit_state_7e=victim->hit_spin_82<0?5u:6u;
        victim->hit_angle_80=0u;
        victim->hit_counter_88=0u;
        victim->banana_ground_timer_b8=60u;
        victim->mushroom_response_flags_b4&=UINT16_C(0xe7ff);
    }else if(victim->hit_state_7e==14u){
        /* State 14 calls sub_8039690 with row zero. _080EBE28+0x0c is
         * 0x00e0, which settles field 0x82 toward zero before a zero field
         * 0x80 returns the dispatcher to state zero. */
        int32_t settled=victim->hit_spin_82;
        if(settled>0){settled-=INT32_C(0x00e0);if(settled<0)settled=0;}
        else if(settled<0){settled+=INT32_C(0x00e0);if(settled>0)settled=0;}
        victim->hit_spin_82=(int16_t)settled;
        if(victim->hit_angle_80==0u)victim->hit_state_7e=0u;
        else if((int16_t)(uint16_t)(victim->hit_angle_80+
                UINT16_C(0x4000))<0){
            victim->hit_state_7e=11u;
            victim->mushroom_mode_84=11u;
        }
        return PORTABLE_GP_ROSTER_OK;
    }else if(victim->hit_state_7e==11u){
        /* sub_8038E34 case 11 treats field-0x96 bit 0x8000 as grounded.
         * At lower speed it clears only field 0x80. Above the separate
         * grounded threshold, fields 0x82 and 0x80 settle toward zero by
         * signed wrapped 0x0400 steps; field 0x80 reaching zero returns to
         * state 14 and clears mode 0x84. */
        if((victim->hit_ground_flags_96&UINT16_C(0x8000))!=0u){
            if(victim->speed_q16<=INT32_C(0x00ffffff)){
                victim->hit_angle_80=0u;
                return PORTABLE_GP_ROSTER_OK;
            }
            victim->hit_spin_82=settle_banana_hit_field_0400(
                victim->hit_spin_82);
            victim->hit_angle_80=(uint16_t)settle_banana_hit_field_0400(
                (int16_t)victim->hit_angle_80);
            if(victim->hit_angle_80==0u){
                victim->hit_state_7e=14u;
                victim->mushroom_mode_84=0u;
            }
            return PORTABLE_GP_ROSTER_OK;
        }
        if(victim->speed_q16>INT32_C(0x000fffff))
            return PORTABLE_GP_ROSTER_OK;
        victim->hit_angle_80=0u;
        victim->hit_state_7e=14u;
        victim->mushroom_mode_84=0u;
        return PORTABLE_GP_ROSTER_OK;
    }else if(victim->hit_state_7e!=5u&&victim->hit_state_7e!=6u){
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    }
    victim->mushroom_response_flags_b4|=UINT16_C(8);
    if(victim->banana_ground_timer_b8!=0u)
        --victim->banana_ground_timer_b8;
    if((victim->mushroom_response_flags_b4&UINT16_C(2))!=0u){
        /* Both sub_8038E34 case 5 and case 6 bypass their spin timers when
         * field-b4 bit 2 is present, set bit 0x0400, and enter state 14. */
        victim->mushroom_response_flags_b4|=UINT16_C(0x0400);
        victim->hit_state_7e=14u;
        return PORTABLE_GP_ROSTER_OK;
    }
    angle=(uint16_t)victim->hit_spin_82;
    if(victim->banana_spin_timer_c8!=0u){
        --victim->banana_spin_timer_c8;
        victim->hit_spin_82=(int16_t)(uint16_t)(victim->hit_state_7e==5u?
            angle-UINT16_C(0x0a00):angle+UINT16_C(0x0a00));
        return PORTABLE_GP_ROSTER_OK;
    }
    victim->hit_spin_82=(int16_t)(uint16_t)(victim->hit_state_7e==5u?
        angle-UINT16_C(0x0a00):angle+UINT16_C(0x0a00));
    if(victim->speed_q16<=INT32_C(0x02ffffff)&&
       (victim->hit_state_7e==5u?angle:
        (uint16_t)victim->hit_spin_82)<=UINT16_C(0x09ff)){
        victim->mushroom_response_flags_b4|=UINT16_C(0x0400);
        victim->hit_state_7e=14u;
    }
    return PORTABLE_GP_ROSTER_OK;
}

/* 37D6C -> 37F38 calls48134 (bx lr), then37F46 clears17A.
 * Allocation still happens before dispatch; the inert node has no turn,
 * drawing or collision callback and stays allocated until pool reuse. */
PortableGpRosterStatus portable_gp_race_roster_use_reserved_item(
    PortableGpRaceRoster *roster, size_t racer_index)
{
    if(roster==NULL || racer_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[racer_index];
    PortableGpItemUseDispatch dispatch;
    if(!portable_gp_item_use_dispatch(racer->item_inventory.held_item_17a,&dispatch) ||
       dispatch.primary_callback!=UINT32_C(0x08048134) ||
       !(racer->control&UINT16_C(0x0080)) ||
       (racer->source_flags&UINT32_C(0x00010800)) || racer->item_use_state_17c)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    portable_gp_item_pool_reserve(roster);
    const uint32_t order=++roster->item_uses;
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i){
        PortableGpMushroomActor *actor=&roster->mushroom_pool[i];
        if(actor->occupied)continue;
        *actor=(PortableGpMushroomActor){.creation_order=order,
            .owner=(uint8_t)racer_index,.occupied=1u};
        break;
    }
    if(racer->is_player)++roster->player_reserved_uses;
    else ++roster->cpu_reserved_uses;
    racer->item_inventory.held_item_17a=0u;
    racer->item_inventory.preview_item=0u;
    racer->item_inventory.settled_item=0u;
    racer->item_inventory.active=0u;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_use_mushroom(
    PortableGpRaceRoster *roster, size_t racer_index)
{
    PortableGpRacer *racer;
    PortableGpItemUseDispatch dispatch;
    if (roster == NULL || racer_index >= portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    racer = &roster->racers[racer_index];
    if(!portable_mushroom_rules_valid(racer->mushroom_rules))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    if (!portable_gp_item_use_dispatch(
            racer->item_inventory.held_item_17a,&dispatch) ||
        (dispatch.primary_callback != UINT32_C(0x08048340) &&
         dispatch.primary_callback != UINT32_C(0x08048370) &&
         dispatch.primary_callback != UINT32_C(0x08048294) &&
         dispatch.primary_callback != UINT32_C(0x080482b8) &&
         dispatch.primary_callback != UINT32_C(0x080482e0)))
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    if ((racer->control & UINT16_C(0x0080)) == 0u ||
        (racer->source_flags & UINT32_C(0x00010800)) != 0u ||
        racer->item_use_state_17c != 0u)
        return PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    racer->item_inventory.held_item_17a =
        dispatch.inventory_action == PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR
            ? dispatch.successor_item
            : dispatch.inventory_action == PORTABLE_GP_ITEM_INVENTORY_KEEP
                ? racer->item_inventory.held_item_17a : 0u;
    racer->item_inventory.preview_item = 0u;
    racer->item_inventory.settled_item = racer->item_inventory.held_item_17a;
    racer->item_inventory.active = 0u;
    /* 48294/48340/48370/482B8/482E0 queue the same driver request.
     * 482B8 alone creates the 600-tick auxiliary inventory owner. */
    racer->item_response_flags_b2 |= UINT16_C(0x8000);
    if (dispatch.primary_callback == UINT32_C(0x080482b8))
        racer->golden_mushroom_ticks = (racer->mushroom_rules)->golden_actor_ticks;
    portable_gp_item_pool_construct_mushroom(roster,racer_index,
        dispatch.primary_callback==UINT32_C(0x08048294)?(racer->mushroom_rules)->single_actor_ticks:
        dispatch.primary_callback==UINT32_C(0x080482b8)?(racer->mushroom_rules)->golden_actor_ticks:0u,
        (uint8_t)(dispatch.primary_callback==UINT32_C(0x080482b8)));
    if (dispatch.primary_callback==UINT32_C(0x08048340)) ++roster->triple_mushroom_uses;
    if (dispatch.primary_callback==UINT32_C(0x080482b8)) ++roster->golden_mushroom_uses;
    if (racer->is_player) ++roster->player_mushroom_uses;
    return PORTABLE_GP_ROSTER_OK;
}

PortableGpRosterStatus portable_gp_race_roster_tick_mushroom_actors(
    PortableGpRaceRoster *roster,size_t racer_index)
{
    if(roster==NULL || racer_index>=portable_gp_race_roster_count(roster))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[racer_index];
    racer->golden_mushroom_ticks=0u;
    for(unsigned i=0u;i<PORTABLE_GP_ITEM_POOL_CAPACITY;++i){
        PortableGpMushroomActor *actor=&roster->mushroom_pool[i];
        if(!actor->occupied || actor->owner!=racer_index || actor->ticks_remaining==0u)continue;
        if(--actor->ticks_remaining==0u){
            actor->occupied=0u;actor->flags_34=0u;
            /* 48310 clears any current inventory, even if stolen/replaced.
             * Forced4F7AC reuse has no such callback and preserves it. */
            if(actor->golden)racer->item_inventory=(PortableGpItemInventory){0};
        }else if(actor->golden && actor->ticks_remaining>racer->golden_mushroom_ticks)
            racer->golden_mushroom_ticks=actor->ticks_remaining;
    }
    return PORTABLE_GP_ROSTER_OK;
}


/* Mod scripts see a CPU's activation once the CPU has decided to use its
 * item. Outside Battle a replacement must be an item the GP CPU code can
 * use; anything else keeps the CPU's own item. Zero cancels and keeps it. */
static uint16_t cpu_script_item(const PortableGpRaceRoster *roster,size_t index,uint16_t item)
{
    const PortableRaceScriptHooks *hooks=roster->script_hooks;
    if(hooks==NULL||hooks->item_use==NULL)return item;
    const uint16_t chosen=hooks->item_use(hooks->context,index,item);
    if(chosen==0u||chosen==item)return chosen;
    if(chosen<PORTABLE_GP_GREEN_SHELL_ITEM||chosen>UINT16_C(0x100c))return item;
    if((roster->race_mode_flags_7da&0x800u)!=0u)return chosen;
    switch(chosen){
        case PORTABLE_GP_GREEN_SHELL_ITEM: case PORTABLE_GP_RED_SHELL_ITEM:
        case PORTABLE_GP_SPINY_SHELL_ITEM: case PORTABLE_GP_BANANA_ITEM:
        case UINT16_C(0x1007): case UINT16_C(0x100a): case UINT16_C(0x100b): case UINT16_C(0x100c):
            return chosen;
        default: return item;
    }
}

/* 42004/42104/426AC: a GP CPU constructs the item it decided to use (or a
 * script's replacement, which goes back to `original` when it cannot be
 * constructed), then starts the shared use cooldown. */
static PortableGpRosterStatus cpu_activate(PortableGpRaceRoster *roster,size_t index,
    uint16_t original,uint16_t item,const int32_t *sine,size_t sine_count)
{
    PortableGpRacer *racer=&roster->racers[index];
    if(item!=original)racer->item_inventory.held_item_17a=item;
    const int green=item==PORTABLE_GP_GREEN_SHELL_ITEM;
    const int red=item==PORTABLE_GP_RED_SHELL_ITEM||item==PORTABLE_GP_SPINY_SHELL_ITEM;
    PortableGpRosterStatus status=PORTABLE_GP_ROSTER_OK;
    if(green||red||item==PORTABLE_GP_BANANA_ITEM){
        /* 42104/426AC: a constructed shell or banana starts its 48-turn hold. */
        if(green?(racer->green_shell.occupied && racer->green_shell.phase<=PORTABLE_GP_GREEN_SHELL_ACTIVE):
           red?(racer->red_shell.occupied && racer->red_shell.phase<=PORTABLE_GP_RED_SHELL_ACTIVE):racer->banana.occupied)
            status=PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
        else{
            racer->item_decision_190=(uint8_t)((racer->item_decision_190&0xfcu)|1u);
            racer->item_decision_timer_18e=48u;
            status=green?portable_gp_race_roster_use_green_shell(roster,index,sine,sine_count):red?
                portable_gp_race_roster_use_red_shell(roster,index,sine,sine_count):
                portable_gp_race_roster_use_banana(roster,index,sine,sine_count);
        }
        if(status==PORTABLE_GP_ROSTER_OK){
            if(green)++roster->cpu_green_uses;else if(item==PORTABLE_GP_SPINY_SHELL_ITEM)++roster->cpu_spiny_uses;
            else if(red)++roster->cpu_red_uses;else ++roster->cpu_banana_uses;
        }
    }else if(item==UINT16_C(0x100c)){
        status=portable_gp_race_roster_use_lightning(roster,index);
    }else if(item==UINT16_C(0x100b)){
        status=portable_gp_race_roster_use_boo(roster,index);
        /* 420E2 rereads inventory after 48694: only a stolen Boo survives. */
        if(status==PORTABLE_GP_ROSTER_OK && racer->item_inventory.held_item_17a!=0x100bu)
            racer->item_inventory=(PortableGpItemInventory){0};
    }else if(item==UINT16_C(0x100a)){
        status=portable_gp_race_roster_use_star(roster,index);
        if(status==PORTABLE_GP_ROSTER_OK)++roster->cpu_star_uses;
    }else{
        if(!portable_mushroom_rules_valid(racer->mushroom_rules))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        /* 48294's mushroom B2 request waits for ground-entry A9AC. */
        portable_gp_item_pool_construct_mushroom(roster,index,(racer->mushroom_rules)->single_actor_ticks,0u);
        racer->item_response_flags_b2|=UINT16_C(0x8000);
        racer->item_inventory=(PortableGpItemInventory){0};
        ++roster->cpu_mushroom_uses;
    }
    /* Only a shell or banana may stay held when it cannot be constructed;
     * the original reports an ineligible lightning, Boo or star. */
    if(status==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE &&
       (item!=original||green||red||item==PORTABLE_GP_BANANA_ITEM)){
        if(item!=original)racer->item_inventory.held_item_17a=original;
        return PORTABLE_GP_ROSTER_OK;
    }
    if(status!=PORTABLE_GP_ROSTER_OK)return status;
    roster->cpu_item_cooldown_c62=roster->cpu_item_engine==0u?510u:
        roster->cpu_item_engine==1u?270u:150u;
    return PORTABLE_GP_ROSTER_OK;
}

/* 421F4/42454/4279C evaluate held green/banana/red items every CPU turn, independently of
 * the global use cooldown and staggered activation slot in42AE4.
 * The dispatcher still requires nonzero17A, including a new roulette. */
static PortableGpRosterStatus cpu_projectile_decision(PortableGpRaceRoster *roster,
    size_t index,size_t checkpoints,const int32_t *sine,size_t sine_count)
{
    PortableGpRacer *racer=&roster->racers[index];
    const uint16_t item=racer->shell_item_18c?racer->shell_item_18c:racer->item_inventory.held_item_17a;
    const int green=item==PORTABLE_GP_GREEN_SHELL_ITEM,red=item==PORTABLE_GP_RED_SHELL_ITEM || item==PORTABLE_GP_SPINY_SHELL_ITEM;
    const unsigned rank=racer->rank_flags_ba>>1u;
    if(rank>=portable_gp_race_roster_count(roster))return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    unsigned seen=0u;
    for(unsigned i=0u;i<portable_gp_race_roster_count(roster);++i){
        if(roster->standings[i]>=8u || (seen&(1u<<roster->standings[i])))
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        seen|=1u<<roster->standings[i];
    }
    const PortableGpRacer *ahead=rank?&roster->racers[roster->standings[rank-1u]]:NULL;
    const PortableGpRacer *behind=rank+1u<portable_gp_race_roster_count(roster)?&roster->racers[roster->standings[rank+1u]]:NULL;
    if(racer->shell_item_18c==PORTABLE_GP_BANANA_ITEM ||
       racer->shell_item_18c==PORTABLE_GP_GREEN_SHELL_ITEM ||
       (racer->shell_item_18c==PORTABLE_GP_RED_SHELL_ITEM || racer->shell_item_18c==PORTABLE_GP_SPINY_SHELL_ITEM)){
        if(racer->item_decision_timer_18e){--racer->item_decision_timer_18e;return PORTABLE_GP_ROSTER_OK;}
        const PortableGpRacer *human=NULL;
        unsigned side=0u;
        if(ahead && (ahead->source_flags&0x8000u) && rank<=5u)human=ahead;
        else if(behind && (behind->source_flags&0x8000u)){human=behind;side=1u;}
        unsigned decision;
        if(human){
            const PortableGpRacer *front=side?racer:human,*back=side?human:racer;
            const int gap=(int)checkpoints*((int)((front->route_wrap_91+1u)&7u)-
                (int)((back->route_wrap_91+1u)&7u))+(int)front->route_index-back->route_index;
            if(gap<0)return PORTABLE_GP_ROSTER_OK;
            const uint8_t valid=green?roster->cpu_green_release_valid:
                red?roster->cpu_red_release_valid:roster->cpu_banana_release_valid;
            if(!valid)return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
            const uint8_t *row = green ? roster->cpu_green_release[side]
                : red ? roster->cpu_red_release[side] : roster->cpu_banana_release[side];
            decision = row[gap > 3 ? 3u : (unsigned)gap];
            if(decision>3u)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        }else{
            const int delta=racer->progress_ae-(ahead?ahead:behind)->progress_ae;
            if(delta>(rank?1:2))decision=rank?0u:1u;
            else if((roster->racers[roster->standings[0]].source_flags&0x8000u) &&
                    (rank+1u==portable_gp_race_roster_count(roster) || racer->progress_ae>2))decision=green?0u:1u;
            else{
                decision=3u;
                if(!red || rank<=5u)
                    for(unsigned i=0u;i<rank;++i)
                        if(roster->racers[roster->standings[i]].source_flags&0x8000u){decision=2u;break;}
            }
        }
        racer->item_decision_190=(uint8_t)((racer->item_decision_190&0xfcu)|((decision&1u)<<1u)|(decision>>1u));
        return PORTABLE_GP_ROSTER_OK;
    }
    /* 42104/426AC initialize a48-turn hold and choose whether to construct.
     * Its thresholds differ for the leading kart and for ranks6/7. */
    racer->item_decision_190=(uint8_t)((racer->item_decision_190&0xfcu)|1u);
    racer->item_decision_timer_18e=48u;
    int use=(ahead && (ahead->source_flags&0x8000u) && rank<=5u) ||
        (behind && (behind->source_flags&0x8000u));
    if(!use)use=racer->progress_ae-(ahead?ahead:behind)->progress_ae>(rank?1:2);
    if(!use && (roster->racers[roster->standings[0]].source_flags&0x8000u))
        use=rank+1u==portable_gp_race_roster_count(roster) || racer->progress_ae>2;
    if(!use || (green?(racer->green_shell.occupied && racer->green_shell.phase<=PORTABLE_GP_GREEN_SHELL_ACTIVE):red?(racer->red_shell.occupied && racer->red_shell.phase<=PORTABLE_GP_RED_SHELL_ACTIVE):racer->banana.occupied))return PORTABLE_GP_ROSTER_OK;
    const uint16_t held=racer->item_inventory.held_item_17a;
    const uint16_t chosen=cpu_script_item(roster,index,item);
    if(chosen==0u)return PORTABLE_GP_ROSTER_OK;
    return cpu_activate(roster,index,held,chosen,sine,sine_count);
}


/* Battle had human participants in the original. This native CPU extension
 * chooses item intent from arena geometry, not GP rank/progress. Constructors,
 * attached-item callbacks and flight/contact effects remain shared. */
static PortableGpRosterStatus battle_cpu_item(PortableGpRaceRoster *roster,
    size_t index,const int32_t *sine,size_t sine_count)
{
    const PortableBattleCombatRules *rules=&roster->battle_combat;
    if (!portable_battle_combat_rules_valid(rules)) return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    PortableGpRacer *r=&roster->racers[index];
    if (r->balloons_remaining==0u || r->lightning_state_78>1u ||
        (r->source_flags&0x820u)!=0u) return PORTABLE_GP_ROSTER_OK;
    r->control&=UINT16_C(0xff7f);
    uint16_t weapon=r->shell_item_18c;
    uint8_t orbit=0u;
    for (size_t actor=0u;weapon==0u && actor<PORTABLE_GP_GREEN_ACTOR_SLOTS;++actor) {
        const PortableGpGreenShell *shell=portable_gp_green_actor(roster,actor);
        if (shell->occupied && portable_gp_green_actor_owner(roster,actor)==index &&
            (shell->phase==PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING ||
             shell->phase==PORTABLE_GP_GREEN_SHELL_ORBITING)) {weapon=PORTABLE_GP_GREEN_SHELL_ITEM;orbit=1u;}
    }
    for (size_t actor=0u;weapon==0u && actor<PORTABLE_GP_RED_ACTOR_SLOTS;++actor) {
        const PortableGpRedShell *shell=portable_gp_red_actor(roster,actor);
        if (shell->occupied && portable_gp_red_actor_owner(roster,actor)==index &&
            (shell->phase==PORTABLE_GP_RED_SHELL_ORBIT_PENDING ||
             shell->phase==PORTABLE_GP_RED_SHELL_ORBITING)) {weapon=PORTABLE_GP_RED_SHELL_ITEM;orbit=1u;}
    }
    uint8_t forward=0u,backward=0u;
    for (size_t i=0u;i<portable_gp_race_roster_count(roster);++i) {
        const PortableGpRacer *other=&roster->racers[i];
        if (i==index || other->balloons_remaining==0u || other->lightning_state_78>1u ||
            other->world_height_q16!=0 || (other->hit_cue_flags_58&0x8000u)!=0u ||
            other->contact_status_66>0) continue;
        const int32_t dx=other->position_x_q16/65536-r->position_x_q16/65536;
        const int32_t dy=other->position_y_q16/65536-r->position_y_q16/65536;
        if (dx<-(int32_t)rules->range || dx>rules->range ||
            dy<-(int32_t)rules->range || dy>rules->range ||
            dx*dx+dy*dy>(int32_t)rules->range*rules->range) continue;
        uint16_t angle;
        if (portable_driver_vector_effect_source_angle((int16_t)dx,(int16_t)dy,&angle)!=
            PORTABLE_DRIVER_VECTOR_EFFECT_OK) return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        const uint16_t delta=(uint16_t)(angle-r->heading);
        const unsigned turn=delta<0x8000u?delta:0x10000u-delta;
        if (turn<=rules->aim_angle) forward=1u;
        if (turn>=0x8000u-rules->aim_angle) backward=1u;
    }
    /* A released item is observed by the next shared actor pass. Reassert
     * hold during the interval so orbiting triples release one at a time. */
    if (weapon!=0u) {
        r->item_decision_190=(uint8_t)((r->item_decision_190&0xfcu)|1u);
        if (r->item_decision_timer_18e!=0u) {--r->item_decision_timer_18e;return PORTABLE_GP_ROSTER_OK;}
        const int banana=weapon==PORTABLE_GP_BANANA_ITEM;
        const int green=weapon==PORTABLE_GP_GREEN_SHELL_ITEM;
        if (forward || (!orbit && (banana || green) && backward)) {
            r->item_decision_190=(uint8_t)((r->item_decision_190&0xfcu)|
                ((banana?backward:!forward)?2u:0u));
            r->item_decision_timer_18e=rules->interval;
            r->control|=0x80u;
        }
        return PORTABLE_GP_ROSTER_OK;
    }
    const uint16_t item=r->item_inventory.held_item_17a;
    if (item==0u || r->item_inventory.active ||
        roster->cpu_item_clock%rules->interval!=(index*7u)%rules->interval)
        return PORTABLE_GP_ROSTER_OK;
    r->item_decision_190=(uint8_t)((r->item_decision_190&0xfcu)|1u);
    r->item_decision_timer_18e=rules->interval;
    const uint16_t chosen=cpu_script_item(roster,index,item);
    if (chosen==0u) return PORTABLE_GP_ROSTER_OK;
    if (chosen!=item) r->item_inventory.held_item_17a=chosen;
    PortableGpRosterStatus status=PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    switch (chosen) {
        case PORTABLE_GP_GREEN_SHELL_ITEM: case PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM:
            status=portable_gp_race_roster_use_green_shell(roster,index,sine,sine_count);
            if (status==PORTABLE_GP_ROSTER_OK) ++roster->cpu_green_uses;
            break;
        case PORTABLE_GP_RED_SHELL_ITEM: case PORTABLE_GP_TRIPLE_RED_SHELL_ITEM:
        case PORTABLE_GP_SPINY_SHELL_ITEM:
            status=portable_gp_race_roster_use_red_shell(roster,index,sine,sine_count);
            if (status==PORTABLE_GP_ROSTER_OK) {
                if (chosen==PORTABLE_GP_SPINY_SHELL_ITEM) ++roster->cpu_spiny_uses;
                else ++roster->cpu_red_uses;
            }
            break;
        case PORTABLE_GP_BANANA_ITEM:
            status=portable_gp_race_roster_use_banana(roster,index,sine,sine_count);
            if (status==PORTABLE_GP_ROSTER_OK) ++roster->cpu_banana_uses;
            break;
        case 0x100au:
            status=portable_gp_race_roster_use_star(roster,index);
            if (status==PORTABLE_GP_ROSTER_OK) ++roster->cpu_star_uses;
            break;
        case 0x100bu: status=portable_gp_race_roster_use_boo(roster,index);break;
        case 0x100cu: status=portable_gp_race_roster_use_lightning(roster,index);break;
        case 0x1006u: {
            const uint16_t control=r->control;
            r->control|=0x80u;
            status=portable_gp_race_roster_use_reserved_item(roster,index);
            r->control=control;
            break;
        }
        case 0x1007u: case 0x1008u: case 0x1009u: case 0x100eu: case 0x100fu: {
            const uint16_t control=r->control;
            r->control|=0x80u;
            status=portable_gp_race_roster_use_mushroom(roster,index);
            r->control=control;
            if (status==PORTABLE_GP_ROSTER_OK) ++roster->cpu_mushroom_uses;
            break;
        }
        default: return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    }
    if (status==PORTABLE_GP_ROSTER_ITEM_INELIGIBLE) {
        if (chosen!=item) r->item_inventory.held_item_17a=item; /* a refused replacement keeps the item */
        return PORTABLE_GP_ROSTER_OK;
    }
    return status;
}


PortableGpRosterStatus portable_gp_race_roster_use_cpu_item(
    PortableGpRaceRoster *roster,size_t racer_index,
    size_t route_count,uint16_t lap_count,const int32_t *sine,size_t sine_count)
{
    if (roster==NULL || racer_index>=portable_gp_race_roster_count(roster) || route_count==0u ||
        (lap_count==0u && (roster->race_mode_flags_7da&UINT16_C(0x0800))==0u) || roster->cpu_item_engine>2u)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpRacer *racer=&roster->racers[racer_index];
    if(!roster->cpu_items_enabled || racer->is_player ||
       (racer->source_flags&0x800u) ||
       (roster->race_mode_flags_7da&1u))return PORTABLE_GP_ROSTER_OK;
    if ((roster->race_mode_flags_7da&0x800u)!=0u && roster->battle_combat.interval!=0u)
        return battle_cpu_item(roster,racer_index,sine,sine_count);
    if (racer->item_inventory.held_item_17a==0u) return PORTABLE_GP_ROSTER_OK;
    if(racer->shell_item_18c==PORTABLE_GP_BANANA_ITEM ||
       racer->shell_item_18c==PORTABLE_GP_GREEN_SHELL_ITEM ||
       (racer->shell_item_18c==PORTABLE_GP_RED_SHELL_ITEM || racer->shell_item_18c==PORTABLE_GP_SPINY_SHELL_ITEM))
        return cpu_projectile_decision(roster,racer_index,route_count,sine,sine_count);
    if (
        (racer->item_inventory.held_item_17a!=UINT16_C(0x1007) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x100a) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x100b) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x1001) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x100c) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x1003) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x1005) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x1000) &&
         racer->item_inventory.held_item_17a!=UINT16_C(0x1002) &&
         racer->item_inventory.held_item_17a!=PORTABLE_GP_SPINY_SHELL_ITEM) ||
        (racer->source_flags&UINT32_C(0x820))!=0u ||
        (roster->race_mode_flags_7da&1u)!=0u || roster->cpu_item_cooldown_c62!=0u)
        return PORTABLE_GP_ROSTER_OK;
    const uint32_t period=UINT32_C(256)>>roster->cpu_item_engine;
    if ((roster->cpu_item_clock&(period*8u-1u))!=racer_index*period)
        return PORTABLE_GP_ROSTER_OK;
    const unsigned lap=racer->route_wrap_91&127u;
    if ((lap==0u && racer->route_index<=route_count/2u) ||
        (lap==(unsigned)lap_count-1u && racer->route_index>=route_count/2u))
        return PORTABLE_GP_ROSTER_OK;
    /* 42C5C converts CPU inventory1001 to lightning at its slot. */
    if (racer->item_inventory.held_item_17a==UINT16_C(0x1001))
        racer->item_inventory.held_item_17a=UINT16_C(0x100c);
    if(racer->item_inventory.held_item_17a==0x1003u)
        racer->item_inventory.held_item_17a=PORTABLE_GP_BANANA_ITEM;
    if(racer->item_inventory.held_item_17a==PORTABLE_GP_BANANA_ITEM ||
       racer->item_inventory.held_item_17a==PORTABLE_GP_GREEN_SHELL_ITEM ||
       (racer->item_inventory.held_item_17a==PORTABLE_GP_RED_SHELL_ITEM || racer->item_inventory.held_item_17a==PORTABLE_GP_SPINY_SHELL_ITEM))
        return cpu_projectile_decision(roster,racer_index,route_count,sine,sine_count);
    const int lightning=racer->item_inventory.held_item_17a==UINT16_C(0x100c);
    const int boo=racer->item_inventory.held_item_17a==UINT16_C(0x100b);
    const unsigned rank=racer->rank_flags_ba>>1u;
    if (rank>=portable_gp_race_roster_count(roster)) return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    unsigned seen=0u;
    for (unsigned i=0u;i<portable_gp_race_roster_count(roster);++i) {
        if (roster->standings[i]>=8u || (seen&(1u<<roster->standings[i]))!=0u)
            return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
        seen|=1u<<roster->standings[i];
    }
    const PortableGpRacer *ahead=rank!=0u?&roster->racers[roster->standings[rank-1u]]:NULL;
    const PortableGpRacer *behind=rank+1u<portable_gp_race_roster_count(roster)?&roster->racers[roster->standings[rank+1u]]:NULL;
    int use=(behind!=NULL && (behind->source_flags&UINT32_C(0x8000))!=0u) ||
        (ahead!=NULL && (ahead->source_flags&UINT32_C(0x8000))!=0u);
    if (!use) {
        const PortableGpRacer *neighbor=ahead!=NULL?ahead:behind;
        use=!lightning && !boo && (int32_t)racer->progress_ae-neighbor->progress_ae>1;
        if (!use && (roster->racers[roster->standings[0]].source_flags&UINT32_C(0x8000))!=0u)
            use=rank+1u==portable_gp_race_roster_count(roster) || racer->progress_ae>2;
    }
    if (!use) return PORTABLE_GP_ROSTER_OK;
    /* 42004 constructs lightning/star immediately; no local input is needed. */
    const uint16_t held=racer->item_inventory.held_item_17a;
    const uint16_t chosen=cpu_script_item(roster,racer_index,held);
    if (chosen==0u) return PORTABLE_GP_ROSTER_OK;
    return cpu_activate(roster,racer_index,held,chosen,sine,sine_count);
}
