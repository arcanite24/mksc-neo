#include "race_interpolation.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int32_t blend(int32_t a,int32_t b,uint32_t alpha)
{
    return (int32_t)((int64_t)a+((int64_t)b-a)*(int64_t)alpha/65536);
}
uint16_t portable_angle_interpolate(uint16_t a,uint16_t b,uint32_t alpha)
{
    if(alpha>65536u)alpha=65536u;
    const uint16_t bits=(uint16_t)(b-a);
    const int32_t delta=bits<=32768u?bits:(int32_t)bits-65536;
    return (uint16_t)((uint32_t)a+(uint32_t)((int64_t)delta*alpha/65536));
}
static int continuous(int32_t ax,int32_t ay,int32_t bx,int32_t by)
{
    return llabs((int64_t)bx-ax)<=INT64_C(64)*65536&&
           llabs((int64_t)by-ay)<=INT64_C(64)*65536;
}
#define POSITION(out,a,b,x,y,z) do{ \
    if(continuous((a)->x,(a)->y,(b)->x,(b)->y)){ \
        (out)->x=blend((a)->x,(b)->x,alpha); \
        (out)->y=blend((a)->y,(b)->y,alpha); \
        (out)->z=blend((a)->z,(b)->z,alpha); \
    } \
}while(0)
/* These course actor arrays retain placement order throughout a race. A change
 * of actor count is a new course/state; newly activated sprites use current. */
/* Teepee Shy Guys move with their kart, independently of the hidden teepee. */
static void riders(PortableCourseProps *out,const PortableCourseProps *a,
    const PortableCourseProps *b,uint32_t alpha)
{
    if(a->count!=b->count)return;
    for(size_t i=0u;i<b->count&&i<PORTABLE_COURSE_PROP_CAPACITY;++i){
        const PortableTeepeeMotion *x=&a->actors[i].teepee,*y=&b->actors[i].teepee;
        if((b->actors[i].source_code>>16u)!=3u || !x->rider_state || x->rider_state!=y->rider_state)continue;
        POSITION(&out->actors[i].teepee,x,y,rider_x_q16,rider_y_q16,rider_z_q16);
    }
}
#define ACTORS(member) do{ \
    if(a->member.count==b->member.count) \
        for(size_t i=0u;i<b->member.count&&i<sizeof b->member.actors/sizeof b->member.actors[0];++i){ \
            if(!(a->member.actors[i].flags&b->member.actors[i].flags&1u))continue; \
            POSITION(&out->member.actors[i],&a->member.actors[i],&b->member.actors[i],x_q16,y_q16,height_q16); \
        } \
}while(0)
static uint32_t valid_alpha(uint32_t alpha){return alpha>65536u?65536u:alpha;}
/* Native source coordinates and visibility stay on the current tick. Only
 * enhanced rendering reads these fractional presentation companions. Large
 * jumps mark a texture wrap, particle respawn or camera cut, never a sweep. */
static int32_t blend_screen_position(int32_t previous, int32_t current, uint32_t alpha)
{
    return llabs((int64_t)current - previous) <= INT64_C(64) * 65536
        ? blend(previous, current, alpha) : current;
}

static void interpolate_sky(const PortableSkyMotionSnapshot *previous,
    const PortableSkyMotionSnapshot *current, uint32_t alpha, PortableSkyMotionSnapshot *out)
{
    if (!previous->precise || !current->precise ||
        previous->layer_mask != current->layer_mask ||
        previous->split_y != current->split_y ||
        previous->split_layer != current->split_layer ||
        previous->split_vertical != current->split_vertical ||
        previous->road_mode != current->road_mode ||
        previous->draw_road_line_valid != current->draw_road_line_valid ||
        memcmp(previous->layer_source, current->layer_source, sizeof current->layer_source) != 0)
        return;
    for (unsigned layer = 0; layer < 3; ++layer)
    {
        out->scroll_x_q16[layer] = blend_screen_position(
            previous->scroll_x_q16[layer], current->scroll_x_q16[layer], alpha);
        out->scroll_y_q16[layer] = blend_screen_position(
            previous->scroll_y_q16[layer], current->scroll_y_q16[layer], alpha);
    }
    out->split_scroll_x_q16 = blend_screen_position(
        previous->split_scroll_x_q16, current->split_scroll_x_q16, alpha);
    out->split_scroll_y_q16 = blend_screen_position(
        previous->split_scroll_y_q16, current->split_scroll_y_q16, alpha);
    if (current->draw_road_line_valid)
        out->draw_road_line_q16 = blend_screen_position(
            previous->draw_road_line_q16, current->draw_road_line_q16, alpha);
}

static void interpolate_weather(const PortableSnowWeatherSnapshot *previous,
    const PortableSnowWeatherSnapshot *current, uint32_t alpha, PortableSnowWeatherSnapshot *out)
{
    if (!previous->precise || !current->precise || !previous->enabled ||
        !current->enabled || previous->count != current->count)
        return;
    for (unsigned slot = 0; slot < current->count && slot < PORTABLE_SNOW_WEATHER_PARTICLES; ++slot)
    {
        const PortableSnowWeatherSprite *a = &previous->particles[slot];
        const PortableSnowWeatherSprite *b = &current->particles[slot];
        if (!a->active || !b->active || a->art != b->art)
            continue;
        out->particles[slot].x_q16 = blend_screen_position(a->x_q16, b->x_q16, alpha);
        out->particles[slot].y_q16 = blend_screen_position(a->y_q16, b->y_q16, alpha);
    }
}

void portable_race_interpolate(const PortableRacePresentationState *a,
    const PortableRacePresentationState *b,uint32_t alpha,PortableRacePresentationState *out)
{
    *out=*b;alpha=valid_alpha(alpha);
    if(a==NULL||a->phase!=b->phase||a->paused||b->paused||a->recovery_phase!=b->recovery_phase||
       a->camera_position_override!=b->camera_position_override||a->total_ticks>b->total_ticks||
       !continuous(a->camera_position_x_q16,a->camera_position_y_q16,b->camera_position_x_q16,b->camera_position_y_q16)||
       !continuous(a->position_x_q16,a->position_y_q16,b->position_x_q16,b->position_y_q16))return;
    POSITION(out,a,b,position_x_q16,position_y_q16,height_q16);
    out->heading=portable_angle_interpolate(a->heading,b->heading,alpha);
    out->visual_yaw=portable_angle_interpolate(a->visual_yaw,b->visual_yaw,alpha);
    out->camera_yaw=portable_angle_interpolate(a->camera_yaw,b->camera_yaw,alpha);
    out->camera_position_x_q16=blend(a->camera_position_x_q16,b->camera_position_x_q16,alpha);
    out->camera_position_y_q16=blend(a->camera_position_y_q16,b->camera_position_y_q16,alpha);
    if(a->ghost_visible&&b->ghost_visible){
        POSITION(out,a,b,ghost_position_x_q16,ghost_position_y_q16,ghost_height_q16);
        out->ghost_visual_yaw=portable_angle_interpolate(a->ghost_visual_yaw,b->ghost_visual_yaw,alpha);
    }
    if(a->opponent_count==b->opponent_count)
        for(size_t i=0u;i<b->opponent_count&&i<PORTABLE_RACE_SCENE_MAX_OPPONENTS;++i)
            if(a->opponents[i].visible&&b->opponents[i].visible){
                POSITION(&out->opponents[i],&a->opponents[i],&b->opponents[i],position_x_q16,position_y_q16,height_q16);
                out->opponents[i].visual_yaw=portable_angle_interpolate(
                    a->opponents[i].visual_yaw,b->opponents[i].visual_yaw,alpha);
            }
    for(size_t i=0u;i<24u;++i){
        if(a->green_shells[i].visible&&b->green_shells[i].visible&&a->green_shells[i].kind==b->green_shells[i].kind)
            POSITION(&out->green_shells[i],&a->green_shells[i],&b->green_shells[i],x_q16,y_q16,z_q16);
        if(a->red_shells[i].visible&&b->red_shells[i].visible&&a->red_shells[i].kind==b->red_shells[i].kind)
            POSITION(&out->red_shells[i],&a->red_shells[i],&b->red_shells[i],x_q16,y_q16,z_q16);
        if(a->bananas[i].visible&&b->bananas[i].visible&&a->bananas[i].kind==b->bananas[i].kind)
            POSITION(&out->bananas[i],&a->bananas[i],&b->bananas[i],x_q16,y_q16,z_q16);
    }
    for(size_t i=0u;i<PORTABLE_BOO_ATTACK_CAPACITY;++i)
        if(a->boo_attacks[i].occupied&&b->boo_attacks[i].occupied&&a->boo_attacks[i].visible&&b->boo_attacks[i].visible&&
           a->boo_attacks[i].target==b->boo_attacks[i].target)
            POSITION(&out->boo_attacks[i],&a->boo_attacks[i],&b->boo_attacks[i],x_q16,y_q16,z_q16);
    interpolate_sky(&a->sky_motion, &b->sky_motion, alpha, &out->sky_motion);
    interpolate_weather(&a->snow_weather, &b->snow_weather, alpha, &out->snow_weather);
    riders(&out->course_props,&a->course_props,&b->course_props,alpha);
    ACTORS(course_props);ACTORS(lava_bubbles);ACTORS(thwomps);ACTORS(crabs);ACTORS(seagulls);ACTORS(piranhas);
    ACTORS(snowmen);ACTORS(mice);ACTORS(falling_stars);ACTORS(penguins);ACTORS(boos);ACTORS(cannonballs);
}
void portable_trial_interpolate(const PortableRecoveredTimeTrialSnapshot *a,
    const PortableRecoveredTimeTrialSnapshot *b,uint32_t alpha,PortableRecoveredTimeTrialSnapshot *out)
{
    *out=*b;alpha=valid_alpha(alpha);
    if(a==NULL||a->compatibility.course_id!=b->compatibility.course_id||a->phase!=b->phase||a->paused||b->paused||a->rescue_count!=b->rescue_count||
       a->recovery_phase!=b->recovery_phase||a->total_ticks>b->total_ticks||
       !continuous(a->position_x_q16,a->position_y_q16,b->position_x_q16,b->position_y_q16))return;
    POSITION(out,a,b,position_x_q16,position_y_q16,height_q16);
    out->heading=portable_angle_interpolate(a->heading,b->heading,alpha);
    out->camera_yaw=portable_angle_interpolate(a->camera_yaw,b->camera_yaw,alpha);
    out->driver_angle=portable_angle_interpolate(a->driver_angle,b->driver_angle,alpha);
    interpolate_sky(&a->sky_motion, &b->sky_motion, alpha, &out->sky_motion);
    interpolate_weather(&a->snow_weather, &b->snow_weather, alpha, &out->snow_weather);
    riders(&out->course_props,&a->course_props,&b->course_props,alpha);
    ACTORS(course_props);ACTORS(lava_bubbles);ACTORS(thwomps);ACTORS(crabs);ACTORS(seagulls);ACTORS(piranhas);
    ACTORS(snowmen);ACTORS(mice);ACTORS(falling_stars);ACTORS(penguins);ACTORS(boos);ACTORS(cannonballs);
}
int portable_present_due(uint64_t now,uint16_t fps,uint64_t *deadline)
{
    if(fps==0u)return 1;
    const uint64_t period=(UINT64_C(1000000000)+fps-1u)/fps;
    if(*deadline==0u||now>=*deadline){
        *deadline=*deadline!=0u&&now-*deadline<period?*deadline+period:now+period;
        return 1;
    }
    return 0;
}
