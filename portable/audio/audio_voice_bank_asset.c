#include "audio_voice_bank_asset.h"

#include <string.h>

static int direct_source(uint8_t source_kind);

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

PortableAudioVoiceStatus portable_audio_voice_bank_voice(
    const PortableAudioVoiceBankView *view, uint16_t index,
    PortableAudioVoice *voice)
{
    const uint8_t *source;
    if (view == NULL || voice == NULL || view->data == NULL)
        return PORTABLE_AUDIO_VOICE_INVALID_ARGUMENT;
    if (index >= view->voice_count)
        return PORTABLE_AUDIO_VOICE_BAD_SIZE;
    source = view->data + view->voices_offset +
        (size_t)index * PORTABLE_AUDIO_VOICE_RECORD_SIZE;
    voice->original_type=source[0];voice->key=source[1];
    voice->length=source[2];voice->pan_sweep=source[3];
    voice->attack=source[4];voice->decay=source[5];
    voice->sustain=source[6];voice->release=source[7];
    voice->source_kind=source[8];voice->source_id=read_u32(source+12u);
    voice->mapping_first=read_u16(source+16u);
    voice->mapping_count=read_u16(source+18u);
    if (source[9]!=0u||source[10]!=0u||source[11]!=0u)
        return PORTABLE_AUDIO_VOICE_BAD_SOURCE;
    return PORTABLE_AUDIO_VOICE_OK;
}

PortableAudioVoiceStatus portable_audio_voice_bank_mapping(
    const PortableAudioVoiceBankView *view, uint32_t index,
    PortableAudioVoiceMapping *mapping)
{
    const uint8_t *source;
    if(view==NULL||mapping==NULL||view->data==NULL)
        return PORTABLE_AUDIO_VOICE_INVALID_ARGUMENT;
    if(index>=view->mapping_count)return PORTABLE_AUDIO_VOICE_BAD_SIZE;
    source=view->data+view->mappings_offset+
        (size_t)index*PORTABLE_AUDIO_VOICE_MAPPING_SIZE;
    mapping->key_start=source[0];mapping->key_end=source[1];
    mapping->voice_index=read_u16(source+2u);mapping->tone_key=source[4];
    mapping->pan=source[5];
    if(read_u16(source+6u)!=0u)return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
    return PORTABLE_AUDIO_VOICE_OK;
}

PortableAudioVoiceStatus portable_audio_voice_bank_resolve(
    const PortableAudioVoiceBankView *view,uint16_t index,uint8_t key,
    PortableAudioVoice *voice,uint16_t *resolved_index)
{
    PortableAudioVoice candidate;PortableAudioVoiceStatus status;
    uint32_t mapping_index,end;
    if(view==NULL||voice==NULL||key>127u)
        return PORTABLE_AUDIO_VOICE_INVALID_ARGUMENT;
    status=portable_audio_voice_bank_voice(view,index,&candidate);
    if(status!=PORTABLE_AUDIO_VOICE_OK)return status;
    if(direct_source(candidate.source_kind)){
        *voice=candidate;if(resolved_index!=NULL)*resolved_index=index;
        return PORTABLE_AUDIO_VOICE_OK;
    }
    end=(uint32_t)candidate.mapping_first+candidate.mapping_count;
    for(mapping_index=candidate.mapping_first;mapping_index<end;++mapping_index){
        PortableAudioVoiceMapping mapping;
        status=portable_audio_voice_bank_mapping(view,mapping_index,&mapping);
        if(status!=PORTABLE_AUDIO_VOICE_OK)return status;
        if(key<mapping.key_start||key>mapping.key_end)continue;
        status=portable_audio_voice_bank_voice(view,mapping.voice_index,voice);
        if(status!=PORTABLE_AUDIO_VOICE_OK||!direct_source(voice->source_kind))
            return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
        voice->key=mapping.tone_key;voice->pan_sweep=mapping.pan;
        if(resolved_index!=NULL)*resolved_index=mapping.voice_index;
        return PORTABLE_AUDIO_VOICE_OK;
    }
    return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
}

static int direct_source(uint8_t source_kind)
{
    return source_kind>=PORTABLE_AUDIO_VOICE_PCM&&
        source_kind<=PORTABLE_AUDIO_VOICE_PSG_NOISE;
}

PortableAudioVoiceStatus portable_audio_voice_bank_open(
    PortableAudioVoiceBankView *view, const uint8_t *data, size_t size)
{
    static const uint8_t magic[8]={'M','K','S','C','V','O','C','1'};
    PortableAudioVoiceBankView candidate;
    uint16_t voice_index;
    if(view==NULL||data==NULL)return PORTABLE_AUDIO_VOICE_INVALID_ARGUMENT;
    if(size<PORTABLE_AUDIO_VOICE_BANK_HEADER_SIZE)
        return PORTABLE_AUDIO_VOICE_BAD_SIZE;
    if(memcmp(data,magic,sizeof magic)!=0)return PORTABLE_AUDIO_VOICE_BAD_MAGIC;
    if(read_u16(data+8u)!=PORTABLE_AUDIO_VOICE_BANK_HEADER_SIZE||
       read_u16(data+10u)!=1u)return PORTABLE_AUDIO_VOICE_BAD_VERSION;
    candidate.data=data;candidate.size=size;candidate.bank_id=read_u16(data+12u);
    candidate.voice_count=read_u16(data+14u);
    candidate.mapping_count=read_u32(data+16u);
    candidate.voices_offset=read_u32(data+20u);
    candidate.mappings_offset=read_u32(data+24u);
    if(candidate.voice_count==0u||
       candidate.voices_offset!=PORTABLE_AUDIO_VOICE_BANK_HEADER_SIZE||
       candidate.mappings_offset!=candidate.voices_offset+
            (size_t)candidate.voice_count*PORTABLE_AUDIO_VOICE_RECORD_SIZE||
       read_u32(data+28u)!=size||read_u32(data+32u)!=0u||
       read_u32(data+36u)!=0u||candidate.mappings_offset>size||
       candidate.mapping_count>(size-candidate.mappings_offset)/
            PORTABLE_AUDIO_VOICE_MAPPING_SIZE||
       candidate.mappings_offset+(size_t)candidate.mapping_count*
            PORTABLE_AUDIO_VOICE_MAPPING_SIZE!=size)
        return PORTABLE_AUDIO_VOICE_BAD_SIZE;
    for(voice_index=0u;voice_index<candidate.voice_count;++voice_index){
        PortableAudioVoice voice;
        PortableAudioVoiceStatus status=portable_audio_voice_bank_voice(
            &candidate,voice_index,&voice);
        uint32_t mapping_index,end;
        uint8_t previous_end=0u;
        int have_previous=0;
        if(status!=PORTABLE_AUDIO_VOICE_OK)return status;
        if(voice.key>127u)return PORTABLE_AUDIO_VOICE_BAD_SOURCE;
        if(direct_source(voice.source_kind)){
            if(voice.mapping_first!=0u||voice.mapping_count!=0u)
                return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
            if((voice.source_kind==PORTABLE_AUDIO_VOICE_PSG_SQUARE&&
                    voice.source_id>3u)||
               (voice.source_kind==PORTABLE_AUDIO_VOICE_PSG_NOISE&&
                    voice.source_id>1u)||
               (voice.source_kind==PORTABLE_AUDIO_VOICE_PSG_WAVE&&
                    voice.source_id<0x8000u))
                return PORTABLE_AUDIO_VOICE_BAD_SOURCE;
            continue;
        }
        if(voice.source_kind!=PORTABLE_AUDIO_VOICE_KEY_SPLIT&&
           voice.source_kind!=PORTABLE_AUDIO_VOICE_DRUM)
            return PORTABLE_AUDIO_VOICE_BAD_SOURCE;
        end=(uint32_t)voice.mapping_first+voice.mapping_count;
        if(voice.source_id!=0u||voice.mapping_count==0u||
           end>candidate.mapping_count)
            return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
        for(mapping_index=voice.mapping_first;mapping_index<end;++mapping_index){
            PortableAudioVoiceMapping mapping;
            PortableAudioVoice target;
            status=portable_audio_voice_bank_mapping(&candidate,mapping_index,
                                                     &mapping);
            if(status!=PORTABLE_AUDIO_VOICE_OK)return status;
            if(mapping.key_start>mapping.key_end||mapping.key_end>127u||
               mapping.tone_key>127u||mapping.voice_index>=candidate.voice_count||
               (have_previous&&mapping.key_start<=previous_end))
                return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
            if(voice.source_kind==PORTABLE_AUDIO_VOICE_DRUM&&
               mapping.key_start!=mapping.key_end)
                return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
            status=portable_audio_voice_bank_voice(&candidate,mapping.voice_index,
                                                   &target);
            if(status!=PORTABLE_AUDIO_VOICE_OK||!direct_source(target.source_kind))
                return PORTABLE_AUDIO_VOICE_BAD_MAPPING;
            previous_end=mapping.key_end;have_previous=1;
        }
    }
    *view=candidate;return PORTABLE_AUDIO_VOICE_OK;
}

const char *portable_audio_voice_status_name(PortableAudioVoiceStatus status)
{
    static const char *const names[]={"ok","invalid argument","bad magic",
        "bad version","bad size","bad source","bad mapping"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:
        "unknown status";
}
