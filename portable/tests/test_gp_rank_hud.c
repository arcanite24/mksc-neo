#include "gp_rank_hud.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"rank HUD failed at %d: %s\n",__LINE__,#x);return 1;} } while(0)
static void put16(uint8_t *p, uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);}
int main(void)
{
    uint8_t stable[520]={0},transition[416]={0},pages[8]={0,1,2,3,4,5,6,7};
    uint8_t palette[64]={0},flash[16]={0};
    uint8_t classes[4]={0,1,2,3};
    for(unsigned r=0;r<5u;++r)put16(stable+r*104u,(uint16_t)(100u+r));
    for(unsigned r=0;r<4u;++r)put16(transition+r*104u,(uint16_t)(200u+r));
    for(unsigned r=0;r<8u;++r)put16(palette+r*8u,(uint16_t)(300u+r));
    put16(flash,400u);put16(flash+8u,500u);
    PortableGpRankHud s;
    CHECK(portable_gp_rank_hud_init(&s,stable,pages,palette,0u,classes));
    CHECK(s.pending_cells[0]==100u&&s.pending_cells[5]==0xd080u);
    classes[0]=1u;classes[1]=0u;
    /* Original zero+1706: preceding portraits/rank remain displayed,
     * changed portraits survive F6F4's zero cells; delay5, timer31. */
    CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,1u,classes,88u,7u));
    CHECK(s.displayed_cells[5]==0xd080u&&s.pending_cells[5]==0xd084u);
    CHECK(s.displayed_rank==0u&&s.pending_rank==1u);
    CHECK(s.displayed_cells[0]==100u&&s.pending_cells[0]==201u);
    CHECK(s.badge_delay==5&&s.color_timer==31);
    CHECK(s.displayed_colors[0]==300u&&s.pending_colors[0]==400u);
    for(unsigned i=0;i<5u;++i)
        CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,1u,classes,90u+i,7u));
    CHECK(s.badge_delay==0&&s.pending_cells[0]==201u);
    CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,1u,classes,100u,7u));
    CHECK(s.badge_delay==-1&&s.pending_cells[0]==101u&&s.displayed_cells[0]==201u);
    for(unsigned i=0;i<25u;++i)
        CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,1u,classes,110u+i,7u));
    CHECK(s.color_timer==0&&s.pending_colors[0]==301u);
    CHECK(s.displayed_colors[0]!=301u);
    CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,1u,classes,140u,7u));
    CHECK(s.displayed_colors[0]==301u&&s.displayed_cells[0]==101u);
    CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,2u,classes,150u,0u));
    CHECK(s.badge_delay==6); /* animation-disabled delay holds */
    CHECK(portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,3u,classes,0u,7u));
    CHECK(s.badge_delay==-1&&s.pending_cells[0]==103u);
    PortableGpRankHud before=s;
    CHECK(!portable_gp_rank_hud_step(&s,stable,transition,pages,palette,flash,8u,classes,0u,7u));
    CHECK(memcmp(&s,&before,sizeof s)==0);
    const uint8_t separators[8]={1u,0u,2u,0u,3u,0u,4u,0u};
    s.pending_separator_colors[0]=9u;s.pending_separator_colors[1]=8u;
    portable_gp_rank_hud_separator_step(&s,4u,separators);
    CHECK(s.displayed_separator_colors[0]==9u&&s.pending_separator_colors[0]==1u);
    portable_gp_rank_hud_separator_step(&s,5u,separators);
    CHECK(s.displayed_separator_colors[0]==1u&&s.pending_separator_colors[0]==1u);
    portable_gp_rank_hud_separator_step(&s,8u,separators);
    CHECK(s.displayed_separator_colors[0]==1u&&s.pending_separator_colors[0]==3u);
    puts("GP rank HUD: PASS source delay/zero-overlay/pre-motion display/color restore");
    return 0;
}
