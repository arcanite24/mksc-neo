#include "mesh_kart_rules_asset.h"
#include <stddef.h>
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}
#define KPH_MAGIC UINT32_C(0x3148504b)
int portable_mesh_kart_profile_asset_open(const PortableAssetSectionView *s,
                                          PortableMeshKartProfile *out)
{
    if (!s || !out || !s->bytes || s->kind != 102u || s->version != PORTABLE_MESH_KART_RULES_VERSION ||
        s->size != PORTABLE_MESH_KART_RULES_SIZE)
        return 0;
    const uint8_t *b = s->bytes;
    if (read_u32(b) != KPH_MAGIC || read_u32(b + 4) != PORTABLE_MESH_KART_RULES_VERSION ||
        read_u32(b + 8) != PORTABLE_MESH_KART_RULES_SIZE || read_u32(b + 12) != s->content_id ||
        read_u32(b + 20) || read_u32(b + 24) || read_u32(b + 28))
        return 0;
    PortableMeshKartProfile p = {0};
    p.profile_id = read_u32(b + 12);
    p.revision = read_u32(b + 16);
    /* Every payload word is a 32-bit field of the profile in declaration
     * order; the two identity words precede them. */
    int32_t *words[PORTABLE_MESH_KART_PROFILE_WORDS];
    size_t n = 0;
    for (unsigned i = 0; i < PORTABLE_MESH_KART_RANGES; ++i)
    {
        words[n++] = &p.ranges[i].level0_q16;
        words[n++] = &p.ranges[i].level19_q16;
    }
    for (unsigned i = 0; i < PORTABLE_MESH_KART_STATS; ++i)
        words[n++] = &p.base_level_q16[i];
    for (unsigned d = 0; d < PORTABLE_MESH_KART_STOCK_DRIVERS; ++d)
        for (unsigned i = 0; i < PORTABLE_MESH_KART_STATS; ++i)
            words[n++] = &p.driver_points_q16[d][i];
    for (unsigned i = 0; i < PORTABLE_MESH_KART_CLASSES; ++i)
        words[n++] = &p.class_speed_ratio_q16[i];
    for (unsigned i = 0; i < PORTABLE_MESH_KART_CLASSES; ++i)
        words[n++] = &p.cpu_speed_ratio_q16[i];
    int32_t *const tail[] = {
        &p.handling_response_q16, &p.drift_response_q16,
        &p.minimum_drift_speed_ratio_q16, &p.outside_drift_angle_q16, &p.outside_drift_recovery_q16,
        &p.air_steering_ratio_q16, &p.normal_alignment_response_q16, (int32_t *)&p.panel_boost_ticks,
        &p.panel_cap_ratio_q16, &p.panel_acceleration_q16, &p.hop_velocity_q16,
        &p.terminal_fall_speed_q16, &p.ground_snap_q16, &p.ground_probe_q16, &p.kart_radius_q16,
        &p.ride_height_q16,
        &p.ground_normal_cutoff_q30, (int32_t *)&p.respawn_ticks, (int32_t *)&p.respawn_grace_ticks,
        &p.respawn_lift_q16};
    for (size_t i = 0; i < sizeof tail / sizeof tail[0]; ++i)
        words[n++] = tail[i];
    if (n != PORTABLE_MESH_KART_PROFILE_WORDS)
        return 0;
    for (size_t i = 0; i < n; ++i)
    {
        const uint32_t word = read_u32(b + 32 + 4 * i);
        *words[i] = (int32_t)word;
    }
    if (!portable_mesh_kart_profile_valid(&p))
        return 0;
    *out = p;
    return 1;
}
int portable_mesh_kart_profile_asset_load(const PortableAssetPackView *pack, uint32_t profile_id,
                                          PortableMeshKartProfile *out)
{
    PortableAssetSectionView section;
    return pack && out && profile_id &&
           portable_asset_pack_find_section(pack, 102u, profile_id, &section) ==
               PORTABLE_ASSET_PACK_OK &&
           portable_mesh_kart_profile_asset_open(&section, out);
}
int portable_mesh_kart_rules_asset_load(const PortableAssetPackView *pack, uint32_t profile_id,
                                        PortableMeshKartRules *out)
{
    PortableMeshKartProfile profile;
    int32_t levels[PORTABLE_MESH_KART_STATS];
    if (!portable_mesh_kart_profile_asset_load(pack, profile_id, &profile))
        return 0;
    portable_mesh_kart_stat_levels(&profile, 0, NULL, levels);
    return portable_mesh_kart_rules_resolve(&profile, PORTABLE_MESH_KART_CLASSES - 1u, levels, out);
}
