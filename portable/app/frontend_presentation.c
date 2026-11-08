#include "frontend_presentation.h"

#include "title_renderer.h"
#include "frontend_image_renderer.h"
#include "character_menu_renderer.h"
#include "course_menu_renderer.h"
#include "menu_chrome_renderer.h"

#include <limits.h>

static PortableFrontendPresentationStatus render_screen(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableFrontendPage page, const PortableFrontendPresentationScratch *scratch,
    PortableSoftwareFrame *frame)
{
    if(page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO || page==PORTABLE_FRONTEND_PAGE_TITLE_MENU)
        return portable_title_render(pack,flow,page,frame);
    if(page==PORTABLE_FRONTEND_PAGE_MODE || page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL)
        return portable_menu_render(pack,flow,page,frame);
    if(page==PORTABLE_FRONTEND_PAGE_CHARACTER)
        return portable_character_menu_render(pack,flow,&scratch->character_preview,frame);
    if(page==PORTABLE_FRONTEND_PAGE_COURSE || page==PORTABLE_FRONTEND_PAGE_RECORDS){
        PortableFrontendFlow course=*flow;
        if(course.course_phase>2u)course.course_phase=2u;
        return page==PORTABLE_FRONTEND_PAGE_RECORDS?portable_records_menu_render(pack,flow,frame):
            portable_course_menu_render(pack,&course,frame);
    }
    return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
}

/* Full-width backgrounds use the same resolved page and clocks as its
 * centered foreground. Old flattened images are drawn only by render_screen. */
static PortableFrontendPresentationStatus render_extended_background(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableFrontendPage page, const PortableFrontendPresentationScratch *scratch,
    PortableSoftwareFrame *frame)
{
    portable_software_frame_clear(frame,UINT32_C(0xff000000));
    if(page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO || page==PORTABLE_FRONTEND_PAGE_TITLE_MENU)
        return portable_title_background_render(pack,scratch->title_background_pack,flow,frame);
    if(page==PORTABLE_FRONTEND_PAGE_MODE || page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL ||
       page==PORTABLE_FRONTEND_PAGE_CHARACTER)
        return portable_menu_background_render(pack,flow,PORTABLE_MENU_PATTERN,
            PORTABLE_MENU_BACKGROUND,frame);
    if(page==PORTABLE_FRONTEND_PAGE_COURSE){
        unsigned cup=0u;
        if(flow->mode_cursor!=3u){
            const PortableTrackSet set=portable_track_set_of_cup(flow->course_cup);
            if(set!=PORTABLE_TRACK_SET_GBA && set!=PORTABLE_TRACK_SET_EXTRA &&
               set!=PORTABLE_TRACK_SET_CUSTOM)
                return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
            cup=flow->course_cup-portable_track_set_first_cup(set);
        }
        return portable_menu_background_render(pack,flow,PORTABLE_COURSE_BACKGROUND+cup,
            PORTABLE_COURSE_BACKGROUND+cup,frame);
    }
    if(page==PORTABLE_FRONTEND_PAGE_RECORDS){
        const int scroll=(int)((flow->tick&1023u)/4u);
        const int negative_scroll=-(int)(((flow->tick&1023u)+3u)/4u);
        const PortableFrontendPresentationStatus status=portable_frontend_pattern_draw(
            pack,0x23804u,frame,scroll,negative_scroll);
        return status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION?
            PORTABLE_FRONTEND_PRESENTATION_OK:status;
    }
    return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
}

/* Brightness is a scene phase, not a crossfade between settled screenshots. */
static void fade_to(PortableSoftwareFrame *frame,unsigned amount,uint32_t color)
{
    if(amount>16u)amount=16u;
    const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
    for(size_t y=0u;y<frame->height*scale;++y)for(size_t x=0u;x<frame->width*scale;++x){
        uint32_t old=frame->pixels[y*stride+x],pixel=0xff000000u;
        /* Source brightness acts on five-bit palette channels before RGB
         * expansion. Interpolating expanded bytes invents intermediate colors. */
        for(unsigned shift=0u;shift<=16u;shift+=8u){
            const unsigned from=(old>>shift)&248u,to=(color>>shift)&248u;
            const unsigned channel=((from>>3u)*(16u-amount)+(to>>3u)*amount)/16u;
            pixel|=((channel<<3u)|(channel>>2u))<<shift;
        }
        portable_software_frame_store_physical(frame,x,y,pixel);
    }
}

PortableFrontendPresentationStatus portable_frontend_presentation_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableSoftwareFrame *frame,PortableFrontendPresentationScratch *scratch)
{
    if(!pack||!flow||!frame||!scratch||!frame->pixels||frame->height!=160u||
       (frame->width!=240u && (!scratch->extended_backgrounds || frame->width<240u)))
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    const int extended=frame->width>240u && scratch->extended_backgrounds;
    const size_t scale=portable_software_frame_scale(frame);
    if(extended && (scale>8u || frame->width>(size_t)INT_MAX/scale))
        return PORTABLE_FRONTEND_PRESENTATION_BAD_FRAME;
    PortableFrontendFlow view=*flow;
    PortableFrontendPage page=flow->page;
    unsigned fade=0u;uint32_t fade_color=0xffffffffu;
    if(flow->transition_ticks){
        if(!flow->transition_total_ticks||flow->transition_ticks>flow->transition_total_ticks||
           flow->transition_out_ticks>flow->transition_total_ticks)return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
        unsigned age=flow->transition_total_ticks-flow->transition_ticks;
        if(flow->pending_page==PORTABLE_FRONTEND_PAGE_RACE){
            const unsigned hold=PORTABLE_FRONTEND_COURSE_RACE_TICKS-PORTABLE_FRONTEND_COURSE_RACE_FADE_TICKS;
            fade=age<=hold?0u:(age-hold)*16u/PORTABLE_FRONTEND_COURSE_RACE_FADE_TICKS;
            fade_color=0xff000000u;
        }else if(age<flow->transition_out_ticks){
            view.motion_stage=2u;view.motion_tick=(uint16_t)age;
            if(page==PORTABLE_FRONTEND_PAGE_MODE)view.mode_phase=flow->transition_from_mode_phase;
            /* Brightness follows the controller's counter, including its
             * terminal turn. Course C23C cleanup has no fade state. */
            if(page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO)
                fade=age*16u/10u;
            else if(page==PORTABLE_FRONTEND_PAGE_TITLE_MENU)
                fade=age>1u?age-1u:0u; /* state28 music fade, then state29 */
            else if(flow->transition_out_ticks>=33u&&page!=flow->pending_page&&
                    !(page==PORTABLE_FRONTEND_PAGE_MODE&&flow->pending_page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL)&&
                    !(page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL&&flow->pending_page==PORTABLE_FRONTEND_PAGE_MODE)){
                const unsigned start=flow->transition_out_ticks-33u;
                if(age>=start)fade=(age-start)/2u; /* 52A4 / 8310 */
            }
        }else{
            page=flow->pending_page;view.page=page;view.motion_stage=1u;
            view.motion_tick=(uint16_t)(age-flow->transition_out_ticks);view.page_ticks=view.motion_tick;
            view.motion_delay=flow->page==PORTABLE_FRONTEND_PAGE_CHARACTER?18u:3u;
            view.motion_player_entry=page==PORTABLE_FRONTEND_PAGE_MODE&&flow->page==PORTABLE_FRONTEND_PAGE_TITLE_MENU;
            if(view.motion_player_entry)view.motion_delay=85u;
            if(page==PORTABLE_FRONTEND_PAGE_CHARACTER){view.character_locked=0u;view.character_player=0u;}
            if(page==PORTABLE_FRONTEND_PAGE_COURSE)view.course_phase=view.mode_cursor==3u?1u:0u;
            unsigned t=view.motion_tick;
            if(page<=PORTABLE_FRONTEND_PAGE_TITLE_MENU||view.motion_player_entry||flow->page==PORTABLE_FRONTEND_PAGE_CHARACTER)
                fade=t<16u?16u-t:0u;
        }
    }
    if(view.short_transitions){view.motion_stage=0u;view.motion_tick=0u;fade=0u;}
    PortableSoftwareFrame centered=extended?
        portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER):*frame;
    PortableFrontendPresentationStatus status=PORTABLE_FRONTEND_PRESENTATION_OK;
    if(extended)status=render_extended_background(pack,&view,page,scratch,frame);
    if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
        status=render_screen(pack,&view,page,scratch,&centered);
    if(status==PORTABLE_FRONTEND_PRESENTATION_OK)
        status=portable_menu_chrome_render(pack,&view,page,&centered);
    if(status==PORTABLE_FRONTEND_PRESENTATION_OK&&fade)fade_to(frame,fade,fade_color);
    return status;
}

const char *portable_frontend_presentation_status_name(
    PortableFrontendPresentationStatus status)
{
    static const char *const names[]={"ok","invalid_argument","missing_section",
        "bad_section","bad_frame"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:"invalid_status";
}
