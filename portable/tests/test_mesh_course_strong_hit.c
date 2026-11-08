#include "mesh_course_strong_hit.h"
#include "driver_steering.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"strong hit line %d: %s\n",__LINE__,#c); return 1; } } while (0)

static int clocks(void)
{
    for (unsigned direction=0;direction<2;++direction)
        for (unsigned longer=0;longer<2;++longer)
        {
            PortableMeshStrongHitState state={0};
            const uint16_t initial=longer?0x2000:0x1000;
            CHECK(portable_mesh_course_strong_hit_admit(&state,initial,(uint8_t)direction));
            const PortableMeshStrongHitState pending=state;
            for(unsigned turn=0;turn<20;++turn)
            {
                int held=77;
                CHECK(portable_mesh_course_strong_hit_tick(&state,0,0,&held));
                CHECK(!held && !memcmp(&state,&pending,sizeof state));
            }
            PortableDriverSteeringState source={.response_flags=(uint16_t)(0x0200|(direction?0x0100:0))};
            uint16_t rate=initial;
            CHECK(portable_driver_queued_hit(&source)==PORTABLE_DRIVER_STEERING_OK);
            const unsigned duration=longer?128u:64u;
            for(unsigned turn=0;turn<duration;++turn)
            {
                int held=0;
                CHECK(portable_mesh_course_strong_hit_tick(&state,turn==0,0,&held));
                CHECK(portable_driver_strong_spin_tick(&source,&rate)==PORTABLE_DRIVER_STEERING_OK);
                CHECK(held && !state.pending && state.rate==rate && state.angle==source.position_82 &&
                    state.active==(source.response_status==13) && portable_mesh_course_strong_hit_valid(&state));
                CHECK(state.active==(turn+1<duration));
                const PortableMeshStrongHitState snapshot=state;
                for(unsigned view=0;view<8;++view)CHECK(portable_mesh_course_strong_hit_valid(&state));
                CHECK(!memcmp(&snapshot,&state,sizeof state));
            }
            CHECK(state.rate==0xc0 && !state.angle && !state.active);
            const PortableMeshStrongHitState complete=state;
            int held=1;
            CHECK(portable_mesh_course_strong_hit_tick(&state,1,0,&held) && !held &&
                !memcmp(&state,&complete,sizeof state));
        }
    return 0;
}

static int readmit_and_recovery(void)
{
    PortableMeshStrongHitState state={0};
    CHECK(portable_mesh_course_strong_hit_admit(&state,0x2000,0));
    int held;
    for(unsigned i=0;i<7;++i)CHECK(portable_mesh_course_strong_hit_tick(&state,1,0,&held) && held);
    const uint16_t angle=state.angle;
    CHECK(angle && portable_mesh_course_strong_hit_admit(&state,0x1000,1));
    CHECK(state.angle==angle && state.pending && !state.active && state.rate==0x1000);
    CHECK(portable_mesh_course_strong_hit_tick(&state,0,0,&held) && !held && state.angle==angle);
    CHECK(portable_mesh_course_strong_hit_tick(&state,1,0,&held) && held && state.active);
    CHECK(portable_mesh_course_strong_hit_tick(&state,0,1,&held) && !held);
    const PortableMeshStrongHitState zero={0};
    CHECK(!memcmp(&state,&zero,sizeof state));
    CHECK(portable_mesh_course_strong_hit_admit(&state,0x1000,0));
    CHECK(portable_mesh_course_strong_hit_tick(&state,0,1,&held) && !held &&
        !memcmp(&state,&zero,sizeof state));
    return 0;
}

static int hold_and_rollback(void)
{
    PortableMeshKartState kart = {.velocity_q16 = {3 * 65536, -5 * 65536, 7 * 65536},
                                  .speed_q16 = 4 * 65536,
                                  .forward_q30 = {1073741824, 0, 0},
                                  .up_q30 = {0, 0, 1073741824},
                                  .drift_charge = 40,
                                  .boost_ticks = {3, 4, 5},
                                  .panel_ticks = 10,
                                  .drift_direction = -1,
                                  .drift_tier = 2,
                                  .last_boost_tier = 2,
                                  .outside_drift_q16 = 100};
    const PortableMeshKartState before = kart;
    PortableMeshKartEffects effects = {.speed_ratio_q16 = 50000,
                                       .body_scale_q16 = 40000,
                                       .boost_cap_ratio_q16 = 100000,
                                       .boost_acceleration_q16 = 8 * 65536,
                                       .boost_active = 1};
    CHECK(portable_mesh_course_strong_hit_hold(&kart, &effects) == PORTABLE_MESH_OK);
    CHECK(!memcmp(kart.velocity_q16, before.velocity_q16, sizeof kart.velocity_q16) &&
          kart.speed_q16 == 3 * 65536);
    CHECK(!kart.drift_charge && !kart.panel_ticks && !kart.drift_direction && !kart.drift_tier &&
          !kart.last_boost_tier && !kart.outside_drift_q16 && !kart.boost_ticks[0] &&
          !kart.boost_ticks[1] && !kart.boost_ticks[2]);
    CHECK(effects.inhibit_controls && effects.cancel_drift && !effects.boost_active &&
          !effects.boost_acceleration_q16 && effects.boost_cap_ratio_q16 == 65536 &&
          effects.speed_ratio_q16 == 50000 && effects.body_scale_q16 == 40000);
    PortableMeshKartState zero_angle = before;
    zero_angle.outside_drift_q16 = 0;
    CHECK(portable_mesh_course_strong_hit_hold(&zero_angle, &effects) == PORTABLE_MESH_OK);
    CHECK(zero_angle.speed_q16 == before.speed_q16);
    PortableMeshKartState invalid_kart = before;
    invalid_kart.forward_q30[0] = invalid_kart.forward_q30[1] = 759250125;
    invalid_kart.velocity_q16[0] = invalid_kart.velocity_q16[1] = 200 * 65536;
    CHECK(portable_mesh_kart_state_valid(&invalid_kart));
    const PortableMeshKartState invalid_before = invalid_kart;
    const PortableMeshKartEffects effects_before = effects;
    CHECK(portable_mesh_course_strong_hit_hold(&invalid_kart, &effects) == PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&invalid_kart, &invalid_before, sizeof invalid_kart) &&
          !memcmp(&effects, &effects_before, sizeof effects));
    CHECK(portable_mesh_course_strong_hit_hold(NULL, &effects) == PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&effects, &effects_before, sizeof effects));
    CHECK(portable_mesh_course_strong_hit_hold(&invalid_kart, NULL) == PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&invalid_kart, &invalid_before, sizeof invalid_kart));
    PortableMeshStrongHitState state = {0};
    CHECK(portable_mesh_course_strong_hit_admit(&state, 0x1000, 1));
    const PortableMeshStrongHitState saved = state;
    int held = 77;
    CHECK(!portable_mesh_course_strong_hit_admit(&state, 0x1001, 0) &&
          !memcmp(&state, &saved, sizeof state));
    CHECK(!portable_mesh_course_strong_hit_admit(&state, 0x1000, 2) &&
          !memcmp(&state, &saved, sizeof state));
    CHECK(!portable_mesh_course_strong_hit_tick(&state, 2, 0, &held) && held == 77 &&
          !memcmp(&state, &saved, sizeof state));
    CHECK(!portable_mesh_course_strong_hit_tick(&state, 0, 2, &held) && held == 77 &&
          !memcmp(&state, &saved, sizeof state));
    CHECK(!portable_mesh_course_strong_hit_tick(&state, 1, 0, NULL) &&
          !memcmp(&state, &saved, sizeof state));
    state.active = 1;
    CHECK(!portable_mesh_course_strong_hit_valid(&state));
    const PortableMeshStrongHitState invalid = state;
    CHECK(!portable_mesh_course_strong_hit_tick(&state, 1, 0, &held) && held == 77 &&
          !memcmp(&state, &invalid, sizeof state));
    CHECK(!portable_mesh_course_strong_hit_valid(NULL));
    return 0;
}

int main(void)
{
    return clocks() || readmit_and_recovery() || hold_and_rollback();
}
