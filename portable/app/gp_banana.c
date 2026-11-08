#include "gp_banana.h"

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

static void attach_to_owner(const PortableGpBananaOwner *owner,
                            PortableGpBanana *banana,
                            const int32_t *sine)
{
    const int32_t index = source_angle_index((int16_t)owner->angle);
    const uint32_t forward =
        (uint32_t)sine[(uint32_t)index & 0x3ffu] << 4u;
    const uint32_t perpendicular =
        (uint32_t)sine[(0x100u - (uint32_t)index) & 0x3ffu] << 4u;

    banana->angle_26 = owner->angle;
    banana->position_x_q16 = add_wrap_i32(owner->position_x_q16,
                                          (int32_t)forward);
    banana->position_y_q16 = subtract_wrap_i32(owner->position_y_q16,
                                                (int32_t)perpendicular);
    banana->position_z_q16 = owner->position_z_q16;
    banana->floor_36 = owner->floor_36;
}

PortableGpBananaStatus portable_gp_banana_use(
    PortableGpItemInventory *inventory,
    PortableGpBananaOwner *owner,
    PortableGpBanana *banana,
    const int32_t *sine,
    size_t sine_count)
{
    PortableGpItemUseDispatch dispatch;
    if (inventory == NULL || owner == NULL || banana == NULL || sine == NULL ||
        sine_count < 1024u)
        return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if (inventory->held_item_17a != PORTABLE_GP_BANANA_ITEM ||
        !portable_gp_item_use_dispatch(PORTABLE_GP_BANANA_ITEM,&dispatch) ||
        dispatch.primary_callback != UINT32_C(0x08048090) ||
        ((owner->flags_48&0x2000u)==0u && (owner->control_96 & UINT16_C(0x0080)) == 0u) ||
        (owner->flags_48 & UINT32_C(0x00010800)) != 0u ||
        owner->item_use_state_17c != 0u || banana->occupied)
        return PORTABLE_GP_BANANA_INELIGIBLE;

    if(!portable_banana_rules_valid((owner->rules)))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    memset(banana,0,sizeof *banana);
    banana->rules=owner->rules;
    banana->occupied = 1u;
    banana->kind_10 = PORTABLE_GP_BANANA_KIND;
    banana->flags_34 = UINT16_C(3);
    banana->phase = PORTABLE_GP_BANANA_HELD;
    attach_to_owner(owner,banana,sine);
    owner->shell_item_18c = PORTABLE_GP_BANANA_ITEM;
    inventory->held_item_17a = PORTABLE_GP_ITEM_NONE;
    inventory->preview_item = 0u;
    inventory->settled_item = 0u;
    inventory->active = 0u;
    return PORTABLE_GP_BANANA_OK;
}

static int32_t signed_bits(uint32_t bits)
{
    return bits<=INT32_MAX?(int32_t)bits:-INT32_C(1)-(int32_t)(UINT32_MAX-bits);
}

/* 476C4 truncates Q16 toward zero, then28D4 retains the old floor only for
 * exact7F; FF is a real source sample and writes the masked127 sentinel. */
static int update_floor(PortableGpBanana *banana,const PortableGpBananaWorld *world)
{
    uint8_t cell;
    const int result=portable_track_raster_query16(world->raster,
        (int16_t)(banana->position_x_q16/65536),
        (int16_t)(banana->position_y_q16/65536),&cell);
    if(result==PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)return 1;
    if(result!=PORTABLE_TRACK_RASTER_OK)return 0;
    if(cell!=0x7fu)banana->floor_36=cell&0x7fu;
    return 1;
}

/* 770C checks finite Q16 bounds before truncating world coordinates. */
static int sample_material(PortableGpBanana *banana,const PortableGpBananaWorld *world)
{
    const PortableTerrainView *terrain=world->terrain;
    if(terrain==NULL || terrain->cells==NULL || terrain->width==0u || terrain->height==0u)
        return 0;
    if(banana->position_x_q16<0 || banana->position_y_q16<0 ||
       (int64_t)banana->position_x_q16>=(int64_t)terrain->width*8*65536 ||
       (int64_t)banana->position_y_q16>=(int64_t)terrain->height*8*65536){
        banana->material_4e=0x26u;return 1;
    }
    uint8_t tile;
    if(portable_terrain_query8(terrain,(int16_t)(banana->position_x_q16/65536),
        (int16_t)(banana->position_y_q16/65536),&tile)!=PORTABLE_TERRAIN_QUERY_OK)
        return 0;
    return portable_track_material_query(world->material,tile,&banana->material_4e)==0;
}

static void begin_destruction(PortableGpBanana *banana)
{
    banana->destruction_field_5a=UINT16_C(0x8000);
    banana->vertical_velocity_q16=(int32_t)(banana->rules)->destruction_velocity_q16;
    banana->flags_34&=UINT16_C(0xfffd);
    banana->collision_active=0u;
    banana->phase=PORTABLE_GP_BANANA_DESTROYING;
}

static void free_banana(PortableGpBananaOwner *owner,PortableGpBanana *banana)
{
    owner->flags_48&=UINT32_C(0xfffeffff);
    banana->occupied=0u;banana->collision_active=0u;
    banana->flags_34&=UINT16_C(0xfffd);
    banana->phase=PORTABLE_GP_BANANA_DESTROYED;
}

static int release(PortableGpBananaOwner *owner,PortableGpBanana *banana,
    const PortableGpBananaWorld *world,const int32_t *sine,int forward)
{
    owner->shell_item_18c=PORTABLE_GP_ITEM_NONE;
    if(!forward){
        if(!sample_material(banana,world))return 0;
        const uint8_t material=banana->material_4e;
        if((material&0x90u)==0x80u){
            owner->flags_48&=UINT32_C(0xfffeffff);begin_destruction(banana);return 1;
        }
        if((material&0x20u)!=0u || material==0x26u){free_banana(owner,banana);return 1;}
        banana->position_z_q16=0;
        if(!update_floor(banana,world))return 0;
        banana->phase=PORTABLE_GP_BANANA_DROPPED;
        banana->collision_active=1u;banana->motion_audio_pending=122u;
        return 1;
    }
    banana->position_z_q16=INT32_C(0x10000);
    if(!(owner->flags_48&0x2000u)){
        banana->angle_26=owner->release_angle;
        const uint32_t index=(uint32_t)source_angle_index((int16_t)owner->release_angle)&0x3ffu;
        banana->velocity_x_q16=signed_bits((uint32_t)((int64_t)sine[index]*(banana->rules)->throw_speed_q16/32768));
        banana->velocity_y_q16=signed_bits(0u-(uint32_t)((int64_t)sine[(256u-index)&0x3ffu]*(banana->rules)->throw_speed_q16/32768));
    }else{
        if(world->checkpoint_count<3u || world->route==NULL || world->route_rows==NULL ||
           world->route_row_count<2u || owner->route_row_191>2u)return 0;
        size_t checkpoint=owner->floor_36+3u;
        if(checkpoint>=world->checkpoint_count)checkpoint-=world->checkpoint_count;
        if(checkpoint>=world->checkpoint_count)return 0;
        const size_t row=(owner->route_row_191+1u)%3u;
        const PortableRacePoint target=row==0u?world->route[checkpoint]:
            world->route_rows[(row-1u)*world->checkpoint_count+checkpoint];
        banana->angle_26=0u;
        banana->velocity_x_q16=signed_bits(((uint32_t)target.x<<16u)-(uint32_t)banana->position_x_q16)/(int32_t)(banana->rules)->cpu_flight_ticks;
        banana->velocity_y_q16=signed_bits(((uint32_t)target.y<<16u)-(uint32_t)banana->position_y_q16)/(int32_t)(banana->rules)->cpu_flight_ticks;
    }
    banana->vertical_velocity_q16=(int32_t)(banana->rules)->throw_velocity_q16;
    if(!update_floor(banana,world))return 0;
    banana->collision_guard_38=(uint16_t)(banana->rules)->collision_grace_ticks;banana->attachment_ticks_4c=0u;
    owner->flags_48|=UINT32_C(0x10000);
    banana->phase=PORTABLE_GP_BANANA_THROWN;banana->collision_active=1u;
    banana->motion_audio_pending=123u;
    return 1;
}

PortableGpBananaStatus portable_gp_banana_tick(
    PortableGpBananaOwner *owner,PortableGpBanana *banana,
    const PortableGpBananaWorld *world,const int32_t *sine,size_t sine_count)
{
    if(banana==NULL || !portable_banana_rules_valid(banana->rules))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(owner==NULL || banana==NULL || world==NULL || world->raster==NULL ||
       world->material==NULL || world->terrain==NULL || sine==NULL || sine_count<1024u)
        return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(!banana->occupied || banana->kind_10!=PORTABLE_GP_BANANA_KIND ||
       (banana->phase!=PORTABLE_GP_BANANA_HELD && banana->phase!=PORTABLE_GP_BANANA_THROWN))
        return PORTABLE_GP_BANANA_INELIGIBLE;
    PortableGpBanana next=*banana;PortableGpBananaOwner next_owner=*owner;
    ++next.attachment_ticks_4c;
    if(next.phase==PORTABLE_GP_BANANA_HELD){
        if(owner->response_status_7e>=16u)return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
        PortableGpBananaOwner attachment=*owner;
        if(((UINT16_C(0xde1f)>>owner->response_status_7e)&1u)==0u)
            attachment.angle=(uint16_t)(owner->target_heading_56+UINT16_C(0x8000));
        attach_to_owner(&attachment,&next,sine);
        const int visible=owner->mode_78!=2u &&
            !((owner->mode_78==3u || owner->mode_78==4u) && owner->position_z_q16==0);
        if(visible)next.flags_34|=3u;else next.flags_34&=UINT16_C(0xfffc);
        int hold,forward;
        if(owner->flags_48&0x8000u){
            hold=(owner->control_96&0x40u)!=0u;forward=(owner->control_96&0x800u)!=0u;
        }else{
            if((int16_t)next.attachment_ticks_4c>=(int32_t)(next.rules)->cpu_hold_ticks)next_owner.decision_190&=UINT8_C(0xfe);
            hold=(next_owner.decision_190&1u)!=0u;forward=(next_owner.decision_190&2u)==0u;
        }
        if(!hold && !release(&next_owner,&next,world,sine,forward))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    }else{
        if(next.collision_guard_38!=0u)--next.collision_guard_38;
        next.position_x_q16=signed_bits((uint32_t)next.position_x_q16+(uint32_t)next.velocity_x_q16);
        next.position_y_q16=signed_bits((uint32_t)next.position_y_q16+(uint32_t)next.velocity_y_q16);
        next.position_z_q16=signed_bits((uint32_t)next.position_z_q16+(uint32_t)next.vertical_velocity_q16);
        next.vertical_velocity_q16=signed_bits((uint32_t)next.vertical_velocity_q16-(next.rules)->throw_gravity_q16);
        if(!update_floor(&next,world))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
        if(next.position_z_q16<=0){
            next.position_z_q16=0;next.vertical_velocity_q16=0;
            next.phase=PORTABLE_GP_BANANA_DROPPED;next.motion_audio_pending=122u;
            if(!sample_material(&next,world))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
            if((next.material_4e&0x90u)==0x80u || next.material_4e==0x26u)begin_destruction(&next);
            else if(next.material_4e&0x20u)free_banana(&next_owner,&next);
        }
        if(next.phase!=PORTABLE_GP_BANANA_DESTROYING && next.phase!=PORTABLE_GP_BANANA_DESTROYED &&
           (int16_t)next.attachment_ticks_4c==(int32_t)(next.rules)->release_lock_ticks)next_owner.flags_48&=UINT32_C(0xfffeffff);
    }
    *banana=next;*owner=next_owner;
    return PORTABLE_GP_BANANA_OK;
}

PortableGpBananaStatus portable_gp_banana_take_motion_audio(
    PortableGpBanana *banana,uint8_t suppressed,int32_t camera_x,int32_t camera_y,
    PortableGpBananaAudioEvent *event)
{
    if(banana==NULL || !portable_banana_rules_valid(banana->rules))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(banana==NULL || event==NULL)return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(!banana->motion_audio_pending)return PORTABLE_GP_BANANA_INELIGIBLE;
    const uint16_t song=banana->motion_audio_pending;banana->motion_audio_pending=0u;
    if(suppressed)return PORTABLE_GP_BANANA_INELIGIBLE;
    const uint16_t spatial_volume=portable_gp_item_spatial_volume(banana->position_x_q16,
        banana->position_y_q16,camera_x,camera_y);
    *event=(PortableGpBananaAudioEvent){.song_id=song,.player=4u,
        .volume=spatial_volume<64u?64u:spatial_volume};
    return PORTABLE_GP_BANANA_OK;
}

static PortableGpBananaStatus collide(
    PortableGpBanana *banana,uint32_t *owner_flags,uint16_t other_kind,
    int16_t other_field_66,uint8_t held)
{
    const uint16_t family=(uint16_t)(other_kind&UINT16_C(0xf000));
    if(banana==NULL||owner_flags==NULL)
        return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(!banana->occupied||banana->kind_10!=PORTABLE_GP_BANANA_KIND||
       (held?banana->phase!=PORTABLE_GP_BANANA_HELD:
        (banana->phase!=PORTABLE_GP_BANANA_DROPPED && banana->phase!=PORTABLE_GP_BANANA_THROWN))||
       (!held&&!banana->collision_active))
        return PORTABLE_GP_BANANA_INELIGIBLE;
    /* sub_804791C ignores 0x8800, the 0x1xxx family, and live 0x2xxx
     * actors. Unlike shell contact it has no height rejection. */
    if(other_kind==UINT16_C(0x8800)||family==UINT16_C(0x1000)||
       (family==UINT16_C(0x2000)&&other_field_66!=0)){
        banana->collision_result=PORTABLE_GP_BANANA_COLLISION_IGNORED;
        return PORTABLE_GP_BANANA_OK;
    }
    *owner_flags&=UINT32_C(0xfffeffff);
    begin_destruction(banana);
    banana->collision_result=family==UINT16_C(0x8000)?
        PORTABLE_GP_BANANA_COLLISION_DESTROYING_ITEM:
        PORTABLE_GP_BANANA_COLLISION_DESTROYING;
    banana->contact_audio_pending=(uint8_t)(family==UINT16_C(0x8000));
    return PORTABLE_GP_BANANA_OK;
}

PortableGpBananaStatus portable_gp_banana_collide(
    PortableGpBanana *banana,uint32_t *owner_flags,uint16_t other_kind,
    int16_t other_field_66)
{
    if(banana==NULL || !portable_banana_rules_valid(banana->rules))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    return collide(banana,owner_flags,other_kind,other_field_66,0u);
}

PortableGpBananaStatus portable_gp_banana_collide_held(
    PortableGpBanana *banana,PortableGpBananaOwner *owner,uint16_t other_kind,
    int16_t other_field_66)
{
    if(banana==NULL || !portable_banana_rules_valid(banana->rules))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(owner==NULL)return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    const PortableGpBananaStatus status=collide(banana,&owner->flags_48,
        other_kind,other_field_66,1u);
    if(status==PORTABLE_GP_BANANA_OK &&
       banana->phase==PORTABLE_GP_BANANA_DESTROYING)
        owner->shell_item_18c=PORTABLE_GP_ITEM_NONE;
    return status;
}

PortableGpBananaStatus portable_gp_banana_take_contact_audio(
    PortableGpBanana *banana,uint8_t suppressed,uint16_t spatial_volume,
    PortableGpBananaAudioEvent *event)
{
    if(banana==NULL || !portable_banana_rules_valid(banana->rules))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(banana==NULL||event==NULL)return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(!banana->contact_audio_pending)return PORTABLE_GP_BANANA_INELIGIBLE;
    banana->contact_audio_pending=0u;
    if(suppressed||spatial_volume==0u)return PORTABLE_GP_BANANA_INELIGIBLE;
    event->song_id=124u;event->player=4u;event->volume=spatial_volume;
    event->pitch=0;
    return PORTABLE_GP_BANANA_OK;
}

PortableGpBananaStatus portable_gp_banana_tick_destroying(
    PortableGpBanana *banana)
{
    if(banana==NULL || !portable_banana_rules_valid(banana->rules))return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(banana==NULL)return PORTABLE_GP_BANANA_INVALID_ARGUMENT;
    if(!banana->occupied||banana->kind_10!=PORTABLE_GP_BANANA_KIND||
       banana->phase!=PORTABLE_GP_BANANA_DESTROYING)
        return PORTABLE_GP_BANANA_INELIGIBLE;
    banana->position_z_q16=(int32_t)((uint32_t)banana->position_z_q16+
                                     (uint32_t)banana->vertical_velocity_q16);
    banana->vertical_velocity_q16=(int32_t)(
        (uint32_t)banana->vertical_velocity_q16-(banana->rules)->destruction_gravity_q16);
    if(banana->position_z_q16<0){
        banana->position_z_q16=0;banana->vertical_velocity_q16=0;
        banana->occupied=0u;banana->phase=PORTABLE_GP_BANANA_DESTROYED;
    }
    return PORTABLE_GP_BANANA_OK;
}
