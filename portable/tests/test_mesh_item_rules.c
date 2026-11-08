#include "mesh_item_rules_asset.h"
#include "mesh_item_fixture.h"
#include "course_3d.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define TAG(s) PORTABLE_COURSE_3D_TAG((s)[0], (s)[1], (s)[2], (s)[3])
int main(void)
{
    _Static_assert(PORTABLE_MESH_ITEM_SCALAR_WORDS == 48, "MIR1 scalar count");
    _Static_assert(PORTABLE_MESH_ITEM_RULES_BYTES == 1396, "MIR1 exact bytes");
    uint8_t bytes[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(bytes);
    PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes};
    PortableMeshItemRules rules, before;
    memset(&rules, 0xa5, sizeof rules);
    before = rules;
    CHECK(portable_mesh_item_rules_asset_open(&section, &rules));
    CHECK(rules.profile_id == 41u && rules.revision == 7u && rules.actors_capacity == 64 &&
          rules.shell_speed_q16 == 12 * 65536 && rules.roulette.preview[9] == 0x100au &&
          rules.roulette.timeline[26] == 2u && rules.player_weights[0][0].weight == 65535u &&
          rules.cpu_weights[7][0].item == 0x1007u);
    for (unsigned i = 0; i < 48u; ++i)
    {
        uint8_t saved[4];
        memcpy(saved, bytes + 32u + i * 4u, 4u);
        test_mir_word(bytes + 32u + i * 4u, UINT32_MAX);
        rules = before;
        CHECK(!portable_mesh_item_rules_asset_open(&section, &rules) &&
              !memcmp(&rules, &before, sizeof rules));
        memcpy(bytes + 32u + i * 4u, saved, 4u);
    }
    const unsigned invalid_offsets[] = {0, 4, 8, 12, 20, 24, 28, 224};
    for (unsigned i = 0; i < sizeof invalid_offsets / sizeof invalid_offsets[0]; ++i)
    {
        uint8_t saved[4];
        memcpy(saved, bytes + invalid_offsets[i], 4u);
        test_mir_word(bytes + invalid_offsets[i], 0x100du);
        rules = before;
        CHECK(!portable_mesh_item_rules_asset_open(&section, &rules) &&
              !memcmp(&rules, &before, sizeof rules));
        memcpy(bytes + invalid_offsets[i], saved, 4u);
    }
    test_mir_word(bytes + 16u, 0u);
    rules = before;
    CHECK(!portable_mesh_item_rules_asset_open(&section, &rules) &&
          !memcmp(&rules, &before, sizeof rules));
    test_mir_word(bytes + 16u, 7u);
    --section.size;
    CHECK(!portable_mesh_item_rules_asset_open(&section, &rules));
    ++section.size;
    /* Exact rank-only weights: overflow, internal successor awards and
     * nonzero entries after padding must reject rather than wrap RNG totals. */
    bytes[376] = 0;
    bytes[377] = 0x10;
    bytes[378] = 1;
    CHECK(!portable_mesh_item_rules_asset_open(&section, &rules));
    memset(bytes + 376, 0, 4);
    bytes[372] = 0x0e;
    CHECK(!portable_mesh_item_rules_asset_open(&section, &rules));
    bytes[372] = 7;
    bytes[380] = 1;
    bytes[381] = 0x10;
    bytes[382] = 1;
    CHECK(!portable_mesh_item_rules_asset_open(&section, &rules));
    memset(bytes + 380, 0, 4);
    CHECK(portable_mesh_item_rules_asset_open(&section, &rules));
    PortableMeshKartRules kart = {.forward_speed_q16 = 256 * 65536};
    rules.mushroom_cap_ratio_q16 = 2 * 65536;
    CHECK(!portable_mesh_item_rules_fit_profile(&rules, &kart));
    kart.forward_speed_q16 = 128 * 65536;
    CHECK(portable_mesh_item_rules_fit_profile(&rules, &kart));
    PortableAssetPackView pack = {.resolved_sections = &section, .section_count = 1};
    char error[256];
    CHECK(portable_course_3d_validate_pack(&pack, error, sizeof error));
    bytes[32] = 65;
    CHECK(!portable_course_3d_validate_pack(&pack, error, sizeof error) && strstr(error, "MIR1"));
    bytes[32] = 64;
    /* A small borrowed metadata view tests the property resolver itself;
     * complete C3D geometry is covered by the existing course format fixture. */
    uint8_t meta[128] = {0}, property[32] = {0};
    const uint8_t strings[] = "mksc:item-rules";
    PortableCourse3DView view = {.pack = &pack, .id = 70000u, .table_count = 3};
    view.tables[0] = (PortableCourse3DTable){TAG("META"), 1, 128, meta};
    view.tables[1] = (PortableCourse3DTable){TAG("STRS"), (uint32_t)sizeof strings, 1, strings};
    view.tables[2] = (PortableCourse3DTable){TAG("PROP"), 1, 32, property};
    uint32_t id = 123u;
    CHECK(portable_course_3d_item_rules(&view, &id, error, sizeof error) && id == 0u);
    test_mir_word(meta + 100u, 1u);
    test_mir_word(property + 4u, PORTABLE_COURSE_3D_PROPERTY_ASSET);
    test_mir_word(property + 8u, 103u);
    test_mir_word(property + 12u, 41u);
    CHECK(portable_course_3d_item_rules(&view, &id, error, sizeof error) && id == 41u);
    test_mir_word(property + 12u, 42u);
    id = 123u;
    CHECK(!portable_course_3d_item_rules(&view, &id, error, sizeof error) && id == 123u &&
          error[0] != 0);
    test_mir_word(property + 12u, 41u);
    section.kind = 102u;
    CHECK(!portable_course_3d_item_rules(&view, &id, error, sizeof error) && id == 123u);
    puts("portable mesh item rules: PASS");
    return 0;
}
