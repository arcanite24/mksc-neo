#include "gp_ceremony_renderer.h"
#include "character_renderer.h"
#include "track_set.h"
#include "menu_font.h"
#include <stdio.h>
#include <stdlib.h>
#include "frontend_config.h"

static void image(const PortableNativeImage *art,PortableSoftwareFrame *frame,int x,int y)
{
    for(unsigned dy=0;dy<art->height;++dy)
    for(unsigned dx=0;dx<art->width;++dx){
        const int tx=x+(int)dx,ty=y+(int)dy;
        if(tx<0||ty<0||(size_t)tx>=frame->width||(size_t)ty>=frame->height)continue;
        const uint32_t pixel=portable_native_image_pixel(art,dx,dy);
        if((pixel>>24)==255u)portable_software_frame_store(frame,(size_t)ty*frame->width+(size_t)tx, pixel);
    }
}
static void centered(const PortableNativeImage *art,PortableSoftwareFrame *frame,int x,int y)
{
    image(art,frame,x-(int)art->width/2,y);
}
static int render_legacy(const PortableAssetPackView *pack,
    const PortableGpCeremonyAssetView *asset,const PortableGpCeremonySession *ceremony,
    const PortableGpCeremonyPresentation *view,
    const PortableCharacterAssetView *const podium[3],
    const PortableCharacterAssetView *selected,PortableSoftwareFrame *frame)
{
    if(asset==NULL||ceremony==NULL||view==NULL||podium==NULL||selected==NULL||
       frame==NULL||frame->pixels==NULL||frame->width!=240u||frame->height!=160u||
       ceremony->track_set>=PORTABLE_TRACK_SET_COUNT||ceremony->track_set==PORTABLE_TRACK_SET_ARENA||ceremony->cup_index>=5u||ceremony->engine_class>=3u||
       ceremony->player_standing_rank>=8u||ceremony->finishing_coin_total>396u||ceremony->performance_rating>=8u||
       view->phase>PORTABLE_CEREMONY_FINISHED||view->entrance_ticks==0u||
       view->fade_ticks==0u)return 0;
    /* A native stage composition: source art, source standings and driver poses.
     * The scene has no dependency on a captured camera or display registers. */
    portable_software_frame_clear(frame,asset->sky_color);
    portable_software_frame_rect(frame,0,118,240,42,asset->ground_color);
    centered(&asset->images[PORTABLE_CEREMONY_CUP_NAME+ceremony->cup_index],frame,120,6);
    centered(&asset->images[PORTABLE_CEREMONY_CLASS+ceremony->engine_class],frame,216,9);
    image(&asset->images[PORTABLE_CEREMONY_CUP_ICON+ceremony->cup_index],frame,8,5);
    /* Custom cups reuse the stock cup art in this ceremony bank. */
    if(ceremony->track_set!=PORTABLE_TRACK_SET_GBA)
        (void)portable_menu_text_centered(pack,frame,120,22,
            ceremony->track_set==PORTABLE_TRACK_SET_EXTRA?"EXTRA":"CUSTOM");
    const int winner=ceremony->player_standing_rank<3u;
    static const int center[3]={120,64,176},top[3]={83,99,109};
    const int entrance=view->phase==PORTABLE_CEREMONY_ENTRANCE?
        (int)((uint64_t)(view->entrance_ticks-view->tick)*160u/view->entrance_ticks):0;
    for(unsigned pass=0;pass<3u;++pass){
        const unsigned rank=pass==2u?0u:pass+1u;
        portable_software_frame_rect(frame,(size_t)(center[rank]-24),(size_t)top[rank],
            48,(size_t)(134-top[rank]),asset->podium_color);
        centered(&asset->images[PORTABLE_CEREMONY_RANK+rank],frame,center[rank],top[rank]+2);
        const uint8_t slot=rank==0u&&view->phase!=PORTABLE_CEREMONY_ENTRANCE?28u:1u;
        if(!portable_character_render_slot_oam_double(podium[rank],frame,
            center[rank]-64,top[rank]-84+entrance,slot,384,384))return 0;
    }
    const unsigned audience=PORTABLE_CEREMONY_AUDIENCE+(view->tick/15u)%2u;
    image(&asset->images[audience],frame,0,96);
    image(&asset->images[audience],frame,190,96);
    if(!winner&&!portable_character_render_slot_oam_double(selected,frame,
        56,63+entrance,24u,384,384))return 0;
    if(view->phase!=PORTABLE_CEREMONY_ENTRANCE){
        centered(&asset->images[PORTABLE_CEREMONY_RANK+ceremony->player_standing_rank],frame,30,34);
        centered(&asset->images[PORTABLE_CEREMONY_RATING+ceremony->performance_rating],frame,212,35);
    }
    if(view->phase!=PORTABLE_CEREMONY_ENTRANCE){
        const unsigned coins=ceremony->finishing_coin_total;
        image(&asset->images[PORTABLE_CEREMONY_COIN],frame,100,132);
        image(&asset->images[PORTABLE_CEREMONY_DIGIT+coins/100u],frame,112,132);
        image(&asset->images[PORTABLE_CEREMONY_DIGIT+coins/10u%10u],frame,120,132);
        image(&asset->images[PORTABLE_CEREMONY_DIGIT+coins%10u],frame,128,132);
    }
    if(view->phase==PORTABLE_CEREMONY_WAIT&&view->tick<45u)
        centered(&asset->images[PORTABLE_CEREMONY_CONTINUE],frame,120,142);
    if(view->phase==PORTABLE_CEREMONY_WAIT&&
       (ceremony->extra_cup_unlocked||ceremony->special_cup_unlocked)){
        char text[48];
        const unsigned speed=ceremony->engine_class==0u?50u:ceremony->engine_class==1u?100u:150u;
        snprintf(text,sizeof text,"%uCC: %s",speed,ceremony->extra_cup_unlocked?"THIS CUP":"SELECT SPECIAL CUP");
        const unsigned lines=ceremony->extra_cup_unlocked?3u:2u;
        if(pack!=NULL&&portable_menu_panel(pack,frame,0,40,240u,lines*16u+16u)!=PORTABLE_FRONTEND_PRESENTATION_OK)return 0;
        (void)portable_menu_text(pack,frame,8,48,ceremony->extra_cup_unlocked?
            "EXTRA CUP UNLOCKED":"SPECIAL CUP UNLOCKED");
        (void)portable_menu_text(pack,frame,8,64,text);
        if(ceremony->extra_cup_unlocked)
            (void)portable_menu_text(pack,frame,8,80,"SELECT/TAB SWITCHES TO EXTRA");
        if(!portable_screen_capture(frame,"ceremony-unlock"))return 0;
    }
    if(view->phase==PORTABLE_CEREMONY_WAIT&&!portable_screen_capture(frame,"ceremony"))return 0;
    if(view->phase==PORTABLE_CEREMONY_FADE||view->phase==PORTABLE_CEREMONY_FINISHED){
        const uint32_t remaining=view->phase==PORTABLE_CEREMONY_FINISHED?0u:
            view->fade_ticks-view->tick;
        const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
        for(size_t y=0;y<frame->height*scale;++y)for(size_t x=0;x<frame->width*scale;++x){
            const uint32_t pixel=frame->pixels[y*stride+x];
            const uint32_t r=((pixel>>16)&255u)*remaining/view->fade_ticks;
            const uint32_t g=((pixel>>8)&255u)*remaining/view->fade_ticks;
            const uint32_t b=(pixel&255u)*remaining/view->fade_ticks;
            portable_software_frame_store_physical(frame,x,y,UINT32_C(0xff000000)|(r<<16)|(g<<8)|b);
        }
    }
    return 1;
}


static int64_t ceremony_floor(int64_t value,int64_t divisor)
{ return value>=0?value/divisor:-((-value+divisor-1)/divisor); }
static uint32_t ceremony_darken_color(uint32_t pixel,unsigned ticks)
{
    uint32_t out=pixel&0xff000000u;
    for(unsigned shift=0;shift<24u;shift+=8u){
        unsigned component=(pixel>>(shift+3u))&31u;
        component=component>ticks?component-ticks:0u;
        out|=(component*8u+component/4u)<<shift;
    }
    return out;
}
static void ceremony_darken_world(PortableSoftwareFrame *frame,unsigned ticks)
{
    if(!ticks)return;
    const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
    for(size_t y=0;y<frame->height*scale;++y)for(size_t x=0;x<frame->width*scale;++x)
        portable_software_frame_store_physical(frame,x,y,ceremony_darken_color(frame->pixels[y*stride+x],ticks));
}
static int draw_source_cell(const PortableNativeImage *art,PortableSoftwareFrame *frame,
    int32_t x_q16,int32_t y_q16,int inverse_x,int inverse_y,uint16_t angle,
    const int32_t *sine,size_t sine_count,unsigned darken)
{
    if(!art||!art->pixels||!inverse_x||!inverse_y)return 1;
    if(inverse_x<0||inverse_y<0||!sine||sine_count<1024u)return 0;
    const int scale=(int)portable_software_frame_scale(frame);
    const int width=(int)frame->width*scale,height=(int)frame->height*scale;
    const int64_t ox=(int64_t)x_q16*scale,oy=(int64_t)y_q16*scale;
    const unsigned direction=angle>>6u;
    const int64_t co=sine[(direction+256u)&1023u],si=sine[direction&1023u];
    const int64_t pa=ceremony_floor(co*inverse_x,32768),pb=ceremony_floor(si*inverse_x,32768);
    const int64_t pc=ceremony_floor(-si*inverse_y,32768),pd=ceremony_floor(co*inverse_y,32768);
    const int min_inverse=inverse_x<inverse_y?inverse_x:inverse_y;
    int radius=abs(art->anchor_x)+abs(art->anchor_y)+(int)art->width+(int)art->height;
    int64_t extent=((int64_t)radius*256*scale+min_inverse-1)/min_inverse+2;
    int64_t left=ceremony_floor(ox,65536)-extent,right=ceremony_floor(ox,65536)+extent;
    int64_t top=ceremony_floor(oy,65536)-extent,bottom=ceremony_floor(oy,65536)+extent;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>width)right=width;
    if(bottom>height)bottom=height;
    const int64_t denominator=(int64_t)scale*65536*256;
    for(int64_t y=top;y<bottom;++y)for(int64_t x=left;x<right;++x){
        const int64_t dx=x*65536-ox,dy=y*65536-oy;
        const int64_t sx=ceremony_floor(dx*pa+dy*pb,denominator)-art->anchor_x;
        const int64_t sy=ceremony_floor(dx*pc+dy*pd,denominator)-art->anchor_y;
        if(sx<0||sy<0||sx>=art->width||sy>=art->height)continue;
        uint32_t pixel=portable_native_image_pixel(art,(size_t)sx,(size_t)sy);
        if(darken)pixel=ceremony_darken_color(pixel,darken);
        if(pixel>>24u)portable_software_frame_store_physical(frame,(size_t)x,(size_t)y,pixel);
    }
    return 1;
}
static int draw_ceremony_sprite(const PortableGpCeremonyAssetView *asset,
    const PortableGpCeremonyWorldView *world,PortableSoftwareFrame *frame,unsigned darken,PortableCeremonySprite sprite)
{
    if(!sprite.visible)return 1;
    if(sprite.image_id>=asset->image_count)return 0;
    const int32_t center=(int32_t)((frame->width-240u)/2u)*65536;
    return draw_source_cell(&asset->images[sprite.image_id],frame,sprite.x_q16+center,sprite.y_q16,
        sprite.inverse_x,sprite.inverse_y,(uint16_t)sprite.angle,world->scene->sine,world->scene->sine_count,asset->image_darken[sprite.image_id]?darken:0u);
}
static int32_t ceremony_palette_asr15(int32_t value)
{ return value>=0?value/32768:(int32_t)(-((-(int64_t)value+32767)/32768)); }
static void ceremony_pulse_colors(const PortableGpCeremonyAssetView *asset,unsigned group,
    unsigned phase,const int32_t *sine,uint16_t colors[16])
{
    int angle=(int)(phase*1024u);if(angle>=32768)angle-=65536;
    const int32_t weight=(32768-sine[(unsigned)(256-angle/64)&1023u])/2;
    for(unsigned i=0;i<16u;++i){
        const unsigned a=asset->pulse_colors[group][0][i],b=asset->pulse_colors[group][1][i];unsigned value=0u;
        for(unsigned shift=0;shift<15u;shift+=5u){const int start=(int)(a>>shift&31u),end=(int)(b>>shift&31u);
            value|=(unsigned)(start+ceremony_palette_asr15((end-start)*weight))<<shift;}
        colors[i]=(uint16_t)value;
    }
}
static void ceremony_indexed_image(const PortableNativeImage *art,PortableSoftwareFrame *frame,
    int x,int y,const uint16_t *colors,int clip_left)
{
    for(unsigned sy=0;sy<art->height;++sy)for(unsigned sx=0;sx<art->width;++sx){
        const int tx=x+(int)sx,ty=y+(int)sy;
        if(tx<clip_left||tx<0||ty<0||(size_t)tx>=frame->width||(size_t)ty>=frame->height)continue;
        const uint32_t pixel=portable_native_image_pixel_palette(art,sx,sy,colors,colors?16u:0u);
        if(pixel>>24u)portable_software_frame_store(frame,(size_t)ty*frame->width+(size_t)tx,pixel);
    }
}
static int draw_summary(const PortableAssetPackView *pack,const PortableGpCeremonyAssetView *asset,
    const PortableGpCeremonySession *session,const PortableGpCeremonyPresentation *view,
    const int32_t *sine,PortableSoftwareFrame *frame)
{
    const int center=(int)(frame->width-240u)/2;
    const unsigned reveal=view->stats_reveal;
    if(!reveal)return 1;
    const PortableNativeImage *panel=&asset->images[asset->panels[session->player_standing_rank<3u?1u:2u]];
    /* 28B90 phase12 copies the lower11 rows upward and the right13x5
     * panel leftward. Zero pixels reveal the live stage below them. */
    const int dy=(11-(int)(11u*reveal/16u))*8,dx=(20-(int)(20u*reveal/16u))*8;
    for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x){
        int tx=(int)x,ty=(int)y;
        if(y>=72u)ty+=dy;else if(y>=32u&&y<72u&&x>=136u)tx+=dx;else continue;
        if(tx<0||ty<0||tx>=240||ty>=160)continue;
        /* 269B0/297A8 replaces BG cells, including zero pixels. Do not
         * retain the previous BG behind the newly copied unlock strip. */
        if(view->unlock_reveal&&y>=72u&&y<80u&&x<(unsigned)(view->unlock_reveal-1u)*8u)continue;
        const uint32_t pixel=portable_native_image_pixel(panel,x,y);
        if(pixel>>24u)portable_software_frame_store(frame,(size_t)ty*frame->width+(size_t)(tx+center),pixel);
    }
    if(reveal<16u)return 1;
    image(&asset->images[PORTABLE_CEREMONY_RANK+session->player_standing_rank],frame,center+136,40);
    image(&asset->images[PORTABLE_CEREMONY_RATING+session->performance_rating],frame,center+208,48);
    image(&asset->images[PORTABLE_CEREMONY_CLASS+session->engine_class],frame,center+(session->engine_class==0u?80:72),80);
    /* Course names remain content references so custom cups preserve names. */
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];PortableFrontendConfig config={0};
    const int have_catalog=pack&&build_frontend_config(pack,drivers,courses,&config);
    for(unsigned i=0;i<4u;++i){
        unsigned slot=0u;
        if(have_catalog){size_t c=0u;while(c<config.course_count&&courses[c].content_id!=session->course_ids[i])++c;
            if(c<config.course_count)slot=courses[c].cup*4u+courses[c].track;}
        PortableAssetSectionView section;PortableNativeImage name;
        if(pack&&portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
            0x23400u+slot,&section)==PORTABLE_ASSET_PACK_OK&&
            portable_native_image_open(section.bytes,section.size,&name))image(&name,frame,center+8,96+(int)i*16);
        const uint32_t centiseconds=session->race_centiseconds[i];
        const unsigned digits[7]={centiseconds/6000u%10u,10u,centiseconds/1000u%6u,
            centiseconds/100u%10u,11u,centiseconds/10u%10u,centiseconds%10u};
        for(unsigned d=0;d<7u;++d)image(&asset->images[asset->glyphs[digits[d]]],frame,center+136+(int)d*8,96+(int)i*16);
        if(session->finishing_ranks[i]<8u)image(&asset->images[asset->rank_labels[session->finishing_ranks[i]]],frame,center+112,96+(int)i*16);
        const unsigned points=session->finishing_points[i],coins=session->finishing_coins[i];
        if(points<10u)image(&asset->images[asset->glyphs[points]],frame,center+200,96+(int)i*16);
        image(&asset->images[asset->glyphs[coins/10u%10u]],frame,center+224,96+(int)i*16);
        image(&asset->images[asset->glyphs[coins%10u]],frame,center+232,96+(int)i*16);
    }
    const unsigned coins=view->coin_total;
    uint16_t pulse[16];ceremony_pulse_colors(asset,1u,view->palette_phase,sine,pulse);
    image(&asset->images[asset->summary_coin_image],frame,center+208,72);
    for(unsigned i=0,divisor=100;i<3u;++i,divisor/=10u)
        ceremony_indexed_image(&asset->images[asset->total_digits[coins/divisor%10u]],frame,
            center+216+(int)i*8,72,coins>99u?pulse:NULL,0);
    for(unsigned i=0;i<4u;++i)if(view->coins[i].value>=0){
        const unsigned value=(unsigned)view->coins[i].value;
        const PortableNativeImage *tens=&asset->images[asset->coin_digits[value/10u%10u]];
        const PortableNativeImage *units=&asset->images[asset->coin_digits[value%10u]];
        image(tens,frame,center+view->coins[i].x+tens->anchor_x,view->coins[i].y+tens->anchor_y);
        image(units,frame,center+view->coins[i].x+8+units->anchor_x,view->coins[i].y+units->anchor_y);
    }
    if(view->unlock_reveal){
        const int x=center+((int)view->unlock_reveal-16)*8;
        ceremony_indexed_image(&asset->images[asset->unlock_image],frame,x,72,pulse,center);
    }
    if(view->unlock_stage>=3u&&(session->extra_cup_unlocked||session->special_cup_unlocked))
        ceremony_indexed_image(&asset->images[asset->highlight_classes[session->engine_class]],frame,
            center+(session->engine_class==0u?80:72),80,pulse,0);
    return 1;
}
int portable_gp_ceremony_render(const PortableAssetPackView *pack,
    const PortableGpCeremonyAssetView *asset,const PortableGpCeremonySession *session,
    const PortableGpCeremonyPresentation *view,const PortableCharacterAssetView *const podium[3],
    const PortableCharacterAssetView *selected,const PortableGpCeremonyWorldView *world,PortableSoftwareFrame *frame)
{
    if(!asset||!session||!view||!frame||!frame->pixels)return 0;
    if(!asset->source_presentation)return render_legacy(pack,asset,session,view,podium,selected,frame);
    if(!world||!world->scene||!world->state||!world->grid||world->grid_count<8u||
       !view->source_enabled||frame->width<240u||frame->height!=160u||session->cup_index>=5u||
       session->player_standing_rank>=8u||session->performance_rating>=8u||session->engine_class>=3u)return 0;
    PortableRacePresentationState background=*world->state;background.suppress_racers=1u;
    if(!portable_race_scene_render(world->scene,&background,frame))return 0;
    const int center=(int)(frame->width-240u)/2;
    PortableCeremonySprite actors[8];
    if(!portable_ceremony_track_sprites(&view->motion,asset->motion.tracks,asset->motion.frames,
        asset->motion.frame_count,256,view->road_line,actors))return 0;
    const PortableCeremonySprite podium_sprite=portable_ceremony_cell_sprite(&view->motion.loser,asset->motion.drop_image,0u);
    if(view->motion.world.shadow_started&&!draw_ceremony_sprite(asset,world,frame,0u,podium_sprite))return 0;
    if(!portable_race_scene_render_racers(world->scene,world->state,frame))return 0;
    if(!view->motion.world.shadow_started&&!draw_ceremony_sprite(asset,world,frame,0u,podium_sprite))return 0;
    ceremony_darken_world(frame,view->stats_reveal);
    for(unsigned i=0;i<8u;++i)if(!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,actors[i]))return 0;
    if(view->motion.loser.visible&&view->motion.loser.y_q16!=122*65536){
        const int size=view->motion.shadow_expansion?view->motion.shadow_expansion:2;
        const int inverse=25600/size;
        if(!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,(PortableCeremonySprite){.image_id=asset->shadow_image,
            .x_q16=view->motion.loser.x_q16,.y_q16=138*65536,.inverse_x=(int16_t)inverse,
            .inverse_y=(int16_t)inverse,.visible=1u}))return 0;
    }
    if(!draw_summary(pack,asset,session,view,world->scene->sine,frame))return 0;
    const int cup_y=88+(view->stats_reveal? (11-(int)(11u*view->stats_reveal/16u))*8:80);
    const PortableNativeImage *cup=&asset->images[asset->cup_labels[session->cup_index]];
    image(cup,frame,center+40+cup->anchor_x,cup_y+cup->anchor_y);
    if(view->fly_stage&&!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,portable_ceremony_cell_sprite(&view->motion.flypast,
        asset->motion.flypast_images[view->motion.fly_frame],0u)))return 0;
    if(session->player_standing_rank<3u&&view->trophy_stage){
        const unsigned rank=session->player_standing_rank,palette=view->motion.sparkle_palette;
        if(!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,portable_ceremony_cell_sprite(&view->motion.trophy,
            asset->motion.trophy_images[rank][view->motion.trophy_frame],0u)))return 0;
        if(view->motion.trophy.inverse_x||view->motion.trophy.inverse_y){
            PortableCeremonySprite sparkle=portable_ceremony_cell_sprite(&view->motion.trophy,asset->sparkles[palette],0u);
            sparkle.angle=(int16_t)view->motion.sparkle_angle;
            if(!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,sparkle))return 0;
            for(unsigned i=0;i<32u;++i){
                const PortableCeremonyConfetti *p=&view->motion.confetti[i];
                if(!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,(PortableCeremonySprite){.image_id=asset->confetti[palette],
                    .x_q16=p->x_q16,.y_q16=p->y_q16,.inverse_x=256,.inverse_y=256,.visible=1u}))return 0;
            }
        }
    }
    /* A roster driver's authored portrait remains its own identity. */
    if(pack&&view->motion.zoom.visible){
        PortableAssetSectionView section;PortableNativeAnimation animation;PortableNativeImage portrait;
        uint32_t id=0x22100u+session->selected_driver_class*3u+view->portrait_mood;
        const uint32_t own=0x22400u+session->selected_character_id*8u+view->portrait_mood;
        if(session->selected_character_id&&portable_asset_pack_find_section(pack,
            PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,own,&section)==PORTABLE_ASSET_PACK_OK)id=own;
        if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)==PORTABLE_ASSET_PACK_OK){
            uint32_t image_id=id;
            if(portable_native_animation_open(section.bytes,section.size,&animation))image_id=portable_native_animation_frame(&animation,view->source_tick);
            if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,image_id,&section)==PORTABLE_ASSET_PACK_OK&&
               portable_native_image_open(section.bytes,section.size,&portrait)){
                /* Menu portraits carry page anchors; ceremony's cell is centered. */
                portrait.anchor_x=-(int16_t)(portrait.width/2u);portrait.anchor_y=-(int16_t)(portrait.height/2u);
                if(!draw_source_cell(&portrait,frame,view->motion.zoom.x_q16+center*65536,view->motion.zoom.y_q16,
                    view->motion.zoom.inverse_x,view->motion.zoom.inverse_y,(uint16_t)view->motion.zoom.angle,world->scene->sine,world->scene->sine_count,0u))return 0;
            }
        }
    }
    if(view->header_active){
        const unsigned length=session->player_standing_rank<3u?16u:7u,start=length==16u?0u:16u;
        for(unsigned i=0;i<length;++i){
            if(!draw_ceremony_sprite(asset,world,frame,view->stats_reveal,portable_ceremony_cell_sprite(&view->headers[i],
                asset->cup_headers[session->cup_index][start+i],0u)))return 0;
        }
    }
    if(view->phase==PORTABLE_CEREMONY_FADE||view->phase==PORTABLE_CEREMONY_FINISHED){
        const unsigned remaining=view->phase==PORTABLE_CEREMONY_FINISHED?0u:view->tick>=view->fade_ticks?0u:view->fade_ticks-view->tick;
        const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
        for(size_t y=0;y<frame->height*scale;++y)for(size_t x=0;x<frame->width*scale;++x){
            uint32_t color=frame->pixels[y*stride+x],out=0xff000000u;
            for(unsigned shift=0;shift<24u;shift+=8u)out|=(((color>>shift)&255u)*remaining/view->fade_ticks)<<shift;
            portable_software_frame_store_physical(frame,x,y,out);
        }
    }
    return 1;
}
