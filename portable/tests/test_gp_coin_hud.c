#include "gp_coin_hud.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"coin HUD failed line %d: %s\n",__LINE__,#x);return 1;} } while(0)
#include <string.h>

static const uint8_t colors[18]={255,255,255,255,255,255,
    0x7f,0x31,0x1b,8,0x55,4,255,255,0xd2,255,0x6e,0xfb};
int main(void)
{
    PortableGpCoinHud s={0};
    CHECK(!portable_gp_coin_hud_step(&s,1u,2u,0u,3u,colors));
    CHECK(portable_gp_coin_hud_init(&s,30u,2u,colors));
    CHECK(s.displayed_cells[2]==0xb012u&&s.displayed_colors[1]==0xffd2u);
    CHECK(portable_gp_coin_hud_step(&s,31u,0u,0u,3u,colors));
    CHECK(s.displayed_cells[2]==0xb012u&&s.pending_cells[2]==0xb010u);
    CHECK(s.pending_colors[1]==0xffffu&&!s.warning);
    CHECK(portable_gp_coin_hud_step(&s,32u,0u,0u,3u,colors));
    CHECK(s.displayed_cells[2]==0xb010u&&s.displayed_colors[1]==0xffffu);
    CHECK(s.pending_colors[1]==0x081bu&&s.warning);
    PortableGpCoinHud before=s;
    CHECK(!portable_gp_coin_hud_step(&s,32u,0u,0u,3u,colors));
    CHECK(!portable_gp_coin_hud_step(&s,34u,0u,0u,3u,colors));
    CHECK(!portable_gp_coin_hud_step(&s,33u,100u,0u,3u,colors));
    CHECK(memcmp(&before,&s,sizeof s)==0);
    CHECK(portable_gp_coin_hud_step(&s,33u,99u,0u,3u,colors));
    CHECK(s.pending_cells[1]==0xb019u&&s.pending_cells[2]==0xb019u);
    CHECK(s.pending_colors[1]==0xffd2u&&!s.warning);
    CHECK(portable_gp_coin_hud_step(&s,34u,0u,0x20u,3u,colors));
    CHECK(s.pending_colors[1]==0xffd2u&&!s.warning);
    for(unsigned clock=0;clock<128u;++clock){
        CHECK(portable_gp_coin_hud_warning(clock,0u,0u,3u)==(clock%32u==0u));
        CHECK(portable_gp_coin_hud_warning(clock,1u,0u,3u)==(clock%64u==0u));
        CHECK(!portable_gp_coin_hud_warning(clock,2u,0u,3u));
        CHECK(!portable_gp_coin_hud_warning(clock,0u,0x20u,3u));
        CHECK(!portable_gp_coin_hud_warning(clock,0u,0u,2u));
    }
    CHECK(portable_gp_coin_hud_init(&s,UINT32_MAX,2u,colors));
    CHECK(portable_gp_coin_hud_step(&s,0u,1u,0u,3u,colors)&&s.warning);
    return 0;
}
