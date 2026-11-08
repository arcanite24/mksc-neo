#include "menu_chrome_renderer.h"
#include "frontend_image_renderer.h"
#include "menu_font.h"
#include "menu_banner.h"

/* Stock face for the 08002EA4 driver tab: page 0 uses its own grid slot,
 * roster drivers their seat driver's slot; Random has no face. */
static int driver_face(const PortableFrontendFlow *flow,uint32_t content_id,uint32_t *face)
{
    for(size_t index=0u;index<flow->config.driver_count;++index){
        const PortableFrontendDriverSlot *driver=&flow->config.drivers[index];
        if(driver->content_id!=content_id||content_id==0u)continue;
        const unsigned slot=index<PORTABLE_FRONTEND_DRIVER_SLOTS?(unsigned)index:driver->seat_slot;
        if(slot==PORTABLE_FRONTEND_RANDOM_SLOT||slot>=PORTABLE_FRONTEND_DRIVER_SLOTS)return 0;
        *face=slot<PORTABLE_FRONTEND_RANDOM_SLOT?slot:slot-1u;
        return 1;
    }
    return 0;
}

/* The source mode action of the chosen medallion: GP 0, TT 1, VS 2,
 * Battle 3, Quick Run 5. Local multiplayer's row 2 is VS. */
static uint32_t mode_action(const PortableFrontendFlow *flow)
{
    static const uint8_t single[4]={0u,1u,5u,3u};
    if(flow->players>1u&&flow->mode_cursor==2u)return 2u;
    return single[flow->mode_cursor&3u];
}

static unsigned tab_ids(const PortableFrontendFlow *flow,PortableFrontendPage page,uint32_t ids[4])
{
    unsigned count=0u;
    const unsigned players=flow->players>=1u&&flow->players<=4u?flow->players:1u;
    /* The 0800C78C records list has no breadcrumb or banner. */
    if(page<PORTABLE_FRONTEND_PAGE_MODE||page>=PORTABLE_FRONTEND_PAGE_RECORDS||
       page==PORTABLE_FRONTEND_PAGE_RACE||page==PORTABLE_FRONTEND_PAGE_PAUSE||
       page==PORTABLE_FRONTEND_PAGE_RESULTS)return 0u;
    ids[count++]=PORTABLE_MENU_TAB_GAME+players-1u;
    if(page==PORTABLE_FRONTEND_PAGE_MODE&&flow->mode_phase==0u)return count;
    ids[count++]=PORTABLE_MENU_TAB_MODE+mode_action(flow);
    if(page==PORTABLE_FRONTEND_PAGE_MODE||page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL)return count;
    ids[count++]=flow->mode_cursor==1u?PORTABLE_MENU_TAB_TRIAL:PORTABLE_MENU_TAB_CLASS+(flow->race_speed%3u);
    uint32_t face;
    const uint32_t chosen=players>1u?flow->player_characters[0]:flow->selected_character;
    if(page==PORTABLE_FRONTEND_PAGE_COURSE&&driver_face(flow,chosen,&face))
        ids[count++]=PORTABLE_MENU_TAB_DRIVER+face;
    return count;
}

/* 58B0 cursor bob: (gSinTable[((tick*5)&127)*8]*2>>15)+2. */
static int option_bob(uint32_t tick)
{
    const unsigned k=(tick*5u)&127u;
    if(k==32u)return 4;
    if(k<11u||(k>53u&&k<65u))return 2;
    if(k<54u)return 3;
    return k<75u||k>117u?1:0;
}

/* sub_8005590/sub_80058B0: Time Trial (action 1) shows two GHOST DISPLAY
 * rows at tile row 10, Quick Run (action 5) three rows at tile row 7 and VS
 * (action 2) four rows at tile row 6 (LAPS, ITEMS, mushroom start, COINS);
 * every window sits at column 7 with brackets 36 px apart on the value. */
static PortableFrontendPresentationStatus option_window(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,PortableSoftwareFrame *frame)
{
    const int vs=flow->players>1u,trial=!vs&&flow->mode_cursor==1u;
    const unsigned rows=vs?4u:trial?2u:3u;
    const int top=vs?48:trial?80:56;
    uint8_t values[4]={0};
    if(trial){values[0]=flow->ghost_display_off&1u;values[1]=(flow->ghost_display_off>>1)&1u;}
    else if(vs){values[0]=flow->vs_options.five_laps;values[1]=flow->vs_options.items_off;
        values[2]=!flow->vs_options.mushroom_start;values[3]=flow->vs_options.coins_off;}
    else{values[0]=flow->quick_run_options.five_laps;values[1]=flow->quick_run_options.items_off;
        values[2]=flow->quick_run_options.coins_off;}
    /* sub_8005590 dims every palette but the window font's and its cursor's. */
    portable_menu_dim(frame);
    PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,
        PORTABLE_MENU_OPTION_WINDOW+(vs?2u:trial?1u:5u),frame,0,0,0,0,240,160);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    status=portable_frontend_image_draw(pack,PORTABLE_MENU_OPTION_CURSOR,frame,
        56+option_bob(flow->tick),top+10+16*(int)(flow->quick_options_cursor%rows),0,0,240,160);
    for(unsigned row=0u;row<rows&&status==PORTABLE_FRONTEND_PRESENTATION_OK;++row)
        status=portable_frontend_image_draw(pack,PORTABLE_MENU_OPTION_BRACKET,frame,
            128+36*(values[row]!=0u),top+8+16*(int)row,0,0,240,160);
    return status;
}

unsigned portable_menu_chrome_tabs(const PortableFrontendFlow *flow,PortableFrontendPage page)
{
    uint32_t ids[4];
    return flow!=NULL?tab_ids(flow,page,ids):0u;
}

PortableFrontendPresentationStatus portable_menu_chrome_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableFrontendPage page,PortableSoftwareFrame *frame)
{
    uint32_t ids[4];
    if(pack==NULL||flow==NULL||frame==NULL)return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    PortableFrontendMotion motion;
    if(!portable_frontend_motion_load(pack,&motion))return PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION;
    const unsigned count=tab_ids(flow,page,ids);
    if(count==0u)return PORTABLE_FRONTEND_PRESENTATION_OK;
    for(unsigned tab=0u;tab<count;++tab){
        const unsigned ticks=!flow->short_transitions&&tab+1u==count&&flow->motion_stage==1u&&flow->motion_tick<16u?flow->motion_tick:16u;
        int x=12,y=48+24*(int)tab;
        if(motion.bytes){const uint8_t *p=motion.bytes+144u+tab*4u;x=portable_menu_i16(p);y=portable_menu_i16(p+2);}
        const PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,ids[tab],
            frame,x-32+portable_menu_asr15((int64_t)32*portable_menu_sine(&motion,ticks*16u)),y,0,0,240,160);
        if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    }
    /* sub_80036E4/spm banner at (32, 144): the choosing player on the
     * character grid, otherwise player 1. */
    const unsigned player=page==PORTABLE_FRONTEND_PAGE_CHARACTER&&flow->players>1u?
        flow->character_player%4u:0u;
    int banner_x=32;
    if(flow->heading_configured&&!flow->short_transitions&&flow->motion_player_entry&&flow->motion_stage==1u){
        /* The first mode header and banner start after the player medallion's
         * 33-turn entry; its constructor submits the first callback. */
        const unsigned turn=flow->motion_tick>33u?flow->motion_tick-33u:0u;
        banner_x=portable_menu_banner_x(turn,flow->heading_sine);
    }
    PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,
        PORTABLE_MENU_PLAYER_BANNER+player,frame,banner_x,144,0,0,240,160);
    const int options=page==PORTABLE_FRONTEND_PAGE_MODE&&flow->mode_phase==0u&&
        ((flow->players==1u&&flow->mode_cursor==1u)||flow->mode_cursor==2u);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK||!options)return status;
    if(flow->quick_options_open)return option_window(pack,flow,frame);
    /* 4860 shows the hint only while the menu is idle. */
    if(flow->transition_ticks!=0u)return status;
    return portable_frontend_image_draw(pack,PORTABLE_MENU_SETTINGS_HINT,frame,28,124,0,0,240,160);
}
