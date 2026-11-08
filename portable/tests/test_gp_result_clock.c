#include "gp_result_clock.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "GP result clock check failed at line %d\n", __LINE__); \
    return 1; } } while (0)

int main(void)
{
    uint8_t steps[60];
    for(unsigned i=0u;i<60u;++i)steps[i]=(uint8_t)(i%3u==0u?1u:2u);
    uint16_t times[4] = {0};
    PortableGpSourceClock source_clock;

    /* Continuous original GP grid: sub_804F078 enables the timer on the
     * first racing frame, after that frame's sub_804D8F8 call. */
    CHECK(portable_gp_source_clock_init(&source_clock,3u));
    for (uint32_t tick = 1u; tick <= 5207u; ++tick)
    {
        CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps));
        if (tick == 1u)
            CHECK(portable_gp_source_clock_enable(&source_clock));
        if (tick == 1948u || tick == 3664u || tick == 5207u)
            CHECK(portable_gp_source_clock_record_lap(&source_clock,
                (uint8_t)(tick == 1948u ? 0u : tick == 3664u ? 1u : 2u)));
    }
    CHECK(source_clock.phase == 46u && source_clock.centiseconds == 8676u);
    CHECK(portable_gp_source_clock_times(&source_clock, times));
    CHECK(times[0] == 3245u && times[1] == 2860u &&
          times[2] == 2571u && times[3] == 8676u);
    CHECK(!portable_gp_source_clock_record_lap(&source_clock, 2u));
    CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps) &&
          portable_gp_source_clock_times(&source_clock, times) &&
          times[3] == 8676u);
    /* A retained route may hand off an already-running source timer. One
     * earlier increment plus one per native racing tick yields the separate
     * eighth-place reference without selecting by rank or finish value. */
    CHECK(portable_gp_source_clock_init(&source_clock,3u) &&
          portable_gp_source_clock_enable(&source_clock) &&
          portable_gp_source_clock_tick(&source_clock,steps,sizeof steps));
    for (uint32_t tick = 1u; tick <= 8517u; ++tick)
    {
        CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps));
        if (tick == 3831u || tick == 5984u || tick == 8517u)
            CHECK(portable_gp_source_clock_record_lap(&source_clock,
                (uint8_t)(tick == 3831u ? 0u : tick == 5984u ? 1u : 2u)));
    }
    CHECK(portable_gp_source_clock_times(&source_clock, times));
    CHECK(times[0] == 6386u && times[1] == 3589u &&
          times[2] == 4221u && times[3] == 14196u);
    uint16_t five[6]={0};
    CHECK(portable_gp_source_clock_init(&source_clock,5u)&&
          portable_gp_source_clock_enable(&source_clock));
    for(uint8_t lap=0u;lap<5u;++lap){
        for(unsigned tick=0u;tick<60u;++tick)
            CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps));
        CHECK(portable_gp_source_clock_record_lap(&source_clock,lap));
        if(lap<4u)CHECK(!portable_gp_source_clock_times(&source_clock,five));
    }
    CHECK(portable_gp_source_clock_times(&source_clock,five)&&five[5]==500u);
    for(unsigned lap=0u;lap<5u;++lap)CHECK(five[lap]==100u);
    CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps)&&
          portable_gp_source_clock_times(&source_clock,five)&&five[5]==500u);
    CHECK(!portable_gp_source_clock_record_lap(&source_clock,5u));
    CHECK(portable_gp_source_clock_init(&source_clock,0u));
    CHECK(portable_gp_source_clock_enable(&source_clock));
    for (unsigned tick=0u;tick<60u;++tick) CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps));
    CHECK(source_clock.centiseconds==100u && source_clock.lap_count==0u);
    CHECK(!portable_gp_source_clock_record_lap(&source_clock,0u));
    CHECK(!portable_gp_source_clock_times(&source_clock,times));
    CHECK(!portable_gp_source_clock_init(&source_clock,6u));
    CHECK(!portable_gp_source_clock_init(NULL,3u));
    CHECK(!portable_gp_source_clock_times(NULL, times));
    /* A nonperiodic authored row proves every phase reads owned content.
     * Missing/short owners reject without advancing an enabled clock. */
    CHECK(portable_gp_source_clock_init(&source_clock,0u));
    CHECK(portable_gp_source_clock_tick(&source_clock,NULL,0u));
    CHECK(portable_gp_source_clock_enable(&source_clock));
    CHECK(!portable_gp_source_clock_tick(&source_clock,NULL,60u));
    CHECK(!portable_gp_source_clock_tick(&source_clock,steps,59u));
    CHECK(source_clock.phase==0u && source_clock.centiseconds==0u);
    steps[59]=7u;source_clock.phase=59u;source_clock.centiseconds=UINT32_MAX-3u;
    CHECK(portable_gp_source_clock_tick(&source_clock,steps,sizeof steps));
    CHECK(source_clock.phase==0u && source_clock.centiseconds==3u && steps[59]==7u);
    puts("portable GP result clock: PASS");
    return 0;
}
