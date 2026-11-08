#include "race_scene_renderer.h"
#include "artwork_lod.h"
#include <stdlib.h>

#include "camera_affine.h"
#include "battle_balloon.h"
#include "camera_projection.h"
#include "camera_state.h"
#include "banana_renderer.h"
#include "boo_attack_renderer.h"
#include "character_renderer.h"
#include "race_particle_renderer.h"
#include "frame_scheduler.h"
#include "green_shell_renderer.h"
#include "hud_renderer.h"
#include "item_box_renderer.h"
#include "lightning_effect_renderer.h"
#include "gp_finish_banner_renderer.h"
#include "gp_results_renderer.h"
#include "red_shell_renderer.h"
#include "sky_renderer.h"
#include "race_ground_3d.h"
#include "model_renderer.h"

#include <stdio.h>
#include <string.h>

/* CC80 adds one frame to the even direction slot. This temporary table
 * belongs to a single draw; the imported asset and simulation stay immutable. */
static int bomb_walk_view(PortableCharacterAssetView *view,uint8_t slots[64],
    uint8_t phase,int32_t speed,uint8_t angle,uint16_t response)
{
    if (view->direction_tiles==NULL || view->view_frame_slots==NULL ||
        view->view_frame_slot_count!=64u) return 0;
    const uint8_t alternate=portable_battle_bomb_walk_frame(phase,speed,angle,response);
    for (size_t i=0u;i<64u;++i) {
        const unsigned base=view->view_frame_slots[i];
        if ((base&1u)!=0u || base+1u>=view->direction_frame_count) return 0;
        slots[i]=(uint8_t)(base+alternate);
    }
    view->view_frame_slots=slots;
    return 1;
}


static uint32_t scenery_rgb(uint16_t color)
{
    const uint32_t r=color&31u,g=(color>>5u)&31u,b=(color>>10u)&31u;
    return (((r<<3u)|(r>>2u))<<16u)|
           (((g<<3u)|(g>>2u))<<8u)|((b<<3u)|(b>>2u));
}

/* 49B0C caches actor positions before main_vblankHandler uploads them;
 * only afterward does 51F84/312E8 publish the next camera offset. Visible
 * actors therefore share the preceding draw boundary with the road. */
static int setup_presentation_camera(PortableCameraState *camera,
    const PortableRacePresentationState *state, unsigned scale)
{
    portable_camera_state_update(camera,
        state->camera_position_override ? state->camera_position_x_q16 : state->position_x_q16,
        state->camera_position_override ? state->camera_position_y_q16 : state->position_y_q16,
        state->camera_yaw);
    const PortableSkyMotionSnapshot *sky=&state->sky_motion;
    if(!sky->initialized)return 1;
    if(sky->road_line<0 || sky->road_line>160)return 0;
    const int first_row=portable_sky_motion_road_first_row(sky,scale);
    if(first_row<0)return 0;
    const int native_line=sky->draw_road_line_valid?sky->draw_road_line:sky->road_line;
    camera->projection_y_offset=(int8_t)(native_line-74);
    if(scale>1u && sky->precise && sky->draw_road_line_valid){
        /* Match the road's snapped physical edge, not a separately rounded
         * actor offset. Ceiling also preserves a whole physical row at3x. */
        const int64_t numerator=((int64_t)first_row-74*(int64_t)scale)*65536;
        camera->projection_y_offset_q16=(int32_t)(numerator>=0?
            (numerator+scale-1u)/scale:-((-numerator)/scale));
        camera->projection_y_offset_precise=1u;
    }
    return 1;
}

int portable_race_scene_player_projection(
    const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,
    int16_t *out_x,int16_t *out_y,
    uint16_t *out_scale)
{
    PortableCameraState camera;
    PortableCameraProjection projection;
    PortableCameraProjectionResult kart={0};
    unsigned sine_index;
    uint8_t lod;
    if(scene==NULL||state==NULL||out_x==NULL||out_y==NULL||
       out_scale==NULL||
       scene->sine==NULL||scene->sine_count<1024u)
        return 0;
    sine_index=((int16_t)state->camera_yaw/64)&1023u;
    if(!setup_presentation_camera(&camera,state,1u))return 0;
    if(portable_camera_projection_build(
           &camera,scene->sine[sine_index],
           scene->sine[(256u-sine_index)&1023u],31414,9319,
           &projection)||
       portable_camera_projection_project(
           &camera,&projection,state->position_x_q16,
           state->position_y_q16,0,&kart))
        return 0;
    lod=kart.distance<512?64u:kart.distance<1024?32u:16u;
    if(scene->driver_projection!=NULL){
        int found=0;
        for(unsigned i=0u;i<PORTABLE_DRIVER_PROJECTION_LODS;++i)
            if((uint16_t)kart.distance<=scene->driver_projection->rows[i].max_distance){
                lod=(uint8_t)scene->driver_projection->rows[i].size;found=1;break;
            }
        if(!found||portable_camera_projection_body(&kart,state->height_q16,&kart))return 0;
    }
    if(lod!=16u&&lod!=32u&&lod!=64u)return 0;
    *out_x=kart.x;
    *out_y=kart.y;
    *out_scale=(uint16_t)((uint32_t)
        ((state->character_affine_scale?state->character_affine_scale:256u)/(64u/lod))*
        (uint16_t)kart.distance/256u);
    return 1;
}

/* Resolution is independent of the source cell's anchor, inverse axes and
 * animation frame. A larger texture samples the same normalized rectangle. */
static const PortableNativeImage *scenery_texture(const PortableCourseSceneryArt *art,
    unsigned lod,const PortableSoftwareFrame *frame,int64_t ax_q16,int64_t ay_q16)
{
    const PortableNativeImage *source=&art->lod[lod];
    if(!frame->artwork_lod)return source;
    unsigned sizes[4];
    for(unsigned i=0;i<4u;++i){
        const PortableNativeImage *image=&art->lod[i];
        sizes[i]=image->pixels&&image->width&&image->height?
            (image->width>image->height?image->width:image->height):0u;
    }
    const int64_t scale=(int64_t)portable_software_frame_scale(frame);
    const int64_t width=((int64_t)source->width*16777216*scale+ax_q16-1)/ax_q16;
    const int64_t height=((int64_t)source->height*16777216*scale+ay_q16-1)/ay_q16;
    const size_t selected=portable_artwork_lod_select(frame->artwork_lod,sizes,4u,lod,
        (unsigned)(width>height?width:height));
    return selected<4u?&art->lod[selected]:source;
}

static const uint8_t *scenery_tile_texture(const PortableCourseSceneryView *scenery,
    const PortableSoftwareFrame *frame,int affine,int source_size,int *texture_size)
{
    const uint8_t *tiles[4]={scenery->lod8_tiles,scenery->tiles,
        scenery->lod32_tiles,scenery->lod64_tiles};
    unsigned sizes[4];
    for(unsigned i=0;i<4u;++i)sizes[i]=tiles[i]?(8u<<i):0u;
    const size_t source=source_size==8?0u:source_size==16?1u:source_size==32?2u:3u;
    const unsigned footprint=(unsigned)((source_size*256*
        (int)portable_software_frame_scale(frame)+affine-1)/affine);
    const size_t selected=portable_artwork_lod_select(frame->artwork_lod,sizes,4u,source,footprint);
    if(selected==4u)return NULL;
    *texture_size=(int)sizes[selected];
    return tiles[selected];
}

static int render_scenery(const PortableCourseSceneryView *scenery,
                          PortableSoftwareFrame *frame,int origin_x,
                          int origin_y,int affine,int obj_size,
                          const uint8_t *tiles)
{
    if(scenery==NULL||scenery->palette==NULL||
       frame==NULL||frame->pixels==NULL||affine<=0||(!tiles&&!frame->artwork_lod))return 0;
    int texture_size=obj_size;
    if(frame->artwork_lod){
        tiles=scenery_tile_texture(scenery,frame,affine,obj_size,&texture_size);
        if(!tiles)return 0;
    }
    const int doubled_affine_box=
        (obj_size==8||obj_size==16||obj_size==32||obj_size==64)&&affine<256;
    const int output_scale=(int)portable_software_frame_scale(frame);
    const int canvas=doubled_affine_box?obj_size*2:obj_size;
    for(int y=0;y<canvas*output_scale;++y){
        const int sy=obj_size/2+
            (int)portable_software_frame_floor_div((y-canvas/2*output_scale)*affine,256*output_scale);
        const int draw_y=origin_y*output_scale+y;
        if(sy<0||sy>=obj_size||draw_y<0||draw_y>=(int)frame->height*output_scale)continue;
        for(int x=0;x<canvas*output_scale;++x){
            const int sx=obj_size/2+
                (int)portable_software_frame_floor_div((x-canvas/2*output_scale)*affine,256*output_scale);
            const int draw_x=origin_x*output_scale+x;
            if(sx<0||sx>=obj_size||draw_x<0||draw_x>=(int)frame->width*output_scale)
                continue;
            const int tx=frame->artwork_lod?texture_size/2+
                (int)portable_software_frame_floor_div(
                    (int64_t)(x-canvas/2*output_scale)*affine*texture_size,
                    (int64_t)256*output_scale*obj_size):sx;
            const int ty=frame->artwork_lod?texture_size/2+
                (int)portable_software_frame_floor_div(
                    (int64_t)(y-canvas/2*output_scale)*affine*texture_size,
                    (int64_t)256*output_scale*obj_size):sy;
            if(tx<0||ty<0||tx>=texture_size||ty>=texture_size)continue;
            const size_t offset=((size_t)(ty/8)*(size_t)(texture_size/8)+
                (size_t)(tx/8))*32u+(size_t)(ty&7)*4u+(size_t)(tx&7)/2u;
            const uint8_t packed=tiles[offset];
            const uint8_t index=(tx&1)!=0?packed>>4u:packed&15u;
            if(index!=0u)
                portable_software_frame_store_rgb_physical(frame,(size_t)draw_x,(size_t)draw_y, scenery_rgb(scenery->palette[index]));
        }
    }
    return 1;
}

static int render_projected_scenery_transform(
    const PortableRaceSceneView *scene, const PortableCourseSceneryView *scenery,
    const PortableCameraProjectionResult *projected,
    PortableSoftwareFrame *frame,int opening_x_bias,size_t object_index,int flip_y,int affine_x_q8,int affine_y_q8,int flip_x)
{
    if(scenery->native_art!=NULL){
        if(object_index>=scenery->object_count)return 0;
        const size_t index=scenery->objects[object_index].art_index;
        if(index>=scenery->native_art_count)return 0;
        if(projected->x < -64||projected->x>(int)frame->width+64||projected->y<0||
           projected->y>224||projected->distance<=127||
           projected->distance>4096)return 1;
        const unsigned lod=projected->distance<512?0u:
            projected->distance<1024?1u:projected->distance<2048?2u:3u;
        const PortableCourseSceneryArt *art=&scenery->native_art[index];
        const PortableNativeImage *image=&art->lod[lod];
        const int affine=(projected->distance*224/256)>>art->distance_shift[lod];
        if(affine<=0)return 1;
        const int ax=affine*affine_x_q8/256,ay=affine*affine_y_q8/256;
        if(ax<=0 || ay<=0)return 1;
        const PortableNativeImage *texture=scenery_texture(art,lod,frame,
            (int64_t)ax*65536,(int64_t)ay*65536);
        if(!image->width || !image->height)return 0;
        const int output_scale=(int)portable_software_frame_scale(frame);
        const int width=((int)image->width*256*output_scale+(output_scale>1?ax-1:0))/ax;
        const int height=((int)image->height*256*output_scale+(output_scale>1?ay-1:0))/ay;
        const int left=projected->x*output_scale+image->anchor_x*256*output_scale/ax;
        const int top=projected->y*output_scale+image->anchor_y*256*output_scale/ay;
        const int first_x=left<0?-left:0,first_y=top<0?-top:0;
        const int available_x=(int)frame->width*output_scale-left;
        const int available_y=(int)frame->height*output_scale-top;
        const int last_x=available_x<width?available_x:width;
        const int last_y=available_y<height?available_y:height;
        for(int y=first_y;y<last_y;++y){
            const int dy=top+y;
            if(dy<0||dy>=(int)frame->height*output_scale)continue;
            for(int x=first_x;x<last_x;++x){
                const int dx=left+x;
                if(dx<0||dx>=(int)frame->width*output_scale)continue;
                size_t sx=texture==image?(size_t)(x*ax/(256*output_scale)):
                    (size_t)((int64_t)x*ax*texture->width/((int64_t)256*output_scale*image->width));
                size_t sy=texture==image?(size_t)(y*ay/(256*output_scale)):
                    (size_t)((int64_t)y*ay*texture->height/((int64_t)256*output_scale*image->height));
                if(sx>=texture->width || sy>=texture->height)continue;
                if(flip_x)sx=texture->width-1u-sx;
                if(flip_y)sy=texture->height-1u-sy;
                const uint32_t pixel=portable_native_image_pixel_palette_masked(texture,sx,sy,
                    scene->object_palette,scene->object_palette_count,scene->object_palette_dirty);
                if(pixel>>24u)portable_software_frame_store_physical(frame,(size_t)dx,(size_t)dy, pixel);
            }
        }
        return 1;
    }
    if(scenery->mipmap_cells!=NULL){
        /* F674 rejects the unwrapped projection before cell selection. */
        if(projected->x < -64||projected->x>(int)frame->width+64||projected->y<0||
           projected->y>224||projected->distance<=127||
           projected->distance>4096)return 1;
        const unsigned lod=projected->distance<512?0u:
            projected->distance<1024?1u:projected->distance<2048?2u:3u;
        const uint8_t *cell=scenery->mipmap_cells+lod*12u;
        const unsigned a0=(unsigned)cell[2]|((unsigned)cell[3]<<8u);
        const unsigned a1=(unsigned)cell[4]|((unsigned)cell[5]<<8u);
        const int size=8<<(a1>>14u);
        const int half=size/2;
        const int affine=((int)projected->distance*224/256)>>lod;
        const uint8_t *tiles=lod==0u?scenery->lod64_tiles:
            lod==1u?scenery->lod32_tiles:lod==2u?scenery->tiles:
            scenery->lod8_tiles;
        if(affine<=0||(tiles==NULL&&!frame->artwork_lod)){
            fprintf(stderr,"ROM scenery cell lacks art: lod=%u projection=%d,%d,%d\n",
                lod,projected->x,projected->y,projected->distance);
            return 0;
        }
        const unsigned raw_x=a1&511u,raw_y=a0&255u;
        int ox=(int)raw_x,oy=(int)raw_y;
        if(affine!=256){
            /* oam_renderMipmapCellData retains the horizontal negative
             * carry before centre compensation; division truncates to0. */
            const int cx=(raw_x<=255u?(int)raw_x:
                -(int)(raw_x^511u))+half;
            const int cy=(raw_y<128u?(int)raw_y:(int)raw_y-256)+half;
            ox+=cx*256/affine-cx-(affine<256?half:0);
            oy+=cy*256/affine-cy-(affine<256?half:0);
        }
        int x=(projected->x+ox)&511,y=(projected->y+oy)&255;
        if(x>=256)x-=512;
        if(y>=160)y-=256;
        return render_scenery(scenery,frame,x,y,affine,size,tiles);
    }
    const int lod64=scenery->lod64_tiles!=NULL&&
        projected->distance<512;
    const int lod32=scenery->lod32_tiles!=NULL&&!lod64&&
        projected->distance<1024;
    const int lod8=scenery->lod8_tiles!=NULL&&
        projected->distance>=2048;
    const int affine=(int)projected->distance*
        (lod64?896:lod32?448:lod8?112:224)/1024;
    if(affine<=0)return 1;
    const int size=lod64?64:lod32?32:lod8?8:16;
    /* Source OAM doubles affine cells below scale 256, including the
     * 64-pixel near tree at the +612 result boundary. */
    const int lod8_foot=lod8?
        2048/((int)projected->distance*224/1024):0;
    const int doubled_lod=affine<256;
    /* Opening OBJ x uses the source's negative-coordinate carry: at +1237
     * the preceding-camera projection is -3 and OAM x is 502 (-10), one
     * pixel right of the result-scene origin formula. */
    const int x=(lod8?
        projected->x-(affine<256?8:affine==256?4:5):
        projected->x-size/2-(doubled_lod?size/2+(lod64?1:0):1))+
        opening_x_bias;
    const int y=lod8?
        projected->y-(affine<256?8:affine==256?8:4)-
            (affine==256?0:lod8_foot):
        projected->y-size/2-((lod64?30:size/2)*256)/affine-
            (doubled_lod?size/2:0);
    return render_scenery(scenery,frame,x,y,affine,size,
        lod64?scenery->lod64_tiles:
        lod32?scenery->lod32_tiles:
        lod8?scenery->lod8_tiles:scenery->tiles);
}

/* Enhanced scenery keeps its anchor and inverse axes fractional until the
 * physical output pixel. Native1x retains the original cell rounding above. */
static int64_t scenery_floor64(int64_t value,int64_t divisor)
{
    return value>=0?value/divisor:-((-value+divisor-1)/divisor);
}
static int render_scenery_precise(const PortableRaceSceneView *scene,
    const PortableCourseSceneryView *scenery,const PortableCameraProjectionResult *native,
    const PortableCameraProjectionPreciseResult *precise,const PortableCameraState *camera,
    PortableSoftwareFrame *frame,
    int opening_bias,int flip_y,int affine_x,int affine_y,int flip_x)
{
    const int scale=(int)portable_software_frame_scale(frame);
    if(scale==1 || !scenery->native_art)
        return render_projected_scenery_transform(scene,scenery,native,frame,opening_bias,0u,
            flip_y,affine_x,affine_y,flip_x);
    if(!scenery->object_count || !scenery->objects)return 0;
    const size_t index=scenery->objects[0].art_index;
    if(index>=scenery->native_art_count)return 0;
    if(native->distance<=127 || native->distance>4096 || precise->y_q16<0 ||
       precise->y_q16>224*65536 || precise->x_q16<-64*65536 ||
       (int64_t)precise->x_q16>((int64_t)frame->width+64)*65536)return 1;
    const unsigned lod=native->distance<512?0u:native->distance<1024?1u:
        native->distance<2048?2u:3u;
    const PortableCourseSceneryArt *art=&scenery->native_art[index];
    const PortableNativeImage *image=&art->lod[lod];
    const int64_t inverse=(int64_t)precise->distance_q16*224/256/(INT64_C(1)<<art->distance_shift[lod]);
    const int64_t ax=inverse*affine_x/256,ay=inverse*affine_y/256;
    if(ax<=0 || ay<=0)return 1;
    if(!image->width || !image->height)return 0;
    const PortableNativeImage *texture=scenery_texture(art,lod,frame,ax,ay);
    /* Keep the camera's integer physical translation outside the inverse
     * sampler. A Q16 round trip cannot represent one pixel at3x exactly. */
    const int32_t camera_offset=camera->projection_y_offset_precise?
        camera->projection_y_offset_q16:0;
    const int physical_shift=(int)scenery_floor64((int64_t)camera_offset*scale,65536);
    const int64_t base_y_q16=(int64_t)precise->y_q16-camera_offset;
    const int64_t left_q16=(int64_t)precise->x_q16+(int64_t)image->anchor_x*INT64_C(1099511627776)/ax;
    const int64_t top_q16=base_y_q16+(int64_t)image->anchor_y*INT64_C(1099511627776)/ay;
    const int64_t right_q16=left_q16+(int64_t)image->width*INT64_C(1099511627776)/ax;
    const int64_t bottom_q16=top_q16+(int64_t)image->height*INT64_C(1099511627776)/ay;
    int left=(int)scenery_floor64(left_q16*scale,65536)-1;
    int top=(int)scenery_floor64(top_q16*scale,65536)-1+physical_shift;
    int right=(int)scenery_floor64(right_q16*scale,65536)+2;
    int bottom=(int)scenery_floor64(bottom_q16*scale,65536)+2+physical_shift;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>(int)frame->width*scale)right=(int)frame->width*scale;
    if(bottom>(int)frame->height*scale)bottom=(int)frame->height*scale;
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
        const int64_t dx=(int64_t)x*65536/scale-precise->x_q16;
        const int64_t dy=scenery_floor64(((int64_t)y-physical_shift)*65536,scale)-base_y_q16;
        int64_t sx=scenery_floor64(dx*ax,INT64_C(1099511627776))-image->anchor_x;
        int64_t sy=scenery_floor64(dy*ay,INT64_C(1099511627776))-image->anchor_y;
        if(sx<0 || sy<0 || sx>=image->width || sy>=image->height)continue;
        if(texture!=image){
            const int64_t u=scenery_floor64(dx*ax,16777216)-(int64_t)image->anchor_x*65536;
            const int64_t v=scenery_floor64(dy*ay,16777216)-(int64_t)image->anchor_y*65536;
            sx=scenery_floor64(u*texture->width,(int64_t)image->width*65536);
            sy=scenery_floor64(v*texture->height,(int64_t)image->height*65536);
        }
        if(sx<0 || sy<0 || sx>=texture->width || sy>=texture->height)continue;
        if(flip_x)sx=texture->width-1-sx;
        if(flip_y)sy=texture->height-1-sy;
        const uint32_t pixel=portable_native_image_pixel_palette_masked(texture,(size_t)sx,(size_t)sy,
            scene->object_palette,scene->object_palette_count,scene->object_palette_dirty);
        if(pixel>>24u)portable_software_frame_store_physical(frame,(size_t)x,(size_t)y,pixel);
    }
    return 1;
}
static int scenery_projection_precise(const PortableCameraState *camera,
    const PortableCameraProjection *projection,int32_t x,int32_t y,int32_t height,
    PortableCameraProjectionPreciseResult *out)
{
    const int status=portable_camera_projection_project_precise(camera,projection,x,y,0,out);
    return status?status:portable_camera_projection_body_precise(out,height,out);
}

static int render_projected_scenery(const PortableRaceSceneView *scene,const PortableCourseSceneryView *scenery,
    const PortableCameraProjectionResult *projected,PortableSoftwareFrame *frame,
    int opening_x_bias,size_t object_index,int flip_y)
{
    return render_projected_scenery_transform(scene,scenery,projected,frame,
        opening_x_bias,object_index,flip_y,256,256,0);
}

/* 2F638 -> 4AF10/4AF24: project the ground foot first, then derive
 * the signed body Y from height. Distance and scale remain ground-based. */
static int project_course_actor(const PortableCameraState *camera,
    const PortableCameraProjection *projection,int32_t x,int32_t y,int32_t height,
    PortableCameraProjectionResult *body)
{
    const int status=portable_camera_projection_project(camera,projection,x,y,0,body);
    if(status)return status;
    return portable_camera_projection_body(body,height,body);
}

static int render_lava_bubbles(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->lava_bubbles.count>PORTABLE_LAVA_BUBBLE_CAPACITY)return 0;
    if(state->lava_bubbles.count!=0u&&scene->lava_bubble_art==NULL)return 0;
    for(size_t i=state->lava_bubbles.count;i!=0u;--i){
        const PortableLavaBubble *actor=&state->lava_bubbles.actors[i-1u];
        if((actor->flags&1u)==0u)continue;
        if(actor->art>1u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->lava_bubble_art,.native_art_count=2u};
        if(!render_projected_scenery(scene,&art,&projected,frame,0,0u,0))return 0;
    }
    return 1;
}

static int render_thwomps(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->thwomps.count>PORTABLE_THWOMP_CAPACITY)return 0;
    if(state->thwomps.count!=0u&&scene->thwomp_art==NULL)return 0;
    for(size_t i=state->thwomps.count;i!=0u;--i){
        const PortableThwomp *actor=&state->thwomps.actors[i-1u];
        if((actor->flags&1u)==0u)continue;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=0u};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->thwomp_art,.native_art_count=1u};
        if(!render_projected_scenery(scene,&art,&projected,frame,0,0u,0))return 0;
    }
    return 1;
}

static int render_crabs(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->crabs.count>PORTABLE_CRAB_CAPACITY)return 0;
    if(state->crabs.count!=0u && scene->crab_art==NULL)return 0;
    for(size_t i=state->crabs.count;i!=0u;--i){
        const PortableCrab *actor=&state->crabs.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(actor->art>3u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->crab_art,.native_art_count=4u};
        if(!render_projected_scenery(scene,&art,&projected,frame,0,0u,actor->flip_y))return 0;
    }
    return 1;
}

static int render_seagulls(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->seagulls.count>PORTABLE_SEAGULL_CAPACITY)return 0;
    if(state->seagulls.count!=0u && scene->seagull_art==NULL)return 0;
    for(size_t i=state->seagulls.count;i!=0u;--i){
        const PortableSeagull *actor=&state->seagulls.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(actor->art>2u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->seagull_art,.native_art_count=3u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,actor->flip_y,
            actor->affine_x_q8,actor->affine_y_q8,
            actor->affine_x_q8==256u && (uint16_t)(0x4000u-camera->yaw)>0x7fffu))return 0;
    }
    return 1;
}
static int render_cannonballs(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->cannonballs.count>PORTABLE_CANNONBALL_CAPACITY)return 0;
    if(state->cannonballs.count!=0u && scene->cannonball_art==NULL)return 0;
    for(size_t i=state->cannonballs.count;i!=0u;--i){
        const PortableCannonball *actor=&state->cannonballs.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(actor->art>6u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->cannonball_art,.native_art_count=7u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,0,
            actor->affine_x_q8,actor->affine_y_q8,
            0))return 0;
    }
    return 1;
}

static int render_piranhas(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->piranhas.count>PORTABLE_PIRANHA_CAPACITY)return 0;
    if(state->piranhas.count!=0u && scene->piranha_art==NULL)return 0;
    for(size_t i=state->piranhas.count;i!=0u;--i){
        const PortablePiranha *actor=&state->piranhas.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(actor->art>4u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->piranha_art,.native_art_count=5u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,actor->flip_y,
            actor->affine_x_q8,actor->affine_y_q8,
            0))return 0;
    }
    return 1;
}

static int render_snowmen(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->snowmen.count>PORTABLE_SNOWMAN_CAPACITY)return 0;
    if(state->snowmen.count!=0u && scene->snowman_art==NULL)return 0;
    for(size_t i=state->snowmen.count;i!=0u;--i){
        const PortableSnowman *actor=&state->snowmen.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(actor->art>1u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->snowman_art,.native_art_count=2u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,0u,
            256u,256u,
            0))return 0;
    }
    return 1;
}

static int render_mice(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->mice.count>PORTABLE_MOUSE_CAPACITY ||
       (state->mice.count && (!scene->mouse_art || !scene->sine)))return 0;
    for(size_t i=state->mice.count;i!=0u;--i){
        const PortableMouse *a=&state->mice.actors[i-1u];
        if(!(a->flags&1u))continue;
        if(a->art>1u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,a->x_q16,a->y_q16,a->height_q16,&projected))continue;
        int flip_x=0;
        if(a->variant!=0x8001u){
            /* 58480 truncates signed angle and Q16 velocities toward zero.
             * Facing samples velocity before 584F4 reverses a walking leg. */
            const int angle=(int16_t)camera->yaw/64;
            const int32_t x=a->facing_x_q16/256,y=-a->facing_y_q16/256;
            const int64_t side=(int64_t)scene->sine[(256u-(unsigned)angle)&1023u]*x-
                (int64_t)scene->sine[(unsigned)angle&1023u]*y;
            flip_x=(side>=0)^(a->turn_flip!=0u);
        }
        const PortableCourseSceneryObject object={.art_index=a->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->mouse_art,.native_art_count=2u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,a->affine_y_q8<0,
            256,256,flip_x))return 0;
    }
    return 1;
}

static int render_falling_stars(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->falling_stars.count>PORTABLE_FALLING_STAR_CAPACITY ||
       (state->falling_stars.count && (!scene->falling_star_art || !scene->sine)))return 0;
    const int output_scale=(int)portable_software_frame_scale(frame);
    for(size_t i=state->falling_stars.count;i!=0u;--i){
        const PortableFallingStar *a=&state->falling_stars.actors[i-1u];
        if(!(a->flags&1u))continue;
        if(a->art>4u || !a->affine_q8)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,a->x_q16,a->y_q16,a->height_q16,&projected))continue;
        if(projected.x < -64 || projected.x>(int)frame->width+64 || projected.y<0 || projected.y>224 ||
           projected.distance<=127 || projected.distance>4096)continue;
        const unsigned lod=projected.distance<512?0u:projected.distance<1024?1u:
            projected.distance<2048?2u:3u;
        const PortableCourseSceneryArt *art=&scene->falling_star_art[a->art];
        const PortableNativeImage *img=&art->lod[lod];
        const int scale=((projected.distance*224/256)>>art->distance_shift[lod])*a->affine_q8/256;
        if(scale<=0)continue;
        if(!img->width || !img->height)return 0;
        const PortableNativeImage *texture=scenery_texture(art,lod,frame,
            (int64_t)scale*65536,(int64_t)scale*65536);
        const unsigned angle=a->angle/64u;
        const int32_t sine=scene->sine[angle],cosine=scene->sine[(angle+256u)&1023u];
        int left=(int)frame->width*output_scale,right=0,top=(int)frame->height*output_scale,bottom=0;
        /* Bound the rotated source corners before inverse sampling. This
         * keeps large output resolutions from scanning empty sprite margins. */
        for(unsigned corner=0;corner<4u;++corner){
            const int u=img->anchor_x+((corner&1u)?(int)img->width:0);
            const int v=img->anchor_y+((corner&2u)?(int)img->height:0);
            const int64_t divisor=(int64_t)scale*32768;
            const int x=projected.x*output_scale+(int)(((int64_t)cosine*u-(int64_t)sine*v)*256*output_scale/divisor);
            const int y=projected.y*output_scale+(int)(((int64_t)sine*u+(int64_t)cosine*v)*256*output_scale/divisor);
            if(x-2<left)left=x-2;
            if(x+2>right)right=x+2;
            if(y-2<top)top=y-2;
            if(y+2>bottom)bottom=y+2;
        }
        if(left<0)left=0;
        if(right>(int)frame->width*output_scale)right=(int)frame->width*output_scale;
        if(top<0)top=0;
        if(bottom>(int)frame->height*output_scale)bottom=(int)frame->height*output_scale;
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
            const int dx=x-projected.x*output_scale,dy=y-projected.y*output_scale;
            /* Floor after the full inverse product avoids signed-shift rules. */
            const int64_t rx=((int64_t)cosine*dx+(int64_t)sine*dy)*scale;
            const int64_t ry=(-(int64_t)sine*dx+(int64_t)cosine*dy)*scale;
            const int sx=(int)portable_software_frame_floor_div(rx,INT64_C(8388608)*output_scale)-img->anchor_x;
            const int sy=(int)portable_software_frame_floor_div(ry,INT64_C(8388608)*output_scale)-img->anchor_y;
            if(sx<0 || sy<0 || sx>=(int)img->width || sy>=(int)img->height)continue;
            int tx=sx,ty=sy;
            if(texture!=img){
                const int64_t u=portable_software_frame_floor_div(rx,128*output_scale)-(int64_t)img->anchor_x*65536;
                const int64_t v=portable_software_frame_floor_div(ry,128*output_scale)-(int64_t)img->anchor_y*65536;
                tx=(int)portable_software_frame_floor_div(u*texture->width,(int64_t)img->width*65536);
                ty=(int)portable_software_frame_floor_div(v*texture->height,(int64_t)img->height*65536);
            }
            if(tx<0 || ty<0 || tx>=texture->width || ty>=texture->height)continue;
            const uint32_t pixel=portable_native_image_pixel_palette_masked(texture,(size_t)tx,(size_t)ty,scene->object_palette,scene->object_palette_count,scene->object_palette_dirty);
            if(pixel>>24u)portable_software_frame_store_physical(frame,(size_t)x,(size_t)y,pixel);
        }
    }
    return 1;
}

static int render_thunderclouds(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->thunderclouds.count>PORTABLE_THUNDERCLOUD_CAPACITY ||
       (state->thunderclouds.count && !scene->thundercloud_art) ||
       state->thunderclouds.palette_phase[0]>8u || state->thunderclouds.palette_phase[1]>8u)return 0;
    for(size_t i=state->thunderclouds.count;i!=0u;--i){
        const PortableThundercloud *a=&state->thunderclouds.actors[i-1u];
        if(!(a->flags&1u))continue;
        if(a->art>7u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,a->x_q16,a->y_q16,a->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=(uint8_t)(a->art+
            8u*state->thunderclouds.palette_phase[a->art/4u])};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->thundercloud_art,.native_art_count=PORTABLE_THUNDERCLOUD_ART_COUNT};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,0,256,256,0))return 0;
    }
    return 1;
}

static int render_penguins(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->penguins.count>PORTABLE_PENGUIN_CAPACITY)return 0;
    if(state->penguins.count!=0u && scene->penguin_art==NULL)return 0;
    for(size_t i=state->penguins.count;i!=0u;--i){
        const PortablePenguin *actor=&state->penguins.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(actor->art>5u)return 0;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=actor->art};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->penguin_art,.native_art_count=6u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,actor->affine_y_q8<0,
            256u,256u,
            0))return 0;
    }
    return 1;
}

static int render_boos(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,const PortableCameraState *camera,
    const PortableCameraProjection *projection,PortableSoftwareFrame *frame,size_t sprite_index)
{
    if(state->boos.count>PORTABLE_BOO_CAPACITY)return 0;
    if(state->boos.count!=0u && scene->boo_art==NULL)return 0;
    for(size_t i=state->boos.count;i!=0u;--i){
        const PortableBoo *actor=&state->boos.actors[i-1u];
        if(!(actor->flags&1u))continue;
        if(i-1u!=sprite_index)continue;
        PortableCameraProjectionResult projected;
        if(project_course_actor(camera,projection,actor->x_q16,
            actor->y_q16,actor->height_q16,&projected))continue;
        const PortableCourseSceneryObject object={.art_index=(uint8_t)portable_boo_view_art(actor,camera->yaw)};
        const PortableCourseSceneryView art={.objects=&object,.object_count=1u,
            .native_art=scene->boo_art,.native_art_count=2u};
        if(!render_projected_scenery_transform(scene,&art,&projected,frame,0,0u,actor->affine_y_q8<0,
            256u,256u,
            0))return 0;
    }
    return 1;
}

/* Compose source +196/+198 inverse Q8 axes with distance/shrink scale. */
/* C928 owns only wheel colors1/2; C84C's selected body bank stays intact.
 * Small-palette custom art keeps its authored colors. */
static void driver_wheel_palette(const PortableCharacterAssetView *character,
    const PortableDriverArtMotion *motion,uint16_t palette[16])
{
    if(!motion->wheel_palette_valid || motion->wheel_phase>2u ||
       character->palette_banks==NULL ||
       motion->wheel_palette_bank>=character->palette_bank_count)return;
    const uint16_t *bank=character->palette_banks+(size_t)motion->wheel_palette_bank*16u;
    palette[1]=bank[motion->wheel_phase];
    palette[2]=bank[motion->wheel_phase+1u];
}

static uint16_t squash_coefficient(uint16_t scale,uint32_t factor)
{
    const uint32_t value=(scale==0u?0x100u:scale)*factor/256u;
    return (uint16_t)(value>UINT16_MAX?UINT16_MAX:value);
}

/* Missing pose variants deliberately retain the source billboard. A valid
 * binding owns authored geometry; only its immutable actor pose changes here. */
static int render_bound_model(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state, const PortableCameraState *camera,
    const PortableCameraProjection *basis, PortableSoftwareFrame *frame,
    uint32_t kind, uint32_t target_id, uint32_t art_bank, uint32_t pose_override,
    const PortableModelPose *pose)
{
    if(!scene->scene_3d || !scene->models)return 0;
    const PortableModelBinding *binding=portable_model_catalog_find(scene->models,
        kind,target_id,art_bank,pose_override);
    if(!binding)return 0;
    const size_t scale=portable_software_frame_scale(frame);
    const int first=state->sky_motion.initialized?
        portable_sky_motion_road_first_row(&state->sky_motion,(unsigned)scale):
        (int)(74u*scale)+camera->projection_y_offset*(int)scale;
    const PortableScene3DProjection perspective={1024.0*(double)scale,
        1024.0*(double)scale,(double)camera->screen_x*(double)scale,
        (double)camera->screen_y*(double)scale+first-(int)(74u*scale),1.0,65535.0};
    if(first>=0 && portable_model_render(scene->scene_3d,binding,pose,camera,
            basis,&perspective,scene->driver_projection,scene->sine,scene->sine_count))return 1;
    if(!scene->scene_3d->model_fallback_reported){
        fprintf(stderr,"race scene: model unavailable; using billboard (course=%u kind=%u target=%u art=%u pose=%u)\n",
            scene->course_id,kind,target_id,art_bank,pose_override);
        scene->scene_3d->model_fallback_reported=1u;
    }
    return 0;
}

static int render_course_scenery(const PortableRaceSceneView *scene,
                                 const PortableCameraState *camera,
                                 const PortableCameraProjection *projection,
                                 PortableSoftwareFrame *frame,
                                 const PortableRacePresentationState *state,size_t sprite_index,int rider_only)
{
    if(scene->scenery==NULL||scene->scenery->object_count==0u)return 1;
    if(scene->scenery->objects==NULL||
       scene->scenery->object_count>PORTABLE_COURSE_SCENERY_MAX_OBJECTS ||
       (state->course_props.count && state->course_props.count!=scene->scenery->object_count))
        return 0;
    for(size_t reverse=scene->scenery->object_count;reverse!=0u;--reverse){
        if(reverse-1u!=sprite_index)continue;
        const PortableCourseSceneryObject *object=
            &scene->scenery->objects[reverse-1u];
        const PortableCourseProp *prop=state->course_props.count==scene->scenery->object_count?
            &state->course_props.actors[reverse-1u]:NULL;
        PortableCameraProjectionResult projected;
        if(rider_only && prop && (prop->source_code>>16u)==3u && prop->teepee.rider_state &&
           !project_course_actor(camera,projection,prop->teepee.rider_x_q16,
                prop->teepee.rider_y_q16,prop->teepee.rider_z_q16,&projected)){
            PortableCameraProjectionPreciseResult precise={0};
            if(frame->scale>1u && scenery_projection_precise(camera,projection,prop->teepee.rider_x_q16,
                    prop->teepee.rider_y_q16,prop->teepee.rider_z_q16,&precise))return 0;
            PortableCourseSceneryObject rider=*object;
            rider.art_index=(uint8_t)portable_teepee_rider_art(prop,camera->yaw);
            PortableCourseSceneryView rider_scenery=*scene->scenery;
            rider_scenery.objects=&rider;rider_scenery.object_count=1u;
            if(!render_scenery_precise(scene,&rider_scenery,&projected,&precise,camera,frame,
                    state->opening_frame!=0u,0,256,256,0))return 0;
        }
        if(rider_only)continue;
        if(prop && !(prop->flags&1u))continue;
        if(project_course_actor(camera,projection,prop?prop->x_q16:object->position_x_q16,
                prop?prop->y_q16:object->position_y_q16,prop?prop->height_q16:
                    ((object->source_code>>16u)==1u?-INT32_C(0x80000):0),&projected))continue;
        PortableCameraProjectionPreciseResult precise={0};
        if(frame->scale>1u && scenery_projection_precise(camera,projection,prop?prop->x_q16:object->position_x_q16,
                prop?prop->y_q16:object->position_y_q16,prop?prop->height_q16:
                    ((object->source_code>>16u)==1u?-INT32_C(0x80000):0),&precise))return 0;
        const int scale=object->affine_q8?object->affine_q8:256;
        const int scale_x=prop?prop->affine_x_q8:scale;
        const int scale_y=prop?prop->affine_y_q8:scale;
        PortableCourseSceneryObject posed=*object;
        if(prop)posed.art_index=(uint8_t)portable_course_prop_view_art(prop,camera->yaw);
        PortableCourseSceneryView posed_scenery=*scene->scenery;
        if(prop && prop->source_code==UINT32_C(0x860000)){
            if(prop->contact_phase>1u)return 0;
            posed.art_index=(uint8_t)(prop->art_index+prop->contact_phase);
        }
        /* Bind the placement's base bank; animation/contact frames select an
         * exact optional variant instead of changing the actor's identity. */
        if(posed.art_index>=object->art_index &&
           (unsigned)(posed.art_index-object->art_index)<=31u){
            const PortableModelPose model_pose={
                .position_x_q16=prop?prop->x_q16:object->position_x_q16,
                .position_y_q16=prop?prop->y_q16:object->position_y_q16,
                .height_q16=prop?prop->height_q16:
                    ((object->source_code>>16u)==1u?-INT32_C(0x80000):0),
                .base_inverse_q8=256u,.inverse_x_q8=scale_x,.inverse_y_q8=scale_y};
            if(render_bound_model(scene,state,camera,projection,frame,
                PORTABLE_MODEL_SCENERY,scene->course_id,object->art_index,
                (uint32_t)(posed.art_index-object->art_index),&model_pose))continue;
        }
        posed_scenery.objects=&posed;posed_scenery.object_count=1u;
        if(!render_scenery_precise(scene,&posed_scenery,&projected,&precise,camera,frame,
                state->opening_frame!=0u,scale_y<0,
                scale_x,scale_y<0?-scale_y:scale_y,0))return 0;
    }
    return 1;
}

/* BA58/BAD0: three source balloon slots orbit the owner's projected foot.
 * The native image already contains the source cell's anchor and palette. */
static int render_balloons(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state, PortableSoftwareFrame *frame,
    const PortableCameraProjectionResult *owner, int y_offset,
    uint16_t heading, uint8_t count, int front)
{
    if (!state->show_battle || count==0u || scene->battle_balloon==NULL ||
        scene->battle_balloon->pixels==NULL || owner->distance<=0) return 1;
    if (count>3u) return 0;
    const PortableNativeImage *image=scene->battle_balloon;
    const int output_scale=(int)portable_software_frame_scale(frame);
    const int32_t depth=owner->distance;
    const int32_t inverse=65536/depth;
    static const uint16_t angle_offsets[3]={0u,0x1800u,0xe800u};
    static const uint8_t bob_phases[3]={0x20u,0u,0x40u};
    for (unsigned slot=0u;slot<count;++slot) {
        const uint16_t angle=(uint16_t)(state->camera_yaw-heading+angle_offsets[slot]);
        const unsigned sector=((uint32_t)angle+0x800u)/0x1000u&15u;
        const int sort_offset=sector<=8u?(int)sector-4:12-(int)sector;
        if ((sort_offset<0)!=front) continue;
        const int32_t signed_angle=angle<0x8000u?angle:(int32_t)angle-65536;
        const unsigned index=(unsigned)(signed_angle/64)&1023u;
        /* The source narrows phase to signed8 before multiplying by256. */
        const uint8_t phase=(uint8_t)(bob_phases[slot]+state->balloon_tick*2u);
        const int32_t signed_phase=phase<128u?phase:(int32_t)phase-256;
        const int32_t sway=scene->sine[index]*24;
        const int32_t bob=scene->sine[(unsigned)(signed_phase*4)&1023u]*6-
            (slot==0u?6*65536:0);
        const int x=owner->x*output_scale+(int)((int64_t)sway*inverse*output_scale/16777216)+image->anchor_x*256*output_scale/depth;
        const int y=(owner->y+y_offset)*output_scale+(int)((int64_t)bob*inverse*output_scale/16777216)+image->anchor_y*256*output_scale/depth;
        const int width=(image->width*256*output_scale+depth-1)/depth;
        const int height=(image->height*256*output_scale+depth-1)/depth;
        const int left=x<0?0:x, top=y<0?0:y;
        const int right=x+width>(int)frame->width*output_scale?(int)frame->width*output_scale:x+width;
        const int bottom=y+height>(int)frame->height*output_scale?(int)frame->height*output_scale:y+height;
        for (int py=top;py<bottom;++py)
            for (int px=left;px<right;++px) {
                const uint32_t pixel=portable_native_image_pixel(image,
                    (size_t)((px-x)*depth/(256*output_scale)),(size_t)((py-y)*depth/(256*output_scale)));
                if ((pixel>>24)!=0u)
                    portable_software_frame_store_physical(frame,(size_t)px,(size_t)py, pixel);
            }
    }
    return 1;
}

/* Roster drivers' own portraits replace the stock 2x2 HUD cells. */
static void draw_racer_portraits(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state, PortableSoftwareFrame *frame)
{
    PortableSoftwareFrame view = portable_software_frame_native_view(frame, PORTABLE_FRAME_LEFT);
    for (size_t rank = 0u; rank < 4u; ++rank) {
        const uint8_t racer = state->gp_top_four_racers[rank];
        const PortableNativeImage *image = racer < 8u ? scene->gp_racer_portraits[racer] : NULL;
        if (image == NULL) continue;
        for (size_t y = 0u; y < image->height; ++y)
            for (size_t x = 0u; x < image->width; ++x) {
                const uint32_t pixel = portable_native_image_pixel(image, x, y);
                const size_t tx = (size_t)PORTABLE_HUD_PORTRAIT_X + x;
                const size_t ty = (size_t)PORTABLE_HUD_PORTRAIT_Y(rank) + y;
                if ((pixel >> 24) == 255u && tx < view.width && ty < view.height)
                    portable_software_frame_store(&view, ty * view.width + tx, pixel);
            }
    }
}

/* 3670C submits priority1 cells before the driver draw pass. Lower particle
 * slots therefore cover higher slots, while priority0 HUD stays above them. */
static int render_snow_weather(const PortableRaceSceneView *scene,
                               const PortableRacePresentationState *state,
                               PortableSoftwareFrame *frame)
{
    const PortableSnowWeatherSnapshot *weather = &state->snow_weather;
    if (!weather->enabled)
        return 1;
    if (weather->count > PORTABLE_SNOW_WEATHER_PARTICLES || scene->weather_profile == NULL)
        return 0;
    PortableSoftwareFrame centered =
        portable_software_frame_native_view(frame, PORTABLE_FRAME_CENTER);
    for (unsigned reverse = weather->count; reverse != 0u; --reverse)
    {
        const PortableSnowWeatherSprite *sprite = &weather->particles[reverse - 1u];
        if (!sprite->visible)
            continue;
        if (sprite->art >= 3u)
            return 0;
        const PortableNativeImage *image = &scene->weather_profile->images[sprite->art];
        if (image->pixels == NULL || !image->width || !image->height)
            return 0;
        /* Source Y is eight bits; the top-edge respawn spans248..255. X is
         * nine bits after cell placement, so X244 must remain offscreen. */
        const int64_t scale = (int64_t)portable_software_frame_scale(&centered);
        const int64_t origin_x =
            scale > 1 && weather->precise ? sprite->x_q16 : (int64_t)sprite->x * 65536;
        const int64_t origin_y =
            scale > 1 && weather->precise
                ? sprite->y_q16
                : (int64_t)(sprite->y >= 160u ? (int)sprite->y - 256 : (int)sprite->y) * 65536;
        const int64_t left =
            portable_software_frame_floor_div(origin_x * scale, 65536) + image->anchor_x * scale;
        const int64_t top =
            portable_software_frame_floor_div(origin_y * scale, 65536) + image->anchor_y * scale;
        const int64_t right = left + image->width * scale, bottom = top + image->height * scale;
        const int64_t width = (int64_t)centered.width * scale,
                      height = (int64_t)centered.height * scale;
        const int64_t first_x = left < 0 ? 0 : left, first_y = top < 0 ? 0 : top;
        const int64_t last_x = right < width ? right : width,
                      last_y = bottom < height ? bottom : height;
        for (int64_t target_y = first_y; target_y < last_y; ++target_y)
        {
            const unsigned y = (unsigned)((target_y - top) / scale);
            for (int64_t target_x = first_x; target_x < last_x; ++target_x)
            {
                const unsigned x = (unsigned)((target_x - left) / scale);
                const uint32_t pixel = portable_native_image_pixel_palette_masked(
                    image, x, y, scene->object_palette, scene->object_palette_count,
                    scene->object_palette_dirty);
                if (pixel >> 24u)
                    portable_software_frame_store_physical(&centered, (size_t)target_x,
                                                           (size_t)target_y, pixel);
            }
        }
    }
    return 1;
}

static int render_scene_hud(const PortableRaceSceneView *scene,
                            const PortableRacePresentationState *state,
                            PortableSoftwareFrame *frame, int background_only)
{
    PortableHudState hud;
    if (scene == NULL || state == NULL || frame == NULL || frame->pixels == NULL)
        return 0;
    /* The original keeps the race HUD for the first two pause-opening frames,
       then clears it before the BG0 panel begins drawing. */
    if (state->suppress_native_hud || (state->paused && state->pause_open_ticks > 2u))
        return 1;
    hud=(PortableHudState){
        .lap=(uint8_t)(state->lap_count==0u ? 0u :
            state->laps_completed<state->lap_count ? state->laps_completed : state->lap_count-1u),
        .lap_count=(uint8_t)state->lap_count,
        .position=0u,
        .checkpoint=(uint8_t)state->next_route_index,
        .elapsed_frames=state->total_ticks,
        .heading=state->heading,
        .show_time_trial_items=state->show_time_trial_items,
        .mushrooms_remaining=state->mushrooms_remaining,
        .show_gp_item=state->show_gp_item,
        .show_battle=state->show_battle,
        .gp_item=state->gp_item,
        .result_clock_mode=state->result_clock_mode,
        .result_lap_centiseconds=state->result_lap_centiseconds,
        .source_clock_centiseconds=state->source_clock_centiseconds,
        .source_clock_override=state->source_clock_override,
    };
    if(background_only){hud.show_time_trial_items=0u;hud.gp_item=0u;}
    const int gp = state->show_gp_item && !state->show_battle && scene->gp_hud != NULL;
    if(!state->result_board_visible && portable_hud_render_with_asset(
            scene->minimap,scene->hud,&hud,frame)!=PORTABLE_HUD_RENDER_OK){
        fprintf(stderr,"race scene HUD failed\n");
        return 0;
    }
    if(!gp && state->show_time_trial_items && !state->suppress_native_minimap){
        if(!state->result_board_visible && scene->gp_hud && scene->gp_hud->course_tiles &&
           !portable_hud_render_course_map(scene->gp_hud,scene->hud,frame))return 0;
        if(!background_only && state->phase!=PORTABLE_RACE_PHASE_FINISHED &&
           scene->gp_markers && scene->gp_markers->tiles){
            PortableRaceMinimapMarker markers[8]={{.x_q16=state->position_x_q16,
                .y_q16=state->position_y_q16,.source_flags=state->marker_source_flags,
                .class_flags=state->marker_class_flags,.selected=1u}};
            uint8_t count=1u;
            if(state->ghost_visible){
                markers[1]=(PortableRaceMinimapMarker){.x_q16=state->ghost_position_x_q16,
                    .y_q16=state->ghost_position_y_q16,.source_flags=state->ghost_marker_source_flags,
                    .class_flags=state->ghost_marker_class_flags};count=2u;
            }
            if(!portable_gp_results_render_race_markers(scene->gp_markers,markers,count,
                    scene->gp_hud&&scene->gp_hud->minimap_shift?scene->gp_hud->minimap_shift:21u,
                    state->marker_clock,frame))return 0;
        }
    }
    if (gp) {
        PortableRaceMinimapMarker markers[8]={0};
        uint8_t classes[4];
        for (size_t rank = 0u; rank < 4u; ++rank)
            classes[rank] = state->gp_top_four_racers[rank] < 8u &&
                scene->gp_racer_portraits[state->gp_top_four_racers[rank]] != NULL ?
                PORTABLE_HUD_PORTRAIT_OVERRIDDEN : state->gp_top_four_classes[rank];
        if(state->result_board_visible){
            if(!portable_hud_render_gp_coins(scene->hud,state->gp_coins,frame))return 0;
        }else{
            if(state->gp_rank_hud.initialized&&state->gp_coin_hud.initialized){
                uint16_t cells[PORTABLE_GP_RANK_HUD_CELLS];
                memcpy(cells,state->gp_rank_hud.displayed_cells,sizeof cells);
                /* Custom roster portraits retain their transparent interiors. */
                for(unsigned rank=0u;rank<4u;++rank)if(classes[rank]==PORTABLE_HUD_PORTRAIT_OVERRIDDEN)
                    for(unsigned y=0u;y<2u;++y)for(unsigned x=0u;x<2u;++x)
                        cells[(1u+rank*3u+y)*4u+1u+x]=0u;
                if(!portable_hud_render_gp_race_owned(scene->gp_hud,scene->hud,
                        state->gp_rank_hud.displayed_rank,classes,state->total_ticks,cells,
                        state->gp_rank_hud.displayed_colors,state->gp_coin_hud.displayed_cells,
                        state->gp_coin_hud.displayed_colors,
                        state->gp_rank_hud.displayed_separator_colors,frame))return 0;
            }else if(!portable_hud_render_gp_race(scene->gp_hud,scene->hud,state->gp_rank,classes,
                    state->gp_coins,state->total_ticks,frame))return 0;
            draw_racer_portraits(scene,state,frame);
        }
        if(background_only)return 1;
        markers[0]=(PortableRaceMinimapMarker){.x_q16=state->position_x_q16,
            .y_q16=state->position_y_q16,.source_flags=state->marker_source_flags,
            .class_flags=state->marker_class_flags,.selected=1u};
        const uint8_t count=(uint8_t)(state->opponent_count<7u?state->opponent_count+1u:8u);
        for(unsigned i=1u;i<count;++i)markers[i]=(PortableRaceMinimapMarker){
            .x_q16=state->opponents[i-1u].position_x_q16,.y_q16=state->opponents[i-1u].position_y_q16,
            .source_flags=state->opponents[i-1u].marker_source_flags,
            .class_flags=state->opponents[i-1u].marker_class_flags};
        if (!portable_gp_results_render_race_markers(scene->gp_markers,markers,count,
                scene->gp_hud->minimap_shift?scene->gp_hud->minimap_shift:21u,
                state->marker_clock,frame)) return 0;
        if (scene->gp_rank_art != NULL){
            uint8_t colors[8u*8u];memcpy(colors,scene->gp_hud->rank_colors,sizeof colors);
            const uint8_t rank=state->gp_rank_hud.initialized?state->gp_rank_hud.displayed_rank:state->gp_rank;
            if(state->gp_rank_hud.initialized)for(unsigned i=0u;i<4u;++i){
                colors[rank*8u+i*2u]=(uint8_t)state->gp_rank_hud.displayed_colors[i];
                colors[rank*8u+i*2u+1u]=(uint8_t)(state->gp_rank_hud.displayed_colors[i]>>8u);
            }
            if(!portable_gp_results_render_rank_with_colors(scene->gp_rank_art,rank,colors,frame))return 0;
        }
    }
    return 1;
}

int portable_race_scene_render_hud(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,PortableSoftwareFrame *frame)
{
    PortableSoftwareFrame centered;
    if(scene && scene->centered_hud && frame){
        centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
        frame=&centered;
    }
    if (!render_scene_hud(scene,state,frame,0))return 0;
    if (state->modern_drift_enabled && state->phase == PORTABLE_RACE_PHASE_RACING &&
        !state->suppress_native_hud && !state->result_board_visible)
    {
        const uint8_t tier = state->modern_drift.last_applied_tier != 0u ?
            state->modern_drift.last_applied_tier : state->modern_drift.charge_tier;
        if (tier > 3u || frame->width < 40u || frame->height < 10u)return 0;
        const uint32_t colors[3] = {UINT32_C(0xffffb040), UINT32_C(0xff2090ff), UINT32_C(0xffff70d0)};
        const size_t x = frame->width/2u-19u, y = frame->height-9u;
        for (unsigned i=0u;i<3u;++i)
        {
            portable_software_frame_rect(frame,x+i*13u,y,12u,7u,UINT32_C(0xff181818));
            portable_software_frame_rect(frame,x+1u+i*13u,y+1u,10u,5u,
                i<tier ? colors[tier-1u] : UINT32_C(0xff605858));
            if (i<tier && state->modern_drift.last_applied_tier != 0u)
                portable_software_frame_rect(frame,x+3u+i*13u,y+2u,6u,1u,UINT32_C(0xffffffff));
        }
    }
    if (state->modern_air_enabled && state->phase == PORTABLE_RACE_PHASE_RACING &&
        !state->suppress_native_hud && !state->result_board_visible)
    {
        if(frame->width<40u || frame->height<20u)return 0;
        const size_t x=frame->width/2u-6u,y=frame->height-18u;
        const uint32_t color=state->modern_air.last_applied?UINT32_C(0xffffffff):
            state->modern_air.tricked?UINT32_C(0xff40e8ff):
            state->modern_air.armed?UINT32_C(0xff40b070):UINT32_C(0xff605858);
        portable_software_frame_rect(frame,x,y,12u,7u,UINT32_C(0xff181818));
        /* A small upward chevron distinguishes air readiness from drift pips. */
        portable_software_frame_rect(frame,x+5u,y+1u,2u,1u,color);
        portable_software_frame_rect(frame,x+3u,y+2u,6u,1u,color);
        portable_software_frame_rect(frame,x+1u,y+3u,10u,2u,color);
    }
    return 1;
}

int portable_race_scene_capture_lap_board_hud(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,PortableLapBoardHud *hud)
{
    if(!scene || !state || !hud)return 0;
    PortableSoftwareFrame frame={.pixels=hud->pixels,.width=240u,.height=160u};
    portable_software_frame_clear(&frame,PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
    PortableRacePresentationState frozen=*state;
    frozen.paused=0;frozen.suppress_native_hud=0;frozen.result_board_visible=0;
    return render_scene_hud(scene,&frozen,&frame,1);
}

static int render_lava_smoke_precise(const PortableRaceParticleAssetView *art,
    const PortableLavaSmoke *smoke,const PortableCameraProjectionResult *body,
    PortableSoftwareFrame *frame,const PortableCameraProjectionPreciseResult *precise)
{
    if(art==NULL||smoke==NULL||body==NULL||body->distance<=0)return 0;
    if(!smoke->visible)return 1;
    for(unsigned i=0u;i<PORTABLE_LAVA_SMOKE_PUFFS;++i){
        const PortableLavaSmokePuff *puff=&smoke->puffs[i];
        if(!puff->age)continue;
        if(puff->age>PORTABLE_LAVA_SMOKE_PUFF_AGE)return 0;
        /* C278 resolves the current offset before decrementing signed-byte Y.
         * Age one is the script load tick, with no generic particle startup. */
        const int y=(int)puff->dy-((int)puff->age-1);
        const uint8_t wrapped_y=(uint8_t)y;
        const int signed_y=wrapped_y>=128u?(int)wrapped_y-256:(int)wrapped_y;
        const int16_t x=(int16_t)((uint16_t)body->x+
            (uint16_t)((int)puff->dx*256/body->distance));
        const int16_t projected_y=(int16_t)((uint16_t)body->y+
            (uint16_t)(signed_y*256/body->distance));
        if(precise){
            const int32_t x_q16=(int32_t)((int64_t)precise->x_q16+(int64_t)puff->dx*INT64_C(1099511627776)/precise->distance_q16);
            const int32_t y_q16=(int32_t)((int64_t)precise->y_q16+(int64_t)signed_y*INT64_C(1099511627776)/precise->distance_q16);
            if(!portable_race_particle_render_positioned(art,frame,x_q16,y_q16,(uint8_t)(puff->age+1u),256u))return 0;
        }else if(!portable_race_particle_render(art,frame,x,projected_y,
            (uint8_t)(puff->age+1u)))return 0;
    }
    return 1;
}

int portable_race_scene_render_lava_smoke(const PortableRaceParticleAssetView *art,
    const PortableLavaSmoke *smoke,const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame)
{ return render_lava_smoke_precise(art,smoke,body,frame,NULL); }

/* C40C/C4D8 use the resolved body and unscaled art. dy is stored after the
 * callback; recover the preceding placement with signed-byte wrap. */
static int render_recovery_fx_precise(const PortableRaceParticleAssetView *thaw,
    const PortableRaceParticleAssetView *drip,const PortableRecoveryFxRules *rules,
    const PortableRecoveryFx *fx,const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame,
    const PortableCameraProjectionPreciseResult *precise)
{
    if(!fx||!body||!rules||body->distance<=0)return 0;
    if(!fx->visible)return 1;
    if(fx->thaw_age && thaw && thaw->frame_count &&
       !(precise?portable_race_particle_render_positioned(thaw,frame,precise->x_q16,precise->y_q16-16*65536,(uint8_t)(fx->thaw_age+1u),256u):
           portable_race_particle_render(thaw,frame,body->x,body->y-16,(uint8_t)(fx->thaw_age+1u))))return 0;
    if(drip && drip->frame_count)for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i){
        const PortableRecoveryDrip *particle=&fx->drips[i];
        if(!particle->age)continue;
        if(particle->age>PORTABLE_RECOVERY_DRIP_TICKS)return 0;
        const uint8_t bits=(uint8_t)((int)particle->dy-rules->drip_dy[particle->age-1u]);
        const int y=bits>=128u?(int)bits-256:(int)bits;
        const int16_t x=(int16_t)((uint16_t)body->x+(uint16_t)((int)particle->dx*256/body->distance));
        const int16_t projected_y=(int16_t)((uint16_t)body->y+(uint16_t)(y*256/body->distance));
        if(precise){
            const int32_t x_q16=(int32_t)((int64_t)precise->x_q16+(int64_t)particle->dx*INT64_C(1099511627776)/precise->distance_q16);
            const int32_t y_q16=(int32_t)((int64_t)precise->y_q16+(int64_t)y*INT64_C(1099511627776)/precise->distance_q16);
            if(!portable_race_particle_render_positioned(drip,frame,x_q16,y_q16,(uint8_t)(particle->age+1u),256u))return 0;
        }else if(!portable_race_particle_render(drip,frame,x,projected_y,(uint8_t)(particle->age+1u)))return 0;
    }
    return 1;
}

int portable_race_scene_render_recovery_fx(const PortableRaceParticleAssetView *thaw,
    const PortableRaceParticleAssetView *drip,const PortableRecoveryFxRules *rules,
    const PortableRecoveryFx *fx,const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame)
{ return render_recovery_fx_precise(thaw,drip,rules,fx,body,frame,NULL); }

int portable_race_scene_render_impact_effects(const PortableRaceParticleAssetView art[6],
    const PortableImpactEffects *effects,PortableSoftwareFrame *frame)
{
    if(!art||!effects||!frame)return 0;
    const int center=frame->width>240u?(int)(frame->width-240u)/2:0;
    for(unsigned i=0u;i<PORTABLE_IMPACT_EFFECT_SLOTS;++i){
        const PortableImpactEffectSlot *slot=&effects->slots[i];
        if(!slot->active || !slot->age)continue;
        if(slot->variant>=6u)return 0;
        const PortableRaceParticleAssetView *asset=&art[slot->variant];
        unsigned row=0u,span=0u;
        while(row<asset->frame_count && slot->age>span+asset->frames[row].duration+1u){
            span+=asset->frames[row].duration+1u;++row;
        }
        if(row==asset->frame_count)return 0;
        PortableRaceParticleAssetView cell=*asset;cell.frames=&asset->frames[row];cell.frame_count=1u;
        if(!portable_race_particle_render_scaled(&cell,frame,slot->x+center,slot->y,2u,slot->inverse))return 0;
    }
    return 1;
}

int portable_race_scene_render_coin_effects(const PortableRaceParticleAssetView art[3],
    const PortableCoinEffects *effects,PortableSoftwareFrame *frame)
{
    if(!art||!effects||!frame)return 0;
    const int center=frame->width>240u?(int)(frame->width-240u)/2:0;
    for(unsigned i=0u;i<PORTABLE_COIN_EFFECT_SLOTS;++i){
        const PortableCoinEffectSlot *slot=&effects->slots[i];
        if(!slot->kind || !slot->visible || !slot->age)continue;
        if(slot->kind!=PORTABLE_COIN_EFFECT_BOUNCE && slot->kind!=PORTABLE_COIN_EFFECT_RISE)continue;
        const PortableRaceParticleAssetView *asset=&art[slot->kind==PORTABLE_COIN_EFFECT_RISE?1u:0u];
        unsigned row=0u,span=0u;
        while(row<asset->frame_count && slot->age>span+asset->frames[row].duration+1u){
            span+=asset->frames[row].duration+1u;++row;
        }
        if(row==asset->frame_count)return 0;
        PortableRaceParticleAssetView cell=*asset;cell.frames=&asset->frames[row];cell.frame_count=1u;
        if(frame->scale>1u && slot->kind==PORTABLE_COIN_EFFECT_BOUNCE){
            if(!portable_race_particle_render_positioned(&cell,frame,((int32_t)slot->x+center)*65536,
                slot->y_q16,2u,slot->inverse))return 0;
        }else if(!portable_race_particle_render_scaled(&cell,frame,slot->x+center,slot->y,2u,slot->inverse))return 0;
    }
    return 1;
}

static int render_surface_trail_cell(const PortableRaceParticleAssetView *art,unsigned row,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,uint16_t inverse)
{
    if(!art||row>=art->frame_count)return 0;
    PortableRaceParticleAssetView cell=*art;cell.frames=&art->frames[row];cell.frame_count=1u;
    return portable_race_particle_render_scaled(&cell,frame,x,y,2u,inverse);
}
/* E1A0 submits the append-list oldest first. Slot reuse cannot reorder newer
 * trails over older ones; screen coordinates are source signed Q7 /128. */
static int render_surface_trails_precise(const PortableRaceParticleAssetView art[5],
    const PortableSurfaceTrailRules *rules,const PortableSurfaceTrail *trails,
    const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame,
    const PortableCameraProjectionPreciseResult *precise)
{
    if(!art||!rules||!trails||!body||!frame)return 0;
    if(!trails->visible)return 1;
    const int center=frame->width>240u?(int)(frame->width-240u)/2:0;
    if(trails->extra_spray && art[4].frame_count && body->distance>0){
        if(precise){
            PortableRaceParticleAssetView cell=art[4];cell.frame_count=1u;
            if(!portable_race_particle_render_positioned(&cell,frame,precise->x_q16+2*65536,
                precise->y_q16+7*65536,2u,(uint16_t)body->distance))return 0;
        }else if(!render_surface_trail_cell(&art[4],0u,frame,body->x+2,body->y+7,(uint16_t)body->distance))return 0;
    }
    for(unsigned age=PORTABLE_SURFACE_TRAIL_LIFETIME;age>0u;--age){
        if(!(age&3u))continue;
        for(unsigned i=0u;i<PORTABLE_SURFACE_TRAIL_SLOTS;++i){
            const PortableSurfaceTrailSlot *slot=&trails->slots[i];
            if(slot->age!=age)continue;
            if(slot->kind<1u||slot->kind>5u)return 0;
            const unsigned group=slot->kind<4u?slot->kind-1u:3u;
            if(art[group].frame_count){
                const unsigned row=portable_surface_trail_frame(rules,slot);
                if(frame->scale>1u){
                    if(row>=art[group].frame_count)return 0;
                    PortableRaceParticleAssetView cell=art[group];cell.frames=&art[group].frames[row];cell.frame_count=1u;
                    if(!portable_race_particle_render_positioned(&cell,frame,slot->x_q7*512+center*65536,
                        slot->y_q7*512,2u,256u))return 0;
                }else if(!render_surface_trail_cell(&art[group],row,frame,
                    slot->x_q7/128+center,slot->y_q7/128,256u))return 0;
            }
        }
    }
    return 1;
}

int portable_race_scene_render_surface_trails(const PortableRaceParticleAssetView art[5],
    const PortableSurfaceTrailRules *rules,const PortableSurfaceTrail *trails,
    const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame)
{ return render_surface_trails_precise(art,rules,trails,body,frame,NULL); }

/* World actors share source priority 1. The source sorts projected depth
 * before assigning sprite order; allocation order only resolves equal depths.
 * Keep screen-space weather/HUD outside this world queue. */
typedef enum {
    WORLD_BOO_ATTACK,WORLD_GREEN,WORLD_BANANA,WORLD_RED,
    WORLD_LAVA,WORLD_THWOMP,WORLD_CRAB,WORLD_SEAGULL,WORLD_CANNON,WORLD_PIRANHA,
    WORLD_SNOWMAN,WORLD_MOUSE,WORLD_THUNDER,WORLD_STAR,WORLD_PENGUIN,WORLD_BOO,
    WORLD_SCENERY,WORLD_RIDER,WORLD_BOX,WORLD_LIGHTNING,WORLD_OPPONENT,WORLD_GHOST,WORLD_PLAYER
} WorldSpriteKind;
typedef struct { WorldSpriteKind kind; size_t index,order; int16_t depth; uint32_t depth_q16; } WorldSprite;
typedef struct {
    const PortableRaceSceneView *scene;
    const PortableRacePresentationState *state;
    const PortableCameraState *camera;
    const PortableCameraProjection *projection;
    PortableSoftwareFrame *frame;
    const PortableCameraProjectionResult *kart, *player_body;
    const PortableCameraProjectionPreciseResult *player_body_precise;
    int player_visible, recovery_y_offset, hop_y_offset;
} WorldSpriteContext;
enum
{
    WORLD_SPRITE_CAPACITY = 2u * PORTABLE_COURSE_SCENERY_MAX_OBJECTS +
        PORTABLE_LAVA_BUBBLE_CAPACITY + PORTABLE_THWOMP_CAPACITY + PORTABLE_CRAB_CAPACITY +
        PORTABLE_SEAGULL_CAPACITY + PORTABLE_CANNONBALL_CAPACITY + PORTABLE_PIRANHA_CAPACITY +
        PORTABLE_SNOWMAN_CAPACITY + PORTABLE_MOUSE_CAPACITY + PORTABLE_THUNDERCLOUD_CAPACITY +
        PORTABLE_FALLING_STAR_CAPACITY + PORTABLE_PENGUIN_CAPACITY + PORTABLE_BOO_CAPACITY +
        3u * 24u + PORTABLE_BOO_ATTACK_CAPACITY + PORTABLE_RACE_SCENE_MAX_ITEM_BOXES +
        PORTABLE_RACE_SCENE_MAX_OPPONENTS + 3u
};
static int world_sprite_compare(const void *left,const void *right)
{
    const WorldSprite *a=left,*b=right;
    if(a->depth!=b->depth)return a->depth>b->depth?-1:1;
    return a->order<b->order?-1:a->order>b->order?1:0;
}
static int world_sprite_add(WorldSprite *sprites,size_t *count,const WorldSpriteContext *ctx,
    WorldSpriteKind kind,size_t index,int32_t x,int32_t y,int32_t z)
{
    PortableCameraProjectionResult projected;
    if(portable_camera_projection_project(ctx->camera,ctx->projection,x,y,z,&projected))return 1;
    if(*count>=WORLD_SPRITE_CAPACITY)return 0;
    uint32_t depth_q16=(uint32_t)projected.distance*65536u;
    if(ctx->scene->scene_3d){
        PortableCameraProjectionPreciseResult precise;
        if(portable_camera_projection_project_precise(ctx->camera,ctx->projection,x,y,z,&precise))return 0;
        depth_q16=(uint32_t)precise.distance_q16;
    }
    sprites[*count]=(WorldSprite){kind,index,*count,projected.distance,depth_q16};
    ++*count;return 1;
}
static int render_driver_model(const WorldSpriteContext *ctx,uint32_t driver_id,
    int32_t x,int32_t y,int32_t height,uint16_t visual_yaw,uint16_t base,
    uint16_t affine_x,uint16_t affine_y,uint8_t flattened,int8_t bob,
    uint8_t pose_override,int screen_y_offset)
{
    const PortableModelPose pose={.position_x_q16=x,.position_y_q16=y,
        .height_q16=height,.visual_yaw=visual_yaw,.base_inverse_q8=base,
        .inverse_x_q8=affine_x?affine_x:(flattened?224u:256u),
        .inverse_y_q8=affine_y?affine_y:(flattened?384u:256u),
        .screen_y_offset_q16=screen_y_offset*65536,.bob_offset=bob,.source_driver=1u};
    return render_bound_model(ctx->scene,ctx->state,ctx->camera,ctx->projection,
        ctx->frame,PORTABLE_MODEL_DRIVER,driver_id,0u,pose_override,&pose);
}

static int world_opponent(const WorldSpriteContext *ctx,size_t index)
{
    const PortableRaceSceneView *scene=ctx->scene;
    const PortableRacePresentationState *state=ctx->state;
    const PortableCameraState *camera=ctx->camera;
    const PortableCameraProjection *projection=ctx->projection;
    PortableSoftwareFrame *frame=ctx->frame;
    const PortableRaceOpponentPresentation *opponent =
        &state->opponents[index];
    const PortableCharacterAssetView *character =
        scene->opponent_characters[index];
    if (opponent->revenge_bomb) {
        character=scene->battle_bomb;
        if (character==NULL || character->direction_tiles==NULL) {
            fprintf(stderr,"Battle opponent=%zu bomb graphics unavailable; reimport assets\n",index);
            return 0;
        }
    }
    PortableCameraProjectionResult projected;
    int y_offset = -(int)(opponent->height_q16 >> 16);
    if (!opponent->visible || character == NULL)
        return 1;
    PortableCharacterAssetView displayed=*character;
    uint8_t bomb_slots[64];
    if (opponent->revenge_bomb && !bomb_walk_view(&displayed,bomb_slots,
            opponent->bomb_walk_phase,opponent->bomb_speed_q16,
            opponent->character_view_angle,opponent->bomb_response)) return 0;
    uint16_t palette[PORTABLE_CHARACTER_PALETTE_COUNT];
    if (!portable_character_asset_palette(character,opponent->palette_bank,palette)) {
        fprintf(stderr,"race scene opponent=%zu palette=%u unavailable; reimport assets\n",
            index,opponent->palette_bank);
        return 0;
    }
    if(!opponent->revenge_bomb)driver_wheel_palette(character,&opponent->art_motion,palette);
    displayed.palette=palette;
    character=&displayed;
    if (
        portable_camera_projection_project(
            camera, projection, opponent->position_x_q16,
            opponent->position_y_q16, 0, &projected) != 0)
        return 1;
    PortableCameraProjectionResult body=projected;
    if(scene->driver_projection!=NULL){
        if(portable_camera_projection_body(&projected,opponent->height_q16,&body))return 0;
        y_offset=body.y-projected.y;
    }
    if (!render_balloons(scene,state,frame,&projected,y_offset,
            opponent->heading,opponent->balloons_remaining,0)) return 0;
    const int modeled=!opponent->revenge_bomb && render_driver_model(ctx,
        scene->opponent_character_ids[index],opponent->position_x_q16,
        opponent->position_y_q16,opponent->height_q16,opponent->visual_yaw,
        opponent->character_affine_scale,opponent->driver_affine_x,
        opponent->driver_affine_y,opponent->flattened,opponent->art_motion.cached_bob_offset,
        opponent->art_pose_valid?opponent->art_pose.frame_override:0u,0);
    if (!modeled && character->direction_tiles != NULL &&
        character->view_frame_slots != NULL)
    {
        const uint16_t scale=opponent->character_affine_scale;
        const PortableCharacterViewPose pose=opponent->art_pose_valid&&!opponent->revenge_bomb?opponent->art_pose:
            (PortableCharacterViewPose){opponent->character_view_angle,0u,(uint8_t)(opponent->character_view_angle>63u)};
        const int rendered=scene->driver_projection!=NULL
            ? portable_character_render_driver_pose_projected(character,frame,scene->driver_projection,
                &body,&pose,scale,
                opponent->driver_affine_x?opponent->driver_affine_x:(opponent->flattened?224u:256u),
                opponent->driver_affine_y?opponent->driver_affine_y:(opponent->flattened?384u:256u),
                opponent->art_motion.cached_bob_offset)
            : opponent->art_pose_valid&&!opponent->revenge_bomb
            ? portable_character_render_driver_pose(character,frame,projected.x-32,projected.y-32+y_offset,
                &pose,squash_coefficient(scale,opponent->driver_affine_x?opponent->driver_affine_x:(opponent->flattened?224u:256u)),
                squash_coefficient(scale,opponent->driver_affine_y?opponent->driver_affine_y:(opponent->flattened?384u:256u)))
            : opponent->driver_affine_x && opponent->driver_affine_y
            ? portable_character_render_driver_view(character,frame,projected.x-32,
                projected.y-32+y_offset,opponent->character_view_angle,
                squash_coefficient(scale,opponent->driver_affine_x),
                squash_coefficient(scale,opponent->driver_affine_y))
            : opponent->flattened
            ? portable_character_render_driver_view(character,frame,projected.x-32,
                projected.y-32+y_offset,opponent->character_view_angle,
                squash_coefficient(scale,0xe0u),squash_coefficient(scale,0x180u))
            : portable_character_render_view_affine(character,frame,projected.x-32,
                projected.y-32+y_offset,opponent->character_view_angle,0,scale);
        if (!rendered)
        {
            fprintf(stderr, "race scene opponent character failed\n");
            return 0;
        }
    }
    else if (!modeled && !portable_character_render_affine(
                 character, frame, projected.x - 32,
                 projected.y - 32 + y_offset, 0, 0,
                 opponent->character_affine_scale))
    {
        fprintf(stderr, "race scene opponent character failed\n");
        return 0;
    }
    if (!render_balloons(scene,state,frame,&projected,y_offset,
            opponent->heading,opponent->balloons_remaining,1)) return 0;
    return 1;
}
static int world_ghost(const WorldSpriteContext *ctx)
{
    const PortableRaceSceneView *scene=ctx->scene;
    const PortableRacePresentationState *state=ctx->state;
    const PortableCameraState *camera=ctx->camera;
    const PortableCameraProjection *projection=ctx->projection;
    PortableSoftwareFrame *frame=ctx->frame;
    PortableCameraProjectionResult ghost_kart;
    const int ghost_hop_y_offset=-(int)(state->ghost_height_q16>>16);
    if (!state->suppress_racers && state->ghost_visible &&
        scene->ghost_character != NULL &&
        portable_camera_projection_project(
            camera, projection, state->ghost_position_x_q16,
            state->ghost_position_y_q16, 0, &ghost_kart) == 0)
    {
        if(render_driver_model(ctx,scene->ghost_character_id,state->ghost_position_x_q16,
            state->ghost_position_y_q16,state->ghost_height_q16,state->ghost_visual_yaw,
            state->ghost_affine_scale,state->ghost_affine_x,state->ghost_affine_y,
            state->ghost_flattened,state->ghost_art_motion.cached_bob_offset,
            state->ghost_art_pose_valid?state->ghost_art_pose.frame_override:0u,0))return 1;
        if (scene->ghost_character->direction_tiles != NULL &&
            scene->ghost_character->view_frame_slots != NULL)
        {
            PortableCharacterAssetView ghost_character=*scene->ghost_character;
            uint16_t ghost_palette[16];
            if(!portable_character_asset_palette(&ghost_character,state->ghost_palette_bank,ghost_palette))return 0;
            driver_wheel_palette(&ghost_character,&state->ghost_art_motion,ghost_palette);
            ghost_character.palette=ghost_palette;
            PortableCameraProjectionResult ghost_body=ghost_kart;
            if(scene->driver_projection!=NULL &&
               portable_camera_projection_body(&ghost_kart,state->ghost_height_q16,&ghost_body))return 0;
            const uint16_t gx=state->ghost_affine_x?state->ghost_affine_x:
                (state->ghost_flattened?224u:256u);
            const uint16_t gy=state->ghost_affine_y?state->ghost_affine_y:
                (state->ghost_flattened?384u:256u);
            const PortableCharacterViewPose pose=state->ghost_art_pose_valid?state->ghost_art_pose:
                (PortableCharacterViewPose){state->ghost_character_view_angle,0u,(uint8_t)(state->ghost_character_view_angle>63u)};
            const int rendered=scene->driver_projection!=NULL
                ? portable_character_render_driver_pose_projected(&ghost_character,frame,
                    scene->driver_projection,&ghost_body,&pose,
                    state->ghost_affine_scale,gx,gy,state->ghost_art_motion.cached_bob_offset)
                : portable_character_render_driver_pose(
                    &ghost_character, frame, ghost_kart.x - 32,
                    ghost_kart.y - 32 + ghost_hop_y_offset,
                    &pose,
                    squash_coefficient(state->ghost_affine_scale,gx),
                    squash_coefficient(state->ghost_affine_scale,gy));
            if(!rendered)
            {
                fprintf(stderr, "race scene ghost character failed\n");
                return 0;
            }
        }
        else if (!portable_character_render(
                     scene->ghost_character, frame, ghost_kart.x - 32,
                     ghost_kart.y - 32 + ghost_hop_y_offset, 0, 0))
        {
            fprintf(stderr, "race scene ghost character failed\n");
            return 0;
        }
    }

    return 1;
}
static int world_player(const WorldSpriteContext *ctx)
{
    const PortableRaceSceneView *scene=ctx->scene;
    const PortableRacePresentationState *state=ctx->state;
    PortableSoftwareFrame *frame=ctx->frame;
    const PortableCameraProjectionResult kart=*ctx->kart,player_body=*ctx->player_body;
    const int player_visible=ctx->player_visible,recovery_y_offset=ctx->recovery_y_offset,hop_y_offset=ctx->hop_y_offset;
    const int player_origin_x=kart.x-32;
    const int player_origin_y=kart.y-32+recovery_y_offset+hop_y_offset;
    const uint16_t player_scale=state->character_affine_scale;
    if (player_visible && !state->suppress_racers && !state->suppress_player &&
        !render_balloons(scene,state,frame,&kart,recovery_y_offset+hop_y_offset,
            state->balloon_heading,state->balloons_remaining,0)) return 0;
    const PortableCharacterAssetView *local_character=state->revenge_bomb?
        scene->battle_bomb:scene->character;
    if (state->revenge_bomb && (local_character==NULL || local_character->direction_tiles==NULL)) {
        fprintf(stderr,"Battle player bomb graphics unavailable; reimport assets\n");
        return 0;
    }
    const int modeled=player_visible && !state->suppress_racers && !state->suppress_player &&
        !state->revenge_bomb && render_driver_model(ctx,scene->character_id,
            state->position_x_q16,state->position_y_q16,state->height_q16,state->visual_yaw,
            player_scale,state->driver_affine_x,state->driver_affine_y,state->flattened,
            state->art_motion.cached_bob_offset,state->art_pose_valid?state->art_pose.frame_override:0u,
            recovery_y_offset);
    if (!modeled && player_visible && !state->suppress_racers && !state->suppress_player &&
        local_character != NULL &&
        local_character->direction_tiles != NULL &&
        local_character->view_frame_slots != NULL &&
        local_character->palette != NULL &&
        local_character->palette_count >= PORTABLE_CHARACTER_PALETTE_COUNT)
    {
        PortableCharacterAssetView player_character = *local_character;
        uint8_t bomb_slots[64];
        if (state->revenge_bomb && !bomb_walk_view(&player_character,bomb_slots,
                state->bomb_walk_phase,state->bomb_speed_q16,
                state->character_view_angle,state->bomb_response)) return 0;
        uint16_t player_palette[PORTABLE_CHARACTER_PALETTE_COUNT];
        if(!portable_character_asset_palette(local_character,state->palette_bank,player_palette)){
            fprintf(stderr,"race scene character=%u palette=%u banks=%zu unavailable; reimport assets\n",
                local_character->character_id,state->palette_bank,local_character->palette_bank_count);
            return 0;
        }
        if(!state->revenge_bomb)driver_wheel_palette(local_character,&state->art_motion,player_palette);
        player_character.palette=player_palette;
        const PortableCharacterViewPose pose=state->art_pose_valid&&!state->revenge_bomb?state->art_pose:
            (PortableCharacterViewPose){state->character_view_angle,0u,(uint8_t)(state->character_view_angle>63u)};
        const int rendered = scene->driver_projection!=NULL
            ? portable_character_render_driver_pose_projected(&player_character,frame,scene->driver_projection,
                &player_body,&pose,player_scale,
                state->driver_affine_x?state->driver_affine_x:(state->flattened?224u:256u),
                state->driver_affine_y?state->driver_affine_y:(state->flattened?384u:256u),
                state->art_motion.cached_bob_offset)
            : state->art_pose_valid&&!state->revenge_bomb
            ? portable_character_render_driver_pose(&player_character,frame,player_origin_x,player_origin_y,&pose,
                squash_coefficient(player_scale,state->driver_affine_x?state->driver_affine_x:(state->flattened?224u:256u)),
                squash_coefficient(player_scale,state->driver_affine_y?state->driver_affine_y:(state->flattened?384u:256u)))
            : state->driver_affine_x && state->driver_affine_y
            ? portable_character_render_driver_view(
                &player_character,frame,player_origin_x,player_origin_y,
                state->character_view_angle,squash_coefficient(player_scale,state->driver_affine_x),
                squash_coefficient(player_scale,state->driver_affine_y))
            : state->flattened
            ? portable_character_render_driver_view(
                &player_character,frame,player_origin_x,player_origin_y,
                state->character_view_angle,squash_coefficient(player_scale,0xe0u),
                squash_coefficient(player_scale,0x180u))
            : portable_character_render_view_affine(
                &player_character, frame, player_origin_x,
                player_origin_y,
                state->character_view_angle, 0,
                player_scale);
        if (!rendered)
        {
            fprintf(stderr, "race scene directional character failed\n");
            return 0;
        }
    }
    else if (!modeled && player_visible && !state->suppress_racers && !state->suppress_player &&
             !portable_character_render_affine(
                 scene->character, frame, player_origin_x,
                 player_origin_y, 0, 0, player_scale))
    {
        fprintf(stderr, "race scene character failed\n");
        return 0;
    }
    if (player_visible && !state->suppress_racers && !state->suppress_player &&
        !render_balloons(scene,state,frame,&kart,recovery_y_offset+hop_y_offset,
            state->balloon_heading,state->balloons_remaining,1)) return 0;

    return 1;
}
static int world_sprite_draw(const WorldSpriteContext *ctx,const WorldSprite *sprite)
{
    const PortableRaceSceneView *scene=ctx->scene;
    const PortableRacePresentationState *state=ctx->state;
    const PortableCameraState *camera=ctx->camera;
    const PortableCameraProjection *projection=ctx->projection;
    PortableSoftwareFrame *frame=ctx->frame;
    const size_t index=sprite->index;
    PortableCameraProjectionResult projected;
    switch(sprite->kind){
    case WORLD_LAVA: return render_lava_bubbles(scene,state,camera,projection,frame,index);
    case WORLD_THWOMP: return render_thwomps(scene,state,camera,projection,frame,index);
    case WORLD_CRAB: return render_crabs(scene,state,camera,projection,frame,index);
    case WORLD_SEAGULL: return render_seagulls(scene,state,camera,projection,frame,index);
    case WORLD_CANNON: return render_cannonballs(scene,state,camera,projection,frame,index);
    case WORLD_PIRANHA: return render_piranhas(scene,state,camera,projection,frame,index);
    case WORLD_SNOWMAN: return render_snowmen(scene,state,camera,projection,frame,index);
    case WORLD_MOUSE: return render_mice(scene,state,camera,projection,frame,index);
    case WORLD_THUNDER: return render_thunderclouds(scene,state,camera,projection,frame,index);
    case WORLD_STAR: return render_falling_stars(scene,state,camera,projection,frame,index);
    case WORLD_PENGUIN: return render_penguins(scene,state,camera,projection,frame,index);
    case WORLD_BOO: return render_boos(scene,state,camera,projection,frame,index);
    case WORLD_SCENERY: case WORLD_RIDER:
        return render_course_scenery(scene,camera,projection,frame,state,index,sprite->kind==WORLD_RIDER);
    case WORLD_OPPONENT:return world_opponent(ctx,index);
    case WORLD_GHOST:return world_ghost(ctx);
    case WORLD_PLAYER:return world_player(ctx);
    case WORLD_LIGHTNING:
        if (ctx->player_body_precise)
            return portable_lightning_effect_render_positioned(
                scene->lightning_effect, scene->charged_exit_particle, frame,
                ctx->player_body_precise->x_q16, ctx->player_body_precise->y_q16,
                state->lightning_effect_frame);
        return portable_lightning_effect_render(
            scene->lightning_effect, scene->charged_exit_particle, frame, ctx->kart->x,
            ctx->kart->y + ctx->recovery_y_offset + ctx->hop_y_offset,
            state->lightning_effect_frame);
    case WORLD_BOX:
    {
        const PortableRaceItemBoxPresentation *box = &state->item_boxes[index];
        if (project_course_actor(camera, projection, box->position_x_q16, box->position_y_q16,
                                 box->height_q16, &projected))
            return 1;
        return portable_item_box_render_projected(scene->item_box, frame, projected.x, projected.y,
                                                  state->total_ticks, (uint16_t)projected.distance);
    }
    case WORLD_BOO_ATTACK:{
        const PortableBooAttack *boo=&state->boo_attacks[index];
        if(project_course_actor(camera,projection,boo->x_q16,boo->y_q16,boo->z_q16,&projected))return 1;
        return portable_boo_attack_render(scene->charged_exit_particle,frame,projected.x,projected.y,boo->frame,(uint8_t)(boo->affine_y_q8<0));
    }
    case WORLD_GREEN:case WORLD_BANANA:case WORLD_RED:{
        const PortableRaceItemPresentation *item=sprite->kind==WORLD_GREEN?&state->green_shells[index]:
            sprite->kind==WORLD_BANANA?&state->bananas[index]:&state->red_shells[index];
        if(project_course_actor(camera,projection,item->x_q16,item->y_q16,item->z_q16,&projected))return 1;
        if(sprite->kind==WORLD_GREEN)return portable_green_shell_render_projected(scene->charged_exit_particle,frame,projected.x,projected.y,item->animation_index,(uint16_t)projected.distance);
        if(sprite->kind==WORLD_BANANA)return portable_banana_render_projected(scene->charged_exit_particle,frame,projected.x,projected.y,(uint16_t)projected.distance);
        return (item->kind==0x8403u?portable_spiny_shell_render_projected:portable_red_shell_render_projected)(scene->charged_exit_particle,frame,projected.x,projected.y,item->animation_index,(uint16_t)projected.distance);
    }
    }
    return 0;
}

/* Depth is borrowed for this queue only. Screen-space effects and HUD retain
 * their source compositing, and no failed draw can retain a stack pointer. */
static int world_sprite_draw_queue(const WorldSpriteContext *ctx,
    const WorldSprite *sprites,size_t count)
{
    PortableSoftwareDepth depth={0};
    if(ctx->scene->scene_3d){
        PortableScene3D *scene=ctx->scene->scene_3d;
        /* Source cells extend below their logical ground anchor (wheels,
         * shadows, squash). Keep that authored coverage over the flat course,
         * while testing every world billboard against every other one. */
        depth.pixels=scene->billboard_depth;depth.stride=scene->width;
        ctx->frame->depth=&depth;
    }
    int ok=1;
    for(size_t i=0u;i<count;++i){
        depth.value=sprites[i].depth_q16;
        if(!world_sprite_draw(ctx,&sprites[i])){ok=0;break;}
    }
    ctx->frame->depth=NULL;
    return ok;
}
static int add_racer_sprites(const WorldSpriteContext *ctx,WorldSprite *sprites,size_t *count)
{
    const PortableRaceSceneView *scene=ctx->scene;
    const PortableRacePresentationState *state=ctx->state;
    const int player_visible=ctx->player_visible;
#define ADD(kind,index,x,y,z) do{if(!world_sprite_add(sprites,count,ctx,kind,index,x,y,z))return 0;}while(0)
    size_t opponents=state->suppress_racers?0u:state->opponent_count;
    if(opponents>scene->opponent_count)opponents=scene->opponent_count;
    if(opponents>PORTABLE_RACE_SCENE_MAX_OPPONENTS)opponents=PORTABLE_RACE_SCENE_MAX_OPPONENTS;
    for(size_t n=opponents;n>0u;--n){const size_t i=n-1u;const PortableRaceOpponentPresentation *a=&state->opponents[i];
        if(a->visible)ADD(WORLD_OPPONENT,i,a->position_x_q16,a->position_y_q16,0);
    }
    if(!state->suppress_racers && state->ghost_visible && scene->ghost_character)
        ADD(WORLD_GHOST,0u,state->ghost_position_x_q16,state->ghost_position_y_q16,0);
    if(player_visible && !state->suppress_racers && !state->suppress_player)
        ADD(WORLD_PLAYER,0u,state->position_x_q16,state->position_y_q16,0);
#undef ADD
    return 1;
}

static int
render_world_sprites(const PortableRaceSceneView *scene, const PortableRacePresentationState *state,
                     const PortableCameraState *camera, const PortableCameraProjection *projection,
                     PortableSoftwareFrame *frame, const PortableCameraProjectionResult *kart,
                     const PortableCameraProjectionResult *player_body,
                     const PortableCameraProjectionPreciseResult *player_body_precise,
                     int player_visible, int recovery_y_offset, int hop_y_offset)
{
    const WorldSpriteContext ctx = {scene,          state,
                                    camera,         projection,
                                    frame,          kart,
                                    player_body,    player_body_precise,
                                    player_visible, recovery_y_offset,
                                    hop_y_offset};
    WorldSprite sprites[WORLD_SPRITE_CAPACITY];
    size_t count = 0u;
    if(!state->suppress_scenery){
        if(!render_lava_bubbles(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_thwomps(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_crabs(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_seagulls(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_cannonballs(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_piranhas(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_snowmen(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_mice(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_thunderclouds(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_falling_stars(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_penguins(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_boos(scene,state,camera,projection,frame,SIZE_MAX))return 0;
        if(!render_course_scenery(scene,camera,projection,frame,state,SIZE_MAX,0))return 0;
    }
#define ADD(kind,index,x,y,z) do{if(!world_sprite_add(sprites,&count,&ctx,kind,index,x,y,z))return 0;}while(0)
    for(size_t i=0u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i){
        const PortableBooAttack *a=&state->boo_attacks[i];
        if(a->occupied&&a->visible)ADD(WORLD_BOO_ATTACK,i,a->x_q16,a->y_q16,a->z_q16);
    }
    for(size_t i=0u;i<24u;++i){
        const PortableRaceItemPresentation *a=&state->green_shells[i];
        if(a->visible)ADD(WORLD_GREEN,i,a->x_q16,a->y_q16,a->z_q16);
    }
    for(size_t i=0u;i<24u;++i){
        const PortableRaceItemPresentation *a=&state->bananas[i];
        if(a->visible)ADD(WORLD_BANANA,i,a->x_q16,a->y_q16,a->z_q16);
    }
    for(size_t i=0u;i<24u;++i){
        const PortableRaceItemPresentation *a=&state->red_shells[i];
        if(a->visible)ADD(WORLD_RED,i,a->x_q16,a->y_q16,a->z_q16);
    }
    if(!state->suppress_scenery){
        for(size_t n=state->lava_bubbles.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_LAVA,i,state->lava_bubbles.actors[i].x_q16,state->lava_bubbles.actors[i].y_q16,0);}
        for(size_t n=state->thwomps.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_THWOMP,i,state->thwomps.actors[i].x_q16,state->thwomps.actors[i].y_q16,0);}
        for(size_t n=state->crabs.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_CRAB,i,state->crabs.actors[i].x_q16,state->crabs.actors[i].y_q16,0);}
        for(size_t n=state->seagulls.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_SEAGULL,i,state->seagulls.actors[i].x_q16,state->seagulls.actors[i].y_q16,0);}
        for(size_t n=state->cannonballs.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_CANNON,i,state->cannonballs.actors[i].x_q16,state->cannonballs.actors[i].y_q16,0);}
        for(size_t n=state->piranhas.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_PIRANHA,i,state->piranhas.actors[i].x_q16,state->piranhas.actors[i].y_q16,0);}
        for(size_t n=state->snowmen.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_SNOWMAN,i,state->snowmen.actors[i].x_q16,state->snowmen.actors[i].y_q16,0);}
        for(size_t n=state->mice.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_MOUSE,i,state->mice.actors[i].x_q16,state->mice.actors[i].y_q16,0);}
        for(size_t n=state->thunderclouds.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_THUNDER,i,state->thunderclouds.actors[i].x_q16,state->thunderclouds.actors[i].y_q16,0);}
        for(size_t n=state->falling_stars.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_STAR,i,state->falling_stars.actors[i].x_q16,state->falling_stars.actors[i].y_q16,0);}
        for(size_t n=state->penguins.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_PENGUIN,i,state->penguins.actors[i].x_q16,state->penguins.actors[i].y_q16,0);}
        for(size_t n=state->boos.count;n>0u;--n){const size_t i=n-1u;
            ADD(WORLD_BOO,i,state->boos.actors[i].x_q16,state->boos.actors[i].y_q16,0);}
        if(scene->scenery)for(size_t n=scene->scenery->object_count;n>0u;--n){
            const size_t i=n-1u;const PortableCourseSceneryObject *a=&scene->scenery->objects[i];
            const PortableCourseProp *prop=state->course_props.count==scene->scenery->object_count?&state->course_props.actors[i]:NULL;
            if(prop && (prop->source_code>>16u)==3u && prop->teepee.rider_state)
                ADD(WORLD_RIDER,i,prop->teepee.rider_x_q16,prop->teepee.rider_y_q16,0);
            ADD(WORLD_SCENERY,i,prop?prop->x_q16:a->position_x_q16,prop?prop->y_q16:a->position_y_q16,0);
        }
    }
    size_t boxes=state->item_box_count;
    if(boxes>PORTABLE_RACE_SCENE_MAX_ITEM_BOXES)boxes=PORTABLE_RACE_SCENE_MAX_ITEM_BOXES;
    if(scene->item_box)for(size_t n=boxes;n>0u;--n){const size_t i=n-1u;
        const PortableRaceItemBoxPresentation *box=&state->item_boxes[i];
        if(box->active)ADD(WORLD_BOX,i,box->position_x_q16,box->position_y_q16,box->height_q16);
    }
    if(player_visible && state->lightning_effect_visible)
        ADD(WORLD_LIGHTNING,0u,state->position_x_q16,state->position_y_q16,0);
    if(!add_racer_sprites(&ctx,sprites,&count))return 0;
#undef ADD
    qsort(sprites,count,sizeof sprites[0],world_sprite_compare);
    return world_sprite_draw_queue(&ctx,sprites,count);
}

int portable_race_scene_render_racers(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,PortableSoftwareFrame *frame)
{
    if(!scene||!state||!frame||!frame->pixels||frame->width<240u||!scene->sine||scene->sine_count<1024u)return 0;
    PortableCameraState camera={0};PortableCameraProjection projection;
    PortableCameraProjectionResult ground={.distance=256},body;
    if(!setup_presentation_camera(&camera,state,
            (unsigned)portable_software_frame_scale(frame)))return 0;
    camera.screen_x+=(int32_t)((frame->width-240u)/2u);
    const unsigned direction=((int16_t)state->camera_yaw/64)&1023u;
    if(portable_camera_projection_build(&camera,scene->sine[direction],scene->sine[(256u-direction)&1023u],31414,9319,&projection))return 0;
    const int status=portable_camera_projection_project(&camera,&projection,state->position_x_q16,state->position_y_q16,0,&ground);
    if(status!=PORTABLE_CAMERA_PROJECTION_OK&&status!=PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA&&
       status!=PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE)return 0;
    body=ground;
    if (status == PORTABLE_CAMERA_PROJECTION_OK &&
        portable_camera_projection_body(&ground, state->height_q16, &body))
        return 0;
    const WorldSpriteContext ctx = {scene,
                                    state,
                                    &camera,
                                    &projection,
                                    frame,
                                    &ground,
                                    &body,
                                    NULL,
                                    status == PORTABLE_CAMERA_PROJECTION_OK,
                                    0,
                                    body.y - ground.y};
    WorldSprite sprites[PORTABLE_RACE_SCENE_MAX_OPPONENTS + 2u];
    size_t count = 0u;
    if (!add_racer_sprites(&ctx, sprites, &count))
        return 0;
    if(scene->scene_3d && !portable_scene_3d_begin(scene->scene_3d,frame))return 0;
    qsort(sprites,count,sizeof sprites[0],world_sprite_compare);
    const int ok=world_sprite_draw_queue(&ctx,sprites,count);
    if(scene->scene_3d)scene->scene_3d->frame=NULL;
    return ok;
}

typedef struct {
    PortableRaceSceneView animated_scene;
    PortableSkyAssetView animated_sky;
    uint8_t sky_tiles[0x2800];
    uint8_t sky_front[0x1000], sky_mid[0x1000], sky_back[0x1000];
} SceneAmbientFrame;

/* Patch borrowed content into per-draw storage. The caller keeps this storage
 * alive through every layer draw; imported assets and snapshots stay immutable. */
static const PortableRaceSceneView *resolve_ambient_scene(
    const PortableRaceSceneView *scene, const PortableRacePresentationState *state,
    SceneAmbientFrame *scratch)
{
    if(state->ambient.initialized){
        if(scene->sky==NULL || state->ambient_rules==NULL ||
           state->ambient.image_patch_count>PORTABLE_AMBIENT_IMAGE_PATCHES)return NULL;
        scratch->animated_scene=*scene;
        scratch->animated_sky=*scene->sky;
        scratch->animated_sky.palette=state->ambient.sky;
        scratch->animated_scene.sky=&scratch->animated_sky;
        scratch->animated_scene.object_palette=state->ambient.object;
        scratch->animated_scene.object_palette_count=128u;
        scratch->animated_scene.object_palette_dirty=state->ambient.object_dirty;
        if(state->ambient.image_patch_count!=0u){
            const size_t lengths[4]={scratch->animated_sky.tile_bytes,scratch->animated_sky.front_map_bytes,
                scratch->animated_sky.mid_map_bytes,scratch->animated_sky.back_map_bytes};
            uint8_t *const destinations[4]={scratch->sky_tiles,scratch->sky_front,scratch->sky_mid,scratch->sky_back};
            const uint8_t *const sources[4]={scratch->animated_sky.tiles,scratch->animated_sky.front_map,
                scratch->animated_sky.mid_map,scratch->animated_sky.back_map};
            for(unsigned i=0u;i<4u;++i){
                if(lengths[i]>(i==0u?sizeof scratch->sky_tiles:sizeof scratch->sky_front) || sources[i]==NULL)return NULL;
                memcpy(destinations[i],sources[i],lengths[i]);
            }
            for(unsigned i=0u;i<state->ambient.image_patch_count;++i){
                const PortableAmbientPatch *patch=portable_ambient_patch(state->ambient_rules,
                    state->ambient.image_patch_ids[i]);
                if(patch==NULL || patch->target<PORTABLE_AMBIENT_SKY_TILES ||
                   patch->target>PORTABLE_AMBIENT_BACK_MAP)return NULL;
                const unsigned target=patch->target-PORTABLE_AMBIENT_SKY_TILES;
                if(patch->offset>lengths[target] || patch->count>lengths[target]-patch->offset ||
                   patch->data_offset>state->ambient_rules->data_size ||
                   patch->count>state->ambient_rules->data_size-patch->data_offset)return NULL;
                memcpy(destinations[target]+patch->offset,
                    state->ambient_rules->data+patch->data_offset,patch->count);
            }
            scratch->animated_sky.tiles=scratch->sky_tiles;scratch->animated_sky.front_map=scratch->sky_front;
            scratch->animated_sky.mid_map=scratch->sky_mid;scratch->animated_sky.back_map=scratch->sky_back;
        }
        return &scratch->animated_scene;
    }
    return scene;
}

static int render_scene_background(PortableRaceSceneView *scene,
    const PortableRacePresentationState *state, const PortableRoadView *road,
    const PortableCameraAffineLine *lines, size_t line_count, PortableSoftwareFrame *frame,
    const PortableCameraState *camera,const PortableCameraProjection *projection,int *fallback)
{
    /* AD00 flashes palette zero while 347B8 hides only the distant sky.
     * Opaque middle/foreground/road pixels retain their own colors. */
    portable_software_frame_clear(frame, state->lightning_scene_active ?
        (state->lightning_scene_white ? 0xffffffu : 0u) :
        state->ambient.initialized?scenery_rgb(state->ambient.road[0]):0x101828u);
    const int sky_back_ok = state->sky_motion.initialized ?
        portable_sky_render_motion_back(scene->sky,frame,&state->sky_motion) :
        state->lightning_scene_active ?
        ((state->source_sky_parallax
             ? portable_sky_render_middle_yaw(scene->sky,frame,(int16_t)state->camera_yaw)
             : portable_sky_render_middle(scene->sky,frame,0,0)) &&
         (state->source_sky_parallax ||
          portable_sky_render_front(scene->sky,frame,0,0))) :
        state->source_sky_parallax ?
        portable_sky_render_race_back(scene->sky,frame,(int16_t)state->camera_yaw) :
        portable_sky_render(scene->sky,frame,0,0);
    if (!sky_back_ok)
    {
        fprintf(stderr, "race scene sky failed\n");
        return 0;
    }
    if(scene->scene_3d){
        PortableScene3D *target=scene->scene_3d;
        const size_t scale=portable_software_frame_scale(frame);
        const int first=state->sky_motion.initialized?
            portable_sky_motion_road_first_row(&state->sky_motion,(unsigned)scale):
            (int)((state->source_sky_parallax?74u:64u)*scale);
        if(first<0)return 0;
        target->clip_top=(size_t)first;
        target->clip_bottom=frame->height<160u?frame->height*scale:160u*scale;
        if(state->sky_motion.initialized &&
           state->sky_motion.road_mode==PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT){
            target->source_weight=13u;target->destination_weight=6u;
        }
        PortableRaceSceneView ground=*scene;
        ground.tileset=road->tileset;
        const int shift=state->sky_motion.initialized?first-(int)(74u*scale):
            camera->projection_y_offset*(int)scale;
        const PortableRaceGround3DStatus status=portable_race_ground_3d_render(
            target,&ground,camera,projection,shift,1.0,65535.0);
        target->clip_top=0u;target->clip_bottom=target->height;
        target->source_weight=16u;target->destination_weight=0u;
        if(status==PORTABLE_RACE_GROUND_3D_UNSUPPORTED){
            *fallback=1;scene->scene_3d=NULL;
            /* Recompose from the sky, including transparent ground holes. */
            return render_scene_background(scene,state,road,lines,line_count,frame,
                camera,projection,fallback);
        }
        if(status!=PORTABLE_RACE_GROUND_3D_OK){
            fprintf(stderr,"race scene 3D ground failed: status=%d map=%zux%zu\n",
                status,scene->world_map.width_tiles,scene->world_map.height_tiles);
            return 0;
        }
    }else if ((state->sky_motion.initialized ? portable_road_render_motion(
             road,lines,line_count,frame,&state->sky_motion) :
         state->source_sky_parallax ? portable_road_render_result_offset(
             road,lines,line_count,frame,0u) :
         portable_road_render(
             road,lines,line_count,frame)))
    {
        fprintf(stderr, "race scene road failed\n");
        return 0;
    }
    if(state->sky_motion.initialized){
        if(!portable_sky_render_motion_front(scene->sky,frame,&state->sky_motion))return 0;
    }else if(state->source_sky_parallax&&
       !portable_sky_render_race_front(scene->sky,frame,(int16_t)state->camera_yaw))return 0;
    return 1;
}

static int render_race_scene(
    const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,
    PortableSoftwareFrame *frame,int *fallback)
{
    PortableCameraState camera = {0};
    PortableCameraProjection projection;
    PortableCameraProjectionResult kart={.distance=256};
    PortableCameraAffineLine lines[PORTABLE_CAMERA_AFFINE_MAX_LINES*8u];
    PortableRoadView road;
    SceneAmbientFrame ambient_frame;
    uint8_t road_window[PORTABLE_ROAD_MAP_BYTES];
    PortableRoadMapView road_window_view;
    uint8_t camera_phase = 0;
    unsigned sine_index;
    int32_t sine;
    int32_t cosine;
    int recovery_y_offset = 0;
    int hop_y_offset;

    if (scene == NULL || state == NULL || frame == NULL ||
        scene->sine == NULL || scene->sine_count < 1024u)
        return 0;
    scene=resolve_ambient_scene(scene,state,&ambient_frame);
    if(scene==NULL)return 0;
    PortableRaceSceneView draw_scene=*scene;
    scene=&draw_scene;
    road = (PortableRoadView){.map = scene->world_map,
                             .tileset = scene->tileset};
    if(state->ambient.initialized)road.tileset.palette=state->ambient.road;
    /* math_sin/math_cos divide the signed 16-bit angle toward zero. A
     * logical shift chooses the preceding lookup slot for negative yaw. */
    sine_index = ((int16_t)state->camera_yaw / 64) & 1023u;
    sine = scene->sine[sine_index];
    cosine = scene->sine[(256u - sine_index) & 1023u];
    if(!setup_presentation_camera(&camera,state,
            (unsigned)portable_software_frame_scale(frame)))return 0;
    if(frame->width>240u)camera.screen_x+=(int32_t)((frame->width-240u)/2u);
    if (state->source_sky_parallax &&
        (road.map.width_tiles != PORTABLE_ROAD_MAP_WIDTH_TILES ||
         road.map.height_tiles != PORTABLE_ROAD_MAP_HEIGHT_TILES))
    {
        const PortableRoadMapBuffers window = {
            road_window, sizeof road_window};
        if (portable_road_map_asset_repack(
                &scene->world_map, &window,
                camera.position_x,camera.position_y,
                (int16_t)state->camera_yaw,
                scene->sine, scene->sine_count, &road_window_view) !=
            PORTABLE_ROAD_MAP_OK)
        {
            fprintf(stderr, "race scene result road window failed\n");
            return 0;
        }
        road.map = road_window_view;
    }
    uint16_t native_line_count=PORTABLE_CAMERA_AFFINE_LINES;
    if(state->sky_motion.initialized){
        const unsigned scale=(unsigned)portable_software_frame_scale(frame);
        const int first_row=portable_sky_motion_road_first_row(&state->sky_motion,scale);
        if(first_row<0)return 0;
        /* AF74 starts at record8 regardless of the current actor offset. */
        const unsigned required=(168u*scale-(unsigned)first_row+scale-1u)/scale;
        if(required>native_line_count)native_line_count=(uint16_t)required;
    }
    if (portable_camera_projection_build(
           &camera, sine, cosine, 31414, 9319, &projection) ||
        portable_camera_affine_build_scaled_rows(
            &camera, 0, 31414, 9319,
            sine, cosine, lines,
            PORTABLE_CAMERA_AFFINE_MAX_LINES*8u, &camera_phase,(uint8_t)portable_software_frame_scale(frame),native_line_count))
    {
        fprintf(stderr, "race scene camera failed\n");
        return 0;
    }

    const int player_projection=portable_camera_projection_project(
           &camera,&projection,state->position_x_q16,state->position_y_q16,0,&kart);
    const int player_visible=player_projection==PORTABLE_CAMERA_PROJECTION_OK;
    if(!player_visible && player_projection!=PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA &&
       player_projection!=PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE){
        fprintf(stderr,"race scene player projection failed: status=%d\n",player_projection);
        return 0;
    }

    if(!render_scene_background(&draw_scene,state,&road,lines,
            native_line_count*portable_software_frame_scale(frame),frame,
            &camera,&projection,fallback))return 0;
    if (scene->driver_projection == NULL) {
        if (state->recovery_phase == PORTABLE_WATER_RECOVERY_SINK)
            recovery_y_offset = state->recovery_phase_ticks < 32u
                ? (int)state->recovery_phase_ticks / 4 : 8;
        else if (state->recovery_phase == PORTABLE_WATER_RECOVERY_LIFT)
            recovery_y_offset = -(int)state->recovery_phase_ticks * 18 /
                                PORTABLE_WATER_RECOVERY_LIFT_TICKS;
        else if (state->recovery_phase == PORTABLE_WATER_RECOVERY_CARRY)
            recovery_y_offset = -18;
        else if (state->recovery_phase == PORTABLE_WATER_RECOVERY_LOWER)
            recovery_y_offset = -18 +
                (int)state->recovery_phase_ticks * 18 /
                    PORTABLE_WATER_RECOVERY_LOWER_TICKS;
    }
    hop_y_offset = -(int)(state->height_q16 >> 16);
    PortableCameraProjectionResult player_body=kart;
    if(player_visible && scene->driver_projection!=NULL){
        if(portable_camera_projection_body(&kart,state->height_q16,&player_body))return 0;
        hop_y_offset=player_body.y-kart.y;
    }
    player_body.y=(int16_t)((uint16_t)player_body.y+(uint16_t)recovery_y_offset);
    PortableCameraProjectionResult smoke_body=player_body;
    if(scene->driver_projection==NULL)
        smoke_body.y=(int16_t)((uint16_t)smoke_body.y+(uint16_t)hop_y_offset);
    PortableCameraProjectionPreciseResult ground_precise={0},fx_body_precise={0};
    const PortableCameraProjectionPreciseResult *fx_body=NULL;
    if(frame->scale>1u && player_visible){
        if(portable_camera_projection_project_precise(&camera,&projection,state->position_x_q16,
                state->position_y_q16,0,&ground_precise))return 0;
        fx_body_precise=ground_precise;
        if(scene->driver_projection!=NULL){
            if(portable_camera_projection_body_precise(&ground_precise,state->height_q16,&fx_body_precise))return 0;
        }else fx_body_precise.y_q16-=state->height_q16;
        fx_body_precise.y_q16+=recovery_y_offset*65536;
        fx_body=&fx_body_precise;
    }
    if (player_visible && scene->charged_exit_particle != NULL &&
        !(fx_body?portable_race_particle_render_positioned(scene->charged_exit_particle,frame,
            fx_body->x_q16,fx_body->y_q16,state->charged_particle_age,256u):
          portable_race_particle_render(scene->charged_exit_particle,frame,kart.x,
            kart.y+recovery_y_offset+hop_y_offset,state->charged_particle_age)))
    {
        fprintf(stderr, "race scene charged-exit particle failed\n");
        return 0;
    }
    /* BD34 keeps the birth screen height while 5B44C animates. Rendering
     * the immutable age never advances the particle or follows a later hop. */
    if (player_visible && scene->mushroom_burst != NULL && scene->mushroom_burst->frame_count != 0u &&
        !(fx_body?portable_race_particle_render_positioned(scene->mushroom_burst,frame,
            ground_precise.x_q16,ground_precise.y_q16-state->boost_particle_birth_height_q16,state->boost_particle_age,256u):
          portable_race_particle_render(scene->mushroom_burst,frame,kart.x,
            kart.y-(state->boost_particle_birth_height_q16>>16),state->boost_particle_age)))
    {
        fprintf(stderr, "race scene mushroom burst failed\n");
        return 0;
    }
    /* BDC4 fixes the entry splash at its birth screen position. It does not
     * follow a later sink/carry; widescreen only shifts the native center. */
    if(state->entry_splash.age && scene->entry_splash!=NULL && scene->entry_splash->frame_count!=0u &&
       !portable_race_particle_render_scaled(scene->entry_splash,frame,
           state->entry_splash.x+(frame->width>240u?(int32_t)(frame->width-240u)/2:0),
           state->entry_splash.y,state->entry_splash.age,state->entry_splash.scale_q8)){
        fprintf(stderr,"race scene entry splash failed\n");return 0;
    }
    if(player_visible && scene->lava_smoke && scene->lava_smoke->frame_count &&
       !render_lava_smoke_precise(scene->lava_smoke,&state->lava_smoke,
           &smoke_body,frame,fx_body)){
        fprintf(stderr,"race scene lava smoke failed\n");return 0;
    }
    if(scene->impact_effect_art && !portable_race_scene_render_impact_effects(scene->impact_effect_art,&state->impact_effects,frame)){
        fprintf(stderr,"race scene impacts failed\n");return 0;
    }
    if(scene->coin_effect_rules && scene->coin_effect_art &&
       !portable_race_scene_render_coin_effects(scene->coin_effect_art,&state->coin_effects,frame)){
        fprintf(stderr,"race scene dropped coins failed\n");return 0;
    }
    if(scene->coin_pickup && scene->coin_pickup->frame_count){
        for(unsigned i=0u;i<PORTABLE_COIN_PICKUP_EFFECT_CAPACITY;++i){
            const PortableCoinPickupEffect *effect=&state->coin_pickups[i];
            if(!(frame->scale>1u?portable_race_particle_render_positioned(scene->coin_pickup,frame,
                    (int32_t)effect->x*65536,effect->y_q16,effect->age,256u):
                  portable_race_particle_render(scene->coin_pickup,frame,effect->x,effect->y_q16>>16,effect->age))){
                fprintf(stderr,"race scene coin pickup failed\n");return 0;
            }
        }
    }
    if (!render_world_sprites(scene, state, &camera, &projection, frame, &kart, &player_body,
                              fx_body, player_visible, recovery_y_offset, hop_y_offset))
    {
        fprintf(stderr, "race scene world sprites failed\n");
        return 0;
    }
    if(player_visible && scene->recovery_fx_rules &&
       !render_recovery_fx_precise(scene->recovery_thaw,scene->recovery_drip,
           scene->recovery_fx_rules,&state->recovery_fx,&smoke_body,frame,fx_body)){
        fprintf(stderr,"race scene recovery effect failed\n");return 0;
    }
    if(scene->surface_trail_rules && scene->surface_trail_art &&
       !render_surface_trails_precise(scene->surface_trail_art,scene->surface_trail_rules,
           &state->surface_trails,&smoke_body,frame,fx_body)){
        fprintf(stderr,"race scene surface trail failed\n");return 0;
    }
    if(!render_snow_weather(scene,state,frame)){
        fprintf(stderr,"race scene snow weather failed\n");return 0;
    }
    if (!portable_race_scene_render_hud(scene,state,frame))
        return 0;
    if(state->gp_finish_banner_visible){
        if(scene->gp_finish_banner==NULL||
           !portable_gp_finish_banner_render(
               scene->gp_finish_banner,frame,state->gp_finish_banner_variant,
               state->gp_finish_banner_affine_scale,
               (int32_t)scene->gp_finish_banner->origin_x+(int32_t)((frame->width>240u?frame->width-240u:0u)/2u),
               state->gp_finish_banner_y)){
            fprintf(stderr,"race scene GP finish banner failed\n");
            return 0;
        }
    }

    return 1;
}

int portable_race_scene_render(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,PortableSoftwareFrame *frame)
{
    if(!scene || !state || !frame)return 0;
    frame->depth=NULL;
    PortableRaceSceneView draw_scene=*scene;
    PortableScene3D *target=scene->scene_3d;
    int fallback=0;
    if(target && !portable_scene_3d_begin(target,frame)){
        draw_scene.scene_3d=NULL;fallback=1;
    }
    const int ok=render_race_scene(&draw_scene,state,frame,&fallback);
    frame->depth=NULL;
    if(target){
        target->frame=NULL;
        if(fallback && !target->fallback_reported){
            fprintf(stderr,"race scene: 3D unavailable; using Original renderer (map=%zux%zu)\n",
                scene->world_map.width_tiles,scene->world_map.height_tiles);
            target->fallback_reported=1u;
        }
    }
    if(ok && fallback)
        portable_software_frame_text(frame,4,150,"Original renderer: 3D unavailable",0xffffffu);
    return ok;
}
