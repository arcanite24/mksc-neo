#ifndef PORTABLE_TEST_MESH_COURSE_FIXTURE_H
#define PORTABLE_TEST_MESH_COURSE_FIXTURE_H
#include "mesh_time_trial.h"
#include <string.h>
/* Authored metre-scale strip: grid at Y=4, intermediate gate Y=0, finish Y=-4. */
typedef struct
{
    PortableCourse3DView course;
    PortableMeshKartRules rules;
    uint8_t meta[128], grid[32], vertices[48], triangles[64], surface[48], gates[128], edges[8];
} PortableTestMeshCourse;
static inline void test_mesh_word(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(n >> (8u * i));
}
/* A level-independent test profile (every range constant). */
static inline PortableMeshKartProfile test_mesh_profile(void)
{
    PortableMeshKartProfile p;
    memset(&p, 0, sizeof(p));
    p.profile_id = 1;
    p.revision = 1;
    const int32_t values[PORTABLE_MESH_KART_RANGES] = {
        [PORTABLE_MESH_KART_RANGE_WEIGHT_LOW] = 65536, [PORTABLE_MESH_KART_RANGE_WEIGHT_HIGH] = 65536,
        [PORTABLE_MESH_KART_RANGE_WEIGHT_BOOST] = 65536,
        [PORTABLE_MESH_KART_RANGE_ACCELERATION_LIMIT] = 10 * 65536,
        [PORTABLE_MESH_KART_RANGE_ACCELERATION_RATE] = 3277,
        [PORTABLE_MESH_KART_RANGE_TRACTION_ON] = 49152, [PORTABLE_MESH_KART_RANGE_TRACTION_OFF] = 61440,
        [PORTABLE_MESH_KART_RANGE_CHARGE_1] = 3 * 65536, [PORTABLE_MESH_KART_RANGE_CHARGE_2] = 6 * 65536,
        [PORTABLE_MESH_KART_RANGE_CHARGE_3] = 9 * 65536,
        [PORTABLE_MESH_KART_RANGE_MT1_TICKS] = 4 * 65536, [PORTABLE_MESH_KART_RANGE_MT1_CAP] = 65536,
        [PORTABLE_MESH_KART_RANGE_MT1_ACCELERATION] = 2 * 65536,
        [PORTABLE_MESH_KART_RANGE_MT2_TICKS] = 5 * 65536, [PORTABLE_MESH_KART_RANGE_MT2_CAP] = 81920,
        [PORTABLE_MESH_KART_RANGE_MT2_ACCELERATION] = 3 * 65536,
        [PORTABLE_MESH_KART_RANGE_TRICK_TICKS] = 4 * 65536, [PORTABLE_MESH_KART_RANGE_TRICK_CAP] = 81920,
        [PORTABLE_MESH_KART_RANGE_TRICK_ACCELERATION] = 3 * 65536,
        [PORTABLE_MESH_KART_RANGE_SPEED_GROUND] = 10 * 65536, [PORTABLE_MESH_KART_RANGE_SPEED_WATER] = 10 * 65536,
        [PORTABLE_MESH_KART_RANGE_SPEED_AIR] = 10 * 65536,
        [PORTABLE_MESH_KART_RANGE_SPEED_ANTIGRAVITY] = 10 * 65536};
    for (unsigned i = 0; i < PORTABLE_MESH_KART_RANGES; ++i)
    {
        int32_t v = values[i];
        if (i >= PORTABLE_MESH_KART_RANGE_HANDLING_GROUND)
            v = (i - PORTABLE_MESH_KART_RANGE_HANDLING_GROUND) % 3u == 0u ? 655
                : (i - PORTABLE_MESH_KART_RANGE_HANDLING_GROUND) % 3u == 1u ? 1311 : 65536;
        p.ranges[i].level0_q16 = p.ranges[i].level19_q16 = v;
    }
    for (unsigned s = 0; s < PORTABLE_MESH_KART_STATS; ++s)
        p.base_level_q16[s] = 9 * 65536;
    p.class_speed_ratio_q16[0] = 52429;
    p.class_speed_ratio_q16[1] = 58982;
    p.class_speed_ratio_q16[2] = 65536;
    p.cpu_speed_ratio_q16[0] = p.cpu_speed_ratio_q16[1] = p.cpu_speed_ratio_q16[2] = 65536;
    p.handling_response_q16 = p.drift_response_q16 = 64 * 65536;
    p.minimum_drift_speed_ratio_q16 = 16384;
    p.outside_drift_angle_q16 = 4096;
    p.outside_drift_recovery_q16 = 8192;
    p.air_steering_ratio_q16 = 16384;
    p.normal_alignment_response_q16 = 64 * 65536;
    p.panel_boost_ticks = 5;
    p.panel_cap_ratio_q16 = 2 * 65536;
    p.panel_acceleration_q16 = 30 * 65536;
    p.hop_velocity_q16 = 3 * 65536;
    p.terminal_fall_speed_q16 = 50 * 65536;
    p.ground_snap_q16 = 3277;
    p.ground_probe_q16 = 65536;
    p.kart_radius_q16 = 32768;
    p.ride_height_q16 = 16384;
    p.ground_normal_cutoff_q30 = 536870912;
    p.respawn_ticks = 5;
    p.respawn_grace_ticks = 3;
    p.respawn_lift_q16 = 65536;
    return p;
}
static inline PortableMeshKartRules test_mesh_rules(void)
{
    const PortableMeshKartProfile p = test_mesh_profile();
    int32_t levels[PORTABLE_MESH_KART_STATS];
    PortableMeshKartRules r;
    memset(&r, 0, sizeof(r));
    portable_mesh_kart_stat_levels(&p, 0, NULL, levels);
    (void)portable_mesh_kart_rules_resolve(&p, 2, levels, &r);
    return r;
}
/* A profile's KPH2 bytes in the public wire order (header, then each field in
 * declaration order); no struct casts. */
static inline void test_mesh_profile_payload(const PortableMeshKartProfile *p,
                                             uint8_t out[PORTABLE_MESH_KART_RULES_SIZE])
{
    uint32_t words[PORTABLE_MESH_KART_RULES_SIZE / 4u];
    unsigned n = 0;
    const uint32_t header[8] = {0x3148504bu, PORTABLE_MESH_KART_RULES_VERSION, PORTABLE_MESH_KART_RULES_SIZE,
                                p->profile_id, p->revision, 0, 0, 0};
    for (unsigned i = 0; i < 8u; ++i)
        words[n++] = header[i];
    for (unsigned i = 0; i < PORTABLE_MESH_KART_RANGES; ++i)
    {
        words[n++] = (uint32_t)p->ranges[i].level0_q16;
        words[n++] = (uint32_t)p->ranges[i].level19_q16;
    }
    for (unsigned i = 0; i < PORTABLE_MESH_KART_STATS; ++i)
        words[n++] = (uint32_t)p->base_level_q16[i];
    for (unsigned d = 0; d < PORTABLE_MESH_KART_STOCK_DRIVERS; ++d)
        for (unsigned i = 0; i < PORTABLE_MESH_KART_STATS; ++i)
            words[n++] = (uint32_t)p->driver_points_q16[d][i];
    for (unsigned i = 0; i < PORTABLE_MESH_KART_CLASSES; ++i)
        words[n++] = (uint32_t)p->class_speed_ratio_q16[i];
    for (unsigned i = 0; i < PORTABLE_MESH_KART_CLASSES; ++i)
        words[n++] = (uint32_t)p->cpu_speed_ratio_q16[i];
    const int32_t tail[] = {p->handling_response_q16, p->drift_response_q16,
        p->minimum_drift_speed_ratio_q16, p->outside_drift_angle_q16, p->outside_drift_recovery_q16,
        p->air_steering_ratio_q16, p->normal_alignment_response_q16, (int32_t)p->panel_boost_ticks,
        p->panel_cap_ratio_q16, p->panel_acceleration_q16, p->hop_velocity_q16,
        p->terminal_fall_speed_q16, p->ground_snap_q16, p->ground_probe_q16, p->kart_radius_q16,
        p->ride_height_q16, p->ground_normal_cutoff_q30,
        (int32_t)p->respawn_ticks, (int32_t)p->respawn_grace_ticks, p->respawn_lift_q16};
    for (unsigned i = 0; i < sizeof tail / sizeof tail[0]; ++i)
        words[n++] = (uint32_t)tail[i];
    for (unsigned i = 0; i < n; ++i)
        test_mesh_word(out + 4u * i, words[i]);
}
static inline void test_mesh_course_init(PortableTestMeshCourse *f)
{
    memset(f, 0, sizeof(*f));
    f->rules = test_mesh_rules();
    f->course.id = 65536;
    test_mesh_word(f->meta + 12u, 2);
    test_mesh_word(f->meta + 16u, 1);
    test_mesh_word(f->meta + 84u, 10u * 65536u);
    test_mesh_word(f->meta + 88u, 1);
    test_mesh_word(f->grid + 4u, 4u * 65536u);
    test_mesh_word(f->grid + 24u, 1073741824);
    const int32_t xyz[4][3] = {{-20 * 65536, -20 * 65536, 0},
                               {20 * 65536, -20 * 65536, 0},
                               {20 * 65536, 20 * 65536, 0},
                               {-20 * 65536, 20 * 65536, 0}};
    for (unsigned i = 0; i < 4u; ++i)
        for (unsigned a = 0; a < 3u; ++a)
            test_mesh_word(f->vertices + i * 12u + a * 4u, (uint32_t)xyz[i][a]);
    const uint32_t indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned i = 0; i < 2u; ++i)
    {
        for (unsigned a = 0; a < 3u; ++a)
            test_mesh_word(f->triangles + i * 32u + a * 4u, indices[i][a]);
        test_mesh_word(f->triangles + i * 32u + 20u, UINT32_MAX);
        test_mesh_word(f->triangles + i * 32u + 24u, UINT32_MAX);
        test_mesh_word(f->triangles + i * 32u + 28u, UINT32_MAX);
    }
    test_mesh_word(f->surface + 8u, 65536);
    test_mesh_word(f->surface + 12u, 65536);
    test_mesh_word(f->surface + 32u, 65536);
    for (unsigned i = 0; i < 2u; ++i)
    {
        uint8_t *g = f->gates + i * 64u;
        test_mesh_word(g + 4u, i ? 0u : (uint32_t)(-4 * 65536));
        test_mesh_word(g + 16u, (uint32_t)-1073741824);
        test_mesh_word(g + 24u, 1073741824);
        test_mesh_word(g + 36u, 5u * 65536u);
        test_mesh_word(g + 40u, 3u * 65536u);
        test_mesh_word(g + 44u, i);
        test_mesh_word(g + 48u, i);
        test_mesh_word(g + 60u, i ? 4u : 5u);
        test_mesh_word(g + 52u, i);
        test_mesh_word(g + 56u, 1);
    }
    test_mesh_word(f->edges, 1);
    test_mesh_word(f->edges + 4u, 0);
#define TABLE(tag, data, count, stride)                                                            \
    f->course.tables[f->course.table_count++] = (PortableCourse3DTable){                           \
        PORTABLE_COURSE_3D_TAG(tag[0], tag[1], tag[2], tag[3]), count, stride, f->data}
    TABLE("META", meta, 1, 128);
    TABLE("GRID", grid, 1, 32);
    TABLE("COLV", vertices, 4, 12);
    TABLE("COLT", triangles, 2, 32);
    TABLE("SURF", surface, 1, 48);
    TABLE("CPNT", gates, 2, 64);
    TABLE("CPED", edges, 2, 4);
#undef TABLE
}
#endif
