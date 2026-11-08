#include "mesh_course_coins.h"
#include "mesh_course_actors.h"
#include "mesh_course_fixture.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "coins line %d: %s\n", __LINE__, #x);                                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static PortableMeshCourseCoinsConfig config(void)
{
    PortableMeshCourseCoinsConfig c = {.course_id = 7,
                                       .enabled = 1,
                                       .profile_id = 5,
                                       .revision = 1,
                                       .modes = 13,
                                       .cap = 99,
                                       .initial = 5,
                                       .placement_count = 1,
                                       .loss = {4, 2, 1, 8, 1}};
    for (unsigned i = 0; i < 100; ++i)
        c.ratios_q16[i] = (int32_t)(((90 + (i < 10 ? i : 10)) * 65536 + 50) / 100);
    c.placements[0] =
        (PortableMeshCourseCoin){.id = 1, .model_id = 1, .half_q16 = {Q / 2, Q / 2, Q / 4}};
    return c;
}
static PortableMeshRacerState racer(const PortableMeshKartRules *rules, int32_t x, int32_t y,
                                    int32_t z)
{
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    const int32_t p[3] = {x, y, z + rules->kart_radius_q16 + rules->ride_height_q16};
    const int32_t forward[3] = {1073741824, 0, 0};
    portable_mesh_kart_init(&r.kart, rules, p, forward);
    r.kart.grounded = 1;
    return r;
}
static int ownership(void)
{
    PortableMeshCourseCoinsConfig c = config();
    PortableMeshCourseCoins w;
    CHECK(portable_mesh_course_coins_init(&c, &w) && w.enabled && w.available == 1);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(w.count[i] == 5 && !w.contact_grace[i]);
    PortableMeshKartEffects effects = {
        .speed_ratio_q16 = Q, .boost_cap_ratio_q16 = Q, .body_scale_q16 = Q};
    CHECK(portable_mesh_course_coins_effect(&c, &w, 1, 0, &effects) &&
          effects.speed_ratio_q16 == 62259);
    effects.speed_ratio_q16 = Q / 2;
    CHECK(portable_mesh_course_coins_effect(&c, &w, 1, 0, &effects) &&
          effects.speed_ratio_q16 == Q / 2);
    effects.speed_ratio_q16 = Q;
    effects.boost_active = 1;
    CHECK(portable_mesh_course_coins_effect(&c, &w, 1, 0, &effects) &&
          effects.speed_ratio_q16 == Q);
    effects.boost_active = 0;
    CHECK(portable_mesh_course_coins_effect(&c, &w, 2, 0, &effects) &&
          effects.speed_ratio_q16 == Q);
    PortableMeshCourseCoins saved = w;
    effects.speed_ratio_q16 = 0;
    PortableMeshKartEffects old = effects;
    CHECK(!portable_mesh_course_coins_effect(&c, &w, 1, 0, &effects) &&
          !memcmp(&effects, &old, sizeof old) && !memcmp(&w, &saved, sizeof w));
    PortableMeshCourseCoinsConfig absent = {.course_id = 7};
    CHECK(portable_mesh_course_coins_init(&absent, &w));
    CHECK(portable_mesh_course_coins_hash(&w, 123) == 123);
    c.placement_count = 0;
    CHECK(portable_mesh_course_coins_init(&c, &w) && !w.available && w.count[0] == 5);
    PortableMeshCourseActorsConfig actors = {
        .course_id = 7, .profile_id = 5, .revision = 1, .coins = c};
    PortableMeshCourseActors actor_world;
    char error[160];
    CHECK(portable_mesh_course_actors_config_count(&actors) == 0 &&
          portable_mesh_course_actors_required(&actors));
    CHECK(portable_mesh_course_actors_init(&actors, &actor_world, error, sizeof error));
    uint64_t hash = portable_mesh_course_actors_checksum(&actor_world);
    --actor_world.coins.count[0];
    CHECK(hash != portable_mesh_course_actors_checksum(&actor_world));
    actors.coins = absent;
    CHECK(!portable_mesh_course_actors_required(&actors));
    CHECK(portable_mesh_course_actors_init(&actors, &actor_world, error, sizeof error));
    CHECK(!actor_world.coins.enabled);
    return 0;
}
static int collection(void)
{
    PortableMeshCourseCoinsConfig c = config();
    PortableMeshCourseCoins w;
    PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshRacerState before[2] = {racer(&rules, -2 * Q, 0, 0), racer(&rules, -2 * Q, 0, 0)};
    PortableMeshRacerState after[2] = {racer(&rules, 2 * Q, 0, 0), racer(&rules, 2 * Q, 0, 0)};
    PortableMeshCourseCoinRequests requests = {0};
    PortableMeshCourseCoinEvents e;
    char error[160];
    c.placement_count = 3;
    c.placements[1] = c.placements[0];
    c.placements[1].object_index = 1;
    c.placements[2] = c.placements[0];
    c.placements[2].id = 2;
    c.placements[2].object_index = 2;
    c.placements[2].position_q16[0] = Q;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, before, after, 2, 3,
                                             &requests, &e, error, sizeof error));
    CHECK(e.picked[0] == 3 && e.picked[1] == 4 && e.before[0] == 5 && e.after[0] == 6 &&
          w.count[1] == 6 && !w.available);
    /* A nearer placement wins over table order, and one racer takes onlyone. */
    c.placements[0].position_q16[0] = Q;
    c.placements[1] = c.placements[0];
    c.placements[1].object_index = 1;
    c.placements[2].position_q16[0] = -Q;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 4, &rules, NULL, before, after, 1, 1,
                                             &requests, &e, error, sizeof error));
    CHECK(e.picked[0] == 4 && w.available == 3 && w.count[0] == 6);
    /* FullXYZ rejects a road directly above, airborne crossings and recovery. */
    for (unsigned guard = 0; guard < 5; ++guard)
    {
        CHECK(portable_mesh_course_coins_init(&c, &w));
        PortableMeshRacerState a = before[0], b = after[0];
        if (guard == 0)
        {
            a.kart.position_q16[2] += 3 * Q;
            b.kart.position_q16[2] += 3 * Q;
        }
        if (guard == 1)
            b.kart.grounded = 0;
        if (guard == 2)
            a.respawn_grace = 1;
        if (guard == 3)
            b.kart.respawn_ticks = 1;
        if (guard == 4)
            b.phase = PORTABLE_MESH_RACER_FINISHED;
        CHECK(portable_mesh_course_coins_resolve(&c, &w, 8, &rules, NULL, &a, &b, 1, 1, &requests,
                                                 &e, error, sizeof error) &&
              !e.picked[0] && w.available == 7);
    }
    /* Suspended body scaling changes the foot; a fixed center is not a valid
     * substitute for subtracting the same scaled clearance as movement. */
    c.placement_count = 1;
    c.placements[0].position_q16[0] = 0;
    c.placements[0].half_q16[2] = Q / 128;
    int32_t scale[8] = {Q / 2};
    before[0] = racer(&rules, -2 * Q, 0, 0);
    after[0] = racer(&rules, 2 * Q, 0, 0);
    int32_t correction = (rules.kart_radius_q16 + rules.ride_height_q16) / 2;
    before[0].kart.position_q16[2] -= correction;
    after[0].kart.position_q16[2] -= correction;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    w.previous_body_scale_q16[0] = Q / 2;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, scale, before, after, 1, 1,
                                             &requests, &e, error, sizeof error) &&
          e.picked[0] == 1);
    /* Two odd authored dimensions distinguish core's independent truncation
     * from truncating their sum. True half-sized clearance is1638+1638;
     * a second truncation strategy invents a foot one LSB below this box. */
    rules.kart_radius_q16 = 3277;
    rules.ride_height_q16 = 3277;
    c.placements[0].position_q16[2] = 1;
    c.placements[0].half_q16[2] = 1;
    before[0] = racer(&rules, -Q, 0, 0);
    after[0] = racer(&rules, Q, 0, 0);
    before[0].kart.position_q16[2] = 3276;
    after[0].kart.position_q16[2] = 3276;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    w.previous_body_scale_q16[0] = Q / 2;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, scale, before, after, 1, 1,
                                             &requests, &e, error, sizeof error) &&
          e.picked[0] == 1);
    return 0;
}
static int body_transitions(void)
{
    PortableMeshCourseCoinsConfig c = config();
    c.placements[0].half_q16[2] = 1;
    PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshCourseCoinRequests requests = {0};
    PortableMeshCourseCoinEvents e;
    char error[160];
    for (unsigned growing = 0; growing < 2; ++growing)
    {
        const int32_t previous = growing ? Q / 2 : Q, current = growing ? Q : Q / 2;
        PortableMeshCourseCoins w;
        CHECK(portable_mesh_course_coins_init(&c, &w));
        w.previous_body_scale_q16[0] = previous;
        PortableMeshRacerState a = racer(&rules, -Q, 0, 0), b = racer(&rules, Q, 0, 0);
        a.kart.position_q16[2] = portable_fixed_3d_mul(rules.kart_radius_q16, previous) +
                                 portable_fixed_3d_mul(rules.ride_height_q16, previous);
        b.kart.position_q16[2] = portable_fixed_3d_mul(rules.kart_radius_q16, current) +
                                 portable_fixed_3d_mul(rules.ride_height_q16, current);
        int32_t scales[8] = {current};
        CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, scales, &a, &b, 1, 1, &requests,
                                                 &e, error, sizeof error));
        CHECK(e.picked[0] == 1 && w.previous_body_scale_q16[0] == current);
    }
    /* Excluded modes still retain the actual last movement shape, even if
     * status changed after that movement. They grant no coin reward/loss. */
    PortableMeshCourseCoins w;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    PortableMeshRacerState a = racer(&rules, -Q, 0, 0), b = racer(&rules, Q, 0, 0);
    b.kart.position_q16[2] = portable_fixed_3d_mul(rules.kart_radius_q16, Q / 2) +
                             portable_fixed_3d_mul(rules.ride_height_q16, Q / 2);
    int32_t scales[8] = {Q / 2};
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 2, &rules, scales, &a, &b, 1, 1, &requests, &e,
                                             error, sizeof error));
    CHECK(w.available == 1 && w.count[0] == 5 && !e.picked[0] &&
          w.previous_body_scale_q16[0] == Q / 2);
    a = b;
    a.kart.position_q16[0] = -Q;
    b = racer(&rules, Q, 0, 0);
    scales[0] = Q;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, scales, &a, &b, 1, 1, &requests, &e,
                                             error, sizeof error) &&
          e.picked[0] == 1);
    uint64_t hash = portable_mesh_course_coins_hash(&w, 123);
    w.previous_body_scale_q16[0] = Q / 2;
    CHECK(hash != portable_mesh_course_coins_hash(&w, 123));
    w.previous_body_scale_q16[0] = 0;
    CHECK(!portable_mesh_course_coins_valid(&c, &w) && !portable_mesh_course_coins_hash(&w, 123));
    return 0;
}
static int losses_and_cap(void)
{
    PortableMeshCourseCoinsConfig c = config();
    PortableMeshCourseCoins w;
    PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshRacerState a = racer(&rules, -Q, 0, 0), b = racer(&rules, Q, 0, 0);
    PortableMeshCourseCoinRequests requests = {0};
    PortableMeshCourseCoinEvents e;
    char error[160];
    CHECK(portable_mesh_course_coins_init(&c, &w));
    w.count[0] = 99;
    requests.post[PORTABLE_MESH_COIN_HAUNT][0] = 1;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 1, &requests, &e,
                                             error, sizeof error));
    CHECK(w.count[0] == 98 && w.available == 1 && !e.picked[0] && e.before[0] == 99 &&
          e.after[0] == 98 && e.lost[0] == 1 && e.causes[0] == 16);
    requests = (PortableMeshCourseCoinRequests){0};
    requests.pre[PORTABLE_MESH_COIN_HIT][0] = 1;
    requests.post[PORTABLE_MESH_COIN_HAUNT][0] = 1;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    w.count[0] = 99;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 1, &requests, &e,
                                             error, sizeof error));
    CHECK(w.count[0] == 95 && !w.available && e.picked[0] == 1 && e.lost[0] == 5 &&
          e.causes[0] == 17);
    /* Causes retain their authored order; repeated contact occurrences share
     * one8turn lock, but independent rescue/hit/haunt requests remain real. */
    c.placement_count = 0;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    requests = (PortableMeshCourseCoinRequests){0};
    requests.pre[PORTABLE_MESH_COIN_CONTACT][0] = 3;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 0, &requests, &e,
                                             error, sizeof error));
    CHECK(w.count[0] == 4 && e.lost[0] == 1 && w.contact_grace[0] == 8);
    for (unsigned i = 0; i < 7; ++i)
    {
        CHECK(portable_mesh_course_coins_advance(&c, &w));
        CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 0, &requests,
                                                 &e, error, sizeof error) &&
              !e.lost[0] && w.count[0] == 4);
    }
    CHECK(portable_mesh_course_coins_advance(&c, &w));
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 0, &requests, &e,
                                             error, sizeof error) &&
          e.lost[0] == 1 && w.count[0] == 3);
    requests = (PortableMeshCourseCoinRequests){0};
    requests.pre[PORTABLE_MESH_COIN_CRUSH][0] = 255;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 0, &requests, &e,
                                             error, sizeof error) &&
          w.count[0] == 0 && e.lost[0] == 3);
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, &a, &b, 1, 0, &requests, &e,
                                             error, sizeof error) &&
          !e.lost[0] && !e.causes[0]);
    CHECK(portable_mesh_course_coins_init(&c, &w));
    PortableMeshCourseCoins saved = w;
    CHECK(portable_mesh_course_coins_resolve(&c, &w, 2, &rules, NULL, &a, &b, 1, 1, &requests, &e,
                                             error, sizeof error) &&
          !memcmp(&w, &saved, sizeof w) && !e.lost[0]);
    return 0;
}
static int rollback(void)
{
    PortableMeshCourseCoinsConfig c = config();
    PortableMeshCourseCoins w;
    CHECK(portable_mesh_course_coins_init(&c, &w));
    PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshRacerState before[2] = {racer(&rules, -Q, 0, 0), racer(&rules, -Q, 0, 0)};
    PortableMeshRacerState after[2] = {racer(&rules, Q, 0, 0), racer(&rules, Q, 0, 0)};
    after[1].kart.up_q30[2] = INT32_MAX;
    PortableMeshCourseCoins saved = w;
    PortableMeshCourseCoinEvents e;
    memset(&e, 0x5a, sizeof e);
    PortableMeshCourseCoinEvents old = e;
    PortableMeshCourseCoinRequests requests = {0};
    requests.pre[0][0] = 1;
    char error[160];
    CHECK(!portable_mesh_course_coins_resolve(&c, &w, 1, &rules, NULL, before, after, 2, 3,
                                              &requests, &e, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w) && !memcmp(&e, &old, sizeof e));
    c.placement_count = 2;
    c.placements[1] = c.placements[0];
    c.placements[1].position_q16[2] = Q;
    CHECK(!portable_mesh_course_coins_init(&c, &w) && !memcmp(&w, &saved, sizeof w));
    c.placements[1] = c.placements[0];
    CHECK(portable_mesh_course_coins_init(&c, &w));
    w.available = 1;
    CHECK(!portable_mesh_course_coins_valid(&c, &w));
    return 0;
}
static int decoder(void)
{
    uint8_t meta[128] = {0}, objects[128] = {0}, props[7 * 32] = {0}, strings[192] = {0},
            mcr[452] = {0};
    const char *keys[] = {"mksc:coin-rules", "mksc:controller", "mksc:controller-version",
                          "coin:id",         "coin:half-x",     "coin:half-y",
                          "coin:half-z"};
    unsigned used = 1;
    for (unsigned i = 0; i < 7; ++i)
    {
        test_mesh_word(props + 32 * i, used);
        size_t n = strlen(keys[i]) + 1;
        memcpy(strings + used, keys[i], n);
        used += (unsigned)n;
    }
    unsigned coins = used;
    memcpy(strings + used, "coins", 6);
    used += 6;
    test_mesh_word(props + 4, 4);
    test_mesh_word(props + 8, 105);
    test_mesh_word(props + 12, 5);
    test_mesh_word(props + 32 + 4, 3);
    test_mesh_word(props + 32 + 8, coins);
    test_mesh_word(props + 64 + 8, 1);
    test_mesh_word(props + 96 + 8, 1);
    for (unsigned i = 4; i < 7; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 1);
        test_mesh_word(props + 32 * i + 8, Q / 2);
    }
    test_mesh_word(meta + 100, 1);
    test_mesh_word(objects, UINT32_C(0x20000001));
    test_mesh_word(objects + 4, 96);
    test_mesh_word(objects + 8, 1);
    test_mesh_word(objects + 56, 1);
    test_mesh_word(objects + 60, 6);
    memcpy(mcr, "MCR1", 4);
    test_mesh_word(mcr + 4, 1);
    test_mesh_word(mcr + 8, 452);
    test_mesh_word(mcr + 12, 5);
    test_mesh_word(mcr + 16, 1);
    test_mesh_word(mcr + 20, 13);
    test_mesh_word(mcr + 24, 99);
    test_mesh_word(mcr + 28, 5);
    PortableMeshCourseCoinsConfig expected = config();
    for (unsigned i = 0; i < 100; ++i)
        test_mesh_word(mcr + 32 + 4 * i, (uint32_t)expected.ratios_q16[i]);
    for (unsigned i = 0; i < 5; ++i)
        test_mesh_word(mcr + 432 + 4 * i, expected.loss[i]);
    PortableAssetSectionView sections[2] = {
        {.kind = 105, .content_id = 5, .version = 1, .bytes = mcr, .size = 452},
        {.kind = 96, .content_id = 1, .version = 1}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 2};
    PortableCourse3DView course = {.id = 7, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, objects};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 7, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    char error[160];
    PortableMeshCourseCoinsConfig c;
    CHECK(portable_mesh_course_coins_config_open(&course, &c, error, sizeof error) && c.enabled &&
          c.placement_count == 1 && c.profile_id == 5 && c.placements[0].id == 1 &&
          c.placements[0].model_id == 1);
    PortableMeshCourseActorsConfig actors;
    CHECK(portable_mesh_course_actors_config_open(&course, &actors, error, sizeof error) &&
          portable_mesh_course_actors_required(&actors) &&
          !portable_mesh_course_actors_config_count(&actors) && actors.profile_id == 5 &&
          actors.revision == 1);
    PortableMeshCourseCoinsConfig saved = c;
    const unsigned offsets[] = {4, 8, 12, 16, 20, 24, 28, 32, 432};
    const uint32_t invalid[] = {2, 451, UINT32_MAX, 0, 32, 100, 100, 3276, 100};
    for (unsigned i = 0; i < 9; ++i)
    {
        uint32_t old_word = (uint32_t)mcr[offsets[i]] | (uint32_t)mcr[offsets[i] + 1] << 8 |
                            (uint32_t)mcr[offsets[i] + 2] << 16 |
                            (uint32_t)mcr[offsets[i] + 3] << 24;
        test_mesh_word(mcr + offsets[i], invalid[i]);
        CHECK(!portable_mesh_course_coins_config_open(&course, &c, error, sizeof error) &&
              !memcmp(&c, &saved, sizeof c));
        test_mesh_word(mcr + offsets[i], old_word);
    }
    sections[0].bytes = NULL;
    CHECK(!portable_mesh_course_coins_config_open(&course, &c, error, sizeof error));
    sections[0].bytes = mcr;
    test_mesh_word(props + 96 + 8, UINT32_MAX);
    CHECK(!portable_mesh_course_coins_config_open(&course, &c, error, sizeof error));
    test_mesh_word(props + 96 + 8, 1);
    memcpy(objects + 64, objects, 64);
    course.tables[1].count = 2;
    CHECK(portable_mesh_course_coins_config_open(&course, &c, error, sizeof error) &&
          c.placement_count == 2);
    test_mesh_word(objects + 64 + 12, Q);
    saved = c;
    CHECK(!portable_mesh_course_coins_config_open(&course, &c, error, sizeof error) &&
          !memcmp(&c, &saved, sizeof c));
    course.tables[1].count = 0;
    CHECK(portable_mesh_course_coins_config_open(&course, &c, error, sizeof error) && c.enabled &&
          !c.placement_count);
    CHECK(portable_mesh_course_actors_config_open(&course, &actors, error, sizeof error) &&
          portable_mesh_course_actors_required(&actors));
    return 0;
}
int main(void)
{
    CHECK(!ownership());
    CHECK(!collection());
    CHECK(!body_transitions());
    CHECK(!losses_and_cap());
    CHECK(!rollback());
    CHECK(!decoder());
    return 0;
}
