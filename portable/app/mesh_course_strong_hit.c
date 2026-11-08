#include "mesh_course_strong_hit.h"
#include "driver_steering.h"
#include <string.h>

int portable_mesh_course_strong_hit_valid(const PortableMeshStrongHitState *s)
{
    if (!s || s->pending > 1 || s->active > 1 || s->direction > 1 ||
        (s->pending && s->active))
        return 0;
    if (s->pending)
        return s->rate == 0x1000 || s->rate == 0x2000;
    if (s->active)
        return s->rate >= 0xc0 && s->rate <= 0x1fc0 && !(s->rate & 0x3f);
    return !s->angle && (s->rate == 0xc0 || (!s->rate && !s->direction));
}

int portable_mesh_course_strong_hit_admit(PortableMeshStrongHitState *s,
                                         uint16_t rate, uint8_t direction)
{
    if (!portable_mesh_course_strong_hit_valid(s) ||
        (rate != 0x1000 && rate != 0x2000) || direction > 1)
        return 0;
    s->rate = rate;
    s->direction = direction;
    s->pending = 1;
    s->active = 0;
    return 1;
}

int portable_mesh_course_strong_hit_tick(PortableMeshStrongHitState *s,
                                        int grounded, int recovering, int *held)
{
    if (!portable_mesh_course_strong_hit_valid(s) || !held ||
        (grounded != 0 && grounded != 1) || (recovering != 0 && recovering != 1))
        return 0;
    PortableMeshStrongHitState next = *s;
    int hold = 0;
    if (recovering)
        memset(&next, 0, sizeof next);
    else
    {
        if (next.pending && grounded)
        {
            next.pending = 0;
            next.active = 1;
        }
        if (next.active)
        {
            /* Borrow only the scalar fields consumed by the recovered pure
             * angular helper. No 2D session, speed or position enters this owner. */
            PortableDriverSteeringState source = {0};
            source.response_status = 13;
            source.driver_mode = 10;
            source.response_flags = (uint16_t)(0x0200 | (next.direction ? 0x0100 : 0));
            source.position_82 = next.angle;
            if (portable_driver_strong_spin_tick(&source, &next.rate) != PORTABLE_DRIVER_STEERING_OK)
                return 0;
            next.angle = source.position_82;
            next.active = source.response_status == 13;
            hold = 1;
        }
    }
    if (!portable_mesh_course_strong_hit_valid(&next))
        return 0;
    *s = next;
    *held = hold;
    return 1;
}

PortableMeshStatus portable_mesh_course_strong_hit_hold(PortableMeshKartState *kart,
                                                        PortableMeshKartEffects *effects)
{
    if (!kart || !effects || !portable_mesh_kart_state_valid(kart))
        return PORTABLE_MESH_INVALID;
    PortableMeshKartState next = *kart;
    PortableMeshKartEffects held = *effects;
    const int reproject = next.outside_drift_q16 != 0;
    next.drift_charge = next.panel_ticks = 0;
    next.drift_direction = 0;
    next.drift_tier = next.last_boost_tier = 0;
    next.outside_drift_q16 = 0;
    if (reproject &&
        portable_mesh_kart_project_travel_speed(&next, &next.speed_q16) != PORTABLE_MESH_OK)
        return PORTABLE_MESH_INVALID;
    memset(next.boost_ticks, 0, sizeof next.boost_ticks);
    next.rocket_ticks = 0;
    held.inhibit_controls = held.cancel_drift = 1;
    held.boost_active = held.star = 0;
    held.boost_cap_ratio_q16 = 65536;
    held.boost_acceleration_q16 = 0;
    *kart = next;
    *effects = held;
    return PORTABLE_MESH_OK;
}
