#include "gp_entry.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"GP entry failed %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)sine[i]=(int32_t)(32768.0*sin((double)i*6.283185307179586/1024.0));
    PortableGpEntryPoint grid[8];
    for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpEntryPoint){(868+(int32_t)(i&1u)*40)*65536,(620+(int32_t)i*24)*65536};
    PortableGpEntry entry;
    CHECK(!portable_gp_entry_init(&entry,grid,8u,7u,0u));
    CHECK(portable_gp_entry_init(&entry,grid,7u,7u,0u));
    CHECK(entry.snapshot.drivers[7].y_q16==(788+86)*65536);
    for(unsigned tick=0u;tick<159u;++tick)CHECK(portable_gp_entry_tick(&entry,sine,1024u)==0);
    /* Ordinary original Peach GP capture at source clock159: gridrank7,
     * angle target2D0B/radius4E, camera3465434/3371150, arrivingY794.5.
     * The analytic Q15 sine fixture is not an extracted ROM table. */
    CHECK(entry.snapshot.camera_x_q16==INT32_C(0x03465434));
    CHECK(entry.snapshot.camera_y_q16==INT32_C(0x03371150));
    CHECK(entry.snapshot.drivers[7].y_q16==INT32_C(0x031a8000));
    CHECK(entry.snapshot.panel_scroll==256u&&!entry.camera_done&&!entry.arrival_done);
    const PortableGpEntrySnapshot frozen=entry.snapshot;
    while(!entry.snapshot.done)CHECK(portable_gp_entry_tick(&entry,sine,1024u)>=0);
    CHECK(entry.ticks==318u&&entry.snapshot.hud_progress==10u&&
        entry.snapshot.window_top==0u&&entry.snapshot.window_bottom==160u);
    CHECK(entry.snapshot.camera_x_q16==grid[7].x_q16&&entry.snapshot.camera_y_q16==grid[7].y_q16&&
        entry.snapshot.camera_yaw==0u&&entry.snapshot.drivers[7].y_q16==grid[7].y_q16);
    CHECK(frozen.drivers[7].y_q16==INT32_C(0x031a8000));
    const PortableGpEntry before=entry;
    CHECK(portable_gp_entry_tick(&entry,sine,1024u)==1&&memcmp(&entry,&before,sizeof entry)==0);
    for(uint8_t course=1u;course<4u;++course){
        CHECK(portable_gp_entry_init(&entry,grid,2u,2u,course));
        CHECK(entry.arrival_done&&entry.snapshot.drivers[2].y_q16==grid[2].y_q16);
        while(entry.phase!=3u)CHECK(portable_gp_entry_tick(&entry,sine,1024u)==0);
        for(unsigned hold=0u;hold<180u;++hold)CHECK(portable_gp_entry_tick(&entry,sine,1024u)==0&&entry.phase==3u);
        CHECK(portable_gp_entry_tick(&entry,sine,1024u)==0&&entry.phase==4u);
        while(!entry.snapshot.done)CHECK(portable_gp_entry_tick(&entry,sine,1024u)>=0);
        CHECK(entry.ticks==295u);
    }
    CHECK(portable_gp_entry_tick(NULL,sine,1024u)==-1);
    CHECK(portable_gp_entry_init(&entry,grid,0u,0u,0u));
    CHECK(portable_gp_entry_tick(&entry,sine,1023u)==-1);
    puts("portable GP entry: PASS");return 0;
}
