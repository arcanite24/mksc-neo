#include "source_random.h"
#include "recovered_time_trial_session.h"
#include "surface_trail_driver.h"

#include <string.h>
#include "thwomp_motion.h"
#include "course_prop_motion.h"
#include "crab_motion.h"
#include "seagull_motion.h"
#include "piranha_motion.h"
#include "snowman_motion.h"
#include "mouse_motion.h"
#include "thundercloud_motion.h"
#include "falling_star_motion.h"
#include "penguin_motion.h"
#include "boo_motion.h"
#include "cannonball_motion.h"
#include "collision.h"
#include "driver_palette.h"
#include "course_contacts.h"

static int collide_course_actors(PortableRecoveredTimeTrialSession *session,
    PortableCourseContactEvents *events)
{
    PortableCourseContacts world={.driver=&session->driver,
        .dependencies=session->runtime.recovered_dependencies,
        .course_props=&session->course_props,
        .lava_bubbles=&session->lava_bubbles,.thwomps=&session->thwomps,
        .crabs=&session->crabs,.seagulls=&session->seagulls,.cannonballs=&session->cannonballs,
        .piranhas=&session->piranhas,.snowmen=&session->snowmen,.mice=&session->mice,.thunderclouds=&session->thunderclouds,.falling_stars=&session->falling_stars,.penguins=&session->penguins,
        .boos=&session->boos,.piranha_sounds=&session->piranha_sounds,
        .impact_sounds=&session->impact_sounds,
        .driver_class=(uint8_t)(session->voice_override!=0u?session->voice_override-1u:
            session->runtime.session.compatibility.character_class),
        .listener_flags=session->driver.steering.state_flags,
        .listener_x=session->driver.pose.position_x,.listener_y=session->driver.pose.position_y,
        .scene_clock=session->scene_clock,.course_id=session->runtime.session.compatibility.course_id,
        .checkpoint_count=(uint16_t)session->runtime.session.route_count,.mode_name="Time Trial",.is_player=1u};
    if(!portable_course_contacts_collide(&world))return 0;
    *events=world.events;
    return 1;
}

/* 59FAC/48C2C start a haunt; 7B30 shortens its lock on accelerator presses. */
static int tick_haunts(PortableRecoveredTimeTrialSession *session,
    const PortableCourseContactEvents *events,uint16_t source_control_word)
{
    if(events->ghost_invalidations)session->ghost_recording_valid=0u;
    /* 805ACFC's smoke emitter belongs to the unmuted local player only. */
    if(events->burns && !(session->driver.steering.state_flags&0x20u))
        portable_lava_smoke_start(&session->lava_smoke);
    for(uint8_t n=0u;n<events->boo_haunts;++n){
        size_t slot=0u;
        while(slot<PORTABLE_BOO_ATTACK_CAPACITY && session->haunts[slot].occupied)++slot;
        if(slot==PORTABLE_BOO_ATTACK_CAPACITY)return 0;
        session->haunts[slot]=(PortableBooAttack){.occupied=1u};
    }
    PortableDriverPilotState *d=&session->driver;
    const int accelerate_pressed=(source_control_word&0x8000u) &&
        !(session->previous_source_control&0x8000u);
    session->previous_source_control=source_control_word;
    if(d->item_lock_ac){
        if(d->invulnerability_timer_6a!=0 || (int16_t)d->strong_spin_rate_b6>255)d->item_lock_ac=0u;
        else if(accelerate_pressed)d->item_lock_ac=d->item_lock_ac>9u?(uint16_t)(d->item_lock_ac-10u):0u;
    }
    for(size_t i=0u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i){
        PortableBooAttack *a=&session->haunts[i];
        if(!a->occupied)continue;
        PortableBooAttackTarget target={.x_q16=d->pose.position_x,.y_q16=d->pose.position_y,
            .z_q16=d->pose.position_z,.component_x=d->motion.component_x,.component_y=d->motion.component_y,
            .heading=(uint16_t)d->steering.steering_offset,.protection=(int16_t)d->invulnerability_timer_6a,
            .lock_ticks=d->item_lock_ac};
        const uint8_t before=a->phase;
        if(!portable_boo_attack_tick(a,&target,session->runtime.recovered_dependencies->sine_table,
               session->runtime.recovered_dependencies->sine_count,session->boo_rules))return 0;
        d->item_lock_ac=target.lock_ticks;
        const int effect=portable_boo_attack_music_transition(before,a->phase);
        if(effect>0)session->haunt_music=1u;
        else if(effect<0)session->haunt_music=0u;
    }
    return 1;
}

/* 58AB0/58DE8 run with the passive callbacks; the Shy Guy holds +AC. */
static int tick_teepees(PortableRecoveredTimeTrialSession *session)
{
    PortableDriverPilotState *d=&session->driver;
    PortableTeepeeKart kart={.x_q16=d->pose.position_x,.y_q16=d->pose.position_y,
        .z_q16=d->pose.position_z,.speed_q16=d->motion.speed_q16,
        .component_x=d->motion.component_x,.component_y=d->motion.component_y,
        .protection=(int16_t)d->invulnerability_timer_6a,.heading=(uint16_t)d->steering.steering_offset,
        .lock=d->item_lock_ac,.is_player=1u,.muted=(uint8_t)((d->steering.state_flags&0x20u)!=0u)};
    const int cues=portable_course_props_tick_teepees(&session->course_props,&kart,1u,
        session->runtime.recovered_dependencies->sine_table,
        session->runtime.recovered_dependencies->sine_count,session->scene_clock);
    if(cues<0)return 0;
    d->item_lock_ac=kart.lock;
    if(cues)d->contact_audio=(PortableDriverContactAudio){.serial=d->contact_audio.serial+1u,
        .song_id=372u,.volume=256u,.restart=1u};
    return 1;
}

static void reset_character_camera(PortableRecoveredTimeTrialSession *session,
                                   uint16_t heading)
{
    portable_character_camera_init(&session->character_camera, heading);
    session->finish_camera=(PortableFinishCamera){0};
}

static int reset_ghost_capture(PortableRecoveredTimeTrialSession *session)
{
    const PortableRaceCompatibilityId *identity =
        &session->runtime.session.compatibility;

    portable_time_trial_ghost_record_init(
        session->ghost_record, (uint8_t)identity->character_class,
        (uint8_t)identity->course_id);
    portable_time_trial_ghost_record_set_roster_driver(
        session->ghost_record, (uint8_t)identity->roster_driver_id);
    session->ghost_recording_valid = (uint8_t)
        portable_time_trial_ghost_encoder_init(
            &session->ghost_encoder,
            session->ghost_record + PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
            PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE);
    return session->ghost_recording_valid != 0u;
}

static void record_ghost_control(PortableRecoveredTimeTrialSession *session,
                                 uint16_t source_control_word)
{
    if (session->ghost_recording_valid != 0u &&
        !portable_time_trial_ghost_encoder_append(
            &session->ghost_encoder, source_control_word))
        session->ghost_recording_valid = 0u;
}

static PortableRecoveredTimeTrialStatus apply_rescue(
    PortableRecoveredTimeTrialSession *session,
    int invalidate_ghost);

static uint16_t time_trial_camera_yaw(const PortableRecoveredTimeTrialSession *session)
{
    return session->finish_camera.snapshot.active?session->finish_camera.snapshot.yaw:
        portable_character_camera_yaw(&session->character_camera);
}

/* main_getRandom uses the source visual LCG, not the item/AI LCG owner.
 * Native race entry/retry seed zero; ordinary frame ticks never burn it. */
static uint32_t main_visual_random(void *context)
{ return portable_source_random((uint32_t *)context); }

static int tick_character_camera(PortableRecoveredTimeTrialSession *session)
{
    if(session->finish_rules!=NULL && session->runtime.session.phase==PORTABLE_RACE_PHASE_FINISHED &&
       !session->finish_camera.phase){
        /* B7B8 retired the selected driver inside the progress callback,
         * before the same turn selected its autonomous motion branch. */
        portable_finish_camera_start(&session->finish_camera);
    }
    const PortableCharacterCameraInput input = {
        .driver_heading_7c =
            (uint16_t)session->driver.steering.steering_offset,
        .response_status_7e = session->driver.steering.response_status,
        .steering_position_82 = session->driver.steering.position_82,
        .control_word_96 = session->driver.steering.control_word,
    };
    if(!session->finish_camera.phase || session->finish_camera.phase==8u)
        if(!portable_character_camera_tick(&session->character_camera,&input))return 0;
    if(session->finish_rules!=NULL){
        const PortableFinishCameraInput finish={
            .driver_x_q16=session->driver.pose.position_x,.driver_y_q16=session->driver.pose.position_y,
            .height_q16=session->driver.pose.position_z,.driver_angle_56=session->driver.steering.source_angle_56,
            .normal_x_q16=session->driver.pose.position_x,.normal_y_q16=session->driver.pose.position_y,
            .normal_yaw=portable_character_camera_yaw(&session->character_camera),.mode_flags_7da=0x200u,
            .driver_mode_78=session->driver.hop.mode,.rank=0u,
            .material_85=session->driver.source_material_85,.bob_phase=session->driver.art_motion.bob_phase};
        if(portable_finish_camera_tick(&session->finish_camera,&session->finish_rules->camera,&finish,
                session->runtime.recovered_dependencies->sine_table,
                session->runtime.recovered_dependencies->sine_count)<0)return 0;
    }
    return 1;
}

static int recovering(const PortableRecoveredTimeTrialSession *session)
{
    return session->driver.source_hazard_kind ||
        (session->driver.source_terrain22_active && session->driver.hop.mode >= 3u);
}

uint16_t portable_recovered_time_trial_source_control(
    uint16_t semantic_buttons)
{
    return portable_driver_control_word_from_semantic_buttons(
        semantic_buttons);
}

static int tick_ambient(PortableRecoveredTimeTrialSession *session,uint8_t stage,
    const PortableSkyMotion *before)
{
    const PortableRaceSession *race=&session->runtime.session;
    const PortableAmbientInputs inputs={.clock=session->scene_clock,
        .selected_flags=session->driver.steering.state_flags,
        .selected_speed_q16=session->driver.motion.speed_q16,
        .selected_base_speed_q16=session->driver.input.base_speed_q16,
        .selected_progress=race->max_progress_raw,.leader_progress=race->max_progress_raw,
        .leader_route_wrap=race->lap_raw,
        .race_phase_7d4=race->phase==PORTABLE_RACE_PHASE_RACING?3u:0u,
        .sky_phase_before=before->phase,.sky_phase_after=session->sky_motion.phase,
        .sky_elapsed_before=before->elapsed,.sky_counter_before=before->transition_counter};
    return portable_ambient_tick(&session->ambient,session->ambient_rules,stage,&inputs,
        session->runtime.recovered_dependencies->sine_table,
        session->runtime.recovered_dependencies->sine_count)>=0;
}

static int tick_sky_motion(PortableRecoveredTimeTrialSession *session, int initialize)
{
    const PortableSkyMotion before=session->sky_motion;
    const PortableRaceSession *race=&session->runtime.session;
    const PortableSkyMotionInputs inputs={.clock=session->scene_clock,
        .yaw=time_trial_camera_yaw(session),
        .vertical_offset=session->finish_camera.snapshot.active?session->finish_camera.snapshot.vertical_offset:
            portable_sky_motion_camera_offset(session->driver.pose.position_z,
            session->driver.source_material_85,session->driver.art_motion.bob_phase),
        .vertical_offset_q16=session->finish_camera.snapshot.active?
            (int32_t)session->finish_camera.snapshot.vertical_offset*65536:
            portable_sky_motion_camera_offset_q16(session->driver.pose.position_z,
                session->driver.source_material_85,session->driver.art_motion.bob_phase),
        .precise_vertical_offset=1u,
        .selected_progress=race->max_progress_raw,.leader_progress=race->max_progress_raw,
        .leader_route_index=race->checkpoint_raw,.leader_route_wrap=race->lap_raw,
        .lightning_phase=(uint8_t)(session->driver.lightning_timer_68>0)};
    if((initialize?portable_sky_motion_init:portable_sky_motion_tick)(&session->sky_motion,session->sky_motion_rules,&inputs,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count)<0)return 0;
    return initialize || tick_ambient(session,PORTABLE_AMBIENT_PRE_PROFILE,&before);
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_init(
    PortableRecoveredTimeTrialSession *session,
    const PortableRecoveredRaceView *race)
{

    if (session == NULL || race == NULL)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_mushroom_rules_valid(race->dependencies.mushroom_rules))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    memset(session, 0, sizeof *session);
    if(!portable_course_props_init_with_random(&session->course_props,race->course_props,race->course_prop_count,
            main_visual_random,&session->main_visual_rng_state))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_lava_bubble_course_init(&session->lava_bubbles,race->lava_bubbles,
                                         race->lava_bubble_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_thwomp_course_init(&session->thwomps,race->thwomps,race->thwomp_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_crab_course_init(&session->crabs,race->crabs,race->crab_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_seagull_course_init(&session->seagulls,race->seagulls,race->seagull_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_cannonball_course_init(&session->cannonballs,race->cannonballs,race->cannonball_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_piranha_course_init(&session->piranhas,race->piranhas,race->piranha_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_mouse_course_init(&session->mice,race->mice,race->mouse_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_thundercloud_course_init(&session->thunderclouds,race->thunderclouds,race->thundercloud_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_falling_star_course_init(&session->falling_stars,race->falling_stars,race->falling_star_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_snowman_course_init(&session->snowmen,race->snowmen,race->snowman_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_penguin_course_init(&session->penguins,race->penguins,race->penguin_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_boo_course_init(&session->boos,race->boos,race->boo_count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    session->driver = race->initial_driver;
    portable_lap_hud_init(&session->lap_hud);
    session->driver.impact_selected=1u;session->driver.impact_x=120;session->driver.impact_y=140;session->driver.impact_depth=256;
    session->sky_motion_rules=race->sky_motion_rules;
    session->ambient_rules=race->ambient_rules;
    session->finish_rules=race->finish_rules;
    session->weather_profile=race->weather_profile;
    session->recovery_fx_rules=race->recovery_fx_rules;
    session->surface_trail_rules=race->surface_trail_rules;
    session->icy_recovery=(uint8_t)(race->icy_recovery && race->recovery_fx_rules!=NULL);
    if(race->ambient_seed!=NULL)session->ambient_seed=*race->ambient_seed;
    if(portable_ambient_init(&session->ambient,session->ambient_rules,&session->ambient_seed)<0)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    session->driver.recovery_contact_34|=UINT16_C(3);
    session->driver.steering.time_trial_mode = 1u;
    session->driver.steering.state_flags|=UINT32_C(0x8000);
    session->driver.source_update_order = 1u;
    session->driver.allow_track_boundary = 1u;
    session->driver.source_hazard_enabled = 1u;
    session->driver.source_terrain22_enabled = 1u;
    session->recovery_bob_offsets = race->recovery_bob_offsets;
    session->finish_lakitu_rules = race->finish_lakitu_rules;
    session->boo_rules = race->dependencies.boo_rules;
    session->mushrooms_remaining = 3u;
    portable_character_camera_init(
        &session->character_camera,
        (uint16_t)session->driver.steering.steering_offset);
    if (portable_race_runtime_init_recovered(
            &session->runtime, &race->rules, &session->driver,
            &race->dependencies) != PORTABLE_RACE_RUNTIME_OK)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    session->voice_override = race->voice_override;
    if(portable_snow_weather_init(&session->snow_weather,
            session->weather_profile?&session->weather_profile->rules:NULL,
            session->driver.pose.position_x,session->driver.pose.position_y,
            time_trial_camera_yaw(session),1)<0)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    session->runtime.recovered_driver = &session->driver;
    if (!tick_sky_motion(session,1))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if (!reset_ghost_capture(session))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    return PORTABLE_RECOVERED_TIME_TRIAL_OK;
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_tick(
    PortableRecoveredTimeTrialSession *session,
    uint16_t semantic_buttons,
    PortableRaceTickDisposition *disposition)
{
    uint16_t source_control_word;

    if (session == NULL || disposition == NULL)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    source_control_word =
        portable_recovered_time_trial_source_control(semantic_buttons);
    if ((semantic_buttons & PORTABLE_INPUT_HOP) != 0u &&
        (session->previous_semantic_buttons & PORTABLE_INPUT_HOP) == 0u)
        source_control_word |= UINT16_C(0x0004);
    session->previous_semantic_buttons = semantic_buttons;
    return portable_recovered_time_trial_tick_source(
        session, source_control_word, disposition);
}

/* Mod scripts may cancel the mushroom about to be used (item IDs 1008,
 * 100F and 100E for three, two and one left). */
static int script_cancels_mushroom(const PortableRecoveredTimeTrialSession *session)
{
    static const uint16_t held[4] = {0u, UINT16_C(0x100e), UINT16_C(0x100f), UINT16_C(0x1008)};
    const PortableRaceScriptHooks *hooks = session->script_hooks;
    return hooks != NULL && hooks->item_use != NULL && session->mushrooms_remaining <= 3u &&
        hooks->item_use(hooks->context, 0u, held[session->mushrooms_remaining]) == 0u;
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_tick_source(
    PortableRecoveredTimeTrialSession *session,
    uint16_t source_control_word,
    PortableRaceTickDisposition *disposition)
{
    uint8_t item_control;
    uint8_t queue_item;
    int record_control;

    if (session == NULL || disposition == NULL)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    /* Keep the crossing tick, but post-finish driving cannot extend or
     * invalidate the completed ghost while its result board is open. */
    record_control=session->runtime.session.phase!=PORTABLE_RACE_PHASE_FINISHED;
    if (session->script_item_swallowed && !session->paused) {
        if (source_control_word & UINT16_C(0x0040))
            source_control_word &= UINT16_C(0xffbf);
        else
            session->script_item_swallowed = 0u;
    }
    item_control = (source_control_word & UINT16_C(0x0040)) != 0u;
    session->runtime.course_props=&session->course_props;
    session->runtime.piranhas=&session->piranhas;
    if (session->paused)
    {
        *disposition = session->runtime.session.phase ==
                               PORTABLE_RACE_PHASE_FINISHED
                           ? PORTABLE_RACE_TICK_FINISHED
                           : PORTABLE_RACE_TICK_INVALID;
        return PORTABLE_RECOVERED_TIME_TRIAL_OK;
    }
    session->driver.impact_event=0u;
    portable_lap_hud_step(&session->lap_hud,session->runtime.session.max_progress_raw,
        session->driver.steering.state_flags);
    if(session->runtime.recovered_dependencies->impact_effect_rules){
        PortableCameraProjectionResult body;
        if(portable_selected_driver_body(&session->driver,portable_character_camera_yaw(&session->character_camera),
                &session->finish_camera.snapshot,session->sky_motion.initialized?(int8_t)(session->sky_motion.snapshot.road_line-74):0,
                session->runtime.recovered_dependencies->sine_table,
                session->runtime.recovered_dependencies->sine_count,&body)){
            session->driver.impact_x=body.x;session->driver.impact_y=body.y;session->driver.impact_depth=(int16_t)body.distance;
        }else if(!(session->driver.steering.state_flags&UINT32_C(0x20)))
            return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    }
    const int finish_camera_active=session->finish_camera.snapshot.active;
    const PortableSnowWeatherInputs weather_input={
        .camera_x_q16=finish_camera_active?session->finish_camera.snapshot.x_q16:session->driver.pose.position_x,
        .camera_y_q16=finish_camera_active?session->finish_camera.snapshot.y_q16:session->driver.pose.position_y,
        .yaw=time_trial_camera_yaw(session),.parity=(uint8_t)((session->scene_clock+1u)&1u)};
    portable_entry_splash_tick(&session->entry_splash);
    ++session->scene_clock;
    if(!tick_sky_motion(session,0))return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    ++session->impact_sounds.serial;session->impact_sounds.count=0u;
    ++session->piranha_sounds.serial;
    session->piranha_sounds.count=0u;
    queue_item = (uint8_t)(
        session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
        !recovering(session) &&
        item_control && !session->previous_item_control);
    session->driver.source_route_index_90 = session->runtime.session.progress_kind ==
        PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1
            ? (uint8_t)(session->runtime.session.checkpoint_raw & 127u)
            : (uint8_t)session->runtime.session.next_route_index;
    /* play.c: 4978C/4AEFC collect and collide the entry poses before
     * 51014 consumes recovery requests and 49850 advances the driver.
     * Passive motion callbacks run later in 49850, after C4C0. */
    PortableCourseContactEvents contact_events={0};
    if (session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
        !collide_course_actors(session,&contact_events))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if (!tick_haunts(session,&contact_events,source_control_word))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    const int16_t prior_shrink=session->driver.lightning_timer_68;
    if(session->driver.lightning_timer_68>0)--session->driver.lightning_timer_68;
    if(session->driver.lightning_timer_68<0)session->driver.lightning_timer_68=0;
    if(prior_shrink==1 && portable_driver_affine_request(&session->driver.affine,
            session->runtime.recovered_dependencies->affine_rules,4u)<0)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(prior_shrink==1 && !(session->driver.steering.state_flags&0x20u))
        session->driver.contact_audio=(PortableDriverContactAudio){
            .serial=session->driver.contact_audio.serial+1u,.song_id=133u,.volume=256u,.restart=1u};
    if(session->driver.lightning_timer_68)session->driver.item_cue_flags_58&=UINT16_C(0xffef);
    else if(prior_shrink)session->driver.item_cue_flags_58|=UINT16_C(0x10);
    const int16_t prior_projected_y = (int16_t)(140 -
        session->driver.pose.position_z / 65536);
    session->driver.source_route_check_due=(uint8_t)((session->scene_clock&7u)==0u);
    /* 51014 consumes preceding requests before the driver update. */
    if(portable_gp_finish_lakitu_consume(&session->hazard_lakitu,
            &session->driver.hop.recovery_requests,session->finish_lakitu_rules)!=1)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;

    if (portable_gp_recovery_lakitu_consume(&session->hazard_lakitu,
            &session->driver.hop.recovery_requests,
            session->driver.steering.state_flags) != 1)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    const int32_t entry_splash_height = session->driver.pose.position_z;
    session->last_runtime_status = portable_race_runtime_tick(
        &session->runtime, source_control_word, disposition);
    if (session->last_runtime_status != PORTABLE_RACE_RUNTIME_OK)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    /* 7FD8/7D6C dispatches after the response pass. Its 10800 gate
     * rejects airborne/throw-locked presses; A9AC separately gates requests
     * already queued by an eligible constructor. */
    if (queue_item && session->mushrooms_remaining != 0u &&
        session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
        !recovering(session) &&
        !(session->driver.steering.state_flags & UINT32_C(0x10800)))
    {
        if (script_cancels_mushroom(session)) {
            /* The item bit is read nowhere else, so the ghost and the
             * finished replay see an unpressed button from here on. */
            session->script_item_swallowed = 1u;
            source_control_word &= UINT16_C(0xffbf);
            item_control = 0u;
        } else {
            --session->mushrooms_remaining;
            session->driver.item_requests_b2 |= UINT16_C(0x8000);
        }
    }
    session->previous_item_control = item_control;
    if(record_control && session->runtime.session.phase==PORTABLE_RACE_PHASE_FINISHED &&
       session->finish_lakitu_rules)
        session->driver.hop.recovery_requests|=UINT16_C(0x4000);
    if(session->runtime.session.phase==PORTABLE_RACE_PHASE_FINISHED){
        /* DA54/41F18 selects the finished tuning row after this driver turn;
         * A878 retires the final-lap cue before the following frame. */
        if((session->scene_clock&7u)==0u)session->driver.source_steering_selector_9a=3u;
        session->driver.steering.state_flags&=~UINT32_C(0x8);
    }
    if (!tick_character_camera(session))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if (session->runtime.boundary_exit)
    {
        if (apply_rescue(session, 0) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK)
            return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
        session->runtime.boundary_exit = 0u;
    }
    if (recovering(session) && session->driver.hop.recovery_started)
    {
        if (session->driver.source_hazard_kind == PORTABLE_DRIVER_HAZARD_LAVA) {
            portable_entry_splash_start(&session->entry_splash,entry_splash_height);
            session->hazard_effect_72 = 3u;
            session->driver.burn_ticks_72=PORTABLE_DRIVER_BURN_TICKS;
            if(!(session->driver.steering.state_flags&UINT32_C(0x20)))
                portable_lava_smoke_start(&session->lava_smoke);
        } else if (session->driver.source_terrain22_active) {
            portable_entry_splash_start(&session->entry_splash,entry_splash_height);
            if(session->recovery_fx_rules){
                if(!portable_recovery_fx_request(&session->recovery_fx,session->recovery_fx_rules,
                    &session->hazard_effect_72,session->icy_recovery?11u:13u,
                    !(session->driver.steering.state_flags&0x20u)))return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
            }else session->hazard_effect_72=13u;
            session->driver.burn_ticks_72=0u;
            session->driver.palette_requests_194&=UINT16_C(0x97ff);
        }
        ++session->runtime.session.rescue_count;
        session->driver.item_requests_b2 = 0u;
        session->previous_item_control = 0u;
    }
    if ((session->driver.source_hazard_kind || session->driver.source_terrain22_active) &&
        session->driver.hop.recovery_touchdown)
    {
        size_t index = session->driver.source_route_index_90;
        if (index == session->runtime.session.route_count - 1u)
            --index;
        session->runtime.session.next_route_index = index + 1u;
    }
    /* AF80 ends the water effect when9904 launches the escape hop. */
    if (((session->hazard_effect_72 & 15u) == 13u ||
         (session->hazard_effect_72 & 15u) == 11u) &&
        (session->driver.hop.animation_flags & UINT16_C(0x8000)))
        session->hazard_effect_72 &= UINT16_C(0x100);
    /* B64 changes height after the driver; newly raised requests wait for
     * the next pre-driver pass. Projection affects only actor presentation. */
    uint16_t consumed_requests = 0u;
    if(session->hazard_lakitu.phase==PORTABLE_GP_LAKITU_FINISH &&
        portable_gp_finish_lakitu_tick(&session->hazard_lakitu,
            session->finish_lakitu_rules,session->recovery_bob_offsets,
            session->runtime.recovered_dependencies->sine_table)!=1)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if (portable_gp_recovery_lakitu_tick(&session->hazard_lakitu,
            &consumed_requests, session->driver.steering.state_flags, prior_projected_y,
            &session->driver.pose.position_z, &session->hazard_effect_72,
            session->recovery_bob_offsets,
            session->runtime.recovered_dependencies->sine_table) != 1)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    session->driver.hop.height_q16 = session->driver.pose.position_z;
    if(session->hazard_lakitu.retrieval_effect_started && session->recovery_fx_rules &&
       !portable_recovery_fx_request(&session->recovery_fx,session->recovery_fx_rules,
           &session->hazard_effect_72,(uint8_t)(session->hazard_effect_72&15u),
           !(session->driver.steering.state_flags&0x20u)))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    session->driver.hop.recovery_requests&=(uint16_t)~session->hazard_lakitu.cleared_requests;
    const PortableCoursePropListener prop_listener={.x_q16=session->driver.pose.position_x,
        .y_q16=session->driver.pose.position_y,.flags=session->driver.steering.state_flags,
        .sounds=&session->prop_sounds};
    if(!portable_course_prop_motion_tick(&session->course_props,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,
            session->runtime.session.compatibility.course_id,"Time Trial",&prop_listener) ||
       !tick_teepees(session))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_lava_bubble_course_advance(&session->lava_bubbles,
            session->runtime.recovered_dependencies->sine_table))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_thwomp_motion_tick(&session->thwomps,
            session->runtime.session.compatibility.course_id,"Time Trial",
            session->driver.pose.position_x,session->driver.pose.position_y,
            session->driver.steering.state_flags,&session->thwomp_sounds))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_crab_motion_tick(&session->crabs,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_seagull_motion_tick(&session->seagulls,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_piranha_motion_tick(&session->piranhas,session->driver.steering.state_flags,
            session->runtime.session.compatibility.course_id,"Time Trial",&session->piranha_sounds))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_mouse_motion_tick(&session->mice,session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_thundercloud_motion_tick(&session->thunderclouds,session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_falling_star_motion_tick(&session->falling_stars,session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_snowman_motion_tick(&session->snowmen,
            session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_penguin_motion_tick(&session->penguins,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,
            session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_boo_motion_tick(&session->boos,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,
            session->runtime.session.compatibility.course_id,"Time Trial"))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    const PortableCannonballListener cannonball_listener={
        .x_q16=session->driver.pose.position_x,.y_q16=session->driver.pose.position_y,
        .heading=(uint16_t)session->driver.steering.steering_offset,
        .driver_flags=session->driver.steering.state_flags,
    };
    if(!portable_cannonball_motion_tick(&session->cannonballs,
            session->runtime.session.compatibility.course_id,"Time Trial",
            &cannonball_listener,session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,&session->cannonball_sounds))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(portable_snow_weather_tick(&session->snow_weather,
            session->weather_profile?&session->weather_profile->rules:NULL,&weather_input,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,
            main_visual_random,&session->main_visual_rng_state)<0)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!tick_ambient(session,PORTABLE_AMBIENT_POST_DRIVER,&session->sky_motion))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    /* Native presentation resolution happens once per tick, never in render.
     * Cloud flash uses the shared shrink timer; track-specific selectors remain zero. */
    const int affine_cue=portable_driver_affine_tick(&session->driver.affine,
        session->runtime.recovered_dependencies->affine_rules);
    if(affine_cue<0)return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(affine_cue && !(session->driver.steering.state_flags&0x20u))
        session->driver.contact_audio=(PortableDriverContactAudio){
            .serial=session->driver.contact_audio.serial+1u,.song_id=133u,.volume=256u,.restart=1u};
    if(session->driver.burn_ticks_72==PORTABLE_DRIVER_BURN_TICKS){
        portable_recovery_fx_cancel(&session->hazard_effect_72,&session->driver.palette_requests_194);
        session->hazard_effect_72=3u;
    }
    session->recovery_fx_events=0u;
    if(session->recovery_fx_rules){
        if(session->finish_camera.snapshot.cancel_effects){
            portable_recovery_fx_cancel(&session->hazard_effect_72,&session->driver.palette_requests_194);
            session->driver.burn_ticks_72=0u;
        }
        const int events=portable_recovery_fx_tick(&session->recovery_fx,session->recovery_fx_rules,
            &session->hazard_effect_72,session->driver.hop.animation_flags,&session->driver.palette_requests_194,
            session->scene_clock,portable_driver_effects_visible(&session->driver),
            !(session->driver.steering.state_flags&0x20u));
        if(events<0)return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
        session->recovery_fx_events=(uint8_t)events;
        if(events&PORTABLE_RECOVERY_FX_THAW_START)session->driver.art_motion.wheel_phase=2u;
    }
    portable_driver_palette_burn(&session->driver.burn_ticks_72,&session->driver.palette_requests_194);
    if(!session->driver.burn_ticks_72 && (session->hazard_effect_72&15u)==3u)
        portable_recovery_fx_cancel(&session->hazard_effect_72,&session->driver.palette_requests_194);
    (void)portable_driver_art_motion_step(&session->driver.art_motion,
        session->runtime.recovered_dependencies->art_motion_rules,session->driver.motion.speed_q16,
        session->driver.steering.state_flags,session->driver.forward_acceleration.state_flags,
        session->driver.source_material_85,session->driver.palette_requests_194,session->scene_clock,
        0u,session->driver.steering.response_status,1,0,0u,0);
    if(portable_lava_smoke_tick_eligible(&session->lava_smoke,session->scene_clock,
            portable_driver_effects_visible(&session->driver)) &&
       session->piranha_sounds.count<PORTABLE_PIRANHA_CAPACITY)
        session->piranha_sounds.songs[session->piranha_sounds.count++]=183u;
    session->driver.palette_bank=portable_driver_palette_select(
        &session->driver.palette_requests_194,(int16_t)session->driver.invulnerability_timer_6a,
        session->driver.lightning_timer_68,session->driver.source_material_85,0,session->scene_clock);
    if(session->surface_trail_rules){
        PortableSurfaceTrailInputs input;
        if(!portable_surface_trail_driver_inputs(&session->driver,
            portable_character_camera_yaw(&session->character_camera),&session->finish_camera.snapshot,
            session->scene_clock,session->sky_motion.initialized?(int8_t)(session->sky_motion.snapshot.road_line-74):0,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count,&input)||
           portable_surface_trail_tick(&session->surface_trails,session->surface_trail_rules,&input,
            session->runtime.recovered_dependencies->sine_table,
            session->runtime.recovered_dependencies->sine_count)<0)
            return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    }
    if(session->runtime.recovered_dependencies->impact_effect_rules &&
       !portable_impact_effect_tick(&session->driver.impact_effects,session->runtime.recovered_dependencies->impact_effect_rules))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(record_control)record_ghost_control(session, source_control_word);
    if(session->runtime.session.phase!=PORTABLE_RACE_PHASE_FINISHED){
        session->marker_display_clock=session->marker_clock++;
    }
    if (session->script_hooks != NULL && session->script_hooks->after_tick != NULL)
        session->script_hooks->after_tick(session->script_hooks->context);
    return PORTABLE_RECOVERED_TIME_TRIAL_OK;
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_set_paused(
    PortableRecoveredTimeTrialSession *session,
    int paused)
{
    if (session == NULL)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    session->paused = paused != 0;
    return PORTABLE_RECOVERED_TIME_TRIAL_OK;
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_restart(
    PortableRecoveredTimeTrialSession *session)
{
    if (session == NULL)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    session->main_visual_rng_state=0u;
    if(!portable_course_props_init_with_random(&session->course_props,
            session->course_props.initial,session->course_props.count,
            main_visual_random,&session->main_visual_rng_state))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_lava_bubble_course_init(&session->lava_bubbles,
            session->lava_bubbles.initial,session->lava_bubbles.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_thwomp_course_init(&session->thwomps,
            session->thwomps.initial,session->thwomps.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_crab_course_init(&session->crabs,session->crabs.initial,session->crabs.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_seagull_course_init(&session->seagulls,session->seagulls.initial,session->seagulls.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_cannonball_course_init(&session->cannonballs,session->cannonballs.initial,session->cannonballs.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    portable_race_runtime_restart(&session->runtime);
    portable_lap_hud_init(&session->lap_hud);
    session->scene_clock=0u;
    session->sky_motion=(PortableSkyMotion){0};
    if(portable_ambient_init(&session->ambient,session->ambient_rules,&session->ambient_seed)<0)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_piranha_course_init(&session->piranhas,session->piranhas.initial,session->piranhas.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_falling_star_course_init(&session->falling_stars,session->falling_stars.initial,session->falling_stars.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_thundercloud_course_init(&session->thunderclouds,session->thunderclouds.initial,session->thunderclouds.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_mouse_course_init(&session->mice,session->mice.initial,session->mice.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if(!portable_snowman_course_init(&session->snowmen,session->snowmen.initial,session->snowmen.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_penguin_course_init(&session->penguins,session->penguins.initial,session->penguins.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if(!portable_boo_course_init(&session->boos,session->boos.initial,session->boos.count))
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    memset(&session->piranha_sounds,0,sizeof session->piranha_sounds);
    memset(&session->cannonball_sounds,0,sizeof session->cannonball_sounds);
    memset(&session->thwomp_sounds,0,sizeof session->thwomp_sounds);
    memset(&session->prop_sounds,0,sizeof session->prop_sounds);
    memset(&session->lava_smoke,0,sizeof session->lava_smoke);
    memset(&session->recovery_fx,0,sizeof session->recovery_fx);
    memset(&session->surface_trails,0,sizeof session->surface_trails);
    session->recovery_fx_events=0u;
    memset(&session->entry_splash,0,sizeof session->entry_splash);
    memset(&session->impact_sounds,0,sizeof session->impact_sounds);
    memset(&session->hazard_lakitu, 0, sizeof session->hazard_lakitu);
    session->hazard_effect_72 = 0u;
    session->previous_semantic_buttons = 0u;
    memset(session->haunts,0,sizeof session->haunts);
    session->previous_source_control = 0u;
    session->haunt_music = 0u;
    session->mushrooms_remaining = 3u;
    session->driver.item_requests_b2 = 0u;
    session->previous_item_control = 0u;
    session->script_item_swallowed = 0u;
    session->last_runtime_status = PORTABLE_RACE_RUNTIME_OK;
    session->paused = 0u;
    reset_character_camera(
        session, (uint16_t)session->driver.steering.steering_offset);
    if(portable_snow_weather_init(&session->snow_weather,
            session->weather_profile?&session->weather_profile->rules:NULL,
            session->driver.pose.position_x,session->driver.pose.position_y,
            time_trial_camera_yaw(session),1)<0)
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if (!tick_sky_motion(session,1))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    if (!reset_ghost_capture(session))
        return PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR;
    return PORTABLE_RECOVERED_TIME_TRIAL_OK;
}

int portable_recovered_time_trial_ghost_candidate(
    const PortableRecoveredTimeTrialSession *session,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    uint32_t total_ticks;

    if (session == NULL || out_record == NULL ||
        session->ghost_recording_valid == 0u ||
        session->runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED)
        return 0;
    total_ticks = portable_race_session_total_ticks(&session->runtime.session);
    if (total_ticks == 0u || total_ticks > UINT16_MAX)
        return 0;
    memcpy(out_record, session->ghost_record,
           PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    portable_time_trial_ghost_record_set_time(out_record,
                                               (uint16_t)total_ticks);
    return 1;
}

static PortableRecoveredTimeTrialStatus apply_rescue(
    PortableRecoveredTimeTrialSession *session,
    int invalidate_ghost)
{
    PortableDriverPilotState rescued;

    if (session == NULL)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    if (session->paused ||
        recovering(session) ||
        !portable_race_session_apply_rescue(&session->runtime.session))
        return PORTABLE_RECOVERED_TIME_TRIAL_ACTION_REJECTED;

    rescued = session->runtime.recovered_initial_state;
    rescued.pose.position_x = session->runtime.session.rescue_x_q16;
    rescued.pose.position_y = session->runtime.session.rescue_y_q16;
    rescued.motion.angle = (int16_t)session->runtime.session.rescue_heading;
    rescued.steering.angle = (int16_t)session->runtime.session.rescue_heading;
    rescued.steering.steering_offset = 0;
    rescued.steering.base_angle = 0;
    /* Rescue resets motion, not the identity of already published cues.
     * Rolling a serial back to zero looks like a new event to audio and
     * would publish the initial state's empty song. Keep the last payload
     * too, so an event pending in this tick is still consumed once. */
    rescued.contact_audio = session->driver.contact_audio;
    rescued.boost_voice = session->driver.boost_voice;
    rescued.charged_audio_cue_count = session->driver.charged_audio_cue_count;
    session->driver = rescued;
    session->previous_semantic_buttons = 0u;
    session->driver.item_requests_b2 = 0u;
    session->previous_item_control = 0u;
    if (invalidate_ghost)
        session->ghost_recording_valid = 0u;
    reset_character_camera(
        session, (uint16_t)session->driver.steering.steering_offset);
    session->runtime.boundary_exit = 0u;
    return PORTABLE_RECOVERED_TIME_TRIAL_OK;
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_rescue(
    PortableRecoveredTimeTrialSession *session)
{
    return apply_rescue(session, 1);
}

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_snapshot(
    const PortableRecoveredTimeTrialSession *session,
    PortableRecoveredTimeTrialSnapshot *snapshot)
{
    const PortableRaceSession *race;

    if (session == NULL || snapshot == NULL ||
        session->runtime.recovered_driver != &session->driver)
        return PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT;
    race = &session->runtime.session;
    *snapshot = (PortableRecoveredTimeTrialSnapshot){
        .phase = race->phase,
        .compatibility = race->compatibility,
        .position_x_q16 = session->driver.pose.position_x,
        .position_y_q16 = session->driver.pose.position_y,
        .height_q16 = session->driver.pose.position_z,
        .speed_q16 = session->driver.motion.speed_q16,
        .engine_sound = portable_driver_engine_sound_input(&session->driver),
        .engine_rev_due = (uint8_t)((session->scene_clock & 7u) == 0u),
        .heading = (uint16_t)session->driver.motion.angle,
        .camera_yaw = time_trial_camera_yaw(session),
        .finish_camera=session->finish_camera,
        .camera_adjustment =
            session->character_camera.current_adjustment,
        .source_flags=session->driver.steering.state_flags,
        .driver_angle = session->driver.steering.source_angle_56,
        .steering_response_q8 = session->driver.steering.response_q8,
        .steering_position_82 = session->driver.steering.position_82,
        .charged_exit_count = session->driver.charged_exit_count,
        .charged_audio_cue_count = session->driver.charged_audio_cue_count,
        .contact_audio = session->driver.contact_audio,
        .boost_voice = session->driver.boost_voice,
        .boost_particle_age = session->driver.boost_particle_age,
        .boost_particle_birth_height_q16 = session->driver.boost_particle_birth_height_q16,
        .palette_bank = session->driver.palette_bank,
        .course_props = session->course_props,
        .lava_bubbles = session->lava_bubbles,
        .thwomps = session->thwomps,
        .crabs = session->crabs,
        .seagulls = session->seagulls,
        .piranhas = session->piranhas,
        .snowmen = session->snowmen,
        .mice = session->mice,
        .thunderclouds = session->thunderclouds,
        .falling_stars = session->falling_stars,
        .penguins = session->penguins,
        .boos = session->boos,
        .piranha_sounds = session->piranha_sounds,
        .cannonballs = session->cannonballs,
        .cannonball_sounds = session->cannonball_sounds,
        .thwomp_sounds = session->thwomp_sounds,
        .prop_sounds = session->prop_sounds,
        .lava_smoke = session->lava_smoke,
        .recovery_fx=session->recovery_fx,
        .surface_trails=session->surface_trails,
        .impact_effects=session->driver.impact_effects,
        .recovery_fx_events=session->recovery_fx_events,
        .entry_splash = session->entry_splash,
        .impact_sounds = session->impact_sounds,
        .flattened = session->driver.flattened_64,
        .affine = session->driver.affine,
        .art_motion = session->driver.art_motion,
        .sky_motion = session->sky_motion.snapshot,
        .ambient = session->ambient.snapshot,
        .ambient_digest = portable_ambient_digest(&session->ambient),
        .snow_weather = session->snow_weather.snapshot,
        .snow_weather_digest = portable_snow_weather_digest(&session->snow_weather),
        .main_visual_rng_state = session->main_visual_rng_state,
        .lightning_timer = (uint16_t)session->driver.lightning_timer_68,
        .charged_particle_count = session->driver.charged_particle_count,
        .charged_particle_age = session->driver.charged_particle_age,
        .modern_drift_enabled = (uint8_t)(session->runtime.recovered_dependencies->modern_drift_rules != NULL),
        .modern_drift = session->driver.modern_drift,
        .modern_air_enabled = (uint8_t)(session->runtime.recovered_dependencies->modern_drift_rules != NULL &&
            session->runtime.recovered_dependencies->modern_drift_rules->air.launch_mask != 0u),
        .modern_air = session->driver.modern_air,
        .mushrooms_remaining = session->mushrooms_remaining,
        .mushroom_active =
            session->driver.steering.driver_mode == 8u,
        .mushroom_timer = session->driver.steering.exit_timer,
        .lap_hud=session->lap_hud,
        .laps_completed = race->laps_completed,
        .lap_count = race->lap_count,
        .countdown_remaining = race->countdown_remaining,
        .start_route_index = race->start_route_index,
        .next_route_index = race->next_route_index,
        .route_count = race->route_count,
        .elapsed_ticks = race->elapsed_ticks,
        .total_ticks = portable_race_session_total_ticks(race),
        .marker_clock=session->marker_display_clock,
        .current_lap_ticks = portable_race_session_current_lap_ticks(race),
        .rescue_count = race->rescue_count,
        .recovery_phase = session->driver.source_terrain22_active &&
            session->driver.hop.mode == 4u ? PORTABLE_WATER_RECOVERY_SINK : PORTABLE_WATER_RECOVERY_NONE,
        .recovery_phase_ticks = session->driver.source_terrain22_active &&
            session->driver.hop.mode == 4u ? (uint16_t)(144u - session->driver.drift_charge.charge_counter) : 0u,
        .lakitu_state = portable_gp_lakitu_source_state(&session->hazard_lakitu),
        .lakitu_scale_q8=portable_gp_lakitu_scale(&session->hazard_lakitu),
        .lakitu_x = session->hazard_lakitu.x,
        .lakitu_y = session->hazard_lakitu.y,
        .lakitu_visible = (uint8_t)(session->hazard_lakitu.visible & 1u),
        .lakitu_art_bank = session->hazard_lakitu.art_bank,
        .fall_mode = session->driver.source_hazard_kind ? session->driver.hop.mode : 0u,
        .fall_timer = session->driver.source_hazard_kind
            ? session->driver.drift_charge.charge_counter : 0u,
        .fall_frames_76 = session->driver.fall_frames_76,
        .driver_hidden = (uint8_t)!portable_driver_effects_visible(&session->driver),
        .paused = session->paused,
        .off_track = (uint8_t)(session->driver.pose.track_cell_valid &&
            (session->driver.pose.track_cell & UINT8_C(0x7f)) ==
                UINT8_C(0x7f)),
    };
    memcpy(snapshot->lap_ticks, race->lap_ticks, sizeof snapshot->lap_ticks);
    memcpy(snapshot->boo_attacks, session->haunts, sizeof snapshot->boo_attacks);
    snapshot->haunt_music_active = session->haunt_music;
    snapshot->item_lock = session->driver.item_lock_ac;
    snapshot->engine_sound.ice_thaw_started=(uint8_t)((session->recovery_fx_events&PORTABLE_RECOVERY_FX_THAW_START)!=0u);
    if(session->recovery_fx_rules)snapshot->engine_sound.water_rescue_arrived=
        (uint8_t)(session->driver.source_terrain22_active && session->hazard_lakitu.retrieval_effect_started);
    return PORTABLE_RECOVERED_TIME_TRIAL_OK;
}
