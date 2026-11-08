#include "gp_countdown_renderer.h"
#include "frontend_image_renderer.h"

int portable_gp_countdown_render(const PortableAssetPackView *pack,
    PortableRacePhase phase,uint16_t duration,uint16_t remaining,
    uint32_t race_ticks,PortableSoftwareFrame *frame)
{
    if(pack==NULL||frame==NULL||frame->pixels==NULL)return 0;
    uint32_t picture;
    int y=46;
    if(phase==PORTABLE_RACE_PHASE_COUNTDOWN){
        if(duration==0u||remaining>duration)return 0;
        /* Original start script: ten-tick flashes, sixty ticks per signal.
         * Entrance/exit motion is native presentation, independent of the
         * old course-opening frame number and captured actor positions. */
        if(remaining>120u)picture=0u;
        else if(remaining>60u)picture=remaining>110u?1u:2u;
        else picture=remaining>50u?3u:4u;
        const uint32_t entrance=duration-remaining;
        if(entrance<30u)y=-14+(int)entrance*2;
    }else if(phase==PORTABLE_RACE_PHASE_RACING&&race_ticks<80u){
        picture=race_ticks<10u?5u:6u;
        if(race_ticks>60u)y-=(int)(race_ticks-60u)*4;
    }else return 1;
    return portable_frontend_image_draw(pack,UINT32_C(0x27000)+picture,
        frame,164,y,0,0,(int)frame->width,(int)frame->height)==
        PORTABLE_FRONTEND_PRESENTATION_OK;
}
