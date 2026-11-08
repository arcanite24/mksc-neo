#ifndef PORTABLE_DRIVER_ART_MOTION_H
#define PORTABLE_DRIVER_ART_MOTION_H

#include <stdint.h>

typedef struct
{
    uint16_t speed_spans[16];
    uint8_t refresh_masks[16];
} PortableDriverArtRules;

typedef struct
{
    uint16_t countdown;
    uint8_t wheel_phase, bob_phase;
    uint8_t wheel_palette_bank, wheel_palette_valid, initialized;
    int8_t cached_bob_offset;
} PortableDriverArtMotion;

static inline void portable_driver_art_motion_init(PortableDriverArtMotion *state)
{
    *state = (PortableDriverArtMotion){.wheel_phase=2u,.initialized=1u};
}

/* CD50: the source character, rather than the seat, schedules CPU cells. */
static inline int portable_driver_art_motion_refresh_due(
    const PortableDriverArtRules *rules, uint32_t clock,
    uint8_t source_character, uint8_t hit_state, int local, int force_refresh)
{
    if (!rules) return 0;
    if (local || force_refresh) return 1;
    if (hit_state >= 16u) return 0;
    const unsigned mask=rules->refresh_masks[hit_state];
    return (mask==3u || mask==7u) &&
        (clock&mask)==(source_character&mask);
}

/* CBE4 reads the old D8 bob phase before C928 advances the palette phase. */
static inline int8_t portable_driver_art_motion_bob_offset(
    const PortableDriverArtMotion *state, int32_t speed_q16,
    uint32_t flags_48, uint16_t status_b4, uint8_t material_85, uint32_t clock)
{
    unsigned offset=!(flags_48&0x40u) && speed_q16<=INT32_C(0x009fffff)?
        clock&1u:0u;
    if (!(status_b4&0x8000u)) {
        offset+=state->bob_phase&1u;
        if ((material_85&0x1eu)>12u) offset+=(state->bob_phase>>1u)&1u;
        if (material_85==0x5cu) offset+=4u;
    }
    return (int8_t)offset;
}

/* C928 changes colors 1/2 only when a new wheel phase is installed. The
 * palette bank is latched before C84C consumes the +194 hit request. */
static inline int portable_driver_art_motion_tick_palette(
    PortableDriverArtMotion *state, const PortableDriverArtRules *rules,
    int32_t speed_q16, uint16_t pending_palette_194)
{
    if (!state || !rules) return 0;
    if (!state->initialized) portable_driver_art_motion_init(state);
    const uint32_t magnitude=speed_q16<0?0u-(uint32_t)speed_q16:
        (uint32_t)speed_q16;
    unsigned speed=magnitude>>16u;
    if (speed>15u) speed=15u;
    const unsigned span=speed?rules->speed_spans[speed]:
        (state->wheel_phase==2u?1u:0u);
    if (state->countdown) {
        --state->countdown;
        if (state->countdown>span) state->countdown=(uint16_t)span;
    } else if (span) {
        state->countdown=(uint16_t)span;
        state->wheel_phase=state->wheel_phase<2u?
            (uint8_t)(state->wheel_phase+1u):0u;
        state->bob_phase=(uint8_t)((state->bob_phase+1u)&7u);
        state->wheel_palette_bank=(pending_palette_194&0x800u)?14u:15u;
        state->wheel_palette_valid=1u;
    }
    return 1;
}

/* One fixed presentation turn: refresh the cached cell offset, then tick
 * the independent wheel colors. Missing optional rules preserve old packs. */
static inline int portable_driver_art_motion_step(
    PortableDriverArtMotion *state, const PortableDriverArtRules *rules,
    int32_t speed_q16, uint32_t flags_48, uint16_t status_b4,
    uint8_t material_85, uint16_t pending_palette_194, uint32_t clock,
    uint8_t source_character, uint8_t hit_state, int local, int force_refresh,
    uint8_t race_phase_7d4, int class8)
{
    if (!state || !rules) return 0;
    if (!state->initialized) portable_driver_art_motion_init(state);
    if (portable_driver_art_motion_refresh_due(rules,clock,source_character,
            hit_state,local,force_refresh))
        state->cached_bob_offset=portable_driver_art_motion_bob_offset(state,
            speed_q16,flags_48,status_b4,material_85,clock);
    if (!(race_phase_7d4&0x40u) && !class8)
        portable_driver_art_motion_tick_palette(state,rules,speed_q16,
            pending_palette_194);
    return 1;
}

#endif
