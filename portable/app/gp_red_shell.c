#include "gp_red_shell.h"
#include "gp_shell_orbit.h"
#include "driver_vector_effect.h"

#include <string.h>

static int shell_kind(uint16_t kind)
{
    return kind==PORTABLE_GP_RED_SHELL_KIND || kind==PORTABLE_GP_SPINY_SHELL_KIND;
}

static int32_t source_angle_index(int16_t angle)
{
    int32_t adjusted=angle;
    if(adjusted<0)adjusted+=0x3f;
    if(adjusted>=0)return adjusted/64;
    return -(((-adjusted)+63)/64);
}

static int32_t arshift14(int32_t value)
{
    if(value>=0)return value/INT32_C(0x4000);
    return -(int32_t)(((int64_t)-value+INT64_C(0x3fff))/INT64_C(0x4000));
}

static int16_t bios_arctan(int32_t value)
{
    int32_t a=-arshift14(value*value);
    int32_t b=arshift14(INT32_C(0xa9)*a)+INT32_C(0x390);
    b=arshift14(b*a)+INT32_C(0x91c);
    b=arshift14(b*a)+INT32_C(0xfb6);
    b=arshift14(b*a)+INT32_C(0x16aa);
    b=arshift14(b*a)+INT32_C(0x2081);
    b=arshift14(b*a)+INT32_C(0x3651);
    b=arshift14(b*a)+INT32_C(0xa2f9);
    /* BIOS ArcTan ends with ASR #16, rounding negative products down.
     * C division truncates toward zero and differs for nonintegral negatives.
     * The Q14 ratio bounds keep this product within signed 32 bits. */
    const int32_t product=value*b;
    return (int16_t)(product>=0 ? product/INT32_C(0x10000) :
        -(int32_t)((-(int64_t)product+INT64_C(0xffff))/INT64_C(0x10000)));
}

static uint16_t bios_arctan2(int32_t x,int32_t y)
{
    if(y==0)return x>=0?0u:UINT16_C(0x8000);
    if(x==0)return y>=0?UINT16_C(0x4000):UINT16_C(0xc000);
    if(y>=0){
        if(x>=0&&x>=y)return (uint16_t)bios_arctan((y*INT32_C(0x4000))/x);
        if(x<0&&-x>=y)
            return (uint16_t)(bios_arctan((y*INT32_C(0x4000))/x)+UINT16_C(0x8000));
        return (uint16_t)(UINT16_C(0x4000)-
                          bios_arctan((x*INT32_C(0x4000))/y));
    }
    if(x<=0&&-x>-y)
        return (uint16_t)(bios_arctan((y*INT32_C(0x4000))/x)+UINT16_C(0x8000));
    if(x>0&&x>=-y)
        return (uint16_t)(bios_arctan((y*INT32_C(0x4000))/x));
    return (uint16_t)(UINT16_C(0xc000)-
                      bios_arctan((x*INT32_C(0x4000))/y));
}

uint16_t portable_gp_red_shell_target_angle(int16_t delta_x,int16_t delta_y)
{
    return (uint16_t)(bios_arctan2(delta_x,delta_y)+UINT16_C(0x4000));
}

static void turn_toward(PortableGpRedShell *shell,uint16_t desired)
{
    int16_t current=(int16_t)shell->angle_26;
    int16_t delta=(int16_t)(desired-(uint16_t)current);
    if(delta<0){
        if(delta<=-INT16_C(0x800))
            current=(int16_t)(uint16_t)((uint16_t)current-UINT16_C(0x400));
        else current=(int16_t)desired;
    }else if(delta>INT16_C(0x7ff))
        current=(int16_t)(uint16_t)((uint16_t)current+UINT16_C(0x400));
    else current=(int16_t)desired;
    shell->angle_26=(uint16_t)current;
}

static uint8_t flight_target_speed(const PortableGpRedShell *shell,uint8_t speed_mode)
{
    const PortableRedShellRules *r=(shell->rules);
    return (uint8_t)(speed_mode==0u?r->flight_speed_50:speed_mode==2u?r->flight_speed_150:r->flight_speed_100);
}

static void advance_flight_to_speed(PortableGpRedShell *shell,
                                    uint8_t target_speed,const int32_t *sine)
{
    int32_t index;int64_t magnitude;
    if(shell->speed_byte_71>=target_speed){
        int next=(int)shell->speed_byte_71-(int)(shell->rules)->deceleration;
        shell->speed_byte_71=(uint8_t)(next>=target_speed?next:target_speed);
    }else{
        int next=(int)shell->speed_byte_71+(int)(shell->rules)->acceleration;
        shell->speed_byte_71=(uint8_t)(next<=target_speed?next:target_speed);
    }
    index=source_angle_index((int16_t)shell->angle_26);
    magnitude=(int64_t)shell->speed_byte_71<<4;
    shell->velocity_x_q16=(int32_t)(magnitude*
        sine[(uint32_t)index&0x3ffu]/128);
    shell->velocity_y_q16=(int32_t)(-magnitude*
        sine[(0x100u-(uint32_t)index)&0x3ffu]/128);
    if((shell->animation_tick_4c&UINT16_C(7))==0u){
        shell->display_index=shell->animation_index_70;
        ++shell->animation_index_70;
        if(shell->animation_index_70>5u)shell->animation_index_70=0u;
    }
    ++shell->animation_tick_4c;
    shell->position_x_q16=(int32_t)((uint32_t)shell->position_x_q16+
                                    (uint32_t)shell->velocity_x_q16);
    shell->position_y_q16=(int32_t)((uint32_t)shell->position_y_q16+
                                    (uint32_t)shell->velocity_y_q16);
}

static void advance_flight(PortableGpRedShell *shell,uint8_t speed_mode,
                           const int32_t *sine)
{
    advance_flight_to_speed(shell,flight_target_speed(shell,speed_mode),sine);
}

static void begin_destruction(PortableGpRedShell *shell)
{
    shell->destruction_field_5a=UINT16_C(0x8000);
    shell->vertical_velocity_q16=(int32_t)(shell->rules)->destruction_velocity_q16;
    shell->flags_34&=UINT16_C(0xfffd);
    shell->collision_active=0u;
    shell->phase=PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE;
}

static void attach(const PortableGpRedShellOwner *owner,
                   PortableGpRedShell *shell,const int32_t *sine)
{
    const int32_t index=source_angle_index((int16_t)owner->angle);
    const uint32_t forward=(uint32_t)sine[(uint32_t)index&0x3ffu]<<4u;
    const uint32_t perpendicular=
        (uint32_t)sine[(0x100u-(uint32_t)index)&0x3ffu]<<4u;
    shell->angle_26=owner->angle;
    shell->position_x_q16=(int32_t)((uint32_t)owner->position_x_q16+forward);
    shell->position_y_q16=(int32_t)((uint32_t)owner->position_y_q16-
                                    perpendicular);
    shell->position_z_q16=owner->position_z_q16;
    shell->floor_36=owner->floor_36;
}

static PortableGpShellOrbitView orbit_view(PortableGpRedShell *shell)
{
    return (PortableGpShellOrbitView){&shell->position_x_q16,&shell->position_y_q16,
        &shell->position_z_q16,&shell->angle_26,&shell->flags_34,&shell->animation_tick_4c,
        &shell->floor_36,&shell->animation_index_70,&shell->display_index,shell->orbit_rules};
}

static PortableGpShellOrbitOwner orbit_owner(const PortableGpRedShellOwner *owner)
{
    return (PortableGpShellOrbitOwner){owner->position_x_q16,owner->position_y_q16,
        owner->position_z_q16,owner->floor_36,owner->mode_78};
}

PortableGpRedShellStatus portable_gp_red_shell_use_triple(
    PortableGpItemInventory *inventory,PortableGpRedShellOwner *owner,
    PortableGpRedShell shells[3],const uint8_t delays[3],const int32_t *sine,size_t sine_count)
{
    if(inventory==NULL || owner==NULL || shells==NULL || delays==NULL || sine==NULL || sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!portable_shell_orbit_rules_valid((owner->orbit_rules)))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(inventory->held_item_17a!=PORTABLE_GP_TRIPLE_RED_SHELL_ITEM ||
       ((owner->flags_48&0x2000u)==0u && !(owner->control_96&0x80u)) ||
       (owner->flags_48&0x10800u) || owner->item_use_state_17c)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(!portable_red_shell_rules_valid((owner->rules)))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const PortableGpShellOrbitOwner view=orbit_owner(owner);
    for(unsigned i=0u;i<3u;++i){
        shells[i]=(PortableGpRedShell){.occupied=1u,.kind_10=PORTABLE_GP_RED_SHELL_KIND,
            .phase=PORTABLE_GP_RED_SHELL_ORBIT_PENDING,.release_timer_74=delays[i],.retire_held_slot=1u,.rules=owner->rules,.orbit_rules=owner->orbit_rules};
        portable_gp_shell_orbit_position(orbit_view(&shells[i]),&view,sine);
    }
    owner->flags_48&=UINT32_C(0xfffeffff);owner->item_use_state_17c=3u;
    inventory->held_item_17a=0u;inventory->preview_item=0u;inventory->settled_item=0u;inventory->active=0u;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_orbit(
    const PortableGpRedShellOwner *owner,PortableGpRedShell *shell,const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(owner==NULL || shell==NULL || sine==NULL || sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!portable_shell_orbit_rules_valid((shell->orbit_rules)))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied || shell->kind_10!=PORTABLE_GP_RED_SHELL_KIND ||
       (shell->phase!=PORTABLE_GP_RED_SHELL_ORBIT_PENDING && shell->phase!=PORTABLE_GP_RED_SHELL_ORBITING))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    const PortableGpShellOrbitOwner view=orbit_owner(owner);
    if(shell->phase==PORTABLE_GP_RED_SHELL_ORBIT_PENDING){
        if(shell->release_timer_74){--shell->release_timer_74;return PORTABLE_GP_RED_SHELL_OK;}
        shell->angle_26=0u;portable_gp_shell_orbit_position(orbit_view(shell),&view,sine);
        shell->flags_34|=3u;shell->animation_tick_4c=0u;shell->speed_byte_71=0u;
        shell->phase=PORTABLE_GP_RED_SHELL_ORBITING;shell->orbit_audio_pending=1u;
    }else portable_gp_shell_orbit_tick(orbit_view(shell),&view,sine);
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_use(
    PortableGpItemInventory *inventory,PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,const int32_t *sine,size_t sine_count)
{
    PortableGpItemUseDispatch dispatch;
    if(inventory==NULL||owner==NULL||shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const int spiny=inventory->held_item_17a==PORTABLE_GP_SPINY_SHELL_ITEM;
    if((inventory->held_item_17a!=PORTABLE_GP_RED_SHELL_ITEM && !spiny)||
       !portable_gp_item_use_dispatch(inventory->held_item_17a,&dispatch)||
       dispatch.primary_callback!=(spiny?UINT32_C(0x08047630):UINT32_C(0x080474cc))||
       ((owner->flags_48&0x2000u)==0u && (owner->control_96&UINT16_C(0x0080))==0u)||
       (owner->flags_48&UINT32_C(0x00000800))!=0u||
       owner->item_use_state_17c!=0u||
       (owner->flags_48&UINT32_C(0x00010000))!=0u||shell->occupied)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(!portable_red_shell_rules_valid((owner->rules)))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    memset(shell,0,sizeof *shell);
    shell->rules=owner->rules;
    shell->orbit_rules=owner->orbit_rules;
    shell->occupied=1u;
    shell->kind_10=spiny?PORTABLE_GP_SPINY_SHELL_KIND:PORTABLE_GP_RED_SHELL_KIND;
    shell->spiny_targeting=(uint8_t)spiny;
    shell->phase=PORTABLE_GP_RED_SHELL_ATTACHED;
    attach(owner,shell,sine);
    owner->flags_48&=UINT32_C(0xfffeffff);
    owner->item_use_state_17c=1u;
    inventory->held_item_17a=PORTABLE_GP_ITEM_NONE;
    inventory->preview_item=0u;
    inventory->settled_item=0u;
    inventory->active=0u;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_attached(
    PortableGpRedShellOwner *owner,PortableGpRedShell *shell,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(owner==NULL||shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_ATTACHED)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    attach(owner,shell,sine);
    shell->flags_34|=UINT16_C(3);
    shell->animation_tick_4c=0u;
    owner->shell_item_18c=shell->kind_10==PORTABLE_GP_SPINY_SHELL_KIND?
        PORTABLE_GP_SPINY_SHELL_ITEM:PORTABLE_GP_RED_SHELL_ITEM;
    shell->phase=PORTABLE_GP_RED_SHELL_ACTIVE;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_held_active(
    PortableGpRedShellOwner *owner,PortableGpRedShell *shell,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(owner==NULL||shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_ACTIVE || owner->response_status_7e>=16u)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    PortableGpRedShellOwner attachment=*owner;
    if((UINT16_C(0xde1f)&(1u<<owner->response_status_7e))==0u)
        attachment.angle=(uint16_t)(owner->target_heading_56+0x8000u);
    attach(&attachment,shell,sine);
    if((shell->animation_tick_4c&UINT16_C(7))==0u){
        shell->display_index=shell->animation_index_70;
        ++shell->animation_index_70;
        if(shell->animation_index_70>5u)shell->animation_index_70=0u;
    }
    ++shell->animation_tick_4c;
    if(owner->mode_78==2u || ((owner->mode_78==3u || owner->mode_78==4u) && owner->position_z_q16==0))
        shell->flags_34&=UINT16_C(0xfffc);
    else shell->flags_34|=3u;
    if((owner->flags_48&0x2000u) && (int16_t)shell->animation_tick_4c>=(int32_t)(shell->rules)->cpu_hold_ticks)
        owner->decision_190&=UINT8_C(0xfe);
    return PORTABLE_GP_RED_SHELL_OK;
}

static int backward_requested(const PortableGpRedShellOwner *owner)
{
    return (owner->flags_48&0x2000u)?(owner->decision_190&2u)!=0u:(owner->control_96&0x400u)!=0u;
}

PortableGpRedShellStatus portable_gp_red_shell_release_backward(
    PortableGpRedShellOwner *owner,PortableGpRedShell *shell,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    int16_t angle;int32_t index;
    if(owner==NULL||shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_ACTIVE||
       (owner->control_96&UINT16_C(0x0040))!=0u||
       !backward_requested(owner))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    shell->position_x_q16=owner->position_x_q16;
    shell->position_y_q16=owner->position_y_q16;
    shell->position_z_q16=0;
    shell->velocity_x_q16=0;
    shell->velocity_y_q16=0;
    shell->lifetime_38=(uint16_t)(shell->rules)->collision_grace_ticks;
    shell->animation_tick_4c=0u;
    shell->release_timer_74=1u;
    shell->route_budget_78=(uint8_t)(shell->rules)->route_budget;shell->previous_floor_79=0u;
    shell->steering_delay_7a=0u;
    shell->collision_active=1u;
    angle=(int16_t)(uint16_t)((uint16_t)owner->release_angle_offset+
                              UINT16_C(0x8000));
    shell->angle_26=UINT16_C(0x8000);
    index=source_angle_index(angle);
    shell->position_x_q16=(int32_t)((uint32_t)shell->position_x_q16+
        ((uint32_t)sine[(uint32_t)index&0x3ffu]<<4u));
    shell->position_y_q16=(int32_t)((uint32_t)shell->position_y_q16-
        ((uint32_t)sine[(0x100u-(uint32_t)index)&0x3ffu]<<4u));
    owner->item_use_state_17c=(uint8_t)(owner->item_use_state_17c-1u);
    owner->shell_item_18c=0u;
    owner->flags_48|=UINT32_C(0x00010000);
    shell->phase=PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD;
    shell->motion_audio_pending=134u;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_release_forward_leader(
    PortableGpRedShellOwner *owner,PortableGpRedShell *shell,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    int32_t speed;int16_t angle;int32_t index;int64_t magnitude;
    if(owner==NULL||shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_ACTIVE||
       (owner->control_96&UINT16_C(0x0040))!=0u||
       backward_requested(owner)||
       (owner->rank_flags_ba>>1)!=0u)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    shell->position_x_q16=owner->position_x_q16;
    shell->position_y_q16=owner->position_y_q16;
    shell->position_z_q16=0;shell->lifetime_38=(uint16_t)(shell->rules)->collision_grace_ticks;
    shell->animation_tick_4c=0u;shell->release_timer_74=1u;
    shell->route_budget_78=(uint8_t)(shell->rules)->route_budget;shell->previous_floor_79=0u;
    shell->steering_delay_7a=0u;
    shell->collision_active=1u;
    shell->target_valid=0u;shell->target_racer_index=UINT8_MAX;
    angle=owner->release_angle_offset;shell->angle_26=(uint16_t)angle;
    speed=(owner->speed_q16/INT32_C(0x10000)+(int32_t)(shell->rules)->launch_speed_bonus)/16;
    shell->speed_byte_71=(uint8_t)speed;
    if(shell->speed_byte_71>=(shell->rules)->max_launch_speed)
        shell->speed_byte_71=(uint8_t)(shell->rules)->max_launch_speed;
    magnitude=(int64_t)shell->speed_byte_71<<4;
    shell->motion_audio_pending=123u;
    index=source_angle_index(angle);
    shell->velocity_x_q16=(int32_t)(magnitude*
        sine[(uint32_t)index&0x3ffu]/128);
    shell->velocity_y_q16=(int32_t)(-magnitude*
        sine[(0x100u-(uint32_t)index)&0x3ffu]/128);
    owner->item_use_state_17c=(uint8_t)(owner->item_use_state_17c-1u);
    owner->shell_item_18c=0u;owner->flags_48|=UINT32_C(0x00010000);
    shell->phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_release_forward_target(
    PortableGpRedShellOwner *owner,PortableGpRedShell *shell,
    uint8_t target_racer_index,const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    int32_t speed;int16_t angle;int32_t index;int64_t magnitude;
    if(owner==NULL||shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_ACTIVE||
       (owner->control_96&UINT16_C(0x0040))!=0u||
       backward_requested(owner)||
       (owner->rank_flags_ba>>1)==0u||target_racer_index>=8u)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    shell->position_x_q16=owner->position_x_q16;
    shell->position_y_q16=owner->position_y_q16;
    shell->position_z_q16=0;shell->lifetime_38=(uint16_t)(shell->rules)->collision_grace_ticks;
    shell->animation_tick_4c=0u;shell->release_timer_74=1u;
    shell->route_budget_78=(uint8_t)(shell->rules)->route_budget;shell->previous_floor_79=0u;
    shell->steering_delay_7a=0u;
    shell->collision_active=1u;
    shell->target_valid=1u;shell->target_racer_index=target_racer_index;
    angle=owner->release_angle_offset;shell->angle_26=(uint16_t)angle;
    speed=(owner->speed_q16/INT32_C(0x10000)+(int32_t)(shell->rules)->launch_speed_bonus)/16;
    shell->speed_byte_71=(uint8_t)speed;
    if(shell->speed_byte_71>=(shell->rules)->max_launch_speed)
        shell->speed_byte_71=(uint8_t)(shell->rules)->max_launch_speed;
    magnitude=(int64_t)shell->speed_byte_71<<4;
    shell->motion_audio_pending=123u;
    index=source_angle_index(angle);
    shell->velocity_x_q16=(int32_t)(magnitude*
        sine[(uint32_t)index&0x3ffu]/128);
    shell->velocity_y_q16=(int32_t)(-magnitude*
        sine[(0x100u-(uint32_t)index)&0x3ffu]/128);
    owner->item_use_state_17c=(uint8_t)(owner->item_use_state_17c-1u);
    owner->shell_item_18c=0u;owner->flags_48|=UINT32_C(0x00010000);
    shell->phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_forward_no_target_clear(
    PortableGpRedShell *shell,uint8_t speed_mode,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET||
       shell->target_valid)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(shell->lifetime_38!=0u)--shell->lifetime_38;
    shell->flags_34|=UINT16_C(3);
    advance_flight(shell,speed_mode,sine);
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_forward_target_far_clear(
    PortableGpRedShell *shell,int32_t target_x_q16,int32_t target_y_q16,
    uint8_t speed_mode,const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    int32_t delta_x_q16,delta_y_q16,delta_x,delta_y;int64_t distance_squared;
    uint16_t desired;
    if(shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET||
       !shell->target_valid)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    delta_x_q16=(int32_t)((uint32_t)target_x_q16-
                          (uint32_t)shell->position_x_q16);
    delta_y_q16=(int32_t)((uint32_t)target_y_q16-
                          (uint32_t)shell->position_y_q16);
    delta_x=delta_x_q16/INT32_C(0x10000);
    delta_y=delta_y_q16/INT32_C(0x10000);
    distance_squared=(int64_t)delta_x*delta_x+(int64_t)delta_y*delta_y;
    if(distance_squared<=INT64_C(0x18f))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    desired=portable_gp_red_shell_target_angle((int16_t)delta_x,
                                                (int16_t)delta_y);
    if(shell->steering_delay_7a!=0u)--shell->steering_delay_7a;
    turn_toward(shell,desired);
    if(shell->lifetime_38!=0u)--shell->lifetime_38;
    shell->flags_34|=UINT16_C(3);
    advance_flight(shell,speed_mode,sine);
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_forward_target_close_clear(
    PortableGpRedShell *shell,int32_t target_x_q16,int32_t target_y_q16,
    int32_t target_z_q16,uint32_t target_flags,uint8_t speed_mode,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    int32_t delta_x_q16,delta_y_q16,delta_x,delta_y;int64_t distance_squared;
    uint16_t desired;uint8_t target_speed;
    if(shell==NULL||sine==NULL||sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET||
       !shell->target_valid)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    delta_x_q16=(int32_t)((uint32_t)target_x_q16-
                          (uint32_t)shell->position_x_q16);
    delta_y_q16=(int32_t)((uint32_t)target_y_q16-
                          (uint32_t)shell->position_y_q16);
    delta_x=delta_x_q16/INT32_C(0x10000);
    delta_y=delta_y_q16/INT32_C(0x10000);
    distance_squared=(int64_t)delta_x*delta_x+(int64_t)delta_y*delta_y;
    if(distance_squared>INT64_C(0x18f))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    desired=portable_gp_red_shell_target_angle((int16_t)delta_x,
                                                (int16_t)delta_y);
    if(shell->steering_delay_7a!=0u){
        --shell->steering_delay_7a;turn_toward(shell,desired);
    }else if(target_z_q16!=0||delta_x>12||delta_x<-12||
             delta_y>12||delta_y<-12)
        turn_toward(shell,desired);
    else shell->angle_26=desired;
    target_speed=(target_flags&UINT32_C(0x00008000))!=0u?0x3cu:0x4cu;
    if(speed_mode==0u)target_speed=(uint8_t)(target_speed-8u);
    else if(speed_mode==2u)target_speed=(uint8_t)(target_speed+0x0au);
    if(shell->lifetime_38!=0u)--shell->lifetime_38;
    shell->flags_34|=UINT16_C(3);
    advance_flight_to_speed(shell,target_speed,sine);
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_target_route_floor(
    PortableGpRedShell *shell,uint8_t current_floor)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL)return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       (shell->phase!=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET &&
        !(shell->spiny_targeting && shell->phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET))||
       (!shell->target_valid && !shell->spiny_targeting))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    shell->floor_36=current_floor;
    if(shell->previous_floor_79!=current_floor){
        shell->previous_floor_79=current_floor;
        --shell->route_budget_78;
    }
    if(shell->route_budget_78==0u){
        begin_destruction(shell);
    }
    return PORTABLE_GP_RED_SHELL_OK;
}

/* 446FC is the attached-shell callback. Unlike445CC it has no same-owner
 * shell exclusion or target reset, and retires one held owner slot. */
PortableGpRedShellStatus portable_gp_red_shell_collide_held(
    PortableGpRedShell *shell, PortableGpRedShellOwner *owner,
    uint16_t other_kind, int32_t other_z_q16, int16_t other_field_66)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const uint16_t family = (uint16_t)(other_kind & UINT16_C(0xf000));
    if (shell == NULL || owner == NULL)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || !shell_kind(shell->kind_10) ||
        (shell->phase != PORTABLE_GP_RED_SHELL_ACTIVE &&
         shell->phase != PORTABLE_GP_RED_SHELL_ORBITING))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if (other_kind == UINT16_C(0x8800) || family == UINT16_C(0x1000) ||
        shell->position_z_q16 > INT32_C(0x41fff) ||
        other_z_q16 > INT32_C(0x41fff) ||
        (family == UINT16_C(0x2000) && other_field_66 != 0)) {
        shell->collision_result = PORTABLE_GP_RED_SHELL_COLLISION_IGNORED;
        return PORTABLE_GP_RED_SHELL_OK;
    }
    owner->flags_48 &= UINT32_C(0xfffeffff);
    owner->item_use_state_17c = (uint8_t)(owner->item_use_state_17c - 1u);
    owner->shell_item_18c = 0u;
    begin_destruction(shell);
    shell->collision_result = family == UINT16_C(0x8000)
        ? PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING_ITEM
        : PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING;
    shell->contact_audio_pending = (uint8_t)(family == UINT16_C(0x8000));
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_collide(
    PortableGpRedShell *shell,uint32_t *owner_flags,uint16_t other_kind,
    int32_t other_z_q16,int16_t other_field_66)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const uint16_t family=(uint16_t)(other_kind&UINT16_C(0xf000));
    if(shell==NULL||owner_flags==NULL)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       !shell->collision_active)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(other_kind==UINT16_C(0x8800)||family==UINT16_C(0x1000)||
       shell->position_z_q16>INT32_C(0x00041fff)||
       other_z_q16>INT32_C(0x00041fff)){
        shell->collision_result=PORTABLE_GP_RED_SHELL_COLLISION_IGNORED;
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if(family==UINT16_C(0x2000)&&other_field_66!=0){
        shell->target_valid=0u;shell->target_racer_index=UINT8_MAX;
        shell->collision_result=
            PORTABLE_GP_RED_SHELL_COLLISION_TARGET_CLEARED;
        return PORTABLE_GP_RED_SHELL_OK;
    }
    *owner_flags&=UINT32_C(0xfffeffff);
    begin_destruction(shell);
    shell->collision_result=family==UINT16_C(0x8000)?
        PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING_ITEM:
        PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING;
    shell->contact_audio_pending=(uint8_t)(family==UINT16_C(0x8000));
    return PORTABLE_GP_RED_SHELL_OK;
}

/* 44814 only retires on the leader, a6A response or another Spiny.
 * Ordinary racer responses remain independent in F7EC/F8C8. */
PortableGpRedShellStatus portable_gp_spiny_shell_collide(
    PortableGpRedShell *shell,uint32_t *owner_flags,uint16_t kind,
    int32_t z,int16_t guard,uint8_t rank,int16_t guard_6a)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL || owner_flags==NULL)return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell->kind_10!=PORTABLE_GP_SPINY_SHELL_KIND || !shell->spiny_targeting ||
       (shell->phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET && !shell->target_valid) ||
       shell->phase==PORTABLE_GP_RED_SHELL_ACTIVE)
        return portable_gp_red_shell_collide(shell,owner_flags,kind,z,guard);
    if(!shell->occupied || !shell->collision_active)return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    const unsigned family=kind&0xf000u;
    if(kind==0x8800u || family==0x1000u || shell->position_z_q16>0x41fff || z>0x41fff){
        shell->collision_result=PORTABLE_GP_RED_SHELL_COLLISION_IGNORED;return PORTABLE_GP_RED_SHELL_OK;
    }
    if(family==0x2000u && guard){
        shell->spiny_targeting=0u;shell->target_valid=0u;shell->target_racer_index=UINT8_MAX;
        shell->phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET;
        shell->collision_result=PORTABLE_GP_RED_SHELL_COLLISION_TARGET_CLEARED;
    }else if((family==0x2000u && (rank==0u || guard_6a)) || kind==PORTABLE_GP_SPINY_SHELL_KIND)
        return portable_gp_red_shell_collide(shell,owner_flags,kind,z,0);
    else{
        shell->collision_result=PORTABLE_GP_RED_SHELL_COLLISION_IGNORED;
        shell->contact_audio_pending=(uint8_t)(family==0x8000u);
    }
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_take_contact_audio(
    PortableGpRedShell *shell,uint8_t suppressed,uint16_t spatial_volume,
    PortableGpRedShellAudioEvent *event)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL||event==NULL)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->contact_audio_pending)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    shell->contact_audio_pending=0u;
    if(suppressed||spatial_volume==0u)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    event->song_id=124u;event->player=4u;event->volume=spatial_volume;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_destroying(
    PortableGpRedShell *shell)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL)return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10)||
       shell->phase!=PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    shell->position_z_q16=(int32_t)((uint32_t)shell->position_z_q16+
                                    (uint32_t)shell->vertical_velocity_q16);
    shell->vertical_velocity_q16=(int32_t)(
        (uint32_t)shell->vertical_velocity_q16-(shell->rules)->destruction_gravity_q16);
    if(shell->position_z_q16<0){
        shell->position_z_q16=0;shell->vertical_velocity_q16=0;
        shell->occupied=0u;shell->phase=PORTABLE_GP_RED_SHELL_DESTROYED;
    }
    return PORTABLE_GP_RED_SHELL_OK;
}

static int32_t signed_bits(uint32_t bits)
{
    return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);
}

static int32_t trap_delta(int32_t target,int32_t shell)
{
    return signed_bits((uint32_t)target-(uint32_t)shell)/65536;
}

static void trap_animate(PortableGpRedShell *shell)
{
    if((shell->animation_tick_4c&7u)==0u){
        shell->display_index=shell->animation_index_70;
        shell->animation_index_70=(uint8_t)((shell->animation_index_70+1u)%6u);
    }
    ++shell->animation_tick_4c;
}

static int trap_sample(PortableGpRedShell *shell,
    const PortableGpRedShellWorld *world,PortableDriverMaterialState *sample)
{
    const int result=portable_driver_material_sample(world->material,
        world->raster,world->terrain,shell->position_x_q16,
        shell->position_y_q16,sample);
    if(result!=PORTABLE_DRIVER_MATERIAL_OK &&
       result!=PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS)return 0;
    shell->material_4e=sample->material_flags;
    return 1;
}

static void trap_free(PortableGpRedShell *shell,uint32_t *owner_flags)
{
    *owner_flags&=UINT32_C(0xfffeffff);
    shell->occupied=0u;shell->collision_active=0u;
    shell->flags_34&=UINT16_C(0xfffd);
    shell->phase=PORTABLE_GP_RED_SHELL_DESTROYED;
}

/* 44aac schedules 7884 for the NEXT actor turn. A first wall contact
 * still completes 45378's current flight, including its final integration. */
static int trap_terrain(PortableGpRedShell *shell,
    const PortableGpRedShellWorld *world,PortableDriverMaterialState *sample)
{
    if(shell->position_z_q16!=0)return 1;
    if(!trap_sample(shell,world,sample))return 0;
    if(!sample->in_bounds)return 1;
    PortableDriverTerrainEffectState cell={.cell_key=shell->terrain_cell_52};
    if(portable_driver_terrain_effect_tick(sample,shell->position_z_q16,&cell)!=0)
        return 0;
    shell->terrain_cell_52=cell.cell_key;
    if(!cell.effect_active){shell->collision_state_50=0u;return 1;}
    PortableDriverTerrainEffectDispatch dispatch;
    if(portable_driver_terrain_effect_dispatch(sample,world->material,
        world->terrain,world->reflection,cell.effect_dispatch_delta,
        &shell->velocity_x_q16,&shell->velocity_y_q16,&dispatch)!=0)return 0;
    if((shell->collision_state_50&0x4000u)!=0u)return 1;
    const unsigned count=(shell->collision_state_50&7u)+1u;
    shell->collision_state_50=(uint16_t)((shell->collision_state_50&0xfff8u)|
        count|(count<=7u?0x8000u:0xc000u));
    shell->contact_audio_pending=1u;
    if(--shell->release_timer_74==0u)
        shell->phase=PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING;
    return 1;
}

/* 44D4C's mode800 branch bypasses GP checkpoint budgets and route rows.
 * The entry7C pointer is retained for this turn: discovery writes next turn's
 * target, and the last eligible roster entry wins without immunity filters. */
static PortableGpRedShellStatus battle_forward(PortableGpRedShell *shell,
    const PortableGpRedShellWorld *world,const PortableGpRedShellTarget *targets,size_t count)
{
    if (count==0u || count>8u || world->owner_index>=count)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if (shell->lifetime_38!=0u) --shell->lifetime_38;
    if (!shell->target_valid) {
        uint8_t acquired=UINT8_MAX;
        for (size_t i=0u;i<count;++i) {
            if (i==world->owner_index) continue;
            const int32_t dx=trap_delta(targets[i].x_q16,shell->position_x_q16);
            const int32_t dy=trap_delta(targets[i].y_q16,shell->position_y_q16);
            if (dx>=-64 && dx<=64 && dy>=-64 && dy<=64) acquired=(uint8_t)i;
        }
        advance_flight_to_speed(shell,flight_target_speed(shell,world->engine),world->sine);
        if (acquired!=UINT8_MAX) {
            shell->target_valid=1u;shell->target_racer_index=acquired;
            shell->phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;
        }
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if (shell->target_racer_index>=count) return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const PortableGpRedShellTarget *target=&targets[shell->target_racer_index];
    const int32_t dx=trap_delta(target->x_q16,shell->position_x_q16);
    const int32_t dy=trap_delta(target->y_q16,shell->position_y_q16);
    const uint16_t desired=portable_gp_red_shell_target_angle((int16_t)dx,(int16_t)dy);
    if (shell->steering_delay_7a!=0u) {
        --shell->steering_delay_7a;turn_toward(shell,desired);
    } else if (target->z_q16!=0 || dx>12 || dx<-12 || dy>12 || dy<-12)
        turn_toward(shell,desired);
    else shell->angle_26=desired;
    const uint8_t speed=world->engine==0u?0x34u:world->engine==2u?0x46u:0x3cu;
    advance_flight_to_speed(shell,speed,world->sine);
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_forward(
    PortableGpRedShell *shell,uint32_t *owner_flags,
    const PortableGpRedShellWorld *world,const PortableGpRedShellRoute *route,
    const PortableGpRedShellTarget *targets,size_t target_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL || owner_flags==NULL || world==NULL || route==NULL ||
       targets==NULL || world->sine==NULL || world->sine_count<1024u ||
       world->engine>2u || ((world->mode_flags&0x800u)==0u &&
       (route->checkpoint_count<2u || route->checkpoint_count>127u)))
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied || !shell_kind(shell->kind_10))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(shell->phase==PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING){
        *owner_flags&=UINT32_C(0xfffeffff);begin_destruction(shell);
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if(shell->phase!=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET &&
       shell->phase!=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    const int32_t audio_x=shell->position_x_q16,audio_y=shell->position_y_q16;
    const uint8_t flight_phase=shell->phase;
    PortableDriverMaterialState sample={0};
    if(!trap_terrain(shell,world,&sample))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const int pending=shell->phase==PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING;
    shell->phase=flight_phase;
    uint8_t raw_floor;
    const int floor_status=portable_track_raster_query16(world->raster,
        (int16_t)(shell->position_x_q16/65536),(int16_t)(shell->position_y_q16/65536),&raw_floor);
    if(floor_status==PORTABLE_TRACK_RASTER_OK){
        if(raw_floor!=0x7fu)shell->floor_36=raw_floor&0x7fu;
    }else if(floor_status!=PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell->material_4e&0x20u){
        if(shell->lifetime_38)--shell->lifetime_38;
        trap_free(shell,owner_flags);return PORTABLE_GP_RED_SHELL_OK;
    }
    shell->flags_34|=3u;
    PortableGpRedShellStatus status;
    const int spiny=shell->spiny_targeting;
    if ((world->mode_flags&0x800u)!=0u && !spiny) {
        status=battle_forward(shell,world,targets,target_count);
    } else if(!shell->target_valid && !spiny){
        shell->phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET;
        status=portable_gp_red_shell_tick_forward_no_target_clear(shell,world->engine,
            world->sine,world->sine_count);
    }else{
        const PortableGpRedShellTarget *target=NULL;
        if(shell->target_valid){
            if(spiny){
                for(size_t i=0u;i<target_count;++i)if(targets[i].rank==0u){shell->target_racer_index=(uint8_t)i;break;}
            }
            if(shell->target_racer_index>=target_count)return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
            target=&targets[shell->target_racer_index];
        }
        status=portable_gp_red_shell_tick_target_route_floor(shell,shell->floor_36);
        if(status!=PORTABLE_GP_RED_SHELL_OK)return status;
        if(shell->phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE){
            if(shell->lifetime_38)--shell->lifetime_38;
            return PORTABLE_GP_RED_SHELL_OK;
        }
        const unsigned floor=shell->floor_36,count=(unsigned)route->checkpoint_count;
        const unsigned next=floor+1u<count?floor+1u:floor-(count-1u);
        const unsigned next2=floor+2u<count?floor+2u:floor-(count-2u);
        const unsigned previous=floor?floor-1u:count-1u;
        if(target==NULL || (target->checkpoint!=floor && target->checkpoint!=next &&
           target->checkpoint!=next2 && target->checkpoint!=previous)){
            const unsigned index=floor_status==PORTABLE_TRACK_RASTER_OK?raw_floor&0x7fu:127u;
            const PortableRacePoint *point=NULL;
            if(index<count && route->row1!=NULL)point=&route->row1[index];
            else if(index==127u)point=route->sentinel_row1;
            uint16_t desired;
            if(point==NULL || portable_driver_track_direction_source_angle(
                shell->position_x_q16,shell->position_y_q16,point->x,point->y,
                route->angle_lut,route->angle_lut_count,&desired)!=0)
                return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
            turn_toward(shell,desired);
            if(shell->lifetime_38)--shell->lifetime_38;
            shell->flags_34|=3u;
            advance_flight(shell,world->engine,world->sine);
            status=PORTABLE_GP_RED_SHELL_OK;
        }else{
            const int32_t dx=trap_delta(target->x_q16,shell->position_x_q16);
            const int32_t dy=trap_delta(target->y_q16,shell->position_y_q16);
            const int32_t squared=signed_bits((uint32_t)dx*(uint32_t)dx+(uint32_t)dy*(uint32_t)dy);
            status=squared<=0x18f?
                portable_gp_red_shell_tick_forward_target_close_clear(shell,target->x_q16,
                    target->y_q16,target->z_q16,target->flags,world->engine,world->sine,world->sine_count):
                portable_gp_red_shell_tick_forward_target_far_clear(shell,target->x_q16,
                    target->y_q16,world->engine,world->sine,world->sine_count);
        }
    }
    if(status!=PORTABLE_GP_RED_SHELL_OK)return status;
    if((int16_t)shell->animation_tick_4c==(int32_t)(shell->rules)->release_lock_ticks)*owner_flags&=UINT32_C(0xfffeffff);
    if(spiny && (shell->animation_tick_4c&15u)==0u){
        shell->flight_audio_pending=1u;
        shell->flight_audio_x_q16=audio_x;shell->flight_audio_y_q16=audio_y;
        shell->flight_audio_heading=shell->angle_26;
        shell->flight_audio_speed=shell->speed_byte_71;
    }
    if(pending)shell->phase=PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING;
    return PORTABLE_GP_RED_SHELL_OK;
}

PortableGpRedShellStatus portable_gp_red_shell_tick_trap(
    PortableGpRedShell *shell,uint32_t *owner_flags,
    const PortableGpRedShellWorld *world,
    const PortableGpRedShellTarget *targets,size_t target_count)
{
    if(shell==NULL || !portable_red_shell_rules_valid(shell->rules))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(shell==NULL||owner_flags==NULL||world==NULL||targets==NULL||
       target_count==0u||target_count>8u||world->engine>2u||
       world->sine==NULL||world->sine_count<1024u)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied||!shell_kind(shell->kind_10))
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(shell->phase==PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING){
        *owner_flags&=UINT32_C(0xfffeffff);begin_destruction(shell);
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if(shell->phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD){
        if(shell->lifetime_38!=0u)--shell->lifetime_38;
        trap_animate(shell);
        PortableDriverMaterialState sample;
        if(!trap_sample(shell,world,&sample))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
        if((shell->material_4e&0x90u)==0x80u){
            shell->phase=PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING;
            return PORTABLE_GP_RED_SHELL_OK;
        }
        if((shell->material_4e&0x20u)!=0u){
            trap_free(shell,owner_flags);return PORTABLE_GP_RED_SHELL_OK;
        }
        shell->flags_34|=3u;
        if(sample.track_cell_valid&&sample.track_cell!=0x7fu)
            shell->floor_36=sample.track_cell&0x7fu;
        if(shell->animation_tick_4c==(shell->rules)->release_lock_ticks)*owner_flags&=UINT32_C(0xfffeffff);
        if(shell->animation_tick_4c==(shell->rules)->trap_arm_ticks){
            shell->animation_tick_4c=0u;shell->phase=PORTABLE_GP_RED_SHELL_TRAP_ARMED;
        }
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if(shell->phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED && shell->spiny_targeting){
        /* 462C0 updates terrain before animation/integration, then reacquires
         * rank zero. At2700 turns even a distant leader starts the lock. */
        PortableDriverMaterialState sample={0};
        if(!trap_terrain(shell,world,&sample))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
        if(sample.track_cell_valid && sample.track_cell!=0x7fu)shell->floor_36=sample.track_cell&0x7fu;
        if(shell->lifetime_38)--shell->lifetime_38;
        if(!shell->lifetime_38)shell->velocity_x_q16=shell->velocity_y_q16=0;
        if(shell->material_4e&0x20u){trap_free(shell,owner_flags);return PORTABLE_GP_RED_SHELL_OK;}
        shell->flags_34|=3u;trap_animate(shell);
        shell->position_x_q16=signed_bits((uint32_t)shell->position_x_q16+(uint32_t)shell->velocity_x_q16);
        shell->position_y_q16=signed_bits((uint32_t)shell->position_y_q16+(uint32_t)shell->velocity_y_q16);
        size_t leader=0u;while(leader<target_count && targets[leader].rank!=0u)++leader;
        if(leader==target_count)return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
        shell->target_valid=1u;shell->target_racer_index=(uint8_t)leader;
        const PortableGpRedShellTarget *target=&targets[leader];
        const int32_t dx=trap_delta(target->x_q16,shell->position_x_q16);
        const int32_t dy=trap_delta(target->y_q16,shell->position_y_q16);
        if((int16_t)shell->animation_tick_4c>=(int32_t)(shell->rules)->trap_lifetime_ticks || (!target->guard_66 &&
            (target->checkpoint&0x7fu)==shell->floor_36 && dx>=-96 && dx<=96 && dy>=-96 && dy<=96)){
            shell->animation_tick_4c=0u;shell->phase=PORTABLE_GP_RED_SHELL_TRAP_LOCKING;
            ++shell->trap_audio_pending;
        }
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if(shell->phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED){
        trap_animate(shell);
        if((int16_t)shell->animation_tick_4c>=(int32_t)(shell->rules)->trap_lifetime_ticks){
            begin_destruction(shell);return PORTABLE_GP_RED_SHELL_OK;
        }
        for(size_t i=0u;i<target_count;++i){
            const PortableGpRedShellTarget *target=&targets[i];
            const int32_t dx=trap_delta(target->x_q16,shell->position_x_q16);
            const int32_t dy=trap_delta(target->y_q16,shell->position_y_q16);
            if(target->guard_66!=0 || (target->checkpoint&0x7fu)!=shell->floor_36 ||
               dx>32||dx<-32||dy>32||dy<-32)continue;
            shell->target_valid=1u;shell->target_racer_index=(uint8_t)i;
            shell->animation_tick_4c=0u;shell->phase=PORTABLE_GP_RED_SHELL_TRAP_LOCKING;
            ++shell->trap_audio_pending; /* Every eligible list entry queues127. */
        }
        return PORTABLE_GP_RED_SHELL_OK;
    }
    if(shell->phase!=PORTABLE_GP_RED_SHELL_TRAP_LOCKING&&
       shell->phase!=PORTABLE_GP_RED_SHELL_TRAP_CHASING)
        return PORTABLE_GP_RED_SHELL_INELIGIBLE;
    if(shell->spiny_targeting && shell->phase==PORTABLE_GP_RED_SHELL_TRAP_CHASING)
        for(size_t i=0u;i<target_count;++i)if(targets[i].rank==0u){shell->target_racer_index=(uint8_t)i;break;}
    if(shell->target_racer_index>=target_count)
        return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    const PortableGpRedShellTarget *target=&targets[shell->target_racer_index];
    const int32_t dx=trap_delta(target->x_q16,shell->position_x_q16);
    const int32_t dy=trap_delta(target->y_q16,shell->position_y_q16);
    if(shell->phase==PORTABLE_GP_RED_SHELL_TRAP_LOCKING){
        trap_animate(shell);
        if(shell->animation_tick_4c==(shell->rules)->trap_lock_ticks){
            shell->angle_26=portable_gp_red_shell_target_angle((int16_t)dx,(int16_t)dy);
            shell->animation_tick_4c=0u;
            shell->speed_byte_71=world->engine==0u?0x40u:(world->engine==2u?0x52u:0x48u);
            shell->phase=PORTABLE_GP_RED_SHELL_TRAP_CHASING;
        }
        return PORTABLE_GP_RED_SHELL_OK;
    }
    PortableDriverMaterialState sample={0};
    if(!trap_terrain(shell,world,&sample))return PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT;
    if(sample.track_cell_valid&&sample.track_cell!=0x7fu)
        shell->floor_36=sample.track_cell&0x7fu;
    if(shell->lifetime_38!=0u)--shell->lifetime_38;
    if((shell->material_4e&0x20u)!=0u){
        trap_free(shell,owner_flags);return PORTABLE_GP_RED_SHELL_OK;
    }
    shell->flags_34|=3u;
    const uint16_t desired=portable_gp_red_shell_target_angle((int16_t)dx,(int16_t)dy);
    if(shell->steering_delay_7a!=0u){--shell->steering_delay_7a;turn_toward(shell,desired);}
    else if(target->z_q16!=0||dx>12||dx<-12||dy>12||dy<-12)turn_toward(shell,desired);
    else shell->angle_26=desired;
    const uint32_t squared=(uint32_t)dx*(uint32_t)dx+(uint32_t)dy*(uint32_t)dy;
    uint8_t speed=flight_target_speed(shell,world->engine);
    if(signed_bits(squared)<=0x18f){
        speed=(target->flags&0x8000u)!=0u?0x3cu:0x4cu;
        if(world->engine==0u)speed-=8u;
        else if(world->engine==2u)speed+=10u;
    }
    advance_flight_to_speed(shell,speed,world->sine);
    return PORTABLE_GP_RED_SHELL_OK;
}
