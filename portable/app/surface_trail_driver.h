#ifndef MKSC_PORTABLE_SURFACE_TRAIL_DRIVER_H
#define MKSC_PORTABLE_SURFACE_TRAIL_DRIVER_H
#include "surface_trail.h"
#include "driver_pilot.h"
#include "finish_camera.h"
#include "camera_projection.h"
/* Cached native projection for source effects; physical scale and wide
 * viewport centering are applied only by the renderer. */
static inline int portable_selected_driver_body(const PortableDriverPilotState *driver,
    uint16_t normal_yaw,const PortableFinishCameraSnapshot *finish,int8_t projection_y_offset,const int32_t *sine,
    size_t count,PortableCameraProjectionResult *body)
{
    if(!driver||!finish||!sine||count<1024u||!body)return 0;
    const uint16_t yaw=finish->active?finish->yaw:normal_yaw;
    const unsigned slot=((int16_t)yaw/64)&1023u;
    PortableCameraState camera;PortableCameraProjection projection;
    portable_camera_state_update(&camera,finish->active?finish->x_q16:driver->pose.position_x,
        finish->active?finish->y_q16:driver->pose.position_y,yaw);
    camera.projection_y_offset=projection_y_offset;
    return !portable_camera_projection_build(&camera,sine[slot],sine[(256u-slot)&1023u],31414,9319,&projection) &&
        !portable_camera_projection_project(&camera,&projection,driver->pose.position_x,driver->pose.position_y,0,body) &&
        !portable_camera_projection_body(body,driver->pose.position_z,body);
}
/* Shared selected-driver adapter: native 240x160 coordinates enter simulation;
 * physical scale and the widescreen center shift belong to rendering. */
static inline int portable_surface_trail_driver_inputs(const PortableDriverPilotState *driver,
    uint16_t normal_yaw,const PortableFinishCameraSnapshot *finish,uint32_t clock,
    int8_t projection_y_offset,const int32_t *sine,size_t count,PortableSurfaceTrailInputs *out)
{
    if(!driver||!finish||!sine||count<1024u||!out)return 0;
    const uint16_t yaw=finish->active?finish->yaw:normal_yaw;
    *out=(PortableSurfaceTrailInputs){.camera_yaw=yaw,
        .source_angle_56=driver->steering.source_angle_56,
        .source_angle_7a=driver->normal_collision_response_angle_7a,
        .animation_flags=driver->forward_acceleration.state_flags,
        .height_q16=driver->pose.position_z,.speed_q16=driver->motion.speed_q16,
        .flags=driver->steering.state_flags,.material=driver->source_material_85,
        .response_status=driver->steering.response_status,.mushroom_state=driver->steering.response_slot,
        .parity=(uint8_t)(clock&1u)};
    if(driver->steering.state_flags&0x20u)return 1;
    PortableCameraProjectionResult body;
    if(!portable_selected_driver_body(driver,normal_yaw,finish,projection_y_offset,sine,count,&body))return 0;
    out->body_x=body.x;out->body_y=body.y;return 1;
}
#endif
