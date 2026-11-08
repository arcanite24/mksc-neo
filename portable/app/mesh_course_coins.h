#ifndef PORTABLE_MESH_COURSE_COINS_H
#define PORTABLE_MESH_COURSE_COINS_H
#include "mesh_racer.h"

enum
{
    PORTABLE_MESH_COIN_CAPACITY = 64,
    PORTABLE_MESH_COIN_RACERS = 8,
    PORTABLE_MESH_COIN_LOSS_CAUSES = 5
};
typedef enum
{
    PORTABLE_MESH_COIN_HIT,
    PORTABLE_MESH_COIN_RESCUE,
    PORTABLE_MESH_COIN_CONTACT,
    PORTABLE_MESH_COIN_CRUSH,
    PORTABLE_MESH_COIN_HAUNT
} PortableMeshCoinCause;
typedef struct
{
    uint32_t id, object_index, model_id;
    int32_t position_q16[3], half_q16[3];
} PortableMeshCourseCoin;
typedef struct
{
    uint32_t course_id, enabled, profile_id, revision, modes, cap, initial, placement_count;
    int32_t ratios_q16[100];
    uint8_t loss[5];
    PortableMeshCourseCoin placements[64];
} PortableMeshCourseCoinsConfig;
typedef struct
{
    uint64_t available;
    uint8_t enabled;
    uint8_t count[8], contact_grace[8];
    int32_t previous_body_scale_q16[8];
} PortableMeshCourseCoins;
typedef struct
{
    uint8_t pre[5][8], post[5][8]; /* Accepted occurrence counts, indexed by cause/racer. */
} PortableMeshCourseCoinRequests;
typedef struct
{
    uint64_t picked[8]; /* All duplicate-ID placement bits removed by this pickup. */
    uint8_t before[8], after[8], lost[8], causes[8]; /* Cause bits only for actual loss. */
} PortableMeshCourseCoinEvents;
/* Config owns all decoded data. All failures preserve outputs. Missing rules
 * permits no coin declarations and creates a zero owner. No implicit respawn. */
int portable_mesh_course_coins_config_open(const PortableCourse3DView *,
                                           PortableMeshCourseCoinsConfig *, char *, size_t);
int portable_mesh_course_coins_config_valid(const PortableMeshCourseCoinsConfig *);
int portable_mesh_course_coins_valid(const PortableMeshCourseCoinsConfig *,
                                     const PortableMeshCourseCoins *);
int portable_mesh_course_coins_init(const PortableMeshCourseCoinsConfig *,
                                    PortableMeshCourseCoins *);
int portable_mesh_course_coins_advance(const PortableMeshCourseCoinsConfig *,
                                       PortableMeshCourseCoins *);
/* Strongest cap wins. An active item boost ignores the coin cap; existing
 * internal panel/mini-turbo effects continue to use the scaled base speed. */
int portable_mesh_course_coins_effect(const PortableMeshCourseCoinsConfig *,
                                      const PortableMeshCourseCoins *, uint32_t mode,
                                      unsigned racer, PortableMeshKartEffects *);
/* Preturn losses, one earliest swept foot-point pickup per racer in ascending
 * racer order, then postturn losses. Caller additionally excludes special held
 * states through eligible_mask. Both endpoints must be grounded/racing and
 * outside recovery. Foot clearance is (radius+ride_height)*body_scale along up;
 * body_scale is per-racer Q16 from admitted effects (NULL means identity).
 * The previous endpoint uses the last accepted movement scale in this owner,
 * not postmove item status. Each resolve stores the actual new movement scale,
 * including modes excluded from pickups/losses; absent owners remain all zero.
 * Request losses are applied even when movement is ineligible, since accepted
 * recovery damage itself is a caller-owned loss event. */
int portable_mesh_course_coins_resolve(
    const PortableMeshCourseCoinsConfig *, PortableMeshCourseCoins *, uint32_t mode,
    const PortableMeshKartRules *, const int32_t body_scale_q16[8],
    const PortableMeshRacerState *before, const PortableMeshRacerState *after, size_t count,
    uint8_t eligible_mask, const PortableMeshCourseCoinRequests *, PortableMeshCourseCoinEvents *,
    char *, size_t);
/* FNV1a over conditional COI1, availability, counts/graces and last movement body scales.
 * Disabled owner contributes no bytes; returns incoming hash unchanged. */
uint64_t portable_mesh_course_coins_hash(const PortableMeshCourseCoins *, uint64_t hash);
#endif
