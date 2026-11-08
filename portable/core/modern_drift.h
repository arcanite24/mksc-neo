#ifndef PORTABLE_MODERN_DRIFT_H
#define PORTABLE_MODERN_DRIFT_H

#include <stdint.h>
#include "modern_air.h"

typedef struct {
    uint32_t profile_id,revision;
    uint16_t charge_ticks[3],boost_ticks[3],cap_ratio_256[3];
    uint16_t minimum_speed_ratio_256;
    int32_t acceleration_q16[3];
    PortableModernAirRules air;
} PortableModernDriftRules;

typedef struct {
    uint16_t charge_ticks,boost_ticks;
    uint8_t charge_tier,boost_tier,previous_held,last_applied_tier;
} PortableModernDriftState;

typedef struct {
    uint8_t held_drift,grounded,eligible,cancel;
    int32_t speed_q16,target_q16;
} PortableModernDriftInput;

typedef struct {
    /* tier_changed signals an upward charge threshold; released signals an
     * awarded boost. cancelled signals discarded charge or an explicit reset. */
    uint8_t tier_changed,released,cancelled,applied_tier;
    int32_t acceleration_q16,speed_cap_q16;
} PortableModernDriftResult;

/* Nonzero identity/revision, increasing charge thresholds, ticks 1..8192,
 * cap ratios 256..1024, minimum ratio 1..256, acceleration 1..128 Q16. */
int portable_modern_drift_rules_valid(const PortableModernDriftRules *rules);
/* One fixed tick. NULL rules return 0 without touching state/result. Invalid
 * arguments/rules/state return -1 unchanged; a valid profile returns 1.
 * Minimum speed gates charging and awards, not an existing boost. Airborne
 * held charge is suspended; an airborne release discards it. Eligible boosts
 * continue in air. Release consumes the first boost tick immediately. */
int portable_modern_drift_tick(PortableModernDriftState *state,
    const PortableModernDriftRules *rules,const PortableModernDriftInput *input,
    PortableModernDriftResult *result);

#endif
