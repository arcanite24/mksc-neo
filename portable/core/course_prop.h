#ifndef PORTABLE_COURSE_PROP_H
#define PORTABLE_COURSE_PROP_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "spatial_audio.h"
#define PORTABLE_COURSE_PROP_CAPACITY 64u
/* 53104/59E80: common static placements. 59E44 starts the protected-hit
 * reaction; 59E00 owns the entire flight and removal, without changing tiles. */
/* 59710/5982C/59778: Mechakoopa's 352-tick patrol and two-frame stride.
 * The ROM gives variant0 positive X and variant1 negative X; the old
 * transcribed constructor incorrectly gives both variants negative X. */
typedef struct
{
    int32_t velocity_x_q16;
    uint16_t cycle, heading;
    uint8_t animation_ticks, animation_phase, has_facing;
} PortableMechakoopaMotion;
/* 592E4/59358/596D8: Kamek accelerates between two patrol endpoints,
 * bobs at twelve units and is repelled only by an unguarded protected kart. */
typedef struct
{
    int32_t velocity_x_q16, velocity_z_q16;
    uint16_t phase, heading;
    uint8_t has_facing;
    uint8_t mirrored; /* Mirror mode: the patrol starts toward -x. */
} PortableKamekMotion;
/* 59E98/59F04/59FAC: Broken Pier Boo bobs three units around height eight.
 * An unprotected kart touch vanishes it and starts 4886C's haunt on that kart;
 * a protected touch flings it. The live course constructor seeds its bob
 * from one main_getRandom draw in authored placement order. */
typedef struct
{
    int32_t velocity_z_q16;
    uint16_t phase;
} PortablePierBooMotion;
/* 58A74/58B4C/58AB0: Sunset Wilds Shy Guy teepee. An unguarded kart touch
 * marks the TT ghost unusable and bursts it along the kart's velocity into
 * three scrap frames over thirty ticks. 58BA8 then puts its Shy Guy on that
 * kart: 58EB8 rides twelve units behind and holds the kart's +AC lock at 360
 * until accelerator presses clear it; 58CA4/58D4C fling it off at once for
 * a protected kart, or when the lock ends. The ROM gate is kart family
 * 0x2000 (movs#0x80/lsls#6), as the decomp's patched 8B4C schedule shows. */
typedef struct
{
    int32_t velocity_x_q16, velocity_y_q16;
    int32_t rider_x_q16, rider_y_q16, rider_z_q16;
    int32_t rider_vx_q16, rider_vy_q16, rider_vz_q16;
    uint16_t rider_spin, rider_spin_rate, rider_gravity, rider_heading;
    uint8_t counter, frame, kart, rider_state; /* rider 0 none, 1 riding, 2 flung. */
} PortableTeepeeMotion;
typedef struct
{
    int32_t x_q16, y_q16, z_q16, speed_q16;
    int16_t component_x, component_y, protection;
    uint16_t heading, lock;
    uint8_t is_player, muted;
} PortableTeepeeKart;
/* 59938/599EC: Lakeside Park class A2 rocks. Hidden for a per-variant delay,
 * they fall 48 units shrinking from 4x scale, play a 10-tick impact, make two
 * diagonal quarter-unit bounces, rest 128 ticks and repeat. Their own collide
 * callbacks are empty; karts treat type 0x101D like a lava bubble. */
typedef struct
{
    int32_t home_x_q16, home_y_q16, velocity_z_q16;
    int16_t timer, delay;
    uint8_t frame;
    uint8_t mirrored; /* Mirror mode: bounces roll toward -x. */
} PortableLakeRockMotion;
/* Positional actor cues for the host: song plus 804CD28 volume. */
#define PORTABLE_COURSE_PROP_SOUND_CAPACITY 16u
typedef struct
{
    uint32_t serial;
    uint16_t songs[PORTABLE_COURSE_PROP_SOUND_CAPACITY],
        volumes[PORTABLE_COURSE_PROP_SOUND_CAPACITY];
    uint8_t count;
} PortableCoursePropSounds;
typedef struct
{
    int32_t x_q16, y_q16;
    uint32_t flags;
    PortableCoursePropSounds *sounds;
} PortableCoursePropListener;
typedef struct
{
    int32_t x_q16, y_q16, height_q16;
    uint32_t source_code;
    int32_t timer;
    int16_t affine_x_q8, affine_y_q8;
    uint8_t state, flags, art_index, checkpoint, placement_order;
    PortableMechakoopaMotion mechakoopa;
    PortableKamekMotion kamek;
    PortablePierBooMotion pier_boo;
    PortableTeepeeMotion teepee;
    PortableLakeRockMotion lake_rock;
    uint8_t contact_phase; /* 56170/55FC0 umbrella bit68: closed0/open1. */
} PortableCourseProp;
typedef struct
{
    PortableCourseProp actors[PORTABLE_COURSE_PROP_CAPACITY];
    const PortableCourseProp *initial;
    size_t count;
} PortableCourseProps;
typedef uint32_t (*PortableCoursePropRandom)(void *context);

int portable_course_prop_init(PortableCourseProp *a, int32_t x, int32_t y, uint32_t code,
                              uint16_t affine, uint8_t art);
int portable_course_props_init_with_random(PortableCourseProps *course,
                                           const PortableCourseProp *initial, size_t count,
                                           PortableCoursePropRandom random, void *context);
int portable_course_props_init(PortableCourseProps *course, const PortableCourseProp *initial,
                               size_t count);
PortableCourseProp *portable_course_prop_at(PortableCourseProps *course, int32_t world_x,
                                            int32_t world_y);
void portable_course_prop_damage(PortableCourseProp *a);
int portable_kamek_advance(PortableCourseProp *a, const int32_t *sine);
void portable_pier_boo_vanish(PortableCourseProp *a);
unsigned portable_course_prop_view_art(const PortableCourseProp *a, uint16_t camera_heading);
int portable_lake_rock_bounce(PortableCourseProp *a);
int portable_course_props_advance_heard(PortableCourseProps *course, const int32_t *sine,
                                        size_t sine_count,
                                        const PortableCoursePropListener *listener);
int portable_course_props_advance(PortableCourseProps *course, const int32_t *sine,
                                  size_t sine_count);
void portable_teepee_hit(PortableCourseProp *a, uint8_t kart, int16_t component_x,
                         int16_t component_y);
int portable_course_props_tick_teepees(PortableCourseProps *course, PortableTeepeeKart *karts,
                                       size_t kart_count, const int32_t *sine, size_t sine_count,
                                       uint32_t clock);
unsigned portable_teepee_rider_art(const PortableCourseProp *a, uint16_t camera_heading);

#endif
