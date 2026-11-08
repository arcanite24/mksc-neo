#include "audio_session.h"

#include <limits.h>
#include <string.h>

static PortableAudioSessionStatus fail(PortableAudioSession *session,
                                       PortableAudioSessionStatus status)
{
    if(session!=NULL)session->status=status;
    return status;
}

/* An N64 START_SONG argument: its section, and for an effect its bank and
 * sound (bank 0xff for a whole song). */
static int n64_song(const PortableAssetPackView *pack, uint16_t argument, PortableN64SequenceView *song,
                    uint8_t *bank, uint8_t *sound)
{
    PortableAssetSectionView section;
    const int effect = (argument & PORTABLE_AUDIO_N64_EFFECT) != 0u;
    const uint16_t id = effect ? 1u : (uint16_t)(argument & 0x1fffu);
    *bank = effect ? (uint8_t)((argument >> 8) & 0x1fu) : 0xffu;
    *sound = (uint8_t)argument;
    return pack != NULL && PORTABLE_AUDIO_IS_N64_SONG(argument) && id != 0u &&
           portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_N64_SEQUENCE, id, &section) ==
               PORTABLE_ASSET_PACK_OK && section.version == 1u &&
           portable_n64_sequence_open(song, section.bytes, section.size) == PORTABLE_N64_OK &&
           (!effect || (*bank < PORTABLE_N64_SOUND_BANKS && song->effect_channels[*bank] && *sound < 0x80u));
}

/* AUTO follows imported sequence metadata. Explicit producers may select a
 * different player, as the original local-lava callback does. */
uint8_t portable_audio_session_song_player(const PortableAssetPackView *pack,
    uint16_t song_id)
{
    PortableAssetSectionView section;
    PortableAudioSequenceView sequence;
    if(PORTABLE_AUDIO_IS_N64_SONG(song_id)){
        PortableN64SequenceView song;
        uint8_t bank,sound;
        if(!n64_song(pack,song_id,&song,&bank,&sound))return PORTABLE_AUDIO_PLAYER_AUTO;
        const uint8_t player=bank==0xffu?song.player:song.bank_players[bank];
        return player<PORTABLE_AUDIO_PLAYER_COUNT?player:PORTABLE_AUDIO_PLAYER_AUTO;
    }
    if(pack!=NULL && (song_id&PORTABLE_AUDIO_NDS_SONG)){
        PortableAssetSectionView section;
        PortableNdsSequenceView song;
        return portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_NDS_SEQUENCE,
                   song_id&0x7fffu,&section)==PORTABLE_ASSET_PACK_OK && (section.version==1u||section.version==2u) &&
               portable_nds_sequence_open(&song,section.bytes,section.size)==PORTABLE_NDS_OK &&
               song.player<PORTABLE_AUDIO_PLAYER_COUNT ? song.player : PORTABLE_AUDIO_PLAYER_AUTO;
    }
    if(pack==NULL || song_id==0u ||
        portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE,
            song_id,&section)!=PORTABLE_ASSET_PACK_OK || section.version!=2u ||
        portable_audio_sequence_open(&sequence,section.bytes,section.size)!=PORTABLE_AUDIO_SEQUENCE_OK ||
        sequence.song_id!=song_id)
        return PORTABLE_AUDIO_PLAYER_AUTO;
    return sequence.player_index;
}

int portable_audio_session_player_playing(
    const PortableAudioSession *session, uint8_t slot)
{
    if (session == NULL || slot >= PORTABLE_AUDIO_PLAYER_COUNT)return 0;
    const PortableAudioSessionPlayer *voice = &session->players[slot];
    if (!voice->active)return 0;
    if (voice->nds)return session->nds[slot].active;
    if (voice->n64)return portable_n64_player_playing(&session->n64[slot]);
    for (size_t track = 0; track < PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS; ++track)
        if (voice->song.runtime.player.tracks[track].active)return 1;
    return 0;
}

int portable_audio_session_song_loaded(const PortableAudioSession *session,
    uint16_t song_id)
{
    if(session==NULL||song_id==0u)return 0;
    for(uint8_t slot=0;slot<PORTABLE_AUDIO_PLAYER_COUNT;++slot)
        if(session->players[slot].active&&session->players[slot].song_id==song_id)return 1;
    return 0;
}

int portable_audio_session_song_playing(
    const PortableAudioSession *session, uint16_t song_id)
{
    if(session==NULL)return 0;
    for(uint8_t slot=0u;slot<PORTABLE_AUDIO_PLAYER_COUNT;++slot)
        if(session->players[slot].song_id==song_id &&
            portable_audio_session_player_playing(session,slot))return 1;
    return 0;
}

static void bind_player(PortableAudioSession *session,uint8_t index)
{
    if(session->players[index].nds||session->players[index].n64)return;
    portable_audio_pack_song_rebind(&session->players[index].song);
    PortableAudioSongRuntime *runtime=&session->players[index].song.runtime;
    runtime->shared_pcm=&session->pcm;
    runtime->shared_psg=&session->psg;
    runtime->player.memory_area=session->memory;
    /* ROM gMPlayTable track arrays ascend by player then track. Reserve16
     * ordered IDs per player; zero is the original detached/null owner. */
    runtime->owner_base=(uint16_t)(1u+index*PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS);
}

static void rebind_session(PortableAudioSession *session)
{
    if(session->pcm.tuning!=NULL)session->pcm.tuning=&session->tuning;
    if(session->psg.tuning!=NULL)session->psg.tuning=&session->tuning;
    for(uint8_t i=0;i<PORTABLE_AUDIO_PLAYER_COUNT;++i)
        if(session->players[i].active)bind_player(session,i);
}

static void stop_player(PortableAudioSession *session,uint8_t index)
{
    if(session->players[index].nds)portable_nds_player_stop(&session->nds[index]);
    else if(session->players[index].n64)portable_n64_player_stop(&session->n64[index]);
    else if(session->players[index].active){
        bind_player(session,index);
        portable_audio_song_runtime_stop(&session->players[index].song.runtime);
    }
    memset(&session->players[index],0,sizeof session->players[index]);
}

PortableAudioSessionStatus portable_audio_session_init(
    PortableAudioSession *session,const PortableAssetPackView *pack)
{
    if(session==NULL||pack==NULL)
        return PORTABLE_AUDIO_SESSION_INVALID_ARGUMENT;
    memset(session,0,sizeof *session);
    session->pack=pack;
    portable_audio_pcm_mixer_init(&session->pcm);
    session->pcm.voice_count=6u; /* m4aSoundInit SOUND_MODE_MAXCHN=6 */
    portable_audio_psg_mixer_init(&session->psg);
    session->status=PORTABLE_AUDIO_SESSION_OK;
    PortableAssetSectionView rules;
    const PortableAssetPackStatus found=portable_asset_pack_find_section(
        pack,PORTABLE_ASSET_SECTION_AUDIO_PLAYER_RULES,1u,&rules);
    if(found==PORTABLE_ASSET_PACK_OK){
        if(rules.bytes==NULL||(rules.version!=1u&&rules.version!=2u)||
           rules.size!=(rules.version==2u?155u:23u)||
           memcmp(rules.bytes,"MKSCPRI1",8u)!=0||
           rules.bytes[8]!=rules.version||rules.bytes[9]!=0u||
           rules.bytes[10]!=PORTABLE_AUDIO_PLAYER_COUNT||rules.bytes[11]!=0u)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
        if(rules.version==2u){
            if(!portable_audio_tuning_open(&session->tuning,rules.bytes+23u,rules.size-23u))
                return fail(session,PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
            session->pcm.tuning=&session->tuning;
            session->psg.tuning=&session->tuning;
        }
        for(size_t i=0u;i<PORTABLE_AUDIO_PLAYER_COUNT;++i){
            if(rules.bytes[12u+i]>1u)
                return fail(session,PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
            session->priority_guard[i]=rules.bytes[12u+i];
        }
    }
    return session->status;
}

/* Starting a song may be a successful no-op when a guarded player already
 * owns a higher-priority sequence. The dispatcher still counts that command. */
static PortableAudioSessionStatus start_song(PortableAudioSession *session,
                                             const PortableAudioCommand *command)
{
    PortableAudioSessionPlayer *player;
    uint8_t player_id;
    if(command->player!=PORTABLE_AUDIO_PLAYER_AUTO&&
       command->player>=PORTABLE_AUDIO_PLAYER_COUNT)
        return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
    player_id=command->player==PORTABLE_AUDIO_PLAYER_AUTO?
        portable_audio_session_song_player(session->pack,command->argument):
        command->player;
    if(player_id==PORTABLE_AUDIO_PLAYER_AUTO)
        return fail(session,PORTABLE_AUDIO_SESSION_BAD_SONG);
    if(session->pcm.tuning==NULL||session->psg.tuning==NULL)
        return fail(session,PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
    player=&session->players[player_id];
    if(PORTABLE_AUDIO_IS_N64_SONG(command->argument)){
        PortableN64SequenceView song;
        uint8_t bank,sound;
        if(!n64_song(session->pack,command->argument,&song,&bank,&sound))
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_SONG);
        stop_player(session,player_id);
        if(bank==0xffu)portable_n64_player_start(&session->n64[player_id],&song);
        else if(portable_n64_player_start_effect(&session->n64[player_id],&song,bank,sound)!=PORTABLE_N64_OK)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_SONG);
        *player=(PortableAudioSessionPlayer){.song_id=command->argument,.volume=UINT16_C(0x0100),
            .fade_volume=UINT16_C(0x0100),.active=1u,.n64=1u,.follow=bank==0xffu?song.follow:0u};
        return session->status;
    }
    if(command->argument&PORTABLE_AUDIO_NDS_SONG){
        PortableAssetSectionView section;
        PortableNdsSequenceView song;
        if(portable_asset_pack_find_section(session->pack,PORTABLE_ASSET_SECTION_NDS_SEQUENCE,
               command->argument&0x7fffu,&section)!=PORTABLE_ASSET_PACK_OK||
           (section.version!=1u&&section.version!=2u)||
           portable_nds_sequence_open(&song,section.bytes,section.size)!=PORTABLE_NDS_OK)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_SONG);
        stop_player(session,player_id);
        portable_nds_player_start(&session->nds[player_id],&song);
        *player=(PortableAudioSessionPlayer){.song_id=command->argument,.volume=UINT16_C(0x0100),
            .fade_volume=UINT16_C(0x0100),.active=1u,.nds=1u,
            .follow=song.follow?(uint16_t)(PORTABLE_AUDIO_NDS_SONG|song.follow):0u};
        return session->status;
    }
    /* MPlayStart: guarded players reject lower priorities until the old
     * song ends or is paused after its initial track-start update. */
    if(session->priority_guard[player_id]&&player->active&&
       (player->start_pending||
        (!player->paused&&portable_audio_session_player_playing(session,player_id)))){
        PortableAssetSectionView section;
        PortableAudioSequenceView next;
        if(portable_asset_pack_find_section(session->pack,
           PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE,command->argument,&section)!=
           PORTABLE_ASSET_PACK_OK||portable_audio_sequence_open(&next,
           section.bytes,section.size)!=PORTABLE_AUDIO_SEQUENCE_OK||
           next.song_id!=command->argument)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_SONG);
        if(player->song.sequence.priority>next.priority)return session->status;
    }
    if(player->nds||player->n64)stop_player(session,player_id);
    else if(player->active){
        bind_player(session,player_id);
        portable_audio_song_runtime_stop(&player->song.runtime);
    }
    if(portable_audio_pack_song_init(&player->song,session->pack,
       command->argument)!=PORTABLE_AUDIO_PACK_SONG_OK)
        return fail(session,PORTABLE_AUDIO_SESSION_BAD_SONG);
    player->song_id=command->argument;
    player->volume=UINT16_C(0x0100);
    player->fade_volume=UINT16_C(0x0100);
    player->fade_interval=0u;
    player->fade_counter=0u;
    player->pitch=0;
    player->active=1u;
    player->paused=0u;
    player->start_pending=1u;
    bind_player(session,player_id);
    return session->status;
}

PortableAudioSessionStatus portable_audio_session_apply(
    PortableAudioSession *session,const PortableAudioCommand *command)
{
    PortableAudioSessionPlayer *player;
    if(session==NULL||command==NULL||session->pack==NULL)
        return PORTABLE_AUDIO_SESSION_INVALID_ARGUMENT;
    if(session->status!=PORTABLE_AUDIO_SESSION_OK)return session->status;
    rebind_session(session);
    switch((PortableAudioCommandOperation)command->operation){
    case PORTABLE_AUDIO_COMMAND_START_SONG:
        if(start_song(session,command)!=PORTABLE_AUDIO_SESSION_OK)
            return session->status;
        break;
    case PORTABLE_AUDIO_COMMAND_STOP_PLAYER:
        if(command->player>=PORTABLE_AUDIO_PLAYER_COUNT||command->argument!=0u)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
        stop_player(session,command->player);
        break;
    case PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER:
    case PORTABLE_AUDIO_COMMAND_RESUME_PLAYER:
        if(command->player>=PORTABLE_AUDIO_PLAYER_COUNT||command->argument!=0u)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
        player=&session->players[command->player];
        player->paused=command->operation==PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER;
        if(player->nds)session->nds[command->player].paused=player->paused;
        else if(player->n64)session->n64[command->player].paused=player->paused;
        else if(player->active)
            portable_audio_pack_song_pause(&player->song,player->paused);
        break;
    case PORTABLE_AUDIO_COMMAND_FADE_PLAYER:
        if(command->player>=PORTABLE_AUDIO_PLAYER_COUNT||command->argument==0u)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
        player=&session->players[command->player];
        player->fade_volume=UINT16_C(0x0100);
        player->fade_interval=command->argument;
        player->fade_counter=command->argument;
        break;
    case PORTABLE_AUDIO_COMMAND_SET_TEMPO:
        if(command->player>=PORTABLE_AUDIO_PLAYER_COUNT)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
        if(session->players[command->player].nds)session->nds[command->player].tempo_scale=command->argument;
        else if(session->players[command->player].n64)session->n64[command->player].tempo_scale=command->argument;
        else session->players[command->player].song.runtime.tempo_scale=command->argument;
        break;
    case PORTABLE_AUDIO_COMMAND_SET_VOLUME:
    case PORTABLE_AUDIO_COMMAND_SET_PITCH:
        if(command->player>=PORTABLE_AUDIO_PLAYER_COUNT)
            return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
        player=&session->players[command->player];
        if(command->operation==PORTABLE_AUDIO_COMMAND_SET_VOLUME){
            if(command->argument>UINT16_C(0x03ff))
                return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
            player->volume=command->argument;
        }else player->pitch=(int16_t)command->argument;
        if(player->nds){
            session->nds[command->player].volume=(uint16_t)(((uint32_t)player->volume*player->fade_volume)>>8);
            session->nds[command->player].pitch=player->pitch;
        }else if(player->n64){
            session->n64[command->player].volume=(uint16_t)(((uint32_t)player->volume*player->fade_volume)>>8);
            session->n64[command->player].pitch=player->pitch;
        }else if(player->active&&portable_audio_pack_song_control(&player->song,1u,
           (uint16_t)(((uint32_t)player->volume*player->fade_volume)>>8),
           player->pitch)!=PORTABLE_AUDIO_PACK_SONG_OK)
            return fail(session,PORTABLE_AUDIO_SESSION_RENDER_ERROR);
        break;
    default:return fail(session,PORTABLE_AUDIO_SESSION_BAD_COMMAND);
    }
    session->applied_commands++;
    return session->status;
}

PortableAudioSessionStatus portable_audio_session_render_frame(
    PortableAudioSession *session,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u])
{
    size_t player_id;
    if(session==NULL||output==NULL||session->pack==NULL)
        return PORTABLE_AUDIO_SESSION_INVALID_ARGUMENT;
    if(session->status!=PORTABLE_AUDIO_SESSION_OK)return session->status;
    rebind_session(session);
    for(player_id=0u;player_id<PORTABLE_AUDIO_PLAYER_COUNT;++player_id){
        PortableAudioSessionPlayer *player=&session->players[player_id];
        if(!player->active)continue;
        if(player->fade_interval!=0u&&--player->fade_counter==0u){
            player->fade_counter=player->fade_interval;
            player->fade_volume=player->fade_volume>UINT16_C(0x0010)?
                (uint16_t)(player->fade_volume-UINT16_C(0x0010)):0u;
            if(player->fade_volume==0u){
                stop_player(session,(uint8_t)player_id);
                continue;
            }
            if(player->nds)session->nds[player_id].volume=(uint16_t)(((uint32_t)player->volume*player->fade_volume)>>8);
            else if(player->n64)session->n64[player_id].volume=(uint16_t)(((uint32_t)player->volume*player->fade_volume)>>8);
            else if(portable_audio_pack_song_control(&player->song,1u,
               (uint16_t)(((uint32_t)player->volume*player->fade_volume)>>8),
               player->pitch)!=PORTABLE_AUDIO_PACK_SONG_OK)
                return fail(session,PORTABLE_AUDIO_SESSION_RENDER_ERROR);
        }
        if(player->nds||player->n64)continue;
        bind_player(session,(uint8_t)player_id);
        if(portable_audio_song_runtime_advance_frame(&player->song.runtime)!=
           PORTABLE_AUDIO_RUNTIME_OK)
            return fail(session,PORTABLE_AUDIO_SESSION_RENDER_ERROR);
        if(!player->paused)player->start_pending=0u;
    }
    portable_audio_song_runtime_mix(&session->pcm,&session->psg,output);
    for(player_id=0u;player_id<PORTABLE_AUDIO_PLAYER_COUNT;++player_id){
        PortableAudioSessionPlayer *player=&session->players[player_id];
        if(!player->nds)continue;
        if(portable_nds_player_mix(&session->nds[player_id],output)!=PORTABLE_NDS_OK)
            return fail(session,PORTABLE_AUDIO_SESSION_RENDER_ERROR);
        /* A DS finish fanfare chains into its results theme. */
        if(player->follow&&!player->paused&&!portable_nds_player_playing(&session->nds[player_id])){
            const PortableAudioCommand next={session->rendered_frames,0u,player->follow,
                PORTABLE_AUDIO_COMMAND_START_SONG,(uint8_t)player_id};
            if(start_song(session,&next)!=PORTABLE_AUDIO_SESSION_OK)return session->status;
        }
    }
    for(player_id=0u;player_id<PORTABLE_AUDIO_PLAYER_COUNT;++player_id){
        PortableAudioSessionPlayer *player=&session->players[player_id];
        if(!player->n64)continue;
        if(portable_n64_player_mix(&session->n64[player_id],output)!=PORTABLE_N64_OK)
            return fail(session,PORTABLE_AUDIO_SESSION_RENDER_ERROR);
        /* MK64 chains a finish fanfare into its results theme. */
        if(player->follow&&!portable_n64_player_playing(&session->n64[player_id])){
            const PortableAudioCommand next={session->rendered_frames,0u,player->follow,
                PORTABLE_AUDIO_COMMAND_START_SONG,(uint8_t)player_id};
            if(start_song(session,&next)!=PORTABLE_AUDIO_SESSION_OK)return session->status;
        }
    }
    session->rendered_frames++;
    return session->status;
}

void portable_audio_session_stop_all(PortableAudioSession *session)
{
    if(session==NULL)return;
    memset(session->players,0,sizeof session->players);
    for(size_t i=0u;i<PORTABLE_AUDIO_PLAYER_COUNT;++i){
        portable_nds_player_stop(&session->nds[i]);
        portable_n64_player_stop(&session->n64[i]);
    }
    const int has_tuning=session->pcm.tuning!=NULL;
    portable_audio_pcm_mixer_init(&session->pcm);
    session->pcm.voice_count=6u;
    portable_audio_psg_mixer_init(&session->psg);
    if(has_tuning){session->pcm.tuning=&session->tuning;session->psg.tuning=&session->tuning;}
}

const char *portable_audio_session_status_name(
    PortableAudioSessionStatus status)
{
    static const char *const names[]={"ok","invalid argument","bad command",
        "bad song","render error","missing or invalid audio rules; reimport owner ROM"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:
        "unknown status";
}
