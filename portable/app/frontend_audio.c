#include "frontend_audio.h"

#include <stdint.h>
#include <string.h>

static int emit(PortableAudioCommandStream *commands,uint32_t tick,
                PortableAudioCommandOperation operation,uint8_t player,
                uint16_t argument)
{
    return portable_audio_command_stream_emit(commands,tick,operation,player,
        argument)==PORTABLE_AUDIO_STREAM_OK;
}

static uint16_t character_lock_song(const PortableFrontendAudio *audio,uint32_t character_id)
{
    return audio->character_cues!=NULL&&character_id>=1u&&character_id<=8u?
        audio->character_cues->lock_voices[character_id-1u]:0u;
}

static PortableCharacterRevCue character_rev(const PortableFrontendAudio *audio,
    uint32_t character_id,unsigned launch,uint8_t *player)
{
    *player=PORTABLE_AUDIO_PLAYER_AUTO;
    if(audio->character_cues==NULL||character_id<1u||character_id>8u)
        return (PortableCharacterRevCue){0};
    *player=audio->rev_players[launch][character_id-1u];
    return launch?audio->character_cues->launch_revs[character_id-1u]:
        audio->character_cues->menu_revs[character_id-1u];
}

int portable_frontend_audio_set_character_cues(PortableFrontendAudio *audio,
    const PortableCharacterAudioRules *rules,const uint8_t rev_players[2][8])
{
    if(audio==NULL)return 0;
    if(rules!=NULL){
        if(!portable_character_audio_rules_valid(rules)||rev_players==NULL)return 0;
        for(unsigned table=0;table<2u;++table)for(unsigned i=0;i<8u;++i)
            if(rev_players[table][i]>=PORTABLE_AUDIO_PLAYER_COUNT)return 0;
        memcpy(audio->rev_players,rev_players,sizeof audio->rev_players);
    }
    audio->character_cues=rules;
    return 1;
}

static int delay_song(PortableFrontendAudio *audio,uint32_t tick,uint16_t song,
                      uint16_t volume,uint8_t volume_player)
{
    for(size_t index=0u;index<sizeof audio->delayed/sizeof audio->delayed[0];++index)
        if(audio->delayed[index].song==0u){
            audio->delayed[index].tick=tick;
            audio->delayed[index].song=song;
            audio->delayed[index].volume=volume;
            audio->delayed[index].volume_player=volume_player;
            return 1;
        }
    return 1; /* A full queue drops the cue like a busy actor. */
}

static int flush_delayed(PortableFrontendAudio *audio,
    PortableAudioCommandStream *commands,uint32_t until)
{
    for(;;){
        size_t next=SIZE_MAX;
        for(size_t index=0u;index<sizeof audio->delayed/sizeof audio->delayed[0];++index)
            if(audio->delayed[index].song!=0u&&audio->delayed[index].tick<=until&&
               (next==SIZE_MAX||audio->delayed[index].tick<audio->delayed[next].tick))
                next=index;
        if(next==SIZE_MAX)return 1;
        const uint32_t tick=audio->delayed[next].tick;
        const uint16_t song=audio->delayed[next].song,volume=audio->delayed[next].volume;
        const uint8_t player=audio->delayed[next].volume_player;
        audio->delayed[next].song=0u;
        if(!emit(commands,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                 PORTABLE_AUDIO_PLAYER_AUTO,song))return 0;
        if(player!=PORTABLE_AUDIO_PLAYER_AUTO&&!emit(commands,tick,PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,volume))
            return 0;
    }
}

int portable_frontend_audio_init(PortableFrontendAudio *audio,
    const PortableFrontendFlow *flow,PortableAudioCommandStream *commands)
{
    if(audio==NULL||flow==NULL||commands==NULL||
       (flow->page!=PORTABLE_FRONTEND_PAGE_TITLE_INTRO&&
        flow->page!=PORTABLE_FRONTEND_PAGE_TITLE_MENU))return 0;
    memset(audio,0,sizeof *audio);
    audio->next_event=flow->event_count;
    if(flow->page==PORTABLE_FRONTEND_PAGE_TITLE_MENU)
        audio->title_loop_started=1u;
    return emit(commands,flow->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                PORTABLE_AUDIO_PLAYER_AUTO,
                flow->page==PORTABLE_FRONTEND_PAGE_TITLE_MENU?52u:
                flow->title_variant==2u?51u:53u); /* 1888: isNight ? 51 : 53 */
}

static int event_command(PortableFrontendAudio *audio,
    const PortableFrontendFlow *flow,const PortableFrontendEvent *event,
    PortableAudioCommandStream *commands)
{
    switch((PortableFrontendEventType)event->type){
    case PORTABLE_FRONTEND_EVENT_PAGE_ENTER:
        if(event->value==PORTABLE_FRONTEND_PAGE_TITLE_MENU&&
           !audio->title_loop_started){
            audio->title_loop_started=1u;
            return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        PORTABLE_AUDIO_PLAYER_AUTO,52u);
        }
        return 1;
    case PORTABLE_FRONTEND_EVENT_MOVE:
        return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                    PORTABLE_AUDIO_PLAYER_AUTO,141u);
    case PORTABLE_FRONTEND_EVENT_OPEN:
        return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                    PORTABLE_AUDIO_PLAYER_AUTO,142u);
    case PORTABLE_FRONTEND_EVENT_RECORD_ERASE:
    case PORTABLE_FRONTEND_EVENT_GHOST_ERASE:
    case PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE:
    case PORTABLE_FRONTEND_EVENT_ACCEPT:
        if(event->page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO){
            if(audio->title_loop_started)return 1;
            audio->title_loop_started=1u;
            return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        PORTABLE_AUDIO_PLAYER_AUTO,52u);
        }
        if(!emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                 PORTABLE_AUDIO_PLAYER_AUTO,143u))return 0;
        if(event->page==PORTABLE_FRONTEND_PAGE_COURSE&&
           event->value==PORTABLE_FRONTEND_PAGE_RACE){
            /* 9998: D95FC rev at T+19; outside GP the kart hops (156) at T+37. */
            uint8_t player;
            const uint32_t voice=portable_frontend_flow_voice_id(flow,flow->selected_character);
            const PortableCharacterRevCue cue=character_rev(audio,voice,1u,&player);
            if(cue.song!=0u&&!delay_song(audio,event->tick+19u,cue.song,cue.volume,player))
                return 0;
            if(flow->mode_cursor!=0u&&!delay_song(audio,event->tick+37u,156u,0u,PORTABLE_AUDIO_PLAYER_AUTO))return 0;
        }
        if(event->page==PORTABLE_FRONTEND_PAGE_CHARACTER&&
           event->value==PORTABLE_FRONTEND_PAGE_COURSE){
            uint8_t player;
            const PortableCharacterRevCue cue=character_rev(audio,
                portable_frontend_flow_voice_id(flow,flow->selected_character),0u,&player);
            if(cue.song!=0u&&!delay_song(audio,event->tick+35u,cue.song,cue.volume,player))return 0;
        }
        if(event->page==PORTABLE_FRONTEND_PAGE_TITLE_MENU){
            audio->title_stop_tick=event->tick+17u;
            audio->menu_start_tick=event->tick+21u;
            audio->title_exit_pending=1u;
        }
        return 1;
    case PORTABLE_FRONTEND_EVENT_BACK:
        return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                    PORTABLE_AUDIO_PLAYER_AUTO,144u);
    case PORTABLE_FRONTEND_EVENT_KART_TRICK:
        if(event->value!=0u)
            return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        PORTABLE_AUDIO_PLAYER_AUTO,123u);
        if(!emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                 PORTABLE_AUDIO_PLAYER_AUTO,156u))return 0;
        return delay_song(audio,event->tick+18u,159u,0u,PORTABLE_AUDIO_PLAYER_AUTO);
    case PORTABLE_FRONTEND_EVENT_TRACK_SET:
        return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                    PORTABLE_AUDIO_PLAYER_AUTO,147u);
    case PORTABLE_FRONTEND_EVENT_UNAVAILABLE:
        return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                    PORTABLE_AUDIO_PLAYER_AUTO,145u);
    case PORTABLE_FRONTEND_EVENT_HORN:
        {
            if(audio->character_cues==NULL||event->value>=9u)return 1;
            const uint16_t song=audio->character_cues->menu_horns[event->value];
            if(song==0u)return 1;
            return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        PORTABLE_AUDIO_PLAYER_AUTO,song);
        }
    case PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK:
        {
            const uint16_t song=character_lock_song(audio,
                portable_frontend_flow_voice_id(flow,event->value));
            if(song==0u)return 1;
            return emit(commands,event->tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        PORTABLE_AUDIO_PLAYER_AUTO,song);
        }
    default:return 1;
    }
}

int portable_frontend_audio_tick(PortableFrontendAudio *audio,
    const PortableFrontendFlow *flow,PortableAudioCommandStream *commands)
{
    if(audio==NULL||flow==NULL||commands==NULL||
       audio->next_event>flow->event_count)return 0;
    while(audio->next_event<flow->event_count){
        if(!flush_delayed(audio,commands,flow->events[audio->next_event].tick))return 0;
        if(!event_command(audio,flow,&flow->events[audio->next_event],commands))
            return 0;
        audio->next_event++;
    }
    if(audio->title_exit_pending&&flow->tick>=audio->title_stop_tick){
        if(!emit(commands,audio->title_stop_tick,
                 PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER,0u,0u))return 0;
        audio->title_stop_tick=UINT32_MAX;
    }
    if(audio->title_exit_pending&&flow->tick>=audio->menu_start_tick){
        if(!emit(commands,audio->menu_start_tick,
                 PORTABLE_AUDIO_COMMAND_START_SONG,
                 PORTABLE_AUDIO_PLAYER_AUTO,2u))return 0;
        audio->title_exit_pending=0u;
    }
    if(!flush_delayed(audio,commands,flow->tick))return 0;
    return 1;
}
