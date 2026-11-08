#include "item_rules_fixture.h"
#include "gp_red_shell.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c) do{if(!(c)){fprintf(stderr,"red check failed line %d: %s\n",__LINE__,#c);return __LINE__;}}while(0)

static int test_battle_forward_navigation(void)
{
    uint8_t cells[128u*128u]={0},floors[128u*128u]={0},flags[256];
    memset(flags,0x40,sizeof flags);flags[1]=0x22u;
    int32_t sine[1024]={0};sine[256]=32768;
    const int32_t offsets[8]={0};
    const PortableTrackRaster raster={.cells=floors,.width=128u,.height=128u};
    const PortableTerrainView terrain={cells,128u,128u,128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    PortableGpRedShellWorld world={&material,&raster,&terrain,&reflection,sine,1024u,0u,0x800u,0u};
    /* Battle ignores circular route rows and budget, even on a one-cell route. */
    const PortableGpRedShellRoute route={.checkpoint_count=1u};
    PortableGpRedShellTarget targets[4]={
        {.x_q16=128*65536,.y_q16=128*65536},
        {.x_q16=129*65536,.y_q16=128*65536},
        {.x_q16=192*65536+65535,.y_q16=192*65536+65535,.z_q16=65536,.guard_66=1},
        {.x_q16=193*65536,.y_q16=128*65536}};
    const PortableGpRedShell initial={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_RED_SHELL_KIND,
        .phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET,
        .position_x_q16=128*65536,.position_y_q16=128*65536,.speed_byte_71=100u,
        .route_budget_78=1u,.previous_floor_79=7u,.lifetime_38=8u};
    PortableGpRedShell shell=initial;uint32_t owner_flags=0x10000u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,targets,4u)==0);
    /* Last eligible, not nearest; fractional +/-64 still qualifies. Entry
     * has no target, so discovery retains untargeted speed/angle this turn. */
    CHECK(shell.target_valid && shell.target_racer_index==2u && shell.angle_26==0u &&
        shell.speed_byte_71==100u+test_red_shell_rules()->acceleration && shell.lifetime_38==7u && shell.route_budget_78==1u &&
        shell.previous_floor_79==7u);
    shell.steering_delay_7a=1u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,targets,4u)==0);
    CHECK(shell.angle_26==0x400u && shell.speed_byte_71==88u && shell.steering_delay_7a==0u &&
        shell.route_budget_78==1u);
    for (unsigned engine=0u;engine<3u;++engine) for (unsigned human=0u;human<2u;++human) {
        world.engine=(uint8_t)engine;shell=initial;
        shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;
        shell.target_valid=1u;shell.target_racer_index=1u;shell.route_budget_78=0u;
        targets[1].flags=human?0x8000u:0u;targets[1].x_q16=137*65536;
        CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,targets,4u)==0);
        CHECK(shell.angle_26==0x4000u && shell.route_budget_78==0u);
        for (unsigned tick=0u;tick<8u;++tick)
            CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,targets,4u)==0);
        CHECK(shell.speed_byte_71==(engine==0u?0x34u:engine==1u?0x3cu:0x46u));
    }
    world.engine=0u;shell=initial;
    targets[1].x_q16=targets[2].x_q16=targets[3].x_q16=300*65536;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,targets,4u)==0);
    CHECK(!shell.target_valid && shell.angle_26==0u);
    cells[16u*128u+16u]=1u;shell=initial;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,targets,4u)==0);
    CHECK(!shell.occupied && !(owner_flags&0x10000u));
    return 0;
}

static int test_spiny_lifecycle(void)
{
    uint8_t cells[128*128]={0},floors[128*128]={0},flags[256]={0};flags[0]=0x40u;flags[1]=0x22u;
    int32_t sine[1024]={0};const int32_t offsets[8]={0};const uint16_t lut[1]={0};
    const PortableTrackRaster raster={.cells=floors,.width=128u,.height=128u};
    const PortableTerrainView terrain={.cells=cells,.width=128u,.height=128u,.row_stride=128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    const PortableGpRedShellWorld world={&material,&raster,&terrain,&reflection,sine,1024u,1u,0u,0u};
    PortableRacePoint row1[8];for(unsigned i=0u;i<8u;++i)row1[i]=(PortableRacePoint){64,0};
    const PortableGpRedShellRoute route={row1,8u,NULL,lut,1u};
    PortableGpRedShellTarget targets[2]={{.x_q16=80*65536,.y_q16=64*65536,.rank=0u},
        {.x_q16=64*65536,.y_q16=80*65536,.rank=1u}};
    PortableGpItemInventory inventory={.held_item_17a=PORTABLE_GP_SPINY_SHELL_ITEM};
    PortableGpRedShellOwner owner={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.position_x_q16=64*65536,.position_y_q16=64*65536,
        .flags_48=0x2000u,.rank_flags_ba=2u};
    PortableGpRedShell shell={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==0);
    CHECK(shell.kind_10==PORTABLE_GP_SPINY_SHELL_KIND && shell.spiny_targeting);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==0 && owner.shell_item_18c==0x1004u);
    CHECK(portable_gp_red_shell_release_forward_target(&owner,&shell,0u,sine,1024u)==0);
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner.flags_48,&world,&route,targets,2u)==0);
    CHECK(shell.angle_26==0x400u && shell.target_racer_index==0u);
    targets[0].rank=1u;targets[1].rank=0u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner.flags_48,&world,&route,targets,2u)==0);
    CHECK(shell.target_racer_index==1u && shell.angle_26==0x800u);
    /* First-place launch has no target, but still follows row1 and expires
     * after48 floor changes. Red's null-target branch travels straight. */
    shell.target_valid=0u;shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET;
    shell.angle_26=0x1000u;shell.route_budget_78=2u;shell.previous_floor_79=7u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner.flags_48,&world,&route,targets,2u)==0);
    CHECK(shell.angle_26==0x0c00u && shell.route_budget_78==1u);
    shell.animation_tick_4c=15u;
    const int32_t sound_x=shell.position_x_q16,sound_y=shell.position_y_q16;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner.flags_48,&world,&route,targets,2u)==0);
    CHECK(shell.flight_audio_pending && shell.flight_audio_x_q16==sound_x &&
        shell.flight_audio_y_q16==sound_y && shell.flight_audio_heading==shell.angle_26 &&
        shell.flight_audio_speed==shell.speed_byte_71);
    shell.previous_floor_79=7u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner.flags_48,&world,&route,targets,2u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE);
    shell=(PortableGpRedShell){.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_SPINY_SHELL_KIND,.spiny_targeting=1u,
        .phase=PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD,.position_x_q16=64*65536,.position_y_q16=64*65536,
        .collision_active=1u,.release_timer_74=1u,.lifetime_38=8u};
    for(unsigned i=0u;i<test_red_shell_rules()->trap_arm_ticks;++i)CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,2u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED);
    targets[1].x_q16=160*65536;targets[1].y_q16=64*65536;
    CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,2u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_LOCKING && shell.target_racer_index==1u && shell.trap_audio_pending==1u);
    for(unsigned i=0u;i<test_red_shell_rules()->trap_lock_ticks;++i)CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,2u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_CHASING && shell.speed_byte_71==0x48u);
    targets[0].rank=0u;targets[1].rank=1u;
    CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,2u)==0 && shell.target_racer_index==0u);
    shell.phase=PORTABLE_GP_RED_SHELL_TRAP_ARMED;shell.animation_tick_4c=(uint16_t)(test_red_shell_rules()->trap_lifetime_ticks-1u);targets[0].guard_66=1;
    CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,2u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_LOCKING); /* Timeout pursues even a guarded leader. */
    shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;
    CHECK(portable_gp_spiny_shell_collide(&shell,&owner.flags_48,0x2002u,0,0,3u,0)==0);
    CHECK(shell.collision_active && shell.collision_result==PORTABLE_GP_RED_SHELL_COLLISION_IGNORED);
    CHECK(portable_gp_spiny_shell_collide(&shell,&owner.flags_48,0x8401u,0,0,0u,0)==0 && shell.collision_active && shell.contact_audio_pending);
    CHECK(portable_gp_spiny_shell_collide(&shell,&owner.flags_48,0x2002u,0,0,0u,0)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE);
    shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;shell.collision_active=1u;
    CHECK(portable_gp_spiny_shell_collide(&shell,&owner.flags_48,0x2002u,0,1,0u,0)==0);
    CHECK(!shell.spiny_targeting && !shell.target_valid && shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET);
    shell.spiny_targeting=1u;shell.phase=PORTABLE_GP_RED_SHELL_TRAP_ARMED;
    cells[8u*128u+8u]=1u;
    CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,2u)==0 && !shell.occupied);
    return 0;
}

static int test_forward_course_navigation(void)
{
    uint8_t cells[128*128]={0},floors[128*128]={0},flags[256]={0};
    flags[0]=0x40u;flags[1]=0x80u;flags[2]=0x22u;
    int32_t sine[1024]={0};
    const int32_t offsets[8]={0};const uint16_t lut[1]={0};
    const PortableTrackRaster raster={.cells=floors,.width=128u,.height=128u};
    const PortableTerrainView terrain={.cells=cells,.width=128u,.height=128u,.row_stride=128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    const PortableGpRedShellWorld world={&material,&raster,&terrain,&reflection,sine,1024u,1u,0u,0u};
    PortableRacePoint row1[8];for(unsigned i=0u;i<8u;++i)row1[i]=(PortableRacePoint){64,0};
    const PortableGpRedShellRoute route={row1,8u,NULL,lut,1u};
    PortableGpRedShellTarget target={.x_q16=128*65536,.y_q16=64*65536,.checkpoint=4u};
    const PortableGpRedShell initial={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_RED_SHELL_KIND,
        .phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET,.target_valid=1u,
        .target_racer_index=0u,.position_x_q16=64*65536,.position_y_q16=64*65536,
        .angle_26=0x1000u,.previous_floor_79=7u,.route_budget_78=48u,
        .speed_byte_71=48u,.steering_delay_7a=9u,.lifetime_38=8u,.release_timer_74=1u,
        .collision_active=1u};
    PortableGpRedShell shell=initial;uint32_t owner_flags=0x10000u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.angle_26==0x0c00u && shell.steering_delay_7a==9u &&
        shell.route_budget_78==47u && shell.speed_byte_71==initial.speed_byte_71+test_red_shell_rules()->acceleration && shell.lifetime_38==7u);
    /* The current, next two and previous checkpoints permit direct pursuit,
     * including wrap; other targets follow row1 rather than cutting corners. */
    const uint8_t near[]={0u,1u,2u,7u};
    for(unsigned i=0u;i<4u;++i){
        shell=initial;target.checkpoint=near[i];
        CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
        CHECK(shell.angle_26==0x1400u && shell.steering_delay_7a==8u);
    }
    shell=initial;target.checkpoint=1u;target.x_q16=74*65536;
    target.z_q16=65536;shell.steering_delay_7a=0u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.angle_26==0x1400u);
    shell=initial;shell.steering_delay_7a=0u;target.z_q16=0;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.angle_26==0x4000u);
    shell=initial;shell.route_budget_78=1u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE && !shell.collision_active &&
        shell.position_x_q16==initial.position_x_q16 && shell.lifetime_38==7u);
    shell=initial;shell.target_valid=0u;shell.animation_tick_4c=(uint16_t)(test_red_shell_rules()->release_lock_ticks-1u);
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET && !(owner_flags&0x10000u));
    shell=initial;cells[8u*128u+8u]=1u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING && shell.animation_tick_4c==1u);
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE && shell.vertical_velocity_q16==(int32_t)test_red_shell_rules()->destruction_velocity_q16);
    shell=initial;cells[8u*128u+8u]=2u;owner_flags=0x10000u;
    CHECK(portable_gp_red_shell_tick_forward(&shell,&owner_flags,&world,&route,&target,1u)==0);
    CHECK(!shell.occupied && shell.phase==PORTABLE_GP_RED_SHELL_DESTROYED && !(owner_flags&0x10000u));
    return 0;
}

static int test_cpu_held_owner(void)
{
    int32_t sine[1024]={0};sine[256]=65536;sine[768]=-65536;
    PortableGpItemInventory inventory={.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM};
    PortableGpRedShellOwner owner={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.flags_48=0x2000u,.decision_190=3u};
    PortableGpRedShell shell={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==0);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==0);
    owner.mode_78=2u;owner.response_status_7e=5u;owner.target_heading_56=0u;
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==0);
    CHECK(shell.angle_26==0x8000u && !(shell.flags_34&3u));
    owner.mode_78=0u;owner.response_status_7e=0u;shell.animation_tick_4c=(uint16_t)(test_red_shell_rules()->cpu_hold_ticks-2u);
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==0);
    CHECK(owner.decision_190==3u && (shell.flags_34&3u)==3u);
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==0);
    CHECK(owner.decision_190==2u);
    CHECK(portable_gp_red_shell_release_backward(&owner,&shell,sine,1024u)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD && shell.motion_audio_pending==134u);
    return 0;
}

static int test_custom_rules(void)
{
    PortableRedShellRules rules=*test_red_shell_rules();
    rules.launch_speed_bonus=384u;rules.max_launch_speed=60u;rules.collision_grace_ticks=4u;
    rules.route_budget=6u;rules.release_lock_ticks=8u;rules.cpu_hold_ticks=60u;
    rules.flight_speed_50=80u;rules.flight_speed_100=90u;rules.flight_speed_150=100u;
    rules.acceleration=8u;rules.deceleration=24u;rules.trap_arm_ticks=24u;
    rules.trap_lifetime_ticks=180u;rules.trap_lock_ticks=10u;
    rules.destruction_velocity_q16=98304u;rules.destruction_gravity_q16=12288u;
    CHECK(portable_red_shell_rules_valid(&rules));
    int32_t sine[1024]={0};sine[256]=65536;
    PortableGpRedShellOwner owner={.orbit_rules=test_shell_orbit_rules(),.rules=&rules,.control_96=0x80u,.speed_q16=1000*65536};
    PortableGpItemInventory inventory={.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM};
    PortableGpRedShell shell={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==0);
    CHECK(shell.rules==&rules);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==0);
    CHECK(portable_gp_red_shell_release_forward_leader(&owner,&shell,sine,1024u)==0);
    CHECK(shell.speed_byte_71==60u&&shell.velocity_y_q16==-60*8192&&shell.lifetime_38==4u&&shell.route_budget_78==6u);
    CHECK(portable_gp_red_shell_tick_forward_no_target_clear(&shell,0u,sine,1024u)==0);
    CHECK(shell.speed_byte_71==68u&&shell.lifetime_38==3u);
    for(unsigned engine=0u;engine<3u;++engine){
        PortableGpRedShell probe=shell;probe.speed_byte_71=200u;
        CHECK(portable_gp_red_shell_tick_forward_no_target_clear(&probe,(uint8_t)engine,sine,1024u)==0);
        CHECK(probe.speed_byte_71==176u);
        for(unsigned i=0u;i<20u;++i)CHECK(portable_gp_red_shell_tick_forward_no_target_clear(&probe,(uint8_t)engine,sine,1024u)==0);
        CHECK(probe.speed_byte_71==80u+engine*10u);
    }
    shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;shell.target_valid=1u;
    for(unsigned i=1u;i<=6u;++i)CHECK(portable_gp_red_shell_tick_target_route_floor(&shell,(uint8_t)i)==0);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE&&shell.vertical_velocity_q16==98304);
    CHECK(portable_gp_red_shell_tick_destroying(&shell)==0&&shell.vertical_velocity_q16==86016);
    owner.item_use_state_17c=0u;owner.flags_48=0x2000u;owner.decision_190=1u;
    inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;shell=(PortableGpRedShell){.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==0);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==0);
    shell.animation_tick_4c=58u;
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==0&&owner.decision_190==1u);
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==0&&owner.decision_190==0u);
    owner.item_use_state_17c=0u;inventory.held_item_17a=PORTABLE_GP_TRIPLE_RED_SHELL_ITEM;
    PortableGpRedShell triple[3];const uint8_t delays[3]={0,1,2};
    CHECK(portable_gp_red_shell_use_triple(&inventory,&owner,triple,delays,sine,1024u)==0);
    for(unsigned i=0u;i<3u;++i)CHECK(triple[i].rules==&rules);
    uint8_t cells[128u*128u]={0},floors[128u*128u]={0},flags[256]={0};flags[0]=0x40u;
    const int32_t offsets[8]={0};
    const PortableTrackRaster raster={.cells=floors,.width=128u,.height=128u};
    const PortableTerrainView terrain={cells,128u,128u,128u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    const PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
    const PortableGpRedShellWorld world={&material,&raster,&terrain,&reflection,sine,1024u,0u,0u,0u};
    const PortableGpRedShellTarget target={.x_q16=200*65536,.y_q16=200*65536,.guard_66=1,.rank=0u};
    for(unsigned spiny=0u;spiny<2u;++spiny){
        shell=(PortableGpRedShell){.orbit_rules=test_shell_orbit_rules(),.rules=&rules,.occupied=1u,.kind_10=spiny?PORTABLE_GP_SPINY_SHELL_KIND:PORTABLE_GP_RED_SHELL_KIND,
            .phase=PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD,.spiny_targeting=(uint8_t)spiny,.position_x_q16=64*65536,.position_y_q16=64*65536};
        uint32_t owner_flags=0x10000u;
        for(unsigned i=0u;i<24u;++i){
            CHECK(portable_gp_red_shell_tick_trap(&shell,&owner_flags,&world,&target,1u)==0);
            if(i<7u)CHECK(owner_flags&0x10000u);else CHECK(!(owner_flags&0x10000u));
        }
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED);
        for(unsigned i=0u;i<180u;++i)CHECK(portable_gp_red_shell_tick_trap(&shell,&owner_flags,&world,&target,1u)==0);
        CHECK(shell.phase==(spiny?PORTABLE_GP_RED_SHELL_TRAP_LOCKING:PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE));
        if(spiny){
            for(unsigned i=0u;i<10u;++i)CHECK(portable_gp_red_shell_tick_trap(&shell,&owner_flags,&world,&target,1u)==0);
            CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_CHASING);
        }
    }
    rules.release_lock_ticks=25u;CHECK(!portable_red_shell_rules_valid(&rules));
    return 0;
}

static int test_custom_orbit(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[12]=1234;sine[244]=2000;sine[1012]=-1234;
    PortableShellOrbitRules rules={22u,768u,3u};
    const uint8_t delays[3]={2u,4u,6u};
    PortableGpRedShellOwner owner={.rules=test_red_shell_rules(),.control_96=0x80u,.position_x_q16=100*65536,
        .position_y_q16=100*65536,.position_z_q16=65536,.floor_36=9u,.orbit_rules=&rules};
    PortableGpItemInventory inventory={.held_item_17a=4099u};
    PortableGpRedShell shells[3];
    CHECK(portable_gp_red_shell_use_triple(&inventory,&owner,shells,delays,sine,1024u)==0);
    owner.control_96=0u;
    CHECK(shells[0].orbit_rules==&rules && shells[0].position_y_q16==89*65536);
    for(unsigned i=0u;i<3u;++i){
        CHECK(shells[i].release_timer_74==delays[i]);
        for(unsigned t=0u;t<delays[i];++t){
            CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[i],sine,1024u)==0);
            CHECK(shells[i].phase==PORTABLE_GP_RED_SHELL_ORBIT_PENDING);
        }
        CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[i],sine,1024u)==0);
        CHECK(shells[i].phase==PORTABLE_GP_RED_SHELL_ORBITING);
    }
    CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].animation_index_70==1u && shells[0].display_index==0u);
    CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].angle_26==768u && shells[0].position_x_q16==100*65536+22*1234 &&
        shells[0].position_y_q16==100*65536-22*2000 && shells[0].animation_index_70==1u);
    for(unsigned t=0u;t<2u;++t)CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].animation_index_70==2u && shells[0].display_index==1u);
    shells[0].animation_tick_4c=65535u;
    CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0);
    CHECK(shells[0].angle_26==0xfd00u && shells[0].animation_tick_4c==0u &&
        shells[0].position_x_q16==100*65536-22*1234);
    owner.mode_78=2u;
    CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[0],sine,1024u)==0 && !(shells[0].flags_34&3u));
    rules.animation_period=0u;
    CHECK(portable_gp_red_shell_tick_orbit(&owner,&shells[0],sine,1024u)==PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    CHECK(test_custom_rules()==0);
    CHECK(test_custom_orbit()==0);
    CHECK(test_battle_forward_navigation()==0);
    CHECK(test_spiny_lifecycle()==0);
    CHECK(test_forward_course_navigation()==0);
    CHECK(test_cpu_held_owner()==0);
    int32_t sine[1024]={0};
    PortableGpItemInventory inventory={0};
    PortableGpRedShellOwner owner={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    PortableGpRedShell shell={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),};
    sine[0]=INT32_C(0x10000);sine[0x100]=INT32_C(0x8000);
    sine[0x10]=INT32_C(0x10000);sine[0xf0]=INT32_C(0x8000);
    sine[0x300]=-INT32_C(0x10000);
    inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;
    owner.position_x_q16=INT32_C(0x1000000);
    owner.position_y_q16=INT32_C(0x2000000);
    owner.position_z_q16=INT32_C(0x30000);
    owner.control_96=UINT16_C(0x00c0);
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.kind_10==UINT16_C(0x8402)&&
          shell.position_x_q16==INT32_C(0x1100000)&&
          shell.position_y_q16==INT32_C(0x1f80000)&&
          owner.item_use_state_17c==1u&&inventory.held_item_17a==0u);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE&&
          owner.shell_item_18c==UINT16_C(0x1002)&&shell.flags_34==3u);
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.animation_tick_4c==1u&&shell.display_index==0u&&
          shell.animation_index_70==1u);
    owner.control_96&=UINT16_C(0xffbf);
    CHECK(portable_gp_red_shell_tick_held_active(&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.animation_tick_4c==2u);
    owner.control_96|=UINT16_C(0x0400);
    CHECK(portable_gp_red_shell_release_backward(
              &owner,&shell,sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD&&
          shell.angle_26==UINT16_C(0x8000)&&shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks&&
          shell.release_timer_74==1u&&shell.velocity_x_q16==0&&
          shell.velocity_y_q16==0&&shell.position_x_q16==0x01000000&&
          shell.position_y_q16==0x02100000&&owner.shell_item_18c==0u&&
          owner.item_use_state_17c==0u&&(owner.flags_48&UINT32_C(0x10000))!=0u);
    /* 446FC retains held state for boxes, scenery, height and ghost guards;
     * impacts retire one slot, with item-only actor contact audio. */
    {
        const PortableGpRedShell held={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.occupied=1u,.kind_10=PORTABLE_GP_RED_SHELL_KIND,
            .phase=PORTABLE_GP_RED_SHELL_ACTIVE,.flags_34=3u};
        for(unsigned guard=0u;guard<5u;++guard){
            PortableGpRedShell probe=held;
            PortableGpRedShellOwner holder={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.item_use_state_17c=2u,
                .shell_item_18c=PORTABLE_GP_RED_SHELL_ITEM,.flags_48=0x10000u};
            if(guard==2u)probe.position_z_q16=0x42000;
            CHECK(portable_gp_red_shell_collide_held(&probe,&holder,
                guard==0u?0x8800u:guard==1u?0x1003u:0x2002u,
                guard==3u?0x42000:0,guard==4u?1:0)==PORTABLE_GP_RED_SHELL_OK);
            CHECK(probe.phase==PORTABLE_GP_RED_SHELL_ACTIVE &&
                  probe.collision_result==PORTABLE_GP_RED_SHELL_COLLISION_IGNORED &&
                  holder.item_use_state_17c==2u && holder.flags_48==0x10000u &&
                  holder.shell_item_18c==PORTABLE_GP_RED_SHELL_ITEM);
        }
        for(unsigned item=0u;item<2u;++item){
            PortableGpRedShell probe=held;
            PortableGpRedShellOwner holder={.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),.item_use_state_17c=2u,
                .shell_item_18c=PORTABLE_GP_RED_SHELL_ITEM,.flags_48=0x10000u};
            CHECK(portable_gp_red_shell_collide_held(&probe,&holder,
                item?0x8401u:0x2002u,0,0)==PORTABLE_GP_RED_SHELL_OK);
            CHECK(holder.item_use_state_17c==1u && holder.shell_item_18c==0u &&
                  holder.flags_48==0u && (probe.flags_34&2u)==0u &&
                  probe.vertical_velocity_q16==(int32_t)test_red_shell_rules()->destruction_velocity_q16 && probe.contact_audio_pending==item);
        }
    }

    /* Full dropped-trap lifecycle: stationary arming, list-order selection,
     * 30-tick lock, engine-specific launch and eventual idle disposal. */
    {
        uint8_t terrain_cells[128u*128u]={0}, raster_cells[64u*64u]={0};
        uint8_t flags[256]={0};
        PortableTerrainView terrain={terrain_cells,128u,128u,128u};
        PortableTrackRaster raster={raster_cells,sizeof raster_cells,64u,64u,0u,0u};
        PortableTrackMaterialView material={.flags=flags,.count=256u};
        int32_t offsets[8]={0};
        PortableTerrainEffectTableView reflection={offsets,8u,offsets,8u};
        PortableGpRedShellWorld world={&material,&raster,&terrain,&reflection,sine,1024u,2u,0u,0u};
        PortableGpRedShellTarget targets[3]={
            {.x_q16=shell.position_x_q16+32*65536,.y_q16=shell.position_y_q16},
            {.x_q16=shell.position_x_q16,.y_q16=shell.position_y_q16,.guard_66=1},
            {.x_q16=shell.position_x_q16+20*65536,.y_q16=shell.position_y_q16}};
        const int32_t x=shell.position_x_q16,y=shell.position_y_q16;
        for(unsigned tick=1u;tick<=test_red_shell_rules()->trap_arm_ticks;++tick){
            CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
            CHECK(shell.position_x_q16==x&&shell.position_y_q16==y);
            if(tick+1u==test_red_shell_rules()->release_lock_ticks)CHECK((owner.flags_48&0x10000u)!=0u);
            if(tick==test_red_shell_rules()->release_lock_ticks)CHECK((owner.flags_48&0x10000u)==0u);
        }
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED&&shell.lifetime_38==0u);
        CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_LOCKING&&
              shell.target_racer_index==2u&&shell.trap_audio_pending==2u);
        for(unsigned tick=0u;tick<test_red_shell_rules()->trap_lock_ticks;++tick)
            CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TRAP_CHASING&&
              shell.speed_byte_71==0x52u&&shell.angle_26==0x4000u&&
              shell.position_x_q16==x);
        sine[0x100]=65536;sine[0]=0;
        CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
        CHECK(shell.position_x_q16>x&&shell.position_y_q16==y);
        /* A wall schedules 7884, with 7848 beginning on the following turn. */
        flags[0]=0x80u;
        CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING&&
              shell.contact_audio_pending==1u&&shell.position_z_q16==0);
        CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE&&
              shell.position_z_q16==0&&!shell.collision_active);
        flags[0]=0u;shell.phase=PORTABLE_GP_RED_SHELL_TRAP_ARMED;
        shell.collision_active=1u;shell.animation_tick_4c=(uint16_t)(test_red_shell_rules()->trap_lifetime_ticks-1u);
        CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE);
        /* Hazard disposal and outside bounds clear the owner and free atomically. */
        for(unsigned outside=0u;outside<2u;++outside){
            shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD;
            shell.occupied=1u;shell.collision_active=1u;
            shell.position_x_q16=outside?-1:x;flags[0]=outside?0u:0x20u;
            owner.flags_48|=0x10000u;
            CHECK(portable_gp_red_shell_tick_trap(&shell,&owner.flags_48,&world,targets,3u)==0);
            CHECK(!shell.occupied&&!shell.collision_active&&(owner.flags_48&0x10000u)==0u);
        }
        sine[0]=65536;sine[0x100]=32768;
    }
    memset(&shell,0,sizeof shell);shell.rules=owner.rules;shell.orbit_rules=owner.orbit_rules;owner.flags_48&=UINT32_C(0xfffeffff);
    owner.control_96=UINT16_C(0x00c0);owner.speed_q16=0;
    owner.item_use_state_17c=0u;inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    owner.control_96&=UINT16_C(0xffbf);
    CHECK(portable_gp_red_shell_release_forward_leader(
              &owner,&shell,sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET&&
          shell.angle_26==0u&&shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u&&
          shell.velocity_x_q16==(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u)*8192&&
          shell.velocity_y_q16==-(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u)*4096&&
          shell.target_valid==0u&&shell.target_racer_index==UINT8_MAX && shell.motion_audio_pending);
    {
        PortableGpRedShell probe=shell;probe.phase=PORTABLE_GP_RED_SHELL_ACTIVE;
        PortableGpRedShellOwner reverse=owner;reverse.speed_q16=-65536;
        reverse.item_use_state_17c=1u;
        CHECK(portable_gp_red_shell_release_forward_leader(&reverse,&probe,sine,1024u)==0);
        CHECK(probe.speed_byte_71==(test_red_shell_rules()->launch_speed_bonus-1u)/16u && probe.motion_audio_pending);
        probe.phase=PORTABLE_GP_RED_SHELL_ACTIVE;reverse.speed_q16=0x10000000;
        CHECK(portable_gp_red_shell_release_forward_leader(&reverse,&probe,sine,1024u)==0);
        CHECK(probe.speed_byte_71==(uint8_t)(test_red_shell_rules()->launch_speed_bonus/16u));
    }
    CHECK(portable_gp_red_shell_tick_forward_no_target_clear(
              &shell,1u,sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration&&shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks-1u&&
          shell.flags_34==3u&&shell.animation_tick_4c==1u&&
          shell.animation_index_70==1u&&shell.display_index==0u&&
          shell.velocity_x_q16==(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration)*8192&&
          shell.velocity_y_q16==-(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration)*4096&&
          shell.position_x_q16==owner.position_x_q16+shell.velocity_x_q16&&
          shell.position_y_q16==owner.position_y_q16+shell.velocity_y_q16);
    memset(&shell,0,sizeof shell);shell.rules=owner.rules;shell.orbit_rules=owner.orbit_rules;owner.flags_48&=UINT32_C(0xfffeffff);
    owner.control_96=UINT16_C(0x00c0);owner.rank_flags_ba=14u;
    inventory.held_item_17a=PORTABLE_GP_RED_SHELL_ITEM;
    CHECK(portable_gp_red_shell_use(&inventory,&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(portable_gp_red_shell_tick_attached(&owner,&shell,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    owner.control_96&=UINT16_C(0xffbf);
    CHECK(portable_gp_red_shell_release_forward_target(
              &owner,&shell,1u,sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.target_valid==1u&&shell.target_racer_index==1u&&
          shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u&&shell.route_budget_78==test_red_shell_rules()->route_budget);
    CHECK(portable_gp_red_shell_target_angle(20,0)==UINT16_C(0x4000));
    CHECK(portable_gp_red_shell_target_angle(0,20)==UINT16_C(0x8000));
    CHECK(portable_gp_red_shell_target_angle(20,20)==UINT16_C(0x6000));
    /* Independently paired original sub_802EC38 call, intro frame 1173.
     * The previous truncating polynomial return produced 16321. */
    CHECK(portable_gp_red_shell_target_angle(815,-5)==UINT16_C(16320));
    CHECK(portable_gp_red_shell_tick_forward_target_far_clear(
              &shell,(int32_t)((uint32_t)shell.position_x_q16+
                               UINT32_C(0x00140000)),
              shell.position_y_q16,1u,sine,1024u)==PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.angle_26==UINT16_C(0x0400)&&shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration&&
          shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks-1u&&shell.animation_tick_4c==1u&&
          shell.velocity_x_q16==(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration)*8192&&
          shell.velocity_y_q16==-(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+test_red_shell_rules()->acceleration)*4096);
    CHECK(portable_gp_red_shell_tick_forward_target_close_clear(
              &shell,(int32_t)((uint32_t)shell.position_x_q16+
                               UINT32_C(0x000a0000)),
              shell.position_y_q16,0,0u,1u,sine,1024u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.angle_26==UINT16_C(0x4000)&&shell.speed_byte_71==test_red_shell_rules()->launch_speed_bonus/16u+2u*test_red_shell_rules()->acceleration&&
          shell.lifetime_38==test_red_shell_rules()->collision_grace_ticks-2u&&shell.animation_tick_4c==2u&&
          shell.velocity_x_q16==(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+2u*test_red_shell_rules()->acceleration)*4096&&
          shell.velocity_y_q16==-(int32_t)(test_red_shell_rules()->launch_speed_bonus/16u+2u*test_red_shell_rules()->acceleration)*8192);
    {
        PortableGpRedShell probe=shell;uint32_t owner_flags=UINT32_C(0x10000);
        CHECK(portable_gp_red_shell_collide(
                  &probe,&owner_flags,UINT16_C(0x2001),0,1)==
              PORTABLE_GP_RED_SHELL_OK);
        CHECK(probe.collision_result==
                  PORTABLE_GP_RED_SHELL_COLLISION_TARGET_CLEARED&&
              !probe.target_valid&&probe.collision_active);
        probe=shell;owner_flags=UINT32_C(0x10000);
        CHECK(portable_gp_red_shell_collide(
                  &probe,&owner_flags,UINT16_C(0x8001),0,0)==
              PORTABLE_GP_RED_SHELL_OK);
        CHECK(probe.collision_result==
                  PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING_ITEM&&
              probe.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE&&
              !probe.collision_active&&(owner_flags&UINT32_C(0x10000))==0u);
        {
            PortableGpRedShellAudioEvent event={0};
            CHECK(portable_gp_red_shell_take_contact_audio(
                      &probe,0u,0x40u,&event)==PORTABLE_GP_RED_SHELL_OK);
            CHECK(event.song_id==124u&&event.player==4u&&event.volume==0x40u&&
                  !probe.contact_audio_pending);
            CHECK(portable_gp_red_shell_take_contact_audio(
                      &probe,0u,0x40u,&event)==
                  PORTABLE_GP_RED_SHELL_INELIGIBLE);
        }
    }
    CHECK(shell.collision_active==1u&&shell.route_budget_78==test_red_shell_rules()->route_budget);
    CHECK(portable_gp_red_shell_tick_target_route_floor(&shell,0u)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.route_budget_78==test_red_shell_rules()->route_budget);
    for(uint8_t floor=1u;floor<=test_red_shell_rules()->route_budget;++floor)
        CHECK(portable_gp_red_shell_tick_target_route_floor(&shell,floor)==
              PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE&&
          shell.route_budget_78==0u&&
          shell.destruction_field_5a==UINT16_C(0x8000)&&
          shell.vertical_velocity_q16==(int32_t)test_red_shell_rules()->destruction_velocity_q16&&
          (shell.flags_34&2u)==0u&&shell.collision_active==0u);
    CHECK(portable_gp_red_shell_tick_destroying(&shell)==
          PORTABLE_GP_RED_SHELL_OK);
    CHECK(shell.position_z_q16==(int32_t)test_red_shell_rules()->destruction_velocity_q16&&
          shell.vertical_velocity_q16==(int32_t)(test_red_shell_rules()->destruction_velocity_q16-test_red_shell_rules()->destruction_gravity_q16));
    unsigned destruction_ticks=1u;
    while((int64_t)destruction_ticks*test_red_shell_rules()->destruction_velocity_q16-
        (int64_t)destruction_ticks*(destruction_ticks-1u)*test_red_shell_rules()->destruction_gravity_q16/2>=0)
        ++destruction_ticks;
    for(unsigned tick=1u;tick<destruction_ticks;++tick) {
        CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE);
        CHECK(portable_gp_red_shell_tick_destroying(&shell)==PORTABLE_GP_RED_SHELL_OK);
        if(tick+1u<destruction_ticks)
            CHECK(shell.position_z_q16==(int64_t)(tick+1u)*test_red_shell_rules()->destruction_velocity_q16-
                (int64_t)(tick+1u)*tick*test_red_shell_rules()->destruction_gravity_q16/2);
    }
    CHECK(shell.phase==PORTABLE_GP_RED_SHELL_DESTROYED&&!shell.occupied&&
          shell.position_z_q16==0&&shell.vertical_velocity_q16==0);
    puts("GP red shell tests passed");
    return 0;
}
