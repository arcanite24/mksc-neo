#include "gp_turn_cue.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"cue check line%d: %s\n",\
    __LINE__,#x);return 1;}}while(0)

int main(void)
{
    /* Course5's first entry. Paired original1927 starts its48-turn cue
     * from prior route7;1928 displays that map. Synthetic maps isolate
     * latch/flip/clear semantics without embedding glyph assets. */
    const uint8_t entry[4]={7u,12u,128u,1u};
    uint8_t maps[84]={0};
    for(unsigned map=0u;map<3u;++map){
        maps[map*28u]=4u;maps[map*28u+2u]=3u;
        for(unsigned i=0u;i<12u;++i){
            const uint16_t cell=map==2u?0xc000u:
                (uint16_t)((map==1u?0xc400u:0xc000u)+0x64u+
                    (map==1u?(i/4u)*4u+3u-i%4u:i));
            maps[map*28u+4u+i*2u]=(uint8_t)cell;
            maps[map*28u+5u+i*2u]=(uint8_t)(cell>>8u);
        }
    }
    PortableGpTurnCue cue={0};
    CHECK(portable_gp_turn_cue_step(&cue,entry,1u,maps,6u,1,0x8000u,1u,53u,3u));
    CHECK(cue.previous==6u&&!cue.active&&cue.pending_cells[0]==0u);
    for(unsigned tick=1927u;tick<=1975u;++tick){
        CHECK(portable_gp_turn_cue_step(&cue,entry,1u,maps,7u,1,0x8000u,1u,53u,3u));
        if(tick==1927u)CHECK(cue.active&&cue.elapsed==1u&&cue.blink==1u&&
            cue.pending_cells[0]==0xc467u&&cue.displayed_cells[0]==0u);
        if(tick==1928u)CHECK(cue.elapsed==2u&&cue.displayed_cells[0]==0xc467u);
        if(tick==1950u)CHECK(cue.elapsed==24u&&cue.cycles==3u&&
            cue.blink==0u&&!cue.hidden&&cue.pending_cells[0]==0xc467u);
        if(tick==1975u)CHECK(!cue.active&&cue.elapsed==49u&&
            cue.pending_cells[0]==0xc000u&&cue.displayed_cells[0]==0xc467u);
    }
    CHECK(portable_gp_turn_cue_step(&cue,entry,1u,maps,7u,1,0x8000u,1u,53u,3u));
    CHECK(cue.displayed_cells[0]==0xc000u&&cue.elapsed==49u);
    /* Flag420 eligibility is part of the source trigger, not rendering. */
    memset(&cue,0,sizeof cue);
    CHECK(portable_gp_turn_cue_step(&cue,entry,1u,maps,7u,1,0x420u,1u,53u,3u));
    CHECK(!cue.active&&cue.previous==7u);
    CHECK(!portable_gp_turn_cue_step(&cue,entry,0u,maps,7u,1,0u,1u,53u,3u));
    CHECK(!portable_gp_turn_cue_step(NULL,entry,1u,maps,7u,1,0u,1u,53u,3u));
    puts("GP turn cue: PASS source trigger/display latch, blink freeze and expiry");
    return 0;
}
