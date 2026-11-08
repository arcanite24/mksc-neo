#include "mesh_race.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q 65536
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"mesh race line %d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct
{
    PortableTestMeshCourse floor;
    PortableMeshItemRules rules;
    uint8_t grids[256], item[32], property[32], payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    uint8_t kart_payload[PORTABLE_MESH_KART_RULES_SIZE], respawn[48];
    PortableAssetSectionView section, sections[2];
    PortableAssetPackView pack;
} Fixture;
/* The test profile's KPH2 bytes, named with the fixture's identity. */
static void kart_payload(Fixture *f)
{
    PortableMeshKartProfile p=test_mesh_profile();
    p.profile_id=f->floor.rules.profile_id;
    p.revision=f->floor.rules.revision;
    test_mesh_profile_payload(&p,f->kart_payload);
}
static int fixture(Fixture *f, int box, int declared)
{
    memset(f,0,sizeof *f);
    test_mesh_course_init(&f->floor);
    test_mesh_word(f->floor.meta+12,8);
    for(unsigned i=0;i<2;++i)
    {
        memcpy(f->grids+i*32,f->floor.grid,32);
        test_mesh_word(f->grids+i*32,(uint32_t)((int32_t)i*4*Q));
    }
    f->floor.course.tables[1].bytes=f->grids;
    f->floor.course.tables[1].count=2;
    test_mir_payload(f->payload);
    f->section=(PortableAssetSectionView){.kind=103,.content_id=41,.version=1,
        .bytes=f->payload,.size=sizeof f->payload};
    if(!portable_mesh_item_rules_asset_open(&f->section,&f->rules))return 0;
    f->rules.mushroom_boost_ticks=4;
    f->rules.mushroom_acceleration_q16=24*Q;
    f->rules.mushroom_cap_ratio_q16=2*Q;
    f->rules.star_player_ticks=5;
    f->rules.hit_spin_ticks=3;
    f->rules.hit_invulnerability_ticks=5;
    f->rules.lightning_speed_ratio_q16=Q/2;
    f->rules.lightning_scale_ratio_q16=Q/2;
    if(box)
    {
        test_mesh_word(f->item+8,3*Q/4);
        test_mesh_word(f->item+12,Q/8);
        test_mesh_word(f->item+20,90);
        f->floor.course.tables[f->floor.course.table_count++]=(PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('I','T','E','M'),1,32,f->item};
    }
    if(declared)
    {
        static const uint8_t key[]="mksc:item-rules";
        test_mesh_word(f->property+4,PORTABLE_COURSE_3D_PROPERTY_ASSET);
        test_mesh_word(f->property+8,103); test_mesh_word(f->property+12,41);
        test_mesh_word(f->floor.meta+100,1);
        f->floor.course.tables[f->floor.course.table_count++]=(PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('P','R','O','P'),1,32,f->property};
        f->floor.course.tables[f->floor.course.table_count++]=(PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('S','T','R','S'),sizeof key,1,key};
        kart_payload(f);
        f->sections[0]=f->section;
        f->sections[1]=(PortableAssetSectionView){.kind=102,.content_id=1,.version=PORTABLE_MESH_KART_RULES_VERSION,
            .bytes=f->kart_payload,.size=sizeof f->kart_payload};
        f->pack.section_count=2; f->pack.resolved_sections=f->sections;
        f->floor.course.pack=&f->pack;
    }
    return portable_mesh_item_rules_valid(&f->rules);
}
static int start(PortableMeshRaceSession *s, Fixture *f, int items)
{
    char error[256];
    if(portable_mesh_race_init(s,&f->floor.course,&f->floor.rules,2,NULL,error,sizeof error)!=PORTABLE_MESH_OK)
    {fprintf(stderr,"init: %s\n",error);return 0;}
    if(items && portable_mesh_race_enable_items(s,&f->rules,123,error,sizeof error)!=PORTABLE_MESH_OK)
    {fprintf(stderr,"enable: %s\n",error);return 0;}
    return 1;
}
static void racing(PortableMeshRaceSession *s)
{
    for(unsigned i=0;i<2;++i)
    {
        s->state.racers[i].phase=PORTABLE_MESH_RACER_RACING;
        s->state.racers[i].countdown_ticks=0;
        s->state.racers[i].kart.grounded=1; /* Authored flat grid is at exact support height. */
    }
}
static void give(PortableMeshRaceSession *s,unsigned i,uint16_t id)
{
    s->items.inventory.racers[i]=(PortableMeshItemRacer){
        .held_item=id,.settled_item=id,.preview_item=id};
}
static int tick(PortableMeshRaceSession *s,const PortableMeshKartInput input[2],
                const PortableMeshItemControls held[2],PortableMeshKartEvents event[2])
{
    char error[256];
    const PortableMeshStatus status=portable_mesh_race_tick_items(s,input,held,event,error,sizeof error);
    if(status!=PORTABLE_MESH_OK)fprintf(stderr,"tick: %s\n",error);
    return status==PORTABLE_MESH_OK;
}
static int countdown_and_identity(void)
{
    Fixture f; CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);
    CHECK(a && b && start(a,&f,0) && start(b,&f,1));
    PortableMeshKartInput input[2]={{0,1},{0,1}};
    PortableMeshKartEvents ae[2],be[2];
    const PortableMeshItemControls held[2]={{.item_pressed=1},{0}};
    give(b,0,0x1007);
    PortableMeshItems before; memcpy(&before,&b->items,sizeof before);
    for(unsigned i=0;i<5;++i)
    {
        CHECK(tick(a,input,NULL,ae) && tick(b,input,held,be));
        CHECK(!memcmp(&a->state,&b->state,sizeof a->state));
        CHECK(!memcmp(ae,be,sizeof ae) && !memcmp(&before,&b->items,sizeof before));
    }
    give(b,0,0); racing(a); racing(b);
    for(unsigned i=0;i<24;++i)
    {
        input[0].steering=i<12?6000:-6000;
        CHECK(tick(a,input,NULL,ae) && tick(b,input,NULL,be));
        CHECK(!memcmp(&a->state,&b->state,sizeof a->state) && !memcmp(ae,be,sizeof ae));
        CHECK(b->items.tick==i+1u && !b->items.prepared && !b->item_events.count);
    }
    free(a);free(b);return 0;
}
static int activation_and_atomic_failure(void)
{
    Fixture f; CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *s=calloc(1,sizeof *s),*before=malloc(sizeof *before);
    CHECK(s && before && start(s,&f,1)); racing(s); give(s,0,0x1007);
    /* Prepare consumes the mushroom, but the last mover fails. Nothing leaks. */
    memset(s->state.racers[1].kart.forward_q30,0,sizeof s->state.racers[1].kart.forward_q30);
    memcpy(before,s,sizeof *s);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshItemControls held[2]={{.item_pressed=1},{0}};
    PortableMeshKartEvents events[2],old_events[2];
    memset(events,0xa5,sizeof events);memcpy(old_events,events,sizeof events);
    char error[256];
    CHECK(portable_mesh_race_tick_items(s,input,held,events,error,sizeof error)!=PORTABLE_MESH_OK);
    CHECK(!memcmp(s,before,sizeof *s) && !memcmp(events,old_events,sizeof events));
    s->state.racers[1].kart.forward_q30[1]=-1073741824;
    CHECK(tick(s,input,held,events));
    CHECK(s->items.inventory.racers[0].held_item==0);
    CHECK(s->items.status.racers[0].mushroom_ticks==f.rules.mushroom_boost_ticks);
    CHECK(s->state.racers[0].kart.speed_q16>0 && s->item_events.count==2);
    CHECK(s->item_events.values[0].kind==PORTABLE_MESH_EVENT_USE &&
          s->item_events.values[1].kind==PORTABLE_MESH_EVENT_BOOST);
    /* Equivalent borrowed profile rebinding is safe; malformed rebind rolls back. */
    PortableMeshItemRules rebound=f.rules;
    s->item_rules=&rebound;held[0].item_pressed=0;
    CHECK(tick(s,input,held,events));
    rebound.shell_speed_q16=0;memcpy(before,s,sizeof *s);memcpy(old_events,events,sizeof events);
    CHECK(portable_mesh_race_tick_items(s,input,held,events,error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(s,before,sizeof *s) && !memcmp(events,old_events,sizeof events));
    free(s);free(before);return 0;
}
static int pair_pickup_and_rescue(void)
{
    Fixture f; CHECK(fixture(&f,1,0));
    PortableMeshRaceSession *s=calloc(1,sizeof *s);CHECK(s && start(s,&f,1));racing(s);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshKartEvents events[2];
    /* Source-foot motion crosses a gate and the authored box in one turn. */
    s->state.racers[0].kart.position_q16[1]=512;
    s->state.racers[0].kart.velocity_q16[1]=-Q;
    s->state.racers[0].kart.speed_q16=Q;
    CHECK(tick(s,input,NULL,events));
    CHECK(s->state.racers[0].checkpoint==1 && s->items.inventory.racers[0].active);
    CHECK(s->items.boxes.cooldowns[0]==90 && s->item_events.count==1);
    CHECK(s->item_events.values[0].kind==PORTABLE_MESH_EVENT_PICKUP);
    /* An accepted fresh rescue cannot use inventory or collect while teleporting. */
    CHECK(start(s,&f,1));racing(s);give(s,0,0x1007);
    PortableMeshItemControls held[2]={{.item_pressed=1},{0}};
    input[0].buttons=PORTABLE_MESH_RESCUE;
    CHECK(tick(s,input,held,events));
    CHECK(s->state.racers[0].kart.respawn_ticks==f.floor.rules.respawn_ticks);
    CHECK(s->items.inventory.racers[0].held_item==0x1007 &&
          !s->items.status.racers[0].mushroom_ticks && !s->items.status.racers[0].pending_mushroom);
    CHECK(!s->item_events.count);
    /* Stable pair policy applies Star before ordinary push, on real moves. */
    CHECK(start(s,&f,1));racing(s);give(s,0,0x100a);
    input[0].buttons=0;
    s->state.racers[1].kart.position_q16[0]=Q/2;
    CHECK(tick(s,input,held,events));
    CHECK(s->items.status.racers[0].star_ticks==f.rules.star_player_ticks);
    CHECK(s->items.status.racers[1].spin_ticks==f.rules.hit_spin_ticks);
    CHECK(s->state.racers[0].kart.position_q16[0]==0);
    int hit=0;
    for(unsigned i=0;i<s->item_events.count;++i)
        hit|=s->item_events.values[i].kind==PORTABLE_MESH_EVENT_HIT &&
             s->item_events.values[i].target==1;
    CHECK(hit);
    free(s);return 0;
}
static int pier_boo_shared_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));f.rules.star_player_ticks=500;
    PortableMeshRaceSession *s=calloc(1,sizeof *s);PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(s && config && start(s,&f,1));racing(s);
    config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
    config->scale_q16=6554;config->pier_boo_count=1;config->actor_visual_seed=123;
    config->pier_boo_rules=(PortableBooRules){1,360,64,1024,1024,3*Q,Q/4};
    CHECK(portable_course_prop_init(&config->initial_pier_boos[0],100*Q,100*Q,0xa30000,256,0));
    config->pier_boo_anchors_q16[0][0]=4*Q;config->pier_boo_anchors_q16[0][1]=4*Q;
    for(unsigned n=0;n<3;++n)config->pier_boo_frame_ids[0][n]=230+n;
    char error[256];CHECK(portable_mesh_race_enable_actors(s,config,error,sizeof error)==PORTABLE_MESH_OK);
    PortableMeshKartInput input[2]={{0,0},{0,0}};PortableMeshKartEvents events[2];
    CHECK(tick(s,input,NULL,events));
    CHECK(s->actor_events.pier_boo_touch_mask==2 && !s->actor_events.hit_mask &&
          s->actors.source_haunts[0].target==1 && s->actors.source_haunts[0].timer==63);
    /* Racer0 leads; source48C2C must retain touched racer1 as its victim. */
    for(unsigned n=1;n<64;++n)CHECK(tick(s,input,NULL,events));
    CHECK(s->items.status.racers[1].boo_lock_ticks==360 && !s->items.status.racers[0].boo_lock_ticks &&
          s->actor_events.source_haunt_assigned_mask==2 && s->actor_events.source_haunt_music_start_mask==2);
    input[1].buttons=PORTABLE_MESH_ACCELERATE;CHECK(tick(s,input,NULL,events));
    CHECK(s->items.status.racers[1].boo_lock_ticks==349 && s->actor_events.source_haunt_clock_mask==2 &&
          s->actor_events.source_haunt_lock_clock[1]==350);
    CHECK(tick(s,input,NULL,events));CHECK(s->items.status.racers[1].boo_lock_ticks==348);
    give(s,1,0x100a);PortableMeshItemControls held[2]={{0},{.item_pressed=1}};
    CHECK(tick(s,input,held,events));
    CHECK(s->actors.source_haunts[0].phase==6 && s->actor_events.source_haunt_music_release_mask==2 &&
          !s->items.status.racers[1].boo_lock_ticks && !s->items.status.racers[1].boo_invisible_ticks &&
          !s->actor_events.hit_mask && !s->items.status.racers[1].spin_ticks);
    free(config);free(s);return 0;
}
static int kamek_shared_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));f.rules.star_player_ticks=100;
    test_mesh_word(f.floor.meta+12,16);test_mesh_word(f.floor.meta+16,0);
    for(unsigned i=0;i<f.floor.course.table_count;++i)
        if(f.floor.course.tables[i].tag==PORTABLE_COURSE_3D_TAG('C','P','N','T') ||
           f.floor.course.tables[i].tag==PORTABLE_COURSE_3D_TAG('C','P','E','D'))
            f.floor.course.tables[i].count=0;
    PortableMeshRaceSession *s=calloc(1,sizeof *s);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(s && config);char error[256];
    CHECK(portable_mesh_race_init_mode(s,&f.floor.course,&f.floor.rules,16,2,2,NULL,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_race_enable_items(s,&f.rules,123,error,sizeof error)==PORTABLE_MESH_OK);
    config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
    config->scale_q16=6554;config->kamek_count=1;
    CHECK(portable_course_prop_init(&config->initial_kameks[0],100*Q,100*Q,0xa00000,224,0));
    config->kamek_anchors_q16[0][1]=4*Q;config->kamek_anchors_q16[0][2]=-Q/2;
    config->kamek_frame_ids[0][0]=220;config->kamek_frame_ids[0][1]=221;
    CHECK(portable_mesh_race_enable_actors(s,config,error,sizeof error)==PORTABLE_MESH_OK);racing(s);
    PortableMeshKartInput input[2]={{0,0},{0,0}};PortableMeshKartEvents events[2];
    CHECK(tick(s,input,NULL,events));
    CHECK(s->actor_events.kamek_contacts==1 && s->actor_events.kamek_touch_mask==1 &&
          !s->actor_events.hit_mask && !s->actor_events.bump_mask &&
          !s->items.status.racers[0].spin_ticks && !s->state.racers[0].kart.velocity_q16[0] &&
          !s->state.racers[0].kart.velocity_q16[1] && s->battle.balloons[0]==3 && s->actors.actor_grace[0]==8);
    give(s,0,0x100a);
    PortableMeshItemControls held[2]={{.item_pressed=1},{0}};
    CHECK(tick(s,input,held,events));
    CHECK(s->actor_events.kamek_protected_mask==1 && !s->actor_events.hit_mask &&
          s->actors.actor_grace[0]==7 && s->actors.kameks[0].state==5 && s->battle.balloons[0]==3);
    const int32_t patrol_x=s->actors.kameks[0].x_q16;
    unsigned turns=0;
    while((s->actors.kameks[0].flags&1) && turns++<64)
    {
        CHECK(tick(s,input,NULL,events));
        CHECK(!s->actor_events.hit_mask && s->battle.balloons[0]==3 &&
              s->actors.kameks[0].x_q16==patrol_x && !s->items.status.racers[0].spin_ticks);
        if(turns==1)CHECK(s->actors.kameks[0].state==6 && s->actors.kameks[0].affine_y_q8==-256);
    }
    CHECK(turns>1 && turns<64 && s->actors.kameks[0].state==8 && !s->state.results_ready);
    free(config);free(s);return 0;
}
static int mechakoopa_cpu_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));
    test_mesh_word(f.floor.meta+12,16);test_mesh_word(f.floor.meta+16,0);
    for(unsigned i=0;i<f.floor.course.table_count;++i)
        if(f.floor.course.tables[i].tag==PORTABLE_COURSE_3D_TAG('C','P','N','T') ||
           f.floor.course.tables[i].tag==PORTABLE_COURSE_3D_TAG('C','P','E','D'))
            f.floor.course.tables[i].count=0;
    /* A real Battle CPU requires a closed authored patrol even when combat
     * selects the nearby opponent before its first movement turn. */
    uint8_t header[32]={0},nodes[128]={0},edges[16]={0};
    const int32_t patrol[4][2]={{-8*Q,-8*Q},{8*Q,-8*Q},{8*Q,8*Q},{-8*Q,8*Q}};
    test_mesh_word(header+8,4);test_mesh_word(header+16,1);
    for(unsigned i=0;i<4;++i)
    {
        test_mesh_word(nodes+i*32,(uint32_t)patrol[i][0]);
        test_mesh_word(nodes+i*32+4,(uint32_t)patrol[i][1]);
        test_mesh_word(nodes+i*32+12,2*Q);test_mesh_word(nodes+i*32+16,8*Q);
        test_mesh_word(nodes+i*32+20,i);test_mesh_word(nodes+i*32+24,1);
        test_mesh_word(edges+i*4,(i+1)%4);
    }
    f.floor.course.tables[f.floor.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','H','D'),1,32,header};
    f.floor.course.tables[f.floor.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','N','D'),4,32,nodes};
    f.floor.course.tables[f.floor.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','E','D'),4,4,edges};
    f.floor.course.tables[f.floor.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('I','T','E','M'),0,32,NULL};
    PortableMeshRaceSession *s=calloc(1,sizeof *s);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(s && config);char error[256];
    const PortableMeshStatus initialized=portable_mesh_race_init_mode(s,&f.floor.course,
        &f.floor.rules,16,1,2,NULL,error,sizeof error);
    if(initialized!=PORTABLE_MESH_OK)fprintf(stderr,"Mechakoopa Battle fixture: %s\n",error);
    CHECK(initialized==PORTABLE_MESH_OK);
    CHECK(portable_mesh_race_enable_items(s,&f.rules,123,error,sizeof error)==PORTABLE_MESH_OK);
    config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
    config->scale_q16=6554;config->mechakoopa_count=1;
    CHECK(portable_course_prop_init(&config->initial_mechakoopas[0],100*Q,100*Q,0xa10000,224,0));
    config->mechakoopa_anchors_q16[0][0]=4*Q;config->mechakoopa_anchors_q16[0][1]=4*Q;
    for(unsigned i=0;i<4;++i)config->mechakoopa_frame_ids[0][i]=210+i;
    CHECK(portable_mesh_race_enable_actors(s,config,error,sizeof error)==PORTABLE_MESH_OK);racing(s);
    PortableMeshKartInput input[2]={{0,0},{0,0}};PortableMeshKartEvents events[2];
    CHECK(tick(s,input,NULL,events));
    CHECK(s->actor_events.mechakoopa_contacts==1 && s->actor_events.mechakoopa_strong_mask==2 &&
          s->actors.strong_hit[1].pending && !s->actors.strong_hit[1].active &&
          !s->items.status.racers[1].spin_ticks && !s->actors.lava_burn_ticks[1] &&
          !s->items.status.racers[1].lightning_ticks);
    /* Ordinary supported pose beyond the patrol: observe the entire real
     * controller without admitting another contact after it releases. */
    s->state.racers[1].kart.position_q16[0]=10*Q;
    s->state.racers[1].kart.grounded=1;
    CHECK(tick(s,input,NULL,events));
    CHECK(s->actors.strong_hit[1].active && !s->actors.strong_hit[1].pending && s->actors.strong_hit[1].angle);
    unsigned turns=0,angles=0;uint16_t previous=s->actors.strong_hit[1].angle;
    while(s->actors.strong_hit[1].active && turns++<140)
    {
        CHECK(tick(s,input,NULL,events));
        angles+=s->actors.strong_hit[1].angle!=previous;previous=s->actors.strong_hit[1].angle;
    }
    CHECK(turns>1 && turns<=128 && angles>2 && !s->actors.strong_hit[1].pending &&
          !s->actors.strong_hit[1].active && !s->actors.strong_hit[1].angle && s->battle.balloons[1]==2);
    /* Three genuine human weak contacts reach the final balloon. The final
     * accepted HIT remains observable even though role cleanup removes its
     * weak spin before the presentation snapshot is published. */
    CHECK(portable_mesh_race_init_mode(s,&f.floor.course,&f.floor.rules,16,2,2,NULL,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_race_enable_items(s,&f.rules,123,error,sizeof error)==PORTABLE_MESH_OK);
    config->mechakoopa_anchors_q16[0][0]=0;
    CHECK(portable_mesh_race_enable_actors(s,config,error,sizeof error)==PORTABLE_MESH_OK);racing(s);
    s->state.racers[1].kart.position_q16[0]=10*Q; /* Supported rival outside the full patrol. */
    unsigned accepted=0;
    for(unsigned turn=0;turn<400 && accepted<3;++turn)
    {
        CHECK(tick(s,input,NULL,events));
        CHECK(!s->actor_events.mechakoopa_strong_mask && !s->actors.strong_hit[0].pending &&
              !s->actors.strong_hit[0].active);
        if(!(s->actor_events.mechakoopa_hit_mask&1))continue;
        ++accepted;
        CHECK((s->actor_events.hit_mask&1) && s->battle.balloons[0]==3-accepted);
        if(accepted<3)CHECK(s->items.status.racers[0].spin_ticks==f.rules.hit_spin_ticks);
        else CHECK(s->battle.elimination[0].revenge_bomb && !s->items.status.racers[0].spin_ticks &&
                   !s->actors.lava_burn_ticks[0]);
    }
    CHECK(accepted==3 && s->state.results_ready && !s->battle.balloons[0]);
    free(config);free(s);return 0;
}
static int common_prop_shared_owner(void)
{
    for(unsigned protected=0;protected<2;++protected)
    {
        Fixture f;CHECK(fixture(&f,0,0));f.rules.star_player_ticks=100;
        PortableMeshRaceSession *session=calloc(1,sizeof *session);
        PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
        CHECK(session && config && start(session,&f,1));racing(session);
        config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
        config->scale_q16=6554;config->common_prop_count=1;
        CHECK(portable_course_prop_init(&config->initial_common_props[0],100*Q,100*Q,0x060000,256,0));
        config->common_prop_anchors_q16[0][0]=Q/4;config->common_prop_anchors_q16[0][1]=4*Q;
        config->common_prop_frame_ids[0][0]=200;
        char error[160];CHECK(portable_mesh_race_enable_actors(session,config,error,sizeof error)==PORTABLE_MESH_OK);
        if(protected)session->items.status.racers[0].star_ticks=100;
        PortableMeshKartInput input[2]={{0,0},{0,0}};PortableMeshKartEvents events[2];
        CHECK(tick(session,input,NULL,events));
        CHECK(session->actor_events.common_prop_contacts==1 && !session->actor_events.hit_mask &&
              !session->items.status.racers[0].spin_ticks && !session->actors.lava_burn_ticks[0]);
        if(protected)
        {
            CHECK(session->actor_events.common_prop_protected_mask==1 && !session->actors.actor_grace[0]);
            for(unsigned n=0;n<33;++n)CHECK(tick(session,input,NULL,events));
            CHECK(!(session->actors.common_props[0].flags&1));
        }
        else
        {
            CHECK(session->actor_events.common_prop_hit_mask==1 && session->actors.actor_grace[0]==8);
            CHECK(tick(session,input,NULL,events));
            CHECK(!session->actor_events.common_prop_contacts && session->actors.actor_grace[0]==7);
        }
        free(config);free(session);
    }
    return 0;
}
static int thundercloud_shared_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *session=calloc(1,sizeof *session);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(session && config && start(session,&f,1));racing(session);
    config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
    config->scale_q16=6554;config->thundercloud_count=1;
    CHECK(portable_thundercloud_init(&config->initial_thunderclouds[0],100*Q,100*Q,0,0));
    config->thundercloud_anchors_q16[0][1]=4*Q;
    for(unsigned i=0;i<72;++i)config->thundercloud_frame_ids[0][i]=80+i;
    char error[160];
    CHECK(portable_mesh_race_enable_actors(session,config,error,sizeof error)==PORTABLE_MESH_OK);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshKartEvents events[2];unsigned hit=0;
    for(unsigned turn=0;turn<300 && !hit;++turn)
    {
        CHECK(tick(session,input,NULL,events));
        hit=session->actor_events.thundercloud_hit_mask&1;
    }
    CHECK(hit && session->items.status.racers[0].lightning_ticks==360 &&
          !session->items.status.racers[0].spin_ticks && session->actors.strong_hit[0].pending &&
          !session->state.racers[0].kart.velocity_q16[0] && !session->state.racers[0].kart.velocity_q16[1]);
    CHECK(tick(session,input,NULL,events));
    CHECK(session->items.status.racers[0].lightning_ticks==359 && session->actors.strong_hit[0].active &&
          !session->actors.strong_hit[0].pending && session->actors.strong_hit[0].angle);
    session->state.racers[0].kart.position_q16[0]=10*Q;
    unsigned angles=0;uint16_t previous=session->actors.strong_hit[0].angle;
    for(unsigned remaining=358;;--remaining)
    {
        CHECK(tick(session,input,NULL,events));
        CHECK(session->items.status.racers[0].lightning_ticks==remaining && !session->state.racers[0].kart.respawn_ticks);
        angles+=session->actors.strong_hit[0].angle!=previous;previous=session->actors.strong_hit[0].angle;
        if(!remaining)break;
    }
    CHECK(angles>2 && !session->actors.strong_hit[0].pending && !session->actors.strong_hit[0].active &&
          !session->actors.strong_hit[0].angle && session->actors.strong_hit[0].rate==0xc0);
    free(config);free(session);return 0;
}
static int falling_star_shared_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *session=calloc(1,sizeof *session);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(session && config && start(session,&f,1));racing(session);
    config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
    config->scale_q16=6554;config->falling_star_count=1;
    CHECK(portable_falling_star_init(&config->initial_falling_stars[0],100*Q,100*Q,0,0));
    config->falling_star_anchors_q16[0][1]=4*Q;
    for(unsigned i=0;i<5;++i)config->falling_star_frame_ids[0][i]=70+i;
    char error[160];
    CHECK(portable_mesh_race_enable_actors(session,config,error,sizeof error)==PORTABLE_MESH_OK);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshKartEvents events[2];unsigned hit=0;
    for(unsigned turn=0;turn<140 && !hit;++turn)
    {
        CHECK(tick(session,input,NULL,events));
        hit=session->actor_events.falling_star_hit_mask&1;
    }
    CHECK(hit && (session->actor_events.hit_mask&1) &&
          !session->items.status.racers[0].spin_ticks &&
          (session->actors.strong_hit[0].pending || session->actors.strong_hit[0].active));
    /* Explicit supported post-contact fixture pose, not an actor-state change. */
    session->state.racers[0].kart.position_q16[2]=f.floor.rules.kart_radius_q16+f.floor.rules.ride_height_q16;
    session->state.racers[0].kart.velocity_q16[2]=0;session->state.racers[0].kart.grounded=1;
    const uint16_t admitted_rate=session->actors.strong_hit[0].rate;
    CHECK(tick(session,input,NULL,events));
    CHECK(!session->actors.strong_hit[0].pending && session->actors.strong_hit[0].active &&
          session->actors.strong_hit[0].rate<admitted_rate && session->actors.strong_hit[0].angle);
    session->state.racers[0].kart.position_q16[0]=10*Q; /* Supported, beyond subsequent star contacts. */
    uint16_t last_angle=session->actors.strong_hit[0].angle;
    unsigned advanced=0;
    for(unsigned turn=0;turn<256 && session->actors.strong_hit[0].active;++turn)
    {
        CHECK(tick(session,input,NULL,events));
        advanced+=session->actors.strong_hit[0].angle!=last_angle;
        last_angle=session->actors.strong_hit[0].angle;
        CHECK(!session->state.racers[0].kart.respawn_ticks);
    }
    CHECK(advanced>2 && !session->actors.strong_hit[0].pending && !session->actors.strong_hit[0].active &&
          !session->actors.strong_hit[0].angle && session->actors.strong_hit[0].rate==0xc0);
    free(config);free(session);return 0;
}
static int teepee_shared_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *session=calloc(1,sizeof *session);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(session && config && start(session,&f,1));
    config->course_id=f.floor.course.id;config->profile_id=config->revision=1;
    config->scale_q16=6554;config->teepee_count=1;
    CHECK(portable_course_prop_init(&config->initial_teepees[0],100*Q,100*Q,0x030000u,256,0));
    config->teepee_anchors_q16[0][1]=4*Q;
    for(unsigned i=0;i<11;++i)config->teepee_frame_ids[0][i]=50+i;
    char error[160];
    CHECK(portable_mesh_race_enable_actors(session,config,error,sizeof error)==PORTABLE_MESH_OK);
    racing(session);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshKartEvents events[2];
    CHECK(tick(session,input,NULL,events));
    CHECK(session->actors.teepees[0].teepee.rider_state==1 &&
          session->items.status.racers[0].boo_lock_ticks==360 &&
          !session->items.status.racers[1].boo_lock_ticks &&
          !session->actor_events.hit_mask && !session->item_events.count);
    input[0].buttons=PORTABLE_MESH_ACCELERATE;
    CHECK(tick(session,input,NULL,events));
    CHECK(session->items.status.racers[0].boo_lock_ticks==349 &&
          !session->items.status.racers[0].spin_ticks && !session->actor_events.hit_mask);
    CHECK(tick(session,input,NULL,events));
    CHECK(session->items.status.racers[0].boo_lock_ticks==348);
    free(config);free(session);return 0;
}
static int coin_only_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *session=calloc(1,sizeof *session),*saved=malloc(sizeof *saved);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(session && saved && config && start(session,&f,1));
    config->course_id=f.floor.course.id;config->profile_id=5;config->revision=1;
    config->coins=(PortableMeshCourseCoinsConfig){.course_id=f.floor.course.id,.enabled=1,
        .profile_id=5,.revision=1,.modes=13,.cap=99,.initial=98,.placement_count=2,
        .loss={4,2,1,8,1}};
    for(unsigned i=0;i<100;++i)config->coins.ratios_q16[i]=Q;
    for(unsigned i=0;i<2;++i)
        config->coins.placements[i]=(PortableMeshCourseCoin){.id=i+1,.object_index=i,
            .model_id=1,.position_q16={0,4*Q,0},.half_q16={Q/2,Q/2,Q/4}};
    CHECK(!portable_mesh_course_actors_config_count(config) &&
          portable_mesh_course_actors_required(config)); /* No MSA actor or source scale. */
    char error[192];
    CHECK(portable_mesh_race_enable_actors(session,config,error,sizeof error)==PORTABLE_MESH_OK);
    racing(session);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshKartEvents events[2],previous[2];
    CHECK(tick(session,input,NULL,events));
    CHECK(session->actors.coins.count[0]==99 && session->actors.coins.available==2 &&
          session->actor_events.coins.picked[0]==1 && !session->actor_events.coins.picked[1]);
    CHECK(tick(session,input,NULL,events));
    CHECK(session->actors.coins.count[0]==99 && session->actors.coins.available==2 &&
          !session->actor_events.coins.picked[0]); /* Cap leaves next coin available. */
    input[0].buttons=PORTABLE_MESH_RESCUE;
    CHECK(tick(session,input,NULL,events));
    CHECK(session->actors.coins.count[0]==97 && session->actor_events.coins.lost[0]==2 &&
          session->actor_events.coins.causes[0]==(1u<<PORTABLE_MESH_COIN_RESCUE) &&
          session->state.racers[0].kart.respawn_ticks==f.floor.rules.respawn_ticks);
    CHECK(tick(session,input,NULL,events));
    CHECK(session->actors.coins.count[0]==97 && !session->actor_events.coins.lost[0] &&
          !session->actor_events.coins.picked[0]); /* Held rescue does not charge twice. */
    *saved=*session;memcpy(previous,events,sizeof events);
    config->coins.ratios_q16[0]=0;
    CHECK(portable_mesh_race_tick_items(session,input,NULL,events,error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(saved,session,sizeof *session) && !memcmp(previous,events,sizeof events));
    free(config);free(saved);free(session);return 0;
}
/* Exercise the production multiplayer owner, not just the crush clock helper.
 * The declared actor is far away so only the explicit initial admission applies. */
static int thwomp_crush_owner(void)
{
    Fixture f; CHECK(fixture(&f,0,0));
    PortableMeshRaceSession *s=calloc(1,sizeof *s);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(s && config && start(s,&f,1));
    config->course_id=f.floor.course.id;
    config->profile_id=config->revision=1;
    config->scale_q16=6554;
    config->thwomp_count=1;
    CHECK(portable_thwomp_init(&config->initial_thwomps[0],100*Q,100*Q,0,0));
    config->thwomp_anchors_q16[0][0]=100*Q;
    config->thwomp_anchors_q16[0][1]=100*Q;
    for(unsigned i=0;i<5;++i)
    {
        config->crush_rules.counts[i]=1;
        config->crush_rules.rows[i][0]=(PortableDriverAffineRow){256,1024,8};
    }
    char error[256];
    CHECK(portable_mesh_race_enable_actors(s,config,error,sizeof error)==PORTABLE_MESH_OK);
    racing(s);
    give(s,0,0x1007);
    CHECK(portable_mesh_course_crush_admit(&config->crush_rules,&s->actors.crush[0],
        &s->state.racers[0].kart,error,sizeof error));
    int32_t origin[3];memcpy(origin,s->state.racers[0].kart.position_q16,sizeof origin);
    PortableMeshKartInput input[2]={{0,PORTABLE_MESH_ACCELERATE},{0,PORTABLE_MESH_ACCELERATE}};
    PortableMeshItemControls held[2]={{.item_pressed=1},{0}};
    PortableMeshKartEvents events[2];
    for(unsigned turn=1;turn<=128;++turn)
    {
        CHECK(tick(s,input,held,events));
        CHECK(!s->state.racers[0].kart.speed_q16);
        CHECK(!memcmp(origin,s->state.racers[0].kart.position_q16,sizeof origin));
        CHECK(s->items.inventory.racers[0].held_item==0x1007);
        CHECK(!s->items.status.racers[0].pending_mushroom &&
              !s->items.status.racers[0].mushroom_ticks);
        CHECK(!s->actors.crush[1].contact_timer);
        CHECK(s->actors.crush[0].contact_timer==(turn<128 ? 0x7000-turn : 0));
        if(turn==1)
        {
            CHECK(s->state.racers[1].kart.speed_q16>0);
            input[1].buttons=0;
        }
    }
    CHECK(tick(s,input,NULL,events));
    CHECK(s->state.racers[0].kart.speed_q16>0);
    CHECK(memcmp(origin,s->state.racers[0].kart.position_q16,sizeof origin));
    /* Raw rescue must bypass crush input suppression on the same accepted turn,
     * while the other racer's independent hold continues. */
    CHECK(start(s,&f,1));
    CHECK(portable_mesh_race_enable_actors(s,config,error,sizeof error)==PORTABLE_MESH_OK);
    racing(s);
    for(unsigned i=0;i<2;++i)
        CHECK(portable_mesh_course_crush_admit(&config->crush_rules,&s->actors.crush[i],
            &s->state.racers[i].kart,error,sizeof error));
    input[0].buttons=PORTABLE_MESH_RESCUE|PORTABLE_MESH_ACCELERATE;
    input[1].buttons=PORTABLE_MESH_ACCELERATE;
    CHECK(tick(s,input,NULL,events));
    CHECK(s->state.racers[0].kart.respawn_ticks==f.floor.rules.respawn_ticks);
    const PortableMeshCrushState empty={0};
    CHECK(!memcmp(&s->actors.crush[0],&empty,sizeof empty));
    CHECK(s->actors.crush[1].contact_timer==0x6fff);
    CHECK(!s->state.racers[1].kart.respawn_ticks && !s->state.racers[1].kart.speed_q16);
    free(config);free(s);return 0;
}
static int declaration_requires_owner(void)
{
    Fixture f;CHECK(fixture(&f,0,1));
    PortableMeshRaceSession *s=calloc(1,sizeof *s),*before=malloc(sizeof *before);
    CHECK(s && before && start(s,&f,0));memcpy(before,s,sizeof *s);
    PortableMeshKartInput input[2]={{0,0},{0,0}};
    PortableMeshKartEvents events[2],old[2];memset(events,0xa5,sizeof events);memcpy(old,events,sizeof old);
    char error[256];
    CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_UNSUPPORTED);
    CHECK(strstr(error,"MIR") && !memcmp(s,before,sizeof *s) && !memcmp(events,old,sizeof old));
    CHECK(portable_mesh_race_enable_items(s,&f.rules,123,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(tick(s,input,NULL,events));
    memcpy(before,s,sizeof *s);
    CHECK(portable_mesh_race_enable_items(s,&f.rules,123,error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(s,before,sizeof *s));
    free(s);free(before);return 0;
}
static int battle_world(void)
{
    Fixture f; CHECK(fixture(&f,0,0));
    test_mesh_word(f.floor.meta+12,16); test_mesh_word(f.floor.meta+16,0);
    for(unsigned i=0;i<4;++i)
    {
        memcpy(f.grids+i*32,f.floor.grid,32);
        test_mesh_word(f.grids+i*32,i*4*Q);
    }
    f.floor.course.tables[1].count=4;
    for(unsigned i=0;i<f.floor.course.table_count;++i)
        if(f.floor.course.tables[i].tag==PORTABLE_COURSE_3D_TAG('C','P','N','T') ||
           f.floor.course.tables[i].tag==PORTABLE_COURSE_3D_TAG('C','P','E','D'))
            f.floor.course.tables[i].count=0;
    /* All arena floor faces may share one safe fall point, but a revenge
     * Bomb must return to its own GRID after detonation. */
    test_mesh_word(f.respawn,15*Q);
    test_mesh_word(f.respawn+24,1073741824);
    test_mesh_word(f.respawn+28,PORTABLE_MESH_ABSENT);
    f.floor.course.tables[f.floor.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','E','S','P'),1,48,f.respawn};
    f.rules.star_player_ticks=100;
    PortableMeshRaceSession *s=calloc(1,sizeof *s), *before=malloc(sizeof *before);
    CHECK(s && before);
    char error[256];
    CHECK(portable_mesh_race_init_mode(s,&f.floor.course,&f.floor.rules,16,4,4,NULL,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_race_enable_items(s,&f.rules,123,error,sizeof error)==PORTABLE_MESH_OK);
    PortableMeshKartInput input[4]={{0}};
    PortableMeshKartEvents events[4], old[4];
    for(unsigned i=0;i<180;++i)
        CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!s->items.tick && !s->battle.tick_valid && s->battle.balloons[0]==3);
    for(unsigned i=0;i<4;++i) s->state.racers[i].kart.grounded=1;
    s->items.status.racers[0].star_ticks=100;
    for(unsigned hit=0;hit<3;++hit)
    {
        for(unsigned i=0;i<2;++i)
        {
            s->state.racers[i].kart.position_q16[0]=(int32_t)i*Q/2;
            s->state.racers[i].kart.position_q16[1]=4*Q;
            memset(s->state.racers[i].kart.velocity_q16,0,sizeof s->state.racers[i].kart.velocity_q16);
        }
        s->items.status.racers[1].hit_grace_ticks=0;
        CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(s->battle.balloons[1]==2-hit && s->battle_events.popped_mask==2);
        CHECK(!s->state.results_ready && !s->state.racers[1].finish_ticks);
        s->state.racers[1].kart.position_q16[0]=4*Q;
        for(unsigned wait=0;wait<6;++wait)
            CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    }
    CHECK(s->battle.elimination[1].revenge_bomb && s->state.racers[1].phase==PORTABLE_MESH_RACER_RACING);
    for(unsigned i=0;i<2;++i)
    {
        s->state.racers[i].kart.position_q16[0]=(int32_t)i*Q/2;
        s->state.racers[i].kart.position_q16[1]=4*Q;
        memset(s->state.racers[i].kart.velocity_q16,0,sizeof s->state.racers[i].kart.velocity_q16);
    }
    CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(s->bomb_recovery_ticks[1]==60 && s->battle.balloons[0]==3);
    int detonation=0;
    for(unsigned i=0;i<s->item_events.count;++i)
        detonation |= s->item_events.values[i].kind==PORTABLE_MESH_EVENT_BOMB_CONTACT && s->item_events.values[i].target==1;
    CHECK(detonation);
    int32_t position[3]; memcpy(position,s->state.racers[1].kart.position_q16,sizeof position);
    for(unsigned wait=0;wait<10;++wait)
    {
        CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(!memcmp(position,s->state.racers[1].kart.position_q16,sizeof position));
    }
    CHECK(s->bomb_recovery_ticks[1]==50);
    s->state.racers[1].last_respawn=0;
    s->state.racers[3].kart.position_q16[2]=INT32_MAX;
    memcpy(before,s,sizeof *s); memset(events,0xa5,sizeof events); memcpy(old,events,sizeof old);
    CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)!=PORTABLE_MESH_OK);
    CHECK(!memcmp(before,s,sizeof *s) && !memcmp(old,events,sizeof old));
    s->state.racers[3].kart.position_q16[2]=3*Q/4;
    for(unsigned wait=0;wait<50;++wait)
        CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!s->bomb_recovery_ticks[1] && s->state.racers[1].kart.respawn_ticks &&
          s->battle.elimination[1].revenge_bomb && !s->state.results_ready);
    CHECK(s->state.racers[1].kart.position_q16[0]==4*Q &&
          s->state.racers[1].kart.position_q16[1]==4*Q &&
          s->state.racers[1].last_respawn==PORTABLE_MESH_ABSENT);
    /* Completed retrieval queues one loss, not one loss on every waiting
     * tick and not at initial fall/manual-rescue admission. */
    input[2].buttons=PORTABLE_MESH_RESCUE;
    CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(s->state.racers[2].kart.respawn_ticks && s->battle.balloons[2]==3);
    input[2].buttons=0;
    unsigned returning=0;
    while(!(s->recovery_hit_mask&4u) && returning++<200)
    {
        CHECK(s->battle.balloons[2]==3);
        CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    }
    CHECK((s->recovery_hit_mask&4u) && s->battle.balloons[2]==3);
    CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(s->battle.balloons[2]==2 && (s->battle_events.popped_mask&4u));
    /* A separate valid one-balloon round exercises simultaneous final hits,
     * complete-pass standings and exact frozen publication. */
    const PortableMeshBattleConfig one={4,1,1};
    CHECK(portable_mesh_battle_init(&s->battle,&one,error,sizeof error));
    memset(s->bomb_recovery_ticks,0,sizeof s->bomb_recovery_ticks);
    memset(s->recovery_pending,0,sizeof s->recovery_pending); s->recovery_hit_mask=0;
    CHECK(portable_mesh_item_status_init(&s->items.status,4,15)==PORTABLE_MESH_ITEM_STATUS_OK);
    for(unsigned i=0;i<4;++i) { s->state.racers[i].kart.respawn_ticks=0; s->state.racers[i].respawn_grace=0; }
    give(s,0,0x100c);
    const PortableMeshItemControls held[4]={{.item_pressed=1},{0},{0},{0}};
    CHECK(portable_mesh_race_tick_items(s,input,held,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(s->state.results_ready && s->state.finish_count==4 && s->battle.finished);
    CHECK(s->battle.balloons[0]==1 && !s->battle.balloons[1] && !s->battle.balloons[2] && !s->battle.balloons[3]);
    uint32_t ticks=123; int estimated=7;
    CHECK(!portable_mesh_race_result_ticks(s,0,&ticks,&estimated) && ticks==123 && estimated==7);
    const uint64_t checksum=portable_mesh_race_battle_checksum(s); CHECK(checksum);
    memcpy(before,s,sizeof *s);
    CHECK(portable_mesh_race_tick(s,input,events,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(before,s,sizeof *s) && checksum==portable_mesh_race_battle_checksum(s));
    for(unsigned i=0;i<sizeof events;++i) CHECK(((const uint8_t *)events)[i]==0);
    s->recovery_hit_mask=1; CHECK(checksum!=portable_mesh_race_battle_checksum(s));
    free(before);free(s);return 0;
}
/* Authored curve, independent of converted stock coordinates: a mandatory
 * finite panel precedes two connected left bends and a low outside wall. */
static int panel_curve_preview(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    f.rules.handling_response_q16=8*Q;
    f.rules.handling_turn_rate_q16=Q/2;
    f.rules.traction_on_q16=52429;
    f.rules.panel_boost_ticks=150;
    uint8_t vertices[144]={0},triangles[256]={0},surfaces[144]={0};
    const int32_t ys[4]={-30*Q,-8*Q,-4*Q,30*Q};
    for(unsigned row=0;row<4;++row)
        for(unsigned side=0;side<2;++side)
        {
            uint8_t *v=vertices+(row*2u+side)*12u;
            test_mesh_word(v,(uint32_t)(side ? 40*Q : -40*Q));
            test_mesh_word(v+4u,(uint32_t)ys[row]);
        }
    const int32_t wall[4][3]={{-36*Q,18*Q,0},{8*Q,18*Q,0},
        {-36*Q,18*Q,2*Q/5},{8*Q,18*Q,2*Q/5}};
    for(unsigned v=0;v<4;++v)
        for(unsigned axis=0;axis<3;++axis)
            test_mesh_word(vertices+(8u+v)*12u+axis*4u,(uint32_t)wall[v][axis]);
    const uint32_t indices[8][3]={{0,1,3},{0,3,2},{2,3,5},{2,5,4},
        {4,5,7},{4,7,6},{8,9,11},{8,11,10}};
    for(unsigned surface=0;surface<3;++surface)
        memcpy(surfaces+surface*48u,f.surface,48);
    test_mesh_word(surfaces+48u,4); /* Panel, not a manually applied boost. */
    test_mesh_word(surfaces+96u,3); /* Solid low wall. */
    for(unsigned t=0;t<8;++t)
    {
        for(unsigned axis=0;axis<3;++axis)
            test_mesh_word(triangles+t*32u+axis*4u,indices[t][axis]);
        test_mesh_word(triangles+t*32u+12u,t>=6u ? 2u : t==2u || t==3u ? 1u : 0u);
        for(unsigned field=20;field<32;field+=4)
            test_mesh_word(triangles+t*32u+field,UINT32_MAX);
    }
    for(unsigned i=0;i<f.course.table_count;++i)
    {
        PortableCourse3DTable *table=&f.course.tables[i];
        if(table->tag==PORTABLE_COURSE_3D_TAG('C','O','L','V'))
            *table=(PortableCourse3DTable){table->tag,12,12,vertices};
        else if(table->tag==PORTABLE_COURSE_3D_TAG('C','O','L','T'))
            *table=(PortableCourse3DTable){table->tag,8,32,triangles};
        else if(table->tag==PORTABLE_COURSE_3D_TAG('S','U','R','F'))
            *table=(PortableCourse3DTable){table->tag,3,48,surfaces};
    }
    uint8_t header[32]={0},nodes[192]={0},edges[24]={0};
    test_mesh_word(header+8u,6);test_mesh_word(header+16u,1);
    const int32_t xy[6][2]={{0,-16*Q},{0,0},{-12*Q,12*Q},{-28*Q,12*Q},
        {-28*Q,-20*Q},{0,-20*Q}};
    for(unsigned node=0;node<6;++node)
    {
        test_mesh_word(nodes+node*32u,(uint32_t)xy[node][0]);
        test_mesh_word(nodes+node*32u+4u,(uint32_t)xy[node][1]);
        test_mesh_word(nodes+node*32u+12u,3*Q/2);
        test_mesh_word(nodes+node*32u+16u,10*Q);
        test_mesh_word(nodes+node*32u+20u,node);
        test_mesh_word(nodes+node*32u+24u,1);
        test_mesh_word(edges+node*4u,(node+1u)%6u);
    }
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','H','D'),1,32,header};
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','N','D'),6,32,nodes};
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','E','D'),6,4,edges};
    /* Actual finite ordered checkpoint planes: +Y midway through the curve,
     * then -X on its exit. The arrival policy cannot manufacture either. */
    memset(f.gates,0,sizeof f.gates);
    for(unsigned gate=0;gate<2;++gate)
    {
        uint8_t *g=f.gates+gate*64u;
        test_mesh_word(g+36u,(gate ? 24u : 8u)*Q);
        test_mesh_word(g+40u,3*Q);
        test_mesh_word(g+44u,gate);test_mesh_word(g+48u,gate);
        test_mesh_word(g+52u,gate);test_mesh_word(g+56u,1);
        test_mesh_word(g+60u,gate ? 4u : 5u);
    }
    test_mesh_word(f.gates,(uint32_t)(-24*Q));test_mesh_word(f.gates+4u,12*Q);
    test_mesh_word(f.gates+12u,(uint32_t)-1073741824);
    test_mesh_word(f.gates+28u,1073741824);
    test_mesh_word(f.gates+64u+4u,6*Q);
    test_mesh_word(f.gates+64u+16u,1073741824);
    test_mesh_word(f.gates+64u+24u,1073741824);
    PortableMeshRacer racer;
    PortableMeshCpuState cpu;
    char error[192];
    CHECK(portable_mesh_time_trial_init(&racer,&f.course,&f.rules,0,error,sizeof error)==PORTABLE_MESH_OK);
    racer.state.phase=PORTABLE_MESH_RACER_RACING;
    racer.state.countdown_ticks=0;racer.state.crossing_armed=1;
    const int32_t position[3]={0,-10*Q,3*Q/4},forward[3]={0,1073741824,0};
    CHECK(portable_mesh_kart_init(&racer.state.kart,&f.rules,position,forward)==PORTABLE_MESH_OK);
    racer.state.kart.grounded=1;racer.state.kart.speed_q16=10*Q;
    racer.state.kart.velocity_q16[1]=10*Q;
    CHECK(portable_mesh_cpu_init(&cpu,&racer,0,error,sizeof error)==PORTABLE_MESH_OK);
    /* At/below the node cap the overspeed preview stays idle: yaw authority
     * scales pursuit steering and corner speeds but never moves the cursor
     * or flips the steering side. */
    PortableMeshRacer boundary=racer;
    boundary.state.kart.position_q16[1]=-Q/2;
    PortableMeshCpuState boundary_cpu;
    CHECK(portable_mesh_cpu_init(&boundary_cpu,&boundary,0,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(boundary_cpu.from_node==0 && boundary_cpu.to_node==1);
    for(unsigned speed=9;speed<=10;++speed)
    {
        boundary.state.kart.speed_q16=(int32_t)speed*Q;
        PortableMeshCpuState normal,zero;
        PortableMeshKartInput a,b;
        CHECK(portable_mesh_cpu_input(&normal,&boundary_cpu,&boundary,&a,error,sizeof error)==PORTABLE_MESH_OK);
        const int32_t response=f.rules.handling_response_q16,turn=f.rules.handling_turn_rate_q16;
        f.rules.handling_response_q16=0;f.rules.handling_turn_rate_q16=0;
        CHECK(portable_mesh_cpu_input(&zero,&boundary_cpu,&boundary,&b,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(a.steering!=0 && (a.steering>0)==(b.steering>0) &&
            !memcmp(&normal,&zero,sizeof normal));
        f.rules.handling_response_q16=response;f.rules.handling_turn_rate_q16=turn;
    }
    boundary.state.kart.speed_q16=20*Q;
    for(unsigned tiny=0;tiny<3;++tiny)
    {
        PortableMeshCpuState bounded;
        PortableMeshKartInput input;
        f.rules.handling_response_q16=tiny==0 ? 0 : 1;
        f.rules.handling_turn_rate_q16=tiny==2 ? 0 : 1;
        CHECK(portable_mesh_cpu_input(&bounded,&boundary_cpu,&boundary,&input,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(bounded.route==boundary_cpu.route && bounded.from_node==0 && bounded.to_node==1 &&
            input.steering>=-32767 && !(input.buttons&~15u));
    }
    /* A saturated preview must still validate every traversed connected edge,
     * preserving its outputs when an invalid edge is encountered later. */
    test_mesh_word(edges+2u*4u,UINT32_MAX);
    PortableMeshCpuState rejected=boundary_cpu;
    PortableMeshKartInput sentinel={123,7},rejected_input=sentinel;
    const uint64_t before=portable_mesh_time_trial_checksum(&boundary.state);
    f.rules.handling_response_q16=1;f.rules.handling_turn_rate_q16=1;
    CHECK(portable_mesh_cpu_input(&rejected,&boundary_cpu,&boundary,&rejected_input,error,sizeof error)!=PORTABLE_MESH_OK);
    CHECK(!memcmp(&rejected,&boundary_cpu,sizeof rejected) &&
        rejected_input.steering==sentinel.steering && rejected_input.buttons==sentinel.buttons &&
        before==portable_mesh_time_trial_checksum(&boundary.state));
    test_mesh_word(edges+2u*4u,3);
    f.rules.handling_response_q16=8*Q;f.rules.handling_turn_rate_q16=Q/2;
    /* Live simulation retains the original -10m approach and its CPU cursor. */
    unsigned panels=0,panel_brake=0,passed=0,visits=0;
    for(unsigned tick=0;tick<600 && racer.state.phase!=PORTABLE_MESH_RACER_FINISHED;++tick)
    {
        PortableMeshKartInput input;
        CHECK(portable_mesh_cpu_input(&cpu,&cpu,&racer,&input,error,sizeof error)==PORTABLE_MESH_OK);
        PortableMeshRacerTurn turn;
        CHECK(portable_mesh_racer_move(&racer,input,&turn,error,sizeof error)==PORTABLE_MESH_OK);
        panels+=turn.events.panel;
        panel_brake|=turn.events.panel && (input.buttons&PORTABLE_MESH_BRAKE);
        CHECK(!turn.events.wall && racer.state.kart.grounded && !racer.state.kart.respawn_ticks);
        CHECK(portable_mesh_racer_publish(&racer,&turn,error,sizeof error)==PORTABLE_MESH_OK);
        passed|=racer.state.checkpoint==1;
        if(cpu.visit_serial>visits) visits=cpu.visit_serial;
    }
    CHECK(panels && panel_brake && passed && visits>=2 &&
        racer.state.phase==PORTABLE_MESH_RACER_FINISHED && racer.state.lap==1);
    return 0;
}
int main(void)
{
    return pier_boo_shared_owner() || kamek_shared_owner() || mechakoopa_cpu_owner() || common_prop_shared_owner() || thundercloud_shared_owner() || falling_star_shared_owner() || teepee_shared_owner() || coin_only_owner() || panel_curve_preview() || countdown_and_identity() || activation_and_atomic_failure() ||
           pair_pickup_and_rescue() || thwomp_crush_owner() || declaration_requires_owner() || battle_world();
}
