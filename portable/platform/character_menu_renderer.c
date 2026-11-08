#include "character_menu_renderer.h"
#include "content_registry.h"
#include "frontend_config.h"
#include "menu_font.h"
#include "frontend_image_renderer.h"
#include "menu_chrome_renderer.h"
#include "menu_names.h"
#include "native_image.h"

#include <stdio.h>
#include <string.h>

static PortableFrontendPresentationStatus animation_affine(
    const PortableAssetPackView *pack,uint32_t id,uint32_t tick,
    const PortableFrontendMotion *motion,int pivot_x,int pivot_y,int x,int y,
    unsigned inverse,unsigned angle,int right,PortableSoftwareFrame *frame)
{
    PortableAssetSectionView section;PortableNativeAnimation animation;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if(!portable_native_animation_open(section.bytes,section.size,&animation))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    return portable_frontend_image_draw_affine(pack,portable_native_animation_frame(&animation,tick),frame,
        motion,pivot_x,pivot_y,x,y,inverse,angle,right);
}

static int has_art(const PortableAssetPackView *pack,uint32_t id)
{
    PortableAssetSectionView section;
    return portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
        id,&section)==PORTABLE_ASSET_PACK_OK;
}

enum { PORTABLE_CHARACTER_PORTRAIT_INSET=2 }; /* Stock portrait frame border. */

/* The native rectangle an image (or an animation's frame at tick) covers
 * when drawn unscaled at (x, y). */
static int art_rect(const PortableAssetPackView *pack,uint32_t id,int animated,uint32_t tick,int x,int y,
                    int *left,int *top,int *width,int *height)
{
    PortableAssetSectionView section;PortableNativeImage image;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return 0;
    if(animated){
        PortableNativeAnimation animation;
        if(!portable_native_animation_open(section.bytes,section.size,&animation))return 0;
        if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
               portable_native_animation_frame(&animation,tick),&section)!=PORTABLE_ASSET_PACK_OK)return 0;
    }
    if(!portable_native_image_open(section.bytes,section.size,&image)||image.width==0u||image.height==0u)return 0;
    *left=x+image.anchor_x;*top=y+image.anchor_y;*width=image.width;*height=image.height;
    return 1;
}

/* Grid slots share one cursor cell placed at each slot, so the difference of
 * two cursor anchors moves art placed for one slot onto another. */
static int slot_offset(const PortableAssetPackView *pack,uint8_t from,uint8_t to,
                       int *x,int *y)
{
    PortableAssetSectionView a,b;
    PortableNativeImage from_image,to_image;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
           PORTABLE_CHARACTER_CURSOR+from,&a)!=PORTABLE_ASSET_PACK_OK||
       portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
           PORTABLE_CHARACTER_CURSOR+to,&b)!=PORTABLE_ASSET_PACK_OK||
       !portable_native_image_open(a.bytes,a.size,&from_image)||
       !portable_native_image_open(b.bytes,b.size,&to_image))return 0;
    *x=to_image.anchor_x-from_image.anchor_x;
    *y=to_image.anchor_y-from_image.anchor_y;
    return 1;
}

/* Stock slots use their own art. Roster drivers use their own art at
 * PORTABLE_CHARACTER_ROSTER_ART, else their seat driver's stock art. */
static int driver_slot_art(const PortableAssetPackView *pack,const PortableFrontendDriverSlot *driver,
                           uint8_t page,uint8_t slot,PortableCharacterSlotArt *out)
{
    if(page==0u||slot==PORTABLE_FRONTEND_RANDOM_SLOT){
        *out=(PortableCharacterSlotArt){PORTABLE_CHARACTER_PORTRAIT+slot*3u,PORTABLE_CHARACTER_NAME+slot,
                       PORTABLE_CHARACTER_PREVIEW+slot,slot};
        return 1;
    }
    if(driver==NULL||driver->content_id==0u)return 0;
    const uint32_t own=portable_character_roster_art(driver->content_id);
    const uint8_t seat=driver->seat_slot;
    const int portrait=has_art(pack,own+PORTABLE_CHARACTER_ROSTER_PORTRAIT);
    *out=(PortableCharacterSlotArt){
        portrait?own+PORTABLE_CHARACTER_ROSTER_PORTRAIT:PORTABLE_CHARACTER_PORTRAIT+seat*3u,
        has_art(pack,own+PORTABLE_CHARACTER_ROSTER_NAME)?own+PORTABLE_CHARACTER_ROSTER_NAME:
            (uint32_t)(PORTABLE_CHARACTER_NAME+seat),
        has_art(pack,own+PORTABLE_CHARACTER_ROSTER_PREVIEW)?own+PORTABLE_CHARACTER_ROSTER_PREVIEW:
            (uint32_t)(PORTABLE_CHARACTER_PREVIEW+seat),
        portrait?driver->art_slot:seat};
    return 1;
}

int portable_character_menu_slot_art(const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
                                     uint8_t page,uint8_t slot,PortableCharacterSlotArt *out)
{
    return driver_slot_art(pack,portable_frontend_flow_driver_at(flow,page,slot),page,slot,out);
}

int portable_character_menu_driver_art(const PortableAssetPackView *pack,uint32_t character_id,
                                       PortableCharacterSlotArt *out)
{
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    if(pack==NULL||out==NULL||character_id==0u||
       !build_frontend_config(pack,drivers,courses,&config))return 0;
    /* Use the same resolved grid and seat fallback as character selection;
     * content IDs do not encode an artwork slot. */
    for(size_t index=0u;index<config.driver_count;++index)
        if(drivers[index].available&&drivers[index].content_id==character_id)
            return driver_slot_art(pack,&drivers[index],
                (uint8_t)(index/PORTABLE_FRONTEND_DRIVER_SLOTS),
                (uint8_t)(index%PORTABLE_FRONTEND_DRIVER_SLOTS),out);
    return 0;
}

uint32_t portable_character_roster_art(uint32_t character_id)
{
    return PORTABLE_CHARACTER_ROSTER_ART+character_id*8u;
}

int portable_character_roster_image(const PortableAssetPackView *pack,
    uint32_t character_id,unsigned part,PortableNativeImage *out)
{
    PortableAssetSectionView section;
    PortableNativeImage image;
    if(pack==NULL||out==NULL||character_id==0u||character_id>PORTABLE_DRIVER_ROSTER_MAX_ID||
       (part!=PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT&&part!=PORTABLE_CHARACTER_ROSTER_BOARD_NAME)||
       portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
           portable_character_roster_art(character_id)+part,&section)!=PORTABLE_ASSET_PACK_OK||
       !portable_native_image_open(section.bytes,section.size,&image))return 0;
    if(part==PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT?image.width!=16u||image.height!=16u:
       image.width==0u||image.width>40u||image.height==0u||image.height>16u)return 0;
    *out=image;return 1;
}

uint32_t portable_character_menu_name_id(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,uint32_t character_id)
{
    if(pack==NULL||flow==NULL)return 0u;
    for(uint8_t page=0u;page<portable_frontend_flow_driver_pages(flow);++page)
        for(uint8_t slot=0u;slot<PORTABLE_FRONTEND_DRIVER_SLOTS;++slot){
            const PortableFrontendDriverSlot *driver=
                portable_frontend_flow_driver_at(flow,page,slot);
            PortableCharacterSlotArt art;
            if(driver!=NULL&&driver->content_id==character_id&&character_id!=0u&&
               portable_character_menu_slot_art(pack,flow,page,slot,&art))return art.name;
        }
    return 0u;
}

PortableFrontendPresentationStatus portable_character_menu_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableCharacterPreviewHook *preview,PortableSoftwareFrame *frame)
{
    PortableFrontendPresentationStatus status;
    const uint32_t selected_tick=(flow->character_animation_timing&1u)
        ? flow->tick-flow->character_selection_tick : flow->page_ticks;
    const uint32_t lock_tick=(flow->character_animation_timing&2u)
        ? flow->tick-flow->character_lock_tick : 0u;
    const uint8_t page=flow->character_page;
    const uint8_t driver_pages=portable_frontend_flow_driver_pages(flow);
    const uint8_t pages=driver_pages!=0u?driver_pages:1u; /* Stock grid alone. */
    PortableCharacterSlotArt cursor_art;
    PortableFrontendMotion motion;
    if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    unsigned entry=flow->motion_stage==1u?flow->motion_tick:16u;
    unsigned shrink=0u;
    if(motion.bytes&&flow->motion_stage==2u){
        unsigned start=flow->pending_page==PORTABLE_FRONTEND_PAGE_COURSE?35u:0u;
        if(flow->motion_tick>start)shrink=flow->motion_tick-start;
    }
    if(flow->character_cursor>=PORTABLE_FRONTEND_DRIVER_SLOTS||page>=pages)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    status=portable_menu_background_render(pack,flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,frame);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    status=portable_frontend_heading_draw(pack,flow,frame,1u,PORTABLE_CHARACTER_HEADING,0,0);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    for(uint8_t slot=0u;slot<PORTABLE_FRONTEND_DRIVER_SLOTS;++slot) {
        unsigned state=slot==flow->character_cursor ? (flow->character_locked ? 2u : 1u) : 0u;
        PortableCharacterSlotArt art;
        int x=0,y=0;
        if(!portable_character_menu_slot_art(pack,flow,page,slot,&art))continue; /* Empty roster slot. */
        if(art.art_slot!=slot&&!slot_offset(pack,art.art_slot,slot,&x,&y))
            return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
        int dx,dy;portable_menu_character_anchor(&motion,slot,entry,&dx,&dy);
        if(shrink>16u)continue;
        const uint8_t *anchor=motion.bytes?motion.bytes+16u+slot*8u:NULL;
        int px=anchor?portable_menu_i16(anchor+4):0,py=anchor?portable_menu_i16(anchor+6):0;
        const uint32_t portrait_tick=state==0u?flow->page_ticks:(state==1u?selected_tick:lock_tick);
        status=animation_affine(pack,art.portrait+state,portrait_tick,&motion,
            px-x,py-y,x+dx,y+dy,256u+shrink*80u,shrink*64u,240,frame);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
        /* 3D-only drivers put their own portrait inside the seat portrait's
         * frame; the frame keeps the stock highlight and lock animation. */
        const PortableFrontendDriverSlot *driver=portable_frontend_flow_driver_at(flow,page,slot);
        int left,top,width,height;
        if(shrink==0u&&preview!=NULL&&preview->portrait!=NULL&&driver!=NULL&&driver->mesh_only&&
           art_rect(pack,art.portrait+state,1,portrait_tick,x+dx,y+dy,&left,&top,&width,&height)&&
           width>8&&height>8)
            preview->portrait(preview->owner,driver->content_id,left+PORTABLE_CHARACTER_PORTRAIT_INSET,
                top+PORTABLE_CHARACTER_PORTRAIT_INSET,width-2*PORTABLE_CHARACTER_PORTRAIT_INSET,
                height-2*PORTABLE_CHARACTER_PORTRAIT_INSET,frame);
    }
    /* sub_8006518: players who already chose show their badge at a corner of
     * that slot; the choosing player keeps its own cursor, locked or not. */
    const unsigned chooser=flow->players>1u?flow->character_player%PORTABLE_FRONTEND_MAX_PLAYERS:0u;
    for(unsigned player=0u;player<chooser;++player){
        const uint32_t chosen=flow->player_characters[player];
        for(uint8_t slot=0u;slot<PORTABLE_FRONTEND_DRIVER_SLOTS;++slot){
            const PortableFrontendDriverSlot *driver=portable_frontend_flow_driver_at(flow,page,slot);
            if(driver==NULL||chosen==0u||driver->content_id!=chosen)continue;
            status=portable_frontend_image_draw(pack,PORTABLE_CHARACTER_PLAYER_BADGE+player*16u+slot,
                                                frame,0,0,0,0,240,160);
            if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
        }
    }
    int cursor_x,cursor_y;portable_menu_character_anchor(&motion,flow->character_cursor,entry,&cursor_x,&cursor_y);
    if(flow->motion_stage!=2u)status=portable_frontend_image_draw(pack,(chooser==0u?PORTABLE_CHARACTER_CURSOR:
        PORTABLE_CHARACTER_PLAYER_CURSOR+chooser*16u)+flow->character_cursor,frame,cursor_x,cursor_y,0,0,240,160);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    if(pages>1u){
        char label[16];
        snprintf(label,sizeof label,"%u/%u",(unsigned)page+1u,(unsigned)pages);
        portable_menu_text(pack,frame,236-8*(int)strlen(label),2,label);
    }
    if(!portable_character_menu_slot_art(pack,flow,page,flow->character_cursor,&cursor_art))
        return PORTABLE_FRONTEND_PRESENTATION_OK;
    const PortableFrontendDriverSlot *highlighted=
        portable_frontend_flow_driver_at(flow,page,flow->character_cursor);
    /* 3D-only drivers' names are text, centred where the seat name art sits. */
    const char *text_name=highlighted!=NULL&&highlighted->mesh_only?portable_menu_driver_name(highlighted->content_id):NULL;
    int name_left,name_top,name_width,name_height;
    if(text_name!=NULL&&art_rect(pack,cursor_art.name,0,0u,0,0,&name_left,&name_top,&name_width,&name_height)){
        int center=name_left+name_width/2;
        const int half=(int)strlen(text_name)*PORTABLE_MENU_FONT_WIDTH/2;
        if(center-half<2)center=2+half;
        if(center+half>140)center=140-half;
        portable_menu_text_centered(pack,frame,center,name_top+(name_height-PORTABLE_MENU_FONT_HEIGHT)/2,text_name);
    }else{
        status=portable_frontend_image_draw(pack,cursor_art.name,frame,0,0,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    /* sub_80065CC: the highlighted driver's speed/weight stars, highlighted
     * or locked alike; roster drivers show their seat driver's stars. */
    const uint8_t stats_slot=page==0u||highlighted==NULL?flow->character_cursor:highlighted->seat_slot;
    if(stats_slot!=PORTABLE_FRONTEND_RANDOM_SLOT&&has_art(pack,PORTABLE_CHARACTER_STATS+stats_slot)){
        status=portable_frontend_image_draw(pack,PORTABLE_CHARACTER_STATS+stats_slot,frame,0,0,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    unsigned inverse=motion.bytes?2048u-(unsigned)portable_menu_asr15(1792ll*portable_menu_sine(&motion,(entry<16u?entry:16u)*16u)):256u;
    int preview_y=0;
    if(flow->motion_stage==2u&&flow->pending_page==PORTABLE_FRONTEND_PAGE_COURSE){
        unsigned t=flow->motion_tick>2u?flow->motion_tick-2u:0u;
        preview_y=portable_menu_sine_lerp(&motion,0,-241,t,32u);
    }
    /* 3D-only drivers show their own model where the host can draw it, at
     * the stock preview's centre and entry zoom. */
    if(preview!=NULL&&preview->draw!=NULL&&highlighted!=NULL&&highlighted->mesh_only){
        const unsigned size=inverse!=0u?72u*256u/inverse:0u;
        if(size<8u||preview->draw(preview->owner,flow,highlighted->content_id,72,80+preview_y,size,frame))
            return PORTABLE_FRONTEND_PRESENTATION_OK;
    }
    return animation_affine(pack,cursor_art.preview,selected_tick,&motion,72,83,0,preview_y,inverse,0u,240,frame);
}
