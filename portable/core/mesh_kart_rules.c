#include "mesh_kart_rules.h"
#include "mesh_kart_mk8.h"
#include "fixed_3d.h"
#define Q PORTABLE_FIXED_3D_ONE

void portable_mesh_kart_stat_ranges(unsigned stat, unsigned *first, unsigned *count)
{
    static const uint8_t table[PORTABLE_MESH_KART_STATS][2] = {
        {PORTABLE_MESH_KART_RANGE_WEIGHT_LOW, 3},
        {PORTABLE_MESH_KART_RANGE_ACCELERATION_LIMIT, 2},
        {PORTABLE_MESH_KART_RANGE_TRACTION_ON, 1},
        {PORTABLE_MESH_KART_RANGE_TRACTION_OFF, 1},
        {PORTABLE_MESH_KART_RANGE_CHARGE_1, 12},
        {PORTABLE_MESH_KART_RANGE_SPEED_GROUND, 1},
        {PORTABLE_MESH_KART_RANGE_SPEED_WATER, 1},
        {PORTABLE_MESH_KART_RANGE_SPEED_AIR, 1},
        {PORTABLE_MESH_KART_RANGE_SPEED_ANTIGRAVITY, 1},
        {PORTABLE_MESH_KART_RANGE_HANDLING_GROUND, 3},
        {PORTABLE_MESH_KART_RANGE_HANDLING_WATER, 3},
        {PORTABLE_MESH_KART_RANGE_HANDLING_AIR, 3},
        {PORTABLE_MESH_KART_RANGE_HANDLING_ANTIGRAVITY, 3}};
    const unsigned s = stat < PORTABLE_MESH_KART_STATS ? stat : 0u;
    if (first)
        *first = table[s][0];
    if (count)
        *count = stat < PORTABLE_MESH_KART_STATS ? table[s][1] : 0u;
}

/* Inclusive bounds of a range's endpoints. */
static void range_bounds(unsigned range, int32_t *low, int32_t *high)
{
    *low = 0;
    *high = Q;
    if (range <= PORTABLE_MESH_KART_RANGE_WEIGHT_BOOST)
        *low = Q, *high = 128 * Q;
    else if (range == PORTABLE_MESH_KART_RANGE_ACCELERATION_LIMIT)
        *high = 1024 * Q;
    else if (range == PORTABLE_MESH_KART_RANGE_ACCELERATION_RATE)
        *low = 1;
    else if (range <= PORTABLE_MESH_KART_RANGE_CHARGE_3 || range == PORTABLE_MESH_KART_RANGE_MT1_TICKS ||
             range == PORTABLE_MESH_KART_RANGE_MT2_TICKS || range == PORTABLE_MESH_KART_RANGE_TRICK_TICKS)
    {
        if (range >= PORTABLE_MESH_KART_RANGE_CHARGE_1)
            *low = Q, *high = 2048 * Q;
    }
    else if (range == PORTABLE_MESH_KART_RANGE_MT1_CAP || range == PORTABLE_MESH_KART_RANGE_MT2_CAP ||
             range == PORTABLE_MESH_KART_RANGE_TRICK_CAP)
        *low = Q, *high = 4 * Q;
    else if (range == PORTABLE_MESH_KART_RANGE_MT1_ACCELERATION ||
             range == PORTABLE_MESH_KART_RANGE_MT2_ACCELERATION ||
             range == PORTABLE_MESH_KART_RANGE_TRICK_ACCELERATION)
        *high = 1024 * Q;
    else if (range <= PORTABLE_MESH_KART_RANGE_SPEED_ANTIGRAVITY)
        *low = Q, *high = 128 * Q;
    else if ((range - PORTABLE_MESH_KART_RANGE_HANDLING_GROUND) % 3u == 2u)
        *high = 16 * Q; /* spin-turn gain */
    else
        *high = Q / 4; /* turn rate, radians a tick */
}

int32_t portable_mesh_kart_stat_value(const PortableMeshKartStatRange *r, int32_t level)
{
    const int64_t clamped = level < 0 ? 0 : level > PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q
                                                ? PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q
                                                : level;
    return (int32_t)(r->level0_q16 + ((int64_t)r->level19_q16 - r->level0_q16) * clamped /
                                         (PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q));
}

static int ordered(const PortableMeshKartProfile *p, unsigned smaller, unsigned larger)
{
    return p->ranges[smaller].level0_q16 < p->ranges[larger].level0_q16 &&
           p->ranges[smaller].level19_q16 < p->ranges[larger].level19_q16;
}

int portable_mesh_kart_profile_valid(const PortableMeshKartProfile *p)
{
    if (!p || !p->profile_id || !p->revision)
        return 0;
    for (unsigned i = 0; i < PORTABLE_MESH_KART_RANGES; ++i)
    {
        int32_t low, high;
        range_bounds(i, &low, &high);
        if (p->ranges[i].level0_q16 < low || p->ranges[i].level0_q16 > high ||
            p->ranges[i].level19_q16 < low || p->ranges[i].level19_q16 > high)
            return 0;
    }
    if (!ordered(p, PORTABLE_MESH_KART_RANGE_CHARGE_1, PORTABLE_MESH_KART_RANGE_CHARGE_2) ||
        !ordered(p, PORTABLE_MESH_KART_RANGE_CHARGE_2, PORTABLE_MESH_KART_RANGE_CHARGE_3) ||
        !ordered(p, PORTABLE_MESH_KART_RANGE_MT1_TICKS, PORTABLE_MESH_KART_RANGE_MT2_TICKS))
        return 0;
    for (unsigned s = 0; s < PORTABLE_MESH_KART_STATS; ++s)
    {
        if (p->base_level_q16[s] < 0 || p->base_level_q16[s] > PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q)
            return 0;
        for (unsigned d = 0; d < PORTABLE_MESH_KART_STOCK_DRIVERS; ++d)
            if (p->driver_points_q16[d][s] < -PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q ||
                p->driver_points_q16[d][s] > PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q)
                return 0;
    }
    for (unsigned c = 0; c < PORTABLE_MESH_KART_CLASSES; ++c)
        if (p->class_speed_ratio_q16[c] < Q / 4 || p->class_speed_ratio_q16[c] > 2 * Q ||
            p->cpu_speed_ratio_q16[c] < Q / 2 || p->cpu_speed_ratio_q16[c] > Q)
            return 0;
    PortableMeshKartRules probe;
    int32_t levels[PORTABLE_MESH_KART_STATS];
    for (unsigned c = 0; c < PORTABLE_MESH_KART_CLASSES; ++c)
        for (int32_t level = 0; level <= PORTABLE_MESH_KART_STAT_LEVEL_MAX; level += PORTABLE_MESH_KART_STAT_LEVEL_MAX)
        {
            for (unsigned s = 0; s < PORTABLE_MESH_KART_STATS; ++s)
                levels[s] = level * Q;
            if (!portable_mesh_kart_rules_resolve_unchecked(p, c, levels, &probe) ||
                !portable_mesh_kart_rules_valid(&probe))
                return 0;
        }
    return 1;
}

void portable_mesh_kart_stat_levels(const PortableMeshKartProfile *p, uint32_t stock_driver,
                                    const int32_t extra[PORTABLE_MESH_KART_STATS],
                                    int32_t out[PORTABLE_MESH_KART_STATS])
{
    for (unsigned s = 0; s < PORTABLE_MESH_KART_STATS; ++s)
    {
        int64_t level = p->base_level_q16[s];
        if (stock_driver >= 1u && stock_driver <= PORTABLE_MESH_KART_STOCK_DRIVERS)
            level += p->driver_points_q16[stock_driver - 1u][s];
        if (extra)
            level += extra[s];
        out[s] = level < 0 ? 0 : level > PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q
                                       ? PORTABLE_MESH_KART_STAT_LEVEL_MAX * Q
                                       : (int32_t)level;
    }
}

/* Ticks MK8's brake takes to stop from speed (its planning average). */
static uint32_t brake_ticks(int32_t speed, int32_t reverse)
{
    int32_t v = speed;
    uint32_t ticks = 0;
    while (v > 0 && ticks < 4096u)
    {
        v = portable_mesh_kart_mk8_drive(v, reverse, MESH_KART_MK8_BRAKE_RATE_Q16,
                                         MESH_KART_MK8_BRAKE_LIMIT_Q16, Q, Q, -1);
        ++ticks;
    }
    return ticks ? ticks : 1u;
}

int portable_mesh_kart_rules_resolve_unchecked(const PortableMeshKartProfile *p, unsigned engine_class,
                                               const int32_t levels[PORTABLE_MESH_KART_STATS],
                                               PortableMeshKartRules *out)
{
    if (!p || !levels || !out || engine_class >= PORTABLE_MESH_KART_CLASSES)
        return 0;
#define VALUE(range, stat) \
    portable_mesh_kart_stat_value(&p->ranges[PORTABLE_MESH_KART_RANGE_##range], \
                                  levels[PORTABLE_MESH_KART_STAT_##stat])
    PortableMeshKartRules r = {0};
    r.profile_id = p->profile_id;
    r.revision = p->revision;
    const int64_t top = (int64_t)VALUE(SPEED_GROUND, SPEED_GROUND) *
                        p->class_speed_ratio_q16[engine_class] / Q;
    if (top < 1 || top > 128 * Q)
        return 0;
    r.forward_speed_q16 = (int32_t)top;
    r.cpu_pace_ratio_q16 = Q;
    r.reverse_speed_q16 = MESH_KART_MK8_REVERSE_Q16;
    r.acceleration_limit_q16 = VALUE(ACCELERATION_LIMIT, ACCELERATION);
    r.acceleration_rate_q16 = VALUE(ACCELERATION_RATE, ACCELERATION);
    /* Coast: speed times 0.98 a tick halves in 34.3 ticks (0.5744 s). */
    r.coast_deceleration_q16 = (int32_t)(top * 57049 / Q);
    r.brake_deceleration_q16 =
        (int32_t)(top * 262144 / ((int64_t)brake_ticks(r.forward_speed_q16, r.reverse_speed_q16) * 4389));
    r.handling_response_q16 = p->handling_response_q16;
    /* Handling rows hold MK8's turn rates in radians a tick at 7+ units/frame. */
    r.handling_turn_rate_q16 = (int32_t)((int64_t)VALUE(HANDLING_GROUND, HANDLING_GROUND) *
                                         MESH_KART_MK8_TURNS_PER_SECOND_Q16 / Q);
    r.drift_response_q16 = p->drift_response_q16;
    r.drift_turn_rate_q16 = (int32_t)((int64_t)VALUE(HANDLING_GROUND_DRIFT, HANDLING_GROUND) *
                                      MESH_KART_MK8_TURNS_PER_SECOND_Q16 / Q);
    r.spin_gain_q16 = VALUE(HANDLING_GROUND_SPIN, HANDLING_GROUND);
    const int64_t anti = (int64_t)VALUE(SPEED_ANTIGRAVITY, SPEED_ANTIGRAVITY) *
                         p->class_speed_ratio_q16[engine_class] / Q;
    if (anti < 1 || anti > 128 * Q)
        return 0;
    r.antigravity_speed_q16 = (int32_t)anti;
    const int64_t glide = (int64_t)VALUE(SPEED_AIR, SPEED_AIR) * p->class_speed_ratio_q16[engine_class] / Q;
    if (glide < 1 || glide > 128 * Q)
        return 0;
    r.glide_speed_q16 = (int32_t)glide;
    {
        /* The anti-gravity handling trio: normal, drift, spin-turn gain. */
        const PortableMeshKartStatRange *h = &p->ranges[PORTABLE_MESH_KART_RANGE_HANDLING_ANTIGRAVITY];
        const int32_t level = levels[PORTABLE_MESH_KART_STAT_HANDLING_ANTIGRAVITY];
        r.antigravity_turn_rate_q16 =
            (int32_t)((int64_t)portable_mesh_kart_stat_value(&h[0], level) * MESH_KART_MK8_TURNS_PER_SECOND_Q16 / Q);
        r.antigravity_drift_turn_rate_q16 =
            (int32_t)((int64_t)portable_mesh_kart_stat_value(&h[1], level) * MESH_KART_MK8_TURNS_PER_SECOND_Q16 / Q);
        r.antigravity_spin_gain_q16 = portable_mesh_kart_stat_value(&h[2], level);
    }
    r.traction_on_q16 = VALUE(TRACTION_ON, TRACTION_ON);
    r.traction_off_q16 = VALUE(TRACTION_OFF, TRACTION_OFF);
    {
        const int32_t level = levels[PORTABLE_MESH_KART_STAT_TRACTION_OFF];
        r.traction_level_q16 = (level < 0 ? 0 : level > 19 * Q ? 19 * Q : level) / 19;
    }
    r.minimum_drift_speed_ratio_q16 = p->minimum_drift_speed_ratio_q16;
    r.outside_drift_angle_q16 = p->outside_drift_angle_q16;
    r.outside_drift_recovery_q16 = p->outside_drift_recovery_q16;
    r.air_steering_ratio_q16 = p->air_steering_ratio_q16;
    r.normal_alignment_response_q16 = p->normal_alignment_response_q16;
    r.charge_thresholds[0] = (uint32_t)(VALUE(CHARGE_1, MINI_TURBO) / Q);
    r.charge_thresholds[1] = (uint32_t)(VALUE(CHARGE_2, MINI_TURBO) / Q);
    r.charge_thresholds[2] = (uint32_t)(VALUE(CHARGE_3, MINI_TURBO) / Q);
    r.neutral_charge_rate = MESH_KART_MK8_CHARGE_NEUTRAL;
    r.inward_charge_rate = MESH_KART_MK8_CHARGE_INWARD;
    const uint32_t mt1 = (uint32_t)(VALUE(MT1_TICKS, MINI_TURBO) / Q),
                   mt2 = (uint32_t)(VALUE(MT2_TICKS, MINI_TURBO) / Q);
    r.boost_ticks[0] = mt1;
    r.boost_ticks[1] = mt2;
    r.boost_ticks[2] = 2u * mt2 > mt1 ? 2u * mt2 - mt1 : mt2; /* MK8: MT3 = 2*MT2 - MT1 */
    r.boost_cap_ratio_q16[0] = VALUE(MT1_CAP, MINI_TURBO);
    r.boost_cap_ratio_q16[1] = r.boost_cap_ratio_q16[2] = VALUE(MT2_CAP, MINI_TURBO);
    r.boost_acceleration_q16[0] = VALUE(MT1_ACCELERATION, MINI_TURBO);
    r.boost_acceleration_q16[1] = r.boost_acceleration_q16[2] = VALUE(MT2_ACCELERATION, MINI_TURBO);
    r.trick_ticks = (uint32_t)(VALUE(TRICK_TICKS, MINI_TURBO) / Q);
    r.trick_cap_ratio_q16 = VALUE(TRICK_CAP, MINI_TURBO);
    r.trick_acceleration_q16 = VALUE(TRICK_ACCELERATION, MINI_TURBO);
    r.panel_boost_ticks = p->panel_boost_ticks;
    r.panel_cap_ratio_q16 = p->panel_cap_ratio_q16;
    r.panel_acceleration_q16 = p->panel_acceleration_q16;
    r.hop_velocity_q16 = p->hop_velocity_q16;
    r.terminal_fall_speed_q16 = p->terminal_fall_speed_q16;
    r.ground_snap_q16 = p->ground_snap_q16;
    r.ground_probe_q16 = p->ground_probe_q16;
    r.kart_radius_q16 = p->kart_radius_q16;
    r.ride_height_q16 = p->ride_height_q16;
    r.weight_q16[0] = VALUE(WEIGHT_LOW, WEIGHT);
    r.weight_q16[1] = VALUE(WEIGHT_HIGH, WEIGHT);
    r.weight_q16[2] = VALUE(WEIGHT_BOOST, WEIGHT);
    r.ground_normal_cutoff_q30 = p->ground_normal_cutoff_q30;
    r.respawn_ticks = p->respawn_ticks;
    r.respawn_grace_ticks = p->respawn_grace_ticks;
    r.respawn_lift_q16 = p->respawn_lift_q16;
#undef VALUE
    *out = r;
    return 1;
}

int portable_mesh_kart_rules_resolve(const PortableMeshKartProfile *p, unsigned engine_class,
                                     const int32_t levels[PORTABLE_MESH_KART_STATS],
                                     PortableMeshKartRules *out)
{
    PortableMeshKartRules r;
    if (!portable_mesh_kart_profile_valid(p) ||
        !portable_mesh_kart_rules_resolve_unchecked(p, engine_class, levels, &r) ||
        !portable_mesh_kart_rules_valid(&r))
        return 0;
    *out = r;
    return 1;
}

int portable_mesh_kart_rules_scale_body(PortableMeshKartRules *rules, int32_t scale_q16)
{
    if (!rules || scale_q16 <= 0)
        return 0;
    PortableMeshKartRules r = *rules;
    r.kart_radius_q16 = (int32_t)(((int64_t)r.kart_radius_q16 * scale_q16 + 32768) >> 16);
    r.ride_height_q16 = (int32_t)(((int64_t)r.ride_height_q16 * scale_q16 + 32768) >> 16);
    if (!portable_mesh_kart_rules_valid(&r))
        return 0;
    *rules = r;
    return 1;
}

int portable_mesh_kart_rules_valid(const PortableMeshKartRules *r)
{
    if (!r || !r->profile_id || !r->revision)
        return 0;
    if (r->forward_speed_q16 < 1 || r->forward_speed_q16 > 16777216 ||
        r->cpu_pace_ratio_q16 < Q / 2 || r->cpu_pace_ratio_q16 > Q)
        return 0;
    if (r->reverse_speed_q16 < 1 || r->reverse_speed_q16 > 16777216)
        return 0;
    if (r->acceleration_limit_q16 < 0 || r->acceleration_limit_q16 > 67108864)
        return 0;
    if (r->acceleration_rate_q16 < 1 || r->acceleration_rate_q16 > Q)
        return 0;
    if (r->coast_deceleration_q16 < 0 || r->coast_deceleration_q16 > 67108864)
        return 0;
    if (r->brake_deceleration_q16 < 0 || r->brake_deceleration_q16 > 67108864)
        return 0;
    if (r->handling_response_q16 < 0 || r->handling_response_q16 > 4194304)
        return 0;
    if (r->handling_turn_rate_q16 < 0 || r->handling_turn_rate_q16 > 262144)
        return 0;
    if (r->drift_response_q16 < 0 || r->drift_response_q16 > 4194304)
        return 0;
    if (r->drift_turn_rate_q16 < 0 || r->drift_turn_rate_q16 > 262144)
        return 0;
    if (r->spin_gain_q16 < 0 || r->spin_gain_q16 > 16 * Q)
        return 0;
    if (r->antigravity_speed_q16 < 1 || r->antigravity_speed_q16 > 128 * Q ||
        r->antigravity_turn_rate_q16 < 0 || r->antigravity_turn_rate_q16 > 262144 ||
        r->antigravity_drift_turn_rate_q16 < 0 || r->antigravity_drift_turn_rate_q16 > 262144 ||
        r->antigravity_spin_gain_q16 < 0 || r->antigravity_spin_gain_q16 > 16 * Q ||
        r->glide_speed_q16 < 1 || r->glide_speed_q16 > 128 * Q)
        return 0;
    if (r->traction_on_q16 < 0 || r->traction_on_q16 > Q || r->traction_off_q16 < 0 ||
        r->traction_off_q16 > Q || r->traction_level_q16 < 0 || r->traction_level_q16 > Q)
        return 0;
    if (r->minimum_drift_speed_ratio_q16 < 1 || r->minimum_drift_speed_ratio_q16 > Q)
        return 0;
    if (r->outside_drift_angle_q16 < 0 || r->outside_drift_angle_q16 > 16384)
        return 0;
    if (r->outside_drift_recovery_q16 < 0 || r->outside_drift_recovery_q16 > 4194304)
        return 0;
    if (r->air_steering_ratio_q16 < 0 || r->air_steering_ratio_q16 > Q)
        return 0;
    if (r->normal_alignment_response_q16 < 0 || r->normal_alignment_response_q16 > 4194304)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (r->charge_thresholds[i] < 1 || r->charge_thresholds[i] > 8192)
            return 0;
    if (r->neutral_charge_rate < 1 || r->neutral_charge_rate > 64)
        return 0;
    if (r->inward_charge_rate < 1 || r->inward_charge_rate > 64)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (r->boost_ticks[i] < 1 || r->boost_ticks[i] > 8192)
            return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (r->boost_cap_ratio_q16[i] < Q || r->boost_cap_ratio_q16[i] > 4 * Q)
            return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (r->boost_acceleration_q16[i] < 0 || r->boost_acceleration_q16[i] > 67108864)
            return 0;
    if (r->panel_boost_ticks < 1 || r->panel_boost_ticks > 8192)
        return 0;
    if (r->trick_ticks < 1 || r->trick_ticks > 255 || r->trick_cap_ratio_q16 < Q || r->trick_cap_ratio_q16 > 4 * Q ||
        r->trick_acceleration_q16 < 0 || r->trick_acceleration_q16 > 67108864)
        return 0;
    if (r->panel_cap_ratio_q16 < Q || r->panel_cap_ratio_q16 > 4 * Q)
        return 0;
    if (r->panel_acceleration_q16 < 0 || r->panel_acceleration_q16 > 67108864)
        return 0;
    if (r->hop_velocity_q16 < 0 || r->hop_velocity_q16 > 16777216)
        return 0;
    if (r->terminal_fall_speed_q16 < 1 || r->terminal_fall_speed_q16 > 16777216)
        return 0;
    if (r->ground_snap_q16 < 0 || r->ground_snap_q16 > 524288)
        return 0;
    if (r->ground_probe_q16 < 1 || r->ground_probe_q16 > 524288)
        return 0;
    if (r->kart_radius_q16 < 3277 || r->kart_radius_q16 > 524288)
        return 0;
    if (r->ride_height_q16 < 3277 || r->ride_height_q16 > 524288)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (r->weight_q16[i] < Q || r->weight_q16[i] > 128 * Q)
            return 0;
    if (r->ground_normal_cutoff_q30 < 1 || r->ground_normal_cutoff_q30 > 1073741824)
        return 0;
    if (r->respawn_ticks < 1 || r->respawn_ticks > 8192)
        return 0;
    if (r->respawn_grace_ticks > 8192)
        return 0;
    if (r->respawn_lift_q16 < 0 || r->respawn_lift_q16 > 524288)
        return 0;
    if (r->ground_snap_q16 > r->ground_probe_q16 || r->neutral_charge_rate > r->inward_charge_rate)
        return 0;
    for (unsigned i = 1; i < 3u; ++i)
        if (r->charge_thresholds[i - 1] >= r->charge_thresholds[i])
            return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if ((int64_t)r->forward_speed_q16 * r->boost_cap_ratio_q16[i] > INT64_C(16777216) * Q)
            return 0;
    if ((int64_t)r->forward_speed_q16 * r->panel_cap_ratio_q16 > INT64_C(16777216) * Q)
        return 0;
    return 1;
}
