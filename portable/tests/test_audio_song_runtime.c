#include "audio_tuning_fixture.h"
#include "audio_song_runtime.h"
#include "audio_pack_song.h"
#include "asset_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"audio song runtime failed line %d: %s\n", \
                __LINE__,#condition); \
        return 1; \
    } \
} while (0)

enum {
    EVENT_COUNT = 14,
    SEQUENCE_SIZE = 32 + EVENT_COUNT * 16,
    VOICE_COUNT = 2,
    BANK_SIZE = 40 + VOICE_COUNT * 20
};

static int8_t pcm_data[512];

static void put16(uint8_t *out,uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *out,uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void event(uint8_t *asset,unsigned index,uint32_t tick,uint16_t value,
                  uint8_t operation,uint8_t arg0,uint8_t arg1)
{
    uint8_t *out = asset + 32u + index * 16u;
    put32(out,tick);
    put16(out + 8u,value);
    out[11] = operation;
    out[12] = arg0;
    out[13] = arg1;
}

static void sequence_asset(uint8_t asset[SEQUENCE_SIZE])
{
    memset(asset,0,SEQUENCE_SIZE);
    memcpy(asset,"MKSCAUD1",8u);
    put16(asset + 8u,32u);
    put16(asset + 10u,1u);
    put16(asset + 12u,25u);
    asset[14] = 1u;
    asset[15] = 4u;
    put16(asset + 18u,25u);
    put32(asset + 20u,EVENT_COUNT);
    put32(asset + 24u,32u);
    put32(asset + 28u,SEQUENCE_SIZE);
    event(asset,0u,0u,75u,PORTABLE_AUDIO_EVENT_TEMPO,0u,0u);
    event(asset,1u,0u,0u,PORTABLE_AUDIO_EVENT_VOICE,0u,0u);
    event(asset,2u,0u,127u,PORTABLE_AUDIO_EVENT_VOLUME,0u,0u);
    event(asset,3u,0u,64u,PORTABLE_AUDIO_EVENT_PAN,0u,0u);
    event(asset,4u,0u,2u,PORTABLE_AUDIO_EVENT_BEND_RANGE,0u,0u);
    event(asset,5u,0u,96u,PORTABLE_AUDIO_EVENT_BEND,0u,0u);
    event(asset,6u,0u,2u,PORTABLE_AUDIO_EVENT_NOTE,60u,127u);
    event(asset,7u,1u,1u,PORTABLE_AUDIO_EVENT_VOICE,0u,0u);
    event(asset,8u,1u,64u,PORTABLE_AUDIO_EVENT_BEND,0u,0u);
    event(asset,9u,1u,2u,PORTABLE_AUDIO_EVENT_NOTE,60u,127u);
    event(asset,10u,1u,64u,PORTABLE_AUDIO_EVENT_LFO_SPEED,0u,0u);
    event(asset,11u,1u,16u,PORTABLE_AUDIO_EVENT_MOD_DEPTH,0u,0u);
    event(asset,12u,1u,80u,PORTABLE_AUDIO_EVENT_VOLUME,0u,0u);
    event(asset,13u,4u,0u,PORTABLE_AUDIO_EVENT_END,0u,0u);
}

static void voice(uint8_t *asset,unsigned index,uint8_t type,uint8_t kind,
                  uint32_t source,uint8_t attack,uint8_t sustain)
{
    uint8_t *out = asset + 40u + index * 20u;
    out[0] = type;
    out[1] = 60u;
    out[4] = attack;
    out[6] = sustain;
    out[8] = kind;
    put32(out + 12u,source);
}

static void bank_asset(uint8_t asset[BANK_SIZE])
{
    memset(asset,0,BANK_SIZE);
    memcpy(asset,"MKSCVOC1",8u);
    put16(asset + 8u,40u);
    put16(asset + 10u,1u);
    put16(asset + 12u,25u);
    put16(asset + 14u,VOICE_COUNT);
    put32(asset + 20u,40u);
    put32(asset + 24u,BANK_SIZE);
    put32(asset + 28u,BANK_SIZE);
    voice(asset,0u,0u,PORTABLE_AUDIO_VOICE_PCM,1u,255u,255u);
    voice(asset,1u,1u,PORTABLE_AUDIO_VOICE_PSG_SQUARE,2u,0u,15u);
}

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

static int test_gate_tempo_pause_and_mix(void)
{
    uint8_t sequence_bytes[SEQUENCE_SIZE];
    uint8_t bank_bytes[BANK_SIZE];
    PortableAudioSequenceView sequence;
    PortableAudioVoiceBankView bank;
    PortableAudioSampleView sample;
    PortableAudioSongRuntime runtime;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u * 6u];
    uint64_t pcm_phase;
    sequence_asset(sequence_bytes);
    bank_asset(bank_bytes);
    CHECK(portable_audio_sequence_open(&sequence,sequence_bytes,
          sizeof sequence_bytes) == PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(portable_audio_voice_bank_open(&bank,bank_bytes,sizeof bank_bytes) ==
          PORTABLE_AUDIO_VOICE_OK);
    memset(&sample,0,sizeof sample);
    sample.data = (const uint8_t *)pcm_data;
    sample.data_size = sizeof pcm_data;
    sample.sample_id = 1u;
    sample.encoding = PORTABLE_AUDIO_SAMPLE_PCM_S8;
    sample.flags = PORTABLE_AUDIO_SAMPLE_LOOP;
    sample.frequency_q10 = 10512u << 10;
    sample.sample_count = sizeof pcm_data;
    sample.loop_start = 1u;
    CHECK(portable_audio_song_runtime_init(&runtime,&sequence,&bank,&sample,1u) ==
          PORTABLE_AUDIO_RUNTIME_OK);
    runtime.pcm.tuning=audio_test_tuning();runtime.psg.tuning=audio_test_tuning();
    CHECK(portable_audio_song_runtime_render_frame(&runtime,output) ==
          PORTABLE_AUDIO_RUNTIME_OK);
    CHECK(runtime.next_tick == 1u && runtime.player.tempo == 150u &&
          runtime.notes_started == 1u);
    CHECK(runtime.pcm.voices[0].step_q23 == UINT32_C(8887326));
    CHECK(checksum(output,PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u) ==
          UINT32_C(0xdf00e665));
    for(unsigned index=0u;index<PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++index)
        CHECK(output[index*2u] == output[index*2u+1u]);
    CHECK(portable_audio_song_runtime_render_frame(&runtime,
          output + PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u) ==
          PORTABLE_AUDIO_RUNTIME_OK);
    CHECK(runtime.next_tick == 2u && runtime.notes_started == 2u);
    CHECK(runtime.tracks[0].mod_value == 16 &&
          runtime.psg.voices[0].step_q32 == UINT64_C(113220037));
    pcm_phase = runtime.pcm.voices[0].phase_q23;
    portable_audio_song_runtime_pause(&runtime,1);
    CHECK(portable_audio_song_runtime_render_frame(&runtime,
          output + PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 4u) ==
          PORTABLE_AUDIO_RUNTIME_OK);
    CHECK(runtime.next_tick == 2u &&
          runtime.pcm.voices[0].phase_q23 == pcm_phase);
    CHECK(checksum(output + PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 4u,
          PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u) == UINT32_C(0x57f36cc5));
    portable_audio_song_runtime_pause(&runtime,0);
    for (unsigned frame = 3u;frame < 6u;++frame)
        CHECK(portable_audio_song_runtime_render_frame(&runtime,output +
              frame * PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u) ==
              PORTABLE_AUDIO_RUNTIME_OK);
    CHECK(runtime.next_tick == 5u && runtime.notes_started == 2u &&
          runtime.notes_released == 2u && runtime.notes_dropped == 0u);
    /* Full NR50/CGB DAC gain; the earlier PCM-only golden stays unchanged. */
    CHECK(checksum(output,sizeof output / sizeof output[0]) ==
          UINT32_C(0xcf37f825));
    return 0;
}

static int test_copied_song_owner(void)
{
    uint8_t sequence_bytes[SEQUENCE_SIZE],bank_bytes[BANK_SIZE];
    PortableAudioPackSong original={0},baseline={0},copy;
    sequence_asset(sequence_bytes);bank_asset(bank_bytes);
    CHECK(portable_audio_sequence_open(&original.sequence,sequence_bytes,sizeof sequence_bytes)==0);
    CHECK(portable_audio_voice_bank_open(&original.bank,bank_bytes,sizeof bank_bytes)==0);
    original.tuning=*audio_test_tuning();original.sample_count=1u;
    original.samples[0]=(PortableAudioSampleView){.data=(const uint8_t *)pcm_data,
        .data_size=sizeof pcm_data,.sample_id=1u,.encoding=PORTABLE_AUDIO_SAMPLE_PCM_S8,
        .flags=PORTABLE_AUDIO_SAMPLE_LOOP,.frequency_q10=10512u<<10,
        .sample_count=sizeof pcm_data,.loop_start=1u};
    CHECK(portable_audio_song_runtime_init(&original.runtime,&original.sequence,
        &original.bank,original.samples,1u)==0);
    original.status=PORTABLE_AUDIO_PACK_SONG_OK;
    portable_audio_pack_song_rebind(&original);
    baseline=original;
    portable_audio_pack_song_rebind(&baseline);
    int16_t expected[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u],actual[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    CHECK(portable_audio_pack_song_render_frame(&original,actual)==0);
    CHECK(portable_audio_pack_song_render_frame(&baseline,expected)==0);
    copy=original;
    memset(&original,0,sizeof original); /* Original storage is no longer an owner. */
    CHECK(portable_audio_pack_song_control(&copy,1u,192u,128)==0);
    CHECK(portable_audio_pack_song_control(&baseline,1u,192u,128)==0);
    CHECK(copy.runtime.pcm.tuning==&copy.tuning&&copy.runtime.psg.tuning==&copy.tuning);
    for(unsigned frame=0;frame<16u;++frame){
        CHECK(portable_audio_pack_song_render_frame(&copy,actual)==0);
        CHECK(portable_audio_pack_song_render_frame(&baseline,expected)==0);
        CHECK(memcmp(actual,expected,sizeof actual)==0);
    }
    CHECK(copy.runtime.notes_started==baseline.runtime.notes_started&&copy.runtime.notes_started==2u);
    return 0;
}

static int test_missing_sample_is_sticky(void)
{
    uint8_t sequence_bytes[SEQUENCE_SIZE];
    uint8_t bank_bytes[BANK_SIZE];
    PortableAudioSequenceView sequence;
    PortableAudioVoiceBankView bank;
    PortableAudioSongRuntime runtime;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    sequence_asset(sequence_bytes);
    bank_asset(bank_bytes);
    CHECK(portable_audio_sequence_open(&sequence,sequence_bytes,
          sizeof sequence_bytes) == PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(portable_audio_voice_bank_open(&bank,bank_bytes,sizeof bank_bytes) ==
          PORTABLE_AUDIO_VOICE_OK);
    CHECK(portable_audio_song_runtime_init(&runtime,&sequence,&bank,NULL,0u) ==
          PORTABLE_AUDIO_RUNTIME_OK);
    runtime.pcm.tuning=audio_test_tuning();runtime.psg.tuning=audio_test_tuning();
    CHECK(portable_audio_song_runtime_render_frame(&runtime,output) ==
          PORTABLE_AUDIO_RUNTIME_MISSING_SAMPLE);
    CHECK(portable_audio_song_runtime_render_frame(&runtime,output) ==
          PORTABLE_AUDIO_RUNTIME_MISSING_SAMPLE);
    return 0;
}

static int test_external_track_control(void)
{
    uint8_t sequence_bytes[SEQUENCE_SIZE];
    uint8_t bank_bytes[BANK_SIZE];
    PortableAudioSequenceView sequence;
    PortableAudioVoiceBankView bank;
    PortableAudioSampleView sample;
    PortableAudioSongRuntime runtime;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    uint32_t original_step;
    uint16_t original_gain;
    sequence_asset(sequence_bytes);
    bank_asset(bank_bytes);
    CHECK(portable_audio_sequence_open(&sequence,sequence_bytes,
          sizeof sequence_bytes)==PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(portable_audio_voice_bank_open(&bank,bank_bytes,sizeof bank_bytes)==
          PORTABLE_AUDIO_VOICE_OK);
    memset(&sample,0,sizeof sample);
    sample.data=(const uint8_t *)pcm_data;
    sample.data_size=sizeof pcm_data;
    sample.sample_id=1u;
    sample.encoding=PORTABLE_AUDIO_SAMPLE_PCM_S8;
    sample.flags=PORTABLE_AUDIO_SAMPLE_LOOP;
    sample.frequency_q10=10512u<<10;
    sample.sample_count=sizeof pcm_data;
    sample.loop_start=1u;
    CHECK(portable_audio_song_runtime_init(&runtime,&sequence,&bank,&sample,1u)==
          PORTABLE_AUDIO_RUNTIME_OK);
    runtime.pcm.tuning=audio_test_tuning();runtime.psg.tuning=audio_test_tuning();
    CHECK(portable_audio_song_runtime_render_frame(&runtime,output)==
          PORTABLE_AUDIO_RUNTIME_OK);
    original_step=runtime.pcm.voices[0].step_q23;
    original_gain=runtime.pcm.voices[0].left_gain;
    CHECK(portable_audio_song_runtime_control(&runtime,1u,0x100u,0x100)==
          PORTABLE_AUDIO_RUNTIME_OK);
    CHECK(runtime.tracks[0].volume_x==64u&&runtime.tracks[0].pitch_x==0x100);
    CHECK(runtime.pcm.voices[0].step_q23>original_step);
    CHECK(portable_audio_song_runtime_control(&runtime,1u,0x80u,0x100)==
          PORTABLE_AUDIO_RUNTIME_OK);
    CHECK(runtime.tracks[0].volume_x==32u&&
          runtime.pcm.voices[0].left_gain<original_gain);
    /* Tempo controls the event clock, while sample pitch remains independent.
     * Fractional Q8 speed truncates exactly like mPlayTempoControl. */
    CHECK(portable_audio_song_runtime_init(&runtime,&sequence,&bank,&sample,1u)==0);
    runtime.pcm.tuning=audio_test_tuning();runtime.psg.tuning=audio_test_tuning();
    CHECK(runtime.tempo_scale==256u && portable_audio_song_runtime_render_frame(&runtime,output)==0);
    const PortableAudioSongRuntime initial=runtime;
    original_step=runtime.pcm.voices[0].step_q23;runtime.tempo_scale=0u;
    CHECK(portable_audio_song_runtime_render_frame(&runtime,output)==0 && runtime.next_tick==1u &&
        runtime.pcm.voices[0].step_q23==original_step);
    runtime.tempo_scale=512u;
    CHECK(portable_audio_song_runtime_render_frame(&runtime,output)==0 && runtime.next_tick==3u);
    runtime=initial;runtime.tempo_scale=320u;
    for(unsigned frame=0u;frame<4u;++frame)CHECK(portable_audio_song_runtime_render_frame(&runtime,output)==0);
    CHECK(runtime.next_tick==5u && runtime.tempo_accumulator==148u);
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

static int render_pack(const PortableAssetPackView *pack,uint16_t song_id,
                       uint32_t *hash,PortableAudioSongRuntime *runtime)
{
    PortableAudioPackSong song;
    PortableAudioPackSongStatus open_status;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    uint32_t value = UINT32_C(2166136261);
    open_status=portable_audio_pack_song_init(&song,pack,song_id);
    if(open_status!=PORTABLE_AUDIO_PACK_SONG_OK)
        fprintf(stderr,"song %u open failed: %s\n",song_id,
                portable_audio_pack_song_status_name(open_status));
    CHECK(open_status==PORTABLE_AUDIO_PACK_SONG_OK);
    for (unsigned frame = 0u;frame < 600u;++frame) {
        CHECK(portable_audio_pack_song_render_frame(&song,output) ==
              PORTABLE_AUDIO_PACK_SONG_OK);
        for (unsigned index = 0u;index < sizeof output / sizeof output[0];
             ++index) {
            uint16_t sample = (uint16_t)output[index];
            value = (value ^ (uint8_t)sample) * UINT32_C(16777619);
            value = (value ^ (uint8_t)(sample >> 8)) * UINT32_C(16777619);
        }
    }
    *runtime=song.runtime;
    *hash = value;
    return 0;
}

static int test_file(const char *path,const char *song_text)
{
    char *end;
    unsigned long parsed = strtoul(song_text,&end,0);
    size_t size;
    uint8_t *bytes;
    PortableAssetPackView pack;
    PortableAudioSongRuntime first;
    PortableAudioSongRuntime second;
    uint32_t first_hash;
    uint32_t second_hash;
    CHECK(*song_text != '\0' && *end == '\0' && parsed <= UINT16_MAX);
    bytes = load(path,&size);
    CHECK(bytes != NULL);
    CHECK(portable_asset_pack_open(bytes,size,&pack) == PORTABLE_ASSET_PACK_OK);
    CHECK(render_pack(&pack,(uint16_t)parsed,&first_hash,&first) == 0);
    CHECK(render_pack(&pack,(uint16_t)parsed,&second_hash,&second) == 0);
    CHECK(first_hash == second_hash && first.notes_started == second.notes_started &&
          first.notes_released == second.notes_released &&
          first.notes_dropped == second.notes_dropped);
    printf("portable audio song file: song=%lu checksum=%08x notes=%u "
           "released=%u dropped=%u tick=%u\n",parsed,first_hash,
           first.notes_started,first.notes_released,first.notes_dropped,
           first.next_tick);
    free(bytes);
    return 0;
}

static int test_all_songs(const char *path)
{
    size_t size;
    uint8_t *bytes = load(path,&size);
    PortableAssetPackView pack;
    unsigned songs = 0u;
    CHECK(bytes != NULL);
    CHECK(portable_asset_pack_open(bytes,size,&pack) == PORTABLE_ASSET_PACK_OK);
    for (uint32_t index = 0u;index < pack.section_count;++index) {
        PortableAssetSectionView section;
        PortableAudioSongRuntime first,second;
        uint32_t first_hash,second_hash;
        CHECK(portable_asset_pack_section_at(&pack,index,&section) ==
              PORTABLE_ASSET_PACK_OK);
        if (section.kind != PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE) continue;
        CHECK(section.content_id <= UINT16_MAX);
        CHECK(render_pack(&pack,(uint16_t)section.content_id,&first_hash,&first) == 0);
        CHECK(render_pack(&pack,(uint16_t)section.content_id,&second_hash,&second) == 0);
        CHECK(first_hash == second_hash && first.notes_started == second.notes_started &&
              first.notes_released == second.notes_released &&
              first.notes_dropped == second.notes_dropped);
        ++songs;
    }
    CHECK(songs != 0u);
    printf("portable audio catalog: PASS songs=%u frames=600 repeats=2\n",songs);
    free(bytes);
    return 0;
}

int main(int argc,char **argv)
{
    if (argc == 3 && strcmp(argv[2],"--all") == 0)
        return test_all_songs(argv[1]);
    if (argc == 3)
        return test_file(argv[1],argv[2]);
    CHECK(argc == 1);
    for (unsigned index = 0u;index < sizeof pcm_data;++index)
        pcm_data[index] = (int8_t)((index * 37u) & 0xffu);
    CHECK(test_gate_tempo_pause_and_mix() == 0);
    CHECK(test_external_track_control() == 0);
    CHECK(test_missing_sample_is_sticky() == 0);
    CHECK(test_copied_song_owner() == 0);
    puts("portable audio song runtime: PASS");
    return 0;
}
