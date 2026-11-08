#include "mesh_course_fixture.h"
#include "mesh_race.h"
#include <stdio.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
/* Four independent starts share the authored strip and gate graph. */
static void local_course(PortableTestMeshCourse *f, uint8_t grids[128])
{
    test_mesh_course_init(f);
    test_mesh_word(f->meta + 12u, 10u);
    for (unsigned i = 0; i < 4u; ++i)
    {
        memcpy(grids + i * 32u, f->grid, 32u);
        test_mesh_word(grids + i * 32u, (uint32_t)(((int)i * 2 - 3) * 65536));
    }
    f->course.tables[1].bytes = grids;
    f->course.tables[1].count = 4;
}
static void racing(PortableMeshRaceSession *race)
{
    for (unsigned i = 0; i < race->state.player_count; ++i)
    {
        race->state.racers[i].phase = PORTABLE_MESH_RACER_RACING;
        race->state.racers[i].countdown_ticks = 0;
    }
}
static void approaching_finish(PortableMeshRacerState *racer)
{
    racer->checkpoint = 1;
    racer->checkpoint_count = 1;
    racer->crossing_armed = 1;
    racer->kart.position_q16[1] = -4 * 65536 + 512;
    racer->kart.velocity_q16[1] = -65536;
    racer->kart.speed_q16 = 65536;
}
static int local_world(void)
{
    PortableTestMeshCourse f;
    uint8_t grids[128];
    local_course(&f, grids);
    PortableMeshRaceSession race;
    PortableMeshKartInput inputs[4] = {{0, 0}};
    PortableMeshKartEvents events[4];
    char error[192];
    CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 4, NULL, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    for (unsigned tick = 0; tick < 180u; ++tick)
        CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) ==
              PORTABLE_MESH_OK);
    CHECK(race.state.tick == 180u);
    for (unsigned i = 0; i < 4u; ++i)
    {
        CHECK(race.state.racers[i].phase == PORTABLE_MESH_RACER_RACING);
        CHECK(race.state.racers[i].race_ticks == 0);
        inputs[i].buttons = PORTABLE_MESH_RESCUE;
    }
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    for (unsigned i = 0; i < 4u; ++i)
    {
        CHECK(race.state.racers[i].kart.position_q16[0] == ((int)i * 2 - 3) * 65536);
        CHECK(race.state.racers[i].kart.respawn_ticks == f.rules.respawn_ticks);
        CHECK(race.state.racers[i].respawn_grace == f.rules.respawn_grace_ticks);
        inputs[i].buttons = 0;
    }
    /* The last mover fails: neither earlier moves nor caller events may leak. */
    race.state.racers[3].kart.forward_q30[0] = 0;
    race.state.racers[3].kart.forward_q30[1] = 0;
    race.state.racers[3].kart.forward_q30[2] = 0;
    race.state.racers[3].kart.respawn_ticks = 0;
    PortableMeshRaceSession before = race;
    memset(events, 0x5a, sizeof(events));
    PortableMeshKartEvents before_events[4];
    memcpy(before_events, events, sizeof(events));
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) != PORTABLE_MESH_OK);
    CHECK(memcmp(&race, &before, sizeof(race)) == 0);
    CHECK(memcmp(events, before_events, sizeof(events)) == 0);

    CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 2, NULL, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    racing(&race);
    /* Neither kart moves across Y=0 alone. Pair separation pushes racer0 through it,
     * so checkpoint publication must happen after the complete contact phase. */
    race.state.racers[0].kart.position_q16[0] = 0;
    race.state.racers[1].kart.position_q16[0] = 0;
    race.state.racers[0].kart.position_q16[1] = 6554;
    race.state.racers[1].kart.position_q16[1] = 26214;
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(race.state.racers[0].kart.position_q16[1] < 0);
    CHECK(race.state.racers[0].checkpoint == 1 && race.state.racers[1].checkpoint == 0);

    /* Countdown/grace racers are excluded from contacts, even when overlapping. */
    CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 2, NULL, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    racing(&race);
    race.state.racers[0].kart.position_q16[0] = 0;
    race.state.racers[1].kart.position_q16[0] = 0;
    race.state.racers[0].respawn_grace = 1;
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(race.state.racers[0].kart.position_q16[0] == 0 &&
          race.state.racers[1].kart.position_q16[0] == 0);

    CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 2, NULL, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    racing(&race);
    approaching_finish(&race.state.racers[0]);
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(race.state.results_ready && race.state.finish_rank[0] == 1 &&
          race.state.finish_rank[1] == 2 && race.state.retired[1]);
    CHECK(race.state.racers[1].phase == PORTABLE_MESH_RACER_RACING &&
          race.state.racers[1].lap == 0 && race.state.racers[1].finish_ticks == 0);
    before = race;
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(memcmp(&race, &before, sizeof(race)) == 0);

    CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 4, NULL, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    racing(&race);
    for (unsigned i = 0; i < 3u; ++i)
        approaching_finish(&race.state.racers[i]);
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(race.state.results_ready && race.state.finish_count == 4u);
    for (unsigned i = 0; i < 4u; ++i)
        CHECK(race.state.finish_order[i] == i && race.state.finish_rank[i] == i + 1u &&
              race.state.retired[i] == (i == 3u));
    /* All legitimate ties rank before retiring: simultaneous last crossings are real finishes. */
    CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 2, NULL, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    racing(&race);
    approaching_finish(&race.state.racers[0]);
    approaching_finish(&race.state.racers[1]);
    CHECK(portable_mesh_race_tick(&race, inputs, events, error, sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(race.state.results_ready && !race.state.retired[0] && !race.state.retired[1] &&
          race.state.finish_order[0] == 0 && race.state.finish_order[1] == 1);
    return 0;
}
/* Eight starts and a closed four-node CPU route stay local to this fixture. */
static void field_course(PortableTestMeshCourse *f, uint8_t grids[256], uint8_t route[32],
                         uint8_t nodes[128], uint8_t edges[16])
{
    test_mesh_course_init(f);
    test_mesh_word(f->meta + 12u, 15u);
    memset(route, 0, 32); memset(nodes, 0, 128); memset(edges, 0, 16);
    test_mesh_word(route + 8u, 4u);
    test_mesh_word(route + 16u, 1u);
    for (unsigned i = 0; i < 8u; ++i)
    {
        memcpy(grids + i * 32u, f->grid, 32);
        test_mesh_word(grids + i * 32u, (uint32_t)(((int)i * 2 - 7) * 65536));
    }
    f->course.tables[1].bytes = grids; f->course.tables[1].count = 8;
    const int32_t xy[4][2] = {{0,4},{0,-4},{8,-4},{8,4}};
    for (unsigned i = 0; i < 4u; ++i)
    {
        for (unsigned a = 0; a < 2u; ++a)
            test_mesh_word(nodes + i * 32u + a * 4u, (uint32_t)(xy[i][a] * 65536));
        test_mesh_word(nodes + i * 32u + 12u, 65536u);
        test_mesh_word(nodes + i * 32u + 16u, 2u * 65536u);
        test_mesh_word(nodes + i * 32u + 20u, i);
        test_mesh_word(nodes + i * 32u + 24u, 1);
        test_mesh_word(edges + i * 4u, (i + 1u) % 4u);
    }
    f->course.tables[f->course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','H','D'), 1, 32, route};
    f->course.tables[f->course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','N','D'), 4, 32, nodes};
    f->course.tables[f->course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','E','D'), 4, 4, edges};
}
static int field_world(void)
{
    PortableTestMeshCourse f;
    uint8_t grids[256], route[32], nodes[128], edges[16];
    field_course(&f, grids, route, nodes, edges);
    PortableMeshRaceSession race, repeat, before;
    PortableMeshKartInput human[1] = {{0,0}};
    PortableMeshKartEvents events[8], repeat_events[8];
    char error[192];
    for (unsigned mode = 1; mode <= 4u; mode += 3u)
    {
        CHECK(portable_mesh_race_init_mode(&race, &f.course, &f.rules, mode, 1, 8, NULL,
                                           error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(race.checkpoint_stages == 2 && race.human_count == 1 && race.state.player_count == 8);
        repeat = race;
        for (unsigned tick = 0; tick < 190u; ++tick)
        {
            CHECK(portable_mesh_race_tick(&race, human, events, error, sizeof error) == PORTABLE_MESH_OK);
            CHECK(portable_mesh_race_tick(&repeat, human, repeat_events, error, sizeof error) == PORTABLE_MESH_OK);
            CHECK(memcmp(&race, &repeat, sizeof race) == 0 &&
                  memcmp(events, repeat_events, sizeof events) == 0);
        }
        CHECK(!race.state.results_ready && race.cpu[1].course_id == f.course.id);
        /* Stage all seven CPU controls before a malformed human fails physics. */
        race.state.racers[0].kart.forward_q30[1] = 0;
        before = race;
        memset(events, 0x5a, sizeof events); memcpy(repeat_events, events, sizeof events);
        CHECK(portable_mesh_race_tick(&race, human, events, error, sizeof error) != PORTABLE_MESH_OK);
        CHECK(memcmp(&race, &before, sizeof race) == 0 &&
              memcmp(events, repeat_events, sizeof events) == 0);
        race = repeat;
        race.state.racers[0].kart.position_q16[0] = 0;
        approaching_finish(&race.state.racers[0]);
        /* One rival already finished; another is ahead of the unfinished field. */
        race.state.racers[1].phase = PORTABLE_MESH_RACER_FINISHED;
        race.state.racers[1].lap = 1; race.state.racers[1].finish_ticks = 7;
        race.state.finish_count = 1; race.state.finish_order[0] = 1; race.state.finish_rank[1] = 1;
        race.state.racers[2].checkpoint = race.state.racers[2].checkpoint_count = 1;
        race.state.racers[2].kart.position_q16[1] = -2 * 65536;
        CHECK(portable_mesh_race_tick(&race, human, events, error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(race.state.results_ready && race.state.finish_count == 2 &&
              race.state.finish_order[0] == 1 && race.state.finish_order[1] == 0 &&
              race.state.finish_order[2] == 2);
        for (unsigned i = 0; i < 8u; ++i)
        {
            uint32_t ticks; int estimated;
            CHECK(!race.state.retired[i]);
            CHECK(portable_mesh_race_result_ticks(&race, i, &ticks, &estimated));
            CHECK(estimated == (i > 1u));
            if (estimated)
                CHECK(race.state.racers[i].phase == PORTABLE_MESH_RACER_RACING &&
                      race.state.racers[i].finish_ticks == 0 && ticks >= race.state.racers[i].race_ticks);
            else CHECK(ticks == race.state.racers[i].finish_ticks);
        }
        before = race;
        CHECK(portable_mesh_race_tick(&race, human, events, error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(memcmp(&race, &before, sizeof race) == 0);
    }
    return 0;
}
static int gate_semantics(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    PortableMeshTimeTrial session;
    PortableMeshKartEvents events;
    char error[160];
    /* Reordering the wire rows does not change which authored gate finishes the race. */
    uint8_t row[64];
    memcpy(row, f.gates, 64);
    memcpy(f.gates, f.gates + 64, 64);
    memcpy(f.gates + 64, row, 64);
    test_mesh_word(f.edges, 0);
    test_mesh_word(f.edges + 4, 1);
    CHECK(portable_mesh_time_trial_init(&session, &f.course, &f.rules, 0, error, sizeof(error)) ==
              PORTABLE_MESH_OK &&
          session.state.checkpoint == 1);
    int jumped = 0, air_gate = 0;
    for (unsigned i = 0; i < 500u && session.state.phase != PORTABLE_MESH_TT_FINISHED; ++i)
    {
        PortableMeshKartInput input = {0, PORTABLE_MESH_ACCELERATE};
        if (!jumped && session.state.phase == PORTABLE_MESH_TT_RACING &&
            session.state.kart.grounded && session.state.kart.position_q16[1] < 16384)
        {
            input.buttons |= PORTABLE_MESH_HOP;
            jumped = 1;
        }
        CHECK(portable_mesh_time_trial_tick(&session, input, &events, error, sizeof(error)) ==
              PORTABLE_MESH_OK);
        if (session.state.checkpoint == 0 && !session.state.kart.grounded)
            air_gate = 1;
    }
    CHECK(jumped && air_gate && session.state.phase == PORTABLE_MESH_TT_FINISHED &&
          session.state.checkpoint == 1);
    test_mesh_course_init(&f);
    test_mesh_word(f.edges, 0);
    CHECK(portable_mesh_time_trial_init(&session, &f.course, &f.rules, 0, error, sizeof(error)) ==
          PORTABLE_MESH_UNSUPPORTED);
    test_mesh_course_init(&f);
    test_mesh_word(f.gates + 64 + 44, 0);
    CHECK(portable_mesh_time_trial_init(&session, &f.course, &f.rules, 0, error, sizeof(error)) ==
          PORTABLE_MESH_UNSUPPORTED);
    test_mesh_course_init(&f);
    test_mesh_word(f.gates + 64 + 16, 1073741824);
    CHECK(portable_mesh_time_trial_init(&session, &f.course, &f.rules, 0, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    for (unsigned i = 0; i < 300u; ++i)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 1}, &events, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(session.state.checkpoint == 0 && session.state.lap == 0);
    /* The same reversed plane explicitly permits two-way traversal when ONE_WAY is absent. */
    test_mesh_word(f.gates + 64 + 60, 0);
    CHECK(portable_mesh_time_trial_init(&session, &f.course, &f.rules, 0, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    for (unsigned i = 0; i < 500u && session.state.phase != PORTABLE_MESH_TT_FINISHED; ++i)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 1}, &events, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(session.state.phase == PORTABLE_MESH_TT_FINISHED);
    return 0;
}
static int natural_fall_and_basis(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    test_mesh_word(f.grid + 4u, (uint32_t)(-19 * 65536));
    PortableMeshTimeTrial session;
    PortableMeshKartEvents events;
    char error[160];
    CHECK(portable_mesh_time_trial_init(&session, &f.course, &f.rules, 0, error, sizeof(error)) ==
          PORTABLE_MESH_OK);
    CHECK(session.fall_limit_q16 == -20 * 65536);
    uint8_t wire[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u];
    CHECK(portable_mesh_time_trial_state_write(&session.state, wire, sizeof(wire)));
    test_mesh_word(wire + 6u * 4u,
                   1073741824); /* Components each fit, but forward now has length sqrt(2). */
    PortableMeshTimeTrialState untouched;
    memcpy(&untouched, &session.state, sizeof(untouched));
    uint8_t before[sizeof(untouched)];
    memcpy(before, &untouched, sizeof(before));
    CHECK(!portable_mesh_time_trial_state_read(&untouched, wire, sizeof(wire)) &&
          memcmp(before, &untouched, sizeof(before)) == 0);
    int fell = 0, respawned = 0;
    for (unsigned tick = 0; tick < 500u && !respawned; ++tick)
    {
        CHECK(portable_mesh_time_trial_tick(&session,
                                            (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                            &events, error, sizeof(error)) == PORTABLE_MESH_OK);
        fell |= session.state.kart.position_q16[2] < -65536;
        respawned = session.state.kart.respawn_ticks != 0;
    }
    CHECK(fell && respawned && session.state.kart.air_ticks == 0 && session.state.lap == 0 &&
          session.state.race_ticks < 320u);
    return 0;
}
static int arena_step(PortableMeshRacer *racer, uint16_t buttons)
{
    PortableMeshRacerTurn turn;
    char error[192];
    PortableMeshStatus status=portable_mesh_racer_move(racer,
        (PortableMeshKartInput){0,buttons},&turn,error,sizeof error);
    if(status==PORTABLE_MESH_OK)
        status=portable_mesh_racer_publish(racer,&turn,error,sizeof error);
    if(status!=PORTABLE_MESH_OK)fprintf(stderr,"arena step: %s\n",error);
    CHECK(status==PORTABLE_MESH_OK);
    CHECK(racer->state.checkpoint==PORTABLE_MESH_ABSENT &&
          racer->state.checkpoint_count==0 && racer->state.crossing_armed==0 &&
          racer->state.lap==0 && racer->state.finish_ticks==0);
    return 0;
}
static int arena_movement(void)
{
    PortableTestMeshCourse f;test_mesh_course_init(&f);
    test_mesh_word(f.meta+12u,16u);test_mesh_word(f.meta+16u,0u);
    f.course.tables[5].count=f.course.tables[6].count=0;
    PortableMeshRacer racer;char error[192];
    CHECK(portable_mesh_racer_init(&racer,&f.course,&f.rules,0,16,error,sizeof error)==PORTABLE_MESH_OK);
    for(unsigned i=0;i<180;++i)CHECK(arena_step(&racer,0)==0);
    CHECK(racer.state.tick==180 && racer.state.race_ticks==0 &&
          racer.state.phase==PORTABLE_MESH_RACER_RACING);
    CHECK(arena_step(&racer,PORTABLE_MESH_RESCUE)==0);
    CHECK(racer.state.kart.respawn_ticks==f.rules.respawn_ticks &&
          racer.state.kart.position_q16[1]==4*65536);
    for(unsigned i=0;i<f.rules.respawn_ticks;++i)CHECK(arena_step(&racer,PORTABLE_MESH_RESCUE)==0);
    CHECK(racer.state.kart.respawn_ticks==0); /* Held rescue has no second edge. */
    test_mesh_word(f.grid+4u,(uint32_t)(-19*65536));
    CHECK(portable_mesh_racer_init(&racer,&f.course,&f.rules,0,16,error,sizeof error)==PORTABLE_MESH_OK);
    int fell=0,respawned=0;
    for(unsigned tick=0;tick<500 && !respawned;++tick)
    {
        CHECK(arena_step(&racer,PORTABLE_MESH_ACCELERATE)==0);
        fell|=racer.state.kart.position_q16[2]<-65536;
        respawned=racer.state.kart.respawn_ticks!=0;
    }
    CHECK(fell && respawned && racer.state.kart.position_q16[1]==-19*65536);
    /* Two overlapping XY decks select their own authored recovery references. */
    uint8_t vertices[96],triangles[128],respawns[96];
    memcpy(vertices,f.vertices,48);memcpy(vertices+48,f.vertices,48);
    memcpy(triangles,f.triangles,64);memcpy(triangles+64,f.triangles,64);
    memset(respawns,0,sizeof respawns);
    for(unsigned deck=0;deck<2;++deck)
    {
        for(unsigned vertex=0;vertex<4;++vertex)
            test_mesh_word(vertices+(deck*4+vertex)*12+8,deck*8*65536);
        for(unsigned triangle=0;triangle<2;++triangle)
        {
            uint8_t *row=triangles+(deck*2+triangle)*32;
            const unsigned indices[2][3]={{0,1,2},{0,2,3}};
            for(unsigned axis=0;axis<3;++axis)test_mesh_word(row+axis*4,indices[triangle][axis]+deck*4);
            test_mesh_word(row+20,deck);
        }
        uint8_t *row=respawns+deck*48;
        test_mesh_word(row+4,4*65536);test_mesh_word(row+8,deck*8*65536);
        test_mesh_word(row+24,1u<<30u);test_mesh_word(row+28,UINT32_MAX);
        test_mesh_word(row+32,65536);test_mesh_word(row+40,UINT32_MAX);
    }
    f.course.tables[2].bytes=vertices;f.course.tables[2].count=8;
    f.course.tables[3].bytes=triangles;f.course.tables[3].count=4;
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','E','S','P'),2,48,respawns};
    for(unsigned deck=0;deck<2;++deck)
    {
        test_mesh_word(f.grid+4,4*65536);test_mesh_word(f.grid+8,deck*8*65536);
        CHECK(portable_mesh_racer_init(&racer,&f.course,&f.rules,0,16,error,sizeof error)==PORTABLE_MESH_OK);
        for(unsigned tick=0;tick<184;++tick)CHECK(arena_step(&racer,0)==0);
        CHECK(racer.state.kart.grounded && racer.state.last_respawn==deck);
        CHECK(arena_step(&racer,PORTABLE_MESH_RESCUE)==0);
        CHECK(racer.state.kart.respawn_ticks==f.rules.respawn_ticks &&
              racer.state.kart.position_q16[2]==(int32_t)(deck*8*65536)+
                  f.rules.kart_radius_q16+f.rules.ride_height_q16+f.rules.respawn_lift_q16);
    }
    /* Battle semantics remain absent even when a dual-mode course has real gates. */
    test_mesh_word(f.meta+12u,18u);test_mesh_word(f.meta+16u,1u);
    f.course.tables[5].count=2;f.course.tables[6].count=2;
    for(unsigned i=0;i<2;++i)test_mesh_word(respawns+i*48+28,0);
    CHECK(portable_mesh_racer_init(&racer,&f.course,&f.rules,0,16,error,sizeof error)==PORTABLE_MESH_OK);
    for(unsigned tick=0;tick<184;++tick)CHECK(arena_step(&racer,0)==0);
    CHECK(arena_step(&racer,PORTABLE_MESH_RESCUE)==0);
    return 0;
}
static int racer_effects_boundary(void)
{
    PortableTestMeshCourse fixture;
    test_mesh_course_init(&fixture);
    PortableMeshRacer original, enhanced;
    char error[192];
    CHECK(portable_mesh_racer_init(&original, &fixture.course, &fixture.rules, 0, 2,
        error, sizeof error) == PORTABLE_MESH_OK);
    memcpy(&enhanced, &original, sizeof enhanced);
    const PortableMeshKartEffects identity = {.speed_ratio_q16 = 65536,
        .body_scale_q16 = 65536, .boost_cap_ratio_q16 = 65536};
    for (unsigned tick = 0; tick < 192u; ++tick)
    {
        const PortableMeshKartInput input = {0, 1};
        PortableMeshRacerTurn a, b;
        CHECK(portable_mesh_racer_move(&original, input, &a, error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_racer_move_effects(&enhanced, input, &identity, &b,
            error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_racer_publish(&original, &a, error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_racer_publish(&enhanced, &b, error, sizeof error) == PORTABLE_MESH_OK);
        uint8_t first[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u];
        uint8_t second[sizeof first];
        CHECK(portable_mesh_time_trial_state_write(&original.state, first, sizeof first));
        CHECK(portable_mesh_time_trial_state_write(&enhanced.state, second, sizeof second));
        CHECK(memcmp(first, second, sizeof first) == 0);
        CHECK(a.publish == b.publish && a.contact == b.contact &&
              a.finish_fraction_q30 == b.finish_fraction_q30);
    }
    unsigned char before[sizeof enhanced];
    memcpy(before, &enhanced, sizeof enhanced);
    PortableMeshRacerTurn turn;
    memset(&turn, 0xa5, sizeof turn);
    unsigned char turn_before[sizeof turn];
    memcpy(turn_before, &turn, sizeof turn);
    PortableMeshKartEffects bad = identity;
    bad.body_scale_q16 = 0;
    CHECK(portable_mesh_racer_move_effects(&enhanced, (PortableMeshKartInput){0, 1}, &bad,
        &turn, error, sizeof error) == PORTABLE_MESH_INVALID);
    CHECK(memcmp(before, &enhanced, sizeof enhanced) == 0 &&
          memcmp(turn_before, &turn, sizeof turn) == 0);
    CHECK(strstr(error, "invalid dynamics effects") != NULL);
    PortableMeshKartEffects inhibited = identity;
    inhibited.inhibit_controls = inhibited.cancel_drift = 1;
    CHECK(portable_mesh_racer_move_effects(&enhanced, (PortableMeshKartInput){20000, 8}, &inhibited,
        &turn, error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(enhanced.state.kart.respawn_ticks == fixture.rules.respawn_ticks);
    return 0;
}

/* Ground COLT recovery points name canonical apertures. Accepting a parallel
 * aperture must retain the same safe anchor without changing branch authority. */
static int alternative_checkpoint_recovery(void)
{
    for (unsigned variant = 0; variant < 5u; ++variant)
    {
        PortableTestMeshCourse f;
        test_mesh_course_init(&f);
        uint8_t gates[320], edges[32], safe[48] = {0};
        memcpy(gates, f.gates, 128);
        memcpy(gates + 128, f.gates + 64, 64);
        memcpy(gates + 192, f.gates + 64, 64);
        memcpy(gates + 256, f.gates + 64, 64);
        /* Stage1 has a distant canonical aperture and a reachable alternative;
         * stage2 has two choices, listed in opposite orders by stage1. */
        test_mesh_word(gates + 64, 10u * 65536u);
        test_mesh_word(gates + 64 + 36, 65536u);
        for (unsigned i = 3; i < 5; ++i)
        {
            test_mesh_word(gates + i * 64 + 4, (uint32_t)(-10 * 65536));
            test_mesh_word(gates + i * 64 + 44, 2);
            test_mesh_word(gates + i * 64 + 48, 2);
        }
        const uint32_t first[5] = {0, 2, 4, 6, 7};
        const uint32_t counts[5] = {2, 2, 2, 1, 1};
        const uint32_t targets[8] = {1, 2, 3, 4, 4, 3, 0, 0};
        for (unsigned i = 0; i < 5; ++i)
        {
            test_mesh_word(gates + i * 64 + 52, first[i]);
            test_mesh_word(gates + i * 64 + 56, counts[i]);
        }
        for (unsigned i = 0; i < 8; ++i)
            test_mesh_word(edges + i * 4, targets[i]);
        f.course.tables[5] = (PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('C','P','N','T'), 5, 64, gates};
        f.course.tables[6] = (PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('C','P','E','D'), 8, 4, edges};
        test_mesh_word(safe, 3 * 65536);
        test_mesh_word(safe + 4, (uint32_t)(-2 * 65536));
        test_mesh_word(safe + 24, 1073741824);
        test_mesh_word(safe + 28, 1);
        f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('R','E','S','P'), 1, 48, safe};
        test_mesh_word(f.triangles + 20, 0);
        test_mesh_word(f.triangles + 32 + 20, 0);
        PortableMeshTimeTrialSession trial;
        char error[192];
        CHECK(portable_mesh_time_trial_init(&trial, &f.course, &f.rules, 0,
            error, sizeof error) == PORTABLE_MESH_OK);
        trial.state.phase = PORTABLE_MESH_RACER_RACING;
        trial.state.countdown_ticks = 0;
        trial.state.kart.position_q16[1] = 512;
        trial.state.kart.velocity_q16[1] = -65536;
        trial.state.kart.speed_q16 = 65536;
        /* Future progress and a different branch remain ineligible. A mutated
         * group/edge also exercises defensive publication after initialization. */
        if (variant == 1)
            test_mesh_word(safe + 28, 3);
        else if (variant == 2)
            test_mesh_word(gates + 64 + 44, 7);
        else if (variant == 3)
            test_mesh_word(gates + 64 + 56, 1);
        else if (variant == 4)
            test_mesh_word(edges + 2 * 4, 5);
        const PortableMeshTimeTrialSession before = trial;
        PortableMeshKartEvents events, untouched;
        memset(&events, 0x5a, sizeof events);
        untouched = events;
        const PortableMeshStatus status = portable_mesh_time_trial_tick(&trial,
            (PortableMeshKartInput){0, 0}, &events, error, sizeof error);
        if (variant == 4)
        {
            CHECK(status == PORTABLE_MESH_INVALID && strstr(error, "physics/checkpoint"));
            CHECK(!memcmp(&trial, &before, sizeof trial) &&
                  !memcmp(&events, &untouched, sizeof events));
            continue;
        }
        CHECK(status == PORTABLE_MESH_OK && trial.state.checkpoint == 2 &&
              trial.state.checkpoint_count == 1 && trial.state.kart.grounded &&
              trial.state.kart.respawn_index == 0);
        if (variant)
        {
            CHECK(trial.state.last_respawn == PORTABLE_MESH_ABSENT);
            continue;
        }
        CHECK(trial.state.last_respawn == 0);
        /* Both manual rescue and a genuine fall restore the canonical aperture
         * with identical semantic progress, not the initial GRID. */
        PortableMeshTimeTrialSession rescue = trial;
        CHECK(portable_mesh_time_trial_tick(&rescue,
            (PortableMeshKartInput){0, PORTABLE_MESH_RESCUE}, &events,
            error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(rescue.state.kart.position_q16[0] == 3 * 65536 &&
              rescue.state.kart.position_q16[1] == -2 * 65536 &&
              rescue.state.checkpoint == 1 && rescue.state.checkpoint_count == 1 &&
              rescue.state.crossing_armed && !rescue.state.lap);
        trial.state.kart.position_q16[0] = 19 * 65536;
        trial.state.kart.forward_q30[0] = 1073741824;
        trial.state.kart.forward_q30[1] = 0;
        trial.state.kart.velocity_q16[0] = 10 * 65536;
        trial.state.kart.velocity_q16[1] = 0;
        trial.state.kart.speed_q16 = 10 * 65536;
        unsigned fell = 0;
        for (unsigned tick = 0; tick < 500u && !trial.state.kart.respawn_ticks; ++tick)
        {
            CHECK(portable_mesh_time_trial_tick(&trial,
                (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events,
                error, sizeof error) == PORTABLE_MESH_OK);
            fell |= trial.state.kart.position_q16[2] < -65536;
        }
        CHECK(fell && trial.state.kart.respawn_ticks && trial.state.last_respawn == 0 &&
              trial.state.kart.position_q16[0] == 3 * 65536 &&
              trial.state.kart.position_q16[1] == -2 * 65536 &&
              trial.state.checkpoint == 1 && trial.state.checkpoint_count == 1 &&
              trial.state.crossing_armed && !trial.state.lap);
        /* A recovery point can sit just before its next gate: a crossing
         * inside the recovery grace still counts. */
        while (trial.state.kart.respawn_ticks)
            CHECK(portable_mesh_time_trial_tick(&trial, (PortableMeshKartInput){0, 0}, &events,
                error, sizeof error) == PORTABLE_MESH_OK);
        trial.state.respawn_grace = 2;
        trial.state.kart.position_q16[0] = 0;
        trial.state.kart.position_q16[1] = -10 * 65536 + 512;
        trial.state.kart.position_q16[2] = before.state.kart.position_q16[2];
        memset(trial.state.kart.velocity_q16, 0, sizeof trial.state.kart.velocity_q16);
        trial.state.kart.velocity_q16[1] = -65536;
        trial.state.kart.forward_q30[0] = 0;
        trial.state.kart.forward_q30[1] = -1073741824;
        trial.state.kart.speed_q16 = 65536;
        CHECK(portable_mesh_time_trial_tick(&trial, (PortableMeshKartInput){0, 0}, &events,
            error, sizeof error) == PORTABLE_MESH_OK);
        CHECK((trial.state.checkpoint == 3 || trial.state.checkpoint == 4) &&
              trial.state.checkpoint_count == 2 && trial.state.respawn_grace == 1 &&
              trial.state.last_respawn == 0);
    }
    return 0;
}

/* A respawn beside the next gate must not turn contact invulnerability into
 * a lap-counting blackout. Exercise both TT and the shared VS race owner. */
static int fall_then_finish(void)
{
    for (unsigned shared = 0; shared < 2; ++shared)
    {
        PortableTestMeshCourse f;
        uint8_t grids[128], safe[48] = {0};
        local_course(&f, grids);
        f.rules.respawn_grace_ticks = 60;
        test_mesh_word(safe + 4, (uint32_t)(-4 * 65536 + 128));
        test_mesh_word(safe + 24, 1073741824);
        test_mesh_word(safe + 28, 1);
        f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('R','E','S','P'), 1, 48, safe};
        test_mesh_word(f.triangles + 20, 0);
        test_mesh_word(f.triangles + 32 + 20, 0);
        PortableMeshTimeTrial trial;
        PortableMeshRaceSession race;
        PortableMeshKartEvents events[2];
        char error[192];
        CHECK(portable_mesh_time_trial_init(&trial, &f.course, &f.rules, 0,
            error, sizeof error) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_race_init(&race, &f.course, &f.rules, 2, NULL,
            error, sizeof error) == PORTABLE_MESH_OK);
        PortableMeshRacerState *state = shared ? &race.state.racers[0] : &trial.state;
        state->phase = PORTABLE_MESH_RACER_RACING;
        state->countdown_ticks = 0;
        state->kart.position_q16[0] = 0;
        state->kart.position_q16[1] = 512;
        state->kart.velocity_q16[1] = -65536;
        state->kart.speed_q16 = 65536;
        unsigned fell = 0, recovered = 0, crossed = 0;
        for (unsigned tick = 0; tick < 600 && !state->lap; ++tick)
        {
            PortableMeshKartInput inputs[2] = {{0, PORTABLE_MESH_ACCELERATE}, {0, 0}};
            CHECK((shared ? portable_mesh_race_tick(&race, inputs, events, error, sizeof error)
                          : portable_mesh_time_trial_tick(&trial, inputs[0], events,
                                error, sizeof error)) == PORTABLE_MESH_OK);
            if (!tick)
            {
                CHECK(state->checkpoint == 1 && state->last_respawn == 0);
                /* Leave the finite road sideways, then let gravity recover us. */
                state->kart.position_q16[0] = 19 * 65536;
                state->kart.forward_q30[0] = 1073741824;
                state->kart.forward_q30[1] = 0;
                state->kart.velocity_q16[0] = 10 * 65536;
                state->kart.velocity_q16[1] = 0;
                state->kart.speed_q16 = 10 * 65536;
            }
            fell |= state->kart.position_q16[2] < -65536;
            if (state->kart.respawn_ticks)
            {
                CHECK(fell && state->checkpoint == 1 && !state->lap);
                recovered = 1;
            }
            if (recovered && state->kart.position_q16[1] < -4 * 65536)
            {
                CHECK(state->respawn_grace && state->lap == 1);
                crossed = 1;
            }
        }
        CHECK(fell && recovered && crossed && state->lap == 1 &&
              state->phase == PORTABLE_MESH_RACER_FINISHED);
    }
    return 0;
}

static int recovery_cannot_skip_next_lap(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    test_mesh_word(f.meta + 16, 2);
    uint8_t safe[48] = {0};
    test_mesh_word(safe + 4, (uint32_t)(-3 * 65536));
    test_mesh_word(safe + 24, 1073741824);
    test_mesh_word(safe + 28, 1);
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','E','S','P'), 1, 48, safe};
    test_mesh_word(f.triangles + 20, 0);
    test_mesh_word(f.triangles + 32 + 20, 0);
    PortableMeshTimeTrial trial;
    PortableMeshKartEvents events;
    char error[192];
    CHECK(portable_mesh_time_trial_init(&trial, &f.course, &f.rules, 0,
        error, sizeof error) == PORTABLE_MESH_OK);
    trial.state.phase = PORTABLE_MESH_RACER_RACING;
    trial.state.countdown_ticks = 0;
    for (unsigned tick = 0; tick < 300 && !trial.state.lap; ++tick)
        CHECK(portable_mesh_time_trial_tick(&trial,
            (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events,
            error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(trial.state.lap == 1 && trial.state.checkpoint == 0);
    CHECK(portable_mesh_time_trial_tick(&trial,
        (PortableMeshKartInput){0, PORTABLE_MESH_RESCUE}, &events,
        error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(trial.state.lap == 1 && trial.state.checkpoint == 0 &&
          !trial.state.crossing_armed && trial.state.kart.position_q16[1] == 4 * 65536);
    /* The next lap still needs its gate, even if the finish is crossed first. */
    for (unsigned tick = 0; tick < f.rules.respawn_ticks; ++tick)
        CHECK(portable_mesh_time_trial_tick(&trial, (PortableMeshKartInput){0, 0},
            &events, error, sizeof error) == PORTABLE_MESH_OK);
    trial.state.kart.position_q16[1] = -4 * 65536 + 512;
    trial.state.kart.velocity_q16[1] = -65536;
    trial.state.kart.speed_q16 = 65536;
    CHECK(portable_mesh_time_trial_tick(&trial, (PortableMeshKartInput){0, 0},
        &events, error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(trial.state.lap == 1 && trial.state.checkpoint == 0);
    return 0;
}

int main(void)
{
    PortableTestMeshCourse fixture;
    test_mesh_course_init(&fixture);
    PortableMeshTimeTrial session;
    char error[160];
    PortableMeshKartEvents e;
    CHECK(portable_mesh_time_trial_init(&session, &fixture.course, &fixture.rules, 0, error,
                                        sizeof(error)) == PORTABLE_MESH_OK);
    uint8_t bytes[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u];
    PortableMeshTimeTrialState decoded;
    CHECK(portable_mesh_time_trial_state_write(&session.state, bytes, sizeof(bytes)));
    CHECK(portable_mesh_time_trial_state_read(&decoded, bytes, sizeof(bytes)));
    CHECK(portable_mesh_time_trial_checksum(&decoded) ==
          portable_mesh_time_trial_checksum(&session.state));
    uint8_t before[sizeof(decoded)];
    memcpy(before, &decoded, sizeof(decoded));
    bytes[0] = 255;
    bytes[1] = 255;
    bytes[2] = 255;
    bytes[3] = 127;
    CHECK(!portable_mesh_time_trial_state_read(&decoded, bytes, sizeof(bytes)) &&
          memcmp(before, &decoded, sizeof(decoded)) == 0);
    for (unsigned i = 0; i < 180u; ++i)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 0}, &e, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(session.state.phase == PORTABLE_MESH_TT_RACING && session.state.race_ticks == 0);
    PortableMeshTimeTrial repeated = session;
    for (unsigned i = 0; i < 300u && session.state.phase != PORTABLE_MESH_TT_FINISHED; ++i)
    {
        const PortableMeshKartInput input = {0, 1};
        const PortableMeshStatus status =
            portable_mesh_time_trial_tick(&session, input, &e, error, sizeof(error));
        CHECK(status == PORTABLE_MESH_OK);
        CHECK(portable_mesh_time_trial_tick(&repeated, input, &e, error, sizeof(error)) ==
              PORTABLE_MESH_OK);
        CHECK(portable_mesh_time_trial_checksum(&session.state) ==
              portable_mesh_time_trial_checksum(&repeated.state));
    }
    CHECK(session.state.phase == PORTABLE_MESH_TT_FINISHED && session.state.lap == 1 &&
          session.state.finish_ticks == session.state.race_ticks);
    CHECK(portable_mesh_time_trial_init(&session, &fixture.course, &fixture.rules, 0, error,
                                        sizeof(error)) == PORTABLE_MESH_OK);
    for (unsigned i = 0; i < 180u; ++i)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 0}, &e, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 8}, &e, error,
                                        sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(session.state.kart.respawn_ticks == fixture.rules.respawn_ticks &&
          session.state.lap == 0 && session.state.checkpoint == 0);
    for (uint32_t i = 0; i < fixture.rules.respawn_ticks; ++i)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 8}, &e, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(!session.state.kart.respawn_ticks);
    CHECK(alternative_checkpoint_recovery() == 0);
    CHECK(fall_then_finish() == 0);
    CHECK(recovery_cannot_skip_next_lap() == 0);
    CHECK(arena_movement() == 0);
    CHECK(racer_effects_boundary() == 0);
    CHECK(local_world() == 0);
    CHECK(field_world() == 0);
    CHECK(gate_semantics() == 0);
    CHECK(natural_fall_and_basis() == 0);
    return 0;
}
