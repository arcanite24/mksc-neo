#include "input_display.h"
#include "driver_input.h"

enum { PANEL_W=60, PANEL_H=22, PAD=15 };

static void box(PortableSoftwareFrame *view,int x,int y,int w,int h,uint32_t color)
{
    if(x<0||y<0)return;
    portable_software_frame_rect(view,(size_t)x,(size_t)y,(size_t)w,(size_t)h,color);
}

static void key(PortableSoftwareFrame *view,int x,int y,const char *label,int pressed,uint32_t lit)
{
    box(view,x,y,9,9,pressed?lit:UINT32_C(0xff303848));
    portable_software_frame_text(view,x+3,y+2,label,pressed?UINT32_C(0xff101010):UINT32_C(0xffa0a8b8));
}

/* Map a signed axis to the 13-pixel travel inside the stick well. */
static int travel(int16_t axis)
{
    return ((int)axis+32768)*12/65535;
}

void portable_input_display_render(PortableSoftwareFrame *view,int x,int y,
    uint16_t buttons,int16_t stick_x,int16_t stick_y)
{
    if(view==NULL)return;
    if(stick_x==0&&stick_y==0){
        stick_x=(int16_t)((buttons&PORTABLE_INPUT_LEFT)?-32767:(buttons&PORTABLE_INPUT_RIGHT)?32767:0);
        stick_y=(int16_t)((buttons&PORTABLE_INPUT_UP)?-32767:(buttons&PORTABLE_INPUT_DOWN)?32767:0);
    }
    box(view,x,y,PANEL_W,PANEL_H,UINT32_C(0xff101828));
    box(view,x+3,y+3,PAD+1,PAD+1,UINT32_C(0xff303848));
    box(view,x+3+PAD/2,y+3,1,PAD+1,UINT32_C(0xff505a6c));
    box(view,x+3,y+3+PAD/2,PAD+1,1,UINT32_C(0xff505a6c));
    box(view,x+3+travel(stick_x),y+3+travel(stick_y),4,4,UINT32_C(0xffffdd00));
    key(view,x+22,y+2,"A",(buttons&PORTABLE_INPUT_ACCELERATE)!=0u,UINT32_C(0xff60e070));
    key(view,x+22,y+11,"B",(buttons&PORTABLE_INPUT_BRAKE)!=0u,UINT32_C(0xffff6060));
    key(view,x+33,y+2,"R",(buttons&PORTABLE_INPUT_HOP)!=0u,UINT32_C(0xff60b0ff));
    key(view,x+33,y+11,"L",(buttons&PORTABLE_INPUT_ITEM)!=0u,UINT32_C(0xffffb040));
    key(view,x+46,y+2,"^",(buttons&PORTABLE_INPUT_UP)!=0u,UINT32_C(0xffffffff));
    key(view,x+46,y+11,"V",(buttons&PORTABLE_INPUT_DOWN)!=0u,UINT32_C(0xffffffff));
}

void portable_input_display_render_corner(PortableSoftwareFrame *frame,
    uint16_t buttons,int16_t stick_x,int16_t stick_y)
{
    if(frame==NULL)return;
    PortableSoftwareFrame view=portable_software_frame_native_view(frame,PORTABLE_FRAME_LEFT);
    portable_input_display_render(&view,42,(int)view.height-PANEL_H-3,buttons,stick_x,stick_y);
}
