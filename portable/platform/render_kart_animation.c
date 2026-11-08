#include "render_kart_animation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define TWO_PI (2 * PI)

static int fail(char *error, size_t capacity, const char *reason)
{
    if (error && capacity)
        (void)snprintf(error, capacity, "kart animation: %s", reason);
    return 0;
}

static int pose_valid(const PortableRenderPose *pose)
{
    PortableRenderPose normalized;
    if (!pose || !portable_render_pose_interpolate(&normalized, pose, pose, 0))
        return 0;
    double up = 0, forward = 0, dot = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        up += pose->up[a] * pose->up[a];
        forward += pose->forward[a] * pose->forward[a];
        dot += pose->up[a] * pose->forward[a];
    }
    return fabs(up - 1) < 1e-6 && fabs(forward - 1) < 1e-6 && fabs(dot) < 1e-6;
}

static int lifetime_valid(uint32_t lifetime)
{
    return lifetime == 0 || (lifetime >= 2 && lifetime <= 65535);
}

static int age_valid(uint16_t age, uint32_t lifetime)
{
    return lifetime_valid(lifetime) && (age == 0 || (lifetime && age < lifetime));
}

static int state_valid(const PortableRenderKartAnimation *s)
{
    if (!s || s->valid != 1 || !pose_valid(&s->pose) || !isfinite(s->lean) ||
        fabs(s->lean) > .180001 || !isfinite(s->bob) || s->bob < 0 || s->bob > .025001 ||
        !isfinite(s->status_spin) || !isfinite(s->spin_phase) || !isfinite(s->wheel_distance) || !isfinite(s->phase) || s->phase < 0 || s->phase >= TWO_PI || s->steer_q16 < -65536 ||
        s->steer_q16 > 65536 || s->speed_q16 < -16777216 || s->speed_q16 > 16777216 ||
        s->top_speed_q16 <= 0 || s->top_speed_q16 > 16777216 || s->grounded > 1 ||
        s->drifting > 1 || s->spark_tier > 3 || s->boosting > 1 || s->wheel_phase > 2 ||
        s->hop_age > 6 || s->landing_age > 10 || !age_valid(s->spark_age, s->spark_lifetime) ||
        !age_valid(s->flame_age, s->flame_lifetime))
        return 0;
    for (unsigned a = 0; a < 3; ++a)
        if (!isfinite(s->status_scale[a]) || s->status_scale[a] <= 0 ||
            !isfinite(s->body_scale[a]) || s->body_scale[a] < .8 || s->body_scale[a] > 1.13)
            return 0;
    return 1;
}

static double speed_ratio(int32_t speed, int32_t top)
{
    return fmin(1, fabs((double)speed) / top);
}

int portable_render_kart_animation_step(PortableRenderKartAnimation *out,
                                        const PortableRenderKartAnimation *before,
                                        const PortableRenderKartAnimationInput *input, char *error,
                                        size_t capacity)
{
    if (!out || !before || !input || before->valid > 1 || (before->valid && !state_valid(before)) ||
        !pose_valid(&input->foot) || !portable_mesh_kart_state_valid(&input->kart) ||
        input->top_speed_q16 <= 0 || input->top_speed_q16 > 16777216 || input->roughness_q16 < 0 ||
        input->roughness_q16 > 65536 || input->boosting > 1 || input->cut > 1 ||
        !lifetime_valid(input->spark_lifetime) || !lifetime_valid(input->flame_lifetime))
        return fail(error, capacity, "invalid accepted pose or owner");
    if (before->valid && !input->cut && input->tick == before->tick)
    {
        *out = *before;
        return 1;
    }
    if (before->valid && !input->cut && input->tick != before->tick + 1u)
        return fail(error, capacity, "nonconsecutive accepted tick");
    const int reset = !before->valid || input->cut;
    PortableRenderKartAnimation s = reset ? (PortableRenderKartAnimation){0} : *before;
    const double ratio = speed_ratio(input->kart.speed_q16, input->top_speed_q16);
    const int drifting = input->kart.drift_direction != 0;
    double lean = input->kart.steer_filtered_q16 / 65536.0 * .10 * ratio;
    if (drifting)
        lean += input->kart.drift_direction * .08 * ratio;
    if (!input->kart.grounded)
        lean = 0;
    s.lean = reset ? lean : s.lean + .22 * (lean - s.lean);
    s.phase = fmod(s.phase + fabs((double)input->kart.speed_q16) / 65536.0 * (4389.0 / 262144.0) *
                                 TWO_PI / 1.4,
                   TWO_PI);
    s.bob = input->kart.grounded
                ? fabs(sin(s.phase)) * input->roughness_q16 / 65536.0 * .025 * ratio
                : 0;
    s.hop_age = reset ? 0
                      : (uint16_t)(before->grounded && !input->kart.grounded &&
                                           input->kart.velocity_q16[2] > 0
                                       ? 1
                                   : before->hop_age && before->hop_age < 6 && !input->kart.grounded
                                       ? before->hop_age + 1
                                       : 0);
    s.landing_age =
        reset ? 0
              : (uint16_t)(!before->grounded && input->kart.grounded ? 1
                           : before->landing_age && before->landing_age < 10 && input->kart.grounded
                               ? before->landing_age + 1
                               : 0);
    s.body_scale[0] = s.body_scale[1] = s.body_scale[2] = 1;
    if (s.hop_age)
    {
        const double weight = (7 - s.hop_age) / 6.0;
        s.body_scale[0] = s.body_scale[1] = 1 + .06 * weight;
        s.body_scale[2] = 1 - .10 * weight;
    }
    if (s.landing_age)
    {
        const double weight = (11 - s.landing_age) / 10.0;
        s.body_scale[0] = s.body_scale[1] = 1 + .10 * weight;
        s.body_scale[2] = 1 - .18 * weight;
    }
    s.spark_lifetime = input->spark_lifetime;
    s.flame_lifetime = input->flame_lifetime;
    s.spark_age = input->spark_lifetime && input->kart.grounded && drifting &&
                          fabs((double)input->kart.speed_q16) > 65536
                      ? (uint16_t)(s.spark_age && (uint32_t)s.spark_age + 1 < s.spark_lifetime
                                       ? s.spark_age + 1
                                       : 1)
                      : 0;
    s.flame_age = input->flame_lifetime && input->boosting
                      ? (uint16_t)(s.flame_age && (uint32_t)s.flame_age + 1 < s.flame_lifetime
                                       ? s.flame_age + 1
                                       : 1)
                      : 0;
    s.spark_tier = input->kart.drift_tier;
    s.pose = input->foot;
    const double right[3] = {
        input->foot.up[1] * input->foot.forward[2] - input->foot.up[2] * input->foot.forward[1],
        input->foot.up[2] * input->foot.forward[0] - input->foot.up[0] * input->foot.forward[2],
        input->foot.up[0] * input->foot.forward[1] - input->foot.up[1] * input->foot.forward[0]};
    for (unsigned a = 0; a < 3; ++a)
    {
        s.pose.up[a] = input->foot.up[a] * cos(s.lean) + right[a] * sin(s.lean);
        s.pose.position[a] += input->foot.up[a] * s.bob;
    }
    if (input->art_rules)
    {
        const int32_t wheel_speed = (int32_t)llround(ratio * 15 * 65536);
        if (!portable_driver_art_motion_tick_palette(&s.wheels, input->art_rules, wheel_speed, 0))
            return fail(error, capacity, "invalid source wheel schedule");
        s.wheel_phase = s.wheels.wheel_phase;
    }
    else
        s.wheel_phase = (uint8_t)((unsigned)(s.phase * 3 / TWO_PI) % 3u);
    /* Signed distance belongs to accepted presentation ticks, never draw calls. */
    s.wheel_distance += reset ? 0 : input->kart.speed_q16 / 65536.0 * (4389.0 / 262144.0);
    s.status_spin = input->status_spin;
    s.spin_phase = input->spin_phase;
    for (unsigned a = 0; a < 3; ++a)
        s.status_scale[a] = input->status_scale[a] == 0 ? 1 : input->status_scale[a];
    s.steer_q16 = input->kart.steer_filtered_q16;
    s.speed_q16 = input->kart.speed_q16;
    s.top_speed_q16 = input->top_speed_q16;
    s.grounded = input->kart.grounded;
    s.drifting = (uint8_t)drifting;
    s.boosting = input->boosting;
    s.tick = input->tick;
    s.valid = 1;
    if (!state_valid(&s))
        return fail(error, capacity, "invalid staged animation");
    *out = s;
    return 1;
}

int portable_render_kart_animation_snapshot(PortableRenderKartAnimationSnapshot *out,
                                            const PortableRenderKartAnimation *previous,
                                            const PortableRenderKartAnimation *current,
                                            uint32_t alpha_q16)
{
    if (!out || !state_valid(previous) || !state_valid(current))
        return 0;
    PortableRenderKartAnimationSnapshot s = {0};
    const double alpha = fmin(alpha_q16, 65536u) / 65536.0;
    if (!portable_render_pose_interpolate(&s.pose, &previous->pose, &current->pose, alpha_q16))
        return 0;
    for (unsigned a = 0; a < 3; ++a)
        s.body_scale[a] =
            previous->body_scale[a] + alpha * (current->body_scale[a] - previous->body_scale[a]);
    s.status_spin = previous->status_spin + alpha * remainder(current->status_spin - previous->status_spin, TWO_PI);
    s.spin_phase = previous->spin_phase + alpha * (current->spin_phase - previous->spin_phase);
    for (unsigned a = 0; a < 3; ++a)
        s.status_scale[a] = previous->status_scale[a] + alpha * (current->status_scale[a] - previous->status_scale[a]);
    s.lean = previous->lean + alpha * (current->lean - previous->lean);
    s.bob = previous->bob + alpha * (current->bob - previous->bob);
    s.wheel_distance = previous->wheel_distance + alpha * (current->wheel_distance - previous->wheel_distance);
    s.tick = previous->tick + alpha * ((double)current->tick - previous->tick);
    s.steer_q16 = (int32_t)llround(previous->steer_q16 + alpha * ((double)current->steer_q16 - previous->steer_q16));
    s.speed_q16 = (int32_t)llround(previous->speed_q16 + alpha * ((double)current->speed_q16 - previous->speed_q16));
    s.top_speed_q16 = current->top_speed_q16;
    s.spark_age = current->spark_age;
    s.flame_age = current->flame_age;
    s.spark_active = current->spark_age != 0;
    s.flame_active = current->flame_age != 0;
    s.spark_tier = current->spark_tier;
    s.spark_phase = (uint16_t)(current->spark_age ? current->spark_age - 1 : 0);
    s.flame_phase = (uint16_t)(current->flame_age ? current->flame_age - 1 : 0);
    s.wheel_phase = current->wheel_phase;
    *out = s;
    return 1;
}

static uint16_t angle_units(double y, double x)
{
    double units = atan2(y, x) * 65536 / TWO_PI;
    if (units < 0)
        units += 65536;
    return (uint16_t)(unsigned)floor(units);
}

int portable_render_kart_animation_view(const PortableRenderKartAnimationSnapshot *s,
                                        const PortableRenderView *view,
                                        const PortableCharacterViewRules *rules, int selected,
                                        PortableCharacterViewPose *out, double *yaw)
{
    if (!s || !view || !out || !yaw || (selected != 0 && selected != 1) || !pose_valid(&s->pose) ||
        s->steer_q16 < -65536 || s->steer_q16 > 65536 || s->speed_q16 < -16777216 ||
        s->speed_q16 > 16777216 || s->top_speed_q16 <= 0 || s->top_speed_q16 > 16777216)
        return 0;
    double x = s->pose.position[0] - view->position[0], y = s->pose.position[1] - view->position[1];
    for (unsigned a = 0; a < 3; ++a)
        if (!isfinite(view->position[a]) || !isfinite(view->forward[a]))
            return 0;
    if (hypot(x, y) < 1e-12)
    {
        x = view->forward[0];
        y = view->forward[1];
    }
    uint16_t driver = 0, camera = 0;
    if (hypot(s->pose.forward[0], s->pose.forward[1]) >= 1e-12 && hypot(x, y) >= 1e-12)
    {
        driver = angle_units(s->pose.forward[1], s->pose.forward[0]);
        camera = angle_units(y, x);
    }
    const int response = (int)llround(s->steer_q16 / 65536.0 * 14) * 256;
    const int bin = (int)floor(speed_ratio(s->speed_q16, s->top_speed_q16) * 14);
    const int32_t mapped_speed = s->speed_q16 < 0 ? -(bin ? bin * 2097152 : 1) : bin * 2097152;
    PortableCharacterViewInput input = {.camera_yaw = camera,
                                        .driver_angle = driver,
                                        .steering_response_q8 = (int16_t)response,
                                        .speed_q16 = mapped_speed,
                                        .selected = (uint8_t)(selected && rules != NULL)};
    const PortableCharacterViewPose pose = portable_character_view_pose(&input, rules);
    const double correction = -portable_character_view_correction(&input, rules) * TWO_PI / 256;
    *out = pose;
    *yaw = correction;
    return 1;
}

int portable_render_kart_animation_foot_lift(const PortableRenderInstance *unleaned,
                                             const PortableRenderInstance *animated,
                                             const PortableRenderView *view, const double bounds[6],
                                             const double support_normal[3], double *out)
{
    if (!unleaned || !animated || !view || !bounds || !support_normal || !out)
        return 0;
    double square = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        if (!isfinite(support_normal[a]) || !isfinite(bounds[a]) || !isfinite(bounds[a + 3]) ||
            fabs(bounds[a]) > 32768 || fabs(bounds[a + 3]) > 32768 || bounds[a] > bounds[a + 3])
            return 0;
        square += support_normal[a] * support_normal[a];
    }
    if (!isfinite(square) || fabs(square - 1) > 1e-6)
        return 0;
    PortableRenderInstance resolved[2];
    if (!portable_render_instance_for_view(unleaned, view, &resolved[0]) ||
        !portable_render_instance_for_view(animated, view, &resolved[1]))
        return 0;
    double minimum[2] = {0, 0};
    for (unsigned instance = 0; instance < 2; ++instance)
    {
        for (unsigned component = 0; component < 12; ++component)
            if (!isfinite(resolved[instance].transform[component]) ||
                fabs(resolved[instance].transform[component]) >
                    (component % 4 == 3 ? 4096.0 : 32768.0))
                return 0;
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            double projection = 0;
            for (unsigned row = 0; row < 3; ++row)
            {
                double position = resolved[instance].transform[row * 4 + 3];
                for (unsigned a = 0; a < 3; ++a)
                    position += resolved[instance].transform[row * 4 + a] *
                                bounds[a + ((corner & (1u << a)) ? 3u : 0u)];
                projection += position * support_normal[row];
            }
            if (!isfinite(projection))
                return 0;
            if (!corner || projection < minimum[instance])
                minimum[instance] = projection;
        }
    }
    const double lift = fmax(0, minimum[0] - minimum[1]);
    if (!isfinite(lift))
        return 0;
    *out = lift;
    return 1;
}
