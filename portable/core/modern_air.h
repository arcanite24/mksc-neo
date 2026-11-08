#ifndef PORTABLE_MODERN_AIR_H
#define PORTABLE_MODERN_AIR_H

#include <stdint.h>

typedef struct {
    uint16_t minimum_air_ticks,trick_window_ticks,boost_ticks,cap_ratio_256,minimum_speed_ratio_256;
    int32_t acceleration_q16;
    uint8_t launch_mask; /* 1: ramp/protected launch, 2: ordinary hop. Zero profile disables. */
} PortableModernAirRules;

typedef struct {
    uint16_t air_ticks,boost_ticks;
    uint8_t armed,tricked,previous_hop,last_applied;
} PortableModernAirState;

typedef struct {
    uint8_t hop_pressed,hop_held,airborne,launch_kind,landed,eligible,cancel;
    int32_t speed_q16,target_q16;
} PortableModernAirInput;

typedef struct {
    uint8_t trick_started,released,cancelled,applied;
    int32_t acceleration_q16,speed_cap_q16;
} PortableModernAirResult;

/* All-zero rules are valid and disabled. Enabled bounds match drift boost
 * bounds; air window is minimum..8192, launch mask contains only bits 1/2. */
int portable_modern_air_rules_valid(const PortableModernAirRules *rules);
int portable_modern_air_rules_enabled(const PortableModernAirRules *rules);
/* NULL/disabled rules return zero untouched; invalid input returns -1
 * transactionally. Launch counts as air tick one and cannot itself trick.
 * A fresh airborne hop within the inclusive window arms one landing reward.
 * The landing consumes the first boost tick; active boosts continue in air. */
int portable_modern_air_tick(PortableModernAirState *state,const PortableModernAirRules *rules,
    const PortableModernAirInput *input,PortableModernAirResult *result);

#endif
