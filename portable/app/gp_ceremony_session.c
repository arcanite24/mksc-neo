#include "gp_ceremony_session.h"
#include "track_set.h"

#include <stddef.h>
#include <string.h>

static uint8_t source_performance_rating(uint8_t points, int16_t score)
{
    int32_t tier;
    int32_t result;

    if (points > 35u)
        tier = 3;
    else if (points > 27u)
        tier = 4;
    else if (points > 21u)
        tier = 5;
    else if (points <= 14u)
        tier = 7;
    else
        tier = 6;

    if (score > 319)
        result = tier - 3;
    else if (score > 199)
        result = tier - 2;
    else if (score > 99)
        result = tier - 1;
    else if (score > 29)
        result = tier;
    else if (score < -120)
        result = tier + 2;
    else
        result = tier + 1;
    if (result < 0)
        result = 0;
    if (result > 7)
        result = 7;
    return (uint8_t)result;
}

PortableGpCeremonyStatus portable_gp_ceremony_session_init(
    PortableGpCeremonySession *session,
    const PortableGpCupSession *final_standings)
{
    PortableGpCeremonySession next = {0};
    uint8_t seen = 0u;
    uint8_t player_rank = UINT8_C(0xff);

    if (session == NULL || final_standings == NULL)
        return PORTABLE_GP_CEREMONY_INVALID_ARGUMENT;
    if (final_standings->track_set >= PORTABLE_TRACK_SET_COUNT ||
        final_standings->track_set == PORTABLE_TRACK_SET_ARENA || final_standings->cup_index >= 5u || final_standings->engine_class >= 3u ||
        final_standings->stage != PORTABLE_GP_CUP_STAGE_STANDINGS ||
        final_standings->course_in_cup != 3u ||
        final_standings->next_mode != 7u ||
        final_standings->driving_quality_sample_mask != 15u ||
        (final_standings->race_mode_flags & UINT16_C(0x0f00)) !=
            UINT16_C(0x0100))
        return PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE;

    for (size_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
    {
        const uint8_t racer = final_standings->standings_order[rank];
        const uint8_t bit = racer < PORTABLE_GP_RACER_COUNT
            ? (uint8_t)(1u << racer) : 0u;
        if (bit == 0u || (seen & bit) != 0u)
            return PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE;
        seen = (uint8_t)(seen | bit);
        if (rank < 3u)
            next.podium_driver_classes[rank] =
                final_standings->racer_driver_classes[racer];
        if (racer == 0u)
            player_rank = (uint8_t)rank;
    }
    if (seen != UINT8_C(0xff) || player_rank == UINT8_C(0xff))
        return PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE;

    for(unsigned race=0u;race<4u;++race){
        if(final_standings->finishing_coins[race]>99u)
            return PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE;
        next.finishing_coin_total=(uint16_t)(next.finishing_coin_total+
            final_standings->finishing_coins[race]);
    }
    memcpy(next.course_ids,final_standings->course_ids,sizeof next.course_ids);
    memcpy(next.race_ticks,final_standings->race_ticks,sizeof next.race_ticks);
    memcpy(next.race_centiseconds,final_standings->race_centiseconds,sizeof next.race_centiseconds);
    memcpy(next.finishing_ranks,final_standings->finishing_ranks,sizeof next.finishing_ranks);
    memcpy(next.finishing_coins,final_standings->finishing_coins,sizeof next.finishing_coins);
    memcpy(next.finishing_points,final_standings->finishing_points,sizeof next.finishing_points);
    next.cup_index = final_standings->cup_index;
    next.track_set = final_standings->track_set;
    next.engine_class = final_standings->engine_class;
    next.course_header_index = final_standings->course_header_index;
    next.selected_driver_class = final_standings->selected_driver_class;
    next.player_standing_rank = player_rank;
    /* 430EC averages the four signed samples into C94; 43550 uses that
     * completed-cup average with the player's cumulative points. */
    next.driving_score = final_standings->driving_quality.average_score;
    next.performance_rating = source_performance_rating(
        final_standings->cumulative_points[0], next.driving_score);
    next.result_song_id = player_rank <= 2u ? UINT16_C(0x38)
                                            : UINT16_C(0x3b);
    next.state5_next_state = player_rank <= 2u ? 6u : 11u;
    /* sub_8028B90 evaluates prior cup grade/time and unlock conditions before
     * entering the visual state machine. Persistence owns those mutations;
     * setup leaves them pending until saved. */
    next.save_evaluation_pending = 1u;
    next.unlock_evaluation_pending = 1u;
    *session = next;
    return PORTABLE_GP_CEREMONY_OK;
}

uint16_t portable_gp_ceremony_song_at(const PortableGpCeremonySession *session,
    uint32_t tick)
{
    const int podium=session!=NULL&&session->result_song_id==UINT16_C(0x38);
    if(session==NULL)return 0u;
    if(tick==0u)return podium?55u:57u;
    return tick==(podium?730u:1338u)?session->result_song_id:0u;
}

int portable_gp_ceremony_presentation_init(PortableGpCeremonyPresentation *view,
    uint32_t entrance_ticks, uint32_t celebration_ticks, uint32_t fade_ticks)
{
    if(view==NULL||entrance_ticks==0u||entrance_ticks>3600u||
       celebration_ticks==0u||celebration_ticks>3600u||fade_ticks==0u||fade_ticks>600u)return 0;
    *view=(PortableGpCeremonyPresentation){.entrance_ticks=entrance_ticks,
        .celebration_ticks=celebration_ticks,.fade_ticks=fade_ticks};
    return 1;
}
void portable_gp_ceremony_presentation_tick(PortableGpCeremonyPresentation *view,
    int confirm_pressed)
{
    if(view==NULL||view->phase==PORTABLE_CEREMONY_FINISHED)return;
    if(view->phase==PORTABLE_CEREMONY_WAIT){
        view->tick=(view->tick+1u)%60u;
        if(confirm_pressed){view->phase=PORTABLE_CEREMONY_FADE;view->tick=0u;}
        return;
    }
    const uint32_t duration=view->phase==PORTABLE_CEREMONY_ENTRANCE?
        view->entrance_ticks:view->phase==PORTABLE_CEREMONY_CELEBRATION?
        view->celebration_ticks:view->fade_ticks;
    if(++view->tick>=duration){
        view->tick=0u;
        view->phase=(PortableGpCeremonyPhase)(view->phase+1);
    }
}

int portable_gp_ceremony_presentation_skip(PortableGpCeremonyPresentation *view)
{
    if(view==NULL||view->phase>=PORTABLE_CEREMONY_WAIT)return 0;
    view->phase=PORTABLE_CEREMONY_WAIT;view->tick=0u;
    return 1;
}

static uint32_t ceremony_points_tick(const PortableGpCeremonySession *session,const PortableCeremonyMotionRules *rules)
{
    return session->player_standing_rank<=2u?
        1171u+rules->trophy_delay[session->player_standing_rank]:954u;
}

uint32_t portable_gp_ceremony_input_tick(const PortableGpCeremonySession *session,const PortableCeremonyMotionRules *rules)
{
    return session!=NULL&&rules!=NULL?ceremony_points_tick(session,rules)+346u:0u;
}

size_t portable_gp_ceremony_cues_at(const PortableGpCeremonySession *session,
    const PortableCeremonyMotionRules *rules,const PortableCharacterAudioRules *audio_rules,
    uint32_t tick,uint16_t songs[3])
{
    size_t count=0u;
    if(session==NULL||rules==NULL||songs==NULL)return 0u;
    const int podium=session->player_standing_rank<=2u;
    const uint32_t entry=podium?564u:807u,flash=entry+103u;
    const uint32_t points=ceremony_points_tick(session,rules);
    if(tick==entry)songs[count++]=174u;
    if(tick==flash||tick==flash+16u||tick==flash+24u)songs[count++]=175u;
    if(podium){
        if(tick==720u||tick==730u||tick==740u)songs[count++]=156u;
        if(tick>=829u&&tick<=997u&&(tick-829u)%28u==0u)songs[count++]=120u;
        if(tick==points)songs[count++]=109u;
        const uint8_t voice=session->voice_override!=0u?(uint8_t)(session->voice_override-1u):
            session->selected_driver_class;
        if(tick==points+124u&&voice<8u&&audio_rules!=NULL&&audio_rules->ceremony_voices[voice]!=0u)
            songs[count++]=audio_rules->ceremony_voices[voice];
    }
    if(tick>=points+140u&&tick<=points+203u&&(tick-points-140u)%21u==0u)
        songs[count++]=101u;
    if(tick==points+325u&&(session->extra_cup_unlocked||session->special_cup_unlocked))
        songs[count++]=110u;
    return count;
}

/* The ceremony's blocking scripts are expressed as named native controllers.
 * Main phase transitions read the preceding actor pass, as 28B90 does. */
static int32_t ceremony_signed32(uint32_t bits)
{ return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits); }
static int32_t ceremony_asr15(int32_t value)
{ return value>=0?value/32768:(int32_t)(-((-(int64_t)value+32767)/32768)); }
static int16_t ceremony_curve9(int start,int end,unsigned tick,unsigned duration,const int32_t *sine)
{
    if(tick>=duration)return (int16_t)end;
    int angle=(int)(tick*tick*131072u/(40u*duration));
    angle=(int)((unsigned)angle&65535u);if(angle>=32768)angle-=65536;
    int32_t value=sine[(unsigned)(256-angle/64)&1023u];
    const uint32_t remaining=duration-tick;
    value=ceremony_signed32((uint32_t)value*remaining*remaining)/(int32_t)(duration*duration);
    if(value<0)value=-value;
    return (int16_t)(end-ceremony_asr15(ceremony_signed32((uint32_t)value*(uint32_t)(end-start))));
}
static void ceremony_header_start(PortableGpCeremonyPresentation *v,const PortableCeremonyMotionRules *rules,unsigned rank,int settled)
{
    const unsigned count=rank<3u?16u:7u,first=rank<3u?0u:16u;
    for(unsigned i=0;i<count;++i){
        const int16_t x=rules->header_targets[first+i][0],y=rules->header_targets[first+i][1];
        v->headers[i]=(PortableCeremonyCellState){.x_q16=(settled||rank>2u?x:256)*65536,
            .y_q16=(settled||rank<3u?y:-40)*65536,.origin_x=x,.origin_y=y,
            .inverse_x=256,.inverse_y=256,.visible=1u};
        v->header_modes[i]=(uint8_t)(settled?13u:rank<3u?6u:11u);
    }
}
static int ceremony_curve5(int start,int end,unsigned tick,unsigned duration,const int32_t *sine)
{
    const unsigned raw=tick*tick*131072u/(15u*duration);
    int angle=(int)(raw&65535u);if(angle>=32768)angle-=65536;
    int32_t value=sine[(unsigned)(256-angle/64)&1023u];
    value=ceremony_asr15(ceremony_signed32((uint32_t)value*(uint32_t)(end-start)));
    value=ceremony_signed32((uint32_t)value*(duration-tick)*(duration-tick))/(int32_t)(duration*duration);
    return end-value;
}
static void ceremony_header_tick(PortableGpCeremonyPresentation *v,unsigned rank,const int32_t *sine)
{
    const unsigned count=rank<3u?16u:7u;
    for(unsigned i=0;i<count;++i){
        PortableCeremonyCellState *cell=&v->headers[i];uint8_t *mode=&v->header_modes[i];
        const unsigned tick=(unsigned)++cell->tick;
        switch(*mode){
        case 6:if(tick>3u*i){cell->tick=0;*mode=7u;}break;
        case 7:
            if(tick<=12u)cell->x_q16=(256+ceremony_asr15((int32_t)((int)i*8-192)*sine[tick*256u/12u]))*65536;
            else {cell->tick=0;*mode=8u;}break;
        case 8:if(tick>8u){cell->tick=0;*mode=9u;}break;
        case 9:if(tick>3u*(count-1u-i)){cell->tick=0;*mode=10u;}break;
        case 10:
            if(tick<=16u)cell->x_q16=ceremony_curve5(64+(int)i*8,cell->origin_x,tick,16u,sine)*65536;
            else {cell->tick=0;cell->inverse_x=256;*mode=13u;}break;
        case 11:if(tick>3u*i){cell->tick=0;*mode=12u;}break;
        case 12:
            if(tick<=64u)cell->y_q16=ceremony_curve9(-40,cell->origin_y,tick,64u,sine)*65536;
            else {cell->tick=0;*mode=13u;}break;
        case 13:if(tick>4u*i){cell->tick=0;*mode=14u;}break;
        case 14:cell->tick=0;cell->origin_y=(int16_t)(cell->y_q16/65536);*mode=15u;break;
        case 15:
            if(tick<=8u){
                const int32_t p=-4*(32768-sine[(256u-tick*128u)&1023u]);
                const int delta=(int)((int64_t)p+(p<0?1:0));
                cell->y_q16=(cell->origin_y+(int)(delta>=0?delta/65536:-((-(int64_t)delta+65535)/65536)))*65536;
            }else {cell->tick=0;*mode=16u;}break;
        case 16:if(tick>4u*(count-1u-i)+300u){cell->tick=0;*mode=13u;}break;
        default:break;
        }
    }
}
static const PortableFinishSchedule *ceremony_finish(const PortableFinishRules *rules,unsigned driver)
{ return rules&&driver<8u?&rules->schedules[driver]:NULL; }
static void ceremony_coins_start(PortableGpCeremonyPresentation *,const PortableGpCeremonySession *);
int portable_gp_ceremony_source_init(PortableGpCeremonyPresentation *v,
    const PortableGpCeremonySession *session,const PortableCeremonyMotionRules *rules,
    const PortableCeremonyWorldRules *world)
{
    if(!v||!session||!rules||!world||session->player_standing_rank>=8u)return 0;
    const uint8_t classes[4]={session->podium_driver_classes[0],session->podium_driver_classes[1],
        session->podium_driver_classes[2],session->selected_driver_class};
    portable_ceremony_motion_reset(&v->motion);
    if(!portable_ceremony_world_init(&v->motion,world,session->player_standing_rank,classes)||
       !portable_ceremony_tracks_start(&v->motion,rules->tracks,rules->frames,rules->frame_count,1))return 0;
    v->source_enabled=1u;v->source_tick=v->phase_ticks=v->overlay_ticks=v->points_ticks=v->zoom_wait_ticks=v->drop_elapsed=0u;
    v->camera_return_tick=0u;
    ceremony_coins_start(v,session);v->palette_phase=0u;
    v->source_phase=v->arrival_completed=v->hop_completed=0u;
    memset(v->driver_script,0,sizeof v->driver_script);memset(v->driver_ticks,0,sizeof v->driver_ticks);
    v->drop_stage=v->trophy_stage=v->zoom_stage=v->fly_stage=0u;
    v->header_active=v->header_exit=v->stats_reveal=v->loser_darken=0u;
    v->portrait_mood=session->player_standing_rank<3u?1u:2u;v->road_line=153;v->camera_vertical_offset=80;
    /* DBE94/DBCB8 are the source skip branch. Ordinary entry waits for
     * phase9/11 before constructing a trophy or portrait. */
    v->phase=PORTABLE_CEREMONY_ENTRANCE;v->tick=0u;
    return 1;
}
/* 26600/268A4 keep the four moving coin pairs separate from the
 * unchanged row values on the summary background. One pair owns each call. */
static void ceremony_coins_start(PortableGpCeremonyPresentation *v,const PortableGpCeremonySession *session)
{
    v->coin_total=0u;v->coin_stage=1u;
    v->unlock_stage=v->unlock_tick=v->unlock_reveal=0u;
    for(unsigned i=0;i<4u;++i){v->coins[i].x=228;v->coins[i].y=(int16_t)(104u+16u*i);
        v->coins[i].value=(int8_t)session->finishing_coins[i];v->coins[i].phase=v->coins[i].tick=0u;}
}
static void ceremony_coins_tick(PortableGpCeremonyPresentation *v,const int32_t *sine)
{
    if(v->coin_stage!=1u||v->points_ticks<120u)return;
    for(unsigned i=0;i<4u;++i){
        if(v->coins[i].phase==3u)continue;
        switch(v->coins[i].phase){
        case 0:
            if(++v->coins[i].tick<=12u){
                const unsigned index=(unsigned)((v->coins[i].tick*16384u/12u)/64u)&1023u;
                v->coins[i].y=(int16_t)(104+(int)i*16+ceremony_asr15((80-104-(int)i*16)*sine[index]));
            }else {v->coins[i].tick=0u;v->coins[i].phase=1u;}
            break;
        case 1:if(++v->coins[i].tick>6u){v->coins[i].tick=0u;v->coins[i].phase=2u;}break;
        case 2:v->coin_total=(uint16_t)(v->coin_total+(uint8_t)v->coins[i].value);
            v->coins[i].value=-1;v->coins[i].phase=3u;break;
        default:break;
        }
        return;
    }
    v->coin_stage=2u;
}
static void ceremony_unlock_tick(PortableGpCeremonyPresentation *v,const PortableGpCeremonySession *session)
{
    if(v->coin_stage<2u||v->points_ticks<324u)return;
    const int unlocked=session->extra_cup_unlocked||session->special_cup_unlocked;
    switch(v->unlock_stage){
    case 0:v->unlock_stage=1u;break; /* Copy the completed summary first. */
    case 1:v->unlock_stage=2u;break; /* Install the authored unlock strip. */
    case 2:
        if(++v->unlock_tick<=16u){if(unlocked)v->unlock_reveal=v->unlock_tick;}
        else {v->unlock_tick=0u;v->unlock_stage=3u;}
        break;
    case 3:v->unlock_stage=4u;break;
    default:break;
    }
}
static void ceremony_next_phase(PortableGpCeremonyPresentation *v)
{ ++v->source_phase;v->phase_ticks=0u; }
static int ceremony_driver_scripts(PortableGpCeremonyPresentation *v,const PortableCeremonyWorldRules *world,
    const int32_t *sine,const PortableDriverAffineRules *affine)
{
    for(unsigned i=0;i<v->motion.world.count;++i){
        uint8_t *script=&v->driver_script[i];uint16_t *tick=&v->driver_ticks[i];
        if(!*script)continue;
        ++*tick;
        switch(*script){
        case 1:
            if(portable_ceremony_world_arrival_tick(&v->motion,i)<0)return 0;
            if(v->motion.world.drivers[i].arrived){
                *tick=0u;
                if(i<3u){*script=0u;++v->arrival_completed;}
                else *script=3u;
            }
            break;
        case 2:
            if(*tick>=30u){if(!portable_ceremony_world_reveal(&v->motion,world,i))return 0;*script=1u;*tick=0u;}
            break;
        case 3:
            if(portable_ceremony_world_rotate_loser_tick(&v->motion)<0)return 0;
            if(*tick>33u){*script=7u;*tick=0u;}
            break;
        case 4:
            if(*tick>=(i+1u)*10u){if(portable_ceremony_world_hop_start(&v->motion,world,i)<0)return 0;*script=5u;*tick=0u;}
            break;
        case 5:
            if(portable_ceremony_world_hop_tick(&v->motion,i,affine)<0)return 0;
            if(v->motion.world.drivers[i].hop_complete){
                if(!portable_ceremony_world_bounce_start(&v->motion,i))return 0;
                *script=6u;*tick=0u;
            }
            break;
        case 6:{
            const int16_t ease=ceremony_curve9(256,0,*tick,32u,sine);
            const int done=portable_ceremony_world_bounce_tick(&v->motion,i,ease);
            if(done<0)return 0;
            if(done){*script=0u;*tick=0u;++v->hop_completed;}
            break;}
        case 7:
            if(*tick>=60u){++v->arrival_completed;*script=8u;*tick=0u;}
            break;
        case 8:
            if(*tick>=30u){v->loser_darken=1u;*script=0u;*tick=0u;}
            break;
        case 9:{
            const int16_t ease=ceremony_curve9(256,0,*tick,32u,sine);
            const int done=portable_ceremony_world_bounce_tick_scaled(&v->motion,i,ease,2048);
            if(done<0)return 0;
            if(done){*script=0u;*tick=0u;}
            break;}
        default:return 0;
        }
    }
    return 1;
}
int portable_gp_ceremony_source_step(PortableGpCeremonyPresentation *v,
    const PortableGpCeremonySession *session,const PortableCeremonyMotionRules *rules,
    const PortableCeremonyWorldRules *world,const int32_t *sine,size_t sine_count,
    const PortableDriverAffineRules *affine,const PortableCharacterViewRules *character,
    const PortableFinishRules *finish,int confirm)
{
    if(!v||!session||!rules||!world||!sine||sine_count<1024u||!v->source_enabled)return 0;
    if(v->phase==PORTABLE_CEREMONY_FINISHED)return 1;
    const unsigned rank=session->player_standing_rank;
    /* Short Transitions settles the same owner, including its world poses. */
    if(v->phase==PORTABLE_CEREMONY_WAIT&&v->source_phase<14u){
        for(unsigned i=0;i<3u;++i){if(!portable_ceremony_world_place_podium(&v->motion,world,i))return 0;v->motion.world.drivers[i].visible=1u;}
        if(rank>2u){if(!portable_ceremony_world_reveal(&v->motion,world,3u))return 0;v->motion.world.drivers[3].position=v->motion.world.drivers[3].target;}
        memset(v->driver_script,0,sizeof v->driver_script);
        v->source_phase=14u;v->stats_reveal=16u;v->header_active=1u;v->header_exit=0u;ceremony_header_start(v,rules,rank,1);
        v->camera_vertical_offset=0;v->road_line=73;v->camera_return_tick=33u;
        portable_ceremony_zoom_start(&v->motion,1);
        if(rank<3u){if(!portable_ceremony_trophy_start(&v->motion,(uint8_t)rank,1))return 0;v->trophy_stage=4u;}
        v->motion.zoom.inverse_x=v->motion.zoom.inverse_y=256;
        v->motion.zoom.angle=0;v->motion.loser.visible=0u;v->source_tick=portable_gp_ceremony_input_tick(session,rules);
        v->coin_total=session->finishing_coin_total;v->coin_stage=2u;v->unlock_stage=4u;
        v->unlock_reveal=(session->extra_cup_unlocked||session->special_cup_unlocked)?16u:0u;
        for(unsigned i=0;i<4u;++i){v->coins[i].value=-1;v->coins[i].phase=3u;}
    }
    switch(v->source_phase){
    case 0:
        for(unsigned i=0;i<3u;++i){if(!portable_ceremony_world_reveal(&v->motion,world,i))return 0;v->driver_script[i]=1u;}
        ceremony_next_phase(v);break;
    case 1:
        if(++v->phase_ticks<=256u){
            v->road_line=(int16_t)(153u-v->phase_ticks*80u/256u);
            v->camera_vertical_offset=(int8_t)(v->road_line-73);
        }else ceremony_next_phase(v);
        break;
    case 2:
        if(v->arrival_completed>=3u){
            if(rank>2u){v->arrival_completed=0u;v->driver_script[3]=2u;v->driver_ticks[3]=0u;}
            ceremony_next_phase(v);
        }
        break;
    case 3:
        if(v->arrival_completed){
            portable_ceremony_loser_start(&v->motion);v->drop_stage=1u;v->overlay_ticks=0u;v->drop_elapsed=0u;
            ceremony_next_phase(v);
        }
        break;
    case 4:
        if(v->drop_stage==3u)ceremony_next_phase(v);
        break;
    case 5:
        if(rank<3u){v->hop_completed=0u;for(unsigned i=0;i<3u;++i){v->driver_script[i]=4u;v->driver_ticks[i]=0u;}ceremony_next_phase(v);}
        else {v->source_phase=11u;v->phase_ticks=0u;}
        break;
    case 6:if(v->hop_completed>=3u)ceremony_next_phase(v);break;
    case 7:
        if(rank==0u&&finish&&portable_ceremony_world_celebrate(&v->motion,0u,ceremony_finish(finish,session->podium_driver_classes[0]))<0)return 0;
        ceremony_next_phase(v);break;
    case 8:ceremony_next_phase(v);break;
    case 9:
        portable_ceremony_flypast_start(&v->motion);v->fly_stage=2u;
        if(rank<3u){v->trophy_stage=1u;v->overlay_ticks=0u;}
        ceremony_next_phase(v);break;
    case 10:if(v->trophy_stage==4u)ceremony_next_phase(v);break;
    case 11:
        portable_ceremony_zoom_start(&v->motion,0);v->zoom_stage=1u;v->points_ticks=0u;ceremony_coins_start(v,session);
        v->header_active=1u;v->header_exit=0u;v->stats_reveal=0u;ceremony_header_start(v,rules,rank,0);
        v->phase=PORTABLE_CEREMONY_CELEBRATION;ceremony_next_phase(v);break;
    case 12:
        if(++v->phase_ticks<=16u)v->stats_reveal=(uint8_t)v->phase_ticks;
        else ceremony_next_phase(v);
        break;
    case 13:
        if(v->zoom_stage==3u&&v->points_ticks>=346u){v->phase=PORTABLE_CEREMONY_WAIT;v->tick=0u;ceremony_next_phase(v);}
        break;
    case 14:
        if(confirm){v->header_exit=1u;v->phase=PORTABLE_CEREMONY_FADE;v->tick=0u;ceremony_next_phase(v);}
        break;
    case 15:
        if(++v->tick>=v->fade_ticks){v->phase=PORTABLE_CEREMONY_FINISHED;v->source_phase=16u;}
        break;
    case 16:break;
    default:return 0;
    }
    /* DBEFC restores the camera and launches the first8-unit settling
     * bounces120 ticks after the falling podium starts. */
    if(v->drop_stage&&++v->drop_elapsed>120u&&v->camera_return_tick<33u){
        ++v->camera_return_tick;
        if(v->camera_return_tick==1u)for(unsigned i=0;i<3u;++i){
            if(!portable_ceremony_world_bounce_start(&v->motion,i))return 0;
            v->driver_script[i]=9u;v->driver_ticks[i]=0u;
        }
        if(v->camera_return_tick<=32u){
            v->camera_vertical_offset=(int8_t)ceremony_curve5(10,0,v->camera_return_tick,32u,sine);
            v->road_line=(int16_t)(73+v->camera_vertical_offset);
        }
    }
    /* Source724 actor callbacks run after the main switch. */
    if(!ceremony_driver_scripts(v,world,sine,affine)||
       !portable_ceremony_tracks_tick(&v->motion,rules->tracks,rules->frames,rules->frame_count))return 0;
    if(v->drop_stage==1u){
        if(++v->overlay_ticks>=100u){v->drop_stage=2u;v->motion.loser.tick=0;}
    }else if(v->drop_stage==2u){
        uint16_t ignored;unsigned t=(unsigned)v->motion.loser.tick+1u;
        const int done=portable_ceremony_loser_tick(&v->motion,ceremony_curve9(-38,122,t,64u,sine),&ignored);
        if(done<0)return 0;
        if(done)v->drop_stage=3u;
    }
    if(v->motion.loser.visible)(void)portable_ceremony_loser_shadow_tick(&v->motion);
    if(v->fly_stage==2u)v->fly_stage=1u; /* DBD00's F1wait1 before firstpoll. */
    else if(v->fly_stage==1u){
        if(portable_ceremony_flypast_tick(&v->motion,sine,sine_count)>0){v->fly_stage=0u;v->motion.flypast.visible=0u;}
        else (void)portable_ceremony_flypast_animation_tick(&v->motion);
    }
    if(v->trophy_stage==1u){
        if(++v->overlay_ticks>rules->trophy_delay[rank]){
            if(!portable_ceremony_trophy_start(&v->motion,(uint8_t)rank,0))return 0;
            v->trophy_stage=2u;v->overlay_ticks=0u;
            if(portable_ceremony_trophy_appear_tick(&v->motion)<0)return 0; /* F4→F5 samepass. */
        }
    }else if(v->trophy_stage==2u){
        if(portable_ceremony_trophy_appear_tick(&v->motion)>0){v->trophy_stage=3u;v->overlay_ticks=0u;}
    }else if(v->trophy_stage==3u){
        if(++v->overlay_ticks>=200u&&portable_ceremony_trophy_move_tick(&v->motion,sine,sine_count)>0)v->trophy_stage=4u;
    }
    if(rank<3u&&v->trophy_stage)portable_ceremony_trophy_animation_tick(&v->motion);
    if(v->zoom_stage==1u){
        if(portable_ceremony_zoom_tick(&v->motion,sine,sine_count)>0){v->zoom_stage=2u;v->zoom_wait_ticks=0u;}
    }else if(v->zoom_stage==2u){if(++v->zoom_wait_ticks>=90u)v->zoom_stage=3u;}
    if(v->source_phase>=12u){ceremony_coins_tick(v,sine);ceremony_unlock_tick(v,session);++v->points_ticks;}
    v->palette_phase=(uint8_t)(v->source_tick&63u);
    if(v->header_active)ceremony_header_tick(v,rank,sine);
    for(unsigned i=0;i<v->motion.world.count;++i){
        const unsigned driver=v->motion.world.drivers[i].driver_class;
        if(!portable_ceremony_world_art_tick(&v->motion,i,character,ceremony_finish(finish,driver),affine,256u))return 0;
    }
    ++v->source_tick;
    return 1;
}
