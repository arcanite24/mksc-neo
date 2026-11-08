#ifndef PORTABLE_MESH_KART_RULES_H
#define PORTABLE_MESH_KART_RULES_H
#include <stdint.h>
#include <stddef.h>

/* MK8 Deluxe's kart model: one MK8 frame is one
 * fixed tick and one MK8 unit is 0.0526316 m, so 1 unit/frame is
 * MESH_KART_UNIT_SPEED m/s. Engine constants live in mesh_kart.c; everything
 * that differs between karts comes from a kind-102 KPH2 profile.
 *
 * A profile holds MK8's 13 stats. Each stat owns one or more ranges, a value
 * at stat level 0 and at level 19; a racer's level is the profile's base level
 * plus its driver's and kart's points, clamped to 0..19 (fractional levels
 * interpolate). portable_mesh_kart_rules_resolve turns a profile, an engine
 * class and levels into the concrete per-racer rules the simulation reads.
 * These are authored values, not MK8's Performance tables. */
#define PORTABLE_MESH_KART_UNIT_SPEED_Q16 INT32_C(206015) /* 3.143553 m/s per unit/frame */
enum
{
    PORTABLE_MESH_KART_RULES_VERSION = 2,
    PORTABLE_MESH_KART_STAT_LEVEL_MAX = 19,
    PORTABLE_MESH_KART_STOCK_DRIVERS = 8,
    PORTABLE_MESH_KART_CLASSES = 3
};
/* MK8's stat order. */
enum
{
    PORTABLE_MESH_KART_STAT_WEIGHT,
    PORTABLE_MESH_KART_STAT_ACCELERATION,
    PORTABLE_MESH_KART_STAT_TRACTION_ON,
    PORTABLE_MESH_KART_STAT_TRACTION_OFF,
    PORTABLE_MESH_KART_STAT_MINI_TURBO,
    PORTABLE_MESH_KART_STAT_SPEED_GROUND,
    PORTABLE_MESH_KART_STAT_SPEED_WATER,
    PORTABLE_MESH_KART_STAT_SPEED_AIR,
    PORTABLE_MESH_KART_STAT_SPEED_ANTIGRAVITY,
    PORTABLE_MESH_KART_STAT_HANDLING_GROUND,
    PORTABLE_MESH_KART_STAT_HANDLING_WATER,
    PORTABLE_MESH_KART_STAT_HANDLING_AIR,
    PORTABLE_MESH_KART_STAT_HANDLING_ANTIGRAVITY,
    PORTABLE_MESH_KART_STATS
};
/* Ranges in wire order; each stat owns a contiguous run (see
 * portable_mesh_kart_stat_ranges). Units: weight is MK8's unitless mass;
 * acceleration limit m/s^2, rate per tick (Q16, the share of the gap to the
 * cap closed each tick); traction is the per-tick share of sideways slip kept
 * (on-road, and the mild slippery class);
 * mini-turbo thresholds are charge units and ticks are ticks, caps are ratios
 * of top speed and accelerations m/s^2; speeds m/s at 150cc; handling is the
 * normal and drift turn rate in radians per tick at 7+ units/frame and the
 * spin-turn gain (stick per tick into MK8's ±30 accumulator). */
enum
{
    PORTABLE_MESH_KART_RANGE_WEIGHT_LOW,
    PORTABLE_MESH_KART_RANGE_WEIGHT_HIGH,
    PORTABLE_MESH_KART_RANGE_WEIGHT_BOOST,
    PORTABLE_MESH_KART_RANGE_ACCELERATION_LIMIT,
    PORTABLE_MESH_KART_RANGE_ACCELERATION_RATE,
    PORTABLE_MESH_KART_RANGE_TRACTION_ON,
    PORTABLE_MESH_KART_RANGE_TRACTION_OFF,
    PORTABLE_MESH_KART_RANGE_CHARGE_1,
    PORTABLE_MESH_KART_RANGE_CHARGE_2,
    PORTABLE_MESH_KART_RANGE_CHARGE_3,
    PORTABLE_MESH_KART_RANGE_MT1_TICKS,
    PORTABLE_MESH_KART_RANGE_MT1_CAP,
    PORTABLE_MESH_KART_RANGE_MT1_ACCELERATION,
    PORTABLE_MESH_KART_RANGE_MT2_TICKS,
    PORTABLE_MESH_KART_RANGE_MT2_CAP,
    PORTABLE_MESH_KART_RANGE_MT2_ACCELERATION,
    PORTABLE_MESH_KART_RANGE_TRICK_TICKS,
    PORTABLE_MESH_KART_RANGE_TRICK_CAP,
    PORTABLE_MESH_KART_RANGE_TRICK_ACCELERATION,
    PORTABLE_MESH_KART_RANGE_SPEED_GROUND,
    PORTABLE_MESH_KART_RANGE_SPEED_WATER,
    PORTABLE_MESH_KART_RANGE_SPEED_AIR,
    PORTABLE_MESH_KART_RANGE_SPEED_ANTIGRAVITY,
    PORTABLE_MESH_KART_RANGE_HANDLING_GROUND, /* normal, drift, spin */
    PORTABLE_MESH_KART_RANGE_HANDLING_GROUND_DRIFT,
    PORTABLE_MESH_KART_RANGE_HANDLING_GROUND_SPIN,
    PORTABLE_MESH_KART_RANGE_HANDLING_WATER = PORTABLE_MESH_KART_RANGE_HANDLING_GROUND + 3,
    PORTABLE_MESH_KART_RANGE_HANDLING_AIR = PORTABLE_MESH_KART_RANGE_HANDLING_WATER + 3,
    PORTABLE_MESH_KART_RANGE_HANDLING_ANTIGRAVITY = PORTABLE_MESH_KART_RANGE_HANDLING_AIR + 3,
    PORTABLE_MESH_KART_RANGES = PORTABLE_MESH_KART_RANGE_HANDLING_ANTIGRAVITY + 3
};
typedef struct
{
    int32_t level0_q16, level19_q16;
} PortableMeshKartStatRange;
/* KPH2/v2 profile. The wire order is this declaration order after a 32-byte
 * header (magic/version/size/id/revision/zero flags/reserved[2]); all words
 * are little-endian; decoders assign members, never cast payloads. */
typedef struct PortableMeshKartProfile
{
    uint32_t profile_id, revision;
    PortableMeshKartStatRange ranges[PORTABLE_MESH_KART_RANGES];
    int32_t base_level_q16[PORTABLE_MESH_KART_STATS];
    /* Points added for stock drivers 1..8 (row index = driver ID - 1). */
    int32_t driver_points_q16[PORTABLE_MESH_KART_STOCK_DRIVERS][PORTABLE_MESH_KART_STATS];
    int32_t class_speed_ratio_q16[PORTABLE_MESH_KART_CLASSES]; /* 50/100/150cc */
    int32_t cpu_speed_ratio_q16[PORTABLE_MESH_KART_CLASSES];   /* CPU top speed share per class */
    int32_t handling_response_q16;
    int32_t drift_response_q16;
    int32_t minimum_drift_speed_ratio_q16;
    int32_t outside_drift_angle_q16;
    int32_t outside_drift_recovery_q16;
    int32_t air_steering_ratio_q16;
    int32_t normal_alignment_response_q16;
    uint32_t panel_boost_ticks;
    int32_t panel_cap_ratio_q16;
    int32_t panel_acceleration_q16;
    int32_t hop_velocity_q16;
    int32_t terminal_fall_speed_q16;
    int32_t ground_snap_q16;
    int32_t ground_probe_q16;
    int32_t kart_radius_q16;
    int32_t ride_height_q16;
    int32_t ground_normal_cutoff_q30;
    uint32_t respawn_ticks;
    uint32_t respawn_grace_ticks;
    int32_t respawn_lift_q16;
} PortableMeshKartProfile;
#define PORTABLE_MESH_KART_PROFILE_WORDS \
    (2u * PORTABLE_MESH_KART_RANGES + PORTABLE_MESH_KART_STATS * (1u + PORTABLE_MESH_KART_STOCK_DRIVERS) + \
     2u * PORTABLE_MESH_KART_CLASSES + 20u)
#define PORTABLE_MESH_KART_RULES_SIZE (32u + 4u * PORTABLE_MESH_KART_PROFILE_WORDS)

/* One racer's resolved rules. Distances, speeds, accelerations and ratios are
 * Q16 SI; turn rates are turns/s; response and grip rates are per second,
 * integrated with the fixed dt. Ground probe/snap measure the gap below the
 * body foot. Charge and duration fields count ticks. */
typedef struct PortableMeshKartRules
{
    uint32_t profile_id, revision;
    int32_t forward_speed_q16;  /* class top speed on ground */
    int32_t cpu_pace_ratio_q16; /* share of its planned speeds a CPU drives; Q for humans */
    int32_t reverse_speed_q16;  /* MK8 brake target: 4 units/frame */
    int32_t acceleration_limit_q16; /* m/s^2 */
    int32_t acceleration_rate_q16;  /* per tick */
    /* Planning approximations of MK8's exponential coast and brake (average
     * deceleration stopping from top speed); route planners only. */
    int32_t coast_deceleration_q16;
    int32_t brake_deceleration_q16;
    int32_t handling_response_q16;
    int32_t handling_turn_rate_q16;
    int32_t drift_response_q16;
    int32_t drift_turn_rate_q16;
    int32_t spin_gain_q16; /* MK8 spin-turn accumulator gain per tick at full stick */
    /* MK8 anti-gravity rows (PRSA/PRTA): top speed and the handling trio. */
    int32_t antigravity_speed_q16, antigravity_turn_rate_q16, antigravity_drift_turn_rate_q16,
        antigravity_spin_gain_q16;
    int32_t glide_speed_q16; /* MK8 glide top speed (PRSG): air speed row x class */
    /* MK8 traction: road slide target (0.75), the slippery classes' mild
     * target and the off-road traction stat row as a share of row 19. */
    int32_t traction_on_q16, traction_off_q16, traction_level_q16;
    int32_t minimum_drift_speed_ratio_q16;
    int32_t outside_drift_angle_q16;
    int32_t outside_drift_recovery_q16;
    int32_t air_steering_ratio_q16;
    int32_t normal_alignment_response_q16;
    uint32_t charge_thresholds[3];
    uint32_t neutral_charge_rate;
    uint32_t inward_charge_rate;
    uint32_t boost_ticks[3];
    int32_t boost_cap_ratio_q16[3];
    int32_t boost_acceleration_q16[3];
    uint32_t trick_ticks; /* MK8 trick boost (slot 6) */
    int32_t trick_cap_ratio_q16, trick_acceleration_q16;
    uint32_t panel_boost_ticks;
    int32_t panel_cap_ratio_q16;
    int32_t panel_acceleration_q16;
    int32_t hop_velocity_q16;
    int32_t terminal_fall_speed_q16;
    int32_t ground_snap_q16;
    int32_t ground_probe_q16;
    int32_t kart_radius_q16;
    int32_t ride_height_q16;
    /* MK8 bump weight stopped, at top speed (lerped by speed ratio) and
     * while any boost runs. */
    int32_t weight_q16[3];
    int32_t ground_normal_cutoff_q30;
    uint32_t respawn_ticks;
    uint32_t respawn_grace_ticks;
    int32_t respawn_lift_q16;
} PortableMeshKartRules;

/* First range and range count owned by a stat. */
void portable_mesh_kart_stat_ranges(unsigned stat, unsigned *first, unsigned *count);
int portable_mesh_kart_profile_valid(const PortableMeshKartProfile *profile);
/* A range's value at a Q16 level in 0..19. */
int32_t portable_mesh_kart_stat_value(const PortableMeshKartStatRange *range, int32_t level_q16);
/* Base levels plus a stock driver's points (0: none) plus extra points,
 * clamped to 0..19. */
void portable_mesh_kart_stat_levels(const PortableMeshKartProfile *profile, uint32_t stock_driver,
                                    const int32_t extra_points_q16[PORTABLE_MESH_KART_STATS],
                                    int32_t out_q16[PORTABLE_MESH_KART_STATS]);
/* 0 on an invalid profile, class (>2) or result; out untouched then. */
int portable_mesh_kart_rules_resolve(const PortableMeshKartProfile *profile, unsigned engine_class,
                                     const int32_t levels_q16[PORTABLE_MESH_KART_STATS],
                                     PortableMeshKartRules *out);
/* A course's kart scale (Q16, META mksc:kart-scale): the collision radius
 * and ride height grow or shrink with it, rounded to nearest. 0 when the
 * scaled rules are invalid; out untouched then. */
int portable_mesh_kart_rules_scale_body(PortableMeshKartRules *rules, int32_t scale_q16);
/* Whole-rules validation shared by content loading and simulation init. */
int portable_mesh_kart_rules_valid(const PortableMeshKartRules *rules);
#endif
