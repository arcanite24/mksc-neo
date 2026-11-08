#include "item_rules_fixture.h"
#include "gp_banana.h"

#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) {fprintf(stderr,"banana check failed line %d: %s\n",__LINE__,#condition);return __LINE__;} } while (0)

static int test_flight_and_terrain(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    uint8_t ground[256u*256u]={0},cells[128u*128u]={0};
    uint8_t flags[256]={0x40u};
    const PortableTerrainView terrain={ground,256u,256u,256u};
    const PortableTrackRaster raster={.cells=cells,.width=128u,.height=128u,.capacity=sizeof cells};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    PortableRacePoint route[8]={0},rows[16]={0};route[2]=(PortableRacePoint){1100,900};
    const PortableGpBananaWorld world={.material=&material,.raster=&raster,.terrain=&terrain,
        .route=route,.route_rows=rows,.route_row_count=2u,.checkpoint_count=8u};
    const PortableBananaRules *authored=test_banana_rules();
    unsigned flight_ticks=1u;
    while(INT64_C(65536)+(int64_t)flight_ticks*authored->throw_velocity_q16-
        (int64_t)flight_ticks*(flight_ticks-1u)*authored->throw_gravity_q16/2>0)++flight_ticks;
    for(unsigned landing=0u;landing<4u;++landing){
        flags[0]=0x40u;
        PortableGpBananaOwner owner={.rules=test_banana_rules(),.position_x_q16=1000*65536,.position_y_q16=1000*65536,
            .flags_48=0x8000u,.control_96=0x880u,.angle=0x8000u};
        PortableGpBanana banana={.rules=test_banana_rules(),};PortableGpItemInventory inventory={.held_item_17a=0x1005u};
        CHECK(portable_gp_banana_use(&inventory,&owner,&banana,sine,1024u)==PORTABLE_GP_BANANA_OK);
        CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK);
        CHECK(banana.phase==PORTABLE_GP_BANANA_THROWN && banana.position_z_q16==65536 &&
              banana.velocity_x_q16==0 && banana.velocity_y_q16==-(int32_t)authored->throw_speed_q16 &&
              banana.vertical_velocity_q16==(int32_t)authored->throw_velocity_q16 && banana.collision_guard_38==authored->collision_grace_ticks &&
              owner.shell_item_18c==0u && (owner.flags_48&0x10000u));
        PortableGpBananaAudioEvent audio;
        CHECK(portable_gp_banana_take_motion_audio(&banana,0u,1000*65536,1000*65536,&audio)==PORTABLE_GP_BANANA_OK &&
              audio.song_id==123u && audio.volume==256u && audio.player==4u);
        const uint8_t materials[]={0x40u,0x80u,0x26u,0x22u};flags[0]=materials[landing];
        for(unsigned tick=1u;tick<=flight_ticks;++tick){
            CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK);
            CHECK(banana.collision_guard_38==(tick<authored->collision_grace_ticks?authored->collision_grace_ticks-tick:0u));
            CHECK(((owner.flags_48&0x10000u)!=0u)==(tick<authored->release_lock_ticks));
            if(tick<flight_ticks)CHECK(banana.phase==PORTABLE_GP_BANANA_THROWN && banana.position_z_q16>0);
        }
        CHECK(banana.position_y_q16==1008*65536-(int32_t)flight_ticks*(int32_t)authored->throw_speed_q16 && banana.position_z_q16==0);
        CHECK(banana.phase==(landing==0u?PORTABLE_GP_BANANA_DROPPED:
            landing==3u?PORTABLE_GP_BANANA_DESTROYED:PORTABLE_GP_BANANA_DESTROYING));
        CHECK(portable_gp_banana_take_motion_audio(&banana,0u,-1000*65536,-1000*65536,&audio)==PORTABLE_GP_BANANA_OK &&
              audio.song_id==122u && audio.volume==64u);
    }
    for(unsigned drop=0u;drop<4u;++drop){
        const uint8_t materials[]={0x40u,0x80u,0x26u,0x22u};flags[0]=materials[drop];
        PortableGpBananaOwner owner={.rules=test_banana_rules(),.position_x_q16=1000*65536,.position_y_q16=1000*65536,
            .flags_48=0x8000u,.control_96=0x80u,.angle=0x8000u};
        PortableGpBanana banana={.rules=test_banana_rules(),};PortableGpItemInventory inventory={.held_item_17a=0x1005u};
        CHECK(portable_gp_banana_use(&inventory,&owner,&banana,sine,1024u)==PORTABLE_GP_BANANA_OK &&
              portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK);
        CHECK(banana.phase==(drop==0u?PORTABLE_GP_BANANA_DROPPED:
            drop==1u?PORTABLE_GP_BANANA_DESTROYING:PORTABLE_GP_BANANA_DESTROYED));
        CHECK(banana.motion_audio_pending==(drop==0u?122u:0u) && owner.shell_item_18c==0u);
    }
    /* CPU190 retains the attachment until its signed deadline, then selects
     * the next source route row and checkpoint+3, including both wraps. */
    flags[0]=0x40u;
    PortableGpBananaOwner owner={.rules=test_banana_rules(),.position_x_q16=1000*65536,.position_y_q16=1000*65536,
        .flags_48=0x2000u,.control_96=0x80u,.angle=0x8000u,.decision_190=1u,
        .floor_36=7u,.route_row_191=2u};
    PortableGpBanana banana={.rules=test_banana_rules(),};PortableGpItemInventory inventory={.held_item_17a=0x1005u};
    CHECK(portable_gp_banana_use(&inventory,&owner,&banana,sine,1024u)==PORTABLE_GP_BANANA_OK);
    banana.attachment_ticks_4c=(uint16_t)(authored->cpu_hold_ticks-2u);
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK && banana.phase==PORTABLE_GP_BANANA_HELD);
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK &&
          banana.phase==PORTABLE_GP_BANANA_THROWN && owner.decision_190==0u &&
          banana.velocity_x_q16==100*65536/(int32_t)authored->cpu_flight_ticks && banana.velocity_y_q16==-108*65536/(int32_t)authored->cpu_flight_ticks);
    /* Water-mode visibility and the EC280 damage-angle override are held
     * behavior; neither should accidentally release a still-held banana. */
    owner.flags_48=0x8000u;owner.control_96=0x40u;owner.mode_78=2u;
    owner.response_status_7e=13u;owner.target_heading_56=0x4000u;
    banana.phase=PORTABLE_GP_BANANA_HELD;
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK &&
          !(banana.flags_34&3u) && banana.angle_26==0xc000u && banana.position_x_q16==992*65536);
    owner.mode_78=1u;
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==PORTABLE_GP_BANANA_OK && (banana.flags_34&3u)==3u);
    return 0;
}

static int test_custom_rules(void)
{
    PortableBananaRules rules={262144u,24u,155648u,12288u,4u,8u,60u,65536u,16384u};
    CHECK(portable_banana_rules_valid(&rules));
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    uint8_t ground[256u*256u]={0},cells[128u*128u]={0},flags[256]={0x40u};
    const PortableTerrainView terrain={ground,256u,256u,256u};
    const PortableTrackRaster raster={.cells=cells,.width=128u,.height=128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    PortableRacePoint route[8]={0},rows[16]={0};route[2]=(PortableRacePoint){1100,900};
    const PortableGpBananaWorld world={.material=&material,.raster=&raster,.terrain=&terrain,
        .route=route,.route_rows=rows,.route_row_count=2u,.checkpoint_count=8u};
    PortableGpBananaOwner owner={.rules=&rules,.position_x_q16=1000*65536,.position_y_q16=1000*65536,
        .flags_48=0x8000u,.control_96=0x880u,.angle=0x8000u};
    PortableGpItemInventory inventory={.held_item_17a=PORTABLE_GP_BANANA_ITEM};PortableGpBanana banana={.rules=test_banana_rules(),};
    CHECK(portable_gp_banana_use(&inventory,&owner,&banana,sine,1024u)==0&&banana.rules==&rules);
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==0);
    CHECK(banana.phase==PORTABLE_GP_BANANA_THROWN&&banana.velocity_y_q16==-4*65536&&
        banana.vertical_velocity_q16==155648&&banana.collision_guard_38==4u);
    const int32_t start_y=banana.position_y_q16;
    for(unsigned tick=1u;tick<=27u;++tick){
        CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==0);
        CHECK(banana.collision_guard_38==(tick<4u?4u-tick:0u));
        CHECK(((owner.flags_48&0x10000u)!=0u)==(tick<8u));
        CHECK(banana.phase==(tick<27u?PORTABLE_GP_BANANA_THROWN:PORTABLE_GP_BANANA_DROPPED));
    }
    CHECK(banana.position_y_q16==start_y-27*4*65536&&banana.position_z_q16==0);
    CHECK(portable_gp_banana_collide(&banana,&owner.flags_48,0x2000u,0)==0&&banana.vertical_velocity_q16==65536);
    CHECK(portable_gp_banana_tick_destroying(&banana)==0&&banana.vertical_velocity_q16==49152);
    for(unsigned i=0u;i<16u&&banana.occupied;++i)CHECK(portable_gp_banana_tick_destroying(&banana)==0);
    CHECK(!banana.occupied);
    owner.flags_48=0x2000u;owner.control_96=0x80u;owner.decision_190=1u;owner.floor_36=7u;owner.route_row_191=2u;
    inventory.held_item_17a=PORTABLE_GP_BANANA_ITEM;banana=(PortableGpBanana){.rules=test_banana_rules(),};
    CHECK(portable_gp_banana_use(&inventory,&owner,&banana,sine,1024u)==0);
    banana.attachment_ticks_4c=58u;
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==0&&banana.phase==PORTABLE_GP_BANANA_HELD);
    CHECK(portable_gp_banana_tick(&owner,&banana,&world,sine,1024u)==0&&banana.phase==PORTABLE_GP_BANANA_THROWN&&owner.decision_190==0u);
    CHECK(banana.velocity_x_q16==100*65536/24&&banana.velocity_y_q16==-108*65536/24);
    rules.release_lock_ticks=28u;CHECK(!portable_banana_rules_valid(&rules));
    rules.release_lock_ticks=1000u;rules.throw_velocity_q16=16777215u;rules.throw_gravity_q16=1u;
    CHECK(!portable_banana_rules_valid(&rules));
    return 0;
}

int main(void)
{
    CHECK(test_custom_rules()==0);
    CHECK(test_flight_and_terrain()==0);
    int32_t sine[1024] = {0};
    PortableGpItemInventory inventory = {0};
    PortableGpBananaOwner owner = {.rules=test_banana_rules(),};
    PortableGpBanana banana = {.rules=test_banana_rules(),};
    uint8_t ground[128u*128u]={0},cells[64u*64u]={0};
    const uint8_t flags[256]={0x40u};
    const PortableTerrainView terrain={ground,128u,128u,128u};
    const PortableTrackRaster raster={.cells=cells,.width=64u,.height=64u,.capacity=sizeof cells};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableGpBananaWorld world={.material=&material,.raster=&raster,.terrain=&terrain};

    sine[0] = INT32_C(0x00010000);
    sine[0x100] = INT32_C(0x00008000);
    inventory.held_item_17a = PORTABLE_GP_BANANA_ITEM;
    inventory.settled_item = PORTABLE_GP_BANANA_ITEM;
    owner.position_x_q16 = INT32_C(0x01000000);
    owner.position_y_q16 = INT32_C(0x02000000);
    owner.position_z_q16 = INT32_C(0x00030000);
    owner.flags_48 = UINT32_C(0x00008000);
    owner.control_96 = UINT16_C(0x00c0);
    owner.floor_36 = 7u;

    CHECK(portable_gp_banana_use(
              &inventory,&owner,&banana,sine,1024u) == PORTABLE_GP_BANANA_OK);
    CHECK(inventory.held_item_17a == 0u && inventory.settled_item == 0u &&
          owner.shell_item_18c == PORTABLE_GP_BANANA_ITEM);
    CHECK(banana.occupied == 1u && banana.kind_10 == 0x8001u &&
          banana.phase == PORTABLE_GP_BANANA_HELD && banana.flags_34 == 3u &&
          banana.angle_26 == 0u &&
          banana.position_x_q16 == INT32_C(0x01100000) &&
          banana.position_y_q16 == INT32_C(0x01f80000) &&
          banana.position_z_q16 == owner.position_z_q16 &&
          banana.floor_36 == 7u);

    owner.position_x_q16 += INT32_C(0x00020000);
    CHECK(portable_gp_banana_tick(
              &owner,&banana,&world,sine,1024u) == PORTABLE_GP_BANANA_OK);
    CHECK(banana.position_x_q16 == INT32_C(0x01120000) &&
          banana.attachment_ticks_4c == 1u && banana.flags_34 == 3u);
    owner.control_96 &= UINT16_C(0xffbf);
    CHECK(portable_gp_banana_tick(
              &owner,&banana,&world,sine,1024u) == PORTABLE_GP_BANANA_OK);
    CHECK(banana.phase == PORTABLE_GP_BANANA_DROPPED &&
          banana.position_z_q16 == 0 && banana.collision_active == 1u &&
          banana.attachment_ticks_4c == 2u && owner.shell_item_18c == 0u);
    CHECK(portable_gp_banana_tick(
              &owner,&banana,&world,sine,1024u) == PORTABLE_GP_BANANA_INELIGIBLE);

    CHECK(portable_gp_banana_collide(
              &banana,&owner.flags_48,UINT16_C(0x8800),0)==
          PORTABLE_GP_BANANA_OK);
    CHECK(banana.collision_result==PORTABLE_GP_BANANA_COLLISION_IGNORED&&
          banana.phase==PORTABLE_GP_BANANA_DROPPED);
    owner.flags_48|=UINT32_C(0x00010000);
    CHECK(portable_gp_banana_collide(
              &banana,&owner.flags_48,UINT16_C(0x8001),0)==
          PORTABLE_GP_BANANA_OK);
    CHECK(banana.phase==PORTABLE_GP_BANANA_DESTROYING&&
          banana.collision_result==
              PORTABLE_GP_BANANA_COLLISION_DESTROYING_ITEM&&
          banana.vertical_velocity_q16==(int32_t)test_banana_rules()->destruction_velocity_q16&&
          banana.destruction_field_5a==UINT16_C(0x8000)&&
          (owner.flags_48&UINT32_C(0x00010000))==0u&&
          banana.contact_audio_pending);
    {
        PortableGpBananaAudioEvent event={0};
        CHECK(portable_gp_banana_take_contact_audio(
                  &banana,0u,0x40u,&event)==PORTABLE_GP_BANANA_OK);
        CHECK(event.song_id==124u&&event.player==4u&&event.volume==0x40u&&
              event.pitch==0&&!banana.contact_audio_pending);
    }
    for(unsigned tick=0u;tick<14u;++tick)
        CHECK(portable_gp_banana_tick_destroying(&banana)==
              PORTABLE_GP_BANANA_OK);
    CHECK(!banana.occupied&&banana.phase==PORTABLE_GP_BANANA_DESTROYED&&
          banana.position_z_q16==0&&banana.vertical_velocity_q16==0);

    /* 479E0 shares the family/66 gates, has no height gate, clears18C
     * only on accepted impact and never consumes a shell17C slot. */
    for(unsigned gate=0u;gate<5u;++gate){
        banana=(PortableGpBanana){.rules=test_banana_rules(),.occupied=1u,.kind_10=PORTABLE_GP_BANANA_KIND,
            .phase=PORTABLE_GP_BANANA_HELD,.flags_34=3u};
        owner.flags_48=0x18000u;owner.shell_item_18c=0x1005u;
        owner.item_use_state_17c=7u;
        const uint16_t kinds[]={0x8800u,0x1003u,0x2002u,0x2001u,0x8402u};
        CHECK(portable_gp_banana_collide_held(&banana,&owner,kinds[gate],gate==2u?1:0)==PORTABLE_GP_BANANA_OK);
        CHECK(owner.item_use_state_17c==7u);
        if(gate<3u){CHECK(owner.shell_item_18c==0x1005u && banana.phase==PORTABLE_GP_BANANA_HELD);}
        else {CHECK(owner.shell_item_18c==0u && !(owner.flags_48&0x10000u) &&
            banana.phase==PORTABLE_GP_BANANA_DESTROYING && banana.contact_audio_pending==(gate==4u));}
    }

    banana.occupied = 0u;
    inventory.held_item_17a = PORTABLE_GP_BANANA_ITEM;
    owner.control_96 = 0u;
    CHECK(portable_gp_banana_use(
              &inventory,&owner,&banana,sine,1024u) ==
          PORTABLE_GP_BANANA_INELIGIBLE);
    CHECK(inventory.held_item_17a == PORTABLE_GP_BANANA_ITEM &&
          banana.occupied == 0u);

    puts("GP banana: PASS held/drop/throw, source flight, terrain landing, owner lock, CPU target and contacts");
    return 0;
}
