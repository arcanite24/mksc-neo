#include "engine_control.h"

#include <stddef.h>

/* ARM narrowing and arithmetic shifts, without signed overflow or relying on
 * an implementation-defined signed right shift. */
static int32_t signed32(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value :
        (int32_t)((int64_t)value - INT64_C(0x100000000));
}
static int32_t shift_right(int32_t value, unsigned bits)
{
    const int64_t divisor = INT64_C(1) << bits;
    return (int32_t)(value < 0 ? -((-(int64_t)value + divisor - 1) / divisor) :
        value / divisor);
}

void portable_engine_control_init(PortableEngineControl *control)
{
    if (control != NULL) *control = (PortableEngineControl){0};
}

void portable_engine_control_prestart_tick(PortableEngineControl *control,
    uint16_t control_word)
{
    if (control != NULL) portable_driver_engine_oscillate(&control->accumulator,
        &control->prestart_mode,control_word,0x20,0x200,0x100);
}

void portable_engine_control_race_tick(PortableEngineControl *control,
    const PortableEngineRevProfile *profile,
    const PortableDriverEngineSoundInput *input)
{
    if (control != NULL && profile != NULL && input != NULL)
        portable_driver_engine_revs_tick(&control->accumulator,
            &control->prestart_mode,profile,input);
}

static int16_t pitch_curve(const PortableEngineControl *control,
    const PortableDriverEngineSoundInput *input)
{
    if (input->character_flags & 8u) return 0x100;
    const int32_t centered = portable_engine_signed16(control->accumulator) - 0x1000;
    int32_t squared = signed32((uint32_t)centered * (uint32_t)centered);
    if (squared < 0) squared = signed32((uint32_t)squared + 0xfffu);
    squared = shift_right(squared,12u);
    int32_t value = signed32((uint32_t)squared * (uint32_t)centered);
    if (value < 0) value = signed32((uint32_t)value + 0xfffu);
    value = shift_right(value,12u);
    int32_t offset = 0;
    if (input->driver_mode == 12u || input->driver_mode == 8u) offset += 0x200;
    if (input->lightning_timer != 0) offset += 0x400;
    offset += (int32_t)input->speed_parameter * 2;
    return (int16_t)portable_engine_signed16((uint16_t)(value + offset + 0x1000));
}

int portable_engine_audio_control(PortableEngineControl *control,
    const PortableEngineSoundRules *rules, uint8_t character,
    const PortableDriverEngineSoundInput *input, int scene_stop, int running,
    PortableEngineAudioControl *out)
{
    if (control == NULL || rules == NULL || input == NULL || out == NULL ||
        character >= PORTABLE_ENGINE_DRIVER_COUNT) return 0;
    *out = (PortableEngineAudioControl){0};
    if (input->state_flags & 0x20u) {
        out->stop_engine = (uint8_t)((control->flags & 3u) != 0u);
        control->flags &= (uint8_t)(0xFFu&~3u);
        return 1;
    }
    if (scene_stop) {
        out->stop_engine = out->stop_recovery = 1u;
        control->flags &= (uint8_t)(0xFFu&~0x23u);
        return 1;
    }
    /* 99A4 stops both players when retrieval begins; 9DAC starts the water
     * voice before the recovery loop and marks41244's recovery ownership. */
    if(input->retrieval_started){
        control->accumulator=control->fade=0u;
        control->flags&=(uint8_t)(0xFFu&~0x23u);
        out->stop_engine=out->stop_recovery=1u;
        return 1;
    }
    if(input->water_recovery_started){
        control->accumulator=control->fade=0u;
        control->flags=(uint8_t)((control->flags&~3u)|0x20u);
        out->stop_engine=1u;
        out->entry_song=138u;
        out->start_song=161u;
        return 1;
    }
    if (input->mode == 3u || input->mode == 5u || input->mode == 6u) return 1;
    if (input->mode == 4u) {
        if (!(control->flags & 0x20u)) {
            control->accumulator = control->fade = 0u;
            out->stop_engine = 1u;
            out->start_song = 161u;
            control->flags = (uint8_t)((control->flags & ~3u) | 0x20u);
        }
        return 1;
    }
    if (input->character_flags & 8u) {
        if (input->height_q16 == 0) {
            if (input->bomb_walk_audio & 4u) out->start_song = 98u;
            else if (input->bomb_walk_audio & 8u) out->start_song = 97u;
        }
        return 1;
    }
    const int16_t curve = pitch_curve(control,input);
    int32_t bucket = shift_right(curve,8u);
    if (bucket < 0) bucket = 0;
    if (bucket > 15) bucket = 15;
    uint32_t volume = rules->volumes[character][bucket];
    if (portable_engine_signed16(control->fade) > 0) {
        volume *= control->fade * UINT32_C(0x155);
        if (volume & UINT32_C(0x80000000)) volume += 0x7fffu;
        volume = (volume << 1u) >> 16u;
    }
    int16_t pitch = curve;
    if (input->mode == 2u) {
        volume *= input->recovery_scale;
        if (volume & UINT32_C(0x80000000)) volume += 0x3fu;
        volume = (volume << 10u) >> 16u;
        pitch = (int16_t)(portable_engine_signed16((uint16_t)volume) / 2);
    }
    out->curve = curve;
    out->bucket = (uint8_t)bucket;
    out->volume = (uint16_t)volume;
    out->pitch = pitch;
    out->apply_controls = 1u;
    if (input->response_status == 13u) {
        if (!(control->flags & 2u)) {
            out->start_song = rules->idle_songs[character];
            control->flags = (uint8_t)((control->flags & ~1u) | 2u);
            control->fade = 0u;
        }
    } else if (running) {
        if (input->control_word & 0x8000u) {
            control->fade = 0u;
            if (!(control->flags & 1u)) {
                out->start_song = rules->throttle_songs[character];
                control->flags = (uint8_t)((control->flags | 1u) & ~2u);
            }
        } else if (control->flags & 2u) {
            if (portable_engine_signed16(control->fade) < 96) ++control->fade;
        } else if (!(control->flags & 1u)) {
            control->fade = 96u;
            out->start_song = rules->coast_songs[character];
            control->flags = (uint8_t)((control->flags & ~1u) | 2u);
        } else {
            if (control->fade == 0u) control->fade = 96u;
            if (portable_engine_signed16(control->fade) > 0) --control->fade;
            if (control->fade == 32u) {
                out->start_song = rules->idle_songs[character];
                control->flags = (uint8_t)((control->flags & ~1u) | 2u);
            }
        }
    }
    return 1;
}

void portable_engine_tire_audio_tick(PortableEngineControl *control,
    const PortableEngineSoundRules *rules,
    const PortableDriverEngineSoundInput *input, int scene_stop,
    PortableTireAudioControl *out)
{
    if (control == NULL || rules == NULL || input == NULL || out == NULL) return;
    *out = (PortableTireAudioControl){0};
    if (input->state_flags & 0x4000u) return;
    if (input->state_flags & 0x20u) {
        out->stop_song = 164u;
        out->stop_if_playing = 1u;
        return;
    }
    if (input->mode == 4u) return;
    if (!(input->control_word & 0x4000u)) {
        if (control->tire_active) {
            out->stop_song = rules->brake_songs[0];
            control->tire_active = 0u;
            control->flags &= (uint8_t)(0xFFu&~0x10u);
        }
        return;
    }
    if ((input->control_word & 0x8000u) && input->response_status == 15u && !scene_stop) {
        if (!(control->flags & 0x10u)) {
            out->start_song = 164u;
            control->flags |= 0x10u;
            control->tire_active = 1u;
        }
        out->volume = (input->control_word & 0x300u) ? 478u : 256u;
        out->apply_volume = 1u;
        return;
    }
    if (control->flags & 0x10u) {
        control->flags &= (uint8_t)(0xFFu&~0x10u);
        out->stop_song = 164u;
        out->stop_if_playing = 1u;
        /* The source keeps48 bit40000 here; only a later release/stop
         * clears ownership. Do not restart the ordinary loop this turn. */
        return;
    }
    if (input->speed_q16 <= 0 || input->mode != 0u) {
        if (control->tire_active) {
            out->stop_song = rules->brake_songs[0];
            control->tire_active = 0u;
            control->flags &= (uint8_t)(0xFFu&~0x10u);
        }
    } else if (!control->tire_active) {
        uint32_t bucket = (uint32_t)input->speed_q16 >> 24u;
        if (bucket > 3u) bucket = 3u;
        out->start_song = rules->brake_songs[bucket];
        control->tire_active = 1u;
        control->flags &= (uint8_t)(0xFFu&~0x10u);
    }
}

static uint16_t surface_cell_key(int32_t x_q16,int32_t y_q16)
{
    /* ADD3ffff/ASR18 truncates negative coordinates toward zero. */
    const uint32_t x=(uint32_t)(x_q16/262144)&63u;
    const uint32_t y=(uint32_t)(y_q16/262144)&63u;
    return (uint16_t)((x<<10u)|(y<<4u));
}

void portable_engine_surface_audio_tick(PortableEngineControl *control,
    PortableSurfaceAudioState *state, const PortableSurfaceSoundRules *rules,
    const PortableDriverEngineSoundInput *input, int32_t x_q16,int32_t y_q16,
    int scene_stop, PortableSurfaceAudioControl *out)
{
    if(control==NULL||state==NULL||rules==NULL||input==NULL||out==NULL)return;
    *out=(PortableSurfaceAudioControl){0};
    if(input->mode==4u)return;
    const int tire_owned=control->tire_active||(input->state_flags&0x40000u);
    const int reverse_response=input->speed_q16<0&&input->response_q8!=0;
    const uint8_t status=input->response_status;
    const int blocked=scene_stop||(input->state_flags&0x20u)||
        (input->response_flags&0x8001u)||(tire_owned&&!reverse_response)||
        status==13u||(status>=5u&&status<=8u)||status==15u;
    const int moving_surface=input->speed_q16!=0&&input->material>=0x50u&&input->material<=0x5fu;
    const int steering_surface=status>=1u&&status<=4u&&(input->response_flags&0x24u);
    if(blocked||!(moving_surface||steering_surface||reverse_response)){
        if(state->song!=0u){
            if(!tire_owned&&!(input->response_flags&0x8000u)){
                out->stop=1u;
                control->flags&=(uint8_t)(0xFFu&~0x10u);
            }
            *state=(PortableSurfaceAudioState){0};
        }
        return;
    }
    const uint8_t index=(input->material>>1u)&15u;
    const uint16_t song=(input->material&0x40u)?rules->songs[index]:0u;
    const uint8_t restart=(input->material&0x40u)?rules->restart_ticks[index]:0u;
    const uint16_t key=surface_cell_key(x_q16,y_q16);
    if(song!=state->song){
        if(song!=0u)out->start_song=song;
        else out->stop=1u; /* MPlayStart(dummy_song) on the tire player. */
        state->song=song;
        state->cell_key=key;
        state->cell_key|=restart;
    }else if(restart!=0u){
        if((state->cell_key&15u)==0u&&(state->cell_key&0xfff0u)!=key)
            state->song=0u; /* Restart on the next tick after crossing a cell. */
    }
    if(state->cell_key&15u)--state->cell_key;
}

void portable_engine_jump_audio(PortableEngineControl *control,
    PortableSurfaceAudioState *surface,const PortableDriverEngineSoundInput *input,
    PortableJumpAudioControl *out)
{
    if(control==NULL||surface==NULL||input==NULL||out==NULL)return;
    *out=(PortableJumpAudioControl){0};
    if(input->state_flags&0x20u)return;
    if(input->protected_hop_started){
        out->song=156u;
        control->flags&=(uint8_t)(0xFFu&~0x10u);
    }else if(input->surface_jump_started){
        const uint8_t kind=input->material&15u;
        if(kind==0u||kind==2u||kind==14u){
            if(surface->song!=0u){
                out->stop_surface=1u;
                *surface=(PortableSurfaceAudioState){0};
            }
            control->flags&=(uint8_t)(0xFFu&~0x10u);
            if(kind==0u)out->song=input->driver_mode==8u?158u:157u;
            else if(kind==2u)out->song=155u;
        }
    }else if(input->landed&&!(input->material&0x20u)){
        out->song=159u;
        out->require_idle=1u;
    }
}

uint16_t portable_engine_horn_song(const PortableCharacterAudioRules *rules,
    uint8_t character,uint32_t listener_flags)
{
    if(rules==NULL||character>=8u||listener_flags==0u||(listener_flags&0x4020u)!=0u)return 0u;
    return rules->race_horns[character];
}

void portable_engine_hazard_audio(const PortableHazardSoundRules *rules,
    uint8_t character,uint8_t kind,uint8_t variant,int rival,
    uint32_t listener_flags,uint16_t spatial_volume,PortableHazardAudioControl *out)
{
    *out=(PortableHazardAudioControl){.player=PORTABLE_AUDIO_PLAYER_AUTO};
    if(listener_flags&0x20u||character>=8u||variant>=2u)return;
    if(kind==PORTABLE_DRIVER_HAZARD_VOID){
        out->song=rules->voices[character][rival?1u:0u][variant];
        out->volume=rival?(spatial_volume<64u?64u:spatial_volume):256u;
    } else if(kind==PORTABLE_DRIVER_HAZARD_LAVA&&!rival){
        out->song=139u;
        out->player=6u; /*39F74 starts the local voice player directly. */
        out->volume=256u;
    }
}

int portable_engine_nearby_tick(PortableNearbyEngineState *state,
    const PortableEngineSoundRules *rules,const PortableNearbyEngineDriver *drivers,
    size_t count,int32_t camera_x,int32_t camera_y,int32_t listener_height,
    uint32_t listener_flags,uint8_t phase,uint16_t mode,PortableNearbyEngineControl *out)
{
    if(!state||!rules||!out||count>254u||(!drivers&&count))return 0;
    *out=(PortableNearbyEngineControl){0};
    mode&=0xf00u;
    if(phase>=5u){
        if(phase==5u&&state->song){out->song=state->song;out->stop=1u;}
        return 1;
    }
    if(phase<2u||(phase==2u&&mode==0x100u))return 1;
    if(mode==0x800u&&(listener_flags&0x20u)){
        if(state->song){out->song=state->song;out->stop=1u;}
        return 1;
    }
    int32_t nearest=0x4000;
    size_t selected=count;
    for(size_t i=0u;i<count;++i){
        if(!drivers[i].visible)continue;
        const int32_t dx=signed32((uint32_t)camera_x-(uint32_t)drivers[i].x_q16)/65536;
        const int32_t dy=signed32((uint32_t)camera_y-(uint32_t)drivers[i].y_q16)/65536;
        const int32_t distance=signed32((uint32_t)dx*(uint32_t)dx+(uint32_t)dy*(uint32_t)dy);
        if(distance<nearest){nearest=distance;selected=i;}
    }
    if(selected==count){
        if(state->active&&state->song){out->song=state->song;out->stop=1u;}
        state->active=0u;
        return 1;
    }
    const PortableNearbyEngineDriver *driver=&drivers[selected];
    if(driver->character>=8u)return 0;
    if(state->target!=selected+1u){state->target=(uint8_t)(selected+1u);state->active=0u;}
    state->song=rules->nearby_songs[driver->character];
    int16_t curve;
    if(driver->input.state_flags&0x2000u){
        if(driver->input.character_flags&8u)curve=0x100;
        else {
            const int32_t centered=shift_right(driver->input.speed_q16,15u)-0x1000;
            int32_t squared=signed32((uint32_t)centered*(uint32_t)centered);
            if(squared<0)squared=signed32((uint32_t)squared+0xfffu);
            squared=shift_right(squared,12u);
            int32_t value=signed32((uint32_t)centered*(uint32_t)squared);
            if(value<0)value=signed32((uint32_t)value+0xfffu);
            const int32_t offset=(driver->input.driver_mode==8u?0x200:0)+
                (driver->input.lightning_timer==8?0x400:0);
            curve=(int16_t)portable_engine_signed16((uint16_t)(shift_right(value,12u)+offset+0x1000));
        }
    }else{
        const PortableEngineControl revs={.accumulator=driver->revs};
        curve=pitch_curve(&revs,&driver->input);
    }
    out->pitch=(int16_t)portable_engine_signed16((uint16_t)((int32_t)curve+driver->doppler));
    int32_t bucket=shift_right(out->pitch,8u);
    if(bucket<0)bucket=0;
    if(bucket>15)bucket=15;
    const int32_t attenuation=signed32(0x4000u-(uint32_t)nearest);
    const int32_t weighted=signed32((uint32_t)(attenuation/128)*rules->volumes[driver->character][bucket]);
    out->volume=(uint16_t)(attenuation/2048*8+weighted/256);
    if(driver->input.character_flags&8u){
        if(listener_height!=0)return 1;
        state->song=driver->input.bomb_walk_audio&4u?100u:
            driver->input.bomb_walk_audio&8u?99u:0u;
        if(state->song){out->song=state->song;out->start=out->apply=1u;}
    }else{
        out->song=state->song;out->start=(uint8_t)!state->active;out->apply=1u;
        state->active=1u;
    }
    return 1;
}
