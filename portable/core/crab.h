#ifndef PORTABLE_CRAB_H
#define PORTABLE_CRAB_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#define PORTABLE_CRAB_CAPACITY 64u

/* 5631C/5619C/56388: class80 beach and island crabs, without actor pointers. */
typedef struct {
    int32_t x_q16, y_q16, height_q16;
    int32_t velocity_x_q16, velocity_y_q16;
    int32_t timer;
    uint16_t variant;
    uint8_t checkpoint, placement_order;
    uint8_t flags, art, animation_frame, animation_timer, flip_y;
} PortableCrab;

typedef struct {
    PortableCrab actors[PORTABLE_CRAB_CAPACITY];
    const PortableCrab *initial;
    size_t count;
} PortableCrabCourse;

static inline int32_t portable_crab_signed(uint32_t bits)
{
    return bits <= INT32_MAX ? (int32_t)bits : -1-(int32_t)(UINT32_MAX-bits);
}

static inline int portable_crab_init(PortableCrab *actor,
    int32_t x, int32_t y, uint8_t checkpoint, uint16_t variant)
{
    if (actor==NULL || x<0 || y<0 || x>2048*65536 || y>2048*65536 ||
        checkpoint>=128u ||
        !(variant<=1u || (variant>=0x8000u && variant<=0x8002u))) return 0;
    *actor=(PortableCrab){.x_q16=x,.y_q16=y,.variant=variant,
        .checkpoint=checkpoint,.flags=3u,.art=variant>=0x8000u?2u:0u};
    /* Factory 53C4A replaces the island constructor image with F0008.
     * ROM 56342..56380: 808002 takes the horizontal default, despite
     * the cached constructor's extra vertical case. */
    if (variant==1u || variant==0x8001u) actor->velocity_y_q16=0x8000;
    else actor->velocity_x_q16=0x8000;
    return 1;
}

static inline int portable_crab_advance(PortableCrab *actor,
    const int32_t *sine)
{
    if (actor==NULL || sine==NULL || actor->art>3u ||
        actor->animation_frame>1u || actor->animation_timer>9u) return 0;
    if (actor->flags&2u) {
        if (actor->timer<0 || actor->timer>379) return 0;
        const int forward=actor->timer<=149;
        const int backward=actor->timer>=190 && actor->timer<=339;
        if (forward || backward) {
            const uint32_t vx=(uint32_t)actor->velocity_x_q16;
            const uint32_t vy=(uint32_t)actor->velocity_y_q16;
            actor->x_q16=portable_crab_signed((uint32_t)actor->x_q16+(forward?vx:0u-vx));
            actor->y_q16=portable_crab_signed((uint32_t)actor->y_q16+(forward?vy:0u-vy));
        }
        if (++actor->animation_timer >= (forward || backward ? 6u : 10u)) {
            actor->animation_timer=0u;
            actor->animation_frame^=1u;
            actor->art=(actor->variant>=0x8000u ? 2u : 0u)+actor->animation_frame;
        }
        if (++actor->timer>379) actor->timer=0;
    } else if (actor->flags&1u) {
        if (actor->timer<0 || actor->timer>32) return 0;
        actor->height_q16=portable_crab_signed((uint32_t)sine[actor->timer*16]*48u);
        if (--actor->timer<0) actor->flags&=UINT8_C(0xfe);
    }
    return 1;
}

static inline void portable_crab_contact(PortableCrab *actor,
    uint16_t type, int16_t guard, int16_t protection)
{
    if (actor!=NULL && (type&0xf000u)==0x2000u && guard==0 && protection!=0) {
        actor->timer=32;
        actor->flags&=UINT8_C(0xfd);
        actor->flip_y=1u;
    }
}

static inline int portable_crab_course_init(PortableCrabCourse *course,
    const PortableCrab *initial, size_t count)
{
    if (course==NULL || count>PORTABLE_CRAB_CAPACITY || (count && initial==NULL)) return 0;
    *course=(PortableCrabCourse){.initial=initial,.count=count};
    for (size_t i=0;i<count;++i) course->actors[i]=initial[i];
    return 1;
}
#endif
