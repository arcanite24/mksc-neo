#include "course_menu_renderer.h"
#include "frontend_image_renderer.h"
#include "menu_font.h"
#include "menu_chrome_renderer.h"
#include "native_image.h"

#include <stdio.h>
#include <string.h>

/* C8DAC is a semi-transparent OBJ: source cup alpha is10/16 over6/16. */
static PortableFrontendPresentationStatus preview_panel(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,unsigned cup,uint32_t id,PortableSoftwareFrame *frame,int x,int y)
{
    PortableAssetSectionView section;PortableNativeImage image;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if(!portable_native_image_open(section.bytes,section.size,&image))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    PortableFrontendMotion motion;
    if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    PortableNativeImage pattern,affine;PortableMenuBackgroundLayers layers;
    int have_layers=0;
    if(motion.bytes&&portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
       PORTABLE_COURSE_BACKGROUND+cup,&section)==PORTABLE_ASSET_PACK_OK){
        if(!portable_native_image_open(section.bytes,section.size,&pattern)||pattern.width!=256u||
           (pattern.height!=256u&&pattern.height!=1792u))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
        const PortableNativeImage *affine_view=NULL;
        PortableAssetPackStatus status=portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
            PORTABLE_COURSE_AFFINE+cup,&section);
        if(status!=PORTABLE_ASSET_PACK_OK)status=portable_asset_pack_find_section(pack,
            PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,PORTABLE_MENU_AFFINE,&section);
        if(status==PORTABLE_ASSET_PACK_OK){
            if(!portable_native_image_open(section.bytes,section.size,&affine)||affine.width!=256u||affine.height!=256u)
                return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
            affine_view=&affine;
        }
        portable_menu_background_layers_init(&layers,&pattern,affine_view,flow,&motion);
        have_layers=1;
    }
    const int scale=(int)portable_software_frame_scale(frame);
    const int left=(x+image.anchor_x)*scale,top=(y+image.anchor_y)*scale;
    for(int py=0;py<(int)image.height*scale;++py)for(int px=0;px<(int)image.width*scale;++px){
        const int dx=left+px,dy=top+py;
        if(dx<0||dy<0||dx>=240*scale||dy>=160*scale||
           dx>=(int)frame->width*scale||dy>=(int)frame->height*scale)continue;
        const uint32_t foreground=portable_native_image_pixel(&image,(unsigned)(px/scale),(unsigned)(py/scale));
        if(foreground>>24u!=255u)continue;
        const uint32_t background=have_layers?
            portable_menu_background_layers_sample(&layers,(unsigned)dx,(unsigned)dy,(unsigned)scale):
            frame->pixels[(size_t)dy*portable_software_frame_stride(frame)+(size_t)dx];
        uint32_t result=UINT32_C(0xff000000);
        for(unsigned shift=0u;shift<24u;shift+=8u){
            unsigned channel=(((foreground>>shift)&255u)*10u+
                              ((background>>shift)&255u)*6u)/16u;
            result|=channel<<shift;
        }
        portable_software_frame_store_physical(frame,(size_t)dx,(size_t)dy,result);
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

/* Custom cups may omit art: an absent slot falls back to stock art or text. */
static PortableFrontendPresentationStatus draw_or(const PortableAssetPackView *pack,
    uint32_t id,uint32_t fallback,PortableSoftwareFrame *frame,int x,int y,unsigned scale)
{
    PortableFrontendPresentationStatus status=portable_frontend_image_draw_scaled(
        pack,id,frame,x,y,0,0,240,160,scale);
    if(status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION&&fallback!=id)
        status=portable_frontend_image_draw_scaled(pack,fallback,frame,x,y,0,0,240,160,scale);
    return status;
}

static uint32_t slot_course(const PortableFrontendFlow *flow,unsigned cup,unsigned track)
{
    for(size_t index=0u;index<flow->config.course_count;++index)
        if(flow->config.courses[index].cup==cup&&flow->config.courses[index].track==track)
            return flow->config.courses[index].content_id;
    return 0u;
}

static PortableFrontendPresentationStatus custom_label(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,PortableSoftwareFrame *frame,int x,int y,unsigned cup,unsigned track,int selected)
{
    char text[24];
    const uint32_t course=slot_course(flow,cup,track);
    if(course!=0u)snprintf(text,sizeof text,"COURSE %u",(unsigned)course);
    else snprintf(text,sizeof text,"---");
    portable_menu_text(pack,frame,x,y,text);
    /* Text labels have no highlight colour; the 58B0 cursor marks the pick. */
    return selected?portable_frontend_image_draw(pack,PORTABLE_MENU_OPTION_CURSOR,frame,x-8,y+8,0,0,240,160):
        PORTABLE_FRONTEND_PRESENTATION_OK;
}

/* Course names and the phase-2 underline, as on the cup page. */
static PortableFrontendPresentationStatus track_list(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,PortableSoftwareFrame *frame,unsigned cup,unsigned track,int custom)
{
    for(unsigned index=0u;index<4u;++index) {
        PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,
            (index==track ? PORTABLE_COURSE_SELECTED_NAME : PORTABLE_COURSE_NAME)+cup*4u+index,
            frame,120,80+(int)index*16,0,0,240,160);
        if(custom&&status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)
            status=custom_label(pack,flow,frame,120,80+(int)index*16,cup,index,index==track);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    if(flow->course_phase==2u)
        portable_software_frame_rect(frame,120,80u+track*16u,96,1,UINT32_C(0xffffdd00));
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

/* A 3D cup group: the group name in the title band, its cups and the
 * selected cup's courses as window text right of the stock side bar (x<32),
 * with the 58B0 cursor; names come from the data. */
/* Draws at most `columns` glyphs. A longer name (a mod's course title) scrolls through that window
 * with a pause at each end instead of being cut, so the whole name can be read. */
static void clipped_text(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,int x,int y,
    const char *text,unsigned columns,int first_word,uint32_t tick)
{
    char line[96];
    snprintf(line,sizeof line,"%s",text&&text[0]?text:"---");
    char *space=first_word?strchr(line,' '):NULL;
    if(space!=NULL&&space!=line)*space=0;
    const size_t length=strlen(line);
    size_t offset=0u;
    if(length>columns){
        const size_t span=length-columns,leg=span+6u;
        size_t step=(size_t)((tick/8u)%(2u*leg));
        if(step>=leg)step=2u*leg-1u-step;
        offset=step>3u?step-3u:0u;
        if(offset>span)offset=span;
    }
    char window[32];
    if(columns>=sizeof window)columns=sizeof window-1u;
    snprintf(window,columns+1u,"%s",line+offset);
    portable_menu_text(pack,frame,x,y,window);
}

static PortableFrontendPresentationStatus mesh_course_render(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,PortableSoftwareFrame *frame)
{
    if(flow->mesh_cup>=flow->mesh_cup_count||flow->mesh_group==0u||flow->mesh_group>flow->mesh_group_count)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    PortableFrontendPresentationStatus status=portable_menu_background_render(pack,flow,
        PORTABLE_COURSE_BACKGROUND,PORTABLE_COURSE_BACKGROUND,frame);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    status=portable_menu_panel(pack,frame,32,24,88u,104u); /* Clear of the kart badge. */
    if(status==PORTABLE_FRONTEND_PRESENTATION_OK)status=portable_menu_panel(pack,frame,120,24,120u,136u);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    char group[27];
    snprintf(group,sizeof group,"%s",flow->mesh_group_names[flow->mesh_group-1u]);
    portable_menu_text_centered(pack,frame,136,8,group);
    unsigned open[PORTABLE_FRONTEND_MESH_CUP_CAPACITY],count=0u,at=0u;
    for(size_t index=0u;index<flow->mesh_cup_count;++index)
        if(flow->mesh_cups[index].group+1u==flow->mesh_group&&portable_frontend_flow_mesh_cup_open(flow,index)){
            if(index==flow->mesh_cup)at=count;
            open[count++]=(unsigned)index;
        }
    /* Cups show their first word ("MUSHROOM" of "MUSHROOM CUP"). */
    const unsigned cup_rows=6u,first=at>=cup_rows?at-cup_rows+1u:0u;
    for(unsigned row=0u;row<cup_rows&&first+row<count;++row){
        const PortableFrontendMeshCup *cup=&flow->mesh_cups[open[first+row]];
        char name[24];
        snprintf(name,sizeof name,"CUP %u",first+row+1u);
        const int y=30+(int)row*16;
        clipped_text(pack,frame,46,y,cup->name&&cup->name[0]?cup->name:name,9u,cup->name&&cup->name[0],flow->tick);
        if(first+row==at&&flow->course_phase==0u&&
           portable_frontend_image_draw(pack,PORTABLE_MENU_OPTION_CURSOR,frame,38,y+8,0,0,240,160)==
               PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION)
            return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    }
    const PortableFrontendMeshCup *cup=&flow->mesh_cups[flow->mesh_cup];
    const uint32_t mode=portable_frontend_flow_mesh_mode(flow);
    clipped_text(pack,frame,128,30,cup->name,13u,0,flow->tick);
    const unsigned visible=7u;
    const unsigned track=flow->mode_cursor==0u?flow->gp_preview_track%cup->course_count:flow->course_track;
    const unsigned rows_first=track>=visible?track-visible+1u:0u;
    for(unsigned row=0u;row<visible&&rows_first+row<cup->course_count;++row){
        const unsigned index=rows_first+row;
        const int y=50+(int)row*16;
        clipped_text(pack,frame,136,y,(cup->course_modes[index]&mode)?cup->course_names[index]:"--",12u,0,flow->tick);
        if(index==track&&flow->course_phase!=0u&&flow->mode_cursor!=0u&&
           portable_frontend_image_draw(pack,PORTABLE_MENU_OPTION_CURSOR,frame,128,y+8,0,0,240,160)==
               PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION)
            return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    }
    if(flow->mode_cursor==0u&&flow->course_phase==1u&&(flow->tick&0x2fu)<=0x0fu){
        status=portable_frontend_image_draw(pack,PORTABLE_COURSE_OK,frame,220,144,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK&&status!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)return status;
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

PortableFrontendPresentationStatus portable_course_menu_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableSoftwareFrame *frame)
{
    PortableFrontendPresentationStatus status;
    PortableFrontendMotion motion;
    if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    int preview_x=flow->motion_stage==1u?portable_menu_preview_offset(&motion,flow->motion_tick):0;
    if(flow->motion_stage==2u&&motion.bytes)
        preview_x=portable_menu_sine_lerp(&motion,0,160,flow->motion_tick,16u);
    static const int cup_positions[5][2]={{32,32},{46,54},{60,76},{74,98},{88,120}};
    const PortableTrackSet set=portable_track_set_of_cup(flow->course_cup);
    unsigned track=flow->mode_cursor==0u?flow->gp_preview_track:flow->course_track;
    if(flow->mesh_group!=0u)return mesh_course_render(pack,flow,frame);
    if (flow->mode_cursor==3u) {
        /* Arenas use the cup page's track layout under CHOOSE A TRACK. */
        if (track>=4u) return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
        status=portable_menu_background_render(pack,flow,PORTABLE_COURSE_BACKGROUND,PORTABLE_COURSE_BACKGROUND,frame);
        if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
            status=portable_frontend_heading_draw(pack,flow,frame,3u,PORTABLE_COURSE_HEADING+1u,1,7);
        if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
            status=portable_frontend_image_draw(pack,PORTABLE_COURSE_PREVIEW+PORTABLE_FRONTEND_ARENA_CUP*4u+track,
                frame,144+preview_x,16,0,0,240,160);
        if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
            status=track_list(pack,flow,frame,PORTABLE_FRONTEND_ARENA_CUP,track,0);
        for(uint8_t group=1u;status==PORTABLE_FRONTEND_PRESENTATION_OK&&group<=flow->mesh_group_count;++group)
            if(portable_frontend_flow_mesh_group_open(flow,group)){
                portable_menu_text(pack,frame,8,142,"L/R: 3D");
                break;
            }
        return status;
    }
    if((set!=PORTABLE_TRACK_SET_GBA&&set!=PORTABLE_TRACK_SET_EXTRA&&set!=PORTABLE_TRACK_SET_CUSTOM) ||
       track>=4u || flow->course_phase>2u)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    const int custom=set==PORTABLE_TRACK_SET_CUSTOM;
    const unsigned cup=flow->course_cup-portable_track_set_first_cup(set);
    /* Custom cup icons/names use slots 15..19 after the stock icons 0..9. */
    const unsigned custom_art=custom?15u:0u;
    unsigned lock_tick=0u;
    const int gp_locked=flow->mode_cursor==0u&&flow->course_phase==1u;
    if(flow->mode_cursor==0u&&(gp_locked||flow->cup_motion_phase!=0u)){
        lock_tick=flow->cup_motion_tick>16u?16u:flow->cup_motion_tick;
        if(flow->cup_motion_phase==2u)lock_tick=16u-lock_tick;
        if(flow->cup_motion_phase==0u)lock_tick=16u;
    }
    status=portable_menu_background_render(pack,flow,PORTABLE_COURSE_BACKGROUND+cup,PORTABLE_COURSE_BACKGROUND+cup,frame);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    status=preview_panel(pack,flow,cup,PORTABLE_COURSE_PANEL+cup,frame,192+preview_x,80);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK&&status!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)return status;
    for(unsigned pass=0u;pass<5u;++pass) {
        unsigned index=pass==4u ? cup : (pass>=cup ? pass+1u : pass);
        /* Stock locked trophies remain visible; access gates navigation.
         * A custom cup may genuinely have no authored slot. */
        if(custom&&!(portable_frontend_flow_cup_access(flow)&(1u<<index)))continue;
        int selected=index==cup;
        if(gp_locked&&flow->cup_motion_phase==0u&&!selected)continue;
        int x=cup_positions[index][0]-(selected?4:0),y=cup_positions[index][1]-(selected?4:0);
        unsigned scale=selected?320u:256u,inverse=256u;
        if(motion.bytes&&motion.size>=4280u){
            const uint8_t *anchor=motion.bytes+4260u+index*4u;
            x=portable_menu_i16(anchor);y=portable_menu_i16(anchor+2u);
            unsigned phase=flow->page_ticks&31u;
            inverse=selected?370u-(unsigned)(144ll*(32768-portable_menu_sine(&motion,256u-phase*32u))/65536):272u;
            scale=65536u/inverse;
        }
        if(flow->mode_cursor==0u&&(gp_locked||flow->cup_motion_phase!=0u)){
            x=portable_menu_sine_lerp(&motion,x,46,lock_tick,16u);
            y=portable_menu_sine_lerp(&motion,y,80,lock_tick,16u);
            if(!selected)inverse=272u+(unsigned)portable_menu_asr15(98ll*portable_menu_sine(&motion,lock_tick*16u));
        }
        uint32_t icon=PORTABLE_COURSE_CUP+(custom?custom_art:portable_track_set_first_cup(set))+index;
        /* Optional source-authored unavailable art; older packs retain normal art. */
        if(!custom&&!(portable_frontend_flow_cup_access(flow)&(1u<<index))){
            PortableAssetSectionView locked;
            const uint32_t locked_id=PORTABLE_COURSE_LOCKED_CUP+portable_track_set_first_cup(set)+index;
            if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
                locked_id,&locked)==PORTABLE_ASSET_PACK_OK)icon=locked_id;
        }
        if(motion.bytes&&motion.size>=4280u){
            status=portable_frontend_image_draw_affine(pack,icon,frame,&motion,0,0,x,y,inverse,0u,240);
            if(status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)
                status=portable_frontend_image_draw_affine(pack,PORTABLE_COURSE_CUP+index,frame,&motion,0,0,x,y,inverse,0u,240);
        }else status=draw_or(pack,icon,PORTABLE_COURSE_CUP+index,frame,x,y,scale);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    int name_x=64,name_y=40;
    if(lock_tick){
        name_x+=portable_menu_asr15(-2ll*portable_menu_sine(&motion,lock_tick*16u));
        name_y+=portable_menu_asr15(32ll*portable_menu_sine(&motion,lock_tick*16u));
    }
    status=draw_or(pack,PORTABLE_COURSE_CUP_NAME+custom_art+cup,PORTABLE_COURSE_CUP_NAME+cup,
        frame,name_x,name_y,256u);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    status=portable_frontend_image_draw(pack,PORTABLE_COURSE_PREVIEW+flow->course_cup*4u+track,
        frame,144+preview_x,16,0,0,240,160);
    if(custom&&status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION){
        status=portable_menu_panel(pack,frame,144,16,80u,64u);
        if(status==PORTABLE_FRONTEND_PRESENTATION_OK)portable_menu_text_centered(pack,frame,184,48,"NO MAP");
    }
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    /* Source heading OBJ priority stays in front of the preview frame. */
    status=portable_frontend_heading_draw(pack,flow,frame,
        flow->mode_cursor!=0u&&flow->course_phase!=0u?3u:2u,
        PORTABLE_COURSE_HEADING+(flow->mode_cursor!=0u&&flow->course_phase!=0u),1,7);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    status=track_list(pack,flow,frame,flow->course_cup,track,custom);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    if(flow->mode_cursor==0u&&flow->course_phase==1u&&flow->cup_motion_phase==0u&&
       (flow->tick&0x2fu)<=0x0fu){
        status=portable_frontend_image_draw(pack,PORTABLE_COURSE_OK,frame,220,144,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK&&status!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)return status;
    }
    /* Extra cups differ only by palette, as in B89C; the native custom set
     * is named, and its L/R access marked while it exists. */
    int has_custom=0;
    for(size_t index=0u;index<flow->config.course_count;++index)
        has_custom|=portable_track_set_of_cup(flow->config.courses[index].cup)==PORTABLE_TRACK_SET_CUSTOM;
    if(custom)portable_menu_text(pack,frame,72,54,"CUSTOM");
    else if(flow->mode_cursor<=2u&&(has_custom||flow->mesh_group_count)&&flow->course_phase==0u)
        portable_menu_text(pack,frame,72,54,"L/R");
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

/* 0800C78C: names at (104, 16+16k) over the panel's alternating stripes
 * (palette banks 5/6); the highlighted course uses the cup page's bank 11. */
PortableFrontendPresentationStatus portable_records_menu_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableSoftwareFrame *frame)
{
    uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
    int scroll=(int)((flow->tick&1023u)/4u);
    int negative_scroll=-(int)(((flow->tick&1023u)+3u)/4u);
    PortableFrontendPresentationStatus status=portable_frontend_pattern_draw(pack,0x23804u,frame,scroll,negative_scroll);
    if(status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)
        status=portable_frontend_image_draw(pack,PORTABLE_RECORDS_PANEL,frame,0,0,0,0,240,160);
    else if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
        status=portable_frontend_image_draw(pack,0x23805u,frame,0,0,0,0,240,160);
    const unsigned count=portable_frontend_flow_records_list(flow,entries);
    for(unsigned row=0u;row<PORTABLE_FRONTEND_RECORDS_ROWS&&status==PORTABLE_FRONTEND_PRESENTATION_OK;++row){
        const unsigned index=flow->records_top+row;
        if(index>=count)break;
        const unsigned course=entries[index];
        const int y=16+16*(int)row;
        const int selected=course==flow->course_cup*4u+flow->course_track;
        status=portable_frontend_image_draw(pack,selected?PORTABLE_COURSE_SELECTED_NAME+course:
            PORTABLE_RECORDS_NAME+(row&1u)*64u+course,frame,104,y,0,0,240,160);
        if(status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION&&
           portable_track_set_of_cup((uint8_t)(course/4u))==PORTABLE_TRACK_SET_CUSTOM)
            status=custom_label(pack,flow,frame,108,y,course/4u,course%4u,selected);
    }
    return status;
}
