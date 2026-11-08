#include "gp_green_shell.h"
#include "gp_shell_orbit.h"

#include <string.h>

static int32_t source_angle_index(int16_t angle)
{
    int32_t adjusted = angle;
    if (adjusted < 0)
        adjusted += 0x3f;
    if (adjusted >= 0)
        return adjusted / 64;
    return -(((-adjusted) + 63) / 64);
}

static int32_t add_wrap_i32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t subtract_wrap_i32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static void begin_destruction(PortableGpGreenShell *shell)
{
    shell->destruction_field_5a = UINT16_C(0x8000);
    shell->vertical_velocity_q16 = (int32_t)(shell->rules)->destruction_velocity_q16;
    shell->flags_34 &= UINT16_C(0xfffd);
    shell->velocity_z_q16 = 0;
    shell->phase = PORTABLE_GP_GREEN_SHELL_DESTROYING;
}

static void attach_to_owner(PortableGpGreenShellOwner *owner,
                            PortableGpGreenShell *shell,
                            const int32_t *sine)
{
    const int16_t signed_angle = (int16_t)owner->angle;
    const int32_t index = source_angle_index(signed_angle);
    const uint32_t forward = (uint32_t)sine[(uint32_t)index & 0x3ffu] << 4u;
    const uint32_t perpendicular =
        (uint32_t)sine[(0x100u - (uint32_t)index) & 0x3ffu] << 4u;

    shell->angle_26 = owner->angle;
    shell->position_x_q16 = add_wrap_i32(owner->position_x_q16,
                                         (int32_t)forward);
    shell->position_y_q16 = subtract_wrap_i32(owner->position_y_q16,
                                               (int32_t)perpendicular);
    shell->position_z_q16 = owner->position_z_q16;
    shell->floor_36 = owner->floor_36;
}

PortableGpGreenShellStatus portable_gp_green_shell_use(
    PortableGpItemInventory *inventory,
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count)
{
    if (inventory == NULL || owner == NULL || shell == NULL || sine == NULL ||
        sine_count < 1024u)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!portable_green_shell_rules_valid((owner->rules)))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (inventory->held_item_17a != PORTABLE_GP_GREEN_SHELL_ITEM ||
        ((owner->flags_48&0x2000u)==0u && (owner->control_96 & UINT16_C(0x0080)) == 0u) ||
        (owner->flags_48 & UINT32_C(0x00000800)) != 0u ||
        owner->item_use_state_17c != 0u ||
        (owner->flags_48 & UINT32_C(0x00010000)) != 0u || shell->occupied)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;

    memset(shell, 0, sizeof *shell);
    shell->rules=owner->rules;
    shell->orbit_rules=owner->orbit_rules;
    shell->occupied = 1u;
    shell->kind_10 = PORTABLE_GP_GREEN_SHELL_KIND;
    shell->phase = PORTABLE_GP_GREEN_SHELL_ATTACHED;
    attach_to_owner(owner, shell, sine);

    owner->flags_48 &= UINT32_C(0xfffeffff);
    owner->item_use_state_17c = 1u;
    inventory->held_item_17a = PORTABLE_GP_ITEM_NONE;
    inventory->preview_item = 0u;
    inventory->settled_item = 0u;
    inventory->active = 0u;
    return PORTABLE_GP_GREEN_SHELL_OK;
}

static PortableGpShellOrbitView orbit_view(PortableGpGreenShell *shell)
{
    return (PortableGpShellOrbitView){&shell->position_x_q16,&shell->position_y_q16,
        &shell->position_z_q16,&shell->angle_26,&shell->flags_34,&shell->animation_tick_4c,
        &shell->floor_36,&shell->animation_index_70,&shell->display_index,shell->orbit_rules};
}

static PortableGpShellOrbitOwner orbit_owner(const PortableGpGreenShellOwner *owner)
{
    return (PortableGpShellOrbitOwner){owner->position_x_q16,owner->position_y_q16,
        owner->position_z_q16,owner->floor_36,owner->mode_78};
}

static void orbit_position(const PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,const int32_t *sine)
{
    const PortableGpShellOrbitOwner view=orbit_owner(owner);
    portable_gp_shell_orbit_position(orbit_view(shell),&view,sine);
}

PortableGpGreenShellStatus portable_gp_green_shell_use_triple(
    PortableGpItemInventory *inventory,PortableGpGreenShellOwner *owner,
    PortableGpGreenShell shells[3],const uint8_t delays[3],
    const int32_t *sine,size_t sine_count)
{
    if(inventory==NULL || owner==NULL || shells==NULL || delays==NULL ||
       sine==NULL || sine_count<1024u)return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!portable_green_shell_rules_valid((owner->rules)))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!portable_shell_orbit_rules_valid((owner->orbit_rules)))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(inventory->held_item_17a!=PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM ||
       ((owner->flags_48&0x2000u)==0u && !(owner->control_96&0x80u)) ||
       (owner->flags_48&0x10800u) || owner->item_use_state_17c)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    for(unsigned i=0u;i<3u;++i){
        shells[i]=(PortableGpGreenShell){.occupied=1u,.kind_10=PORTABLE_GP_GREEN_SHELL_KIND,
            .phase=PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING,.release_timer_74=delays[i],
            .retire_held_slot=1u,.rules=owner->rules,.orbit_rules=owner->orbit_rules};
        orbit_position(owner,&shells[i],sine);
    }
    owner->flags_48&=UINT32_C(0xfffeffff);owner->item_use_state_17c=3u;
    inventory->held_item_17a=0u;inventory->preview_item=0u;
    inventory->settled_item=0u;inventory->active=0u;
    return PORTABLE_GP_GREEN_SHELL_OK;
}

PortableGpGreenShellStatus portable_gp_green_shell_tick_orbit(
    PortableGpGreenShellOwner *owner,PortableGpGreenShell *shell,
    const int32_t *sine,size_t sine_count)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(owner==NULL || shell==NULL || sine==NULL || sine_count<1024u)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!portable_shell_orbit_rules_valid((shell->orbit_rules)))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied || shell->kind_10!=PORTABLE_GP_GREEN_SHELL_KIND ||
       (shell->phase!=PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING &&
        shell->phase!=PORTABLE_GP_GREEN_SHELL_ORBITING))return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    if(shell->phase==PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING){
        if(shell->release_timer_74){--shell->release_timer_74;return PORTABLE_GP_GREEN_SHELL_OK;}
        shell->angle_26=0u;orbit_position(owner,shell,sine);
        shell->flags_34|=3u;shell->animation_tick_4c=0u;shell->state_71=0u;
        shell->phase=PORTABLE_GP_GREEN_SHELL_ORBITING;shell->orbit_audio_pending=1u;
        return PORTABLE_GP_GREEN_SHELL_OK;
    }
    const PortableGpShellOrbitOwner view=orbit_owner(owner);
    portable_gp_shell_orbit_tick(orbit_view(shell),&view,sine);
    if((owner->control_96&0x80u) && !(owner->flags_48&0x10800u)){
        /*4676C receives angle zero here, independent of Down/CPU drop. */
        PortableGpGreenShellOwner forward=*owner;
        forward.control_96&=UINT16_C(0xfbbf);forward.decision_190&=UINT8_C(0xfd);
        shell->phase=PORTABLE_GP_GREEN_SHELL_ACTIVE;
        const PortableGpGreenShellStatus status=portable_gp_green_shell_release(&forward,shell,sine,sine_count);
        owner->flags_48=forward.flags_48;owner->item_use_state_17c=forward.item_use_state_17c;
        owner->shell_item_18c=forward.shell_item_18c;shell->retire_held_slot=0u;
        return status;
    }
    return PORTABLE_GP_GREEN_SHELL_OK;
}

PortableGpGreenShellStatus portable_gp_green_shell_tick_attached(
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (owner == NULL || shell == NULL || sine == NULL || sine_count < 1024u)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || shell->kind_10 != PORTABLE_GP_GREEN_SHELL_KIND ||
        shell->phase != PORTABLE_GP_GREEN_SHELL_ATTACHED)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;

    attach_to_owner(owner, shell, sine);
    shell->flags_34 |= UINT16_C(0x0003);
    shell->animation_tick_4c = 0u;
    shell->state_71 = 0u;
    owner->shell_item_18c = PORTABLE_GP_GREEN_SHELL_ITEM;
    shell->phase = PORTABLE_GP_GREEN_SHELL_ACTIVE;
    return PORTABLE_GP_GREEN_SHELL_OK;
}

PortableGpGreenShellStatus portable_gp_green_shell_tick_held_active(
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (owner == NULL || shell == NULL || sine == NULL || sine_count < 1024u)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || shell->kind_10 != PORTABLE_GP_GREEN_SHELL_KIND ||
        shell->phase != PORTABLE_GP_GREEN_SHELL_ACTIVE || owner->response_status_7e>=16u)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    PortableGpGreenShellOwner attachment=*owner;
    if((UINT16_C(0xde1f)&(1u<<owner->response_status_7e))==0u)
        attachment.angle=(uint16_t)(owner->target_heading_56+0x8000u);
    attach_to_owner(&attachment, shell, sine);
    if ((shell->animation_tick_4c & UINT16_C(7)) == 0u)
    {
        shell->display_index = shell->animation_index_70;
        ++shell->animation_index_70;
        if (shell->animation_index_70 > 5u)
            shell->animation_index_70 = 0u;
    }
    ++shell->animation_tick_4c;
    if(owner->mode_78==2u || ((owner->mode_78==3u || owner->mode_78==4u) && owner->position_z_q16==0))
        shell->flags_34&=UINT16_C(0xfffc);
    else shell->flags_34|=3u;
    if((owner->flags_48&0x2000u)!=0u && (int16_t)shell->animation_tick_4c>=(int32_t)(shell->rules)->cpu_hold_ticks)
        owner->decision_190&=UINT8_C(0xfe);
    if((owner->control_96&0x40u)!=0u ||
       ((owner->flags_48&0x2000u)!=0u && (owner->decision_190&1u)))
        return PORTABLE_GP_GREEN_SHELL_OK;
    return portable_gp_green_shell_release(owner,shell,sine,sine_count);
}

PortableGpGreenShellStatus portable_gp_green_shell_release(
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    int32_t speed;
    int16_t angle;
    int32_t index;
    int64_t magnitude;

    if (owner == NULL || shell == NULL || sine == NULL || sine_count < 1024u)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || shell->kind_10 != PORTABLE_GP_GREEN_SHELL_KIND ||
        shell->phase != PORTABLE_GP_GREEN_SHELL_ACTIVE ||
        (owner->control_96 & UINT16_C(0x0040)) != 0u)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;

    const PortableGreenShellRules *rules=(owner->rules);
    if(!portable_green_shell_rules_valid(rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    shell->rules=owner->rules;
    shell->orbit_rules=owner->orbit_rules;
    shell->position_x_q16 = owner->position_x_q16;
    shell->position_y_q16 = owner->position_y_q16;
    shell->position_z_q16 = 0;
    shell->velocity_x_q16 = 0;
    shell->velocity_y_q16 = 0;
    shell->velocity_z_q16 = 0;
    shell->lifetime_38 = rules->collision_grace_ticks;
    shell->animation_tick_4c = 0u;
    shell->collision_flags_4e = 0u;
    shell->collision_state_50 = 0u;
    shell->terrain_state_52 = 0u;
    shell->release_timer_74 = (uint8_t)rules->wall_contact_budget;

    const uint16_t backward=(owner->flags_48&0x2000u)==0u?
        (owner->control_96&0x400u):(owner->decision_190&2u);
    angle = backward != 0u
                ? (int16_t)INT16_MIN : 0;
    angle = (int16_t)(uint16_t)((uint16_t)angle +
                                (uint16_t)owner->release_angle_offset);
    shell->angle_26 = (uint16_t)angle;
    speed = owner->speed_q16 / INT32_C(0x10000);
    speed = (speed + rules->launch_speed_bonus) / 16;
    shell->state_71 = (uint8_t)speed;
    if (shell->state_71 >= rules->max_speed)
        shell->state_71 = (uint8_t)rules->max_speed;
    magnitude = (int64_t)shell->state_71 << 4;
    index = source_angle_index(angle);
    shell->velocity_x_q16 = (int32_t)(
        magnitude * sine[(uint32_t)index & 0x3ffu] / 128);
    shell->velocity_y_q16 = (int32_t)(
        -magnitude * sine[(0x100u - (uint32_t)index) & 0x3ffu] / 128);
    shell->phase = PORTABLE_GP_GREEN_SHELL_RELEASED;
    shell->launch_audio_pending = 1u;
    owner->item_use_state_17c = (uint8_t)(owner->item_use_state_17c - 1u);
    owner->shell_item_18c = 0u;
    owner->flags_48 |= UINT32_C(0x00010000);
    return PORTABLE_GP_GREEN_SHELL_OK;
}

static int sample_terrain(PortableGpGreenShell *shell,const PortableGpGreenShellWorld *world,
    PortableDriverMaterialState *sample)
{
    const int status=portable_driver_material_sample(world->material,world->raster,world->terrain,
        shell->position_x_q16,shell->position_y_q16,sample);
    if(status!=PORTABLE_DRIVER_MATERIAL_OK && status!=PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS)return 0;
    shell->collision_flags_4e=sample->material_flags;
    return 1;
}

/*44AAC still completes4BA0 after the seventh wall contact schedules7884.
 * Once50 bit4000 is set, geometry runs but the contact budget is retained
 * until a clear sample resets50. Airborne entries retain the prior material. */
static int terrain_response(PortableGpGreenShell *shell,const PortableGpGreenShellWorld *world)
{
    if(shell->position_z_q16!=0)return 1;
    PortableDriverMaterialState sample;
    if(!sample_terrain(shell,world,&sample))return 0;
    if(!sample.in_bounds)return 1;
    PortableDriverTerrainEffectState cell={.cell_key=shell->terrain_state_52};
    if(portable_driver_terrain_effect_tick(&sample,0,&cell)!=0)return 0;
    shell->terrain_state_52=cell.cell_key;
    if(!cell.effect_active){shell->collision_state_50=0u;return 1;}
    PortableDriverTerrainEffectDispatch dispatch;
    if(portable_driver_terrain_effect_dispatch(&sample,world->material,world->terrain,
        world->reflection,cell.effect_dispatch_delta,&shell->velocity_x_q16,
        &shell->velocity_y_q16,&dispatch)!=0)return 0;
    if(shell->collision_state_50&0x4000u)return 1;
    const unsigned count=(shell->collision_state_50&7u)+1u;
    shell->collision_state_50=(uint16_t)((shell->collision_state_50&0xfff8u)|count|
        (count<=7u?0x8000u:0xc000u));
    shell->contact_audio_pending=1u;
    if(--shell->release_timer_74==0u)shell->phase=PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING;
    return 1;
}

PortableGpGreenShellStatus portable_gp_green_shell_tick_released(
    PortableGpGreenShellOwner *owner,PortableGpGreenShell *shell,const PortableGpGreenShellWorld *world)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(owner==NULL || shell==NULL || world==NULL || world->material==NULL ||
       world->raster==NULL || world->terrain==NULL)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!shell->occupied || shell->kind_10!=PORTABLE_GP_GREEN_SHELL_KIND ||
       (shell->phase!=PORTABLE_GP_GREEN_SHELL_RELEASED &&
        shell->phase!=PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING))
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    if(shell->phase==PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING){
        owner->flags_48&=UINT32_C(0xfffeffff);begin_destruction(shell);
        return PORTABLE_GP_GREEN_SHELL_OK;
    }
    if(!terrain_response(shell,world))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    uint8_t floor;
    const int status=portable_track_raster_query16(world->raster,
        (int16_t)(shell->position_x_q16/65536),(int16_t)(shell->position_y_q16/65536),&floor);
    if(status==PORTABLE_TRACK_RASTER_OK){if(floor!=0x7fu)shell->floor_36=floor&0x7fu;}
    else if(status!=PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(shell->lifetime_38)--shell->lifetime_38;
    if(shell->collision_flags_4e&0x20u){
        owner->flags_48&=UINT32_C(0xfffeffff);shell->occupied=0u;
        shell->flags_34&=UINT16_C(0xfffd);shell->phase=PORTABLE_GP_GREEN_SHELL_DESTROYED;
        return PORTABLE_GP_GREEN_SHELL_OK;
    }
    shell->flags_34|=3u;
    if((shell->animation_tick_4c&7u)==0u){
        shell->display_index=shell->animation_index_70;
        shell->animation_index_70=(uint8_t)((shell->animation_index_70+1u)%6u);
    }
    ++shell->animation_tick_4c;
    if((int16_t)shell->animation_tick_4c==(int32_t)(shell->rules)->release_lock_ticks)owner->flags_48&=UINT32_C(0xfffeffff);
    shell->position_x_q16=add_wrap_i32(shell->position_x_q16,shell->velocity_x_q16);
    shell->position_y_q16=add_wrap_i32(shell->position_y_q16,shell->velocity_y_q16);
    return PORTABLE_GP_GREEN_SHELL_OK;
}

/* 446FC is the attached-shell callback. Unlike445CC it has no same-owner
 * shell exclusion or target reset, and retires one held owner slot. */
PortableGpGreenShellStatus portable_gp_green_shell_collide_held(
    PortableGpGreenShell *shell, PortableGpGreenShellOwner *owner,
    uint16_t other_kind, int32_t other_z_q16, int16_t other_field_66)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    const uint16_t family = (uint16_t)(other_kind & UINT16_C(0xf000));
    if (shell == NULL || owner == NULL)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || shell->kind_10 != PORTABLE_GP_GREEN_SHELL_KIND ||
        (shell->phase != PORTABLE_GP_GREEN_SHELL_ACTIVE &&
         shell->phase != PORTABLE_GP_GREEN_SHELL_ORBITING))
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    if (other_kind == UINT16_C(0x8800) || family == UINT16_C(0x1000) ||
        shell->position_z_q16 > INT32_C(0x41fff) ||
        other_z_q16 > INT32_C(0x41fff) ||
        (family == UINT16_C(0x2000) && other_field_66 != 0)) {
        shell->collision_result = PORTABLE_GP_GREEN_SHELL_COLLISION_IGNORED;
        return PORTABLE_GP_GREEN_SHELL_OK;
    }
    owner->flags_48 &= UINT32_C(0xfffeffff);
    owner->item_use_state_17c = (uint8_t)(owner->item_use_state_17c - 1u);
    owner->shell_item_18c = 0u;
    begin_destruction(shell);
    shell->collision_result = family == UINT16_C(0x8000)
        ? PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING_ITEM
        : PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING;
    shell->contact_audio_pending = (uint8_t)(family == UINT16_C(0x8000));
    return PORTABLE_GP_GREEN_SHELL_OK;
}

PortableGpGreenShellStatus portable_gp_green_shell_collide(
    PortableGpGreenShell *shell, uint32_t *owner_flags,
    uint16_t other_kind, int32_t other_z_q16, int16_t other_field_66,
    uint8_t same_owner)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    const uint16_t family = (uint16_t)(other_kind & UINT16_C(0xf000));
    if (shell == NULL || owner_flags == NULL)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || shell->kind_10 != PORTABLE_GP_GREEN_SHELL_KIND ||
        (shell->phase != PORTABLE_GP_GREEN_SHELL_RELEASED &&
         shell->phase != PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING))
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    if (other_kind == UINT16_C(0x8800) || family == UINT16_C(0x1000) ||
        shell->position_z_q16 > INT32_C(0x00041fff) ||
        other_z_q16 > INT32_C(0x00041fff) ||
        (other_kind == PORTABLE_GP_GREEN_SHELL_KIND && same_owner) ||
        (family == UINT16_C(0x2000) && other_field_66 != 0))
    {
        shell->collision_result = PORTABLE_GP_GREEN_SHELL_COLLISION_IGNORED;
        return PORTABLE_GP_GREEN_SHELL_OK;
    }
    *owner_flags &= UINT32_C(0xfffeffff);
    begin_destruction(shell);
    shell->collision_result = family == UINT16_C(0x8000)
        ? PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING_ITEM
        : PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING;
    shell->contact_audio_pending = (uint8_t)(family == UINT16_C(0x8000));
    return PORTABLE_GP_GREEN_SHELL_OK;
}

PortableGpGreenShellStatus portable_gp_green_shell_take_contact_audio(
    PortableGpGreenShell *shell, uint8_t suppressed, uint16_t spatial_volume,
    PortableGpGreenShellAudioEvent *event)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (shell == NULL || event == NULL)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->contact_audio_pending)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    shell->contact_audio_pending = 0u;
    if (suppressed || spatial_volume == 0u)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    event->song_id = 124u;
    event->player = 4u;
    event->volume = spatial_volume;
    return PORTABLE_GP_GREEN_SHELL_OK;
}

PortableGpGreenShellStatus portable_gp_green_shell_tick_destroying(
    PortableGpGreenShell *shell)
{
    if(shell==NULL || !portable_green_shell_rules_valid(shell->rules))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (shell == NULL)
        return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if(!portable_green_shell_rules_valid((shell->rules)))return PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT;
    if (!shell->occupied || shell->kind_10 != PORTABLE_GP_GREEN_SHELL_KIND ||
        shell->phase != PORTABLE_GP_GREEN_SHELL_DESTROYING)
        return PORTABLE_GP_GREEN_SHELL_INELIGIBLE;
    shell->position_z_q16 = add_wrap_i32(
        shell->position_z_q16, shell->vertical_velocity_q16);
    shell->vertical_velocity_q16 = add_wrap_i32(
        shell->vertical_velocity_q16, -(int32_t)(shell->rules)->destruction_gravity_q16);
    if (shell->position_z_q16 < 0)
    {
        shell->position_z_q16 = 0;
        shell->vertical_velocity_q16 = 0;
        shell->occupied = 0u;
        shell->phase = PORTABLE_GP_GREEN_SHELL_DESTROYED;
    }
    return PORTABLE_GP_GREEN_SHELL_OK;
}
