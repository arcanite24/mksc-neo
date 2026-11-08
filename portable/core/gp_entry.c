#include "gp_entry.h"
#include "driver_vector_effect.h"
#include <limits.h>
#include <string.h>

static int32_t signed32(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-INT32_C(1)-(int32_t)(UINT32_MAX-bits);}
static int32_t source_product(int32_t value)
{
    const int64_t adjusted=(int64_t)value+(value<0?1:0);
    return (int32_t)(adjusted<0?(adjusted-65535)/65536:adjusted/65536);
}
static int camera(PortableGpEntry *e,const int32_t *sine)
{
    int32_t angle=INT32_C(0x7700);
    int32_t radius=208-(7-(int32_t)e->grid_rank)*16;
    if(e->camera_ticks>11u){
        unsigned step=e->camera_ticks-11u;if(step>256u)step=256u;
        const int32_t delta=32768-sine[(256u-2u*step)&1023u];
        angle+=source_product(-angle*delta);radius+=source_product(-radius*delta);
    }
    const uint16_t bits=(uint16_t)(angle+INT32_C(0x4000));
    const int32_t signed_angle=bits<=INT16_MAX?(int32_t)bits:(int32_t)bits-65536;
    const unsigned index=(unsigned)(signed_angle/64)&1023u;
    const PortableGpEntryPoint anchor=e->grid[e->selected];
    e->snapshot.camera_x_q16=signed32((uint32_t)anchor.x_q16+
        (uint32_t)((int64_t)sine[(256u-index)&1023u]*radius*2));
    e->snapshot.camera_y_q16=signed32((uint32_t)anchor.y_q16+
        (uint32_t)((int64_t)sine[index]*radius*2));
    int64_t dx=(int64_t)anchor.x_q16-e->snapshot.camera_x_q16;
    int64_t dy=(int64_t)anchor.y_q16-e->snapshot.camera_y_q16;
    while(dx< -1024||dx>1023||dy< -1024||dy>1023){dx/=2;dy/=2;}
    if(dx==0&&dy==0)e->snapshot.camera_yaw=0u;
    else if(portable_driver_vector_effect_source_angle((int16_t)dx,(int16_t)dy,
            &e->snapshot.camera_yaw)!=PORTABLE_DRIVER_VECTOR_EFFECT_OK)return 0;
    return 1;
}
int portable_gp_entry_init(PortableGpEntry *e,const PortableGpEntryPoint grid[8],
    uint8_t selected,uint8_t rank,uint8_t course)
{
    if(!e||!grid||selected>=8u||rank>=8u||course>=4u)return 0;
    memset(e,0,sizeof *e);memcpy(e->grid,grid,sizeof e->grid);
    memcpy(e->snapshot.drivers,grid,sizeof e->grid);
    e->selected=selected;e->grid_rank=rank;e->first_course=(uint8_t)(course==0u);
    e->snapshot.window_top=24u;e->snapshot.window_bottom=144u;
    e->snapshot.show_panel=1u;e->initialized=1u;
    if(e->first_course)e->snapshot.drivers[selected].y_q16=signed32(
        (uint32_t)grid[selected].y_q16+((30u+8u*rank)<<16u));
    else e->arrival_done=1u;
    return 1;
}
int portable_gp_entry_tick(PortableGpEntry *e,const int32_t *sine,size_t count)
{
    if(!e||!e->initialized||!sine||count<1024u||e->phase>10u)return -1;
    if(e->snapshot.done)return 1;
    ++e->ticks;
    if(!e->camera_done){
        if(e->camera_ticks<269u)++e->camera_ticks;
        if(e->camera_ticks==269u)e->camera_done=1u;
    }
    if(!camera(e,sine))return -1;
    if(!e->arrival_done){
        PortableGpEntryPoint *driver=&e->snapshot.drivers[e->selected];
        if(driver->y_q16==e->grid[e->selected].y_q16)e->arrival_done=1u;
        else driver->y_q16=signed32((uint32_t)driver->y_q16-UINT32_C(32768));
    }
    if(e->ticks>1u){unsigned phase=(e->ticks-2u)/3u;
        e->snapshot.veil_phase=(uint8_t)(phase>10u?11u:phase);}
    switch(e->phase){
    case 0:e->phase=1u;break;
    case 1:if(++e->phase_ticks>=31u){e->phase=2u;e->phase_ticks=0u;}break;
    case 2:
        if(++e->phase_ticks>32u){e->snapshot.panel_scroll=256u;e->phase=3u;e->phase_ticks=0u;}
        else e->snapshot.panel_scroll=(uint16_t)(((int64_t)sine[(e->phase_ticks*8u)&1023u]*256)/32768);
        break;
    case 3:
        if(e->first_course?(e->camera_done&&e->arrival_done):++e->phase_ticks>180u){e->phase=4u;e->phase_ticks=0u;}
        break;
    case 4:
        if(++e->phase_ticks>32u){e->snapshot.panel_scroll=512u;e->phase=5u;e->phase_ticks=0u;}
        else e->snapshot.panel_scroll=(uint16_t)(256+((int64_t)sine[(e->phase_ticks*8u)&1023u]*256)/32768);
        break;
    case 5:e->phase=6u;break;
    case 6:e->phase=7u;break; /* source reveal transition has already completed */
    case 7:e->snapshot.show_panel=0u;e->phase=8u;break;
    case 8:e->snapshot.show_hud=1u;e->phase=9u;break;
    case 9:
        if(++e->phase_ticks>10u){e->snapshot.hud_progress=10u;e->phase=10u;}
        else e->snapshot.hud_progress=(uint8_t)e->phase_ticks;
        break;
    case 10:e->snapshot.done=1u;break;
    default:return -1;
    }
    if(e->snapshot.show_hud){
        unsigned progress=e->snapshot.hud_progress;
        if(progress>8u)progress=8u;
        e->snapshot.window_top=(uint8_t)(24u-progress*3u);
        e->snapshot.window_bottom=(uint8_t)(144u+progress*2u);
    }
    return e->snapshot.done?1:0;
}
