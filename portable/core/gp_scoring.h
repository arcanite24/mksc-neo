#ifndef PORTABLE_GP_SCORING_H
#define PORTABLE_GP_SCORING_H
#include <stdint.h>
/* 1E7C8 reads DB0A8[finish_rank+1] and adds to each racer's byte score.
 * Limit configurable awards to63 so four races fit that original byte.
 * 2E98 starts three retries; mods may reduce them, retaining source grading. */
typedef struct {
    int32_t rank_bonus[3][4];
    int32_t speed_target[3];
    int32_t driver_bonus[8];
} PortableGpDrivingQualityRules;

typedef struct {
    uint8_t finish_points[8];
    uint8_t initial_retry_credits;
    uint8_t has_quality_rules;
    PortableGpDrivingQualityRules quality;
} PortableGpScoringRules;
#endif
