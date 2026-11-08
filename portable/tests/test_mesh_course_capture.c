#include "mesh_course_actors.h"
#include "mesh_course_fixture.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define U INT32_C(1073741824)
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"capture line %d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct
{
    PortableMeshCourseActorsConfig config;
    PortableMeshCourseActors world;
    PortableCourse3DView course;
    PortableTestMeshCourse geometry;
    PortableMeshKartRules rules;
    PortableMeshItemRules items;
    PortableMeshItemStatusOwner status;
    PortableMeshRacer racers[2];
} Fixture;
static int fixture(Fixture *f, unsigned count)
{
    memset(f,0,sizeof *f);
    f->config=(PortableMeshCourseActorsConfig){.course_id=7,.profile_id=8,.revision=1,
        .scale_q16=6554,.piranha_count=1,.capture_surface_count=1,
        .capture_forward_speed_q16=3*Q,.capture_up_speed_q16=2*Q};
    CHECK(portable_piranha_init(&f->config.initial_piranhas[0],100*Q,100*Q,0,0));
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f->config,&f->world,error,sizeof error));
    test_mesh_course_init(&f->geometry);
    f->course=f->geometry.course;
    f->course.id=7;
    f->rules=test_mesh_rules();
    f->items.profile_id=f->items.revision=1;
#define MINIMUM(name, minimum, maximum) f->items.name=minimum;
    PORTABLE_MESH_ITEM_FIELDS(MINIMUM)
#undef MINIMUM
    f->items.lightning_scale_ratio_q16=Q/2;
    for(unsigned i=0;i<10;++i)f->items.roulette.preview[i]=(uint16_t)(0x1000+i);
    for(unsigned i=0;i<27;++i)f->items.roulette.timeline[i]=2;
    for(unsigned i=0;i<8;++i)
        f->items.player_weights[i][0]=f->items.cpu_weights[i][0]=(PortableGpItemWeight){0x1007,1};
    CHECK(portable_mesh_item_status_init(&f->status,count,(1u<<count)-1u)==PORTABLE_MESH_ITEM_STATUS_OK);
    for (unsigned i=0;i<count;++i)
    {
        f->racers[i]=(PortableMeshRacer){.course=&f->course,.rules=&f->rules};
        const int32_t position[3]={Q,0,f->rules.kart_radius_q16+f->rules.ride_height_q16};
        const int32_t forward[3]={0,-U,0};
        CHECK(portable_mesh_kart_init(&f->racers[i].state.kart,&f->rules,position,forward)==PORTABLE_MESH_OK);
        f->racers[i].state.phase=PORTABLE_MESH_RACER_RACING;
        f->racers[i].state.kart.grounded=1;
        f->racers[i].state.kart.contact_triangle=f->racers[i].state.kart.contact_surface=0;
    }
    return 0;
}
static int admit(Fixture *f, unsigned count, uint8_t recovering, PortableMeshCourseActorEvents *events)
{
    PortableMeshRacerState racers[2];
    for (unsigned i=0;i<count;++i)racers[i]=f->racers[i].state;
    char error[160];
    return portable_mesh_course_capture_admit(&f->config,&f->world,&f->course,&f->rules,
        &f->items,&f->status,racers,count,recovering,events,error,sizeof error);
}
static int sequence(void)
{
    Fixture f;
    CHECK(!fixture(&f,1));
    PortableMeshCourseActorEvents events={0};
    CHECK(admit(&f,1,0,&events));
    CHECK(events.capture_mask==1 && f.world.capture[0].remaining==92);
    const uint64_t initial=portable_mesh_course_actors_checksum(&f.world);
    int handled=9;char error[160];PortableMeshRacerTurn turn;
    CHECK(portable_mesh_course_capture_move(&f.config,&f.world,0,&f.racers[0],
        (PortableMeshKartInput){.buttons=5},&turn,&handled,&events,error,sizeof error));
    CHECK(handled==1 && !turn.contact && !turn.publish && f.racers[0].state.tick==1);
    CHECK(f.world.capture[0].remaining==92 && f.racers[0].state.kart.position_q16[0]==Q);
    for (unsigned n=1;n<=92;++n)
    {
        ++f.world.tick;
        CHECK(portable_mesh_course_capture_move(&f.config,&f.world,0,&f.racers[0],
            (PortableMeshKartInput){.buttons=5},&turn,&handled,&events,error,sizeof error));
        if (n<92)
        {
            CHECK(handled==1 && f.world.capture[0].remaining==92-n);
            CHECK(f.racers[0].state.kart.position_q16[0]==Q-(int32_t)(n<33?n:33)*(Q/32));
            CHECK(!turn.contact && !turn.publish && f.racers[0].state.kart.previous_buttons==5);
            CHECK(f.world.capture[0].spin==((n<33?n:33)*0xc00u&65535u));
        }
    }
    CHECK(!handled && events.launch_mask==1 && !f.world.capture[0].remaining);
    const PortableMeshCaptureState zero={0};
    CHECK(!memcmp(&zero,&f.world.capture[0],sizeof zero));
    CHECK(f.racers[0].state.kart.velocity_q16[1]==-3*Q &&
          f.racers[0].state.kart.velocity_q16[2]==2*Q && !f.racers[0].state.kart.grounded);
    CHECK(f.racers[0].state.tick==92 && initial!=portable_mesh_course_actors_checksum(&f.world));
    /* The launch itself is integrated by ordinary mesh physics, which owns
     * its clock, terrain sweep and checkpoint authority. */
    CHECK(portable_mesh_racer_move_effects(&f.racers[0],(PortableMeshKartInput){0},NULL,
        &turn,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(f.racers[0].state.tick==93 && f.racers[0].state.kart.position_q16[2]>
        f.rules.kart_radius_q16+f.rules.ride_height_q16);
    return 0;
}
static int guards_and_transactions(void)
{
    Fixture f;PortableMeshCourseActorEvents events={0};
    for (unsigned guard=0;guard<9;++guard)
    {
        CHECK(!fixture(&f,1));
        switch(guard)
        {
        case 0:f.config.piranha_count=f.world.piranha_count=0;break;
        case 1:f.racers[0].state.kart.grounded=0;break;
        case 2:f.racers[0].state.kart.position_q16[2]+=Q;break;
        case 3:f.racers[0].state.kart.position_q16[0]=5*Q;break;
        case 4:f.status.racers[0].star_ticks=1;break;
        case 5:f.status.racers[0].spin_ticks=1;break;
        case 6:f.status.racers[0].boo_invisible_ticks=1;break;
        case 7:f.racers[0].state.kart.contact_surface=1;break;
        case 8:break;
        }
        CHECK(admit(&f,1,guard==8,&events));
        CHECK(!f.world.capture[0].remaining && !events.capture_mask);
    }
    CHECK(!fixture(&f,2));
    CHECK(admit(&f,2,0,&events));
    CHECK(events.capture_mask==3 && f.world.capture[0].remaining==92 && f.world.capture[1].remaining==92);
    CHECK(admit(&f,2,1,&events));
    CHECK(!f.world.capture[0].remaining && f.world.capture[1].remaining==92);
    /* Disabled first candidate shadows a collidable second candidate. */
    CHECK(!fixture(&f,1));
    f.config.piranha_count=f.world.piranha_count=2;
    f.config.initial_piranhas[1]=f.config.initial_piranhas[0];
    f.config.initial_piranhas[1].placement_order=1;
    f.world.piranhas[1]=f.config.initial_piranhas[1];
    CHECK(portable_piranha_contact(&f.world.piranhas[0],0x2000,0,0,1));
    uint16_t song;
    CHECK(portable_piranha_advance(&f.world.piranhas[0],0,&song));
    memset(&events,0,sizeof events);
    CHECK(admit(&f,1,0,&events));
    CHECK(!f.world.capture[0].remaining);
    CHECK(!fixture(&f,2));
    f.racers[1].state.kart.position_q16[2]=INT32_MAX;
    const PortableMeshCourseActors saved=f.world;
    const PortableMeshCourseActorEvents saved_events=events;
    CHECK(!admit(&f,2,0,&events));
    CHECK(!memcmp(&saved,&f.world,sizeof saved) && !memcmp(&events,&saved_events,sizeof events));
    CHECK(!fixture(&f,1));
    CHECK(admit(&f,1,0,&events));
    f.world.capture[0].remaining=1;++f.world.tick;
    f.config.capture_up_speed_q16=257*Q;
    const PortableMeshCourseActors before=f.world;
    const PortableMeshRacer old=f.racers[0];
    PortableMeshRacerTurn turn={.contact=1};int handled=99;char error[160];
    CHECK(!portable_mesh_course_capture_move(&f.config,&f.world,0,&f.racers[0],
        (PortableMeshKartInput){0},&turn,&handled,&events,error,sizeof error));
    CHECK(!memcmp(&before,&f.world,sizeof before) && !memcmp(&old,&f.racers[0],sizeof old));
    CHECK(handled==99 && turn.contact==1);
    /* Malformed direct configs must fail without signed scale multiplication
     * overflow, before mutating held state or the caller's output sentinels. */
    f.config.capture_up_speed_q16=2*Q;
    f.config.scale_q16=INT32_MAX;
    CHECK(!portable_mesh_course_capture_move(&f.config,&f.world,0,&f.racers[0],
        (PortableMeshKartInput){0},&turn,&handled,&events,error,sizeof error));
    CHECK(!memcmp(&before,&f.world,sizeof before) && !memcmp(&old,&f.racers[0],sizeof old));
    CHECK(handled==99 && turn.contact==1);
    return 0;
}
int main(void)
{
    CHECK(!sequence());
    CHECK(!guards_and_transactions());
    return 0;
}
