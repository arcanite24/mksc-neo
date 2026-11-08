#ifndef MKSC_PORTABLE_CEREMONY_MOTION_H
#define MKSC_PORTABLE_CEREMONY_MOTION_H
#include <stddef.h>
#include <stdint.h>
#include "character_view.h"
#include "driver_affine.h"
#include "finish_camera.h"

enum { PORTABLE_CEREMONY_CONFETTI=32,PORTABLE_CEREMONY_TIMED_ACTORS=8 };
typedef struct {
    int32_t x_q16,y_q16;
    int16_t inverse_x,inverse_y,angle,tick,origin_x,origin_y;
    uint8_t visible;
} PortableCeremonyCellState;
typedef struct {
    uint16_t image_id;
    int32_t x_q16,y_q16;
    int16_t inverse_x,inverse_y,angle,depth;
    uint8_t palette_group,palette_frame,priority,visible;
} PortableCeremonySprite;
typedef struct {
    int32_t x_q16,y_q16,vx_q16,vy_q16,target_x_q16,target_y_q16;
    uint8_t remaining,active;
} PortableCeremonyConfetti;
typedef struct { uint16_t image_id,duration; } PortableCeremonySequenceFrame;
typedef struct {
    int16_t x,y,depth_bias;
    uint16_t first,count,initial_frame,initial_remaining;
} PortableCeremonyTrack;
typedef struct { int32_t x_q16,y_q16;uint16_t frame,remaining;int16_t depth_bias; } PortableCeremonyTimedActor;
/* Decoder owns these normalized IDs; no ROM addresses enter the native owner. */
typedef struct {
    uint16_t winner_header[16],loser_header[7],flypast_images[8],trophy_images[3][16];
    uint16_t zoom_image,drop_image,sparkle_image,confetti_image;
    uint8_t trophy_delay[3];
    PortableCeremonyTrack tracks[8];
    PortableCeremonySequenceFrame frames[32];
    uint16_t frame_count;
    int16_t header_targets[23][2];
} PortableCeremonyMotionRules;
typedef struct { int32_t x_q16,y_q16,z_q16; } PortableCeremonyWorldPoint;
typedef struct {
    PortableCeremonyWorldPoint grid[8],podium_offset[3];
} PortableCeremonyWorldRules;
typedef struct {
    PortableCeremonyWorldPoint position,target;
    int32_t vx_q16,vy_q16,vz_q16,bounce_base_q16;
    uint32_t source_flags;
    uint16_t heading,callback_tick;
    int16_t response_88;
    uint8_t visible,selected,driver_class,arrived,hop_complete;
    uint8_t finish_mode,finish_phase,finish_frame,finish_flags;
    PortableDriverAffine affine;
    PortableCharacterViewPose art_pose;
} PortableCeremonyWorldDriver;
typedef struct {
    PortableCeremonyWorldDriver drivers[4];
    int32_t camera_x_q16,camera_y_q16;
    uint16_t camera_yaw;
    uint8_t count,selected_index,arrival_mask,hop_mask,shadow_started;
} PortableCeremonyWorld;
typedef struct {
    PortableCeremonyWorld world;
    PortableCeremonyCellState zoom,flypast,trophy,loser;
    PortableCeremonyConfetti confetti[PORTABLE_CEREMONY_CONFETTI];
    PortableCeremonyTimedActor actors[PORTABLE_CEREMONY_TIMED_ACTORS];
    uint32_t confetti_rng;
    uint16_t sparkle_angle,shadow_expansion;
    int16_t actor_scroll_remaining;
    uint8_t fly_frame,fly_counter,trophy_frame,trophy_counter,sparkle_palette;
} PortableCeremonyMotion;
void portable_ceremony_motion_reset(PortableCeremonyMotion *state);
int portable_ceremony_world_init(PortableCeremonyMotion *state,const PortableCeremonyWorldRules *rules,
    uint8_t selected_rank,const uint8_t classes[4]);
int portable_ceremony_world_target(const PortableCeremonyWorldRules *rules,unsigned index,PortableCeremonyWorldPoint *out);
int portable_ceremony_world_reveal(PortableCeremonyMotion *state,const PortableCeremonyWorldRules *rules,unsigned index);
int portable_ceremony_world_arrival_tick(PortableCeremonyMotion *state,unsigned index);
/* Returns source hop cue156; negative means invalid. */
int portable_ceremony_world_hop_start(PortableCeremonyMotion *state,const PortableCeremonyWorldRules *rules,unsigned index);
int portable_ceremony_world_hop_tick(PortableCeremonyMotion *state,unsigned index,const PortableDriverAffineRules *affine);
int portable_ceremony_world_place_podium(PortableCeremonyMotion *state,const PortableCeremonyWorldRules *rules,unsigned index);
int portable_ceremony_world_rotate_loser_tick(PortableCeremonyMotion *state);
int portable_ceremony_world_bounce_start(PortableCeremonyMotion *state,unsigned index);
int portable_ceremony_world_bounce_tick(PortableCeremonyMotion *state,unsigned index,int16_t source_ease_q8);
int portable_ceremony_world_bounce_tick_scaled(PortableCeremonyMotion *state,unsigned index,
    int16_t source_ease_q8,int32_t height_factor);
int portable_ceremony_world_celebrate(PortableCeremonyMotion *state,unsigned index,const PortableFinishSchedule *schedule);
int portable_ceremony_world_art_tick(PortableCeremonyMotion *state,unsigned index,const PortableCharacterViewRules *view_rules,
    const PortableFinishSchedule *schedule,const PortableDriverAffineRules *affine,uint16_t distance);

void portable_ceremony_zoom_start(PortableCeremonyMotion *state,int visible);
int portable_ceremony_zoom_tick(PortableCeremonyMotion *state,const int32_t *sine,size_t count);
void portable_ceremony_flypast_start(PortableCeremonyMotion *state);
int portable_ceremony_flypast_tick(PortableCeremonyMotion *state,const int32_t *sine,size_t count);
/* Source draw callback26D54 advances graphics every7 calls;120 sounds at3/7. */
uint16_t portable_ceremony_flypast_animation_tick(PortableCeremonyMotion *state);
int portable_ceremony_trophy_start(PortableCeremonyMotion *state,uint8_t rank,int small_trophy);
int portable_ceremony_trophy_appear_tick(PortableCeremonyMotion *state);
int portable_ceremony_trophy_move_tick(PortableCeremonyMotion *state,const int32_t *sine,size_t count);
void portable_ceremony_trophy_animation_tick(PortableCeremonyMotion *state);
void portable_ceremony_confetti_tick(PortableCeremonyMotion *state);
void portable_ceremony_loser_start(PortableCeremonyMotion *state);
/* Caller supplies source E930 variant9 interpolation; return1 after64ticks. */
int portable_ceremony_loser_tick(PortableCeremonyMotion *state,int16_t interpolated_y,uint16_t *song);
uint16_t portable_ceremony_loser_shadow_tick(PortableCeremonyMotion *state);
int portable_ceremony_tracks_start(PortableCeremonyMotion *state,const PortableCeremonyTrack tracks[8],
    const PortableCeremonySequenceFrame *frames,size_t frame_count,int scrolling);
int portable_ceremony_tracks_tick(PortableCeremonyMotion *state,const PortableCeremonyTrack tracks[8],
    const PortableCeremonySequenceFrame *frames,size_t frame_count);
PortableCeremonySprite portable_ceremony_cell_sprite(const PortableCeremonyCellState *cell,uint16_t image_id,uint8_t priority);
int portable_ceremony_track_sprites(const PortableCeremonyMotion *state,const PortableCeremonyTrack tracks[8],
    const PortableCeremonySequenceFrame *frames,size_t frame_count,int16_t selected_depth,int16_t road_line,
    PortableCeremonySprite out[8]);
#endif
