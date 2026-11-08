#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "game_host_internal.h"
#include "modern_menu_host.h"

#ifdef PORTABLE_ENABLE_SDL3
struct PortableGpCeremonyHost
{
    PortableUiTextBank ui_text;
    ModernMenuHost modern;
    uint8_t modern_menus;
    const PortableAssetPackView *pack;
    PortableGpCeremonyAssetView asset;
    PortableGpCeremonySession ceremony;
    PortableGpCeremonyPresentation presentation;
    PortableRecoveredPackStorage *race_storage;
    PortablePresentationPackStorage *stage_storage;
    PortableRecoveredRaceView stage_race;
    PortablePresentationPackView stage;
    PortableRaceSceneView scene;
    PortableRacePresentationState state;
    PortableCeremonyWorldRules world_rules;
    PortableSkyMotion sky_motion;
    PortableCharacterPackedStorage *driver_storage;
    PortableCharacterAssetView drivers[4];
    const PortableCharacterAssetView *podium[3];
    RouteAudioHost *audio;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    PortableFrameScheduler scheduler;
    PortableSoftwareRenderTarget target;
    uint8_t internal_scale,fit_scaling,extended_backgrounds;
    uint16_t aspect_width;
    int focused,quit,error,needs_present;
    uint8_t confirm,frame_written,skippable;
    uint32_t music_tick;
};

static int load_driver(const PortableAssetPackView *pack,uint8_t driver_class,
    uint32_t selected_character,PortableCharacterPackedStorage *storage,
    PortableCharacterAssetView *view)
{
    PortableCharacterContent content={0};
    PortableAssetSectionView graphics;
    /* The player's roster driver stands on the podium in its seat's place. */
    const int selected=selected_character!=0u&&
        portable_content_registry_character(pack,selected_character,&content)==
            PORTABLE_CONTENT_OK&&content.character_class==driver_class;
    return (selected||portable_content_registry_find_character_class(pack,driver_class,&content)==
        PORTABLE_CONTENT_OK)&&portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,content.graphics,&graphics)==
        PORTABLE_ASSET_PACK_OK&&portable_character_asset_open_packed(&graphics,storage,view)==
        PORTABLE_CHARACTER_ASSET_OK;
}

/* Ceremony content uses the ordinary course and driver decoders. Its stage
 * manifest is private to the ceremony and absent from the selectable catalog. */
static int load_stage(PortableGpCeremonyHost *host,uint32_t character)
{
    PortableContentSelection selection;
    host->race_storage=calloc(1,sizeof *host->race_storage);
    host->stage_storage=calloc(1,sizeof *host->stage_storage);
    if(!host->race_storage||!host->stage_storage||
       portable_content_registry_select(host->pack,host->asset.stage_content_id,character,&selection)!=PORTABLE_CONTENT_OK||
       portable_recovered_pack_open_race(host->pack,&selection,host->race_storage,&host->stage_race)!=PORTABLE_RECOVERED_PACK_OK||
       portable_presentation_pack_open(host->pack,&selection,host->race_storage,&host->stage_race,
            host->stage_storage,&host->stage)!=PORTABLE_RECOVERED_PACK_OK||host->stage_race.gp_grid_count!=8u)return 0;
    for(unsigned i=0;i<8u;++i){
        host->world_rules.grid[i]=(PortableCeremonyWorldPoint){host->stage_race.gp_grid[i].x_q16,
            host->stage_race.gp_grid[i].y_q16,0};
    }
    for(unsigned i=0;i<3u;++i)host->world_rules.podium_offset[i]=(PortableCeremonyWorldPoint){
        host->asset.podium_offsets[i][0],host->asset.podium_offsets[i][1],host->asset.podium_offsets[i][2]};
    host->scene=(PortableRaceSceneView){.world_map=host->stage.world_map,.tileset=host->stage.tileset,
        .sky=&host->stage.sky,.character=&host->drivers[3],.driver_projection=host->stage.driver_projection,
        .sine=host->race_storage->sine,.sine_count=1024u,.opponent_count=4u};
    for(unsigned i=0;i<4u;++i)host->scene.opponent_characters[i]=&host->drivers[i];
    if(!portable_gp_ceremony_source_init(&host->presentation,&host->ceremony,&host->asset.motion,&host->world_rules))return 0;
    const PortableSkyMotionInputs input={.vertical_offset=host->presentation.camera_vertical_offset};
    return portable_sky_motion_init(&host->sky_motion,host->stage_race.sky_motion_rules,
        &input,host->scene.sine,host->scene.sine_count)>=0;
}

static void build_world(PortableGpCeremonyHost *host)
{
    const PortableCeremonyWorld *world=&host->presentation.motion.world;
    host->state=(PortableRacePresentationState){.camera_position_override=1u,
        .camera_position_x_q16=world->camera_x_q16,.camera_position_y_q16=world->camera_y_q16,
        .camera_yaw=world->camera_yaw,.suppress_player=1u,.suppress_native_hud=1u,
        .suppress_native_minimap=1u,.opponent_count=world->count};
    host->state.sky_motion=host->sky_motion.snapshot;
    for(unsigned i=0;i<world->count;++i){
        const PortableCeremonyWorldDriver *driver=&world->drivers[i];
        host->state.opponents[i]=(PortableRaceOpponentPresentation){
            .position_x_q16=driver->position.x_q16,.position_y_q16=driver->position.y_q16,
            .height_q16=driver->position.z_q16,.heading=driver->heading,.visible=driver->visible,
            .art_pose=driver->art_pose,.art_pose_valid=1u,
            .character_affine_scale=256u,.driver_affine_x=driver->affine.inverse_x_q8,
            .driver_affine_y=driver->affine.inverse_y_q8};
    }
}

int portable_gp_ceremony_host_end(PortableGpCeremonyHost *host,uint8_t *quit_requested)
{
    if(host==NULL)return 1;
    if(quit_requested!=NULL)*quit_requested=(uint8_t)host->quit;
    const int error=host->error;
    portable_mobile_touch_clear();
    modern_menu_host_close(&host->modern);
    portable_software_render_target_close(&host->target);
    free(host->race_storage);free(host->stage_storage);
    free(host->driver_storage);free(host);
    return error;
}

void portable_gp_ceremony_host_allow_skip(PortableGpCeremonyHost *host)
{
    if(host!=NULL)host->skippable=1u;
}

int portable_gp_ceremony_host_video(PortableGpCeremonyHost *host,uint8_t scale,int fit)
{
    if(host==NULL||scale>8u)return 0;
    host->internal_scale=scale>1u?scale:1u;host->fit_scaling=(uint8_t)(fit!=0);
    host->needs_present=1;return 1;
}

PortableGpCeremonyHost *portable_gp_ceremony_host_begin(
    const PortableAssetPackView *pack,const PortableGpCeremonySession *ceremony,
    uint32_t selected_character,SDL_Window *window,SDL_Renderer *renderer,
    SDL_Texture *texture,RouteAudioHost *audio)
{
    if(pack==NULL||ceremony==NULL||window==NULL||renderer==NULL||texture==NULL||
       ceremony->cup_index>=5u||ceremony->engine_class>=3u||
       ceremony->player_standing_rank>=8u||ceremony->performance_rating>=8u||
       ceremony->state5_next_state!=(ceremony->player_standing_rank<3u?6u:11u))return NULL;
    PortableGpCeremonyHost *host=calloc(1,sizeof *host);
    if(host==NULL)return NULL;
    if(!portable_ui_text_load(pack,&host->ui_text)){free(host);return NULL;}
    host->pack=pack;host->renderer=renderer;host->texture=texture;host->audio=audio;
    host->focused=1;host->needs_present=1;
    host->internal_scale=1u;host->target.frame.ui_text=&host->ui_text;
    host->driver_storage=calloc(4,sizeof *host->driver_storage);
    if(host->driver_storage==NULL)goto fail;
    PortableAssetSectionView section;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_GP_CEREMONY_GRAPHICS,
        1u,&section)!=PORTABLE_ASSET_PACK_OK||
       portable_gp_ceremony_asset_open(&section,&host->asset)!=PORTABLE_GP_CEREMONY_ASSET_OK||
       !portable_gp_ceremony_presentation_init(&host->presentation,host->asset.entrance_ticks,
        host->asset.celebration_ticks,host->asset.fade_ticks))goto fail;
    host->ceremony=*ceremony;
    host->ceremony.selected_character_id=selected_character;
    {
        PortableCharacterContent voice;
        if(selected_character!=0u&&
           portable_content_registry_voice_driver(pack,selected_character,&voice)==PORTABLE_CONTENT_OK&&
           voice.character_class<8u&&voice.character_class!=ceremony->selected_driver_class)
            host->ceremony.voice_override=(uint8_t)(voice.character_class+1u);
    }
    {
        /* Confirm opens on the original's frame; the celebration covers the cues. */
        const uint32_t input=portable_gp_ceremony_input_tick(ceremony,&host->asset.motion);
        if(input>host->presentation.entrance_ticks)
            host->presentation.celebration_ticks=input-host->presentation.entrance_ticks;
    }
    for(size_t index=0;index<4u;++index){
        const uint8_t driver=index<3u?ceremony->podium_driver_classes[index]:
            ceremony->selected_driver_class;
        if(!load_driver(pack,driver,driver==ceremony->selected_driver_class?selected_character:0u,
                        &host->driver_storage[index],&host->drivers[index]))goto fail;
        if(index<3u)host->podium[index]=&host->drivers[index];
    }
    if(host->asset.source_presentation&&!load_stage(host,selected_character))goto fail;
    portable_frame_scheduler_init(&host->scheduler);
    if(!route_audio_begin_ceremony(audio,portable_gp_ceremony_song_at(ceremony,0u)))goto fail;
    SDL_SetWindowTitle(window,"MKSC GP ceremony | Enter/Space/Z continue | Esc quit");
    return host;
fail:
    fprintf(stderr,"GP ceremony setup failed: cup=%u course-header=%u branch=%u: %s\n",
        ceremony->cup_index,ceremony->course_header_index,ceremony->state5_next_state,SDL_GetError());
    portable_gp_ceremony_host_end(host,NULL);
    return NULL;
}

void portable_gp_ceremony_host_modern(PortableGpCeremonyHost *host,uint8_t enabled)
{
    if(host==NULL)return;
    host->modern_menus=(uint8_t)(enabled!=0u);
    host->needs_present=1;
}

void portable_gp_ceremony_host_aspect(PortableGpCeremonyHost *host,uint16_t width)
{
    if(host==NULL)return;
    host->extended_backgrounds=1u;
    host->aspect_width=width;
    host->needs_present=1;
}

void portable_gp_ceremony_host_event(PortableGpCeremonyHost *host,const SDL_Event *event)
{
    if(host==NULL||event==NULL)return;
    modern_menu_host_event(&host->modern,event);
    (void)portable_mobile_touch_event(event,host->renderer);
    if(portable_mobile_back(event)&&host->focused){host->confirm=1;return;}
    if(event->type==SDL_EVENT_QUIT)host->quit=1;
    else if((event->type>=SDL_EVENT_WINDOW_FIRST&&event->type<=SDL_EVENT_WINDOW_LAST)){
        if(event->type==SDL_EVENT_WINDOW_FOCUS_LOST){portable_mobile_touch_clear();host->focused=0;host->confirm=0;}
        else if(event->type==SDL_EVENT_WINDOW_FOCUS_GAINED)host->focused=1;
        if(event->type==SDL_EVENT_WINDOW_EXPOSED||
           event->type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)host->needs_present=1;
    }else if(event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&host->focused){
        if(event->gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH||
           event->gbutton.button==SDL_GAMEPAD_BUTTON_START)host->confirm=1;
        else if(event->gbutton.button==SDL_GAMEPAD_BUTTON_EAST)host->quit=1;
    }else if(event->type==SDL_EVENT_KEY_DOWN&&!event->key.repeat&&host->focused){
        if(event->key.key==SDLK_ESCAPE||event->key.key==SDLK_B)host->quit=1;
        else if(event->key.key==SDLK_RETURN||event->key.key==SDLK_SPACE||
                event->key.key==SDLK_Z)host->confirm=1;
    }
}

static int ceremony_present(PortableGpCeremonyHost *host, PortableSoftwareFrame *frame)
{
    if (host->modern_menus && host->music_tick < 48u)
    {
        const ModernMenuTransition view = {.pack = host->pack,
            .title = "Cup Complete", .subtitle = "Grand Prix Ceremony",
            .course_id = host->ceremony.course_ids[3], .elapsed = host->music_tick,
            .duration = 48u, .kind = MODERN_TRANSITION_CUP, .gamepad = host->modern.gamepad};
        return modern_menu_host_present_transition(&host->modern, host->renderer, frame,
            host->fit_scaling, &view, "ceremony-card-modern");
    }
    if (host->modern_menus && host->presentation.phase >= PORTABLE_CEREMONY_FADE)
    {
        const uint32_t duration = host->presentation.fade_ticks;
        const uint32_t tick = host->presentation.tick < duration ? host->presentation.tick : duration;
        const ModernMenuTransition view = {.pack = host->pack, .title = "Cup Complete",
            .subtitle = "Returning to menus", .course_id = host->ceremony.course_ids[3],
            .elapsed = duration - tick, .duration = duration, .kind = MODERN_TRANSITION_CUP};
        return modern_menu_host_present_transition(&host->modern, host->renderer, frame,
            host->fit_scaling, &view, "ceremony-exit-modern");
    }
    return portable_software_frame_present(host->renderer, host->target.texture, frame,
        host->fit_scaling);
}

int portable_gp_ceremony_host_frame(PortableGpCeremonyHost *host,uint64_t now_ns)
{
    if(host==NULL||host->error)return -1;
    if(host->quit)return 1;
    const unsigned lifecycle=portable_mobile_lifecycle_take();
    if(lifecycle&PORTABLE_MOBILE_BACKGROUND){
        portable_mobile_touch_clear();SDL_ResetKeyboard();host->focused=0;host->confirm=0;
        portable_frame_scheduler_reset(&host->scheduler,now_ns,0);
        if(host->audio!=NULL&&!route_audio_set_paused(host->audio,1)){host->error=1;return -1;}
    }
    if(lifecycle&PORTABLE_MOBILE_FOREGROUND){
        host->focused=1;host->needs_present=1;
        portable_frame_scheduler_reset(&host->scheduler,now_ns,1);
        if(host->audio!=NULL&&!route_audio_set_paused(host->audio,0)){host->error=1;return -1;}
    }
    if(lifecycle&PORTABLE_MOBILE_TERMINATING){portable_mobile_touch_clear();host->quit=1;return 1;}
    const uint16_t touch=portable_mobile_touch_take_pressed();
    if(host->focused){
        if(touch&(PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_START|PORTABLE_FRONTEND_BUTTON_B))host->confirm=1;
    }
    uint16_t samples[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t count=0;int dropped=0;
    if(portable_frame_scheduler_update(&host->scheduler,now_ns,host->focused,0,samples,
        PORTABLE_FRAME_MAX_CATCH_UP,&count,&dropped)!=PORTABLE_FRAME_SCHEDULER_OK){
        host->error=1;return -1;
    }
    (void)dropped;
    for(size_t tick=0;tick<count&&host->presentation.phase!=PORTABLE_CEREMONY_FINISHED;++tick){
        const PortableGpCeremonyPhase previous=host->presentation.phase;
        if(host->skippable&&host->confirm&&portable_gp_ceremony_presentation_skip(&host->presentation)){
            /* Land on the original confirm frame with the result theme playing. */
            const int podium=host->ceremony.result_song_id==UINT16_C(0x38);
            if(host->music_tick<(podium?730u:1338u)&&
               !route_audio_menu_cue(host->audio,host->ceremony.result_song_id)){host->error=1;return -1;}
            host->music_tick=portable_gp_ceremony_input_tick(&host->ceremony,&host->asset.motion);
            host->confirm=0;
        }
        if(host->asset.source_presentation){
            if(!portable_gp_ceremony_source_step(&host->presentation,&host->ceremony,&host->asset.motion,
                &host->world_rules,host->scene.sine,host->scene.sine_count,host->stage_race.affine_rules,
                host->stage_race.view_rules,host->stage_race.finish_rules,host->confirm)){
                host->error=1;return -1;
            }
        }else portable_gp_ceremony_presentation_tick(&host->presentation,host->confirm);
        if(previous==PORTABLE_CEREMONY_WAIT&&host->presentation.phase==PORTABLE_CEREMONY_FADE&&
           !route_audio_fade_ceremony(host->audio)){
            host->error=1;return -1;
        }
        host->confirm=0;
        if(host->asset.source_presentation&&host->stage_race.sky_motion_rules!=NULL){
            const PortableSkyMotionInputs sky_input={.clock=host->presentation.source_tick,
                .yaw=host->presentation.motion.world.camera_yaw,
                .vertical_offset=host->presentation.camera_vertical_offset};
            if(portable_sky_motion_tick(&host->sky_motion,host->stage_race.sky_motion_rules,
                &sky_input,host->scene.sine,host->scene.sine_count)<0){host->error=1;return -1;}
        }
        if(host->presentation.phase<PORTABLE_CEREMONY_FADE){
            const uint16_t song=portable_gp_ceremony_song_at(&host->ceremony,++host->music_tick);
            uint16_t cues[3];
            const size_t cue_count=portable_gp_ceremony_cues_at(&host->ceremony,&host->asset.motion,
                host->audio!=NULL?&host->audio->hazard_rules.character_cues:NULL,host->music_tick,cues);
            if(song!=0u&&!route_audio_menu_cue(host->audio,song)){host->error=1;return -1;}
            for(size_t cue=0u;cue<cue_count;++cue)
                if(!route_audio_menu_cue(host->audio,cues[cue])){host->error=1;return -1;}
        }
        if(host->audio!=NULL&&!route_audio_render(host->audio,0,0,NULL)){
            host->error=1;return -1;
        }
    }
    if(count!=0u)host->needs_present=1;
    if(host->needs_present){
        SDL_ScaleMode filter=SDL_SCALEMODE_NEAREST;
        if(!SDL_GetTextureScaleMode(host->texture,&filter)||
           !portable_software_render_target_resize(host->renderer,&host->target,
            host->extended_backgrounds&&host->asset.source_presentation
                ?portable_software_frame_window_width(host->renderer,host->aspect_width):240u,160u,
            host->internal_scale,filter==SDL_SCALEMODE_LINEAR)){
            host->error=1;return -1;
        }
        PortableSoftwareFrame frame=host->target.frame;
        if(host->asset.source_presentation)build_world(host);
        const PortableGpCeremonyWorldView world={&host->scene,&host->state,
            host->stage_race.gp_grid,host->stage_race.gp_grid_count};
        /* Suppress only the classic black fade in the immutable draw view;
         * source clocks, skip acceptance and completion retain their owner. */
        PortableGpCeremonyPresentation draw_presentation = host->presentation;
        if(host->modern_menus && draw_presentation.phase >= PORTABLE_CEREMONY_FADE)
            draw_presentation.phase = PORTABLE_CEREMONY_WAIT;
        if(!portable_gp_ceremony_render(host->pack,&host->asset,&host->ceremony,&draw_presentation,
            host->podium,&host->drivers[3],host->asset.source_presentation?&world:NULL,&frame)||
           !ceremony_present(host,&frame)){
            fprintf(stderr,"GP ceremony render failed: stage=%u phase=%u tick=%u camera-offset=%d road-line=%d: %s\n",
                host->asset.stage_content_id,host->presentation.source_phase,host->presentation.source_tick,
                host->presentation.camera_vertical_offset,host->state.sky_motion.road_line,SDL_GetError());
            host->error=1;return -1;
        }
        if(host->asset.source_presentation){
            const char *moment=host->presentation.source_phase==1u?"ceremony-entrance":
                host->presentation.source_phase==6u?"ceremony-podium":
                host->presentation.source_phase==10u?"ceremony-trophy":
                host->presentation.source_phase==12u?"ceremony-summary":
                host->presentation.phase==PORTABLE_CEREMONY_WAIT?"ceremony":NULL;
            if(moment!=NULL&&!portable_screen_capture(&frame,moment)){host->error=1;return -1;}
            if(host->ceremony.player_standing_rank>=3u&&host->presentation.source_tick>=600u&&
               host->presentation.source_tick<650u&&
               !portable_screen_capture(&frame,"ceremony-unplaced600")){host->error=1;return -1;}
            if(host->presentation.unlock_reveal==16u&&
               (host->ceremony.extra_cup_unlocked||host->ceremony.special_cup_unlocked)&&
               !portable_screen_capture(&frame,"ceremony-unlock")){host->error=1;return -1;}
        }
        const char *capture=getenv("MKSC_CEREMONY_FRAME");
        if(capture!=NULL&&capture[0]!='\0'&&!host->frame_written&&
           host->presentation.phase==PORTABLE_CEREMONY_WAIT){
            if(!portable_software_frame_write_ppm(&frame,capture)){
                host->error=1;return -1;
            }
            host->frame_written=1;
        }
        host->needs_present=0;
    }
    return host->presentation.phase==PORTABLE_CEREMONY_FINISHED;
}
#endif
