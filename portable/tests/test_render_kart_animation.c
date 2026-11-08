#include "render_kart_animation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define Q 65536
#define PI 3.14159265358979323846
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static PortableRenderKartAnimationInput initial(void)
{
    return (PortableRenderKartAnimationInput){
        .foot = {.position = {3, 4, 0}, .forward = {0, 1, 0}, .up = {0, 0, 1}},
        .kart = {.forward_q30 = {0, 1073741824, 0}, .up_q30 = {0, 0, 1073741824}, .grounded = 1},
        .tick = 10,
        .spark_lifetime = 22,
        .flame_lifetime = 52,
        .top_speed_q16 = 24 * Q};
}

static int transitions(void)
{
    PortableRenderKartAnimationInput in = initial();
    PortableRenderKartAnimation s = {0};
    char error[128];
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.pose.position[0] == 3 && s.pose.position[1] == 4 && s.pose.position[2] == 0);
    CHECK(s.body_scale[2] == 1 && !s.hop_age && !s.landing_age && !s.lean);
    const PortableRenderKartAnimation idle = s;
    in.kart.speed_q16 = 24 * Q;
    in.kart.steer_filtered_q16 = Q;
    in.kart.drift_direction = 1;
    in.kart.drift_tier = 2;
    in.boosting = 1;
    in.roughness_q16 = Q;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!memcmp(&idle, &s, sizeof s)); /* Pause cannot accumulate effects. */
    for (unsigned t = 0; t < 60; ++t)
    {
        ++in.tick;
        CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
        CHECK(s.bob >= 0 && s.bob <= .025 && s.pose.position[2] >= 0);
        CHECK(s.spark_age && s.flame_age && s.spark_tier == 2);
    }
    CHECK(s.lean > .179 && s.lean <= .18 && s.pose.up[0] < -.17);
    CHECK(s.body_scale[2] == 1); /* Lean never changes body footprint/pivot scale. */
    PortableRenderKartAnimationSnapshot snapshot;
    const PortableRenderKartAnimation saved = s;
    for (unsigned alpha = 0; alpha <= 65536; alpha += 4096)
    {
        CHECK(portable_render_kart_animation_snapshot(&snapshot, &idle, &s, alpha));
        CHECK(snapshot.spark_active && snapshot.flame_active && snapshot.spark_phase < 21 &&
              snapshot.flame_phase < 51);
        CHECK(snapshot.body_scale[0] == 1 && snapshot.pose.position[2] >= 0);
    }
    CHECK(!memcmp(&saved, &s, sizeof s));
    in.kart.drift_direction = 0;
    in.kart.steer_filtered_q16 = 0;
    in.boosting = 0;
    in.roughness_q16 = 0;
    for (unsigned t = 0; t < 50; ++t)
    {
        ++in.tick;
        CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    }
    CHECK(fabs(s.lean) < .000001 && !s.spark_age && !s.flame_age && !s.bob);
    /* Actual hop: animate only a positive-velocity departure, never a stepdown. */
    ++in.tick;
    in.kart.grounded = 0;
    in.kart.velocity_q16[2] = 3 * Q;
    in.foot.position[2] = .05;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.hop_age == 1 && fabs(s.body_scale[2] - .9) < 1e-9 && s.body_scale[0] > 1);
    CHECK(s.pose.position[2] == in.foot.position[2] && !s.bob && !s.spark_age);
    for (unsigned t = 0; t < 6; ++t)
    {
        ++in.tick;
        CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    }
    CHECK(!s.hop_age && s.body_scale[2] == 1);
    ++in.tick;
    in.kart.grounded = 1;
    in.kart.velocity_q16[2] = 0;
    in.foot.position[2] = 0;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.landing_age == 1 && fabs(s.body_scale[2] - .82) < 1e-9 && s.pose.position[2] == 0);
    double previous = s.body_scale[2];
    for (unsigned t = 0; t < 10; ++t)
    {
        ++in.tick;
        CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
        CHECK(s.body_scale[2] >= previous && s.pose.position[2] == 0);
        previous = s.body_scale[2];
    }
    CHECK(!s.landing_age && s.body_scale[2] == 1);
    ++in.tick;
    in.kart.grounded = 0;
    in.kart.velocity_q16[2] = -Q;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!s.hop_age && s.body_scale[2] == 1);
    /* Retry resets clocks/transients rather than inventing a landing event. */
    in.cut = 1;
    in.tick = 1;
    in.kart.grounded = 1;
    in.foot.position[0] = 100;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.tick == 1 && !s.landing_age && !s.hop_age && s.pose.position[0] == 100);
    return 0;
}

static int authored_effect_clocks(void)
{
    PortableRenderKartAnimationInput in = initial();
    in.spark_lifetime = 5;
    in.flame_lifetime = 301;
    in.kart.speed_q16 = 12 * Q;
    in.kart.drift_direction = 1;
    in.boosting = 1;
    PortableRenderKartAnimation s = {0};
    char error[128];
    for (unsigned i = 0; i < 310; ++i)
    {
        ++in.tick;
        CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
        CHECK(s.spark_age == 1 + i % 4 && s.flame_age == 1 + i % 300);
    }
    CHECK(s.spark_lifetime == 5 && s.flame_lifetime == 301);
    in.spark_lifetime = 0;
    in.flame_lifetime = 0;
    ++in.tick;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!s.spark_age && !s.flame_age);
    const PortableRenderKartAnimation saved = s;
    in.spark_lifetime = 65536;
    ++in.tick;
    CHECK(!portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!memcmp(&s, &saved, sizeof s));
    in.spark_lifetime = 65535;
    in.flame_lifetime = 2;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.spark_age == 1 && s.flame_age == 1);
    ++in.tick;
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.spark_age == 2 && s.flame_age == 1);
    return 0;
}

static int source_views(void)
{
    PortableRenderKartAnimationInput in = initial();
    in.kart.speed_q16 = 12 * Q;
    in.kart.steer_filtered_q16 = Q / 2;
    PortableRenderKartAnimation s = {0};
    char error[128];
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    PortableRenderKartAnimationSnapshot snapshot;
    CHECK(portable_render_kart_animation_snapshot(&snapshot, &s, &s, 65536));
    PortableCharacterViewRules rules;
    for (unsigned i = 0; i < 15; ++i)
        rules.response_limit[i] = rules.speed_limit[i] = (uint8_t)i;
    PortableRenderView view = {.position = {3, -6, 0}, .forward = {0, 1, 0}};
    PortableCharacterViewPose pose;
    double yaw;
    CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 1, &pose, &yaw));
    CHECK(pose.angle == 124 && pose.mirror == 1 && fabs(yaw - 9 * 2 * PI / 256) < 1e-12);
    /* Independent source oracle: normalized half steer/speed select bin7.
     * This catches accidental SI speed>>21 (which would wrongly select bin0). */
    PortableCharacterViewInput oracle = {.camera_yaw = 16384,
                                         .driver_angle = 16384,
                                         .steering_response_q8 = 7 * 256,
                                         .speed_q16 = 7 * 2097152,
                                         .selected = 1};
    const PortableCharacterViewPose expected = portable_character_view_pose(&oracle, &rules);
    CHECK(!memcmp(&pose, &expected, sizeof pose));
    CHECK(fabs(yaw + (double)portable_character_view_correction(&oracle, &rules) * 2 * PI / 256) <
          1e-12);
    CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 0, &pose, &yaw));
    CHECK(pose.angle == 0 && pose.mirror == 1 && yaw == 0);
    view.position[0] = -7;
    view.position[1] = 4;
    CHECK(portable_render_kart_animation_view(&snapshot, &view, NULL, 1, &pose, &yaw));
    CHECK(pose.angle == 96 && yaw == 0); /* Side view, missing optional legacy rules. */
    view.position[0] = 3;
    view.position[1] = 14;
    CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 0, &pose, &yaw));
    CHECK(pose.angle == 64 && yaw == 0);
    const PortableRenderKartAnimationSnapshot before = snapshot;
    snapshot.speed_q16 = -12 * Q;
    CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 1, &pose, &yaw));
    CHECK(pose.angle == 64 && yaw == 0); /* Reverse front view suppresses correction. */
    snapshot.speed_q16 = -1;
    CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 1, &pose, &yaw));
    CHECK(pose.angle == 64 && yaw == 0); /* Preserve reverse below the first mapped speed bin. */
    snapshot = before;
    snapshot.steer_q16 = -Q / 2;
    view.position[1] = -6;
    CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 1, &pose, &yaw));
    CHECK(pose.angle == 3 && pose.mirror == 0 && fabs(yaw + 7 * 2 * PI / 256) < 1e-12);
    /* Same immutable pose and correction feed models and sprites; per-camera
     * queries cannot consume animation phases or move the supported foot. */
    const PortableRenderKartAnimationSnapshot fixed = snapshot;
    for (unsigned camera = 0; camera < 4; ++camera)
    {
        view.position[0] = 3 + (camera & 1 ? 10 : -10);
        view.position[1] = 4 + (camera & 2 ? 10 : -10);
        CHECK(portable_render_kart_animation_view(&snapshot, &view, &rules, 1, &pose, &yaw));
        CHECK(pose.angle < 128 && fabs(yaw) < .5);
    }
    CHECK(!memcmp(&fixed, &snapshot, sizeof fixed));
    CHECK(!memcmp(&s.pose.position, &in.foot.position, sizeof in.foot.position));
    return 0;
}

static int slope_and_rollback(void)
{
    PortableRenderKartAnimationInput in = initial();
    in.foot.up[0] = .6;
    in.foot.up[2] = .8;
    in.kart.speed_q16 = 24 * Q;
    in.kart.steer_filtered_q16 = -Q;
    in.roughness_q16 = Q;
    PortableRenderKartAnimation s = {0};
    char error[128];
    CHECK(portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(s.lean < 0 && s.bob > 0);
    CHECK(fabs((s.pose.position[0] - in.foot.position[0]) / .6 - s.bob) < 1e-12);
    CHECK(fabs((s.pose.position[2] - in.foot.position[2]) / .8 - s.bob) < 1e-12);
    const PortableRenderKartAnimation saved = s;
    const PortableRenderKartAnimationInput original = in;
    ++in.tick;
    in.foot.position[0] = NAN;
    CHECK(!portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!memcmp(&s, &saved, sizeof s) && strstr(error, "kart animation"));
    in = original;
    in.tick += 2;
    CHECK(!portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!memcmp(&s, &saved, sizeof s));
    in = original;
    ++in.tick;
    in.roughness_q16 = Q + 1;
    CHECK(!portable_render_kart_animation_step(&s, &s, &in, error, sizeof error));
    CHECK(!memcmp(&s, &saved, sizeof s));
    PortableRenderKartAnimation bad = s;
    bad.body_scale[2] = NAN;
    PortableRenderKartAnimationSnapshot out = {0}, copy = out;
    CHECK(!portable_render_kart_animation_snapshot(&out, &s, &bad, 32768));
    CHECK(!memcmp(&out, &copy, sizeof out));
    CHECK(portable_render_kart_animation_snapshot(&out, &s, &s, 65536));
    PortableRenderView view = {.position = {NAN, 0, 0}};
    PortableCharacterViewPose pose = {11, 12, 1}, before = pose;
    double yaw = 17;
    CHECK(!portable_render_kart_animation_view(&out, &view, NULL, 0, &pose, &yaw));
    CHECK(!memcmp(&pose, &before, sizeof pose) && yaw == 17);
    /* Another owner (CPU/ghost/pane) has no shared phases or landing state. */
    in = initial();
    PortableRenderKartAnimation other = {0};
    CHECK(portable_render_kart_animation_step(&other, &other, &in, error, sizeof error));
    CHECK(!other.bob && !other.lean && !other.spark_age && !other.flame_age);
    CHECK(!memcmp(&s, &saved, sizeof s));
    return 0;
}

static int foot_lift(void)
{
    const double angle = .18, c = cos(angle), sn = sin(angle);
    const double normal[3] = {0, 0, 1};
    const double bounds[6] = {-.4, -.6, 0, .4, .6, 1};
    PortableRenderView view = {.right = {1, 0, 0}, .up = {0, 0, 1}, .forward = {0, 1, 0}};
    PortableRenderInstance base, animated;
    portable_render_instance_identity(&base, NULL);
    portable_render_instance_identity(&animated, NULL);
    /* Arbitrary authored pivot is deliberately below the foot. Preserve that
     * existing offset; compensate only the new lean, including nonuniform squash. */
    const double px = .15, py = .2, pz = .3, sx = 1.1, sz = .82;
    base.transform[0] = sx;
    base.transform[5] = sx;
    base.transform[10] = sz;
    base.transform[3] = 2 - sx * px;
    base.transform[7] = 3 - sx * py;
    base.transform[11] = 5 - sz * pz;
    animated = base;
    animated.transform[0] = sx * c;
    animated.transform[2] = sz * sn;
    animated.transform[8] = -sx * sn;
    animated.transform[10] = sz * c;
    animated.transform[3] = 2 - sx * px * c - sz * pz * sn;
    animated.transform[11] = 5 + sx * px * sn - sz * pz * c;
    const PortableRenderInstance before_base = base, before_animated = animated;
    double lift = -1;
    CHECK(portable_render_kart_animation_foot_lift(&base, &animated, &view, bounds, normal, &lift));
    CHECK(fabs(lift - ((.4 - px) * sx * sn + pz * sz * (c - 1))) < 1e-12 && lift > 0);
    /* With this lift the lowest bound keeps the original authored minZ. */
    const double min_animated = animated.transform[11] - .4 * sx * sn;
    CHECK(fabs(min_animated + lift - base.transform[11]) < 1e-12);
    CHECK(!memcmp(&base, &before_base, sizeof base) &&
          !memcmp(&animated, &before_animated, sizeof animated));
    CHECK(portable_render_kart_animation_foot_lift(&base, &base, &view, bounds, normal, &lift) &&
          lift == 0);
    animated = base;
    animated.transform[11] += 1;
    CHECK(
        portable_render_kart_animation_foot_lift(&base, &animated, &view, bounds, normal, &lift) &&
        lift == 0);
    /* Opaque billboard bounds, with an off-center imported cell anchor. The
     * empty transparent parts of the 64px cell do not enlarge compensation. */
    const double opaque[6] = {-.2, 0, -.7, .6, 0, .5};
    portable_render_instance_identity(&base, NULL);
    base.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_UP;
    base.transform[11] = .7;
    base.billboard_scale[0] = base.billboard_scale[1] = base.billboard_scale[2] = 1;
    animated = base;
    animated.transform[2] = -sn;
    animated.transform[10] = c;
    animated.transform[3] = -.7 * sn;
    animated.transform[11] = .7 * c;
    CHECK(portable_render_kart_animation_foot_lift(&base, &animated, &view, opaque, normal, &lift));
    CHECK(fabs(lift - .2 * sn) < 1e-12);
    /* Mirrored asymmetric art uses its mirrored opaque bound, not the same
     * unmirrored width or a full-cell rectangle. */
    const double mirror[6] = {-.6, 0, -.7, .2, 0, .5};
    CHECK(portable_render_kart_animation_foot_lift(&base, &animated, &view, mirror, normal, &lift));
    CHECK(fabs(lift - .6 * sn) < 1e-12);
    view.right[0] = -1;
    CHECK(portable_render_kart_animation_foot_lift(&base, &animated, &view, opaque, normal, &lift));
    CHECK(fabs(lift - .6 * sn) < 1e-12);
    /* A banked original floor normal receives the same relative correction. */
    view.right[0] = 1;
    const double banked_normal[3] = {.6, 0, .8};
    const double bank_c = .8, bank_s = .6;
    for (unsigned i = 0; i < 2; ++i)
    {
        PortableRenderInstance *instance = i ? &animated : &base;
        for (unsigned col = 0; col < 4; ++col)
        {
            const double x = instance->transform[col], z = instance->transform[8 + col];
            instance->transform[col] = bank_c * x + bank_s * z;
            instance->transform[8 + col] = -bank_s * x + bank_c * z;
        }
    }
    /* Rotated +Z is(.6,0,.8), while the test rotation above maps X oppositely. */
    CHECK(portable_render_kart_animation_foot_lift(&base, &animated, &view, opaque, banked_normal,
                                                   &lift));
    CHECK(fabs(lift - .2 * sn) < 1e-12);
    const double saved = lift;
    double bad[6];
    memcpy(bad, opaque, sizeof bad);
    bad[0] = NAN;
    CHECK(!portable_render_kart_animation_foot_lift(&base, &animated, &view, bad, normal, &lift));
    CHECK(lift == saved);
    const double zero[3] = {0, 0, 0};
    CHECK(!portable_render_kart_animation_foot_lift(&base, &animated, &view, opaque, zero, &lift));
    CHECK(lift == saved);
    animated.transform[3] = NAN;
    CHECK(
        !portable_render_kart_animation_foot_lift(&base, &animated, &view, opaque, normal, &lift));
    CHECK(lift == saved);
    CHECK(!portable_render_kart_animation_foot_lift(NULL, &base, &view, opaque, normal, &lift));
    CHECK(lift == saved);
    return 0;
}

static int fractional_motion(void)
{
    PortableRenderKartAnimationInput in = initial();
    PortableRenderKartAnimation a = {0}, b;
    char error[128];
    CHECK(portable_render_kart_animation_step(&a, &a, &in, error, sizeof error));
    ++in.tick;
    in.kart.speed_q16 = 12 * Q;
    in.kart.steer_filtered_q16 = Q;
    in.status_spin = 2 * PI - .2;
    in.status_scale[0] = in.status_scale[1] = in.status_scale[2] = .5;
    CHECK(portable_render_kart_animation_step(&b, &a, &in, error, sizeof error));
    const PortableRenderKartAnimation saved = b;
    PortableRenderKartAnimationSnapshot out;
    /* 240 Hz equivalent samples of the same two accepted ticks. */
    for (unsigned alpha = 0; alpha <= Q; alpha += Q / 4)
    {
        CHECK(portable_render_kart_animation_snapshot(&out, &a, &b, alpha));
        const double t = alpha / (double)Q;
        CHECK(fabs(out.wheel_distance - t * 12 * 4389 / 262144.0) < 1e-12);
        CHECK(out.steer_q16 == (int32_t)alpha && out.speed_q16 == (int32_t)(alpha * 12));
        CHECK(fabs(out.tick - (10 + t)) < 1e-12);
        CHECK(fabs(out.status_spin + t * .2) < 1e-12);
        CHECK(fabs(out.status_scale[2] - (1 - t * .5)) < 1e-12);
    }
    CHECK(!memcmp(&b, &saved, sizeof b));
    /* Repeated draw/tick cannot accumulate tire distance; reverse rolls back. */
    CHECK(portable_render_kart_animation_step(&b, &b, &in, error, sizeof error));
    CHECK(!memcmp(&b, &saved, sizeof b));
    ++in.tick;
    in.kart.speed_q16 = -12 * Q;
    CHECK(portable_render_kart_animation_step(&b, &b, &in, error, sizeof error));
    CHECK(fabs(b.wheel_distance) < 1e-12);
    in.cut = 1;
    in.tick = 0;
    CHECK(portable_render_kart_animation_step(&b, &b, &in, error, sizeof error));
    CHECK(b.wheel_distance == 0);
    CHECK(portable_render_kart_animation_snapshot(&out, &b, &b, 12345));
    CHECK(out.wheel_distance == 0 && out.tick == 0);
    return 0;
}

int main(void)
{
    CHECK(fractional_motion() == 0);
    CHECK(foot_lift() == 0);
    CHECK(transitions() == 0);
    CHECK(authored_effect_clocks() == 0);
    CHECK(source_views() == 0);
    CHECK(slope_and_rollback() == 0);
    return 0;
}
