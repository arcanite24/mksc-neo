#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "game_host_internal.h"
#include "character_menu_renderer.h"
#include "gp_countdown_renderer.h"
#include "frontend_image_renderer.h"
#include "local_players.h"
#include "input_display.h"
#include "menu_font.h"
#include "pause_menu.h"
#include "pause_menu_renderer.h"
#include "modern_menu_host.h"
#include "race_interpolation.h"

#ifdef PORTABLE_ENABLE_SDL3

/* Local VS/Battle: one race session drives every local kart; each player
 * gets a logical 160-high view sampled at the selected internal resolution. */
#define VIEW_W 240u
#define VIEW_H 160u
#define RESULT_HOLD_TICKS 90u
#define LOCAL_REPLAY_MAX_TICKS 131072u

typedef struct {
    PortableMultiRaceScene scene;
    PortableModelCatalog models;
    PortableScene3D scene_3d; /* Split views render sequentially into this workspace. */
    PortableRecoveredPackStorage *storage[PORTABLE_GP_MAX_LOCALS];
    PortableRecoveredRaceView races[PORTABLE_GP_MAX_LOCALS];
    PortableContentSelection selection[PORTABLE_GP_MAX_LOCALS];
    PortablePresentationPackStorage presentations[PORTABLE_GP_MAX_LOCALS];
    PortablePresentationPackView views[PORTABLE_GP_MAX_LOCALS];
    PortableRaceSceneView scenes[PORTABLE_GP_MAX_LOCALS];
    PortableNativeImage portraits[PORTABLE_GP_MAX_LOCALS],names[PORTABLE_GP_MAX_LOCALS];
    uint8_t has_portrait[PORTABLE_GP_MAX_LOCALS];
    PortableGpRaceSession session;
    PortableGpRaceSession *replay_session;
    uint16_t (*replay_buttons)[PORTABLE_GP_MAX_LOCALS];
    uint8_t *replay_terrain_cells;
    PortableTerrainView replay_terrain;
    uint32_t replay_count;
    uint8_t replay_verified;
    struct {
        uint8_t enabled,stage;
        uint32_t ticks,frame_hash;
        size_t width;
        uint64_t digest,snapshot_digest;
    } resize_aid;
    PortableGpRaceSnapshot snapshots[PORTABLE_GP_MAX_LOCALS];
    PortableRacePresentationState states[PORTABLE_GP_MAX_LOCALS];
    PortableRacePresentationState previous_states[PORTABLE_GP_MAX_LOCALS];
    PortableRacePresentationState render_state;
    uint8_t presentation_ready,history_ready,needs_present,redraw_requested;
    uint64_t last_present_ns;
    struct {
        uint8_t enabled;
        uint32_t presents,intermediate,interpolated;
    } presentation_aid;
    PortableBattleNavWorkspace navigation;
    PortableUiTextBank ui_text;
    uint8_t *visual_world_map;
    uint32_t *pixels;
    size_t columns,rows,view_width;
    SDL_Texture *texture;
    SDL_Gamepad *pads[PORTABLE_LOCAL_PLAYERS_MAX];
    PortableLocalInput inputs[PORTABLE_LOCAL_PLAYERS_MAX];
    PortableFrameScheduler scheduler,menu_scheduler;
    PortablePauseMenu pause_menu; /* DDF04 Continue/Quit */
    ModernMenuHost modern; /* Modern pause overlay. */
    uint64_t frequency,aid_now,digest;
    uint32_t ticks,finished_ticks,result_frames,modern_result_frames;
    uint8_t paused,focused,results_open,frame_captured,aided_pause,pause_menu_active,mobile_pause_cancel;
    uint8_t kart_contact_seen;
    uint32_t pause_ticks;
    uint32_t air_boosts[PORTABLE_GP_MAX_LOCALS];
    uint8_t previous_air_boost[PORTABLE_GP_MAX_LOCALS];
    PortableRumbleTracker rumble[PORTABLE_GP_MAX_LOCALS];
    uint16_t display_buttons[PORTABLE_GP_MAX_LOCALS];
    PortableVsResults vs_results;
    PortableBattleResults battle_results;
} MultiHost;

static uint64_t mix(uint64_t hash,uint64_t value)
{
    for(unsigned byte=0u;byte<8u;++byte){hash=(hash^(value&255u))*UINT64_C(1099511628211);value>>=8u;}
    return hash;
}

/* Live and verification paths mix the same fields in the same local order. */
static uint64_t mix_local_snapshot(uint64_t digest,const PortableGpRaceSnapshot *s)
{
    if(s->modern_drift_enabled){
        digest=mix(digest,s->modern_drift.charge_ticks);
        digest=mix(digest,s->modern_drift.boost_ticks);
        digest=mix(digest,s->modern_drift.charge_tier);
        digest=mix(digest,s->modern_drift.boost_tier);
        digest=mix(digest,s->modern_drift.previous_held);
        digest=mix(digest,s->modern_drift.last_applied_tier);
        digest=mix(digest,s->modern_drift_release_serial);
    }
    if(s->modern_air_enabled){
        digest=mix(digest,s->modern_air.air_ticks);
        digest=mix(digest,s->modern_air.boost_ticks);
        digest=mix(digest,s->modern_air.armed);
        digest=mix(digest,s->modern_air.tricked);
        digest=mix(digest,s->modern_air.previous_hop);
        digest=mix(digest,s->modern_air.last_applied);
    }
    return mix(mix(mix(mix(digest,(uint32_t)s->player_x_q16),(uint32_t)s->player_y_q16),
        s->player_heading),s->player_rank|(uint32_t)s->laps_completed<<8u);
}

static int prepare_local_replay(MultiHost *host)
{
    const char *requested=getenv("MKSC_AID_VERIFY_LOCAL_REPLAY");
    if(requested==NULL || strcmp(requested,"1")!=0)return 1;
    if(host->scene.scripts!=NULL){SDL_SetError("local replay verification does not support scripts");return 0;}
    /* Replay verifies semantic inputs for every driving profile. Optional
     * Modern state is already included conditionally by mix_local_snapshot. */
    host->replay_session=malloc(sizeof *host->replay_session);
    host->replay_buttons=malloc(LOCAL_REPLAY_MAX_TICKS*sizeof *host->replay_buttons);
    if(host->replay_session==NULL || host->replay_buttons==NULL){
        SDL_SetError("local replay recording allocation failed");return 0;
    }
    memcpy(host->replay_session,&host->session,sizeof *host->replay_session);
    const PortableDriverPilotDependencies *original=&host->session.player_dependencies;
    if(original->owned_terrain_cells!=NULL){
        if(original->terrain==NULL || original->terrain->cells!=original->owned_terrain_cells ||
           original->owned_terrain_capacity==0u){
            SDL_SetError("local replay mutable terrain is not owned");return 0;
        }
        host->replay_terrain_cells=malloc(original->owned_terrain_capacity);
        if(host->replay_terrain_cells==NULL){SDL_SetError("local replay terrain allocation failed");return 0;}
        memcpy(host->replay_terrain_cells,original->owned_terrain_cells,original->owned_terrain_capacity);
        host->replay_terrain=*original->terrain;
        host->replay_terrain.cells=host->replay_terrain_cells;
    }
    PortableGpRaceSession *copy=host->replay_session;
    /* Parked locals target the active fields too: select_local exchanges those
     * fields rather than moving the player object to another address. */
    for(unsigned local=0u;local<host->scene.players;++local){
        PortableRaceRuntime *runtime=local?&copy->locals[local-1u].runtime:&copy->runtime;
        PortableDriverPilotDependencies *deps=local?&copy->locals[local-1u].player_dependencies:&copy->player_dependencies;
        runtime->recovered_driver=&copy->player;
        runtime->recovered_dependencies=&copy->player_dependencies;
        runtime->course_props=&copy->course_props;
        runtime->piranhas=&copy->piranhas;
        if(host->replay_terrain_cells!=NULL){
            deps->owned_terrain_cells=host->replay_terrain_cells;
            deps->terrain=&host->replay_terrain;
        }
    }
    return 1;
}

static int verify_local_replay(MultiHost *host)
{
    if(host->replay_session==NULL || host->replay_verified)return 1;
    if(host->replay_count!=host->ticks){SDL_SetError("local replay recording tick mismatch");return 0;}
    PortableGpRaceSnapshot *snapshot=malloc(sizeof *snapshot);
    if(snapshot==NULL){SDL_SetError("local replay snapshot allocation failed");return 0;}
    PortableGpRaceSession *session=host->replay_session;
    uint64_t digest=UINT64_C(14695981039346656037);
    for(uint32_t tick=0u;tick<host->replay_count;++tick){
        PortableRaceTickDisposition disposition;
        if(portable_gp_race_session_tick_locals(session,host->replay_buttons[tick],&disposition)!=PORTABLE_GP_RACE_SESSION_OK){
            SDL_SetError("local replay tick %u failed",tick);free(snapshot);return 0;
        }
        for(unsigned local=host->scene.players;local-->0u;){
            if(portable_gp_race_session_select_local(session,(uint8_t)local)!=PORTABLE_GP_RACE_SESSION_OK ||
               portable_gp_race_session_snapshot(session,snapshot)!=PORTABLE_GP_RACE_SESSION_OK){
                SDL_SetError("local replay snapshot tick=%u player=%u failed",tick,local);free(snapshot);return 0;
            }
            digest=mix_local_snapshot(digest,snapshot);
        }
        digest=mix(digest,session->source_rng_state);
    }
    free(snapshot);
    if(digest!=host->digest || !portable_gp_race_session_locals_finished(session)){
        SDL_SetError("local replay mismatch ticks=%u live=%016llx replay=%016llx",host->replay_count,
            (unsigned long long)host->digest,(unsigned long long)digest);return 0;
    }
    if(!host->scene.battle){
        PortableVsResults live={0},replayed={0};
        if(portable_vs_results_build(&host->session,&live)!=PORTABLE_GP_RACE_RESULTS_OK ||
           portable_vs_results_build(session,&replayed)!=PORTABLE_GP_RACE_RESULTS_OK ||
           live.count!=replayed.count){
            SDL_SetError("local VS replay result construction mismatch");return 0;
        }
        for(unsigned row=0u;row<live.count;++row){
            const PortableVsResultRow *a=&live.rows[row],*b=&replayed.rows[row];
            if(a->centiseconds!=b->centiseconds || a->racer_index!=b->racer_index ||
               a->driver_class!=b->driver_class || a->retired!=b->retired){
                SDL_SetError("local VS replay result mismatch row=%u live_time=%u replay_time=%u",
                    row,a->centiseconds,b->centiseconds);return 0;
            }
        }
    }
    printf("Local replay: players=%u ticks=%u digest=%016llx PASS\n",host->scene.players,
        host->replay_count,(unsigned long long)digest);
    host->replay_verified=1u;
    return 1;
}

static void close_pads(MultiHost *host)
{
    for(unsigned i=0u;i<PORTABLE_LOCAL_PLAYERS_MAX;++i){
        if(host->pads[i]!=NULL)SDL_CloseGamepad(host->pads[i]);
        host->pads[i]=NULL;
    }
}

/* Pads in connection order; player i reads slot i. */
static void open_pads(MultiHost *host)
{
    close_pads(host);
    int count=0;
    SDL_JoystickID *ids=SDL_GetGamepads(&count);
    unsigned slot=0u;
    for(int i=0;i<count&&slot<PORTABLE_LOCAL_PLAYERS_MAX;++i)
        if((host->pads[slot]=SDL_OpenGamepad(ids[i]))!=NULL)++slot;
    SDL_free(ids);
    (void)portable_local_players_assign(host->scene.players,slot,host->inputs);
    if(slot<2u&&host->scene.players>2u)
        (void)portable_local_players_assign(2u,slot,host->inputs);
}

static int build_scene(MultiHost *host,unsigned local)
{
    PortablePresentationPackView *v=&host->views[local];
    PortableRaceSceneView *scene=&host->scenes[local];
    if(local==0u && host->scene.race_renderer &&
       !portable_model_catalog_open(&host->models,host->scene.pack))
        fprintf(stderr,"Optional model catalog unavailable; using billboard art.\n");
    *scene=(PortableRaceSceneView){
        .scene_3d=host->scene.race_renderer?&host->scene_3d:NULL,
        .centered_hud=1u,
        .models=host->scene.race_renderer?&host->models:NULL,
        .course_id=host->selection[local].course.id,
        .character_id=host->selection[local].character.id,
        .world_map=host->views[0].world_map,.tileset=v->tileset,.sky=&v->sky,.character=&v->character,
        .charged_exit_particle=&v->charged_exit_particle,.mushroom_burst=&v->mushroom_burst,
        .driver_projection=v->driver_projection,
        .recovery_thaw=&v->recovery_thaw,.recovery_drip=&v->recovery_drip,.recovery_fx_rules=v->recovery_fx_rules,
        .surface_trail_art=v->surface_trail_art,.surface_trail_rules=v->surface_trail_rules,
        .coin_effect_art=v->coin_effect_art,.impact_effect_art=v->impact_effect_art,.coin_effect_rules=v->coin_effect_rules,
        .weather_profile=v->weather_profile,
        .coin_pickup=&v->coin_pickup,.lava_smoke=&v->lava_smoke,.entry_splash=&v->entry_splash,.battle_balloon=&v->battle_balloon,
        .battle_bomb=&v->battle_bomb,.lightning_effect=&v->lightning_effect,
        .gp_finish_banner=&v->gp_finish_banner,.item_box=&v->item_box,.scenery=&v->scenery,
        .lava_bubble_art=v->lava_bubble_art,.thwomp_art=v->thwomp_art,.crab_art=v->crab_art,
        .seagull_art=v->seagull_art,.piranha_art=v->piranha_art,.snowman_art=v->snowman_art,
        .mouse_art=v->mouse_art,.thundercloud_art=v->thundercloud_art,
        .falling_star_art=v->falling_star_art,.penguin_art=v->penguin_art,.boo_art=v->boo_art,
        .cannonball_art=v->cannonball_art,.minimap=&v->minimap,.hud=&v->hud,
        .gp_hud=&v->gp_race_hud,.gp_markers=&v->gp_minimap_markers,
        .gp_rank_art=v->gp_results.rank_tiles!=NULL?&v->gp_results:NULL,
        .sine=host->storage[0]->sine,.sine_count=1024u,
        .opponent_count=(uint8_t)(host->scene.players-1u)};
    scene->world_map.map=host->visual_world_map;
    for(unsigned slot=0u;slot+1u<host->scene.players;++slot)
    {
        unsigned other=slot<local?slot:slot+1u;
        scene->opponent_characters[slot]=&host->views[other].character;
        scene->opponent_character_ids[slot]=host->selection[other].character.id;
    }
    for(unsigned k=0u;k<host->scene.players;++k)
        if(host->has_portrait[k])scene->gp_racer_portraits[k]=&host->portraits[k];
    return v->gp_race_hud.rank_maps!=NULL&&v->gp_minimap_markers.tiles!=NULL;
}

static int open_race(MultiHost *host)
{
    const PortableMultiRaceScene *s=&host->scene;
    const PortableRecoveredRaceView *races[PORTABLE_GP_MAX_LOCALS];
    uint8_t classes[PORTABLE_GP_MAX_LOCALS];
    if(!portable_ui_text_load(s->pack,&host->ui_text))return 0;
    for(unsigned k=0u;k<s->players;++k){
        host->storage[k]=malloc(sizeof *host->storage[k]);
        if(host->storage[k]==NULL||
           portable_content_registry_select(s->pack,s->course_id,s->characters[k],&host->selection[k])!=PORTABLE_CONTENT_OK||
           portable_course_open_race(s->pack,&host->selection[k],host->storage[k],&host->races[k],portable_mod_identity_mirror())!=
               PORTABLE_RECOVERED_PACK_OK){
            SDL_SetError("local race content course=%u character=%u",s->course_id,s->characters[k]);return 0;
        }
        if((host->races[k].rules.lap_count==0u)!=(s->battle!=0u)){SDL_SetError("course/mode mismatch");return 0;}
        if(k!=0u){
            /* Every pilot drives the one shared terrain, so wall breaks and
             * coin cells published by any local apply to all of them. */
            PortableDriverPilotDependencies *d=&host->races[k].dependencies;
            const PortableDriverPilotDependencies *w=&host->races[0].dependencies;
            d->raster=w->raster;d->terrain=w->terrain;d->track_material=w->track_material;
            d->terrain_effect_tables=w->terrain_effect_tables;
            d->owned_terrain_cells=w->owned_terrain_cells;d->initial_terrain_cells=w->initial_terrain_cells;
            d->owned_terrain_capacity=w->owned_terrain_capacity;
        }
        races[k]=&host->races[k];
        classes[k]=(uint8_t)host->selection[k].character.character_class;
    }
    PortableGpRaceSession *session=&host->session;
    if(portable_gp_race_session_init_locals(session,races,classes,s->players,s->engine_class,s->options)!=
           PORTABLE_GP_RACE_SESSION_OK||
       portable_gp_race_session_handoff_clock_boundary(session,PORTABLE_GP_CLOCK_ENABLE_ON_FIRST_RACING_TICK)!=
           PORTABLE_GP_RACE_SESSION_OK||
       portable_gp_race_session_handoff_launch_rng(session,103u)!=PORTABLE_GP_RACE_SESSION_OK||
       !portable_gp_item_art_clock_begin_direct(&session->item_art_clock,session->runtime.session.countdown_remaining,
           host->races[0].item_art_intro_rules)){
        SDL_SetError("local race session init players=%u",s->players);return 0;
    }
    session->fix_doppler_wrap=s->fix_doppler_wrap;
    if(!portable_script_host_begin_race(s->scripts,session,
           s->battle?PORTABLE_SCRIPT_MODE_BATTLE:PORTABLE_SCRIPT_MODE_VS,
           s->course_id,s->engine_class))return 0;
    for(unsigned k=0u;k<s->players;++k){
        if(portable_course_open_presentation(s->pack,&host->selection[k],host->storage[k],&host->races[k],
               &host->presentations[k],&host->views[k],portable_mod_identity_mirror())!=PORTABLE_RECOVERED_PACK_OK){
            SDL_SetError("local race presentation %u",k);return 0;
        }
        if(host->views[k].hud.item_palette_selectors==NULL ||
           host->views[k].hud.item_palette_count!=PORTABLE_HUD_ITEM_PALETTE_COUNT){
            SDL_SetError("course %u mode %s local %u: missing item HUD palette selectors; reimport your ROM",
                s->course_id,s->battle?"Battle":"VS",k);return 0;
        }
    }
    if(portable_gp_race_session_configure_hud(session,&host->views[0].gp_race_hud,
           &host->views[0].hud)!=PORTABLE_GP_RACE_SESSION_OK){
        SDL_SetError("local race HUD configuration players=%u",s->players);return 0;
    }
    const PortableRoadMapView *map=&host->views[0].world_map;
    host->visual_world_map=malloc(map->map_bytes);
    if(host->visual_world_map==NULL)return 0;
    memcpy(host->visual_world_map,map->map,map->map_bytes);
    for(size_t i=0u;i<session->coin_count;++i){
        const int32_t x=session->coins[i].placement.position_x_q16/(8*65536);
        const int32_t y=session->coins[i].placement.position_y_q16/(8*65536);
        if(x<0||y<0||x>=(int32_t)map->width_tiles||y>=(int32_t)map->height_tiles)return 0;
        host->visual_world_map[(size_t)y*map->width_tiles+(size_t)x]=UINT8_C(0xc0);
    }
    for(unsigned k=0u;k<s->players;++k){
        host->has_portrait[k]=(uint8_t)portable_character_roster_image(s->pack,
            host->selection[k].character.id,PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT,&host->portraits[k]);
        if(host->has_portrait[k])host->views[0].gp_standings.racer_portraits[k]=&host->portraits[k];
        if(portable_character_roster_image(s->pack,host->selection[k].character.id,
               PORTABLE_CHARACTER_ROSTER_BOARD_NAME,&host->names[k]))
            host->views[0].gp_standings.racer_names[k]=&host->names[k];
    }
    for(unsigned k=0u;k<s->players;++k)
        if(!build_scene(host,k)){SDL_SetError("local race HUD assets missing");return 0;}
    return 1;
}

/* Capture all local views only after a complete shared-world turn. Drawing
 * then borrows these endpoints without swapping the session's active pilot. */
static int publish_presentations(MultiHost *host,int snapshots_ready)
{
    host->history_ready=host->presentation_ready;
    if(host->presentation_ready)
        memcpy(host->previous_states,host->states,sizeof host->states);
    for(unsigned local=host->scene.players;local-->0u;){
        PortableGpRaceSessionStatus status=portable_gp_race_session_select_local(&host->session,(uint8_t)local);
        if(status!=PORTABLE_GP_RACE_SESSION_OK)return 0;
        if(!snapshots_ready && portable_gp_race_session_snapshot(&host->session,&host->snapshots[local])!=
            PORTABLE_GP_RACE_SESSION_OK)return 0;
        if(!portable_gp_race_presentation_build(&host->session,&host->snapshots[local],&host->states[local])){
            SDL_SetError("local presentation player=%u tick=%u",local,host->ticks);return 0;
        }
    }
    host->presentation_ready=1u;host->needs_present=1u;
    return portable_gp_race_session_select_local(&host->session,0u)==PORTABLE_GP_RACE_SESSION_OK;
}

static void update_visual_map(MultiHost *host);

/* Two players stack or sit side by side, whichever scales larger; three or
 * four use quadrants. */
static int layout(MultiHost *host)
{
    int output_w=960,output_h=640;
    if(!SDL_GetCurrentRenderOutputSize(host->scene.renderer,&output_w,&output_h) ||
       output_w<=0 || output_h<=0){output_w=960;output_h=640;}
    size_t columns=2u,rows=2u;
    if(host->scene.players==2u){
        const size_t reference=host->scene.aspect_width?host->scene.aspect_width:VIEW_W;
        const double stacked=SDL_min((double)output_w/reference,(double)output_h/(2.0*VIEW_H));
        const double side=SDL_min((double)output_w/(2.0*reference),(double)output_h/VIEW_H);
        columns=side>stacked?2u:1u;rows=side>stacked?1u:2u;
    }
    size_t width=host->scene.aspect_width;
    if(!width){
        const uint64_t pane_width=(uint64_t)(unsigned)output_w*rows*VIEW_H/
                                  ((uint64_t)(unsigned)output_h*columns);
        width=pane_width<VIEW_W?VIEW_W:pane_width>1024u?1024u:(size_t)pane_width;
    }
    if(host->texture && columns==host->columns && rows==host->rows && width==host->view_width)return 1;
    const size_t scale=host->scene.internal_scale>1u?host->scene.internal_scale:1u;
    const size_t physical_w=columns*width*scale,physical_h=rows*VIEW_H*scale;
    if(physical_w>SIZE_MAX/physical_h || physical_w*physical_h>SIZE_MAX/sizeof(uint32_t)){
        SDL_SetError("local race surface size overflow");return 0;
    }
    /* Allocate and configure the replacement before releasing the frame that
     * pause/results currently borrow. No pane pointers survive a draw. */
    uint32_t *pixels=calloc(physical_w*physical_h,sizeof *pixels);
    SDL_Texture *texture=SDL_CreateTexture(host->scene.renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,(int)physical_w,(int)physical_h);
    if(!pixels || !texture || !SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST)){
        free(pixels);SDL_DestroyTexture(texture);return 0;
    }
    SDL_DestroyTexture(host->texture);free(host->pixels);
    host->pixels=pixels;host->texture=texture;
    host->columns=columns;host->rows=rows;host->view_width=width;
    host->history_ready=0u;host->needs_present=1u;host->redraw_requested=1u;host->last_present_ns=0u;
    return 1;
}

/* The optional acceptance aid resizes the real SDL window on its owner
 * thread. Every stage then runs the ordinary layout and split draw paths. */
static int prepare_resize_aid(MultiHost *host)
{
    if(!host->resize_aid.enabled || !host->pause_menu_active ||
       host->pause_menu.open_ticks<PORTABLE_PAUSE_MENU_OPEN_TICKS || host->resize_aid.stage>=4u)return 1;
    if(!host->resize_aid.stage)host->resize_aid.stage=1u;
    SDL_Window *window=SDL_GetRenderWindow(host->scene.renderer);
    const int changed=host->resize_aid.stage==2u;
    int width,height;
    if(host->scene.players==2u){
        width=changed?(host->scene.aspect_width?480:800):240;
        height=changed?160:320;
    }else{
        width=changed?746:480;height=320;
    }
    return window!=NULL && SDL_SetWindowSize(window,width,height);
}

static int capture_resize_aid(MultiHost *host,const PortableSoftwareFrame *frame)
{
    const unsigned stage=host->resize_aid.stage;
    if(!host->resize_aid.enabled || stage==0u || stage>=4u)return 1;
    const int side=stage==2u;
    const size_t columns=host->scene.players==2u&&!side?1u:2u;
    const size_t rows=host->scene.players==2u&&side?1u:2u;
    uint64_t snapshot_digest=UINT64_C(14695981039346656037);
    for(unsigned local=0u;local<host->scene.players;++local)
        snapshot_digest=mix_local_snapshot(snapshot_digest,&host->snapshots[local]);
    snapshot_digest=mix(snapshot_digest,host->session.main_visual_rng_state);
    snapshot_digest=mix(snapshot_digest,host->session.source_rng_state);
    const uint32_t frame_hash=portable_software_frame_checksum(frame);
    if(!host->paused || !host->pause_menu_active || host->results_open ||
       host->columns!=columns || host->rows!=rows ||
       (host->scene.aspect_width && host->view_width!=host->scene.aspect_width)){
        SDL_SetError("local resize aid: invalid paused layout stage=%u",stage);return 0;
    }
    if(stage==1u){
        host->resize_aid.ticks=host->ticks;host->resize_aid.digest=host->digest;
        host->resize_aid.snapshot_digest=snapshot_digest;host->resize_aid.frame_hash=frame_hash;
        host->resize_aid.width=host->view_width;
    }else if(host->ticks!=host->resize_aid.ticks || host->digest!=host->resize_aid.digest ||
             snapshot_digest!=host->resize_aid.snapshot_digest ||
             (stage==3u && (frame_hash!=host->resize_aid.frame_hash ||
                             host->view_width!=host->resize_aid.width)) ||
             (stage==2u && !host->scene.aspect_width && host->view_width<=host->resize_aid.width)){
        SDL_SetError("local resize aid: paused world/HUD or simulation changed stage=%u",stage);return 0;
    }
    const int legacy=host->scene.players==2u&&!host->scene.battle;
    const char *capture=legacy?
        (stage==1u?"vs-resize-stacked":stage==2u?"vs-resize-side":"vs-resize-restored"):
        (stage==1u?"local-resize-before":stage==2u?"local-resize-wide":"local-resize-restored");
    if(!portable_screen_capture(frame,capture))return 0;
    printf("Local resize view: stage=%u players=%u width=%zu columns=%zu rows=%zu\n",
        stage,host->scene.players,host->view_width,host->columns,host->rows);
    ++host->resize_aid.stage;
    return 1;
}

static PortableSoftwareFrame composite(MultiHost *host)
{
    return (PortableSoftwareFrame){host->pixels,host->columns*host->view_width,host->rows*VIEW_H,&host->ui_text,
        (uint8_t)(host->scene.internal_scale>1u?host->scene.internal_scale:1u),0u,host->scene.artwork_lod,NULL};
}

static PortableSoftwareFrame view_frame(MultiHost *host,unsigned local)
{
    const PortableSoftwareFrame full=composite(host);
    const size_t scale=portable_software_frame_scale(&full),stride=portable_software_frame_stride(&full);
    const size_t x=(local%host->columns)*host->view_width*scale,y=(local/host->columns)*VIEW_H*scale;
    return (PortableSoftwareFrame){host->pixels+y*stride+x,host->view_width,VIEW_H,&host->ui_text,full.scale,stride,
        full.artwork_lod,NULL};
}

/* The native 240x160 rectangle at the centre of the split frame. */
static PortableSoftwareFrame center_view(MultiHost *host)
{
    PortableSoftwareFrame centered=composite(host);
    const size_t scale=portable_software_frame_scale(&centered),stride=portable_software_frame_stride(&centered);
    centered.pixels+=((centered.height-VIEW_H)/2u)*scale*stride;
    centered.height=VIEW_H;
    return portable_software_frame_native_view(&centered,PORTABLE_FRAME_CENTER);
}

static uint16_t menu_buttons(MultiHost *host)
{
    uint16_t buttons=portable_mobile_touch_frontend();
    for(unsigned i=0u;i<PORTABLE_LOCAL_PLAYERS_MAX;++i)buttons|=portable_sdl_gamepad_frontend(host->pads[i]);
    const bool *keys=SDL_GetKeyboardState(NULL);
    if(keys==NULL)return buttons;
    if(keys[SDL_SCANCODE_UP])buttons|=PORTABLE_FRONTEND_BUTTON_UP;
    if(keys[SDL_SCANCODE_DOWN])buttons|=PORTABLE_FRONTEND_BUTTON_DOWN;
    /* Left/Right, B and Select only drive the Graphics page; the recovered list ignores them. */
    if(keys[SDL_SCANCODE_LEFT])buttons|=PORTABLE_FRONTEND_BUTTON_LEFT;
    if(keys[SDL_SCANCODE_RIGHT])buttons|=PORTABLE_FRONTEND_BUTTON_RIGHT;
    if(keys[SDL_SCANCODE_X])buttons|=PORTABLE_FRONTEND_BUTTON_B;
    if(keys[SDL_SCANCODE_TAB])buttons|=PORTABLE_FRONTEND_BUTTON_SELECT;
    if(keys[SDL_SCANCODE_RETURN]||keys[SDL_SCANCODE_Z])buttons|=PORTABLE_FRONTEND_BUTTON_A;
    if(keys[SDL_SCANCODE_SPACE]||keys[SDL_SCANCODE_P])buttons|=PORTABLE_FRONTEND_BUTTON_START;
    return buttons;
}

/* The Graphics page changed a setting. Presentation pacing and scaling apply at once; the split-view layout is
 * built from Internal Scale and Aspect Ratio at the start, so those two (and the renderer) wait for the next race. */
static void apply_graphics(MultiHost *host,const PortableSettings *settings)
{
    host->scene.fit_scaling=settings->fit_scaling;host->scene.interpolate=settings->interpolate;
    host->scene.render_fps=(uint16_t)settings->render_fps;
    if(host->texture!=NULL)(void)SDL_SetTextureScaleMode(host->texture,
        settings->linear_filter?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);
    host->needs_present=1u;host->redraw_requested=1u;
}

/* Start, focus loss and pad removal open the DDF04 Continue/Quit window. */
static int open_pause(MultiHost *host,uint64_t now)
{
    if(host->pause_menu_active||host->results_open)return 1;
    host->paused=1u;host->pause_menu_active=1u;host->pause_ticks=0u;
    host->history_ready=0u;host->needs_present=1u;host->redraw_requested=1u;host->last_present_ns=0u;
    portable_pause_menu_init_gp(&host->pause_menu,host->scene.input_aid?0u:menu_buttons(host),0);
    portable_frame_scheduler_reset(&host->scheduler,now,0);
    portable_frame_scheduler_reset(&host->menu_scheduler,now,1);
    return host->scene.audio==NULL||route_audio_set_paused(host->scene.audio,1);
}

/* Returns 0 on error; sets *leave when Quit is chosen. Aided runs pick
 * Continue a few frames after the window opens. */
static int pause_frame(MultiHost *host,uint64_t now,int *leave)
{
    uint16_t samples[PORTABLE_FRAME_MAX_CATCH_UP];size_t count=0u;int dropped=0;
    if(portable_frame_scheduler_update(&host->menu_scheduler,now,host->focused,0,samples,PORTABLE_FRAME_MAX_CATCH_UP,
           &count,&dropped)!=PORTABLE_FRAME_SCHEDULER_OK)return 0;
    RouteAudioHost *audio=host->scene.audio;
    if(count)host->needs_present=1u;
    for(size_t i=0u;i<count&&host->pause_menu_active&&!*leave;++i){
        ++host->pause_ticks;
        const int aid_continue=host->resize_aid.enabled?
            host->resize_aid.stage==4u && host->pause_ticks>=PORTABLE_PAUSE_MENU_OPEN_TICKS+8u:
            host->pause_ticks==PORTABLE_PAUSE_MENU_OPEN_TICKS+8u;
        uint16_t buttons=host->scene.input_aid?
            (aid_continue?PORTABLE_FRONTEND_BUTTON_A:0u):
            (uint16_t)(menu_buttons(host)|portable_mobile_touch_take_pressed());
        if(host->mobile_pause_cancel&&host->pause_menu.open_ticks>=PORTABLE_PAUSE_MENU_OPEN_TICKS){
            host->pause_menu.cursor=0u;host->pause_menu.buttons&=(uint16_t)~PORTABLE_FRONTEND_BUTTON_A;
            buttons|=PORTABLE_FRONTEND_BUTTON_A;host->mobile_pause_cancel=0u;
        }
        const PortablePauseAction action=portable_pause_menu_tick(&host->pause_menu,buttons);
        const PortableSettings *graphics=portable_graphics_pause_update(&host->pause_menu.graphics,&host->scene.graphics);
        if(graphics!=NULL)apply_graphics(host,graphics);
        if(host->pause_menu.cue!=0u&&audio!=NULL&&!route_audio_menu_cue(audio,host->pause_menu.cue))return 0;
        if(action==PORTABLE_PAUSE_ACTION_QUIT)*leave=1;
        if(action==PORTABLE_PAUSE_ACTION_CONTINUE){
            if(host->resize_aid.enabled && host->resize_aid.stage==4u){
                host->resize_aid.stage=5u;
                printf("Local resize: PASS renderer=%u scale=%u paused world/HUD restored; Continue accepted\n",
                    host->scene.race_renderer,host->scene.internal_scale>1u?host->scene.internal_scale:1u);
            }
            host->pause_menu_active=0u;host->paused=0u;
            host->history_ready=0u;host->needs_present=1u;host->redraw_requested=1u;host->last_present_ns=0u;
            if(audio!=NULL&&!route_audio_set_paused(audio,0))return 0;
            portable_frame_scheduler_reset(&host->scheduler,now,host->focused);
        }
        if(audio!=NULL&&!route_audio_render(audio,0,0,NULL))return 0;
    }
    return 1;
}

static void sample_buttons(MultiHost *host,uint16_t buttons[PORTABLE_LOCAL_PLAYERS_MAX])
{
    PortableGpRaceSession *session=&host->session;
    if(host->scene.input_aid){
        for(unsigned k=0u;k<host->scene.players;++k){
            (void)portable_gp_race_session_select_local(session,(uint8_t)k);
            buttons[k]=host->scene.battle?portable_battle_local_aid(session,&host->navigation,host->ticks):
                portable_gp_race_smoke_control(session,host->ticks);
            const char *modern_drift=getenv("MKSC_AID_MODERN_DRIFT");
            buttons[k]=portable_recovered_modern_drift_aid(buttons[k],
                modern_drift!=NULL && strcmp(modern_drift,"1")==0,&session->player,
                session->player_dependencies.modern_drift_rules);
            const char *modern_air=getenv("MKSC_AID_MODERN_AIR");
            buttons[k]=portable_recovered_modern_air_aid(buttons[k],
                modern_air==NULL?0:strcmp(modern_air,"once")==0?2:strcmp(modern_air,"1")==0,&session->player,
                session->player_dependencies.modern_drift_rules);
        }
        (void)portable_gp_race_session_select_local(session,0u);
        return;
    }
    uint16_t pads[PORTABLE_LOCAL_PLAYERS_MAX];
    for(unsigned i=0u;i<PORTABLE_LOCAL_PLAYERS_MAX;++i)pads[i]=portable_sdl_gamepad_driving(host->pads[i]);
    portable_local_players_buttons(host->inputs,host->scene.players,portable_sdl_keyboard_driving(),
        portable_sdl_keyboard2_driving(),pads,buttons);
    buttons[0] |= portable_mobile_touch_take_driving_pressed();
}

static int tick(MultiHost *host,const uint16_t buttons[PORTABLE_LOCAL_PLAYERS_MAX])
{
    PortableGpRaceSession *session=&host->session;
    PortableRaceTickDisposition disposition;
    if(host->replay_session!=NULL){
        if(host->replay_count>=LOCAL_REPLAY_MAX_TICKS){SDL_SetError("local replay recording limit reached");return 0;}
        memcpy(host->replay_buttons[host->replay_count],buttons,
            host->scene.players*sizeof *buttons);
    }
    if(portable_gp_race_session_tick_locals(session,buttons,&disposition)!=PORTABLE_GP_RACE_SESSION_OK){
        SDL_SetError("local race tick %u driver_status=%d roster_stage=%u",host->ticks,
            session->runtime.last_driver_status,(unsigned)session->failed_roster_stage);
        return 0;
    }
    ++host->ticks;
    if(host->replay_session!=NULL)++host->replay_count;
    for(unsigned k=host->scene.players;k-->0u;){
        PortableGpRaceSessionStatus status=portable_gp_race_session_select_local(session,(uint8_t)k);
        if(status!=PORTABLE_GP_RACE_SESSION_OK){
            SDL_SetError("local tick select player=%u status=%u",k,(unsigned)status);return 0;
        }
        status=portable_gp_race_session_snapshot(session,&host->snapshots[k]);
        if(status!=PORTABLE_GP_RACE_SESSION_OK){
            SDL_SetError("local tick snapshot player=%u status=%u roster_phase=%02x",k,(unsigned)status,
                session->roster.race_phase_7d4);return 0;
        }
        const PortableGpRaceSnapshot *s=&host->snapshots[k];
        if(!host->scene.battle && s->player_kart_contacts &&
           session->player.normal_collision_timer_38>1u){
            if(!host->kart_contact_seen && host->scene.input_aid)
                printf("Local kart contact: player=%u hold=%u impulse=%d,%d coins=%u\n",k,
                    session->player.normal_collision_timer_38,
                    session->player.pose.component_x,session->player.pose.component_y,
                    session->roster.racers[k].coin_count_178);
            host->kart_contact_seen=1u;
        }
        if(s->modern_air_enabled){
            if(s->modern_air.last_applied && !host->previous_air_boost[k])++host->air_boosts[k];
            host->previous_air_boost[k]=s->modern_air.last_applied;
        }
        host->display_buttons[k]=buttons[k];
        if(host->scene.rumble)portable_sdl_gamepad_race_rumble(host->pads[k],&host->rumble[k],&s->engine_sound,
            &s->contact_audio,&s->boost_voice,s->phase==PORTABLE_RACE_PHASE_RACING);
        host->digest=mix_local_snapshot(host->digest,s);
    }
    host->digest=mix(host->digest,session->source_rng_state);
    /* One mix: P1's snapshot carries the music, every kart's engine through
     * its opponent rows and each cue raised this tick exactly once. */
    if(!route_audio_render_gp(host->scene.audio,buttons[0],&host->snapshots[0])){SDL_SetError("local race audio");return 0;}
    if(portable_gp_race_session_locals_finished(session))++host->finished_ticks;
    update_visual_map(host);
    return publish_presentations(host,1);
}

static int build_results(MultiHost *host)
{
    PortableGpRaceSession *session=&host->session;
    if(!verify_local_replay(host))return 0;
    const char *air_aid=getenv("MKSC_AID_MODERN_AIR");
    if(host->scene.input_aid && air_aid!=NULL &&
       (strcmp(air_aid,"1")==0 || strcmp(air_aid,"once")==0)){
        for(unsigned local=0u;local<host->scene.players;++local){
            printf("Local Modern air: player=%u boosts=%u\n",local+1u,host->air_boosts[local]);
            if(host->air_boosts[local]==0u){SDL_SetError("Modern air player=%u had no landing boosts",local+1u);return 0;}
        }
    }
    if(host->scene.battle){
        if(portable_battle_results_build(session,&host->battle_results)!=PORTABLE_GP_RACE_RESULTS_OK){
            SDL_SetError("Battle results phase=%02x",session->roster.race_phase_7d4);return 0;
        }
        const PortableBattleResults results=host->battle_results;
        if(host->scene.input_aid)printf("Battle local result: players=%u ticks=%u winner=P%u balloons=%u/%u/%u/%u digest=%016llx\n",
            host->scene.players,host->ticks,results.rows[0].racer_index+1u,results.rows[0].balloons,
            results.count>1u?results.rows[1].balloons:0u,results.count>2u?results.rows[2].balloons:0u,
            results.count>3u?results.rows[3].balloons:0u,(unsigned long long)host->digest);
        return 1;
    }
    if(portable_vs_results_build(session,&host->vs_results)!=PORTABLE_GP_RACE_RESULTS_OK){
        SDL_SetError("VS results phase=%02x",session->roster.race_phase_7d4);return 0;
    }
    const PortableVsResults results=host->vs_results;
    const char *modern_aid=getenv("MKSC_AID_MODERN_DRIFT");
    const int require_releases=host->scene.input_aid && modern_aid!=NULL &&
        strcmp(modern_aid,"1")==0 && session->player_dependencies.modern_drift_rules!=NULL;
    if(require_releases){
        for(unsigned local=0u;local<host->scene.players;++local){
            const uint32_t releases=host->snapshots[local].modern_drift_release_serial;
            printf("VS Modern drift: player=%u releases=%u\n",local+1u,releases);
            if(releases==0u){SDL_SetError("VS Modern drift player=%u had no releases",local+1u);return 0;}
        }
    }
    unsigned finished=0u;
    for(unsigned row=0u;row<results.count;++row)finished+=!results.rows[row].retired;
    if(host->scene.input_aid){
        printf("VS local result: players=%u ticks=%u finished=%u retired=%u digest=%016llx order",
            host->scene.players,host->ticks,finished,results.count-finished,(unsigned long long)host->digest);
        for(unsigned row=0u;row<results.count;++row)
            printf(" P%u:%u%s",results.rows[row].racer_index+1u,results.rows[row].centiseconds,results.rows[row].retired?"(retired)":"");
        putchar('\n');
    }
    return 1;
}

static void update_visual_map(MultiHost *host)
{
    const PortableRoadMapView *map=&host->views[0].world_map;
    const PortableRecoveredPackStorage *world=host->storage[0];
    for(size_t cell=0u;cell<map->map_bytes;++cell)
        if(world->terrain_cells[cell]!=world->initial_terrain_cells[cell])
            host->visual_world_map[cell]=world->terrain_cells[cell];
    for(size_t i=0u;i<host->session.coin_count;++i){
        const PortableGpCoinState *coin=&host->session.coins[i];
        const int32_t x=coin->placement.position_x_q16/(8*65536),y=coin->placement.position_y_q16/(8*65536);
        if(!coin->active&&x>=0&&y>=0&&x<(int32_t)map->width_tiles&&y<(int32_t)map->height_tiles)
            host->visual_world_map[(size_t)y*map->width_tiles+(size_t)x]=UINT8_C(0xc1);
    }
}

static int render(MultiHost *host,uint32_t alpha)
{
    if(!prepare_resize_aid(host))return 0;
    if(!layout(host))return 0;
    PortableSoftwareFrame frame=composite(host);
    portable_software_frame_clear(&frame,UINT32_C(0xff000000));
    for(unsigned k=0u;k<host->scene.players;++k){
        PortableSoftwareFrame view=view_frame(host,k);
        PortableSoftwareFrame native=portable_software_frame_native_view(&view,PORTABLE_FRAME_CENTER);
        const PortableGpRaceSnapshot *s=&host->snapshots[k];
        host->render_state=host->states[k];
        if(host->scene.interpolate && host->history_ready && host->focused &&
           !host->paused && !host->results_open)
            portable_race_interpolate(&host->previous_states[k],&host->states[k],alpha,&host->render_state);
        const PortableRacePresentationState *state=&host->render_state;
        if(host->presentation_aid.enabled && host->ticks>=320u && host->ticks<352u &&
           alpha>0u && alpha<65536u &&
           (state->position_x_q16!=host->states[k].position_x_q16 ||
            state->position_y_q16!=host->states[k].position_y_q16))
            ++host->presentation_aid.interpolated;
        if(!portable_race_scene_render(&host->scenes[k],state,&view)){
            SDL_SetError("local render scene player=%u scale=%u stride=%zu",k,view.scale,view.stride);return 0;
        }
        if(s->lakitu_visible&&portable_frontend_image_draw_scaled(host->scene.pack,
               portable_gp_lakitu_image(s->lakitu_art_bank),&native,s->lakitu_x,s->lakitu_y,0,0,
               (int)VIEW_W,(int)VIEW_H,s->lakitu_scale_q8)!=PORTABLE_FRONTEND_PRESENTATION_OK)return 0;
        if(!s->results_ready&&!portable_gp_countdown_render(host->scene.pack,s->phase,
               host->races[0].rules.countdown_ticks,s->countdown_remaining,s->total_ticks,&native))return 0;
        if(host->scene.input_display&&!s->results_ready){
            int16_t stick_x,stick_y;
            portable_sdl_gamepad_stick(host->pads[k],&stick_x,&stick_y);
            portable_input_display_render(&native,50,(int)VIEW_H-25,host->display_buttons[k],stick_x,stick_y);
        }
        char label[8];snprintf(label,sizeof label,"P%u",k+1u);
        (void)portable_menu_text(host->scene.pack,&native,4,(int)VIEW_H-18,label);
    }
    for(unsigned k=0u;k<host->scene.players;++k)
        if(host->states[k].modern_air_enabled && !portable_screen_capture_air(&frame,
            host->scene.battle?"battle-local":"vs",&host->states[k].modern_air))return 0;
    if(host->kart_contact_seen && !portable_screen_capture(&frame,"vs-kart-contact"))return 0;
    for(unsigned k=0u;k<host->scene.players;++k)
        for(unsigned i=0u;i<PORTABLE_IMPACT_EFFECT_SLOTS;++i){
            const PortableImpactEffectSlot *impact=&host->snapshots[k].impact_effects.slots[i];
            if(impact->active && impact->age>=2u &&
               !portable_screen_capture(&frame,host->scene.battle?"battle-local-impact":"vs-impact"))return 0;
        }
    if(!host->scene.battle)for(unsigned k=0u;k<host->scene.players;++k)
        for(unsigned i=0u;i<PORTABLE_COIN_EFFECT_SLOTS;++i){
            const PortableCoinEffectSlot *coin=&host->snapshots[k].coin_effects.slots[i];
            if(coin->kind==PORTABLE_COIN_EFFECT_BOUNCE && coin->visible && coin->age>=12u &&
               !portable_screen_capture(&frame,"vs-dropped-coin"))return 0;
        }
    if(host->rows>1u)portable_software_frame_rect(&frame,0u,VIEW_H,frame.width,1u,UINT32_C(0xff000000));
    if(host->scene.players==2u&&!host->scene.battle && !capture_resize_aid(host,&frame))return 0;
    if(host->scene.players==3u){
        PortableSoftwareFrame pane=view_frame(host,3u);
        PortableSoftwareFrame spare=portable_software_frame_native_view(&pane,PORTABLE_FRAME_CENTER);
        if(portable_menu_panel(host->scene.pack,&spare,56,56,128u,48u)!=PORTABLE_FRONTEND_PRESENTATION_OK)return 0;
        (void)portable_menu_text_centered(host->scene.pack,&spare,(int)VIEW_W/2,72,host->scene.battle?"BATTLE":"VS");
    }
    /* Compare the immutable world/HUD and completed spare pane before the
     * pause palette animation legitimately changes its own overlay pixels. */
    if((host->scene.players!=2u||host->scene.battle) && !capture_resize_aid(host,&frame))return 0;
    PortableSoftwareFrame center=center_view(host);
    const char *screen=NULL;
    if(host->results_open&&!host->scene.modern_menus){
        /* The 1P time board (sub_8020770) stands in for the link-cable
         * screens; the ROM has no single-pak VS/Battle result layout. */
        portable_software_frame_clear(&frame,UINT32_C(0xff000000));
        const PortableGpResultsAssetView *art=&host->views[0].gp_results;
        const PortableGpStandingsAssetView *board=&host->views[0].gp_standings;
        if(!(host->scene.battle?portable_battle_board_render(board,art,&host->battle_results,host->scene.engine_class,&center):
               portable_vs_board_render(board,art,&host->vs_results,host->scene.engine_class,&center))){
            SDL_SetError("local results board");return 0;
        }
        screen=host->scene.battle?"battle-local-results":"vs-results";
    }else if(host->pause_menu_active&&!host->scene.modern_menus){
        const PortableMenuPaletteProfile profile=portable_gp_results_menu_profile(&host->views[0].gp_results);
        if(portable_pause_menu_render_menu_palette(host->scene.pack,&host->pause_menu,&profile,&center)!=PORTABLE_FRONTEND_PRESENTATION_OK)return 0;
        if(host->pause_menu.open_ticks>=PORTABLE_PAUSE_MENU_OPEN_TICKS)
            screen=host->scene.battle?"battle-local-pause":"vs-pause";
    }
    const char *message=portable_script_host_message(host->scene.scripts);
    if(!host->results_open&&!host->pause_menu_active&&message!=NULL){
        if(!portable_menu_notice(host->scene.pack,&center,message))return 0;
        if(!portable_screen_capture(&frame,host->scene.battle?"battle-local-script":"vs-script-message"))return 0;
    }
    if(screen!=NULL&&!portable_screen_capture(&frame,screen))return 0;
    const char *capture=getenv("MKSC_RACE_FRAME");
    if(!host->frame_captured&&host->ticks>=300u&&capture!=NULL&&capture[0]!='\0'){
        if(!portable_software_frame_write_ppm(&frame,capture))return 0;
        host->frame_captured=1u;
    }
    const int modern_pause=host->scene.modern_menus&&host->pause_menu_active&&!host->results_open;
    if(!modern_pause)modern_menu_host_pause_closed(&host->modern);
    if(host->scene.modern_menus&&host->results_open){
        ModernMenuResults view={.pack=host->scene.pack,.course_id=host->scene.course_id,.title=host->scene.battle?"Battle Results":"VS Results",
            .subtitle="Local Multiplayer",.tick=host->modern_result_frames,
            .item_count=1u,.labels={"Continue"},.hint="Enter / A / Start: Continue"};
        view.row_count=host->scene.battle?host->battle_results.count:host->vs_results.count;
        for(unsigned i=0u;i<view.row_count;++i){
            const unsigned racer=host->scene.battle?host->battle_results.rows[i].racer_index:host->vs_results.rows[i].racer_index;
            ModernMenuResultRow *row=&view.rows[i];
            row->driver_id=host->selection[racer].character.id;
            row->rank=host->scene.battle?host->battle_results.rows[i].rank+1u:i+1u;
            row->highlighted=(uint8_t)(i==0u);
            (void)snprintf(row->detail,sizeof row->detail,"Player %u",racer+1u);
            if(host->scene.battle)(void)snprintf(row->value,sizeof row->value,"%u balloons",host->battle_results.rows[i].balloons);
            else if(host->vs_results.rows[i].retired)(void)snprintf(row->value,sizeof row->value,"Retired");
            else modern_menu_host_format_time(row->value,sizeof row->value,host->vs_results.rows[i].centiseconds);
        }
        return modern_menu_host_present_results(&host->modern,host->scene.renderer,&frame,host->scene.fit_scaling,
            &view,host->scene.battle?"battle-local-modern":"vs-results-modern");
    }
    modern_menu_host_results_closed(&host->modern);
    if(modern_pause)
        return modern_menu_host_present_pause(&host->modern,host->scene.renderer,&frame,host->scene.fit_scaling,
            &host->pause_menu,host->scene.battle?"Battle":"VS Race",
            host->scene.battle?"battle-local-pause-ui":"vs-pause-modern");
    const uint32_t countdown=host->races[0].rules.countdown_ticks;
    const uint32_t remaining=host->snapshots[0].countdown_remaining;
    if(host->scene.modern_menus&&host->snapshots[0].phase==PORTABLE_RACE_PHASE_COUNTDOWN&&
       remaining<=countdown&&countdown-remaining<48u){
        const ModernMenuTransition entry={.pack=host->scene.pack,.course_id=host->scene.course_id,
            .title=host->scene.battle?"Local Battle":"VS Race",.subtitle="Ready to race",
            .elapsed=countdown-remaining,.duration=48u,.kind=MODERN_TRANSITION_ENTRY};
        return modern_menu_host_present_transition(&host->modern,host->scene.renderer,&frame,
            host->scene.fit_scaling,&entry,host->scene.battle?"battle-local-entry":"vs-entry-modern");
    }
    return portable_software_frame_present(host->scene.renderer,host->texture,&frame,host->scene.fit_scaling);
}

/* Returns 1 to leave the race, 0 to continue. */
static int handle_event(MultiHost *host,const SDL_Event *event,PortableMultiRaceOutcome *outcome)
{
    modern_menu_host_event(&host->modern,event);
    const uint16_t touch=portable_mobile_touch_event(event,host->scene.renderer);
    if((touch&PORTABLE_FRONTEND_BUTTON_B)&&host->pause_menu_active)host->mobile_pause_cancel=1u;
    if((portable_mobile_back(event)||((touch&PORTABLE_FRONTEND_BUTTON_B)&&host->results_open))&&host->focused){
        if(host->results_open){outcome->completed=1u;return 1;}
        if(host->pause_menu_active)host->mobile_pause_cancel=1u;
        else if(!open_pause(host,frontend_now_ns(host->frequency)))return -1;
        return 0;
    }
    if(event->type==SDL_EVENT_QUIT){outcome->quit_requested=1u;return 1;}
    if(event->type==SDL_EVENT_WINDOW_EXPOSED || event->type==SDL_EVENT_WINDOW_RESIZED ||
       event->type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED){
        host->needs_present=1u;host->redraw_requested=1u;
        if(event->type!=SDL_EVENT_WINDOW_EXPOSED)host->history_ready=0u;
    }
    if(event->type==SDL_EVENT_GAMEPAD_ADDED||event->type==SDL_EVENT_GAMEPAD_REMOVED){
        open_pads(host);
        if(event->type==SDL_EVENT_GAMEPAD_REMOVED&&!open_pause(host,frontend_now_ns(host->frequency)))return -1;
    }
    if(event->type==SDL_EVENT_WINDOW_FOCUS_LOST){
        host->focused=0u;
        portable_mobile_touch_clear();host->mobile_pause_cancel=0u;
        if(!open_pause(host,frontend_now_ns(host->frequency)))return -1;
    }
    if(event->type==SDL_EVENT_WINDOW_FOCUS_GAINED){
        host->focused=1u;host->history_ready=0u;host->needs_present=1u;host->redraw_requested=1u;host->last_present_ns=0u;
    }
    const int key=event->type==SDL_EVENT_KEY_DOWN&&!event->key.repeat;
    const int pad=event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    const int confirm=(touch&(PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_START))||(key&&(event->key.scancode==SDL_SCANCODE_RETURN||event->key.scancode==SDL_SCANCODE_SPACE||
        event->key.scancode==SDL_SCANCODE_P))||
        (pad&&(event->gbutton.button==SDL_GAMEPAD_BUTTON_START||event->gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH));
    if(host->results_open){
        if(confirm||(key&&event->key.scancode==SDL_SCANCODE_ESCAPE)){outcome->completed=1u;return 1;}
        return 0;
    }
    if(key&&event->key.scancode==SDL_SCANCODE_ESCAPE)return 1;
    const int start=(touch&PORTABLE_FRONTEND_BUTTON_START)||(key&&(event->key.scancode==SDL_SCANCODE_P||event->key.scancode==SDL_SCANCODE_SPACE))||
        (pad&&event->gbutton.button==SDL_GAMEPAD_BUTTON_START);
    if(start&&!open_pause(host,frontend_now_ns(host->frequency)))return -1;
    return 0;
}

int run_multi_race_sdl(const PortableMultiRaceScene *scene,PortableMultiRaceOutcome *outcome)
{
    PortableMultiRaceOutcome ignored;
    if(outcome==NULL)outcome=&ignored;
    memset(outcome,0,sizeof *outcome);
    if(scene==NULL||scene->pack==NULL||scene->renderer==NULL||scene->players<2u||
       scene->players>PORTABLE_GP_MAX_LOCALS||scene->battle>1u||scene->engine_class>2u||
       scene->internal_scale>8u||scene->artwork_lod>2u||scene->race_renderer>1u||scene->interpolate>1u||
       (scene->aspect_width && (scene->aspect_width<VIEW_W||scene->aspect_width>1024u))||
       scene->render_fps>1000u)return 1;
    MultiHost *host=calloc(1,sizeof *host);
    if(host==NULL)return 1;
    host->scene=*scene;host->focused=1u;host->digest=UINT64_C(14695981039346656037);
    const char *resize_aid=getenv("MKSC_AID_LOCAL_RESIZE");
    host->resize_aid.enabled=(uint8_t)(scene->input_aid && resize_aid && strcmp(resize_aid,"1")==0);
    const char *presentation_aid=getenv("MKSC_AID_LOCAL_PRESENTATION");
    host->presentation_aid.enabled=(uint8_t)(scene->input_aid && presentation_aid &&
                                             strcmp(presentation_aid,"1")==0);
    if(host->presentation_aid.enabled && !scene->interpolate){
        SDL_SetError("local presentation aid requires interpolation");free(host);return 1;
    }
    int error=!open_race(host);
    if(!error)error=!publish_presentations(host,0);
    if(!error)error=!prepare_local_replay(host);
    if(!error)error=!layout(host);
    if(!error){
        open_pads(host);
        host->frequency=SDL_GetPerformanceFrequency();
        portable_frame_scheduler_init(&host->scheduler);
        if(scene->input_aid){
            printf("%s local launch: players=%u course=%u class=%u\n",scene->battle?"Battle":"VS",
                scene->players,scene->course_id,scene->engine_class);
            printf("Local video: players=%u width=%zu aspect=%u fps=%u interpolate=%u scale=%u renderer=%u\n",
                scene->players,host->view_width,scene->aspect_width,scene->render_fps,
                scene->interpolate,scene->internal_scale>1u?scene->internal_scale:1u,scene->race_renderer);
        }
    }
    while(!error){
        SDL_Event event;int leave=0;
        while(SDL_PollEvent(&event)&&!leave)leave=handle_event(host,&event,outcome);
        if(leave<0){error=1;break;}
        if(leave)break;
        const int presentation_window=host->presentation_aid.enabled &&
            host->ticks>=320u && host->ticks<352u;
        const uint64_t aid_step=presentation_window?UINT64_C(1046419):UINT64_C(16742707);
        const uint64_t now=scene->input_aid?(host->aid_now+=aid_step):frontend_now_ns(host->frequency);
        const unsigned lifecycle=portable_mobile_lifecycle_take();
        if(lifecycle&PORTABLE_MOBILE_BACKGROUND){
            portable_mobile_touch_clear();SDL_ResetKeyboard();host->focused=0u;host->mobile_pause_cancel=0u;
            if(!open_pause(host,now)){error=1;break;}
            if(host->results_open&&scene->audio!=NULL&&!route_audio_set_paused(scene->audio,1)){error=1;break;}
            portable_frame_scheduler_reset(&host->menu_scheduler,now,0);
        }
        if(lifecycle&PORTABLE_MOBILE_FOREGROUND){
            host->focused=1u;host->history_ready=0u;host->needs_present=1u;host->redraw_requested=1u;host->last_present_ns=0u;
            if(host->results_open&&scene->audio!=NULL&&!route_audio_set_paused(scene->audio,0)){error=1;break;}
            portable_frame_scheduler_reset(&host->scheduler,now,0);
            portable_frame_scheduler_reset(&host->menu_scheduler,now,1);
        }
        if(lifecycle&PORTABLE_MOBILE_TERMINATING){portable_mobile_touch_clear();outcome->quit_requested=1u;break;}
        if(host->paused||host->results_open)(void)portable_mobile_touch_take_driving_pressed();
        if(!host->pause_menu_active&&!host->results_open)(void)portable_mobile_touch_take_pressed();
        if(scene->input_aid&&!host->aided_pause&&host->ticks>=240u){
            host->aided_pause=1u;
            if(!open_pause(host,now)){error=1;break;}
        }
        if(host->pause_menu_active){
            if(!pause_frame(host,now,&leave)){error=1;break;}
            if(leave)break;
        }
        uint16_t tick_buttons[PORTABLE_FRAME_MAX_CATCH_UP];size_t count=0u;int dropped=0;
        if(portable_frame_scheduler_update(&host->scheduler,now,host->focused&&!host->paused&&!host->results_open,0u,
               tick_buttons,PORTABLE_FRAME_MAX_CATCH_UP,&count,&dropped)!=PORTABLE_FRAME_SCHEDULER_OK){error=1;break;}
        for(size_t i=0u;i<count&&!error;++i){
            uint16_t buttons[PORTABLE_LOCAL_PLAYERS_MAX]={0};
            sample_buttons(host,buttons);
            error=!tick(host,buttons);
        }
        if(error)break;
        if(!host->results_open&&host->finished_ticks>=RESULT_HOLD_TICKS){
            if(!build_results(host)){error=1;break;}
            host->results_open=1u;host->history_ready=0u;host->needs_present=1u;host->redraw_requested=1u;
            portable_frame_scheduler_reset(&host->menu_scheduler,now,host->focused);
        }
        if(host->results_open&&scene->modern_menus){
            size_t presentation_ticks=0u;
            if(portable_frame_scheduler_update(&host->menu_scheduler,now,host->focused,0u,
                tick_buttons,PORTABLE_FRAME_MAX_CATCH_UP,&presentation_ticks,&dropped)!=PORTABLE_FRAME_SCHEDULER_OK){error=1;break;}
            host->modern_result_frames+=(uint32_t)presentation_ticks;
            if(presentation_ticks)host->needs_present=1u;
        }
        if(!layout(host)){error=1;break;}
        const int smooth=scene->interpolate&&host->focused&&!host->paused&&!host->results_open;
        const int capture_due=!host->frame_captured&&host->ticks>=300u&&getenv("MKSC_RACE_FRAME")!=NULL;
        int present;
        if(scene->input_aid&&!presentation_window){
            /* Keep existing fast acceptance captures at their source turns.
             * The explicit presentation aid alone adds between-turn draws. */
            present=(host->ticks&7u)==0u||host->results_open||host->pause_menu_active||capture_due;
        }else if(host->redraw_requested){
            present=1;
        }else if(host->focused&&!host->paused&&!host->results_open &&
                 (smooth||scene->render_fps)){
            present=(smooth||host->needs_present) &&
                portable_present_due(now,scene->render_fps,&host->last_present_ns);
        }else{
            present=host->needs_present;
        }
        if(present){
            const uint32_t ticks_before=host->ticks;
            const uint64_t digest_before=host->digest;
            const uint32_t source_rng=host->session.source_rng_state;
            const uint32_t visual_rng=host->session.main_visual_rng_state;
            const uint32_t alpha=smooth&&host->history_ready?
                portable_frame_scheduler_alpha(&host->scheduler):65536u;
            if(!render(host,alpha)){error=1;break;}
            host->needs_present=0u;host->redraw_requested=0u;
            if(presentation_window){
                if(host->ticks!=ticks_before || host->digest!=digest_before ||
                   host->session.source_rng_state!=source_rng ||
                   host->session.main_visual_rng_state!=visual_rng || host->session.local_index!=0u){
                    SDL_SetError("local presentation changed the shared world");error=1;break;
                }
                ++host->presentation_aid.presents;
                if(count==0u && alpha>0u && alpha<65536u)++host->presentation_aid.intermediate;
            }
        }
        if(host->results_open&&scene->input_aid&&++host->result_frames>=60u){outcome->completed=1u;break;}
        if(scene->input_aid&&host->ticks>=72000u){SDL_SetError("local race did not finish");error=1;break;}
        if(!scene->input_aid)SDL_Delay(1);
    }
    const char *require_kart_contact=getenv("MKSC_REQUIRE_KART_CONTACT");
    if(scene->input_aid && require_kart_contact && strcmp(require_kart_contact,"1")==0 &&
       !host->kart_contact_seen){SDL_SetError("missing local kart contact");error=1;}
    if(!error && host->resize_aid.enabled && host->resize_aid.stage!=5u){
        SDL_SetError("local resize aid did not complete");error=1;
    }
    if(!error && host->presentation_aid.enabled){
        if(!host->presentation_aid.presents || !host->presentation_aid.intermediate ||
           !host->presentation_aid.interpolated){
            SDL_SetError("local presentation aid missing fractional frames presents=%u intermediate=%u interpolated=%u",
                host->presentation_aid.presents,host->presentation_aid.intermediate,
                host->presentation_aid.interpolated);error=1;
        }else printf("Local presentation: PASS players=%u fps=%u presents=%u intermediate=%u interpolated=%u immutable ticks/digest/RNG\n",
            scene->players,scene->render_fps,host->presentation_aid.presents,
            host->presentation_aid.intermediate,host->presentation_aid.interpolated);
    }
    if(error)fprintf(stderr,"Local race failed: players=%u course=%u tick=%u: %s\n",
        scene->players,scene->course_id,host->ticks,SDL_GetError());
    portable_mobile_touch_clear();
    portable_script_host_end_race(scene->scripts);
    close_pads(host);
    modern_menu_host_close(&host->modern);
    portable_model_catalog_close(&host->models);
    portable_scene_3d_close(&host->scene_3d);
    SDL_DestroyTexture(host->texture);free(host->pixels);free(host->visual_world_map);
    for(unsigned k=0u;k<PORTABLE_GP_MAX_LOCALS;++k)free(host->storage[k]);
    free(host->replay_terrain_cells);free(host->replay_buttons);free(host->replay_session);
    free(host);
    return error;
}

#endif
