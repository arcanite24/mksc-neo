#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "game_host_internal.h"
#include "frontend_image_renderer.h"
#include "modern_menu_host.h"
#include "content_registry.h"

#ifdef PORTABLE_ENABLE_SDL3
struct PortableGpResultsHost
{
    ModernMenuHost modern;
    uint8_t modern_menus,mobile_back_requested;
    uint32_t modern_ticks;
    uint32_t selected_driver_id,course_id;
    PortableGpFailureMenu menu;
    PortableGpResultsFlow quick_menu;
    PortableQuickResultsOpening quick_opening;
    RouteAudioHost *audio;
    uint8_t quick, battle,replay_available,quick_board_ticks,frame_written;
    PortableGpStandingsSession standings;
    const PortableAssetPackView *pack;
    const PortableGpStandingsAssetView *standings_asset;
    const PortableGpResultsAssetView *window;
    uint32_t menu_ticks;
    uint32_t race_pixels[240u*160u];
    PortableSoftwareFrame frozen;
    uint32_t *frozen_pixels;
    PortableSoftwareRenderTarget target;
    int fit;
    uint16_t aspect_width;
    uint8_t video_scale,video_configured;
    const PortableGpStandingsAssetView *board_asset;
    PortableQuickRunResults quick_results;
    PortableBattleResults battle_results;
    uint8_t board_present,engine_class;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Gamepad *controller;
    PortableFrameScheduler scheduler;
    uint32_t pixels[240u*160u];
    PortableSoftwareFrame frame;
    PortableUiTextBank ui_text;
    uint16_t pressed,held,previous_held,hold_ticks;
    uint8_t next_mode,quit,credit_art,failure_reported;
    int focused,error,needs_present;
    int (*race_background)(void *,PortableSoftwareFrame *);
    void *race_background_context;
};

void portable_gp_results_host_modern(PortableGpResultsHost *host,
    const PortableAssetPackView *pack,uint8_t enabled,uint32_t selected_driver_id,uint32_t course_id)
{
    if(host!=NULL){host->pack=pack;host->modern_menus=(uint8_t)(enabled!=0u);
        host->selected_driver_id=selected_driver_id;host->course_id=course_id;}
}

int portable_gp_results_failure_exiting(const PortableGpResultsHost *host)
{
    return host && (host->menu.phase==PORTABLE_GP_FAILURE_MENU_RETRY_SHUTTER ||
        host->menu.phase==PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER ||
        host->menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE);
}
int portable_gp_results_failure_advances_portraits(const PortableGpResultsHost *host)
{
    /* Cases28/29 register and advance; later dialogs only draw their cached rows. */
    return host && (host->menu.phase==PORTABLE_GP_FAILURE_MENU_OPEN ||
        host->menu.phase==PORTABLE_GP_FAILURE_MENU_CHOOSE);
}
int portable_gp_results_failure_hud_visible(const PortableGpResultsHost *host)
{
    return host && (!portable_gp_results_failure_exiting(host) || !host->menu.phase_ticks);
}

void portable_gp_results_source_clock(PortableGpResultsHost *host,uint32_t clock)
{
    if(host!=NULL&&!host->battle)host->menu_ticks=clock;
}
void portable_gp_results_failure_palette(PortableGpResultsHost *host,
    const PortableGpResultsAssetView *asset)
{ if(host!=NULL&&!host->quick&&host->standings_asset==NULL)host->window=asset; }

void portable_gp_results_set_race_background(PortableGpResultsHost *host,
    int (*background)(void *,PortableSoftwareFrame *),void *context)
{
    if(host!=NULL){host->race_background=background;host->race_background_context=context;}
}

/* Results own the frozen surface: callers may immediately reuse their race buffer.
 * Equal-sized inputs copy every physical sample, including fractional edges. */
static void copy_background(PortableSoftwareFrame *target,const PortableSoftwareFrame *source)
{
    const size_t width=target->width*portable_software_frame_scale(target);
    const size_t height=target->height*portable_software_frame_scale(target);
    const size_t source_width=source->width*portable_software_frame_scale(source);
    const size_t source_height=source->height*portable_software_frame_scale(source);
    for(size_t y=0u;y<height;++y)
        for(size_t x=0u;x<width;++x)
            portable_software_frame_store_physical(target,x,y,
                source->pixels[(y*source_height/height)*portable_software_frame_stride(source)+
                    x*source_width/width]);
}

int portable_gp_results_host_video(PortableGpResultsHost *host,uint8_t scale,
    uint16_t aspect_width,int fit,const PortableSoftwareFrame *race_frame)
{
    if(host==NULL||scale>8u)return 0;
    const PortableSoftwareFrame *source=race_frame!=NULL?race_frame:&host->frozen;
    if(source->pixels==NULL||source->width==0u||source->height==0u||source->artwork_lod>2u||
       portable_software_frame_stride(source)<source->width*portable_software_frame_scale(source))return 0;
    const size_t width=portable_software_frame_window_width(host->renderer,aspect_width);
    if(width<240u||width>2048u)return 0;
    SDL_ScaleMode filter=SDL_SCALEMODE_NEAREST;
    if(!SDL_GetTextureScaleMode(host->texture,&filter))return 0;
    PortableSoftwareFrame frozen={.width=width,.height=160u,.scale=scale?scale:1u,
        .ui_text=&host->ui_text,.artwork_lod=source->artwork_lod};
    frozen.pixels=malloc(portable_software_frame_pixel_count(&frozen)*sizeof *frozen.pixels);
    if(frozen.pixels==NULL)return 0;
    copy_background(&frozen,source);
    host->target.frame.ui_text=&host->ui_text;
    if(!portable_software_render_target_resize(host->renderer,&host->target,width,160u,scale,
        filter==SDL_SCALEMODE_LINEAR)){free(frozen.pixels);return 0;}
    host->target.frame.artwork_lod=source->artwork_lod;
    free(host->frozen_pixels);
    host->frozen_pixels=frozen.pixels;host->frozen=frozen;
    host->frame=host->target.frame;host->fit=fit!=0;host->needs_present=1;
    host->aspect_width=aspect_width;host->video_scale=scale;host->video_configured=1u;
    return 1;
}

static uint32_t results_driver_id(const PortableGpResultsHost *host,uint8_t racer,uint8_t driver_class)
{
    if(racer==0u&&host->selected_driver_id!=0u)return host->selected_driver_id;
    PortableCharacterContent driver;
    return portable_content_registry_find_character_class(host->pack,driver_class,&driver)==PORTABLE_CONTENT_OK?driver.id:0u;
}

static int present_modern_results(PortableGpResultsHost *host)
{
    ModernMenuResults view={.pack=host->pack,.course_id=host->course_id,.tick=host->modern_ticks,
        .gamepad=host->modern.gamepad};
    const char *capture;
    if(host->quick){
        view.title=host->battle?"Battle results":"Quick Run results";
        view.row_count=host->battle?host->battle_results.count:8u;
        for(unsigned rank=0u;rank<view.row_count;++rank){
            ModernMenuResultRow *row=&view.rows[rank];
            const uint8_t racer=host->battle?host->battle_results.rows[rank].racer_index:host->quick_results.rows[rank].racer_index;
            const uint8_t driver_class=host->battle?host->battle_results.rows[rank].driver_class:host->quick_results.rows[rank].driver_class;
            row->rank=host->battle?(unsigned)host->battle_results.rows[rank].rank+1u:rank+1u;
            row->driver_id=results_driver_id(host,racer,driver_class);row->highlighted=(uint8_t)(racer==0u);
            if(host->battle)snprintf(row->value,sizeof row->value,"%u balloons",host->battle_results.rows[rank].balloons);
            else{
                modern_menu_host_format_time(row->value,sizeof row->value,host->quick_results.rows[rank].centiseconds);
                if(host->quick_results.rows[rank].estimated)snprintf(row->detail,sizeof row->detail,"Estimated finish");
            }
        }
        const int actions=host->battle?host->quick_board_ticks>=61u:host->quick_opening.state==10u;
        if(actions){
            view.item_count=5u;view.cursor=host->quick_menu.menu_selection;
            for(unsigned i=0u;i<5u;++i){
                const uint8_t code=host->window->menu_option_codes[i];
                view.labels[i]=modern_menu_host_result_action_label(code);
                if(code==3u&&!host->replay_available)view.disabled_mask|=(uint8_t)(1u<<i);
            }
        }else{view.item_count=1u;view.labels[0]="Continue";view.notice="Race complete";}
        capture=host->battle?(actions?"battle-actions-modern":"battle-results-modern"):
            (actions?"quick-actions-modern":"quick-results-modern");
    }else if(host->standings_asset!=NULL){
        const PortableGpStandingsSession *standings=&host->standings;
        view.title=standings->sorted_order?"Cup standings":"Race points";
        view.row_count=8u;
        for(unsigned rank=0u;rank<8u;++rank){
            const uint8_t racer=(standings->sorted_order?standings->cup.standings_order:standings->cup.race_order)[rank];
            const PortableGpStandingRow *source=&standings->cup.rows[racer];
            ModernMenuResultRow *row=&view.rows[rank];
            row->rank=rank+1u;row->highlighted=(uint8_t)(racer==0u);
            row->driver_id=results_driver_id(host,racer,source->driver_class);
            snprintf(row->value,sizeof row->value,"%u points",standings->displayed_points[racer]);
            if(standings->remaining_award[racer])snprintf(row->detail,sizeof row->detail,"+%u race points",standings->remaining_award[racer]);
        }
        view.item_count=1u;view.labels[0]="Continue";
        if(standings->phase!=PORTABLE_GP_STANDINGS_HOLD&&standings->phase!=PORTABLE_GP_STANDINGS_SORTED_HOLD)
            view.notice="Updating standings";
        capture=standings->sorted_order?"gp-cumulative-modern":"gp-standings-modern";
    }else{
        const PortableGpFailureMenu *menu=&host->menu;
        const int confirm=menu->phase==PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT||
            menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_OPEN||menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_YES_START;
        view.title=menu->phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER?"Game over":confirm?"Quit Grand Prix?":"Race complete";
        view.item_count=2u;view.labels[0]=confirm?"No, keep racing":"Try again";
        view.labels[1]=confirm?"Yes, quit":"Quit";
        view.cursor=confirm?menu->confirm_yes:menu->choice;
        if(menu->phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER){view.item_count=1u;view.labels[0]="Continue";view.cursor=0u;}
        capture=menu->phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER?"gp-game-over-modern":"gp-retry-modern";
    }
    return modern_menu_host_present_results(&host->modern,host->renderer,&host->frozen,host->fit,&view,capture);
}

static int render_quick_board(PortableGpResultsHost *host,PortableSoftwareFrame *board)
{
    if(host->board_present){
        if(host->battle){
            if(!portable_battle_board_render(host->board_asset,host->window,&host->battle_results,
                host->engine_class,board))return 0;
        }else{
            PortableTimeBoardRow rows[PORTABLE_GP_RACER_COUNT];
            for(unsigned rank=0u;rank<PORTABLE_GP_RACER_COUNT;++rank)
                rows[rank]=(PortableTimeBoardRow){host->quick_results.rows[rank].centiseconds,
                    host->quick_results.rows[rank].racer_index,host->quick_results.rows[rank].driver_class,
                    (uint8_t)rank,PORTABLE_TIME_BOARD_TIME,(uint8_t)(host->quick_results.rows[rank].racer_index==0u)};
            const uint8_t hidden_rows[8]={32u,32u,32u,32u,32u,32u,32u,32u};
            PortableGpStandingsAssetView art=*host->board_asset;
            uint16_t palette[256];
            if(host->quick_opening.state==10u){
                memcpy(palette,art.palette,sizeof palette);
                /* Older packs have no pulse profile; keep their static art. */
                if(portable_gp_results_menu_colors(host->window,host->menu_ticks,palette+14u*16u))
                    art.palette=palette;
            }
            if(!portable_time_board_render_slide(&art,host->window,rows,
                PORTABLE_GP_RACER_COUNT,host->quick_results.engine_class,
                host->quick_opening.state==7u?hidden_rows:host->quick_opening.row_x,board))return 0;
        }
    }
    return 1;
}

static int render_quick(PortableGpResultsHost *host)
{
    if(host->race_background==NULL)copy_background(&host->frame,&host->frozen);
    PortableSoftwareFrame board=portable_software_frame_native_view(&host->frame,PORTABLE_FRAME_CENTER);
    if(!render_quick_board(host,&board))return 0;
    if(!host->battle&&host->quick_opening.state!=10u){
        if(host->quick_opening.state==9u||host->quick_opening.state==8u){
            if(host->window->followup_map==NULL)return 1;
            return portable_gp_results_render_action_window_open(host->window,
                host->quick_opening.window_progress,(uint8_t)!host->replay_available,&board)&&
                (host->quick_opening.window_progress<3u||
                 portable_screen_capture(&host->frame,"quick-window"));
        }
        return portable_screen_capture(&host->frame,"quick-board");
    }
    if(host->quick_board_ticks<61u)
        return portable_screen_capture(&host->frame,host->battle?"battle-board":"quick-board");
    /* Battle reuses the window with REPLAY greyed, as Quick Run without a recording. */
    if(host->window->followup_map==NULL)return 1;
    return portable_gp_results_render_action_window(host->window,host->quick_menu.menu_selection,
            (uint8_t)!host->replay_available,host->menu_ticks,&board)&&
        portable_screen_capture(&host->frame,host->battle?"battle-menu":"quick-menu");
}

/* Source28/35/37 copy four authored quadrants into a growing window. */
static int draw_failure_window(PortableGpResultsHost *host,PortableSoftwareFrame *board,
    uint32_t image,unsigned half_x,unsigned half_y,unsigned inset)
{
    if(!half_x||!half_y)return 1;
    if(half_x>6u)half_x=6u;
    if(half_y>3u)half_y=3u;
    uint16_t palette[16];uint8_t mask[16];memset(mask,1,sizeof mask);
    const int pulse=host->window!=NULL&&portable_gp_results_menu_colors(host->window,host->menu_ticks,palette);
    for(unsigned y=0u;y<2u;++y)for(unsigned x=0u;x<2u;++x){
        const int sx=x?96-(int)inset-(int)half_x*8:(int)inset,sy=y?48-(int)half_y*8:0;
        const int dx=x?120:120-(int)half_x*8,dy=y?136:136-(int)half_y*8;
        if(portable_frontend_image_draw_palette(host->pack,image,board,dx-sx,dy-sy,
            dx,dy,dx+(int)half_x*8,dy+(int)half_y*8,pulse?palette:NULL,
            pulse?16u:0u,pulse?mask:NULL)!=PORTABLE_FRONTEND_PRESENTATION_OK)return 0;
    }
    return 1;
}

static int render_retry(PortableGpResultsHost *host)
{
    const PortableGpFailureMenu *menu=&host->menu;
    if(host->race_background==NULL)copy_background(&host->frame,&host->frozen);
    PortableSoftwareFrame board=portable_software_frame_native_view(&host->frame,PORTABLE_FRAME_CENTER);
    const int shutter=menu->phase==PORTABLE_GP_FAILURE_MENU_RETRY_SHUTTER||
        menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER||
        menu->phase==PORTABLE_GP_FAILURE_MENU_COMPLETE;
    if(shutter){
        const unsigned progress=menu->phase==PORTABLE_GP_FAILURE_MENU_COMPLETE?
            20u:menu->phase_ticks>2u?menu->phase_ticks-2u:0u;
        const size_t scale=portable_software_frame_scale(&host->frame);
        const size_t width=host->frame.width*scale,height=host->frame.height*scale;
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x)
            if(progress>=20u||x<progress*width/40u||x>=width-progress*width/40u||
                y<progress*height/40u||y>=height-progress*height/40u)
                portable_software_frame_store_physical(&host->frame,x,y,UINT32_C(0xff000000));
        return menu->phase_ticks!=13u || portable_screen_capture(&host->frame,"gp-retry-exit-13");
    }
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER_SETUP)return 1;
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER)
        return portable_frontend_image_draw(host->pack,UINT32_C(0x25004),
            &board,0,0,0,0,240,160)==PORTABLE_FRONTEND_PRESENTATION_OK &&
            portable_screen_capture(&host->frame,"gp-game-over");
    const int confirm=menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_OPEN||
        menu->phase==PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT||
        menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_YES_START;
    const uint32_t image=UINT32_C(0x25000)+(confirm?
        2u+(menu->confirm_yes?0u:1u):menu->choice);
    unsigned half_x=6u,half_y=3u;
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_OPEN){
        half_x=menu->phase_ticks;half_y=half_x;
    }else if(menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_OPEN){
        half_x=menu->phase_ticks?menu->phase_ticks-1u:0u;half_y=half_x;
    }else if(menu->phase==PORTABLE_GP_FAILURE_MENU_QUIT_NO_RETURN){
        half_x=menu->phase_ticks;half_y=half_x;
    }
    if(!draw_failure_window(host,&board,image,half_x,half_y,menu->phase==PORTABLE_GP_FAILURE_MENU_OPEN?8u:0u))return 0;
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_OPEN &&
       (menu->phase_ticks==1u||menu->phase_ticks==3u||menu->phase_ticks==5u)){
        char name[24];(void)snprintf(name,sizeof name,"gp-retry-open-%u",menu->phase_ticks);
        if(!portable_screen_capture(&host->frame,name))return 0;
    }
    if(host->credit_art && menu->phase!=PORTABLE_GP_FAILURE_MENU_OPEN){
        unsigned count=menu->retry_credits;
        if(menu->phase==PORTABLE_GP_FAILURE_MENU_RETRY_HOLD){
            const unsigned prior=menu->phase_ticks?menu->phase_ticks-1u:0u;
            if(prior<40u && !(prior&4u))++count;
        }
        for(unsigned i=0u;i<count;++i)
            if(portable_frontend_image_draw(host->pack,UINT32_C(0x25005),&board,
                216-(int)i*16,144,0,0,240,160)!=PORTABLE_FRONTEND_PRESENTATION_OK)return 0;
    }
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_CHOOSE){
        if(!host->failure_reported){
            printf("GP retry window: credits=%u live-background=%u\n",menu->retry_credits,host->race_background!=NULL);
            host->failure_reported=1u;
        }
        return portable_screen_capture(&host->frame,"gp-retry");
    }
    return 1;
}

static PortableGpResultsHost *begin_results(const PortableUiTextBank *ui_text,SDL_Renderer *renderer,SDL_Texture *texture)
{
    if(renderer==NULL||texture==NULL)return NULL;
    PortableGpResultsHost *host=calloc(1,sizeof *host);
    if(host==NULL)return NULL;
    host->renderer=renderer;host->texture=texture;
    host->focused=1;host->needs_present=1;
    host->controller=portable_sdl_gamepad_open_first();
    if(ui_text!=NULL)host->ui_text=*ui_text;
    host->frame=(PortableSoftwareFrame){host->pixels,240u,160u,&host->ui_text,1u,0u,0u,NULL};
    host->frozen=(PortableSoftwareFrame){host->race_pixels,240u,160u,&host->ui_text,1u,0u,0u,NULL};
    portable_frame_scheduler_init(&host->scheduler);
    return host;
}

PortableGpResultsHost *portable_quick_results_begin(const PortableUiTextBank *ui_text,const uint32_t *race_pixels,
    uint8_t replay_available,RouteAudioHost *audio,
    SDL_Renderer *renderer,SDL_Texture *texture,const PortableGpResultsAssetView *window,
    const PortableGpStandingsAssetView *asset,const PortableQuickRunResults *results)
{
    if(race_pixels==NULL || window==NULL || window->menu_option_codes==NULL || ((asset==NULL)!=(results==NULL)))return NULL;
    PortableGpResultsHost *host=begin_results(ui_text,renderer,texture);
    if(host==NULL)return NULL;
    host->quick=1u;host->window=window;host->replay_available=(uint8_t)(replay_available!=0u);host->audio=audio;
    memcpy(host->race_pixels,race_pixels,sizeof host->race_pixels);
    host->quick_board_ticks=results==NULL?61u:0u;
    portable_quick_results_opening_init(&host->quick_opening);
    if(results==NULL){host->quick_opening.state=10u;host->quick_opening.window_progress=6u;}
    if(results!=NULL&&window->time_board_map!=NULL){
        host->board_present=1u;host->board_asset=asset;host->quick_results=*results;
        if(!render_quick_board(host,&host->frame)){
            portable_gp_results_host_end(host,NULL,NULL);return NULL;
        }
    }
    portable_gp_results_flow_init(&host->quick_menu);
    host->quick_menu.state=11u;
    if(!route_audio_begin_ceremony(audio,17u)){portable_gp_results_host_end(host,NULL,NULL);return NULL;}
    return host;
}

PortableGpResultsHost *portable_battle_results_begin(const PortableUiTextBank *ui_text,const uint32_t *race_pixels,
    RouteAudioHost *audio,SDL_Renderer *renderer,SDL_Texture *texture,const PortableGpResultsAssetView *window,
    const PortableGpStandingsAssetView *asset,const PortableBattleResults *results,uint8_t engine_class)
{
    if(results==NULL||results->count<2u||results->count>4u)return NULL;
    PortableGpResultsHost *host=portable_quick_results_begin(ui_text,race_pixels,0u,audio,renderer,texture,window,NULL,NULL);
    if(host==NULL)return NULL;
    host->battle=1u;host->quick_board_ticks=0u;
    host->board_present=1u;host->board_asset=asset;
    host->battle_results=*results;host->engine_class=engine_class;
    if(!render_quick_board(host,&host->frame)){portable_gp_results_host_end(host,NULL,NULL);return NULL;}
    return host;
}

void portable_gp_results_skip_transfer(PortableGpResultsHost *host)
{
    if(host!=NULL&&host->standings_asset!=NULL)
        host->standings.phase=PORTABLE_GP_STANDINGS_CONSOLIDATE;
}

void portable_gp_results_allow_skip(PortableGpResultsHost *host)
{
    if(host!=NULL)host->standings.short_transitions=1u;
}

/* Supply ordinary events to the same handler used by keyboard/controller. */
void portable_quick_results_aid(PortableGpResultsHost *host,uint8_t replay)
{
    if(host==NULL||!host->quick||host->quick_menu.state!=11u)return;
    if(host->modern_menus&&getenv("MKSC_AID_RESULTS_HOLD")!=NULL&&
        host->modern_ticks-host->modern.results_capture_tick<40u)return;
    /* Confirm the rows once, then let the ordinary opening owner finish
     * before navigating. This also captures its authored window growth. */
    if(!host->battle&&host->quick_opening.state!=5u&&host->quick_opening.state!=10u)return;
    const SDL_Event event={.key={.type=SDL_EVENT_KEY_DOWN,
        .key=(host->battle?host->quick_board_ticks<61u:host->quick_opening.state!=10u) || host->quick_menu.menu_selection==(replay?1u:3u)?SDLK_RETURN:SDLK_DOWN}};
    portable_gp_results_host_event(host,&event);
    SDL_Event release=event;release.type=SDL_EVENT_KEY_UP;
    portable_gp_results_host_event(host,&release);
}

PortableGpResultsHost *portable_gp_retry_begin(const PortableAssetPackView *pack,
    const uint32_t *race_pixels,uint8_t retry_credits,SDL_Renderer *renderer,SDL_Texture *texture,
    RouteAudioHost *audio)
{
    if(pack==NULL||race_pixels==NULL)return NULL;
    PortableUiTextBank ui_text;
    if(!portable_ui_text_load(pack,&ui_text))return NULL;
    PortableGpResultsHost *host=begin_results(&ui_text,renderer,texture);
    if(host==NULL)return NULL;
    host->pack=pack;host->audio=audio;
    PortableAssetSectionView flag;
    host->credit_art=(uint8_t)(portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,UINT32_C(0x25005),&flag)==PORTABLE_ASSET_PACK_OK);
    memcpy(host->race_pixels,race_pixels,sizeof host->race_pixels);
    if(!portable_gp_failure_menu_init(&host->menu,retry_credits)){portable_gp_results_host_end(host,NULL,NULL);return NULL;}
    if(!render_retry(host)){portable_gp_results_host_end(host,NULL,NULL);return NULL;}
    return host;
}

PortableGpResultsHost *portable_gp_standings_begin(const PortableUiTextBank *ui_text,
    const PortableGpStandingsAssetView *asset,const PortableGpCupSession *cup,
    SDL_Renderer *renderer,SDL_Texture *texture,RouteAudioHost *audio)
{
    if(asset==NULL||asset->tiles==NULL||cup==NULL)return NULL;
    PortableGpResultsHost *host=begin_results(ui_text,renderer,texture);
    if(host==NULL)return NULL;
    if(portable_gp_standings_session_init(&host->standings,cup)!=PORTABLE_GP_STANDINGS_OK){
        portable_gp_results_host_end(host,NULL,NULL);return NULL;
    }
    host->standings_asset=asset;host->next_mode=cup->next_mode;host->audio=audio;
    /* The race result owner starts the rank theme before the timeboard. */
    return host;
}

void portable_gp_results_host_event(PortableGpResultsHost *host,const SDL_Event *event)
{
    if(host==NULL||event==NULL)return;
    modern_menu_host_event(&host->modern,event);
    const uint16_t touch=portable_mobile_touch_event(event,host->renderer);
    if ((portable_mobile_back(event)||(touch&PORTABLE_FRONTEND_BUTTON_B)) && host->focused) {
        host->mobile_back_requested=1u;
        return;
    }
    const SDL_JoystickID id=portable_sdl_gamepad_id(host->controller);
    if(id!=0u&&(event->type==SDL_EVENT_GAMEPAD_REMOVED||event->type==SDL_EVENT_GAMEPAD_REMAPPED)&&
       event->gdevice.which==id){host->pressed=0u;host->held=0u;host->previous_held=0u;host->hold_ticks=0u;}
    portable_sdl_gamepad_device_event(&host->controller,event);
    if((event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||event->type==SDL_EVENT_GAMEPAD_BUTTON_UP)&&host->focused&&
       event->gbutton.which==portable_sdl_gamepad_id(host->controller)){
        const uint16_t button=portable_sdl_gamepad_frontend_button(event->gbutton.button);
        if(event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){host->pressed|=button;host->held|=button;}
        else host->held=(uint16_t)(host->held&~button);
    }
    if(host->quick&&(event->type==SDL_EVENT_KEY_DOWN||event->type==SDL_EVENT_KEY_UP)&&host->focused){
        uint16_t button=0u;
        if(event->key.key==SDLK_UP)button=PORTABLE_FRONTEND_BUTTON_UP;
        if(event->key.key==SDLK_DOWN)button=PORTABLE_FRONTEND_BUTTON_DOWN;
        if(event->key.type==SDL_EVENT_KEY_DOWN)host->held|=button;
        else host->held=(uint16_t)(host->held&~button);
    }
    if(event->type==SDL_EVENT_QUIT)host->quit=1;
    else if((event->type>=SDL_EVENT_WINDOW_FIRST&&event->type<=SDL_EVENT_WINDOW_LAST)){
        if(event->type==SDL_EVENT_WINDOW_FOCUS_LOST){
            portable_mobile_touch_clear();
            host->focused=0;host->pressed=0;host->held=0;host->previous_held=0;host->hold_ticks=0;
        }else if(event->type==SDL_EVENT_WINDOW_FOCUS_GAINED)host->focused=1;
        if(event->type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED&&host->video_configured&&
           host->aspect_width==0u&&host->frame.width!=
               portable_software_frame_window_width(host->renderer,0u)){
            if(!portable_gp_results_host_video(host,host->video_scale,0u,host->fit,NULL))
                host->error=1;
        }
        if(event->type==SDL_EVENT_WINDOW_EXPOSED||
           event->type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)host->needs_present=1;
    }else if(event->type==SDL_EVENT_KEY_DOWN&&!event->key.repeat&&host->focused){
        switch(event->key.key){
        case SDLK_ESCAPE:host->quit=1;break;
        case SDLK_UP:host->pressed|=PORTABLE_FRONTEND_BUTTON_UP;break;
        case SDLK_DOWN:host->pressed|=PORTABLE_FRONTEND_BUTTON_DOWN;break;
        case SDLK_LEFT:host->pressed|=PORTABLE_FRONTEND_BUTTON_LEFT;break;
        case SDLK_RIGHT:host->pressed|=PORTABLE_FRONTEND_BUTTON_RIGHT;break;
        case SDLK_RETURN:case SDLK_Z:host->pressed|=PORTABLE_FRONTEND_BUTTON_A;break;
        case SDLK_SPACE:host->pressed|=PORTABLE_FRONTEND_BUTTON_START;break;
        default:break;
        }
    }
}

int portable_gp_results_host_frame(PortableGpResultsHost *host,uint64_t now_ns)
{
    if(host==NULL||host->error)return -1;
    if(host->quit)return 1;
    const unsigned lifecycle=portable_mobile_lifecycle_take();
    if(lifecycle&PORTABLE_MOBILE_BACKGROUND){
        portable_mobile_touch_clear();SDL_ResetKeyboard();
        host->focused=0;host->pressed=host->held=host->previous_held=0u;host->hold_ticks=0u;
        host->mobile_back_requested=0u;
        portable_frame_scheduler_reset(&host->scheduler,now_ns,0);
        if(host->audio!=NULL&&!route_audio_set_paused(host->audio,1)){host->error=1;return -1;}
    }
    if(lifecycle&PORTABLE_MOBILE_FOREGROUND){
        host->focused=1;host->needs_present=1;
        portable_frame_scheduler_reset(&host->scheduler,now_ns,1);
        if(host->audio!=NULL&&!route_audio_set_paused(host->audio,0)){host->error=1;return -1;}
    }
    if(lifecycle&PORTABLE_MOBILE_TERMINATING){portable_mobile_touch_clear();host->quit=1;return 1;}
    host->pressed|=portable_mobile_touch_take_pressed();
    int mobile_confirm=0;
    if(host->focused&&host->mobile_back_requested){
        if(host->quick){
            if((host->battle?host->quick_board_ticks>=61u:host->quick_opening.state==10u)&&
               host->quick_menu.state==11u&&host->window!=NULL){
                unsigned selection=5u;
                for(unsigned i=0u;i<5u;++i)if(host->window->menu_option_codes[i]==1u){selection=i;break;}
                if(selection==5u)for(unsigned i=0u;i<5u;++i)if(host->window->menu_option_codes[i]==4u){selection=i;break;}
                if(selection<5u){host->quick_menu.menu_selection=(uint8_t)selection;mobile_confirm=1;}
            }
        }else if(host->standings_asset!=NULL)mobile_confirm=1;
        else if(host->menu.phase==PORTABLE_GP_FAILURE_MENU_CHOOSE){host->menu.choice=1u;mobile_confirm=1;}
        else if(host->menu.phase==PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT){host->menu.confirm_yes=0u;mobile_confirm=1;}
        else if(host->menu.phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER&&host->menu.phase_ticks>=60u)mobile_confirm=1;
        if(mobile_confirm)host->pressed|=PORTABLE_FRONTEND_BUTTON_A;
    }
    uint16_t samples[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t count=0;int dropped=0;
    if(portable_frame_scheduler_update(&host->scheduler,now_ns,host->focused,
        host->pressed,samples,PORTABLE_FRAME_MAX_CATCH_UP,&count,&dropped)!=
        PORTABLE_FRAME_SCHEDULER_OK){host->error=1;return -1;}
    (void)dropped;
    if(count!=0u&&mobile_confirm)host->mobile_back_requested=0u;
    for(size_t tick=0;tick<count;++tick){
        if(host->modern_menus)++host->modern_ticks;
        if(host->quick){
            PortableGpResultsFlow *menu=&host->quick_menu;
            if(menu->state==13u)break;
            const uint16_t pressed=tick==0u?samples[tick]:0u;
            /* Source2123C reads the accepted scene clock, including the row
             * and window phases and the callback that first enters case10. */
            if(!host->battle)++host->menu_ticks;
            if(host->race_background!=NULL&&
               !host->race_background(host->race_background_context,NULL)){host->error=1;return -1;}
            if(!host->battle&&host->quick_opening.state!=10u){
                if(portable_quick_results_opening_tick(&host->quick_opening,
                    (uint8_t)((pressed&(PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_START))!=0u))!=PORTABLE_GP_RESULTS_OK){host->error=1;return -1;}
                host->quick_board_ticks=host->quick_opening.state==10u?61u:0u;
                host->needs_present=1;
                if(host->audio!=NULL&&!route_audio_render(host->audio,0u,0,NULL)){host->error=1;return -1;}
                continue;
            }
            if(host->quick_board_ticks<61u){
                ++host->quick_board_ticks;
                if((pressed&(PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_START))!=0u)
                    host->quick_board_ticks=61u;
                if(host->audio!=NULL&&!route_audio_render(host->audio,0u,0,NULL)){host->error=1;return -1;}
                continue;
            }
            if(host->battle)++host->menu_ticks;
            host->needs_present=1;
            const uint16_t directions=(host->held|portable_mobile_touch_frontend())&UINT16_C(0xc0);
            uint16_t repeated=pressed&UINT16_C(0xc0);
            if(directions==0u || directions!=host->previous_held)host->hold_ticks=0u;
            else if(host->hold_ticks<UINT16_MAX)++host->hold_ticks;
            if(host->hold_ticks>=PORTABLE_FRONTEND_REPEAT_DELAY &&
               (host->hold_ticks-PORTABLE_FRONTEND_REPEAT_DELAY)%PORTABLE_FRONTEND_REPEAT_PERIOD==0u)
                repeated|=directions;
            host->previous_held=directions;
            const int selection=menu->state==11u;
            PortableGpResultsStatus status=selection?
                portable_gp_results_flow_tick_menu_selection(menu,repeated,pressed,
                    host->replay_available?0u:2u,host->window?host->window->menu_option_codes:NULL):
                portable_gp_results_flow_tick_action_dispatch(menu);
            if(status!=PORTABLE_GP_RESULTS_OK){host->error=1;return -1;}
            if((selection&&menu->menu_navigation_song_pending&&!route_audio_menu_cue(host->audio,141u))||
               (selection&&menu->menu_confirm_song_pending&&!route_audio_menu_cue(host->audio,menu->menu_confirm_song_id))||
               (host->audio!=NULL&&!route_audio_render(host->audio,0u,0,NULL))){host->error=1;return -1;}
            if(menu->action_dispatch_pending)host->next_mode=menu->action_dispatch_code;
        }else if(host->standings_asset!=NULL){
            if(host->standings.phase==PORTABLE_GP_STANDINGS_COMPLETE)break;
            if(host->race_background!=NULL &&
               !host->race_background(host->race_background_context,NULL)){host->error=1;return -1;}
            const uint8_t confirm=(uint8_t)(tick==0u&&(host->pressed&
                (PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_START))!=0u);
            if(portable_gp_standings_session_tick(&host->standings,confirm)!=
                PORTABLE_GP_STANDINGS_OK){host->error=1;return -1;}
            if((portable_gp_standings_session_take_player_point_cue(&host->standings)&&
                !route_audio_menu_cue(host->audio,101u))||
               (host->standings.confirm_cue_pending&&!route_audio_menu_cue(host->audio,143u))||
               (host->audio!=NULL&&!route_audio_render(host->audio,0u,0,NULL))){host->error=1;return -1;}
            host->standings.confirm_cue_pending=0u;
        }else{
            if(host->menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE)break;
            ++host->menu_ticks;
            if(host->race_background!=NULL &&
                !host->race_background(host->race_background_context,NULL)){host->error=1;return -1;}
            if(!portable_gp_failure_menu_tick(&host->menu,tick==0u?samples[tick]:0u)||
               (host->menu.cue!=0u&&!route_audio_menu_cue(host->audio,host->menu.cue))||
               (host->audio!=NULL&&!route_audio_render(host->audio,0u,0,NULL))){
                host->error=1;return -1;
            }
            host->next_mode=(uint8_t)host->menu.action;
        }
    }
    if(count!=0){host->pressed=0;host->needs_present=1;}
    if(host->needs_present){
        /* Modern results already own the clean frozen picture. The NULL
         * callbacks above retain source clocks; hidden redraws add no pixels. */
        if(host->race_background!=NULL && !host->modern_menus &&
           !host->race_background(host->race_background_context,&host->frame)){host->error=1;return -1;}
        if(host->modern_menus){
            if(!present_modern_results(host)){host->error=1;return -1;}
            host->needs_present=0;
            return host->quick?host->quick_menu.state==13u:host->standings_asset!=NULL?
                host->standings.phase==PORTABLE_GP_STANDINGS_COMPLETE:host->menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE;
        }
        PortableSoftwareFrame board=portable_software_frame_native_view(&host->frame,PORTABLE_FRAME_CENTER);
        if(host->standings_asset!=NULL&&host->race_background==NULL)
            portable_software_frame_clear(&host->frame,UINT32_C(0xff000000));
        const int rendered=host->quick?render_quick(host):host->standings_asset!=NULL?
            (host->race_background!=NULL?
             portable_gp_standings_render_over(host->standings_asset,&host->standings,&board):
             portable_gp_standings_render(host->standings_asset,&host->standings,&board))&&
                (host->standings.phase!=PORTABLE_GP_STANDINGS_HOLD||
                 portable_screen_capture(&host->frame,"gp-standings"))&&
                (host->standings.phase!=PORTABLE_GP_STANDINGS_SORTED_HOLD||
                 portable_screen_capture(&host->frame,"gp-cumulative")):
            render_retry(host);
        const char *capture=getenv("MKSC_RESULTS_FRAME");
        if(rendered && host->quick && host->quick_board_ticks>=61u && capture!=NULL && !host->frame_written){
            if(!portable_software_frame_write_ppm(&host->frame,capture)){host->error=1;return -1;}
            host->frame_written=1u;
        }
        if(!rendered||!portable_software_frame_present(host->renderer,
            host->target.texture!=NULL?host->target.texture:host->texture,&host->frame,host->fit)){
            host->error=1;return -1;
        }
        host->needs_present=0;
    }
    return host->quick?host->quick_menu.state==13u:host->standings_asset!=NULL?
        host->standings.phase==PORTABLE_GP_STANDINGS_COMPLETE:
        host->menu.phase==PORTABLE_GP_FAILURE_MENU_COMPLETE;
}

int portable_gp_results_host_end(PortableGpResultsHost *host,
    uint8_t *next_mode,uint8_t *quit_requested)
{
    if(host==NULL)return 1;
    if(next_mode!=NULL)*next_mode=host->next_mode;
    if(quit_requested!=NULL)*quit_requested=host->quit;
    const int error=host->error;
    portable_mobile_touch_clear();
    if(host->controller!=NULL)SDL_CloseGamepad(host->controller);
    modern_menu_host_close(&host->modern);
    portable_software_render_target_close(&host->target);
    free(host->frozen_pixels);free(host);return error;
}
#endif
