#include "input_display.h"
#include "driver_input.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"input display failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)

enum { W=240, H=160 };

static uint32_t draw(uint32_t *pixels,uint16_t buttons,int16_t sx,int16_t sy)
{
    PortableSoftwareFrame frame;
    if(!portable_software_frame_init(&frame,pixels,W,H))return 0u;
    portable_software_frame_clear(&frame,UINT32_C(0xff000000));
    portable_input_display_render_corner(&frame,buttons,sx,sy);
    return portable_software_frame_checksum(&frame);
}

/* Only the bottom-left panel is touched. */
static int outside_untouched(const uint32_t *pixels)
{
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x){
        const int inside=x>=42u&&x<102u&&y>=H-25u&&y<H-3u;
        if(!inside&&pixels[y*W+x]!=UINT32_C(0xff000000))return 0;
    }
    return 1;
}

int main(void)
{
    static uint32_t a[W*H],b[W*H];
    const uint32_t idle=draw(a,0u,0,0);
    CHECK(idle!=0u&&outside_untouched(a));
    CHECK(a[(H-20u)*W+70u]!=UINT32_C(0xff000000)); /* panel is drawn */
    /* Each driving button lights its own key. */
    static const uint16_t buttons[]={PORTABLE_INPUT_ACCELERATE,PORTABLE_INPUT_BRAKE,
        PORTABLE_INPUT_HOP,PORTABLE_INPUT_ITEM,PORTABLE_INPUT_UP,PORTABLE_INPUT_DOWN};
    uint32_t seen[6];
    for(size_t i=0u;i<6u;++i){
        seen[i]=draw(b,buttons[i],0,0);
        CHECK(seen[i]!=idle&&outside_untouched(b));
        for(size_t j=0u;j<i;++j)CHECK(seen[j]!=seen[i]);
    }
    /* The stick knob follows the analog stick, or the D-pad without one. */
    const uint32_t left=draw(b,PORTABLE_INPUT_LEFT,0,0);
    CHECK(left!=idle&&draw(b,0u,-32767,0)==left);
    CHECK(draw(b,0u,32767,0)!=left&&draw(b,0u,0,32767)!=idle);
    CHECK(memcmp(a,b,sizeof a)!=0);
    portable_input_display_render(NULL,0,0,0u,0,0);
    puts("input display: PASS buttons and stick light distinct overlay keys in the HUD corner");
    return 0;
}
