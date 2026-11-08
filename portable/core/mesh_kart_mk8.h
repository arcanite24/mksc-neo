#ifndef PORTABLE_MESH_KART_MK8_H
#define PORTABLE_MESH_KART_MK8_H
/* MK8 Deluxe engine constants shared by the kart core and the rules resolver
 * (recovered from MK8D v1.0). Per-tick
 * factors are Q16; speeds are Q16 m/s; "limit" values are m/s per tick. */
#include "mesh_kart_rules.h"
#define MESH_KART_MK8_REVERSE_Q16 INT32_C(824060)      /* brake target, 4 units/frame */
#define MESH_KART_MK8_BRAKE_RATE_Q16 INT32_C(918)      /* 0.014 of the gap a tick */
#define MESH_KART_MK8_BRAKE_LIMIT_Q16 INT32_C(51504)   /* 0.25 units/frame^2 */
#define MESH_KART_MK8_DRIFT_RATE_Q16 INT32_C(983)      /* 0.015 while drifting */
#define MESH_KART_MK8_COAST_Q16 INT32_C(64225)         /* 0.98 */
#define MESH_KART_MK8_REVERSE_COAST_Q16 INT32_C(62259) /* 0.95 */
#define MESH_KART_MK8_OVER_CAP_Q16 INT32_C(64225)      /* 0.98 above the cap */
#define MESH_KART_MK8_AIR_Q16 INT32_C(65208)           /* 0.995 once fully airborne */
#define MESH_KART_MK8_AIR_TICKS 20u                    /* air ratio 0.05 a tick reaches 1 */
#define MESH_KART_MK8_STEER_LOSS_Q16 INT32_C(65274)    /* 0.996 at full lock */
#define MESH_KART_MK8_DRIFT_STEER_LOSS_Q16 INT32_C(65405) /* 0.998 drifting */
#define MESH_KART_MK8_ALIGN_MIN_Q16 INT32_C(6554)      /* 0.1 */
#define MESH_KART_MK8_ALIGN_SPEED_Q16 INT32_C(65536)   /* below 1 m/s alignment is 1 */
#define MESH_KART_MK8_TURNS_PER_SECOND_Q16 INT64_C(622980) /* one radian a tick */
#define MESH_KART_MK8_TURN_LOW_Q16 INT32_C(1180)      /* 0.018 rad/tick at 0.5 units/frame */
#define MESH_KART_MK8_DRIFT_TURN_LOW_Q24 INT32_C(335544) /* 0.02 rad/tick drifting */
#define MESH_KART_MK8_UNIT_SPEED_Q16 INT32_C(205892)  /* one unit/frame in m/s */
#define MESH_KART_MK8_DRIFT_BIAS_Q24 INT32_C(251658)    /* 0.015 rad/tick toward the drift side */
#define MESH_KART_MK8_DRIFT_RAMP_Q16 INT32_C(3058)      /* bias ramp +0.0007/0.015 a tick, squared */
#define MESH_KART_MK8_DRIFT_OUTWARD_Q24 INT32_C(301990) /* outward stick row at most 0.018 */
#define MESH_KART_MK8_SIDE_DEAD_ZONE 3277              /* hop picks a side above 0.1 stick */
#define MESH_KART_MK8_INWARD_STICK 23265               /* 0.71: charge 5 into the drift */
#define MESH_KART_MK8_SPIN_DECAY_Q16 INT32_C(64881)   /* spin accumulator ×0.99 */
#define MESH_KART_MK8_SPIN_LIMIT_Q16 INT32_C(1966080) /* ±30 enters a hop-less drift */
#define MESH_KART_MK8_SLIDE_MIN_TICKS 40u
#define MESH_KART_MK8_SLIDE_MAX_TICKS 80u               /* 15 ticks per unit/frame between */
#define MESH_KART_MK8_SLIDE_EXIT_TICKS 5u              /* grip stays loose 5 ticks after a slide */
#define MESH_KART_MK8_SLIDE_SPEED_Q16 INT32_C(26214)   /* entry above 0.4 of top speed */
#define MESH_KART_MK8_SLIDE_YAW_GAIN_Q16 INT32_C(295)  /* 0.0045 rad a tick per unit/frame of slip */
#define MESH_KART_MK8_SLIDE_YAW_LIMIT_Q16 INT32_C(1311) /* at most 0.02 rad a tick */
#define MESH_KART_MK8_FLOOR_CAP_Q16 INT32_C(1564779)   /* dash/mushroom cap base: at least 7.6 units/frame */
#define MESH_KART_MK8_ROCKET_CAP_Q16 INT32_C(72090)    /* rocket start ×1.1 top speed */
#define MESH_KART_MK8_ROCKET_ACCEL_Q16 INT32_C(1229742) /* 0.1 units/frame² as m/s² */
#define MESH_KART_MK8_ROCKET_GAIN_Q16 INT32_C(1114)    /* held: c += 0.017 − 0.015c */
#define MESH_KART_MK8_ROCKET_LOSS_Q16 INT32_C(983)
#define MESH_KART_MK8_ROCKET_DECAY_Q16 INT32_C(62915)  /* released: c ×0.96 */
#define MESH_KART_MK8_BURNOUT_Q16 INT32_C(62259)       /* above 0.95 */
#define MESH_KART_MK8_BURNOUT_TICKS 60u                 /* inferred: one second without throttle */
/* MK8 surface classes (0x1791cc): SURF `mksc:surface-class`, or 0 to derive
 * from the row (off-road by speed ratio, slippery by friction). Slippery and
 * ice take a per-surface level 0 mild .. 2 harsh. */
enum
{
    MESH_KART_MK8_CLASS_AUTO,
    MESH_KART_MK8_CLASS_ROAD,
    MESH_KART_MK8_CLASS_DIRT,
    MESH_KART_MK8_CLASS_GRASS,
    MESH_KART_MK8_CLASS_HEAVY,
    MESH_KART_MK8_CLASS_SLIPPERY, /* + level */
    MESH_KART_MK8_CLASS_ICE = MESH_KART_MK8_CLASS_SLIPPERY + 3, /* + level */
    MESH_KART_MK8_CLASSES = MESH_KART_MK8_CLASS_ICE + 3
};
#define MESH_KART_MK8_OFFROAD_SLIDE_Q16 INT32_C(62259)  /* off-road slide target 0.95 */
#define MESH_KART_MK8_SLIPPERY_X_Q16 INT32_C(64291)     /* slippery speed 0.981 at row 0 */
#define MESH_KART_MK8_ICE_X_Q16 INT32_C(63635)          /* ice speed 0.971 at row 0 */
#define MESH_KART_MK8_SLIPPERY_X_SPAN_Q16 INT32_C(1245) /* +0.019 by row 19 */
#define MESH_KART_MK8_TRACTION_STEP_Q16 INT32_C(328)    /* traction eases 0.005 a tick */
#define MESH_KART_MK8_TRACTION_CAP_Q16 INT32_C(64881)   /* slip kept at most 0.99 a tick */
#define MESH_KART_MK8_DRIFT_SLIDE_Q16 INT32_C(1311)     /* drifting adds 0.02 */
#define MESH_KART_MK8_SLIDE_ADD_Q16 INT32_C(16056)      /* brake slide, bumps and landings add 0.245 */
#define MESH_KART_MK8_BURNOUT_SLIDE_Q16 INT32_C(14418)  /* burnout adds 0.22 */
/* MK8 walls and bumps (collisions.md). Knocks are units/frame scaled to
 * m/s; a knock holds traction raised for its frames. */
#define MESH_KART_MK8_WALL_KEEP_Q16 INT32_C(58982)     /* grounded speed x(1-c)*0.9 */
#define MESH_KART_MK8_WALL_AIR_Q16 INT32_C(19661)      /* airborne 0.3 + 0.69*sin */
#define MESH_KART_MK8_WALL_AIR_SPAN_Q16 INT32_C(45220)
#define MESH_KART_MK8_WALL_KNOCK_TICKS 14u
#define MESH_KART_MK8_WALL_COOLDOWN 34u                 /* effect cooldown 1.0 at 0.03 a tick */
#define MESH_KART_MK8_WALL_EVENT_Q16 INT32_C(-32768)    /* event below -0.5 units/frame */
#define MESH_KART_MK8_WALL_KICK_DECAY_Q16 INT32_C(5243) /* heading kick eases 8% a tick */
#define MESH_KART_MK8_BUMP_DELAY 3u
#define MESH_KART_MK8_BUMP_TICKS 12u
#define MESH_KART_MK8_BUMP_PUSH_Q16 INT32_C(183501)     /* 2.8 units/frame */
#define MESH_KART_MK8_BUMP_PUSH_LIMIT_Q16 INT32_C(327680) /* 5 units/frame */
#define MESH_KART_MK8_HAZARD_TICKS 5u
/* MK8 slipstream (boosts.md). */
#define MESH_KART_MK8_UNIT_Q16 INT32_C(3449)            /* one MK8 unit in metres */
#define MESH_KART_MK8_SLIP_GRACE 15u
#define MESH_KART_MK8_SLIP_CHARGE 90u                   /* fires on the 91st charging tick */
#define MESH_KART_MK8_SLIP_TICKS 135u
#define MESH_KART_MK8_SLIP_CAP_Q16 INT32_C(78643)       /* 1.2 x top speed */
#define MESH_KART_MK8_SLIP_ACCEL_Q16 INT32_C(491897)    /* 0.04 units/frame^2 as m/s^2 */
#define MESH_KART_MK8_OFFROAD_TICKS 12u                 /* off-road meter 0.09 a tick reaches 1 */
/* MK8 tricks (surfaces-tricks.md 9-13). */
#define MESH_KART_MK8_TRICK_SPEED_Q16 INT32_C(42598)   /* hop at 0.65 of top speed or more */
#define MESH_KART_MK8_TRICK_AIR_TICKS 7u                /* an airborne hop still counts this early */
#define MESH_KART_MK8_TRICK_LAND_TICKS 20u              /* landing sooner earns no boost */
/* MK8 anti-gravity (antigravity.md 2-4). */
#define MESH_KART_MK8_ANTIGRAVITY_RAMP 25u               /* presentation rate 0.04 a tick */
#define MESH_KART_MK8_AIR_UP_TICKS 11u                   /* then up relaxes toward gravity */
#define MESH_KART_MK8_AIR_UP_Q16 INT32_C(2621)           /* 0.04 a tick */
#define MESH_KART_MK8_SPIN_SPEED_Q16 INT32_C(13107)      /* bumps spin above 0.2 of top speed */
#define MESH_KART_MK8_SPIN_FAST_Q16 INT32_C(39322)       /* 0.6: the 40-tick spin */
#define MESH_KART_MK8_SPIN_TICKS_SLOW 50u                /* timer step 0.02 */
#define MESH_KART_MK8_SPIN_TICKS_FAST 40u                /* 0.025 */
#define MESH_KART_MK8_SPIN_TICKS_BOOST 34u               /* 0.03 while boosting */
#define MESH_KART_MK8_SPIN_BOOST_TICKS 20u               /* slot 2, rocket cap and thrust */
#define MESH_KART_MK8_BUMP_PUSH_ANTI_Q16 INT32_C(163840) /* 2.5 units/frame */
#define MESH_KART_MK8_BUMPER_SCALE_Q16 INT32_C(19661)    /* object impulse s0 0.3 */
#define MESH_KART_MK8_BUMPER_TICKS 18u
/* MK8 glider (0x171e4c..0x173e2c); speeds in units/frame Q16. */
#define MESH_KART_MK8_GLIDE_AIR_TICKS 15u        /* airborne ticks before a glide opens */
#define MESH_KART_MK8_GLIDE_CANNON_AIR_TICKS 5u
#define MESH_KART_MK8_GLIDE_ALIGN_Q30 INT32_C(214748365)    /* facing 0.2 along the path */
#define MESH_KART_MK8_GLIDE_GRACE 20u                       /* no landing in the first ticks */
#define MESH_KART_MK8_GLIDE_OPEN_Q16 INT32_C(72090)         /* 1.1 lift on opening */
#define MESH_KART_MK8_GLIDE_MIN_Q16 INT32_C(393216)         /* launch at 6, 10 on a cannon */
#define MESH_KART_MK8_GLIDE_CANNON_MIN_Q16 INT32_C(655360)
#define MESH_KART_MK8_GLIDE_CANNON_TOP_Q16 INT32_C(1638400) /* 25 */
#define MESH_KART_MK8_GLIDE_YAW_Q16 INT32_C(32768)          /* PRTG1: 0.5 of the ground yaw law */
#define MESH_KART_MK8_GLIDE_BANK_Q16 INT32_C(13107)         /* PRTG0 0.2 */
#define MESH_KART_MK8_GLIDE_LIFT_Q30 INT32_C(322123)        /* 0.0003 v^2 */
#define MESH_KART_MK8_GLIDE_DIVE_Q30 INT32_C(3221225)       /* 0.003 */
#define MESH_KART_MK8_GLIDE_GRAVITY_Q16 INT32_C(492)        /* 0.0075 along the path's up */
#define MESH_KART_MK8_GLIDE_LAND_Q16 INT32_C(19661)         /* contact past 0.3 ends the glide */
#define MESH_KART_MK8_GLIDE_TUBE_U 100                      /* units outside the radius for one tube */
#define MESH_KART_MK8_CHARGE_NEUTRAL 2u
#define MESH_KART_MK8_CHARGE_INWARD 5u

/* MK8's accelerate/brake core: v *= sign; below target it closes
 * f*min(rate*(target-v), limit)*m of the gap, at or above it v *= m. */
static inline int32_t portable_mesh_kart_mk8_drive(int32_t v, int32_t target, int32_t rate,
                                                   int32_t limit, int32_t m, int32_t f, int sign)
{
    int64_t x = (int64_t)v * sign;
    if (x < target)
    {
        int64_t step = (target - x) * rate / 65536;
        if (step > limit)
            step = limit;
        x += step * f / 65536 * m / 65536;
    }
    else
        x = x * m / 65536;
    return (int32_t)(x * sign);
}
int portable_mesh_kart_rules_resolve_unchecked(const PortableMeshKartProfile *profile, unsigned engine_class,
                                               const int32_t levels_q16[PORTABLE_MESH_KART_STATS],
                                               PortableMeshKartRules *out);
#endif
