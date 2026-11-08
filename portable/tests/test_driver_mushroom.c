#include "item_rules_fixture.h"
#include <stdint.h>
#include <stdio.h>

#include "driver_mushroom.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, \
            #condition); return 1; } } while (0)

int main(void)
{
    CHECK(portable_driver_mushroom_begin(NULL,NULL,NULL,NULL)==PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT);
    uint8_t mode = 0u, started = 0u;
    uint16_t timer = 0u;
    uint16_t flags = UINT16_C(0x0041);
    int32_t speed = INT32_C(0x00b90000);
    int32_t acceleration = INT32_C(0x00001234);

    CHECK(portable_driver_mushroom_begin(&mode, &timer, &flags,test_mushroom_rules()) ==
          PORTABLE_DRIVER_MUSHROOM_OK);
    CHECK(mode == 8u && timer == test_mushroom_rules()->boost_ticks &&
          flags == UINT16_C(0x00c1));
    CHECK(portable_driver_mushroom_tick(
              &mode, &timer, &flags, &speed, &acceleration,test_mushroom_rules()) ==
          PORTABLE_DRIVER_MUSHROOM_OK);
    CHECK(mode == 8u && timer == test_mushroom_rules()->boost_ticks-1u &&
          (uint32_t)acceleration == (((uint32_t)test_mushroom_rules()->acceleration_units<<16u)|0x1234u));

    speed = (int32_t)test_mushroom_rules()->max_speed_q16+1;
    acceleration = INT32_C(0x12345678);
    CHECK(portable_driver_mushroom_tick(
              &mode, &timer, &flags, &speed, &acceleration,test_mushroom_rules()) ==
          PORTABLE_DRIVER_MUSHROOM_OK);
    CHECK(speed == (int32_t)test_mushroom_rules()->max_speed_q16 &&
          acceleration == INT32_C(0x00005678));

    while (mode == 8u)
        CHECK(portable_driver_mushroom_tick(
                  &mode, &timer, &flags, &speed, &acceleration,test_mushroom_rules()) ==
              PORTABLE_DRIVER_MUSHROOM_OK);
    CHECK(timer == 0u && mode == 0u && flags == UINT16_C(0x0001));
    /* Every source response gate, both material variants, and exclusions. */
    for (unsigned status = 0; status < 17u; ++status)
    {
        uint8_t response = (uint8_t)status;
        mode = 0u; timer = 7u; flags = UINT16_C(0x0021);
        CHECK(portable_driver_surface_boost(0u, 6u, &mode, &timer,
                  &flags, &response, &started) == PORTABLE_DRIVER_MUSHROOM_OK);
        if (status < 16u && ((UINT16_C(0xde1f) >> status) & 1u))
            CHECK(started && mode == 8u && timer == 34u && flags == 0x80u && response == 14u);
        else
            CHECK(!started && mode == 0u && timer == 7u && flags == 0x21u && response == status);
    }
    {
        uint8_t response = 0u;
        mode = 0u; timer = 0u; flags = 0x20u;
        CHECK(portable_driver_surface_boost(1u, 6u, &mode, &timer,
                  &flags, &response, &started) == 0 && mode == 0u);
        CHECK(portable_driver_surface_boost(0u, 0x26u, &mode, &timer,
                  &flags, &response, &started) == 0 && mode == 0u);
        CHECK(portable_driver_surface_boost(0u, 0x16u, &mode, &timer,
                  &flags, &response, &started) == 0 && mode == 8u && flags == 0xa0u);
        speed = 0x03000000; acceleration = 0x1234;
        CHECK(portable_driver_mushroom_tick(&mode, &timer, &flags,
                  &speed, &acceleration,test_mushroom_rules()) == 0 && timer == 33u &&
                  (uint32_t)acceleration == (((uint32_t)test_mushroom_rules()->acceleration_units<<16u)|0x1234u));
        CHECK(portable_driver_surface_boost(0u, 6u, &mode, &timer,
                  &flags, &response, &started) == 0 && timer == 34u);
        for (unsigned tick = 0; tick < 34u; ++tick)
            CHECK(portable_driver_mushroom_tick(&mode, &timer, &flags,
                      &speed, &acceleration,test_mushroom_rules()) == 0);
        CHECK(mode == 0u && timer == 0u && flags == 0x20u);
    }
    /* A9AC retains queued requests through every blocked status, damage
     * priority and air; accepted boosts clear drift and reset impact angle. */
    for (unsigned status=0u;status<17u;++status) {
        uint8_t response=(uint8_t)status;
        uint16_t request=0x8001u;
        int16_t angle=-1234;
        mode=0u;timer=7u;flags=0x21u;
        CHECK(portable_driver_mushroom_consume(0u,&request,&mode,&timer,
            &flags,&response,&angle,test_mushroom_rules())==0);
        if(status<16u && ((UINT16_C(0xde1f)>>status)&1u))
            CHECK(request==1u && mode==8u && timer==test_mushroom_rules()->boost_ticks && flags==0x80u && response==14u && angle==0);
        else
            CHECK(request==0x8001u && mode==0u && timer==7u && flags==0x21u && angle==-1234);
    }
    for (unsigned gate=0u;gate<4u;++gate) {
        uint8_t response=0u;
        uint16_t request=0x8000u;
        int16_t angle=1234;
        mode=0u;timer=7u;flags=gate==1u?0x200u:gate==2u?0x1000u:gate==3u?0x800u:0u;
        CHECK(portable_driver_mushroom_consume(gate==0u?1u:0u,&request,&mode,&timer,
            &flags,&response,&angle,test_mushroom_rules())==0);
        CHECK(request==0x8000u && mode==0u && timer==7u && angle==1234);
        flags=0u;
        CHECK(portable_driver_mushroom_consume(0u,&request,&mode,&timer,
            &flags,&response,&angle,test_mushroom_rules())==0 && request==0u && mode==8u && timer==test_mushroom_rules()->boost_ticks);
    }
    /* Charged boost uses the target-relative cap and preserves C0 fractions. */
    mode=12u;timer=48u;flags=0x00c1u;speed=0x03000000;acceleration=0x12345678;
    CHECK(portable_driver_charged_boost_tick(&mode,&timer,&flags,0x03000000,
              &speed,&acceleration)==0 && timer==47u && acceleration==0x00035678);
    speed=0x03dfffff;
    CHECK(portable_driver_charged_boost_tick(&mode,&timer,&flags,0x03000000,
              &speed,&acceleration)==0 && timer==46u && speed==0x03dfffff &&
              acceleration==0x00035678);
    speed=0x03e00000;
    CHECK(portable_driver_charged_boost_tick(&mode,&timer,&flags,0x03000000,
              &speed,&acceleration)==0 && timer==45u && speed==0x03c00000 &&
              acceleration==0x5678);
    timer=1u;acceleration=0xfffd5678;
    CHECK(portable_driver_charged_boost_tick(&mode,&timer,&flags,0x03000000,
              &speed,&acceleration)==0 && timer==0u && mode==0u && flags==1u &&
              (uint32_t)acceleration==0xfffd5678u);
    mode=12u;timer=0u;speed=0x03000000;
    CHECK(portable_driver_charged_boost_tick(&mode,&timer,&flags,0x03000000,
              &speed,&acceleration)==0 && timer==65535u && mode==12u &&
              acceleration==0x00035678);
    {
        PortableMushroomRules rules={3u,2u,4u,7u,1000u*65536u};
        mode=0u;timer=0u;flags=0u;speed=999*65536;acceleration=0x1234;
        CHECK(portable_driver_mushroom_begin(&mode,&timer,&flags,&rules)==0&&timer==3u);
        CHECK(portable_driver_mushroom_tick(&mode,&timer,&flags,&speed,&acceleration,&rules)==0&&timer==2u&&acceleration==0x71234);
        speed=1001*65536;
        CHECK(portable_driver_mushroom_tick(&mode,&timer,&flags,&speed,&acceleration,&rules)==0&&speed==1000*65536&&timer==1u&&acceleration==0x1234);
        CHECK(portable_driver_mushroom_tick(&mode,&timer,&flags,&speed,&acceleration,&rules)==0&&mode==0u&&timer==0u);
        rules.boost_ticks=65535u;
        CHECK(portable_driver_mushroom_begin(&mode,&timer,&flags,&rules)==0&&timer==65535u);
        CHECK(portable_driver_mushroom_tick(&mode,&timer,&flags,&speed,&acceleration,&rules)==0&&timer==65534u);
        timer=0u;
        CHECK(portable_driver_mushroom_tick(&mode,&timer,&flags,&speed,&acceleration,&rules)==0&&timer==65535u);
        rules.boost_ticks=0u;
        CHECK(portable_driver_mushroom_begin(&mode,&timer,&flags,&rules)==PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT&&timer==65535u);
    }
    puts("portable driver mushroom: PASS");
    return 0;
}
