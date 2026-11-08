#ifndef PORTABLE_GP_CEREMONY_ASSET_H
#define PORTABLE_GP_CEREMONY_ASSET_H
#include "asset_pack.h"
#include "native_image.h"
#include "ceremony_motion.h"

enum {
    PORTABLE_CEREMONY_RANK = 0, PORTABLE_CEREMONY_RATING = 8,
    PORTABLE_CEREMONY_CLASS = 16, PORTABLE_CEREMONY_CUP_NAME = 19,
    PORTABLE_CEREMONY_CUP_ICON = 24, PORTABLE_CEREMONY_AUDIENCE = 29,
    PORTABLE_CEREMONY_CONTINUE = 31, PORTABLE_CEREMONY_COIN = 32,
    PORTABLE_CEREMONY_DIGIT = 33, PORTABLE_CEREMONY_IMAGE_COUNT = 43,
    PORTABLE_CEREMONY_MAX_IMAGES = 384
};
typedef enum {
    PORTABLE_GP_CEREMONY_ASSET_OK = 0,
    PORTABLE_GP_CEREMONY_ASSET_INVALID_ARGUMENT,
    PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION
} PortableGpCeremonyAssetStatus;
typedef struct {
    uint32_t entrance_ticks, celebration_ticks, fade_ticks;
    uint32_t sky_color, ground_color, podium_color;
    PortableNativeImage images[PORTABLE_CEREMONY_MAX_IMAGES];
    uint16_t image_count, stage_content_id;
    uint8_t source_presentation;
    PortableCeremonyMotionRules motion;
    uint16_t cup_headers[5][23], panels[3], sparkles[12], confetti[12];
    int32_t podium_offsets[3][3];
    uint16_t shadow_image,summary_coin_image;
    int16_t header_targets[23][2];
    uint16_t cup_labels[5],glyphs[12],rank_labels[8];
    uint16_t coin_digits[10],total_digits[10],highlight_classes[3],unlock_image;
    uint16_t pulse_colors[2][2][16];
    uint8_t image_darken[PORTABLE_CEREMONY_MAX_IMAGES];
} PortableGpCeremonyAssetView;
PortableGpCeremonyAssetStatus portable_gp_ceremony_asset_open(
    const PortableAssetSectionView *section, PortableGpCeremonyAssetView *out);
#endif
