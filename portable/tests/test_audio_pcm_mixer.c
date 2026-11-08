#include "audio_tuning_fixture.h"
#include "audio_pcm_mixer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if(!(condition)){fprintf(stderr,"audio PCM mixer failed line %d: %s\n", \
        __LINE__,#condition);return 1;} \
} while(0)

static int8_t sample_data[512];

static PortableAudioSampleView sample_view(uint8_t looping)
{
    PortableAudioSampleView sample;
    memset(&sample,0,sizeof sample);sample.data=(const uint8_t *)sample_data;
    sample.data_size=sizeof sample_data;sample.sample_id=1u;
    sample.encoding=PORTABLE_AUDIO_SAMPLE_PCM_S8;
    sample.flags=looping?PORTABLE_AUDIO_SAMPLE_LOOP:0u;
    sample.frequency_q10=10512u<<10;sample.sample_count=sizeof sample_data;
    sample.loop_start=looping?1u:0u;return sample;
}

static PortableAudioVoice voice(uint8_t fixed)
{
    PortableAudioVoice voice;memset(&voice,0,sizeof voice);
    voice.original_type=fixed?8u:0u;voice.source_kind=PORTABLE_AUDIO_VOICE_PCM;
    voice.source_id=1u;voice.attack=255u;voice.decay=255u;
    voice.sustain=255u;voice.release=128u;return voice;
}

static int test_imported_tuning(void)
{
    uint8_t bytes[PORTABLE_AUDIO_TUNING_BYTES];
    PortableAudioTuning tuning;
    audio_test_tuning_bytes(bytes);
    CHECK(portable_audio_tuning_open(&tuning,bytes,sizeof bytes));
    CHECK(tuning.pcm_frequency[1]==audio_test_tuning()->pcm_frequency[1]);
    CHECK(tuning.psg_frequency[9]==audio_test_tuning()->psg_frequency[9]);
    CHECK(tuning.noise[39]==0x34u);
    CHECK(!portable_audio_tuning_open(&tuning,bytes,sizeof bytes-1u));
    bytes[72]=8u;CHECK(!portable_audio_tuning_open(&tuning,bytes,sizeof bytes));
    audio_test_tuning_bytes(bytes);memset(bytes,0,4u);
    CHECK(!portable_audio_tuning_open(&tuning,bytes,sizeof bytes));
    audio_test_tuning_bytes(bytes);bytes[48]=0;bytes[49]=0;
    CHECK(!portable_audio_tuning_open(&tuning,bytes,sizeof bytes));
    PortableAudioPcmMixer mixer;PortableAudioVoice normal=voice(0u);
    PortableAudioSampleView sample=sample_view(1u);PortableAudioVoiceHandle handle;
    portable_audio_pcm_mixer_init(&mixer);
    CHECK(portable_audio_pcm_mixer_start(&mixer,&normal,&sample,60u,127u,127u,
        64u,1u,&handle)==PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE);
    return 0;
}

static int test_fixed_render_pause_and_release(void)
{
    PortableAudioPcmMixer mixer;PortableAudioSampleView sample=sample_view(1u);
    PortableAudioVoice tone=voice(1u);PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];uint32_t position;
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_pcm_mixer_start(&mixer,&tone,&sample,60u,127u,127u,
          64u,4u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    portable_audio_pcm_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[handle.slot].position==176u);
    CHECK(portable_audio_pcm_checksum(output,sizeof output/sizeof output[0])==
          UINT32_C(0xedb955c6));
    position=mixer.voices[handle.slot].position;
    portable_audio_pcm_mixer_pause(&mixer,1);
    portable_audio_pcm_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[handle.slot].position==position);
    CHECK(portable_audio_pcm_checksum(output,sizeof output/sizeof output[0])==
          UINT32_C(0x57f36cc5));
    portable_audio_pcm_mixer_pause(&mixer,0);
    CHECK(portable_audio_pcm_mixer_release(&mixer,handle)==PORTABLE_AUDIO_MIXER_OK);
    for(unsigned frame=0u;frame<9u;++frame)
        portable_audio_pcm_mixer_render_frame(&mixer,output);
    CHECK(!mixer.voices[handle.slot].active);
    return 0;
}

static int test_source_phase_and_loop(void)
{
    PortableAudioPcmMixer mixer;PortableAudioVoice normal=voice(0u),fixed=voice(1u);
    PortableAudioSampleView sample=sample_view(1u);PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_pcm_mixer_start(&mixer,&normal,&sample,60u,127u,127u,
          64u,1u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(mixer.voices[handle.slot].step_q23==UINT32_C(8388576));
    CHECK(portable_audio_pcm_mixer_update(&mixer,handle,&normal,60u,128u,127u,
          127u,64u,0,0)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(mixer.voices[handle.slot].step_q23==UINT32_C(8637552));
    normal.source_id=2u;
    CHECK(portable_audio_pcm_mixer_update(&mixer,handle,&normal,60u,0u,127u,
          127u,64u,0,0)==PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE);
    normal.source_id=1u;
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    sample.frequency_q10=(50003u<<10)+800u;
    CHECK(portable_audio_pcm_mixer_start(&mixer,&normal,&sample,69u,127u,127u,
          64u,1u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(mixer.voices[handle.slot].step_q23==UINT32_C(67107810));
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();sample.sample_count=4u;
    sample.frequency_q10=10512u<<10;
    sample.data_size=4u;
    CHECK(portable_audio_pcm_mixer_start(&mixer,&fixed,&sample,60u,127u,127u,
          64u,1u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    portable_audio_pcm_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[handle.slot].active&&mixer.voices[handle.slot].position==2u);
    return 0;
}

static int test_priority_and_handles(void)
{
    PortableAudioPcmMixer mixer;PortableAudioVoice tone=voice(1u);
    PortableAudioSampleView sample=sample_view(1u);PortableAudioVoiceHandle first={0},last;
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    for(unsigned index=0u;index<PORTABLE_AUDIO_PCM_VOICES;++index){
        PortableAudioVoiceHandle handle;
        CHECK(portable_audio_pcm_mixer_start(&mixer,&tone,&sample,60u,127u,
              127u,64u,2u,&handle)==PORTABLE_AUDIO_MIXER_OK);
        if(index==PORTABLE_AUDIO_PCM_VOICES-1u)first=handle; /* equal owner: last slot */
    }
    CHECK(portable_audio_pcm_mixer_start(&mixer,&tone,&sample,60u,127u,127u,
          64u,1u,&last)==PORTABLE_AUDIO_MIXER_NO_CHANNEL);
    CHECK(portable_audio_pcm_mixer_start(&mixer,&tone,&sample,60u,127u,127u,
          64u,3u,&last)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(last.slot==first.slot&&last.generation!=first.generation);
    CHECK(portable_audio_pcm_mixer_release(&mixer,first)==
          PORTABLE_AUDIO_MIXER_STALE_HANDLE);
    CHECK(portable_audio_pcm_mixer_release(&mixer,last)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_pcm_mixer_stop(&mixer,last)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_pcm_mixer_stop(&mixer,last)==
          PORTABLE_AUDIO_MIXER_STALE_HANDLE);
    return 0;
}

static int test_source_owner_order(void)
{
    PortableAudioPcmMixer mixer;PortableAudioVoice tone=voice(1u);
    PortableAudioSampleView sample=sample_view(1u);
    PortableAudioVoiceHandle handles[3],next;
    const uint16_t owners[3]={10u,30u,20u};
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();mixer.voice_count=3u;
    for(unsigned i=0u;i<3u;++i){
        CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
            60u,0u,127u,127u,64u,64u,2u,owners[i],&handles[i])==PORTABLE_AUDIO_MIXER_OK);
        CHECK(handles[i].slot==i);
    }
    CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
        60u,0u,127u,127u,64u,64u,1u,0u,&next)==PORTABLE_AUDIO_MIXER_NO_CHANNEL);
    CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
        60u,0u,127u,127u,64u,64u,2u,40u,&next)==PORTABLE_AUDIO_MIXER_NO_CHANNEL);
    CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
        60u,0u,127u,127u,64u,64u,2u,25u,&next)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(next.slot==1u); /* equal priority favors the lower-address track */
    CHECK(portable_audio_pcm_mixer_stop(&mixer,handles[1])==PORTABLE_AUDIO_MIXER_STALE_HANDLE);
    CHECK(mixer.voices[1].active&&mixer.voices[1].owner_order==25u);
    mixer.voices[0].priority=255u;
    CHECK(portable_audio_pcm_mixer_release(&mixer,handles[0])==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
        60u,0u,127u,127u,64u,64u,0u,200u,&next)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(next.slot==0u); /* releasing voices precede every active priority */
    portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();mixer.voice_count=3u;
    for(unsigned i=0u;i<3u;++i)
        CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
            60u,0u,127u,127u,64u,64u,2u,10u,&handles[i])==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_pcm_mixer_start_owned_control(&mixer,&tone,&sample,
        60u,0u,127u,127u,64u,64u,2u,10u,&next)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(next.slot==2u); /* equal track chooses the last scanned voice */
    mixer.voice_count=0u;
    CHECK(portable_audio_pcm_mixer_start(&mixer,&tone,&sample,60u,127u,127u,
        64u,2u,&next)==PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT);
    return 0;
}

static uint8_t *load(const char *path,size_t *size)
{
    uint8_t *bytes;long length;FILE *file=fopen(path,"rb");
    if(file==NULL||fseek(file,0L,SEEK_END)!=0)return NULL;
    length=ftell(file);if(length<=0||fseek(file,0L,SEEK_SET)!=0){fclose(file);return NULL;}
    bytes=(uint8_t *)malloc((size_t)length);
    if(bytes==NULL||fread(bytes,1u,(size_t)length,file)!=(size_t)length){
        free(bytes);fclose(file);return NULL;}
    fclose(file);*size=(size_t)length;return bytes;
}

static int test_files(const char *bank_path,const char *sample_path)
{
    size_t bank_size,sample_size;uint8_t *bank_bytes=load(bank_path,&bank_size);
    uint8_t *sample_bytes=load(sample_path,&sample_size);
    PortableAudioVoiceBankView bank;PortableAudioSampleView sample;
    PortableAudioVoice tone;PortableAudioPcmMixer mixer;PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u*10u];uint16_t index;
    CHECK(bank_bytes!=NULL&&sample_bytes!=NULL);
    CHECK(portable_audio_voice_bank_open(&bank,bank_bytes,bank_size)==PORTABLE_AUDIO_VOICE_OK);
    CHECK(portable_audio_sample_open(&sample,sample_bytes,sample_size)==PORTABLE_AUDIO_SAMPLE_OK);
    for(index=0u;index<bank.voice_count;++index){
        CHECK(portable_audio_voice_bank_voice(&bank,index,&tone)==PORTABLE_AUDIO_VOICE_OK);
        if(tone.source_kind==PORTABLE_AUDIO_VOICE_PCM&&tone.source_id==sample.sample_id)break;
    }
    CHECK(index<bank.voice_count);portable_audio_pcm_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_pcm_mixer_start(&mixer,&tone,&sample,60u,127u,127u,
          64u,0u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    for(unsigned frame=0u;frame<10u;++frame)
        portable_audio_pcm_mixer_render_frame(&mixer,output+
            frame*PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u);
    printf("portable audio PCM file: bank=%u voice=%u sample=%u checksum=%08x\n",
           bank.bank_id,index,sample.sample_id,portable_audio_pcm_checksum(output,
           sizeof output/sizeof output[0]));
    free(sample_bytes);free(bank_bytes);return 0;
}

int main(int argc,char **argv)
{
    if(argc==3)return test_files(argv[1],argv[2]);
    CHECK(argc==1);
    for(unsigned index=0u;index<sizeof sample_data;++index)
        sample_data[index]=(int8_t)((index*37u)&0xffu);
    CHECK(test_imported_tuning()==0);
    CHECK(test_fixed_render_pause_and_release()==0);
    CHECK(test_source_phase_and_loop()==0);
    CHECK(test_priority_and_handles()==0);
    CHECK(test_source_owner_order()==0);
    puts("portable audio PCM mixer: PASS");return 0;
}
