#include "local_players.h"

#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"local players failed line %d: %s\n",__LINE__,#condition); \
    return 1; \
} } while (0)

int main(void)
{
    PortableLocalInput inputs[PORTABLE_LOCAL_PLAYERS_MAX];
    CHECK(portable_local_players_max(0u)==2u && portable_local_players_max(1u)==2u &&
          portable_local_players_max(3u)==3u && portable_local_players_max(6u)==4u);
    CHECK(!portable_local_players_assign(3u,1u,inputs) && !portable_local_players_assign(0u,4u,inputs));
    /* No pads: two keyboard layouts. */
    CHECK(portable_local_players_assign(2u,0u,inputs));
    CHECK(inputs[0].keyboard==1u && inputs[0].pad_slot<0 && inputs[1].keyboard==2u &&
          inputs[1].pad_slot<0 && inputs[2].keyboard==0u && inputs[2].pad_slot<0);
    /* Four pads: pads in connection order, keyboards still shared by P1/P2. */
    CHECK(portable_local_players_assign(4u,4u,inputs));
    for(unsigned p=0u;p<4u;++p)CHECK(inputs[p].pad_slot==(int)p);
    CHECK(inputs[2].keyboard==0u && inputs[3].keyboard==0u);
    const uint16_t pads[4]={0x10u,0x20u,0x40u,0x80u};
    uint16_t buttons[4];
    portable_local_players_buttons(inputs,4u,0x1u,0x2u,pads,buttons);
    CHECK(buttons[0]==0x11u && buttons[1]==0x22u && buttons[2]==0x40u && buttons[3]==0x80u);
    CHECK(portable_local_players_assign(3u,3u,inputs));
    portable_local_players_buttons(inputs,3u,0x1u,0x2u,pads,buttons);
    CHECK(buttons[2]==0x40u && buttons[3]==0u);
    puts("portable local players: PASS");
    return 0;
}
