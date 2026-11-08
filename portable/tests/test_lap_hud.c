#include "lap_hud.h"
#include <stdio.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"lap HUD failed line %d: %s\n",__LINE__,#x);return 1;} } while(0)

int main(void)
{
    PortableLapHud lap;
    portable_lap_hud_init(&lap);
    CHECK(lap.initialized&&lap.pending_lap==1u&&lap.displayed_lap==1u);
    /* A crossing changes C6 after this call. The following pre-motion call
     * queues2; only the next accepted display consumes that pending digit. */
    portable_lap_hud_step(&lap,0x8000u,0x8000u);
    portable_lap_hud_step(&lap,0x8100u,0x8000u);
    CHECK(lap.pending_lap==2u&&lap.displayed_lap==1u);
    portable_lap_hud_step(&lap,0x8100u,0x8000u);
    CHECK(lap.pending_lap==2u&&lap.displayed_lap==2u);
    CHECK(portable_lap_hud_digit(0x83ffu,0x20u)==3u);
    CHECK(portable_lap_hud_digit(0x83ffu,0u)==4u);
    CHECK(portable_lap_hud_digit(0x87ffu,0u)==1u); /* maskedzero maps1 */
    CHECK(portable_lap_hud_digit(0x7e00u,0u)==7u); /* halfword subtraction */
    /* Backtracking changes current checkpoint, but C6 is retained max. */
    portable_lap_hud_step(&lap,0x8100u,0u);
    CHECK(lap.displayed_lap==2u&&lap.pending_lap==2u);
    portable_lap_hud_init(&lap);
    CHECK(lap.pending_lap==1u&&lap.displayed_lap==1u);
    puts("lap HUD: PASS source progress and pending display chronology");
    return 0;
}
