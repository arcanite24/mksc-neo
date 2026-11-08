#include "mesh_cup.h"
#include "gp_cup_scoring.h"
#include "mesh_course_fixture.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh cup line%d: %s\n", __LINE__, #c);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static const PortableGpScoringRules scoring = {.finish_points = {9, 6, 3, 1, 0, 0, 0, 0},
                                               .initial_retry_credits = 3};
static const uint32_t drivers[8] = {65536, 70000, 80000, 90000, 100000, 110000, 120000, UINT32_MAX};
static int points_and_ties(void)
{
    const uint8_t finish[8] = {3, 2, 1, 0, 4, 5, 6, 7};
    const uint8_t previous[8] = {8, 6, 3, 0, 0, 0, 0, 0};
    uint8_t points[8], awards[8], order[8], ranks[8];
    CHECK(portable_gp_cup_scoring(&scoring, finish, previous, points, awards, order, ranks));
    CHECK(points[0] == 9 && points[1] == 9 && points[2] == 9 && points[3] == 9);
    CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3);
    CHECK(ranks[0] == 0 && ranks[3] == 0 && ranks[4] == 4);
    CHECK(awards[3] == 9 && awards[2] == 6 && awards[1] == 3 && awards[0] == 1 && awards[7] == 0);
    uint8_t original[8];
    memcpy(original, points, 8);
    uint8_t invalid[8];
    memcpy(invalid, finish, 8);
    invalid[1] = invalid[0];
    CHECK(!portable_gp_cup_scoring(&scoring, invalid, previous, points, awards, order, ranks));
    CHECK(!memcmp(original, points, 8));
    uint8_t wrap[8] = {0};
    wrap[3] = 255;
    CHECK(portable_gp_cup_scoring(&scoring, finish, wrap, wrap, awards, order, ranks));
    CHECK(wrap[3] == 8); /* Source byte addition, including aliased previous points. */
    return 0;
}
static void frozen_order(PortableMeshRaceSession *race, const uint8_t order[8])
{
    memcpy(race->state.finish_order, order, 8);
    for (unsigned rank = 0; rank < 8u; ++rank)
        race->state.finish_rank[order[rank]] = (uint8_t)(rank + 1u);
}
static void current_grid(PortableMeshRaceSession *race, const PortableMeshCupSession *cup)
{
    for (unsigned racer = 0; racer < 8u; ++racer)
        race->spawn_grids[racer] = cup->grid_slots[racer];
}
static void race_fixture(PortableTestMeshCourse *f, uint8_t grids[256], uint8_t header[32],
                         uint8_t nodes[64], uint8_t edges[8])
{
    test_mesh_course_init(f);
    test_mesh_word(f->meta + 12u, 3);
    test_mesh_word(f->meta + 20u, 70001);
    for (unsigned i = 0; i < 8u; ++i)
    {
        memcpy(grids + i * 32u, f->grid, 32);
        test_mesh_word(grids + i * 32u, (uint32_t)((int32_t)i * 65536 - 229376));
    }
    f->course.tables[1].bytes = grids;
    f->course.tables[1].count = 8;
    memset(header, 0, 32);
    memset(nodes, 0, 64);
    test_mesh_word(header + 8u, 2);
    test_mesh_word(header + 16u, 1);
    for (unsigned i = 0; i < 2u; ++i)
    {
        test_mesh_word(nodes + i * 32u + 4u, (uint32_t)(i ? -4 * 65536 : 4 * 65536));
        test_mesh_word(nodes + i * 32u + 12u, 16384);
        test_mesh_word(nodes + i * 32u + 16u, 10 * 65536);
        test_mesh_word(nodes + i * 32u + 20u, i);
        test_mesh_word(nodes + i * 32u + 24u, 1);
    }
    test_mesh_word(edges, 1);
    test_mesh_word(edges + 4u, 0);
    f->course.tables[f->course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'H', 'D'), 1, 32, header};
    f->course.tables[f->course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'N', 'D'), 2, 32, nodes};
    f->course.tables[f->course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'E', 'D'), 2, 4, edges};
}
int main(void)
{
    CHECK(points_and_ties() == 0);
    PortableTestMeshCourse f;
    uint8_t grids[256], header[32], nodes[64], edges[8];
    race_fixture(&f, grids, header, nodes, edges);
    const PortableMeshCupDefinition definition = {70001, {65536, 70000, 80000, UINT32_MAX}};
    f.course.id = definition.course_ids[0];
    PortableMeshCupSession cup;
    char error[192];
    CHECK(portable_mesh_cup_init(&cup, &definition, drivers, 2, &scoring, error, sizeof error) ==
          PORTABLE_MESH_OK);
    for (unsigned i = 0; i < 8u; ++i)
        CHECK(cup.grid_slots[i] == 7u - i && cup.driver_ids[i] == drivers[i]);
    const PortableMeshCupSession initialized = cup;
    PortableMeshCupDefinition invalid_definition = definition;
    invalid_definition.cup_id = 0;
    CHECK(portable_mesh_cup_init(&cup, &invalid_definition, drivers, 2, &scoring, error,
                                 sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &initialized, sizeof cup));
    invalid_definition = definition;
    invalid_definition.course_ids[1] = invalid_definition.course_ids[0];
    CHECK(portable_mesh_cup_init(&cup, &invalid_definition, drivers, 2, &scoring, error,
                                 sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &initialized, sizeof cup));
    CHECK(portable_mesh_cup_init(&cup, &definition, drivers, 3, &scoring, error, sizeof error) !=
              PORTABLE_MESH_OK &&
          !memcmp(&cup, &initialized, sizeof cup));
    PortableMeshCupSession pause_retry = initialized;
    CHECK(portable_mesh_cup_retry(&pause_retry, error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(pause_retry.stage == PORTABLE_MESH_CUP_RACE && pause_retry.retry_credits == 2 &&
          pause_retry.course_index == initialized.course_index &&
          !memcmp(pause_retry.grid_slots, initialized.grid_slots, 8) &&
          !memcmp(pause_retry.points, initialized.points, 8));
    uint32_t starts[8];
    for (unsigned i = 0; i < 8u; ++i)
        starts[i] = cup.grid_slots[i];
    PortableMeshRaceSession race;
    CHECK(portable_mesh_race_init_mode(&race, &f.course, &f.rules, 1, 1, 8, starts, error,
                                       sizeof error) == PORTABLE_MESH_OK);
    PortableMeshKartEvents events[8];
    for (unsigned tick = 0; tick < 1500u && !race.state.results_ready; ++tick)
    {
        /* Hold the throttle late in the countdown for MK8's best rocket start. */
        const PortableMeshRacerState *player = &race.state.racers[0];
        const PortableMeshKartInput input = {0, player->phase == PORTABLE_MESH_RACER_COUNTDOWN
            ? portable_mesh_kart_rocket_buttons(player->countdown_ticks, 5u) : PORTABLE_MESH_ACCELERATE};
        CHECK(portable_mesh_race_tick(&race, &input, events, error, sizeof error) ==
              PORTABLE_MESH_OK);
    }
    CHECK(race.state.results_ready && race.state.racers[0].finish_ticks);
    CHECK(portable_mesh_cup_finish(&cup, &race, error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(cup.stage == PORTABLE_MESH_CUP_STANDINGS && cup.player_rank < 4u);
    CHECK(cup.race_ticks[0] == race.state.racers[0].finish_ticks &&
          cup.points[0] == scoring.finish_points[cup.player_rank]);
    PortableMeshCupSession completed = cup;
    CHECK(portable_mesh_cup_finish(&cup, &race, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &completed, sizeof cup));
    CHECK(portable_mesh_cup_advance(&cup, error, sizeof error) == PORTABLE_MESH_OK &&
          cup.course_index == 1);
    for (unsigned rank = 0; rank < 8u; ++rank)
        CHECK(cup.grid_slots[completed.race_order[rank]] == rank);
    CHECK(!memcmp(cup.points, completed.points, 8));
    const PortableMeshCupSession next = cup;
    PortableMeshRaceSession wrong = race;
    CHECK(portable_mesh_cup_finish(&cup, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &next, sizeof cup));
    PortableMeshCupSession checking = completed;
    checking.stage = PORTABLE_MESH_CUP_RACE;
    const PortableMeshCupSession checking_before = checking;
    wrong = race;
    wrong.spawn_grids[0] ^= 1u;
    CHECK(portable_mesh_cup_finish(&checking, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&checking, &checking_before, sizeof checking));
    wrong = race;
    wrong.state.finish_rank[0] = 0;
    CHECK(portable_mesh_cup_finish(&checking, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&checking, &checking_before, sizeof checking));
    wrong = race;
    wrong.rules = NULL;
    CHECK(portable_mesh_cup_finish(&checking, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&checking, &checking_before, sizeof checking));
    /* Remaining transitions use frozen semantic results from that real race;
     * no2D session or fabricated driver physics is involved. */
    f.course.id = definition.course_ids[1];
    test_mesh_word(f.meta + 24u, 1);
    wrong = race;
    wrong.mode = 4;
    CHECK(portable_mesh_cup_finish(&cup, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &next, sizeof cup));
    wrong = race;
    wrong.human_count = 2;
    CHECK(portable_mesh_cup_finish(&cup, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &next, sizeof cup));
    wrong = race;
    wrong.course = NULL;
    CHECK(portable_mesh_cup_finish(&cup, &wrong, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &next, sizeof cup));
    /* A failed fifth place awards nobody, retains grid/points and permits
     * exactly three accepted retries. Fourth failure is game over. */
    uint8_t failed_order[8] = {1, 2, 3, 4, 0, 5, 6, 7};
    current_grid(&race, &cup);
    frozen_order(&race, failed_order);
    for (unsigned attempt = 0; attempt < 4u; ++attempt)
    {
        PortableMeshCupSession before = cup;
        CHECK(portable_mesh_cup_finish(&cup, &race, error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(!memcmp(cup.points, next.points, 8) && !memcmp(cup.grid_slots, next.grid_slots, 8));
        for (unsigned i = 0; i < 8u; ++i)
            CHECK(!cup.awards[i]);
        if (attempt < 3u)
        {
            CHECK(cup.stage == PORTABLE_MESH_CUP_RETRY);
            CHECK(portable_mesh_cup_retry(&cup, error, sizeof error) == PORTABLE_MESH_OK);
            CHECK(cup.retry_credits == before.retry_credits - 1u && cup.course_index == 1);
        }
        else
            CHECK(cup.stage == PORTABLE_MESH_CUP_GAME_OVER);
    }
    completed = cup;
    CHECK(portable_mesh_cup_retry(&cup, error, sizeof error) != PORTABLE_MESH_OK &&
          !memcmp(&cup, &completed, sizeof cup));
    /* Final successful standings advance to ceremony with full IDs intact. */
    cup = next;
    for (unsigned course = 1; course < 4u; ++course)
    {
        f.course.id = definition.course_ids[course];
        test_mesh_word(f.meta + 24u, course);
        const uint8_t success[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        frozen_order(&race, success);
        current_grid(&race, &cup);
        CHECK(portable_mesh_cup_finish(&cup, &race, error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_cup_advance(&cup, error, sizeof error) == PORTABLE_MESH_OK);
    }
    CHECK(cup.stage == PORTABLE_MESH_CUP_COMPLETE && cup.course_index == 3 &&
          cup.definition.course_ids[3] == UINT32_MAX);
    CHECK(!memcmp(cup.driver_ids, drivers, sizeof drivers));
    puts("Mesh cup: PASS");
    return 0;
}
