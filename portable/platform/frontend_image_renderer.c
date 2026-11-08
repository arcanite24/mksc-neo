#include "frontend_image_renderer.h"
#include "native_image.h"
#include "course_menu_renderer.h"

static PortableFrontendPresentationStatus draw_scaled_palette(
    const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame,
    int x, int y, int left, int top, int right, int bottom, unsigned scale,
    const uint16_t *palette,size_t palette_count,const uint8_t *palette_mask)
{
    PortableAssetSectionView section;
    PortableNativeImage image;
    if(scale==0u || scale>4096u)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if (portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section) != PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if (!portable_native_image_open(section.bytes,section.size,&image))
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    const int output_scale=(int)portable_software_frame_scale(frame);
    const int origin_x=x*output_scale+image.anchor_x*(int)scale*output_scale/256;
    const int origin_y=y*output_scale+image.anchor_y*(int)scale*output_scale/256;
    const int width=(int)((image.width*scale*(unsigned)output_scale+255u)/256u);
    const int height=(int)((image.height*scale*(unsigned)output_scale+255u)/256u);
    int first_x=left*output_scale,first_y=top*output_scale;
    int last_x=right*output_scale,last_y=bottom*output_scale;
    if(first_x<0)first_x=0;
    if(first_y<0)first_y=0;
    if(first_x<origin_x)first_x=origin_x;
    if(first_y<origin_y)first_y=origin_y;
    if(last_x>(int)frame->width*output_scale)last_x=(int)frame->width*output_scale;
    if(last_y>(int)frame->height*output_scale)last_y=(int)frame->height*output_scale;
    if(last_x>origin_x+width)last_x=origin_x+width;
    if(last_y>origin_y+height)last_y=origin_y+height;
    for(int target_y=first_y;target_y<last_y;++target_y)
    for(int target_x=first_x;target_x<last_x;++target_x){
        const unsigned sx=(unsigned)(target_x-origin_x)*256u/(scale*(unsigned)output_scale);
        const unsigned sy=(unsigned)(target_y-origin_y)*256u/(scale*(unsigned)output_scale);
        const uint32_t pixel=portable_native_image_pixel_palette_masked(&image,sx,sy,
            palette,palette_count,palette_mask);
        if((pixel>>24)==255u)
            portable_software_frame_store_physical(frame,(size_t)target_x,(size_t)target_y,pixel);
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

PortableFrontendPresentationStatus portable_frontend_image_draw_scaled(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    int x,int y,int left,int top,int right,int bottom,unsigned scale)
{
    return draw_scaled_palette(pack,id,frame,x,y,left,top,right,bottom,scale,NULL,0u,NULL);
}

PortableFrontendPresentationStatus portable_frontend_image_draw_palette(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    int x,int y,int left,int top,int right,int bottom,
    const uint16_t *palette,size_t palette_count,const uint8_t *palette_mask)
{
    return draw_scaled_palette(pack,id,frame,x,y,left,top,right,bottom,256u,
        palette,palette_count,palette_mask);
}

PortableFrontendPresentationStatus portable_frontend_image_draw(
    const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame,
    int x, int y, int left, int top, int right, int bottom)
{
    return portable_frontend_image_draw_scaled(pack,id,frame,x,y,left,top,right,bottom,256u);
}

/* Inverse affine sampling around an authored menu-cell origin. Images retain
 * their own anchors, including custom portraits placed on another grid slot. */
PortableFrontendPresentationStatus portable_frontend_image_draw_affine(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    const PortableFrontendMotion *motion,int pivot_x,int pivot_y,int x,int y,
    unsigned inverse,unsigned angle,int right)
{
    PortableAssetSectionView section;PortableNativeImage image;
    if(!inverse||inverse>4096u)return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if(!portable_native_image_open(section.bytes,section.size,&image))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    int sine=angle?portable_menu_sine(motion,angle):0;
    int cosine=angle?portable_menu_sine(motion,angle+256u):32768;
    const int output_scale=(int)portable_software_frame_scale(frame);
    int bottom=(int)frame->height;if(bottom>160)bottom=160;
    int clipped_right=(int)frame->width;if(clipped_right>240)clipped_right=240;
    if(clipped_right>right)clipped_right=right;
    for(int dy=0;dy<bottom*output_scale;++dy)
    for(int dx=0;dx<clipped_right*output_scale;++dx){
        int rx=dx-(x+pivot_x)*output_scale,ry=dy-(y+pivot_y)*output_scale;
        int sx=pivot_x+portable_menu_asr15(((int64_t)rx*cosine+(int64_t)ry*sine)*inverse/(256*output_scale))-image.anchor_x;
        int sy=pivot_y+portable_menu_asr15((-(int64_t)rx*sine+(int64_t)ry*cosine)*inverse/(256*output_scale))-image.anchor_y;
        if(sx<0||sy<0||sx>=image.width||sy>=image.height)continue;
        const uint32_t pixel=portable_native_image_pixel(&image,(unsigned)sx,(unsigned)sy);
        if((pixel>>24)==255u)portable_software_frame_store_physical(frame,(size_t)dx,(size_t)dy,pixel);
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

PortableFrontendPresentationStatus portable_frontend_image_draw_atlas(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    unsigned rows,unsigned row,int x,int y,int left,int top,int right,int bottom)
{
    PortableAssetSectionView section;PortableNativeImage image;
    if(!rows||rows>32u||row>=rows)return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if(!portable_native_image_open(section.bytes,section.size,&image)||image.height%rows!=0u||image.height<rows)
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    unsigned height=image.height/rows;x+=image.anchor_x;y+=image.anchor_y;
    for(unsigned sy=0;sy<height;++sy)for(unsigned sx=0;sx<image.width;++sx){
        int dx=x+(int)sx,dy=y+(int)sy;
        if(dx<left||dx>=right||dy<top||dy>=bottom||dx<0||dy<0||
           (size_t)dx>=frame->width||(size_t)dy>=frame->height)continue;
        uint32_t pixel=portable_native_image_pixel(&image,sx,row*height+sy);
        if((pixel>>24)==255u)portable_software_frame_store(frame,(size_t)dy*frame->width+(unsigned)dx,pixel);
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

void portable_menu_background_layers_init(PortableMenuBackgroundLayers *layers,
    const PortableNativeImage *pattern,const PortableNativeImage *affine,
    const PortableFrontendFlow *flow,const PortableFrontendMotion *motion)
{
    const unsigned angle=(0u-(flow->tick&255u)*4u)&1023u;
    *layers=(PortableMenuBackgroundLayers){pattern,affine,
        (int)((flow->tick&1023u)/4u),-(int)(((flow->tick&1023u)+3u)/4u),
        portable_menu_sine(motion,256u-angle)*213/32768,
        portable_menu_sine(motion,angle)*213/32768,(flow->page_ticks/8u)%7u};
}

uint32_t portable_menu_background_layers_sample_at(const PortableMenuBackgroundLayers *layers,
    int64_t physical_x, int64_t physical_y, unsigned scale)
{
    if (scale == 0u)
        return 0u;
    uint32_t pixel=0u;
    if(layers->pattern){
        const unsigned row=layers->pattern->height==1792u?layers->pattern_row*256u:0u;
        const unsigned x=(unsigned)portable_software_frame_floor_div(physical_x,scale);
        const unsigned y=(unsigned)portable_software_frame_floor_div(physical_y,scale);
        pixel=portable_native_image_pixel(layers->pattern,
            (x+(unsigned)layers->scroll_x)&255u,
            row+((y+(unsigned)layers->scroll_y)&255u));
    }
    if(layers->affine){
        const int64_t x=physical_x-200*(int64_t)scale,y=physical_y-80*(int64_t)scale;
        const int64_t tx=96*256*(int64_t)scale+layers->affine_a*x+layers->affine_b*y;
        const int64_t ty=112*256*(int64_t)scale-layers->affine_b*x+layers->affine_a*y;
        const unsigned sx=(unsigned)portable_software_frame_floor_div(tx,256*(int64_t)scale)&255u;
        const unsigned sy=(unsigned)portable_software_frame_floor_div(ty,256*(int64_t)scale)&255u;
        const uint32_t foreground=portable_native_image_pixel(layers->affine,sx,sy);
        if(foreground>>24u==255u)pixel=foreground;
    }
    return pixel;
}

uint32_t portable_menu_background_layers_sample(const PortableMenuBackgroundLayers *layers,
    unsigned physical_x,unsigned physical_y,unsigned scale)
{
    return portable_menu_background_layers_sample_at(layers,physical_x,physical_y,scale);
}

/* 1DDC advances the wrapped pattern independently of 3948's rotating
 * affine decoration. Sampling is stateless: rendering never changes clocks. */
static PortableFrontendPresentationStatus pattern_row_draw(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    int offset_x,int offset_y,unsigned row,int additive_blend)
{
    PortableAssetSectionView section;PortableNativeImage image;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if(!portable_native_image_open(section.bytes,section.size,&image))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    if(image.width!=256u||(image.height!=256u&&image.height!=256u*7u))return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    const unsigned top=image.height==256u?0u:(row%7u)*256u;
    const size_t center=frame->width>240u?(frame->width-240u)/2u:0u;
    for(unsigned y=0;y<160u;++y)for(size_t x=0;x<frame->width;++x){
        /* Convert the centered source coordinate before wrapping: unsigned
         * division would give the wrong left-side phase at odd scales. */
        const unsigned source_x=(unsigned)((int64_t)x-(int64_t)center+offset_x)&255u;
        uint32_t pixel=portable_native_image_pixel(&image,source_x,top+((y+(unsigned)offset_y)&255u));
        if(pixel>>24!=255u)continue;
        if(!additive_blend){portable_software_frame_store(frame,(size_t)y*frame->width+x,pixel);continue;}
        /* Source BG0 blends8/16 over BG1 at16/16 in RGB555. */
        const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
        for(size_t sy=0;sy<scale;++sy)for(size_t sx=0;sx<scale;++sx){
            const size_t px=x*scale+sx,py=y*scale+sy;
            const uint32_t behind=frame->pixels[py*stride+px];
            uint32_t blended=0xff000000u;
            for(unsigned shift=0;shift<24u;shift+=8u){
                unsigned channel=(((pixel>>shift)&255u)>>3u)/2u+(((behind>>shift)&255u)>>3u);
                if(channel>31u)channel=31u;
                blended|=((channel<<3u)|(channel>>2u))<<shift;
            }
            portable_software_frame_store_physical(frame,px,py,blended);
        }
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

PortableFrontendPresentationStatus portable_frontend_pattern_draw(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    int offset_x,int offset_y)
{ return pattern_row_draw(pack,id,frame,offset_x,offset_y,0u,0); }

PortableFrontendPresentationStatus portable_menu_background_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    uint32_t pattern_id,uint32_t legacy_id,PortableSoftwareFrame *frame)
{
    PortableFrontendMotion motion;
    if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    int scroll=(int)((flow->tick&1023u)/4u);
    int negative_scroll=-(int)(((flow->tick&1023u)+3u)/4u);
    portable_software_frame_clear(frame,UINT32_C(0xff000000));
    const int course=pattern_id>=PORTABLE_COURSE_BACKGROUND&&pattern_id<PORTABLE_COURSE_BACKGROUND+5u;
    /* Source priority3 BG1 is stationary; BG0 wins ties and scrolls above it.
     * Missing old-pack art retains the existing single-pattern fallback. */
    PortableFrontendPresentationStatus status=course?PORTABLE_FRONTEND_PRESENTATION_OK:
        portable_frontend_pattern_draw(pack,PORTABLE_MENU_BACK_PATTERN,frame,0,0);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK&&status!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)
        return status;
    const int additive_blend=!course&&status==PORTABLE_FRONTEND_PRESENTATION_OK;
    status=pattern_row_draw(pack,pattern_id,frame,scroll,negative_scroll,
        course?(flow->page_ticks/8u)%7u:0u,additive_blend);
    if(status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION||!motion.bytes){
        /* A legacy flattened page is foreground, never a repeating backdrop.
         * The centered page pass below the wide compositor retains it. */
        if(frame->width>240u)return PORTABLE_FRONTEND_PRESENTATION_OK;
        return portable_frontend_image_draw(pack,legacy_id,frame,0,0,0,0,240,160);
    }
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    PortableAssetSectionView section;PortableNativeImage image;
    uint32_t affine_id=course?PORTABLE_COURSE_AFFINE+pattern_id-PORTABLE_COURSE_BACKGROUND:PORTABLE_MENU_AFFINE;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,affine_id,&section)!=PORTABLE_ASSET_PACK_OK&&
       portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,PORTABLE_MENU_AFFINE,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_OK;
    if(!portable_native_image_open(section.bytes,section.size,&image)||image.width!=256u||image.height!=256u)
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    /* The original signed16 angle decreases256 each tick. */
    unsigned angle=(0u-(flow->tick&255u)*4u)&1023u;
    PortableMenuBackgroundLayers layers;
    portable_menu_background_layers_init(&layers,NULL,&image,flow,&motion);
    const int center_x=200,center_y=80;
    const int output_scale=(int)portable_software_frame_scale(frame);
    const int width=(int)frame->width*output_scale;
    const int64_t center=(int64_t)(frame->width>240u?(frame->width-240u)/2u:0u)*output_scale;
    const int height=(int)(frame->height<160u?frame->height:160u)*output_scale;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const uint32_t pixel=portable_menu_background_layers_sample_at(&layers,(int64_t)x-center,y,(unsigned)output_scale);
        if(pixel>>24==255u)portable_software_frame_store_physical(frame,(size_t)x,(size_t)y,pixel);
    }
    /* The wheel and its orbiters are centered foreground decorations. */
    if(frame->width>240u)return PORTABLE_FRONTEND_PRESENTATION_OK;
    status=portable_frontend_image_draw_affine(pack,PORTABLE_MENU_WHEEL,frame,&motion,0,0,center_x,center_y,213u,angle,240);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    for(unsigned i=0;i<8u;++i){
        unsigned orbit=(angle+i*128u)&1023u;
        int x=center_x+portable_menu_asr15(38ll*portable_menu_sine(&motion,256u-orbit));
        int y=center_y+portable_menu_asr15(38ll*portable_menu_sine(&motion,orbit));
        status=portable_frontend_image_draw_affine(pack,PORTABLE_MENU_ORBIT,frame,&motion,0,0,x,y,213u,(0u-angle)&1023u,240);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    }
    return status;
}

PortableFrontendPresentationStatus portable_frontend_heading_draw(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,PortableSoftwareFrame *frame,
    unsigned group,uint32_t fallback_id,int fallback_x,int fallback_y)
{
    if(!pack||!flow||!frame||!frame->pixels||group>=4u)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if(!flow->heading_configured||(flow->heading_static_mask&(1u<<group)))
        return portable_frontend_image_draw(pack,fallback_id,frame,fallback_x,fallback_y,0,0,240,160);
    const PortableMenuHeading *s=&flow->heading;
    if(!s->initialized||s->group!=group)return PORTABLE_FRONTEND_PRESENTATION_OK;
    if(s->count>16u||s->count!=flow->heading_rules.groups[group].count)
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    for(unsigned reverse=s->count;reverse>0u;--reverse){const unsigned i=reverse-1u;
        const PortableMenuHeadingCell *cell=&s->cells[i];
        PortableAssetSectionView section;PortableNativeImage art;
        if(portable_asset_pack_find_section(pack,15u,flow->heading_rules.groups[group].letters[i].image_id,&section)!=PORTABLE_ASSET_PACK_OK)
            return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
        if(!portable_native_image_open(section.bytes,section.size,&art)||cell->inverse_x<=0||cell->inverse_y<=0)
            return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
        const int scale=(int)portable_software_frame_scale(frame);
        int left=cell->x*scale+(int)portable_software_frame_floor_div((int64_t)art.anchor_x*256*scale,cell->inverse_x);
        int top=cell->y*scale+(int)portable_software_frame_floor_div((int64_t)art.anchor_y*256*scale,cell->inverse_y);
        int right=cell->x*scale-(int)portable_software_frame_floor_div(-(int64_t)(art.anchor_x+art.width)*256*scale,cell->inverse_x);
        int bottom=cell->y*scale-(int)portable_software_frame_floor_div(-(int64_t)(art.anchor_y+art.height)*256*scale,cell->inverse_y);
        if(left<0)left=0;
        if(top<0)top=0;
        if(right>240*scale)right=240*scale;
        if(bottom>160*scale)bottom=160*scale;
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
            const int sx=(int)portable_software_frame_floor_div((int64_t)(x-cell->x*scale)*cell->inverse_x,256*scale)-art.anchor_x;
            const int sy=(int)portable_software_frame_floor_div((int64_t)(y-cell->y*scale)*cell->inverse_y,256*scale)-art.anchor_y;
            if(sx<0||sy<0||(unsigned)sx>=art.width||(unsigned)sy>=art.height)continue;
            const uint32_t pixel=portable_native_image_pixel(&art,(unsigned)sx,(unsigned)sy);
            if(pixel>>24u)portable_software_frame_store_physical(frame,(size_t)x,(size_t)y,pixel);
        }
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

/* Original medallion positions; menu input and race availability remain in
 * frontend_flow. The selected image grows by 4/3, matching the original settled scale. */
PortableFrontendPresentationStatus portable_menu_render(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableFrontendPage page, PortableSoftwareFrame *frame)
{
    uint32_t base;
    unsigned count, cursor;
    PortableFrontendMotion motion;
    if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    PortableFrontendPresentationStatus status;
    static const int positions[3][2]={{68,64},{128,92},{188,120}};
    /* 42C4 link games use the two-row D905C table: VS, then Battle. */
    const int link=page==PORTABLE_FRONTEND_PAGE_MODE&&flow->mode_phase==0u&&flow->players>1u;
    if (page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL) {
        base=PORTABLE_MENU_TRIAL;count=2u;cursor=flow->time_trial_cursor;
    } else if (link) {
        base=PORTABLE_MENU_MODE+3u;count=2u;cursor=flow->mode_cursor==3u?1u:0u;
    } else {
        base=flow->mode_phase ? PORTABLE_MENU_CLASS : PORTABLE_MENU_MODE;
        count=flow->mode_phase?3u:4u;cursor=flow->mode_phase ? flow->race_speed : flow->mode_cursor;
    }
    if (cursor>=count) return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    status=portable_menu_background_render(pack,flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,frame);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    /* 3FD0 positions the individual heading cells and each cell contributes
     * its own(-8,-8) anchor. The normalized full strip needs that offset;
     * negative source X coordinates add one in the constructor. */
    status=portable_frontend_heading_draw(pack,flow,frame,0u,PORTABLE_MENU_HEADING,121,16);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    if(flow->motion_stage==1u&&flow->motion_player_entry&&motion.bytes&&flow->motion_tick<83u){
        const unsigned player=flow->players>=1u&&flow->players<=4u?flow->players-1u:0u;
        const uint8_t *p=motion.bytes+88u+(10u+player)*4u;
        int x=portable_menu_i16(p),y=portable_menu_i16(p+2);
        unsigned phase=flow->motion_tick&31u;
        unsigned inverse=192u+(unsigned)portable_menu_asr15(20ll*(32768-portable_menu_sine(&motion,256u-phase*32u)));
        if(flow->motion_tick>=49u){
            unsigned t=flow->motion_tick-49u;if(t>20u)return status;
            x=portable_menu_sine_lerp(&motion,x,portable_menu_i16(motion.bytes+144u)+4,t,20u);
            y=portable_menu_sine_lerp(&motion,y,portable_menu_i16(motion.bytes+146u),t,20u);
            inverse=25600u/(100u-70u*t/20u);
        }
        return portable_frontend_image_draw_scaled(pack,PORTABLE_MENU_PLAYER_MEDALLION+player,frame,x,y,0,0,240,160,65536u/inverse);
    }
    /* The ROM's one-player row is GP, TT and Quick Run on the class
     * diagonal; the native Battle medallion extends it, and the three-slot
     * window follows the cursor onto it. */
    const unsigned first=count==4u&&cursor==3u?1u:0u;
    for(unsigned pass=0u;pass<count;++pass) {
        unsigned index=pass==count-1u ? cursor : (pass>=cursor ? pass+1u : pass);
        if(count==4u&&(index<first||index>=first+3u))continue;
        int x=count==2u ? 88+(int)index*80 : positions[index-first][0];
        int y=count==2u ? 88 : positions[index-first][1];
        unsigned phase=flow->page_ticks&31u;
        unsigned inverse=index==cursor&&motion.bytes?192u+(unsigned)portable_menu_asr15(20ll*(32768-portable_menu_sine(&motion,256u-phase*32u))):index==cursor?192u:256u;
        const unsigned medallion_group=link?3u:page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL?2u:flow->mode_phase?1u:0u;
        if(flow->heading_configured&&flow->motion_stage==0u&&flow->medallion.initialized&&flow->medallion.group==medallion_group)
            inverse=flow->medallion.inverse[index];
        if(motion.bytes){
            unsigned row=link?8u+index:page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL?6u+index:
                flow->mode_phase?3u+index:index-first;
            /* Battle extends the source one-player diagonal; use the row
             * occupied by that authored art rather than a course-ID rule. */
            if(row<10u){const uint8_t *p=motion.bytes+88u+row*4u;x=portable_menu_i16(p);y=portable_menu_i16(p+2);}
            if(flow->motion_stage==1u){
                int t=(int)flow->motion_tick-(int)flow->motion_delay-(int)(index-first)*4;
                x=portable_menu_curve10(&motion,x+240,x,t,20);
                if(index==cursor){
                    unsigned entry_phase=flow->motion_tick&31u;
                    inverse=192u+(unsigned)portable_menu_asr15(20ll*(32768-portable_menu_sine(&motion,256u-entry_phase*32u)));
                }
            }else if(flow->motion_stage==2u){
                unsigned t=flow->motion_tick;
                if(index!=cursor){
                    int local=(int)t-(int)(count-1u-index)*4;
                    if(local>0)x+=portable_menu_sine_lerp(&motion,0,240,(unsigned)local,16u);
                }else if(t<=20u){
                    unsigned tab=flow->mode_phase||page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL?2u:1u;
                    const uint8_t *p=motion.bytes+144u+tab*4u;
                    x=portable_menu_sine_lerp(&motion,x,portable_menu_i16(p)+4,t,20u);
                    y=portable_menu_sine_lerp(&motion,y,portable_menu_i16(p+2),t,20u);
                    inverse=25600u/(100u-70u*t/20u);
                }else continue;
            }
        }
        const uint32_t image=link?(index==0u?PORTABLE_MENU_MODE+4u:PORTABLE_MENU_MODE+3u):base+index;
        status=motion.bytes?portable_frontend_image_draw_affine(pack,image,frame,&motion,0,0,x,y,inverse,0u,240):
            portable_frontend_image_draw_scaled(pack,image,frame,x,y,0,0,240,160,65536u/inverse);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    return status;
}
