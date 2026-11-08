#include "sdl_audio_queue.h"

#include <string.h>

static void close_parts(PortableSdlAudioQueue *queue)
{
    /* SDL_OpenAudioDeviceStream owns its logical device; destroying the stream
     * closes that device without affecting another owner's audio subsystem. */
    if(queue->stream!=NULL)SDL_DestroyAudioStream(queue->stream);
    if(queue->owns_audio_subsystem)SDL_QuitSubSystem(SDL_INIT_AUDIO);
    memset(queue,0,sizeof *queue);
}

PortableSdlAudioStatus portable_sdl_audio_queue_open(PortableSdlAudioQueue *queue)
{
    if(queue==NULL)return PORTABLE_SDL_AUDIO_INVALID_ARGUMENT;
    memset(queue,0,sizeof *queue);
    if((SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO)==0u){
        if(!SDL_InitSubSystem(SDL_INIT_AUDIO))return PORTABLE_SDL_AUDIO_INIT_FAILED;
        queue->owns_audio_subsystem=1u;
    }
    const SDL_AudioSpec source={.format=SDL_AUDIO_S16,.channels=2,
        .freq=PORTABLE_SDL_AUDIO_SOURCE_RATE};
    queue->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&source,NULL,NULL);
    if(queue->stream==NULL){close_parts(queue);return PORTABLE_SDL_AUDIO_OPEN_FAILED;}
    queue->device=SDL_GetAudioStreamDevice(queue->stream);
    if(!SDL_GetAudioDeviceFormat(queue->device,&queue->obtained,NULL)){
        close_parts(queue);return PORTABLE_SDL_AUDIO_OPEN_FAILED;
    }
    /* GetAudioStreamQueued counts unconverted source bytes, so both the budget
     * and one-block reservation are measured in our fixed stereo S16 format. */
    queue->max_queued_bytes=PORTABLE_SDL_AUDIO_SOURCE_RATE*2u*sizeof(int16_t)*
        PORTABLE_SDL_AUDIO_MAX_LATENCY_MS/1000u;
    queue->source_block_bytes=PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u*sizeof(int16_t);
    if(!SDL_ResumeAudioStreamDevice(queue->stream)){
        close_parts(queue);return PORTABLE_SDL_AUDIO_OPEN_FAILED;
    }
    return PORTABLE_SDL_AUDIO_OK;
}

PortableSdlAudioStatus portable_sdl_audio_queue_submit(PortableSdlAudioQueue *queue,
    const int16_t samples[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u])
{
    if(queue==NULL||samples==NULL||queue->stream==NULL)return PORTABLE_SDL_AUDIO_INVALID_ARGUMENT;
    if(queue->paused)return PORTABLE_SDL_AUDIO_PAUSED;
    const int queued=SDL_GetAudioStreamQueued(queue->stream);
    if(queued<0)return PORTABLE_SDL_AUDIO_QUEUE_FAILED;
    if((uint32_t)queued>queue->max_queued_bytes-queue->source_block_bytes){
        queue->dropped_blocks++;return PORTABLE_SDL_AUDIO_DROPPED;
    }
    if(!SDL_PutAudioStreamData(queue->stream,samples,(int)queue->source_block_bytes))
        return PORTABLE_SDL_AUDIO_QUEUE_FAILED;
    queue->submitted_blocks++;return PORTABLE_SDL_AUDIO_OK;
}

void portable_sdl_audio_queue_pause(PortableSdlAudioQueue *queue,int paused)
{
    if(queue==NULL||queue->stream==NULL)return;
    queue->paused=paused?1u:0u;
    if(queue->paused){
        SDL_PauseAudioStreamDevice(queue->stream);SDL_ClearAudioStream(queue->stream);
    }else SDL_ResumeAudioStreamDevice(queue->stream);
}

void portable_sdl_audio_queue_close(PortableSdlAudioQueue *queue)
{
    if(queue!=NULL)close_parts(queue);
}

const char *portable_sdl_audio_status_name(PortableSdlAudioStatus status)
{
    static const char *const names[]={"ok","invalid argument","init failed",
        "open failed","queue failed","paused","dropped"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:"unknown status";
}
