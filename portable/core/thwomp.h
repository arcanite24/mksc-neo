#ifndef PORTABLE_THWOMP_H
#define PORTABLE_THWOMP_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#define PORTABLE_THWOMP_CAPACITY 64u

/* Class81: constructor 53EF4, updates 540A0/5442C/54888/54D50/551D0/5563C. */
typedef struct {
    int32_t x_q16, y_q16, height_q16;
    int32_t velocity_x_q16, velocity_y_q16, velocity_z_q16;
    int32_t timer;
    uint8_t variant, state, flags, checkpoint;
    uint8_t placement_order;
} PortableThwomp;

typedef struct {
    PortableThwomp actors[PORTABLE_THWOMP_CAPACITY];
    const PortableThwomp *initial;
    size_t count;
} PortableThwompCourse;

static inline int32_t portable_thwomp_signed(uint32_t bits)
{
    return bits <= INT32_MAX ? (int32_t)bits : -1-(int32_t)(UINT32_MAX-bits);
}

static inline int portable_thwomp_init(PortableThwomp *actor,
    int32_t x, int32_t y, uint8_t checkpoint, unsigned variant)
{
    if (!actor || variant > 11u || checkpoint >= 128u || x < 0 || y < 0 ||
        x > 2048*65536 || y > 2048*65536) return 0;
    *actor = (PortableThwomp){.x_q16=x, .y_q16=y, .variant=(uint8_t)variant,
        .checkpoint=checkpoint, .flags=3u};
    if (variant <= 3u) actor->timer = 1+(int32_t)variant*32;
    else if (variant <= 5u) {
        actor->state = variant == 4u ? 0u : 16u;
        actor->velocity_x_q16 = variant == 4u ? -0x2d555 : 0x2d555;
        actor->velocity_y_q16 = -actor->velocity_x_q16;
    } else if (variant <= 9u) {
        actor->timer = variant == 6u ? 32 : variant == 7u ? 56 : variant == 9u ? 80 : 0;
        actor->state = variant == 7u ? 18u : variant == 9u ? 36u : 0u;
        actor->velocity_x_q16 = variant <= 7u ? -0x20000 : 0x20000;
        actor->velocity_y_q16 = -actor->velocity_x_q16;
    } else {
        actor->state = variant == 10u ? 9u : 36u;
        actor->velocity_x_q16 = -0x1aaaa;
    }
    return 1;
}

static inline unsigned portable_thwomp_normal_states(unsigned variant)
{
    return variant <= 3u ? 7u : variant <= 5u ? 32u :
        variant == 6u || variant == 9u || variant == 11u ? 54u : 36u;
}

static inline int portable_thwomp_rumble(int32_t timer)
{
    return (timer >= 4 && timer <= 7) || (timer >= 12 && timer <= 15) ||
        (timer >= 22 && timer <= 27);
}

/* Returns a landing event separately from success, for presentation consumers. */
static inline int portable_thwomp_advance(PortableThwomp *actor, int *landed)
{
    if (!actor || !landed || actor->variant > 11u) return 0;
    *landed = 0;
    PortableThwomp next = *actor;
    const unsigned normal = portable_thwomp_normal_states(next.variant);
    const unsigned flight = next.variant <= 3u ? 7u : normal+1u;
    if (next.state > flight) return 0;
    if (next.variant <= 3u)
        next.timer = portable_thwomp_signed((uint32_t)next.timer-1u);
    if (next.state == flight) {
        if (next.flags & 1u) {
            next.x_q16 = portable_thwomp_signed((uint32_t)next.x_q16+(uint32_t)next.velocity_x_q16);
            next.y_q16 = portable_thwomp_signed((uint32_t)next.y_q16+(uint32_t)next.velocity_y_q16);
            next.height_q16 = portable_thwomp_signed((uint32_t)next.height_q16+(uint32_t)next.velocity_z_q16);
            next.velocity_z_q16 = portable_thwomp_signed((uint32_t)next.velocity_z_q16-0x4000u);
            if (next.height_q16 <= 0) next.flags &= UINT8_C(0xfe);
        }
    } else if (next.variant <= 3u) {
        switch (next.state) {
        case 0u:
            if (next.timer <= 0) {next.state=1u; next.timer=36;} break;
        case 1u:
            next.height_q16 = portable_thwomp_rumble(next.timer) ? 0x8000 : 0;
            if (next.timer <= 0) {next.state=2u; next.timer=64; next.velocity_z_q16=0x8000;} break;
        case 2u:
            next.height_q16 = portable_thwomp_signed((64u-(uint32_t)next.timer)<<15);
            if (next.timer <= 0) {next.state=3u; next.timer=32;} break;
        case 3u:
            if (next.timer <= 0) {next.state=4u; next.timer=36;} break;
        case 4u:
            next.height_q16 = 0x200000+(portable_thwomp_rumble(next.timer) ? 0x8000 : 0);
            if (next.timer <= 0) {next.state=5u; next.timer=8; next.velocity_z_q16=-0x40000;} break;
        case 5u:
            next.height_q16 = portable_thwomp_signed((uint32_t)next.timer<<18);
            if (next.timer <= 0) {next.state=6u; next.timer=32; next.velocity_z_q16=0; *landed=1;} break;
        case 6u:
            /* 540A0's landing rumble differs from every moving variant. */
            next.height_q16 = next.timer >= 0 && next.timer < 32 &&
                ((unsigned)next.timer & 7u) >= 4u ? 0x8000 : 0;
            if (next.timer <= 0) {next.state=0u; next.timer=32;} break;
        default: return 0;
        }
    } else if (next.state < normal) {
        const unsigned phases = next.variant <= 5u ? 8u : 9u;
        const unsigned phase = next.state % phases, cycle = next.state / phases;
        const unsigned move_phase = phases == 8u ? 3u : 4u;
        const unsigned top_phase = phases-3u;
        const int32_t old_timer = next.timer;
        if (phase == 1u || phase == top_phase || phase == phases-1u) {
            next.height_q16 = (phase == top_phase ? 0x200000 : 0)+
                (portable_thwomp_rumble(old_timer) ? 0x8000 : 0);
            next.timer = portable_thwomp_signed((uint32_t)old_timer+1u);
            if (next.timer > 35) {
                ++next.state;
                if (phase == 1u) {next.timer=0; next.velocity_z_q16=0x8000;}
                else if (phase == top_phase) {next.timer=8; next.velocity_z_q16=-0x40000;}
                else {next.timer=phases == 8u ? 48 : 16; if (next.state == normal) next.state=0u;}
            }
        } else if (phase == 2u) {
            next.timer = portable_thwomp_signed((uint32_t)old_timer+1u);
            next.height_q16 = portable_thwomp_signed((uint32_t)next.timer<<15);
            if (next.timer > 63) {++next.state; next.timer=phases == 8u ? 48 : 16;}
        } else {
            next.timer = portable_thwomp_signed((uint32_t)old_timer-1u);
            if (phase == move_phase) {
                int32_t *position = &next.x_q16;
                int32_t velocity = next.velocity_x_q16;
                int reverse = 0;
                if (next.variant <= 5u || next.variant == 7u || next.variant == 8u) {
                    if (cycle >= 2u) {position=&next.y_q16; velocity=next.velocity_y_q16;}
                    reverse = (cycle & 1u) != 0u;
                } else if (next.variant == 6u || next.variant == 9u) {
                    if (cycle >= 4u) {position=&next.y_q16; velocity=next.velocity_y_q16; reverse=cycle == 5u;}
                    else reverse=cycle >= 2u;
                } else reverse=cycle >= (next.variant == 10u ? 2u : 3u);
                *position = portable_thwomp_signed(reverse ? (uint32_t)*position-(uint32_t)velocity :
                    (uint32_t)*position+(uint32_t)velocity);
                if (next.timer <= 0) {
                    ++next.state;
                    next.timer = phases == 8u ? 64 : next.variant == 10u ? 24 : next.variant == 11u ? 16 : 32;
                }
            } else if (phase == phases-2u) {
                next.height_q16 = portable_thwomp_signed((uint32_t)next.timer<<18);
                if (next.timer <= 0) {++next.state; next.timer=0; next.velocity_z_q16=0; *landed=1;}
            } else if (next.timer <= 0) {
                ++next.state;
                next.timer = phases == 9u && phase == 3u ? 48 : 0;
            }
        }
    }
    *actor = next;
    return 1;
}

/* 55AE8..55D04: star/protected drivers launch a Thwomp out of the course. */
static inline void portable_thwomp_contact(PortableThwomp *actor,
    uint16_t type, int16_t guard, int16_t protection, int16_t vx, int16_t vy)
{
    if (actor && (type & 0xf000u) == 0x2000u && guard == 0 && protection != 0) {
        actor->state = (uint8_t)(actor->variant <= 3u ? 7u : portable_thwomp_normal_states(actor->variant)+1u);
        actor->velocity_x_q16 = (int32_t)vx*384;
        actor->velocity_y_q16 = (int32_t)vy*384;
        actor->velocity_z_q16 = 0x24000;
        actor->flags &= UINT8_C(0xfd);
    }
}

static inline int portable_thwomp_course_init(PortableThwompCourse *course,
    const PortableThwomp *initial, size_t count)
{
    if (!course || count > PORTABLE_THWOMP_CAPACITY || (count && !initial)) return 0;
    *course = (PortableThwompCourse){.initial=initial, .count=count};
    for (size_t i=0; i<count; ++i) course->actors[i]=initial[i];
    return 1;
}

#endif
