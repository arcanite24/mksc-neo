#include "item_rules_fixture.h"
#include "test_support.h"

static int driver_pilot_failure(int line)
{
    fprintf(stderr,"driver pilot failed line %d\n",line);
    return 1;
}

static const PortableModernDriftRules pilot_modern_rules={
    .profile_id=71u,.revision=1u,.charge_ticks={2u,3u,4u},
    .boost_ticks={2u,3u,4u},.cap_ratio_256={256u,272u,288u},
    .minimum_speed_ratio_256=128u,.acceleration_q16={65536,2*65536,3*65536}
};

static PortableDriverResponseRules captured_response_profile(void)
{
    PortableDriverResponseRules rules=*portable_test_response_profile();
    /* Individual coefficients needed by the source-call cases below. Rows
     * without captured numeric assertions retain the authored test profile. */
    const int16_t zero[8]={-16,224,2048,128,160,192,224,2048};
    const int16_t turn[8]={-32,224,6144,64,96,128,272,7424};
    const int16_t charged[8]={-256,256,10240,192,288,256,336,7424};
    const int16_t sharp[8]={-768,480,10240,576,864,1152,640,12544};
    const int16_t rebound[8]={-512,208,8192,352,480,640,448,9472};
    memcpy(rules.rows[0],zero,sizeof zero);memcpy(rules.rows[2],turn,sizeof turn);
    memcpy(rules.rows[4],charged,sizeof charged);memcpy(rules.rows[6],sharp,sizeof sharp);
    memcpy(rules.rows[7],rebound,sizeof rebound);
    rules.selector_rows[13]=4;rules.selector_rows[12]=rules.selector_rows[14]=0;
    rules.character_offsets[3]=-1;
    return rules;
}

static int test_driver_art_motion(void)
{
    PortableDriverArtRules rules={0};
    /* Deliberately synthetic speeds and refresh assignments. */
    for(unsigned i=0;i<16u;++i){
        rules.speed_spans[i]=(uint16_t)(3u+i%4u);
        rules.refresh_masks[i]=(uint8_t)(i&1u?3u:7u);
    }
    PortableDriverArtMotion art={0};
    const PortableDriverArtMotion absent=art;
    if(portable_driver_art_motion_step(&art,NULL,0,0,0,0,0,1,0,0,1,0,0,0) ||
       memcmp(&art,&absent,sizeof art))return driver_pilot_failure(__LINE__);
    /* Stopping settles phase2 once; bobbing reads the preceding phase. */
    if(!portable_driver_art_motion_step(&art,&rules,0,0,0,0,0,2,0,0,1,0,0,0) ||
       art.cached_bob_offset || art.wheel_phase || art.bob_phase!=1u ||
       art.countdown!=1u || art.wheel_palette_bank!=15u ||
       !art.wheel_palette_valid)return driver_pilot_failure(__LINE__);
    portable_driver_art_motion_step(&art,&rules,0,0,0,0,0,3,0,0,1,0,0,0);
    if(art.cached_bob_offset!=2 || art.countdown || art.bob_phase!=1u)return driver_pilot_failure(__LINE__);
    portable_driver_art_motion_step(&art,&rules,0,0,0,0,0,4,0,0,1,0,0,0);
    if(art.cached_bob_offset!=1 || art.wheel_phase || art.bob_phase!=1u)return driver_pilot_failure(__LINE__);
    /* A changed speed clamps the remaining span, but never advances on
     * the turn that decrements the countdown to zero. */
    art.countdown=20u;
    portable_driver_art_motion_tick_palette(&art,&rules,2*65536,0x800u);
    if(art.countdown!=5u || art.wheel_phase || art.wheel_palette_bank!=15u)return driver_pilot_failure(__LINE__);
    for(unsigned i=0;i<5u;++i)
        portable_driver_art_motion_tick_palette(&art,&rules,2*65536,0x800u);
    if(art.countdown || art.wheel_phase)return driver_pilot_failure(__LINE__);
    portable_driver_art_motion_tick_palette(&art,&rules,2*65536,0x800u);
    if(art.countdown!=5u || art.wheel_phase!=1u || art.bob_phase!=2u ||
       art.wheel_palette_bank!=14u)return driver_pilot_failure(__LINE__);
    portable_driver_art_motion_tick_palette(&art,&rules,2*65536,0);
    if(art.wheel_palette_bank!=14u)return driver_pilot_failure(__LINE__);
    /* Unsigned magnitude preserves INT32_MIN; negative and positive speeds
     * select the same span, with the whole-unit magnitude clamped to15. */
    PortableDriverArtMotion reverse={0},forward={0};
    portable_driver_art_motion_tick_palette(&reverse,&rules,INT32_MIN,0);
    portable_driver_art_motion_tick_palette(&forward,&rules,INT32_MAX,0);
    if(memcmp(&reverse,&forward,sizeof reverse) || reverse.countdown!=6u)return driver_pilot_failure(__LINE__);
    /* CPU refresh follows character ID, while its wheel owner keeps ticking. */
    art=(PortableDriverArtMotion){.initialized=1u,.wheel_phase=0u,
        .bob_phase=3u,.cached_bob_offset=7};
    portable_driver_art_motion_step(&art,&rules,65536,0,0,0x5c,0,
        2,5,0,0,0,0,0);
    if(art.cached_bob_offset!=7 || art.wheel_phase!=1u || art.bob_phase!=4u)return driver_pilot_failure(__LINE__);
    portable_driver_art_motion_step(&art,&rules,65536,0,0,0x5c,0,
        5,5,0,0,0,0,0);
    if(art.cached_bob_offset!=5)return driver_pilot_failure(__LINE__);
    if(!portable_driver_art_motion_refresh_due(&rules,1,5,1,0,0) ||
       portable_driver_art_motion_refresh_due(&rules,1,5,0,0,0) ||
       !portable_driver_art_motion_refresh_due(&rules,1,5,0,0,1))return driver_pilot_failure(__LINE__);
    /* Source phase gate/class8 stop wheel updates, after the cell refresh. */
    const uint16_t held_countdown=art.countdown;
    portable_driver_art_motion_step(&art,&rules,65536,0,0,0,0,
        6,5,0,1,0,0x40,0);
    if(art.countdown!=held_countdown || art.cached_bob_offset)return driver_pilot_failure(__LINE__);
    portable_driver_art_motion_step(&art,&rules,65536,0,0,0,0,
        7,5,0,1,0,0,1);
    if(art.countdown!=held_countdown || art.cached_bob_offset!=1)return driver_pilot_failure(__LINE__);
    art.bob_phase=3u;
    if(portable_driver_art_motion_bob_offset(&art,0,0,0,0x5c,1)!=7 ||
       portable_driver_art_motion_bob_offset(&art,0,0,0x8000,0x5c,1)!=1 ||
       portable_driver_art_motion_bob_offset(&art,0,0x40,0x8000,0x5c,1)!=0 ||
       portable_driver_art_motion_bob_offset(&art,0xa00000,0,0,0,1)!=1 ||
       portable_driver_art_motion_bob_offset(&art,0x9fffff,0,0,0,1)!=2)return driver_pilot_failure(__LINE__);
    /* Three wheel phases and eight bob phases wrap independently. */
    art=(PortableDriverArtMotion){.initialized=1u,.wheel_phase=2u,.bob_phase=7u};
    portable_driver_art_motion_tick_palette(&art,&rules,65536,0);
    if(art.wheel_phase || art.bob_phase || art.countdown!=4u)return driver_pilot_failure(__LINE__);
    return 0;
}

/* Synthetic tables exercise source timing and constructor priorities. */
static int test_driver_affine(void)
{
    static const PortableDriverAffineRow landing[]={
        {246,266,4},{234,278,7},{256,256,1}};
    static const PortableDriverAffineRow expiry[]={
        {378,382,12},{256,256,1}};
    static const PortableDriverAffineRow hold[]={{220,380,110}};
    static const PortableDriverAffineRow lift[]={{220,380,18}};
    static const PortableDriverAffineRow rebound[]={
        {220,380,112},{270,194,1},{256,256,1}};
    const PortableDriverAffineRules rules={.tables={
        {1,3,landing},{4,2,expiry},{5,1,hold},{6,1,lift},{7,3,rebound}}};
    PortableDriverPilotState crushed={0};
    portable_driver_pilot_thwomp_hit(&crushed,65537,-1);
    if(!crushed.flattened_64 || crushed.affine.pending_mode!=5u)return driver_pilot_failure(__LINE__);
    PortableDriverAffine *a=&crushed.affine;
    /* Pending crush takes priority over expiry and landing that same turn. */
    if(portable_driver_affine_request(a,&rules,4u)!=0 || a->mode!=5u ||
       portable_driver_affine_request(a,&rules,1u)!=0)return driver_pilot_failure(__LINE__);
    for(unsigned i=0u;i<110u;++i)
        if(portable_driver_affine_tick(a,&rules)!=0 || a->inverse_x_q8!=220u ||
           a->inverse_y_q8!=380u)return driver_pilot_failure(__LINE__);
    if(a->mode!=6u || a->remaining!=18u)return driver_pilot_failure(__LINE__);
    for(unsigned i=0u;i<17u;++i)
        if(portable_driver_affine_tick(a,&rules)!=0)return driver_pilot_failure(__LINE__);
    /* Physics releases on tick128, before that tick's affine pass. */
    if(a->mode!=6u || a->remaining!=1u ||
       portable_driver_affine_request(a,&rules,7u)!=1)return driver_pilot_failure(__LINE__);
    for(unsigned i=0u;i<112u;++i){
        if(portable_driver_affine_tick(a,&rules)!=(i==111u) ||
           a->inverse_x_q8!=220u || a->inverse_y_q8!=380u)return driver_pilot_failure(__LINE__);
    }
    for(unsigned row=1u;row<3u;++row){
        if(portable_driver_affine_tick(a,&rules)!=0 ||
           a->inverse_x_q8!=rebound[row].inverse_x_q8 ||
           a->inverse_y_q8!=rebound[row].inverse_y_q8)return driver_pilot_failure(__LINE__);
    }
    if(a->mode || a->inverse_x_q8!=256u || a->inverse_y_q8!=256u)return driver_pilot_failure(__LINE__);
    if(portable_driver_affine_request(a,&rules,7u)!=1 ||
       portable_driver_affine_request(a,&rules,1u)!=1 ||
       portable_driver_affine_request(a,&rules,7u)!=0)return driver_pilot_failure(__LINE__);
    unsigned calls=0u;
    while(a->mode){if(portable_driver_affine_tick(a,&rules)!=0 || ++calls>12u)return driver_pilot_failure(__LINE__);}
    if(calls!=12u)return driver_pilot_failure(__LINE__);
    if(portable_driver_affine_request(a,&rules,7u)!=1 ||
       portable_driver_affine_request(a,&rules,4u)!=1 ||
       portable_driver_affine_request(a,&rules,7u)!=0)return driver_pilot_failure(__LINE__);
    calls=0u;
    while(a->mode){if(portable_driver_affine_tick(a,&rules)!=0 || ++calls>13u)return driver_pilot_failure(__LINE__);}
    if(calls!=13u)return driver_pilot_failure(__LINE__);
    /* Optional tables keep the legacy simulation owner untouched. */
    PortableDriverAffine legacy={.pending_mode=5u};
    const PortableDriverAffine original=legacy;
    if(portable_driver_affine_request(&legacy,NULL,1u)!=0 ||
       portable_driver_affine_tick(&legacy,NULL)!=0 ||
       memcmp(&legacy,&original,sizeof legacy)!=0)return driver_pilot_failure(__LINE__);
    return 0;
}

#define FINISHED_CHECK(x) do { if(!(x)){fprintf(stderr,"finished driver line %d: %s\n",__LINE__,#x);return driver_pilot_failure(__LINE__);} } while(0)
static int finished_retire_on_progress(void *context,PortableRaceMotionObservation *motion)
{
    PortableDriverPilotState *state=context;
    (void)motion;portable_driver_pilot_retire_selected(state);return driver_pilot_failure(__LINE__);
}
static int test_finished_driver(void)
{
    uint8_t cells[64*64]={0},target[0x1a0]={0};
    uint16_t angle_lut[1]={0};int32_t sine[1024]={0};
    int16_t deceleration[8]={-2,-4,-8,-24,-32,-40,-48,-56};
    const PortableTrackRaster raster={.cells=cells,.capacity=sizeof cells,.width=64,.height=64};
    const PortableRacePoint route[1]={{16,0}},alternate[1]={{16,48}};
    PortableDriverPilotDependencies deps={.shell_orbit_rules=test_shell_orbit_rules(),.banana_rules=test_banana_rules(),.red_shell_rules=test_red_shell_rules(),.green_shell_rules=test_green_shell_rules(),.boo_rules=test_boo_rules(),.item_status_rules=test_item_status_rules(),.mushroom_rules=test_mushroom_rules(),.response_rules=portable_test_response_profile(),.raster=&raster,.sine_table=sine,.sine_count=1024,
        .source_track_route=route,.source_track_route_count=1,
        .source_track_angle_lut=angle_lut,.source_track_angle_lut_count=1,
        .source_angle_target_table=target,.source_angle_target_table_count=sizeof target,
        .source_auto_deceleration_table=deceleration,.source_auto_deceleration_count=8};
    PortableDriverFinishedInputs inputs={.alternate_route=alternate,.alternate_route_count=1};
    PortableDriverPilotState state={.source_update_order=1,.allow_track_boundary=1};
    state.steering.state_flags=0x800cu;state.steering.control_word=0x8100u;
    state.steering.position_82=42;state.steering.base_angle=43;state.steering.response_status=11;
    state.steering.driver_mode=8;state.steering.response_q8=44;state.finished_target_angle_ca=45;
    portable_driver_pilot_retire_selected(&state);
    FINISHED_CHECK(state.steering.state_flags==0xa03cu && !state.steering.position_82 &&
        !state.steering.base_angle && !state.steering.response_status &&
        state.steering.control_word==0x8100u && state.steering.driver_mode==8u &&
        state.steering.response_q8==44 && state.finished_target_angle_ca==45 &&
        state.finished_initialized && state.finished_route_row_191==1u);
    /* Prior components integrate once; current acceleration only affects the
     * next vector. Alternate route changes the next CA but not this heading. */
    state=(PortableDriverPilotState){.source_update_order=1,.allow_track_boundary=1};
    state.steering.state_flags=0x8000u;state.steering.control_word=0x8000u;
    state.pose.position_x=16*65536;state.pose.position_y=24*65536;
    state.pose.component_x=16;state.pose.component_y=-32;
    state.motion.speed_q16=100*65536;state.motion.acceleration_q16=65536;
    sine[256]=32768;target[0x80]=20;target[1]=7;
    deps.observe_progress=finished_retire_on_progress;deps.progress_context=&state;
    PortableModernDriftRules finish_modern_rules=pilot_modern_rules;
    finish_modern_rules.air=(PortableModernAirRules){.minimum_air_ticks=2u,.trick_window_ticks=8u,
        .boost_ticks=3u,.cap_ratio_256=320u,.minimum_speed_ratio_256=128u,
        .acceleration_q16=5*65536,.launch_mask=3u};
    deps.modern_drift_rules=&finish_modern_rules;
    state.modern_air=(PortableModernAirState){.air_ticks=3u,.armed=1u,.tricked=1u,.boost_ticks=2u};
    state.modern_drift=(PortableModernDriftState){.charge_ticks=4u,.charge_tier=3u,
        .previous_held=1u,.boost_ticks=2u,.boost_tier=3u,.last_applied_tier=3u};
    FINISHED_CHECK(portable_driver_pilot_tick_with_finished(&state,0xffffu,&deps,&inputs)==PORTABLE_DRIVER_PILOT_OK);
    FINISHED_CHECK(!state.modern_drift.charge_ticks&&!state.modern_drift.boost_ticks&&
        !state.modern_drift.charge_tier&&!state.modern_drift.boost_tier&&
        !state.modern_drift.previous_held&&!state.modern_drift.last_applied_tier);
    FINISHED_CHECK(!state.modern_air.air_ticks && !state.modern_air.armed && !state.modern_air.tricked &&
        !state.modern_air.boost_ticks && !state.modern_air.last_applied);
    deps.modern_drift_rules=NULL;
    FINISHED_CHECK(state.pose.position_x==16*65536+4096 && state.pose.position_y==24*65536-8192 &&
        state.motion.speed_q16==101*65536 && state.motion.component_x==0 && state.motion.component_y==-101 &&
        state.motion.acceleration_q16==7*4096 && state.steering.control_word==0x8000u &&
        state.finished_target_angle_ca==0x8000u && state.finished_route_row_191==1u);
    deps.observe_progress=NULL;deps.progress_context=NULL;
    const PortableDriverPilotState before=state;
    PortableDriverPilotState left=before,right=before;
    FINISHED_CHECK(portable_driver_pilot_tick_with_finished(&left,0x8200u,&deps,&inputs)==0 &&
        portable_driver_pilot_tick_with_finished(&right,0x4100u,&deps,&inputs)==0 &&
        memcmp(&left,&right,sizeof left)==0);
    /* Contact hold skips8200 but BD24 still refreshes acceleration. */
    state=before;state.steering.state_flags|=0x4000u;
    const int32_t speed=state.motion.speed_q16;
    const int16_t xcomponent=state.motion.component_x,ycomponent=state.motion.component_y;
    FINISHED_CHECK(portable_driver_pilot_finished_tick(&state,&deps,&inputs)==0 &&
        state.motion.speed_q16==speed && state.motion.component_x==xcomponent && state.motion.component_y==ycomponent);
    /* BC84's outside-route query forces row0 without changing191. DCA4
     * tests the lap byte91==7F, not raster sentinel7F/FF or48bits0/1. */
    for(unsigned sentinel=0u;sentinel<2u;++sentinel){
        memset(cells,sentinel?255:127,sizeof cells);
        state=before;state.pose.component_x=state.pose.component_y=0;
        state.motion.speed_q16=0;state.motion.acceleration_q16=0;
        state.steering.source_angle_56=state.steering.angle=0;
        state.finished_target_angle_ca=0;state.source_route_index_90=0;
        FINISHED_CHECK(portable_driver_pilot_finished_tick(&state,&deps,&inputs)==0 &&
            state.finished_route_row_191==1u && state.finished_target_angle_ca==0u &&
            state.source_route_index_90==0u && (state.steering.state_flags&2u));
    }
    memset(cells,0,sizeof cells);
    /* BD24 uses two distinct signed conversions: BC=-1 compares as0,
     * while ASR16/ASR6&15 selects acceleration bin15, not bin0. It writes
     * A into96 only after BC00/8200 and the new CA have been resolved. */
    state=before;state.pose.component_x=state.pose.component_y=0;
    state.motion.speed_q16=-1;state.motion.acceleration_q16=0;
    state.steering.source_angle_56=state.steering.angle=0;
    state.finished_target_angle_ca=0;state.steering.control_word=0x4100u;
    target[0]=3;target[15]=9;
    FINISHED_CHECK(portable_driver_pilot_finished_tick(&state,&deps,&inputs)==0 &&
        state.motion.speed_q16==-1 && state.motion.acceleration_q16==9*4096 &&
        state.steering.control_word==0x8000u && state.input.control_word==0x8000u);
    /* Original ordinary Peach TT, first C4C0 call after the5035-frame aid
     * finish: XY, prior components, speed, heading and fractionalC0 captured.
     * Sparse synthetic basis samples reproduce its next vector, not ROM art
     * or extracted sine tables. CA0 and retained96 come from the source call. */
    memset(sine,0,sizeof sine);memset(target,0,sizeof target);
    sine[994]=-6000;sine[286]=32200;target[0x80]=44;
    const PortableRacePoint source_route[1]={{896,480}};
    deps.source_track_route=source_route;inputs.alternate_route=source_route;
    state=(PortableDriverPilotState){.source_update_order=1,.allow_track_boundary=1,
        .finished_initialized=1,.finished_target_angle_ca=0};
    state.steering.state_flags=0xa034;state.steering.control_word=0x8000;
    state.steering.source_angle_56=63600;state.steering.angle=-1040; /* source halfword0xfbf0 */
    state.pose.position_x=59206656;state.pose.position_y=38699520;
    state.pose.component_x=-92;state.pose.component_y=-943;
    state.motion.speed_q16=62160896;state.motion.acceleration_q16=-2064384;
    FINISHED_CHECK(portable_driver_pilot_finished_tick(&state,&deps,&inputs)==0);
    FINISHED_CHECK(state.pose.position_x==59183104 && state.pose.position_y==38458112 &&
        (uint16_t)state.steering.angle==63600 && state.motion.speed_q16==60096512 &&
        state.motion.acceleration_q16==-1540096 && state.steering.control_word==0x8000u);
    return 0;
}

#define MODERN_CHECK(condition) do { if(!(condition))return driver_pilot_failure(__LINE__); } while(0)
static int test_modern_pilot(const PortableDriverPilotDependencies *fixture)
{
    /* Inert material and zero acceleration rows isolate the real source-order
     * owner: old C0 enters motion, then the selected reward publishes next C0. */
    uint8_t cells[16]={0};
    PortableTrackRaster raster={.cells=cells,.capacity=sizeof cells,.width=4u,.height=4u};
    PortableDriverPilotDependencies modern=*fixture,original=*fixture;
    modern.raster=original.raster=&raster;
    modern.modern_drift_rules=&pilot_modern_rules;
    PortableDriverPilotState seed={.source_update_order=1u,.preserve_source_base_speed=1u};
    seed.input.base_speed_q16=400*65536;
    seed.motion.speed_q16=300*65536;
    seed.steering.response_row=6u;
    seed.steering.control_word=0x8210u;
    PortableDriverPilotState state=seed;
    for(unsigned tick=1u;tick<=4u;++tick){
        MODERN_CHECK(portable_driver_pilot_tick(&state,0x8210u,&modern)==PORTABLE_DRIVER_PILOT_OK);
        MODERN_CHECK(state.modern_drift.charge_ticks==tick&&
            state.modern_drift.charge_tier==(tick<2u?0u:tick==2u?1u:tick==3u?2u:3u));
        MODERN_CHECK(!(state.steering.response_flags&0x40u)&&
            !(state.forward_acceleration.state_flags&0x40u)&&!(state.hop.animation_flags&0x40u));
    }
    PortableDriverPilotState copied=state;
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&modern)==PORTABLE_DRIVER_PILOT_OK);
    MODERN_CHECK(portable_driver_pilot_tick(&copied,0x8000u,&modern)==PORTABLE_DRIVER_PILOT_OK);
    MODERN_CHECK(state.modern_drift.boost_ticks==3u&&state.modern_drift.boost_tier==3u&&
        state.modern_drift.last_applied_tier==3u&&!state.modern_drift.charge_ticks);
    MODERN_CHECK(state.motion.speed_q16==300*65536&&state.motion.acceleration_q16==3*65536&&
        state.auto_acceleration.acceleration_q16==3*65536&&state.forward_acceleration.acceleration_q16==3*65536&&
        state.brake.acceleration_q16==3*65536);
    MODERN_CHECK(state.charged_exit_count==1u&&state.charged_audio_cue_count==1u&&state.charged_particle_count==1u);
    MODERN_CHECK(copied.motion.speed_q16==state.motion.speed_q16&&
        copied.motion.acceleration_q16==state.motion.acceleration_q16&&
        copied.modern_drift.boost_ticks==state.modern_drift.boost_ticks);
    for(unsigned tick=1u;tick<4u;++tick){
        MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&modern)==PORTABLE_DRIVER_PILOT_OK);
        MODERN_CHECK(state.modern_drift.last_applied_tier==3u&&state.modern_drift.boost_ticks==3u-tick&&
            state.motion.speed_q16==(300+(int32_t)tick*3)*65536&&state.motion.acceleration_q16==3*65536);
    }
    MODERN_CHECK(!state.modern_drift.boost_tier&&state.charged_exit_count==1u);
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&modern)==PORTABLE_DRIVER_PILOT_OK);
    MODERN_CHECK(!state.modern_drift.last_applied_tier&&state.motion.speed_q16==312*65536);
    MODERN_CHECK(copied.modern_drift.boost_ticks==3u); /* Another local owner never advances implicitly. */

    state=seed;state.motion.speed_q16=600*65536;
    state.modern_drift=(PortableModernDriftState){.boost_ticks=1u,.boost_tier=3u};
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&modern)==PORTABLE_DRIVER_PILOT_OK);
    MODERN_CHECK(state.motion.speed_q16==450*65536&&state.auto_acceleration.speed_q16==450*65536&&
        state.forward_acceleration.speed_q16==450*65536&&state.brake.speed_q16==450*65536&&
        state.steering.speed_q16==450*65536&&state.modern_drift.last_applied_tier==3u);

    /* The old charge producer still exists for Original; selected Modern
     * suppresses only its reward flag and preserves the source counter. */
    PortableDriverPilotState stock=seed,selected=seed;
    stock.motion.speed_q16=selected.motion.speed_q16=400*65536;
    stock.drift_charge.charge_counter=selected.drift_charge.charge_counter=200u;
    stock.modern_drift=(PortableModernDriftState){.charge_ticks=1u,.previous_held=1u};
    MODERN_CHECK(portable_driver_pilot_tick(&stock,0x8210u,&original)==PORTABLE_DRIVER_PILOT_OK);
    MODERN_CHECK(portable_driver_pilot_tick(&selected,0x8210u,&modern)==PORTABLE_DRIVER_PILOT_OK);
    MODERN_CHECK((stock.forward_acceleration.state_flags&0x40u)&&
        !(selected.forward_acceleration.state_flags&0x40u)&&
        stock.drift_charge.charge_counter==selected.drift_charge.charge_counter&&
        stock.modern_drift.charge_ticks==1u&&!stock.modern_drift.charge_tier);
    for(unsigned gate=0u;gate<6u;++gate){
        state=seed;
        state.modern_drift=(PortableModernDriftState){.charge_ticks=4u,.charge_tier=3u,
            .previous_held=1u,.boost_ticks=2u,.boost_tier=3u};
        uint16_t controls=0u;
        if(gate==0u)controls=0x4000u;
        else if(gate==1u)state.flattened_64=1u;
        else if(gate==2u)state.lightning_timer_68=1;
        else if(gate==3u)state.item_lock_ac=1u;
        else if(gate==4u)state.item_requests_b2=0x8000u;
        else {
            state.source_hazard_enabled=1u;
            state.source_hazard_kind=PORTABLE_DRIVER_HAZARD_LAVA;
            state.hop.mode=3u;
        }
        MODERN_CHECK(portable_driver_pilot_tick(&state,controls,&modern)==PORTABLE_DRIVER_PILOT_OK);
        MODERN_CHECK(!state.modern_drift.charge_ticks&&!state.modern_drift.boost_ticks&&
            !state.modern_drift.last_applied_tier&&!state.charged_exit_count);
    }
    return 0;
}
static int test_modern_air_pilot(const PortableDriverPilotDependencies *fixture)
{
    uint8_t cells[16]={0};
    PortableTrackRaster raster={.cells=cells,.capacity=sizeof cells,.width=4u,.height=4u};
    PortableModernDriftRules rules=pilot_modern_rules;
    rules.air=(PortableModernAirRules){.minimum_air_ticks=3u,.trick_window_ticks=8u,
        .boost_ticks=3u,.cap_ratio_256=320u,.minimum_speed_ratio_256=128u,
        .acceleration_q16=5*65536,.launch_mask=3u};
    PortableDriverPilotDependencies deps=*fixture;deps.raster=&raster;deps.modern_drift_rules=&rules;
    PortableDriverPilotState seed={.source_update_order=1u,.preserve_source_base_speed=1u};
    seed.input.base_speed_q16=400*65536;seed.motion.speed_q16=300*65536;
    seed.steering.response_row=6u;
    /* Source-order 9B38 consumes the stored prior control word before 8A08
     * publishes this turn's input. Begin on the accepted launch turn. */
    seed.steering.control_word=0x8014u;
    PortableDriverPilotState state=seed;
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8014u,&deps)==0);
    MODERN_CHECK(state.hop.mode==1u && state.hop.height_q16==0 && state.modern_air.armed &&
        state.modern_air.air_ticks==1u && !state.modern_air.tricked && !state.charged_exit_count);
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0 && state.modern_air.air_ticks==2u);
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8014u,&deps)==0 && state.modern_air.air_ticks==3u &&
        state.modern_air.tricked && !state.charged_exit_count);
    PortableDriverPilotState copy=state;
    unsigned landing_tick=0u;
    for(unsigned tick=1u;tick<=32u;++tick){
        MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0);
        MODERN_CHECK(portable_driver_pilot_tick(&copy,0x8000u,&deps)==0);
        MODERN_CHECK(state.modern_air.air_ticks==copy.modern_air.air_ticks &&
            state.modern_air.boost_ticks==copy.modern_air.boost_ticks &&
            state.modern_air.tricked==copy.modern_air.tricked &&
            state.motion.speed_q16==copy.motion.speed_q16 && state.pose.position_z==copy.pose.position_z);
        if(state.hop.landed){landing_tick=tick;break;}
        MODERN_CHECK(!state.charged_exit_count && !state.modern_air.boost_ticks);
    }
    MODERN_CHECK(landing_tick>0u && state.modern_air.boost_ticks==2u && state.modern_air.last_applied &&
        !state.modern_air.armed && !state.modern_air.tricked && state.charged_exit_count==1u &&
        state.charged_audio_cue_count==1u && state.charged_particle_count==1u &&
        state.motion.acceleration_q16==5*65536 && state.forward_acceleration.acceleration_q16==5*65536);
    const int32_t landing_speed=state.motion.speed_q16;
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0 &&
        state.motion.speed_q16==landing_speed+5*65536 && state.modern_air.boost_ticks==1u);
    MODERN_CHECK(copy.modern_air.boost_ticks==2u); /* A second local's clock stays independent. */
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0 &&
        !state.modern_air.boost_ticks && state.modern_air.last_applied && state.charged_exit_count==1u);
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0 && !state.modern_air.last_applied);
    /* Two valid rewards consume both clocks; the lower drift cap cannot
     * truncate the higher air cap and only one next-turn C0 is published. */
    seed.steering.control_word=0x8000u;
    state=seed;state.motion.speed_q16=600*65536;
    state.modern_drift=(PortableModernDriftState){.boost_ticks=3u,.boost_tier=3u};
    state.modern_air=(PortableModernAirState){.boost_ticks=2u};
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0 &&
        state.modern_drift.boost_ticks==2u && state.modern_air.boost_ticks==1u &&
        state.motion.speed_q16==500*65536 && state.motion.acceleration_q16==5*65536 &&
        state.auto_acceleration.speed_q16==500*65536 && state.brake.speed_q16==500*65536 &&
        state.forward_acceleration.speed_q16==500*65536 && state.steering.speed_q16==500*65536);
    for(unsigned gate=0u;gate<5u;++gate){
        state=seed;state.modern_air=(PortableModernAirState){.air_ticks=3u,.armed=1u,.tricked=1u,.boost_ticks=2u};
        uint16_t control=0u;
        if(gate==0u)control=0x4000u;
        else if(gate==1u)state.flattened_64=1u;
        else if(gate==2u)state.lightning_timer_68=1;
        else if(gate==3u)state.item_lock_ac=1u;
        else {state.source_hazard_enabled=1u;state.source_hazard_kind=PORTABLE_DRIVER_HAZARD_LAVA;state.hop.mode=3u;}
        MODERN_CHECK(portable_driver_pilot_tick(&state,control,&deps)==0 && !state.modern_air.armed &&
            !state.modern_air.tricked && !state.modern_air.boost_ticks && !state.modern_air.last_applied &&
            !state.charged_exit_count);
    }
    state=seed;state.modern_air=(PortableModernAirState){.air_ticks=3u,.armed=1u,.tricked=1u,.boost_ticks=2u};
    deps.modern_drift_rules=&pilot_modern_rules; /* Version one never touches the new owner. */
    MODERN_CHECK(portable_driver_pilot_tick(&state,0x8000u,&deps)==0 && state.modern_air.air_ticks==3u &&
        state.modern_air.armed && state.modern_air.tricked && state.modern_air.boost_ticks==2u);
    return 0;
}

#undef MODERN_CHECK

int run_driver_pilot(void)
{
    const PortableDriverResponseRules response_rules=captured_response_profile();
    if(test_driver_art_motion())return driver_pilot_failure(__LINE__);
    if(test_driver_affine())return driver_pilot_failure(__LINE__);
    if(test_finished_driver())return driver_pilot_failure(__LINE__);
    /* AF10: both headings must cross their strict threshold; clearing has
     * its own inclusive threshold, and source slot3 cannot enter. */
    for(unsigned sign=0u;sign<2u;++sign){
        PortableDriverPilotState warning={0};
        warning.steering.source_angle_56=sign?0x9801u:0x67ffu;
        warning.steering.angle=INT16_MIN;
        portable_driver_pilot_wrong_way(&warning,0u);
        if(warning.steering.state_flags&0x400u)return driver_pilot_failure(__LINE__);
        warning.steering.source_angle_56=sign?0x9800u:0x6800u;
        warning.steering.angle=sign?0xa801u:0x57ffu;
        portable_driver_pilot_wrong_way(&warning,0u);
        if(warning.steering.state_flags&0x400u)return driver_pilot_failure(__LINE__);
        warning.steering.angle=sign?0xa800u:0x5800u;
        portable_driver_pilot_wrong_way(&warning,0u);
        if(!(warning.steering.state_flags&0x400u)||warning.hop.recovery_requests!=0x1000u)return driver_pilot_failure(__LINE__);
        warning.hop.recovery_requests=0u;
        warning.steering.source_angle_56=0x3000u;
        portable_driver_pilot_wrong_way(&warning,0u);
        if(!(warning.steering.state_flags&0x400u)||warning.hop.recovery_requests)return driver_pilot_failure(__LINE__);
        warning.steering.source_angle_56=0x2fffu;
        portable_driver_pilot_wrong_way(&warning,0u);
        if((warning.steering.state_flags&0x400u)||warning.hop.recovery_requests!=0x800u)return driver_pilot_failure(__LINE__);
        warning.steering.source_angle_56=warning.steering.angle=INT16_MIN;
        warning.steering.response_slot=3u;warning.hop.recovery_requests=0u;
        portable_driver_pilot_wrong_way(&warning,0u);
        if(warning.steering.state_flags&0x400u)return driver_pilot_failure(__LINE__);
    }
    /* 38F1C: scene mode2 arms drift 288 units below A8. Other modes
     * retain the ordinary margin/response gate, with no early arm. */
    for (unsigned trial=0u;trial<2u;++trial)
    for (int offset=-1;offset<=1;++offset)
    {
        PortableDriverSteeringState gate={
            .speed_q16=(806+offset)*65536,.time_trial_mode=(uint8_t)trial,
            .control_word=0x8000u,.response_row=2u,
        };
        if (portable_driver_source_left_turn_response_tick(&gate,1094*65536,&response_rules)!=0 ||
            gate.response_status!=(trial && offset>=0 ? 1u:0u)) return driver_pilot_failure(__LINE__);
    }
    /* FF80 -> A9AC -> 8E34: cap, both spin directions, mandatory turns,
     * low-speed wrap exit and protected/braking cancellation. */
    for (unsigned left=0;left<2u;++left)
    {
        PortableDriverSteeringState hit = {
            .speed_q16=0x04000000,.position_82=left?0xffffu:1u,
            .response_flags=0x1000u,.driver_mode=8u,.response_q8=500,
        };
        if (portable_driver_queued_hit(&hit) != 0 || hit.speed_q16 != 0x03800000 ||
            hit.response_status != (left?5u:6u) || hit.spin_delay != 60u ||
            hit.drift_angle != 60u || hit.response_flags != 0u || hit.driver_mode != 0u ||
            hit.response_q8 != 0) return driver_pilot_failure(__LINE__);
        hit.spin_delay=0u;hit.speed_q16=0;hit.position_82=0u;
        for (unsigned tick=0;tick<60u;++tick)
            if (portable_driver_source_left_turn_response_tick(&hit,0,&response_rules) != 0 ||
                hit.response_status != (left?5u:6u) || hit.drift_angle != 59u-tick)
                return driver_pilot_failure(__LINE__);
        for (unsigned tick=0;tick<26u && hit.response_status!=14u;++tick)
            if (portable_driver_source_left_turn_response_tick(&hit,0,&response_rules) != 0) return driver_pilot_failure(__LINE__);
        if (hit.response_status!=14u || !(hit.response_flags&0x400u)) return driver_pilot_failure(__LINE__);
        hit.response_status=left?5u:6u;hit.response_flags=2u;hit.spin_delay=0u;
        if (portable_driver_source_left_turn_response_tick(&hit,0,&response_rules)!=0 ||
            hit.response_status!=14u || !(hit.response_flags&0x400u)) return driver_pilot_failure(__LINE__);
    }
    /* E490/A9AC/8E34 strong response: both directions, short and long rates,
     * the signed low-rate exit window, and halfword overflow. */
    for (unsigned positive=0;positive<2u;++positive)
    for (unsigned short_hit=0;short_hit<2u;++short_hit)
    {
        PortableDriverSteeringState hit = {
            .response_flags=(uint16_t)(0x0200u | (positive?0x0100u:0u)),
            .driver_mode=8u,.response_q8=500,
        };
        uint16_t rate=short_hit?0x1000u:0x2000u;
        if(portable_driver_queued_hit(&hit)!=0 || hit.response_status!=13u ||
           hit.driver_mode!=10u || hit.response_q8!=0) return driver_pilot_failure(__LINE__);
        if(portable_driver_strong_spin_tick(&hit,&rate)!=0 ||
           hit.position_82!=(positive?0x1000u:0xf000u) ||
           rate!=(short_hit?0x0fc0u:0x1fc0u)) return driver_pilot_failure(__LINE__);
        unsigned ticks=1u;
        while(hit.response_status==13u && ticks<256u)
        {
            if(portable_driver_queued_hit(&hit)!=0 ||
               portable_driver_strong_spin_tick(&hit,&rate)!=0) return driver_pilot_failure(__LINE__);
            ++ticks;
        }
        if(hit.response_status!=0u || hit.driver_mode!=0u ||
           hit.position_82!=0u || hit.base_angle!=0 || rate!=192u ||
           hit.response_flags!=0x0400u || ticks!=(short_hit?64u:128u)) return driver_pilot_failure(__LINE__);
    }
    const int16_t strong_positions[]={383,384,-383,-384,32767,-32768};
    const uint16_t strong_expected[]={0u,0xfd00u,0u,0x0300u,0x7b7fu,0x8480u};
    for(unsigned i=0;i<6u;++i)
    {
        PortableDriverSteeringState hit={.response_status=13u,
            .driver_mode=10u,.response_flags=0x0300u,
            .position_82=(uint16_t)strong_positions[i],.base_angle=123};
        uint16_t rate=0;
        if(portable_driver_strong_spin_tick(&hit,&rate)!=0 ||
           hit.position_82!=strong_expected[i] ||
           hit.response_status!=((i==0u||i==2u)?0u:13u)) return driver_pilot_failure(__LINE__);
    }
    /* Cheese Land frame646: row6 arms early, then low speed enters14.
     * At256 the turn branch runs; mode4 has the same threshold. */
    for (unsigned mode = 0; mode <= 4u; mode += 4u)
    for (unsigned high = 0; high < 2u; ++high)
    {
        PortableDriverSteeringState low_turn = {
            .response_status=1u, .response_row=6u, .driver_mode=(uint8_t)mode,
            .control_word=0x8100u, .base_angle=512,
            .speed_q16=high ? 0x01000000 : 0x00ffffff,
        };
        if (portable_driver_source_left_turn_response_tick(&low_turn,0,&response_rules) != 0 ||
            low_turn.response_status != (high ? 1u : 14u) ||
            low_turn.base_angle != (high ? -640 : 512))
            return driver_pilot_failure(__LINE__);
    }
    for (unsigned status = 0u; status <= 4u; ++status)
    for (unsigned mode = 0u; mode <= 4u; mode += 4u)
    for (unsigned high = 0u; high < 2u; ++high)
    {
        PortableDriverSteeringState turn = {
            .response_status=(uint8_t)status, .driver_mode=(uint8_t)mode,
            .response_row=6u, .control_word=0xc200u, .base_angle=512,
            .speed_q16=high ? 0x01000001 : 0x01000000,
        };
        if (portable_driver_source_left_turn_response_tick(&turn,0,&response_rules) != 0 ||
            (turn.response_status == 15u) != (!high && mode != 4u))
            return driver_pilot_failure(__LINE__);
    }
    {
        PortableDriverSteeringState turn = {.response_status=15u,
            .response_row=6u, .control_word=0xc200u, .base_angle=512,
            .position_82=512u, .response_q8=-800};
        if (portable_driver_source_left_turn_response_tick(&turn,0,&response_rules) != 0 ||
            turn.response_status != 15u || turn.response_slot != 3u ||
            turn.base_angle != 352 || turn.position_82 != 288u ||
            turn.response_q8 != -800)
            return driver_pilot_failure(__LINE__);
        turn.control_word=0xc000u;
        if (portable_driver_source_left_turn_response_tick(&turn,0,&response_rules) != 0 ||
            turn.response_status != 15u || turn.response_q8 != 0 ||
            turn.base_angle != 192 || turn.position_82 != 64u)
            return driver_pilot_failure(__LINE__);
        turn.control_word=0x8000u;turn.response_q8=800;
        if (portable_driver_source_left_turn_response_tick(&turn,0,&response_rules) != 0 ||
            turn.response_status != 0u || turn.response_q8 != 0 ||
            turn.base_angle != 192 || turn.position_82 != 64u)
            return driver_pilot_failure(__LINE__);
    }
    const int16_t steering_response_data[4] = {
        0x09ae, 0x009f, 0x006c, 0x0094,
    };
    PortableDriverSteeringState response_state = {
        .response_q8 = 0,
        .control_word = UINT16_C(0x0100),
    };
    PortableDriverSteeringState high_speed_state = {
        .speed_q16 = INT32_C(0x00700000),
        .response_q8 = -0x006c,
        .control_word = UINT16_C(0x0200),
    };
    int16_t auto_table[64] = {0};
    const int16_t brake_table[8] = {-4, -10, -16, -24, -48, -112, -160,
                                    -192};
    const int16_t brake_accelerate_table[8] = {4, -8, -16, -32, -64, -128,
                                               -160, -256};
    int16_t response_curve[8] = {0};
    int16_t local_table[16] = {0};
    int16_t limit_thresholds[16] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,688,416,640,640,864,928};
    const int16_t steering_delta_table[10] = {
        0, 0x1000, 0x2000, 0x3000, 0x3800,
        0x3c00, 0x3e00, 0x4000, INT16_MIN, 0};
    const int16_t vector_alternate_x[4] = {256, 256, -256, -256};
    const int16_t vector_alternate_y[4] = {-256, 256, 256, -256};
    const int16_t normal_resultant_x[4] = {128, 128, 240, 240};
    const int16_t normal_resultant_y[4] = {240, 240, 128, 128};
    int32_t sine_table[1024] = {0};
    uint8_t cells[4 * 4];
    /* Low material 4 is inert and preserves tests' explicit response selector. */
    uint8_t material_flags[256] = {4};
    uint8_t terrain_cells[4 * 4] = {0};
    PortableTrackRaster raster = {
        .cells = cells,
        .capacity = sizeof(cells),
        .width = 4,
        .height = 4,
    };
    const PortableTrackMaterialView track_material = {
        .flags = material_flags,
        .count = sizeof(material_flags),
    };
    const PortableTerrainView terrain = {
        .cells = terrain_cells,
        .row_stride = 4,
        .width = 4,
        .height = 4,
    };
    static const int32_t vector_x_offsets[8] = {0};
    static const int32_t vector_y_offsets[8] = {0};
    const PortableTerrainEffectTableView vector_effect_tables = {
        .x_offsets = vector_x_offsets,
        .x_count = 8,
        .y_offsets = vector_y_offsets,
        .y_count = 8,
    };
    static const int32_t normal_response_entries[
        PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT] = {
            -4, -8, -16, -24, -36, -56, -64, -85,
        };
    const PortableSourceNormalResponseTableView normal_response_table = {
        .entries = normal_response_entries,
        .count = PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT,
    };
    PortableDriverPilotDependencies dependencies = {.shell_orbit_rules=test_shell_orbit_rules(),.banana_rules=test_banana_rules(),.red_shell_rules=test_red_shell_rules(),.green_shell_rules=test_green_shell_rules(),.boo_rules=test_boo_rules(),.item_status_rules=test_item_status_rules(),.mushroom_rules=test_mushroom_rules(),.response_rules=&response_rules,
        .auto_acceleration_table = auto_table,
        .auto_acceleration_count = 64,
        .brake_table = brake_table,
        .brake_table_count = 8,
        .brake_accelerate_table = brake_accelerate_table,
        .brake_accelerate_table_count = 8,
        .forward_response_curve = response_curve,
        .forward_response_count = 8,
        .forward_local_table = local_table,
        .forward_local_count = 16,
        .steering_delta_table = steering_delta_table,
        .steering_delta_count = 10,
        .sine_table = sine_table,
        .sine_count = 1024,
        .raster = &raster,
        .track_material = &track_material,
        .terrain = &terrain,
        .terrain_effect_tables = &vector_effect_tables,
        .vector_alternate_x_components = vector_alternate_x,
        .vector_alternate_x_count = 4,
        .vector_alternate_y_components = vector_alternate_y,
        .vector_alternate_y_count = 4,
        .normal_resultant_x_table = normal_resultant_x,
        .normal_resultant_x_count = 4,
        .normal_resultant_y_table = normal_resultant_y,
        .normal_resultant_y_count = 4,
        .normal_response_table = &normal_response_table,
    };
    if(test_modern_pilot(&dependencies))return driver_pilot_failure(__LINE__);
    if(test_modern_air_pilot(&dependencies))return driver_pilot_failure(__LINE__);
    /* Complete8710 case15 replaces C0, including fractions, at128. */
    for (unsigned high = 0u; high < 2u; ++high)
    {
        PortableDriverPilotState turn = {0};
        turn.source_update_order=1u;
        turn.input.speed_parameter=5;
        turn.steering.response_status=15u;
        turn.steering.control_word=0xc210u;
        turn.drift_charge.charge_counter=64u;
        turn.steering.response_q8=800;
        turn.forward_acceleration.response_q8=800;
        turn.motion.speed_q16=high ? 0x007fbce0 : 0x007fbcdf;
        turn.motion.acceleration_q16=0x4321;
        if (portable_driver_pilot_tick(&turn,0xc210u,&dependencies) != 0 ||
            turn.steering.response_status != 15u ||
            turn.motion.acceleration_q16 != (high ? -0x00080000 : 0x00080000) ||
            turn.drift_charge.charge_counter != 0 ||
            !(turn.steering.response_flags & 8u))
            return driver_pilot_failure(__LINE__);
    }
    {
        /* 378FC consumes entry5A before B550, but its feedback owner must
         * not be required to retain the independent timer38 collision hold. */
        const PortableImpactEffectRules impact_rules={{14u,14u,14u,14u,13u,13u}};
        PortableDriverPilotDependencies impact_dependencies=dependencies;
        impact_dependencies.impact_effect_rules=&impact_rules;
        PortableDriverPilotState selected={.source_update_order=1u,.impact_selected=1u,
            .impact_x=120,.impact_y=140,.impact_depth=256};
        selected.input.speed_parameter=5;
        selected.vector_effect.contact_word_5a=0x8007u;
        selected.normal_collision_source_from_native_state=1u;
        selected.normal_collision_source_use_b550_table=1u;
        selected.normal_collision_timer_38=8u;
        PortableDriverPilotState unselected=selected;
        unselected.impact_selected=0u;
        if(portable_driver_pilot_tick(&selected,0u,&impact_dependencies)!=0 ||
           portable_driver_pilot_tick(&unselected,0u,&impact_dependencies)!=0 ||
           selected.impact_event!=1u || selected.vector_effect.contact_word_5a!=0u ||
           unselected.impact_event!=0u || unselected.vector_effect.contact_word_5a!=0x8007u ||
           selected.normal_collision_timer_38!=unselected.normal_collision_timer_38 ||
           selected.normal_collision_timer_38!=7u ||
           selected.steering.state_flags!=unselected.steering.state_flags ||
           !selected.impact_effects.slots[0].active || selected.impact_effects.slots[0].age!=0u ||
           selected.impact_effects.slots[0].x!=120 || selected.impact_effects.slots[0].y!=140 ||
           selected.impact_effects.slots[0].depth!=257)
            return driver_pilot_failure(__LINE__);
        if(portable_driver_pilot_tick(&selected,0u,&impact_dependencies)!=0 ||
           selected.impact_event!=0u || selected.impact_effects.slots[0].age!=0u ||
           selected.normal_collision_timer_38!=6u)
            return driver_pilot_failure(__LINE__); /* only the later app FX pass advances callback age */
        selected.steering.state_flags|=0x20u;
        selected.vector_effect.contact_word_5a=0xc007u;
        if(portable_driver_pilot_tick(&selected,0u,&impact_dependencies)!=0 ||
           selected.impact_event!=0u || selected.vector_effect.contact_word_5a!=0xc007u)
            return driver_pilot_failure(__LINE__);
    }
    PortableDriverPilotState state = {0};
    PortableDriverPilotState repeat = {0};
    PortableDriverPilotState brake_state = {0};
    PortableDriverPilotState steering_state = {0};
    PortableDriverPilotState vector_state = {0};
    PortableDriverPilotState before_error;

    {
        /* ROM DAEC preserves mode11 on a nonzero angle. It clears the
         * mode only after the response handler reaches zero. */
        PortableDriverPilotState impact = {0};
        impact.source_update_order = 1u;
        impact.input.speed_parameter = 5;
        impact.motion.speed_q16 = 1024*65536;
        impact.steering.driver_mode = 11u;
        impact.auto_acceleration.driver_mode = 11u;
        impact.steering.response_status = 11u;
        impact.steering.base_angle = 4096;
        for (unsigned tick = 0; tick < 2u; ++tick)
            if (portable_driver_pilot_tick(&impact,0x4000u,&dependencies) != 0 ||
                impact.steering.driver_mode != 11u ||
                impact.auto_acceleration.driver_mode != 11u ||
                impact.steering.base_angle != 4096 ||
                impact.motion.acceleration_q16 != -16*65536)
                return driver_pilot_failure(__LINE__);
        PortableDriverPilotState held = impact;
        held.source_terrain22_enabled = 1u;
        held.normal_collision_timer_62 = 2u;
        held.steering.state_flags = 0x4000u;
        held.normal_collision_response_output_pending = 1u;
        held.normal_collision_response_output_c0 = -64*65536;
        held.steering.control_word = 0x8200u;
        if (portable_driver_pilot_tick(&held,0x8000u,&dependencies) != 0 ||
            held.motion.acceleration_q16 != -16*65536 ||
            held.steering.control_word != 0x8200u ||
            held.normal_collision_response_output_pending != 1u)
            return driver_pilot_failure(__LINE__);
        PortableDriverPilotState stopping = impact;
        stopping.motion.speed_q16 = 0x004c8000;
        stopping.motion.acceleration_q16 = -64*65536;
        stopping.normal_collision_response_output_pending = 1u;
        stopping.normal_collision_response_output_c0 = -85*65536;
        if (portable_driver_pilot_tick(&stopping,0x4000u,&dependencies) != 0 ||
            stopping.motion.speed_q16 != 0x000c8000 ||
            stopping.steering.driver_mode != 0u ||
            stopping.steering.response_status != 14u ||
            stopping.normal_collision_response_output_pending != 0u ||
            stopping.motion.acceleration_q16 == -85*65536)
            return driver_pilot_failure(__LINE__);
        /* An impact exit to ordinary acceleration still runs8AFC first.
         * Neutral1000 ->852, then8640's -2500 curve truncates to820. */
        const int16_t curve[8] = {-2500,-2500,-2500,-2500,-2500,-2500,-2500,-2500};
        PortableDriverPilotDependencies exit_dependencies = dependencies;
        exit_dependencies.forward_response_curve = curve;
        exit_dependencies.steering_response_data = steering_response_data;
        exit_dependencies.steering_response_data_count = 4u;
        PortableDriverPilotState ordered_exit = impact;
        ordered_exit.motion.speed_q16 = 1024*65536;
        ordered_exit.motion.acceleration_q16 = 0;
        ordered_exit.steering.base_angle = 1000;
        ordered_exit.forward_acceleration.response_q8 = 1000;
        if (portable_driver_pilot_tick(&ordered_exit,0x8000u,&exit_dependencies) != 0 ||
            ordered_exit.steering.response_status != 14u ||
            ordered_exit.steering.response_q8 != 820)
            return driver_pilot_failure(__LINE__);
        impact.steering.base_angle = 1024;
        if (portable_driver_pilot_tick(&impact,0x8000u,&dependencies) != 0 ||
            impact.steering.base_angle != 0 || impact.steering.driver_mode != 0u ||
            impact.steering.response_status != 14u ||
            impact.motion.acceleration_q16 != -16*65536)
            return driver_pilot_failure(__LINE__);
    }

    /* P107 exact source sub_8038A08 state: +0x88=0x0900 contributes
     * 0x0120 to +0x7c, then +0x80 derives +0x7a and +0x82 derives +0x56.
     * The source gate is 0x2000, not the scheduling bit 0x4000. */
    {
        PortableDriverSteeringState source_composition_state = {
            .speed_q16 = INT32_C(0x013a0000),
            .response_q8 = INT16_C(0x0900),
            .control_word = UINT16_C(0x8100),
            .state_flags = UINT32_C(0x0000c400),
            .angle = INT16_C(0x492c),
            .steering_offset = INT16_C(0x616c),
            .base_angle = (int16_t)UINT16_C(0x4fb4),
            .position_82 = UINT16_C(0xe320),
        };

        if (portable_driver_steering_tick(
                &source_composition_state, steering_delta_table, 10) !=
                PORTABLE_DRIVER_STEERING_OK ||
            source_composition_state.steering_offset !=
                (int16_t)UINT16_C(0x628c) ||
            source_composition_state.angle != (-INT16_C(19904)) ||
            source_composition_state.source_angle_56 !=
                (int16_t)UINT16_C(0x7f6c) ||
            source_composition_state.response_q8 != INT16_C(0x0900))
            return driver_pilot_failure(__LINE__);
    }

    if (portable_driver_steering_tick(
            &high_speed_state, steering_delta_table, 10) !=
            PORTABLE_DRIVER_STEERING_OK ||
        high_speed_state.angle != -13 ||
        high_speed_state.steering_offset != -13)
        return driver_pilot_failure(__LINE__);

    {
        PortableDriverSteeringState accumulated_state = {
            .speed_q16 = INT32_C(0x00100000),
            .control_word = UINT16_C(0x0100),
            .steering_offset = 0x0010,
            .base_angle = 0x0020,
            .angle = 0x0030,
        };

        if (portable_driver_steering_tick(
                &accumulated_state, steering_delta_table, 10) !=
                PORTABLE_DRIVER_STEERING_OK ||
            accumulated_state.steering_offset != 0x1010 ||
            accumulated_state.angle != 0x1030)
            return driver_pilot_failure(__LINE__);
        accumulated_state.control_word = UINT16_C(0x0200);
        if (portable_driver_steering_tick(
                &accumulated_state, steering_delta_table, 10) !=
                PORTABLE_DRIVER_STEERING_OK ||
            accumulated_state.steering_offset != 0x0010 ||
            accumulated_state.angle != 0x0030)
            return driver_pilot_failure(__LINE__);
    }

    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != 0x006c)
        return driver_pilot_failure(__LINE__);
    response_state.control_word = UINT16_C(0x0200);
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != -0x0033)
        return driver_pilot_failure(__LINE__);
    response_state.response_q8 = 0;
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
        PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != -0x006c)
        return driver_pilot_failure(__LINE__);
    response_state.response_q8 = -0x09b4;
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
        PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != -0x09ae)
        return driver_pilot_failure(__LINE__);
    response_state.control_word = 0;
    response_state.response_q8 = 200;
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != 52)
        return driver_pilot_failure(__LINE__);
    response_state.response_q8 = -10;
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != 0)
        return driver_pilot_failure(__LINE__);
    response_state.response_q8 = 10;
    response_state.response_flags = UINT16_C(0x0200);
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != 10)
        return driver_pilot_failure(__LINE__);
    response_state.response_flags = 0;
    response_state.response_status = 5;
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != 10)
        return driver_pilot_failure(__LINE__);
    response_state.response_status = 0;
    response_state.state_flags = UINT32_C(0x80);
    if (portable_driver_steering_response_tick(
            &response_state, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_OK ||
        response_state.response_q8 != 0 ||
        portable_driver_steering_response_tick(
            NULL, steering_response_data, 4) !=
            PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT ||
        portable_driver_steering_response_tick(
            &response_state, steering_response_data, 3) !=
            PORTABLE_DRIVER_STEERING_RESPONSE_TABLE_TOO_SMALL)
        return driver_pilot_failure(__LINE__);

    {
        PortableDriverSteeringState selector = {
            .response_selector_flags = 3u,
            .response_selector_index = 0u,
        };

        if (portable_driver_response_row_select(&selector, 0u,&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            selector.response_slot != 0u || selector.response_row != 1u)
            return driver_pilot_failure(__LINE__);
        selector.response_selector_index = 13u;
        if (portable_driver_response_row_select(&selector, 0u,&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            selector.response_slot != 0u || selector.response_row != 3u)
            return driver_pilot_failure(__LINE__);
        if (portable_driver_response_row_select(
                &selector, UINT16_C(0x0010),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            selector.response_slot != 2u || selector.response_row != 7u)
            return driver_pilot_failure(__LINE__);
        selector.response_flags = UINT16_C(0x0002);
        if (portable_driver_response_row_select(&selector, 0u,&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            selector.response_slot != 1u || selector.response_row != 1u)
            return driver_pilot_failure(__LINE__);
        selector.state_flags = UINT32_C(0x20);
        if (portable_driver_response_row_select(&selector, 0u,&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            selector.response_slot != 2u || selector.response_row != 0u)
            return driver_pilot_failure(__LINE__);
        selector.state_flags = 0u;
        selector.response_flags = 0u;
        selector.response_selector_index = 16u;
        if (portable_driver_response_row_select(&selector, 0u,&response_rules) !=
                PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT ||
            portable_driver_response_row_select(NULL, 0u,&response_rules) !=
                PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT)
            return driver_pilot_failure(__LINE__);
    }

    {
        /* Paired Beach1897: row0 enters14, while the prior ordinary
         * row2 neutral exit returns to0. Case14 clears next turn. */
        PortableDriverChargedExitEvent status14_event;
        PortableDriverSteeringState water = {
            .speed_q16 = INT32_C(0x03070000),
            .control_word = UINT16_C(0x8000),
            .response_status = 1u,
            .response_row = 0u,
        };
        if (portable_driver_source_left_turn_response_tick(
                &water, INT32_C(0x031f0000),&response_rules) != PORTABLE_DRIVER_STEERING_OK ||
            water.response_status != 14u ||
            portable_driver_source_status14_tick(&water, INT32_C(0x031f0000), 1, 1,&response_rules, &status14_event) !=
                PORTABLE_DRIVER_STEERING_OK || water.response_status != 0u)
            return driver_pilot_failure(__LINE__);
        water.response_status = 1u;
        water.response_row = 2u;
        if (portable_driver_source_left_turn_response_tick(
                &water, INT32_C(0x031f0000),&response_rules) != PORTABLE_DRIVER_STEERING_OK ||
            water.response_status != 0u)
            return driver_pilot_failure(__LINE__);
        /* Direct original frame-6185 8E34 capture: the unarmed row-2
         * branch still relaxes the steering angle before lap wrap. */
        PortableDriverSteeringState turn = {
            .speed_q16 = INT32_C(0x02580000),
            .control_word = UINT16_C(0x8000),
            .response_row = 2u,
            .base_angle = (-INT16_C(160)),
        };
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x03220000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.response_status != 0u ||
            turn.base_angle != (-INT16_C(64)) ||
            turn.position_82 != 0u)
            return driver_pilot_failure(__LINE__);
        /* The frame-3511 row-4 call crosses zero, so the source preserves
         * +0x80=0x20 rather than clamping it to zero. */
        turn.speed_q16 = INT32_C(0x03050000);
        turn.response_row = 4u;
        turn.base_angle = INT16_C(0x0020);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.base_angle != INT16_C(0x0020))
            return driver_pilot_failure(__LINE__);
    }

    {
        /* Ribbon frame363: B+Right relaxes row7 angle by480, whereas
         * A+Right actively countersteers by640. R needs speed >=448. */
        const uint16_t controls[] = {0x4100, 0x0100, 0x8110, 0x0110,
                                     0x0110, 0x8300};
        const int32_t speeds[] = {1094*65536, 1094*65536, 1094*65536,
                                  0x01bfffff, 0x01c00000, 1094*65536};
        const int16_t angles[] = {1216, 1216, 1056, 1216, 1056, 2048};
        for (unsigned i = 0; i < sizeof(controls)/sizeof(controls[0]); ++i)
        {
            PortableDriverSteeringState turn = {
                .speed_q16 = speeds[i], .control_word = controls[i],
                .response_status = 1u, .response_row = 7u,
                .base_angle = 1696,
            };
            if (portable_driver_source_left_turn_response_tick(&turn,
                    1094*65536,&response_rules) != PORTABLE_DRIVER_STEERING_OK ||
                turn.base_angle != angles[i])
                return driver_pilot_failure(__LINE__);
        }
    }

    {
        /* Original A+Left GP call captures at frames 1,877 and 1,878:
         * row 4 enters status 1, then advances +0x80/+0x82/+0xc8. */
        PortableDriverSteeringState turn = {
            .speed_q16 = INT32_C(0x02a00000),
            .control_word = UINT16_C(0x8200),
            .response_row = 4u,
            .response_q8 = (-INT16_C(2502)),
        };
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.response_status != 1u || turn.base_angle != 0 ||
            turn.position_82 != 0u)
            return driver_pilot_failure(__LINE__);
        turn.speed_q16 = INT32_C(0x02900000);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.base_angle != INT16_C(0x00c0) ||
            turn.position_82 != UINT16_C(0x0150) ||
            turn.drift_angle != UINT16_C(0xff00))
            return driver_pilot_failure(__LINE__);
    }

    {
        /* Original GP right-turn sub_8038E34 frame 3,059 enters status 1
         * with zero angle/position/drift; the next call stays on the
         * negative-angle branch. */
        PortableDriverSteeringState turn = {
            .speed_q16 = INT32_C(0x02460000),
            .control_word = UINT16_C(0x8100),
            .response_status = 1u,
            .response_row = 4u,
        };
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x03200000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.base_angle != (-INT16_C(256)) ||
            turn.position_82 != UINT16_C(0xfeb0) ||
            turn.drift_angle != UINT16_C(0xfe20) ||
            turn.response_status != 1u)
            return driver_pilot_failure(__LINE__);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x03200000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.base_angle != (-INT16_C(448)) ||
            turn.position_82 != UINT16_C(0xfd60) ||
            turn.drift_angle != UINT16_C(0xff20) ||
            turn.response_status != 1u)
            return driver_pilot_failure(__LINE__);
    }

    {
        /* Original straight GP frame 1,950 arms status 1 with +0x96=0x8000.
         * A released-left frame with zero response returns to 0; the next
         * held-left status-1 frame begins row-2 +0x80/+0x82 movement. */
        PortableDriverSteeringState turn = {
            .speed_q16 = INT32_C(0x02ff0000),
            .control_word = UINT16_C(0x8000),
            .response_row = 2u,
        };
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK || turn.response_status != 0u)
            return driver_pilot_failure(__LINE__);
        turn.speed_q16 = INT32_C(0x031f0000);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.response_status != 1u || turn.base_angle != 0)
            return driver_pilot_failure(__LINE__);
        turn.speed_q16 = INT32_C(0x031d0000);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK || turn.response_status != 0u)
            return driver_pilot_failure(__LINE__);
        turn.speed_q16 = INT32_C(0x031f0000);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK || turn.response_status != 1u)
            return driver_pilot_failure(__LINE__);
        turn.speed_q16 = INT32_C(0x031d0000);
        turn.control_word = UINT16_C(0x8200);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK || turn.response_status != 1u ||
            turn.base_angle != INT16_C(0x0040) ||
            turn.position_82 != UINT16_C(0x0110))
            return driver_pilot_failure(__LINE__);
    }

    {
        /* Original bend release at frame 1,981: row 2 relaxes all three
         * status-1 steering fields on the first A-only frame. */
        PortableDriverSteeringState turn = {
            .speed_q16 = INT32_C(0x031d0000),
            .control_word = UINT16_C(0x8000),
            .response_status = 1u,
            .response_row = 2u,
            .base_angle = INT16_C(0x0780),
            .position_82 = UINT16_C(0x1dc0),
            .drift_angle = UINT16_C(0xe5c0),
        };
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK || turn.response_status != 1u ||
            turn.base_angle != INT16_C(0x0720) ||
            turn.position_82 != UINT16_C(0x1cb0) ||
            turn.drift_angle != UINT16_C(0xe780))
            return driver_pilot_failure(__LINE__);
    }

    {
        /* Original GP sub_8038E34 call at frame 2,542: the signed
         * +0xc8 counter crosses -0x7a00 and enters status 8. */
        PortableDriverSteeringState turn = {
            .speed_q16 = INT32_C(0x02d40000),
            .control_word = UINT16_C(0x8200),
            .response_status = 1u,
            .response_row = 4u,
            .response_flags = UINT16_C(0x0024),
            .response_q8 = (-INT16_C(2502)),
            .position_82 = UINT16_C(0x1d10),
            .base_angle = INT16_C(0x1980),
            .drift_angle = UINT16_C(0x86a0),
        };
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.response_status != 8u ||
            turn.drift_angle != UINT16_C(0x85a0) ||
            turn.position_82 != UINT16_C(0x1bc0) ||
            turn.base_angle != INT16_C(0x1a40) ||
            turn.response_q8 != 0 ||
            turn.response_flags != UINT16_C(0x0024))
            return driver_pilot_failure(__LINE__);
        /* The following original status-8 call advances +0x82 and
         * leaves the signed drift counter unchanged. */
        turn.speed_q16 = INT32_C(0x02d40000);
        turn.position_82 = UINT16_C(0x1bc0);
        turn.response_flags = UINT16_C(0x0024);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.response_status != 8u ||
            turn.position_82 != UINT16_C(0x2040) ||
            turn.response_flags != UINT16_C(0x0008) ||
            turn.drift_angle != UINT16_C(0x85a0))
            return driver_pilot_failure(__LINE__);
        /* Direct frame-2565 capture takes the low-speed +0x82 branch. */
        turn.speed_q16 = INT32_C(0x01740000);
        turn.position_82 = UINT16_C(0x7ec0);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.position_82 != UINT16_C(0x8340) ||
            turn.response_status != 8u ||
            turn.response_flags != UINT16_C(0x0008))
            return driver_pilot_failure(__LINE__);
        /* Direct frame-2932 capture wraps +0x82 and enters status 14. */
        turn.speed_q16 = 0;
        turn.position_82 = UINT16_C(0xfde0);
        if (portable_driver_source_left_turn_response_tick(
                &turn, INT32_C(0x031e0000),&response_rules) !=
                PORTABLE_DRIVER_STEERING_OK ||
            turn.position_82 != UINT16_C(0x0260) ||
            turn.response_status != 14u)
            return driver_pilot_failure(__LINE__);
    }

    {
        static const int16_t response_row_3[8] = {
            -64, 240, 8192, 128, 192, 256, 288, 7424,
        };
        PortableDriverChargedExitEvent event;
        PortableDriverSteeringState exit_state = {
            .speed_q16 = INT32_C(0x036d8000),
            .control_word = UINT16_C(0x8000),
            .base_angle = INT16_C(0x0060),
            .position_82 = UINT16_C(0x04c0),
            .response_status = 1u,
            .response_flags = UINT16_C(0x0040),
            .drift_angle = UINT16_C(0xc550),
        };
        if (portable_driver_charged_steering_exit_tick(
                &exit_state, INT32_C(0x04400000), response_row_3,
                1, 1, &event) != PORTABLE_DRIVER_STEERING_OK ||
            exit_state.position_82 != UINT16_C(0x03a0) ||
            exit_state.base_angle != INT16_C(0x0060) ||
            exit_state.drift_angle != UINT16_C(0xc720) ||
            exit_state.response_flags != 0u ||
            exit_state.response_status != 1u || event.audio_cue_140 ||
            event.particle_effect || event.strong_exit)
            return driver_pilot_failure(__LINE__);
        exit_state.speed_q16 = INT32_C(0x036f0000);
        if (portable_driver_charged_steering_exit_tick(
                &exit_state, INT32_C(0x04400000), response_row_3,
                1, 1, &event) != PORTABLE_DRIVER_STEERING_OK ||
            exit_state.position_82 != UINT16_C(0x0280) ||
            exit_state.base_angle != INT16_C(0x0060) ||
            exit_state.drift_angle != UINT16_C(0xc8f0) ||
            exit_state.response_status != 0u)
            return driver_pilot_failure(__LINE__);

        exit_state = (PortableDriverSteeringState){
            .speed_q16 = INT32_C(0x03c10000),
            .control_word = UINT16_C(0x8000),
            .response_status = 1u,
            .response_flags = UINT16_C(0x0040),
        };
        if (portable_driver_charged_steering_exit_tick(
                &exit_state, INT32_C(0x04400000), response_row_3,
                1, 1, &event) != PORTABLE_DRIVER_STEERING_OK ||
            exit_state.response_status != 14u ||
            exit_state.response_flags != 0u ||
            exit_state.exit_timer != UINT16_C(0x0030) ||
            exit_state.driver_mode != 12u || !event.audio_cue_140 ||
            !event.particle_effect || !event.strong_exit ||
            portable_driver_charged_steering_exit_tick(
                NULL, 0, response_row_3, 0, 0, &event) !=
                PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT)
            return driver_pilot_failure(__LINE__);
    }

    /* Case0 always clears C8 and calls9690 at low speed. Its signed
     * approaches and charged event must use the same dispatch as case1. */
    for (unsigned negative = 0; negative < 2u; ++negative)
    {
        PortableDriverChargedExitEvent event;
        PortableDriverSteeringState low = {
            .speed_q16 = INT32_C(0x00ffffff), .response_row = 2u,
            .base_angle = negative ? -160 : 160,
            .position_82 = negative ? (uint16_t)-400 : 400u,
            .drift_angle = 1234u, .response_flags = 0x2cu,
        };
        if (portable_driver_source_turn_response_tick(&low,INT32_C(0x03000000),&response_rules,
                &event) != 0 || low.drift_angle != 0u ||
            low.base_angle != (negative ? -64 : 64) ||
            low.position_82 != (negative ? (uint16_t)-128 : 128u) ||
            low.response_flags != 0u || low.response_status != 0u ||
            event.strong_exit)
            return driver_pilot_failure(__LINE__);
    }
    {
        PortableDriverChargedExitEvent event;
        PortableDriverSteeringState release = {
            .speed_q16=INT32_C(0x02800000), .response_row=2u,
            .response_flags=0x40u, .drift_angle=1234u,
        };
        if (portable_driver_source_turn_response_tick(&release,INT32_C(0x03000000),&response_rules,
                &event) != 0 || !event.strong_exit || !event.audio_cue_140 ||
            !event.particle_effect || release.response_status != 14u ||
            release.driver_mode != 12u || release.exit_timer != 48u ||
            release.drift_angle != 0u)
            return driver_pilot_failure(__LINE__);
    }

    memset(cells, 0x7f, sizeof(cells));
    local_table[0] = 0x0900;
    sine_table[0] = 0x4000;
    sine_table[256] = -0x4000;
    state.input.speed_parameter = 5;
    state.auto_acceleration.driver_mode = 0;
    state.auto_acceleration.character_class = 3;
    state.forward_acceleration.state_flags = 0;
    state.motion.angle = 0;
    repeat = state;

    /* 40810 accepts a low hop, skips a high hop/protected driver, and
     * selects the fixed 4073C escape when the contact timer reaches one. */
    {
        for (unsigned scenario = 0; scenario < 4u; ++scenario)
        {
            PortableDriverPilotState contact = state;
            contact.source_update_order = 1u;
            contact.normal_collision_source_from_native_state = 1u;
            contact.normal_collision_source_use_b550_table = 1u;
            contact.pose.position_x = INT32_C(0x000c0000);
            contact.hop.mode = 1u;
            contact.hop.height_q16 = scenario == 1u
                ? INT32_C(0x50000) : INT32_C(0x30000);
            contact.pose.position_z = contact.hop.height_q16;
            contact.invulnerability_timer_6a = scenario == 2u ? 1u : 0u;
            contact.normal_collision_timer_38 = scenario == 3u ? 1u : 0u;
            material_flags[0] = UINT8_C(0x90);
            sine_table[0] = 0;
            sine_table[256] = 32768;
            if (portable_driver_pilot_tick(&contact, UINT16_C(0x8000),
                    &dependencies) != PORTABLE_DRIVER_PILOT_OK)
                return driver_pilot_failure(__LINE__);
            if (scenario == 0u || scenario == 3u)
            {
                if (contact.normal_collision_timer_38 != 7u ||
                    !contact.normal_collision_pending ||
                    contact.vector_effect.contact_word_5a != UINT16_C(0x8007) ||
                    (scenario == 3u && (contact.motion.component_x != 0 ||
                     contact.motion.component_y != -512)))
                    return driver_pilot_failure(__LINE__);
            }
            else if (contact.normal_collision_pending ||
                     contact.normal_collision_timer_38 != 0u)
                return driver_pilot_failure(__LINE__);
        }
        /* Known static owners suppress repeated hits while flying/removed.
         * Guard66 and mode6 skip both protected damage and ordinary bounce. */
        for(unsigned scenario=0;scenario<8u;++scenario){
            PortableCourseProp initial;
            if(!portable_course_prop_init(&initial,12*65536,4*65536,
                scenario==7u?0xa0004u:0xc0000u,224u,0u))return driver_pilot_failure(__LINE__);
            PortableCourseProps props;
            if(!portable_course_props_init(&props,&initial,1u))return driver_pilot_failure(__LINE__);
            PortableDriverPilotDependencies prop_deps=dependencies;
            prop_deps.course_props=&props;
            PortableDriverPilotState hit=state;
            hit.source_update_order=1u;hit.pose.position_x=12*65536;
            hit.invulnerability_timer_6a=scenario<=3u?1u:0u;
            hit.contact_guard_66=scenario==1u?1u:0u;
            hit.hop.mode=scenario==2u?6u:0u;
            if(scenario==2u){hit.source_hazard_enabled=1u;hit.source_hazard_kind=PORTABLE_DRIVER_HAZARD_VOID;}
            if(scenario==3u || scenario==5u || scenario==6u){
                portable_course_prop_damage(&props.actors[0]);
                if(scenario==6u)props.actors[0].flags=0u;
            }
            const int prop_result=portable_driver_pilot_tick(&hit,0x8000u,&prop_deps);
            if(prop_result!=0)return driver_pilot_failure(__LINE__);
            if(scenario==0u){
                if(props.actors[0].state!=1u || props.actors[0].timer!=32 ||
                   props.actors[0].affine_y_q8!=-256 || hit.normal_collision_pending ||
                   hit.contact_audio.song_id!=160u || hit.contact_audio.serial!=1u)return driver_pilot_failure(__LINE__);
            }else if(scenario==4u || scenario==7u){
                if(hit.normal_collision_timer_38!=7u || hit.vector_effect.contact_word_5a!=0x8007u || props.actors[0].state ||
                   (scenario==4u && hit.contact_audio.song_id!=170u) ||
                   (scenario==7u && hit.contact_audio.serial))return driver_pilot_failure(__LINE__);
            }else if(hit.normal_collision_pending || hit.contact_audio.serial ||
                ((scenario==1u || scenario==2u) && props.actors[0].state))return driver_pilot_failure(__LINE__);
        }
        /* 56170/55FC0 beach props toggle on first contact, with their own
         * speed-scaled sound. Escape, guards, removal and height do not toggle. */
        for(unsigned scenario=0u;scenario<8u;++scenario){
            PortableCourseProp initial;
            if(!portable_course_prop_init(&initial,12*65536,4*65536,0x860000u,256u,0u))return driver_pilot_failure(__LINE__);
            initial.contact_phase=(uint8_t)(scenario==1u || scenario==5u);
            PortableCourseProps props;
            if(!portable_course_props_init(&props,&initial,1u))return driver_pilot_failure(__LINE__);
            PortableDriverPilotDependencies prop_deps=dependencies;prop_deps.course_props=&props;
            PortableDriverPilotState hit=state;
            hit.source_update_order=1u;hit.pose.position_x=12*65536;
            if(scenario==2u)hit.normal_collision_timer_38=1u;
            if(scenario==3u)hit.contact_guard_66=1u;
            if(scenario==4u)hit.pose.position_z=hit.hop.height_q16=0x40001;
            if(scenario==5u)hit.invulnerability_timer_6a=1u;
            if(scenario==6u){props.actors[0].state=1u;props.actors[0].flags=0u;}
            if(scenario==7u)hit.steering.state_flags|=0x20u;
            if(portable_driver_pilot_tick(&hit,0x8000u,&prop_deps)!=0)return driver_pilot_failure(__LINE__);
            const uint8_t phase=(uint8_t)(scenario==0u || scenario==5u || scenario==7u);
            if(props.actors[0].contact_phase!=phase)return driver_pilot_failure(__LINE__);
            const uint16_t song=scenario==0u?376u:scenario==1u?375u:scenario==5u?160u:0u;
            if(hit.contact_audio.song_id!=song || hit.contact_audio.serial!=(unsigned)(song!=0u))return driver_pilot_failure(__LINE__);
            if(scenario==5u && (props.actors[0].state!=1u || props.actors[0].timer!=32))return driver_pilot_failure(__LINE__);
        }
        material_flags[0] = 4;
        sine_table[0] = 0x4000;
        sine_table[256] = -0x4000;
    }

    {
        PortableDriverPilotDependencies selector_dependencies = dependencies;
        PortableDriverPilotState selector_state = state;

        selector_dependencies.terrain_effect_tables = NULL;
        material_flags[0] = UINT8_C(0x5a);
        if (portable_driver_pilot_tick(
                &selector_state, UINT16_C(0x8000),
                &selector_dependencies) != PORTABLE_DRIVER_PILOT_OK ||
            selector_state.forward_limit_selector != 13u ||
            selector_state.steering.response_selector_index != 13u ||
            selector_state.steering.response_selector_flags != 3u ||
            selector_state.steering.response_slot != 0u ||
            selector_state.steering.response_row != 3u)
            return driver_pilot_failure(__LINE__);
        /* A wall/object byte and an airborne road sample preserve the
         * terrain selector used by the response and acceleration tables. */
        for (unsigned byte = 0x80; byte <= 0xff; ++byte)
        {
            selector_state = state;
            selector_state.forward_limit_selector = 13u;
            material_flags[0] = (uint8_t)byte;
            if (portable_driver_pilot_tick(&selector_state, UINT16_C(0x8000),
                    &selector_dependencies) != PORTABLE_DRIVER_PILOT_OK ||
                selector_state.forward_limit_selector != 13u ||
                selector_state.steering.response_selector_index != 13u)
                return driver_pilot_failure(__LINE__);
        }
        selector_state = state;
        selector_state.hop.mode = 1u;
        selector_state.hop.height_q16 = INT32_C(0x30000);
        selector_state.pose.position_z = INT32_C(0x30000);
        selector_state.forward_limit_selector = 13u;
        material_flags[0] = UINT8_C(0x40);
        if (portable_driver_pilot_tick(&selector_state, UINT16_C(0x8000),
                &selector_dependencies) != PORTABLE_DRIVER_PILOT_OK ||
            selector_state.forward_limit_selector != 13u)
            return driver_pilot_failure(__LINE__);
        material_flags[0] = UINT8_C(4);
    }

    /* 8E34/DB84 writes turn flags before 8AFC reads the same driver. The
     * separated native acceleration copy must not erase the new bit4. */
    {
        PortableDriverPilotState turn = {0};
        PortableDriverPilotDependencies turn_dependencies = dependencies;
        turn_dependencies.steering_response_data = steering_response_data;
        turn_dependencies.steering_response_data_count = 4u;
        turn.source_update_order = 1u;
        turn.input.speed_parameter = 5u;
        turn.input.base_speed_q16 = INT32_C(0x032c0000);
        turn.auto_acceleration.character_class = 2u;
        turn.steering.state_flags = UINT32_C(0x8000);
        turn.steering.response_status = 1u;
        turn.steering.position_82 = UINT16_C(0x0d20);
        turn.steering.base_angle = INT16_C(0x05e0);
        turn.motion.speed_q16 = INT32_C(0x03000000);
        material_flags[0] = UINT8_C(0x40);
        const int result = portable_driver_pilot_tick(
            &turn, UINT16_C(0x8200), &turn_dependencies);
        material_flags[0] = UINT8_C(4);
        if (result != PORTABLE_DRIVER_PILOT_OK ||
            turn.steering.response_flags != UINT16_C(4) ||
            turn.hop.animation_flags != UINT16_C(4) ||
            turn.forward_acceleration.state_flags != UINT16_C(4))
        {
            fprintf(stderr, "source turn flags lost: result=%d flags=%04x\n",
                    result, turn.steering.response_flags);
            return driver_pilot_failure(__LINE__);
        }
    }

    /* 8298 mode1 retains only fractional acceleration through airborne
     * throttle/brake/release. Prior C0 is consumed before the writer, and
     * reverse airborne speed bypasses the ground stop cleanup too, including
     * each spin status's low-speed wrap exit to14 in the same airborne turn. */
    for (unsigned reverse=0;reverse<2u;++reverse)
    for (unsigned buttons=0;buttons<3u;++buttons)
    for (unsigned response=0;response<5u;++response)
    {
        const uint16_t controls[3]={0x8000u,0x4000u,0u};
        PortableDriverPilotState jump={0};
        jump.source_update_order=1u;
        jump.input.speed_parameter=5u;
        jump.input.base_speed_q16=INT32_C(0x03000000);
        jump.auto_acceleration.character_class=2u;
        jump.steering.state_flags=UINT32_C(0x8800);
        jump.steering.driver_mode=1u;
        jump.steering.response_status=response? (uint8_t)(response+4u):0u;
        jump.steering.position_82=response==2u?0xf600u:response==4u?0xfc00u:0u;
        jump.auto_acceleration.driver_mode=1u;
        jump.hop.mode=1u;
        jump.hop.height_q16=INT32_C(0x50000);
        jump.pose.position_z=jump.hop.height_q16;
        jump.motion.speed_q16=reverse?-INT32_C(0x01400000):INT32_C(0x01400000);
        jump.motion.acceleration_q16=INT32_C(0x00101234);
        const int32_t expected=jump.motion.speed_q16+jump.motion.acceleration_q16;
        if (portable_driver_pilot_tick(&jump,controls[buttons],&dependencies) != 0 ||
            jump.motion.speed_q16 != expected || jump.motion.acceleration_q16 != 0x1234 ||
            jump.auto_acceleration.acceleration_q16 != 0x1234 ||
            jump.forward_acceleration.acceleration_q16 != 0x1234 ||
            jump.brake.acceleration_q16 != 0x1234 || jump.hop.mode != 1u ||
            jump.steering.driver_mode != 1u ||
            (response && jump.steering.response_status != 14u))
        {
            fprintf(stderr,"airborne inertia: reverse=%u controls=%04x response=%u speed=%d expected=%d accel=%d\n",
                reverse,controls[buttons],response,jump.motion.speed_q16,expected,
                jump.motion.acceleration_q16);
            return driver_pilot_failure(__LINE__);
        }
    }

    /* 38854/DB4C: lightning/input lock cap throttle at the engine byte;
     * signed negative differences wrap the eight-entry response index. */
    for (unsigned lock=0u;lock<3u;++lock) {
        uint8_t engine[512]={0};engine[0x8f]=32u;
        const int16_t response[8]={-2,-8,-16,-24,-32,-40,-48,-56};
        PortableDriverPilotDependencies effects=dependencies;
        effects.source_angle_target_table=engine;
        effects.source_angle_target_table_count=sizeof engine;
        effects.forward_target_response_table=response;
        effects.forward_target_response_count=8u;
        PortableDriverPilotState shrink={0};
        shrink.source_update_order=1u;shrink.preserve_source_base_speed=1u;
        shrink.input.base_speed_q16=1094*65536;
        shrink.input.speed_parameter=5u;
        shrink.steering.state_flags=0x8000u;
        shrink.lightning_timer_68=lock==0u?100:lock==2u?-1:0;
        shrink.item_lock_ac=lock==1u?10u:0u;
        shrink.motion.speed_q16=(lock==2u?967:512)*65536;
        if (portable_driver_pilot_tick(&shrink,0x8000u,&effects)!=0 ||
            shrink.motion.acceleration_q16!=-48*65536) {
            fprintf(stderr,"shrink throttle lock=%u speed=%d target=%d acceleration=%d\n",
                lock,shrink.motion.speed_q16,shrink.input.speed_target_q16,shrink.motion.acceleration_q16);
            return driver_pilot_failure(__LINE__);
        }
    }

    /* A fixed Time Trial target survives boost expiry; the acceleration
     * adapter must observe the same cleared84 before ordinary terrain caps. */
    {
        int16_t limits[16];
        for (unsigned i=0;i<16u;++i) limits[i]=-1;
        PortableDriverPilotDependencies speed_dependencies=dependencies;
        speed_dependencies.forward_limit_thresholds=limits;
        speed_dependencies.forward_limit_threshold_count=16u;
        PortableDriverPilotState boost={0};
        boost.source_update_order=1u;
        boost.preserve_source_base_speed=1u;
        boost.input.base_speed_q16=1094*65536;
        boost.input.speed_parameter=5u;
        boost.steering.state_flags=0x8000u;
        boost.steering.driver_mode=8u;
        boost.steering.exit_timer=1u;
        boost.auto_acceleration.driver_mode=8u;
        boost.motion.speed_q16=800*65536;
        for (unsigned tick=0;tick<2u;++tick)
            if (portable_driver_pilot_tick(&boost,0x8000u,&speed_dependencies)!=0 ||
                boost.steering.driver_mode!=0u || boost.auto_acceleration.driver_mode!=0u ||
                boost.input.speed_target_q16!=1094*65536) return driver_pilot_failure(__LINE__);
    }

    /* A charged neutral release enters14 once, instead of immediately
     * dispatching case14 and clearing back to0 in the same driver turn. */
    {
        PortableDriverPilotState release = {0};
        release.source_update_order=1u;
        release.input.speed_parameter=5u;
        release.input.base_speed_q16=INT32_C(0x03000000);
        release.auto_acceleration.character_class=2u;
        release.steering.state_flags=UINT32_C(0x8000);
        release.steering.response_status=1u;
        release.steering.response_flags=0x40u;
        release.hop.animation_flags=0x40u;
        release.forward_acceleration.state_flags=0x40u;
        release.motion.speed_q16=INT32_C(0x02800000);
        material_flags[0]=0x40u;
        const int result=portable_driver_pilot_tick(&release,0x8000u,&dependencies);
        material_flags[0]=4u;
        if (result != 0 || release.steering.response_status != 14u ||
            release.steering.driver_mode != 12u || release.steering.exit_timer != 47u ||
            (release.motion.acceleration_q16 >> 16) != 3 ||
            release.charged_exit_count != 1u || release.charged_audio_cue_count != 1u ||
            release.charged_particle_count != 1u || release.charged_exit_active)
        {
            fprintf(stderr,"charged dispatch: result=%d status=%u mode=%u exits=%u row=%u\n",
                result,release.steering.response_status,release.steering.driver_mode,
                release.charged_exit_count,release.steering.response_row);
            return driver_pilot_failure(__LINE__);
        }
    }

    /* E002 bounded integration: explicit source selector/table state only.
     * Equality falls back to the existing local-table helper; the next
     * captured speed takes the high-speed branch. B retains source priority. */
    {
        PortableDriverPilotDependencies limit_dependencies=dependencies;
        PortableDriverPilotState limit={0};
        limit_dependencies.forward_limit_thresholds=limit_thresholds;
        limit_dependencies.forward_limit_threshold_count=16;
        limit.source_update_order=1;
        limit.input.speed_parameter=5;
        limit.auto_acceleration.driver_mode=1;
        limit.auto_acceleration.character_class=2;
        limit.forward_limit_selector=13;
        limit.forward_acceleration.response_q8=0x0900;
        limit.motion.speed_q16=INT32_C(0x02800000);
        local_table[10]=0x1200;
        if(portable_driver_pilot_tick(&limit,UINT16_C(0x8000),
                                      &limit_dependencies)!=
               PORTABLE_DRIVER_PILOT_OK ||
           limit.motion.acceleration_q16!=INT32_C(0x00120000))
            return driver_pilot_failure(__LINE__);
        limit=(PortableDriverPilotState){0};
        limit.source_update_order=1;
        limit.input.speed_parameter=5;
        limit.auto_acceleration.driver_mode=1;
        limit.auto_acceleration.character_class=2;
        limit.forward_limit_selector=13;
        limit.forward_acceleration.response_q8=0x0900;
        limit.motion.speed_q16=INT32_C(0x02920000);
        if(portable_driver_pilot_tick(&limit,UINT16_C(0x8000),
                                      &limit_dependencies)!=
               PORTABLE_DRIVER_PILOT_OK ||
           limit.motion.acceleration_q16!=-INT32_C(0x00100000) ||
           limit.forward_acceleration.response_q8!=0x0900)
            return driver_pilot_failure(__LINE__);
        limit=(PortableDriverPilotState){0};
        limit.source_update_order=1;
        limit.input.speed_parameter=5;
        limit.auto_acceleration.driver_mode=1;
        limit.forward_limit_selector=UINT8_MAX;
        limit.motion.speed_q16=INT32_C(0x02920000);
        if(portable_driver_pilot_tick(&limit,UINT16_C(0xc000),
                                      &limit_dependencies)!=
               PORTABLE_DRIVER_PILOT_OK)
            return driver_pilot_failure(__LINE__);
        limit=(PortableDriverPilotState){0};
        limit.source_update_order=1;
        limit.input.speed_parameter=5;
        /* A response can clear the shared source mode byte after a boost.
         * A stale acceleration copy must not reject the ordinary A branch. */
        limit.auto_acceleration.driver_mode=8;
        limit.steering.driver_mode=0;
        limit.motion.speed_q16=INT32_C(0x02920000);
        if(portable_driver_pilot_tick(&limit,UINT16_C(0x8000),
                                      &limit_dependencies)!=
               PORTABLE_DRIVER_PILOT_OK ||
           limit.auto_acceleration.driver_mode!=0u)
            return driver_pilot_failure(__LINE__);
        /* Beach1897: the source consumes prior -24, enters14 from row0,
         * then writes local +2 despite a lower terrain speed threshold. */
        limit=(PortableDriverPilotState){0};
        limit.source_update_order=1;
        limit.input.speed_parameter=5;
        limit.auto_acceleration.driver_mode=0;
        limit.forward_limit_selector=14;
        limit.steering.response_status=1u;
        limit.motion.speed_q16=INT32_C(0x031f0000);
        limit.motion.acceleration_q16=-INT32_C(0x00180000);
        limit_thresholds[14]=512;
        local_table[12]=0x0200;
        if(portable_driver_pilot_tick(&limit,UINT16_C(0x8000),
                                      &limit_dependencies)!=
               PORTABLE_DRIVER_PILOT_OK ||
           limit.steering.response_row!=0u ||
           limit.steering.response_status!=14u ||
           limit.motion.speed_q16!=INT32_C(0x03070000) ||
           limit.motion.acceleration_q16!=INT32_C(0x00020000))
            return driver_pilot_failure(__LINE__);
        local_table[12]=0;
        limit_thresholds[14]=864;
        local_table[10]=0;
    }

    if (portable_driver_pilot_tick(&state, UINT16_C(0x8000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        state.input.control_word != UINT16_C(0x8000) ||
        state.motion.speed_q16 != INT32_C(0x00090000) ||
        state.motion.acceleration_q16 != INT32_C(0x00090000) ||
        state.motion.component_x != 4 || state.motion.component_y != 4 ||
        state.pose.position_x != 0 || state.pose.position_y != 0 ||
        state.pose.sampled_world_x != 0 || state.pose.sampled_world_y != 0 ||
        state.pose.track_cell_valid != 1 || state.pose.track_cell != 0x7f)
    {
        return driver_pilot_failure(__LINE__);
    }

    if (portable_driver_pilot_tick(&state, UINT16_C(0x8000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        state.motion.speed_q16 != INT32_C(0x00120000) ||
        state.motion.component_x != 9 || state.motion.component_y != 9 ||
        state.pose.position_x != INT32_C(0x00000400) ||
        state.pose.position_y != INT32_C(0x00000400))
    {
        return driver_pilot_failure(__LINE__);
    }

    if (portable_driver_pilot_tick(&repeat, UINT16_C(0x8000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        repeat.motion.speed_q16 != INT32_C(0x00090000) ||
        repeat.pose.position_x != 0 || repeat.pose.position_y != 0)
    {
        return driver_pilot_failure(__LINE__);
    }

    /* The source driver consumes the previous acceleration before computing
     * the current control's next value. Keep this opt-in so the accepted
     * same-tick pilot contract above remains stable. */
    {
        PortableDriverPilotState source_frame_state = {0};
        const int16_t saved_auto_table = auto_table[24];

        source_frame_state.source_update_order = 1;
        source_frame_state.input.speed_parameter = 5;
        source_frame_state.auto_acceleration.driver_mode = 0;
        source_frame_state.auto_acceleration.character_class = 3;
        local_table[0] = 0x0300;
        auto_table[24] = -2;
        if (portable_driver_pilot_tick(&source_frame_state,
                                       UINT16_C(0x8000), &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            source_frame_state.motion.speed_q16 != 0 ||
            source_frame_state.motion.acceleration_q16 !=
                INT32_C(0x00030000) ||
            source_frame_state.pose.position_x != 0 ||
            source_frame_state.pose.position_y != 0 ||
            portable_driver_pilot_tick(&source_frame_state, 0,
                                       &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            source_frame_state.motion.speed_q16 != INT32_C(0x00030000) ||
            source_frame_state.motion.acceleration_q16 !=
                -INT32_C(0x00020000))
        {
            return driver_pilot_failure(__LINE__);
        }
        auto_table[24] = saved_auto_table;
        local_table[0] = 0x0900;
    }

    /* V01-C03 source cleanup in sub_8038298. The inner no-input table write
     * remains -2, but speed+acceleration <= 0 clears both scalar fields.
     * Components came from the earlier sub_8038200 stage and are not erased. */
    {
        PortableDriverPilotState rest = {0};
        PortableDriverPilotState crossing = {0};
        const int16_t saved_auto_table = auto_table[24];

        auto_table[24] = -2;
        rest.source_update_order = 1u;
        rest.input.speed_parameter = 5;
        rest.auto_acceleration.character_class = 3u;
        if (portable_driver_pilot_tick(&rest, 0u, &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            rest.input.control_word != 0u ||
            rest.motion.speed_q16 != 0 ||
            rest.motion.acceleration_q16 != 0 ||
            rest.auto_acceleration.acceleration_q16 != 0)
            return driver_pilot_failure(__LINE__);

        crossing.source_update_order = 1u;
        crossing.input.speed_parameter = 5;
        crossing.auto_acceleration.character_class = 3u;
        crossing.motion.speed_q16 = INT32_C(0x00010000);
        crossing.motion.acceleration_q16 = -INT32_C(0x00040000);
        if (portable_driver_pilot_tick(&crossing, 0u, &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            crossing.motion.speed_q16 != 0 ||
            crossing.motion.acceleration_q16 != 0 ||
            crossing.motion.component_x != -1 ||
            crossing.motion.component_y != -1)
            return driver_pilot_failure(__LINE__);
        auto_table[24] = saved_auto_table;
    }

    /* A2B4 pad contact reaches the shared mode-8 acceleration consumer in
     * the real pilot update, including refresh on a second grounded tick. */
    {
        PortableDriverPilotState pad = {0};
        pad.source_update_order = 1u;
        pad.input.speed_parameter = 5;
        pad.auto_acceleration.character_class = 3u;
        material_flags[0] = 6u;
        if (portable_driver_pilot_tick(&pad, UINT16_C(0x8000), &dependencies) != 0 ||
            pad.steering.driver_mode != 8u || pad.steering.exit_timer != 33u ||
            pad.motion.acceleration_q16 != (int32_t)test_mushroom_rules()->acceleration_units*65536)
            return driver_pilot_failure(__LINE__);
        if (portable_driver_pilot_tick(&pad, UINT16_C(0x8000), &dependencies) != 0 ||
            pad.steering.driver_mode != 8u || pad.steering.exit_timer != 33u)
            return driver_pilot_failure(__LINE__);
        material_flags[0] = 4u;
    }

    /* The post-response acceleration dispatch must still apply 8298's
     * stop cleanup. Otherwise a stopped spin integrates backwards next turn. */
    {
        PortableDriverPilotState stopped = {0};
        stopped.source_update_order = 1u;
        stopped.input.speed_parameter = 5;
        stopped.auto_acceleration.character_class = 3u;
        stopped.steering.response_status = 8u;
        for (unsigned tick = 0; tick < 3u; ++tick)
            if (portable_driver_pilot_tick(&stopped, UINT16_C(0x8000), &dependencies) != 0 ||
                stopped.motion.speed_q16 != 0 || stopped.motion.acceleration_q16 != 0 ||
                stopped.motion.component_x != 0 || stopped.motion.component_y != 0)
                return driver_pilot_failure(__LINE__);
    }

    /* C702 keeps9B38 alive during a hold: bounds retrieval must stop
     * speed and clear old components even while8200 is skipped. */
    for (unsigned held = 0; held < 2u; ++held)
    {
        PortableDriverPilotState bounds = {0};
        bounds.source_update_order = 1u;
        bounds.source_hazard_enabled = 1u;
        bounds.source_terrain22_enabled = 1u;
        bounds.source_hazard_kind = PORTABLE_DRIVER_HAZARD_BOUNDS;
        bounds.hop.mode = 5u;
        bounds.steering.driver_mode = 5u;
        bounds.steering.state_flags = held ? 0x4000u : 0u;
        bounds.normal_collision_timer_62 = held ? 2u : 0u;
        bounds.motion.speed_q16 = 0x004e0000;
        bounds.motion.acceleration_q16 = 0x00050000;
        bounds.motion.component_x = 128;
        bounds.motion.component_y = -64;
        bounds.input.speed_parameter = 5;
        if (portable_driver_pilot_tick(&bounds,0x8000u,&dependencies) != 0 ||
            bounds.hop.mode != 5u || bounds.steering.driver_mode != 5u ||
            bounds.motion.speed_q16 != 0 || bounds.motion.acceleration_q16 != 0 ||
            bounds.motion.component_x != 0 || bounds.motion.component_y != 0)
            return driver_pilot_failure(__LINE__);
    }

    /* 384D8 wins over an armed wall response during fall/carry/descent.
     * A reflected speed must not survive into the next heading update. */
    for (unsigned mode = 2u; mode <= 7u; ++mode)
    {
        if (mode != 2u && mode != 6u && mode != 7u) continue;
        PortableDriverPilotState falling = {0};
        falling.source_update_order = 1u;
        falling.source_hazard_enabled = 1u;
        falling.source_hazard_kind = PORTABLE_DRIVER_HAZARD_VOID;
        falling.hop.mode = (uint8_t)mode;
        falling.hop.height_q16 = mode == 2u ? 256 : 0x300000;
        falling.pose.position_z = falling.hop.height_q16;
        falling.drift_charge.charge_counter = 60u;
        falling.input.speed_parameter = 5;
        falling.auto_acceleration.character_class = 3u;
        falling.motion.speed_q16 = 0x004e0000;
        falling.motion.acceleration_q16 = 0x00050000;
        falling.steering.driver_mode = 2u;
        falling.steering.state_flags = 0x1000u;
        falling.normal_collision_pending = 1u;
        falling.normal_collision_source_valid = 1u;
        falling.normal_collision_source_from_native_state = 1u;
        falling.normal_collision_source_use_motion = 1u;
        falling.normal_collision_source_use_b550_table = 1u;
        falling.normal_collision_response_output_pending = 1u;
        falling.normal_collision_response_output_c0 = -0x100000;
        if (portable_driver_pilot_tick(&falling, 0x8200u, &dependencies) != 0 ||
            falling.motion.speed_q16 != 0 || falling.motion.acceleration_q16 != 0 ||
            falling.auto_acceleration.speed_q16 != 0 ||
            falling.forward_acceleration.response_q8 != 0 ||
            falling.steering.response_q8 != 0 || falling.steering.response_status != 0u ||
            falling.steering.base_angle != 0 || falling.steering.position_82 != 0u ||
            falling.normal_collision_pending != 1u ||
            falling.normal_collision_response_output_pending != 0u ||
            falling.fall_frames_76 != (mode==2u?1u:0u) ||
            (mode==2u && falling.pose.position_z!=-32768))
            return driver_pilot_failure(__LINE__);
        if(mode==2u){
            for(unsigned step=2u;step<=15u;++step){
                FINISHED_CHECK(portable_driver_pilot_tick(&falling,0x8200u,&dependencies)==0);
                FINISHED_CHECK(falling.fall_frames_76==step &&
                    falling.pose.position_z==-(int32_t)step*32768 &&
                    falling.hop.height_q16==falling.pose.position_z &&
                    falling.drift_charge.charge_counter==60u-step);
            }
            FINISHED_CHECK(!portable_driver_effects_visible(&falling));
        }
    }

    /* 9B38's airborne branch leaves the scheduled normal response pending;
     * the first later grounded branch consumes it with current components. */
    {
        PortableDriverPilotState jumping = {0};
        jumping.source_update_order = 1u;
        jumping.hop.mode = 1u;
        jumping.hop.height_q16 = 0x100000;
        jumping.pose.position_z = jumping.hop.height_q16;
        jumping.input.speed_parameter = 5;
        jumping.auto_acceleration.character_class = 3u;
        jumping.normal_collision_pending = 1u;
        jumping.normal_collision_source_valid = 1u;
        jumping.normal_collision_source_from_native_state = 1u;
        jumping.normal_collision_source_use_motion = 1u;
        jumping.normal_collision_source_use_b550_table = 1u;
        jumping.steering.state_flags = 0x1000u;
        if (portable_driver_pilot_tick(&jumping, 0x8000u, &dependencies) != 0 ||
            jumping.normal_collision_pending != 1u || jumping.normal_collision_count != 0u)
            return driver_pilot_failure(__LINE__);
        jumping.hop.mode = 0u;
        jumping.hop.height_q16 = 0;
        jumping.pose.position_z = 0;
        if (portable_driver_pilot_tick(&jumping, 0x8000u, &dependencies) != 0 ||
            jumping.normal_collision_pending != 0u || jumping.normal_collision_count != 1u)
            return driver_pilot_failure(__LINE__);
    }

    /* B110 mutates the occupied wall, retains this turn's reflection and
     * exposes floor material on the next pilot turn. High jumps skip it. */
    {
        PortableDriverPilotDependencies walls = dependencies;
        walls.owned_terrain_cells = terrain_cells;
        walls.owned_terrain_capacity = sizeof terrain_cells;
        walls.wall_replacement_tile = 1u;
        material_flags[0] = 0x82u;
        material_flags[1] = 0x40u;
        PortableDriverPilotState hit = {0};
        hit.source_update_order = 1u;
        hit.input.speed_parameter = 5;
        hit.auto_acceleration.character_class = 3u;
        if (portable_driver_pilot_tick(&hit, 0x8000u, &walls) != 0 ||
            terrain_cells[0] != 1u || hit.wall_mutation_count != 1u ||
            !hit.vector_trigger.contact || hit.normal_collision_timer_62 != 8u)
            return driver_pilot_failure(__LINE__);
        if (portable_driver_pilot_tick(&hit, 0x8000u, &walls) != 0 ||
            hit.wall_mutation_count != 1u || hit.vector_trigger.contact)
            return driver_pilot_failure(__LINE__);
        terrain_cells[0] = 0u;
        hit = (PortableDriverPilotState){0};
        hit.source_update_order = 1u;
        hit.pose.position_z = 0x50000;
        hit.hop.mode = 1u;
        if (portable_driver_pilot_tick(&hit, 0u, &walls) != 0 ||
            terrain_cells[0] != 0u || hit.wall_mutation_count != 0u)
            return driver_pilot_failure(__LINE__);
        /* B2B4 precedes the hop: rising across four units still hits
         * and destroys this wall using the entry height. */
        hit = (PortableDriverPilotState){0};
        hit.source_update_order = 1u;
        hit.input.speed_parameter = 5;
        hit.pose.position_z = 0x35d00;
        hit.hop.mode = 1u;
        hit.hop.velocity_q8 = 221;
        hit.motion.speed_q16 = 0x02b48000;
        hit.motion.acceleration_q16 = 0x8000;
        if (portable_driver_pilot_tick(&hit,0x8000u,&walls) != 0 ||
            hit.pose.position_z <= 0x40000 || terrain_cells[0] != 1u ||
            hit.wall_mutation_count != 1u || !hit.vector_trigger.contact ||
            hit.normal_collision_timer_62 != 8u ||
            hit.motion.speed_q16 != 0x02b48000)
            return driver_pilot_failure(__LINE__);
        terrain_cells[0] = 0u;
        hit = (PortableDriverPilotState){0};
        hit.source_update_order = 1u;
        hit.invulnerability_timer_6a = 1u;
        material_flags[0] = 0x84u;
        if (portable_driver_pilot_tick(&hit, 0u, &walls) != 0 ||
            terrain_cells[0] != 1u || hit.wall_mutation_count != 1u ||
            hit.vector_trigger.contact || hit.normal_collision_timer_62 != 0u ||
            hit.contact_audio.serial != 1u || hit.contact_audio.song_id != 124u ||
            hit.contact_audio.volume != 192u || hit.contact_audio.pitch != -512 ||
            !hit.contact_audio.restart)
            return driver_pilot_failure(__LINE__);
        terrain_cells[0] = 0u;
        material_flags[0] = 4u;
    }

    /* B2B4 captures signed pre-response speed; continued contact requests
     * another cue, leaving actual voice-busy suppression to the audio host. */
    {
        PortableDriverPilotDependencies walls = dependencies;
        walls.wall_contact_song = 169u;
        material_flags[0] = 0x80u;
        const struct { int32_t speed; uint16_t volume; int16_t pitch; } cases[] = {
            {0, 192u, -512}, {0x1234567, 210u, -221},
            {-0x1234567, 210u, -221}, {INT32_MIN, 192u, -512},
        };
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        {
            PortableDriverPilotState hit = {0};
            hit.source_update_order = 1u;
            hit.motion.speed_q16 = cases[i].speed;
            if (portable_driver_pilot_tick(&hit, 0u, &walls) != 0 ||
                hit.contact_audio.serial != 1u || hit.contact_audio.song_id != 169u ||
                hit.contact_audio.volume != cases[i].volume ||
                hit.contact_audio.pitch != cases[i].pitch || hit.contact_audio.restart)
                return driver_pilot_failure(__LINE__);
            if (portable_driver_pilot_tick(&hit, 0u, &walls) != 0 ||
                hit.contact_audio.serial != 2u)
                return driver_pilot_failure(__LINE__);
        }
        PortableDriverPilotState silent = {0};
        silent.source_update_order = 1u;
        silent.steering.state_flags = 0x20u;
        if (portable_driver_pilot_tick(&silent, 0u, &walls) != 0 ||
            silent.contact_audio.serial != 0u)
            return driver_pilot_failure(__LINE__);
        silent = (PortableDriverPilotState){0};
        silent.source_update_order = 1u;
        silent.pose.position_z = 0x50000;
        silent.hop.mode = 1u;
        if (portable_driver_pilot_tick(&silent, 0u, &walls) != 0 ||
            silent.contact_audio.serial != 0u)
            return driver_pilot_failure(__LINE__);
        material_flags[0] = 4u;
    }

    /* 8E34 case 11 distinguishes signed speed, released A and the
     * positive exact-zero branch. Position changes only during fast A. */
    {
        const struct {
            uint16_t controls;
            int32_t speed;
            int16_t angle;
            uint16_t position;
            int16_t next_angle;
            uint16_t next_position;
            uint8_t next_status;
        } cases[] = {
            {0, 0x00100000, 0x800, 0x700, 0x800, 0x700, 11},
            {0, 0x000fffff, 0x800, 0x700, 0, 0x700, 14},
            {0, -1, -0x800, 0xf900, 0, 0xf900, 14},
            {0x8000, 0x00ffffff, 0x800, 0x700, 0, 0x700, 11},
            {0x8000, -1, -0x800, 0xf900, 0, 0xf900, 11},
            {0x8000, 0x01000000, 0x400, 0x300, 0, 0, 11},
            {0x8000, 0x01000000, -0x400, 0xfd00, 0, 0, 14},
            {0x8000, 0x01000000, 0x300, 0x700, 0, 0x300, 14},
            {0x8000, 0x01000000, -0x300, 0xf900, 0, 0xfd00, 14},
            {0x8000, 0x01000000, 0, 0, 0, 0, 14},
            {0x8000, 0x01000000, 0x800, 0x700, 0x400, 0x300, 11},
            {0x8000, 0x01000000, -0x800, 0xf900, -0x400, 0xfd00, 11},
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            PortableDriverSteeringState rebound = {
                .control_word = cases[i].controls, .speed_q16 = cases[i].speed,
                .base_angle = cases[i].angle, .position_82 = cases[i].position,
                .response_status = 11u, .response_flags = 0x2cu, .driver_mode = 3u};
            if (portable_driver_collision_response_tick(&rebound) != 0 ||
                rebound.base_angle != cases[i].next_angle ||
                rebound.position_82 != cases[i].next_position ||
                rebound.response_status != cases[i].next_status ||
                rebound.response_flags != 0u ||
                rebound.driver_mode != (cases[i].next_status == 14u ? 0u : 3u))
                return driver_pilot_failure(__LINE__);
        }
    }

    /* Low-speed A clears the impact before DAEC: it exits 11 while
     * retaining C0. Released A above its lower threshold keeps the angle
     * and receives the normal-response table acceleration instead. */
    {
        PortableDriverPilotState rebound = {0};
        rebound.source_update_order = 1u;
        rebound.input.speed_parameter = 5;
        rebound.auto_acceleration.character_class = 3u;
        rebound.motion.speed_q16 = 0x00200000;
        rebound.motion.acceleration_q16 = 0x00050000;
        rebound.steering.response_status = 11u;
        rebound.steering.base_angle = 0x1000;
        rebound.steering.position_82 = 0x700;
        if (portable_driver_pilot_tick(&rebound, 0x8000u, &dependencies) != 0 ||
            rebound.steering.response_status != 14u ||
            rebound.steering.base_angle != 0 ||
            rebound.steering.position_82 != 0x700u ||
            rebound.motion.acceleration_q16 != 0x00050000)
            return driver_pilot_failure(__LINE__);
        rebound.steering.response_status = 11u;
        rebound.steering.base_angle = 0x1000;
        if (portable_driver_pilot_tick(&rebound, 0u, &dependencies) != 0 ||
            rebound.steering.response_status != 11u ||
            rebound.steering.base_angle != 0x1000 ||
            rebound.steering.position_82 != 0x700u ||
            rebound.motion.acceleration_q16 != -0x00100000)
            return driver_pilot_failure(__LINE__);
    }

    /* A374/A3A0 entry and 8BF4 wobble feed both 8E34 spin directions. */
    {
        PortableDriverSteeringState spin = {.speed_q16 = 0x03000000,
            .response_flags = 0x1800, .response_q8 = 0x100, .base_angle = 0x200};
        if (portable_driver_surface_spin(&spin, 0u, 8u) != 0 ||
            spin.response_status != 8u || spin.spin_delay != 60u ||
            spin.base_angle != 0 || spin.response_q8 != 0 || spin.response_flags != 0)
            return driver_pilot_failure(__LINE__);
        if (portable_driver_source_left_turn_response_tick(&spin, 0,&response_rules) != 0 ||
            spin.spin_delay != 59u || spin.position_82 != 0x600u) return driver_pilot_failure(__LINE__);
        /* Repeated contact preserves the existing wobble countdown. */
        if (portable_driver_surface_spin(&spin, 0u, 0x18u) != 0 ||
            spin.spin_delay != 59u) return driver_pilot_failure(__LINE__);
        spin.position_82 = 0xff00u; spin.spin_delay = 0u;
        if (portable_driver_surface_spin(&spin, 0u, 0x1cu) != 0 ||
            spin.response_status != 7u || spin.spin_delay != 60u) return driver_pilot_failure(__LINE__);
        spin.position_82 = 0x2000u;
        if (portable_driver_source_left_turn_response_tick(&spin, 0,&response_rules) != 0 ||
            spin.spin_delay != 0u || spin.position_82 != 0x1b80u ||
            spin.spin_delay_cue != 162u) return driver_pilot_failure(__LINE__);
        spin.speed_q16 = 0x017fffff; spin.position_82 = 0x047fu;
        if (portable_driver_source_left_turn_response_tick(&spin, 0,&response_rules) != 0 ||
            spin.response_status != 14u || spin.position_82 != 0xffffu) return driver_pilot_failure(__LINE__);
        spin.response_status = 8u; spin.spin_delay = 60u;
        spin.speed_q16 = 0x03000000; spin.position_82 = 0;
        spin.control_word = 0x4000u;
        if (portable_driver_source_left_turn_response_tick(&spin, 0,&response_rules) != 0 ||
            spin.spin_delay != 0 || spin.response_status != 14u || spin.position_82 != 0 ||
            spin.spin_delay_cue != 165u)
            return driver_pilot_failure(__LINE__);
        spin = (PortableDriverSteeringState){.speed_q16 = 0x02ffffff,
            .response_flags = 0x1800};
        if (portable_driver_surface_spin(&spin, 0u, 8u) != 0 ||
            spin.spin_delay != 0u || spin.response_flags != 0u) return driver_pilot_failure(__LINE__);
        spin.speed_q16 = 0x03000000; spin.response_flags = 2u;
        if (portable_driver_surface_spin(&spin, 0u, 8u) != 0 ||
            spin.spin_delay != 0u) return driver_pilot_failure(__LINE__);
        spin.response_flags = 0; spin.state_flags = 0x2000u;
        if (portable_driver_surface_spin(&spin, 0u, 12u) != 0 ||
            spin.spin_delay != 0u) return driver_pilot_failure(__LINE__);
        if (portable_driver_surface_spin(&spin, 1u, 8u) != 0 ||
            spin.spin_delay != 0u) return driver_pilot_failure(__LINE__);
        spin = (PortableDriverSteeringState){.speed_q16 = 0x04000000,
            .response_status = 8u, .spin_delay = 60u};
        for (unsigned tick = 0; tick < 60; ++tick)
            if (portable_driver_source_left_turn_response_tick(&spin, 0,&response_rules) != 0)
                return driver_pilot_failure(__LINE__);
        if (spin.spin_delay != 0u || spin.position_82 != 0x480u) return driver_pilot_failure(__LINE__);
    }

    /* P151 source-control staging: sub_8038A08 consumes the prior +0x96
     * value, while the following sub_8037FD8 store makes the current input
     * visible to the next source tick. */
    {
        PortableDriverPilotState source_control_phase = {0};

        source_control_phase.source_update_order = 1;
        source_control_phase.input.speed_parameter = 5;
        source_control_phase.auto_acceleration.driver_mode = 0;
        source_control_phase.auto_acceleration.character_class = 3;
        source_control_phase.motion.speed_q16 = INT32_C(0x00100000);
        if (portable_driver_pilot_tick(
                &source_control_phase, UINT16_C(0x0100), &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            source_control_phase.steering.steering_offset != 0 ||
            source_control_phase.steering.control_word != UINT16_C(0x0100) ||
            portable_driver_pilot_tick(
                &source_control_phase, 0, &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            source_control_phase.steering.steering_offset != 0x1000)
            return driver_pilot_failure(__LINE__);
    }

    /* P108 pilot-order seam: carry the captured +0x48/+0x82/+0x56 views
     * through one source-order native tick while isolating sub_8038A08 from
     * the separately scheduled response-state helper. */
    {
        PortableDriverPilotState source_composition_pilot = {0};
        PortableDriverPilotDependencies composition_dependencies =
            dependencies;

        composition_dependencies.steering_response_data = NULL;
        composition_dependencies.steering_response_data_count = 0;
        source_composition_pilot.source_update_order = 1;
        source_composition_pilot.input.speed_parameter = 5;
        source_composition_pilot.auto_acceleration.driver_mode = 0;
        source_composition_pilot.auto_acceleration.character_class = 3;
        source_composition_pilot.motion.speed_q16 =
            INT32_C(0x013a0000);
        source_composition_pilot.forward_acceleration.response_q8 =
            INT16_C(0x0900);
        source_composition_pilot.forward_acceleration.state_flags =
            UINT16_C(0xc400);
        source_composition_pilot.steering.state_flags =
            UINT32_C(0x0000c400);
        source_composition_pilot.steering.angle =
            (int16_t)UINT16_C(0x492c);
        source_composition_pilot.steering.steering_offset =
            (int16_t)UINT16_C(0x616c);
        source_composition_pilot.steering.base_angle =
            (int16_t)UINT16_C(0x4fb4);
        source_composition_pilot.steering.position_82 = UINT16_C(0xe320);
        source_composition_pilot.normal_collision_source_valid = 1;
        source_composition_pilot.normal_collision_pending = 1;
        source_composition_pilot.normal_collision_source_use_motion = 0;
        source_composition_pilot.normal_collision_source_skip_motion_tick =
            1;
        source_composition_pilot.normal_collision_source =
            (PortableDriverNormalCollisionSourceInput){
                .resultant_x = 0,
                .resultant_y = 0,
                .current_angle = UINT16_C(0x616c),
                .source_flags = UINT32_C(0x0000d400),
                .prior_angle_history_7a = UINT16_C(0x492c),
                .prior_angle_delta_80 = UINT16_C(0xe7c0),
                .prior_resistance_92 = INT16_C(0x0100),
                .prior_magnitude_state_bc = INT32_C(0x013a0000),
                .prior_timer_value_84 = 0,
                .prior_response_timer_7e = 1,
            };
        if (portable_driver_pilot_tick(
                &source_composition_pilot, 0,
                &composition_dependencies) != PORTABLE_DRIVER_PILOT_OK ||
            source_composition_pilot.steering.steering_offset !=
                (int16_t)UINT16_C(0x628c) ||
            source_composition_pilot.steering.angle !=
                (-INT16_C(19904)) ||
            source_composition_pilot.steering.source_angle_56 !=
                (int16_t)UINT16_C(0x7f6c) ||
            source_composition_pilot.motion.speed_q16 !=
                INT32_C(0x013a0000))
            return driver_pilot_failure(__LINE__);
    }

    /* Continuous GP frame 1912: sub_8038E34 case 14 invokes sub_8039690
     * with row zero, moving +0x82 from 0x1a30 to 0x1950 and resetting
     * status +0x7e once +0x80 is zero. */
    {
        PortableDriverSteeringState source_status14 = {0};
        PortableDriverChargedExitEvent status14_event;

        source_status14.response_status = UINT16_C(0x000e);
        source_status14.position_82 = UINT16_C(0x1a30);
        source_status14.base_angle = 0;
        source_status14.response_flags = UINT16_C(0x0024);
        if (portable_driver_source_status14_tick(
                &source_status14, 0, 1, 1,&response_rules, &status14_event) != PORTABLE_DRIVER_STEERING_OK ||
            source_status14.response_status != 0u ||
            source_status14.position_82 != UINT16_C(0x1950) ||
            source_status14.response_flags != 0u)
            return driver_pilot_failure(__LINE__);
        /* Original frame 2933 remains in status 14 while row zero
         * approaches both position and positive base angle. */
        source_status14.response_status = 14u;
        source_status14.position_82 = UINT16_C(0x0260);
        source_status14.base_angle = INT16_C(0x2800);
        source_status14.response_flags = UINT16_C(0x0008);
        if (portable_driver_source_status14_tick(
                &source_status14, 0, 1, 1,&response_rules, &status14_event) != PORTABLE_DRIVER_STEERING_OK ||
            source_status14.response_status != 14u ||
            source_status14.position_82 != UINT16_C(0x0180) ||
            source_status14.base_angle != INT16_C(0x2760))
            return driver_pilot_failure(__LINE__);
        /* The original frame-2750 crossing exits status 14 but keeps
         * +0x80 at 0x20; sub_8039690 does not store the negative step. */
        source_status14.response_status = 14u;
        source_status14.base_angle = INT16_C(0x0020);
        source_status14.position_82 = UINT16_C(0x0040);
        source_status14.response_flags = 0u;
        if (portable_driver_source_status14_tick(
                &source_status14, 0, 1, 1,&response_rules, &status14_event) != PORTABLE_DRIVER_STEERING_OK ||
            source_status14.response_status != 0u ||
            source_status14.base_angle != INT16_C(0x0020) ||
            source_status14.position_82 != 0u)
            return driver_pilot_failure(__LINE__);
    }

    /* 9690 approaches both signed directions; 8E34 then tests the wrapped
     * large-angle window and applies its zero exit even after a charged exit. */
    {
        const struct { int16_t before, after; uint8_t status; } cases[] = {
            {-0x2800, -0x2760, 14u}, {-0x0020, -0x0020, 0u},
            {-0x00a0, 0, 0u}, {0x409f, 0x3fff, 14u},
            {0x40a0, 0x4000, 11u}, {-0x40a0, -0x4000, 14u},
            {-0x40a1, -0x4001, 11u},
        };
        for (size_t i=0u; i<sizeof cases/sizeof cases[0]; ++i)
        {
            PortableDriverSteeringState response = {
                .response_status=14u, .base_angle=cases[i].before,
                .position_82=(uint16_t)-0x260, .response_flags=0x2cu,
                .drift_angle=0x1234u,
            };
            PortableDriverChargedExitEvent event;
            if (portable_driver_source_status14_tick(&response,0,1,1,&response_rules,&event) !=
                    PORTABLE_DRIVER_STEERING_OK ||
                response.base_angle != cases[i].after ||
                response.response_status != cases[i].status ||
                response.position_82 != (uint16_t)-0x180 ||
                response.drift_angle != 0x1234u || response.response_flags != 0u ||
                response.driver_mode != (cases[i].status==11u ? 11u : 0u) ||
                event.strong_exit)
                return driver_pilot_failure(__LINE__);
        }
        PortableDriverSteeringState charged = {
            .response_status=14u, .response_flags=0x40u,
            .speed_q16=INT32_C(0x02800000), .control_word=0x0300u,
            .drift_angle=0x1234u,
        };
        PortableDriverChargedExitEvent event;
        if (portable_driver_source_status14_tick(&charged,INT32_C(0x03000000),
                1,1,&response_rules,&event) != PORTABLE_DRIVER_STEERING_OK ||
            !event.strong_exit || !event.audio_cue_140 || !event.particle_effect ||
            charged.response_status != 0u || charged.driver_mode != 12u ||
            charged.exit_timer != 48u || charged.response_flags != 0u ||
            charged.drift_angle != 0x1234u)
            return driver_pilot_failure(__LINE__);
        charged.response_status=14u; charged.driver_mode=0u;
        charged.response_flags=0x40u; charged.state_flags=0x800u;
        if (portable_driver_source_status14_tick(&charged,INT32_C(0x03000000),
                1,1,&response_rules,&event) != PORTABLE_DRIVER_STEERING_OK ||
            event.strong_exit || charged.response_flags != 0x40u ||
            charged.response_status != 0u)
            return driver_pilot_failure(__LINE__);
    }

    /* The frame-2576 source contact shows 8A08 composes heading from the
     * prior +0x88, then 7B30/8AFC writes the next response. */
    {
        PortableDriverPilotState source_response_pilot = {0};
        PortableDriverPilotDependencies response_dependencies = dependencies;

        response_dependencies.steering_response_data =
            steering_response_data;
        response_dependencies.steering_response_data_count = 4;
        source_response_pilot.source_update_order = 1;
        source_response_pilot.input.speed_parameter = 5;
        source_response_pilot.auto_acceleration.driver_mode = 0;
        source_response_pilot.auto_acceleration.character_class = 3;
        source_response_pilot.motion.speed_q16 = INT32_C(0x013a0000);
        source_response_pilot.forward_acceleration.response_q8 =
            INT16_C(0x0900);
        source_response_pilot.forward_acceleration.state_flags =
            UINT16_C(0xc400);
        source_response_pilot.steering.state_flags = UINT32_C(0x0000c400);
        source_response_pilot.steering.angle =
            (int16_t)UINT16_C(0x492c);
        source_response_pilot.steering.steering_offset =
            (int16_t)UINT16_C(0x616c);
        source_response_pilot.steering.base_angle =
            (int16_t)UINT16_C(0x4fb4);
        source_response_pilot.steering.position_82 = UINT16_C(0xe320);
        if (portable_driver_pilot_tick(
                &source_response_pilot, 0, &response_dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            source_response_pilot.forward_acceleration.response_q8 !=
                INT16_C(0x086c) ||
            source_response_pilot.steering.steering_offset !=
                (int16_t)UINT16_C(0x628c) ||
            source_response_pilot.steering.angle !=
                (-INT16_C(19904)) ||
            source_response_pilot.steering.source_angle_56 !=
                (int16_t)UINT16_C(0x7f6c))
            return driver_pilot_failure(__LINE__);
    }

    /* P111 native response-stage fixture: seed the explicitly mapped fields
     * from the live P110 source window. Keep the control argument at zero so
     * this checks the response-stage isolation only; it is not a claim that
     * the bridge sample and this native tick share an instruction boundary. */
    {
        PortableDriverPilotState source_window_pilot = {0};
        PortableDriverPilotDependencies window_dependencies = dependencies;

        window_dependencies.steering_response_data =
            steering_response_data;
        window_dependencies.steering_response_data_count = 4;
        source_window_pilot.source_update_order = 1;
        source_window_pilot.input.speed_parameter = 5;
        source_window_pilot.auto_acceleration.driver_mode = 0;
        source_window_pilot.auto_acceleration.character_class = 3;
        source_window_pilot.motion.speed_q16 = INT32_C(0x013a0000);
        source_window_pilot.forward_acceleration.response_q8 =
            INT16_C(0x0900);
        source_window_pilot.forward_acceleration.state_flags =
            UINT16_C(0xc400);
        source_window_pilot.steering.state_flags = UINT32_C(0x0000c400);
        source_window_pilot.steering.angle =
            (-INT16_C(19616));
        source_window_pilot.steering.steering_offset =
            (int16_t)UINT16_C(0x63ac);
        source_window_pilot.steering.base_angle =
            (int16_t)UINT16_C(0x4fb4);
        source_window_pilot.steering.position_82 = UINT16_C(0xe320);
        if (portable_driver_pilot_tick(
                &source_window_pilot, 0, &window_dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            source_window_pilot.forward_acceleration.response_q8 !=
                INT16_C(0x086c) ||
            source_window_pilot.steering.steering_offset !=
                (int16_t)UINT16_C(0x64cc) ||
            source_window_pilot.steering.angle !=
                (-INT16_C(19328)) ||
            source_window_pilot.steering.source_angle_56 !=
                UINT16_C(0x81ac))
            return driver_pilot_failure(__LINE__);
    }

    /* The source terrain boundary writes material 0x26 and returns to the
     * caller; only the explicit host adapter may carry a raster miss through
     * the pilot. The strict default must still reject that pose. */
    {
        PortableDriverPilotState boundary_state = {0};

        boundary_state.input.speed_parameter = 5;
        boundary_state.auto_acceleration.driver_mode = 0;
        boundary_state.auto_acceleration.character_class = 3;
        boundary_state.pose.position_x = INT32_C(0x003f0000);
        boundary_state.pose.component_x = 256;
        if (portable_driver_pilot_tick(&boundary_state, 0,
                                       &dependencies) !=
                PORTABLE_DRIVER_PILOT_POSE_ERROR)
            return driver_pilot_failure(__LINE__);
        boundary_state = (PortableDriverPilotState){0};
        boundary_state.allow_track_boundary = 1;
        boundary_state.input.speed_parameter = 5;
        boundary_state.auto_acceleration.driver_mode = 0;
        boundary_state.auto_acceleration.character_class = 3;
        boundary_state.pose.position_x = INT32_C(0x003f0000);
        boundary_state.pose.component_x = 256;
        if (portable_driver_pilot_tick(&boundary_state, 0,
                                       &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            boundary_state.pose.sampled_world_x != 64 ||
            boundary_state.pose.track_cell_valid != 0)
            return driver_pilot_failure(__LINE__);
    }

    vector_state = (PortableDriverPilotState){0};
    vector_state.input.speed_parameter = 5;
    vector_state.auto_acceleration.driver_mode = 0;
    vector_state.auto_acceleration.character_class = 3;
    vector_state.motion.angle = 0;
    vector_state.vector_effect_pending = 1;
    vector_state.vector_effect_mode = 0;
    if (portable_driver_pilot_tick(&vector_state, UINT16_C(0x8000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        vector_state.vector_effect_pending != 0 ||
        vector_state.motion.component_x != -4 ||
        vector_state.motion.component_y != 4 ||
        vector_state.vector_effect.contact_word_5a != UINT16_C(0xc007) ||
        vector_state.vector_effect.orientation != 1)
        return driver_pilot_failure(__LINE__);
    if (portable_driver_pilot_tick(&vector_state, UINT16_C(0x8000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        vector_state.pose.position_x != -INT32_C(0x00000400) ||
        vector_state.pose.position_y != INT32_C(0x00000400))
        return driver_pilot_failure(__LINE__);

    /* A raw 0x80 cell transition schedules the source-owned consequence
     * without a caller-injected pending event. */
    memset(cells, 0x80, sizeof(cells));
    material_flags[1] = 0x80;
    terrain_cells[1] = 1;
    {
        PortableDriverPilotState automatic_state = {0};
        PortableDriverPilotState alternate_state = {0};

        automatic_state.input.speed_parameter = 5;
        automatic_state.auto_acceleration.driver_mode = 0;
        automatic_state.auto_acceleration.character_class = 3;
        automatic_state.forward_acceleration.state_flags = 0;
        automatic_state.pose.position_x = INT32_C(0x00080000);
        automatic_state.vector_effect_previous_cell_key = 0;
        automatic_state.vector_effect_previous_cell_key_valid = 1;
        if (portable_driver_pilot_tick(
                &automatic_state, UINT16_C(0x8000), &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            automatic_state.vector_trigger_count != 1u ||
            automatic_state.vector_effect_count != 1u ||
            automatic_state.vector_effect_pending != 0 ||
            automatic_state.vector_trigger.mode != 0 ||
            automatic_state.vector_effect.mode != 0 ||
            automatic_state.vector_effect.contact_word_5a != UINT16_C(0xc007))
            return driver_pilot_failure(__LINE__);

        /* Native wall entry reflects prior components before the hold;
         * remaining in the cell keeps the arm, leaving releases it once. */
        {
            PortableDriverPilotState wall = {0};
            PortableDriverPilotDependencies wall_dependencies = dependencies;
            wall_dependencies.steering_delta_table = NULL;
            wall_dependencies.steering_response_data = NULL;
            wall.source_update_order = 1u;
            wall.input.speed_parameter = 5u;
            wall.auto_acceleration.character_class = 3u;
            wall.pose.position_x = INT32_C(0x80000);
            wall.pose.component_x = 256;
            wall.motion.component_x = 256;
            wall.motion.speed_q16 = INT32_C(0x1000000);
            wall.vector_effect_previous_cell_key_valid = 1u;
            wall.forward_limit_selector = 13u;
            if (portable_driver_pilot_tick(&wall, 0x8000u, &wall_dependencies) != 0 ||
                wall.motion.component_x != -256 || wall.motion.component_y != 0 ||
                wall.vector_effect_source_state != 0x8001u ||
                wall.vector_effect.contact_word_5a != 0xc007u ||
                wall.steering.response_status == 7u || wall.forward_limit_selector != 13u ||
                wall.normal_collision_count != 0u || wall.normal_collision_timer_62 != 8u)
                return driver_pilot_failure(__LINE__);
            if (portable_driver_pilot_tick(&wall, 0x8000u, &wall_dependencies) != 0 ||
                wall.vector_effect_source_state != 0x8002u ||
                wall.vector_effect_count != 1u || wall.normal_collision_count != 0u ||
                wall.motion.component_x != -256) return driver_pilot_failure(__LINE__);
            if (portable_driver_pilot_tick(&wall, 0x8000u, &wall_dependencies) != 0 ||
                wall.vector_effect_source_state != 0u || wall.normal_collision_count != 1u ||
                wall.normal_collision_timer_62 != 7u || wall.vector_effect_count != 1u)
                return driver_pilot_failure(__LINE__);
        }

        /* B2B4 gates on sub_8032990's material byte, not the 0x80 route
         * raster byte. A one-row boundary change selects mode 6 only when
         * the material is 0x80, as in the original frame-2092 contact. */
        {
            PortableDriverPilotState boundary = {0};

            boundary.input.speed_parameter = 5;
            boundary.auto_acceleration.character_class = 3;
            boundary.pose.position_x = INT32_C(0x00080000);
            boundary.vector_effect_previous_cell_key = UINT16_C(0x0101);
            boundary.vector_effect_previous_cell_key_valid = 1;
            material_flags[1] = UINT8_C(0x40);
            if (portable_driver_pilot_tick(
                    &boundary, UINT16_C(0x8000), &dependencies) !=
                    PORTABLE_DRIVER_PILOT_OK ||
                boundary.vector_trigger_count != 0u)
                return driver_pilot_failure(__LINE__);
            boundary.vector_effect_previous_cell_key = UINT16_C(0x0101);
            material_flags[1] = UINT8_C(0x80);
            if (portable_driver_pilot_tick(
                    &boundary, UINT16_C(0x8000), &dependencies) !=
                    PORTABLE_DRIVER_PILOT_OK ||
                boundary.vector_trigger_count != 1u ||
                boundary.vector_trigger.mode != 6 ||
                boundary.vector_trigger.material_flags != UINT8_C(0x80))
                return driver_pilot_failure(__LINE__);
        }

        {
            PortableDriverPilotState scheduled_state = {0};

            scheduled_state.input.speed_parameter = 5;
            scheduled_state.auto_acceleration.driver_mode = 0;
            scheduled_state.auto_acceleration.character_class = 3;
            scheduled_state.pose.position_x = INT32_C(0x00080000);
            scheduled_state.vector_effect_previous_cell_key = 1;
            scheduled_state.vector_effect_previous_cell_key_valid = 1;
            scheduled_state.vector_effect_source_state = UINT16_C(0x8001);
            scheduled_state.normal_collision_source_valid = 1;
            scheduled_state.normal_collision_source =
                (PortableDriverNormalCollisionSourceInput){
                    .resultant_x = -161,
                    .resultant_y = 374,
                    .current_angle = UINT16_C(0x66eb),
                    .source_flags = UINT32_C(0x0000d400),
                    .prior_angle_history_7a = UINT16_C(0x66eb),
                    .prior_angle_delta_80 = 0,
                    .prior_resistance_92 = INT16_C(0x00e0),
                    .prior_magnitude_state_bc = INT32_C(0x02028000),
                    .prior_timer_value_84 = 0,
                    .prior_response_timer_7e = 0,
                };
            if (portable_driver_pilot_tick(
                    &scheduled_state, UINT16_C(0x8000), &dependencies) !=
                    PORTABLE_DRIVER_PILOT_OK ||
                scheduled_state.normal_collision_schedule_count != 1u ||
                scheduled_state.normal_collision_count != 1u ||
                scheduled_state.normal_collision_pending != 0 ||
                scheduled_state.vector_effect_source_state != 0 ||
                scheduled_state.normal_collision.source_flags !=
                    UINT32_C(0x0000c400) ||
                scheduled_state.normal_collision.angle_history_7a !=
                    UINT16_C(0x66eb) ||
                scheduled_state.normal_collision.angle_delta_80 !=
                    UINT16_C(0x29a5) ||
                scheduled_state.normal_collision_response_angle_7a !=
                    UINT16_C(0x9090) ||
                scheduled_state.normal_collision_base_heading_7c !=
                    UINT16_C(0x66eb) ||
                scheduled_state.normal_collision_response_delta_80 !=
                    UINT16_C(0x29a5) ||
                scheduled_state.normal_collision.magnitude_state_bc !=
                    INT32_C(0x01760000))
                return driver_pilot_failure(__LINE__);

            scheduled_state = (PortableDriverPilotState){0};
            scheduled_state.input.speed_parameter = 5;
            scheduled_state.auto_acceleration.driver_mode = 0;
            scheduled_state.auto_acceleration.character_class = 3;
            scheduled_state.pose.position_x = INT32_C(0x00080000);
            scheduled_state.vector_effect_previous_cell_key = 1;
            scheduled_state.vector_effect_previous_cell_key_valid = 1;
            scheduled_state.vector_effect_source_state = UINT16_C(0x8001);
            scheduled_state.normal_collision_source_valid = 1;
            scheduled_state.normal_collision_source_use_motion = 1;
            scheduled_state.normal_collision_source =
                (PortableDriverNormalCollisionSourceInput){
                    .current_angle = UINT16_C(0x66eb),
                    .source_flags = UINT32_C(0x0000d400),
                    .prior_angle_history_7a = UINT16_C(0x66eb),
                    .prior_angle_delta_80 = 0,
                    .prior_resistance_92 = INT16_C(0x00e0),
                    .prior_magnitude_state_bc = INT32_C(0x02028000),
                    .prior_timer_value_84 = 0,
                    .prior_response_timer_7e = 0,
                };
            if (portable_driver_pilot_tick(
                    &scheduled_state, UINT16_C(0x8000), &dependencies) !=
                    PORTABLE_DRIVER_PILOT_OK ||
                scheduled_state.normal_collision_count != 1u ||
                scheduled_state.normal_collision_pending != 0 ||
                scheduled_state.normal_collision_generated_resultant_x !=
                    scheduled_state.motion.component_x ||
                scheduled_state.normal_collision_generated_resultant_y !=
                    scheduled_state.motion.component_y ||
                scheduled_state.normal_collision.source_flags !=
                    UINT32_C(0x0000c400) ||
                scheduled_state.normal_collision.angle_delta_80 !=
                    UINT16_C(0xf915) ||
                scheduled_state.normal_collision.magnitude_state_bc !=
                    INT32_C(0x00040000))
                return driver_pilot_failure(__LINE__);

            /* These armed-response captures represent release onto road,
             * rather than continued contact in the same wall cell. */
            material_flags[1] = UINT8_C(0x40);
            {
                PortableDriverPilotState source_order_state = {0};

                source_order_state.input.speed_parameter = 5;
                source_order_state.auto_acceleration.driver_mode = 0;
                source_order_state.auto_acceleration.character_class = 3;
                source_order_state.source_update_order = 1;
                source_order_state.pose.position_x = INT32_C(0x00080000);
                source_order_state.vector_effect_previous_cell_key = 1;
                source_order_state.vector_effect_previous_cell_key_valid = 1;
                source_order_state.vector_effect_source_state =
                    UINT16_C(0x8001);
                source_order_state.normal_collision_source_valid = 1;
                source_order_state.motion.component_x = -161;
                source_order_state.motion.component_y = 374;
                source_order_state.normal_collision_source_use_motion = 1;
                source_order_state.normal_collision_source_skip_motion_tick =
                    0;
                source_order_state.normal_collision_source =
                    (PortableDriverNormalCollisionSourceInput){
                        .resultant_x = 0,
                        .resultant_y = 0,
                        .current_angle = UINT16_C(0x66eb),
                        .source_flags = UINT32_C(0x0000d400),
                        .prior_angle_history_7a = UINT16_C(0x66eb),
                        .prior_angle_delta_80 = 0,
                        .prior_resistance_92 = INT16_C(0x00e0),
                        .prior_magnitude_state_bc = INT32_C(0x02028000),
                        .prior_timer_value_84 = 0,
                        .prior_response_timer_7e = 0,
                    };
                if (portable_driver_pilot_tick(
                        &source_order_state, UINT16_C(0x8000),
                        &dependencies) != PORTABLE_DRIVER_PILOT_OK ||
                    source_order_state.normal_collision_schedule_count != 1u ||
                    source_order_state.normal_collision_count != 1u ||
                    source_order_state.normal_collision_pending != 0 ||
                    source_order_state.normal_collision_generated_resultant_x !=
                        -161 ||
                    source_order_state.normal_collision_generated_resultant_y !=
                        374 ||
                    source_order_state.motion.component_x != 0 ||
                    source_order_state.motion.component_y != 0 ||
                    source_order_state.normal_collision.source_flags !=
                        UINT32_C(0x0000c400) ||
                    source_order_state.normal_collision.angle_delta_80 !=
                        UINT16_C(0x29a5) ||
                    source_order_state.normal_collision_response_angle_7a !=
                        UINT16_C(0x9090) ||
                    source_order_state.normal_collision_base_heading_7c !=
                        UINT16_C(0x66eb) ||
                    source_order_state.normal_collision_response_delta_80 !=
                        UINT16_C(0x29a5) ||
                    source_order_state.normal_collision_response_output_c0 !=
                        -INT32_C(0x00380000) ||
                    source_order_state.normal_collision_response_output_written !=
                        1u ||
                    source_order_state.normal_collision_response_next_status !=
                        UINT8_C(0x0b) ||
                    source_order_state.motion.acceleration_q16 !=
                        -INT32_C(0x00380000) ||
                    source_order_state.normal_collision.magnitude_state_bc !=
                        INT32_C(0x01760000))
                    return driver_pilot_failure(__LINE__);
            }

            /* P120 source-resultant fixture: the armed normal branch owns the
             * pre-handler pair, so the opt-in applies _080EC780/_080EC788
             * semantics before the existing response adapter. */
            {
                PortableDriverPilotState b550_state = {0};

                b550_state.input.speed_parameter = 5;
                b550_state.auto_acceleration.driver_mode = 0;
                b550_state.auto_acceleration.character_class = 3;
                b550_state.source_update_order = 1;
                b550_state.pose.position_x = INT32_C(0x00080000);
                b550_state.vector_effect_previous_cell_key = 1;
                b550_state.vector_effect_previous_cell_key_valid = 1;
                b550_state.vector_effect_source_state = UINT16_C(0x8001);
                b550_state.vector_effect_source_direction = 0;
                b550_state.normal_collision_source_valid = 1;
                b550_state.normal_collision_source_use_motion = 1;
                b550_state.normal_collision_source_use_b550_table = 1;
                b550_state.motion.component_x = 654;
                b550_state.motion.component_y = 64;
                b550_state.motion.speed_q16 = INT32_C(0x02920000);
                b550_state.normal_collision_source =
                    (PortableDriverNormalCollisionSourceInput){
                        .current_angle = UINT16_C(0x66eb),
                        .source_flags = UINT32_C(0x0000d400),
                        .prior_angle_history_7a = UINT16_C(0x66eb),
                        .prior_resistance_92 = INT16_C(0x00e0),
                        .prior_magnitude_state_bc = INT32_C(0x02920000),
                    };
                /* Independent synthetic timer scenario; this timer value
                 * is not asserted to be the P116 original event's value. */
                PortableDriverPilotState native_stage = b550_state;
                PortableDriverPilotDependencies stage_dependencies = dependencies;
                stage_dependencies.steering_delta_table = NULL;
                stage_dependencies.steering_response_data = NULL;
                native_stage.normal_collision_source_from_native_state = 1;
                native_stage.normal_collision_timer_38 = 1;
                native_stage.steering.state_flags = UINT32_C(0x8400);
                native_stage.steering.angle = INT16_C(0x1234);
                native_stage.steering.steering_offset = INT16_C(0x66eb);
                native_stage.normal_collision.resistance_92 = INT16_C(0xe0);
                PortableDriverPilotState no_timer_stage = native_stage;
                no_timer_stage.normal_collision_timer_38 = 0;
                PortableKartMotionState expected_motion = no_timer_stage.motion;
                if (portable_kart_motion_tick(&expected_motion,
                        dependencies.sine_table, dependencies.sine_count) != 0 ||
                    portable_driver_pilot_tick(&no_timer_stage, 0x8000,
                                               &stage_dependencies) != 0 ||
                    no_timer_stage.normal_collision_count != 1 ||
                    no_timer_stage.normal_collision_generated_resultant_x !=
                        expected_motion.component_x ||
                    no_timer_stage.normal_collision_generated_resultant_y !=
                        expected_motion.component_y ||
                    (no_timer_stage.steering.state_flags & 0x4000) != 0)
                {
                    fputs("native B550 motion-before-handler regression failed\n", stderr);
                    return driver_pilot_failure(__LINE__);
                }
                if (portable_driver_pilot_tick(&native_stage, 0x8000,
                                               &stage_dependencies) != 0 ||
                    native_stage.normal_collision_timer_38 != 0 ||
                    native_stage.steering.state_flags != UINT32_C(0xc400) ||
                    native_stage.motion.component_x != 327 ||
                    native_stage.motion.component_y != 60 ||
                    native_stage.motion.speed_q16 != INT32_C(0x01470000) ||
                    native_stage.steering.angle != INT16_C(0x1234) ||
                    native_stage.normal_collision_count != 1 ||
                    native_stage.normal_collision_pending != 0)
                {
                    fputs("native B550 canonical-state regression failed\n", stderr);
                    return driver_pilot_failure(__LINE__);
                }
                const int32_t prior_x = native_stage.pose.position_x;
                const int32_t prior_y = native_stage.pose.position_y;
                if (portable_driver_pilot_tick(&native_stage, 0x8000,
                                               &stage_dependencies) != 0 ||
                    native_stage.pose.position_x != prior_x + 327 * 256 ||
                    native_stage.pose.position_y != prior_y + 60 * 256 ||
                    (native_stage.steering.state_flags & 0x4000) != 0)
                {
                    fputs("native B550 next-tick release regression failed\n", stderr);
                    return driver_pilot_failure(__LINE__);
                }
                if (portable_driver_pilot_tick(
                        &b550_state, UINT16_C(0x8000), &dependencies) !=
                        PORTABLE_DRIVER_PILOT_OK ||
                    b550_state.normal_collision_count != 1u ||
                    b550_state.normal_collision_generated_resultant_x != 327 ||
                    b550_state.normal_collision_generated_resultant_y != 60 ||
                    b550_state.normal_collision_pending != 0)
                    return driver_pilot_failure(__LINE__);
            }

            /* P122 captured Bowser transition: this is a separate muted mGBA
             * event from the Peach fixtures above.  Seed only the source
             * fields observed before B550, then assert the same B550 stores,
             * timer transition and AD6C flag clear at the native boundary. */
            {
                PortableDriverPilotState bowser_state = {0};

                bowser_state.input.speed_parameter = 5;
                bowser_state.auto_acceleration.driver_mode = 0;
                bowser_state.auto_acceleration.character_class = 3;
                bowser_state.source_update_order = 1;
                bowser_state.pose.position_x = INT32_C(0x00080000);
                bowser_state.vector_effect_previous_cell_key = 1;
                bowser_state.vector_effect_previous_cell_key_valid = 1;
                bowser_state.vector_effect_source_state = UINT16_C(0x8001);
                bowser_state.vector_effect_source_direction = 1;
                bowser_state.normal_collision_source_valid = 1;
                bowser_state.normal_collision_source_use_motion = 1;
                bowser_state.normal_collision_source_from_native_state = 1;
                bowser_state.normal_collision_source_use_b550_table = 1;
                bowser_state.normal_collision_timer_38 = 0;
                bowser_state.normal_collision_timer_62 = 8;
                bowser_state.motion.component_x = 268;
                bowser_state.motion.component_y = 246;
                bowser_state.motion.speed_q16 = INT32_C(0x016d8000);
                bowser_state.steering.state_flags = UINT32_C(0x0000c000);
                bowser_state.steering.angle = (-INT16_C(24787));
                bowser_state.steering.steering_offset =
                    (-INT16_C(24787));
                bowser_state.steering.base_angle =
                    (-INT16_C(13483));
                bowser_state.steering.response_status = UINT8_C(0x0b);
                bowser_state.normal_collision.resistance_92 =
                    INT16_C(0x0160);
                bowser_state.normal_collision.timer_value_84 = UINT8_C(0x0b);
                bowser_state.normal_collision.response_timer_7e =
                    UINT8_C(0x0b);
                bowser_state.normal_collision_source =
                    (PortableDriverNormalCollisionSourceInput){
                        .resultant_x = 268,
                        .resultant_y = 246,
                        .current_angle = UINT16_C(0x9f2d),
                        .source_flags = UINT32_C(0x0000c000),
                        .prior_angle_history_7a = UINT16_C(0x9f2d),
                        .prior_angle_delta_80 = UINT16_C(0xcb55),
                        .prior_resistance_92 = INT16_C(0x0160),
                        .prior_magnitude_state_bc = INT32_C(0x016d8000),
                        .prior_timer_value_84 = UINT8_C(0x0b),
                        .prior_response_timer_7e = UINT8_C(0x0b),
                    };

                if (portable_driver_pilot_tick(
                        &bowser_state, UINT16_C(0x8000), &dependencies) !=
                        PORTABLE_DRIVER_PILOT_OK ||
                    bowser_state.normal_collision_schedule_count != 1u ||
                    bowser_state.normal_collision_count != 1u ||
                    bowser_state.normal_collision_pending != 0u ||
                    bowser_state.normal_collision_timer_38 != 0u ||
                    bowser_state.normal_collision_timer_62 != 7u ||
                    bowser_state.vector_effect_source_state != 0u ||
                    bowser_state.motion.component_x != 134 ||
                    bowser_state.motion.component_y != 230 ||
                    bowser_state.normal_collision_generated_resultant_x !=
                        134 ||
                    bowser_state.normal_collision_generated_resultant_y !=
                        230 ||
                    bowser_state.steering.state_flags !=
                        UINT32_C(0x0000c000) ||
                    bowser_state.normal_collision.source_flags !=
                        UINT32_C(0x0000c000) ||
                    bowser_state.normal_collision.angle_history_7a !=
                        UINT16_C(0x9f2d) ||
                    bowser_state.normal_collision.base_heading_7c !=
                        UINT16_C(0x9f2d) ||
                    bowser_state.normal_collision.angle_delta_80 !=
                        UINT16_C(0xcb55) ||
                    bowser_state.normal_collision.magnitude_state_bc !=
                        INT32_C(0x00e60000) ||
                    bowser_state.normal_collision.timer_value_84 !=
                        UINT8_C(0x0b) ||
                    bowser_state.normal_collision.response_timer_7e !=
                        UINT8_C(0x0b))
                {
                    fputs("P122 captured Bowser B550 transition failed\n",
                          stderr);
                    return driver_pilot_failure(__LINE__);
                }
            }

            /* P102 matched-event fixture: seed only the already-written
             * source +0x4e/+0x50 pair and response state. The resultant
             * members below are deliberately zero because source-order
             * native production must read motion.component_x/y instead. */
            {
                PortableDriverPilotState matched_state = {0};

                matched_state.input.speed_parameter = 5;
                matched_state.auto_acceleration.driver_mode = 0;
                matched_state.auto_acceleration.character_class = 3;
                matched_state.source_update_order = 1;
                matched_state.pose.position_x = INT32_C(0x00080000);
                matched_state.vector_effect_previous_cell_key = 1;
                matched_state.vector_effect_previous_cell_key_valid = 1;
                matched_state.vector_effect_source_state = UINT16_C(0x8001);
                matched_state.normal_collision_source_valid = 1;
                matched_state.normal_collision_source_use_motion = 1;
                matched_state.motion.component_x = -628;
                matched_state.motion.component_y = 129;
                matched_state.normal_collision_source =
                    (PortableDriverNormalCollisionSourceInput){
                        .resultant_x = 0,
                        .resultant_y = 0,
                        .current_angle = UINT16_C(0x616c),
                        .source_flags = UINT32_C(0x0000d400),
                        .prior_angle_history_7a = UINT16_C(0x492c),
                        .prior_angle_delta_80 = UINT16_C(0xe7c0),
                        .prior_resistance_92 = INT16_C(0x0180),
                        .prior_magnitude_state_bc = INT32_C(0x02828000),
                        .prior_timer_value_84 = 0,
                        .prior_response_timer_7e = 0,
                    };
                if (portable_driver_pilot_tick(
                        &matched_state, UINT16_C(0x8100), &dependencies) !=
                    PORTABLE_DRIVER_PILOT_OK ||
                    matched_state.normal_collision_generated_resultant_x !=
                        -628 ||
                    matched_state.normal_collision_generated_resultant_y !=
                        129 ||
                    matched_state.normal_collision_response_angle_7a !=
                        UINT16_C(0xb7bf) ||
                    matched_state.normal_collision_base_heading_7c !=
                        UINT16_C(0x616c) ||
                    matched_state.normal_collision_response_delta_80 !=
                        UINT16_C(0x5653) ||
                    matched_state.normal_collision_response_output_c0 !=
                        -INT32_C(0x00550000) ||
                    matched_state.normal_collision_response_output_written !=
                        1u ||
                    matched_state.motion.component_x != 0 ||
                    matched_state.motion.component_y != 0)
                    return driver_pilot_failure(__LINE__);
            }

            /* P106 exact handler-stage fixture: the debugger stopped at
             * sub_803AD6C with (-314,120), then the source BIOS wrapper
             * returned 0xb120 and the handler produced 0x4fb4. The following
             * sub_803DAEC table stage writes 0xffab0000. The caller-owned
             * resultant remains zero. */
            {
                PortableDriverPilotState handler_matched_state = {0};

                handler_matched_state.input.speed_parameter = 5;
                handler_matched_state.auto_acceleration.driver_mode = 0;
                handler_matched_state.auto_acceleration.character_class = 3;
                handler_matched_state.source_update_order = 1;
                handler_matched_state.pose.position_x = INT32_C(0x00080000);
                handler_matched_state.vector_effect_previous_cell_key = 1;
                handler_matched_state.vector_effect_previous_cell_key_valid =
                    1;
                handler_matched_state.vector_effect_source_state =
                    UINT16_C(0x8001);
                handler_matched_state.normal_collision_source_valid = 1;
                handler_matched_state.normal_collision_source_use_motion = 1;
                handler_matched_state.motion.component_x = -314;
                handler_matched_state.motion.component_y = 120;
                handler_matched_state.normal_collision_source =
                    (PortableDriverNormalCollisionSourceInput){
                        .resultant_x = 0,
                        .resultant_y = 0,
                        .current_angle = UINT16_C(0x616c),
                        .source_flags = UINT32_C(0x0000d400),
                        .prior_angle_history_7a = UINT16_C(0x492c),
                        .prior_angle_delta_80 = UINT16_C(0xe7c0),
                        .prior_resistance_92 = INT16_C(0x0100),
                        .prior_magnitude_state_bc = INT32_C(0x013a0000),
                        .prior_timer_value_84 = 0,
                        .prior_response_timer_7e = 1,
                    };
                if (portable_driver_pilot_tick(
                        &handler_matched_state, UINT16_C(0x8100),
                        &dependencies) != PORTABLE_DRIVER_PILOT_OK ||
                    handler_matched_state.normal_collision_generated_resultant_x !=
                        -314 ||
                    handler_matched_state.normal_collision_generated_resultant_y !=
                        120 ||
                    handler_matched_state.normal_collision_response_angle_7a !=
                        UINT16_C(0xb120) ||
                    handler_matched_state.normal_collision_base_heading_7c !=
                        UINT16_C(0x616c) ||
                    handler_matched_state.normal_collision_response_delta_80 !=
                        UINT16_C(0x4fb4) ||
                    handler_matched_state.normal_collision_response_output_c0 !=
                        -INT32_C(0x00550000) ||
                    handler_matched_state.normal_collision_response_output_written !=
                        1u ||
                    handler_matched_state.normal_collision_response_next_status !=
                        UINT8_C(0x0b) ||
                    handler_matched_state.motion.component_x != 0 ||
                    handler_matched_state.motion.component_y != 0)
                    return driver_pilot_failure(__LINE__);
            }

            /* P114 source-owned input seam: use the native source-shaped
             * fields for the same P106 handler boundary. The explicit source
             * input struct is deliberately left zero so this catches any
             * fallback to caller metadata. */
            {
                PortableDriverPilotState native_state = {0};

                native_state.input.speed_parameter = 5;
                native_state.auto_acceleration.driver_mode = 0;
                native_state.auto_acceleration.character_class = 3;
                native_state.source_update_order = 1;
                native_state.pose.position_x = INT32_C(0x00080000);
                native_state.normal_collision_pending = 1;
                native_state.normal_collision_source_valid = 1;
                native_state.normal_collision_source_use_motion = 1;
                native_state.normal_collision_source_from_native_state = 1;
                native_state.motion.speed_q16 = INT32_C(0x013a0000);
                native_state.motion.component_x = -314;
                native_state.motion.component_y = 120;
                native_state.steering.state_flags = UINT32_C(0x0000d400);
                native_state.steering.angle =
                    (int16_t)UINT16_C(0x492c);
                native_state.steering.steering_offset =
                    (int16_t)UINT16_C(0x616c);
                native_state.steering.base_angle =
                    (-INT16_C(6208));
                native_state.steering.response_status = 1;
                native_state.normal_collision.resistance_92 =
                    INT16_C(0x0100);
                native_state.normal_collision.timer_value_84 = 0;
                native_state.normal_collision.response_timer_7e = 1;
                if (portable_driver_pilot_tick(
                        &native_state, UINT16_C(0x8100), &dependencies) !=
                        PORTABLE_DRIVER_PILOT_OK ||
                    native_state.normal_collision_source.resultant_x != -314 ||
                    native_state.normal_collision_source.resultant_y != 120 ||
                    native_state.normal_collision_source.current_angle !=
                        UINT16_C(0x616c) ||
                    native_state.normal_collision_source.source_flags !=
                        UINT32_C(0x0000d400) ||
                    native_state.normal_collision_source.prior_angle_history_7a !=
                        UINT16_C(0x492c) ||
                    native_state.normal_collision_source.prior_angle_delta_80 !=
                        UINT16_C(0xe7c0) ||
                    native_state.normal_collision_source.prior_resistance_92 !=
                        INT16_C(0x0100) ||
                    native_state.normal_collision_source.prior_magnitude_state_bc !=
                        INT32_C(0x013a0000) ||
                    native_state.steering.state_flags !=
                        UINT32_C(0x0000c400) ||
                    native_state.normal_collision_response_delta_80 !=
                        UINT16_C(0x4fb4))
                {
                    return driver_pilot_failure(__LINE__);
                }
            }

            /* P115 exact muted-ROM automatic-event fixture: the Thumb
             * debugger stopped at sub_803AD6C with (-279,209). Build every
             * source input field from the native source-shaped state and keep
             * the explicit metadata struct zero so this is a second event,
             * not a restatement of P106's caller tuple. */
            {
                PortableDriverPilotState captured_state = {0};

                captured_state.input.speed_parameter = 5;
                captured_state.auto_acceleration.driver_mode = 0;
                captured_state.auto_acceleration.character_class = 3;
                captured_state.source_update_order = 1;
                captured_state.pose.position_x = INT32_C(0x00080000);
                captured_state.normal_collision_pending = 1;
                captured_state.normal_collision_source_valid = 1;
                captured_state.normal_collision_source_use_motion = 1;
                captured_state.normal_collision_source_from_native_state = 1;
                captured_state.motion.speed_q16 = INT32_C(0x02598000);
                captured_state.motion.component_x = -279;
                captured_state.motion.component_y = 209;
                captured_state.steering.state_flags = UINT32_C(0x0000d400);
                captured_state.steering.angle = (int16_t)UINT16_C(0x5332);
                captured_state.steering.steering_offset =
                    (int16_t)UINT16_C(0x6032);
                captured_state.steering.base_angle =
                    (-INT16_C(5376));
                captured_state.steering.response_status = 1;
                captured_state.normal_collision.resistance_92 =
                    INT16_C(0x00a8);
                captured_state.normal_collision.timer_value_84 = 0;
                captured_state.normal_collision.response_timer_7e = 1;
                if (portable_driver_pilot_tick(
                        &captured_state, UINT16_C(0x8100), &dependencies) !=
                        PORTABLE_DRIVER_PILOT_OK ||
                    captured_state.normal_collision_source.resultant_x != -279 ||
                    captured_state.normal_collision_source.resultant_y != 209 ||
                    captured_state.normal_collision_source.current_angle !=
                        UINT16_C(0x6032) ||
                    captured_state.normal_collision_source.source_flags !=
                        UINT32_C(0x0000d400) ||
                    captured_state.normal_collision_source.prior_angle_history_7a !=
                        UINT16_C(0x5332) ||
                    captured_state.normal_collision_source.prior_angle_delta_80 !=
                        UINT16_C(0xeb00) ||
                    captured_state.normal_collision_source.prior_resistance_92 !=
                        INT16_C(0x00a8) ||
                    captured_state.normal_collision_source.prior_magnitude_state_bc !=
                        INT32_C(0x02598000) ||
                    captured_state.normal_collision_response_angle_7a !=
                        UINT16_C(0xa5ce) ||
                    captured_state.normal_collision_base_heading_7c !=
                        UINT16_C(0x6032) ||
                    captured_state.normal_collision_response_delta_80 !=
                        UINT16_C(0x459c) ||
                    captured_state.normal_collision.source_flags !=
                        UINT32_C(0x0000c400) ||
                    captured_state.steering.state_flags !=
                        UINT32_C(0x0000c400) ||
                    captured_state.normal_collision.resistance_92 !=
                        INT16_C(0x0100) ||
                    captured_state.normal_collision.magnitude_state_bc !=
                        INT32_C(0x01170000) ||
                    captured_state.normal_collision.timer_value_84 !=
                        UINT8_C(0x0b) ||
                    captured_state.normal_collision.response_timer_7e !=
                        UINT8_C(0x0b))
                {
                    return driver_pilot_failure(__LINE__);
                }
            }
        }

        material_flags[1] = UINT8_C(0x80);
        alternate_state.input.speed_parameter = 5;
        alternate_state.auto_acceleration.driver_mode = 0;
        alternate_state.auto_acceleration.character_class = 3;
        alternate_state.forward_acceleration.state_flags = 0;
        alternate_state.pose.position_x = INT32_C(0x00080000);
        alternate_state.vector_effect_previous_cell_key = 0;
        alternate_state.vector_effect_previous_cell_key_valid = 1;
        alternate_state.vector_effect_source_state = UINT16_C(0x4000);
        alternate_state.vector_effect_source_direction = UINT16_C(0x4000);
        if (portable_driver_pilot_tick(
                &alternate_state, UINT16_C(0x8000), &dependencies) !=
                PORTABLE_DRIVER_PILOT_OK ||
            alternate_state.vector_trigger_count != 1u ||
            alternate_state.vector_effect_count != 1u ||
            alternate_state.vector_effect_pending != 0 ||
            alternate_state.vector_trigger.alternate_state_branch != 1 ||
            alternate_state.vector_effect.alternate_state_branch != 1 ||
            alternate_state.vector_effect.source_direction_index != 1 ||
            alternate_state.vector_effect.contact_word_5a != 0 ||
            alternate_state.motion.component_x != 256 ||
            alternate_state.motion.component_y != 256)
            return driver_pilot_failure(__LINE__);
    }
    memset(cells, 0x7f, sizeof(cells));
    memset(material_flags, 0, sizeof(material_flags));
    memset(terrain_cells, 0, sizeof(terrain_cells));

    brake_state = repeat;
    if (portable_driver_pilot_tick(&brake_state, UINT16_C(0x4000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        brake_state.input.control_word != UINT16_C(0x4000) ||
        brake_state.motion.acceleration_q16 != -INT32_C(0x00040000) ||
        brake_state.motion.speed_q16 != INT32_C(0x00050000))
    {
        return driver_pilot_failure(__LINE__);
    }

    steering_state = repeat;
    for (size_t index = 0; index < 4; ++index)
    {
        if (portable_driver_pilot_tick(&steering_state, UINT16_C(0x8000),
                                       &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK)
        {
            return driver_pilot_failure(__LINE__);
        }
    }
    fprintf(stderr, "pilot steering probe before: angle=0x%04x speed=0x%08x control=0x%04x\\n",
            (uint16_t)steering_state.motion.angle,
            (uint32_t)steering_state.motion.speed_q16,
            steering_state.input.control_word);
    if (portable_driver_pilot_tick(&steering_state, UINT16_C(0x8100),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_OK ||
        steering_state.motion.angle != 0x3000)
    {
        return driver_pilot_failure(__LINE__);
    }

    before_error = state;
    dependencies.brake_table_count = 7;
    if (portable_driver_pilot_tick(&state, UINT16_C(0x4000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR ||
        state.motion.speed_q16 != before_error.motion.speed_q16 ||
        state.pose.position_x != before_error.pose.position_x +
                                     (int32_t)before_error.pose.component_x *
                                         0x100 ||
        state.pose.position_y != before_error.pose.position_y +
                                     (int32_t)before_error.pose.component_y *
                                         0x100)
    {
        return driver_pilot_failure(__LINE__);
    }
    dependencies.brake_table_count = 8;
    if (portable_driver_pilot_tick(NULL, 0, &dependencies) !=
            PORTABLE_DRIVER_PILOT_INVALID_ARGUMENT)
    {
        return driver_pilot_failure(__LINE__);
    }

    dependencies.sine_table = NULL;
    if (portable_driver_pilot_tick(&state, UINT16_C(0x8000),
                                   &dependencies) !=
            PORTABLE_DRIVER_PILOT_MOTION_ERROR)
    {
        return driver_pilot_failure(__LINE__);
    }

    /* FD7C uses the ROM's E760 clamp, halves toward zero, and crushes only
     * when the block is strictly above one unit and descending. */
    {
        static const int16_t inputs[]={INT16_MIN,-1024,-1023,-257,-129,-128,0,128,129,257,1023,1024,INT16_MAX};
        static const int16_t outputs[]={512,512,511,128,64,64,64,-64,-64,-128,-511,-512,-512};
        for(size_t i=0;i<sizeof inputs/sizeof inputs[0];++i){
            PortableDriverPilotState hit={0};
            hit.motion.component_x=inputs[i];hit.motion.component_y=inputs[i];
            portable_driver_pilot_thwomp_hit(&hit,65536,-0x40000);
            if(hit.flattened_64 || hit.motion.component_x!=outputs[i] ||
               hit.pose.component_y!=outputs[i])return driver_pilot_failure(__LINE__);
            portable_driver_pilot_thwomp_hit(&hit,65537,0);
            if(hit.flattened_64)return driver_pilot_failure(__LINE__);
        }
        PortableDriverPilotState hit={0};
        hit.motion.component_x=-256;hit.motion.component_y=256;
        hit.steering.response_q8=42;hit.forward_acceleration.response_q8=42;
        hit.forward_acceleration.state_flags=0xffff;
        portable_driver_pilot_thwomp_hit(&hit,65537,-1);
        if(!hit.flattened_64 || hit.normal_collision_timer_38!=0x7000u ||
           hit.motion.component_x || hit.pose.component_y || hit.steering.response_q8 ||
           hit.forward_acceleration.state_flags!=0xffd3u)return driver_pilot_failure(__LINE__);
    }

    {
        PortableDriverPilotState effect={0};
        FINISHED_CHECK(portable_driver_effects_visible(&effect));
        for(uint8_t mode=3u;mode<=4u;++mode){
            effect.hop.mode=mode;effect.pose.position_z=0;
            FINISHED_CHECK(!portable_driver_effects_visible(&effect));
            effect.pose.position_z=1;FINISHED_CHECK(portable_driver_effects_visible(&effect));
        }
        effect.hop.mode=8u;effect.drift_charge.charge_counter=60u;
        FINISHED_CHECK(!portable_driver_effects_visible(&effect));
        effect.drift_charge.charge_counter=61u;FINISHED_CHECK(portable_driver_effects_visible(&effect));
        effect.hop.mode=2u;effect.drift_charge.charge_counter=46u;
        FINISHED_CHECK(portable_driver_effects_visible(&effect));
        effect.drift_charge.charge_counter=45u;FINISHED_CHECK(portable_driver_effects_visible(&effect));
        effect.fall_frames_76=15u;FINISHED_CHECK(!portable_driver_effects_visible(&effect));
        effect.hop.mode=6u;effect.hop.retrieval_started=1u;
        effect.source_hazard_kind=PORTABLE_DRIVER_HAZARD_VOID;
        FINISHED_CHECK(!portable_driver_effects_visible(&effect));
        effect.hop.retrieval_started=0u;FINISHED_CHECK(!portable_driver_effects_visible(&effect));
        effect.fall_frames_76=0u;FINISHED_CHECK(portable_driver_effects_visible(&effect));
        effect.hop.retrieval_started=1u;effect.source_hazard_kind=PORTABLE_DRIVER_HAZARD_LAVA;
        FINISHED_CHECK(portable_driver_effects_visible(&effect));
        FINISHED_CHECK(!portable_driver_effects_visible(NULL));
    }
    puts("portable driver pilot: PASS");
    return 0;
}
