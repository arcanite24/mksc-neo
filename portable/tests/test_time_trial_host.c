#include <SDL3/SDL_main.h>
#include "sdl_test_frame.h"
#include "recovered_time_trial_host.h"
#include "content_registry.h"
#include "presentation_pack.h"
#include "camera_projection.h"
#include "driver_projection_asset.h"
#include "character_renderer.h"
#include "hud_renderer.h"
#include "frame_scheduler.h"
#include "race_particle_renderer.h"
#include "recovered_race_smoke.h"
#include "shell_cells_fixture.h"
#include "driver_item_fixture.h"
#include "driver_response_fixture.h"
#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "sdl_gamepad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "Time Trial host check failed at %d: %s\n", \
            __LINE__, #expression); return 1; } } while (0)

typedef struct {
    PortableRecoveredTimeTrialSnapshot snapshot;
    unsigned calls;
} MobileTrialAudio;

static int mobile_trial_audio_pause(void *user,int paused)
{
    (void)user;(void)paused;return 1;
}

static int mobile_trial_audio_frame(void *user,uint16_t buttons,int prestart,
    const PortableRecoveredTimeTrialSnapshot *snapshot)
{
    MobileTrialAudio *audio=user;(void)buttons;(void)prestart;
    audio->snapshot=*snapshot;++audio->calls;return 1;
}

static int mobile_trial_same_simulation(const PortableRecoveredTimeTrialSnapshot *a,
    const PortableRecoveredTimeTrialSnapshot *b)
{
    return a->paused&&b->paused&&a->phase==b->phase&&a->total_ticks==b->total_ticks&&
        a->elapsed_ticks==b->elapsed_ticks&&a->marker_clock==b->marker_clock&&
        a->position_x_q16==b->position_x_q16&&a->position_y_q16==b->position_y_q16&&
        a->speed_q16==b->speed_q16&&a->main_visual_rng_state==b->main_visual_rng_state&&
        a->ambient_digest==b->ambient_digest&&a->snow_weather_digest==b->snow_weather_digest;
}

static int mobile_trial_lifecycle(const PortableRecoveredTimeTrialHostScene *base,const char *fixture)
{
    char path[4096];CHECK(snprintf(path,sizeof path,"%s.mobile-replay.txt",fixture)>0);
    CHECK(strlen(fixture)+sizeof ".mobile-replay.txt"<=sizeof path);
    MobileTrialAudio *audio=calloc(1,sizeof *audio);
    PortableRecoveredTimeTrialSnapshot *paused=malloc(sizeof *paused);
    CHECK(audio!=NULL&&paused!=NULL);
    PortableRecoveredTimeTrialHostScene scene=*base;
    int32_t render_sine[1024];
    for(unsigned angle=0u;angle<1024u;++angle)
        render_sine[angle]=(int32_t)(32768.0*sin(6.28318530717958647692*angle/1024.0));
    PortableRecoveredRaceView render_race=*base->race;
    render_race.dependencies.sine_table=render_sine;render_race.dependencies.sine_count=1024u;
    scene.race=&render_race;scene.presentation.sine=render_sine;scene.presentation.sine_count=1024u;
    scene.input_aid=1u;scene.interpolate=0u;scene.internal_scale=1u;scene.aspect_width=0u;
    scene.race_renderer=1u;
    scene.audio_user=audio;scene.audio_set_paused=mobile_trial_audio_pause;
    scene.audio_render_frame=mobile_trial_audio_frame;scene.record_path=path;
    PortableTimeTrialHost *host=portable_time_trial_host_begin(&scene);CHECK(host!=NULL);
    PortableSettings settings;portable_settings_defaults(&settings);settings.touch_controls=1u;
    portable_sdl_input_configure(&settings);
    portable_mobile_lifecycle_start();
    unsigned frames=0u;
    while((audio->calls==0u||audio->snapshot.phase!=PORTABLE_RACE_PHASE_RACING||
           audio->snapshot.elapsed_ticks<8u)&&frames++<512u)
        CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(audio->snapshot.phase==PORTABLE_RACE_PHASE_RACING&&audio->snapshot.elapsed_ticks>=8u);
    const SDL_Event back={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=SDL_SCANCODE_AC_BACK}};
    SDL_Event repeat=back;repeat.key.repeat=true;
    portable_time_trial_host_event(host,&back);
    for(unsigned i=0u;i<20u;++i)CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(audio->snapshot.paused);*paused=audio->snapshot;
    const SDL_Event resize={.window={.type=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED}};
    CHECK(SDL_SetWindowSize(scene.host_window,373,160));
    portable_time_trial_host_event(host,&resize);CHECK(portable_time_trial_host_frame(host)==0);
    SDL_Surface *wide=SDL_RenderReadPixels(scene.host_renderer,NULL);
    CHECK(wide!=NULL&&wide->w==373&&wide->h==160);SDL_DestroySurface(wide);
    CHECK(mobile_trial_same_simulation(paused,&audio->snapshot));
    CHECK(SDL_SetWindowSize(scene.host_window,240,160));
    portable_time_trial_host_event(host,&resize);CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(mobile_trial_same_simulation(paused,&audio->snapshot));
    portable_time_trial_host_event(host,&repeat);
    for(unsigned i=0u;i<8u;++i)CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(mobile_trial_same_simulation(paused,&audio->snapshot));
    portable_time_trial_host_event(host,&back);
    for(unsigned i=0u;i<8u;++i)CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(!audio->snapshot.paused&&audio->snapshot.elapsed_ticks>paused->elapsed_ticks);

    int width,height;CHECK(SDL_GetCurrentRenderOutputSize(scene.host_renderer,&width,&height));
    PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
    CHECK(portable_mobile_touch_layout(width,height,buttons,PORTABLE_MOBILE_TOUCH_BUTTONS)==PORTABLE_MOBILE_TOUCH_BUTTONS);
    unsigned accelerator=0u;
    while(accelerator<PORTABLE_MOBILE_TOUCH_BUTTONS&&buttons[accelerator].driving_mask!=PORTABLE_INPUT_ACCELERATE)++accelerator;
    CHECK(accelerator<PORTABLE_MOBILE_TOUCH_BUTTONS);
    const SDL_Event finger={.tfinger={.type=SDL_EVENT_FINGER_DOWN,.touchID=12,.fingerID=4,
        .x=buttons[accelerator].x/(float)width,.y=buttons[accelerator].y/(float)height}};
    portable_time_trial_host_event(host,&finger);
    CHECK(portable_mobile_touch_driving()&PORTABLE_INPUT_ACCELERATE);
    SDL_Event lifecycle[]={
        {.type=SDL_EVENT_WILL_ENTER_BACKGROUND},{.type=SDL_EVENT_DID_ENTER_BACKGROUND},
        {.type=SDL_EVENT_WILL_ENTER_FOREGROUND},{.type=SDL_EVENT_DID_ENTER_FOREGROUND}};
    for(unsigned i=0u;i<4u;++i)CHECK(SDL_PushEvent(&lifecycle[i]));
    SDL_Event event;while(SDL_PollEvent(&event))portable_time_trial_host_event(host,&event);
    for(unsigned i=0u;i<20u;++i)CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(audio->snapshot.paused);*paused=audio->snapshot;
    CHECK(portable_mobile_touch_driving()==0u&&portable_mobile_touch_frontend()==0u);
    /* A delayed window notification after the explicit lifecycle cannot
     * create another semantic pause action. */
    const SDL_Event lost={.type=SDL_EVENT_WINDOW_FOCUS_LOST};
    portable_time_trial_host_event(host,&lost);
    const SDL_Event gained={.type=SDL_EVENT_WINDOW_FOCUS_GAINED};
    portable_time_trial_host_event(host,&gained);
    for(unsigned i=0u;i<12u;++i)CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(mobile_trial_same_simulation(paused,&audio->snapshot));
    portable_time_trial_host_event(host,&repeat);
    CHECK(portable_time_trial_host_frame(host)==0&&audio->snapshot.paused);
    portable_time_trial_host_event(host,&back);
    for(unsigned i=0u;i<8u;++i)CHECK(portable_time_trial_host_frame(host)==0);
    CHECK(!audio->snapshot.paused&&audio->snapshot.elapsed_ticks>paused->elapsed_ticks&&
        audio->snapshot.elapsed_ticks<=paused->elapsed_ticks+8u);
    const SDL_Event quit={.type=SDL_EVENT_QUIT};portable_time_trial_host_event(host,&quit);
    CHECK(portable_time_trial_host_frame(host)==1);
    PortableRecoveredTimeTrialHostOutcome outcome;
    CHECK(portable_time_trial_host_end(host,&outcome)==0&&outcome.quit_requested);
    FILE *record=fopen(path,"r");CHECK(record!=NULL);
    unsigned pauses=0u,resumes=0u;char line[16384];
    while(fgets(line,sizeof line,record)!=NULL){
        if(strcmp(line,"A pause 1\n")==0)++pauses;
        if(strcmp(line,"A pause 0\n")==0)++resumes;
    }
    CHECK(!ferror(record)&&fclose(record)==0&&pauses==2u&&resumes==2u);
    CHECK(remove(path)==0);
    portable_sdl_input_configure(NULL);portable_mobile_lifecycle_stop();free(paused);free(audio);
    puts("Time Trial mobile lifecycle: PASS Back, suspend RNG freeze and single pause records");
    return 0;
}

static void authored_item_palettes(uint16_t palettes[PORTABLE_HUD_ITEM_PALETTE_COUNT])
{
    for(size_t i=0u;i<PORTABLE_HUD_ITEM_PALETTE_COUNT;++i)
        palettes[i]=(uint16_t)(UINT16_C(0xa000)+(i%6u)*UINT16_C(0x1000));
}

/* The source result board clears HUD BG art while retaining OBJ coins,
 * rank and minimap markers. Distinct synthetic colors isolate those owners. */
static int finish_board_hud_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles[33u*32u],marker_tiles[64u];
    uint8_t rank_tiles[PORTABLE_GP_RESULTS_RANK_COUNT*PORTABLE_GP_RESULTS_RANK_TILE_BYTES];
    uint8_t rank_colors[64u]={0};
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT],marker_palette[32u]={0};
    uint16_t item_palettes[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    authored_item_palettes(item_palettes);
    uint16_t rank_palette[PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT]={0};
    memset(tiles,0x11,sizeof tiles);memset(marker_tiles,0x22,sizeof marker_tiles);
    memset(rank_tiles,0x11,sizeof rank_tiles);
    for(unsigned i=0;i<PORTABLE_HUD_PALETTE_COUNT;++i)palette[i]=31u;
    marker_palette[2]=31u<<5u;rank_palette[1]=31u<<10u;
    const PortableHudAssetView hud={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=PORTABLE_HUD_PALETTE_COUNT,
        .item_palette_selectors=item_palettes,.item_palette_count=PORTABLE_HUD_ITEM_PALETTE_COUNT};
    const PortableGpRaceHudAssetView gp={.rank_colors=rank_colors};
    const PortableGpMinimapMarkerAssetView markers={.tiles=marker_tiles,.palette=marker_palette};
    const PortableGpResultsAssetView rank={.rank_tiles=rank_tiles,.rank_tile_bytes=sizeof rank_tiles,
        .rank_palette=rank_palette,.rank_palette_count=PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT};
    PortableRaceSceneView view=*scene;
    view.hud=&hud;view.gp_hud=&gp;view.gp_markers=&markers;view.gp_rank_art=&rank;
    PortableRacePresentationState state={.show_gp_item=1u,.result_board_visible=1u,
        .phase=PORTABLE_RACE_PHASE_FINISHED,.lap_count=3u,.laps_completed=3u,.gp_coins=12u};
    const PortableRacePresentationState before=state;
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    for(unsigned i=0;i<240u*160u;++i)pixels[i]=0x123456u;
    CHECK(portable_race_scene_render_hud(&view,&state,&frame));
    CHECK(!memcmp(&before,&state,sizeof state));
    for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x){
        const uint32_t expected=y>=8u&&y<16u&&x<24u?0xff0000u:
            y>=92u&&y<100u&&x>=172u&&x<180u?0x00ff00u:
            y>=128u&&y<144u&&x>=8u&&x<32u?0x0000ffu:
            y>=144u&&y<152u&&x>=8u&&x<48u?0x0000ffu:0x123456u;
        CHECK(pixels[y*240u+x]==expected);
    }
    return 0;
}

/* Compare against the real Original HUD, not a second implementation of it.
 * Only the three authored indicator rectangles may change the frame. */
static int modern_air_hud_pixels(const PortableRaceSceneView *scene)
{
    for(unsigned scale=1u;scale<=4u;scale*=4u)for(unsigned wide=0u;wide<2u;++wide){
        const size_t width=wide?373u:240u,stride=width*scale+7u,count=stride*160u*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels),*original=malloc(count*sizeof *original);
        CHECK(pixels && original);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        for(size_t n=0u;n<count;++n)pixels[n]=0x1234abcd;
        CHECK(portable_race_scene_render_hud(scene,&state,&frame));
        memcpy(original,pixels,count*sizeof *pixels);
        const uint32_t colors[]={0xff605858u,0xff40b070u,0xff40e8ffu,0xffffffffu};
        for(unsigned stage=0u;stage<7u;++stage){
            state.modern_air_enabled=(uint8_t)(stage!=0u);
            state.modern_air=(PortableModernAirState){.armed=(uint8_t)(stage>=2u),
                .tricked=(uint8_t)(stage>=3u),.last_applied=(uint8_t)(stage==4u || stage==0u)};
            state.suppress_native_hud=(uint8_t)(stage==5u);
            state.result_board_visible=(uint8_t)(stage==6u);
            if(stage>=5u){
                state.modern_air_enabled=0u;
                for(size_t n=0u;n<count;++n)pixels[n]=0x1234abcd;
                CHECK(portable_race_scene_render_hud(scene,&state,&frame));
                memcpy(original,pixels,count*sizeof *pixels);state.modern_air_enabled=1u;
            }
            PortableRacePresentationState before;memcpy(&before,&state,sizeof state);
            for(size_t n=0u;n<count;++n)pixels[n]=0x1234abcd;
            CHECK(portable_race_scene_render_hud(scene,&state,&frame));
            CHECK(!memcmp(&before,&state,sizeof state));
            for(size_t y=0u;y<160u*scale;++y)for(size_t x=0u;x<stride;++x){
                uint32_t expected=original[y*stride+x];
                const size_t lx=x/scale,ly=y/scale,left=width/2u-6u;
                if(stage>=1u && stage<=4u && x<width*scale && ly>=142u && ly<149u && lx>=left && lx<left+12u){
                    const size_t dx=lx-left,dy=ly-142u;
                    expected=0xff181818u;
                    if((dy==1u && dx>=5u && dx<7u) || (dy==2u && dx>=3u && dx<9u) ||
                        (dy>=3u && dy<5u && dx>=1u && dx<11u))expected=colors[stage-1u];
                }
                CHECK(pixels[y*stride+x]==expected);
            }
        }
        free(original);free(pixels);
    }
    return 0;
}

static int modern_drift_hud_pixels(const PortableRaceSceneView *scene)
{
    const uint32_t guard=UINT32_C(0xabcddcba);
    const uint32_t active[3]={UINT32_C(0xffffb040),UINT32_C(0xff2090ff),UINT32_C(0xffff70d0)};
    PortableRaceSceneView scene_before;memcpy(&scene_before,scene,sizeof scene_before);
    for(unsigned layout=0u;layout<2u;++layout)for(unsigned resolution=0u;resolution<2u;++resolution){
        const size_t width=layout?373u:240u,scale=resolution?4u:1u;
        const size_t stride=width*scale+7u,rows=160u*scale,count=stride*rows;
        uint32_t *pixels=malloc(count*sizeof *pixels),*original=malloc(count*sizeof *original);
        CHECK(pixels && original);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,
            .scale=(uint8_t)scale,.stride=stride};
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        for(size_t i=0u;i<count;++i)pixels[i]=guard;
        CHECK(portable_race_scene_render_hud(scene,&state,&frame));
        memcpy(original,pixels,count*sizeof *pixels);
        for(unsigned mode=0u;mode<9u;++mode){
            /* Disabled with dirty state must match Original byte for byte.
             * Enabled idle, three charge tiers and three boost tiers follow. */
            state.modern_drift_enabled=(uint8_t)(mode!=0u);
            const unsigned tier=mode==0u?3u:mode<=4u?mode-1u:mode<=7u?mode-4u:0u;
            const int boosting=mode>=5u && mode<=7u;
            state.modern_drift=(PortableModernDriftState){.charge_ticks=11u,.boost_ticks=7u,
                .charge_tier=(uint8_t)(boosting?3u:tier),.boost_tier=(uint8_t)tier,
                .previous_held=1u,.last_applied_tier=(uint8_t)(boosting?tier:0u)};
            state.result_board_visible=(uint8_t)(mode==8u);
            /* The board suppresses the entire regular HUD; compare its own
             * Original baseline before enabling the modern indicator. */
            if(mode==8u){
                state.modern_drift_enabled=0u;
                for(size_t i=0u;i<count;++i)pixels[i]=guard;
                CHECK(portable_race_scene_render_hud(scene,&state,&frame));
                memcpy(original,pixels,count*sizeof *pixels);
                state.modern_drift_enabled=1u;
            }
            PortableRacePresentationState before;memcpy(&before,&state,sizeof before);
            for(size_t i=0u;i<count;++i)pixels[i]=guard;
            CHECK(portable_race_scene_render_hud(scene,&state,&frame));
            CHECK(!memcmp(&before,&state,sizeof state));
            CHECK(!memcmp(&scene_before,scene,sizeof *scene));
            for(size_t y=0u;y<rows;++y)for(size_t x=0u;x<stride;++x){
                uint32_t expected=original[y*stride+x];
                const size_t lx=x/scale,ly=y/scale,left=width/2u-19u;
                if(mode>0u && mode<8u && x<width*scale && ly>=151u && ly<158u && lx>=left && lx<left+38u){
                    const size_t pip=(lx-left)/13u,px=(lx-left)%13u,py=ly-151u;
                    if(pip<3u && px<12u){
                        expected=UINT32_C(0xff181818);
                        if(px>=1u && px<11u && py>=1u && py<6u)
                            expected=pip<tier?active[tier-1u]:UINT32_C(0xff605858);
                        if(boosting && pip<tier && px>=3u && px<9u && py==2u)
                            expected=UINT32_MAX;
                    }
                }
                CHECK(pixels[y*stride+x]==expected);
                if(x>=width*scale)CHECK(pixels[y*stride+x]==guard);
            }
        }
        free(original);free(pixels);
    }
    return 0;
}

static int lap_board_hud_capture_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles[0x80u*32u],portraits[PORTABLE_GP_RACE_HUD_PORTRAIT_BYTES];
    uint8_t course[PORTABLE_GP_RACE_HUD_COURSE_BYTES],maps[5u*52u*2u]={0};
    uint8_t pages[8]={0},colors[64]={0},custom_pixels[16u*16u*4u];
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT];
    uint16_t item_palettes[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    authored_item_palettes(item_palettes);
    memset(tiles,0x11,sizeof tiles);memset(portraits,0x11,sizeof portraits);
    memset(course,0x22,sizeof course);
    for(unsigned i=0;i<PORTABLE_HUD_PALETTE_COUNT;++i)palette[i]=31u<<10u;
    palette[(11u-10u)*16u+2u]=31u<<5u;
    for(unsigned i=0;i<16u*16u;++i){custom_pixels[4u*i]=0x77u;custom_pixels[4u*i+1u]=0x33u;
        custom_pixels[4u*i+2u]=0xccu;custom_pixels[4u*i+3u]=0xffu;}
    memset(custom_pixels,0xff,4u); /* Opaque white must not alias the empty sentinel. */
    PortableNativeImage portrait={.pixels=custom_pixels,.width=16u,.height=16u};
    const PortableHudAssetView hud={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=PORTABLE_HUD_PALETTE_COUNT,
        .item_palette_selectors=item_palettes,.item_palette_count=PORTABLE_HUD_ITEM_PALETTE_COUNT};
    const PortableGpRaceHudAssetView gp={.kind_pages=pages,.portrait_tiles=portraits,
        .course_tiles=course,.rank_maps=maps,.rank_colors=colors};
    PortableRaceSceneView view=*scene;view.hud=&hud;view.gp_hud=&gp;
    /* Capture must not touch independent marker/rank objects. */
    view.gp_markers=NULL;view.gp_rank_art=NULL;view.gp_racer_portraits[0]=&portrait;
    PortableRacePresentationState state={.show_gp_item=1u,.result_board_visible=1u,
        .phase=PORTABLE_RACE_PHASE_FINISHED,.lap_count=3u,.laps_completed=3u,.gp_coins=12u};
    const PortableRacePresentationState before=state;
    PortableLapBoardHud *layer=malloc(sizeof *layer),*again=malloc(sizeof *again);
    CHECK(layer!=NULL && again!=NULL);
    CHECK(portable_race_scene_capture_lap_board_hud(&view,&state,layer));
    CHECK(!memcmp(&before,&state,sizeof state));
    for(unsigned y=24u;y<40u;++y)for(unsigned x=8u;x<24u;++x)
        CHECK(layer->pixels[y*240u+x]==(x==8u && y==24u?UINT32_MAX:UINT32_C(0xffcc3377)));
    CHECK(layer->pixels[96u*240u+176u]==UINT32_C(0x0000ff00));
    CHECK(layer->pixels[92u*240u+172u]==PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
    CHECK(portable_race_scene_capture_lap_board_hud(&view,&state,again));
    CHECK(!memcmp(layer,again,sizeof *layer));
    /* TT's marker is independent of the captured HUD background, then clears
     * when source4A74C stops submitting4F340 after the finish. */
    uint16_t empty_cells[PORTABLE_MINIMAP_CELL_COUNT]={0};
    PortableMinimapAssetView minimap=*scene->minimap;minimap.cells=empty_cells;
    view.minimap=&minimap;view.gp_hud=NULL;state.show_gp_item=0u;state.show_time_trial_items=1u;
    CHECK(portable_race_scene_capture_lap_board_hud(&view,&state,layer));
    CHECK(layer->pixels[136u*240u+24u]==PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
    PortableSoftwareFrame frame={.pixels=again->pixels,.width=240u,.height=160u};
    portable_software_frame_clear(&frame,UINT32_C(0x00123456));
    CHECK(portable_race_scene_render_hud(&view,&state,&frame));
    for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x){
        CHECK(again->pixels[y*240u+x]==UINT32_C(0x00123456));
    }
    uint8_t marker_tiles[64];memset(marker_tiles,0x11,sizeof marker_tiles);
    uint16_t marker_palette[16]={0};marker_palette[1]=31u;
    PortableGpMinimapMarkerAssetView markers={.tiles=marker_tiles,
        .palette=marker_palette,.rival_palette=marker_palette};
    view.gp_markers=&markers;state.phase=PORTABLE_RACE_PHASE_RACING;
    CHECK(portable_race_scene_capture_lap_board_hud(&view,&state,layer));
    CHECK(layer->pixels[92u*240u+172u]==PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
    CHECK(portable_race_scene_render_hud(&view,&state,&frame));
    CHECK(again->pixels[92u*240u+172u]==UINT32_C(0x00ff0000));
    state.phase=PORTABLE_RACE_PHASE_FINISHED;
    portable_software_frame_clear(&frame,UINT32_C(0x00123456));
    CHECK(portable_race_scene_render_hud(&view,&state,&frame));
    CHECK(again->pixels[92u*240u+172u]==UINT32_C(0x00123456));
    free(again);free(layer);return 0;
}

/* Public rendering keeps the final-lap split until the board replaces HUD BG.
 * Synthetic glyphs isolate the host's snapshot-to-HUD mapping from road art. */
static int finished_split_hud_pixels(const PortableRecoveredTimeTrialHostScene *scene)
{
    uint8_t tiles[0x60u*32u]={0};uint16_t palette[PORTABLE_HUD_PALETTE_COUNT]={0};
    uint16_t item_palettes[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    authored_item_palettes(item_palettes);
    uint16_t digits[20],left[6],right[6],slash[2];
    for(unsigned i=0;i<20u;++i){
        digits[i]=(uint16_t)(i+1u);
        memset(tiles+(i+1u)*32u,(int)(((i%10u)+1u)*17u),32u);
    }
    for(unsigned i=1u;i<16u;++i)palette[32u+i]=(uint16_t)(i*UINT16_C(0x0421));
    memset(tiles+0x30u*32u,0xff,32u);memset(tiles+0x31u*32u,0xee,32u);
    memset(tiles+0x32u*32u,0xdd,32u);
    for(unsigned i=0;i<6u;++i){left[i]=0x30u;right[i]=0x31u;}
    slash[0]=slash[1]=0x32u;
    const PortableHudAssetView hud={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=PORTABLE_HUD_PALETTE_COUNT,
        .item_palette_selectors=item_palettes,.item_palette_count=PORTABLE_HUD_ITEM_PALETTE_COUNT,
        .digit_tiles=digits,.digit_count=20u,.left_cells=left,.left_cell_count=6u,
        .right_cells=right,.right_cell_count=6u,.slash_cells=slash,.slash_cell_count=2u};
    PortableRecoveredTimeTrialHostScene view=*scene;view.presentation.hud=&hud;
    PortableRecoveredTimeTrialSnapshot snapshot={.phase=PORTABLE_RACE_PHASE_FINISHED,
        .lap_count=3u,.laps_completed=3u,.total_ticks=1800u};
    snapshot.lap_ticks[0]=500u;snapshot.lap_ticks[1]=700u;snapshot.lap_ticks[2]=600u;
    uint32_t *pixels=malloc(240u*160u*sizeof *pixels),*expected=malloc(240u*160u*sizeof *expected);
    CHECK(pixels!=NULL && expected!=NULL);
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u};
    PortableSoftwareFrame expected_frame=frame;expected_frame.pixels=expected;
    uint32_t split_pixels[8u*8u];
    for(unsigned pass=0;pass<3u;++pass){
        if(pass==1u)snapshot.total_ticks=6000u;
        if(pass==2u)snapshot.lap_ticks[2]=1200u;
        const PortableRecoveredTimeTrialSnapshot immutable=snapshot;
        portable_software_frame_clear(&frame,UINT32_C(0x00234567));
        CHECK(portable_recovered_time_trial_render_with_ghost(&view,&snapshot,NULL,NULL,&frame,0u,0u));
        CHECK(!memcmp(&snapshot,&immutable,sizeof snapshot));
        const uint64_t cs=(uint64_t)snapshot.lap_ticks[2]*PORTABLE_FRAME_TICK_NUMERATOR*100u/
            PORTABLE_FRAME_TICK_DENOMINATOR;
        const PortableHudState state={.lap=2u,.lap_count=3u,.result_clock_mode=2u,
            .result_lap_centiseconds=(uint16_t)cs};
        portable_software_frame_clear(&expected_frame,PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
        CHECK(portable_hud_render_without_minimap_with_asset(view.presentation.minimap,&hud,&state,
            &expected_frame)==PORTABLE_HUD_RENDER_OK);
        unsigned compared=0u;
        for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<240u;++x)
            if(expected[y*240u+x]!=PORTABLE_LAP_BOARD_HUD_TRANSPARENT){
                CHECK(pixels[y*240u+x]==expected[y*240u+x]);++compared;
            }
        CHECK(compared>600u);
        unsigned changed=0u;
        for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<8u;++x){
            const unsigned at=y*8u+x;const uint32_t pixel=pixels[y*240u+200u+x];
            if(pass==0u)split_pixels[at]=pixel;
            else if(pass==1u)CHECK(pixel==split_pixels[at]);
            else changed+=(pixel!=split_pixels[at]);
        }
        if(pass==2u)CHECK(changed>0u);
    }
    free(expected);free(pixels);return 0;
}

static int snow_weather_pixels(const PortableRaceSceneView *scene)
{
    uint8_t image_pixels[3][8u*8u*4u];
    PortableWeatherAssetView profile={0};
    for(unsigned art=0;art<3u;++art){
        for(unsigned i=0;i<64u;++i){
            image_pixels[art][i*4u]=(uint8_t)(0x31u+art);
            image_pixels[art][i*4u+1u]=(uint8_t)(0x52u+art);
            image_pixels[art][i*4u+2u]=(uint8_t)(0x73u+art);
            image_pixels[art][i*4u+3u]=255u;
        }
        image_pixels[art][3]=0u;
        profile.images[art]=(PortableNativeImage){.pixels=image_pixels[art],.width=8u,.height=8u};
    }
    PortableRaceSceneView view=*scene;view.weather_profile=&profile;
    uint32_t pixels[373u*160u],baseline[373u*160u];
    for(unsigned wide=0;wide<2u;++wide){
        const unsigned width=wide?373u:240u,margin=(width-240u)/2u;
        PortableSoftwareFrame frame;CHECK(portable_software_frame_init(&frame,pixels,width,160u));
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
            .suppress_native_hud=1u,.suppress_racers=1u};
        CHECK(portable_race_scene_render(&view,&state,&frame));
        memcpy(baseline,pixels,(size_t)width*160u*sizeof *pixels);
        state.snow_weather =
            (PortableSnowWeatherSnapshot){.enabled = 1u, .count = 12u, .parity = 0u};
        state.snow_weather.particles[0] =
            (PortableSnowWeatherSprite){.x = 10, .y = 20, .art = 0, .visible = 1};
        state.snow_weather.particles[1] = (PortableSnowWeatherSprite){.x = 110, .y = 80, .art = 1};
        state.snow_weather.particles[2] =
            (PortableSnowWeatherSprite){.x = 30, .y = 40, .art = 1, .visible = 1};
        state.snow_weather.particles[4] =
            (PortableSnowWeatherSprite){.x = 60, .y = 70, .art = 2, .visible = 1};
        state.snow_weather.particles[6] =
            (PortableSnowWeatherSprite){.x = 90, .y = 252, .art = 0, .visible = 1};
        state.snow_weather.particles[8] =
            (PortableSnowWeatherSprite){.x = 244, .y = 100, .art = 0, .visible = 1};
        state.snow_weather.particles[10] =
            (PortableSnowWeatherSprite){.x = 10, .y = 20, .art = 2, .visible = 1};
        const PortableRacePresentationState before = state;
        CHECK(portable_race_scene_render(&view, &state, &frame));
        CHECK(!memcmp(&state, &before, sizeof state));
        for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<width;++x){
            uint32_t expected=baseline[y*width+x];
            const int nx=(int)x-(int)margin;
            if(nx>=10&&nx<18&&y>=20u&&y<28u && !(nx==10&&y==20u))expected=0xff735231u;
            if(nx>=30&&nx<38&&y>=40u&&y<48u && !(nx==30&&y==40u))expected=0xff745332u;
            if(nx>=60&&nx<68&&y>=70u&&y<78u && !(nx==60&&y==70u))expected=0xff755433u;
            if(nx>=90&&nx<98&&y<4u)expected=0xff735231u;
            CHECK(pixels[y*width+x]==expected);
        }
        const uint32_t repeated=portable_software_frame_checksum(&frame);
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(repeated == portable_software_frame_checksum(&frame));
        state.snow_weather.precise = 1u;
        for (unsigned i = 0u; i < state.snow_weather.count; ++i)
        {
            PortableSnowWeatherSprite *sprite = &state.snow_weather.particles[i];
            sprite->x_q16 = (int32_t)sprite->x * 65536 + 49152;
            sprite->y_q16 =
                (sprite->y >= 160u ? (int32_t)sprite->y - 256 : (int32_t)sprite->y) * 65536 + 49152;
        }
        CHECK(portable_race_scene_render(&view, &state, &frame));
        CHECK(repeated == portable_software_frame_checksum(&frame));
        state.snow_weather.enabled = 0;
        CHECK(portable_race_scene_render(&view, &state, &frame));
        CHECK(!memcmp(baseline, pixels, (size_t)width * 160u * sizeof *pixels));
    }
    return 0;
}

static int snow_weather_fractional_pixels(const PortableRaceSceneView *scene)
{
    enum
    {
        WIDTH = 373,
        SCALE = 4,
        STRIDE = WIDTH * SCALE + 7,
        ROWS = 160 * SCALE
    };
    uint32_t *pixels = malloc((size_t)STRIDE * ROWS * sizeof *pixels);
    uint32_t *baseline = malloc((size_t)STRIDE * ROWS * sizeof *baseline);
    CHECK(pixels && baseline);
    for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
        pixels[i] = UINT32_C(0x87654321);
    uint8_t art[8u * 8u * 4u];
    for (unsigned i = 0u; i < 64u; ++i)
    {
        art[i * 4u] = 0x31;
        art[i * 4u + 1u] = 0x52;
        art[i * 4u + 2u] = 0x73;
        art[i * 4u + 3u] = 255;
    }
    art[3] = 0;
    PortableWeatherAssetView profile = {0};
    profile.images[0] = (PortableNativeImage){.pixels = art, .width = 8, .height = 8};
    PortableRaceSceneView view = *scene;
    view.weather_profile = &profile;
    PortableSoftwareFrame frame = {pixels, WIDTH, 160u, NULL, SCALE, STRIDE,0u,NULL};
    PortableRacePresentationState state = {.phase = PORTABLE_RACE_PHASE_RACING,
                                           .lap_count = 3,
                                           .suppress_native_hud = 1,
                                           .suppress_racers = 1};
    CHECK(portable_race_scene_render(&view, &state, &frame));
    memcpy(baseline, pixels, (size_t)STRIDE * ROWS * sizeof *pixels);
    for (unsigned step = 0u; step < 5u; ++step)
    {
        state.snow_weather = (PortableSnowWeatherSnapshot){.enabled = 1, .count = 1, .precise = 1};
        state.snow_weather.particles[0] =
            (PortableSnowWeatherSprite){.x = 10,
                                        .y = 20,
                                        .visible = 1,
                                        .active = 1,
                                        .x_q16 = 10 * 65536 + (int32_t)(step % 4u) * 16384,
                                        .y_q16 = step == 4u ? -4 * 65536 + 16384 : 20 * 65536};
        if (step == 4u)
            state.snow_weather.particles[0].y = 252;
        const PortableRacePresentationState before = state;
        CHECK(portable_race_scene_render(&view, &state, &frame));
        CHECK(!memcmp(&state, &before, sizeof state));
        const int left = ((WIDTH - 240) / 2 + 10) * SCALE + (int)(step % 4u);
        const int top = step == 4u ? -15 : 20 * SCALE;
        for (unsigned y = 0u; y < ROWS; ++y)
            for (unsigned x = 0u; x < STRIDE; ++x)
            {
                uint32_t expected = baseline[(size_t)y * STRIDE + x];
                if ((int)x >= left && (int)x < left + 8 * SCALE && (int)y >= top &&
                    (int)y < top + 8 * SCALE && !((int)x < left + SCALE && (int)y < top + SCALE))
                    expected = UINT32_C(0xff735231);
                CHECK(pixels[(size_t)y * STRIDE + x] == expected);
                if (x >= WIDTH * SCALE)
                    CHECK(expected == UINT32_C(0x87654321));
            }
    }
    free(baseline);
    free(pixels);
    return 0;
}

/* Exercise the actual scene renderer: every recovered actor family must move
 * its body with height while preserving ground X/depth and immutable state. */
static int hop_camera_alignment_pixels(const PortableRaceSceneView *scene)
{
    uint8_t image_pixels[8u * 16u * 4u];
    for(size_t i=0u;i<sizeof image_pixels;i+=4u){
        image_pixels[i]=0xcf;image_pixels[i+1u]=0xa7;image_pixels[i+2u]=0x13;image_pixels[i+3u]=0xff;
    }
    PortableCourseSceneryArt tree_art={0};
    for(unsigned lod=0u;lod<4u;++lod){
        tree_art.lod[lod]=(PortableNativeImage){.pixels=image_pixels,.width=8u,.height=16u,.anchor_x=-4,.anchor_y=-16};
        tree_art.distance_shift[lod]=(uint8_t)lod;
    }
    const PortableCourseSceneryObject tree={.position_x_q16=20*65536,.affine_q8=256u};
    const PortableCourseSceneryView scenery={.objects=&tree,.object_count=1u,.native_art=&tree_art,.native_art_count=1u};
    uint8_t map[1]={0},road_tile[64];
    for(unsigned i=0u;i<64u;++i)road_tile[i]=(uint8_t)(1u+(i/8u+i%8u)%3u);
    const uint16_t palette[4]={0u,31u,31u<<5u,31u<<10u};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    const PortableDriverProjectionRules driver_rules={{{511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    PortableRaceSceneView view=*scene;view.sine=sine;view.scenery=&scenery;view.driver_projection=&driver_rules;
    view.world_map=(PortableRoadMapView){.map=map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u};
    view.tileset=(PortableTrackTilesetView){.tiles=road_tile,.tile_bytes=sizeof road_tile,.palette=palette,.palette_count=4u};
    uint32_t *pixels=malloc(750u*320u*sizeof *pixels),*baseline=malloc(750u*320u*sizeof *baseline);
    CHECK(pixels && baseline);
    const int32_t heights[]={0,32767,32768,65536,2*65536,12*65536,20*65536};
    const int expected_offsets[]={0,0,1,2,4,24,24};
    for(unsigned wide=0u;wide<2u;++wide){
        const unsigned scale=wide?2u:1u,stride=wide?750u:240u;
        PortableSoftwareFrame frame={.pixels=pixels,.width=wide?373u:240u,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        int baseline_top=160*(int)scale;
        for(unsigned pose=0u;pose<sizeof heights/sizeof heights[0];++pose){
            const int offset=portable_sky_motion_camera_offset(heights[pose],0u,0u);
            CHECK(offset==expected_offsets[pose]);
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                .height_q16=heights[pose],.sky_motion={.initialized=1u,.road_line=(int16_t)(74+offset),
                    .split_y=-1,.layer_source={0u,1u,2u}}};
            const PortableRacePresentationState before=state;
            CHECK(portable_race_scene_render(&view,&state,&frame));
            CHECK(!memcmp(&state,&before,sizeof state));
            int top=160*(int)scale;
            for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<frame.width*scale;++x)
                if(pixels[y*stride+x]==0xff13a7cfu && (int)y<top)top=(int)y;
            CHECK(top<160*(int)scale);
            if(!pose){
                baseline_top=top;memcpy(baseline,pixels,stride*160u*scale*sizeof *pixels);
                unsigned colored=0u;
                for(unsigned y=86u;y<108u;++y)for(unsigned x=8u;x<40u;++x)
                    colored+=baseline[y*scale*stride+x*scale]!=0u;
                CHECK(colored>0u);
            }
            CHECK(top==baseline_top+offset*(int)scale);
            /* The road and the stationary tree share the source camera shift.
             * Moving screen_y instead would alter these sampled road pixels. */
            for(unsigned y=86u;y<108u;++y)for(unsigned x=8u;x<40u;++x)
                CHECK(pixels[(y+(unsigned)offset)*scale*stride+x*scale]==baseline[y*scale*stride+x*scale]);
            int16_t body_x,body_y;uint16_t inverse;
            CHECK(portable_race_scene_player_projection(&view,&state,&body_x,&body_y,&inverse));
            CHECK(body_x==120 && body_y==140+offset-heights[pose]/16384 && inverse==256u);
        }
    }
    free(baseline);free(pixels);return 0;
}

/* A stationary tree and the road beneath it must use the same visible camera
 * turn, even when the player's current hop offset has already advanced. */
static int latched_hop_ground_pixels(const PortableRaceSceneView *scene)
{
    uint8_t image[8u*16u*4u];
    for(size_t i=0u;i<sizeof image;i+=4u){image[i]=0xcf;image[i+1u]=0xa7;image[i+2u]=0x13;image[i+3u]=0xff;}
    PortableCourseSceneryArt art={0};
    for(unsigned lod=0u;lod<4u;++lod){
        art.lod[lod]=(PortableNativeImage){.pixels=image,.width=8u,.height=16u,.anchor_x=-4,.anchor_y=-16};
        art.distance_shift[lod]=(uint8_t)lod;
    }
    const PortableCourseSceneryObject tree={.position_x_q16=20*65536,.affine_q8=256u};
    const PortableCourseSceneryView scenery={.objects=&tree,.object_count=1u,.native_art=&art,.native_art_count=1u};
    uint8_t map[1]={0},tile[64];
    for(unsigned i=0u;i<64u;++i)tile[i]=(uint8_t)(1u+(i/8u+i%8u)%3u);
    const uint16_t palette[4]={0u,31u,31u<<5u,31u<<10u};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    const PortableDriverProjectionRules driver_rules={{{511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    PortableRaceSceneView view=*scene;view.sine=sine;view.scenery=&scenery;view.driver_projection=&driver_rules;
    view.world_map=(PortableRoadMapView){.map=map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u};
    view.tileset=(PortableTrackTilesetView){.tiles=tile,.tile_bytes=sizeof tile,.palette=palette,.palette_count=4u};
    const unsigned scales[]={1u,2u,3u,4u,8u};
    const int32_t prior_offsets[]={0,8192,16384,49152,81920,-16384,-49152};
    for(unsigned wide=0u;wide<2u;++wide)for(unsigned resolution=0u;resolution<5u;++resolution){
        const unsigned scale=scales[resolution],width=wide?373u:240u,stride=width*scale+7u,rows=160u*scale;
        const size_t count=(size_t)stride*rows;
        uint32_t *storage=malloc((count+16u)*sizeof *storage),*baseline=malloc(count*sizeof *baseline);
        CHECK(storage && baseline);
        PortableSoftwareFrame frame={.pixels=storage+8u,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        for(unsigned pose=0u;pose<7u;++pose)for(unsigned current=0u;current<2u;++current){
            const int32_t prior=prior_offsets[pose];
            const int integer=prior>=0?prior/65536:-((-prior+65535)/65536);
            /* Independent physical translation oracle; the current offset is
             * deliberately different and may advance without moving scenery. */
            const int64_t numerator=(int64_t)prior*scale;
            const int shift=scale==1u?integer:(int)(numerator>=0?(numerator+65535)/65536:-((-numerator)/65536));
            for(size_t i=0u;i<count+16u;++i)storage[i]=0x2468ace0u;
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                .camera_position_override=1u,.suppress_player=1u,.suppress_racers=1u,
                .suppress_native_hud=1u,.suppress_native_minimap=1u,.height_q16=(int32_t)current*3*65536,
                .sky_motion={.initialized=1u,.precise=1u,.road_line=(int16_t)(current?80:74),
                    .draw_road_line_valid=1u,.draw_road_line=(int16_t)(74+integer),
                    .draw_road_line_q16=74*65536+prior,.split_y=-1,.layer_source={0u,1u,2u}}};
            unsigned char immutable[sizeof state];memcpy(immutable,&state,sizeof state);
            CHECK(portable_race_scene_render(&view,&state,&frame));
            CHECK(!memcmp(immutable,&state,sizeof state));
            if(!pose && !current)memcpy(baseline,frame.pixels,count*sizeof *baseline);
            unsigned tree_pixels=0u;
            for(unsigned y=0u;y<rows;++y){
                for(unsigned x=0u;x<width*scale;++x){
                    const int source_y=(int)y-shift;
                    const int expected=source_y>=0 && source_y<(int)rows &&
                        baseline[(size_t)source_y*stride+x]==0xff13a7cfu;
                    if((frame.pixels[(size_t)y*stride+x]==0xff13a7cfu)!=expected){
                        fprintf(stderr,"latched tree scale=%u width=%u prior=%d current=%u shift=%d pixel=%u,%u\n",
                            scale,width,prior,current,shift,x,y);free(baseline);free(storage);return 1;
                    }
                    tree_pixels+=(unsigned)expected;
                }
                for(unsigned x=width*scale;x<stride;++x)CHECK(frame.pixels[(size_t)y*stride+x]==0x2468ace0u);
            }
            CHECK(tree_pixels>0u);
            for(unsigned y=90u*scale;y<108u*scale;++y)for(unsigned x=8u*scale;x<40u*scale;++x)
                CHECK(frame.pixels[(size_t)((int)y+shift)*stride+x]==baseline[(size_t)y*stride+x]);
            for(unsigned i=0u;i<8u;++i)CHECK(storage[i]==0x2468ace0u && storage[count+8u+i]==0x2468ace0u);
            int16_t x,y;uint16_t inverse;
            CHECK(portable_race_scene_player_projection(&view,&state,&x,&y,&inverse));
            CHECK(x==120 && y==140+integer-state.height_q16/16384 && inverse==256u);
        }
        free(baseline);free(storage);
    }
    return 0;
}

/* Fractional camera motion must advance scenery by physical pixels instead
 * of holding it for a native pixel and then jumping an entire scaled block. */
static int scenery_precision_motion_pixels(const PortableRaceSceneView *scene)
{
    uint8_t image_pixels[8u*16u*4u];
    for(size_t i=0u;i<sizeof image_pixels;i+=4u){
        image_pixels[i]=0xcf;image_pixels[i+1u]=0xa7;image_pixels[i+2u]=0x13;image_pixels[i+3u]=0xff;
    }
    PortableCourseSceneryArt tree_art={0};
    for(unsigned lod=0u;lod<4u;++lod){
        tree_art.lod[lod]=(PortableNativeImage){.pixels=image_pixels,.width=8u,.height=16u,.anchor_x=-4,.anchor_y=-16};
        tree_art.distance_shift[lod]=(uint8_t)lod;
    }
    const PortableCourseSceneryObject tree={.position_x_q16=20*65536,.affine_q8=256u};
    const PortableCourseSceneryView scenery={.objects=&tree,.object_count=1u,.native_art=&tree_art,.native_art_count=1u};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    for(unsigned i=1u;i<=16u;++i){sine[i]=(int32_t)i*64;sine[256u-i]=32768;}
    PortableRaceSceneView view=*scene;view.sine=sine;view.scenery=&scenery;
    const PortableRaceSceneView view_before=view;
    for(unsigned wide=0u;wide<2u;++wide)for(unsigned scale=1u;scale<=4u;scale*=2u){
        const unsigned width=wide?373u:240u,stride=width*scale+4u,rows=160u*scale;
        const size_t count=(size_t)stride*rows;
        uint32_t *storage=malloc((count+16u)*sizeof *storage);
        CHECK(storage!=NULL);
        PortableSoftwareFrame frame={.pixels=storage+8u,.width=width,.height=160u,
            .scale=(uint8_t)scale,.stride=stride};
        for(unsigned mode=0u;mode<3u;++mode){
            int previous_left=0,first_top=0;unsigned moves=0u;
            for(unsigned step=0u;step<=16u;++step){
                for(size_t i=0u;i<count+16u;++i)storage[i]=0x2468ace0u;
                PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                    .camera_position_override=1u,.suppress_player=1u,.suppress_racers=1u,
                    .suppress_native_hud=1u,.suppress_native_minimap=1u};
                if(mode==0u)state.camera_position_x_q16=(int32_t)step*2048;
                if(mode==1u)state.camera_yaw=(uint16_t)(step*64u);
                const int32_t height=mode==2u?(int32_t)step*8192:0;
                const int offset=portable_sky_motion_camera_offset(height,0u,0u);
                if(mode==2u){
                    state.height_q16=height;
                    state.sky_motion=(PortableSkyMotionSnapshot){.initialized=1u,.road_line=(int16_t)(74+offset),
                        .split_y=-1,.layer_source={0u,1u,2u}};
                }
                const PortableRacePresentationState before=state;
                CHECK(portable_race_scene_render(&view,&state,&frame));
                CHECK(!memcmp(&state,&before,sizeof state) && !memcmp(&view,&view_before,sizeof view));
                int left=(int)(width*scale),top=(int)rows;
                for(unsigned y=0u;y<rows;++y){
                    for(unsigned x=0u;x<width*scale;++x)if(frame.pixels[(size_t)y*stride+x]==0xff13a7cfu){
                        if((int)x<left)left=(int)x;
                        if((int)y<top)top=(int)y;
                    }
                    for(unsigned x=width*scale;x<stride;++x)CHECK(frame.pixels[(size_t)y*stride+x]==0x2468ace0u);
                }
                for(unsigned i=0u;i<8u;++i)CHECK(storage[i]==0x2468ace0u && storage[count+8u+i]==0x2468ace0u);
                CHECK(left<(int)(width*scale) && top<(int)rows);
                if(!step)first_top=top;
                if(mode==2u)CHECK(top==first_top+offset*(int)scale);
                if(step && mode<2u){
                    CHECK(abs(left-previous_left)<=1);
                    moves+=(unsigned)(left!=previous_left);
                }
                previous_left=left;
            }
            if(mode==0u)CHECK(moves>=scale);
            if(mode==1u && scale>1u && moves==0u){
                fprintf(stderr,"scenery turn produced no physical movement: width=%u scale=%u left=%d\n",
                    width,scale,previous_left);
                free(storage);return 1;
            }
        }
        free(storage);
    }
    return 0;
}

static int actor_height_pixels(const PortableRaceSceneView *scene)
{
    uint32_t pixels[240u*160u];
    PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    uint8_t image_pixels[8u*16u*4u];
    for(size_t i=0;i<sizeof image_pixels;i+=4u){
        image_pixels[i]=0xcf;image_pixels[i+1u]=0xa7;
        image_pixels[i+2u]=0x13;image_pixels[i+3u]=0xff;
    }
    PortableCourseSceneryArt art[72]={0};
    for(unsigned f=0;f<72u;++f)for(unsigned lod=0;lod<4u;++lod){
        art[f].lod[lod]=(PortableNativeImage){.pixels=image_pixels,.width=8u,.height=16u,.anchor_x=-4,.anchor_y=-16};
        art[f].distance_shift[lod]=(uint8_t)lod;
    }
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    PortableRaceSceneView view=*scene;
    view.sine=sine;view.scenery=NULL;
    view.lava_bubble_art=art;view.thwomp_art=art;view.crab_art=art;
    view.seagull_art=art;view.cannonball_art=art;view.piranha_art=art;view.snowman_art=art;view.mouse_art=art;view.thundercloud_art=art;view.falling_star_art=art;view.penguin_art=art;view.boo_art=art;
    const PortableCourseSceneryObject prop_object={.position_x_q16=20*65536,.affine_q8=256u};
    const PortableCourseSceneryView prop_art={.objects=&prop_object,.object_count=1u,
        .native_art=art,.native_art_count=1u};
    for(unsigned family=0;family<14u;++family){
        view.scenery=(family==9u || family==13u)?&prop_art:NULL;
        int top[4];
        for(unsigned pose=0;pose<4u;++pose){
            const int32_t heights[4]={0,8*65536,-8*65536,40*65536};
            const int32_t height=heights[pose];
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
            switch(family){
            case 0:state.lava_bubbles.count=1u;
                state.lava_bubbles.actors[0]=(PortableLavaBubble){.x_q16=20*65536,.height_q16=height,.flags=1u};break;
            case 1:state.thwomps.count=1u;
                state.thwomps.actors[0]=(PortableThwomp){.x_q16=20*65536,.height_q16=height,.flags=1u};break;
            case 2:state.crabs.count=1u;
                state.crabs.actors[0]=(PortableCrab){.x_q16=20*65536,.height_q16=height,.flags=1u};break;
            case 3:state.seagulls.count=1u;
                state.seagulls.actors[0]=(PortableSeagull){.x_q16=20*65536,.height_q16=height,
                    .flags=1u,.affine_x_q8=224u,.affine_y_q8=224u};break;
            case 4:state.cannonballs.count=1u;
                state.cannonballs.actors[0]=(PortableCannonball){.x_q16=20*65536,.height_q16=height,
                    .flags=1u,.affine_x_q8=224u,.affine_y_q8=224u};break;
            case 5:state.piranhas.count=1u;
                state.piranhas.actors[0]=(PortablePiranha){.x_q16=20*65536,.height_q16=height,
                    .flags=1u,.affine_x_q8=224u,.affine_y_q8=224u};break;
            case 6:state.snowmen.count=1u;
                state.snowmen.actors[0]=(PortableSnowman){.x_q16=20*65536,.height_q16=height,.flags=1u};break;
            case 7:state.penguins.count=1u;
                state.penguins.actors[0]=(PortablePenguin){.x_q16=20*65536,.height_q16=height,
                    .affine_y_q8=256,.flags=1u};break;
            case 8:state.boos.count=1u;
                state.boos.actors[0]=(PortableBoo){.x_q16=20*65536,.height_q16=height,
                    .affine_y_q8=256,.flags=1u};break;
            case 12:state.falling_stars.count=1u;
                state.falling_stars.actors[0]=(PortableFallingStar){.x_q16=20*65536,.height_q16=height,
                    .flags=1u,.affine_q8=256u};break;
            case 11:state.thunderclouds.count=1u;
                state.thunderclouds.palette_phase[1]=8u;
                state.thunderclouds.actors[0]=(PortableThundercloud){.x_q16=20*65536,.height_q16=height,
                    .flags=1u,.art=7u};break;
            case 10:state.mice.count=1u;
                state.mice.actors[0]=(PortableMouse){.x_q16=20*65536,.height_q16=height,
                    .flags=1u,.affine_y_q8=-256};break;
            default:state.course_props.count=1u;
                state.course_props.actors[0]=(PortableCourseProp){.x_q16=20*65536,.height_q16=height,
                    .affine_x_q8=256,.affine_y_q8=-256,.flags=1u,.source_code=family==13u?0xa00000u:0u};break;
            }
            const PortableRacePresentationState before=state;
            CHECK(portable_race_scene_render(&view,&state,&frame));
            CHECK(memcmp(&state,&before,sizeof state)==0);
            top[pose]=160;
            for(size_t y=0;y<160u;++y)for(size_t x=0;x<240u;++x)
                if(pixels[y*240u+x]==0xff13a7cfu && (int)y<top[pose])top[pose]=(int)y;
            CHECK(pose==3u?top[pose]==160:top[pose]<160);
            if(family==9u || family==13u){
                state.course_props.actors[0].flags=0u;
                CHECK(portable_race_scene_render(&view,&state,&frame));
                for(size_t i=0;i<240u*160u;++i)CHECK(pixels[i]!=0xff13a7cfu);
            }
        }
        CHECK(top[1]==top[0]-32 && top[2]==top[0]+32);
    }
    /* The asymmetric source image rotates about its imported anchor. */
    view.scenery=NULL;
    int widths[2]={0},heights[2]={0};
    for(unsigned pose=0;pose<2u;++pose){
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        state.falling_stars.count=1u;
        state.falling_stars.actors[0]=(PortableFallingStar){.x_q16=20*65536,
            .flags=1u,.affine_q8=256u,.angle=(uint16_t)(pose*16384u)};
        const PortableRacePresentationState before=state;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(!memcmp(&before,&state,sizeof state));
        int left=240,right=0,top=160,bottom=0;
        for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x)if(pixels[y*240u+x]==0xff13a7cfu){
            if((int)x<left)left=(int)x;
            if((int)x>right)right=(int)x;
            if((int)y<top)top=(int)y;
            if((int)y>bottom)bottom=(int)y;
        }
        CHECK(right>left && bottom>top);
        widths[pose]=right-left;heights[pose]=bottom-top;
    }
    CHECK(widths[0]==heights[1] && heights[0]==widths[1] && widths[0]<widths[1]);
    /* 58480: horizontal facing follows the pre-turn velocity and camera,
     * while the source turn windows reverse the visible side once more. */
    for(unsigned y=0;y<16u;++y)for(unsigned x=4u;x<8u;++x){
        const size_t i=(y*8u+x)*4u;
        image_pixels[i]=0x21;image_pixels[i+1u]=0x45;image_pixels[i+2u]=0xef;
    }
    view.scenery=NULL;
    int centroid[3]={0};
    for(unsigned pose=0;pose<3u;++pose){
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        state.mice.count=1u;
        CHECK(portable_mouse_init(&state.mice.actors[0],20*65536,0,0u,0u));
        state.mice.actors[0].facing_x_q16=pose==0u?-32768:32768;
        state.mice.actors[0].turn_flip=pose==2u;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        int count=0;
        for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x)
            if(pixels[y*240u+x]==0xff13a7cfu){centroid[pose]+=(int)x;++count;}
        CHECK(count>0);centroid[pose]/=count;
    }
    CHECK(centroid[0]<centroid[1] && centroid[0]==centroid[2]);
    for(unsigned y=0;y<16u;++y)for(unsigned x=4u;x<8u;++x){
        const size_t i=(y*8u+x)*4u;
        image_pixels[i]=0xcf;image_pixels[i+1u]=0xa7;image_pixels[i+2u]=0x13;
    }
    /* 53BE8's sunken scenery uses the same height in imported previews and
     * live snapshots; the renderer must not move the simulation copy. */
    PortableCourseSceneryObject sunken=prop_object;sunken.source_code=0x10001u;
    PortableCourseSceneryView sunken_art=prop_art;sunken_art.objects=&sunken;
    view.scenery=&sunken_art;
    uint32_t preview_pixels[240u*160u];
    PortableRacePresentationState scenery_state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
    CHECK(portable_race_scene_render(&view,&scenery_state,&frame));
    memcpy(preview_pixels,pixels,sizeof pixels);
    scenery_state.course_props.count=1u;
    CHECK(portable_course_prop_init(&scenery_state.course_props.actors[0],
        20*65536,0,0x10001u,256u,0u));
    const PortableRacePresentationState scenery_before=scenery_state;
    CHECK(portable_race_scene_render(&view,&scenery_state,&frame));
    CHECK(!memcmp(&scenery_state,&scenery_before,sizeof scenery_state));
    CHECK(!memcmp(preview_pixels,pixels,sizeof pixels));
    size_t scenery_pixels=0u;
    for(size_t i=0;i<240u*160u;++i)scenery_pixels+=pixels[i]==0xff13a7cfu;
    CHECK(scenery_pixels>0u);
    /* Live beach-prop art follows the contact phase, independently of
     * the immutable placement and without mutating presentation state. */
    uint8_t alternate_pixels[sizeof image_pixels];
    for(size_t i=0;i<sizeof alternate_pixels;i+=4u){
        alternate_pixels[i]=0x21;alternate_pixels[i+1u]=0x45;
        alternate_pixels[i+2u]=0xef;alternate_pixels[i+3u]=0xff;
    }
    for(unsigned lod=0;lod<4u;++lod)art[1].lod[lod].pixels=alternate_pixels;
    PortableCourseSceneryView beach_art=prop_art;beach_art.native_art_count=2u;
    view.scenery=&beach_art;
    for(unsigned phase=0u;phase<2u;++phase){
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        state.course_props.count=1u;
        state.course_props.actors[0]=(PortableCourseProp){.x_q16=20*65536,
            .source_code=0x860000u,.affine_x_q8=256,.affine_y_q8=256,
            .flags=1u,.contact_phase=(uint8_t)phase};
        const PortableRacePresentationState before=state;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(!memcmp(&state,&before,sizeof state));
        size_t expected=0u,other=0u;
        for(size_t i=0;i<240u*160u;++i){
            expected+=pixels[i]==(phase?0xffef4521u:0xff13a7cfu);
            other+=pixels[i]==(phase?0xff13a7cfu:0xffef4521u);
        }
        CHECK(expected>0u && other==0u);
    }
    for(unsigned front=0;front<2u;++front){
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        state.course_props.count=1u;
        CHECK(portable_course_prop_init(&state.course_props.actors[0],20*65536,0,0xa00000u,224u,0u));
        state.course_props.actors[0].kamek.has_facing=1u;
        state.course_props.actors[0].kamek.heading=front?0x4000u:0xc000u;
        const PortableRacePresentationState before=state;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(!memcmp(&state,&before,sizeof state));
        size_t matching=0u;
        for(size_t i=0;i<240u*160u;++i){
            matching+=pixels[i]==(front?0xffef4521u:0xff13a7cfu);
            CHECK(pixels[i]!=(front?0xff13a7cfu:0xffef4521u));
        }
        CHECK(matching>0u);
    }
    /* Moving props use live positions and camera-facing stride art. */
    PortableCourseSceneryView patrol_art=prop_art;patrol_art.native_art_count=4u;
    view.scenery=&patrol_art;
    for(unsigned lod=0;lod<4u;++lod)art[3].lod[lod].pixels=alternate_pixels;
    int left[2]={240,240};
    for(unsigned pose=0;pose<2u;++pose){
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u};
        state.course_props.count=1u;
        CHECK(portable_course_prop_init(&state.course_props.actors[0],(20+(int)pose*4)*65536,
            0,0xa10000u,224u,0u));
        state.course_props.actors[0].mechakoopa.has_facing=1u;
        state.course_props.actors[0].mechakoopa.heading=0x4000u;
        const PortableRacePresentationState before=state;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(!memcmp(&before,&state,sizeof state));
        size_t matching=0u;
        for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x){
            CHECK(pixels[y*240u+x]!=0xff13a7cfu);
            if(pixels[y*240u+x]==0xffef4521u){
                ++matching;if((int)x<left[pose])left[pose]=(int)x;
            }
        }
        CHECK(matching>0u);
    }
    CHECK(left[0]!=left[1]);
    return 0;
}

static int kart_palette_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles[PORTABLE_CHARACTER_FRAME_BYTES],slots[64]={0};
    memset(tiles,0x33,sizeof tiles);
    uint16_t banks[256]={0};banks[3]=0x001fu;banks[7u*16u+3u]=0x7c00u;banks[1u*16u+3u]=0x7c00u;
    PortableCharacterAssetView character={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=banks,.palette_count=16u,.palette_banks=banks,.palette_bank_count=16u,
        .width=64u,.height=64u,.direction_tiles=tiles,.direction_tile_bytes=sizeof tiles,
        .direction_frame_count=1u,.view_frame_slots=slots,.view_frame_slot_count=64u};
    PortableRaceSceneView view=*scene;view.character=&character;view.scenery=NULL;
    uint32_t pixels[240*160];PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    for (unsigned actor=0u;actor<2u;++actor) {
        view.opponent_count=actor;
        view.opponent_characters[0]=&character;
        size_t colors[2]={0};
        for(unsigned pass=0;pass<2u;++pass){
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,
                .lap_count=3u,.palette_bank=pass?7u:0u,
                .suppress_player=(uint8_t)actor,.opponent_count=actor};
            state.opponents[0]=(PortableRaceOpponentPresentation){
                .position_x_q16=20*65536,.visible=1u,
                .character_affine_scale=256u,.palette_bank=pass?1u:0u};
            const PortableRacePresentationState before=state;
            CHECK(portable_race_scene_render(&view,&state,&frame));
            CHECK(!memcmp(&state,&before,sizeof state) && character.palette==banks);
            for(size_t i=0;i<240u*160u;++i)
                if(pixels[i]==(pass?0x0000ffu:0xff0000u))++colors[pass];
        }
        CHECK(colors[0]>0u && colors[0]==colors[1]);
    }
    return 0;
}

/* Distinct LOD colors expose the selected source bank in the actual scene,
 * while equal poses must project identically for local, opponent and ghost. */
static int lava_smoke_pixels(void)
{
    uint8_t tiles[64];memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);
    const uint16_t palette[16]={0u,0x001fu,0x03e0u};
    const uint8_t cells[16]={1u,0u,0u,0u,0u,0u,0u,0u,
        1u,0u,0u,0u,0u,0u,1u,0u};
    const PortableRaceParticleFrame steps[2]={{0u,8u,6u},{8u,8u,6u}};
    const PortableRaceParticleAssetView art={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.cells=cells,.cell_bytes=sizeof cells,
        .frames=steps,.frame_count=2u};
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    const int16_t distances[3]={128,256,1024};
    for(unsigned d=0u;d<3u;++d)for(unsigned age=1u;age<=14u;++age){
        const PortableCameraProjectionResult body={.x=120,.y=100,.distance=distances[d]};
        PortableLavaSmoke smoke={.visible=1u};smoke.puffs[0]=(PortableLavaSmokePuff){-15,-14,(uint8_t)age};
        const PortableLavaSmoke before=smoke;
        portable_software_frame_clear(&frame,0u);
        CHECK(portable_race_scene_render_lava_smoke(&art,&smoke,&body,&frame));
        CHECK(!memcmp(&before,&smoke,sizeof smoke));
        const int x=120-15*256/distances[d],y=100+(-14-(int)age+1)*256/distances[d];
        const uint32_t color=age<=7u?0xff0000u:0x00ff00u;
        unsigned count=0u;
        for(unsigned py=0u;py<160u;++py)for(unsigned px=0u;px<240u;++px){
            const int inside=(int)px>=x&&(int)px<x+8&&(int)py>=y&&(int)py<y+8;
            CHECK(pixels[py*240u+px]==(inside?color:0u));count+=inside;
        }
        CHECK(count==64u); /* Offsets scale; source puff cells remain 8x8. */
        smoke.visible=0u;portable_software_frame_clear(&frame,0u);
        CHECK(portable_race_scene_render_lava_smoke(&art,&smoke,&body,&frame));
        for(size_t pixel=0u;pixel<240u*160u;++pixel)CHECK(!pixels[pixel]);
    }
    /* Original C278 first callback at clock2970: owner(120,137,256),
     * offset(9,-14) gives puff origin(129,123) before its Y decrement. */
    PortableLavaSmoke captured={.visible=1u,.puffs={{9,-14,1u}}};
    const PortableCameraProjectionResult captured_body={.x=120,.y=137,.distance=256};
    portable_software_frame_clear(&frame,0u);
    CHECK(portable_race_scene_render_lava_smoke(&art,&captured,&captured_body,&frame));
    for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x)
        CHECK(pixels[y*240u+x]==(x>=129u&&x<137u&&y>=123u&&y<131u?0xff0000u:0u));
    PortableLavaSmoke empty={0};
    const PortableCameraProjectionResult invalid={.distance=0};
    CHECK(!portable_race_scene_render_lava_smoke(&art,&empty,&invalid,&frame));
    return 0;
}

static int recovery_fx_pixels(void)
{
    uint8_t tiles[96];memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);memset(tiles+64u,0x33,32u);
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    const uint8_t cells[24]={1u,0u,0u,0u,0u,0u,0u,0u,
        1u,0u,0u,0u,0u,0u,1u,0u,1u,0u,0u,0u,0u,0u,2u,0u};
    const PortableRaceParticleFrame thaw_steps[3]={{0u,8u,4u},{8u,8u,3u},{16u,8u,2u}};
    const PortableRaceParticleFrame drip_step={0u,8u,8u};
    const PortableRaceParticleAssetView thaw={.tiles=tiles,.tile_bytes=sizeof tiles,.palette=palette,
        .palette_count=16u,.cells=cells,.cell_bytes=sizeof cells,.frames=thaw_steps,.frame_count=3u};
    PortableRaceParticleAssetView drip=thaw;drip.frames=&drip_step;drip.frame_count=1u;
    const PortableRecoveryFxRules rules={{127u,127u,127u},{0,1,1,1,2,2,3,3,1}};
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    const int16_t distances[3]={128,256,1024};
    for(unsigned d=0u;d<3u;++d){
        const PortableCameraProjectionResult body={.x=120,.y=100,.distance=distances[d]};
        for(unsigned age=1u;age<=12u;++age){
            const PortableRecoveryFx fx={.visible=1u,.thaw_age=(uint8_t)age};
            const PortableRecoveryFx before=fx;portable_software_frame_clear(&frame,0u);
            CHECK(portable_race_scene_render_recovery_fx(&thaw,&drip,&rules,&fx,&body,&frame));
            CHECK(!memcmp(&before,&fx,sizeof fx));
            const uint32_t color=age<=5u?0xff0000u:age<=9u?0x00ff00u:0x0000ffu;
            for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x)
                CHECK(pixels[y*240u+x]==(x>=120u&&x<128u&&y>=84u&&y<92u?color:0u));
        }
        int offset=-4;
        for(unsigned age=1u;age<=9u;++age){
            const int placed_y=100+offset*256/distances[d];offset+=rules.drip_dy[age-1u];
            PortableRecoveryFx fx={.visible=1u,.drips={{-15,(int8_t)offset,(uint8_t)age}}};
            const PortableRecoveryFx before=fx;portable_software_frame_clear(&frame,0u);
            CHECK(portable_race_scene_render_recovery_fx(&thaw,&drip,&rules,&fx,&body,&frame));
            CHECK(!memcmp(&before,&fx,sizeof fx));
            const int placed_x=120-15*256/distances[d];
            for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x)
                CHECK(pixels[y*240u+x]==((int)x>=placed_x&&(int)x<placed_x+8&&
                    (int)y>=placed_y&&(int)y<placed_y+8?0xff0000u:0u));
            fx.visible=0u;portable_software_frame_clear(&frame,0u);
            CHECK(portable_race_scene_render_recovery_fx(&thaw,&drip,&rules,&fx,&body,&frame));
            for(unsigned i=0u;i<240u*160u;++i)CHECK(!pixels[i]);
        }
    }
    return 0;
}

static int surface_trail_pixels(void)
{
    uint8_t tiles[96];memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);memset(tiles+64u,0x33,32u);
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    const uint8_t cells[24]={1u,0u,0u,0u,0u,0u,0u,0u,
        1u,0u,0u,0u,0u,0u,1u,0u,1u,0u,0u,0u,0u,0u,2u,0u};
    const PortableRaceParticleFrame steps[3]={{0u,8u,1u},{8u,8u,1u},{16u,8u,1u}};
    PortableRaceParticleAssetView art[5];
    for(unsigned i=0u;i<5u;++i)art[i]=(PortableRaceParticleAssetView){.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.cells=cells,.cell_bytes=sizeof cells,.frames=steps,.frame_count=3u};
    PortableSurfaceTrailRules rules={0};
    for(unsigned i=0u;i<5u;++i){rules.frame_cutoffs[i][0]=i<3u?9u:5u;rules.frame_cutoffs[i][1]=i<3u?19u:11u;}
    uint32_t *pixels=malloc(750u*320u*sizeof *pixels);CHECK(pixels!=NULL);
    for(unsigned wide=0u;wide<2u;++wide){
        PortableSoftwareFrame frame={.pixels=pixels,.width=wide?373u:240u,.height=160u,
            .scale=wide?2u:1u,.stride=wide?750u:240u};
        const unsigned scale=wide?2u:1u,stride=wide?750u:240u,center=wide?66u:0u;
        const PortableCameraProjectionResult body={.x=(int16_t)(120+center),.y=100,.distance=256};
        for(unsigned kind=1u;kind<=5u;++kind)for(unsigned age=2u;age<=29u;++age){
            PortableSurfaceTrail trails={.visible=1u,.slots={{120*128+127,100*128+127,0u,(uint8_t)age,(uint8_t)kind}}};
            const PortableSurfaceTrail before=trails;
            for(unsigned i=0u;i<stride*160u*scale;++i)pixels[i]=0x123456u;
            CHECK(portable_race_scene_render_surface_trails(art,&rules,&trails,&body,&frame));
            CHECK(!memcmp(&before,&trails,sizeof trails));
            const unsigned row=age<=rules.frame_cutoffs[kind-1u][0]?0u:
                age<=rules.frame_cutoffs[kind-1u][1]?1u:2u;
            const uint32_t color=row==0u?0xff0000u:row==1u?0x00ff00u:0x0000ffu;
            /* Native1x preserves source truncation; enhanced output retains
             * the Q7 fraction through the final physical anchor. */
            const int left=scale==1u?120+(int)center:
                (int)portable_software_frame_floor_div((int64_t)trails.slots[0].x_q7*scale,128)+(int)(center*scale);
            const int top=scale==1u?100:
                (int)portable_software_frame_floor_div((int64_t)trails.slots[0].y_q7*scale,128);
            CHECK(scale==1u || (left>(int)((120u+center)*scale) && top>(int)(100u*scale)));
            for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                const int inside=(age&3u)&&(int)x>=left&&(int)x<left+(int)(8u*scale)&&
                    (int)y>=top&&(int)y<top+(int)(8u*scale);
                CHECK(pixels[y*stride+x]==(inside?color:0x123456u));
            }
        }
        /* Source allocation list survives slot reuse: younger red covers old
         * blue even when the young particle occupies the lower slot index. */
        PortableSurfaceTrail overlap={.visible=1u,.slots={{120*128,100*128,0u,2u,1u},{120*128,100*128,0u,21u,1u}}};
        portable_software_frame_clear(&frame,0u);
        CHECK(portable_race_scene_render_surface_trails(art,&rules,&overlap,&body,&frame));
        CHECK(pixels[100u*scale*stride+(120u+center)*scale]==0xff0000u);
        overlap.visible=0u;portable_software_frame_clear(&frame,0u);
        CHECK(portable_race_scene_render_surface_trails(art,&rules,&overlap,&body,&frame));
        for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<frame.width*scale;++x)CHECK(!pixels[y*stride+x]);
    }
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u};
    PortableSurfaceTrail negative={.visible=1u,.slots={{-127,100*128,0u,2u,1u}}};
    PortableCameraProjectionResult body={.x=120,.y=100,.distance=256};
    portable_software_frame_clear(&frame,0u);
    CHECK(portable_race_scene_render_surface_trails(art,&rules,&negative,&body,&frame));
    CHECK(pixels[100u*240u]==0xff0000u && pixels[100u*240u+7u]==0xff0000u && !pixels[100u*240u+8u]);
    const PortableSurfaceTrail spray={.visible=1u,.extra_spray=1u};
    for(unsigned inverse=128u;inverse<=512u;inverse*=2u){
        body.distance=(int16_t)inverse;portable_software_frame_clear(&frame,0u);
        CHECK(portable_race_scene_render_surface_trails(art,&rules,&spray,&body,&frame));
        uint32_t expected[240u*160u];PortableSoftwareFrame target={.pixels=expected,.width=240u,.height=160u};
        portable_software_frame_clear(&target,0u);
        PortableRaceParticleAssetView one=art[4];one.frame_count=1u;
        CHECK(portable_race_particle_render_scaled(&one,&target,122,107,2u,(uint16_t)inverse));
        CHECK(!memcmp(pixels,expected,sizeof expected));
    }
    free(pixels);return 0;
}

static int coin_effect_pixels(void)
{
    uint8_t tiles[96];memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);memset(tiles+64u,0x33,32u);
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    uint8_t cells[18u*8u]={0};PortableRaceParticleFrame steps[18];
    for(unsigned i=0u;i<18u;++i){
        cells[i*8u]=i==4u?0u:1u;cells[i*8u+2u]=252u; /* authored -4,-4 anchor */
        cells[i*8u+4u]=252u;cells[i*8u+5u]=1u;cells[i*8u+6u]=(uint8_t)(i%3u);
        steps[i]=(PortableRaceParticleFrame){i*8u,i==4u?2u:8u,i==0u?255u:i==1u?2u:i==2u?1u:3u};
    }
    PortableRaceParticleAssetView art[3];
    for(unsigned i=0u;i<3u;++i)art[i]=(PortableRaceParticleAssetView){.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.cells=cells,.cell_bytes=sizeof cells,.frames=steps,.frame_count=18u};
    uint8_t saved_cells[sizeof cells];memcpy(saved_cells,cells,sizeof cells);
    PortableRaceParticleFrame saved_steps[18];memcpy(saved_steps,steps,sizeof steps);
    uint32_t *pixels=malloc(750u*320u*sizeof *pixels);CHECK(pixels!=NULL);
    static const struct {uint16_t age;uint8_t row;} cases[]={
        {1u,0u},{255u,0u},{256u,0u},{257u,1u},{259u,1u},{260u,2u},{261u,2u},
        {262u,3u},{265u,3u},{266u,4u},{269u,4u},{270u,5u},{273u,5u},{318u,17u},{321u,17u}};
    for(unsigned wide=0u;wide<2u;++wide)for(unsigned inverse=128u;inverse<=512u;inverse*=2u)
        for(unsigned kind=PORTABLE_COIN_EFFECT_BOUNCE;kind<=PORTABLE_COIN_EFFECT_RISE;++kind)
            for(unsigned c=0u;c<sizeof cases/sizeof cases[0];++c){
                const unsigned scale=wide?2u:1u,stride=wide?750u:240u,center=wide?66u:0u;
                PortableSoftwareFrame frame={.pixels=pixels,.width=wide?373u:240u,.height=160u,
                    .scale=(uint8_t)scale,.stride=stride};
                PortableCoinEffects effects={0};effects.slots[0]=(PortableCoinEffectSlot){
                    .x=120,.y=100,.y_q16=100*65536+32768,.age=cases[c].age,.inverse=(uint16_t)inverse,.kind=(uint8_t)kind,.visible=1u};
                const PortableCoinEffects before=effects;
                for(unsigned i=0u;i<stride*160u*scale;++i)pixels[i]=0x123456u;
                CHECK(portable_race_scene_render_coin_effects(art,&effects,&frame));
                CHECK(!memcmp(&before,&effects,sizeof effects));
                const unsigned row=cases[c].row;
                const uint32_t color=row%3u==0u?0xff0000u:row%3u==1u?0x00ff00u:0x0000ffu;
                const unsigned left=(120u+center)*scale-4u*256u*scale/inverse;
                /* Only bouncing coins own Q16 Y motion. Rising coins update
                 * their integer Y directly and leave constructor Y_q16 stale. */
                const unsigned physical_y=kind==PORTABLE_COIN_EFFECT_BOUNCE && scale>1u?
                    (unsigned)((int64_t)effects.slots[0].y_q16*scale/65536):100u*scale;
                const unsigned top=physical_y-4u*256u*scale/inverse;
                CHECK(physical_y==100u*scale+(kind==PORTABLE_COIN_EFFECT_BOUNCE && scale>1u?scale/2u:0u));
                const unsigned size=8u*256u/inverse*scale;
                for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                    const int inside=row!=4u && x>=left && x<left+size && y>=top && y<top+size;
                    CHECK(pixels[y*stride+x]==(inside?color:0x123456u));
                }
            }
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u};
    PortableCoinEffects skipped={0};skipped.slots[0]=(PortableCoinEffectSlot){
        .x=120,.y=100,.age=2u,.inverse=256u,.kind=PORTABLE_COIN_EFFECT_BOUNCE,.visible=0u};
    skipped.slots[1]=(PortableCoinEffectSlot){.age=2u,.kind=PORTABLE_COIN_EFFECT_RISE_EMITTER,.visible=1u};
    skipped.slots[2]=(PortableCoinEffectSlot){.kind=PORTABLE_COIN_EFFECT_RISE,.visible=1u};
    portable_software_frame_clear(&frame,0x123456u);
    CHECK(portable_race_scene_render_coin_effects(art,&skipped,&frame));
    for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==0x123456u);
    CHECK(!memcmp(saved_cells,cells,sizeof cells) && !memcmp(saved_steps,steps,sizeof steps));
    free(pixels);return 0;
}

static int impact_effect_pixels(void)
{
    uint8_t tiles[96];memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);memset(tiles+64u,0x33,32u);
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    uint8_t cells[6][5u*14u]={0};PortableRaceParticleFrame steps[6][5];
    PortableRaceParticleAssetView art[6];
    for(unsigned variant=0u;variant<6u;++variant){
        const int dx=(int)variant*9-27,dy=(int)variant*5-15;
        for(unsigned row=0u;row<5u;++row){
            uint8_t *cell=cells[variant]+row*14u;cell[0]=2u;
            for(unsigned entry=0u;entry<2u;++entry){
                const uint16_t x=(uint16_t)(dx+(int)entry*10)&0x1ffu;
                cell[2u+entry*6u]=(uint8_t)(dy+(int)entry*2);
                cell[4u+entry*6u]=(uint8_t)x;cell[5u+entry*6u]=(uint8_t)(x>>8u);
                cell[6u+entry*6u]=(uint8_t)((row+entry)%3u);
            }
            steps[variant][row]=(PortableRaceParticleFrame){row*14u,14u,row<2u?1u:row==2u?3u:2u};
        }
        art[variant]=(PortableRaceParticleAssetView){.tiles=tiles,.tile_bytes=sizeof tiles,
            .palette=palette,.palette_count=16u,.cells=cells[variant],.cell_bytes=sizeof cells[variant],
            .frames=steps[variant],.frame_count=5u};
    }
    uint32_t *pixels=malloc(750u*320u*sizeof *pixels);CHECK(pixels!=NULL);
    for(unsigned modded=0u;modded<2u;++modded){
        if(modded)for(unsigned variant=0u;variant<6u;++variant){
            steps[variant][0].duration=255u;steps[variant][2].size=2u;cells[variant][2u*14u]=0u;
        }
        const uint16_t long_ages[]={0u,1u,255u,256u,257u,258u,259u,262u,263u,265u,266u,268u};
        for(unsigned wide=0u;wide<2u;++wide)for(unsigned variant=0u;variant<6u;++variant)
            for(unsigned c=0u;c<(modded?sizeof long_ages/sizeof long_ages[0]:14u);++c){
                const unsigned age=modded?long_ages[c]:c+1u;
                const unsigned row=modded?(age<=256u?0u:age<=258u?1u:age<=262u?2u:age<=265u?3u:4u):
                    age<=2u?0u:age<=4u?1u:age<=8u?2u:age<=11u?3u:4u;
                const unsigned scale=wide?2u:1u,stride=wide?750u:240u,center=wide?66u:0u;
                PortableSoftwareFrame frame={.pixels=pixels,.width=wide?373u:240u,.height=160u,
                    .scale=(uint8_t)scale,.stride=stride};
                PortableImpactEffects effects={0};effects.slots[0]=(PortableImpactEffectSlot){
                    .x=120,.y=100,.depth=65,.inverse=256u,.age=(uint16_t)age,.variant=(uint8_t)variant,.active=1u};
                const PortableImpactEffects before=effects;
                uint8_t saved_cells[sizeof cells];memcpy(saved_cells,cells,sizeof cells);
                PortableRaceParticleFrame saved_steps[6][5];memcpy(saved_steps,steps,sizeof steps);
                for(unsigned i=0u;i<stride*160u*scale;++i)pixels[i]=0x123456u;
                CHECK(portable_race_scene_render_impact_effects(art,&effects,&frame));
                CHECK(!memcmp(&before,&effects,sizeof effects) && !memcmp(saved_cells,cells,sizeof cells) &&
                    !memcmp(saved_steps,steps,sizeof steps));
                const unsigned left=(unsigned)(120+(int)center+(int)variant*9-27)*scale;
                const unsigned top=(unsigned)(100+(int)variant*5-15)*scale;
                const uint32_t colors[3]={0xff0000u,0x00ff00u,0x0000ffu};
                for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                    uint32_t expected=0x123456u;
                    if(age && !(modded&&row==2u))for(unsigned entry=0u;entry<2u;++entry)
                        if(x>=left+entry*10u*scale && x<left+(entry*10u+8u)*scale &&
                           y>=top+entry*2u*scale && y<top+(entry*2u+8u)*scale)expected=colors[(row+entry)%3u];
                    CHECK(pixels[y*stride+x]==expected);
                }
            }
    }
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u};
    PortableImpactEffects inactive={0};inactive.slots[0]=(PortableImpactEffectSlot){.age=1u,.inverse=256u};
    portable_software_frame_clear(&frame,0x123456u);
    CHECK(portable_race_scene_render_impact_effects(art,&inactive,&frame));
    for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==0x123456u);
    free(pixels);return 0;
}

/* Compare each actor's opaque mask with the combined scene. Reversing
 * world depth must reverse coverage, independently of family draw order. */
/* Item height changes body Y, never the ground depth used for scale. */
static int race_item_height_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles[0x800],box_tiles[PORTABLE_ITEM_BOX_TILE_BYTES],map[1]={0},road_tile[64]={0};
    uint16_t palette[48]={0},box_palette[16]={0},black_palette[64]={0};
    memset(tiles,0x11,sizeof tiles);memset(box_tiles,0x11,sizeof box_tiles);
    palette[1]=31u;palette[17]=31u;palette[33]=31u;box_palette[1]=31u;
    const PortableRaceParticleAssetView art={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=48u,.tile_base=0x100u,.palette_base=3u,
        .cells=shell_test_cells(),.cell_bytes=304u};
    const PortableItemBoxAssetView box={.tiles=box_tiles,.tile_bytes=sizeof box_tiles,
        .palette=box_palette,.palette_count=16u,.frame_count=8u,.width=32u,.height=32u};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    PortableSkyAssetView sky=*scene->sky;sky.palette=black_palette;sky.palette_count=64u;
    PortableRaceSceneView view=*scene;view.sine=sine;view.sky=&sky;view.scenery=NULL;
    view.charged_exit_particle=&art;view.item_box=&box;
    view.world_map=(PortableRoadMapView){.map=map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u};
    view.tileset=(PortableTrackTilesetView){.tiles=road_tile,.tile_bytes=sizeof road_tile,
        .palette=black_palette,.palette_count=64u};
    const PortableRaceSceneView view_before=view;
    for(unsigned scale=1u;scale<=4u;scale*=2u){
        const unsigned width=240u*scale,rows=160u*scale;
        const size_t count=(size_t)width*rows;
        uint32_t *pixels=malloc(count*sizeof *pixels);
        uint8_t *mask=malloc(count);
        CHECK(pixels!=NULL && mask!=NULL);
        PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u,.scale=(uint8_t)scale};
        for(unsigned kind=0u;kind<5u;++kind){
            int baseline_top=0,baseline_left=0,baseline_width=0,baseline_height=0;
            unsigned baseline_count=0u;
            for(unsigned pose=0u;pose<3u;++pose){
                const int32_t height=pose==1u?2*65536:pose==2u?-2*65536:0;
                const int shift=pose==1u?-8*(int)scale:pose==2u?8*(int)scale:0;
                PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                    .camera_position_override=1u,.suppress_player=1u,.suppress_racers=1u,
                    .suppress_native_hud=1u,.suppress_native_minimap=1u};
                const PortableRaceItemPresentation item={.z_q16=height,.visible=1u};
                if(kind==0u)state.green_shells[0]=item;
                else if(kind==1u)state.bananas[0]=item;
                else if(kind<4u){state.red_shells[0]=item;state.red_shells[0].kind=kind==3u?0x8403u:0x8402u;}
                else{state.item_box_count=1u;state.item_boxes[0]=(PortableRaceItemBoxPresentation){.height_q16=height,.active=1u};}
                const PortableRacePresentationState before=state;
                CHECK(portable_race_scene_render(&view,&state,&frame));
                CHECK(!memcmp(&before,&state,sizeof state) && !memcmp(&view_before,&view,sizeof view));
                int left=(int)width,right=-1,top=(int)rows,bottom=-1;unsigned coverage=0u;
                for(unsigned y=0u;y<rows;++y)for(unsigned x=0u;x<width;++x){
                    const int opaque=pixels[(size_t)y*width+x]==0xff0000u;
                    if(!pose)mask[(size_t)y*width+x]=(uint8_t)opaque;
                    else{
                        const int source_y=(int)y-shift;
                        const int expected=source_y>=0 && source_y<(int)rows?
                            mask[(size_t)source_y*width+x]:0;
                        CHECK(opaque==expected);
                    }
                    if(opaque){
                        if((int)x<left)left=(int)x;
                        if((int)x>right)right=(int)x;
                        if((int)y<top)top=(int)y;
                        if((int)y>bottom)bottom=(int)y;
                        ++coverage;
                    }
                }
                CHECK(coverage>0u);
                if(!pose){baseline_left=left;baseline_top=top;baseline_width=right-left+1;
                    baseline_height=bottom-top+1;baseline_count=coverage;}
                else CHECK(left==baseline_left && top==baseline_top+shift &&
                    right-left+1==baseline_width && bottom-top+1==baseline_height && coverage==baseline_count);
            }
        }
        free(mask);free(pixels);
    }
    return 0;
}

static int world_sprite_depth_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles[PORTABLE_CHARACTER_FRAME_BYTES],slots[64]={0};
    memset(tiles,0x33,sizeof tiles);
    uint16_t palettes[3][16]={{0}};
    palettes[0][3]=31u;palettes[1][3]=31u<<5u;palettes[2][3]=31u<<10u;
    PortableCharacterAssetView characters[3];
    for(unsigned i=0u;i<3u;++i)characters[i]=(PortableCharacterAssetView){
        .tiles=tiles,.tile_bytes=sizeof tiles,.palette=palettes[i],.palette_count=16u,
        .width=64u,.height=64u,.direction_tiles=tiles,.direction_tile_bytes=sizeof tiles,
        .direction_frame_count=1u,.view_frame_slots=slots,.view_frame_slot_count=64u};
    uint8_t tree_pixels[64u*64u*4u],box_tiles[PORTABLE_ITEM_BOX_TILE_BYTES];
    for(size_t i=0u;i<sizeof tree_pixels;i+=4u){
        tree_pixels[i]=0xff;tree_pixels[i+1u]=0xff;tree_pixels[i+2u]=0;tree_pixels[i+3u]=0xff;
    }
    memset(box_tiles,0x33,sizeof box_tiles);
    uint16_t box_palette[16]={0};box_palette[3]=31u|(31u<<10u);
    const PortableItemBoxAssetView box={.tiles=box_tiles,.tile_bytes=sizeof box_tiles,
        .palette=box_palette,.palette_count=16u,.frame_count=8u,.width=32u,.height=32u};
    PortableCourseSceneryArt tree_art={0};
    for(unsigned lod=0u;lod<4u;++lod){
        tree_art.lod[lod]=(PortableNativeImage){.pixels=tree_pixels,.width=64u,.height=64u,.anchor_x=-32,.anchor_y=-46};
        tree_art.distance_shift[lod]=(uint8_t)lod;
    }
    PortableCourseSceneryObject tree={.affine_q8=256u};
    const PortableCourseSceneryView scenery={.objects=&tree,.object_count=1u,.native_art=&tree_art,.native_art_count=1u};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    const PortableDriverProjectionRules rules={{{511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    PortableRaceSceneView view=*scene;
    view.sine=sine;view.character=&characters[0];view.opponent_characters[0]=&characters[1];
    view.ghost_character=&characters[2];view.driver_projection=&rules;view.opponent_count=1u;
    view.item_box=&box;view.scenery=NULL;
    const uint32_t colors[5]={0xff00ffffu,0xff00ffu,0x00ff00u,0x0000ffu,0xff0000u};
    uint32_t pixels[240u*160u];uint8_t masks[2][240u*160u];
    PortableSoftwareFrame frame;CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    PortableCameraState camera;PortableCameraProjection projection;
    portable_camera_state_update(&camera,0,0,0u);
    CHECK(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection)==0);
    /* Equal-depth order is source allocation order: scenery, boxes, CPU,
     * ghost, selected kart. The CPU path also represents another local view. */
    for(unsigned a=0u;a<5u;++a)for(unsigned b=a+1u;b<5u;++b)
    for(unsigned order=0u;order<3u;++order){
        const int32_t ys[2]={order==1u?4*65536:0,order==0u?4*65536:0};
        int16_t depths[2];
        for(unsigned side=0u;side<2u;++side){
            PortableCameraProjectionResult projected;
            CHECK(portable_camera_projection_project(&camera,&projection,0,ys[side],0,&projected)==0);
            depths[side]=projected.distance;
        }
        if(order<2u)CHECK(depths[0]!=depths[1]);else CHECK(depths[0]==depths[1]);
        for(unsigned pass=0u;pass<3u;++pass){
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                .camera_position_override=1u,.suppress_player=1u,.suppress_native_hud=1u,
                .suppress_native_minimap=1u,.character_affine_scale=256u,.ghost_affine_scale=256u};
            view.scenery=NULL;
            for(unsigned side=0u;side<2u;++side){
                if(pass<2u && side!=pass)continue;
                const unsigned actor=side?b:a;
                switch(actor){
                case 0:tree.position_y_q16=ys[side];view.scenery=&scenery;break;
                case 1:state.item_box_count=1u;state.item_boxes[0]=(PortableRaceItemBoxPresentation){.position_y_q16=ys[side],.active=1u};break;
                case 2:state.opponent_count=1u;state.opponents[0]=(PortableRaceOpponentPresentation){.position_y_q16=ys[side],.visible=1u,.character_affine_scale=256u};break;
                case 3:state.ghost_visible=1u;state.ghost_position_y_q16=ys[side];break;
                default:state.suppress_player=0u;state.position_y_q16=ys[side];break;
                }
            }
            const PortableRacePresentationState before=state;
            CHECK(portable_race_scene_render(&view,&state,&frame));
            CHECK(!memcmp(&state,&before,sizeof state));
            if(a>=2u){
                uint8_t expected[240u*160u];
                for(size_t i=0;i<240u*160u;++i){
                    expected[i]=0u;
                    for(unsigned actor=2u;actor<5u;++actor)if(pixels[i]==colors[actor])expected[i]=(uint8_t)(actor+1u);
                }
                portable_software_frame_clear(&frame,0x123456u);
                CHECK(portable_race_scene_render_racers(&view,&state,&frame));
                CHECK(!memcmp(&state,&before,sizeof state));
                for(size_t i=0;i<240u*160u;++i)
                    CHECK(pixels[i]==(expected[i]?colors[expected[i]-1u]:0x123456u));
            }

            if(pass<2u){
                unsigned count=0u;
                for(size_t i=0u;i<240u*160u;++i){masks[pass][i]=(uint8_t)(pixels[i]==colors[pass?b:a]);count+=masks[pass][i];}
                CHECK(count>0u);
            }else{
                const unsigned front=depths[0]<depths[1]?a:b;
                unsigned overlap=0u;
                for(size_t i=0u;i<240u*160u;++i)if(masks[0][i]&&masks[1][i]){
                    CHECK(pixels[i]==colors[front]);++overlap;
                }
                CHECK(overlap>0u);
            }
        }
    }
    return 0;
}

/* An oblique inverse matrix must cross source texels inside a logical pixel
 * at enhanced resolutions. Padded stride also catches native-index writes. */
static int perspective_physical_sampling_pixels(void)
{
    uint8_t tiles[PORTABLE_CHARACTER_FRAME_BYTES]={0};
    uint16_t palette[16]={0u,31u,31u<<5u};
    static uint32_t pixels[520u*512u];
    for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x){
        const unsigned index=(x+2u*y)%3u;
        const size_t offset=((y/8u)*8u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        tiles[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    const PortableCharacterAssetView character={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.width=64u,.height=64u,
        .direction_tiles=tiles,.direction_tile_bytes=sizeof tiles,.direction_frame_count=1u};
    const PortableCharacterAssetView before=character;
    const int16_t axes[][2]={{193,211},{385,401},{-769,787}};
    for(unsigned scale=1u;scale<=4u;scale*=2u)for(unsigned n=0;n<3u;++n){
        const size_t stride=128u*scale+3u;
        PortableSoftwareFrame frame={pixels,128u,128u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(size_t i=0;i<stride*128u*scale;++i)pixels[i]=0x123456u;
        CHECK(portable_character_render_slot_oam_double(&character,&frame,0,0,0u,axes[n][0],axes[n][1]));
        unsigned non_block=0u;
        for(unsigned y=0;y<128u*scale;++y)for(unsigned x=0;x<stride;++x){
            uint32_t expected=0x123456u;
            const int sx=32+(int)portable_software_frame_floor_div(((int64_t)x-64*(int)scale)*axes[n][0],256*(int)scale);
            const int sy=32+(int)portable_software_frame_floor_div(((int64_t)y-64*(int)scale)*axes[n][1],256*(int)scale);
            if(x<128u*scale && sx>=0 && sx<64 && sy>=0 && sy<64){
                const unsigned index=(unsigned)(sx+2*sy)%3u;
                if(index)expected=index==1u?0xff0000u:0x00ff00u;
            }
            CHECK(pixels[y*stride+x]==expected);
            if(scale>1u && x<128u*scale && pixels[y*stride+x]!=pixels[(y/scale*scale)*stride+x/scale*scale])++non_block;
        }
        CHECK(scale==1u || non_block>0u);
        CHECK(memcmp(&character,&before,sizeof character)==0);
    }
    /* Shared particle cells use the same physical inverse sampling, including
     * fractional extents and negative authored anchors. */
    const uint8_t cells[8]={1u,0u,0xfdu,0u,0xfbu,1u,0u,0u};
    const PortableRaceParticleFrame frames[1]={{0u,8u,8u}};
    const PortableRaceParticleAssetView particle={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.cells=cells,.cell_bytes=sizeof cells,
        .frames=frames,.frame_count=1u};
    for(unsigned scale=1u;scale<=4u;scale*=2u){
        const size_t stride=128u*scale+3u;
        PortableSoftwareFrame frame={pixels,128u,128u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(size_t i=0;i<stride*128u*scale;++i)pixels[i]=0x123456u;
        CHECK(portable_race_particle_render_scaled(&particle,&frame,64,64,2u,193u));
        const int left=64*(int)scale-5*256*(int)scale/193;
        const int top=64*(int)scale-3*256*(int)scale/193;
        const unsigned extent=(8u*256u*scale+192u)/193u;
        for(unsigned y=0;y<128u*scale;++y)for(unsigned x=0;x<stride;++x){
            uint32_t expected=0x123456u;
            const int dx=(int)x-left,dy=(int)y-top;
            if(x<128u*scale && dx>=0 && dy>=0 && dx<(int)extent && dy<(int)extent){
                const unsigned sx=(unsigned)dx*193u/(256u*scale),sy=(unsigned)dy*193u/(256u*scale);
                const unsigned index=(sx+2u*sy)%3u;
                if(index)expected=index==1u?0xff0000u:0x00ff00u;
            }
            CHECK(pixels[y*stride+x]==expected);
        }
    }
    /* A mod can request inverse1. Clip the expanded canvas before walking it:
     * this tiny surface must never scan a 65,536 by65,536 character extent. */
    PortableSoftwareFrame tiny={pixels,8u,8u,NULL,4u,35u,0u,NULL};
    for(unsigned i=0;i<35u*32u;++i)pixels[i]=0x123456u;
    CHECK(portable_character_render_affine(&character,&tiny,0,0,0,0,1u));
    CHECK(portable_race_particle_render_scaled(&particle,&tiny,0,0,2u,1u));
    for(unsigned y=0;y<32u;++y)for(unsigned x=32u;x<35u;++x)
        CHECK(pixels[y*35u+x]==0x123456u);
    return 0;
}

static int character_physical_lod_detail_pixels(void)
{
    uint8_t tiles64[2048]={0},tiles32[512]={0},tiles16[128]={0},slots[64]={0};
    uint8_t *textures[3]={tiles16,tiles32,tiles64};
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    for(unsigned t=0;t<3u;++t){
        const unsigned size=16u<<t;
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            const unsigned index=(x+2u*y)%4u?1u+(x+y+size/16u)%3u:0u;
            const size_t offset=((y/8u)*(size/8u)+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
            textures[t][offset]|=(uint8_t)(index<<((x&1u)*4u));
        }
    }
    static uint32_t pixels[260u*256u];
    for(unsigned available=0;available<2u;++available){
        const PortableCharacterAssetView character={.tiles=tiles64,.tile_bytes=sizeof tiles64,
            .palette=palette,.palette_count=16u,.width=64u,.height=64u,
            .direction_tiles=tiles64,.direction_tile_bytes=sizeof tiles64,.direction_frame_count=1u,
            .lod32_tiles=available?tiles32:NULL,.lod16_tiles=available?tiles16:NULL,
            .view_frame_slots=slots,.view_frame_slot_count=64u};
        const PortableCharacterAssetView before=character;
        for(unsigned obj=16u;obj<=32u;obj*=2u)for(unsigned scale=1u;scale<=4u;scale*=2u)
        for(unsigned doubled=0;doubled<2u;++doubled)for(unsigned negative=0;negative<2u;++negative){
            const int coefficient=negative?-385:256,inverse=negative?385:256;
            const size_t stride=64u*scale+3u;
            PortableSoftwareFrame frame={pixels,64u,64u,NULL,(uint8_t)scale,stride,0u,NULL};
            for(size_t i=0;i<stride*64u*scale;++i)pixels[i]=0x123456u;
            if(doubled)CHECK(portable_character_render_view_oam_lod_double(&character,&frame,0,0,0u,(uint8_t)obj,(int16_t)coefficient,(int16_t)inverse));
            else CHECK(portable_character_render_slot_oam_lod(&character,&frame,0,0,0u,(uint8_t)obj,(int16_t)coefficient,(int16_t)inverse,(int16_t)(obj/2u),1));
            unsigned texture=obj;
            if(scale>1u){
                const unsigned footprint=(obj*256u*scale+(unsigned)inverse-1u)/(unsigned)inverse;
                while(texture<64u && footprint>texture)texture*=2u;
            }
            if(!available)texture=64u;
            const int center=(int)(doubled?obj:obj/2u)*(int)scale;
            const int ratio=(int)(texture/obj),canvas=(int)(doubled?obj*2u:obj)*(int)scale;
            unsigned detailed=0u;
            for(unsigned y=0;y<64u*scale;++y)for(unsigned x=0;x<stride;++x){
                uint32_t expected=0x123456u;
                const int nx=(int)obj/2+(int)portable_software_frame_floor_div(((int64_t)x-center)*coefficient,256*(int)scale);
                const int ny=(int)obj/2+(int)portable_software_frame_floor_div(((int64_t)y-center)*inverse,256*(int)scale);
                const int sx=scale==1u?nx*ratio:(int)texture/2+(int)portable_software_frame_floor_div(((int64_t)x-center)*coefficient*ratio,256*(int)scale);
                const int sy=scale==1u?ny*ratio:(int)texture/2+(int)portable_software_frame_floor_div(((int64_t)y-center)*inverse*ratio,256*(int)scale);
                if(x<64u*scale && (int)x<canvas && (int)y<canvas && nx>=0 && nx<(int)obj && ny>=0 && ny<(int)obj && sx>=0 && sx<(int)texture && sy>=0 && sy<(int)texture){
                    const unsigned index=(unsigned)(sx+2*sy)%4u?1u+(unsigned)(sx+sy+(int)texture/16)%3u:0u;
                    if(index)expected=index==1u?0xff0000u:index==2u?0x00ff00u:0x0000ffu;
                    if(scale>1u && texture>obj && (sx%ratio || sy%ratio))++detailed;
                }
                CHECK(pixels[y*stride+x]==expected);
            }
            CHECK(scale==1u || texture==obj || detailed>0u);
            CHECK(!memcmp(&character,&before,sizeof character));
        }
    }
    return 0;
}

static int rotated_perspective_physical_pixels(const PortableRaceSceneView *scene)
{
    uint8_t image_pixels[8u*16u*4u]={0};
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<8u;++x){
        const unsigned offset=(y*8u+x)*4u;
        image_pixels[offset]=0xcfu;image_pixels[offset+1u]=0xa7u;image_pixels[offset+2u]=0x13u;
        image_pixels[offset+3u]=(x+y)%3u?255u:0u;
    }
    PortableCourseSceneryArt art[5]={0};
    for(unsigned i=0;i<5u;++i)for(unsigned lod=0;lod<4u;++lod){
        art[i].lod[lod]=(PortableNativeImage){.pixels=image_pixels,.width=8u,.height=16u,.anchor_x=-4,.anchor_y=-16};
        art[i].distance_shift[lod]=(uint8_t)lod;
    }
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;sine[128]=23170;sine[384]=23170;
    PortableRaceSceneView view=*scene;view.sine=sine;view.scenery=NULL;view.falling_star_art=art;
    PortableCameraState camera;PortableCameraProjection projection;PortableCameraProjectionResult projected;
    portable_camera_state_update(&camera,0,0,0u);
    CHECK(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection)==0);
    CHECK(portable_camera_projection_project(&camera,&projection,20*65536,0,0,&projected)==0);
    const int inverse=projected.distance*224/256;
    for(unsigned scale=1u;scale<=4u;scale*=2u)for(unsigned rotated=0;rotated<2u;++rotated){
        const size_t stride=240u*scale+3u,count=stride*160u*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels);CHECK(pixels!=NULL);
        for(size_t i=0;i<count;++i)pixels[i]=0x2468ace0u;
        PortableSoftwareFrame frame={pixels,240u,160u,NULL,(uint8_t)scale,stride,0u,NULL};
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
            .suppress_player=1u,.suppress_racers=1u,.suppress_native_hud=1u,.suppress_native_minimap=1u};
        state.falling_stars.count=1u;
        state.falling_stars.actors[0]=(PortableFallingStar){.x_q16=20*65536,.flags=1u,.affine_q8=256u,
            .angle=(uint16_t)(rotated?8192u:0u)};
        const PortableRacePresentationState before=state;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(!memcmp(&before,&state,sizeof state));
        const int sin_value=rotated?23170:0,cos_value=rotated?23170:32768;
        unsigned colored=0u,non_block=0u;
        for(unsigned y=0;y<160u*scale;++y)for(unsigned x=0;x<stride;++x){
            const int64_t dx=(int)x-projected.x*(int)scale,dy=(int)y-projected.y*(int)scale;
            const int sx=(int)portable_software_frame_floor_div((cos_value*dx+sin_value*dy)*inverse,INT64_C(8388608)*scale)+4;
            const int sy=(int)portable_software_frame_floor_div((-sin_value*dx+cos_value*dy)*inverse,INT64_C(8388608)*scale)+16;
            const int expected=x<240u*scale && sx>=0 && sx<8 && sy>=0 && sy<16 && (sx+sy)%3!=0;
            CHECK((pixels[y*stride+x]==0xff13a7cfu)==expected);
            if(x>=240u*scale)CHECK(pixels[y*stride+x]==0x2468ace0u);
            colored+=(unsigned)expected;
            if(scale>1u && x<240u*scale && expected!=(pixels[(y/scale*scale)*stride+x/scale*scale]==0xff13a7cfu))++non_block;
        }
        CHECK(colored>0u && (scale==1u || non_block>0u));free(pixels);
    }
    return 0;
}

static int driver_projection_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles64[PORTABLE_CHARACTER_FRAME_BYTES],tiles32[PORTABLE_CHARACTER_LOD32_FRAME_BYTES];
    uint8_t tiles16[PORTABLE_CHARACTER_LOD16_FRAME_BYTES],slots[64]={0};
    memset(tiles64,0x33,sizeof tiles64);memset(tiles32,0x44,sizeof tiles32);
    memset(tiles16,0x55,sizeof tiles16);
    uint16_t palette[16]={0};palette[3]=0x001fu;palette[4]=0x03e0u;palette[5]=0x7c00u;
    const PortableCharacterAssetView character={.tiles=tiles64,.tile_bytes=sizeof tiles64,
        .palette=palette,.palette_count=16u,.width=64u,.height=64u,
        .direction_tiles=tiles64,.direction_tile_bytes=sizeof tiles64,.direction_frame_count=1u,
        .lod32_tiles=tiles32,.lod16_tiles=tiles16,.view_frame_slots=slots,.view_frame_slot_count=64u};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    const PortableDriverProjectionRules rules={.rows={
        {511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    PortableRaceSceneView view=*scene;view.character=&character;view.ghost_character=&character;
    view.driver_projection=&rules;
    view.opponent_characters[0]=&character;view.scenery=NULL;view.sine=sine;
    PortableCameraState camera;PortableCameraProjection projection;
    portable_camera_state_update(&camera,0,0,0u);
    CHECK(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection)==0);
    static const int16_t distances[5]={256,508,512,1020,1024};
    static const int32_t heights[4]={0,16384,8*65536,-8*65536};
    uint32_t pixels[240u*160u];uint8_t reference[240u*160u];
    PortableSoftwareFrame frame;CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    /* Short custom frame sets retain their authored view when finish/contact
     * art is absent, while an available special frame still replaces it. */
    uint32_t pose_reference[240u*160u];
    uint8_t special_tiles[2u*PORTABLE_CHARACTER_FRAME_BYTES];
    memcpy(special_tiles,tiles64,sizeof tiles64);
    memset(special_tiles+sizeof tiles64,0x44,sizeof tiles64);
    for(unsigned projected=0u;projected<2u;++projected){
        const PortableCameraProjectionResult body={.x=120,.y=100,.distance=256};
        PortableCharacterAssetView custom=character;
        custom.direction_tiles=special_tiles;
        custom.direction_tile_bytes=sizeof special_tiles;
        const PortableCharacterViewPose normal={.angle=100u,.mirror=1u};
        PortableCharacterViewPose special=normal;special.frame_override=31u;
        memset(pixels,0,sizeof pixels);
        CHECK(projected?portable_character_render_driver_pose_projected(&custom,&frame,&rules,
            &body,&normal,256u,256u,256u,0):
            portable_character_render_driver_pose(&custom,&frame,120,100,&normal,256u,256u));
        memcpy(pose_reference,pixels,sizeof pixels);
        size_t drawn=0u;for(size_t i=0u;i<240u*160u;++i)drawn+=pixels[i]!=0u;
        CHECK(drawn>0u);
        memset(pixels,0,sizeof pixels);
        CHECK(projected?portable_character_render_driver_pose_projected(&custom,&frame,&rules,
            &body,&special,256u,256u,256u,0):
            portable_character_render_driver_pose(&custom,&frame,120,100,&special,256u,256u));
        CHECK(!memcmp(pose_reference,pixels,sizeof pixels));
        custom.direction_frame_count=2u;special.frame_override=1u;
        memset(pixels,0,sizeof pixels);
        CHECK(projected?portable_character_render_driver_pose_projected(&custom,&frame,&rules,
            &body,&special,256u,256u,256u,0):
            portable_character_render_driver_pose(&custom,&frame,120,100,&special,256u,256u));
        CHECK(memcmp(pose_reference,pixels,sizeof pixels)!=0);
    }
    int prior_width=241;
    for(unsigned depth=0u;depth<5u;++depth){
        PortableCameraProjectionResult ground={0};int32_t position_y=0;int found=0;
        /* Source depth is published in multiples of four; sample whole quarter
         * world units to hit both sides of its 511/1023 art thresholds. */
        for(int32_t quarter=-8192;quarter<=8192&&!found;++quarter){
            PortableCameraProjectionResult candidate;
            if(portable_camera_projection_project(&camera,&projection,0,quarter*16384,0,&candidate)==0&&
               candidate.distance==distances[depth]){
                ground=candidate;position_y=quarter*16384;found=1;
            }
        }
        CHECK(found);
        const uint32_t color=depth<2u?0xff0000u:depth<4u?0x00ff00u:0x0000ffu;
        int ground_top=0;
        for(unsigned pose=0u;pose<4u;++pose){
            PortableCameraProjectionResult body;
            CHECK(portable_camera_projection_body(&ground,heights[pose],&body)==0);
            for(unsigned kind=0u;kind<3u;++kind){
                PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                    .position_y_q16=position_y,.height_q16=heights[pose],.camera_position_override=1u,
                    .character_affine_scale=256u,.suppress_player=(uint8_t)(kind!=0u),
                    .suppress_native_minimap=1u,.opponent_count=kind==1u?1u:0u,
                    .ghost_visible=(uint8_t)(kind==2u),.ghost_position_y_q16=position_y,
                    .ghost_height_q16=heights[pose],.ghost_affine_scale=256u};
                state.opponents[0]=(PortableRaceOpponentPresentation){.position_y_q16=position_y,
                    .height_q16=heights[pose],.visible=1u,.character_affine_scale=256u};
                view.opponent_count=kind==1u?1u:0u;
                int16_t body_x,body_y;uint16_t body_scale;
                CHECK(portable_race_scene_player_projection(&view,&state,&body_x,&body_y,&body_scale));
                CHECK(body_x==body.x&&body_y==body.y&&
                      body_scale==(uint16_t)(distances[depth]/(depth<2u?1u:depth<4u?2u:4u)));
                const PortableRacePresentationState before=state;
                const PortableCharacterAssetView art_before=character;
                CHECK(portable_race_scene_render(&view,&state,&frame));
                CHECK(memcmp(&before,&state,sizeof state)==0&&memcmp(&art_before,&character,sizeof character)==0);
                int left=240,right=-1,top=160;
                for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x){
                    const size_t index=y*240u+x;
                    const int colored=pixels[index]==color;
                    if(pixels[index]==0xff0000u||pixels[index]==0x00ff00u||pixels[index]==0x0000ffu)
                        CHECK(colored); /* A wrong LOD cannot hide behind size alone. */
                    if(kind==0u)reference[index]=(uint8_t)colored;
                    else CHECK(reference[index]==colored);
                    if(colored){if((int)x<left)left=(int)x;if((int)x>right)right=(int)x;if((int)y<top)top=(int)y;}
                }
                CHECK(right>=left&&top<160);
                if(pose==0u&&kind==0u){
                    ground_top=top;CHECK(right-left+1<=prior_width);prior_width=right-left+1;
                }
                CHECK(top-ground_top==body.y-ground.y);
            }
        }
    }
    /* A source finish camera holds while the kart drives behind it. This is
     * ordinary visibility culling; visible rivals and the live board remain. */
    PortableRacePresentationState clipped={.position_y_q16=2000*65536,
        .camera_position_override=1u,.character_affine_scale=256u,
        .driver_affine_x=256u,.driver_affine_y=256u,.suppress_native_hud=1u,
        .opponent_count=1u};
    clipped.opponents[0]=(PortableRaceOpponentPresentation){.visible=1u,
        .character_affine_scale=256u,.driver_affine_x=256u,.driver_affine_y=256u};
    view.opponent_count=1u;
    PortableCameraProjectionResult hidden;
    const int clipped_result=portable_camera_projection_project(&camera,&projection,0,2000*65536,0,&hidden);
    CHECK(clipped_result==PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA ||
        clipped_result==PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE);
    const PortableRacePresentationState immutable=clipped;
    CHECK(portable_race_scene_render(&view,&clipped,&frame));
    unsigned rival_pixels=0u;
    for(unsigned i=0u;i<240u*160u;++i)rival_pixels+=pixels[i]==0xff0000u;
    CHECK(rival_pixels>0u && memcmp(&immutable,&clipped,sizeof clipped)==0);
    CHECK(tiles64[0]==0x33u&&tiles32[0]==0x44u&&tiles16[0]==0x55u);
    return 0;
}

/* Wheels have their own latched palette, independent of body hit colors.
 * Exercise all three scene consumers and apply bob at the source cell pivot. */
static int driver_art_motion_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles64[PORTABLE_CHARACTER_FRAME_BYTES],tiles32[PORTABLE_CHARACTER_LOD32_FRAME_BYTES];
    uint8_t tiles16[PORTABLE_CHARACTER_LOD16_FRAME_BYTES],slots[64]={0};
    uint8_t *const tiles[3]={tiles64,tiles32,tiles16};
    const unsigned sizes[3]={64u,32u,16u};
    for(unsigned lod=0u;lod<3u;++lod)
        for(unsigned y=0u;y<sizes[lod];++y)for(unsigned x=0u;x<sizes[lod];x+=2u){
            const uint8_t color=x<sizes[lod]/4u?1u:x>=sizes[lod]*3u/4u?2u:3u;
            const size_t offset=((size_t)(y/8u)*(sizes[lod]/8u)+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
            tiles[lod][offset]=(uint8_t)(color|(color<<4u));
        }
    uint16_t banks[256]={0};banks[1]=0x001fu;banks[2]=0x03e0u;banks[3]=0x7c00u;
    banks[7u*16u+3u]=0x7fffu;
    banks[14u*16u+2u]=0x03ffu;banks[14u*16u+3u]=0x7c1fu;
    banks[15u*16u+1u]=0x7fe0u;banks[15u*16u+2u]=0x4210u;
    uint16_t original_banks[256];memcpy(original_banks,banks,sizeof banks);
    PortableCharacterAssetView character={.tiles=tiles64,.tile_bytes=sizeof tiles64,
        .palette=banks,.palette_count=16u,.palette_banks=banks,.palette_bank_count=16u,
        .width=64u,.height=64u,.direction_tiles=tiles64,.direction_tile_bytes=sizeof tiles64,
        .direction_frame_count=1u,.lod32_tiles=tiles32,.lod16_tiles=tiles16,
        .view_frame_slots=slots,.view_frame_slot_count=64u};
    const PortableDriverProjectionRules rules={.rows={
        {511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    PortableRaceSceneView view=*scene;view.character=&character;view.ghost_character=&character;
    view.opponent_characters[0]=&character;view.driver_projection=&rules;view.scenery=NULL;view.sine=sine;
    PortableCameraState camera;PortableCameraProjection projection;
    portable_camera_state_update(&camera,0,0,0u);
    CHECK(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection)==0);
    const int16_t distances[3]={256,768,1536};
    uint32_t pixels[240u*160u];uint8_t reference[240u*160u];
    PortableSoftwareFrame frame;CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    for(unsigned depth=0u;depth<3u;++depth){
        int32_t position_y=0;int found=0;
        for(int32_t quarter=-8192;quarter<=8192&&!found;++quarter){
            PortableCameraProjectionResult candidate;
            if(portable_camera_projection_project(&camera,&projection,0,quarter*16384,0,&candidate)==0&&
               candidate.distance==distances[depth]){position_y=quarter*16384;found=1;}
        }
        CHECK(found);
        for(unsigned body=0u;body<2u;++body)for(unsigned wheels=0u;wheels<4u;++wheels){
            if(body&&wheels==3u)continue; /* Missing banks can only resolve base body colors. */
            character.palette_bank_count=wheels==3u?1u:16u;
            int baseline_top=0;
            for(unsigned bob=0u;bob<2u;++bob)for(unsigned kind=0u;kind<(body?2u:3u);++kind){
                const PortableDriverArtMotion motion={.wheel_phase=wheels==1u?1u:2u,
                    .wheel_palette_bank=wheels==1u?15u:14u,.wheel_palette_valid=(uint8_t)(wheels!=2u),
                    .initialized=1u,.cached_bob_offset=bob?3:0};
                PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                    .position_y_q16=position_y,.camera_position_override=1u,.character_affine_scale=256u,
                    .suppress_player=(uint8_t)(kind!=0u),.suppress_native_minimap=1u,
                    .palette_bank=body?7u:0u,.art_motion=motion,.opponent_count=kind==1u?1u:0u,
                    .ghost_visible=(uint8_t)(kind==2u),.ghost_position_y_q16=position_y,
                    .ghost_affine_scale=256u,.ghost_art_motion=motion};
                state.opponents[0]=(PortableRaceOpponentPresentation){.position_y_q16=position_y,
                    .visible=1u,.character_affine_scale=256u,.palette_bank=body?7u:0u,.art_motion=motion};
                view.opponent_count=kind==1u?1u:0u;
                const PortableRacePresentationState before=state;
                const PortableCharacterAssetView descriptor=character;
                CHECK(portable_race_scene_render(&view,&state,&frame));
                CHECK(!memcmp(&before,&state,sizeof state)&&!memcmp(&descriptor,&character,sizeof character));
                CHECK(!memcmp(banks,original_banks,sizeof banks));
                const uint32_t wheel1=wheels>=2u?0xff0000u:wheels==1u?0x00ffffu:0xffff00u;
                const uint32_t wheel2=wheels>=2u?0x00ff00u:wheels==1u?0x848484u:0xff00ffu;
                const uint32_t body_color=body?0xffffffu:0x0000ffu;
                unsigned counts[3]={0u};int top=160;
                for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x){
                    const size_t index=y*240u+x;
                    const int part=pixels[index]==wheel1?1:pixels[index]==wheel2?2:pixels[index]==body_color?3:0;
                    if(kind==0u)reference[index]=(uint8_t)part;
                    else CHECK(reference[index]==part);
                    if(part){++counts[part-1];if((int)y<top)top=(int)y;}
                }
                CHECK(counts[0]>0u&&counts[1]>0u&&counts[2]>0u);
                if(!bob&&kind==0u)baseline_top=top;
                const PortableDriverProjectionRow *row=&rules.rows[depth];
                const int inverse=(256/(64/(int)row->size))*distances[depth]/256;
                const int pivot=(int)row->offset_y+(int)row->size/2;
                const int shift=(pivot+(bob?3:0))*256/inverse-pivot*256/inverse;
                CHECK(top-baseline_top==shift);
            }
        }
    }
    return 0;
}

static void window_event(PortableTimeTrialHost *host, SDL_EventType kind)
{
    const SDL_Event event = {.window = {.type = kind}};
    portable_time_trial_host_event(host, &event);
}

/* A strike hides distant art, not the middle sky or foreground. Exercise
 * the production compositor with distinct opaque and transparent pixels. */
static int lightning_backdrop_pixels(const PortableRaceSceneView *scene)
{
    uint8_t tiles[128]={0},back[2048],middle[2048]={0},front[2048]={0};
    uint16_t palette[16]={0,0x7c00u,0x03e0u,0x001fu};
    uint32_t pixels[240u*160u];
    for(unsigned i=32u;i<128u;++i)tiles[i]=(uint8_t)((i/32u)*17u);
    for(unsigned i=0u;i<sizeof back;i+=2u){back[i]=1u;back[i+1u]=0u;}
    front[2u*(11u*32u+10u)]=2u;
    middle[2u*(11u*32u+11u)]=3u;
    PortableSkyAssetView sky={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.front_map=front,.mid_map=middle,
        .back_map=back,.front_map_bytes=sizeof front,.mid_map_bytes=sizeof middle,
        .back_map_bytes=sizeof back,.front_width_tiles=32u,.mid_width_tiles=32u,
        .back_width_tiles=32u,.map_height_tiles=32u};
    PortableRaceSceneView view=*scene;view.sky=&sky;
    PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    for(unsigned phase=0u;phase<3u;++phase){
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,
            .lap_count=3u,.source_sky_parallax=1u,
            .lightning_scene_active=(uint8_t)(phase!=0u),
            .lightning_scene_white=(uint8_t)(phase==2u)};
        const PortableRacePresentationState before=state;
        CHECK(portable_race_scene_render(&view,&state,&frame));
        CHECK(memcmp(&state,&before,sizeof state)==0);
        CHECK(pixels[20u*240u+80u]==(phase==0u?0x0000ffu:phase==1u?0u:0xffffffu));
        CHECK(pixels[20u*240u+88u]==0xff0000u);
        CHECK(pixels[20u*240u+120u]==0x00ff00u);
    }
    return 0;
}

static void key_event(PortableTimeTrialHost *host, SDL_Keycode key)
{
    const SDL_Event event = {.key = {.type = SDL_EVENT_KEY_DOWN, .key = key,
        .scancode = SDL_GetScancodeFromKey(key,NULL)}};
    portable_time_trial_host_event(host, &event);
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    FILE *frontend_file = fopen(argv[2], "rb");
    CHECK(frontend_file != NULL && fseek(frontend_file,0,SEEK_END)==0);
    const long frontend_length = ftell(frontend_file);
    CHECK(frontend_length>0 && fseek(frontend_file,0,SEEK_SET)==0);
    uint8_t *frontend_bytes = malloc((size_t)frontend_length);
    CHECK(frontend_bytes != NULL);
    CHECK(fread(frontend_bytes,1,(size_t)frontend_length,frontend_file)==(size_t)frontend_length);
    CHECK(fclose(frontend_file)==0);
    PortableAssetPackView frontend_pack;
    CHECK(portable_asset_pack_open(frontend_bytes,(size_t)frontend_length,&frontend_pack)==PORTABLE_ASSET_PACK_OK);
    FILE *file = fopen(argv[1], "rb");
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    PortableRecoveredPackStorage *storage = malloc(sizeof *storage);
    PortablePresentationPackStorage *art = malloc(sizeof *art);
    CHECK(bytes != NULL && storage != NULL && art != NULL);
    CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    PortableAssetPackView pack;
    PortableContentSelection selection;
    PortableRecoveredRaceView race;
    PortablePresentationPackView presentation;
    CHECK(portable_asset_pack_open(bytes, (size_t)length, &pack) == PORTABLE_ASSET_PACK_OK);
    CHECK(portable_content_registry_select(&pack, 0u, 0u, &selection) == PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(&pack, &selection, storage, &race) ==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(portable_presentation_pack_open(&pack, &selection, storage, &race,
          art, &presentation) == PORTABLE_RECOVERED_PACK_OK);
    uint16_t item_palettes[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    authored_item_palettes(item_palettes);
    presentation.hud.item_palette_selectors=item_palettes;
    presentation.hud.item_palette_count=PORTABLE_HUD_ITEM_PALETTE_COUNT;
    /* The v2 pack remains a structural-loader fixture; running its host
     * requires explicit authored simulation owners instead of stock defaults. */
    test_item_dependencies(&race.dependencies);
    race.dependencies.response_rules=portable_test_response_profile();
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                   SDL_INIT_GAMEPAD));
    SDL_Window *window = SDL_CreateWindow("Time Trial lifecycle", 240, 160,
                                          SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    SDL_Renderer *renderer = SDL_CreateRenderer(window,"software");
    CHECK(renderer != NULL);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, 240, 160);
    CHECK(texture != NULL);
    SDL_Gamepad *controller = NULL;
    PortableRecoveredTimeTrialHostScene scene = {
        .frontend_pack = &frontend_pack,
        .race = &race, .host_window = window, .host_renderer = renderer,
        .host_texture = texture, .host_controller_slot = &controller,
        .presentation = {
            .world_map = presentation.world_map, .tileset = presentation.tileset,
            .sky = &presentation.sky, .character = &presentation.character,
            .charged_exit_particle = &presentation.charged_exit_particle,
            .scenery = &presentation.scenery,
            .lava_bubble_art = presentation.lava_bubble_art,
            .thwomp_art = presentation.thwomp_art,
            .crab_art = presentation.crab_art,
            .seagull_art = presentation.seagull_art,
            .piranha_art = presentation.piranha_art,
            .snowman_art = presentation.snowman_art,
            .mouse_art = presentation.mouse_art,
            .thundercloud_art = presentation.thundercloud_art,
            .falling_star_art = presentation.falling_star_art,
            .penguin_art = presentation.penguin_art,
            .boo_art = presentation.boo_art,
            .cannonball_art = presentation.cannonball_art,
            .minimap = &presentation.minimap, .hud = &presentation.hud,
            .sine = storage->sine, .sine_count = 1024u,
        },
    };
    CHECK(finished_split_hud_pixels(&scene)==0);
    CHECK(finish_board_hud_pixels(&scene.presentation)==0);
    CHECK(modern_drift_hud_pixels(&scene.presentation)==0);
    CHECK(modern_air_hud_pixels(&scene.presentation)==0);
    CHECK(lap_board_hud_capture_pixels(&scene.presentation)==0);
    CHECK(snow_weather_pixels(&scene.presentation)==0);
    CHECK(snow_weather_fractional_pixels(&scene.presentation)==0);
    CHECK(hop_camera_alignment_pixels(&scene.presentation)==0);
    CHECK(latched_hop_ground_pixels(&scene.presentation)==0);
    CHECK(scenery_precision_motion_pixels(&scene.presentation)==0);
    CHECK(actor_height_pixels(&scene.presentation)==0);
    CHECK(race_item_height_pixels(&scene.presentation)==0);
    CHECK(kart_palette_pixels(&scene.presentation)==0);
    {
        PortableDriverPilotState driver={0};
        driver.source_terrain22_active=1u;
        const uint16_t buttons=PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_LEFT|PORTABLE_INPUT_ITEM;
        for(uint8_t mode=0u;mode<=9u;++mode){
            driver.hop.mode=mode;
            const PortableDriverPilotState before=driver;
            CHECK(portable_recovered_water_rescue_aid(buttons,0,&driver)==buttons);
            CHECK(portable_recovered_water_rescue_aid(buttons,1,&driver)==
                (mode==3u||mode==4u||mode==6u||mode==7u?PORTABLE_INPUT_BRAKE:buttons));
            CHECK(!memcmp(&before,&driver,sizeof driver));
        }
        driver.source_terrain22_active=0u;driver.hop.mode=3u;
        CHECK(portable_recovered_water_rescue_aid(buttons,1,&driver)==buttons);
        CHECK(portable_recovered_water_rescue_aid(buttons,1,NULL)==buttons);
    }
    CHECK(mobile_trial_lifecycle(&scene,argv[1])==0);
    CHECK(lava_smoke_pixels()==0);
    CHECK(recovery_fx_pixels()==0);
    CHECK(surface_trail_pixels()==0);
    CHECK(coin_effect_pixels()==0);
    CHECK(impact_effect_pixels()==0);
    CHECK(perspective_physical_sampling_pixels()==0);
    CHECK(character_physical_lod_detail_pixels()==0);
    CHECK(rotated_perspective_physical_pixels(&scene.presentation)==0);
    CHECK(driver_projection_pixels(&scene.presentation)==0);
    CHECK(world_sprite_depth_pixels(&scene.presentation)==0);
    CHECK(driver_art_motion_pixels(&scene.presentation)==0);
    CHECK(lightning_backdrop_pixels(&scene.presentation)==0);
    /* A rejected begin must neither consume the controller slot nor shut down
     * the application's SDL subsystems or destroy its borrowed surface. */
    scene.record_path = "";
    CHECK(portable_time_trial_host_begin(&scene) == NULL);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0u);
    CHECK(SDL_RenderClear(renderer));
    scene.record_path = NULL;
    /* A menu-advertised ghost cannot silently disappear at race startup.
     * This scene deliberately has no ghost driver resolver. */
    scene.require_selected_ghost=1u;scene.ghost_received_slot=1u;
    CHECK(portable_time_trial_host_begin(&scene)==NULL);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO)!=0u&&SDL_RenderClear(renderer));
    scene.require_selected_ghost=0u;scene.ghost_received_slot=0u;
    int32_t render_sine[1024];
    for(unsigned angle=0u;angle<1024u;++angle)
        render_sine[angle]=(int32_t)(32768.0*sin(6.28318530717958647692*angle/1024.0));
    PortableRecoveredRaceView render_race=race;
    render_race.dependencies.sine_table=render_sine;render_race.dependencies.sine_count=1024u;
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        scene.internal_scale=(uint8_t)(cycle+1u);
        scene.race_renderer=(uint8_t)(cycle==0u||cycle==3u);
        scene.race=scene.race_renderer?&render_race:&race;
        scene.presentation.sine=scene.race_renderer?render_sine:storage->sine;
        scene.aspect_width=scene.race_renderer?0u:cycle%2u?284u:373u;
        scene.fit_scaling=(uint8_t)(cycle&1u);
        scene.interpolate=(uint8_t)(cycle!=0u);
        scene.render_fps=cycle==1u?120u:144u;
        PortableTimeTrialHost *host = portable_time_trial_host_begin(&scene);
        CHECK(host != NULL);
        window_event(host, SDL_EVENT_WINDOW_EXPOSED);
        CHECK(portable_time_trial_host_frame(host) == 0);
        if(cycle==0u){
            CHECK(SDL_SetWindowSize(window,373,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_time_trial_host_frame(host)==0);
            SDL_Surface *wide=SDL_RenderReadPixels(renderer,NULL);
            CHECK(wide!=NULL&&wide->w==373&&wide->h==160);SDL_DestroySurface(wide);
            CHECK(SDL_SetWindowSize(window,240,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_time_trial_host_frame(host)==0);
        }
        uint32_t before_focus_loss[240u*160u];
        if(scene.race_renderer)CHECK(read_test_frame(renderer,before_focus_loss));
        window_event(host, SDL_EVENT_WINDOW_FOCUS_LOST);
        CHECK(portable_time_trial_host_frame(host) == 0);
        if(scene.race_renderer){
            uint32_t after_focus_loss[240u*160u];
            CHECK(read_test_frame(renderer,after_focus_loss));
            CHECK(memcmp(before_focus_loss,after_focus_loss,sizeof before_focus_loss)!=0);
        }
        /* Focus loss pauses through the same native overlay. Once settled,
           further presentation frames must leave the complete race unchanged. */
        for(unsigned tick=0;tick<20;++tick) {
            SDL_Delay(18);
            CHECK(portable_time_trial_host_frame(host)==0);
        }
        uint32_t paused_pixels[240u*160u], repeated_pixels[240u*160u];
        CHECK(read_test_frame(renderer,paused_pixels));
        if(scene.race_renderer){
            CHECK(SDL_SetWindowSize(window,373,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_time_trial_host_frame(host)==0);
            SDL_Surface *wide=SDL_RenderReadPixels(renderer,NULL);
            CHECK(wide!=NULL&&wide->w==373&&wide->h==160);SDL_DestroySurface(wide);
            CHECK(SDL_SetWindowSize(window,240,160));
            window_event(host,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            CHECK(portable_time_trial_host_frame(host)==0);
            CHECK(read_test_frame(renderer,repeated_pixels));
            CHECK(memcmp(paused_pixels,repeated_pixels,sizeof paused_pixels)==0);
        }
        SDL_Delay(100);
        CHECK(portable_time_trial_host_frame(host)==0);
        CHECK(read_test_frame(renderer,repeated_pixels));
        CHECK(memcmp(paused_pixels,repeated_pixels,sizeof paused_pixels)==0);
        window_event(host, SDL_EVENT_WINDOW_FOCUS_GAINED);
        CHECK(portable_time_trial_host_frame(host) == 0);
        key_event(host, SDLK_R);
        window_event(host, SDL_EVENT_WINDOW_EXPOSED);
        CHECK(portable_time_trial_host_frame(host) == 0);
        if (cycle < 7) key_event(host, SDLK_ESCAPE);
        else {
            const SDL_Event quit = {.type = SDL_EVENT_QUIT};
            portable_time_trial_host_event(host, &quit);
        }
        CHECK(portable_time_trial_host_frame(host) == 1);
        PortableRecoveredTimeTrialHostOutcome outcome;
        CHECK(portable_time_trial_host_end(host, &outcome) == 0);
        CHECK(outcome.return_to_selection && !outcome.finished);
        CHECK(outcome.quit_requested == (cycle == 7));
        if (cycle < 7) CHECK(outcome.return_destination == PORTABLE_TIME_TRIAL_RETURN_COURSE);
        CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0u);
        CHECK(SDL_RenderClear(renderer));
    }
    puts("Time Trial 3D host resize: PASS 1x/4x paused world and HUD restored after auto-aspect resize");
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(art);
    free(storage);
    free(bytes);
    free(frontend_bytes);
    puts("Time Trial scene lifecycle: PASS");
    return 0;
}
