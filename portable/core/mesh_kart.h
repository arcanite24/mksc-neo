#ifndef PORTABLE_MESH_KART_H
#define PORTABLE_MESH_KART_H
#include "mesh_kart_rules.h"
#include <stdint.h>
#define PORTABLE_MESH_ABSENT UINT32_MAX
/* Positions denote the collision sphere center, in metres Q16. */
typedef struct
{
    int32_t position_q16[3], velocity_q16[3], forward_q30[3], up_q30[3];
    int32_t steer_filtered_q16, speed_q16, outside_drift_q16;
    uint32_t drift_charge, boost_ticks[3], panel_ticks, air_ticks, respawn_ticks;
    uint32_t contact_triangle, contact_surface, respawn_index;
    uint16_t previous_buttons;
    int8_t drift_direction;
    uint8_t drift_tier, grounded, last_boost_tier;
    /* MK8 handling: the drift bias ramp (Q16 of 1), the spin-turn
     * accumulator that starts a hop-less drift at ±30, and the brake
     * slide's remaining ticks. */
    int32_t drift_ramp_q16, spin_q16;
    uint8_t slide_ticks;
    /* MK8 rocket start: countdown charge (Q16 of 1), then the slot-7 boost or
     * a burnout that holds the throttle. */
    int32_t rocket_charge_q16;
    uint16_t rocket_ticks;
    uint8_t burnout_ticks;
    /* MK8 traction T (Q16), easing toward the surface's slide target, and
     * the ticks a landing holds it raised. */
    int32_t traction_q16;
    uint8_t traction_hold_ticks;
    /* MK8 knock velocity (m/s Q16): walls, bumps and hazards push through it
     * on top of the drive; it decays with traction and stays horizontal. A
     * bump's push waits bump_delay ticks in bump_push; a wall hit kicks the
     * heading (radians a tick, Q24) and its effects wait out wall_cooldown. */
    int32_t knock_q16[3], bump_push_q16[3], wall_yaw_q24;
    uint8_t bump_delay, wall_cooldown;
    /* MK8 slipstream: slip_in is set between ticks by the race when the kart
     * sits in another's strip; the grace bridges the strip's gaps, 91
     * charging ticks fire the 135-tick slot-8 boost, and 12 off-road ticks
     * cancel it. */
    uint8_t slip_in, slip_grace, slip_charge, slip_ticks, offroad_ticks;
    /* MK8 tricks: the last floor's trick-ramp flag (kept in the air), the
     * running trick (1 neutral, 2 sideways; signed by stick), ticks since it
     * started (0 never, saturating), and the slot-6 boost a late landing earns. */
    uint8_t trick_surface, trick_age, trick_boost_ticks;
    int8_t trick_kind;
    /* MK8 anti-gravity: the race sets path_up (the gravity path's up; zero
     * none) and anti_gravity between ticks, which pick the anti-gravity speed
     * and handling rows; anti_gravity_rate ramps the 25-tick presentation. A
     * bump starts spin_ticks of spin_length (34..50); an armed spin, only in
     * anti-gravity, turns the model a full spin_sign turn and ends in the
     * 20-tick slot-2 spin boost. */
    int32_t path_up_q30[3];
    uint8_t anti_gravity, anti_gravity_rate, spin_ticks, spin_length, spin_armed, spin_boost_ticks;
    int8_t spin_sign;
    /* MK8 glider: the race sets glide_path (the nearest glide-path point)
     * before each tick. Standing on a glide ramp arms glide (1); 15 airborne
     * ticks (5 on a cannon path) facing the path open it (2). While gliding,
     * heading is the flight direction, glide_ticks count up, land
     * accumulates contact (Q16) and bank (spring position and velocity),
     * roll (smoothed bank, for presentation) and pitch (smoothed dive +,
     * pull-up -) are Q16. */
    struct
    {
        int32_t closest_q16[3], tangent_q30[3], up_q30[3], radius_q16;
        uint8_t found, cannon;
    } glide_path;
    int32_t glide_heading_q30[3];
    int32_t glide_land_q16, glide_bank_q16, glide_bank_velocity_q16, glide_roll_q16, glide_pitch_q16;
    uint16_t glide_ticks;
    uint8_t glide;
    /* Cannon flight: tick cannon_tick of cannon_ticks (0: not flying) from
     * cannon_from to surface cannon_surface's target; collision is off. */
    uint32_t cannon_surface;
    uint16_t cannon_tick, cannon_ticks;
    int32_t cannon_from_q16[3];
    /* Up for gravity and floor tests: zero is world +Z; on surface-gravity
     * roads (loops) it is the last floor's face normal. */
    int32_t gravity_up_q30[3];
} PortableMeshKartState;
/* Immutable per-turn dynamics; item ownership/timers never live in core.
 * Explicit identity uses65536 for speed/body/boost-cap and zero flags/accel.
 * cancel_drift also clears MT/panel clocks and suppresses this turn's boosts.
 * inhibit_controls masks driving/hop only, retaining raw button history. */
typedef struct
{
    /* boost_active holds the mushroom slot, whose cap ratio (of the larger of
     * top speed and 7.6 units/frame) and acceleration follow; star replaces
     * acceleration with its own and scales top speed. */
    int32_t speed_ratio_q16, boost_cap_ratio_q16, boost_acceleration_q16, body_scale_q16;
    int32_t star_cap_ratio_q16, star_acceleration_q16;
    uint8_t boost_active, cancel_drift, inhibit_controls, star;
} PortableMeshKartEffects;

typedef struct
{
    int16_t steering;
    uint16_t buttons;
} PortableMeshKartInput;
enum
{
    PORTABLE_MESH_ACCELERATE = 1,
    PORTABLE_MESH_BRAKE = 2,
    PORTABLE_MESH_HOP = 4,
    PORTABLE_MESH_RESCUE = 8
};
typedef struct
{
    int32_t vertices_q16[3][3];
    uint32_t surface, respawn;
} PortableMeshTriangle;
typedef struct
{
    uint32_t type;
    int32_t friction_q16, speed_ratio_q16, rolling_resistance_q16, restitution_q16;
    int32_t boost_q16, launch_q16, gravity_ratio_q16;
    int32_t jump_max_distance_q16; /*0 absent; optional launch ceiling in metres. */
    /* Type 9 cannon triggers only: landing point (floor, metres) and exit
     * heading; launch_q16 is the flight speed. */
    int32_t cannon_target_q16[3], cannon_heading_q16[3];
    uint8_t one_sided; /* karts pass through the face from behind */
    uint8_t surface_gravity; /* karts on it fall toward the face (loops) */
    uint8_t surface_class;   /* MESH_KART_MK8_CLASS_*: 0 derives it from the row */
    uint8_t trick;           /* trick ramp: 0 derives it (jump surfaces), 1 yes, 2 no */
    uint8_t glide;           /* glide ramp: leaving it over a glide path opens the glider */
} PortableMeshSurface;
typedef struct PortableMeshCourse
{
    const void *context;
    uint32_t triangle_count;
    int32_t gravity_q16;
    int (*triangle)(const void *, uint32_t, PortableMeshTriangle *);
    int (*surface)(const void *, uint32_t, PortableMeshSurface *);
    /* Optional immutable, fully prevalidated index. Return the first candidate
     * ID >=first overlapping inclusive bounds: -1 invalid, 0 none, 1 found.
     * IDs remain canonical ascending table IDs; NULL preserves full traversal. */
    int (*next_triangle)(const void *context, const int32_t low_q16[3], const int32_t high_q16[3],
                         uint32_t first, uint32_t *out);
    /* Optional moving collision: moves a kart grounded on `triangle` with it
     * from the previous tick (static rows leave it alone); 0 invalid. */
    int (*carry)(const void *context, uint32_t triangle, int32_t position_q16[3], int32_t forward_q30[3],
                 int32_t velocity_q16[3]);
} PortableMeshCourse;
typedef enum
{
    PORTABLE_MESH_OK = 0,
    PORTABLE_MESH_INVALID,
    PORTABLE_MESH_UNSUPPORTED,
    PORTABLE_MESH_COLLISION_BUDGET,
    PORTABLE_MESH_OUTSIDE
} PortableMeshStatus;
typedef struct
{
    uint8_t hopped, landed, tier_changed, released, panel, wall, cannon, hazard, tricked, trick_boost, spin_boost,
        glided, glide_end;
    uint8_t hazard_reaction; /* PORTABLE_MESH_HAZARD_* of the struck hazard */
} PortableMeshKartEvents;
/* How a hazard reacts on contact; every kind but the spin bumper knocks the
 * kart out first. Spin, tumble and squash also start the race's hit status
 * where the course races with items. */
enum
{
    PORTABLE_MESH_HAZARD_BUMP,
    PORTABLE_MESH_HAZARD_SPIN_BUMPER,
    PORTABLE_MESH_HAZARD_SPIN,   /* spun out (a Goomba, a crab, a fireball) */
    PORTABLE_MESH_HAZARD_TUMBLE, /* thrown up and flipped (a car, a Chain Chomp) */
    PORTABLE_MESH_HAZARD_SQUASH, /* flattened and stopped (a Thwomp) */
    PORTABLE_MESH_HAZARD_REACTIONS
};
/* Engine speed is measured along travel, including a retained outside-drift
 * angle while it recovers. Zero angle copies forward exactly. Pure helpers;
 * invalid vectors/angles/velocity or scalar outside +/-256m/s preserve output. */
PortableMeshStatus portable_mesh_kart_travel_direction(const PortableMeshKartState *state,
                                                       int32_t out_q30[3]);
PortableMeshStatus portable_mesh_kart_project_travel_speed(const PortableMeshKartState *state,
                                                           int32_t *out_q16);
/* MK8 ground turn rate at full stick for a signed speed, in turns a second Q16. */
int32_t portable_mesh_kart_turn_rate(const PortableMeshKartRules *r, int32_t speed_q16);
/* Radius (Q16 m) of the tightest full-lock ground arc: below half a unit a
 * frame the rate falls with speed, so slowing further never tightens it. */
int32_t portable_mesh_kart_min_turn_radius(const PortableMeshKartRules *r);
/* One countdown tick of MK8's rocket-start charge, then GO: tiers 1..5 start
 * the rocket boost, a charge above 0.95 burns out. */
void portable_mesh_kart_countdown(PortableMeshKartState *state, uint16_t buttons);
void portable_mesh_kart_go(PortableMeshKartState *state);
/* Rocket tier for a charge: 0 none, 1..5, or 6 burnout. */
unsigned portable_mesh_kart_rocket_tier(int32_t charge_q16);
/* Final countdown ticks a held accelerator needs to land in tier 1..5, and
 * the buttons that land it with countdown_ticks still to run before this turn. */
unsigned portable_mesh_kart_rocket_hold_ticks(unsigned tier);
uint16_t portable_mesh_kart_rocket_buttons(uint32_t countdown_ticks, unsigned tier);
int portable_mesh_kart_state_valid(const PortableMeshKartState *state);
/* Any kart-owned boost slot runs (MT, panel, rocket start, slipstream). */
int portable_mesh_kart_boosting(const PortableMeshKartState *state);
/* The model's spin, in turns Q16 (-65536..65536): a full turn eased over an
 * armed spin's timer, zero otherwise. Presentation only. */
/* Gravity's up for presentation (cameras): a surface-gravity floor's face,
 * else the gravity path's up, else world up. */
void portable_mesh_kart_gravity_up(const PortableMeshKartState *state, int32_t up_q30[3]);
int32_t portable_mesh_kart_spin_turn(const PortableMeshKartState *state);
/* MK8 GravityPath (0x7b839c): a kart at `position` on segment from->to
 * projects onto it at t in [0,1] within the lerped radius; up is the lerped,
 * normalized ups. 1 on the segment (distance squared in m^2 Q32 set), 0 off. */
typedef struct
{
    int32_t position_q16[3], up_q30[3], radius_q16;
} PortableMeshGravityPoint;
int portable_mesh_gravity_segment(const PortableMeshGravityPoint *from, const PortableMeshGravityPoint *to,
                                  const int32_t position_q16[3], uint64_t *distance_squared,
                                  int32_t up_q30[3]);
/* MK8 GlidePath follower (0x1892c4): the closest point of segment from->to
 * to `position` (clamped to the ends), the segment's tangent and its lerped
 * up and radius. 1 set (distance squared in m^2 Q32), 0 degenerate. */
int portable_mesh_glide_segment(const PortableMeshGravityPoint *from, const PortableMeshGravityPoint *to,
                                const int32_t position_q16[3], uint64_t *distance_squared,
                                int32_t closest_q16[3], int32_t tangent_q30[3], int32_t up_q30[3],
                                int32_t *radius_q16);
/* MK8 slipstream sensing (0x12eee4/0x12f950): a leader faster than half its
 * top speed drops box `box` (0..4) of its strip 15+12*box units behind it,
 * half-size max(radius, 4 units)*1.2^box; a follower faster than half its
 * own top speed drafts when its sensor (half its length plus 4 units)
 * overlaps it. 1 drafting, 0 not; the race decides who may emit. */
int portable_mesh_kart_drafting(const PortableMeshKartState *follower, const PortableMeshKartRules *follower_rules,
                                const PortableMeshKartState *leader, const PortableMeshKartRules *leader_rules,
                                unsigned box);
PortableMeshStatus portable_mesh_kart_init(PortableMeshKartState *state,
                                           const PortableMeshKartRules *rules,
                                           const int32_t position_q16[3],
                                           const int32_t forward_q30[3]);
PortableMeshStatus portable_mesh_kart_tick(PortableMeshKartState *state,
                                           const PortableMeshKartRules *rules,
                                           const PortableMeshCourse *course,
                                           PortableMeshKartInput input,
                                           PortableMeshKartEvents *events);
/* A hazard sphere (an enemy, a swinging pendulum) knocks a kart touching it
 * out of the sphere: MK8's object impulse knocks it 6 m/s outward for 5
 * ticks, and it loses half its speed and its drift. 1 struck, 0 clear,
 * -1 invalid. Karts in cannon flight pass through. */
int portable_mesh_kart_hazard(PortableMeshKartState *state, const PortableMeshKartRules *rules,
                              const int32_t center_q16[3], int32_t radius_q16);
/* After a struck portable_mesh_kart_hazard: a tumble throws the kart up at
 * 4 m/s along its up axis; a squash stops it where it stands. Other kinds
 * leave the knock alone. 0 done, -1 invalid. */
int portable_mesh_kart_hazard_reaction(PortableMeshKartState *state, unsigned reaction);
/* A spin bumper (MK8 reaction 23, 0x17c1c0) pushes a kart out of its sphere;
 * one driving into it with the spin timer idle gets MK8's object impulse
 * (0.3 of its closing speed, 2..6 units/frame, 18 ticks) and starts the spin
 * timer, armed in anti-gravity. 1 struck, 0 clear, -1 invalid. */
int portable_mesh_kart_spin_bumper(PortableMeshKartState *state, const PortableMeshKartRules *rules,
                                   const int32_t center_q16[3], int32_t radius_q16);
PortableMeshStatus portable_mesh_karts_contact(PortableMeshKartState *a,
                                               const PortableMeshKartRules *ar,
                                               PortableMeshKartState *b,
                                               const PortableMeshKartRules *br);
/* NULL preserves the authored default path. Ratios/body are .05..1,
 * boost cap1..4, acceleration0..1024m/s2; new speed products must fit256m/s. */
int portable_mesh_kart_effects_valid(const PortableMeshKartRules *rules,
                                      const PortableMeshKartEffects *effects);
PortableMeshStatus portable_mesh_kart_tick_effects(PortableMeshKartState *state,
    const PortableMeshKartRules *rules, const PortableMeshCourse *course,
    PortableMeshKartInput input, const PortableMeshKartEffects *effects,
    PortableMeshKartEvents *events);
PortableMeshStatus portable_mesh_karts_contact_effects(PortableMeshKartState *a,
    const PortableMeshKartRules *ar, const PortableMeshKartEffects *ae,
    PortableMeshKartState *b, const PortableMeshKartRules *br, const PortableMeshKartEffects *be);
const char *portable_mesh_status_name(PortableMeshStatus status);
#endif
