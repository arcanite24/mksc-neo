#include "audio_sample_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"audio sample asset failed line %d: %s\n", \
                __LINE__,#condition); \
        return 1; \
    } \
} while (0)

enum { PCM_COUNT=8, PCM_SIZE=40+PCM_COUNT, PSG_SIZE=40+16 };

static void put16(uint8_t *out,uint16_t value)
{out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *out,uint32_t value)
{out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8);out[2]=(uint8_t)(value>>16);out[3]=(uint8_t)(value>>24);}

static void header(uint8_t *asset,size_t size,uint8_t encoding,uint8_t flags,
                   uint32_t rate,uint32_t count,uint32_t loop)
{
    memset(asset,0,size);memcpy(asset,"MKSCSMP1",8u);
    put16(asset+8u,40u);put16(asset+10u,1u);put16(asset+12u,133u);
    asset[14]=encoding;asset[15]=flags;put32(asset+16u,rate);
    put32(asset+20u,count);put32(asset+24u,loop);put32(asset+28u,40u);
    put32(asset+32u,(uint32_t)size);
}

static int test_valid(void)
{
    uint8_t pcm[PCM_SIZE],psg[PSG_SIZE];
    PortableAudioSampleView view;
    header(pcm,sizeof pcm,PORTABLE_AUDIO_SAMPLE_PCM_S8,
           PORTABLE_AUDIO_SAMPLE_LOOP,10512u<<10,PCM_COUNT,3u);
    pcm[40]=0x80u;pcm[47]=0x7fu;
    CHECK(portable_audio_sample_open(&view,pcm,sizeof pcm)==
          PORTABLE_AUDIO_SAMPLE_OK);
    CHECK(view.sample_id==133u&&view.sample_count==PCM_COUNT&&
          view.data_size==PCM_COUNT&&view.data[0]==0x80u);
    header(psg,sizeof psg,PORTABLE_AUDIO_SAMPLE_PSG_WAVE4,0u,0u,32u,0u);
    CHECK(portable_audio_sample_open(&view,psg,sizeof psg)==
          PORTABLE_AUDIO_SAMPLE_OK);
    CHECK(view.data_size==16u&&view.encoding==PORTABLE_AUDIO_SAMPLE_PSG_WAVE4);
    return 0;
}

static int test_rejections(void)
{
    uint8_t asset[PCM_SIZE];
    PortableAudioSampleView view;
    header(asset,sizeof asset,PORTABLE_AUDIO_SAMPLE_PCM_S8,0u,10512u<<10,
           PCM_COUNT,0u);
    asset[0]='X';
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_MAGIC);
    header(asset,sizeof asset,3u,0u,10512u<<10,PCM_COUNT,0u);
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_ENCODING);
    header(asset,sizeof asset,PORTABLE_AUDIO_SAMPLE_PCM_S8,2u,10512u<<10,
           PCM_COUNT,0u);
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_FLAGS);
    header(asset,sizeof asset,PORTABLE_AUDIO_SAMPLE_PCM_S8,0u,0u,PCM_COUNT,0u);
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_RATE);
    header(asset,sizeof asset,PORTABLE_AUDIO_SAMPLE_PCM_S8,
           PORTABLE_AUDIO_SAMPLE_LOOP,10512u<<10,PCM_COUNT,PCM_COUNT);
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_LOOP);
    header(asset,sizeof asset,PORTABLE_AUDIO_SAMPLE_PCM_S8,0u,10512u<<10,
           PCM_COUNT,1u);
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_LOOP);
    header(asset,sizeof asset,PORTABLE_AUDIO_SAMPLE_PCM_S8,0u,10512u<<10,
           PCM_COUNT+1u,0u);
    CHECK(portable_audio_sample_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SAMPLE_BAD_SIZE);
    return 0;
}

static int test_file(const char *path)
{
    PortableAudioSampleView view;
    uint8_t *bytes;long size;FILE *file=fopen(path,"rb");
    CHECK(file!=NULL&&fseek(file,0L,SEEK_END)==0);
    size=ftell(file);CHECK(size>0&&fseek(file,0L,SEEK_SET)==0);
    bytes=(uint8_t *)malloc((size_t)size);
    CHECK(bytes!=NULL&&fread(bytes,1u,(size_t)size,file)==(size_t)size);
    CHECK(fclose(file)==0);
    CHECK(portable_audio_sample_open(&view,bytes,(size_t)size)==
          PORTABLE_AUDIO_SAMPLE_OK);
    printf("portable audio sample file: id=%u encoding=%u samples=%u\n",
           view.sample_id,view.encoding,view.sample_count);
    free(bytes);return 0;
}

int main(int argc,char **argv)
{
    if(argc==2)return test_file(argv[1]);
    CHECK(argc==1);
    CHECK(test_valid()==0);CHECK(test_rejections()==0);
    puts("portable audio sample asset: PASS");return 0;
}
