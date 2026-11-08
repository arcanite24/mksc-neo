#include "item_rules_fixture.h"
#include "gp_green_shell.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"green check failed line %d: %s\n",__LINE__,#condition); return __LINE__; } } while (0)

static int test_custom_rules(void)
{
    const PortableGreenShellRules custom={384u,60u,4u,2u,8u,60u,0x18000u,0x3000u};
    CHECK(portable_green_shell_rules_valid(&custom));
    int32_t sine[1024]={0};sine[256]=65536;
    PortableGpItemInventory inventory={.held_item_17a=PORTABLE_GP_GREEN_SHELL_ITEM};
    PortableGpGreenShellOwner owner={.orbit_rules=test_shell_orbit_rules(),.position_x_q16=512*65536,.position_y_q16=512*65536,
        .flags_48=0x8000u,.control_96=0x80u,.speed_q16=1000*65536,.rules=&custom};
    PortableGpGreenShell shell={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    CHECK(portable_gp_green_shell_use(&inventory,&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK&&shell.rules==&custom);
    CHECK(portable_gp_green_shell_tick_attached(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    owner.control_96=0u;
    CHECK(portable_gp_green_shell_release(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.state_71==60u&&shell.velocity_y_q16==-60*8192&&shell.lifetime_38==4u&&shell.release_timer_74==2u);
    PortableGreenShellRules uncapped=custom;uncapped.max_speed=255u;
    PortableGpGreenShellOwner slow=owner;slow.rules=&uncapped;slow.speed_q16=0;slow.item_use_state_17c=1u;
    PortableGpGreenShell probe={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_GREEN_SHELL_KIND,.phase=PORTABLE_GP_GREEN_SHELL_ACTIVE};
    CHECK(portable_gp_green_shell_release(&slow,&probe,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK&&probe.state_71==24u&&probe.velocity_y_q16==-24*8192);
    uint8_t cells[128*128]={0},floors[128*128]={0},flags[256]={0};flags[0]=0x40u;flags[1]=0x80u;
    const int32_t offsets[8]={0};
    const PortableTrackRaster raster={.cells=floors,.width=128u,.height=128u};
    const PortableTerrainView terrain={.cells=cells,.width=128u,.height=128u,.row_stride=128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    const PortableGpGreenShellWorld world={&material,&raster,&terrain,&reflection};
    for(unsigned tick=1u;tick<=8u;++tick){
        CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(shell.lifetime_38==(tick<4u?4u-tick:0u));
        CHECK((owner.flags_48&0x10000u)==(tick<8u?0x10000u:0u));
    }
    cells[8u*128u+8u]=1u;
    for(unsigned contact=0u;contact<2u;++contact){
        shell.position_x_q16=shell.position_y_q16=64*65536;shell.terrain_state_52=8u*256u+7u;
        CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(shell.release_timer_74==1u-contact);
    }
    CHECK(shell.phase==PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING);
    CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK&&shell.vertical_velocity_q16==0x18000);
    CHECK(portable_gp_green_shell_tick_destroying(&shell)==PORTABLE_GP_GREEN_SHELL_OK&&shell.position_z_q16==0x18000&&shell.vertical_velocity_q16==0x15000);
    for(unsigned tick=0u;tick<64u&&shell.occupied;++tick)CHECK(portable_gp_green_shell_tick_destroying(&shell)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(!shell.occupied);
    shell=(PortableGpGreenShell){.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_GREEN_SHELL_KIND,.phase=PORTABLE_GP_GREEN_SHELL_ACTIVE,.rules=&custom,.animation_tick_4c=58u};
    owner.flags_48=0x2000u;owner.decision_190=1u;owner.item_use_state_17c=1u;
    CHECK(portable_gp_green_shell_tick_held_active(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK&&shell.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE&&owner.decision_190==1u);
    CHECK(portable_gp_green_shell_tick_held_active(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK&&shell.phase==PORTABLE_GP_GREEN_SHELL_RELEASED&&owner.decision_190==0u);
    PortableGpGreenShell triple[3];const uint8_t delays[3]={0};
    inventory.held_item_17a=PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM;owner.item_use_state_17c=0u;owner.flags_48=0x8000u;owner.control_96=0x80u;
    CHECK(portable_gp_green_shell_use_triple(&inventory,&owner,triple,delays,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    for(unsigned i=0u;i<3u;++i)CHECK(triple[i].rules==&custom);
    return 0;
}

static int test_terrain_lifecycle(void)
{
    uint8_t cells[128*128]={0},floor_cells[128*128]={0},flags[256]={0};
    flags[0]=0x40u;flags[1]=0x80u;flags[2]=0x22u;
    const int32_t offsets[8]={0};
    const PortableTrackRaster raster={.cells=floor_cells,.width=128u,.height=128u};
    const PortableTerrainView terrain={.cells=cells,.width=128u,.height=128u,.row_stride=128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    const PortableGpGreenShellWorld world={&material,&raster,&terrain,&reflection};
    PortableGpGreenShellOwner owner={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.flags_48=0x10000u};
    PortableGpGreenShell shell={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_GREEN_SHELL_KIND,
        .phase=PORTABLE_GP_GREEN_SHELL_RELEASED,.flags_34=3u,.release_timer_74=7u,
        .lifetime_38=8u,.velocity_x_q16=65536,.velocity_y_q16=131072};
    cells[8u*128u+8u]=1u;
    /* A step into the next X cell dispatches mode0. Each wall visit consumes
     * one of seven contacts; the final visit still integrates before7884. */
    for(unsigned contact=0u;contact<7u;++contact){
        shell.position_x_q16=64*65536;shell.position_y_q16=64*65536;
        shell.terrain_state_52=8u*256u+7u;
        const int32_t previous_x=shell.velocity_x_q16;
        CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(shell.velocity_x_q16==-previous_x && shell.velocity_y_q16==131072);
        CHECK(shell.position_x_q16==64*65536-previous_x && shell.position_y_q16==66*65536);
        CHECK(shell.release_timer_74==6u-contact && shell.contact_audio_pending);
        CHECK(shell.collision_state_50==(0x8001u+contact));
    }
    CHECK(shell.phase==PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING && shell.lifetime_38==1u);
    const int32_t last_x=shell.position_x_q16;
    CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_GREEN_SHELL_DESTROYING && shell.position_x_q16==last_x &&
        shell.vertical_velocity_q16==(int32_t)test_green_shell_rules()->destruction_velocity_q16 && !(shell.flags_34&2u) && !(owner.flags_48&0x10000u));
    /* Suppressed repeated contacts still reflect. Clear ground resets the
     * contact state, and airborne actors retain the previous material. */
    shell.phase=PORTABLE_GP_GREEN_SHELL_RELEASED;shell.release_timer_74=7u;
    shell.position_x_q16=64*65536;shell.position_y_q16=64*65536;
    shell.terrain_state_52=8u*256u+7u;shell.collision_state_50=0xc000u;
    shell.contact_audio_pending=0u;
    CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.release_timer_74==7u && !shell.contact_audio_pending && shell.collision_state_50==0xc000u);
    cells[8u*128u+8u]=0u;
    shell.position_x_q16=64*65536;shell.position_y_q16=64*65536;
    shell.floor_36=9u;floor_cells[4u*128u+4u]=0x7fu;
    CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.collision_state_50==0u && shell.floor_36==9u);
    floor_cells[4u*128u+4u]=0xffu;
    shell.position_x_q16=64*65536;shell.position_y_q16=64*65536;
    shell.position_z_q16=65536;shell.collision_flags_4e=0x40u;
    cells[8u*128u+8u]=2u;
    CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.occupied && shell.floor_36==0x7fu && shell.collision_flags_4e==0x40u);
    for(unsigned out=0u;out<2u;++out){
        shell.position_z_q16=0;shell.position_x_q16=out?-1:64*65536;
        shell.position_y_q16=64*65536;owner.flags_48|=0x10000u;
        shell.occupied=1u;shell.phase=PORTABLE_GP_GREEN_SHELL_RELEASED;
        CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world)==PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(!shell.occupied && shell.phase==PORTABLE_GP_GREEN_SHELL_DESTROYED && !(owner.flags_48&0x10000u));
    }
    return 0;
}

static int test_cpu_held_lifecycle(void)
{
    int32_t sine[1024]={0};sine[256]=65536;sine[768]=-65536;
    PortableGpItemInventory inventory={.held_item_17a=PORTABLE_GP_GREEN_SHELL_ITEM};
    PortableGpGreenShellOwner owner={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.flags_48=0x2000u,.decision_190=3u};
    PortableGpGreenShell shell={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    CHECK(portable_gp_green_shell_use(&inventory,&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(portable_gp_green_shell_tick_attached(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    owner.mode_78=2u;owner.response_status_7e=5u;owner.target_heading_56=0u;
    CHECK(portable_gp_green_shell_tick_held_active(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.angle_26==0x8000u && !(shell.flags_34&3u) && shell.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE);
    owner.mode_78=0u;owner.response_status_7e=0u;shell.animation_tick_4c=(uint16_t)(test_green_shell_rules()->cpu_hold_ticks-2u);
    CHECK(portable_gp_green_shell_tick_held_active(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE && (shell.flags_34&3u)==3u);
    owner.speed_q16=-65536;
    CHECK(portable_gp_green_shell_tick_held_active(&owner,&shell,sine,1024u)==PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_GREEN_SHELL_RELEASED && owner.decision_190==2u &&
        shell.angle_26==0x8000u && shell.state_71==(test_green_shell_rules()->launch_speed_bonus-1u)/16u && shell.launch_audio_pending);
    return 0;
}

static int test_triple_orbit(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;sine[257]=1234;
    const uint8_t delays[3]={0u,17u,34u};
    PortableGpGreenShellOwner owner={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.control_96=0x80u,.position_x_q16=100*65536,
        .position_y_q16=100*65536,.position_z_q16=65536,.floor_36=9u};
    PortableGpItemInventory inventory={.held_item_17a=0x1001u};
    PortableGpGreenShell shells[3];
    CHECK(portable_gp_green_shell_use_triple(&inventory,&owner,shells,delays,sine,1024u)==0);
    CHECK(owner.item_use_state_17c==3u && !owner.shell_item_18c && !inventory.held_item_17a);
    for(unsigned i=0u;i<3u;++i){
        CHECK(!shells[i].flags_34 && shells[i].retire_held_slot &&
            shells[i].position_y_q16==100*65536-(int32_t)test_shell_orbit_rules()->radius_multiplier*32768 && shells[i].position_z_q16==65536);
        for(unsigned tick=0u;tick<delays[i];++tick){
            CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[i],sine,1024u)==0);
            CHECK(shells[i].phase==PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING);
        }
        CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[i],sine,1024u)==0);
        CHECK(shells[i].phase==PORTABLE_GP_GREEN_SHELL_ORBITING && shells[i].orbit_audio_pending &&
            shells[i].animation_tick_4c==0u && shells[i].flags_34==3u);
    }
    owner.control_96=0x40u;
    shells[0].animation_tick_4c=64u; /* low byte461*5==1: angle0100. */
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].angle_26==(uint16_t)(64u*test_shell_orbit_rules()->angle_step) && shells[0].position_x_q16==100*65536+(int32_t)test_shell_orbit_rules()->radius_multiplier*1234 &&
        shells[0].animation_tick_4c==65u && owner.item_use_state_17c==3u);
    owner.mode_78=2u;
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0 && !(shells[0].flags_34&3u));
    owner.mode_78=0u;owner.control_96=0x4c0u;owner.release_angle_offset=0x4000;
    /* Down and held Item do not turn an orbit shot backward. One release
     * locks siblings even when the same press reaches each callback. */
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[2],sine,1024u)==0);
    CHECK(shells[2].phase==PORTABLE_GP_GREEN_SHELL_RELEASED && !shells[2].retire_held_slot &&
        shells[2].angle_26==0x4000u && shells[2].velocity_x_q16>0 &&
        owner.item_use_state_17c==2u && (owner.flags_48&0x10000u));
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[1],sine,1024u)==0 &&
        shells[1].phase==PORTABLE_GP_GREEN_SHELL_ORBITING && owner.item_use_state_17c==2u);
    owner.flags_48=0x800u;
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[1],sine,1024u)==0 && owner.item_use_state_17c==2u);
    owner.flags_48=0u;
    CHECK(portable_gp_green_shell_collide_held(&shells[1],&owner,0x2002u,0,1)==0 &&
        shells[1].phase==PORTABLE_GP_GREEN_SHELL_ORBITING);
    CHECK(portable_gp_green_shell_collide_held(&shells[1],&owner,0x2002u,0,0)==0 &&
        shells[1].phase==PORTABLE_GP_GREEN_SHELL_DESTROYING && owner.item_use_state_17c==1u &&
        shells[1].retire_held_slot); /*446FC leaves6C installed. */
    return 0;
}

static int test_custom_orbit(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[12]=1234;sine[244]=2000;sine[1012]=-1234;
    PortableShellOrbitRules rules={22u,768u,3u};
    const uint8_t delays[3]={2u,4u,6u};
    PortableGpGreenShellOwner owner={.rules=test_green_shell_rules(),.control_96=0x80u,.position_x_q16=100*65536,
        .position_y_q16=100*65536,.position_z_q16=65536,.floor_36=9u,.orbit_rules=&rules};
    PortableGpItemInventory inventory={.held_item_17a=4097u};
    PortableGpGreenShell shells[3];
    CHECK(portable_gp_green_shell_use_triple(&inventory,&owner,shells,delays,sine,1024u)==0);
    owner.control_96=0u;
    CHECK(shells[0].orbit_rules==&rules && shells[0].position_y_q16==89*65536);
    for(unsigned i=0u;i<3u;++i){
        CHECK(shells[i].release_timer_74==delays[i]);
        for(unsigned t=0u;t<delays[i];++t){
            CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[i],sine,1024u)==0);
            CHECK(shells[i].phase==PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING);
        }
        CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[i],sine,1024u)==0);
        CHECK(shells[i].phase==PORTABLE_GP_GREEN_SHELL_ORBITING);
    }
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].animation_index_70==1u && shells[0].display_index==0u);
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].angle_26==768u && shells[0].position_x_q16==100*65536+22*1234 &&
        shells[0].position_y_q16==100*65536-22*2000 && shells[0].animation_index_70==1u);
    for(unsigned t=0u;t<2u;++t)CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].animation_index_70==2u && shells[0].display_index==1u);
    shells[0].animation_tick_4c=65535u;
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].angle_26==0xfd00u && shells[0].animation_tick_4c==0u &&
        shells[0].position_x_q16==100*65536-22*1234);
    owner.mode_78=2u;
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0 && !(shells[0].flags_34&3u));
    rules.animation_period=0u;
    CHECK(portable_gp_green_shell_tick_orbit(&owner,&shells[0],sine,1024u)==PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    CHECK(test_custom_rules()==0);
    CHECK(test_custom_orbit()==0);
    CHECK(test_terrain_lifecycle()==0);
    CHECK(test_triple_orbit()==0);
    CHECK(test_cpu_held_lifecycle()==0);
    uint8_t cells[128*128]={0},materials[256]={0};materials[0]=0x40u;
    const int32_t offsets[8]={0};
    const PortableTrackRaster raster={.cells=cells,.width=128u,.height=128u};
    const PortableTerrainView terrain={.cells=cells,.width=128u,.height=128u};
    const PortableTrackMaterialView material={.flags=materials,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    const PortableGpGreenShellWorld world={&material,&raster,&terrain,&reflection};
    int32_t sine[1024] = {0};
    PortableGpItemInventory inventory = {0};
    PortableGpGreenShellOwner owner = {.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    PortableGpGreenShell shell = {.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};

    sine[0] = INT32_C(0x00010000);
    sine[0x100] = INT32_C(0x00008000);
    inventory.held_item_17a = PORTABLE_GP_GREEN_SHELL_ITEM;
    inventory.settled_item = PORTABLE_GP_GREEN_SHELL_ITEM;
    owner.position_x_q16 = INT32_C(0x01000000);
    owner.position_y_q16 = INT32_C(0x02000000);
    owner.position_z_q16 = INT32_C(0x00030000);
    owner.control_96 = UINT16_C(0x00c0);
    owner.angle = 0u;
    owner.floor_36 = 7u;

    CHECK(portable_gp_green_shell_use(&inventory,&owner,&shell,sine,1024u) ==
          PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(inventory.held_item_17a == 0u && inventory.settled_item == 0u &&
          owner.item_use_state_17c == 1u);
    CHECK(shell.occupied == 1u && shell.kind_10 == 0x8401u &&
          shell.phase == PORTABLE_GP_GREEN_SHELL_ATTACHED);
    CHECK(shell.angle_26 == 0u &&
          shell.position_x_q16 == INT32_C(0x01100000) &&
          shell.position_y_q16 == INT32_C(0x01f80000) &&
          shell.position_z_q16 == owner.position_z_q16 && shell.floor_36 == 7u);

    owner.position_x_q16 += INT32_C(0x00020000);
    CHECK(portable_gp_green_shell_tick_attached(
              &owner,&shell,sine,1024u) == PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.position_x_q16 == INT32_C(0x01120000) &&
          shell.flags_34 == 3u && shell.animation_tick_4c == 0u &&
          shell.state_71 == 0u && shell.phase == PORTABLE_GP_GREEN_SHELL_ACTIVE &&
          owner.shell_item_18c == PORTABLE_GP_GREEN_SHELL_ITEM);

    CHECK(portable_gp_green_shell_tick_held_active(
              &owner,&shell,sine,1024u) == PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.animation_tick_4c == 1u && shell.display_index == 0u &&
          shell.animation_index_70 == 1u);
    for(unsigned tick=1u;tick<9u;++tick)
        CHECK(portable_gp_green_shell_tick_held_active(
                  &owner,&shell,sine,1024u) == PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.animation_tick_4c == 9u && shell.display_index == 1u &&
          shell.animation_index_70 == 2u);

    sine[0] = 0;
    sine[0x100] = INT32_C(0x00010000);
    sine[0x300] = -INT32_C(0x00010000);
    owner.control_96 = UINT16_C(0x0080);
    owner.speed_q16 = 0;
    owner.release_angle_offset = 0;
    CHECK(portable_gp_green_shell_release(
              &owner,&shell,sine,1024u) == PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.phase == PORTABLE_GP_GREEN_SHELL_RELEASED &&
          shell.position_x_q16 == owner.position_x_q16 &&
          shell.position_y_q16 == owner.position_y_q16 &&
          shell.position_z_q16 == 0 && shell.state_71 == test_green_shell_rules()->launch_speed_bonus/16u &&
          shell.velocity_x_q16 == 0 &&
          shell.velocity_y_q16 == -(int32_t)(test_green_shell_rules()->launch_speed_bonus/16u)*8192 &&
          shell.lifetime_38 == test_green_shell_rules()->collision_grace_ticks && shell.release_timer_74 == test_green_shell_rules()->wall_contact_budget &&
          owner.item_use_state_17c == 0u && owner.shell_item_18c == 0u &&
          (owner.flags_48 & UINT32_C(0x00010000)) != 0u);
    CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world) ==
          PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.position_y_q16 == owner.position_y_q16-
              (int32_t)(test_green_shell_rules()->launch_speed_bonus/16u)*8192 && shell.lifetime_38 == test_green_shell_rules()->collision_grace_ticks-1u &&
          shell.animation_tick_4c == 1u);
    for(unsigned tick=1u;tick<test_green_shell_rules()->release_lock_ticks;++tick)
        CHECK(portable_gp_green_shell_tick_released(&owner,&shell,&world) ==
              PORTABLE_GP_GREEN_SHELL_OK);
    CHECK((owner.flags_48 & UINT32_C(0x00010000)) == 0u &&
          shell.animation_tick_4c == test_green_shell_rules()->release_lock_ticks);

    /* 446FC retains held state for boxes, scenery, height and ghost guards;
     * impacts retire one slot, with item-only actor contact audio. */
    {
        const PortableGpGreenShell held={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_GREEN_SHELL_KIND,
            .phase=PORTABLE_GP_GREEN_SHELL_ACTIVE,.flags_34=3u};
        for(unsigned guard=0u;guard<5u;++guard){
            PortableGpGreenShell probe=held;
            PortableGpGreenShellOwner holder={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.item_use_state_17c=2u,
                .shell_item_18c=PORTABLE_GP_GREEN_SHELL_ITEM,.flags_48=0x10000u};
            if(guard==2u)probe.position_z_q16=0x42000;
            CHECK(portable_gp_green_shell_collide_held(&probe,&holder,
                guard==0u?0x8800u:guard==1u?0x1003u:0x2002u,
                guard==3u?0x42000:0,guard==4u?1:0)==PORTABLE_GP_GREEN_SHELL_OK);
            CHECK(probe.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE &&
                  probe.collision_result==PORTABLE_GP_GREEN_SHELL_COLLISION_IGNORED &&
                  holder.item_use_state_17c==2u && holder.flags_48==0x10000u &&
                  holder.shell_item_18c==PORTABLE_GP_GREEN_SHELL_ITEM);
        }
        for(unsigned item=0u;item<2u;++item){
            PortableGpGreenShell probe=held;
            PortableGpGreenShellOwner holder={.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.item_use_state_17c=2u,
                .shell_item_18c=PORTABLE_GP_GREEN_SHELL_ITEM,.flags_48=0x10000u};
            CHECK(portable_gp_green_shell_collide_held(&probe,&holder,
                item?0x8401u:0x2002u,0,0)==PORTABLE_GP_GREEN_SHELL_OK);
            CHECK(holder.item_use_state_17c==1u && holder.shell_item_18c==0u &&
                  holder.flags_48==0u && (probe.flags_34&2u)==0u &&
                  probe.vertical_velocity_q16==(int32_t)test_green_shell_rules()->destruction_velocity_q16 && probe.contact_audio_pending==item);
        }
    }

    memset(&shell,0,sizeof shell);shell.rules=owner.rules;shell.orbit_rules=owner.orbit_rules;
    shell.occupied = 1u;
    shell.kind_10 = PORTABLE_GP_GREEN_SHELL_KIND;
    shell.phase = PORTABLE_GP_GREEN_SHELL_ACTIVE;
    owner.control_96 = UINT16_C(0x0480);
    owner.speed_q16 = INT32_C(0x08000000);
    owner.item_use_state_17c = 1u;
    CHECK(portable_gp_green_shell_release(
              &owner,&shell,sine,1024u) == PORTABLE_GP_GREEN_SHELL_OK);
    CHECK(shell.angle_26 == UINT16_C(0x8000) && shell.state_71 == test_green_shell_rules()->max_speed &&
          shell.velocity_x_q16 == 0 &&
          shell.velocity_y_q16 == (int32_t)test_green_shell_rules()->max_speed*8192);

    /* sub_80445CC owns a green-specific same-owner shell filter, then uses
     * the common shell destruction callback and kart-contact song. */
    {
        PortableGpGreenShellAudioEvent event = {0};
        shell.position_z_q16 = 0;
        owner.flags_48 |= UINT32_C(0x00010000);
        CHECK(portable_gp_green_shell_collide(
                  &shell,&owner.flags_48,PORTABLE_GP_GREEN_SHELL_KIND,0,0,1u) ==
              PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(shell.phase == PORTABLE_GP_GREEN_SHELL_RELEASED &&
              shell.collision_result ==
                  PORTABLE_GP_GREEN_SHELL_COLLISION_IGNORED);
        CHECK(portable_gp_green_shell_collide(
                  &shell,&owner.flags_48,UINT16_C(0x8001),0,0,0u) ==
              PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(shell.phase == PORTABLE_GP_GREEN_SHELL_DESTROYING &&
              shell.collision_result ==
                  PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING_ITEM &&
              shell.destruction_field_5a == UINT16_C(0x8000) &&
              shell.vertical_velocity_q16 == (int32_t)test_green_shell_rules()->destruction_velocity_q16 &&
              (owner.flags_48 & UINT32_C(0x00010000)) == 0u);
        CHECK(portable_gp_green_shell_take_contact_audio(
                  &shell,0u,UINT16_C(0x0100),&event) ==
              PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(event.song_id == 124u && event.player == 4u &&
              event.volume == UINT16_C(0x0100));
        CHECK(portable_gp_green_shell_tick_destroying(&shell) ==
              PORTABLE_GP_GREEN_SHELL_OK);
        CHECK(shell.position_z_q16 == (int32_t)test_green_shell_rules()->destruction_velocity_q16 &&
              shell.vertical_velocity_q16 == (int32_t)(test_green_shell_rules()->destruction_velocity_q16-test_green_shell_rules()->destruction_gravity_q16));
    }

    memset(&shell,0,sizeof shell);shell.rules=owner.rules;shell.orbit_rules=owner.orbit_rules;
    inventory.held_item_17a = PORTABLE_GP_GREEN_SHELL_ITEM;
    owner.control_96 = 0u;
    CHECK(portable_gp_green_shell_use(&inventory,&owner,&shell,sine,1024u) ==
          PORTABLE_GP_GREEN_SHELL_INELIGIBLE);
    CHECK(inventory.held_item_17a == PORTABLE_GP_GREEN_SHELL_ITEM &&
          shell.occupied == 0u);
    owner.control_96 = UINT16_C(0x0080);
    owner.flags_48 = UINT32_C(0x00000800);
    CHECK(portable_gp_green_shell_use(&inventory,&owner,&shell,sine,1024u) ==
          PORTABLE_GP_GREEN_SHELL_INELIGIBLE);

    puts("GP green shell: PASS source use gate, 0x8401 spawn, attachment and animation");
    return 0;
}
