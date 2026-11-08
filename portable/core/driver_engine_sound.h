#ifndef PORTABLE_DRIVER_ENGINE_SOUND_H
#define PORTABLE_DRIVER_ENGINE_SOUND_H

#include "driver_pilot.h"
#include "character_audio.h"

enum { PORTABLE_ENGINE_CLASS_COUNT = 3, PORTABLE_ENGINE_DRIVER_COUNT = 8,
       PORTABLE_ENGINE_VOLUME_BUCKETS = 16 };

typedef struct
{
    int16_t limit, acceleration, release, skid, drift;
} PortableEngineRevProfile;

typedef struct
{
    PortableEngineRevProfile profiles[PORTABLE_ENGINE_CLASS_COUNT];
    uint16_t volumes[PORTABLE_ENGINE_DRIVER_COUNT][PORTABLE_ENGINE_VOLUME_BUCKETS];
    uint16_t idle_songs[PORTABLE_ENGINE_DRIVER_COUNT];
    uint16_t coast_songs[PORTABLE_ENGINE_DRIVER_COUNT];
    uint16_t throttle_songs[PORTABLE_ENGINE_DRIVER_COUNT];
    uint16_t nearby_songs[PORTABLE_ENGINE_DRIVER_COUNT];
    uint16_t brake_songs[4];
} PortableEngineSoundRules;

typedef struct {
    uint16_t voices[8][2][2], impact_voices[8][2][2];
    uint16_t taunt[8][2], laugh[8][2];
    PortableCharacterAudioRules character_cues; /* absent in HAZ1 v2/v3 */
} PortableHazardSoundRules;

typedef struct { uint16_t songs[16]; uint8_t restart_ticks[16]; uint8_t theme; } PortableSurfaceSoundRules;

/* Immutable view of original driver fields; audio outputs never feed physics. */
typedef struct
{
    uint32_t state_flags;
    int32_t height_q16, speed_q16;
    uint16_t control_word, response_flags, spin_delay;
    uint16_t speed_parameter, recovery_scale;
    int16_t lightning_timer;
    uint8_t mode, driver_mode, response_status, selector_index;
    uint8_t character_flags, flattened, bomb_walk_audio, material;
    int16_t response_q8;
    uint8_t water_recovery_started, retrieval_started, water_rescue_arrived, ice_thaw_started;
    uint8_t hazard_audio_kind, hazard_audio_variant;
    uint8_t surface_jump_started, protected_hop_started, landed;
} PortableDriverEngineSoundInput;

/* Source halfword loads and writes are explicit on every rev update. */
static inline int32_t portable_engine_signed16(uint16_t value)
{
    return value < 0x8000u ? (int32_t)value : (int32_t)value - 65536;
}
static inline void portable_driver_engine_oscillate(uint16_t *accumulator,
    uint16_t *oscillation_mode, uint16_t word,
    int32_t acceleration, int32_t deceleration, int32_t release)
{
    int32_t value = portable_engine_signed16(*accumulator);
    if ((word & UINT16_C(0x8000)) != 0u) {
        if (*oscillation_mode == 0u) {
            value = portable_engine_signed16((uint16_t)(value + acceleration));
            if (value > 0x0fbf) --*oscillation_mode;
        } else {
            value = portable_engine_signed16((uint16_t)(value - deceleration));
            if (value <= 0x0dbf) *oscillation_mode = 0u;
        }
    } else if (*oscillation_mode == 0u) {
        value = portable_engine_signed16((uint16_t)(value - release));
    }
    if (value > 0x0fbf) value = 0x0fc0;
    if (value < 0) value = 0;
    *accumulator = (uint16_t)value;
}

/* 40FEC, shared by GP's launch flag owner and the engine audio controller. */
static inline void portable_driver_engine_revs_tick(uint16_t *accumulator,
    uint16_t *oscillation_mode, const PortableEngineRevProfile *profile,
    const PortableDriverEngineSoundInput *input)
{
    if (input->mode >= 1u && input->mode <= 6u) return;
    if (input->mode == 7u) {
        portable_driver_engine_oscillate(accumulator,oscillation_mode,
            input->control_word,0x100,0x1000,0x800);
        return;
    }
    int32_t value = portable_engine_signed16(*accumulator);
    const int impact = input->mode == 8u || input->response_status == 13u ||
        (input->response_status >= 5u && input->response_status <= 7u &&
         input->spin_delay == 0u);
    int32_t delta;
    if (impact) {
        delta = (int32_t)profile->skid * 8;
    } else if ((input->control_word & 0xc000u) == 0x8000u && !input->flattened) {
        if (input->selector_index <= 9u)
            delta = (input->response_flags & 4u) && value >= profile->limit - 0x200 ?
                profile->drift : profile->acceleration;
        else
            delta = value < profile->limit / 4 ? profile->acceleration : profile->skid;
    } else {
        delta = profile->release;
    }
    value = portable_engine_signed16((uint16_t)(value + delta));
    if (!impact && value >= profile->limit) value = profile->limit;
    if (value < 0) value = 0;
    *accumulator = (uint16_t)value;
}

static inline PortableDriverEngineSoundInput portable_driver_engine_sound_input(
    const PortableDriverPilotState *driver)
{
    const PortableDriverSteeringState *steering = &driver->steering;
    return (PortableDriverEngineSoundInput){
        .state_flags = steering->state_flags,
        .height_q16 = driver->pose.position_z,
        .speed_q16 = driver->motion.speed_q16,
        .control_word = steering->control_word,
        .response_flags = steering->response_flags,
        .spin_delay = steering->spin_delay,
        .speed_parameter = driver->input.speed_parameter,
        .recovery_scale = driver->drift_charge.charge_counter,
        .lightning_timer = driver->lightning_timer_68,
        .mode = steering->mode,
        .driver_mode = steering->driver_mode,
        .response_status = steering->response_status,
        .selector_index = steering->response_selector_index,
        .character_flags = steering->response_selector_flags,
        .flattened = driver->flattened_64,
        .material = driver->source_material_85,
        .response_q8 = steering->response_q8,
        .water_recovery_started = (uint8_t)(driver->source_terrain22_active&&driver->hop.recovery_started),
        .retrieval_started = driver->hop.retrieval_started,
        .water_rescue_arrived = (uint8_t)(driver->source_terrain22_active&&driver->hop.lakitu_arrived),
        .hazard_audio_kind = driver->hazard_audio_kind,
        .hazard_audio_variant = driver->hazard_audio_variant,
        .surface_jump_started = driver->hop.surface_jump_started,
        .protected_hop_started = driver->hop.protected_hop_started,
        .landed = driver->hop.landed,
    };
}

#endif
