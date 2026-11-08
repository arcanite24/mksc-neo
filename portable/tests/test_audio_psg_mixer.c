#include "audio_tuning_fixture.h"
#include "audio_psg_mixer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"audio PSG mixer failed line %d: %s\n", \
                __LINE__,#condition); \
        return 1; \
    } \
} while (0)

static uint32_t checksum(const int16_t *samples,size_t count)
{
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    for (index = 0u;index < count;++index) {
        uint16_t value = (uint16_t)samples[index];
        hash = (hash ^ (uint8_t)value) * UINT32_C(16777619);
        hash = (hash ^ (uint8_t)(value >> 8)) * UINT32_C(16777619);
    }
    return hash;
}

/* Check AC energy, so a large DC offset cannot pass as an audible tone. */
static int audible_ac(const int16_t *samples, int32_t minimum_rms)
{
    int64_t sum=0, squares=0;
    const int64_t count=PORTABLE_AUDIO_PCM_FRAME_SAMPLES;
    for (size_t i=0u;i<PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++i) {
        const int32_t sample=samples[i*2u];
        sum+=sample;
        squares+=(int64_t)sample*sample;
    }
    return squares*count-sum*sum >
        count*count*minimum_rms*minimum_rms;
}

static int square_samples(const int16_t *samples, int16_t amplitude)
{
    int positive=0,negative=0;
    for (size_t i=0u;i<PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++i) {
        const int16_t value=samples[i*2u];
        if (value==amplitude)positive=1;
        else if (value==-amplitude)negative=1;
        else return 0;
        if(samples[i*2u+1u]!=value)return 0;
    }
    return positive&&negative;
}

static PortableAudioVoice psg_voice(uint8_t kind,uint8_t channel,
                                    uint32_t source)
{
    PortableAudioVoice voice;
    memset(&voice,0,sizeof voice);
    voice.original_type = channel;
    voice.key = 36u;
    voice.source_kind = kind;
    voice.source_id = source;
    voice.attack = 0u;
    voice.decay = 0u;
    voice.sustain = 15u;
    voice.release = 1u;
    return voice;
}

static int test_square_pitch_render_pause_and_release(void)
{
    PortableAudioPsgMixer mixer;
    PortableAudioVoice voice = psg_voice(PORTABLE_AUDIO_VOICE_PSG_SQUARE,
                                         1u,2u);
    PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    uint64_t phase;
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start(&mixer,&voice,NULL,60u,127u,127u,
          64u,4u,&handle) == PORTABLE_AUDIO_MIXER_OK);
    CHECK(handle.slot == 0u);
    CHECK(mixer.voices[0].step_q32 == UINT64_C(106892370));
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(square_samples(output,5760));
    CHECK(audible_ac(output,5000));
    phase = mixer.voices[0].phase_q32;
    portable_audio_psg_mixer_pause(&mixer,1);
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[0].phase_q32 == phase);
    CHECK(checksum(output,sizeof output / sizeof output[0]) ==
          UINT32_C(0x57f36cc5));
    portable_audio_psg_mixer_pause(&mixer,0);
    CHECK(portable_audio_psg_mixer_release(&mixer,handle) ==
          PORTABLE_AUDIO_MIXER_OK);
    for (unsigned frame = 0u;frame < 32u && mixer.voices[0].active;++frame)
        portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(!mixer.voices[0].active);
    CHECK(portable_audio_psg_mixer_release(&mixer,handle) ==
          PORTABLE_AUDIO_MIXER_STALE_HANDLE);
    return 0;
}

static int test_cgb_fix_flag_and_priority(void)
{
    PortableAudioPsgMixer mixer;
    PortableAudioVoice voice = psg_voice(PORTABLE_AUDIO_VOICE_PSG_SQUARE,
                                         9u,0u);
    PortableAudioVoiceHandle first;
    PortableAudioVoiceHandle second;
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start(&mixer,&voice,NULL,80u,127u,127u,
          64u,3u,&first) == PORTABLE_AUDIO_MIXER_OK);
    CHECK(mixer.voices[0].step_q32 == UINT64_C(338943530));
    voice.source_id = 1u;
    CHECK(portable_audio_psg_mixer_update(&mixer,first,&voice,80u,0u,127u,
          127u,64u,0,0) == PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE);
    voice.source_id = 0u;
    CHECK(portable_audio_psg_mixer_start(&mixer,&voice,NULL,80u,127u,127u,
          64u,2u,&second) == PORTABLE_AUDIO_MIXER_NO_CHANNEL);
    CHECK(portable_audio_psg_mixer_start(&mixer,&voice,NULL,80u,127u,127u,
          64u,3u,&second) == PORTABLE_AUDIO_MIXER_OK);
    CHECK(second.slot == first.slot && second.generation != first.generation);
    CHECK(portable_audio_psg_mixer_stop(&mixer,first) ==
          PORTABLE_AUDIO_MIXER_STALE_HANDLE);
    CHECK(portable_audio_psg_mixer_stop(&mixer,second) ==
          PORTABLE_AUDIO_MIXER_OK);
    return 0;
}

static int test_source_owner_order(void)
{
    PortableAudioPsgMixer mixer;
    PortableAudioVoice voice=psg_voice(PORTABLE_AUDIO_VOICE_PSG_SQUARE,1u,0u);
    PortableAudioVoiceHandle old,next;
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start_owned_control(&mixer,&voice,NULL,
        60u,0u,127u,127u,64u,64u,3u,20u,&old)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_psg_mixer_start_owned_control(&mixer,&voice,NULL,
        60u,0u,127u,127u,64u,64u,3u,21u,&next)==PORTABLE_AUDIO_MIXER_NO_CHANNEL);
    CHECK(portable_audio_psg_mixer_start_owned_control(&mixer,&voice,NULL,
        60u,0u,127u,127u,64u,64u,3u,19u,&next)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_psg_mixer_stop(&mixer,old)==PORTABLE_AUDIO_MIXER_STALE_HANDLE);
    CHECK(portable_audio_psg_mixer_release(&mixer,next)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(portable_audio_psg_mixer_start_owned_control(&mixer,&voice,NULL,
        60u,0u,127u,127u,64u,64u,0u,200u,&next)==PORTABLE_AUDIO_MIXER_OK);
    CHECK(mixer.voices[0].owner_order==200u&&mixer.voices[0].priority==0u);
    return 0;
}

static int test_wave_and_noise(void)
{
    static const uint8_t wave_data[16] = {
        0x01u,0x23u,0x45u,0x67u,0x89u,0xabu,0xcdu,0xefu,
        0xfeu,0xdcu,0xbau,0x98u,0x76u,0x54u,0x32u,0x10u
    };
    PortableAudioPsgMixer mixer;
    PortableAudioVoice wave = psg_voice(PORTABLE_AUDIO_VOICE_PSG_WAVE,
                                        3u,UINT32_C(0x8009));
    PortableAudioVoice noise = psg_voice(PORTABLE_AUDIO_VOICE_PSG_NOISE,
                                         4u,1u);
    PortableAudioSampleView sample;
    PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    memset(&sample,0,sizeof sample);
    sample.data = wave_data;
    sample.data_size = sizeof wave_data;
    sample.sample_count = 32u;
    sample.sample_id = UINT16_C(0x8009);
    sample.encoding = PORTABLE_AUDIO_SAMPLE_PSG_WAVE4;
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start(&mixer,&wave,&sample,60u,127u,127u,
          64u,1u,&handle) == PORTABLE_AUDIO_MIXER_OK);
    CHECK(handle.slot == 2u);
    CHECK(mixer.voices[2].source_id == UINT32_C(0x8009));
    CHECK(mixer.voices[2].step_q32 == UINT64_C(53446185));
    portable_audio_psg_mixer_render_frame(&mixer,output);
    for (size_t i=0u;i<sizeof output/sizeof output[0];++i)
        CHECK(output[i]>=-6144 && output[i]<=5376 && output[i]%768==0);
    CHECK(audible_ac(output,3000));
    sample.sample_id++;
    CHECK(portable_audio_psg_mixer_start(&mixer,&wave,&sample,60u,127u,127u,
          64u,1u,&handle) == PORTABLE_AUDIO_MIXER_SAMPLE_MISMATCH);
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start(&mixer,&noise,NULL,60u,127u,127u,
          64u,1u,&handle) == PORTABLE_AUDIO_MIXER_OK);
    CHECK(handle.slot == 3u);
    /* Authored register0x34: divisor(2*4)<<3 =64 oscillator clocks. */
    CHECK(mixer.voices[3].step_q32 ==
        (UINT64_C(524288)<<32)/(UINT64_C(64)*PORTABLE_AUDIO_PCM_RATE));
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(square_samples(output,5760));
    CHECK(audible_ac(output,3000));
    return 0;
}

static int test_low_range_wave_and_cue_square_ac(void)
{
    /* Synthetic low-range wave: only DAC levels0/3, so nonzero/DC energy
     * alone would miss the former inaudible output. No ROM art is embedded. */
    uint8_t data[16];
    memset(data,0,sizeof data);
    memset(data+8,0x33,8u);
    PortableAudioSampleView sample={0};
    sample.data=data;sample.data_size=sizeof data;sample.sample_count=32u;
    sample.sample_id=0x8001u;sample.encoding=PORTABLE_AUDIO_SAMPLE_PSG_WAVE4;
    PortableAudioVoice wave=psg_voice(PORTABLE_AUDIO_VOICE_PSG_WAVE,3u,0x8001u);
    PortableAudioVoice square=psg_voice(PORTABLE_AUDIO_VOICE_PSG_SQUARE,1u,1u);
    PortableAudioPsgMixer mixer;
    PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start(&mixer,&wave,&sample,57u,127u,
        127u,64u,100u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(audible_ac(output,1000)); /* Old wave was at most96 AC RMS. */
    for(size_t i=0u;i<PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++i)
        CHECK(output[i*2u]==-6144 || output[i*2u]==-3840);
    /* Source CGB3 register levels: mute,1/4,1/2,3/4,full. In particular
     * 75% truncates each unsigned nibble before DC centering. */
    static const uint8_t envelopes[5]={0u,2u,6u,10u,14u};
    static const int16_t lows[5]={0,-1536,-3072,-4608,-6144};
    static const int16_t highs[5]={0,768,2304,3840,5376};
    memset(data+8,0xff,8u);
    for(size_t level=0u;level<5u;++level) {
        portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
        CHECK(portable_audio_psg_mixer_start(&mixer,&wave,&sample,57u,127u,
            127u,64u,100u,&handle)==PORTABLE_AUDIO_MIXER_OK);
        mixer.voices[2].envelope=envelopes[level];
        mixer.voices[2].envelope_goal=envelopes[level];
        mixer.voices[2].sustain_goal=envelopes[level];
        portable_audio_psg_mixer_render_frame(&mixer,output);
        int seen_low=0,seen_high=0;
        for(size_t i=0u;i<PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++i) {
            const int16_t value=output[i*2u];
            CHECK(value==lows[level] || value==highs[level]);
            seen_low|=value==lows[level];seen_high|=value==highs[level];
        }
        CHECK(seen_low&&seen_high);
    }
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    square.decay=2u;square.sustain=0u;
    CHECK(portable_audio_psg_mixer_start(&mixer,&square,NULL,57u,64u,
        127u,64u,100u,&handle)==PORTABLE_AUDIO_MIXER_OK);
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[0].envelope==7u);
    CHECK(square_samples(output,2688));
    CHECK(audible_ac(output,2000)); /* Old square amplitude was59. */
    return 0;
}

static int test_envelope_cadence_and_pan(void)
{
    PortableAudioPsgMixer mixer;
    PortableAudioVoice voice = psg_voice(PORTABLE_AUDIO_VOICE_PSG_SQUARE,
                                         2u,3u);
    PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    voice.attack = 2u;
    CHECK(portable_audio_psg_mixer_start(&mixer,&voice,NULL,60u,127u,127u,
          0u,1u,&handle) == PORTABLE_AUDIO_MIXER_OK);
    CHECK(handle.slot == 1u && mixer.voices[1].envelope_goal == 15u);
    CHECK(mixer.voices[1].left_enabled && !mixer.voices[1].right_enabled);
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[1].envelope == 0u &&
          mixer.voices[1].envelope_counter == 1u);
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[1].envelope == 0u &&
          mixer.voices[1].envelope_counter == 0u);
    portable_audio_psg_mixer_render_frame(&mixer,output);
    CHECK(mixer.voices[1].envelope == 1u &&
          mixer.voices[1].envelope_counter == 2u);
    while (mixer.envelope_frame != 14u)
        portable_audio_psg_mixer_render_frame(&mixer,output);
    {
        uint8_t before = mixer.voices[1].envelope_counter;
        portable_audio_psg_mixer_render_frame(&mixer,output);
        CHECK(mixer.envelope_frame == 0u);
        CHECK(mixer.voices[1].envelope_counter < before ||
              mixer.voices[1].envelope > 1u);
    }
    return 0;
}

static uint8_t *load(const char *path,size_t *size)
{
    uint8_t *bytes;
    long length;
    FILE *file = fopen(path,"rb");
    if (file == NULL || fseek(file,0L,SEEK_END) != 0)
        return NULL;
    length = ftell(file);
    if (length <= 0 || fseek(file,0L,SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL || fread(bytes,1u,(size_t)length,file) !=
        (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static int test_files(const char *bank_path,const char *sample_path)
{
    size_t bank_size;
    size_t sample_size;
    uint8_t *bank_bytes = load(bank_path,&bank_size);
    uint8_t *sample_bytes = load(sample_path,&sample_size);
    PortableAudioVoiceBankView bank;
    PortableAudioSampleView sample;
    PortableAudioVoice voice;
    PortableAudioPsgMixer mixer;
    PortableAudioVoiceHandle handle;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u * 10u];
    uint16_t index;
    CHECK(bank_bytes != NULL && sample_bytes != NULL);
    CHECK(portable_audio_voice_bank_open(&bank,bank_bytes,bank_size) ==
          PORTABLE_AUDIO_VOICE_OK);
    CHECK(portable_audio_sample_open(&sample,sample_bytes,sample_size) ==
          PORTABLE_AUDIO_SAMPLE_OK);
    CHECK(sample.encoding == PORTABLE_AUDIO_SAMPLE_PSG_WAVE4);
    for (index = 0u;index < bank.voice_count;++index) {
        CHECK(portable_audio_voice_bank_voice(&bank,index,&voice) ==
              PORTABLE_AUDIO_VOICE_OK);
        if (voice.source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE &&
            voice.source_id == sample.sample_id)
            break;
    }
    CHECK(index < bank.voice_count);
    portable_audio_psg_mixer_init(&mixer);mixer.tuning=audio_test_tuning();
    CHECK(portable_audio_psg_mixer_start(&mixer,&voice,&sample,60u,127u,127u,
          64u,0u,&handle) == PORTABLE_AUDIO_MIXER_OK);
    for (unsigned frame = 0u;frame < 10u;++frame)
        portable_audio_psg_mixer_render_frame(&mixer,output +
            frame * PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u);
    printf("portable audio PSG file: bank=%u voice=%u sample=%u "
           "checksum=%08x\n",bank.bank_id,index,sample.sample_id,
           checksum(output,sizeof output / sizeof output[0]));
    free(sample_bytes);
    free(bank_bytes);
    return 0;
}

int main(int argc,char **argv)
{
    if (argc == 3)
        return test_files(argv[1],argv[2]);
    CHECK(argc == 1);
    CHECK(test_square_pitch_render_pause_and_release() == 0);
    CHECK(test_cgb_fix_flag_and_priority() == 0);
    CHECK(test_source_owner_order() == 0);
    CHECK(test_wave_and_noise() == 0);
    CHECK(test_low_range_wave_and_cue_square_ac() == 0);
    CHECK(test_envelope_cadence_and_pan() == 0);
    puts("portable audio PSG mixer: PASS");
    return 0;
}
