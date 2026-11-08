#include "title_renderer.h"
#include "frontend_image_renderer.h"
#include "menu_chrome_renderer.h"
#include "menu_font.h"

#include <string.h>

static unsigned title_wrap(int64_t value,unsigned period)
{
    const int64_t remainder=value%(int64_t)period;
    return (unsigned)(remainder<0?remainder+period:remainder);
}

static uint32_t title_background_id(unsigned variant,unsigned phase)
{
    return variant==1u?PORTABLE_TITLE_SUNSET_BACKGROUND+phase:
        variant==2u?PORTABLE_TITLE_NIGHT_BACKGROUND+phase:
        phase?PORTABLE_TITLE_BACKGROUND_FRAME+phase:PORTABLE_TITLE_BACKGROUND;
}

static int title_stock_identity(const PortableAssetPackView *pack,
    const PortableAssetPackView *stock,unsigned variant)
{
    unsigned matched=0u;
    /* Check the complete animation, so a single overridden flag/star frame
     * cannot make source wings appear and disappear during the same scene. */
    const unsigned count=variant==2u?12u:5u;
    for(unsigned frame=0u;frame<count;++frame){
        PortableAssetSectionView resolved,source;
        const uint32_t id=frame<5u?title_background_id(variant,frame):
            PORTABLE_TITLE_NIGHT_TWINKLE+frame-4u;
        const int have_resolved=portable_asset_pack_find_section(pack,
            PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&resolved)==PORTABLE_ASSET_PACK_OK;
        const int have_source=portable_asset_pack_find_section(stock,
            PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&source)==PORTABLE_ASSET_PACK_OK;
        if(have_resolved!=have_source)return 0;
        if(have_resolved){
            if(resolved.size!=source.size||memcmp(resolved.bytes,source.bytes,resolved.size))return 0;
            ++matched;
        }
    }
    return matched!=0u;
}

PortableFrontendPresentationStatus portable_title_background_render(
    const PortableAssetPackView *pack,const PortableAssetPackView *background_pack,
    const PortableFrontendFlow *flow,PortableSoftwareFrame *frame)
{
    if(!pack||!flow||!frame||!frame->pixels||frame->width<240u||frame->height!=160u)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    unsigned variant=flow->title_variant<=2u?flow->title_variant:0u;
    PortableAssetSectionView sky_section,cloud_section;
    uint32_t sky_id=PORTABLE_TITLE_WIDE_SKY+variant*2u;
    uint32_t cloud_id=PORTABLE_TITLE_WIDE_CLOUDS+variant*2u;
    const int custom_sky=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,sky_id,&sky_section)==PORTABLE_ASSET_PACK_OK;
    const int custom_clouds=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,cloud_id,&cloud_section)==PORTABLE_ASSET_PACK_OK;
    if(custom_sky!=custom_clouds)return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    if(!custom_sky){
        /* A flat title override remains a complete authored composition. Never
         * invent stock surroundings for it; mods may supply explicit wide art. */
        if(!background_pack)return PORTABLE_FRONTEND_PRESENTATION_OK;
        PortableAssetSectionView metadata;
        if(portable_asset_pack_find_section(background_pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
                PORTABLE_TITLE_WIDE_METADATA,&metadata)!=PORTABLE_ASSET_PACK_OK ||
           metadata.size!=32u || memcmp(metadata.bytes,"MKSCWUI1\1\0\0\0",12u))
            return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
        if(variant!=0u){
            PortableAssetSectionView resolved;
            unsigned present=0u;
            for(unsigned phase=0u;phase<5u;++phase)
                present+=portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
                    title_background_id(variant,phase),&resolved)==PORTABLE_ASSET_PACK_OK;
            if(present==0u)variant=0u;
            else if(present!=5u)return PORTABLE_FRONTEND_PRESENTATION_OK;
        }
        if(!title_stock_identity(pack,background_pack,variant))return PORTABLE_FRONTEND_PRESENTATION_OK;
        sky_id=PORTABLE_TITLE_WIDE_SKY+variant*2u;
        cloud_id=PORTABLE_TITLE_WIDE_CLOUDS+variant*2u;
        if(portable_asset_pack_find_section(background_pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
                sky_id,&sky_section)!=PORTABLE_ASSET_PACK_OK ||
           portable_asset_pack_find_section(background_pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
                cloud_id,&cloud_section)!=PORTABLE_ASSET_PACK_OK)
            return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    }
    PortableNativeImage sky,clouds;
    if(!portable_native_image_open(sky_section.bytes,sky_section.size,&sky) ||
       !portable_native_image_open(cloud_section.bytes,cloud_section.size,&clouds) ||
       sky.width!=256u || (sky.height!=256u && !(variant==2u && sky.height==2048u)) ||
       clouds.width!=256u || clouds.height!=512u)
        return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    const int stock_wings=!custom_sky;
    PortableNativeImage floor={0};
    if(stock_wings){
        PortableAssetSectionView flat;
        if(portable_asset_pack_find_section(background_pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
                title_background_id(variant,0u),&flat)!=PORTABLE_ASSET_PACK_OK ||
           !portable_native_image_open(flat.bytes,flat.size,&floor) ||
           floor.width!=240u || floor.height!=160u)
            return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    }
    const unsigned star_row=sky.height==2048u?(flow->tick/9u)%8u:0u;
    const unsigned scale=(unsigned)portable_software_frame_scale(frame);
    const int center=(int)((frame->width-240u)/2u);
    /* Continue the title's own sky/cloud scene on both wings. The original
     * central composite supplies every logo, character and flag pixel. The
     * cloud drift reads the same immutable title clock, with no extra timer. */
    for(unsigned y=0u;y<frame->height*scale;++y)for(unsigned x=0u;x<frame->width*scale;++x){
        const int64_t scene_x=portable_software_frame_floor_div(x,scale)-center;
        const unsigned scene_y=y/scale;
        /* Stock sky has an authored 240-pixel scene; its remaining map cells
         * contain only backdrop. Reflect the sky beyond each edge to continue
         * its rays without the unused cyan strip or any copied characters.
         * Intro clouds are absent from the settled source panorama. Explicit
         * mod layers retain their full wrapped coordinate space. */
        unsigned source_x=title_wrap(scene_x,stock_wings?480u:256u);
        if(stock_wings && source_x>=240u)source_x=479u-source_x;
        uint32_t pixel=portable_native_image_pixel(&sky,source_x,
            star_row*256u+(scene_y+80u)%256u);
        /* The four-column pavement strip at the source panorama's left edge
         * contains no driver or logo pixels. Continue its shading below the
         * same horizon, keeping the central character lineup intact. */
        if(stock_wings && scene_y>=144u)
            pixel=portable_native_image_pixel(&floor,title_wrap(scene_x,4u),scene_y);
        if(!stock_wings){
            const uint32_t cloud=portable_native_image_pixel(&clouds,
                title_wrap(scene_x+(int64_t)(flow->tick/8u),256u),(scene_y+96u)%512u);
            if(cloud>>24u==255u)pixel=cloud;
        }
        if(pixel>>24u==255u)portable_software_frame_store_physical(frame,x,y,pixel);
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

static PortableFrontendPresentationStatus title_chrome_draw(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    uint32_t legacy,unsigned slot,PortableSoftwareFrame *frame,
    int x,int y,int left,int top,int right,int bottom)
{
    PortableFrontendPresentationStatus status=portable_frontend_image_draw_atlas(pack,0x20800u+slot,frame,
        16u,(flow->tick/6u)&15u,x,y,left,top,right,bottom);
    return status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION?
        portable_frontend_image_draw(pack,legacy,frame,x,y,left,top,right,bottom):status;
}

/* title_8000350 menu spacing/clipping and title_main nine-tick flag cadence.
 * Pixels come from normalized ROM art; drawing never advances frontend state. */

PortableFrontendPresentationStatus portable_title_render(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableFrontendPage page, PortableSoftwareFrame *frame)
{
    unsigned phase;
    uint32_t background;
    PortableFrontendPresentationStatus status;
    if (pack==NULL || flow==NULL || frame==NULL || frame->pixels==NULL ||
        frame->width!=240u || frame->height!=160u)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    phase=(flow->tick/9u)%5u;
    /* Short Transitions enters the settled title list immediately. Source
     * brightness phases live in frontend_presentation, not a page crossfade. */
    portable_software_frame_clear(frame,UINT32_C(0xff000000));
    status=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if (flow->title_variant==1u||flow->title_variant==2u) {
        /* Packs imported before the sunset/night art keep the normal title. */
        background=(flow->title_variant==2u ? PORTABLE_TITLE_NIGHT_BACKGROUND :
                    PORTABLE_TITLE_SUNSET_BACKGROUND)+phase;
        status=portable_frontend_image_draw(pack,background,frame,0,0,0,0,240,160);
        /* Night stars step bank 7 colours 10..13 through eight rows every 9 frames. */
        const unsigned row=(flow->tick/9u)%8u;
        if (status==PORTABLE_FRONTEND_PRESENTATION_OK&&flow->title_variant==2u&&row!=0u)
            status=portable_frontend_image_draw(pack,PORTABLE_TITLE_NIGHT_TWINKLE+row,
                                                frame,0,0,0,0,240,160);
    }
    if (status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION) {
        background=phase==0u ? PORTABLE_TITLE_BACKGROUND :
                            PORTABLE_TITLE_BACKGROUND_FRAME+phase;
        status=portable_frontend_image_draw(pack,background,frame,0,0,0,0,240,160);
    }
    if (status!=PORTABLE_FRONTEND_PRESENTATION_OK ||
        page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO) return status;
    if(flow->title_variant==2u){
        PortableFrontendMotion motion;
        if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
        if(motion.bytes){
            unsigned flight=flow->title_ship_ticks>601u?flow->title_ship_ticks-601u:0u;
            int x=flight<2304u?-32+(int)(flight/8u):256;
            int sine=portable_menu_sine(&motion,(flight&255u)*4u);
            int y=flight?32+(sine>=0?sine*8/65536:-((-sine*8+65535)/65536)):0;
            PortableFrontendPresentationStatus actor=portable_frontend_image_draw(pack,0x20700u,frame,x,y,0,0,240,160);
            if(actor!=PORTABLE_FRONTEND_PRESENTATION_OK&&actor!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)return actor;
            if(flow->title_star_state==2u){
                unsigned t=flow->title_star_counter;if(t>32u)t=32u;
                unsigned remaining=(32u-t)*(32u-t);
                int star_x=10+(int)(50u*remaining/1024u),star_y=50-(int)(60u*remaining/1024u);
                actor=portable_frontend_image_draw_affine(pack,0x20701u,frame,&motion,0,0,star_x,star_y,384u-t,(0u-t*64u)&1023u,240);
                if(actor!=PORTABLE_FRONTEND_PRESENTATION_OK&&actor!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)return actor;
            }
        }
    }

    if (flow->title_cursor>=4u) return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if (flow->player_count_open) {
        /* Local player count in the link sub-menu frame: the spm N-P tab,
         * with the title's blinking arrows while more or fewer are possible. */
        const unsigned most=flow->max_players>=2u?flow->max_players:2u;
        status=title_chrome_draw(pack,flow,PORTABLE_TITLE_LINK_FRAME,3u,frame,120,132,0,0,240,160);
        if (status==PORTABLE_FRONTEND_PRESENTATION_OK)
            status=portable_frontend_image_draw(pack,PORTABLE_MENU_TAB_GAME+flow->players-1u,frame,120,132,0,0,240,160);
        if (status!=PORTABLE_FRONTEND_PRESENTATION_OK||(flow->tick&0x2fu)>0x1fu) return status;
        if (flow->players<most)
            status=title_chrome_draw(pack,flow,PORTABLE_TITLE_UP,1u,frame,120,125,0,0,240,160);
        if (status==PORTABLE_FRONTEND_PRESENTATION_OK&&flow->players>2u)
            status=title_chrome_draw(pack,flow,PORTABLE_TITLE_DOWN,2u,frame,120,147,0,0,240,160);
        return status;
    }
    status=title_chrome_draw(pack,flow,PORTABLE_TITLE_FRAME,0u,frame,120,132,40,117,200,147);
    if (status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    const int list_y=flow->title_cursor_motion.configured&&!flow->short_transitions?
        flow->title_cursor_motion.y:132-16*(int)flow->title_cursor;
    for (unsigned option=0u;option<3u;++option) {
        uint32_t id=(option==flow->title_cursor ? PORTABLE_TITLE_SELECTED_LABEL :
                     PORTABLE_TITLE_LABEL)+option;
        status=option==flow->title_cursor?portable_frontend_image_draw_atlas(pack,0x20810u+option,frame,
            4u,(flow->tick/4u)&3u,120,list_y+16*(int)option,40,117,200,147):
            PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
        if(status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)
            status=portable_frontend_image_draw(pack,id,frame,120,list_y+16*(int)option,40,117,200,147);
        if (status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    /* The native-only fourth row has no ROM label; it uses the menu font. */
    if (flow->title_cursor==3u) portable_menu_text_centered(pack,frame,120,124,"OPTIONS");
    if ((flow->tick&0x2fu)<=0x1fu) {
        if (flow->title_cursor>0u) {
            status=title_chrome_draw(pack,flow,PORTABLE_TITLE_UP,1u,frame,120,125,40,117,200,147);
            if (status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
        }
        if (flow->title_cursor<3u)
            status=title_chrome_draw(pack,flow,PORTABLE_TITLE_DOWN,2u,frame,120,147,40,117,200,160);
    }
    return status;
}
