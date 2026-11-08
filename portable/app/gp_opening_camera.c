#include "gp_opening_camera.h"

#include <limits.h>
#include <stddef.h>

#include "driver_vector_effect.h"

static int32_t source_asr16_with_sign(int32_t value)
{
    /* (value + (u32(value) >> 31)) ASR 16, without relying on a signed
     * right shift of negative values on the host. */
    const int64_t adjusted=(int64_t)value+(value<0?1:0);
    return (int32_t)(adjusted<0?
        (adjusted-INT64_C(65535))/INT64_C(65536):
        adjusted/INT64_C(65536));
}

static int opening_camera_build(uint32_t turn,
    int32_t grid_x_q16,int32_t grid_y_q16,
    const int32_t sine[1024],PortableGpOpeningCamera *out,uint32_t last_turn)
{
    int32_t target_angle=INT32_C(0x7700);
    int32_t target_distance=INT32_C(0x60);
    int32_t angle;
    int32_t dx;
    int32_t dy;
    int64_t world_x;
    int64_t world_y;
    uint16_t yaw;
    if(out==NULL||sine==NULL||turn==0u||turn>last_turn)return 0;
    /* The source orbit actor reaches its grid-centre endpoint on turn 251
     * and holds it. Continuing the sine curve would start another orbit
     * after turn 283, while source camera/IWRAM remain fixed. */
    if(turn>=251u){
        out->position_x_q16=grid_x_q16;
        out->position_y_q16=grid_y_q16;
        out->yaw=0u;
        out->target_angle=0;
        out->target_distance=0;
        return 1;
    }
    if(turn>11u){
        const uint32_t step=turn-11u;
        const int32_t delta=INT32_C(0x8000)-
            sine[(256u-2u*step)&1023u];
        target_angle+=source_asr16_with_sign(-delta*INT32_C(0x7700));
        target_distance+=source_asr16_with_sign(-delta*INT32_C(0x60));
    }
    /* The source sign-adjusts before ASR 6, i.e. division toward zero. */
    angle=(uint16_t)(target_angle+INT32_C(0x4000));
    if(angle>=INT32_C(0x8000))angle-=INT32_C(0x10000);
    angle/=64;
    world_x=(int64_t)grid_x_q16+
        (int64_t)sine[(256u-(uint32_t)angle)&1023u]*target_distance*2;
    world_y=(int64_t)grid_y_q16+
        (int64_t)sine[(uint32_t)angle&1023u]*target_distance*2;
    if(world_x<INT32_MIN||world_x>INT32_MAX||
       world_y<INT32_MIN||world_y>INT32_MAX)return 0;
    out->position_x_q16=(int32_t)world_x;
    out->position_y_q16=(int32_t)world_y;
    dx=grid_x_q16-out->position_x_q16;
    dy=grid_y_q16-out->position_y_q16;
    while(dx< -1024||dx>1023||dy< -1024||dy>1023){
        dx/=2;
        dy/=2;
    }
    /* At scene clock 251 the orbit reaches distance zero. The original
     * camera actor stores yaw zero at its grid centre; vector-angle on a
     * zero displacement would instead return a quarter turn. */
    if(target_distance==0)
        yaw=0u;
    else if(portable_driver_vector_effect_source_angle((int16_t)dx,
        (int16_t)dy,&yaw)!=PORTABLE_DRIVER_VECTOR_EFFECT_OK)return 0;
    out->yaw=yaw;
    out->target_angle=target_angle;
    out->target_distance=target_distance;
    return 1;
}

int portable_gp_opening_camera_build(uint32_t turn,
    int32_t grid_x_q16,int32_t grid_y_q16,
    const int32_t sine[1024],PortableGpOpeningCamera *out)
{
    return opening_camera_build(turn,grid_x_q16,grid_y_q16,sine,out,1589u);
}

int portable_gp_opening_camera_build_hit_continuation(uint32_t turn,
    int32_t grid_x_q16,int32_t grid_y_q16,
    const int32_t sine[1024],PortableGpOpeningCamera *out)
{
    return opening_camera_build(turn,grid_x_q16,grid_y_q16,sine,out,1709u);
}
