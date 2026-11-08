#include "software_frame.h"
#include "../core/text_glyph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int portable_screen_capture_drift(const PortableSoftwareFrame *frame,const char *mode,
    const PortableModernDriftState *drift)
{
    const char *directory=getenv("MKSC_SCREEN_DIR");
    if(directory==NULL || *directory=='\0')return 1;
    if(mode==NULL || drift==NULL)return 0;
    if(!drift->charge_tier && !drift->last_applied_tier)return 1;
    char name[48];
    const int count=snprintf(name,sizeof name,"%s-drift-%s-%u",mode,
        drift->last_applied_tier?"boost":"charge",
        drift->last_applied_tier?drift->last_applied_tier:drift->charge_tier);
    return count>0 && (size_t)count<sizeof name && portable_screen_capture(frame,name);
}

int portable_screen_capture_air(const PortableSoftwareFrame *frame,const char *mode,
    const PortableModernAirState *air)
{
    const char *directory=getenv("MKSC_SCREEN_DIR");
    if(directory==NULL || *directory=='\0')return 1;
    if(mode==NULL || air==NULL)return 0;
    if(!air->armed && !air->tricked && !air->last_applied)return 1;
    char name[48];
    const int count=snprintf(name,sizeof name,"%s-air-%s",mode,
        air->last_applied?"boost":air->tricked?"trick":"ready");
    return count>0 && (size_t)count<sizeof name && portable_screen_capture(frame,name);
}

int portable_software_frame_init(PortableSoftwareFrame *frame,
                                 uint32_t *pixels,
                                 size_t width,
                                 size_t height)
{
    if (frame == NULL || pixels == NULL || width == 0 || height == 0 ||
        height > SIZE_MAX / width)
        return 0;
    frame->pixels = pixels;
    frame->width = width;
    frame->height = height;
    frame->ui_text = NULL;
    frame->scale = 1u;
    frame->stride = 0u;
    frame->artwork_lod = 0u;
    frame->depth = NULL;
    return 1;
}

void portable_software_frame_clear(PortableSoftwareFrame *frame,
                                   uint32_t color)
{
    if(frame==NULL||frame->pixels==NULL)return;
    const size_t scale=portable_software_frame_scale(frame);
    const size_t width=frame->width*scale,stride=portable_software_frame_stride(frame);
    for(size_t y=0u;y<frame->height*scale;++y){
        uint32_t *row=frame->pixels+y*stride;
        for(size_t x=0u;x<width;++x)row[x]=color;
    }
}

void portable_software_frame_rect(PortableSoftwareFrame *frame,
                                  size_t x,
                                  size_t y,
                                  size_t width,
                                  size_t height,
                                  uint32_t color)
{
    size_t end_x;
    size_t end_y;

    if (frame == NULL || frame->pixels == NULL || x >= frame->width ||
        y >= frame->height)
        return;
    if (width > frame->width - x)
        end_x = frame->width;
    else
        end_x = x + width;
    if (height > frame->height - y)
        end_y = frame->height;
    else
        end_y = y + height;
    for (size_t row = y; row < end_y; ++row)
    {
        for (size_t column = x; column < end_x; ++column)
            portable_software_frame_store(frame,row * frame->width + column,color);
    }
}

uint32_t portable_software_frame_checksum(const PortableSoftwareFrame *frame)
{
    uint32_t hash = UINT32_C(2166136261);
    size_t count;

    if (frame == NULL || frame->pixels == NULL)
        return 0;
    count = portable_software_frame_pixel_count(frame);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t pixel = frame->pixels[portable_software_frame_physical_index(frame,i)];

        hash ^= pixel;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

int portable_software_frame_write_ppm(const PortableSoftwareFrame *frame,
                                      const char *path)
{
    FILE *file;

    if (frame == NULL || frame->pixels == NULL || path == NULL)
        return 0;
    const size_t scale=portable_software_frame_scale(frame);
    const size_t width=frame->width*scale,height=frame->height*scale;
    const size_t stride=portable_software_frame_stride(frame);
    file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (fprintf(file, "P6\n%zu %zu\n255\n", width, height) < 0)
    {
        fclose(file);
        return 0;
    }
    /* Bound conversion storage independently of output resolution, and avoid
     * one CRT stream lock/write per physical pixel on captured wide frames. */
    enum { CHUNK_PIXELS=1024 };
    unsigned char rgb[CHUNK_PIXELS*3u];
    for(size_t y=0;y<height;++y){
        const uint32_t *row=frame->pixels+y*stride;
        for(size_t x=0;x<width;){
            const size_t count=width-x<CHUNK_PIXELS?width-x:CHUNK_PIXELS;
            for(size_t i=0;i<count;++i){
                const uint32_t pixel=row[x+i];
                rgb[i*3u]=(unsigned char)(pixel>>16u);
                rgb[i*3u+1u]=(unsigned char)(pixel>>8u);
                rgb[i*3u+2u]=(unsigned char)pixel;
            }
            if(fwrite(rgb,3u,count,file)!=count){
                fclose(file);
                return 0;
            }
            x+=count;
        }
    }
    return fclose(file) == 0;
}

void portable_software_frame_text(PortableSoftwareFrame *frame,
                  int x,
                  int y,
                  const char *text,
                  uint32_t color)
{

    text=portable_ui_text_resolve(frame!=NULL?frame->ui_text:NULL,text);
    if(text==NULL)return;
    for (; *text != '\0'; ++text, x += 4)
    {
        const uint16_t glyph = portable_text_glyph(*text);
        for (int row = 0; row < 5; ++row)
            for (int column = 0; column < 3; ++column)
                if ((glyph & (1u << (14 - row * 3 - column))) != 0 &&
                    x + column >= 0 && y + row >= 0)
                    portable_software_frame_rect(
                        frame, (size_t)(x + column), (size_t)(y + row),
                        1, 1, color);
    }
}

void portable_software_frame_copy_native(const PortableSoftwareFrame *frame,uint32_t *pixels)
{
    const PortableSoftwareFrame view=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    const size_t scale=portable_software_frame_scale(&view),row_pixels=portable_software_frame_stride(&view)*scale;
    for(size_t y=0u;y<view.height;++y){
        const uint32_t *source=view.pixels+y*row_pixels;
        uint32_t *target=pixels+y*view.width;
        for(size_t x=0u;x<view.width;++x)target[x]=source[x*scale];
    }
}
void portable_software_frame_add_half(PortableSoftwareFrame *frame,size_t index,uint32_t color)
{
    const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
    const size_t first=(index/frame->width)*stride*scale+(index%frame->width)*scale;
    for(size_t y=0u;y<scale;++y)for(size_t x=0u;x<scale;++x){
        uint32_t *pixel=&frame->pixels[first+y*stride+x],result=0u;
        for(unsigned shift=0u;shift<=16u;shift+=8u){
            unsigned channel=((*pixel>>shift)&255u)+((color>>shift)&255u)/2u;
            result|=(channel>255u?255u:channel)<<shift;
        }
        if(frame->depth!=NULL && frame->depth->value!=0u)
            portable_software_frame_store_physical(frame,index%frame->width*scale+x,
                index/frame->width*scale+y,result|(color&UINT32_C(0xff000000)));
        else *pixel=result;
    }
}

#ifdef PORTABLE_ENABLE_SDL3
#include "mobile_touch.h"
#include <SDL3/SDL.h>
void portable_software_render_target_close(PortableSoftwareRenderTarget *target)
{
    if(target==NULL)return;
    SDL_DestroyTexture(target->texture);free(target->frame.pixels);
    memset(target,0,sizeof *target);
}

int portable_software_render_target_resize(void *renderer,
    PortableSoftwareRenderTarget *target,size_t width,size_t height,uint8_t scale,int linear)
{
    if(renderer==NULL||target==NULL||width==0u||width>2048u||height==0u||height>640u||scale>8u)return 0;
    if(scale==0u)scale=1u;
    const SDL_ScaleMode filter=linear?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST;
    if(target->texture!=NULL&&target->frame.width==width&&target->frame.height==height&&
       target->frame.scale==scale)return SDL_SetTextureScaleMode(target->texture,filter);
    PortableSoftwareFrame frame={.width=width,.height=height,.scale=scale,.ui_text=target->frame.ui_text,
        .artwork_lod=target->frame.artwork_lod};
    frame.pixels=calloc(portable_software_frame_pixel_count(&frame),sizeof *frame.pixels);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,(int)(width*scale),(int)(height*scale));
    if(frame.pixels==NULL||texture==NULL||!SDL_SetTextureScaleMode(texture,filter)){
        free(frame.pixels);SDL_DestroyTexture(texture);return 0;
    }
    portable_software_render_target_close(target);
    target->frame=frame;target->texture=texture;return 1;
}
size_t portable_software_frame_window_width(void *renderer,uint16_t aspect_width)
{
    if(aspect_width!=0u)return aspect_width;
    int width=240,height=160;
    if(!SDL_GetCurrentRenderOutputSize(renderer,&width,&height)||height<=0)return 240u;
    size_t logical=(size_t)(((uint64_t)(unsigned)width*160u+(unsigned)height/2u)/(unsigned)height);
    return logical<240u?240u:logical>1024u?1024u:logical;
}
int portable_software_frame_present(void *renderer,void *texture,
                                    PortableSoftwareFrame *frame,int fit)
{
    const int source_width=(int)(frame->width*portable_software_frame_scale(frame));
    const int source_height=(int)(frame->height*portable_software_frame_scale(frame));
    int width,height;
    const size_t stride=portable_software_frame_stride(frame);
    for(int y=0;y<source_height;++y){
        uint32_t *row=frame->pixels+(size_t)y*stride;
        for(int x=0;x<source_width;++x)row[x]|=UINT32_C(0xff000000);
    }
    if(!SDL_UpdateTexture(texture,NULL,frame->pixels,(int)portable_software_frame_stride(frame)*4)||
       !SDL_GetCurrentRenderOutputSize(renderer,&width,&height))return 0;
    double scale=(double)width/source_width;
    if((double)height/source_height<scale)scale=(double)height/source_height;
    /* Integer scaling is measured in logical art pixels. Increasing internal
     * sampling resolution must not shrink the picture inside the same window. */
    const double sampling=(double)portable_software_frame_scale(frame);
    if(!fit&&scale*sampling>=1.0)scale=(int)(scale*sampling)/sampling;
    SDL_FRect viewport={(float)((width-source_width*scale)/2.0),
        (float)((height-source_height*scale)/2.0),
        (float)(source_width*scale),(float)(source_height*scale)};
    if(!fit&&scale*sampling>=1.0){
        viewport.x=(float)((width-(int)viewport.w)/2);
        viewport.y=(float)((height-(int)viewport.h)/2);
    }
    if(!SDL_SetRenderDrawColor(renderer,0,0,0,255)||!SDL_RenderClear(renderer)||
       !SDL_RenderTexture(renderer,texture,NULL,&viewport)||!portable_mobile_touch_draw(renderer))return 0;
    /* A window-backed retained target is an offscreen draw, not a swapchain.
     * Windowless SDL_GPU still needs Present to submit its command buffer. */
    if(SDL_GetRenderTarget(renderer)!=NULL&&SDL_GetRenderWindow(renderer)!=NULL)
        return SDL_FlushRenderer(renderer);
    return SDL_RenderPresent(renderer);
}
#endif

PortableSoftwareFrame portable_software_frame_native_view(const PortableSoftwareFrame *frame,PortableFrameAnchor anchor)
{
    PortableSoftwareFrame view=*frame;
    view.depth=NULL;
    if(view.width<=240u)return view;
    const size_t margin=view.width-240u;
    const size_t offset=anchor==PORTABLE_FRAME_RIGHT?margin:anchor==PORTABLE_FRAME_CENTER?margin/2u:0u;
    view.pixels+=offset*portable_software_frame_scale(frame);
    view.stride=portable_software_frame_stride(frame);
    view.width=240u;
    return view;
}

typedef struct {
    char (*names)[24];
    size_t count,capacity;
} ScreenCaptures;
static ScreenCaptures screen_captures;

void portable_screen_capture_reset(void)
{
    free(screen_captures.names);
    screen_captures=(ScreenCaptures){0};
}

int portable_screen_capture(const PortableSoftwareFrame *frame,const char *name)
{
    static int cleanup_registered;
    const char *directory=getenv("MKSC_SCREEN_DIR");
    if(directory==NULL||directory[0]=='\0'||frame==NULL||name==NULL||strlen(name)>=24u)return 1;
    for(size_t index=0u;index<screen_captures.count;++index)
        if(strcmp(screen_captures.names[index],name)==0)return 1;
    if(!cleanup_registered){
        if(atexit(portable_screen_capture_reset)!=0){
            fprintf(stderr,"screen capture cleanup registration failed at %s\n",name);return 0;
        }
        cleanup_registered=1;
    }
    if(screen_captures.count==screen_captures.capacity){
        const size_t limit=SIZE_MAX/sizeof *screen_captures.names;
        if(screen_captures.capacity>limit/2u){
            fprintf(stderr,"screen capture registry size overflow at %s\n",name);return 0;
        }
        const size_t capacity=screen_captures.capacity?screen_captures.capacity*2u:64u;
        void *grown=realloc(screen_captures.names,capacity*sizeof *screen_captures.names);
        if(grown==NULL){fprintf(stderr,"screen capture registry allocation failed at %s\n",name);return 0;}
        screen_captures.names=grown;screen_captures.capacity=capacity;
    }
    char path[4096];
    if(snprintf(path,sizeof path,"%s/%s.ppm",directory,name)>=(int)sizeof path||
       !portable_software_frame_write_ppm(frame,path))return 0;
    memcpy(screen_captures.names[screen_captures.count++],name,strlen(name)+1u);
    printf("Screen capture: %s\n",name);
    return 1;
}

int portable_race_motion_capture(const PortableSoftwareFrame *frame,const char *mode,uint32_t tick)
{
    const char *directory=getenv("MKSC_MOTION_DIR");
    if(!directory || !*directory || tick<512u || tick>=528u)return 1;
    char path[4096];
    if(!frame || !mode || snprintf(path,sizeof path,"%s/%s-motion-%02u.ppm",directory,mode,tick-512u)>=(int)sizeof path ||
       !portable_software_frame_write_ppm(frame,path))return 0;
    if(tick==527u)printf("Race motion capture: %s frames=16 scale=%zu width=%zu\n",mode,
        portable_software_frame_scale(frame),frame->width);
    return 1;
}

int portable_race_hop_capture(const PortableSoftwareFrame *frame, const char *mode,
                              int32_t height_q16)
{
    const char *directory = getenv("MKSC_SCREEN_DIR");
    if (directory == NULL || !*directory || height_q16 <= 0)
        return 1;
    if (mode == NULL)
        return 0;
    unsigned band = (uint32_t)height_q16 / 65536u;
    if (band > 7u)
        band = 7u;
    char name[32];
    const int length = snprintf(name, sizeof name, "%s-hop-%u", mode, band);
    return length >= 0 && (size_t)length < sizeof name &&
           portable_screen_capture(frame, name);
}
