#include <SDL3/SDL_main.h>
#include "asset_pack.h"
#include "audio_pack_song.h"
#include "audio_session.h"
#include "sdl_audio_queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb");
    uint8_t *bytes;
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
        return NULL;
    length = ftell(file);
    if (length <= 0 || (uint64_t)length > (uint64_t)INT32_MAX || fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, file) != (size_t)length)
    {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static int parse_u16(const char *text, uint16_t *value)
{
    char *end;
    unsigned long parsed;
    if (text[strspn(text, " \t\r\n\v\f")] == '-')
        return 0;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *text == '\0' || *end != '\0' || parsed == 0u || parsed > UINT16_MAX)
        return 0;
    *value = (uint16_t)parsed;
    return 1;
}

static int queue_self_test(void)
{
    PortableSdlAudioQueue queue;
    int16_t silence[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u]={0};
    PortableSdlAudioStatus status;
    if(!SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE))return 0;
    status=portable_sdl_audio_queue_open(&queue);
    if(status!=PORTABLE_SDL_AUDIO_OK)return 0;
    /* Freeze consumption to test the actual source-byte budget deterministically. */
    if(!SDL_PauseAudioStreamDevice(queue.stream))goto fail;
    while(queue.submitted_blocks<100u){
        status=portable_sdl_audio_queue_submit(&queue,silence);
        if(status==PORTABLE_SDL_AUDIO_DROPPED)break;
        if(status!=PORTABLE_SDL_AUDIO_OK)goto fail;
    }
    if(queue.dropped_blocks!=1u||queue.submitted_blocks==0u||
       SDL_GetAudioStreamQueued(queue.stream)>(int)queue.max_queued_bytes)goto fail;
    portable_sdl_audio_queue_pause(&queue,1);
    if(SDL_GetAudioStreamQueued(queue.stream)!=0||
       portable_sdl_audio_queue_submit(&queue,silence)!=PORTABLE_SDL_AUDIO_PAUSED)
        goto fail;
    portable_sdl_audio_queue_pause(&queue,0);
    if(portable_sdl_audio_queue_submit(&queue,silence)!=PORTABLE_SDL_AUDIO_OK)
        goto fail;
    portable_sdl_audio_queue_close(&queue);
    if(SDL_WasInit(SDL_INIT_AUDIO)!=0u)return 0;
    /* A borrowed subsystem must survive closing our stream. */
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO))return 0;
    status=portable_sdl_audio_queue_open(&queue);
    if(status!=PORTABLE_SDL_AUDIO_OK){SDL_QuitSubSystem(SDL_INIT_AUDIO);return 0;}
    portable_sdl_audio_queue_close(&queue);
    const bool retained=SDL_WasInit(SDL_INIT_AUDIO)!=0u;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if(!retained)return 0;
    puts("portable SDL audio queue: PASS rate=10512 block=176 bounded=125ms");
    return 1;
fail:
    portable_sdl_audio_queue_close(&queue);return 0;
}

typedef struct {
    double sum,squares;
    size_t count;
    float peak;
} OutputMeasurement;

static void SDLCALL measure_output(void *user,const SDL_AudioSpec *spec,
    float *samples,int bytes)
{
    OutputMeasurement *measurement=user;
    (void)spec;
    for(size_t i=0u;i<(size_t)bytes/sizeof *samples;++i){
        const float sample=samples[i];
        measurement->sum+=sample;
        measurement->squares+=(double)sample*sample;
        ++measurement->count;
        const float magnitude=sample<0.0f?-sample:sample;
        if(magnitude>measurement->peak)measurement->peak=magnitude;
    }
}

/* Observe SDL's final device mix, after stream gain and conversion. A mixer
 * WAV with nonzero music or DC cannot prove the countdown reaches this path. */
static int countdown_output_test(const PortableAssetPackView *pack)
{
    PortableAudioSession *session=malloc(sizeof *session);
    PortableSdlAudioQueue queue;
    if(session==NULL)return 0;
    if(portable_sdl_audio_queue_open(&queue)!=PORTABLE_SDL_AUDIO_OK){free(session);return 0;}
    printf("SDL countdown device: driver=%s rate=%d channels=%d\n",
        SDL_GetCurrentAudioDriver(),queue.obtained.freq,queue.obtained.channels);
    int passed=1;
    OutputMeasurement measurement={0};
    for(unsigned test=0u;test<3u && passed;++test){
        const uint16_t cue=test==1u?115u:114u;
        const float gain=test==2u?0.0f:1.0f;
        memset(&measurement,0,sizeof measurement);
        portable_sdl_audio_queue_pause(&queue,1);
        if(!SDL_SetAudioStreamGain(queue.stream,gain) ||
           !SDL_SetAudioPostmixCallback(queue.device,measure_output,&measurement) ||
           portable_audio_session_init(session,pack)!=PORTABLE_AUDIO_SESSION_OK){passed=0;break;}
        PortableAudioCommand command={.operation=PORTABLE_AUDIO_COMMAND_START_SONG,
            .player=PORTABLE_AUDIO_PLAYER_AUTO,.argument=cue};
        if(portable_audio_session_apply(session,&command)!=PORTABLE_AUDIO_SESSION_OK){passed=0;break;}
        portable_sdl_audio_queue_pause(&queue,0);
        const uint64_t start=SDL_GetTicksNS();
        for(unsigned frame=0u;frame<24u;++frame){
            int16_t samples[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
            if(portable_audio_session_render_frame(session,samples)!=PORTABLE_AUDIO_SESSION_OK ||
               portable_sdl_audio_queue_submit(&queue,samples)!=PORTABLE_SDL_AUDIO_OK){passed=0;break;}
            const uint64_t due=start+(uint64_t)(frame+1u)*PORTABLE_AUDIO_PCM_FRAME_SAMPLES*
                UINT64_C(1000000000)/PORTABLE_SDL_AUDIO_SOURCE_RATE;
            const uint64_t now=SDL_GetTicksNS();
            if(due>now)SDL_DelayNS(due-now);
        }
        SDL_Delay(150u);
        /* Unregistering synchronizes with the audio thread before reading. */
        if(!SDL_SetAudioPostmixCallback(queue.device,NULL,NULL)){passed=0;break;}
        const double mean=measurement.count?measurement.sum/(double)measurement.count:0.0;
        const double variance=measurement.count?measurement.squares/(double)measurement.count-mean*mean:0.0;
        const double peak=(double)measurement.peak*32768.0;
        printf("SDL countdown output: song=%u gain=%.1f samples=%zu peak=%.0f ac_energy=%.0f dropped=%u\n",
            cue,(double)gain,measurement.count,peak,variance*1073741824.0,queue.dropped_blocks);
        if(!measurement.count || queue.dropped_blocks!=0u ||
           (gain==0.0f?peak!=0.0:peak<1024.0 || variance*1073741824.0<16384.0))passed=0;
    }
    (void)SDL_SetAudioPostmixCallback(queue.device,NULL,NULL);
    portable_sdl_audio_queue_close(&queue);free(session);
    if(passed)puts("portable SDL countdown output: PASS full-volume AC and master mute");
    return passed;
}

int main(int argc,char **argv)
{
    PortableAssetPackView pack;
    PortableAudioPackSong song;
    PortableSdlAudioQueue queue;
    PortableAudioPackSongStatus song_status;
    PortableSdlAudioStatus queue_status;
    uint8_t *bytes;
    size_t size;
    uint16_t song_id;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    unsigned frame;
    if(argc==2&&strcmp(argv[1],"--self-test")==0)
        return queue_self_test()?0:1;
    if(argc==3&&strcmp(argv[1],"--output-test")==0){
        bytes=read_file(argv[2],&size);
        if(bytes==NULL||portable_asset_pack_open(bytes,size,&pack)!=PORTABLE_ASSET_PACK_OK){free(bytes);return 1;}
        const int passed=countdown_output_test(&pack);free(bytes);return passed?0:1;
    }
    if(argc!=3||!parse_u16(argv[2],&song_id)){
        fprintf(stderr,"usage: %s --self-test | --output-test PACK | PACK SONG_ID\n",argv[0]);
        return 2;
    }
    bytes=read_file(argv[1],&size);
    if(bytes==NULL||portable_asset_pack_open(bytes,size,&pack)!=
       PORTABLE_ASSET_PACK_OK){free(bytes);fputs("audio pack rejected\n",stderr);return 1;}
    song_status=portable_audio_pack_song_init(&song,&pack,song_id);
    if(song_status!=PORTABLE_AUDIO_PACK_SONG_OK){
        fprintf(stderr,"audio song rejected: %s\n",
            portable_audio_pack_song_status_name(song_status));free(bytes);return 1;
    }
    queue_status=portable_sdl_audio_queue_open(&queue);
    if(queue_status!=PORTABLE_SDL_AUDIO_OK){
        fprintf(stderr,"SDL audio rejected: %s: %s\n",
            portable_sdl_audio_status_name(queue_status),SDL_GetError());
        free(bytes);return 1;
    }
    for(frame=0u;frame<600u;++frame){
        if(frame==240u){portable_audio_pack_song_pause(&song,1);
            portable_sdl_audio_queue_pause(&queue,1);}
        if(frame==300u){portable_audio_pack_song_pause(&song,0);
            portable_sdl_audio_queue_pause(&queue,0);}
        song_status=portable_audio_pack_song_render_frame(&song,output);
        if(song_status!=PORTABLE_AUDIO_PACK_SONG_OK)break;
        queue_status=portable_sdl_audio_queue_submit(&queue,output);
        if(queue_status!=PORTABLE_SDL_AUDIO_OK&&
           queue_status!=PORTABLE_SDL_AUDIO_PAUSED&&
           queue_status!=PORTABLE_SDL_AUDIO_DROPPED)break;
        SDL_Delay(16u);
    }
    printf("portable SDL audio song: song=%u frames=%u submitted=%u dropped=%u "
           "device=%dHz/%uch/%ubit\n",song_id,frame,queue.submitted_blocks,
           queue.dropped_blocks,queue.obtained.freq,queue.obtained.channels,
           SDL_AUDIO_BITSIZE(queue.obtained.format));
    portable_sdl_audio_queue_close(&queue);free(bytes);
    return frame==600u?0:1;
}
