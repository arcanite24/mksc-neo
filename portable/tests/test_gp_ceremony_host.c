#include <SDL3/SDL_main.h>
#include "game_host_internal.h"

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr,"GP ceremony host failed line %d: %s\n",__LINE__,#expression); \
    exit(1); } } while(0)

static uint64_t now_ns;
static int step(PortableGpCeremonyHost *host)
{
    now_ns+=UINT64_C(80000000);
    return portable_gp_ceremony_host_frame(host,now_ns);
}
static void event(PortableGpCeremonyHost *host,SDL_Keycode key)
{
    const SDL_Event input={.key={.type=SDL_EVENT_KEY_DOWN,.key=key}};
    portable_gp_ceremony_host_event(host,&input);
}
static void check_physical_viewport(SDL_Window *window,SDL_Renderer *renderer)
{
    PortableSoftwareRenderTarget target={0};
    CHECK(portable_software_render_target_resize(renderer,&target,240u,160u,4u,0));
    float width=0,height=0;
    CHECK(SDL_GetTextureSize(target.texture,&width,&height));
    CHECK(width==960.0f&&height==640.0f);
    CHECK(SDL_SetWindowSize(window,1200,800));
    SDL_PumpEvents();
    portable_software_frame_clear(&target.frame,UINT32_C(0xffff0000));
    CHECK(portable_software_frame_present(renderer,target.texture,&target.frame,0));
    SDL_Surface *surface=SDL_RenderReadPixels(renderer,NULL);
    CHECK(surface!=NULL&&surface->w==1200&&surface->h==800);
    const int points[4][2]={{0,0},{1199,0},{0,799},{1199,799}};
    for(unsigned i=0;i<4u;++i){
        Uint8 r,g,b,a;
        CHECK(SDL_ReadSurfacePixel(surface,points[i][0],points[i][1],&r,&g,&b,&a));
        CHECK(r==255u&&g==0u&&b==0u);
    }
    SDL_DestroySurface(surface);
    portable_software_render_target_close(&target);
    CHECK(target.texture==NULL&&target.frame.pixels==NULL);
    CHECK(SDL_SetWindowSize(window,240,160));
    SDL_PumpEvents();
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    FILE *file=fopen(argv[1],"rb");
    CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);
    CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);
    CHECK(bytes!=NULL&&fread(bytes,1,(size_t)length,file)==(size_t)length);
    CHECK(fclose(file)==0);
    PortableAssetPackView pack;
    CHECK(portable_asset_pack_open(bytes,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    SDL_Window *window=SDL_CreateWindow("GP ceremony lifecycle",240,160,SDL_WINDOW_HIDDEN);
    CHECK(window!=NULL);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,"software");
    CHECK(renderer!=NULL);
    check_physical_viewport(window,renderer);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,240,160);
    CHECK(texture!=NULL);
    PortableGpCeremonySession ceremony={.state5_next_state=6u,
        .podium_driver_classes={0,1,2},.selected_driver_class=3u,.result_song_id=0x38u};
    for(unsigned branch=0;branch<2;++branch){
        ceremony.state5_next_state=branch==0?6u:11u;
        ceremony.player_standing_rank=branch==0?0u:3u;
        PortableGpCeremonyHost *host=portable_gp_ceremony_host_begin(&pack,&ceremony,0u,
            window,renderer,texture,NULL);
        CHECK(host!=NULL&&step(host)==0);
        /* Early confirmation must not bypass the later prompt. */
        event(host,SDLK_RETURN);CHECK(step(host)==0);
        for(unsigned frame=0;frame<550;++frame)CHECK(step(host)==0);
        /* Confirmation queued before focus loss is discarded. */
        event(host,SDLK_RETURN);
        SDL_Event input={.window={.type=SDL_EVENT_WINDOW_FOCUS_LOST}};
        portable_gp_ceremony_host_event(host,&input);
        for(unsigned frame=0;frame<10;++frame)CHECK(step(host)==0);
        input.type=SDL_EVENT_WINDOW_FOCUS_GAINED;
        portable_gp_ceremony_host_event(host,&input);
        for(unsigned frame=0;frame<10;++frame)CHECK(step(host)==0);
        if(branch==0u)event(host,SDLK_RETURN);
        else {
            const SDL_Event confirm={.gbutton={.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN,
                .button=SDL_GAMEPAD_BUTTON_SOUTH}};
            portable_gp_ceremony_host_event(host,&confirm);
        }
        int status=0;
        for(unsigned frame=0;status==0&&frame<40;++frame)status=step(host);
        CHECK(status==1);
        uint8_t quit=1;
        CHECK(portable_gp_ceremony_host_end(host,&quit)==0&&!quit);
        CHECK(SDL_WasInit(SDL_INIT_VIDEO)!=0u&&SDL_RenderClear(renderer));
    }
    PortableGpCeremonyHost *host=portable_gp_ceremony_host_begin(&pack,&ceremony,0u,
        window,renderer,texture,NULL);
    CHECK(host!=NULL);
    event(host,SDLK_ESCAPE);
    CHECK(step(host)==1);
    uint8_t quit=0;
    CHECK(portable_gp_ceremony_host_end(host,&quit)==0&&quit);
    ceremony.selected_driver_class=255u;
    CHECK(portable_gp_ceremony_host_begin(&pack,&ceremony,0u,window,renderer,texture,NULL)==NULL);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO)!=0u&&SDL_RenderClear(renderer));
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    SDL_Quit();free(bytes);
    puts("GP ceremony scene lifecycle: PASS");
    return 0;
}
