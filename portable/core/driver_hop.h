#ifndef MKSC_PORTABLE_DRIVER_HOP_H
#define MKSC_PORTABLE_DRIVER_HOP_H

#include <stdint.h>
#include <stddef.h>

enum
{
    PORTABLE_DRIVER_HOP_OK = 0,
    PORTABLE_DRIVER_HOP_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE = 2,
};

/* C4C0/C1BC after B550: source +76 saturates at15 only when entered
 * at0..14. Larger retained halfwords are preserved, not clamped. */
static inline void portable_driver_sink_prefix(uint8_t mode,uint16_t *counter,
    int32_t *height_q16,uint16_t *position82)
{
    if(mode!=2u){*counter=0u;return;}
    if(*counter<=14u)++*counter;
    *height_q16=-(int32_t)*counter*INT32_C(32768);
    *position82=(uint16_t)((uint32_t)*counter*2048u);
}
/* D818: visibility uses the post-prefix sink owner, including the retrieval
 * transition which retains terminal76 until the following prefix. */
static inline int portable_driver_hidden(uint8_t mode,int32_t height_q16,
    uint16_t sink_counter,uint16_t timer9c)
{
    return ((mode==3u||mode==4u)&&height_q16==0) || sink_counter==15u ||
        (mode==8u&&timer9c<=60u);
}

typedef struct
{
    int32_t height_q16;       /* original +0x08 */
    uint32_t state_flags;     /* original +0x48 */
    int16_t velocity_q8;      /* original +0x52 */
    uint8_t mode;             /* original +0x78 */
    uint16_t animation_flags; /* original +0xb4 */
    uint8_t surface_jump_started; /* one-turn ground ramp launch */
    uint8_t protected_hop_started; /* one-turn9AA4 audio event */
    uint8_t landed;           /* event from A58C's ground transition */
    uint8_t recovery_started; /* 9DAC's grounded low-speed effect event */
    uint16_t recovery_requests; /* +0xa4, consumed by the next51014 pass */
    uint8_t retrieval_started; /* one-turn99A4 reset event */
    uint8_t recovery_checkpoint_sample; /* A8EE samples after an unsnapped Y carry step */
    uint8_t recovery_touchdown; /* A904 ground transition, distinct from hop landing */
    uint8_t lakitu_ticks;      /* F980 rescue sprite descent after request400 */
    uint8_t lakitu_arrived;    /* one-turn F9E4 y=88 arrival (D214(12) on icy themes) */
} PortableDriverHopState;

typedef struct
{
    uint16_t charge_counter; /* original +0x9c */
} PortableDriverDriftChargeState;

/* Qualified normal-ground branch of sub_8039B38. The source input word uses
 * bit 0x0004 for the R-button rising edge and bit 0x0010 while R is held. */
int portable_driver_hop_tick(PortableDriverHopState *state,
                             uint16_t source_control_word);

/* A118/A21C/A3A0/A448 ground jump surfaces, dispatched after motion.
 * A58C landing cannot launch again until the following grounded turn.
 * A zero ramp cap means unrestricted; nonzero is a content-owned policy. */
int portable_driver_surface_jump(PortableDriverHopState *state,
    uint8_t mode_at_entry, uint8_t material, int32_t *speed_q16,
    int32_t ramp_speed_limit_q16, const int32_t *sine, size_t sine_count);

/* 9AA4: protected material28 uses the retained terrain selector. Thresholds
 * are six material-data entries for selectors10..15. Driver84 is unchanged. */
int portable_driver_protected_surface_hop(PortableDriverHopState *state,
    uint8_t mode_at_entry,uint8_t material,uint8_t selector,int32_t *speed_q16,
    const uint16_t *thresholds,size_t threshold_count);

/* 9DAC/9904 ground material22 response, after8200 creates components.
 * A58C already owns mode1; a landing must not dispatch mode4 in that turn.
 * A6F4 sets request400 when the decremented timer reaches60. Incoming zero
 * calls99A4; A6C4's prior-height threshold enters mode6 on the next driver
 * turn after the lift reaches70000. Driver84 remains separately owned. */
int portable_driver_terrain22_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, uint8_t material_flags, int32_t *speed_q16, uint16_t *timer_9c,
    const int32_t *sine, size_t sine_count);

/* 39C9E/39D42 void entry and A630 fall hold. The timer's zero test
 * precedes its decrement; carry starts on the turn after it reaches zero. */
int portable_driver_fall_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, int32_t *speed_q16, uint16_t *timer_9c,
    uint16_t *contact34);

/* A94C mode9: E7F0 has already selected the arena start and installed the
 * 60-tick hold. Incoming zero lifts to48 units and enters shared carry6/7. */
int portable_driver_bomb_respawn_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, uint16_t *timer_9c);

/* 39F74/99A4 lava entry, followed by A6C4. No void hold timer. */
int portable_driver_lava_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, int32_t *speed_q16, uint16_t *flags74);

/* 39FD8 bounds entry; A6C4 waits for the post-driver Lakitu lift. */
int portable_driver_bounds_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, int32_t *speed_q16);

/* 9B38 material 0x2A: sub_8040A44 takes the first actor of the course
 * object list (+0x718, linked through +0x3c) whose tile lies within four
 * 8-unit tiles of the kart. Only a collidable (+0x34 bit 2) actor captures;
 * no actor in range leaves 0x2A as plain ground. Course data places these
 * cells around Piranha Plant pipes. */
typedef struct
{
    int32_t x_q16, y_q16;
    uint16_t flags_34;
} PortableDriverCaptureActor;

/* Source driver fields owned by capture entry and hop mode 8. */
typedef struct
{
    int32_t x_q16, y_q16;                /* +0x00/+0x04, read only */
    int32_t height_q16;                  /* +0x08 */
    uint32_t state_flags;                /* +0x48 */
    int16_t component_x, component_y;    /* +0x4e/+0x50 */
    int16_t velocity_q8;                 /* +0x52 */
    uint16_t timer_62;                   /* B550 motion hold */
    uint8_t mode;                        /* +0x78 */
    uint8_t response_status_7e;
    uint16_t hit_angle_80;
    uint16_t spin_82;
    uint8_t driver_mode_84;
    uint16_t response_88;
    uint16_t timer_9c;
    uint16_t animation_b4, spin_rate_b6;
    int32_t speed_q16, acceleration_q16; /* +0xbc/+0xc0 */
    uint8_t captured, launched;          /* one-turn events */
} PortableDriverCaptureState;

#define PORTABLE_DRIVER_CAPTURE_MATERIAL UINT8_C(0x2a)
#define PORTABLE_DRIVER_CAPTURE_MODE 8u
#define PORTABLE_DRIVER_CAPTURE_SWALLOW_SONG 162u /* 8040A44, unless playing */
#define PORTABLE_DRIVER_CAPTURE_LAUNCH_SONG 381u  /* 8040C3C, player 6 */

/* sub_8040A44, from ground mode on material 0x2A. Gates +0x58 bit 0x20,
 * +0x6a and +0x66 precede the search; returns OK with captured clear when
 * nothing qualifies. Entry sets a 92-turn timer and a pull of 1/32 of the
 * remaining distance per turn while B550 holds 8200. */
int portable_driver_capture_begin(PortableDriverCaptureState *state,
    uint16_t cue_flags_58, int16_t protection_6a, int16_t guard_66,
    const PortableDriverCaptureActor *actors, size_t actor_count);

/* sub_8040C3C for entry mode 8: spin (+0x82 += 0xC00) above timer 59, then
 * stop and wait hidden; at zero clear the pull and launch as hop mode 1 with
 * +0x52=0x2F0, height 0x100, speed 0x1F00000 and response status 12. The
 * caller sets +0x56/+0x7a/+0x7c to the course direction on launch. */
int portable_driver_capture_tick(PortableDriverCaptureState *state);

/* 8E34 case 12, the launched kart's airborne flip: +0x82 turns 0x800 per
 * turn (forward with +0xb4 bit 0x2000, backward otherwise) and settles to
 * status 14 at the first revolution boundary while descending, or at once
 * without +0xb4 bit 0x8000. */
int portable_driver_launch_flip_tick(uint8_t *response_status_7e,
    uint16_t *animation_b4, uint16_t *spin_82, int16_t velocity_q8);

/* A800/39894 early relocation:320 heading step,512 snap zone, X-first
 * three-unit movement. Later mode7 arrival is explicitly unsupported. */
int portable_driver_recovery_relocate(int32_t *x_q16, int32_t *y_q16,
    uint16_t *heading, int16_t target_x, int16_t target_y,
    uint16_t target_heading);

/* A800 arrival and A904 descent. flags74/contact34 are original halfwords,
 * separate from flags48. Heading snap returns no descent on that same turn. */
int portable_driver_recovery_tick(PortableDriverHopState *state,
    int32_t *x_q16, int32_t *y_q16, uint16_t *heading,
    uint16_t *flags74, uint16_t *contact34,
    int16_t target_x, int16_t target_y, uint16_t target_heading);

/* The normal driver path observes two control boundaries per grounded frame:
 * sub_8039B38 sees the previously stored +0x96 word, then sub_8038298 sees the
 * current word. Airborne frames skip the first boundary. */
int portable_driver_drift_charge_pre_tick(
    PortableDriverDriftChargeState *state,
    uint16_t prior_source_control_word,
    uint8_t hop_mode_at_entry);

int portable_driver_drift_charge_post_tick(
    PortableDriverDriftChargeState *state,
    uint16_t current_source_control_word,
    int32_t speed_q16,
    int32_t speed_target_q16,
    uint16_t *animation_flags);

#endif
