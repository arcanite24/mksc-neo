#ifndef PORTABLE_ENGINE_CONTROL_H
#define PORTABLE_ENGINE_CONTROL_H

#include <stdint.h>
#include "driver_engine_sound.h"
#include "audio_command_stream.h"

typedef struct
{
    uint16_t accumulator, prestart_mode;
    uint16_t fade;
    uint8_t flags, tire_active;
} PortableEngineControl;

typedef struct
{
    uint16_t volume;
    int16_t pitch, curve;
    uint8_t bucket;
    uint16_t start_song, entry_song;
    uint8_t stop_engine, stop_recovery, apply_controls;
} PortableEngineAudioControl;

typedef struct
{
    uint16_t start_song, stop_song, volume;
    uint8_t apply_volume, stop_if_playing;
} PortableTireAudioControl;

typedef struct { uint16_t song, cell_key; } PortableSurfaceAudioState;
typedef struct { uint16_t start_song; uint8_t stop; } PortableSurfaceAudioControl;

typedef struct { uint16_t song; uint8_t stop_surface, require_idle; } PortableJumpAudioControl;
/* 9B38/A58C launch and landing events before the shared contact producers. */
void portable_engine_jump_audio(PortableEngineControl *control,
    PortableSurfaceAudioState *surface,const PortableDriverEngineSoundInput *input,
    PortableJumpAudioControl *out);

typedef struct
{
    int32_t x_q16,y_q16;
    PortableDriverEngineSoundInput input;
    uint16_t revs;
    int16_t doppler;
    uint8_t character,visible;
} PortableNearbyEngineDriver;
typedef struct { uint16_t song; uint8_t active,target; } PortableNearbyEngineState;
typedef struct
{
    uint16_t song,volume;
    int16_t pitch;
    uint8_t start,stop,apply;
} PortableNearbyEngineControl;
/* C38C nearest-rival engine/footstep ownership,411D0 speed pitch and419D0 volume. */
int portable_engine_nearby_tick(PortableNearbyEngineState *state,
    const PortableEngineSoundRules *rules,const PortableNearbyEngineDriver *drivers,
    size_t count,int32_t camera_x,int32_t camera_y,int32_t listener_height,
    uint32_t listener_flags,uint8_t phase,uint16_t mode,PortableNearbyEngineControl *out);

typedef struct { uint16_t song, volume; uint8_t player; } PortableHazardAudioControl;
/* 7FD8: the local Select horn from ECB78, or 0 when +0x48 gates it off. */
uint16_t portable_engine_horn_song(const PortableCharacterAudioRules *rules,
    uint8_t character,uint32_t listener_flags);
/* 39C9E void voices and39F74 local lava, using ECC08 content. */
void portable_engine_hazard_audio(const PortableHazardSoundRules *rules,
    uint8_t character, uint8_t kind, uint8_t variant, int rival,
    uint32_t listener_flags, uint16_t spatial_volume, PortableHazardAudioControl *out);

/* C38C material sounds, shared with41704's tire-player ownership. */
void portable_engine_surface_audio_tick(PortableEngineControl *control,
    PortableSurfaceAudioState *state, const PortableSurfaceSoundRules *rules,
    const PortableDriverEngineSoundInput *input, int32_t x_q16, int32_t y_q16,
    int scene_stop, PortableSurfaceAudioControl *out);

/* Complete41704 braking/burnout loop ownership, after engine controls. */
void portable_engine_tire_audio_tick(PortableEngineControl *control,
    const PortableEngineSoundRules *rules,
    const PortableDriverEngineSoundInput *input, int scene_stop,
    PortableTireAudioControl *out);

void portable_engine_control_init(PortableEngineControl *control);
/* sub_8040E8C, held on the grid; sub_8040F38 uses faster oscillation. */
void portable_engine_control_prestart_tick(PortableEngineControl *control,
    uint16_t control_word);
/* Complete sub_8040FEC, called on the driver's eight-tick update. */
void portable_engine_control_race_tick(PortableEngineControl *control,
    const PortableEngineRevProfile *profile,
    const PortableDriverEngineSoundInput *input);
/* sub_8041144 and local-player sub_8041244, once per simulation tick.
 * Scene stop is original race phase bit40; running enables throttle changes. */
int portable_engine_audio_control(PortableEngineControl *control,
    const PortableEngineSoundRules *rules, uint8_t character,
    const PortableDriverEngineSoundInput *input, int scene_stop, int running,
    PortableEngineAudioControl *out);

#endif
