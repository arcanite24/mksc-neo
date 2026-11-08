#include "mesh_kart_rules_asset.h"
#include "mesh_course_fixture.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh profile line %d: %s\n", __LINE__, #c);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void write_word(uint8_t *bytes, unsigned word, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
        bytes[word * 4u + i] = (uint8_t)(value >> (i * 8u));
}
static uint32_t read_word(const uint8_t *bytes, unsigned word)
{
    return (uint32_t)bytes[word * 4u] | (uint32_t)bytes[word * 4u + 1u] << 8 |
           (uint32_t)bytes[word * 4u + 2u] << 16 | (uint32_t)bytes[word * 4u + 3u] << 24;
}

int main(void)
{
    /* A profile whose stats vary between levels 0 and 19. */
    PortableMeshKartProfile authored = test_mesh_profile();
    authored.profile_id = 37;
    authored.revision = 4;
    authored.ranges[PORTABLE_MESH_KART_RANGE_SPEED_GROUND] = (PortableMeshKartStatRange){19 * 65536, 38 * 65536};
    authored.ranges[PORTABLE_MESH_KART_RANGE_ACCELERATION_LIMIT] = (PortableMeshKartStatRange){0, 19 * 65536};
    authored.ranges[PORTABLE_MESH_KART_RANGE_WEIGHT_LOW] = (PortableMeshKartStatRange){5 * 65536, 50 * 65536};
    authored.ranges[PORTABLE_MESH_KART_RANGE_MT1_TICKS] = (PortableMeshKartStatRange){21 * 65536, 40 * 65536};
    authored.ranges[PORTABLE_MESH_KART_RANGE_MT2_TICKS] = (PortableMeshKartStatRange){72 * 65536, 110 * 65536};
    authored.base_level_q16[PORTABLE_MESH_KART_STAT_SPEED_GROUND] = 10 * 65536;
    authored.driver_points_q16[7][PORTABLE_MESH_KART_STAT_SPEED_GROUND] = 3 * 65536;
    authored.driver_points_q16[2][PORTABLE_MESH_KART_STAT_SPEED_GROUND] = -15 * 65536;
    uint8_t bytes[PORTABLE_MESH_KART_RULES_SIZE];
    test_mesh_profile_payload(&authored, bytes);
    CHECK(read_word(bytes, 0) == 0x3148504bu && read_word(bytes, 1) == 2u && read_word(bytes, 2) == 884u);
    PortableAssetSectionView section = {0};
    section.kind = 102u;
    section.content_id = 37u;
    section.version = PORTABLE_MESH_KART_RULES_VERSION;
    section.size = sizeof(bytes);
    section.bytes = bytes;
    PortableMeshKartProfile profile = {0};
    CHECK(portable_mesh_kart_profile_asset_open(&section, &profile));
    CHECK(!memcmp(&profile, &authored, sizeof profile));

    /* Base levels, stock-driver points (clamped) and class ratios. */
    int32_t levels[PORTABLE_MESH_KART_STATS];
    PortableMeshKartRules rules;
    portable_mesh_kart_stat_levels(&profile, 0, NULL, levels);
    CHECK(levels[PORTABLE_MESH_KART_STAT_SPEED_GROUND] == 10 * 65536);
    CHECK(portable_mesh_kart_rules_resolve(&profile, 2, levels, &rules));
    CHECK(rules.profile_id == 37u && rules.revision == 4u && rules.forward_speed_q16 == 29 * 65536);
    CHECK(rules.acceleration_limit_q16 == 9 * 65536 && rules.weight_q16[0] == 5 * 65536 + 45 * 65536 * 9 / 19);
    /* Platform kart scale shrinks the collision body only. */
    PortableMeshKartRules scaled = rules;
    CHECK(portable_mesh_kart_rules_scale_body(&scaled, 40960));
    CHECK(scaled.kart_radius_q16 == (int32_t)(((int64_t)rules.kart_radius_q16 * 40960 + 32768) >> 16));
    CHECK(scaled.ride_height_q16 == (int32_t)(((int64_t)rules.ride_height_q16 * 40960 + 32768) >> 16));
    CHECK(scaled.forward_speed_q16 == rules.forward_speed_q16);
    scaled.kart_radius_q16 = rules.kart_radius_q16;
    scaled.ride_height_q16 = rules.ride_height_q16;
    CHECK(!memcmp(&scaled, &rules, sizeof rules));
    CHECK(!portable_mesh_kart_rules_scale_body(&scaled, 0) && !memcmp(&scaled, &rules, sizeof rules));
    CHECK(rules.boost_ticks[0] == 30u && rules.boost_ticks[1] == 90u && rules.boost_ticks[2] == 150u);
    CHECK(rules.reverse_speed_q16 == 824060 && rules.neutral_charge_rate == 2u && rules.inward_charge_rate == 5u);
    portable_mesh_kart_stat_levels(&profile, 8, NULL, levels);
    CHECK(levels[PORTABLE_MESH_KART_STAT_SPEED_GROUND] == 13 * 65536);
    portable_mesh_kart_stat_levels(&profile, 3, NULL, levels);
    CHECK(levels[PORTABLE_MESH_KART_STAT_SPEED_GROUND] == 0);
    const int32_t extra[PORTABLE_MESH_KART_STATS] = {[PORTABLE_MESH_KART_STAT_SPEED_GROUND] = 30 * 65536};
    portable_mesh_kart_stat_levels(&profile, 0, extra, levels);
    CHECK(levels[PORTABLE_MESH_KART_STAT_SPEED_GROUND] == 19 * 65536);
    CHECK(portable_mesh_kart_rules_resolve(&profile, 0, levels, &rules) &&
          rules.forward_speed_q16 == 38 * 52429);
    CHECK(!portable_mesh_kart_rules_resolve(&profile, 3, levels, &rules));

    /* Header, range bounds, ordering and cross-field corruptions. */
    PortableMeshKartProfile untouched = profile;
    const unsigned speed = 8u + 2u * PORTABLE_MESH_KART_RANGE_SPEED_GROUND,
                   charge = 8u + 2u * PORTABLE_MESH_KART_RANGE_CHARGE_2,
                   base = 8u + 2u * PORTABLE_MESH_KART_RANGES,
                   classes = base + PORTABLE_MESH_KART_STATS * (1u + PORTABLE_MESH_KART_STOCK_DRIVERS),
                   tail = classes + 2u * PORTABLE_MESH_KART_CLASSES;
    const struct
    {
        unsigned word;
        uint32_t invalid;
    } corruptions[] = {{0, 0}, {1, 1}, {2, 252}, {3, 0}, {4, 0}, {5, 1}, {6, 1}, {7, 1},
                       {speed, 0}, {speed + 1, 0x80000000u}, {charge, 3 * 65536},
                       {base, 20 * 65536}, {base + 20, 0xff000000u}, {classes, 1}, {classes + PORTABLE_MESH_KART_CLASSES, 65537},
                       {tail + 11, 0}, {tail + 13, 0}, {tail + 17, 0}};
    for (unsigned i = 0; i < sizeof(corruptions) / sizeof(corruptions[0]); ++i)
    {
        const unsigned word = corruptions[i].word;
        const uint32_t kept = read_word(bytes, word);
        write_word(bytes, word, corruptions[i].invalid);
        CHECK(!portable_mesh_kart_profile_asset_open(&section, &profile));
        CHECK(!memcmp(&profile, &untouched, sizeof(profile)));
        write_word(bytes, word, kept);
    }
    CHECK(portable_mesh_kart_profile_asset_open(&section, &profile));
    PortableMeshKartRules boundary = test_mesh_rules();
    boundary.respawn_grace_ticks = 0;
    /* The query subtracts radius/ride height before testing its gap. A small
     * contact reserve is valid independently of the body center clearance. */
    boundary.ground_snap_q16 = 655;
    boundary.ground_probe_q16 = 6554;
    CHECK(portable_mesh_kart_rules_valid(&boundary));
    boundary.ground_probe_q16 = boundary.ground_snap_q16;
    CHECK(portable_mesh_kart_rules_valid(&boundary));
    boundary.ground_probe_q16 -= 1;
    CHECK(!portable_mesh_kart_rules_valid(&boundary));
    section.kind = 101u;
    CHECK(!portable_mesh_kart_profile_asset_open(&section, &profile));
    section.kind = 102u;
    section.content_id = 38u;
    CHECK(!portable_mesh_kart_profile_asset_open(&section, &profile));
    section.content_id = 37u;
    section.version = 1u;
    CHECK(!portable_mesh_kart_profile_asset_open(&section, &profile));
    section.version = PORTABLE_MESH_KART_RULES_VERSION;
    section.size = sizeof(bytes) - 1u;
    CHECK(!portable_mesh_kart_profile_asset_open(&section, &profile));
    CHECK(!portable_mesh_kart_profile_asset_open(NULL, &profile));
    CHECK(!portable_mesh_kart_profile_asset_open(&section, NULL));
    puts("Mesh kart profile: PASS");
    return 0;
}
