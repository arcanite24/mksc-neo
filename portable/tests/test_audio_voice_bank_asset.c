#include "audio_voice_bank_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if(!(condition)){fprintf(stderr,"audio voice bank failed line %d: %s\n", \
        __LINE__,#condition);return 1;} \
} while(0)

enum{VOICE_COUNT=3,MAPPING_COUNT=2,ASSET_SIZE=40+VOICE_COUNT*20+MAPPING_COUNT*8};
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void voice(uint8_t *a,unsigned i,uint8_t type,uint8_t kind,uint32_t source,
                  uint16_t first,uint16_t count)
{
    uint8_t *p=a+40u+i*20u;p[0]=type;p[1]=60u;p[4]=255u;p[6]=255u;
    p[8]=kind;put32(p+12u,source);put16(p+16u,first);put16(p+18u,count);
}
static void mapping(uint8_t *a,unsigned i,uint8_t start,uint8_t end,
                    uint16_t target)
{
    uint8_t *p=a+40u+VOICE_COUNT*20u+i*8u;p[0]=start;p[1]=end;
    put16(p+2u,target);p[4]=60u;p[5]=64u;
}
static void valid(uint8_t a[ASSET_SIZE])
{
    memset(a,0,ASSET_SIZE);memcpy(a,"MKSCVOC1",8u);put16(a+8u,40u);
    put16(a+10u,1u);put16(a+12u,25u);put16(a+14u,VOICE_COUNT);
    put32(a+16u,MAPPING_COUNT);put32(a+20u,40u);
    put32(a+24u,40u+VOICE_COUNT*20u);put32(a+28u,ASSET_SIZE);
    voice(a,0u,0u,PORTABLE_AUDIO_VOICE_PCM,133u,0u,0u);
    voice(a,1u,1u,PORTABLE_AUDIO_VOICE_PSG_SQUARE,2u,0u,0u);
    voice(a,2u,0x40u,PORTABLE_AUDIO_VOICE_KEY_SPLIT,0u,0u,2u);
    mapping(a,0u,0u,63u,0u);mapping(a,1u,64u,127u,1u);
}
static int test_valid(void)
{
    uint8_t a[ASSET_SIZE];PortableAudioVoiceBankView view;
    PortableAudioVoice v;PortableAudioVoiceMapping m;valid(a);
    CHECK(portable_audio_voice_bank_open(&view,a,sizeof a)==PORTABLE_AUDIO_VOICE_OK);
    CHECK(view.bank_id==25u&&view.voice_count==3u&&view.mapping_count==2u);
    CHECK(portable_audio_voice_bank_voice(&view,2u,&v)==PORTABLE_AUDIO_VOICE_OK);
    CHECK(v.source_kind==PORTABLE_AUDIO_VOICE_KEY_SPLIT&&v.mapping_count==2u);
    CHECK(portable_audio_voice_bank_mapping(&view,1u,&m)==PORTABLE_AUDIO_VOICE_OK);
    CHECK(m.key_start==64u&&m.key_end==127u&&m.voice_index==1u);
    CHECK(portable_audio_voice_bank_resolve(&view,2u,10u,&v,NULL)==
          PORTABLE_AUDIO_VOICE_OK&&v.source_kind==PORTABLE_AUDIO_VOICE_PCM);
    CHECK(portable_audio_voice_bank_resolve(&view,2u,100u,&v,NULL)==
          PORTABLE_AUDIO_VOICE_OK&&v.source_kind==PORTABLE_AUDIO_VOICE_PSG_SQUARE);
    return 0;
}
static int test_rejections(void)
{
    uint8_t a[ASSET_SIZE];PortableAudioVoiceBankView view;
    valid(a);a[0]='X';CHECK(portable_audio_voice_bank_open(&view,a,sizeof a)==
        PORTABLE_AUDIO_VOICE_BAD_MAGIC);
    valid(a);put32(a+24u,41u);CHECK(portable_audio_voice_bank_open(&view,a,sizeof a)==
        PORTABLE_AUDIO_VOICE_BAD_SIZE);
    valid(a);a[40u+8u]=99u;CHECK(portable_audio_voice_bank_open(&view,a,sizeof a)==
        PORTABLE_AUDIO_VOICE_BAD_SOURCE);
    valid(a);put32(a+40u+20u+12u,4u);CHECK(portable_audio_voice_bank_open(&view,a,sizeof a)==
        PORTABLE_AUDIO_VOICE_BAD_SOURCE);
    valid(a);a[40u+60u+8u]=63u;CHECK(portable_audio_voice_bank_open(&view,a,sizeof a)==
        PORTABLE_AUDIO_VOICE_BAD_MAPPING);
    valid(a);put16(a+40u+60u+8u+2u,2u);CHECK(
        portable_audio_voice_bank_open(&view,a,sizeof a)==PORTABLE_AUDIO_VOICE_BAD_MAPPING);
    valid(a);put16(a+40u+2u*20u+18u,3u);CHECK(
        portable_audio_voice_bank_open(&view,a,sizeof a)==PORTABLE_AUDIO_VOICE_BAD_MAPPING);
    return 0;
}
static int test_file(const char *path)
{
    PortableAudioVoiceBankView view;uint8_t *bytes;long size;
    FILE *file=fopen(path,"rb");CHECK(file!=NULL&&fseek(file,0L,SEEK_END)==0);
    size=ftell(file);CHECK(size>0&&fseek(file,0L,SEEK_SET)==0);
    bytes=(uint8_t *)malloc((size_t)size);
    CHECK(bytes!=NULL&&fread(bytes,1u,(size_t)size,file)==(size_t)size);
    CHECK(fclose(file)==0);
    CHECK(portable_audio_voice_bank_open(&view,bytes,(size_t)size)==
          PORTABLE_AUDIO_VOICE_OK);
    printf("portable audio voice file: bank=%u voices=%u mappings=%u\n",
           view.bank_id,view.voice_count,view.mapping_count);
    free(bytes);return 0;
}
int main(int argc,char **argv)
{
    if(argc==2)return test_file(argv[1]);
    CHECK(argc==1);
    CHECK(test_valid()==0);CHECK(test_rejections()==0);
    puts("portable audio voice bank: PASS");return 0;
}
