#ifndef MKSC_PORTABLE_CHARACTER_RENDERER_H
#define MKSC_PORTABLE_CHARACTER_RENDERER_H

#include <stdint.h>

#include "character_asset.h"
#include "camera_projection.h"
#include "driver_projection_asset.h"
#include "software_frame.h"

#include "character_view.h"

typedef struct
{
    uint16_t size,inverse_x,inverse_y;
    int32_t center_x,center_y,bob_delta; /* Physical pixels, source rounding. */
    uint8_t double_size;
} PortableCharacterProjectionGeometry;

/* Size0 is valid far clipping. Shared source matrix/pivot calculations keep
 * optional models' squash and cached bob aligned with the billboard owner. */
int portable_character_projection_geometry(const PortableSoftwareFrame *frame,
    const PortableDriverProjectionRules *rules,const PortableCameraProjectionResult *body,
    uint16_t base_inverse_q8,uint16_t effect_x_q8,uint16_t effect_y_q8,int8_t cell_bob,
    PortableCharacterProjectionGeometry *out);

int portable_character_render(const PortableCharacterAssetView *character,
                              PortableSoftwareFrame *frame,
                              int32_t origin_x,
                              int32_t origin_y,
                              int flip_x,
                              int flip_y);

int portable_character_render_view(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    int flip_y);

/* The GBA OAM affine values are inverse scale coefficients: 0x100 is normal
 * and 0x180 draws a sprite at two thirds of its normal width and height. */
int portable_character_render_affine(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    int flip_x,
    int flip_y,
    uint16_t affine_scale);

int portable_character_render_view_affine(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    int flip_y,
    uint16_t affine_scale);

/* Native nonuniform scaling for effects, optionally retaining the bottom anchor. */
int portable_character_render_view_scaled(
    const PortableCharacterAssetView *character, PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y, uint8_t view_angle, int flip_y,
    uint16_t scale_x, uint16_t scale_y, int bottom_anchor);

/* Draw one 64x64, 4bpp affine OBJ using its signed OAM diagonal matrix. */
int portable_character_render_view_oam_affine(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    int16_t pa,
    int16_t pd,
    int16_t sample_center_y);

/* The source selects 64, 32 or 16 pixel OBJ art as a racer recedes. Sample
 * the packed 64 pixel view frame at the corresponding 2:1 or 4:1 step. */
int portable_character_render_view_oam_lod(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t view_angle,
    uint8_t obj_size,
    int16_t pa,
    int16_t pd,
    int16_t sample_center_y,
    int floor_sample);

/* The GP result animation selects a source graphics frame directly, beyond
 * the 29 directional frames addressed by the ordinary view-angle table. */
int portable_character_render_slot_oam_lod(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t frame_slot,
    uint8_t obj_size,
    int16_t pa,
    int16_t pd,
    int16_t sample_center_y,
    int floor_sample);

int portable_character_render_slot_oam_double(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y,
    uint8_t frame_slot, int16_t pa, int16_t pd);

int portable_character_render_view_oam_double(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y,
    uint8_t view_angle, int16_t pa, int16_t pd);

int portable_character_render_view_oam_lod_double(
    const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y,
    uint8_t view_angle, uint8_t obj_size,
    int16_t pa, int16_t pd);

/* Original driver cell pivot; axes are inverse Q8 coefficients. */
int portable_character_render_driver_view(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,uint8_t angle,
    uint16_t inverse_x,uint16_t inverse_y);

/* CD50/O3: source LOD, cell pivot and affine sampling from a body projection. */
int portable_character_render_driver_projected(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,const PortableDriverProjectionRules *rules,
    const PortableCameraProjectionResult *body,uint8_t angle,
    uint16_t base_inverse_q8,uint16_t effect_x_q8,uint16_t effect_y_q8,
    int8_t cell_bob);

/* Resolved immutable frame/mirror, including near contact art. The projected
 * path suppresses contact overrides beyond source LOD0/distance511. */
int portable_character_render_driver_pose_projected(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,const PortableDriverProjectionRules *rules,
    const PortableCameraProjectionResult *body,const PortableCharacterViewPose *pose,
    uint16_t base_inverse_q8,uint16_t effect_x_q8,uint16_t effect_y_q8,int8_t cell_bob);
int portable_character_render_driver_pose(const PortableCharacterAssetView *character,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,const PortableCharacterViewPose *pose,
    uint16_t inverse_x,uint16_t inverse_y);

#endif
