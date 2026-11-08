#include <SDL3/SDL_main.h>
#include "sdl_test_frame.h"
#include "game_host_internal.h"
#include "frame_scheduler.h"
#include "sdl_test_gamepad.h"
#include "modern_menu_host.h"

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr,"GP results host failed line %d: %s\n",__LINE__,#expression); \
    exit(1); } } while(0)

static uint64_t now_ns;
typedef struct { unsigned ticks,renders; } Background;
static int race_background(void *context,PortableSoftwareFrame *frame)
{
    Background *background=context;
    if(frame==NULL){++background->ticks;return 1;}
    ++background->renders;
    portable_software_frame_clear(frame,UINT32_C(0xff002000)|background->ticks);
    return 1;
}
static uint32_t picture(SDL_Renderer *renderer,const char *directory,const char *name)
{
    uint32_t pixels[240u*160u],checksum=UINT32_C(2166136261);
    CHECK(read_test_frame(renderer,pixels));
    for(unsigned i=0;i<240u*160u;++i)checksum=(checksum^pixels[i])*UINT32_C(16777619);
    if(directory!=NULL){
        char path[1024];
        CHECK(snprintf(path,sizeof path,"%s/%s.ppm",directory,name)>0);
        FILE *out=fopen(path,"wb");CHECK(out!=NULL);
        CHECK(fprintf(out,"P6\n240 160\n255\n")>0);
        for(unsigned i=0;i<240u*160u;++i){
            const uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),
                (uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
            CHECK(fwrite(rgb,1,3,out)==3);
        }
        CHECK(fclose(out)==0);
    }
    return checksum;
}
static void key(PortableGpResultsHost *host,SDL_Keycode keycode)
{
    const SDL_Event event={.key={.type=SDL_EVENT_KEY_DOWN,.key=keycode}};
    portable_gp_results_host_event(host,&event);
    SDL_Event release=event;release.type=SDL_EVENT_KEY_UP;
    portable_gp_results_host_event(host,&release);
}
static int step(PortableGpResultsHost *host)
{
    now_ns+=UINT64_C(20000000);
    return portable_gp_results_host_frame(host,now_ns);
}
static int one_tick(PortableGpResultsHost *host)
{
    now_ns+=((uint64_t)PORTABLE_FRAME_TICK_NUMERATOR*UINT64_C(1000000000)+
        PORTABLE_FRAME_TICK_DENOMINATOR-1u)/PORTABLE_FRAME_TICK_DENOMINATOR;
    return portable_gp_results_host_frame(host,now_ns);
}
static void settle_retry(PortableGpResultsHost *host)
{
    CHECK(step(host)==0);
    for(unsigned tick=0u;tick<6u;++tick)CHECK(one_tick(host)==0);
}
static void finish(PortableGpResultsHost *host,uint8_t expected_mode,uint8_t expected_quit)
{
    int status=0;
    for(unsigned tick=0;status==0&&tick<240u;++tick)status=step(host);
    CHECK(status==1);
    uint8_t mode=0,quit=0;
    CHECK(portable_gp_results_host_end(host,&mode,&quit)==0);
    CHECK(mode==expected_mode&&quit==expected_quit);
}

static uint32_t physical_color(size_t x,size_t y)
{
    return UINT32_C(0xff000000)|((uint32_t)(x&255u)<<16)|
        ((uint32_t)(y&255u)<<8)|(uint32_t)((x+y)&255u);
}

typedef struct { unsigned ticks,renders;size_t width;uint8_t artwork_lod; } PhysicalBackground;
static int physical_background(void *context,PortableSoftwareFrame *frame)
{
    PhysicalBackground *background=context;
    if(frame==NULL){++background->ticks;return 1;}
    CHECK(frame->width==background->width&&frame->height==160u&&frame->scale==4u);
    CHECK(frame->artwork_lod==background->artwork_lod);
    ++background->renders;
    for(size_t y=0u;y<640u;++y)for(size_t x=0u;x<frame->width*4u;++x)
        portable_software_frame_store_physical(frame,x,y,physical_color(x,y));
    return 1;
}

static void read_results_size(SDL_Renderer *renderer,uint32_t *pixels,int width)
{
    SDL_Surface *surface=SDL_RenderReadPixels(renderer,NULL);
    CHECK(surface!=NULL&&surface->w==width&&surface->h==640);
    CHECK(SDL_ConvertPixels(width,640,surface->format,surface->pixels,surface->pitch,
        SDL_PIXELFORMAT_ARGB8888,pixels,width*(int)sizeof *pixels));
    SDL_DestroySurface(surface);
}

static void read_physical_results(SDL_Renderer *renderer,uint32_t *pixels)
{
    read_results_size(renderer,pixels,1280);
}

static void results_window_resize(SDL_Window *window,PortableGpResultsHost *host,int width)
{
    CHECK(SDL_SetWindowSize(window,width,640));
    SDL_PumpEvents();
    const SDL_Event event={.type=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED};
    portable_gp_results_host_event(host,&event);
}

static void physical_results(const PortableAssetPackView *pack,
    const PortableGpStandingsAssetView *asset,const PortableGpResultsAssetView *window_asset,
    const PortableGpCupSession *cup,const uint32_t *native_pixels)
{
    SDL_Window *window=SDL_CreateWindow("Physical results",1280,640,SDL_WINDOW_HIDDEN);
    CHECK(window!=NULL);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,"software");CHECK(renderer!=NULL);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,240,160);CHECK(texture!=NULL);
    CHECK(SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST));
    const size_t stride=1287u,count=stride*640u;
    uint32_t *source=malloc(count*sizeof *source),*saved=malloc(count*sizeof *saved);
    uint32_t *output=malloc(1280u*640u*sizeof *output);
    CHECK(source!=NULL&&saved!=NULL&&output!=NULL);
    for(size_t y=0u;y<640u;++y)for(size_t x=0u;x<stride;++x)
        source[y*stride+x]=x<1280u?physical_color(x,y):UINT32_C(0xdeadbeef);
    memcpy(saved,source,count*sizeof *saved);
    const PortableSoftwareFrame race={source,320u,160u,NULL,4u,stride,2u,NULL};
    const PortableSoftwareFrame original=race;
    const uint8_t empty_options[5]={0,3,1,2,4};
    const PortableGpResultsAssetView empty_window={.menu_option_codes=empty_options};
    PortableQuickRunResults quick={.engine_class=1u};
    for(unsigned row=0u;row<8u;++row)
        quick.rows[row]=(PortableQuickRunResultRow){9000u,(uint8_t)row,(uint8_t)row,0u};
    const PortableBattleResults battle={.rows={{.racer_index=0u,.driver_class=0u,.rank=0u,.balloons=3u},
        {.racer_index=1u,.driver_class=1u,.rank=1u,.balloons=0u}},.count=2u};
    for(unsigned kind=0u;kind<4u;++kind){
        PortableGpResultsHost *host=kind==0u?
            portable_quick_results_begin(NULL,native_pixels,0u,NULL,renderer,texture,&empty_window,NULL,NULL):
            kind==1u?portable_gp_retry_begin(pack,native_pixels,3u,renderer,texture,NULL):
            kind==2u?portable_quick_results_begin(NULL,native_pixels,0u,NULL,renderer,texture,
                window_asset,asset,&quick):
            portable_battle_results_begin(NULL,native_pixels,NULL,renderer,texture,window_asset,asset,&battle,1u);
        CHECK(host!=NULL);
        CHECK(!portable_gp_results_host_video(host,9u,320u,1,&race));
        CHECK(portable_gp_results_host_video(host,4u,320u,1,&race));
        CHECK(memcmp(&race,&original,sizeof race)==0&&memcmp(source,saved,count*sizeof *source)==0);
        /* The host must own this copy rather than borrow the caller's next frame. */
        source[0]=UINT32_C(0xffabcdef);
        CHECK(step(host)==0);read_physical_results(renderer,output);
        CHECK(output[0]==physical_color(0u,0u));
        for(size_t y=0u;y<640u;y+=79u)for(size_t x=0u;x<160u;x+=17u){
            CHECK(output[y*1280u+x]==physical_color(x,y));
            CHECK(output[y*1280u+1120u+x]==physical_color(1120u+x,y));
        }
        if(kind==0u){
            /* No board: every physical sample survives, including details within one native pixel. */
            for(size_t y=0u;y<640u;++y)for(size_t x=0u;x<1280u;++x)
                CHECK(output[y*1280u+x]==physical_color(x,y));
            CHECK(output[0]!=output[1]&&output[0]!=output[1280]);
            CHECK(portable_gp_results_host_video(host,4u,320u,1,NULL));
            CHECK(step(host)==0);read_physical_results(renderer,output);
            CHECK(output[0]==physical_color(0u,0u));
            /* Automatic aspect follows the window, resampling the owned physical copy. */
            CHECK(portable_gp_results_host_video(host,4u,0u,1,NULL));
            results_window_resize(window,host,960);
            CHECK(step(host)==0);read_results_size(renderer,output,960);
            for(size_t y=0u;y<640u;y+=79u)for(size_t x=0u;x<960u;x+=37u)
                CHECK(output[y*960u+x]==physical_color(x*1280u/960u,y));
            /* An explicit aspect keeps its wide image, letterboxed in this narrower window. */
            CHECK(portable_gp_results_host_video(host,4u,320u,1,NULL));
            results_window_resize(window,host,960);
            CHECK(step(host)==0);read_results_size(renderer,output,960);
            CHECK(output[0]==UINT32_C(0xff000000));
            results_window_resize(window,host,1280);
            CHECK(step(host)==0);read_physical_results(renderer,output);
            CHECK(output[0]==physical_color(0u,0u));
        }else if(kind>=2u){
            /* Replacing the background after begin must retain the authored board. */
            CHECK(output[160u]!=physical_color(160u,0u));
        }
        source[0]=saved[0];
        CHECK(memcmp(source,saved,count*sizeof *source)==0);
        CHECK(portable_gp_results_host_end(host,NULL,NULL)==0);
    }
    PhysicalBackground live={.width=320u,.artwork_lod=2u};
    PortableGpResultsHost *host=portable_gp_standings_begin(NULL,asset,cup,renderer,texture,NULL);
    CHECK(host!=NULL&&portable_gp_results_host_video(host,4u,320u,1,&race));
    portable_gp_results_set_race_background(host,physical_background,&live);
    CHECK(step(host)==0&&live.renders==1u);read_physical_results(renderer,output);
    CHECK(output[0]==physical_color(0u,0u)&&output[1]==physical_color(1u,0u));
    CHECK(output[1279u+639u*1280u]==physical_color(1279u,639u));
    CHECK(memcmp(source,saved,count*sizeof *source)==0);
    CHECK(portable_gp_results_host_video(host,4u,0u,1,NULL));
    live.width=240u;results_window_resize(window,host,960);
    CHECK(step(host)==0);read_results_size(renderer,output,960);
    CHECK(output[0]==physical_color(0u,0u)&&output[1]==physical_color(1u,0u));
    CHECK(output[959u+639u*960u]==physical_color(959u,639u));
    CHECK(memcmp(source,saved,count*sizeof *source)==0);
    const SDL_Event lost={.type=SDL_EVENT_WINDOW_FOCUS_LOST};
    portable_gp_results_host_event(host,&lost);
    const unsigned ticks=live.ticks,renders=live.renders;
    CHECK(step(host)==0&&live.ticks==ticks&&live.renders==renders);
    CHECK(portable_gp_results_host_end(host,NULL,NULL)==0);
    free(output);free(saved);free(source);
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
}

static void tiny_modern_results_aspect(void)
{
    const int widths[2]={240,373};
    for(unsigned layout=0u;layout<2u;++layout){
        const int width=widths[layout],height=160;
        SDL_Window *window=SDL_CreateWindow("Tiny modern results",width,height,SDL_WINDOW_HIDDEN);CHECK(window!=NULL);
        SDL_Renderer *renderer=SDL_CreateRenderer(window,"software");CHECK(renderer!=NULL);
        const size_t count=(size_t)width*(size_t)height;
        uint32_t *race=malloc(count*sizeof *race);CHECK(race!=NULL);
        for(size_t i=0u;i<count;++i)race[i]=UINT32_C(0xffffffff);
        const PortableSoftwareFrame frame={.pixels=race,.width=(size_t)width,.height=(size_t)height};
        const ModernMenuResults results={.title="",.tick=0u};
        ModernMenuHost host={0};
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        int out_width=0,out_height=0;CHECK(SDL_GetCurrentRenderOutputSize(renderer,&out_width,&out_height));
        CHECK(out_width==width && out_height==height && host.width>=320 && host.height>=200);
        const int64_t aspect_error=(int64_t)host.width*out_height-(int64_t)host.height*out_width;
        CHECK(aspect_error>=-(int64_t)host.width && aspect_error<=(int64_t)host.width);
        CHECK(host.result_width==width && host.result_height==height && host.results_active);
        CHECK(!memcmp(host.race,race,count*sizeof *race));
        /* No rows/header/footer occupy the middle scanline. A matching race
         * fills it through both edges; black bars cannot hide in a resized
         * backing canvas even though its minimum dimensions exceed the window. */
        const size_t middle=(size_t)(host.height/2)*(size_t)host.width;
        CHECK((host.pixels[middle]&UINT32_C(0x00ffffff))!=0u);
        CHECK((host.pixels[middle+(size_t)host.width-1u]&UINT32_C(0x00ffffff))!=0u);
        const size_t canvas_count=(size_t)host.width*(size_t)host.height;
        uint32_t *before=malloc(canvas_count*sizeof *before);CHECK(before!=NULL);
        memcpy(before,host.pixels,canvas_count*sizeof *before);
        memset(race,0,count*sizeof *race);
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        CHECK(!memcmp(before,host.pixels,canvas_count*sizeof *before));
        CHECK(host.race[0]==UINT32_C(0xffffffff) && host.race[count-1u]==UINT32_C(0xffffffff));
        /* A live race keeps driving behind the board: every present recopies it. */
        ModernMenuResults live=results;live.live_frame=1u;live.tick=1u;
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&live,NULL));
        CHECK(host.race[0]==UINT32_C(0xff000000) && host.race[count-1u]==UINT32_C(0xff000000));
        CHECK(host.results_entry_tick==0u);
        free(before);modern_menu_host_close(&host);free(race);
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
}

static void modern_transition_host_ownership(void)
{
    for(unsigned resolution=0u;resolution<2u;++resolution){
        const size_t scale=resolution?4u:1u,width=373u*scale,height=160u*scale,stride=width+7u;
        const size_t count=stride*height;
        uint32_t *source=malloc(count*sizeof *source),*saved=malloc(count*sizeof *saved);
        CHECK(source!=NULL && saved!=NULL);
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<stride;++x)
            source[y*stride+x]=x<width?physical_color(x,y):UINT32_C(0xdeadbeef);
        memcpy(saved,source,count*sizeof *saved);
        const PortableSoftwareFrame frame={.pixels=source,.width=373u,.height=160u,
            .scale=(uint8_t)scale,.stride=stride};
        PortableSoftwareFrame frame_before;memcpy(&frame_before,&frame,sizeof frame);
        SDL_Window *window=SDL_CreateWindow("Transition ownership",373,160,SDL_WINDOW_HIDDEN);CHECK(window!=NULL);
        SDL_Renderer *renderer=SDL_CreateRenderer(window,"software");CHECK(renderer!=NULL);
        ModernMenuHost host={0};
        ModernMenuTransition entry={.title="Ready to race",.subtitle="Fixture course",.course_id=900001u,
            .kind=MODERN_TRANSITION_ENTRY,.elapsed=12u,.duration=40u};
        ModernMenuTransition entry_before;memcpy(&entry_before,&entry,sizeof entry);
        CHECK(modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
        const size_t canvas_count=(size_t)host.width*(size_t)host.height;
        uint32_t *first=malloc(canvas_count*sizeof *first),*drawn=malloc(canvas_count*sizeof *drawn);
        CHECK(first!=NULL && drawn!=NULL);memcpy(first,host.pixels,canvas_count*sizeof *first);
        const SDL_Event expose={.type=SDL_EVENT_WINDOW_EXPOSED};
        for(unsigned redraw=0u;redraw<3u;++redraw){
            modern_menu_host_event(&host,&expose);
            CHECK(modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
            CHECK(!memcmp(first,host.pixels,canvas_count*sizeof *first));
            CHECK(!memcmp(&entry,&entry_before,sizeof entry));
        }
        entry.elapsed=32u;CHECK(modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
        CHECK(memcmp(first,host.pixels,canvas_count*sizeof *first)!=0);
        memcpy(first,host.pixels,canvas_count*sizeof *first);
        CHECK(SDL_SetWindowSize(window,240,160));SDL_PumpEvents();
        CHECK(modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
        CHECK(SDL_SetWindowSize(window,373,160));SDL_PumpEvents();
        CHECK(modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
        CHECK((size_t)host.width*(size_t)host.height==canvas_count);
        CHECK(!memcmp(first,host.pixels,canvas_count*sizeof *first));
        CHECK(!memcmp(source,saved,count*sizeof *source) && !memcmp(&frame,&frame_before,sizeof frame));

        ModernMenuResults results={.title="Results fixture",.subtitle="Frozen race",.tick=100u,
            .row_count=1u,.item_count=1u,.labels={"Continue"},
            .rows={{.driver_id=900u,.name="Authored racer",.rank=1u,.value="1:23.45",.detail="Personal best"}}};
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        ModernMenu *uninterrupted=modern_menu_create();CHECK(uninterrupted!=NULL);
        uint32_t *reference_pixels=malloc(canvas_count*sizeof *reference_pixels);CHECK(reference_pixels!=NULL);
        UiCanvas reference_canvas;
        ui_canvas_init(&reference_canvas,reference_pixels,host.width,host.height,host.width);
        ModernMenuResults reference_view=results;
        reference_view.frame=host.race;reference_view.frame_width=host.result_width;
        reference_view.frame_height=host.result_height;
        reference_view.frame_w=(float)host.width;reference_view.frame_h=(float)host.height;
        CHECK(modern_menu_render_results(uninterrupted,&reference_view,&reference_canvas));
        CHECK(host.results_active && host.results_entry_tick==100u &&
            host.result_width==(int)width && host.result_height==(int)height);
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x)
            CHECK(host.race[y*width+x]==saved[y*stride+x]);
        memcpy(first,host.pixels,canvas_count*sizeof *first);
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x)source[y*stride+x]=UINT32_C(0xffff00ff);
        for(unsigned redraw=0u;redraw<3u;++redraw){
            modern_menu_host_event(&host,&expose);
            CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
            CHECK(host.results_entry_tick==100u && !memcmp(first,host.pixels,canvas_count*sizeof *first));
        }
        results.tick=108u;CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        reference_view.tick=108u;CHECK(modern_menu_render_results(uninterrupted,&reference_view,&reference_canvas));
        CHECK(memcmp(first,host.pixels,canvas_count*sizeof *first)!=0);
        memcpy(drawn,host.pixels,canvas_count*sizeof *drawn);
        CHECK(SDL_SetWindowSize(window,240,160));SDL_PumpEvents();
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        CHECK(SDL_SetWindowSize(window,373,160));SDL_PumpEvents();
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        CHECK(!memcmp(drawn,host.pixels,canvas_count*sizeof *drawn));
        results.tick=116u;CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        reference_view.tick=116u;CHECK(modern_menu_render_results(uninterrupted,&reference_view,&reference_canvas));
        /* The first unobscured result is the same age16 picture as a results
         * renderer that ran continuously beneath no transition at all. */
        CHECK(!memcmp(reference_pixels,host.pixels,canvas_count*sizeof *reference_pixels));
        memcpy(drawn,host.pixels,canvas_count*sizeof *drawn);
        CHECK(modern_menu_host_present_results(&host,renderer,&frame,1,&results,NULL));
        CHECK(!memcmp(drawn,host.pixels,canvas_count*sizeof *drawn));
        for(size_t y=0u;y<height;++y){
            for(size_t x=0u;x<width;++x)CHECK(host.race[y*width+x]==saved[y*stride+x]);
            for(size_t x=width;x<stride;++x)CHECK(source[y*stride+x]==UINT32_C(0xdeadbeef));
        }
        CHECK(!modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
        CHECK(!memcmp(drawn,host.pixels,canvas_count*sizeof *drawn));
        modern_menu_host_results_closed(&host);
        CHECK(modern_menu_host_present_transition(&host,renderer,&frame,1,&entry,NULL));
        CHECK(host.race[0]==UINT32_C(0xffff00ff)); /* A new handoff borrows the new race, not the old result. */
        modern_menu_destroy(uninterrupted);free(reference_pixels);
        modern_menu_host_close(&host);free(drawn);free(first);free(saved);free(source);
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
}

static void modern_standings_lifecycle(const PortableAssetPackView *pack,
    const PortableGpStandingsAssetView *asset,const PortableGpCupSession *cup,
    SDL_Renderer *renderer,SDL_Texture *texture)
{
    PortableGpCupSession cup_before;memcpy(&cup_before,cup,sizeof cup_before);
    PortableGpResultsHost *classic=portable_gp_standings_begin(NULL,asset,cup,renderer,texture,NULL);
    PortableGpResultsHost *modern=portable_gp_standings_begin(NULL,asset,cup,renderer,texture,NULL);
    CHECK(classic!=NULL && modern!=NULL);
    portable_gp_results_host_modern(modern,pack,1u,1u,1u);
    portable_gp_results_allow_skip(classic);portable_gp_results_allow_skip(modern);
    now_ns+=UINT64_C(20000000);
    CHECK(portable_gp_results_host_frame(classic,now_ns)==0);
    CHECK(portable_gp_results_host_frame(modern,now_ns)==0);
    const uint64_t duration=((uint64_t)PORTABLE_FRAME_TICK_NUMERATOR*UINT64_C(1000000000)+
        PORTABLE_FRAME_TICK_DENOMINATOR-1u)/PORTABLE_FRAME_TICK_DENOMINATOR;
    unsigned completed=0u;
    for(unsigned turn=1u;turn<=120u;++turn){
        /* Identical ordinary edges, with held-key repeats explicitly ignored
         * between edges. Rendering must not move either results controller. */
        if(turn%8u==0u){key(classic,SDLK_RETURN);key(modern,SDLK_RETURN);}
        else if(turn%8u==1u){
            const SDL_Event repeat={.key={.type=SDL_EVENT_KEY_DOWN,.key=SDLK_RETURN,.repeat=true}};
            portable_gp_results_host_event(classic,&repeat);portable_gp_results_host_event(modern,&repeat);
        }
        now_ns+=duration;
        const int a=portable_gp_results_host_frame(classic,now_ns),b=portable_gp_results_host_frame(modern,now_ns);
        CHECK(a>=0 && a==b);
        CHECK(!memcmp(cup,&cup_before,sizeof *cup));
        if(a==1){completed=turn;break;}
    }
    CHECK(completed>0u);
    uint8_t classic_mode=0u,modern_mode=0u,classic_quit=1u,modern_quit=1u;
    CHECK(portable_gp_results_host_end(classic,&classic_mode,&classic_quit)==0);
    CHECK(portable_gp_results_host_end(modern,&modern_mode,&modern_quit)==0);
    CHECK(classic_mode==cup->next_mode && modern_mode==classic_mode && !classic_quit && !modern_quit);
    CHECK(!memcmp(cup,&cup_before,sizeof *cup));

    SDL_Window *window=SDL_CreateWindow("Modern frozen standings",1280,640,SDL_WINDOW_HIDDEN);
    CHECK(window!=NULL);
    SDL_Renderer *wide_renderer=SDL_CreateRenderer(window,"software");CHECK(wide_renderer!=NULL);
    SDL_Texture *wide_texture=SDL_CreateTexture(wide_renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,240,160);CHECK(wide_texture!=NULL);
    const size_t stride=1287u,count=stride*640u;
    uint32_t *source=malloc(count*sizeof *source),*frozen=malloc(1280u*640u*sizeof *frozen),
        *output=malloc(1280u*640u*sizeof *output);
    CHECK(source!=NULL && frozen!=NULL && output!=NULL);
    for(size_t y=0u;y<640u;++y)for(size_t x=0u;x<stride;++x)
        source[y*stride+x]=x<1280u?physical_color(x,y):UINT32_C(0xdeadbeef);
    const PortableSoftwareFrame race={.pixels=source,.width=320u,.height=160u,.scale=4u,.stride=stride};
    modern=portable_gp_standings_begin(NULL,asset,cup,wide_renderer,wide_texture,NULL);CHECK(modern!=NULL);
    portable_gp_results_host_modern(modern,pack,1u,1u,1u);
    CHECK(portable_gp_results_host_video(modern,4u,0u,1,&race));
    CHECK(step(modern)==0);
    /* Settle the overlay with four presentations, rather than spending a
     * full high-resolution render on every accepted simulation tick. */
    for(unsigned frame=0u;frame<4u;++frame){now_ns+=duration*8u;CHECK(portable_gp_results_host_frame(modern,now_ns)==0);}
    read_physical_results(wide_renderer,frozen);
    for(size_t y=0u;y<640u;++y)for(size_t x=0u;x<1280u;++x)source[y*stride+x]=UINT32_C(0xffff00ff);
    const SDL_Event exposed={.type=SDL_EVENT_WINDOW_EXPOSED};
    portable_gp_results_host_event(modern,&exposed);
    CHECK(portable_gp_results_host_frame(modern,now_ns)==0);read_physical_results(wide_renderer,output);
    CHECK(!memcmp(output,frozen,1280u*640u*sizeof *output));
    results_window_resize(window,modern,960);
    CHECK(portable_gp_results_host_frame(modern,now_ns)==0);read_results_size(wide_renderer,output,960);
    results_window_resize(window,modern,1280);
    CHECK(portable_gp_results_host_frame(modern,now_ns)==0);read_physical_results(wide_renderer,output);
    CHECK(!memcmp(output,frozen,1280u*640u*sizeof *output));
    for(size_t y=0u;y<640u;++y){
        CHECK(source[y*stride]==UINT32_C(0xffff00ff));
        for(size_t x=1280u;x<stride;++x)CHECK(source[y*stride+x]==UINT32_C(0xdeadbeef));
    }
    CHECK(!memcmp(cup,&cup_before,sizeof *cup));
    CHECK(portable_gp_results_host_end(modern,NULL,NULL)==0);
    free(output);free(frozen);free(source);
    SDL_DestroyTexture(wide_texture);SDL_DestroyRenderer(wide_renderer);SDL_DestroyWindow(window);
}

int main(int argc,char **argv)
{
    CHECK(argc>=2&&argc<=4);
    const char *directory=argc>=3?argv[2]:NULL;
    FILE *file=fopen(argv[1],"rb");
    CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);
    CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);
    CHECK(bytes!=NULL&&fread(bytes,1,(size_t)length,file)==(size_t)length);
    CHECK(fclose(file)==0);
    PortableAssetPackView pack;
    CHECK(portable_asset_pack_open(bytes,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(test_gamepad_isolate());
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD));
    tiny_modern_results_aspect();
    modern_transition_host_ownership();
    SDL_JoystickID gamepad_id;
    SDL_Joystick *joystick=test_gamepad_attach(&gamepad_id);
    CHECK(joystick!=NULL);
    SDL_Window *window=SDL_CreateWindow("GP results lifecycle",240,160,SDL_WINDOW_HIDDEN);
    CHECK(window!=NULL);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,"software");
    CHECK(renderer!=NULL);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,240,160);
    CHECK(texture!=NULL);
    static uint32_t race_pixels[240u*160u];
    for(unsigned i=0;i<240u*160u;++i)race_pixels[i]=UINT32_C(0xff445566);
    PortableGpResultsHost *host=portable_gp_retry_begin(&pack,race_pixels,3u,
        renderer,texture,NULL);
    CHECK(host!=NULL&&step(host)==0);
    /* The scene owns the frozen frame, independent of the parent race buffer. */
    race_pixels[0]=0u;
    uint32_t opening_picture=picture(renderer,NULL,NULL);
    for(unsigned tick=0u;tick<6u;++tick){
        CHECK(one_tick(host)==0);
        const uint32_t next=picture(renderer,NULL,NULL);
        CHECK(next!=opening_picture);
        opening_picture=next;
    }
    const uint32_t retry_picture=picture(renderer,directory,"retry");
    const SDL_Event other={.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        .which=gamepad_id+1u,.button=SDL_GAMEPAD_BUTTON_SOUTH}};
    portable_gp_results_host_event(host,&other);CHECK(step(host)==0);
    CHECK(picture(renderer,NULL,NULL)==retry_picture);
    const SDL_Event down={.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        .which=gamepad_id,.button=SDL_GAMEPAD_BUTTON_DPAD_DOWN}};
    portable_gp_results_host_event(host,&down);CHECK(step(host)==0);
    const uint32_t quit_picture=picture(renderer,directory,"quit");
    CHECK(quit_picture!=retry_picture);
    const SDL_Event confirm={.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        .which=gamepad_id,.button=SDL_GAMEPAD_BUTTON_SOUTH}};
    portable_gp_results_host_event(host,&confirm);CHECK(step(host)==0);
    for(unsigned tick=0;tick<12;++tick)CHECK(step(host)==0);
    const uint32_t no_picture=picture(renderer,directory,"confirm-no");
    key(host,SDLK_LEFT);CHECK(step(host)==0);
    CHECK(picture(renderer,directory,"confirm-yes")!=no_picture);
    key(host,SDLK_RIGHT);CHECK(step(host)==0);
    CHECK(picture(renderer,NULL,NULL)==no_picture);
    key(host,SDLK_RETURN);CHECK(step(host)==0);
    for(unsigned tick=0;tick<12;++tick)CHECK(step(host)==0);
    /* NO returns to the original QUIT selection, not TRY AGAIN. */
    CHECK(picture(renderer,NULL,NULL)==quit_picture);
    key(host,SDLK_UP);CHECK(step(host)==0);
    CHECK(picture(renderer,NULL,NULL)==retry_picture);
    key(host,SDLK_RETURN);
    finish(host,PORTABLE_GP_FAILURE_ACTION_RETRY,0);
    race_pixels[0]=UINT32_C(0xff445566);

    host=portable_gp_retry_begin(&pack,race_pixels,3u,renderer,texture,NULL);
    CHECK(host!=NULL);settle_retry(host);
    key(host,SDLK_DOWN);CHECK(step(host)==0);
    key(host,SDLK_RETURN);CHECK(step(host)==0);
    for(unsigned tick=0;tick<12;++tick)CHECK(step(host)==0);
    key(host,SDLK_LEFT);CHECK(step(host)==0);
    key(host,SDLK_RETURN);
    finish(host,PORTABLE_GP_FAILURE_ACTION_QUIT,0);

    host=portable_gp_retry_begin(&pack,race_pixels,3u,renderer,texture,NULL);
    CHECK(host!=NULL);settle_retry(host);
    key(host,SDLK_RETURN);
    SDL_Event event={.window={.type=SDL_EVENT_WINDOW_FOCUS_LOST}};
    portable_gp_results_host_event(host,&event);
    for(unsigned tick=0;tick<12;++tick)CHECK(step(host)==0);
    event.type=SDL_EVENT_WINDOW_FOCUS_GAINED;
    portable_gp_results_host_event(host,&event);
    for(unsigned tick=0;tick<160;++tick)CHECK(step(host)==0);
    key(host,SDLK_RETURN);
    finish(host,PORTABLE_GP_FAILURE_ACTION_RETRY,0);

    host=portable_gp_retry_begin(&pack,race_pixels,0u,renderer,texture,NULL);
    CHECK(host!=NULL&&step(host)==0);
    uint32_t game_over_pixels[240u*160u];
    CHECK(read_test_frame(renderer,game_over_pixels));
    CHECK(game_over_pixels[0]==race_pixels[0]);
    key(host,SDLK_RETURN);
    for(unsigned tick=0;tick<80u;++tick)CHECK(step(host)==0);
    key(host,SDLK_RETURN);
    finish(host,PORTABLE_GP_FAILURE_ACTION_QUIT,0);

    host=portable_gp_retry_begin(&pack,race_pixels,3u,renderer,texture,NULL);
    CHECK(host!=NULL);
    Background retry_live={0};
    portable_gp_results_set_race_background(host,race_background,&retry_live);
    CHECK(step(host)==0&&retry_live.ticks==0u&&retry_live.renders==1u);
    for(unsigned tick=1u;tick<=6u;++tick){
        CHECK(one_tick(host)==0);
        CHECK(retry_live.ticks==tick&&retry_live.renders==tick+1u);
    }
    const uint32_t live_retry_picture=picture(renderer,NULL,NULL);
    const SDL_Event exposed={.type=SDL_EVENT_WINDOW_EXPOSED};
    portable_gp_results_host_event(host,&exposed);
    CHECK(portable_gp_results_host_frame(host,now_ns)==0);
    CHECK(retry_live.ticks==6u&&retry_live.renders==8u);
    CHECK(picture(renderer,NULL,NULL)==live_retry_picture);
    CHECK(race_pixels[0]==UINT32_C(0xff445566));
    CHECK(portable_gp_results_host_end(host,NULL,NULL)==0);

    static uint8_t tiles[PORTABLE_GP_STANDINGS_TILE_BYTES];
    static uint16_t palette[PORTABLE_GP_STANDINGS_PALETTE_COUNT];
    static uint16_t background[PORTABLE_GP_STANDINGS_MAP_COUNT];
    static uint16_t drivers[PORTABLE_GP_STANDINGS_DRIVER_COUNT*PORTABLE_GP_STANDINGS_DRIVER_CELLS];
    static uint16_t footer[PORTABLE_GP_STANDINGS_FOOTER_CELLS];
    PortableGpStandingsAssetView asset={tiles,sizeof tiles,palette,
        PORTABLE_GP_STANDINGS_PALETTE_COUNT,background,PORTABLE_GP_STANDINGS_MAP_COUNT,
        drivers,PORTABLE_GP_STANDINGS_DRIVER_COUNT,PORTABLE_GP_STANDINGS_DRIVER_CELLS,
        footer,PORTABLE_GP_STANDINGS_FOOTER_CELLS,24u,4u,{NULL},{NULL}};
    PortableGpCupSession cup={.stage=PORTABLE_GP_CUP_STAGE_STANDINGS,
        .next_mode=8u,.race_mode_flags=0x2200u};
    const uint8_t points[8]={9,6,3,1,0,0,0,0};
    for(unsigned racer=0;racer<8;++racer){
        cup.race_order[racer]=(uint8_t)racer;
        cup.standings_order[racer]=(uint8_t)racer;
        cup.rows[racer].standing_rank=(uint8_t)racer;
        cup.rows[racer].racer_index=(uint8_t)racer;
        cup.rows[racer].driver_class=(uint8_t)racer;
        cup.rows[racer].points_awarded=points[racer];
        cup.rows[racer].points_after=points[racer];
    }
    modern_standings_lifecycle(&pack,&asset,&cup,renderer,texture);
    host=portable_gp_standings_begin(NULL,&asset,&cup,renderer,texture,NULL);
    CHECK(host!=NULL);
    Background live={0};
    portable_gp_results_set_race_background(host,race_background,&live);
    CHECK(step(host)==0 && live.renders==1u);
    uint32_t live_pixels[240u*160u];
    CHECK(read_test_frame(renderer,live_pixels));
    CHECK(live_pixels[0]==(UINT32_C(0xff002000)|live.ticks));
    for(unsigned tick=0u;tick<350u;++tick)CHECK(step(host)==0);
    CHECK(live.ticks>=350u && live.renders>=350u);
    CHECK(read_test_frame(renderer,live_pixels));
    CHECK(live_pixels[0]==(UINT32_C(0xff002000)|live.ticks));
    key(host,SDLK_RETURN);
    finish(host,8u,0);
    host=portable_gp_standings_begin(NULL,&asset,&cup,renderer,texture,NULL);
    CHECK(host!=NULL&&step(host)==0);
    portable_gp_results_allow_skip(host);
    key(host,SDLK_RETURN);CHECK(step(host)==0);
    CHECK(step(host)==0);
    key(host,SDLK_RETURN);CHECK(step(host)==0);
    CHECK(step(host)==0 && step(host)==0);
    key(host,SDLK_RETURN);
    finish(host,8u,0);
    /* The connected pad uses the same A/Start edges as the keyboard:
     * first consolidate points, then leave the completed hold. */
    for(unsigned button=0u;button<2u;++button){
        host=portable_gp_standings_begin(NULL,&asset,&cup,renderer,texture,NULL);
        CHECK(host!=NULL&&step(host)==0);
        portable_gp_results_allow_skip(host);
        const SDL_Event advance={.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
            .which=gamepad_id,.button=button?SDL_GAMEPAD_BUTTON_START:SDL_GAMEPAD_BUTTON_SOUTH}};
        portable_gp_results_host_event(host,&advance);CHECK(step(host)==0);
        CHECK(step(host)==0);
        portable_gp_results_host_event(host,&advance);CHECK(step(host)==0);
        CHECK(step(host)==0 && step(host)==0);
        portable_gp_results_host_event(host,&advance);
        finish(host,8u,0);
    }
    host=portable_gp_standings_begin(NULL,&asset,&cup,renderer,texture,NULL);
    CHECK(host!=NULL);
    event=(SDL_Event){.type=SDL_EVENT_QUIT};
    portable_gp_results_host_event(host,&event);
    finish(host,8u,1u);
    /* A synthetic 2123C window and 9B4CC board map stand in for the ROM's. */
    static uint8_t window_tiles[PORTABLE_GP_RESULTS_TILE_BYTES];
    static uint16_t window_palette[PORTABLE_GP_RESULTS_PALETTE_COUNT],
        window_map[PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT],window_scroll[PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT],
        menu_palette[PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT],pulse_base[PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT],
        pulse_factors[PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT],board_map[PORTABLE_GP_RESULTS_MAP_COUNT];
    for(unsigned i=0u;i<sizeof window_tiles;++i)window_tiles[i]=(uint8_t)(((i/32u)%15u+1u)*0x11u);
    for(unsigned i=0u;i<PORTABLE_GP_RESULTS_PALETTE_COUNT;++i)window_palette[i]=(uint16_t)(i*0x0421u);
    for(unsigned i=0u;i<PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT;++i){menu_palette[i]=(uint16_t)(0x7c00u|i);pulse_base[i]=(uint16_t)i;}
    for(unsigned i=0u;i<PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT;++i)window_map[i]=(uint16_t)(0x1000u|(i%0x200u));
    for(unsigned i=0u;i<PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT;++i)window_scroll[i]=(uint16_t)(0x2000u|(i%0x200u));
    for(unsigned i=0u;i<PORTABLE_GP_RESULTS_MAP_COUNT;++i)board_map[i]=(uint16_t)(0xf000u|(i%0x180u));
    const uint8_t option_codes[5]={0,3,1,2,4};
    const PortableGpResultsAssetView action_window={.menu_option_codes=option_codes,
        .followup_tiles=window_tiles,.followup_tile_bytes=sizeof window_tiles,
        .followup_palette=window_palette,.followup_palette_count=PORTABLE_GP_RESULTS_PALETTE_COUNT,
        .followup_map=window_map,.followup_map_count=PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT,
        .scroll_map=window_scroll,.scroll_map_count=PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT,
        .menu_palette=menu_palette,.menu_palette_count=PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,
        .menu_pulse_base_palette=pulse_base,.menu_pulse_base_palette_count=PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,
        .menu_pulse_factors=pulse_factors,.menu_pulse_factor_count=PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT,
        .time_board_map=board_map,.time_board_map_count=PORTABLE_GP_RESULTS_MAP_COUNT};
    physical_results(&pack,&asset,&action_window,&cup,race_pixels);
    /* Quick Run shares the recovered five-action menu, including Replay's
     * disabled branch and wrapping. Keyboard and pad reach the same flow. */
    for(unsigned i=0u;i<240u*160u;++i)race_pixels[i]=UINT32_C(0xff445566);
    const uint8_t actions[5]={1u,6u,2u,3u,4u};
    for(unsigned row=0u;row<5u;++row){
        host=portable_quick_results_begin(NULL,race_pixels,1u,NULL,renderer,texture,&action_window,NULL,NULL);
        CHECK(host!=NULL&&step(host)==0);
        const uint32_t panel=picture(renderer,directory,"quick-results");
        CHECK(panel!=retry_picture);
        for(unsigned move=0u;move<row;++move){key(host,SDLK_DOWN);CHECK(step(host)==0);}
        event=(SDL_Event){.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
            .which=gamepad_id,.button=SDL_GAMEPAD_BUTTON_SOUTH}};
        portable_gp_results_host_event(host,&event);
        finish(host,actions[row],0u);
    }
    PortableQuickRunResults board={.engine_class=2u};
    for(unsigned row=0u;row<8u;++row){
        board.rows[row]=(PortableQuickRunResultRow){(uint16_t)(9000u+row*100u),
            (uint8_t)row,(uint8_t)row,(uint8_t)(row>2u)};
    }
    host=portable_quick_results_begin(NULL,race_pixels,1u,NULL,renderer,texture,&action_window,&asset,&board);
    CHECK(host!=NULL && step(host)==0);
    const uint32_t board_picture=picture(renderer,directory,"quick-board");
    /* 2123C: confirm completes the row phase, then clear/redraw and the
     * five-step window owner precede selection. It never activates Try Again. */
    Background quick_live={0};
    portable_gp_results_set_race_background(host,race_background,&quick_live);
    /* Enter at61: ten accepted callbacks reach71/phase7 on the first menu
     * frame. A menu-local counter would instead select the red phase0. */
    portable_gp_results_source_clock(host,61u);pulse_factors[7]=32768u;
    key(host,SDLK_RETURN);CHECK(one_tick(host)==0);
    for(unsigned tick=0u;tick<9u;++tick)CHECK(one_tick(host)==0);
    CHECK(quick_live.ticks==10u&&quick_live.renders==10u);
    CHECK(picture(renderer,directory,"quick-board-menu")!=board_picture);
    uint32_t clock_pixels[240u*160u];
    CHECK(read_test_frame(renderer,clock_pixels));
    CHECK((clock_pixels[72u*240u+176u]&UINT32_C(0xffffff))==UINT32_C(0x5a00ff));
    const unsigned ticks_before=quick_live.ticks,renders_before=quick_live.renders;
    CHECK(portable_gp_results_host_frame(host,now_ns)==0);
    CHECK(quick_live.ticks==ticks_before&&quick_live.renders==renders_before);
    key(host,SDLK_UP);CHECK(step(host)==0);
    key(host,SDLK_RETURN);finish(host,4u,0u);
    pulse_factors[7]=0u;
    /* A ready action menu (return from Replay) inherits the same global
     * phase and crosses63->64 without advancing on the constructor render. */
    host=portable_quick_results_begin(NULL,race_pixels,1u,NULL,renderer,texture,&action_window,NULL,NULL);
    CHECK(host!=NULL);portable_gp_results_source_clock(host,63u);
    pulse_factors[0]=32768u;
    CHECK(step(host)==0&&read_test_frame(renderer,clock_pixels));
    CHECK((clock_pixels[72u*240u+176u]&UINT32_C(0xffffff))==UINT32_C(0x5a0000));
    CHECK(one_tick(host)==0&&read_test_frame(renderer,clock_pixels));
    CHECK((clock_pixels[72u*240u+176u]&UINT32_C(0xffffff))==UINT32_C(0x5a00ff));
    key(host,SDLK_RETURN);finish(host,1u,0u);pulse_factors[0]=0u;
    CHECK(portable_quick_results_begin(NULL,race_pixels,1u,NULL,renderer,texture,&action_window,NULL,&board)==NULL);
    host=portable_quick_results_begin(NULL,race_pixels,0u,NULL,renderer,texture,&action_window,NULL,NULL);
    CHECK(host!=NULL&&step(host)==0);
    key(host,SDLK_DOWN);CHECK(step(host)==0);
    key(host,SDLK_RETURN);CHECK(step(host)==0);
    for(unsigned tick=0u;tick<8u;++tick)CHECK(step(host)==0);
    key(host,SDLK_DOWN);CHECK(step(host)==0);
    key(host,SDLK_DOWN);CHECK(step(host)==0);
    key(host,SDLK_SPACE);finish(host,3u,0u);
    host=portable_quick_results_begin(NULL,race_pixels,1u,NULL,renderer,texture,&action_window,NULL,NULL);
    CHECK(host!=NULL&&step(host)==0);
    key(host,SDLK_UP);CHECK(step(host)==0);
    key(host,SDLK_RETURN);finish(host,4u,0u);
    if(argc==4){
        FILE *game_file=fopen(argv[3],"rb");
        CHECK(game_file!=NULL && fseek(game_file,0,SEEK_END)==0);
        const long game_length=ftell(game_file);
        CHECK(game_length>0 && fseek(game_file,0,SEEK_SET)==0);
        uint8_t *game_bytes=malloc((size_t)game_length);
        CHECK(game_bytes!=NULL && fread(game_bytes,1,(size_t)game_length,game_file)==(size_t)game_length);
        CHECK(fclose(game_file)==0);
        PortableAssetPackView game_pack;
        CHECK(portable_asset_pack_open(game_bytes,(size_t)game_length,&game_pack)==PORTABLE_ASSET_PACK_OK);
        PortableContentSelection selection;
        PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
        PortablePresentationPackStorage *art=malloc(sizeof *art);
        PortableRecoveredRaceView race;
        PortablePresentationPackView view;
        CHECK(storage!=NULL && art!=NULL);
        CHECK(portable_content_registry_select(&game_pack,24u,1u,&selection)==PORTABLE_CONTENT_OK);
        CHECK(portable_recovered_pack_open_race(&game_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(portable_presentation_pack_open(&game_pack,&selection,storage,&race,art,&view)==PORTABLE_RECOVERED_PACK_OK);
        host=portable_quick_results_begin(NULL,race_pixels,1u,NULL,renderer,texture,&view.gp_results,&view.gp_standings,&board);
        CHECK(host!=NULL && step(host)==0);
        (void)picture(renderer,directory,"quick-imported-board");
        key(host,SDLK_RETURN);CHECK(step(host)==0);
        (void)picture(renderer,directory,"quick-imported-menu");
        key(host,SDLK_RETURN);finish(host,1u,0u);
        free(art);free(storage);free(game_bytes);
    }
    cup.stage=PORTABLE_GP_CUP_STAGE_NEXT_RACE;
    CHECK(portable_gp_standings_begin(NULL,&asset,&cup,renderer,texture,NULL)==NULL);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO)!=0u&&SDL_RenderClear(renderer));
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    SDL_CloseJoystick(joystick);CHECK(SDL_DetachVirtualJoystick(gamepad_id));
    SDL_Quit();free(bytes);
    puts("GP result scene lifecycle: PASS");
    return 0;
}
