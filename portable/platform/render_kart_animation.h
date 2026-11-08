#ifndef PORTABLE_RENDER_KART_ANIMATION_H
#define PORTABLE_RENDER_KART_ANIMATION_H

#include "character_view.h"
#include "driver_art_motion.h"
#include "render_pose.h"

#include <stddef.h>

/* Accepted movement only. foot is the P2 supported foot, not sphere center.
 * Optional art_rules are borrowed for this call; owners retain no pointers.
 * roughness is an explicit presentation ratio0..1; no material ID guesses. */
typedef struct
{
    PortableRenderPose foot;
    PortableMeshKartState kart;
    const PortableDriverArtRules *art_rules;
    uint32_t tick;
    /* First invisible accepted age from the FX asset:0 disables,2..65535
     * cycles ages1..lifetime-1. No stock FX timing is assumed here. */
    uint32_t spark_lifetime, flame_lifetime;
    int32_t top_speed_q16, roughness_q16;
    double status_spin, status_scale[3], spin_phase; /* Presentation radians/ratios; zero scales mean one. */
    uint8_t boosting, cut;
} PortableRenderKartAnimationInput;

typedef struct
{
    PortableRenderPose pose;
    double body_scale[3], lean, bob, phase, wheel_distance;
    double status_spin, status_scale[3], spin_phase;
    PortableDriverArtMotion wheels;
    int32_t steer_q16, speed_q16, top_speed_q16;
    uint32_t tick, spark_lifetime, flame_lifetime;
    uint16_t hop_age, landing_age, spark_age, flame_age;
    uint8_t grounded, drifting, spark_tier, boosting, wheel_phase, valid;
} PortableRenderKartAnimation;

typedef struct
{
    PortableRenderPose pose; /* Animated foot: bob and lean, no model pivot. */
    double body_scale[3], lean, bob, wheel_distance, tick;
    double status_spin, status_scale[3], spin_phase;
    int32_t steer_q16, speed_q16, top_speed_q16;
    uint16_t spark_age, flame_age, spark_phase, flame_phase;
    /* phase is the zero-based accepted clock, not an imported frame index.
     * The FX asset adapter selects actual cells using its duration+1 spans. */
    uint8_t spark_active, flame_active, spark_tier, wheel_phase;
} PortableRenderKartAnimationSnapshot;

/* Zero-init owners. Same accepted tick is immutable; lower/nonconsecutive
 * clocks require cut. All failures preserve out, including aliased calls. */
int portable_render_kart_animation_step(PortableRenderKartAnimation *out,
                                        const PortableRenderKartAnimation *before,
                                        const PortableRenderKartAnimationInput *input, char *error,
                                        size_t capacity);
int portable_render_kart_animation_snapshot(PortableRenderKartAnimationSnapshot *out,
                                            const PortableRenderKartAnimation *previous,
                                            const PortableRenderKartAnimation *current,
                                            uint32_t alpha_q16);

/* Explicit modern adapter: |steer| maps0..1 to source response bins0..14;
 * |speed|/authored top maps0..1 to source view bins0..14 (source >>21).
 * Source wheel scheduling separately maps that ratio to0..15 Q16 units.
 * No claim that source acceleration/handling units equal modern metres.
 * selected is true only for this driver's own pane; missing optional rules use
 * the unselected source direction path. Returned yaw correction (radians) is
 * the SAME source signed correction used by cells, for mod model facing.
 * Callers may pass a local snapshot with status-spin composed into pose first.
 * Both outputs preserve their previous values on failure. */
int portable_render_kart_animation_view(const PortableRenderKartAnimationSnapshot *snapshot,
                                        const PortableRenderView *view,
                                        const PortableCharacterViewRules *rules, int selected,
                                        PortableCharacterViewPose *out,
                                        double *yaw_correction_radians);

/* Compensate only the animation's newly lowered bound, preserving authored
 * pivots/seating. bounds={minX,minY,minZ,maxX,maxY,maxZ} is a cached local AABB
 * (opaque source bounds or model bounds), never a per-draw vertex scan.
 * Resolve both instances for this pane, then project the eight corners onto
 * the original unit support normal. Return the nonnegative lift to apply along
 * that normal to animated's translation. Both instances should otherwise have
 * the same status scales/pivot/yaw. Aliasing with out is allowed; failures leave
 * out untouched. No instances, geometry, views or caches are modified. */
int portable_render_kart_animation_foot_lift(const PortableRenderInstance *unleaned,
                                             const PortableRenderInstance *animated,
                                             const PortableRenderView *view, const double bounds[6],
                                             const double support_normal[3], double *out);

#endif
