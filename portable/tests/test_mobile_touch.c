#include <SDL3/SDL_main.h>
#include "mobile_touch.h"
#include "mobile_lifecycle.h"
#include "driver_input.h"
#include "frontend_flow.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"mobile touch failed line %d: %s\n",__LINE__,#x);exit(1);} } while(0)

static uint16_t finger(SDL_Renderer *renderer,SDL_EventType type,SDL_TouchID device,SDL_FingerID id,float x,float y)
{
    SDL_Event event={.tfinger={.type=(Uint32)type,.touchID=device,.fingerID=id,.x=x,.y=y}};
    SDL_Event before;memcpy(&before,&event,sizeof before);
    const uint16_t pressed=portable_mobile_touch_event(&event,renderer);
    CHECK(!memcmp(&before,&event,sizeof event));
    return pressed;
}

static PortableMobileTouchButton button(SDL_Renderer *renderer,uint16_t driving_mask)
{
    int width=0,height=0;CHECK(SDL_GetCurrentRenderOutputSize(renderer,&width,&height));
    PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
    CHECK(portable_mobile_touch_layout(width,height,buttons,PORTABLE_MOBILE_TOUCH_BUTTONS)==PORTABLE_MOBILE_TOUCH_BUTTONS);
    for(unsigned i=0u;i<PORTABLE_MOBILE_TOUCH_BUTTONS;++i)if(buttons[i].driving_mask==driving_mask)return buttons[i];
    CHECK(0);return buttons[0];
}

static uint16_t hit(SDL_Renderer *renderer,SDL_EventType type,SDL_TouchID device,SDL_FingerID id,uint16_t mask)
{
    int width=0,height=0;CHECK(SDL_GetCurrentRenderOutputSize(renderer,&width,&height));
    const PortableMobileTouchButton target=button(renderer,mask);
    return finger(renderer,type,device,id,target.x/(float)width,target.y/(float)height);
}

static void driving_edge_cases(SDL_Renderer *renderer)
{
    portable_mobile_touch_configure(1);portable_mobile_touch_clear();
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,3,21,PORTABLE_INPUT_HOP);
    (void)finger(renderer,SDL_EVENT_FINGER_UP,3,21,0.0f,0.0f);
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,3,22,PORTABLE_INPUT_ITEM);
    (void)finger(renderer,SDL_EVENT_FINGER_UP,3,22,0.0f,0.0f);
    CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_frontend());
    CHECK(portable_mobile_touch_draw(renderer));
    CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_frontend());
    CHECK(portable_mobile_touch_take_pressed()==(PORTABLE_FRONTEND_BUTTON_R|PORTABLE_FRONTEND_BUTTON_L));
    CHECK(portable_mobile_touch_draw(renderer));
    CHECK(portable_mobile_touch_take_driving_pressed()==(PORTABLE_INPUT_HOP|PORTABLE_INPUT_ITEM));
    CHECK(!portable_mobile_touch_take_driving_pressed());
    const uint16_t continuous[]={PORTABLE_INPUT_ACCELERATE,PORTABLE_INPUT_BRAKE,
        PORTABLE_INPUT_LEFT,PORTABLE_INPUT_RIGHT,PORTABLE_INPUT_UP,PORTABLE_INPUT_DOWN};
    for(unsigned i=0u;i<sizeof continuous/sizeof continuous[0];++i){
        (void)hit(renderer,SDL_EVENT_FINGER_DOWN,3,21,continuous[i]);
        CHECK(portable_mobile_touch_driving()==continuous[i]);
        CHECK(!portable_mobile_touch_take_driving_pressed());
        (void)finger(renderer,SDL_EVENT_FINGER_UP,3,21,0.0f,0.0f);
        CHECK(!portable_mobile_touch_take_driving_pressed());
    }
    portable_mobile_touch_clear();
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,4,1,PORTABLE_INPUT_HOP);
    CHECK(portable_mobile_touch_take_driving_pressed()==PORTABLE_INPUT_HOP);
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,4,2,PORTABLE_INPUT_HOP);
    CHECK(portable_mobile_touch_driving()==PORTABLE_INPUT_HOP && !portable_mobile_touch_take_driving_pressed());
    (void)finger(renderer,SDL_EVENT_FINGER_UP,4,1,0.0f,0.0f);
    CHECK(portable_mobile_touch_driving()==PORTABLE_INPUT_HOP && !portable_mobile_touch_take_driving_pressed());
    (void)finger(renderer,SDL_EVENT_FINGER_CANCELED,4,2,0.0f,0.0f);
    CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_take_driving_pressed());
    portable_mobile_touch_clear();
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,4,3,PORTABLE_INPUT_ITEM);
    (void)finger(renderer,SDL_EVENT_FINGER_CANCELED,4,3,0.0f,0.0f);
    CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_take_driving_pressed());
    /* Independent queued buttons survive cancellation of only one finger. */
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,4,4,PORTABLE_INPUT_ITEM);
    (void)hit(renderer,SDL_EVENT_FINGER_DOWN,4,5,PORTABLE_INPUT_HOP);
    (void)finger(renderer,SDL_EVENT_FINGER_CANCELED,4,4,0.0f,0.0f);
    CHECK(portable_mobile_touch_take_driving_pressed()==PORTABLE_INPUT_HOP);
    portable_mobile_touch_clear();
    for(unsigned cancel=0u;cancel<5u;++cancel){
        portable_mobile_touch_configure(1);
        (void)hit(renderer,SDL_EVENT_FINGER_DOWN,5,1,PORTABLE_INPUT_ITEM);
        (void)finger(renderer,SDL_EVENT_FINGER_UP,5,1,0.0f,0.0f);
        if(cancel==0u)portable_mobile_touch_clear();
        else if(cancel==1u)portable_mobile_touch_configure(0);
        else{
            SDL_Event event={.type=cancel==2u?SDL_EVENT_WINDOW_FOCUS_LOST:
                cancel==3u?SDL_EVENT_WILL_ENTER_BACKGROUND:SDL_EVENT_DID_ENTER_BACKGROUND};
            (void)portable_mobile_touch_event(&event,renderer);
        }
        CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_take_driving_pressed());
    }
    portable_mobile_touch_configure(0);
}

static void input_cases(SDL_Renderer *renderer)
{
    portable_mobile_touch_configure(0);
    CHECK(!hit(renderer,SDL_EVENT_FINGER_DOWN,1,1,PORTABLE_INPUT_ACCELERATE));
    CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_frontend() && !portable_mobile_touch_take_pressed());
    portable_mobile_touch_configure(1);
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,1,1,PORTABLE_INPUT_ACCELERATE)==PORTABLE_FRONTEND_BUTTON_A);
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,1,2,PORTABLE_INPUT_LEFT)==PORTABLE_FRONTEND_BUTTON_LEFT);
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,1,3,PORTABLE_INPUT_HOP)==PORTABLE_FRONTEND_BUTTON_R);
    CHECK(portable_mobile_touch_driving()==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_LEFT|PORTABLE_INPUT_HOP));
    CHECK(portable_mobile_touch_frontend()==(PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_LEFT|PORTABLE_FRONTEND_BUTTON_R));
    CHECK(portable_mobile_touch_take_pressed()==portable_mobile_touch_frontend());
    CHECK(!portable_mobile_touch_take_pressed());
    CHECK(hit(renderer,SDL_EVENT_FINGER_MOTION,1,1,PORTABLE_INPUT_BRAKE)==PORTABLE_FRONTEND_BUTTON_B);
    CHECK(portable_mobile_touch_driving()==(PORTABLE_INPUT_BRAKE|PORTABLE_INPUT_LEFT|PORTABLE_INPUT_HOP));
    portable_mobile_touch_clear();
    /* A second finger holding A never produces a second edge or releases the
     * first finger's hold. A quick tap edge remains available after both ups. */
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,1,1,PORTABLE_INPUT_ACCELERATE)==PORTABLE_FRONTEND_BUTTON_A);
    CHECK(!hit(renderer,SDL_EVENT_FINGER_DOWN,1,2,PORTABLE_INPUT_ACCELERATE));
    CHECK(!finger(renderer,SDL_EVENT_FINGER_UP,1,1,NAN,NAN));
    CHECK(portable_mobile_touch_driving()==PORTABLE_INPUT_ACCELERATE);
    CHECK(!finger(renderer,SDL_EVENT_FINGER_UP,1,2,0.0f,0.0f));
    CHECK(!portable_mobile_touch_driving() && portable_mobile_touch_take_pressed()==PORTABLE_FRONTEND_BUTTON_A);
    CHECK(!portable_mobile_touch_take_pressed());
    /* Devices may reuse the same finger ID. */
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,10,7,PORTABLE_INPUT_ACCELERATE)==PORTABLE_FRONTEND_BUTTON_A);
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,11,7,PORTABLE_INPUT_LEFT)==PORTABLE_FRONTEND_BUTTON_LEFT);
    CHECK(!finger(renderer,SDL_EVENT_FINGER_UP,10,7,0.0f,0.0f));
    CHECK(portable_mobile_touch_driving()==PORTABLE_INPUT_LEFT);
    CHECK(!finger(renderer,SDL_EVENT_FINGER_CANCELED,11,7,0.0f,0.0f));
    CHECK(!portable_mobile_touch_driving());portable_mobile_touch_clear();
    CHECK(!hit(renderer,SDL_EVENT_FINGER_MOTION,2,99,PORTABLE_INPUT_ACCELERATE));
    CHECK(!portable_mobile_touch_driving());
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,2,1,PORTABLE_INPUT_ACCELERATE)==PORTABLE_FRONTEND_BUTTON_A);
    const float outside[4][2]={{-0.1f,0.5f},{1.1f,0.5f},{NAN,0.5f},{0.5f,INFINITY}};
    for(unsigned i=0u;i<4u;++i){
        CHECK(!finger(renderer,SDL_EVENT_FINGER_MOTION,2,1,outside[i][0],outside[i][1]));
        CHECK(!portable_mobile_touch_driving());
        CHECK(hit(renderer,SDL_EVENT_FINGER_MOTION,2,1,PORTABLE_INPUT_ACCELERATE)==PORTABLE_FRONTEND_BUTTON_A);
    }
    CHECK(!finger(renderer,SDL_EVENT_FINGER_MOTION,2,1,0.5f,0.2f)); /* Valid empty part of the screen. */
    CHECK(!portable_mobile_touch_driving());portable_mobile_touch_clear();
    for(unsigned i=0u;i<PORTABLE_MOBILE_TOUCH_FINGERS;++i)
        (void)hit(renderer,SDL_EVENT_FINGER_DOWN,1,(SDL_FingerID)(i+1u),PORTABLE_INPUT_ACCELERATE);
    CHECK(!hit(renderer,SDL_EVENT_FINGER_DOWN,1,99,PORTABLE_INPUT_LEFT));
    CHECK(portable_mobile_touch_driving()==PORTABLE_INPUT_ACCELERATE);
    (void)finger(renderer,SDL_EVENT_FINGER_UP,1,1,0.0f,0.0f);
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,1,99,PORTABLE_INPUT_LEFT)==PORTABLE_FRONTEND_BUTTON_LEFT);
    CHECK(portable_mobile_touch_driving()==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_LEFT));
    const SDL_EventType interruptions[]={SDL_EVENT_WINDOW_FOCUS_LOST,SDL_EVENT_WILL_ENTER_BACKGROUND,SDL_EVENT_DID_ENTER_BACKGROUND};
    for(unsigned i=0u;i<3u;++i){
        SDL_Event event={.type=(Uint32)interruptions[i]};
        (void)portable_mobile_touch_event(&event,renderer);
        CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_frontend() && !portable_mobile_touch_take_pressed());
        (void)hit(renderer,SDL_EVENT_FINGER_DOWN,1,1,PORTABLE_INPUT_ACCELERATE);
    }
    portable_mobile_touch_configure(0);portable_mobile_touch_configure(1);
    CHECK(!portable_mobile_touch_driving() && !portable_mobile_touch_take_pressed());
    CHECK(!hit(renderer,SDL_EVENT_FINGER_MOTION,1,1,PORTABLE_INPUT_ACCELERATE));
    portable_mobile_touch_clear();
}

static void read_pixels(SDL_Renderer *renderer,uint32_t *pixels,int width,int height)
{
    SDL_Surface *surface=SDL_RenderReadPixels(renderer,NULL);CHECK(surface!=NULL);
    CHECK(surface->w==width && surface->h==height);
    CHECK(SDL_ConvertPixels(width,height,surface->format,surface->pixels,surface->pitch,
        SDL_PIXELFORMAT_ARGB8888,pixels,width*(int)sizeof *pixels));
    SDL_DestroySurface(surface);
}

static void draw_scene(SDL_Renderer *renderer,SDL_Texture *background)
{
    CHECK(SDL_SetRenderDrawColor(renderer,0u,0u,0u,255u) && SDL_RenderClear(renderer));
    CHECK(SDL_RenderTexture(renderer,background,NULL,NULL));
    CHECK(portable_mobile_touch_draw(renderer));
}

static void layout_and_draw(int width,int height)
{
    const size_t stride=(size_t)width+5u,count=stride*(size_t)height,packed_count=(size_t)width*(size_t)height;
    uint32_t *source=malloc(count*sizeof *source),*saved=malloc(count*sizeof *saved),
        *idle=malloc(packed_count*sizeof *idle),*held=malloc(packed_count*sizeof *held);
    CHECK(source && saved && idle && held);
    for(int y=0;y<height;++y)for(size_t x=0u;x<stride;++x)
        source[(size_t)y*stride+x]=x<(size_t)width?UINT32_C(0xff123456):UINT32_C(0xdeadbeef);
    memcpy(saved,source,count*sizeof *source);
    SDL_Window *window=SDL_CreateWindow("Touch controls",width,height,SDL_WINDOW_HIDDEN);CHECK(window!=NULL);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,"software");CHECK(renderer!=NULL);
    SDL_Texture *background=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,width,height);
    CHECK(background!=NULL && SDL_UpdateTexture(background,NULL,source,(int)(stride*sizeof *source)));
    PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
    CHECK(portable_mobile_touch_layout(width,height,NULL,0u)==PORTABLE_MOBILE_TOUCH_BUTTONS);
    CHECK(portable_mobile_touch_layout(width,height,buttons,PORTABLE_MOBILE_TOUCH_BUTTONS)==PORTABLE_MOBILE_TOUCH_BUTTONS);
    for(unsigned i=0u;i<PORTABLE_MOBILE_TOUCH_BUTTONS;++i){
        CHECK(isfinite(buttons[i].x) && isfinite(buttons[i].y) && buttons[i].radius>0.0f && buttons[i].label!=NULL);
        CHECK(buttons[i].x-buttons[i].radius>=0.0f && buttons[i].x+buttons[i].radius<=(float)width);
        CHECK(buttons[i].y-buttons[i].radius>=0.0f && buttons[i].y+buttons[i].radius<=(float)height);
    }
    if((width==960 && height==640) || (width==1492 && height==640) || (width==2560 && height==1440)){
        const int factor=width==2560?8:4;
        PortableMobileTouchButton native[PORTABLE_MOBILE_TOUCH_BUTTONS];
        CHECK(portable_mobile_touch_layout(width/factor,height/factor,native,PORTABLE_MOBILE_TOUCH_BUTTONS)==PORTABLE_MOBILE_TOUCH_BUTTONS);
        for(unsigned i=0u;i<PORTABLE_MOBILE_TOUCH_BUTTONS;++i){
            CHECK(fabsf(buttons[i].x-native[i].x*(float)factor)<0.03f);
            CHECK(fabsf(buttons[i].y-native[i].y*(float)factor)<0.03f);
            CHECK(fabsf(buttons[i].radius-native[i].radius*(float)factor)<0.03f);
            CHECK(buttons[i].driving_mask==native[i].driving_mask && buttons[i].frontend_mask==native[i].frontend_mask);
        }
    }
    portable_mobile_touch_configure(0);CHECK(portable_mobile_touch_draw(NULL));
    draw_scene(renderer,background);read_pixels(renderer,idle,width,height);
    for(size_t i=0u;i<packed_count;++i)CHECK(idle[i]==UINT32_C(0xff123456));
    portable_mobile_touch_configure(1);CHECK(!portable_mobile_touch_draw(NULL));
    draw_scene(renderer,background);read_pixels(renderer,idle,width,height);
    size_t changed=0u;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const uint32_t pixel=idle[(size_t)y*(size_t)width+(size_t)x];
        changed+=pixel!=UINT32_C(0xff123456);
        if(y<height/4)CHECK(pixel==UINT32_C(0xff123456));
    }
    CHECK(changed>0u);
    CHECK(hit(renderer,SDL_EVENT_FINGER_DOWN,1,1,PORTABLE_INPUT_ACCELERATE)==PORTABLE_FRONTEND_BUTTON_A);
    draw_scene(renderer,background);read_pixels(renderer,held,width,height);
    CHECK(memcmp(idle,held,packed_count*sizeof *held)!=0);
    draw_scene(renderer,background);read_pixels(renderer,idle,width,height);
    CHECK(!memcmp(idle,held,packed_count*sizeof *held)); /* Held cache is deterministic, not cumulative blending. */
    CHECK(portable_mobile_touch_take_pressed()==PORTABLE_FRONTEND_BUTTON_A); /* Drawing does not consume input. */
    CHECK(portable_mobile_touch_driving()==PORTABLE_INPUT_ACCELERATE && !memcmp(source,saved,count*sizeof *source));
    portable_mobile_touch_configure(0);draw_scene(renderer,background);read_pixels(renderer,idle,width,height);
    for(size_t i=0u;i<packed_count;++i)CHECK(idle[i]==UINT32_C(0xff123456));
    if(width==240 && height==160){input_cases(renderer);driving_edge_cases(renderer);}
    portable_mobile_touch_close();SDL_DestroyTexture(background);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    free(held);free(idle);free(saved);free(source);
}

static void lifecycle_cases(void)
{
    _Static_assert(SDL_SCANCODE_AC_BACK==282, "Android Back scancode changed");
    SDL_Event back={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=SDL_SCANCODE_AC_BACK}};
    CHECK(portable_mobile_back(&back));back.key.repeat=true;CHECK(!portable_mobile_back(&back));
    back.key.repeat=false;back.type=SDL_EVENT_KEY_UP;CHECK(!portable_mobile_back(&back));
    back.type=SDL_EVENT_KEY_DOWN;back.key.scancode=SDL_SCANCODE_ESCAPE;CHECK(!portable_mobile_back(&back));
    CHECK(!portable_mobile_back(NULL));
    portable_mobile_lifecycle_start();portable_mobile_lifecycle_start();
    (void)portable_mobile_lifecycle_take();
    const SDL_EventType events[]={SDL_EVENT_WILL_ENTER_BACKGROUND,SDL_EVENT_DID_ENTER_FOREGROUND,SDL_EVENT_TERMINATING};
    for(unsigned i=0u;i<3u;++i){SDL_Event event={.type=(Uint32)events[i]};CHECK(SDL_PushEvent(&event));}
    CHECK(portable_mobile_lifecycle_take()==(PORTABLE_MOBILE_BACKGROUND|PORTABLE_MOBILE_FOREGROUND|PORTABLE_MOBILE_TERMINATING));
    CHECK(!portable_mobile_lifecycle_take());portable_mobile_lifecycle_stop();
    SDL_Event event={.type=SDL_EVENT_WILL_ENTER_BACKGROUND};CHECK(SDL_PushEvent(&event));
    CHECK(!portable_mobile_lifecycle_take());portable_mobile_lifecycle_stop();
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    PortableMobileTouchButton canary[2],before[2];memset(canary,0x5a,sizeof canary);memcpy(before,canary,sizeof before);
    CHECK(!portable_mobile_touch_layout(0,160,canary,2u) && !memcmp(canary,before,sizeof canary));
    CHECK(portable_mobile_touch_layout(240,160,canary,1u)==PORTABLE_MOBILE_TOUCH_BUTTONS &&
        !memcmp(&canary[1],&before[1],sizeof canary[1]));
    const int sizes[][2]={{240,160},{373,160},{240,320},{960,640},{1492,640},{2560,1440},{5120,320}};
    for(unsigned i=0u;i<sizeof sizes/sizeof sizes[0];++i)layout_and_draw(sizes[i][0],sizes[i][1]);
    lifecycle_cases();portable_mobile_touch_configure(0);portable_mobile_touch_close();SDL_Quit();
    puts("mobile touch: PASS");return 0;
}
