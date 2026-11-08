#ifndef PORTABLE_MODEL_RENDERER_H
#define PORTABLE_MODEL_RENDERER_H

#include "model_asset.h"
#include "scene_3d.h"
#include "camera_projection.h"
#include "driver_projection_asset.h"

typedef struct
{
    int32_t position_x_q16,position_y_q16,height_q16;
    uint16_t visual_yaw,base_inverse_q8;
    int32_t inverse_x_q8,inverse_y_q8;
    int32_t screen_y_offset_q16;
    int8_t bob_offset;
    uint8_t source_driver;
} PortableModelPose;

/* The caller resolves the exact binding/source pose and supplies the shared
 * camera and physical projection. Returns0 before any fragments on invalid or
 * unsupported input; caller can then draw its original billboard. */
int portable_model_render(PortableScene3D *target,const PortableModelBinding *binding,
    const PortableModelPose *pose,const PortableCameraState *camera,
    const PortableCameraProjection *basis,const PortableScene3DProjection *projection,
    const PortableDriverProjectionRules *driver_projection,
    const int32_t *sine,size_t sine_count);

#endif
