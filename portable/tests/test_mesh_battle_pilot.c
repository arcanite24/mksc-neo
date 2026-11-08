#include "mesh_battle_pilot.h"
#include "mesh_time_trial_pilot.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "Battle pilot line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define Q 65536
#define U 1073741824
typedef struct
{
    PortableTestMeshCourse floor;
    PortableMeshItemRules rules;
    PortableMeshRaceSession race;
    uint8_t items[96], header[32], nodes[128], edges[16], wall_vertices[84], wall_triangles[96];
} Fixture;
static Fixture f;
static PortableMeshRaceSession before;
static PortableMeshKartInput drive;
static PortableMeshItemControls control;
static void point(uint8_t *row, int32_t x, int32_t y, int32_t z)
{
    test_mesh_word(row, (uint32_t)x);
    test_mesh_word(row + 4, (uint32_t)y);
    test_mesh_word(row + 8, (uint32_t)z);
}
static int setup(void)
{
    memset(&f, 0, sizeof f);
    test_mesh_course_init(&f.floor);
    test_mesh_word(f.floor.meta + 12, 16);
    test_mesh_word(f.floor.meta + 16, 0);
    const int32_t boxes[3][3] = {{0, -6*Q, Q}, {4*Q, 0, Q}, {0, -2*Q, 10*Q}};
    for (unsigned i = 0; i < 3; ++i)
    {
        point(f.items + i*32, boxes[i][0], boxes[i][1], boxes[i][2]);
        test_mesh_word(f.items + i*32 + 12, Q);
        test_mesh_word(f.items + i*32 + 20, 30);
    }
    test_mesh_word(f.header + 8, 4);
    test_mesh_word(f.header + 16, 1);
    const int32_t route[4][3] = {{-8*Q,-8*Q,0},{8*Q,-8*Q,0},{8*Q,8*Q,0},{-8*Q,8*Q,0}};
    for (unsigned i = 0; i < 4; ++i)
    {
        point(f.nodes + i*32, route[i][0], route[i][1], route[i][2]);
        test_mesh_word(f.nodes + i*32 + 12, 2*Q);
        test_mesh_word(f.nodes + i*32 + 16, 8*Q);
        test_mesh_word(f.nodes + i*32 + 20, i);
        test_mesh_word(f.nodes + i*32 + 24, 1);
        test_mesh_word(f.edges + i*4, (i+1)%4);
    }
    PortableCourse3DView *course = &f.floor.course;
    course->tables[course->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('I','T','E','M'),3,32,f.items};
    course->tables[course->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R','T','H','D'),1,32,f.header};
    course->tables[course->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R','T','N','D'),4,32,f.nodes};
    course->tables[course->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R','T','E','D'),4,4,f.edges};
    uint8_t bytes[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(bytes);
    const PortableAssetSectionView section = {.kind=103,.content_id=41,.version=1,.bytes=bytes,.size=sizeof bytes};
    CHECK(portable_mesh_item_rules_asset_open(&section, &f.rules));
    f.rules.cpu_use_distance_q16 = 8*Q;
    f.rules.cpu_backward_distance_q16 = 4*Q;
    f.race.course = course;
    f.race.rules = &f.floor.rules;
    f.race.fall_limit_q16 = -20 * Q; /* Authored flat floor Z=0, normal recovery margin. */
    f.race.mode = 16;
    f.race.state.player_count = f.race.human_count = 4;
    f.race.item_rules = &f.rules;
    f.race.items_enabled = 1;
    const PortableMeshBattleConfig battle = {4,3,1};
    CHECK(portable_mesh_battle_init(&f.race.battle,&battle,NULL,0));
    const int32_t forward[3] = {0,-U,0};
    for (unsigned i = 0; i < 4; ++i)
    {
        const int32_t position[3] = {i ? 12*Q : 0, i ? 12*Q : 0, Q};
        CHECK(portable_mesh_kart_init(&f.race.state.racers[i].kart,&f.floor.rules,position,forward)==PORTABLE_MESH_OK);
        f.race.state.racers[i].phase = PORTABLE_MESH_RACER_RACING;
        f.race.state.racers[i].kart.grounded = 1;
        f.race.state.standing_order[i] = (uint8_t)i;
        f.race.state.standing_rank[i] = (uint8_t)(i + 1u);
    }
    const PortableMeshItemsConfig config = {.course=course,.rules=&f.rules,.kart_rules=&f.floor.rules,
        .mode=16,.count=4,.human_mask=15};
    CHECK(portable_mesh_items_init(&f.race.items,&config,123,NULL,0)==PORTABLE_MESH_ITEMS_OK);
    return 0;
}
static PortableMeshStatus pilot(char *error, size_t capacity)
{
    return portable_mesh_battle_pilot(&f.race,0,&drive,&control,error,capacity);
}
static void rival(unsigned i, int32_t x, int32_t y, int32_t z)
{
    int32_t *p=f.race.state.racers[i].kart.position_q16;
    p[0]=x;p[1]=y;p[2]=z;
}
static void give(uint16_t item)
{
    f.race.items.inventory.racers[0] = (PortableMeshItemRacer){.held_item=item,.settled_item=item,.preview_item=item};
}
static int boxes_and_targets(void)
{
    CHECK(!setup());
    before=f.race;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering>30000 && drive.buttons==PORTABLE_MESH_ACCELERATE);
    CHECK(!control.item_pressed && !memcmp(&before,&f.race,sizeof before));
    f.race.items.boxes.cooldowns[1]=10;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering==0);
    /* Nearby upper-deck XY does not beat a nearer genuine fullXYZ box. */
    CHECK(!control.item_pressed);
    give(0x1000);
    rival(1,3*Q,0,Q);rival(2,0,-5*Q,Q);rival(3,0,-Q,9*Q);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering>30000 && !control.item_pressed);
    f.race.items.status.racers[1].star_ticks=10;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering==0 && control.item_pressed==1);
    f.race.items.previous_item[0]=1;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !control.item_pressed);
    f.race.items.previous_item[0]=0;
    f.race.items.status.racers[2].boo_invisible_ticks=10;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !control.item_pressed); /* >8m fullXYZ range. */
    return 0;
}
static int attachments_and_bomb(void)
{
    CHECK(!setup());
    rival(1,0,-3*Q,Q);
    f.race.items.actors[0]=(PortableMeshItemActor){.owner=0,.phase=PORTABLE_MESH_ACTOR_HELD,.item=0x1002,.serial=1};
    f.race.items.previous_item[0]=1;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !control.item_pressed && !control.aim_backward);
    rival(1,3*Q,0,Q);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && control.item_pressed && drive.steering>30000);
    rival(1,0,3*Q,Q);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !control.item_pressed && control.aim_backward && drive.steering==32767);
    f.race.items.actors[0].item=0x1005;
    rival(1,0,-3*Q,Q);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !control.item_pressed && control.aim_forward);
    f.race.items.actors[0].phase=PORTABLE_MESH_ACTOR_ORBIT;
    f.race.items.actors[0].item=0x1000;
    f.race.items.previous_item[0]=0;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && control.item_pressed);
    f.race.items.release_lock[0]=2;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !control.item_pressed);
    f.race.items.release_lock[0]=0;
    f.race.battle.balloons[0]=0;
    f.race.battle.elimination[0].revenge_bomb=1;
    f.race.items.status.racers[1].star_ticks=10;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering==0 && !control.item_pressed);
    f.race.items.status.racers[1].boo_invisible_ticks=10;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering>0 && !control.item_pressed);
    /* A rival inside the low-speed turning circle (about 1.5 m) would be
     * orbited forever: drive straight out first. */
    f.race.items.status.racers[1].boo_invisible_ticks=0;
    f.race.items.status.racers[1].star_ticks=0;
    rival(1,Q,0,Q);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering==0 && drive.buttons==PORTABLE_MESH_ACCELERATE);
    rival(1,4*Q,0,Q);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering>0);
    f.race.bomb_recovery_ticks[0]=1;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !drive.buttons && !drive.steering && !control.item_pressed);
    return 0;
}
static int patrol_and_failures(void)
{
    CHECK(!setup());
    for(unsigned i=0;i<3;++i)f.race.items.boxes.cooldowns[i]=10;
    PortableMeshTimeTrialSession route={.course=f.race.course,.rules=f.race.rules,.mode=16,.state=f.race.state.racers[0]};
    PortableMeshKartInput expected;
    CHECK(portable_mesh_time_trial_pilot(&route,&expected,NULL,0)==PORTABLE_MESH_OK);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering==expected.steering && drive.buttons==expected.buttons && !control.item_pressed);
    test_mesh_word(f.header+16,0);
    drive=(PortableMeshKartInput){123,456};control=(PortableMeshItemControls){1,1,1,1};
    PortableMeshKartInput old_drive=drive;PortableMeshItemControls old_control=control;
    char error[192];
    before=f.race;
    CHECK(pilot(error,sizeof error)==PORTABLE_MESH_UNSUPPORTED && strstr(error,"Battle") && strstr(error,"course=65536"));
    CHECK(!memcmp(&old_drive,&drive,sizeof drive) && !memcmp(&old_control,&control,sizeof control) && !memcmp(&before,&f.race,sizeof before));
    CHECK(!setup());
    f.items[12]=0;f.items[13]=0;f.items[14]=0;f.items[15]=0;
    drive=old_drive;control=old_control;before=f.race;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_INVALID && !memcmp(&old_drive,&drive,sizeof drive) &&
        !memcmp(&old_control,&control,sizeof control) && !memcmp(&before,&f.race,sizeof before));
    CHECK(!setup());
    f.race.items.prepared=1;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_INVALID);
    f.race.items.prepared=0;
    f.race.state.racers[0].phase=PORTABLE_MESH_RACER_COUNTDOWN;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !drive.buttons && !drive.steering && !control.item_pressed);
    f.race.state.racers[0].phase=PORTABLE_MESH_RACER_RACING;
    f.race.state.racers[0].kart.respawn_ticks=1;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && !drive.buttons && !control.item_pressed);
    CHECK(portable_mesh_battle_pilot(NULL,0,&drive,&control,NULL,0)==PORTABLE_MESH_INVALID);
    CHECK(portable_mesh_battle_pilot(&f.race,8,&drive,&control,NULL,0)==PORTABLE_MESH_INVALID);
    return 0;
}
static int blocked_face_patrol(void)
{
    CHECK(!setup());
    memcpy(f.wall_vertices,f.floor.vertices,sizeof f.floor.vertices);
    memcpy(f.wall_triangles,f.floor.triangles,sizeof f.floor.triangles);
    point(f.wall_vertices+48,2*Q,-2*Q,0);
    point(f.wall_vertices+60,2*Q,2*Q,0);
    point(f.wall_vertices+72,2*Q,0,4*Q);
    test_mesh_word(f.wall_triangles+64,4);test_mesh_word(f.wall_triangles+68,5);test_mesh_word(f.wall_triangles+72,6);
    test_mesh_word(f.wall_triangles+64+20,UINT32_MAX);
    test_mesh_word(f.wall_triangles+64+24,UINT32_MAX);
    test_mesh_word(f.wall_triangles+64+28,UINT32_MAX);
    for(unsigned i=0;i<f.floor.course.table_count;++i)
    {
        PortableCourse3DTable *t=&f.floor.course.tables[i];
        if(t->tag==PORTABLE_COURSE_3D_TAG('C','O','L','V')){t->bytes=f.wall_vertices;t->count=7;}
        if(t->tag==PORTABLE_COURSE_3D_TAG('C','O','L','T')){t->bytes=f.wall_triangles;t->count=3;}
    }
    PortableMeshTimeTrialSession route={.course=f.race.course,.rules=f.race.rules,.mode=16,.state=f.race.state.racers[0]};
    PortableMeshKartInput expected;
    CHECK(portable_mesh_time_trial_pilot(&route,&expected,NULL,0)==PORTABLE_MESH_OK);
    CHECK(pilot(NULL,0)==PORTABLE_MESH_OK && drive.steering==expected.steering && drive.buttons==expected.buttons);
    test_mesh_word(f.wall_triangles+72,999);
    const PortableMeshKartInput saved_drive=drive;
    const PortableMeshItemControls saved_control=control;
    CHECK(pilot(NULL,0)==PORTABLE_MESH_INVALID && !memcmp(&saved_drive,&drive,sizeof drive) &&
        !memcmp(&saved_control,&control,sizeof control));
    return 0;
}
static int close_range_aiming(void)
{
    CHECK(!setup());
    /* Two side-on armed rivals start moving together. Ordinary accepted
     * world turns must let both aim and actually launch, rather than orbit. */
    rival(0, -2 * Q, 0, Q);
    rival(1, 2 * Q, 0, Q);
    rival(2, -12 * Q, 12 * Q, Q);
    rival(3, 12 * Q, 12 * Q, Q);
    for (unsigned i = 0; i < 2; ++i)
    {
        f.race.state.racers[i].kart.speed_q16 = 3 * Q;
        f.race.state.racers[i].kart.velocity_q16[1] = -3 * Q;
        f.race.items.inventory.racers[i] = (PortableMeshItemRacer){
            .held_item = 0x1002, .settled_item = 0x1002, .preview_item = 0x1002};
    }
    for (unsigned i = 2; i < 4; ++i)
    {
        f.race.battle.balloons[i] = 0;
        f.race.battle.elimination[i].revenge_bomb = 1;
    }
    unsigned used = 0, released = 0;
    for (unsigned tick = 0; tick < 480 && released != 3u; ++tick)
    {
        PortableMeshKartInput inputs[4] = {{0}};
        PortableMeshItemControls item_inputs[4] = {{0}};
        for (unsigned i = 0; i < 2; ++i)
        {
            before = f.race;
            CHECK(portable_mesh_battle_pilot(&f.race, i, &inputs[i], &item_inputs[i], NULL, 0) ==
                  PORTABLE_MESH_OK);
            CHECK(!memcmp(&before, &f.race, sizeof before));
            if (!tick)
                CHECK(inputs[i].buttons == PORTABLE_MESH_BRAKE && !item_inputs[i].item_pressed);
        }
        PortableMeshKartEvents events[4];
        char error[256];
        const PortableMeshStatus step = portable_mesh_race_tick_items(
            &f.race, inputs, item_inputs, events, error, sizeof error);
        if (step != PORTABLE_MESH_OK)
            fprintf(stderr, "Battle pilot accepted step %u: %s\n", tick, error);
        CHECK(step == PORTABLE_MESH_OK);
        for (unsigned e = 0; e < f.race.item_events.count; ++e)
        {
            const PortableMeshItemEvent *event = &f.race.item_events.values[e];
            if (event->racer >= 2 || event->item != 0x1002)
                continue;
            if (event->kind == PORTABLE_MESH_EVENT_USE)
                used |= 1u << event->racer;
            if (event->kind == PORTABLE_MESH_EVENT_RELEASE)
                released |= 1u << event->racer;
        }
    }
    CHECK(used == 3u && released == 3u);
    return 0;
}
int main(void)
{
    CHECK(!boxes_and_targets() && !attachments_and_bomb() && !patrol_and_failures() &&
          !blocked_face_patrol() && !close_range_aiming());
    puts(
        "Mesh Battle pilot: PASS (real boxes, eligible targets, held edges, Bomb pursuit, patrol, rollback)");
    return 0;
}
