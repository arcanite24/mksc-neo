#include "race_interpolation.h"
#include "frame_scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"interpolation line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void fractional_layers(void)
{
    PortableRacePresentationState previous = {0}, current = {0}, out;
    previous.sky_motion.precise = current.sky_motion.precise = 1;
    current.sky_motion.scroll_x[0] = 7;
    current.sky_motion.scroll_x_q16[0] = 32768;
    current.sky_motion.scroll_y_q16[1] = -32768;
    current.sky_motion.split_scroll_x_q16 = 32768;
    previous.sky_motion.draw_road_line_valid = current.sky_motion.draw_road_line_valid = 1;
    previous.sky_motion.draw_road_line = 74;
    current.sky_motion.draw_road_line = 75;
    previous.sky_motion.draw_road_line_q16 = 74*65536+49152;
    current.sky_motion.draw_road_line_q16 = 75*65536+16384;
    previous.sky_motion.scroll_y_q16[0] = -49152;
    current.sky_motion.scroll_y_q16[0] = -81920;
    previous.snow_weather.precise = current.snow_weather.precise = 1;
    previous.snow_weather.enabled = current.snow_weather.enabled = 1;
    previous.snow_weather.count = current.snow_weather.count = 1;
    previous.snow_weather.particles[0].active = current.snow_weather.particles[0].active = 1;
    current.snow_weather.particles[0].visible = 1;
    current.snow_weather.particles[0].x = 9;
    current.snow_weather.particles[0].x_q16 = 32768;
    current.snow_weather.particles[0].y_q16 = -32768;
    portable_race_interpolate(&previous, &current, 32768, &out);
    CHECK(out.sky_motion.scroll_x_q16[0] == 16384);
    CHECK(out.sky_motion.scroll_y_q16[1] == -16384);
    CHECK(out.sky_motion.split_scroll_x_q16 == 16384);
    CHECK(out.sky_motion.scroll_x[0] == 7);
    CHECK(out.sky_motion.draw_road_line == 75 &&
          out.sky_motion.draw_road_line_q16 == 75*65536 &&
          out.sky_motion.scroll_y_q16[0] == -65536 &&
          portable_sky_motion_road_first_row(&out.sky_motion,4u) == 300);
    CHECK(out.snow_weather.particles[0].x_q16 == 16384);
    CHECK(out.snow_weather.particles[0].y_q16 == -16384);
    CHECK(out.snow_weather.particles[0].x == 9 && out.snow_weather.particles[0].visible == 1);
    /* Source parity alternates visibility; particle lifetime, not visibility,
     * identifies the same moving slot. Respawns and wraps keep current. */
    previous.snow_weather.particles[0].active = 0;
    previous.sky_motion.scroll_x_q16[0] = INT32_MAX;
    portable_race_interpolate(&previous, &current, 32768, &out);
    CHECK(out.snow_weather.particles[0].x_q16 == 32768);
    CHECK(out.sky_motion.scroll_x_q16[0] == 32768);
    CHECK(out.sky_motion.draw_road_line_q16 == 75*65536);
    previous.sky_motion.draw_road_line_q16 = 0;
    portable_race_interpolate(&previous, &current, 32768, &out);
    CHECK(out.sky_motion.draw_road_line_q16 == current.sky_motion.draw_road_line_q16);
    previous.sky_motion.draw_road_line_q16 = 74*65536+49152;
    previous.sky_motion.layer_mask = 0;
    previous.sky_motion.draw_road_line_valid = 0;
    portable_race_interpolate(&previous, &current, 32768, &out);
    CHECK(out.sky_motion.draw_road_line_q16 == current.sky_motion.draw_road_line_q16 &&
          out.sky_motion.scroll_x_q16[0] == current.sky_motion.scroll_x_q16[0]);
    previous.sky_motion.scroll_x_q16[0] = 0;
    previous.sky_motion.layer_mask = 1;
    portable_race_interpolate(&previous, &current, 32768, &out);
    CHECK(out.sky_motion.scroll_x_q16[0] == 32768);
    PortableRecoveredTimeTrialSnapshot a = {0}, b = {0}, result;
    a.sky_motion.precise = b.sky_motion.precise = 1;
    b.sky_motion.scroll_x_q16[2] = 32768;
    a.sky_motion.draw_road_line_valid = b.sky_motion.draw_road_line_valid = 1;
    a.sky_motion.draw_road_line_q16 = 74*65536;
    b.sky_motion.draw_road_line_q16 = 74*65536+32768;
    a.snow_weather = previous.snow_weather;
    a.snow_weather.particles[0].active = 1;
    b.snow_weather = current.snow_weather;
    portable_trial_interpolate(&a, &b, 32768, &result);
    CHECK(result.sky_motion.scroll_x_q16[2] == 16384);
    CHECK(result.sky_motion.draw_road_line_q16 == 74*65536+16384);
    CHECK(result.snow_weather.particles[0].x_q16 == 16384);
    portable_trial_interpolate(&a, &b, 65536, &result);
    CHECK(result.sky_motion.scroll_x_q16[2] == b.sky_motion.scroll_x_q16[2]);
    CHECK(result.sky_motion.draw_road_line_q16 == b.sky_motion.draw_road_line_q16);
    CHECK(result.snow_weather.particles[0].x_q16 == b.snow_weather.particles[0].x_q16);
    CHECK(result.snow_weather.particles[0].y_q16 == b.snow_weather.particles[0].y_q16);
    CHECK(result.snow_weather.particles[0].visible == b.snow_weather.particles[0].visible);
}

int main(void)
{
    fractional_layers();
    PortableRacePresentationState a={0},b={0},out;
    a.position_x_q16=-65536;b.position_x_q16=65536;b.height_q16=4*65536;
    a.camera_yaw=65500u;b.camera_yaw=36u;b.total_ticks=1u;
    a.opponent_count=b.opponent_count=1u;a.opponents[0].visible=b.opponents[0].visible=1u;
    b.opponents[0].position_y_q16=8*65536;
    a.lava_bubbles.count=b.lava_bubbles.count=1u;
    a.lava_bubbles.actors[0].flags=b.lava_bubbles.actors[0].flags=1u;
    b.lava_bubbles.actors[0].height_q16=10*65536;
    a.green_shells[0].visible=b.green_shells[0].visible=1u;b.green_shells[0].x_q16=6*65536;
    a.visual_yaw=a.ghost_visual_yaw=a.opponents[0].visual_yaw=65520u;
    b.visual_yaw=b.ghost_visual_yaw=b.opponents[0].visual_yaw=16u;
    a.ghost_visible=b.ghost_visible=1u;
    const PortableRacePresentationState original_a=a,original_b=b;
    portable_race_interpolate(&a,&b,32768u,&out);
    CHECK(out.position_x_q16==0&&out.height_q16==2*65536&&out.camera_yaw==0u);
    CHECK(out.visual_yaw==0u&&out.ghost_visual_yaw==0u&&out.opponents[0].visual_yaw==0u);
    CHECK(out.opponents[0].position_y_q16==4*65536&&out.lava_bubbles.actors[0].height_q16==5*65536);
    CHECK(out.green_shells[0].x_q16==3*65536&&out.total_ticks==b.total_ticks);
    CHECK(memcmp(&a,&original_a,sizeof a)==0&&memcmp(&b,&original_b,sizeof b)==0);
    portable_race_interpolate(&a,&b,65536u,&out);CHECK(memcmp(&out,&b,sizeof b)==0);
    b.position_x_q16=100*65536;portable_race_interpolate(&a,&b,32768u,&out);CHECK(out.position_x_q16==b.position_x_q16);
    b=original_b;b.paused=1u;portable_race_interpolate(&a,&b,0u,&out);CHECK(out.position_x_q16==b.position_x_q16);
    b=original_b;b.camera_position_override=1u;portable_race_interpolate(&a,&b,0u,&out);CHECK(out.position_x_q16==b.position_x_q16);
    /* An independent camera cut must not sweep through the course. */
    a.camera_position_override=1u;b=original_b;b.camera_position_override=1u;
    b.camera_position_x_q16=100*65536;
    portable_race_interpolate(&a,&b,32768u,&out);
    CHECK(out.camera_position_x_q16==b.camera_position_x_q16&&out.camera_yaw==b.camera_yaw);
    CHECK(portable_angle_interpolate(36u,65500u,32768u)==0u);
    CHECK(portable_angle_interpolate(0u,32768u,32768u)==16384u);
    PortableRecoveredTimeTrialSnapshot ta={0},tb={0},to;
    tb.position_x_q16=8*65536;tb.driver_angle=65500u;ta.driver_angle=36u;
    portable_trial_interpolate(&ta,&tb,32768u,&to);CHECK(to.position_x_q16==4*65536&&to.driver_angle==0u);
    tb.rescue_count=1u;portable_trial_interpolate(&ta,&tb,0u,&to);CHECK(to.position_x_q16==tb.position_x_q16);
    tb.rescue_count=0u;tb.compatibility.course_id=24u;portable_trial_interpolate(&ta,&tb,0u,&to);CHECK(to.position_x_q16==tb.position_x_q16);
    /* Presentation pacing cannot change the scheduler's fixed tick sequence. */
    const uint16_t rates[]={60u,120u,144u,0u};
    for(size_t rate=0u;rate<4u;++rate){
        PortableFrameScheduler clock;portable_frame_scheduler_init(&clock);
        uint64_t deadline=0u;unsigned ticks=0u,presents=0u;
        for(uint64_t now=0u;now<=UINT64_C(1000000000);now+=1000000u){
            uint16_t buttons[4];size_t count;int dropped;
            CHECK(portable_frame_scheduler_update(&clock,now,1,0u,buttons,4u,&count,&dropped)==0&&!dropped);
            ticks+=(unsigned)count;presents+=(unsigned)portable_present_due(now,rates[rate],&deadline);
            CHECK(portable_frame_scheduler_alpha(&clock)<=65536u);
        }
        CHECK(ticks==59u);
        CHECK(rates[rate]==0u?presents==1001u:presents>=rates[rate]&&presents<=rates[rate]+1u);
    }
    puts("race interpolation: PASS immutable endpoints, wrap, lifecycle cuts, actor positions, render pacing");return 0;
}
