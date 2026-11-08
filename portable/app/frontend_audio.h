#ifndef PORTABLE_FRONTEND_AUDIO_H
#define PORTABLE_FRONTEND_AUDIO_H

#include "audio_command_stream.h"
#include "frontend_flow.h"
#include "../core/character_audio.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const PortableCharacterAudioRules *character_cues;
    uint8_t rev_players[2][8]; /* resolved once from composed sequence metadata */
    size_t next_event;
    uint32_t title_stop_tick;
    uint32_t menu_start_tick;
    uint8_t title_exit_pending;
    uint8_t title_loop_started;
    struct { uint32_t tick; uint16_t song,volume; uint8_t volume_player; } delayed[4]; /* song 0 = free */
} PortableFrontendAudio;

int portable_frontend_audio_set_character_cues(PortableFrontendAudio *audio,
    const PortableCharacterAudioRules *rules,const uint8_t rev_players[2][8]);
int portable_frontend_audio_init(PortableFrontendAudio *audio,
    const PortableFrontendFlow *flow,PortableAudioCommandStream *commands);
int portable_frontend_audio_tick(PortableFrontendAudio *audio,
    const PortableFrontendFlow *flow,PortableAudioCommandStream *commands);

#endif
