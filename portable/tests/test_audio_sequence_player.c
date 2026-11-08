#include "audio_sequence_player.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "audio sequence player failed line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

enum { EVENT_COUNT = 5, ASSET_SIZE = 32 + EVENT_COUNT * 16 };

static void put16(uint8_t *out, uint16_t value)
{
    out[0]=(uint8_t)value; out[1]=(uint8_t)(value>>8);
}

static void put32(uint8_t *out, uint32_t value)
{
    out[0]=(uint8_t)value; out[1]=(uint8_t)(value>>8);
    out[2]=(uint8_t)(value>>16); out[3]=(uint8_t)(value>>24);
}

static void event(uint8_t *asset, unsigned index, uint32_t tick,
                  uint32_t target, uint16_t value, uint8_t track, uint8_t op,
                  uint8_t arg0, uint8_t arg1)
{
    uint8_t *out=asset+32u+index*16u;
    put32(out,tick);put32(out+4u,target);put16(out+8u,value);
    out[10]=track;out[11]=op;out[12]=arg0;out[13]=arg1;
}

static void make_asset(uint8_t asset[ASSET_SIZE])
{
    memset(asset,0,ASSET_SIZE);memcpy(asset,"MKSCAUD1",8u);
    put16(asset+8u,32u);put16(asset+10u,1u);put16(asset+12u,25u);
    asset[14]=2u;asset[16]=148u;put32(asset+20u,EVENT_COUNT);
    put16(asset+18u,25u);
    put32(asset+24u,32u);put32(asset+28u,ASSET_SIZE);
    event(asset,0u,0u,0u,66u,0u,PORTABLE_AUDIO_EVENT_TEMPO,0u,0u);
    event(asset,1u,0u,0u,0u,1u,PORTABLE_AUDIO_EVENT_MEMORY_SET,4u,9u);
    event(asset,2u,0u,0u,6u,0u,PORTABLE_AUDIO_EVENT_NOTE,67u,100u);
    event(asset,3u,6u,0u,0u,0u,PORTABLE_AUDIO_EVENT_LOOP,0u,0u);
    event(asset,4u,6u,0u,0u,1u,PORTABLE_AUDIO_EVENT_END,0u,0u);
}

static int test_deterministic_loop_and_budget(void)
{
    uint8_t asset[ASSET_SIZE];
    PortableAudioSequenceView view;
    PortableAudioSequencePlayer direct, chunked;
    make_asset(asset);
    CHECK(portable_audio_sequence_open(&view,asset,sizeof asset)==
          PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(portable_audio_sequence_player_init(&direct,&view)==
          PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_init(&chunked,&view)==
          PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_advance_to(&direct,18u,100u)==
          PORTABLE_AUDIO_PLAYER_OK);
    CHECK(direct.tracks[0].note_count==4u&&direct.tracks[0].tick_bias==18u);
    CHECK(!direct.tracks[1].active&&direct.memory[4]==9u&&direct.tempo==132u);
    CHECK(portable_audio_sequence_player_advance_to(&chunked,18u,2u)==
          PORTABLE_AUDIO_PLAYER_EVENT_BUDGET);
    CHECK(portable_audio_sequence_player_advance_to(&chunked,6u,20u)==
          PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_advance_to(&chunked,12u,20u)==
          PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_advance_to(&chunked,18u,20u)==
          PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_checksum(&direct)==
          portable_audio_sequence_player_checksum(&chunked));
    CHECK(portable_audio_sequence_player_checksum(&direct)==
          UINT32_C(0x585b169e));
    CHECK(portable_audio_sequence_player_advance_to(&direct,17u,1u)==
          PORTABLE_AUDIO_PLAYER_TICK_REGRESSION);
    return 0;
}

static int test_shared_memory(void)
{
    uint8_t asset[ASSET_SIZE],other[ASSET_SIZE],memory[256]={0};
    PortableAudioSequenceView view,other_view;
    PortableAudioSequencePlayer first,second,isolated;
    make_asset(asset);memcpy(other,asset,sizeof asset);
    other[32u+16u+13u]=0u; /* a second song explicitly clears byte4 */
    CHECK(portable_audio_sequence_open(&view,asset,sizeof asset)==PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(portable_audio_sequence_open(&other_view,other,sizeof other)==PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(portable_audio_sequence_player_init(&first,&view)==PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_init(&second,&other_view)==PORTABLE_AUDIO_PLAYER_OK);
    CHECK(portable_audio_sequence_player_init(&isolated,&view)==PORTABLE_AUDIO_PLAYER_OK);
    memory[4]=42u;memory[255]=77u;
    first.memory_area=second.memory_area=memory;
    CHECK(portable_audio_sequence_player_advance_to(&first,0u,100u)==PORTABLE_AUDIO_PLAYER_OK);
    CHECK(memory[4]==9u&&memory[255]==77u&&first.memory[4]==0u);
    const uint32_t before=portable_audio_sequence_player_checksum(&first);
    CHECK(portable_audio_sequence_player_advance_to(&second,0u,100u)==PORTABLE_AUDIO_PLAYER_OK);
    CHECK(memory[4]==0u&&memory[255]==77u);
    CHECK(portable_audio_sequence_player_checksum(&first)!=before);
    CHECK(portable_audio_sequence_player_advance_to(&first,6u,100u)==PORTABLE_AUDIO_PLAYER_OK);
    CHECK(memory[4]==0u&&memory[255]==77u); /* no stale per-song copyback */
    CHECK(portable_audio_sequence_player_advance_to(&isolated,0u,100u)==PORTABLE_AUDIO_PLAYER_OK);
    CHECK(isolated.memory[4]==9u&&memory[4]==0u);
    return 0;
}

int main(void)
{
    CHECK(test_deterministic_loop_and_budget()==0);
    CHECK(test_shared_memory()==0);
    puts("portable audio sequence player: PASS");
    return 0;
}
