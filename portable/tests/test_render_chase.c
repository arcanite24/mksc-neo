#include "render_chase.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define Q 65536
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct
{
    PortableMeshTriangle rows[258];
    unsigned count, reads;
    int fail;
} Fixture;

static int triangle(const void *context, uint32_t id, PortableMeshTriangle *out)
{
    Fixture *f = (Fixture *)context;
    ++f->reads;
    if (id >= f->count || (int)id == f->fail)
        return 0;
    *out = f->rows[id];
    return 1;
}

static int next_triangle(const void *context, const int32_t low[3], const int32_t high[3],
                         uint32_t first, uint32_t *out)
{
    const Fixture *f = context;
    for (uint32_t i = first; i < f->count; ++i)
    {
        int inside = 1;
        for (unsigned a = 0; a < 3; ++a)
        {
            int32_t lo = f->rows[i].vertices_q16[0][a], hi = lo;
            for (unsigned v = 1; v < 3; ++v)
            {
                const int32_t value = f->rows[i].vertices_q16[v][a];
                if (value < lo)
                    lo = value;
                if (value > hi)
                    hi = value;
            }
            if (hi < low[a] || lo > high[a])
                inside = 0;
        }
        if (inside)
        {
            *out = i;
            return 1;
        }
    }
    return 0;
}

static void horizontal(Fixture *f, double z)
{
    const int32_t v[4][3] = {
        {-20 * Q, -20 * Q, 0}, {20 * Q, -20 * Q, 0}, {20 * Q, 20 * Q, 0}, {-20 * Q, 20 * Q, 0}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2; ++t)
    {
        PortableMeshTriangle *row = &f->rows[f->count++];
        for (unsigned p = 0; p < 3; ++p)
        {
            memcpy(row->vertices_q16[p], v[indices[t][p]], sizeof v[0]);
            row->vertices_q16[p][2] = (int32_t)llround(z * Q);
        }
    }
}

static void wall(Fixture *f, double xleft)
{
    const int32_t v[4][3] = {
        {0, -2 * Q, 0}, {10 * Q, -2 * Q, 0}, {10 * Q, -2 * Q, 6 * Q}, {0, -2 * Q, 6 * Q}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2; ++t)
    {
        PortableMeshTriangle *row = &f->rows[f->count++];
        for (unsigned p = 0; p < 3; ++p)
        {
            memcpy(row->vertices_q16[p], v[indices[t][p]], sizeof v[0]);
            row->vertices_q16[p][0] += (int32_t)llround(xleft * Q);
        }
    }
}

static void cross_wall(Fixture *f, double y)
{
    const int32_t v[4][3] = {
        {-5 * Q, 0, 0}, {5 * Q, 0, 0}, {5 * Q, 0, 6 * Q}, {-5 * Q, 0, 6 * Q}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2; ++t)
    {
        PortableMeshTriangle *row = &f->rows[f->count++];
        for (unsigned p = 0; p < 3; ++p)
        {
            memcpy(row->vertices_q16[p], v[indices[t][p]], sizeof v[0]);
            row->vertices_q16[p][1] = (int32_t)llround(y * Q);
        }
    }
}

static PortableMeshCourse course(Fixture *f, int indexed)
{
    return (PortableMeshCourse){.context = f,
                                .triangle_count = f->count,
                                .triangle = triangle,
                                .next_triangle = indexed ? next_triangle : NULL};
}

static PortableRenderChaseInput initial(void)
{
    return (PortableRenderChaseInput){
        .foot = {.position = {0, 0, 0}, .forward = {0, 1, 0}, .up = {0, 0, 1}},
        .tick = 10,
        .grounded = 1};
}

static int orthonormal(const PortableRenderChaseSnapshot *s)
{
    double rr = 0, uu = 0, ff = 0, ru = 0, rf = 0, uf = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        rr += s->right[a] * s->right[a];
        uu += s->up[a] * s->up[a];
        ff += s->forward[a] * s->forward[a];
        ru += s->right[a] * s->up[a];
        rf += s->right[a] * s->forward[a];
        uf += s->up[a] * s->forward[a];
    }
    return fabs(rr - 1) < 1e-9 && fabs(uu - 1) < 1e-9 && fabs(ff - 1) < 1e-9 && fabs(ru) < 1e-9 &&
           fabs(rf) < 1e-9 && fabs(uf) < 1e-9;
}

static int frame(const PortableRenderChaseSnapshot *s)
{
    return orthonormal(s) && s->up[2] > 0 && s->right[2] == 0;
}

static int accepted_camera(void)
{
    Fixture f = {.fail = -1};
    horizontal(&f, 0);
    PortableMeshCourse c = course(&f, 1);
    PortableRenderChaseInput in = initial();
    PortableRenderChase s = {0};
    char error[128];
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OK);
    CHECK(fabs(s.eye[0]) < 1e-8 && fabs(s.eye[1] + 3.9) < 1e-8 && fabs(s.eye[2] - 1.7) < 1e-8);
    /* The view runs from the eye over the kart and on ahead of it. */
    const double pitch = .85 / 3.9, norm = sqrt(1 + pitch * pitch);
    CHECK(fabs(s.target[1] - 1.9 / norm) < 1e-8 && fabs(s.target[2] - (.85 - 1.9 * pitch / norm)) < 1e-8);
    CHECK(s.focal_scale == 1 && s.swing == 0 && s.distance == 3.9 && s.shake == 0);
    const PortableRenderChase first = s;
    PortableRenderChaseSnapshot axis;
    CHECK(portable_render_chase_snapshot(&axis, &s, &s, 0));
    CHECK(axis.right[0] == -1 && axis.right[1] == 0 && axis.up[2] > 0);

    in.foot.position[0] = 1;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OK);
    CHECK(!memcmp(&s, &first, sizeof s)); /* Pause/render repeats never advance lag. */
    ++in.tick;
    in.foot.position[0] = 0;
    in.foot.position[1] = 1;
    in.foot.position[2] = .2;
    in.grounded = 0;
    in.boosting = 1;
    in.dynamic = 1;
    in.shake = 1;
    in.speed_ratio_q16 = Q;
    in.outside_drift_q16 = 5461;
    /* Terrain bank is ignored by the level chase horizon. */
    in.foot.up[0] = .6;
    in.foot.up[2] = .8;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OK);
    /* Translation is rigid (MK8): the boom rides the kart, only rotation lags. */
    CHECK(s.eye[1] > -3.2 && s.eye[1] < -2.5 && !memcmp(s.anchor, in.foot.position, sizeof s.anchor));
    /* Drift yaw turns toward travel (forward rotated by -outside_drift). */
    CHECK(s.swing < 0 && s.swing > -.65 && s.focal_scale < 1 && s.focal_scale > .7);
    CHECK(s.distance > 3.9 && s.shake > 0 && s.boosting == 1);
    PortableRenderChaseSnapshot snapshot;
    const PortableRenderChase saved = s;
    for (unsigned alpha = 0; alpha <= 65536; alpha += 8192)
    {
        CHECK(portable_render_chase_snapshot(&snapshot, &first, &s, alpha));
        CHECK(frame(&snapshot));
        CHECK(snapshot.right[0] < -.97);
        CHECK(snapshot.position[1] >= first.eye[1] && snapshot.position[1] <= s.eye[1]);
        CHECK(snapshot.focal_scale >= s.focal_scale && snapshot.focal_scale <= 1);
    }
    CHECK(!memcmp(&saved, &s, sizeof s));
    in.grounded = 1;
    in.boosting = 0;
    in.speed_ratio_q16 = 0;
    in.outside_drift_q16 = 0;
    in.foot.position[2] = 0;
    for (unsigned i = 0; i < 90; ++i)
    {
        ++in.tick;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
    }
    CHECK(fabs(s.swing) < .00001 && s.focal_scale > .9999 && s.anchor[2] < .00001);
    CHECK(s.distance < 3.91 && s.shake == 0);
    /* Independent pane owner responds to a perpendicular heading, no shared state. */
    PortableRenderChase other = {0};
    in = initial();
    in.foot.forward[0] = 1;
    in.foot.forward[1] = 0;
    CHECK(portable_render_chase_step(&other, &other, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OK);
    CHECK(fabs(other.eye[0] + 3.9) < 1e-8 && fabs(other.eye[1]) < 1e-8);
    CHECK(s.tick == 101 && first.tick == 10 && fabs(first.eye[1] + 3.9) < 1e-8);
    in.cut = 1;
    in.tick = 1;
    in.foot.position[0] = 100;
    CHECK(portable_render_chase_step(&other, &other, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OK);
    CHECK(fabs(other.eye[0] - 96.1) < 1e-8 && other.tick == 1);
    return 0;
}

static int collision(int immersive)
{
    for (int indexed = 0; indexed < 2; ++indexed)
    {
        Fixture f = {.fail = -1};
        horizontal(&f, 0);
        wall(&f, -5);
        const Fixture original = f;
        PortableMeshCourse c = course(&f, indexed);
        PortableRenderChase s = {0};
        PortableRenderChaseInput in = initial();
        in.immersive = (uint8_t)immersive;
        char error[128];
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.eye[1] > -1.8 && s.eye[1] < -1.5); /* Sphere stays in front of wall. */
        CHECK(!memcmp(f.rows, original.rows, sizeof f.rows));
        CHECK(f.reads <= f.count); /* Geometry cached once, not per sample. */
        /* A previously clear eye on the far side must cut to the new safe boom,
         * rather than remain trapped behind the wall or interpolate through it. */
        PortableRenderChase far = s;
        far.eye[1] = -4;
        far.eye[2] = 3;
        PortableRenderChaseInput moved = in;
        ++moved.tick;
        PortableRenderChase near;
        CHECK(portable_render_chase_step(&near, &far, &moved, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(near.discontinuous && near.eye[1] > -1.8);
        PortableRenderChaseSnapshot cut;
        CHECK(portable_render_chase_snapshot(&cut, &far, &near, 0));
        CHECK(cut.position[1] == near.eye[1]);

        /* Moving sideways interpolates only across cleared camera segments. */
        PortableRenderChase before = s;
        ++in.tick;
        in.foot.position[0] = .2;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        PortableRenderChaseSnapshot view;
        CHECK(portable_render_chase_snapshot(&view, &before, &s, 32768));
        CHECK(view.position[1] > -1.8);
        /* The finite edge must block a body even though the central ray misses it. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        wall(&f, .12);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        in = initial();
        in.immersive = (uint8_t)immersive;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.eye[1] > -1.9);
        /* Tunnel ceiling compresses height and distance; never jumps to its top. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        horizontal(&f, immersive ? 1.35 : 1.8);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.eye[2] < (immersive ? 1.17 : 1.62) && s.eye[2] > .8 && s.eye[1] > -4);
        /* Look target touching scenery can emerge without making physics fail. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        horizontal(&f, immersive ? .87 : 1);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.eye[2] > (immersive ? 1.02 : 1.15));
        /* Scenery just behind the look target still leaves a view direction. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        cross_wall(&f, 1.5);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(hypot(s.eye[0] - s.target[0], s.eye[1] - s.target[1]) > .59);
        PortableRenderChaseSnapshot blocked;
        CHECK(portable_render_chase_snapshot(&blocked, &s, &s, 0) && frame(&blocked));
        /* Nose against a wall: the look target stays on the kart's side
         * and the eye stays behind the kart, never past the wall. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        cross_wall(&f, .6);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.target[1] < .6 && s.eye[1] < 0);
        CHECK(portable_render_chase_snapshot(&blocked, &s, &s, 0) && frame(&blocked));
        /* Backed against a wall: the pulled-in boom keeps the open view's
         * pitch instead of looking down onto the kart and losing the sky. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        PortableRenderChaseSnapshot open;
        CHECK(portable_render_chase_snapshot(&open, &s, &s, 0));
        cross_wall(&f, -.8);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.eye[1] > -.8 && portable_render_chase_snapshot(&blocked, &s, &s, 0) && frame(&blocked));
        CHECK(fabs(blocked.forward[2] / hypot(blocked.forward[1], blocked.forward[2]) -
                   open.forward[2] / hypot(open.forward[1], open.forward[2])) < .02);
        /* Up onto a deck 2 m above: the lagging anchor stays at most 1.2 m
         * below the kart, and the full boom is taken from above the deck
         * rather than squeezed out from under it. */
        f = (Fixture){.fail = -1};
        horizontal(&f, 0);
        horizontal(&f, 2);
        c = course(&f, indexed);
        s = (PortableRenderChase){0};
        PortableRenderChaseInput low = initial();
        low.immersive = (uint8_t)immersive;
        CHECK(portable_render_chase_step(&s, &s, &low, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        PortableRenderChaseInput high = low;
        ++high.tick;
        high.foot.position[2] = 2;
        CHECK(portable_render_chase_step(&s, &s, &high, &c, error, sizeof error) ==
              PORTABLE_RENDER_CHASE_OK);
        CHECK(s.anchor[2] > .79 && s.boom > .9 && s.eye[2] > 2.1 && s.target[2] > 2);
    }
    return 0;
}

/* Gravity turning a full half turn (a ceiling) rolls the camera over
 * instead of stalling with its up opposite gravity's. */
static int inversion(int immersive)
{
    Fixture f = {.fail = -1};
    horizontal(&f, -20);
    PortableMeshCourse c = course(&f, 1);
    PortableRenderChase s = {0};
    PortableRenderChaseInput in = initial();
    in.immersive = (uint8_t)immersive;
    in.gravity_up[2] = 1;
    char error[128];
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    in.gravity_up[2] = -1;
    in.foot.up[2] = -1;
    /* The roll is capped at 0.035 rad a tick, as MK8's: a half turn in 90. */
    for (unsigned tick = 0; tick < 100; ++tick)
    {
        ++in.tick;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
        CHECK(tick >= 80 || s.up[2] > -.99);
    }
    CHECK(s.up[2] < -.99);
    PortableRenderChaseSnapshot view;
    CHECK(portable_render_chase_snapshot(&view, &s, &s, 0) && view.up[2] < -.9);
    return 0;
}

static int rollback(void)
{
    Fixture f = {.fail = -1};
    wall(&f, -5);
    PortableMeshCourse c = course(&f, 1);
    PortableRenderChaseInput in = initial();
    PortableRenderChase s = {0};
    char error[128];
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OK);
    const PortableRenderChase saved = s;
    ++in.tick;
    in.foot.position[0] = NAN;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_INVALID);
    CHECK(!memcmp(&saved, &s, sizeof s) && strstr(error, "chase camera"));
    in = initial();
    in.tick = 12;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_INVALID);
    CHECK(!memcmp(&saved, &s, sizeof s));
    in.tick = 11;
    f.fail = 1;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_INVALID);
    CHECK(!memcmp(&saved, &s, sizeof s));
    f.fail = -1;
    PortableRenderChase distant = saved;
    distant.eye[1] = -30;
    CHECK(portable_render_chase_step(&s, &distant, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_BUDGET);
    CHECK(!memcmp(&saved, &s, sizeof s));
    CHECK(strstr(error, "movement sample budget"));
    for (unsigned i = 2; i < 257; ++i)
        f.rows[i] = f.rows[0];
    f.count = 257;
    c = course(&f, 1);
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_BUDGET);
    CHECK(!memcmp(&saved, &s, sizeof s));
    c.next_triangle = NULL;
    c.triangle_count = 513;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_BUDGET);
    CHECK(!memcmp(&saved, &s, sizeof s));
    PortableRenderChaseSnapshot out = {0}, copy = out;
    PortableRenderChase bad = s;
    bad.eye[0] = NAN;
    CHECK(!portable_render_chase_snapshot(&out, &s, &bad, 32768));
    CHECK(!memcmp(&out, &copy, sizeof out));
    /* A named obstruction fails transactionally instead of placing inside a slab. */
    f = (Fixture){.fail = -1};
    horizontal(&f, 0);
    for (unsigned i = 1; i <= 16; ++i)
        horizontal(&f, (double)i * .2);
    c = course(&f, 1);
    in = initial();
    in.tick = 11;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) ==
          PORTABLE_RENDER_CHASE_OBSTRUCTED);
    CHECK(!memcmp(&saved, &s, sizeof s));
    return 0;
}

static int immersive_camera(void)
{
    Fixture f = {.fail = -1};
    horizontal(&f, 0);
    PortableMeshCourse c = course(&f, 1);
    PortableRenderChaseInput in = initial();
    PortableRenderChase s = {0}, stock = {0};
    char error[128];
    CHECK(portable_render_chase_step(&stock, &stock, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    in.immersive = in.dynamic = in.shake = 1;
    const PortableRenderChaseInput unchanged = in;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(!memcmp(&unchanged, &in, sizeof in));
    CHECK(fabs(s.eye[1] + 3.05) < 1e-8 && fabs(s.eye[2] - 1.25) < 1e-8);
    CHECK(s.eye[1] > stock.eye[1] && s.eye[2] < stock.eye[2]);
    CHECK(s.motion_pitch == 0 && s.lean == 0 && s.shake == 0);
    const PortableRenderChase idle = s;
    /* Smooth acceleration and a corner, mirrored below. */
    PortableRenderChase right = s;
    for (unsigned t = 1; t <= 60; ++t)
    {
        PortableRenderChase before = s;
        ++in.tick;
        in.speed_ratio_q16 = (int32_t)(t * Q / 60);
        in.foot.forward[0] = sin(t * .01);
        in.foot.forward[1] = cos(t * .01);
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
        PortableRenderChaseInput mirror = in;
        mirror.foot.forward[0] = -mirror.foot.forward[0];
        CHECK(portable_render_chase_step(&right, &right, &mirror, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
        CHECK(fabs(s.lean + right.lean) < 1e-10 && fabs(s.motion_pitch - right.motion_pitch) < 1e-10);
        CHECK(fabs(s.motion_pitch) <= .025 && fabs(s.lean) <= .03);
        CHECK(fabs(s.lean - before.lean) < .003 && fabs(s.motion_pitch - before.motion_pitch) < .003);
        CHECK(fabs(s.focal_scale - before.focal_scale) < .01);
        PortableRenderChaseSnapshot view;
        const PortableRenderChase frozen = s;
        for (unsigned alpha = 0; alpha <= Q; alpha += 4096)
            CHECK(portable_render_chase_snapshot(&view, &before, &s, alpha) && orthonormal(&view));
        CHECK(!memcmp(&frozen, &s, sizeof s));
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
        CHECK(!memcmp(&frozen, &s, sizeof s)); /* Repeated render/pause ticks. */
    }
    CHECK(s.motion_pitch < -.005 && s.lean < -.01 && s.focal_scale < .86);
    const PortableRenderChase cruising = s;
    for (unsigned t = 0; t < 60; ++t)
    {
        ++in.tick;
        in.boosting = 1;
        in.speed_ratio_q16 = 2 * Q;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
        CHECK(s.focal_scale >= .7 && s.distance < 3.87 && s.shake <= .045);
    }
    CHECK(s.distance > cruising.distance + .45 && s.focal_scale < cruising.focal_scale);
    for (unsigned t = 1; t <= 60; ++t)
    {
        ++in.tick;
        in.boosting = 0;
        in.speed_ratio_q16 = 2 * Q - (int32_t)(t * Q / 30);
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    }
    CHECK(s.motion_pitch > .01); /* Braking pitches down, independently of shake. */
    for (unsigned t = 0; t < 30; ++t)
    {
        ++in.tick;
        in.grounded = 0;
        in.foot.position[2] = 1;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    }
    ++in.tick;
    in.grounded = 1;
    in.foot.position[2] = 0;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(s.shake > 0 && s.shake <= .045);
    ++in.tick;
    in.spinning = 1;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(s.shake == .045); /* Bounded impact. */
    ++in.tick;
    in.shake = 0;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(s.shake == 0 && s.jolt[0] == 0 && s.jolt[1] == 0 && s.jolt[2] == 0);
    in.dynamic = 0;
    for (unsigned t = 0; t < 240; ++t)
    {
        ++in.tick;
        CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    }
    CHECK(fabs(s.lean) < 1e-8 && fabs(s.motion_pitch) < 1e-8 && fabs(s.focal_scale - 1) < 1e-8);
    CHECK(fabs(s.distance - idle.distance) < 1e-6);
    /* Recovery and mode changes clear inertia and cut interpolation. */
    in.cut = 1;
    in.tick = 0;
    in.foot.forward[0] = 0;
    in.foot.forward[1] = 1;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(s.discontinuous && s.shake == 0 && s.motion_pitch == 0 && s.lean == 0);
    PortableRenderChase before = s;
    in.cut = 0;
    in.immersive = 0;
    ++in.tick;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(s.discontinuous && fabs(s.eye[1] - stock.eye[1]) < 1e-8 && fabs(s.eye[2] - stock.eye[2]) < 1e-8);
    PortableRenderChaseSnapshot view;
    CHECK(portable_render_chase_snapshot(&view, &before, &s, 0) && view.position[1] == s.eye[1]);
    before = s;
    in.immersive = 2;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_INVALID);
    CHECK(!memcmp(&before, &s, sizeof s));
    return 0;
}

static int rig(void)
{
    PortableRenderChaseRig r;
    CHECK(portable_render_chase_rig(&r, NULL, NULL, 1));
    CHECK(r.trail == 3.9 && r.height == 1.7 && r.look_height == .85 && r.look_ahead == 1.9);
    /* MK64 1P race: eye (0, -2.841, .852), look-at (0, 3.977, .3125) metres. */
    const double eye[3] = {0, -2.841, .852}, target[3] = {0, 3.977, .3125};
    CHECK(portable_render_chase_rig(&r, eye, target, 1));
    const double pitch = (.852 - .3125) / (2.841 + 3.977);
    CHECK(fabs(r.trail - 2.841) < 1e-9 && fabs(r.height - .852) < 1e-9);
    CHECK(fabs(r.look_height - (.852 - pitch * 2.841)) < 1e-9);
    CHECK(fabs(r.look_ahead - 3.977 * sqrt(1 + pitch * pitch)) < 1e-9);
    const double front[3] = {0, .5, 1}, low[3] = {0, -3, .1}, high[3] = {0, 0, .5};
    CHECK(!portable_render_chase_rig(&r, front, target, 1));
    CHECK(!portable_render_chase_rig(&r, low, high, 1));
    CHECK(portable_render_chase_rig(&r, NULL, NULL, .5) && r.trail == 1.95 && r.height == .85);

    Fixture f = {.fail = -1};
    horizontal(&f, 0);
    PortableMeshCourse c = course(&f, 1);
    PortableRenderChaseInput in = initial();
    CHECK(portable_render_chase_rig(&in.rig, eye, target, 1));
    PortableRenderChase s = {0};
    char error[128];
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_OK);
    CHECK(fabs(s.eye[1] + 2.841) < 1e-8 && fabs(s.eye[2] - .852) < 1e-8 && fabs(s.distance - 2.841) < 1e-8);
    s = (PortableRenderChase){0};
    in.rig.trail = 20;
    CHECK(portable_render_chase_step(&s, &s, &in, &c, error, sizeof error) == PORTABLE_RENDER_CHASE_INVALID);
    return 0;
}

int main(void)
{
    CHECK(rig() == 0);
    CHECK(accepted_camera() == 0);
    CHECK(collision(0) == 0);
    CHECK(collision(1) == 0);
    CHECK(immersive_camera() == 0);
    CHECK(rollback() == 0);
    CHECK(inversion(0) == 0);
    CHECK(inversion(1) == 0);
    return 0;
}
