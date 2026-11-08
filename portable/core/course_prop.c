#include "course_prop.h"

static int32_t portable_teepee_add(int32_t a, int32_t b)
{
    const uint32_t bits = (uint32_t)a + (uint32_t)b;
    return bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}
/* 53BE8..53D62 inert constructors: no callback and no tile-list insertion. */
static int portable_course_prop_is_decoration(uint32_t code)
{
    const uint32_t family = code >> 16u;
    return family == 1u || family == 2u || family == 7u || family == 11u || family == 0x85u ||
           family == 0x8cu || family == 0x90u || family == 0x9au || family == 0x9bu;
}
int portable_course_prop_init(PortableCourseProp *a, int32_t x, int32_t y, uint32_t code,
                              uint16_t affine, uint8_t art)
{
    const uint32_t family = code >> 16u, variant = code & 65535u;
    const int known = (family == 3u || family == 4u || family == 8u || family == 9u ||
                       family == 14u || family == 0x86u)
                          ? variant == 0u
                      : family == 5u                             ? variant <= 2u
                      : (family == 6u || family == 0x98u)        ? variant <= 1u
                      : (family == 0xa0u || family == 0xa3u)     ? variant == 0u
                      : family == 0xa1u                          ? variant <= 1u
                      : family == 0xa2u                          ? variant <= 3u
                      : family == 10u                            ? variant <= 7u
                      : family == 12u                            ? variant <= 11u
                      : (family == 1u || family == 11u)          ? variant <= 1u
                      : portable_course_prop_is_decoration(code) ? variant == 0u
                                                                 : 0;
    if (!a || !known || x < 0 || y < 0 || x > 2048 * 65536 || y > 2048 * 65536 || !affine ||
        affine > 256u || art >= 64u || (family == 3u && art > 53u) ||
        (family == 0xa2u && art > 59u) || (family == 0xa1u && art > 60u) ||
        (family == 0xa0u && art > 62u))
        return 0;
    *a = (PortableCourseProp){
        .x_q16 = x,
        .y_q16 = y,
        .source_code = code,
        .height_q16 = family == 1u ? -INT32_C(0x80000) : 0,
        .affine_x_q8 = (int16_t)affine,
        .affine_y_q8 = (int16_t)affine,
        .art_index = art,
        .flags =
            (uint8_t)((family == 3u || family == 8u || family == 9u || family >= 0xa0u) ? 3u : 1u)};
    if (family == 0xa3u)
    {
        a->height_q16 = 0x80000;
    }
    if (family == 0xa1u)
        a->mechakoopa.velocity_x_q16 = variant == 1u ? -32768 : 32768;
    if (family == 0xa2u)
    {
        a->flags = 0u;
        a->lake_rock = (PortableLakeRockMotion){.home_x_q16 = x,
                                                .home_y_q16 = y,
                                                .delay = (int16_t)(variant == 1u   ? 0x80
                                                                   : variant == 2u ? 0x90
                                                                   : variant == 3u ? 0xa0
                                                                                   : 0x70)};
    }
    return 1;
}
/* 532DC constructs placements in their authored order; active actor lists
 * insert at the head only afterward. Only59E98/A3 draws in this family. */
int portable_course_props_init_with_random(PortableCourseProps *course,
                                           const PortableCourseProp *initial, size_t count,
                                           PortableCoursePropRandom random, void *context)
{
    if (!course || count > PORTABLE_COURSE_PROP_CAPACITY || (count && !initial))
        return 0;
    *course = (PortableCourseProps){.initial = initial, .count = count};
    for (size_t i = 0; i < count; ++i)
    {
        course->actors[i] = initial[i];
        if (random && (initial[i].source_code >> 16u) == 0xa3u)
            course->actors[i].pier_boo.phase = (uint16_t)(random(context) & UINT32_C(0xfc00));
    }
    return 1;
}
/* Content decoding and legacy fixtures have no live visual RNG owner. */
int portable_course_props_init(PortableCourseProps *course, const PortableCourseProp *initial,
                               size_t count)
{
    return portable_course_props_init_with_random(course, initial, count, NULL, NULL);
}
PortableCourseProp *portable_course_prop_at(PortableCourseProps *course, int32_t world_x,
                                            int32_t world_y)
{
    if (!course)
        return NULL;
    /* Head insertion in 532DC reverses placement order. Division is signed
     * truncation toward zero, matching the source's negative-coordinate bias. */
    for (size_t i = course->count; i != 0u; --i)
    {
        PortableCourseProp *a = &course->actors[i - 1u];
        if (!portable_course_prop_is_decoration(a->source_code) &&
            (a->source_code >> 16u) < 0xa0u && (a->source_code >> 16u) != 3u &&
            a->x_q16 / 0x80000 == world_x / 8 && a->y_q16 / 0x80000 == world_y / 8)
            return a;
    }
    return NULL;
}
void portable_course_prop_damage(PortableCourseProp *a)
{
    if (a && ((a->source_code >> 16u) == 3u || (a->source_code >> 16u) == 0xa2u))
        return;
    if (a && ((a->source_code >> 16u) == 0xa0u || (a->source_code >> 16u) == 0xa3u))
    {
        if (a->state < 5u)
            a->state = 5u;
        return;
    }
    if (a && !portable_course_prop_is_decoration(a->source_code) && !a->state)
    {
        a->state = 1u;
        a->timer = 32;
        a->affine_y_q8 = -256;
        if ((a->source_code >> 16u) == 0xa1u)
            a->flags &= UINT8_C(0xfd);
    }
}
static int portable_mechakoopa_advance(PortableCourseProp *a)
{
    PortableMechakoopaMotion *m = &a->mechakoopa;
    if (m->cycle > 351u || m->animation_phase > 1u || m->animation_ticks > 9u ||
        m->has_facing > 1u ||
        m->velocity_x_q16 != ((a->source_code & 65535u) == 1u ? -32768 : 32768))
        return 0;
    const int forward = m->cycle < 176u;
    const int walking = m->cycle < 96u || (m->cycle >= 176u && m->cycle < 272u);
    const int32_t vx = forward ? m->velocity_x_q16 : -m->velocity_x_q16;
    if (walking)
    {
        const uint32_t bits = (uint32_t)a->x_q16 + (uint32_t)vx;
        a->x_q16 = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
    }
    m->heading = vx > 0 ? 0x4000u : 0xc000u;
    m->has_facing = 1u;
    if (++m->animation_ticks >= (walking ? 6u : 10u))
    {
        m->animation_ticks = 0u;
        m->animation_phase ^= 1u;
    }
    m->cycle = (uint16_t)(m->cycle == 351u ? 0u : m->cycle + 1u);
    return 1;
}
int portable_kamek_advance(PortableCourseProp *a, const int32_t *sine)
{
    PortableKamekMotion *m = &a->kamek;
    if (a->state > 8u || a->timer < 0 || a->timer > 128 || m->has_facing > 1u ||
        m->velocity_x_q16 < -32768 || m->velocity_x_q16 > 32768 || m->velocity_x_q16 % 2048)
        return 0;
    const int initial_bob = a->state == 0u;
    if (a->state == 0u)
    {
        if (a->timer > 0)
            --a->timer;
        else
        {
            a->state = 2u;
            a->timer = 64;
        }
    }
    else if (a->state == 1u)
    {
        a->state = 2u;
        a->timer = 128;
    }
    else if (a->state == 3u)
    {
        a->state = 4u;
        a->timer = 128;
    }
    else if (a->state == 5u)
    {
        a->state = 6u;
        a->flags &= UINT8_C(0xfd);
        a->affine_y_q8 = -256;
        m->velocity_z_q16 = 0x30000;
    }
    if (a->state == 6u)
    {
        const uint32_t height = (uint32_t)a->height_q16 + (uint32_t)m->velocity_z_q16;
        a->height_q16 = height <= INT32_MAX ? (int32_t)height : -1 - (int32_t)(UINT32_MAX - height);
        if (a->height_q16 < 0)
        {
            a->state = 8u;
            a->flags &= UINT8_C(0xfe);
        }
        else
        {
            const uint32_t speed = (uint32_t)m->velocity_z_q16 - 0x4000u;
            m->velocity_z_q16 =
                speed <= INT32_MAX ? (int32_t)speed : -1 - (int32_t)(UINT32_MAX - speed);
        }
        return 1;
    }
    if (a->state == 7u)
    {
        a->state = 8u;
        a->flags &= UINT8_C(0xfe);
        return 1;
    }
    if (a->state == 8u)
        return 1;
    /* State0's first bob sets state2 but skips patrol integration that tick. */
    if (!initial_bob)
    {
        const uint32_t x = (uint32_t)a->x_q16 + (uint32_t)m->velocity_x_q16;
        a->x_q16 = x <= INT32_MAX ? (int32_t)x : -1 - (int32_t)(UINT32_MAX - x);
    }
    m->phase = (uint16_t)(m->phase + 1024u);
    int phase = m->phase;
    if (phase >= 32768)
        phase -= 65536;
    if (phase < 0)
        phase += 63;
    const uint32_t bob = (uint32_t)sine[(uint16_t)phase / 64u] * 3u + 0xc0000u;
    a->height_q16 = bob <= INT32_MAX ? (int32_t)bob : -1 - (int32_t)(UINT32_MAX - bob);
    if (initial_bob)
        return 1;
    const int accelerating = a->state == 2u;
    const int sign = m->mirrored ? -1 : 1;
    const int target = (accelerating ? 32768 : -32768) * sign;
    if (m->velocity_x_q16 != target)
        m->velocity_x_q16 += (accelerating ? 2048 : -2048) * sign;
    const int direction =
        m->velocity_x_q16 ? m->velocity_x_q16 : (accelerating ? 2048 : -2048) * sign;
    m->heading = direction > 0 ? 0x4000u : 0xc000u;
    m->has_facing = 1u;
    if (--a->timer <= 0)
    {
        a->state = (uint8_t)(accelerating ? 3u : 1u);
        a->timer = 128;
    }
    return 1;
}
/* 59FAC's unprotected branch: no collision, no draw, no further callback. */
void portable_pier_boo_vanish(PortableCourseProp *a)
{
    if (a && (a->source_code >> 16u) == 0xa3u && a->state < 8u)
    {
        a->state = 8u;
        a->flags &= UINT8_C(0xfc);
    }
}
static int portable_pier_boo_advance(PortableCourseProp *a, const int32_t *sine)
{
    PortablePierBooMotion *m = &a->pier_boo;
    if (a->state == 0u)
    {
        int angle = (int16_t)m->phase;
        if (angle < 0)
            angle += 63;
        const uint32_t bob = (uint32_t)sine[(uint16_t)angle / 64u] * 3u + 0x80000u;
        a->height_q16 = bob <= INT32_MAX ? (int32_t)bob : -1 - (int32_t)(UINT32_MAX - bob);
        m->phase = (uint16_t)(m->phase + 1024u);
        return 1;
    }
    if (a->state == 5u)
    {
        a->state = 6u;
        a->flags &= UINT8_C(0xfd);
        a->affine_y_q8 = -256;
        m->velocity_z_q16 = 0xc0000;
    }
    if (a->state == 6u)
    {
        const uint32_t height = (uint32_t)a->height_q16 + (uint32_t)m->velocity_z_q16;
        a->height_q16 = height <= INT32_MAX ? (int32_t)height : -1 - (int32_t)(UINT32_MAX - height);
        if (a->height_q16 >= 0)
        {
            const uint32_t speed = (uint32_t)m->velocity_z_q16 - 0x4000u;
            m->velocity_z_q16 =
                speed <= INT32_MAX ? (int32_t)speed : -1 - (int32_t)(UINT32_MAX - speed);
        }
        else
        {
            a->state = 8u;
            a->flags &= UINT8_C(0xfe);
        }
        return 1;
    }
    return a->state == 8u;
}
unsigned portable_course_prop_view_art(const PortableCourseProp *a, uint16_t camera_heading)
{
    if ((a->source_code >> 16u) == 3u)
        return a->art_index + a->teepee.frame;
    if ((a->source_code >> 16u) == 0xa2u)
        return a->art_index + a->lake_rock.frame;
    if ((a->source_code >> 16u) == 0xa0u)
    {
        return a->art_index + (unsigned)(a->kamek.has_facing == 1u &&
                                         (uint16_t)(a->kamek.heading - camera_heading) <= 0x7fffu);
    }
    if ((a->source_code >> 16u) != 0xa1u || !a->mechakoopa.has_facing)
        return a->art_index;
    const int front = (uint16_t)(a->mechakoopa.heading - camera_heading) <= 0x7fffu;
    return a->art_index + (unsigned)(front ? (a->mechakoopa.animation_phase ? 2 : 3)
                                           : (a->mechakoopa.animation_phase ? 0 : 1));
}
/* 599EC's cues require an unmuted local player within volume 0x40. Song386
 * also checks projection and an idle player; the port does not model them. */
static void portable_lake_rock_cue(const PortableCourseProp *a,
                                   const PortableCoursePropListener *listener, uint16_t song)
{
    if (!listener || !listener->sounds || (listener->flags & 0x20u))
        return;
    const uint16_t volume =
        portable_spatial_audio_volume((uint32_t)a->x_q16, (uint32_t)a->y_q16,
                                      (uint32_t)listener->x_q16, (uint32_t)listener->y_q16);
    PortableCoursePropSounds *sounds = listener->sounds;
    if (volume <= 0x3fu || sounds->count >= PORTABLE_COURSE_PROP_SOUND_CAPACITY)
        return;
    sounds->songs[sounds->count] = song;
    sounds->volumes[sounds->count++] = volume;
}
int portable_lake_rock_bounce(PortableCourseProp *a)
{
    PortableLakeRockMotion *m = &a->lake_rock;
    a->x_q16 = portable_teepee_add(a->x_q16, m->mirrored ? -0x4000 : 0x4000);
    a->y_q16 = portable_teepee_add(a->y_q16, 0x4000);
    a->height_q16 = portable_teepee_add(a->height_q16, m->velocity_z_q16);
    if (a->height_q16 <= 0)
    {
        a->height_q16 = 0;
        return 1;
    }
    m->velocity_z_q16 = portable_teepee_add(m->velocity_z_q16, -0x1800);
    return 0;
}
/* 599EC: later states fall through in the same tick where the source does. */
static int portable_lake_rock_advance(PortableCourseProp *a,
                                      const PortableCoursePropListener *listener)
{
    PortableLakeRockMotion *m = &a->lake_rock;
    if (a->state > 11u || m->frame > 4u)
        return 0;
    switch (a->state)
    {
    case 0u:
        a->state = 1u;
        m->timer = m->delay;
        a->flags &= UINT8_C(0xfc);
        a->x_q16 = m->home_x_q16;
        a->y_q16 = m->home_y_q16;
        /* fall through */
    case 1u:
        if (--m->timer <= 0)
            a->state = 2u;
        return 1;
    case 2u:
        a->state = 3u;
        m->frame = 0u;
        a->height_q16 = 0x300000;
        m->velocity_z_q16 = 0;
        a->affine_x_q8 = a->affine_y_q8 = 0x400;
        a->flags |= 3u;
        portable_lake_rock_cue(a, listener, 386u);
        /* fall through */
    case 3u:
    {
        const int16_t scale =
            a->affine_x_q8 - 0x18 > 0xc0 ? (int16_t)(a->affine_x_q8 - 0x18) : 0xc0;
        a->affine_x_q8 = a->affine_y_q8 = scale;
        m->velocity_z_q16 -= 0xc00;
        a->height_q16 = portable_teepee_add(a->height_q16, m->velocity_z_q16);
        if (a->height_q16 <= 0)
        {
            a->height_q16 = 0;
            a->state = 4u;
        }
        return 1;
    }
    case 4u:
        a->state = 5u;
        m->timer = 10;
        a->affine_x_q8 = a->affine_y_q8 = 0xc0;
        portable_lake_rock_cue(a, listener, 387u);
        /* fall through */
    case 5u:
        m->frame = m->timer > 8 ? 1u : m->timer > 5 ? 2u : m->timer > 2 ? 3u : 4u;
        if (--m->timer <= 0)
            a->state = 6u;
        return 1;
    case 6u:
        a->state = 7u;
        m->velocity_z_q16 = 0x10000;
        m->frame = 0u;
        portable_lake_rock_cue(a, listener, 388u);
        /* fall through */
    case 7u:
        if (portable_lake_rock_bounce(a))
            a->state = 8u;
        return 1;
    case 8u:
        a->state = 9u;
        m->velocity_z_q16 = -(m->velocity_z_q16 / 2);
        portable_lake_rock_cue(a, listener, 389u);
        /* fall through */
    case 9u:
        if (portable_lake_rock_bounce(a))
            a->state = 10u;
        return 1;
    case 10u:
        a->state = 11u;
        m->timer = 0x80;
        /* fall through */
    default:
        if (--m->timer <= 0)
            a->state = 0u;
        return 1;
    }
}
int portable_course_props_advance_heard(PortableCourseProps *course, const int32_t *sine,
                                        size_t sine_count,
                                        const PortableCoursePropListener *listener)
{
    if (!course || course->count > PORTABLE_COURSE_PROP_CAPACITY || !sine || sine_count < 1024u)
        return 0;
    if (listener && listener->sounds)
    {
        listener->sounds->count = 0u;
        ++listener->sounds->serial;
    }
    for (size_t i = 0; i < course->count; ++i)
    {
        PortableCourseProp *a = &course->actors[i];
        if ((a->source_code >> 16u) == 0xa2u)
        {
            if (!portable_lake_rock_advance(a, listener))
                return 0;
            continue;
        }
        if (!(a->flags & 1u))
            continue;
        if ((a->source_code >> 16u) == 0xa0u)
        {
            if (!portable_kamek_advance(a, sine))
                return 0;
            continue;
        }
        if ((a->source_code >> 16u) == 0xa3u)
        {
            if (!portable_pier_boo_advance(a, sine))
                return 0;
            continue;
        }
        if ((a->source_code >> 16u) == 3u)
            continue;
        if ((a->source_code >> 16u) == 0xa1u && !a->state)
        {
            if (!portable_mechakoopa_advance(a))
                return 0;
            continue;
        }
        if (!a->state)
            continue;
        if (a->state != 1u || a->timer < 0 || a->timer > 32)
            return 0;
        a->flags &= UINT8_C(0xfd);
        const uint32_t bits = (uint32_t)sine[(unsigned)a->timer * 16u] * 48u;
        a->height_q16 = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
        if (--a->timer < 0)
            a->flags &= UINT8_C(0xfe);
    }
    return 1;
}
int portable_course_props_advance(PortableCourseProps *course, const int32_t *sine,
                                  size_t sine_count)
{
    return portable_course_props_advance_heard(course, sine, sine_count, NULL);
}
/* 58B4C: the latest unguarded kart in one collision pass owns the burst. */
void portable_teepee_hit(PortableCourseProp *a, uint8_t kart, int16_t component_x,
                         int16_t component_y)
{
    if (!a || (a->source_code >> 16u) != 3u || a->state > 1u)
        return;
    a->state = 1u;
    a->teepee.kart = kart;
    a->teepee.velocity_x_q16 = (int32_t)component_x * 256;
    a->teepee.velocity_y_q16 = (int32_t)component_y * 256;
}
/* 58CA4: velocity from the kart, vertical 0x12000, gravity 0x1800. */
static int portable_teepee_fling(PortableTeepeeMotion *m, const PortableTeepeeKart *kart)
{
    m->rider_state = 2u;
    m->rider_vx_q16 = (int32_t)kart->component_x * 256;
    m->rider_vy_q16 = (int32_t)kart->component_y * 256;
    m->rider_vz_q16 = 0x12000;
    m->rider_spin_rate = 0u;
    m->rider_gravity = 0x1800u;
    return kart->is_player && !kart->muted;
}
/* 58EB8: twelve units behind the heading. The kart-sprite wheel and material
 * bob terms are not modelled; the speed-band clock bob is. */
static void portable_teepee_ride(PortableTeepeeMotion *m, const PortableTeepeeKart *kart,
                                 const int32_t *sine, uint32_t clock)
{
    int angle = (int16_t)(uint16_t)(kart->heading - 0x4000u);
    if (angle < 0)
        angle += 63;
    angle >>= 6;
    const int32_t across = sine[(unsigned)(256 - angle) & 1023u] * 12;
    const int32_t along = sine[(unsigned)angle & 1023u] * 12;
    const uint32_t band = (uint32_t)kart->speed_q16 - 0x10000u;
    const int32_t bob = band <= 0x9effffu ? (int32_t)((clock & 2u) >> 1) : 0;
    m->rider_x_q16 = portable_teepee_add(kart->x_q16, -across);
    m->rider_y_q16 = portable_teepee_add(kart->y_q16, -along);
    m->rider_z_q16 = portable_teepee_add(kart->z_q16, bob * 32768);
    m->rider_heading = kart->heading;
}
/* Returns the number of player 58CA4 cues (song 372); -1 on bad state. */
int portable_course_props_tick_teepees(PortableCourseProps *course, PortableTeepeeKart *karts,
                                       size_t kart_count, const int32_t *sine, size_t sine_count,
                                       uint32_t clock)
{
    if (!course || course->count > PORTABLE_COURSE_PROP_CAPACITY || !sine || sine_count < 1024u ||
        (kart_count && !karts))
        return -1;
    int cues = 0;
    for (size_t i = 0; i < course->count; ++i)
    {
        PortableCourseProp *a = &course->actors[i];
        if ((a->source_code >> 16u) != 3u)
            continue;
        PortableTeepeeMotion *m = &a->teepee;
        if (a->state > 3u || m->rider_state > 2u || m->counter > 31u || m->frame > 3u)
            return -1;
        int spawned = 0;
        if (a->state == 1u)
        {
            if (m->kart >= kart_count)
                return -1;
            PortableTeepeeKart *kart = &karts[m->kart];
            a->flags &= UINT8_C(0xfd);
            m->rider_spin = 0u;
            if (kart->protection)
            {
                /* 58BA8's protected branch scales 58CA4 by 3/2 and spins it. */
                m->rider_x_q16 = kart->x_q16;
                m->rider_y_q16 = kart->y_q16;
                m->rider_z_q16 = kart->z_q16;
                cues += portable_teepee_fling(m, kart);
                m->rider_vx_q16 = (int32_t)(((int64_t)m->rider_vx_q16 * 3) / 2);
                m->rider_vy_q16 = (int32_t)(((int64_t)m->rider_vy_q16 * 3) / 2);
                m->rider_spin_rate = 0x1000u;
                m->rider_gravity = 0x2000u;
                m->rider_heading = kart->heading;
            }
            else
            {
                m->rider_state = 1u;
                portable_teepee_ride(m, kart, sine, clock);
                kart->lock = 360u;
            }
            a->state = 2u;
            m->counter = 0u;
            spawned = 1;
        }
        if (a->state == 2u)
        {
            ++m->counter;
            a->x_q16 = portable_teepee_add(a->x_q16, m->velocity_x_q16);
            a->y_q16 = portable_teepee_add(a->y_q16, m->velocity_y_q16);
            if (m->counter == 1u)
                m->frame = 1u;
            else if (m->counter == 11u)
                m->frame = 2u;
            else if (m->counter == 21u)
                m->frame = 3u;
            else if (m->counter > 30u)
            {
                a->state = 3u;
                a->flags &= UINT8_C(0xfe);
            }
        }
        if (spawned || !m->rider_state)
            continue;
        if (m->kart >= kart_count)
            return -1;
        PortableTeepeeKart *kart = &karts[m->kart];
        if (m->rider_state == 1u)
        {
            portable_teepee_ride(m, kart, sine, clock);
            if ((int16_t)kart->lock <= 0)
                cues += portable_teepee_fling(m, kart);
            continue;
        }
        /* 58D4C: spin, integrate, fall until height drops below zero. */
        m->rider_spin = (uint16_t)(m->rider_spin + m->rider_spin_rate);
        m->rider_heading = kart->heading;
        m->rider_x_q16 = portable_teepee_add(m->rider_x_q16, m->rider_vx_q16);
        m->rider_y_q16 = portable_teepee_add(m->rider_y_q16, m->rider_vy_q16);
        m->rider_z_q16 = portable_teepee_add(m->rider_z_q16, m->rider_vz_q16);
        if (m->rider_z_q16 >= 0)
            m->rider_vz_q16 = portable_teepee_add(m->rider_vz_q16, -(int32_t)m->rider_gravity);
        else
            m->rider_state = 0u;
    }
    return cues;
}
/* 58E10: seven Shy Guy views after the teepee's four frames. */
unsigned portable_teepee_rider_art(const PortableCourseProp *a, uint16_t camera_heading)
{
    const uint16_t delta =
        (uint16_t)(camera_heading - a->teepee.rider_heading - a->teepee.rider_spin);
    const unsigned view = delta <= 0x3000u   ? 2u
                          : delta <= 0x5000u ? 1u
                          : delta <= 0x8000u ? 0u
                          : delta <= 0xb000u ? 4u
                          : delta <= 0xd000u ? 5u
                          : delta <= 0xefffu ? 6u
                                             : 3u;
    return a->art_index + 4u + view;
}
