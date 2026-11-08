#include "audio_pack_song.h"

#include "audio_sample_asset.h"
#include "audio_sequence_asset.h"
#include "audio_voice_bank_asset.h"

#include <string.h>

static PortableAudioPackSongStatus fail(PortableAudioPackSong *song,
                                        PortableAudioPackSongStatus status)
{
    if(song!=NULL)song->status=status;
    return status;
}

void portable_audio_pack_song_rebind(PortableAudioPackSong *song)
{
    if(song==NULL)return;
    song->runtime.bank=&song->bank;
    song->runtime.samples=song->samples;
    song->runtime.player.sequence=&song->sequence;
    song->runtime.pcm.tuning=&song->tuning;
    song->runtime.psg.tuning=&song->tuning;
}

PortableAudioPackSongStatus portable_audio_pack_song_init(
    PortableAudioPackSong *song,const PortableAssetPackView *pack,
    uint16_t song_id)
{
    PortableAssetSectionView section;
    uint32_t index;
    if(song==NULL||pack==NULL||song_id==0u)
        return PORTABLE_AUDIO_PACK_SONG_INVALID_ARGUMENT;
    memset(song,0,sizeof *song);
    if(portable_asset_pack_find_section(pack,
       PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE,song_id,&section)!=
       PORTABLE_ASSET_PACK_OK)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_MISSING_SEQUENCE);
    if(portable_audio_sequence_open(&song->sequence,section.bytes,section.size)!=
       PORTABLE_AUDIO_SEQUENCE_OK||song->sequence.song_id!=song_id)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_BAD_SEQUENCE);
    if(portable_asset_pack_find_section(pack,
       PORTABLE_ASSET_SECTION_AUDIO_VOICE_BANK,song->sequence.bank_id,&section)!=
       PORTABLE_ASSET_PACK_OK)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_MISSING_BANK);
    if(portable_audio_voice_bank_open(&song->bank,section.bytes,section.size)!=
       PORTABLE_AUDIO_VOICE_OK||
       song->bank.bank_id!=song->sequence.bank_id)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_BAD_BANK);
    for(index=0u;index<pack->section_count;++index){
        PortableAudioSampleView *sample;
        if(portable_asset_pack_section_at(pack,index,&section)!=
           PORTABLE_ASSET_PACK_OK)
            return fail(song,PORTABLE_AUDIO_PACK_SONG_BAD_SAMPLE);
        if(section.kind!=PORTABLE_ASSET_SECTION_AUDIO_SAMPLE)continue;
        if(song->sample_count>=PORTABLE_AUDIO_PACK_MAX_SAMPLES)
            return fail(song,PORTABLE_AUDIO_PACK_SONG_TOO_MANY_SAMPLES);
        sample=&song->samples[song->sample_count];
        if(portable_audio_sample_open(sample,section.bytes,section.size)!=
           PORTABLE_AUDIO_SAMPLE_OK||sample->sample_id!=section.content_id)
            return fail(song,PORTABLE_AUDIO_PACK_SONG_BAD_SAMPLE);
        song->sample_count++;
    }
    if(portable_audio_song_runtime_init(&song->runtime,&song->sequence,
       &song->bank,song->samples,song->sample_count)!=
       PORTABLE_AUDIO_RUNTIME_OK)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_RUNTIME_ERROR);
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_AUDIO_PLAYER_RULES,
        1u,&section)!=PORTABLE_ASSET_PACK_OK || section.version!=2u ||
        section.size!=155u || section.bytes==NULL || memcmp(section.bytes,"MKSCPRI1",8u)!=0 ||
        section.bytes[8]!=2u || section.bytes[9]!=0u || section.bytes[10]!=11u || section.bytes[11]!=0u ||
        !portable_audio_tuning_open(&song->tuning,section.bytes+23u,section.size-23u))
        return fail(song,PORTABLE_AUDIO_PACK_SONG_BAD_TUNING);
    song->runtime.pcm.tuning=&song->tuning;
    song->runtime.psg.tuning=&song->tuning;
    song->status=PORTABLE_AUDIO_PACK_SONG_OK;
    return song->status;
}

PortableAudioPackSongStatus portable_audio_pack_song_render_frame(
    PortableAudioPackSong *song,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u])
{
    if(song==NULL||output==NULL)
        return PORTABLE_AUDIO_PACK_SONG_INVALID_ARGUMENT;
    if(song->status!=PORTABLE_AUDIO_PACK_SONG_OK)return song->status;
    portable_audio_pack_song_rebind(song);
    if(portable_audio_song_runtime_render_frame(&song->runtime,output)!=
       PORTABLE_AUDIO_RUNTIME_OK)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_RUNTIME_ERROR);
    return PORTABLE_AUDIO_PACK_SONG_OK;
}

void portable_audio_pack_song_pause(PortableAudioPackSong *song,int paused)
{
    if(song!=NULL&&song->status==PORTABLE_AUDIO_PACK_SONG_OK){
        portable_audio_pack_song_rebind(song);
        portable_audio_song_runtime_pause(&song->runtime,paused);
    }
}

PortableAudioPackSongStatus portable_audio_pack_song_control(
    PortableAudioPackSong *song,uint16_t track_bits,uint16_t volume,
    int16_t pitch)
{
    if(song==NULL)return PORTABLE_AUDIO_PACK_SONG_INVALID_ARGUMENT;
    if(song->status!=PORTABLE_AUDIO_PACK_SONG_OK)return song->status;
    portable_audio_pack_song_rebind(song);
    if(portable_audio_song_runtime_control(&song->runtime,track_bits,volume,
       pitch)!=PORTABLE_AUDIO_RUNTIME_OK)
        return fail(song,PORTABLE_AUDIO_PACK_SONG_RUNTIME_ERROR);
    return song->status;
}

const char *portable_audio_pack_song_status_name(
    PortableAudioPackSongStatus status)
{
    static const char *const names[]={"ok","invalid argument",
        "missing sequence","bad sequence","missing bank","bad bank",
        "too many samples","bad sample","runtime error",
        "missing or invalid audio tuning; reimport owner ROM"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:
        "unknown status";
}
