#include <SDL3/SDL_main.h>
#include "game_host_internal.h"
#include "gp_clock_fixture.h"
#include "sdl_test_gamepad.h"
#include "sdl_test_frame.h"
#include "mobile_lifecycle.h"
#include "mobile_touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "GP host check failed at %d: %s\n", \
            __LINE__, #expression); return 1; } } while (0)

static void window_event(PortableGpHost *host, SDL_EventType kind)
{
    const SDL_Event event = {.window = {.type = kind}};
    portable_gp_host_event(host, &event);
}

static void key_event(PortableGpHost *host, SDL_Keycode key)
{
    const SDL_Event event = {.key = {.type = SDL_EVENT_KEY_DOWN,
        .key = key, .scancode = SDL_GetScancodeFromKey(key,NULL)}};
    portable_gp_host_event(host, &event);
}

static int gp_start_pause(const PortableGpHostScene *base)
{
    PortableGpHostScene scene=*base;
    scene.input_aid=1u;scene.modern_menus=1u;scene.quick_run=0u;
    scene.internal_scale=1u;scene.interpolate=0u;scene.aspect_width=240u;
    PortableGpHost *host=portable_gp_host_begin(&scene);CHECK(host!=NULL);
    for(unsigned i=0u;i<16u;++i)CHECK(portable_gp_host_frame(host)==0);
    uint32_t *pictures=malloc(3u*240u*160u*sizeof *pictures);CHECK(pictures!=NULL);
    uint32_t *race=pictures,*paused=pictures+240u*160u,*resumed=pictures+2u*240u*160u;
    CHECK(read_test_frame(scene.renderer,race));
    /* Keyboard P follows the same Start path as a physical controller. */
    const SDL_Event start={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=SDL_SCANCODE_P,.key=SDLK_P}};
    portable_gp_host_event(host,&start);
    SDL_Event release=start;release.type=SDL_EVENT_KEY_UP;portable_gp_host_event(host,&release);
    for(unsigned i=0u;i<24u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(read_test_frame(scene.renderer,paused));
    CHECK(memcmp(race,paused,240u*160u*sizeof *race)!=0);
    /* Continue must use the original pause owner, rather than leaving GP. */
    const SDL_Event back={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=SDL_SCANCODE_AC_BACK,.key=SDLK_AC_BACK}};
    portable_gp_host_event(host,&back);
    release=back;release.type=SDL_EVENT_KEY_UP;portable_gp_host_event(host,&release);
    for(unsigned i=0u;i<8u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(read_test_frame(scene.renderer,resumed));
    CHECK(memcmp(paused,resumed,240u*160u*sizeof *paused)!=0);
    const SDL_Event quit={.type=SDL_EVENT_QUIT};portable_gp_host_event(host,&quit);
    CHECK(portable_gp_host_frame(host)==1);
    PortableGpRaceHostOutcome outcome;
    CHECK(portable_gp_host_end(host,&outcome)==0&&outcome.quit_requested&&!outcome.results_completed);
    free(pictures);
    puts("GP Start pause: PASS visible modern pause and Back Continue through normal GP owner");
    return 0;
}

/* Exercise only ordinary host input. The replay counter is public evidence
 * that neither pause nor a suspend/foreground pair admits a race tick. */
static int mobile_gp_lifecycle(const PortableGpHostScene *base)
{
    PortableGpInputRecording *recording=calloc(1,sizeof *recording);
    CHECK(recording!=NULL);
    PortableGpHostScene scene=*base;
    scene.quick_run=1u;scene.input_aid=1u;scene.recording=recording;
    scene.internal_scale=1u;scene.aspect_width=0u;scene.interpolate=0u;scene.race_renderer=1u;
    PortableGpHost *host=portable_gp_host_begin(&scene);
    CHECK(host!=NULL);
    PortableSettings settings;portable_settings_defaults(&settings);settings.touch_controls=1u;
    portable_sdl_input_configure(&settings);
    portable_mobile_lifecycle_start();
    for(unsigned i=0u;i<32u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(recording->count>0u);
    const SDL_Event back={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=SDL_SCANCODE_AC_BACK}};
    portable_gp_host_event(host,&back);
    const uint32_t paused_count=recording->count;
    SDL_Event repeat=back;repeat.key.repeat=true;
    portable_gp_host_event(host,&repeat);
    for(unsigned i=0u;i<20u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(recording->count==paused_count);
    uint16_t *paused_inputs=malloc((size_t)paused_count*sizeof *paused_inputs);CHECK(paused_inputs!=NULL);
    memcpy(paused_inputs,recording->buttons,(size_t)paused_count*sizeof *paused_inputs);
    CHECK(SDL_SetWindowSize(scene.window,373,160));
    window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    CHECK(portable_gp_host_frame(host)==0);
    SDL_Surface *wide=SDL_RenderReadPixels(scene.renderer,NULL);
    CHECK(wide!=NULL&&wide->w==373&&wide->h==160);SDL_DestroySurface(wide);
    CHECK(SDL_SetWindowSize(scene.window,240,160));
    window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    CHECK(portable_gp_host_frame(host)==0);
    CHECK(recording->count==paused_count&&
        memcmp(paused_inputs,recording->buttons,(size_t)paused_count*sizeof *paused_inputs)==0);
    free(paused_inputs);
    portable_gp_host_event(host,&back);
    for(unsigned i=0u;i<8u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(recording->count>paused_count); /* Back selected Continue, not Quit. */

    int width,height;CHECK(SDL_GetCurrentRenderOutputSize(scene.renderer,&width,&height));
    PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
    CHECK(portable_mobile_touch_layout(width,height,buttons,PORTABLE_MOBILE_TOUCH_BUTTONS)==PORTABLE_MOBILE_TOUCH_BUTTONS);
    unsigned accelerator=0u;
    while(accelerator<PORTABLE_MOBILE_TOUCH_BUTTONS&&buttons[accelerator].driving_mask!=PORTABLE_INPUT_ACCELERATE)++accelerator;
    CHECK(accelerator<PORTABLE_MOBILE_TOUCH_BUTTONS);
    const SDL_Event finger={.tfinger={.type=SDL_EVENT_FINGER_DOWN,.touchID=11,.fingerID=3,
        .x=buttons[accelerator].x/(float)width,.y=buttons[accelerator].y/(float)height}};
    portable_gp_host_event(host,&finger);
    CHECK(portable_mobile_touch_driving()&PORTABLE_INPUT_ACCELERATE);
    SDL_Event background[]={
        {.type=SDL_EVENT_WILL_ENTER_BACKGROUND},{.type=SDL_EVENT_DID_ENTER_BACKGROUND},
        {.type=SDL_EVENT_WILL_ENTER_FOREGROUND},{.type=SDL_EVENT_DID_ENTER_FOREGROUND}};
    for(unsigned i=0u;i<4u;++i)CHECK(SDL_PushEvent(&background[i]));
    SDL_Event event;while(SDL_PollEvent(&event))portable_gp_host_event(host,&event);
    const uint32_t suspended_count=recording->count;
    for(unsigned i=0u;i<32u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(recording->count==suspended_count);
    CHECK(portable_mobile_touch_driving()==0u&&portable_mobile_touch_frontend()==0u);
    portable_gp_host_event(host,&repeat);
    CHECK(portable_gp_host_frame(host)==0&&recording->count==suspended_count);
    portable_gp_host_event(host,&back);
    for(unsigned i=0u;i<8u;++i)CHECK(portable_gp_host_frame(host)==0);
    CHECK(recording->count>suspended_count&&recording->count<=suspended_count+8u);
    const SDL_Event quit={.type=SDL_EVENT_QUIT};portable_gp_host_event(host,&quit);
    CHECK(portable_gp_host_frame(host)==1);
    PortableGpRaceHostOutcome outcome;
    CHECK(portable_gp_host_end(host,&outcome)==0&&outcome.quit_requested);
    portable_sdl_input_configure(NULL);portable_mobile_lifecycle_stop();free(recording);
    puts("GP mobile lifecycle: PASS Back pause/Continue, repeated Back and background freeze");
    return 0;
}

/* A fixed source-LCG seed exercises all eleven stock constructors through
 * the same content and session owners used by the executable. */
static int imported_visual_seeds(const PortableAssetPackView *pack)
{
    const uint16_t phases[11]={0u,0x5000u,0x2400u,0x5400u,0x0c00u,0x4000u,
        0x3c00u,0x4c00u,0x2c00u,0x2000u,0x2c00u};
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    PortableGpRaceSession *gp=malloc(sizeof *gp);
    PortableRecoveredTimeTrialSession *tt=malloc(sizeof *tt);
    CHECK(storage && gp && tt);
    PortableContentSelection selection;PortableRecoveredRaceView view;
    CHECK(portable_content_registry_select(pack,41u,1u,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(pack,&selection,storage,&view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(test_gp_init(gp,&view,(uint8_t)view.rules.compatibility.character_class)==0);
    CHECK(portable_recovered_time_trial_init(tt,&view)==0);
    CHECK(gp->main_visual_rng_state==UINT32_C(2951033815) &&
        tt->main_visual_rng_state==UINT32_C(2951033815));
    unsigned ordinal=0u;
    for(size_t i=0u;i<view.course_prop_count;++i){
        if(view.course_props[i].source_code>>16u!=0xa3u)continue;
        CHECK(ordinal<11u && !view.course_props[i].pier_boo.phase &&
            gp->course_props.actors[i].pier_boo.phase==phases[ordinal] &&
            tt->course_props.actors[i].pier_boo.phase==phases[ordinal]);
        ++ordinal;
    }
    CHECK(ordinal==11u && portable_recovered_time_trial_restart(tt)==0 &&
        tt->main_visual_rng_state==UINT32_C(2951033815));
    ordinal=0u;
    for(size_t i=0u;i<tt->course_props.count;++i)
        if(tt->course_props.actors[i].source_code>>16u==0xa3u)
            CHECK(tt->course_props.actors[i].pier_boo.phase==phases[ordinal++]);
    CHECK(ordinal==11u);
    free(tt);free(gp);free(storage);
    puts("imported visual seeds: PASS eleven placed Boos, shared GP/TT sequence and retry");
    return 0;
}

static int centered_split_hud(const PortableAssetPackView *pack,
    const PortableRaceSceneView *source,const PortableRacePresentationState *state)
{
    const uint32_t guard=UINT32_C(0x5a173c91);
    PortableRaceSceneView scene;memcpy(&scene,source,sizeof scene);
    PortableRaceSceneView source_before;memcpy(&source_before,source,sizeof source_before);
    PortableRacePresentationState state_before;memcpy(&state_before,state,sizeof state_before);
    PortableHudAssetView hud_before;memcpy(&hud_before,source->hud,sizeof hud_before);
    uint8_t digest_before[PORTABLE_ASSET_PACK_DIGEST_SIZE],digest_after[PORTABLE_ASSET_PACK_DIGEST_SIZE];
    const uint8_t *parts[]={pack->bytes};const size_t sizes[]={pack->size};
    portable_asset_pack_sha256(parts,sizes,1u,digest_before);
    const unsigned scales[3]={1u,2u,4u};
    for(unsigned resolution=0u;resolution<3u;++resolution){
        const unsigned scale=scales[resolution];
        const size_t rows=160u*scale,native_stride=240u*scale+5u;
        uint32_t *native=malloc(native_stride*rows*sizeof *native);
        CHECK(native!=NULL);
        for(size_t p=0;p<native_stride*rows;++p)native[p]=guard;
        PortableSoftwareFrame baseline={.pixels=native,.width=240u,.height=160u,
            .scale=(uint8_t)scale,.stride=native_stride};
        scene.centered_hud=0u;
        CHECK(portable_race_scene_render_hud(&scene,state,&baseline));
        size_t painted=0u;
        for(size_t y=0;y<rows;++y)for(size_t x=0;x<240u*scale;++x)
            painted+=(native[y*native_stride+x]!=guard);
        CHECK(painted>0u);
        for(unsigned aspect=0u;aspect<2u;++aspect){
            const size_t width=aspect?373u:240u,stride=width*scale+11u,count=stride*rows;
            uint32_t *storage=malloc((count+16u)*sizeof *storage);
            CHECK(storage!=NULL);
            for(size_t p=0;p<count+16u;++p)storage[p]=guard;
            PortableSoftwareFrame frame={.pixels=storage+7u,.width=width,.height=160u,
                .scale=(uint8_t)scale,.stride=stride};
            PortableSoftwareFrame frame_before;memcpy(&frame_before,&frame,sizeof frame_before);
            scene.centered_hud=1u;
            PortableRaceSceneView scene_before;memcpy(&scene_before,&scene,sizeof scene_before);
            CHECK(portable_race_scene_render_hud(&scene,state,&frame));
            CHECK(!memcmp(&frame_before,&frame,sizeof frame));
            CHECK(!memcmp(&scene_before,&scene,sizeof scene));
            CHECK(!memcmp(&state_before,state,sizeof state_before));
            const size_t start=((width-240u)/2u)*scale;
            for(size_t y=0;y<rows;++y)for(size_t x=0;x<stride;++x){
                const uint32_t expected=x>=start&&x<start+240u*scale?
                    native[y*native_stride+x-start]:guard;
                CHECK(frame.pixels[y*stride+x]==expected);
            }
            for(size_t p=0;p<7u;++p)CHECK(storage[p]==guard);
            for(size_t p=count+7u;p<count+16u;++p)CHECK(storage[p]==guard);
            /* Original single-player widescreen keeps its existing edge
             * anchors. Turning centering off must still paint the wings. */
            for(size_t p=0;p<count;++p)frame.pixels[p]=guard;
            scene.centered_hud=0u;
            CHECK(portable_race_scene_render_hud(&scene,state,&frame));
            size_t wing_pixels=0u;
            for(size_t y=0;y<rows;++y)for(size_t x=0;x<stride;++x){
                if(x>=width*scale)CHECK(frame.pixels[y*stride+x]==guard);
                else if(x<start||x>=start+240u*scale)
                    wing_pixels+=(frame.pixels[y*stride+x]!=guard);
                if(!aspect)CHECK(frame.pixels[y*stride+x]==
                    (x<240u*scale?native[y*native_stride+x]:guard));
            }
            CHECK(!aspect||wing_pixels>0u);
            free(storage);
        }
        for(size_t y=0;y<rows;++y)for(size_t x=240u*scale;x<native_stride;++x)
            CHECK(native[y*native_stride+x]==guard);
        free(native);
    }
    CHECK(!memcmp(&source_before,source,sizeof source_before));
    CHECK(!memcmp(&hud_before,source->hud,sizeof hud_before));
    portable_asset_pack_sha256(parts,sizes,1u,digest_after);
    CHECK(!memcmp(digest_before,digest_after,sizeof digest_before));
    puts("Centered split HUD: PASS native/wide 1x/2x/4x, padded stride, original anchors and immutable content");
    return 0;
}

static int imported_arena_sessions(const PortableAssetPackView *pack)
{
    PortableAssetSectionView catalog;
    CHECK(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_COURSE_CATALOG,3u,&catalog)==PORTABLE_ASSET_PACK_OK);
    CHECK(catalog.version==2u && catalog.size==56u);
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    PortableGpRaceSession *session=malloc(sizeof *session);
    PortablePresentationPackStorage *art=calloc(4u,sizeof *art);
    CHECK(storage!=NULL && session!=NULL && art!=NULL);
    for (unsigned arena=0u;arena<4u;++arena) {
        const uint8_t *row=catalog.bytes+8u+arena*12u;
        const uint32_t id=(uint32_t)row[0]|(uint32_t)row[1]<<8u|(uint32_t)row[2]<<16u|(uint32_t)row[3]<<24u;
        for (uint8_t driver=0u;driver<8u;++driver) {
            PortableContentSelection selection;PortableRecoveredRaceView view;
            CHECK(portable_content_registry_select(pack,id,driver+1u,&selection)==PORTABLE_CONTENT_OK);
            CHECK(portable_recovered_pack_open_race(pack,&selection,storage,&view)==PORTABLE_RECOVERED_PACK_OK);
            const PortableGpRaceSessionStatus initialized=test_gp_init(session,&view,(uint8_t)view.rules.compatibility.character_class);
            if (initialized!=PORTABLE_GP_RACE_SESSION_OK)
                fprintf(stderr,"arena=%u driver=%u init=%u\n",id,driver,initialized);
            CHECK(initialized==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_roster_count(&session->roster)==4u && session->rules.lap_count==0u);
            for (unsigned start=0u;start<4u;++start) {
                CHECK(session->roster.racers[start].position_x_q16==view.gp_grid[start].x_q16 &&
                    session->roster.racers[start].position_y_q16==view.gp_grid[start].y_q16 &&
                    session->roster.racers[start].heading==view.gp_grid[start].heading);
            }
            const PortableGpRacer unused=session->roster.racers[4];
            PortableRaceTickDisposition disposition;
            for (unsigned tick=0u;tick<view.rules.countdown_ticks+32u;++tick) {
                const PortableGpRaceSessionStatus result=portable_gp_race_session_tick(session,0u,&disposition);
                if (result!=PORTABLE_GP_RACE_SESSION_OK)
                    fprintf(stderr,"arena=%u driver=%u tick=%u status=%u stage=%u\n",id,driver,tick,result,session->failed_roster_stage);
                CHECK(result==PORTABLE_GP_RACE_SESSION_OK);
            }
            CHECK(memcmp(&unused,&session->roster.racers[4],sizeof unused)==0);
            PortableGpRaceSnapshot snapshot;
            CHECK(portable_gp_race_session_snapshot(session,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(snapshot.phase==PORTABLE_RACE_PHASE_RACING && snapshot.lap_count==0u && snapshot.laps_completed==0u);
            for (unsigned opponent=0u;opponent<7u;++opponent)
                CHECK(snapshot.opponents[opponent].visible==(opponent<3u));
            PortableRacePresentationState state;
            CHECK(portable_gp_race_presentation_build(session,&snapshot,&state));
            CHECK(state.show_battle && state.lap_count==0u && state.opponent_count==3u &&
                state.balloons_remaining==3u);
            PortablePresentationPackView graphics[4];
            for (unsigned racer=0u;racer<4u;++racer) {
                PortableContentSelection racer_selection=selection;
                CHECK(portable_content_registry_find_character_class(pack,
                    session->roster.racers[racer].driver_class,&racer_selection.character)==PORTABLE_CONTENT_OK);
                CHECK(portable_presentation_pack_open(pack,&racer_selection,storage,&view,
                    &art[racer],&graphics[racer])==PORTABLE_RECOVERED_PACK_OK);
            }
            const PortableRaceSceneView scene={
                .world_map=graphics[0].world_map,.tileset=graphics[0].tileset,
                .sky=&graphics[0].sky,.character=&graphics[0].character,
                .battle_balloon=&graphics[0].battle_balloon,
                .battle_bomb=&graphics[0].battle_bomb,
                .item_box=&graphics[0].item_box,.scenery=&graphics[0].scenery,
                .minimap=&graphics[0].minimap,.hud=&graphics[0].hud,
                .gp_hud=&graphics[0].gp_race_hud,.gp_markers=&graphics[0].gp_minimap_markers,
                .sine=storage->sine,.sine_count=1024u,.opponent_count=3u,
                .opponent_characters={&graphics[1].character,&graphics[2].character,&graphics[3].character}};
            uint32_t pixels[240u*160u];
            PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
            CHECK(portable_race_scene_render(&scene,&state,&frame));
            CHECK(portable_race_scene_render_hud(&scene,&state,&frame));
            if(arena==0u&&driver==0u)CHECK(!centered_split_hud(pack,&scene,&state));
            const char *capture=getenv("MKSC_ARENA_FRAME");
            if (capture!=NULL && arena==0u && driver==0u)
                CHECK(portable_software_frame_write_ppm(&frame,capture));
            if (graphics[0].battle_balloon.pixels!=NULL) {
                CHECK(graphics[0].battle_balloon.width==16u && graphics[0].battle_balloon.height==16u);
                state.suppress_native_hud=1u;
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                const uint32_t three_sprites=portable_software_frame_checksum(&frame);
                state.balloons_remaining=0u;
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                CHECK(portable_software_frame_checksum(&frame)!=three_sprites);
                if (arena==0u && driver==0u) {
                    uint32_t previous=portable_software_frame_checksum(&frame);
                    for (uint8_t balloons=1u;balloons<=3u;++balloons) {
                        state.balloons_remaining=balloons;
                        CHECK(portable_race_scene_render(&scene,&state,&frame));
                        const uint32_t current=portable_software_frame_checksum(&frame);
                        CHECK(current!=previous);
                        CHECK(portable_race_scene_render(&scene,&state,&frame));
                        CHECK(portable_software_frame_checksum(&frame)==current);
                        previous=current;
                    }
                    state.balloon_tick+=16u;
                    CHECK(portable_race_scene_render(&scene,&state,&frame));
                    CHECK(portable_software_frame_checksum(&frame)!=previous);
                }
                state.balloons_remaining=3u;state.suppress_native_hud=0u;
            }
            if (graphics[0].battle_bomb.direction_tiles!=NULL) {
                CHECK(graphics[0].battle_bomb.direction_frame_count==16u &&
                    graphics[0].battle_bomb.lod32_tiles!=NULL &&
                    graphics[0].battle_bomb.lod16_tiles!=NULL);
                state.balloons_remaining=0u;state.suppress_native_hud=1u;
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                const uint32_t kart=portable_software_frame_checksum(&frame);
                state.revenge_bomb=1u;state.bomb_walk_phase=0u;
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                const uint32_t bomb=portable_software_frame_checksum(&frame);
                CHECK(bomb!=kart);
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                CHECK(portable_software_frame_checksum(&frame)==bomb);
                state.bomb_walk_phase=10u;
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                CHECK(portable_software_frame_checksum(&frame)!=bomb);
                for (unsigned i=0u;i<3u;++i) {
                    state.opponents[i].revenge_bomb=1u;
                    state.opponents[i].balloons_remaining=0u;
                }
                CHECK(portable_race_scene_render(&scene,&state,&frame));
                const char *bomb_capture=getenv("MKSC_BOMB_FRAME");
                if (bomb_capture!=NULL && arena==0u && driver==0u)
                    CHECK(portable_software_frame_write_ppm(&frame,bomb_capture));
                for (unsigned i=0u;i<3u;++i) {
                    state.opponents[i].revenge_bomb=0u;
                    state.opponents[i].balloons_remaining=3u;
                }
                state.revenge_bomb=0u;state.balloons_remaining=3u;state.suppress_native_hud=0u;
            }
            portable_software_frame_clear(&frame,UINT32_C(0x123456));
            CHECK(portable_race_scene_render_hud(&scene,&state,&frame));
            const uint32_t three_balloons=portable_software_frame_checksum(&frame);
            state.gp_rank=7u;state.gp_coins=99u;
            portable_software_frame_clear(&frame,UINT32_C(0x123456));
            CHECK(portable_race_scene_render_hud(&scene,&state,&frame));
            CHECK(portable_software_frame_checksum(&frame)==three_balloons);
            /* Balloons are race-scene sprites (G11 removed the HUD text). */
            state.balloons_remaining=0u;
            portable_software_frame_clear(&frame,UINT32_C(0x123456));
            CHECK(portable_race_scene_render_hud(&scene,&state,&frame));
            CHECK(portable_software_frame_checksum(&frame)==three_balloons);
            if (driver==0u && view.battle_items->ai.cruise_speed!=0u) {
                unsigned collected=0u,falls=0u;
                for (unsigned tick=0u;tick<1800u;++tick) {
                    const PortableGpRaceSessionStatus result=portable_gp_race_session_tick(session,0u,&disposition);
                    if (result!=0) fprintf(stderr,"arena=%u extended-tick=%u status=%u stage=%u\n",id,tick,result,session->failed_roster_stage);
                    CHECK(result==0);
                    for (unsigned cpu=1u;cpu<4u;++cpu) {
                        const PortableGpRacer *r=&session->roster.racers[cpu];
                        if (r->item_inventory.active || r->item_inventory.held_item_17a!=0u) collected|=1u<<cpu;
                        if (r->recovery_material!=0u) falls|=1u<<cpu;
                    }
                }
                fprintf(stderr,"arena=%u collected=%x falls=%x uses=%u green=%u red=%u banana=%u star=%u balloons=%u/%u/%u/%u\n",
                    id,collected,falls,session->roster.item_uses,session->roster.cpu_green_uses,
                    session->roster.cpu_red_uses,session->roster.cpu_banana_uses,session->roster.cpu_star_uses,
                    session->roster.racers[0].balloons_remaining,session->roster.racers[1].balloons_remaining,
                    session->roster.racers[2].balloons_remaining,session->roster.racers[3].balloons_remaining);
                /* Live combat can eliminate a shopper before its first box.
                 * Collection reachability is tested independently below. */
                CHECK(collected!=0u && falls==0u);
                if (view.battle_items->combat.interval!=0u) {
                    CHECK(session->roster.cpu_green_uses+session->roster.cpu_red_uses+
                        session->roster.cpu_banana_uses+session->roster.cpu_star_uses>0u);
                    unsigned balloons=0u;
                    for (unsigned racer=0u;racer<4u;++racer)
                        balloons+=session->roster.racers[racer].balloons_remaining;
                    CHECK(balloons<12u);
                }
                for (unsigned cpu=1u;cpu<4u;++cpu)
                    CHECK(session->roster.racers[cpu].battle_navigation_valid);
                /* Isolate navigation/pickup from weapon intent without
                 * removing imported boxes or changing driver movement. The
                 * normal items-off gate disables CPU use on every turn. */
                CHECK(test_gp_init(session,&view,
                    (uint8_t)view.rules.compatibility.character_class)==PORTABLE_GP_RACE_SESSION_OK);
                session->quick_run_options.items_off=1u;
                /* F450's independent contact guard isolates navigation from
                 * pushes as well as weapon damage; it does not gate boxes. */
                session->player.contact_guard_66=1u;
                for(unsigned racer=0u;racer<4u;++racer)session->roster.racers[racer].contact_status_66=1;
                collected=0u;falls=0u;
                for(unsigned tick=0u;tick<view.rules.countdown_ticks+1800u;++tick){
                    CHECK(portable_gp_race_session_tick(session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                    for(unsigned cpu=1u;cpu<4u;++cpu){
                        const PortableGpRacer *r=&session->roster.racers[cpu];
                        if(r->item_inventory.active || r->item_inventory.held_item_17a!=0u)collected|=1u<<cpu;
                        if(r->recovery_material!=0u)falls|=1u<<cpu;
                    }
                    if(collected==0x0eu)break;
                }
                if(collected!=0x0eu || falls || session->roster.item_uses)
                    fprintf(stderr,"arena=%u isolated-shopping collected=%x falls=%x uses=%u\n",id,collected,falls,session->roster.item_uses);
                CHECK(collected==0x0eu && falls==0u && session->roster.item_uses==0u);
                for(unsigned racer=0u;racer<4u;++racer)
                    CHECK(session->roster.racers[racer].balloons_remaining==3u);
            }
        }
    }
    free(art);free(session);free(storage);
    puts("imported arena sessions: PASS four arenas/eight drivers, shared countdown/motion/render/HUD, four participants, zero laps; arena CPUs collect boxes without terrain recovery");
    return 0;
}

/* A rejected content owner must not enter teardown's live-session diagnostics
 * or take ownership of the shell's borrowed SDL resources. */
static int malformed_roulette_begin(const PortableGpHostScene *scene)
{
    const PortableAssetPackView *pack=scene->pack;
    PortableAssetSectionView *sections=calloc((size_t)pack->section_count+1u,sizeof *sections);
    CHECK(sections!=NULL);
    uint32_t count=pack->section_count,index=count;
    for(uint32_t i=0u;i<count;++i){
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        if(sections[i].kind==PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS && sections[i].content_id==12u)index=i;
    }
    uint8_t malformed[176]={0};
    sections[index]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS,
        .content_id=12u,.version=1u,.bytes=malformed,.size=sizeof malformed};
    if(index==count)++count;
    PortableAssetPackView invalid=*pack;invalid.resolved_sections=sections;invalid.section_count=count;
    for(unsigned battle=0u;battle<2u;++battle){
        PortableGpHostScene rejected=*scene;
        rejected.pack=&invalid;rejected.input_aid=1u;
        rejected.quick_run=(uint8_t)!battle;rejected.battle=(uint8_t)battle;
        CHECK(portable_gp_host_begin(&rejected)==NULL);
        CHECK(SDL_WasInit(SDL_INIT_VIDEO)!=0u && SDL_RenderClear(scene->renderer));
        float width=0,height=0;
        CHECK(SDL_GetTextureSize(scene->texture,&width,&height) && width==240.0f && height==160.0f);
        CHECK(SDL_RenderTexture(scene->renderer,scene->texture,NULL,NULL));
    }
    free(sections);
    return 0;
}

int main(int argc, char **argv)
{
    const int arenas=argc==3 && strcmp(argv[1],"--arena-sessions")==0;
    const int visual=argc==3 && strcmp(argv[1],"--visual-seeds")==0;
    CHECK(argc == 2 || arenas || visual);
    FILE *file = fopen(argv[arenas||visual?2:1], "rb");
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    PortableAssetPackView pack;
    CHECK(portable_asset_pack_open(bytes, (size_t)length, &pack) == PORTABLE_ASSET_PACK_OK);
    if (visual) {
        const int result=imported_visual_seeds(&pack);free(bytes);return result;
    }
    if (arenas) {
        const int result=imported_arena_sessions(&pack);
        free(bytes);
        return result;
    }
    /* Structural motion fixtures have only two nonzero lookup entries.
     * Give lifecycle rendering its own complete authored Q15 basis without
     * changing the loader/rules fixture or its other callers. */
    PortableAssetSectionView *render_sections=calloc(pack.section_count,sizeof *render_sections);
    uint8_t *render_motion=NULL;
    CHECK(render_sections!=NULL);
    for(uint32_t i=0u;i<pack.section_count;++i){
        CHECK(portable_asset_pack_section_at(&pack,i,&render_sections[i])==PORTABLE_ASSET_PACK_OK);
        if(render_sections[i].kind!=PORTABLE_ASSET_SECTION_MOTION_TABLES)continue;
        CHECK(render_motion==NULL && render_sections[i].size>=36u+1024u*4u);
        render_motion=malloc(render_sections[i].size);CHECK(render_motion!=NULL);
        memcpy(render_motion,render_sections[i].bytes,render_sections[i].size);
        for(unsigned angle=0u;angle<1024u;++angle){
            const uint32_t sample=(uint32_t)(int32_t)(32768.0*sin(6.28318530717958647692*angle/1024.0));
            for(unsigned byte=0u;byte<4u;++byte)render_motion[36u+angle*4u+byte]=(uint8_t)(sample>>(byte*8u));
        }
        render_sections[i].bytes=render_motion;
    }
    CHECK(render_motion!=NULL);
    PortableAssetPackView render_pack=pack;render_pack.resolved_sections=render_sections;
    CHECK(test_gamepad_isolate());
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                   SDL_INIT_GAMEPAD));
    SDL_JoystickID gamepad_id;
    SDL_Joystick *joystick=test_gamepad_attach(&gamepad_id);
    CHECK(joystick!=NULL);
    SDL_Gamepad *gamepad=SDL_OpenGamepad(gamepad_id);
    CHECK(gamepad!=NULL&&portable_sdl_gamepad_driving(gamepad)==0u);
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,-16000));
    SDL_UpdateJoysticks();
    CHECK(portable_sdl_gamepad_driving(gamepad)==(PORTABLE_INPUT_ACCELERATE|
        PORTABLE_INPUT_HOP|PORTABLE_INPUT_ITEM|PORTABLE_INPUT_DOWN|PORTABLE_INPUT_LEFT));
    char *original=SDL_GetGamepadMapping(gamepad);
    char guid[33],mapping[512];
    SDL_GUIDToString(SDL_GetJoystickGUID(joystick),guid,sizeof guid);
    CHECK(snprintf(mapping,sizeof mapping,"%s,remapped,a:b1,b:b0,leftshoulder:b9,"
        "rightshoulder:b10,dpdown:b12,leftx:a0,lefttrigger:a4,righttrigger:a5,",guid)>0);
    CHECK(SDL_SetGamepadMapping(gamepad_id,mapping));
    SDL_UpdateJoysticks();
    CHECK((portable_sdl_gamepad_driving(gamepad)&(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_BRAKE))==
        PORTABLE_INPUT_BRAKE);
    CHECK(SDL_SetGamepadMapping(gamepad_id,original));SDL_free(original);
    for(int button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button)
        CHECK(SDL_SetJoystickVirtualButton(joystick,button,false));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0));
    SDL_UpdateJoysticks();SDL_CloseGamepad(gamepad);
    SDL_Window *window = SDL_CreateWindow("GP lifecycle", 240, 160,
                                          SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    SDL_Renderer *renderer = SDL_CreateRenderer(window,"software");
    CHECK(renderer != NULL);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, 240, 160);
    CHECK(texture != NULL);
    /* The native menu keeps its proportions in an ultrawide window. */
    static uint32_t menu_pixels[240u*160u];
    for(size_t i=0u;i<240u*160u;++i)menu_pixels[i]=(i%240u)&1u?UINT32_C(0xff654321):UINT32_C(0xff123456);
    CHECK(SDL_SetWindowSize(window,373,160));
    CHECK(SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR));
    PortableSoftwareFrame menu_frame={menu_pixels,240u,160u,NULL,1u,0u,0u,NULL};
    CHECK(portable_software_frame_present(renderer,texture,&menu_frame,0));
    SDL_Surface *menu_read=SDL_RenderReadPixels(renderer,NULL);
    CHECK(menu_read!=NULL);
    SDL_Surface *menu=SDL_ConvertSurface(menu_read,SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(menu_read);CHECK(menu!=NULL&&menu->w==373&&menu->h==160);
    for(int y=0;y<menu->h;++y){
        const uint32_t *row=(const uint32_t *)((const uint8_t *)menu->pixels+(size_t)y*(size_t)menu->pitch);
        for(int x=0;x<menu->w;++x)CHECK(row[x]==(x<66||x>=306?UINT32_C(0xff000000):((x-66)&1)?UINT32_C(0xff654321):UINT32_C(0xff123456)));
    }
    SDL_DestroySurface(menu);CHECK(SDL_SetWindowSize(window,240,160));
    CHECK(SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST));
    PortableGpHostScene scene = {.pack=&pack, .course_id=24u, .character_id=1u,
        .window=window, .renderer=renderer, .texture=texture, .course_header_index=4u, .retry_credits=3u};
    scene.character_id = UINT32_MAX;
    CHECK(portable_gp_host_begin(&scene) == NULL);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0u);
    CHECK(SDL_RenderClear(renderer));
    scene.character_id = 1u;
    CHECK(malformed_roulette_begin(&scene)==0);
    scene.pack=&render_pack;CHECK(mobile_gp_lifecycle(&scene)==0);scene.pack=&pack;
    CHECK(gp_start_pause(&scene)==0);
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        scene.internal_scale=(uint8_t)(cycle+1u);
        scene.race_renderer=(uint8_t)(cycle==0u||cycle==3u);
        scene.pack=scene.race_renderer?&render_pack:&pack;
        scene.aspect_width=scene.race_renderer?0u:cycle%2u?284u:373u;
        scene.fit_scaling=(uint8_t)(cycle&1u);
        scene.interpolate=(uint8_t)(cycle!=0u);
        scene.render_fps=cycle==1u?120u:144u;
        PortableGpHost *host = portable_gp_host_begin(&scene);
        CHECK(host != NULL);
        window_event(host, SDL_EVENT_WINDOW_EXPOSED);
        CHECK(portable_gp_host_frame(host) == 0);
        if(cycle==0u){
            CHECK(SDL_SetWindowSize(window,373,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_gp_host_frame(host)==0);
            SDL_Surface *wide=SDL_RenderReadPixels(renderer,NULL);
            CHECK(wide!=NULL&&wide->w==373&&wide->h==160);SDL_DestroySurface(wide);
            CHECK(SDL_SetWindowSize(window,240,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_gp_host_frame(host)==0);
        }
        for (unsigned frame = 0; frame < (cycle == 0 ? 200u : 6u); ++frame) {
            SDL_Delay(20);
            CHECK(portable_gp_host_frame(host) == 0);
        }
        window_event(host, SDL_EVENT_WINDOW_FOCUS_LOST);
        CHECK(portable_gp_host_frame(host) == 0);
        uint32_t paused[240u*160u],repeated[240u*160u];
        CHECK(read_test_frame(renderer,paused));
        if(scene.race_renderer){
            /* Rebuilding the selected 3D target must preserve the paused
             * world and HUD when returning to the original output size. */
            CHECK(SDL_SetWindowSize(window,373,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_gp_host_frame(host)==0);
            SDL_Surface *wide=SDL_RenderReadPixels(renderer,NULL);
            CHECK(wide!=NULL&&wide->w==373&&wide->h==160);SDL_DestroySurface(wide);
            CHECK(SDL_SetWindowSize(window,240,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_gp_host_frame(host)==0);
            CHECK(read_test_frame(renderer,repeated));
            CHECK(memcmp(paused,repeated,sizeof paused)==0);
        }
        SDL_Delay(100);CHECK(portable_gp_host_frame(host)==0);
        CHECK(read_test_frame(renderer,repeated));
        CHECK(memcmp(paused,repeated,sizeof paused)==0);
        window_event(host, SDL_EVENT_WINDOW_FOCUS_GAINED);
        CHECK(portable_gp_host_frame(host) == 0);
        key_event(host,SDLK_RETURN);CHECK(portable_gp_host_frame(host)==0);
        const SDL_Event remap={.gdevice={.type=SDL_EVENT_GAMEPAD_REMAPPED,.which=gamepad_id}};
        portable_gp_host_event(host,&remap);CHECK(portable_gp_host_frame(host)==0);
        CHECK(read_test_frame(renderer,paused));
        SDL_Delay(100);CHECK(portable_gp_host_frame(host)==0);
        CHECK(read_test_frame(renderer,repeated));
        CHECK(memcmp(paused,repeated,sizeof paused)==0);
        key_event(host,SDLK_RETURN);CHECK(portable_gp_host_frame(host)==0);
        window_event(host, SDL_EVENT_WINDOW_EXPOSED);
        CHECK(portable_gp_host_frame(host) == 0);
        if (cycle < 7) key_event(host, SDLK_ESCAPE);
        else {
            const SDL_Event quit = {.type = SDL_EVENT_QUIT};
            portable_gp_host_event(host, &quit);
        }
        CHECK(portable_gp_host_frame(host) == 1);
        PortableGpRaceHostOutcome outcome;
        CHECK(portable_gp_host_end(host, &outcome) == 0);
        CHECK(!outcome.results_completed);
        CHECK(outcome.quit_requested == (cycle == 7));
        CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0u);
        CHECK(SDL_RenderClear(renderer));
    }
    puts("GP 3D host resize: PASS 1x/4x paused world and HUD restored after auto-aspect resize");
    /* Escape from playback returns to its retained menu, not the shell.
     * Playback cannot silently fall back to live input without a recording. */
    PortableGpInputRecording *recording=calloc(1,sizeof *recording);
    CHECK(recording!=NULL);
    PortableGpHostScene replay_scene=scene;
    replay_scene.quick_run=1u;replay_scene.playback=1u;
    CHECK(portable_gp_host_begin(&replay_scene)==NULL);
    recording->complete=1u;recording->count=1u;
    recording->driving_profile=scene.pack->driving_profile;
    memcpy(recording->deterministic_digest,scene.pack->deterministic_digest,
        sizeof recording->deterministic_digest);
    replay_scene.recording=recording;
    /* A complete input buffer is meaningful only under its recorded driving
     * profile and deterministic content. Reject before consuming any input. */
    recording->driving_profile^=1u;
    CHECK(portable_gp_host_begin(&replay_scene)==NULL);
    CHECK(recording->complete&&recording->count==1u);
    recording->driving_profile^=1u;
    recording->deterministic_digest[0]^=1u;
    CHECK(portable_gp_host_begin(&replay_scene)==NULL);
    CHECK(recording->complete&&recording->count==1u);
    recording->deterministic_digest[0]^=1u;
    CHECK(memcmp(recording->deterministic_digest,scene.pack->deterministic_digest,
        sizeof recording->deterministic_digest)==0);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO)!=0u&&SDL_RenderClear(renderer));
    PortableGpHost *replay_host=portable_gp_host_begin(&replay_scene);
    CHECK(replay_host!=NULL);
    key_event(replay_host,SDLK_ESCAPE);
    CHECK(portable_gp_host_frame(replay_host)==0);
    uint32_t replay_picture[240u*160u];
    CHECK(read_test_frame(renderer,replay_picture)&&replay_picture[0]!=UINT32_C(0xff445566));
    key_event(replay_host,SDLK_RETURN);
    SDL_Delay(20);CHECK(portable_gp_host_frame(replay_host)==0);
    /* Replay returns through the same source row/window opening. The first
     * confirm advances the board; wait for the action window before choosing. */
    for(unsigned tick=0u;tick<16u;++tick){
        SDL_Delay(20);CHECK(portable_gp_host_frame(replay_host)==0);
    }
    key_event(replay_host,SDLK_RETURN);
    int replay_status=0;
    for(unsigned tick=0u;tick<10u&&replay_status==0;++tick){
        SDL_Delay(20);replay_status=portable_gp_host_frame(replay_host);
    }
    CHECK(replay_status==1);
    PortableGpRaceHostOutcome replay_outcome;
    CHECK(portable_gp_host_end(replay_host,&replay_outcome)==0 &&
        replay_outcome.results_completed && replay_outcome.next_mode==1u && !replay_outcome.quit_requested);
    CHECK(recording->complete&&recording->count==1u);
    free(recording);
    /* A continued cup uses the ordinary native grid on any course, with no
     * captured opening section or exact result-clock prerequisite. */
    PortableContentSelection selection;
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    PortableGpRaceSession *race=malloc(sizeof *race);
    PortableRecoveredRaceView race_view;
    CHECK(storage!=NULL&&race!=NULL);
    CHECK(portable_content_registry_select(&pack,24u,1u,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race_view)==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(test_gp_init(race,&race_view,
        (uint8_t)selection.character.character_class)==PORTABLE_GP_RACE_SESSION_OK);
    PortableGpCupSession previous={.stage=PORTABLE_GP_CUP_STAGE_NEXT_RACE,
        .course_id=24u,.cup_index=3u,.course_in_cup=1u,.course_header_index=4u,
        .selected_driver_class=(uint8_t)selection.character.character_class,
        .terrain_effect_counter=3u,.terrain_effect_phase=1u,.item_art_clock={12345u,0u}};
    for(unsigned racer=0u;racer<8u;++racer){
        previous.racer_driver_classes[racer]=race->roster.racers[racer].driver_class;
        previous.race_order[racer]=(uint8_t)(7u-racer);
    }
    free(race);free(storage);
    scene.previous_cup=&previous;scene.course_in_cup=1u;scene.cup_index=3u;
    PortableGpHost *continued=portable_gp_host_begin(&scene);
    CHECK(continued!=NULL&&portable_gp_host_frame(continued)==0);
    for(unsigned frame=0u;frame<12u;++frame){
        SDL_Delay(20);CHECK(portable_gp_host_frame(continued)==0);
    }
    SDL_CloseJoystick(joystick);CHECK(SDL_DetachVirtualJoystick(gamepad_id));
    const SDL_Event removed={.gdevice={.type=SDL_EVENT_GAMEPAD_REMOVED,.which=gamepad_id}};
    portable_gp_host_event(continued,&removed);CHECK(portable_gp_host_frame(continued)==0);
    uint32_t frozen[240u*160u],later[240u*160u];
    CHECK(read_test_frame(renderer,frozen));
    SDL_Delay(100);CHECK(portable_gp_host_frame(continued)==0);
    CHECK(read_test_frame(renderer,later));CHECK(memcmp(frozen,later,sizeof frozen)==0);
    joystick=test_gamepad_attach(&gamepad_id);CHECK(joystick!=NULL);
    const SDL_Event added={.gdevice={.type=SDL_EVENT_GAMEPAD_ADDED,.which=gamepad_id}};
    portable_gp_host_event(continued,&added);CHECK(portable_gp_host_frame(continued)==0);
    const SDL_Event resume={.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        .which=gamepad_id,.button=SDL_GAMEPAD_BUTTON_START}};
    portable_gp_host_event(continued,&resume);CHECK(portable_gp_host_frame(continued)==0);
    CHECK(read_test_frame(renderer,later));CHECK(memcmp(frozen,later,sizeof frozen)!=0);
    key_event(continued,SDLK_ESCAPE);
    CHECK(portable_gp_host_frame(continued)==1);
    CHECK(portable_gp_host_end(continued,NULL)==0);
    previous.item_art_clock.pending=1u;
    CHECK(portable_gp_host_begin(&scene)==NULL);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_CloseJoystick(joystick);CHECK(SDL_DetachVirtualJoystick(gamepad_id));
    SDL_Quit();
    free(render_motion);free(render_sections);
    free(bytes);
    puts("GP scene lifecycle: PASS");
    return 0;
}
