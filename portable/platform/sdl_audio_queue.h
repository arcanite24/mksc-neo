#ifndef PORTABLE_SDL_AUDIO_QUEUE_H
#define PORTABLE_SDL_AUDIO_QUEUE_H

#include "audio_pcm_mixer.h"

#include <SDL3/SDL.h>
#include <stdint.h>

#define PORTABLE_SDL_AUDIO_SOURCE_RATE PORTABLE_AUDIO_PCM_RATE
#define PORTABLE_SDL_AUDIO_MAX_LATENCY_MS 125u

typedef enum
{
    PORTABLE_SDL_AUDIO_OK = 0,
    PORTABLE_SDL_AUDIO_INVALID_ARGUMENT,
    PORTABLE_SDL_AUDIO_INIT_FAILED,
    PORTABLE_SDL_AUDIO_OPEN_FAILED,
    PORTABLE_SDL_AUDIO_QUEUE_FAILED,
    PORTABLE_SDL_AUDIO_PAUSED,
    PORTABLE_SDL_AUDIO_DROPPED
} PortableSdlAudioStatus;

typedef struct
{
    SDL_AudioDeviceID device;
    SDL_AudioStream *stream;
    SDL_AudioSpec obtained;
    uint32_t max_queued_bytes;
    uint32_t source_block_bytes;
    uint32_t submitted_blocks;
    uint32_t dropped_blocks;
    uint8_t paused;
    uint8_t owns_audio_subsystem;
} PortableSdlAudioQueue;

PortableSdlAudioStatus portable_sdl_audio_queue_open(
    PortableSdlAudioQueue *queue);
PortableSdlAudioStatus portable_sdl_audio_queue_submit(
    PortableSdlAudioQueue *queue,
    const int16_t samples[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
void portable_sdl_audio_queue_pause(PortableSdlAudioQueue *queue,int paused);
void portable_sdl_audio_queue_close(PortableSdlAudioQueue *queue);
const char *portable_sdl_audio_status_name(PortableSdlAudioStatus status);

#endif
