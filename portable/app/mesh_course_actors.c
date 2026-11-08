#include "source_random.h"
#include "mesh_course_actors.h"
#include "fixed_3d.h"
#include "mesh_query.h"
#include "driver_vector_effect.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t signed_word(const uint8_t *p)
{
    uint32_t n = word(p);
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static int fail(char *error, size_t cap, uint32_t course, const char *why)
{
    if (error && cap)
        snprintf(error, cap, "3D course=%u source actors: %s", course, why);
    return 0;
}
static int integer(const PortableCourse3DView *c, uint32_t first, uint32_t count, const char *key,
                   int32_t *out)
{
    PortableCourse3DProperty p;
    if (!portable_course_3d_property_find(c, first, count, key, &p) ||
        p.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER)
        return 0;
    *out = p.value.integer;
    return 1;
}
unsigned portable_mesh_course_actors_config_count(const PortableMeshCourseActorsConfig *c)
{
    return c && c->count <= 64 && c->snowman_count <= 64 && c->penguin_count <= 64 &&
                   c->mouse_count <= 64 && c->piranha_count <= 64 && c->seagull_count <= 64 &&
                   c->lava_count <= 64 && c->thwomp_count <= 64 && c->cannonball_count <= 64 &&
                   c->boo_count <= 64 && c->rock_count <= 64 && c->teepee_count <= 64 &&
                   c->falling_star_count <= 64 && c->thundercloud_count <= 64 &&
                   c->common_prop_count <= 64 && c->mechakoopa_count <= 64 &&
                   c->kamek_count <= 64 && c->pier_boo_count <= 64
               ? c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                     c->piranha_count + c->seagull_count + c->lava_count + c->thwomp_count +
                     c->cannonball_count + c->boo_count + c->rock_count + c->teepee_count +
                     c->falling_star_count + c->thundercloud_count + c->common_prop_count +
                     c->mechakoopa_count + c->pier_boo_count + c->kamek_count
               : 0;
}
int portable_mesh_course_actors_needs_items(const PortableMeshCourseActorsConfig *c)
{
    return c && (portable_mesh_course_actors_config_count(c) || c->coins.enabled);
}
int portable_mesh_course_actors_required(const PortableMeshCourseActorsConfig *c)
{
    return c && (portable_mesh_course_actors_needs_items(c) || c->sky.enabled);
}
unsigned portable_mesh_course_actors_count(const PortableMeshCourseActors *w)
{
    return w && w->count <= 64 && w->snowman_count <= 64 && w->penguin_count <= 64 &&
                   w->mouse_count <= 64 && w->piranha_count <= 64 && w->seagull_count <= 64 &&
                   w->lava_count <= 64 && w->thwomp_count <= 64 && w->cannonball_count <= 64 &&
                   w->boo_count <= 64 && w->rock_count <= 64 && w->teepee_count <= 64 &&
                   w->falling_star_count <= 64 && w->thundercloud_count <= 64 &&
                   w->common_prop_count <= 64 && w->mechakoopa_count <= 64 &&
                   w->kamek_count <= 64 && w->pier_boo_count <= 64
               ? w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count + w->thwomp_count +
                     w->cannonball_count + w->boo_count + w->rock_count + w->teepee_count +
                     w->falling_star_count + w->thundercloud_count + w->common_prop_count +
                     w->mechakoopa_count + w->pier_boo_count + w->kamek_count
               : 0;
}
static unsigned placement(const PortableMeshCourseActorsConfig *c, unsigned i)
{
    if (i < c->count)
        return c->initial[i].placement_order;
    i -= c->count;
    if (i < c->snowman_count)
        return c->initial_snowmen[i].placement_order;
    i -= c->snowman_count;
    if (i < c->penguin_count)
        return c->initial_penguins[i].placement_order;
    i -= c->penguin_count;
    if (i < c->mouse_count)
        return c->initial_mice[i].placement_order;
    i -= c->mouse_count;
    if (i < c->piranha_count)
        return c->initial_piranhas[i].placement_order;
    i -= c->piranha_count;
    if (i < c->seagull_count)
        return c->initial_seagulls[i].placement_order;
    i -= c->seagull_count;
    if (i < c->lava_count)
        return c->initial_lava[i].placement_order;
    i -= c->lava_count;
    if (i < c->thwomp_count)
        return c->initial_thwomps[i].placement_order;
    i -= c->thwomp_count;
    if (i < c->cannonball_count)
        return c->initial_cannonballs[i].placement_order;
    i -= c->cannonball_count;
    if (i < c->boo_count)
        return c->initial_boos[i].placement_order;
    i -= c->boo_count;
    if (i < c->rock_count)
        return c->initial_rocks[i].placement_order;
    i -= c->rock_count;
    if (i < c->teepee_count)
        return c->initial_teepees[i].placement_order;
    i -= c->teepee_count;
    if (i < c->falling_star_count)
        return c->initial_falling_stars[i].placement_order;
    i -= c->falling_star_count;
    if (i < c->thundercloud_count)
        return c->initial_thunderclouds[i].placement_order;
    i -= c->thundercloud_count;
    if (i < c->common_prop_count)
        return c->initial_common_props[i].placement_order;
    i -= c->common_prop_count;
    if (i < c->mechakoopa_count)
        return c->initial_mechakoopas[i].placement_order;
    i -= c->mechakoopa_count;
    if (i < c->kamek_count)
        return c->initial_kameks[i].placement_order;
    return c->initial_pier_boos[i - c->kamek_count].placement_order;
}
static int teepee_motion_zero(const PortableTeepeeMotion *m)
{
    return !m->velocity_x_q16 && !m->velocity_y_q16 && !m->rider_x_q16 && !m->rider_y_q16 &&
           !m->rider_z_q16 && !m->rider_vx_q16 && !m->rider_vy_q16 && !m->rider_vz_q16 &&
           !m->rider_spin && !m->rider_spin_rate && !m->rider_gravity && !m->rider_heading &&
           !m->counter && !m->frame && !m->kart && !m->rider_state;
}
static int common_prop_code(uint32_t code)
{
    const uint32_t family = code >> 16, variant = code & 65535;
    return (family == 6 || family == 0x98)   ? variant <= 1
           : (family == 8 || family == 0x86) ? variant == 0
           : family == 10                    ? variant <= 7
           : family == 12                    ? variant <= 11
                                             : 0;
}
static int common_prop_motion_zero(const PortableCourseProp *a)
{
    return !a->mechakoopa.velocity_x_q16 && !a->mechakoopa.cycle && !a->mechakoopa.heading &&
           !a->mechakoopa.animation_ticks && !a->mechakoopa.animation_phase &&
           !a->mechakoopa.has_facing && !a->kamek.velocity_x_q16 && !a->kamek.velocity_z_q16 &&
           !a->kamek.phase && !a->kamek.heading && !a->kamek.has_facing && !a->kamek.mirrored &&
           !a->pier_boo.velocity_z_q16 && !a->pier_boo.phase && teepee_motion_zero(&a->teepee) &&
           !a->lake_rock.home_x_q16 && !a->lake_rock.home_y_q16 && !a->lake_rock.velocity_z_q16 &&
           !a->lake_rock.timer && !a->lake_rock.delay && !a->lake_rock.frame &&
           !a->lake_rock.mirrored;
}
static int mechakoopa_other_motion_zero(const PortableCourseProp *a)
{
    PortableCourseProp only_other = *a;
    memset(&only_other.mechakoopa, 0, sizeof only_other.mechakoopa);
    return common_prop_motion_zero(&only_other);
}
static int kamek_other_motion_zero(const PortableCourseProp *a)
{
    PortableCourseProp only_other = *a;
    memset(&only_other.kamek, 0, sizeof only_other.kamek);
    return common_prop_motion_zero(&only_other);
}
static int config_valid(const PortableMeshCourseActorsConfig *c)
{
    if (!c || c->ground_follow_version > 1 ||
        (c->ground_follow_version && !(c->count || c->penguin_count || c->mouse_count || c->mechakoopa_count)) ||
        c->count > 64 || c->snowman_count > 64 || c->penguin_count > 64 ||
        c->mouse_count > 64 || c->piranha_count > 64 || c->seagull_count > 64 ||
        c->lava_count > 64 || c->thwomp_count > 64 || c->cannonball_count > 64 ||
        c->boo_count > 64 || c->rock_count > 64 || c->teepee_count > 64 ||
        c->falling_star_count > 64 || c->thundercloud_count > 64 || c->common_prop_count > 64 ||
        c->mechakoopa_count > 64 || c->kamek_count > 64 || c->pier_boo_count > 64 ||
        c->capture_surface_count > 256 ||
        ((c->count || c->snowman_count || c->penguin_count || c->mouse_count || c->piranha_count ||
          c->seagull_count || c->lava_count || c->thwomp_count || c->cannonball_count ||
          c->boo_count || c->rock_count || c->teepee_count || c->falling_star_count ||
          c->thundercloud_count || c->common_prop_count || c->mechakoopa_count ||
          (c->kamek_count || c->pier_boo_count)) &&
         (!c->profile_id || c->profile_id == UINT32_MAX || !c->revision || c->scale_q16 < 1311 ||
          c->scale_q16 > 131072)))
        return 0;
    if (c->pier_boo_count &&
        (!portable_boo_rules_valid(&c->pier_boo_rules) || c->actor_visual_seed > INT32_MAX))
        return 0;
    if (!portable_mesh_sky_config_valid(&c->sky) ||
        (c->sky.enabled && (!c->profile_id || c->profile_id == UINT32_MAX || !c->revision)))
        return 0;
    if (!portable_mesh_course_coins_config_valid(&c->coins) ||
        (c->coins.enabled && (c->coins.course_id != c->course_id || !c->profile_id ||
                              c->profile_id == UINT32_MAX || !c->revision)))
        return 0;
    if (c->capture_surface_count &&
        (c->capture_forward_speed_q16 <= 0 || c->capture_forward_speed_q16 > 16777216 ||
         c->capture_up_speed_q16 <= 0 || c->capture_up_speed_q16 > 16777216))
        return 0;
    if (c->thwomp_count && !portable_mesh_course_crush_rules_valid(&c->crush_rules))
        return 0;
    for (unsigned i = 0; i < c->capture_surface_count; ++i)
    {
        if (c->capture_surfaces[i] > 255)
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (c->capture_surfaces[j] == c->capture_surfaces[i])
                return 0;
    }
    for (unsigned i = 0; i < 1024; ++i)
        if (c->sine[i] < -32768 || c->sine[i] > 32768)
            return 0;
    for (unsigned i = 0; i < portable_mesh_course_actors_config_count(c); ++i)
    {
        const int32_t *anchor;
        if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count;
            const PortableCourseProp *p = &c->initial_pier_boos[n];
            PortableCourseProp initial;
            if (!portable_course_prop_init(&initial, p->x_q16, p->y_q16, 0xa30000,
                                           (uint16_t)p->affine_x_q8, 0) ||
                p->source_code != 0xa30000 || p->state || p->flags != 3 ||
                p->height_q16 != 0x80000 || p->timer || p->contact_phase || p->checkpoint >= 128 ||
                p->art_index || !common_prop_motion_zero(p) || p->affine_y_q8 != p->affine_x_q8)
                return 0;
            anchor = c->pier_boo_anchors_q16[n];
        }
        else if (i >=
                 portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count;
            const PortableCourseProp *p = &c->initial_kameks[n];
            PortableCourseProp a;
            if (p->source_code != 0xa00000 || p->affine_x_q8 < 1 || p->affine_x_q8 > 256 ||
                !portable_course_prop_init(&a, p->x_q16, p->y_q16, p->source_code,
                                           (uint16_t)p->affine_x_q8, 0) ||
                p->affine_y_q8 != a.affine_y_q8 || p->height_q16 || p->state || p->timer ||
                p->flags != 3 || p->art_index || p->checkpoint >= 128 || p->contact_phase ||
                !common_prop_motion_zero(p))
                return 0;
            anchor = c->kamek_anchors_q16[n];
        }
        else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count -
                          c->kamek_count - c->mechakoopa_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count + c->mechakoopa_count;
            const PortableCourseProp *p = &c->initial_mechakoopas[n];
            PortableCourseProp a;
            if ((p->source_code != 0xa10000 && p->source_code != 0xa10001) || p->affine_x_q8 < 1 ||
                p->affine_x_q8 > 256 ||
                !portable_course_prop_init(&a, p->x_q16, p->y_q16, p->source_code,
                                           (uint16_t)p->affine_x_q8, 0) ||
                p->affine_y_q8 != a.affine_y_q8 || p->height_q16 || p->timer || p->state ||
                p->flags != 3 || p->art_index || p->checkpoint >= 128 || p->contact_phase ||
                p->mechakoopa.velocity_x_q16 != a.mechakoopa.velocity_x_q16 ||
                p->mechakoopa.cycle || p->mechakoopa.heading || p->mechakoopa.animation_ticks ||
                p->mechakoopa.animation_phase || p->mechakoopa.has_facing ||
                !mechakoopa_other_motion_zero(p))
                return 0;
            anchor = c->mechakoopa_anchors_q16[n];
        }
        else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count -
                          c->kamek_count - c->mechakoopa_count - c->common_prop_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count + c->mechakoopa_count + c->common_prop_count;
            const PortableCourseProp *p = &c->initial_common_props[n];
            PortableCourseProp a;
            if (!common_prop_code(p->source_code) || p->affine_x_q8 < 1 || p->affine_x_q8 > 256 ||
                !portable_course_prop_init(&a, p->x_q16, p->y_q16, p->source_code,
                                           (uint16_t)p->affine_x_q8, 0) ||
                p->affine_y_q8 != a.affine_y_q8 || p->height_q16 || p->timer || p->state ||
                p->flags != a.flags || p->art_index || p->checkpoint >= 128 || p->contact_phase ||
                !common_prop_motion_zero(p))
                return 0;
            anchor = c->common_prop_anchors_q16[n];
        }
        else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count -
                          c->kamek_count - c->mechakoopa_count - c->common_prop_count -
                          c->thundercloud_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count + c->mechakoopa_count + c->common_prop_count +
                         c->thundercloud_count;
            const PortableThundercloud *p = &c->initial_thunderclouds[n];
            PortableThundercloud a;
            if (!portable_thundercloud_init(&a, p->x_q16, p->y_q16, p->checkpoint, p->variant) ||
                p->height_q16 != a.height_q16 || p->state != 1 || p->timer != 32 ||
                p->palette_timer || p->flags != 1 || p->art != 4)
                return 0;
            anchor = c->thundercloud_anchors_q16[n];
        }
        else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count -
                          c->kamek_count - c->mechakoopa_count - c->common_prop_count -
                          c->thundercloud_count - c->falling_star_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count + c->mechakoopa_count + c->common_prop_count +
                         c->thundercloud_count + c->falling_star_count;
            const PortableFallingStar *p = &c->initial_falling_stars[n];
            PortableFallingStar a;
            if (!portable_falling_star_init(&a, p->x_q16, p->y_q16, p->checkpoint, p->variant) ||
                p->height_q16 || p->initial_x_q16 != a.initial_x_q16 ||
                p->offset_q16 != a.offset_q16 || p->velocity_x_q16 || p->velocity_z_q16 ||
                p->timer || p->delay != a.delay || p->affine_q8 != 224 || p->angle ||
                p->state != 1 || p->flags || p->art)
                return 0;
            anchor = c->falling_star_anchors_q16[n];
        }
        else if (i < c->count)
        {
            PortableCrab a;
            const PortableCrab *p = &c->initial[i];
            if (!portable_crab_init(&a, p->x_q16, p->y_q16, p->checkpoint, p->variant))
                return 0;
            anchor = c->anchors_q16[i];
        }
        else if (i < c->count + c->snowman_count)
        {
            unsigned n = i - c->count;
            PortableSnowman a;
            const PortableSnowman *p = &c->initial_snowmen[n];
            if (!portable_snowman_init(&a, p->x_q16, p->y_q16, p->checkpoint, 0) || p->height_q16 ||
                p->state != 17 || p->timer || p->flags != 3 || p->art)
                return 0;
            anchor = c->snowman_anchors_q16[n];
        }
        else if (i < c->count + c->snowman_count + c->penguin_count)
        {
            unsigned n = i - c->count - c->snowman_count;
            PortablePenguin a;
            const PortablePenguin *p = &c->initial_penguins[n];
            if (!portable_penguin_init(&a, p->x_q16, p->y_q16, p->checkpoint, 0) || p->height_q16 ||
                p->timer || p->affine_y_q8 != 256 || p->flags != 3 || p->art)
                return 0;
            anchor = c->penguin_anchors_q16[n];
        }
        else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count;
            const PortableMouse *p = &c->initial_mice[n];
            PortableMouse a;
            if (!portable_mouse_init(&a, p->x_q16, p->y_q16, p->checkpoint, p->variant) ||
                p->height_q16 || p->velocity_x_q16 != a.velocity_x_q16 ||
                p->velocity_y_q16 != a.velocity_y_q16 || p->velocity_z_q16 ||
                p->facing_x_q16 != a.facing_x_q16 || p->facing_y_q16 != a.facing_y_q16 ||
                p->timer || p->affine_y_q8 != 256 || p->flags != 3 || p->art || p->walk_phase ||
                p->walk_ticks || p->hop_phase || p->turn_flip)
                return 0;
            anchor = c->mouse_anchors_q16[n];
        }
        else if (i <
                 c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count - c->mouse_count;
            const PortablePiranha *p = &c->initial_piranhas[n];
            PortablePiranha a;
            if (!portable_piranha_init(&a, p->x_q16, p->y_q16, p->checkpoint, 0) || p->height_q16 ||
                p->velocity_z_q16 || p->state || p->timer || p->phase || p->affine_x_q8 != 224 ||
                p->affine_y_q8 != 224 || p->flags != 2 || p->art || p->flip_y || p->struck_player)
                return 0;
            anchor = c->piranha_anchors_q16[n];
        }
        else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                         c->piranha_count + c->seagull_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                         c->piranha_count;
            const PortableSeagull *p = &c->initial_seagulls[n];
            PortableSeagull a;
            if (!portable_seagull_init(&a, p->x_q16, p->y_q16, p->checkpoint, 0) || p->height_q16 ||
                p->velocity_z_q16 || p->state || p->timer || p->phase || p->affine_x_q8 != 224 ||
                p->affine_y_q8 != 224 || p->flags != 3 || p->art || p->flip_y)
                return 0;
            anchor = c->seagull_anchors_q16[n];
        }
        else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                         c->piranha_count + c->seagull_count + c->lava_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                         c->piranha_count - c->seagull_count;
            const PortableLavaBubble *p = &c->initial_lava[n];
            int matched = 0;
            for (unsigned variant = 0; variant < 13; ++variant)
            {
                PortableLavaBubble a;
                if (portable_lava_bubble_init(&a, p->x_q16, p->y_q16, p->checkpoint, variant) &&
                    a.timer == p->timer && a.period == p->period && p->phase == 1 && !p->flags &&
                    !p->art && !p->height_q16 && !p->velocity_x_q16 && !p->velocity_y_q16 &&
                    !p->velocity_z_q16)
                    matched = 1;
            }
            if (!matched)
                return 0;
            anchor = c->lava_anchors_q16[n];
        }
        else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                         c->piranha_count + c->seagull_count + c->lava_count + c->thwomp_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                         c->piranha_count - c->seagull_count - c->lava_count;
            const PortableThwomp *p = &c->initial_thwomps[n];
            PortableThwomp a;
            if (!portable_thwomp_init(&a, p->x_q16, p->y_q16, p->checkpoint, p->variant) ||
                p->height_q16 || p->velocity_x_q16 != a.velocity_x_q16 ||
                p->velocity_y_q16 != a.velocity_y_q16 || p->velocity_z_q16 || p->timer != a.timer ||
                p->state != a.state || p->flags != 3)
                return 0;
            anchor = c->thwomp_anchors_q16[n];
        }
        else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                         c->piranha_count + c->seagull_count + c->lava_count + c->thwomp_count +
                         c->cannonball_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                         c->piranha_count - c->seagull_count - c->lava_count - c->thwomp_count;
            const PortableCannonball *p = &c->initial_cannonballs[n];
            PortableCannonball a;
            if (!portable_cannonball_init(&a, p->x_q16, p->y_q16, p->checkpoint,
                                          c->cannonball_variants[n]) ||
                p->height_q16 || p->velocity_z_q16 || p->state != 1 || p->timer != a.timer ||
                p->affine_x_q8 != 256 || p->affine_y_q8 != 256 || p->flags || p->art ||
                p->size != 4)
                return 0;
            anchor = c->cannonball_anchors_q16[n];
        }
        else if (i < portable_mesh_course_actors_config_count(c) - c->pier_boo_count -
                         c->kamek_count - c->mechakoopa_count - c->common_prop_count -
                         c->thundercloud_count - c->falling_star_count - c->rock_count -
                         c->teepee_count)
        {
            unsigned n = i - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                         c->piranha_count - c->seagull_count - c->lava_count - c->thwomp_count -
                         c->cannonball_count;
            const PortableBoo *p = &c->initial_boos[n];
            PortableBoo a;
            if (!portable_boo_init(&a, p->x_q16, p->y_q16, p->checkpoint, 0) || p->height_q16 ||
                p->base_height_q16 != 8 * 65536 || p->velocity_z_q16 || p->phase ||
                p->state != 16 || p->heading != 0x4000 || p->affine_y_q8 != 256 || p->flags != 3)
                return 0;
            anchor = c->boo_anchors_q16[n];
        }
        else if (i < portable_mesh_course_actors_config_count(c) - c->pier_boo_count -
                         c->kamek_count - c->mechakoopa_count - c->common_prop_count -
                         c->thundercloud_count - c->falling_star_count - c->teepee_count)
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count + c->mechakoopa_count + c->common_prop_count +
                         c->thundercloud_count + c->falling_star_count + c->rock_count +
                         c->teepee_count;
            const PortableCourseProp *p = &c->initial_rocks[n];
            PortableCourseProp a;
            if (!portable_course_prop_init(&a, p->x_q16, p->y_q16, p->source_code, 256, 0) ||
                p->source_code < 0xa20000 || p->source_code > 0xa20003 || p->height_q16 ||
                p->timer || p->state || p->flags || p->art_index || p->checkpoint >= 128 ||
                p->affine_x_q8 < 1 || p->affine_x_q8 > 256 || p->affine_x_q8 != p->affine_y_q8 ||
                p->lake_rock.home_x_q16 != p->x_q16 || p->lake_rock.home_y_q16 != p->y_q16 ||
                p->lake_rock.velocity_z_q16 || p->lake_rock.timer ||
                p->lake_rock.delay != a.lake_rock.delay || p->lake_rock.frame ||
                p->lake_rock.mirrored)
                return 0;
            anchor = c->rock_anchors_q16[n];
        }
        else
        {
            unsigned n = i - portable_mesh_course_actors_config_count(c) + c->pier_boo_count +
                         c->kamek_count + c->mechakoopa_count + c->common_prop_count +
                         c->thundercloud_count + c->falling_star_count + c->teepee_count;
            const PortableCourseProp *p = &c->initial_teepees[n];
            PortableCourseProp a;
            if (p->source_code != 0x030000 ||
                !portable_course_prop_init(&a, p->x_q16, p->y_q16, p->source_code, 256, 0) ||
                p->height_q16 || p->timer || p->state || p->flags != 3 || p->art_index ||
                p->checkpoint >= 128 || p->affine_x_q8 < 1 || p->affine_x_q8 > 256 ||
                p->affine_y_q8 != p->affine_x_q8 || !teepee_motion_zero(&p->teepee))
                return 0;
            anchor = c->teepee_anchors_q16[n];
        }
        for (unsigned j = 0; j < 3; ++j)
            if (anchor[j] < -268435456 || anchor[j] > 268435456)
                return 0;
        for (unsigned j = 0; j < i; ++j)
            if (placement(c, i) == placement(c, j))
                return 0;
    }
    return 1;
}
int portable_mesh_course_actors_binding(const PortableMeshCourseActorsConfig *c, unsigned i,
                                        PortableMeshCourseActorBinding *out)
{
    if (!out || !config_valid(c) || i >= portable_mesh_course_actors_config_count(c))
        return 0;
    PortableMeshCourseActorBinding b;
    if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count;
        b = (PortableMeshCourseActorBinding){c->pier_boo_object_indices[i], 3,
                                             c->pier_boo_frame_ids[i]};
    }
    else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count;
        b = (PortableMeshCourseActorBinding){c->kamek_object_indices[i], 2, c->kamek_frame_ids[i]};
    }
    else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
                      c->mechakoopa_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
             c->mechakoopa_count;
        b = (PortableMeshCourseActorBinding){c->mechakoopa_object_indices[i], 4,
                                             c->mechakoopa_frame_ids[i]};
    }
    else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
                      c->mechakoopa_count - c->common_prop_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
             c->mechakoopa_count - c->common_prop_count;
        b = (PortableMeshCourseActorBinding){
            c->common_prop_object_indices[i],
            c->initial_common_props[i].source_code == 0x860000 ? 2u : 1u,
            c->common_prop_frame_ids[i]};
    }
    else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
                      c->mechakoopa_count - c->common_prop_count - c->thundercloud_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
             c->mechakoopa_count - c->common_prop_count - c->thundercloud_count;
        b = (PortableMeshCourseActorBinding){c->thundercloud_object_indices[i], 72,
                                             c->thundercloud_frame_ids[i]};
    }
    else if (i >= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
                      c->mechakoopa_count - c->common_prop_count - c->thundercloud_count -
                      c->falling_star_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
             c->mechakoopa_count - c->common_prop_count - c->thundercloud_count -
             c->falling_star_count;
        b = (PortableMeshCourseActorBinding){c->falling_star_object_indices[i], 5,
                                             c->falling_star_frame_ids[i]};
    }
    else if (i < c->count)
        b = (PortableMeshCourseActorBinding){c->object_indices[i], 4, c->frame_ids[i]};
    else if (i < c->count + c->snowman_count)
    {
        i -= c->count;
        b = (PortableMeshCourseActorBinding){c->snowman_object_indices[i], 2,
                                             c->snowman_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count)
    {
        i -= c->count + c->snowman_count;
        b = (PortableMeshCourseActorBinding){c->penguin_object_indices[i], 6,
                                             c->penguin_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count;
        b = (PortableMeshCourseActorBinding){c->mouse_object_indices[i], 2, c->mouse_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count + c->mouse_count;
        b = (PortableMeshCourseActorBinding){c->piranha_object_indices[i], 5,
                                             c->piranha_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                     c->piranha_count + c->seagull_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count;
        b = (PortableMeshCourseActorBinding){c->seagull_object_indices[i], 3,
                                             c->seagull_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                     c->piranha_count + c->seagull_count + c->lava_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count +
             c->seagull_count;
        b = (PortableMeshCourseActorBinding){c->lava_object_indices[i], 2, c->lava_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                     c->piranha_count + c->seagull_count + c->lava_count + c->thwomp_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count +
             c->seagull_count + c->lava_count;
        b = (PortableMeshCourseActorBinding){c->thwomp_object_indices[i], 1,
                                             c->thwomp_frame_ids[i]};
    }
    else if (i < c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                     c->piranha_count + c->seagull_count + c->lava_count + c->thwomp_count +
                     c->cannonball_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count +
             c->seagull_count + c->lava_count + c->thwomp_count;
        b = (PortableMeshCourseActorBinding){c->cannonball_object_indices[i], 7,
                                             c->cannonball_frame_ids[i]};
    }
    else if (i < portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
                     c->mechakoopa_count - c->common_prop_count - c->thundercloud_count -
                     c->falling_star_count - c->rock_count - c->teepee_count)
    {
        i -= c->count + c->snowman_count + c->penguin_count + c->mouse_count + c->piranha_count +
             c->seagull_count + c->lava_count + c->thwomp_count + c->cannonball_count;
        b = (PortableMeshCourseActorBinding){c->boo_object_indices[i], 2, c->boo_frame_ids[i]};
    }
    else if (i < portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
                     c->mechakoopa_count - c->common_prop_count - c->thundercloud_count -
                     c->falling_star_count - c->teepee_count)
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
             c->mechakoopa_count - c->common_prop_count - c->thundercloud_count -
             c->falling_star_count - c->rock_count - c->teepee_count;
        b = (PortableMeshCourseActorBinding){c->rock_object_indices[i], 5, c->rock_frame_ids[i]};
    }
    else
    {
        i -= portable_mesh_course_actors_config_count(c) - c->pier_boo_count - c->kamek_count -
             c->mechakoopa_count - c->common_prop_count - c->thundercloud_count -
             c->falling_star_count - c->teepee_count;
        b = (PortableMeshCourseActorBinding){c->teepee_object_indices[i], 11,
                                             c->teepee_frame_ids[i]};
    }
    *out = b;
    return 1;
}
int portable_mesh_course_actors_config_open(const PortableCourse3DView *course,
                                            PortableMeshCourseActorsConfig *out, char *error,
                                            size_t cap)
{
    if (!course || !out)
        return fail(error, cap, 0, "invalid configuration arguments");
    PortableMeshCourseActorsConfig next = {.course_id = course->id};
    PortableCourse3DMeta meta;
    if (!portable_course_3d_meta(course, &meta))
        return fail(error, cap, course->id, "invalid META");
    const PortableCourse3DTable *properties =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'));
    if (properties)
    {
        if (properties->count > 16384)
            return fail(error, cap, course->id, "property budget exceeded");
        for (unsigned i = 0; i < properties->count; ++i)
        {
            PortableCourse3DProperty checked;
            if (!portable_course_3d_property_at(course, i, &checked))
                return fail(error, cap, course->id, "invalid property/reference");
        }
    }
    for (uint32_t i = 0; i < meta.property_count; ++i)
    {
        PortableCourse3DProperty ground;
        if (!portable_course_3d_property_at(course, meta.property_first + i, &ground))
            return fail(error, cap, course->id, "invalid ground-follow META span");
        if (strcmp(ground.key, "mksc:actor-ground-follow"))
            continue;
        if (next.ground_follow_version || ground.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
            ground.value.integer != 1)
            return fail(error, cap, course->id, "invalid/duplicate mksc:actor-ground-follow");
        next.ground_follow_version = 1;
    }
    if (!portable_mesh_course_coins_config_open(course, &next.coins, error, cap))
        return 0;
    if (!portable_mesh_sky_config_open(course, &next.sky, &next.sky_bindings, error, cap))
        return 0;
    PortableCourse3DProperty p;
    int rules = portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                                 "mksc:actor-rules", &p);
    if (rules)
    {
        PortableAssetSectionView section;
        if (p.type != PORTABLE_COURSE_3D_PROPERTY_ASSET || p.value.asset.kind != 104 ||
            portable_asset_pack_find_section(course->pack, 104, p.value.asset.id, &section) !=
                PORTABLE_ASSET_PACK_OK ||
            section.version != 1 || section.size != 4128 || !section.bytes ||
            memcmp(section.bytes, "MSA1", 4) || word(section.bytes + 4) != 1 ||
            word(section.bytes + 8) != 4128 || !word(section.bytes + 12) ||
            word(section.bytes + 12) != section.content_id || !word(section.bytes + 16) ||
            word(section.bytes + 20) != 1024 || word(section.bytes + 24) ||
            word(section.bytes + 28))
            return fail(error, cap, course->id, "invalid MSA1 rules");
        next.profile_id = word(section.bytes + 12);
        next.revision = word(section.bytes + 16);
        for (unsigned i = 0; i < 1024; ++i)
            next.sine[i] = signed_word(section.bytes + 32 + 4 * i);
    }
    const PortableCourse3DTable *objects =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
    if (objects &&
        (objects->stride != 64 || objects->count > 4096 || (objects->count && !objects->bytes)))
        return fail(error, cap, course->id, "invalid OBJS");
    for (unsigned i = 0; objects && i < objects->count; ++i)
    {
        const uint8_t *row = objects->bytes + 64 * i;
        uint32_t first = word(row + 56), count = word(row + 60);
        int declared =
            portable_course_3d_property_find(course, first, count, "mksc:controller", &p);
        if (!declared)
        {
            PortableCourse3DProperty v;
            if (portable_course_3d_property_find(course, first, count, "mksc:controller-version",
                                                 &v))
                return fail(error, cap, course->id, "controller version without family");
            continue;
        }
        if (p.type == PORTABLE_COURSE_3D_PROPERTY_STRING && p.value.string &&
            !strcmp(p.value.string, "coins"))
            continue;
        if (p.type != PORTABLE_COURSE_3D_PROPERTY_STRING || !p.value.string ||
            (strcmp(p.value.string, "crabs") && strcmp(p.value.string, "snowmen") &&
             strcmp(p.value.string, "penguins") && strcmp(p.value.string, "mice") &&
             strcmp(p.value.string, "piranhas") && strcmp(p.value.string, "seagulls") &&
             strcmp(p.value.string, "lava_bubbles") && strcmp(p.value.string, "thwomps") &&
             strcmp(p.value.string, "cannonballs") && strcmp(p.value.string, "boos") &&
             strcmp(p.value.string, "rocks") && strcmp(p.value.string, "teepees") &&
             strcmp(p.value.string, "falling_stars") && strcmp(p.value.string, "thunderclouds") &&
             strcmp(p.value.string, "common_props") && strcmp(p.value.string, "mechakoopas") &&
             strcmp(p.value.string, "kamek") && strcmp(p.value.string, "pier_boos")))
        {
            if (error && cap)
                snprintf(
                    error, cap, "3D course=%u object=%u controller=%s unsupported", course->id, i,
                    p.type == PORTABLE_COURSE_3D_PROPERTY_STRING && p.value.string ? p.value.string
                                                                                   : "<invalid>");
            return 0;
        }
        const int pier_boo = !strcmp(p.value.string, "pier_boos");
        const int kamek = !strcmp(p.value.string, "kamek");
        const int mechakoopa = !strcmp(p.value.string, "mechakoopas");
        const int common_prop = !strcmp(p.value.string, "common_props");
        const int thundercloud = !strcmp(p.value.string, "thunderclouds");
        const int falling_star = !strcmp(p.value.string, "falling_stars");
        const int snowman = !strcmp(p.value.string, "snowmen");
        const int penguin = !strcmp(p.value.string, "penguins");
        const int mouse = !strcmp(p.value.string, "mice");
        const int piranha = !strcmp(p.value.string, "piranhas");
        const int seagull = !strcmp(p.value.string, "seagulls");
        const int lava = !strcmp(p.value.string, "lava_bubbles");
        const int thwomp = !strcmp(p.value.string, "thwomps");
        const int cannonball = !strcmp(p.value.string, "cannonballs");
        const int boo = !strcmp(p.value.string, "boos");
        const int rock = !strcmp(p.value.string, "rocks");
        const int teepee = !strcmp(p.value.string, "teepees");
        int32_t source_code = 0;
        if (common_prop && (!integer(course, first, count, "source:code", &source_code) ||
                            source_code < 0 || !common_prop_code((uint32_t)source_code)))
            return fail(error, cap, course->id, "invalid common prop source:code");
        const char *family = pier_boo       ? "pier_boos"
                             : kamek        ? "kamek"
                             : mechakoopa   ? "mechakoopas"
                             : common_prop  ? "common_props"
                             : thundercloud ? "thunderclouds"
                             : falling_star ? "falling_stars"
                             : snowman      ? "snowmen"
                             : penguin      ? "penguins"
                             : mouse        ? "mice"
                             : piranha      ? "piranhas"
                             : teepee       ? "teepees"
                             : rock         ? "rocks"
                             : boo          ? "boos"
                             : cannonball   ? "cannonballs"
                             : thwomp       ? "thwomps"
                             : lava         ? "lava_bubbles"
                             : seagull      ? "seagulls"
                                            : "crabs";
        const unsigned frame_count = pier_boo       ? 3u
                                     : kamek        ? 2u
                                     : mechakoopa   ? 4u
                                     : common_prop  ? (source_code == 0x860000 ? 2u : 1u)
                                     : thundercloud ? 72u
                                     : falling_star ? 5u
                                     : teepee       ? 11u
                                     : rock         ? 5u
                                     : boo          ? 2u
                                     : cannonball   ? 7u
                                     : thwomp       ? 1u
                                     : snowman || mouse || lava ? 2u
                                     : penguin                  ? 6u
                                     : piranha                  ? 5u
                                     : seagull                  ? 3u
                                                                : 4u;
        int32_t version = 0, x, y, variant = 0, checkpoint, order, frames;
        if (!integer(course, first, count, "mksc:controller-version", &version) || version != 1)
        {
            if (error && cap)
                snprintf(error, cap, "3D course=%u object=%u controller=%s version=%d unsupported",
                         course->id, i, family, version);
            return 0;
        }
        if (!integer(course, first, count, "mksc:controller-version", &version) || version != 1 ||
            !rules ||
            (pier_boo       ? next.pier_boo_count
             : thundercloud ? next.thundercloud_count
             : falling_star ? next.falling_star_count
             : snowman      ? next.snowman_count
             : penguin      ? next.penguin_count
             : mouse        ? next.mouse_count
             : piranha      ? next.piranha_count
             : teepee       ? next.teepee_count
             : rock         ? next.rock_count
             : boo          ? next.boo_count
             : cannonball   ? next.cannonball_count
             : thwomp       ? next.thwomp_count
             : lava         ? next.lava_count
             : seagull      ? next.seagull_count
                            : next.count) == 64 ||
            !integer(course, first, count, "source:x-q16", &x) ||
            !integer(course, first, count, "source:y-q16", &y) ||
            (!common_prop && !integer(course, first, count, "source:variant", &variant)) ||
            !integer(course, first, count, "source:checkpoint", &checkpoint) ||
            !integer(course, first, count, "source:placement-order", &order) ||
            !integer(course, first, count, "source:frame-count", &frames) ||
            frames != (int32_t)frame_count || variant < 0 || variant > 65535 || checkpoint < 0 ||
            checkpoint >= 128 || order < 0 || order > 255)
        {
            if (error && cap)
                snprintf(
                    error, cap,
                    "3D course=%u object=%u controller=%s invalid declaration/version or budget",
                    course->id, i, family);
            return 0;
        }
        unsigned n = pier_boo       ? next.pier_boo_count
                     : kamek        ? next.kamek_count
                     : mechakoopa   ? next.mechakoopa_count
                     : common_prop  ? next.common_prop_count
                     : thundercloud ? next.thundercloud_count
                     : falling_star ? next.falling_star_count
                     : snowman      ? next.snowman_count
                     : penguin      ? next.penguin_count
                     : mouse        ? next.mouse_count
                     : piranha      ? next.piranha_count
                     : teepee       ? next.teepee_count
                     : rock         ? next.rock_count
                     : boo          ? next.boo_count
                     : cannonball   ? next.cannonball_count
                     : thwomp       ? next.thwomp_count
                     : lava         ? next.lava_count
                     : seagull      ? next.seagull_count
                                    : next.count;
        int32_t *anchor;
        uint32_t *frame_ids;
        if (pier_boo)
        {
            int32_t inverse = 256;
            PortableCourse3DProperty affine;
            if (portable_course_3d_property_find(course, first, count, "source:affine-q8", &affine))
            {
                if (affine.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                    affine.value.integer < 1 || affine.value.integer > 256)
                    return fail(error, cap, course->id, "invalid Pier Boo affine");
                inverse = affine.value.integer;
            }
            if (n >= 64 || variant || word(row) != 0x1008 ||
                !portable_course_prop_init(&next.initial_pier_boos[n], x, y, 0xa30000,
                                           (uint16_t)inverse, 0))
                return fail(error, cap, course->id, "invalid Pier Boo placement/class");
            next.initial_pier_boos[n].checkpoint = (uint8_t)checkpoint;
            next.initial_pier_boos[n].placement_order = (uint8_t)order;
            next.pier_boo_object_indices[n] = i;
            anchor = next.pier_boo_anchors_q16[n];
            frame_ids = next.pier_boo_frame_ids[n];
        }
        else if (kamek)
        {
            int32_t inverse = 224;
            PortableCourse3DProperty affine;
            if (portable_course_3d_property_find(course, first, count, "source:affine-q8", &affine))
            {
                if (affine.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                    affine.value.integer < 1 || affine.value.integer > 256)
                    return fail(error, cap, course->id, "invalid Kamek affine");
                inverse = affine.value.integer;
            }
            if (word(row) != 0x101b || variant ||
                !portable_course_prop_init(&next.initial_kameks[n], x, y, 0xa00000,
                                           (uint16_t)inverse, 0))
                return fail(error, cap, course->id, "invalid Kamek placement/class");
            next.initial_kameks[n].checkpoint = (uint8_t)checkpoint;
            next.initial_kameks[n].placement_order = (uint8_t)order;
            next.kamek_object_indices[n] = i;
            anchor = next.kamek_anchors_q16[n];
            frame_ids = next.kamek_frame_ids[n];
        }
        else if (mechakoopa)
        {
            int32_t inverse = 224;
            PortableCourse3DProperty affine;
            if (portable_course_3d_property_find(course, first, count, "source:affine-q8", &affine))
            {
                if (affine.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                    affine.value.integer < 1 || affine.value.integer > 256)
                    return fail(error, cap, course->id, "invalid Mechakoopa affine");
                inverse = affine.value.integer;
            }
            if (word(row) != 0x101c || variant > 1 ||
                !portable_course_prop_init(&next.initial_mechakoopas[n], x, y,
                                           UINT32_C(0xa10000) | (uint32_t)variant,
                                           (uint16_t)inverse, 0))
                return fail(error, cap, course->id, "invalid Mechakoopa placement/class");
            next.initial_mechakoopas[n].checkpoint = (uint8_t)checkpoint;
            next.initial_mechakoopas[n].placement_order = (uint8_t)order;
            next.mechakoopa_object_indices[n] = i;
            anchor = next.mechakoopa_anchors_q16[n];
            frame_ids = next.mechakoopa_frame_ids[n];
        }
        else if (common_prop)
        {
            PortableCourse3DProperty affine, declared_variant;
            int32_t inverse = 256;
            if (portable_course_3d_property_find(course, first, count, "source:affine-q8", &affine))
            {
                if (affine.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                    affine.value.integer < 1 || affine.value.integer > 256)
                    return fail(error, cap, course->id, "invalid common prop affine");
                inverse = affine.value.integer;
            }
            if (portable_course_3d_property_find(course, first, count, "source:variant",
                                                 &declared_variant) &&
                (declared_variant.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                 declared_variant.value.integer != (source_code & 65535)))
                return fail(error, cap, course->id, "inconsistent common prop variant");
            if (word(row) != 0x1019 ||
                !portable_course_prop_init(&next.initial_common_props[n], x, y,
                                           (uint32_t)source_code, (uint16_t)inverse, 0))
                return fail(error, cap, course->id, "invalid common prop placement/class");
            next.initial_common_props[n].checkpoint = (uint8_t)checkpoint;
            next.initial_common_props[n].placement_order = (uint8_t)order;
            next.common_prop_object_indices[n] = i;
            anchor = next.common_prop_anchors_q16[n];
            frame_ids = next.common_prop_frame_ids[n];
        }
        else if (thundercloud)
        {
            if (word(row) != 0x1010 ||
                !portable_thundercloud_init(&next.initial_thunderclouds[n], x, y,
                                            (uint8_t)checkpoint, (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source thundercloud placement/class");
            next.initial_thunderclouds[n].placement_order = (uint8_t)order;
            next.thundercloud_object_indices[n] = i;
            anchor = next.thundercloud_anchors_q16[n];
            frame_ids = next.thundercloud_frame_ids[n];
        }
        else if (falling_star)
        {
            if (word(row) != 0x1011 ||
                !portable_falling_star_init(&next.initial_falling_stars[n], x, y,
                                            (uint8_t)checkpoint, (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source falling star placement/class");
            next.initial_falling_stars[n].placement_order = (uint8_t)order;
            next.falling_star_object_indices[n] = i;
            anchor = next.falling_star_anchors_q16[n];
            frame_ids = next.falling_star_frame_ids[n];
        }
        else if (snowman)
        {
            if (!portable_snowman_init(&next.initial_snowmen[n], x, y, (uint8_t)checkpoint,
                                       (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source snowman placement");
            next.initial_snowmen[n].placement_order = (uint8_t)order;
            next.snowman_object_indices[n] = i;
            anchor = next.snowman_anchors_q16[n];
            frame_ids = next.snowman_frame_ids[n];
        }
        else if (penguin)
        {
            if (!portable_penguin_init(&next.initial_penguins[n], x, y, (uint8_t)checkpoint,
                                       (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source penguin placement");
            next.initial_penguins[n].placement_order = (uint8_t)order;
            next.penguin_object_indices[n] = i;
            anchor = next.penguin_anchors_q16[n];
            frame_ids = next.penguin_frame_ids[n];
        }
        else if (mouse)
        {
            if (!portable_mouse_init(&next.initial_mice[n], x, y, (uint8_t)checkpoint,
                                     (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source mouse placement");
            next.initial_mice[n].placement_order = (uint8_t)order;
            next.mouse_object_indices[n] = i;
            anchor = next.mouse_anchors_q16[n];
            frame_ids = next.mouse_frame_ids[n];
        }
        else if (piranha)
        {
            if (!portable_piranha_init(&next.initial_piranhas[n], x, y, (uint8_t)checkpoint,
                                       (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source piranha placement");
            next.initial_piranhas[n].placement_order = (uint8_t)order;
            next.piranha_object_indices[n] = i;
            anchor = next.piranha_anchors_q16[n];
            frame_ids = next.piranha_frame_ids[n];
        }
        else if (teepee)
        {
            if (variant || word(row) != 0x1018 ||
                !portable_course_prop_init(&next.initial_teepees[n], x, y, 0x030000, 256, 0))
                return fail(error, cap, course->id, "invalid source teepee placement/class");
            PortableCourse3DProperty affine;
            if (portable_course_3d_property_find(course, first, count, "source:affine-q8", &affine))
            {
                if (affine.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                    affine.value.integer < 1 || affine.value.integer > 256)
                    return fail(error, cap, course->id, "invalid source teepee affine");
                next.initial_teepees[n].affine_x_q8 = next.initial_teepees[n].affine_y_q8 =
                    (int16_t)affine.value.integer;
            }
            next.initial_teepees[n].checkpoint = (uint8_t)checkpoint;
            next.initial_teepees[n].placement_order = (uint8_t)order;
            next.teepee_object_indices[n] = i;
            anchor = next.teepee_anchors_q16[n];
            frame_ids = next.teepee_frame_ids[n];
        }
        else if (rock)
        {
            if (variant > 3 || !portable_course_prop_init(&next.initial_rocks[n], x, y,
                                                          0xa20000u | (uint32_t)variant, 256, 0))
                return fail(error, cap, course->id, "invalid source rock placement");
            PortableCourse3DProperty affine;
            if (portable_course_3d_property_find(course, first, count, "source:affine-q8", &affine))
            {
                if (affine.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                    affine.value.integer < 1 || affine.value.integer > 256)
                    return fail(error, cap, course->id, "invalid source rock affine-q8");
                next.initial_rocks[n].affine_x_q8 = next.initial_rocks[n].affine_y_q8 =
                    (int16_t)affine.value.integer;
            }
            next.initial_rocks[n].checkpoint = (uint8_t)checkpoint;
            next.initial_rocks[n].placement_order = (uint8_t)order;
            next.rock_object_indices[n] = i;
            anchor = next.rock_anchors_q16[n];
            frame_ids = next.rock_frame_ids[n];
        }
        else if (boo)
        {
            if (!portable_boo_init(&next.initial_boos[n], x, y, (uint8_t)checkpoint,
                                   (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source boo placement");
            next.initial_boos[n].placement_order = (uint8_t)order;
            next.boo_object_indices[n] = i;
            anchor = next.boo_anchors_q16[n];
            frame_ids = next.boo_frame_ids[n];
        }
        else if (cannonball)
        {
            if (!portable_cannonball_init(&next.initial_cannonballs[n], x, y, (uint8_t)checkpoint,
                                          (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source cannonball placement");
            next.cannonball_variants[n] = (uint8_t)variant;
            next.initial_cannonballs[n].placement_order = (uint8_t)order;
            next.cannonball_object_indices[n] = i;
            anchor = next.cannonball_anchors_q16[n];
            frame_ids = next.cannonball_frame_ids[n];
        }
        else if (thwomp)
        {
            if (!portable_thwomp_init(&next.initial_thwomps[n], x, y, (uint8_t)checkpoint,
                                      (unsigned)variant))
                return fail(error, cap, course->id, "invalid source thwomp placement");
            next.initial_thwomps[n].placement_order = (uint8_t)order;
            next.thwomp_object_indices[n] = i;
            anchor = next.thwomp_anchors_q16[n];
            frame_ids = next.thwomp_frame_ids[n];
        }
        else if (lava)
        {
            if (!portable_lava_bubble_init(&next.initial_lava[n], x, y, (uint8_t)checkpoint,
                                           (unsigned)variant))
                return fail(error, cap, course->id, "invalid source lava placement");
            next.initial_lava[n].placement_order = (uint8_t)order;
            next.lava_object_indices[n] = i;
            anchor = next.lava_anchors_q16[n];
            frame_ids = next.lava_frame_ids[n];
        }
        else if (seagull)
        {
            if (!portable_seagull_init(&next.initial_seagulls[n], x, y, (uint8_t)checkpoint,
                                       (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source seagull placement");
            next.initial_seagulls[n].placement_order = (uint8_t)order;
            next.seagull_object_indices[n] = i;
            anchor = next.seagull_anchors_q16[n];
            frame_ids = next.seagull_frame_ids[n];
        }
        else
        {
            if (!portable_crab_init(&next.initial[n], x, y, (uint8_t)checkpoint, (uint16_t)variant))
                return fail(error, cap, course->id, "invalid source crab placement");
            next.initial[n].placement_order = (uint8_t)order;
            next.object_indices[n] = i;
            anchor = next.anchors_q16[n];
            frame_ids = next.frame_ids[n];
        }
        for (unsigned axis = 0; axis < 3; ++axis)
            anchor[axis] = signed_word(row + 12 + axis * 4);
        for (unsigned f = 0; f < frame_count; ++f)
        {
            char key[32];
            PortableAssetSectionView mesh;
            snprintf(key, sizeof key, "source:frame:%u", f);
            if (!portable_course_3d_property_find(course, first, count, key, &p) ||
                p.type != PORTABLE_COURSE_3D_PROPERTY_ASSET || p.value.asset.kind != 96 ||
                portable_asset_pack_find_section(course->pack, 96, p.value.asset.id, &mesh) !=
                    PORTABLE_ASSET_PACK_OK)
                return fail(error, cap, course->id, "missing actor frame model");
            frame_ids[f] = p.value.asset.id;
        }
        if (pier_boo)
            ++next.pier_boo_count;
        else if (kamek)
            ++next.kamek_count;
        else if (mechakoopa)
            ++next.mechakoopa_count;
        else if (common_prop)
            ++next.common_prop_count;
        else if (thundercloud)
            ++next.thundercloud_count;
        else if (falling_star)
            ++next.falling_star_count;
        else if (snowman)
            ++next.snowman_count;
        else if (penguin)
            ++next.penguin_count;
        else if (mouse)
            ++next.mouse_count;
        else if (piranha)
            ++next.piranha_count;
        else if (teepee)
            ++next.teepee_count;
        else if (rock)
            ++next.rock_count;
        else if (boo)
            ++next.boo_count;
        else if (cannonball)
            ++next.cannonball_count;
        else if (thwomp)
            ++next.thwomp_count;
        else if (lava)
            ++next.lava_count;
        else if (seagull)
            ++next.seagull_count;
        else
            ++next.count;
    }
    if (next.pier_boo_count)
    {
        PortableAssetSectionView section;
        if (portable_asset_pack_find_section(course->pack, PORTABLE_ASSET_SECTION_BOO_RULES, 1,
                                             &section) != PORTABLE_ASSET_PACK_OK ||
            section.version != 1 || section.size != 36 || !section.bytes ||
            word(section.bytes) != 0x314f4f42 || word(section.bytes + 4) != 7)
            return fail(error, cap, course->id, "missing/invalid Pier BOO1 rules");
        uint16_t *fields[5] = {&next.pier_boo_rules.invisibility_ticks,
                               &next.pier_boo_rules.lock_ticks, &next.pier_boo_rules.orbit_ticks,
                               &next.pier_boo_rules.orbit_step, &next.pier_boo_rules.bob_step};
        for (unsigned i = 0; i < 5; ++i)
        {
            uint32_t value = word(section.bytes + 8 + 4 * i);
            if (value > 32767)
                return fail(error, cap, course->id, "invalid Pier BOO1 field");
            *fields[i] = (uint16_t)value;
        }
        next.pier_boo_rules.repel_velocity_q16 = word(section.bytes + 28);
        next.pier_boo_rules.repel_gravity_q16 = word(section.bytes + 32);
        if (!portable_boo_rules_valid(&next.pier_boo_rules))
            return fail(error, cap, course->id, "invalid Pier BOO1 values");
        if (portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                             "mksc:actor-visual-seed", &p))
        {
            if (p.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || p.value.integer < 0)
                return fail(error, cap, course->id, "invalid actor visual seed");
            next.actor_visual_seed = (uint32_t)p.value.integer;
        }
    }
    if (next.thwomp_count)
    {
        char detail[160];
        if (!portable_mesh_course_crush_rules_open(course, &next.crush_rules, detail,
                                                   sizeof detail))
            return fail(error, cap, course->id, detail);
    }
    if (rules && !portable_mesh_course_actors_needs_items(&next))
        return fail(error, cap, course->id, "actor rules declared without controllers");
    if (portable_mesh_course_actors_config_count(&next))
    {
        if (!portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                              "source:metres-per-unit", &p) ||
            p.type != PORTABLE_COURSE_3D_PROPERTY_FIXED)
            return fail(error, cap, course->id, "missing source scale");
        next.scale_q16 = p.value.fixed_q16;
    }
    if (!rules && next.coins.enabled)
    {
        next.profile_id = next.coins.profile_id;
        next.revision = next.coins.revision;
    }
    if (!rules && !next.coins.enabled && next.sky.enabled)
    {
        next.profile_id = next.sky.profile_id;
        next.revision = next.sky.revision;
    }
    const PortableCourse3DTable *surfaces =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'));
    if (surfaces && surfaces->count > 256)
        return fail(error, cap, course->id, "capture surface budget exceeded");
    for (unsigned i = 0; surfaces && i < surfaces->count; ++i)
    {
        PortableCourse3DSurface surface;
        if (!portable_course_3d_surface(course, i, &surface))
            return fail(error, cap, course->id, "invalid capture surface");
        if (portable_course_3d_property_find(course, surface.property_first, surface.property_count,
                                             "mksc:capture", &p))
        {
            if (p.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                return fail(error, cap, course->id, "mksc:capture requires boolean");
            if (p.value.boolean)
                next.capture_surfaces[next.capture_surface_count++] = i;
        }
    }
    const char *speed_keys[2] = {"mksc:capture-forward-speed", "mksc:capture-up-speed"};
    int32_t *speeds[2] = {&next.capture_forward_speed_q16, &next.capture_up_speed_q16};
    for (unsigned i = 0; i < 2; ++i)
    {
        int present = portable_course_3d_property_find(course, meta.property_first,
                                                       meta.property_count, speed_keys[i], &p);
        if (present)
        {
            if (p.type != PORTABLE_COURSE_3D_PROPERTY_FIXED || p.value.fixed_q16 <= 0 ||
                p.value.fixed_q16 > 16777216)
                return fail(error, cap, course->id, "invalid capture launch speed");
            *speeds[i] = p.value.fixed_q16;
        }
        else if (next.capture_surface_count)
            return fail(error, cap, course->id, "missing capture launch speed");
    }
    if (!config_valid(&next))
        return fail(error, cap, course->id, "invalid source rules/scale/order");
    *out = next;
    return 1;
}
/* Source controller invariants, not SI trajectory assumptions. Protected
 * hopping starts from one of the exact heights of the ordinary 17-step hop. */
static int hop_height(int64_t h)
{
    for (int n = 0; n <= 17; ++n)
        if (h == (int64_t)131072 * n - (int64_t)8192 * n * (n - 1))
            return 1;
    return 0;
}
static int mouse_valid(const PortableMouse *a, const PortableMouse *initial, const int32_t *sine)
{
    if (a->variant != initial->variant || a->checkpoint != initial->checkpoint ||
        a->placement_order != initial->placement_order || a->art > 1 || a->walk_phase > 1 ||
        a->walk_ticks > 9 || a->hop_phase > 1 || a->turn_flip > 1 ||
        (a->flags != 3 && a->flags != 1 && a->flags != 0))
        return 0;
    if (a->affine_y_q8 != ((a->flags & 2) ? 256 : -256))
        return 0;
    if (a->variant != 0x8001)
    {
        if (a->variant != 0 && a->variant != 1)
            return 0;
        const int vertical = a->variant == 1;
        const int32_t v = vertical ? a->velocity_y_q16 : a->velocity_x_q16;
        const int32_t facing = vertical ? a->facing_y_q16 : a->facing_x_q16;
        if ((v != -32768 && v != 32768) || (facing != -32768 && facing != 32768) ||
            (vertical ? a->velocity_x_q16 : a->velocity_y_q16) ||
            (vertical ? a->facing_x_q16 : a->facing_y_q16) || a->hop_phase)
            return 0;
        if (a->flags == 3)
            return a->timer >= 0 && a->timer <= 620 && !a->height_q16 && !a->velocity_z_q16;
        if (a->velocity_z_q16 != 196608)
            return 0;
        if (a->flags == 0)
            return a->timer == -1 && a->height_q16 == sine[0] * 48;
        if (a->timer < 0 || a->timer > 32)
            return 0;
        return a->height_q16 == (a->timer == 32 ? 0 : sine[(unsigned)(a->timer + 1) * 16] * 48);
    }
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 || a->velocity_x_q16 ||
        a->velocity_y_q16 || a->facing_x_q16 || a->facing_y_q16 || a->walk_phase || a->walk_ticks ||
        a->turn_flip)
        return 0;
    if (a->flags == 3)
    {
        if (!a->hop_phase)
            return a->timer >= 0 && a->timer <= 59 && !a->height_q16 &&
                   (a->velocity_z_q16 == 0 || a->velocity_z_q16 == -147456);
        if (a->timer < 60 || a->timer > 76)
            return 0;
        const int64_t n = a->timer - 60;
        return a->height_q16 == 131072 * n - 8192 * n * (n - 1) &&
               a->velocity_z_q16 == 131072 - 16384 * n;
    }
    if (a->timer != 32 || a->velocity_z_q16 > 196608 || a->velocity_z_q16 < -262144 ||
        (196608 - (int64_t)a->velocity_z_q16) % 16384)
        return 0;
    const int64_t n = (196608 - (int64_t)a->velocity_z_q16) / 16384;
    if (a->flags == 1)
    {
        if (a->height_q16 < 0 || (n && !a->height_q16))
            return 0;
        return hop_height((int64_t)a->height_q16 - 196608 * n + 8192 * n * (n - 1));
    }
    if (a->height_q16 || a->art || n < 25 || n > 28)
        return 0;
    for (int start = 0; start <= 17; ++start)
    {
        int64_t h = 131072 * (int64_t)start - 8192 * (int64_t)start * (start - 1);
        int64_t now = h + 196608 * n - 8192 * n * (n - 1),
                before = h + 196608 * (n - 1) - 8192 * (n - 1) * (n - 2);
        if (now <= 0 && before > 0)
            return 1;
    }
    return 0;
}
static int piranha_valid(const PortablePiranha *a, const PortablePiranha *initial)
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 ||
        a->checkpoint != initial->checkpoint || a->placement_order != initial->placement_order ||
        a->art > 4 || a->struck_player > 1 || a->flip_y > 1 || a->affine_x_q8 != 224)
        return 0;
    if (a->state == 240)
        return a->height_q16 == 0 && a->velocity_z_q16 == 0 && (a->flags == 1 || a->flags == 2) &&
               a->affine_y_q8 == 224 && !a->flip_y && a->timer == 0 &&
               (a->phase == 0 || a->phase == 5);
    if (a->state == 241 || a->state == 243)
    {
        if (a->affine_y_q8 != 256 || !a->flip_y || (a->flags != 0 && a->flags != 1) || a->timer ||
            (a->phase != 0 && a->phase != 5))
            return 0;
        if (a->state == 243)
            return a->flags == 0 && a->height_q16 == -212992 && a->velocity_z_q16 == -212992;
        const int64_t difference = 196608 - (int64_t)a->velocity_z_q16;
        if (difference < 16384 || difference > 25 * 16384 || difference % 16384)
            return 0;
        const int64_t n = difference / 16384;
        return a->height_q16 == 196608 * n - 8192 * n * (n - 1);
    }
    if (a->height_q16 || a->velocity_z_q16 || a->affine_y_q8 != 224 || a->flip_y)
        return 0;
    if (a->state == 0)
        return !a->timer && ((a->phase == 0 && a->flags == 2 && a->art == 0) ||
                             (a->phase == 5 && a->flags == 1 && a->art == 3));
    if (a->state == 1 || a->state == 16)
        return a->flags == 2 && !a->timer &&
               ((a->phase == 0 && a->art == 0) || (a->phase == 5 && a->art == 3));
    if (a->state == 17)
    {
        const int32_t timers[5] = {3, 3, 6, 4, 2};
        const uint8_t art[5] = {0, 1, 2, 4, 3};
        return a->flags == 1 && a->phase >= 0 && a->phase <= 4 && a->timer >= 0 &&
               a->timer <= timers[a->phase] && a->art == art[a->phase];
    }
    return 0;
}
static int capture_valid(const PortableMeshCaptureState *a, const PortableMeshCourseActorsConfig *c,
                         uint32_t tick)
{
    if (!a->remaining)
    {
        if (a->actor_index_plus1 || a->started_tick || a->spin)
            return 0;
        for (unsigned j = 0; j < 3; ++j)
            if (a->pull_q16[j] || a->forward_q30[j] || a->up_q30[j])
                return 0;
        return 1;
    }
    if (!c->capture_surface_count || a->remaining > 92 || !a->actor_index_plus1 ||
        a->actor_index_plus1 > c->piranha_count || a->started_tick > tick || a->spin > 65535)
        return 0;
    for (unsigned j = 0; j < 3; ++j)
        if (a->pull_q16[j] < -2 * c->scale_q16 || a->pull_q16[j] > 2 * c->scale_q16 ||
            a->forward_q30[j] < -1073741824 || a->forward_q30[j] > 1073741824 ||
            a->up_q30[j] < -1073741824 || a->up_q30[j] > 1073741824)
            return 0;
    return 1;
}
static int32_t gull_height(const PortableSeagull *a, const int32_t *sine)
{
    const uint16_t bits = (uint16_t)((uint32_t)(a->phase * 4 + a->timer) << 10);
    const int32_t angle = bits <= 32767 ? (int32_t)bits : (int32_t)bits - 65536;
    return sine[(unsigned)(angle / 64) & 1023u] * 20;
}
static int gull_base_height(int64_t height, const PortableSeagull *a, const int32_t *sine)
{
    return height == gull_height(a, sine) ||
           (height == 0 && (a->phase == 0 || a->phase == 8) && a->timer == 0);
}
static int seagull_valid(const PortableSeagull *a, const PortableSeagull *initial,
                         const int32_t *sine)
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 ||
        a->checkpoint != initial->checkpoint || a->placement_order != initial->placement_order ||
        a->art > 2 || a->timer < 0 || a->timer > 4 || a->phase < 0 || a->phase > 8 ||
        (a->phase == 8 && a->timer) || (a->affine_x_q8 != 224 && a->affine_x_q8 != 256))
        return 0;
    if (a->state == 240 || a->state == 241 || a->state == 243)
    {
        if (a->state == 240)
            return a->flags == 3 && a->affine_y_q8 == 224 && !a->flip_y && !a->velocity_z_q16 &&
                   gull_base_height(a->height_q16, a, sine);
        if (a->affine_y_q8 != 256 || a->flip_y != 1)
            return 0;
        const int64_t difference = 196608 - (int64_t)a->velocity_z_q16;
        if (difference < 0 || difference > 27 * 16384 || difference % 16384)
            return 0;
        const int64_t n = difference / 16384 + (a->state == 243);
        if (a->state == 241 && (!n || a->flags != 1 || a->height_q16 < 0))
            return 0;
        if (a->state == 243 && (a->flags || a->height_q16 >= 0 ||
                                (n > 1 && (int64_t)a->height_q16 - a->velocity_z_q16 < 0)))
            return 0;
        return gull_base_height((int64_t)a->height_q16 - 196608 * n + 8192 * n * (n - 1), a, sine);
    }
    if (a->flags != 3 || a->affine_y_q8 != 224 || a->flip_y || a->velocity_z_q16)
        return 0;
    if (a->state == 17)
        return a->affine_x_q8 == 256 && a->phase <= 7 && a->art == 1 + (a->phase & 1) &&
               a->height_q16 == gull_height(a, sine);
    if (a->state == 0)
        return (a->phase == 0 && !a->timer && !a->art && !a->height_q16) ||
               (a->phase == 8 && !a->timer && a->art == 2 && a->height_q16 == gull_height(a, sine));
    if (a->state == 1)
        return a->affine_x_q8 == 256 && !a->art && !a->height_q16 && !a->timer &&
               (a->phase == 0 || a->phase == 8);
    if (a->state == 16)
        return !a->timer && (a->phase == 0 || a->phase == 8) &&
               gull_base_height(a->height_q16, a, sine);
    return 0;
}
/* Protected motion copies signed source halfwords, not full SI velocities.
 * The multiplication is <2^63 for validated 256m/s and scale>=1311.
 * Convert metres/second to source Q8 component units/turn, truncate toward zero,
 * then explicitly retain/sign-extend the low sixteen bits as 55F74 does.
 * Driver position adds component*256; the actor deliberately copies that
 * small component directly into its Q16 velocity, without rescaling it. */
static int16_t lava_velocity(int32_t velocity, int32_t scale)
{
    int64_t source = (int64_t)velocity * 4389 * 256 / ((int64_t)262144 * scale);
    uint16_t bits = (uint16_t)((uint64_t)source & 65535u);
    return bits <= 32767 ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
}
static int lava_valid(const PortableLavaBubble *a, const PortableLavaBubble *b, const int32_t *sine)
{
    if (a->period != b->period || a->checkpoint != b->checkpoint ||
        a->placement_order != b->placement_order || a->phase > 6 || a->art > 1 || (a->flags & ~3u))
        return 0;
    if (a->phase >= 4)
    {
        if (a->velocity_x_q16 < INT16_MIN || a->velocity_x_q16 > INT16_MAX ||
            a->velocity_y_q16 < INT16_MIN || a->velocity_y_q16 > INT16_MAX)
            return 0;
        int64_t n = 0;
        if (a->phase == 4)
        {
            if (a->flags != 3 || a->velocity_z_q16 != 196608)
                return 0;
        }
        else
        {
            int64_t d = 196608 - (int64_t)a->velocity_z_q16;
            if (d < 16384 || d > 32 * 16384 || d % 16384)
                return 0;
            n = d / 16384;
            if (a->art != (a->velocity_z_q16 + 16384 > 0 ? 0 : 1))
                return 0;
            if (a->phase == 5 && (a->flags != 1 || a->height_q16 < 0))
                return 0;
            if (a->phase == 6 &&
                (a->flags || a->height_q16 >= 0 ||
                 (n > 1 && (int64_t)a->height_q16 - (a->velocity_z_q16 + 16384) < 0)))
                return 0;
        }
        if ((int64_t)a->x_q16 != (int64_t)b->x_q16 + n * a->velocity_x_q16 ||
            (int64_t)a->y_q16 != (int64_t)b->y_q16 + n * a->velocity_y_q16)
            return 0;
        int64_t height = (int64_t)a->height_q16 - 196608 * n + 8192 * n * (n - 1);
        for (unsigned t = 0; t < 64; ++t)
            if (height == (int64_t)sine[t * 8] * 48)
                return 1;
        return 0;
    }
    if (a->x_q16 != b->x_q16 || a->y_q16 != b->y_q16 || a->velocity_x_q16 || a->velocity_y_q16 ||
        a->velocity_z_q16)
        return 0;
    if (a->phase == 3)
        return a->flags == 3 && a->timer >= 1 && a->timer <= 63 &&
               a->height_q16 == sine[a->timer * 8] * 48 && a->art == (a->timer <= 31);
    if (a->phase == 0)
        return a->flags == 3 && a->timer == a->period && a->art == 1 &&
               a->height_q16 == sine[0] * 48;
    if (a->phase == 2)
        return !a->flags && a->timer == 64 && (a->height_q16 == 0 || a->height_q16 == sine[0] * 48);
    return a->phase == 1 && !a->flags && a->timer >= 0 && a->timer <= 200 &&
           (a->height_q16 == 0 || a->height_q16 == sine[0] * 48);
}
/* All normal source variants stay within their finite shuttle envelope.
 * Protected Q8*384 flight can move out of the course, but remains bounded by
 * at most29 ballistic updates from the normal32.5-source-unit ceiling. */
static int thwomp_valid(const PortableThwomp *a, const PortableThwomp *b)
{
    if (a->variant != b->variant || a->checkpoint != b->checkpoint ||
        a->placement_order != b->placement_order || (a->flags & ~3u) || a->variant > 11)
        return 0;
    const unsigned normal = portable_thwomp_normal_states(a->variant);
    const unsigned flight = a->variant <= 3 ? 7 : normal + 1;
    if (a->state == flight)
    {
        const int64_t d = 147456 - (int64_t)a->velocity_z_q16;
        if (d < 0 || d > 29 * 16384 || d % 16384 || (a->flags != 1 && a->flags != 0) ||
            a->velocity_x_q16 % 384 || a->velocity_y_q16 % 384 ||
            a->velocity_x_q16 / 384 < INT16_MIN || a->velocity_x_q16 / 384 > INT16_MAX ||
            a->velocity_y_q16 / 384 < INT16_MIN || a->velocity_y_q16 / 384 > INT16_MAX)
            return 0;
        int64_t n = d / 16384;
        if (!a->flags && (!n || a->height_q16 > 0))
            return 0;
        if (n && a->flags && a->height_q16 <= 0)
            return 0;
        if (!a->flags && n > 1 && (int64_t)a->height_q16 - (a->velocity_z_q16 + 16384) <= 0)
            return 0;
        int64_t initial_height = (int64_t)a->height_q16 - 147456 * n + 8192 * n * (n - 1);
        if (initial_height < 0 || initial_height > 2129920)
            return 0;
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            int64_t p = axis ? a->y_q16 : a->x_q16, origin = axis ? b->y_q16 : b->x_q16;
            int64_t velocity = axis ? a->velocity_y_q16 : a->velocity_x_q16;
            int64_t offset = p - n * velocity - origin;
            if (offset < -512 * 65536 || offset > 512 * 65536)
                return 0;
        }
        return 1;
    }
    if (a->state >= normal || a->flags != 3 || a->timer < 0 || a->timer > 100 ||
        a->height_q16 < 0 || a->height_q16 > 2129920 || a->velocity_x_q16 != b->velocity_x_q16 ||
        a->velocity_y_q16 != b->velocity_y_q16 ||
        (a->velocity_z_q16 != 0 && a->velocity_z_q16 != 32768 && a->velocity_z_q16 != -262144))
        return 0;
    return (int64_t)a->x_q16 - b->x_q16 >= -512 * 65536 &&
           (int64_t)a->x_q16 - b->x_q16 <= 512 * 65536 &&
           (int64_t)a->y_q16 - b->y_q16 >= -512 * 65536 &&
           (int64_t)a->y_q16 - b->y_q16 <= 512 * 65536;
}
/* Published states only: transition0/2 falls through on the same source
 * update. Exact falling parabola and countdown-indexed explosion frames keep
 * corrupted clocks/scales from silently entering the core state machine. */
static int cannonball_valid(const PortableCannonball *a, const PortableCannonball *initial,
                            unsigned variant)
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 ||
        a->checkpoint != initial->checkpoint || a->placement_order != initial->placement_order ||
        variant > 3 || a->affine_x_q8 != a->affine_y_q8)
        return 0;
    if (a->state == 1)
    {
        if (a->flags || a->size != 4 || a->height_q16 || a->affine_x_q8 != 256 || a->timer < 0)
            return 0;
        return (a->art == 0 && !a->velocity_z_q16 && a->timer <= (int32_t)(variant * 32)) ||
               (a->art == 6 && a->velocity_z_q16 == -104448 && a->timer <= 64);
    }
    if (a->state == 3)
    {
        if (a->velocity_z_q16 >= 0 || a->velocity_z_q16 % 2048)
            return 0;
        int64_t n = -(int64_t)a->velocity_z_q16 / 2048;
        int64_t affine = 1024 - 24 * n;
        if (affine < 256)
            affine = 256;
        return n >= 1 && n <= 50 && a->timer == 1000 - n &&
               a->height_q16 == 2621440 - 1024 * n * (n + 1) && a->affine_x_q8 == affine &&
               a->flags == 3 && !a->art && a->size == 4;
    }
    if (a->height_q16 || a->affine_x_q8 != 256 || a->velocity_z_q16 != -104448)
        return 0;
    if (a->state == 4)
        return a->timer == 1 && a->flags == 3 && !a->art && a->size == 4;
    if (a->state != 5 || a->timer < 0 || a->timer > 31 || a->flags != 1)
        return 0;
    unsigned frame = a->timer == 31   ? 6
                     : a->timer >= 28 ? 1
                     : a->timer >= 24 ? 2
                     : a->timer >= 20 ? 3
                     : a->timer >= 14 ? 4
                     : a->timer >= 8  ? 5
                                      : 6;
    return a->art == frame && a->size == (a->timer == 31 ? 4 : 20);
}
static int boo_valid(const PortableBoo *a, const PortableBoo *initial, const int32_t *sine)
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 ||
        a->checkpoint != initial->checkpoint || a->placement_order != initial->placement_order ||
        a->base_height_q16 != 8 * 65536 || a->velocity_z_q16 || a->heading != 0x4000 ||
        a->affine_y_q8 != 256 || a->flags != 3 || (a->phase & 1023u))
        return 0;
    if (a->state == 16)
        return !a->phase && !a->height_q16;
    if (a->state != 17)
        return 0;
    const uint16_t bits = (uint16_t)(a->phase - 1024u);
    int32_t phase = bits <= 32767 ? bits : (int32_t)bits - 65536;
    if (phase < 0)
        phase += 63;
    const unsigned index = (uint16_t)phase / 64u;
    return a->height_q16 == a->base_height_q16 + sine[index] * 3;
}
/* Closed source phases:45 falling updates,23 first-bounce updates and13
 * second-bounce updates. Validate their exact integer parabolas rather than
 * allowing arbitrary velocities to reach the unchanged source integrator. */
static int rock_valid(const PortableCourseProp *a, const PortableCourseProp *initial)
{
    const PortableLakeRockMotion *m = &a->lake_rock;
    if (a->source_code != initial->source_code || a->checkpoint != initial->checkpoint ||
        a->placement_order != initial->placement_order || a->art_index || a->timer ||
        a->contact_phase || m->home_x_q16 != initial->x_q16 || m->home_y_q16 != initial->y_q16 ||
        m->delay != initial->lake_rock.delay || m->mirrored || a->state > 11 ||
        a->affine_x_q8 != a->affine_y_q8 || m->velocity_z_q16 < -138240 ||
        m->velocity_z_q16 > 65536 || a->height_q16 < 0 || a->height_q16 > 48 * 65536)
        return 0;
    int32_t height = 0, velocity = 0;
    unsigned steps = 0, frame = 0;
    int affine = 192, flags = 3;
    if (a->state <= 2)
    {
        const int first = m->velocity_z_q16 == 0;
        affine = first ? initial->affine_x_q8 : 192;
        velocity = first ? 0 : -38912;
        flags = a->state == 0 && !first ? 3 : 0;
        if (a->state == 0)
        {
            if (m->timer)
                return 0;
            steps = first ? 0 : 36;
        }
        else if ((a->state == 1 && (m->timer < 1 || m->timer >= m->delay)) ||
                 (a->state == 2 && m->timer))
            return 0;
    }
    else if (a->state <= 4)
    {
        const int n = -m->velocity_z_q16 / 3072;
        if (m->velocity_z_q16 >= 0 || m->velocity_z_q16 % 3072 ||
            (a->state == 3 ? n < 1 || n > 44 : n != 45) || m->timer)
            return 0;
        velocity = -3072 * n;
        height = a->state == 4 ? 0 : 48 * 65536 - 1536 * n * (n + 1);
        affine = 1024 - 24 * n;
        if (affine < 192)
            affine = 192;
    }
    else if (a->state <= 6)
    {
        velocity = -138240;
        if ((a->state == 5 && (m->timer < 1 || m->timer > 9)) || (a->state == 6 && m->timer))
            return 0;
        const int before = m->timer + 1;
        frame = before > 8 ? 1 : before > 5 ? 2 : before > 2 ? 3 : 4;
    }
    else if (a->state <= 8)
    {
        if (m->timer || m->velocity_z_q16 > 65536 || (65536 - m->velocity_z_q16) % 6144)
            return 0;
        const int n = (65536 - m->velocity_z_q16) / 6144;
        if (a->state == 7 ? n < 1 || n > 22 : n != 22)
            return 0;
        velocity = 65536 - 6144 * n;
        steps = a->state == 8 ? 23 : (unsigned)n;
        height = a->state == 8 ? 0 : n * 65536 - 3072 * n * (n - 1);
    }
    else
    {
        if (a->state == 9)
        {
            if (m->timer || m->velocity_z_q16 > 34816 || (34816 - m->velocity_z_q16) % 6144)
                return 0;
            const int n = (34816 - m->velocity_z_q16) / 6144;
            if (n < 1 || n > 12)
                return 0;
            velocity = 34816 - 6144 * n;
            steps = 23 + (unsigned)n;
            height = n * 34816 - 3072 * n * (n - 1);
        }
        else
        {
            if ((a->state == 10 && m->timer) ||
                (a->state == 11 && (m->timer < 1 || m->timer > 127)))
                return 0;
            velocity = -38912;
            steps = 36;
        }
    }
    return a->height_q16 == height && m->velocity_z_q16 == velocity && m->frame == frame &&
           a->affine_x_q8 == affine && a->flags == flags &&
           (int64_t)a->x_q16 == (int64_t)m->home_x_q16 + steps * 16384 &&
           (int64_t)a->y_q16 == (int64_t)m->home_y_q16 + steps * 16384;
}
static int teepee_valid(const PortableCourseProp *a, const PortableCourseProp *initial)
{
    const PortableTeepeeMotion *m = &a->teepee;
    if (a->source_code != 0x030000 || a->checkpoint != initial->checkpoint ||
        a->placement_order != initial->placement_order || a->height_q16 || a->timer ||
        a->art_index || a->contact_phase || a->affine_x_q8 != initial->affine_x_q8 ||
        a->affine_y_q8 != initial->affine_y_q8)
        return 0;
    if (a->state == 0)
        return a->flags == 3 && a->x_q16 == initial->x_q16 && a->y_q16 == initial->y_q16 &&
               teepee_motion_zero(m);
    if ((a->state != 2 && a->state != 3) ||
        (a->state == 2 ? m->counter < 1 || m->counter > 30 || a->flags != 1
                       : m->counter != 31 || a->flags) ||
        m->frame != (m->counter <= 10   ? 1
                     : m->counter <= 20 ? 2
                                        : 3) ||
        m->kart >= 8 || m->rider_state > 2 || m->velocity_x_q16 < -8388608 ||
        m->velocity_x_q16 > 8388352 || m->velocity_y_q16 < -8388608 ||
        m->velocity_y_q16 > 8388352 || m->velocity_x_q16 % 256 || m->velocity_y_q16 % 256 ||
        (uint32_t)a->x_q16 != (uint32_t)initial->x_q16 + (uint32_t)m->velocity_x_q16 * m->counter ||
        (uint32_t)a->y_q16 != (uint32_t)initial->y_q16 + (uint32_t)m->velocity_y_q16 * m->counter)
        return 0;
    if (m->rider_state == 1)
        return !m->rider_vx_q16 && !m->rider_vy_q16 && !m->rider_vz_q16 && !m->rider_spin &&
               !m->rider_spin_rate && !m->rider_gravity;
    return (m->rider_gravity == 0x1800
                ? !m->rider_spin_rate
                : m->rider_gravity == 0x2000 && m->rider_spin_rate == 0x1000) &&
           m->rider_vx_q16 >= -12582912 && m->rider_vx_q16 <= 12582528 &&
           m->rider_vy_q16 >= -12582912 && m->rider_vy_q16 <= 12582528 &&
           m->rider_vz_q16 >= -128 * 65536 && m->rider_vz_q16 <= 0x12000;
}
/* The source only publishes states1(wait),3(descent),5(bounce). Derive each
 * complete named state from its timer, including the retained last-cycle
 * velocities, rather than accepting arbitrary values that core advance leaves
 * untouched. Products are bounded by64 descending and41 bounce steps. */
static int falling_star_same(const PortableFallingStar *a, const PortableFallingStar *b)
{
    return a->x_q16 == b->x_q16 && a->y_q16 == b->y_q16 && a->height_q16 == b->height_q16 &&
           a->initial_x_q16 == b->initial_x_q16 && a->offset_q16 == b->offset_q16 &&
           a->velocity_x_q16 == b->velocity_x_q16 && a->velocity_z_q16 == b->velocity_z_q16 &&
           a->timer == b->timer && a->delay == b->delay && a->affine_q8 == b->affine_q8 &&
           a->angle == b->angle && a->state == b->state && a->variant == b->variant &&
           a->checkpoint == b->checkpoint && a->placement_order == b->placement_order &&
           a->flags == b->flags && a->art == b->art;
}
static void falling_star_bounce_state(PortableFallingStar *b, unsigned steps)
{
    const int64_t z = (int64_t)steps * 0x28000 - (int64_t)steps * (steps - 1) / 2 * 0x2800;
    b->state = 5;
    b->timer = (int16_t)(41 - steps);
    b->affine_q8 = 256;
    b->flags = 3;
    b->velocity_x_q16 = -(b->offset_q16 / 64);
    b->velocity_z_q16 = 0x28000 - (int32_t)steps * 0x2800;
    b->x_q16 = b->initial_x_q16 + (int32_t)steps * b->velocity_x_q16;
    b->height_q16 = (int32_t)(z > 0 ? z : 0);
    b->angle = (uint16_t)(steps * (b->offset_q16 < 0 ? 768u : UINT16_C(0xfd00)));
    b->art = b->timer <= 3    ? 4
             : b->timer <= 7  ? 1
             : b->timer <= 11 ? 3
             : b->timer <= 15 ? 2
             : b->timer <= 19 ? 1
                              : 0;
}
static int falling_star_valid(const PortableFallingStar *a, const PortableFallingStar *initial)
{
    if (falling_star_same(a, initial))
        return 1;
    PortableFallingStar b = *initial;
    if (a->state == 3 && a->timer >= 0 && a->timer <= 64)
    {
        const unsigned steps = (unsigned)(64 - a->timer);
        b.state = 3;
        b.timer = a->timer;
        b.flags = 3;
        b.art = 0;
        b.affine_q8 = steps < 11 ? (uint16_t)(960 - steps * 64) : 256;
        b.angle = (uint16_t)((steps + 1) * (b.offset_q16 < 0 ? 1024u : UINT16_C(0xfc00)));
        b.x_q16 = b.initial_x_q16 + b.offset_q16 - (int32_t)(steps * steps) * (b.offset_q16 / 4096);
        b.height_q16 = 0x400000 - (int32_t)(steps * steps) * 0x400;
        if (falling_star_same(a, &b))
            return 1;
        b.velocity_x_q16 = -(b.offset_q16 / 64);
        b.velocity_z_q16 = 0x28000 - 41 * 0x2800;
        /* A new descent resets art/affine but retains the preceding bounce's
         * final rotation. Bounce itself resets angle on entry every cycle. */
        b.angle = (uint16_t)(b.angle + 41u * (b.offset_q16 < 0 ? 768u : UINT16_C(0xfd00)));
        return falling_star_same(a, &b);
    }
    if (a->state == 5 && a->timer >= 0 && a->timer <= 40)
    {
        falling_star_bounce_state(&b, (unsigned)(41 - a->timer));
        return falling_star_same(a, &b);
    }
    if (a->state == 1 && a->timer >= 0 && a->timer <= b.delay)
    {
        falling_star_bounce_state(&b, 41);
        b.state = 1;
        b.timer = a->timer;
        b.flags = 0;
        return falling_star_same(a, &b);
    }
    return 0;
}
static int thundercloud_valid(const PortableThundercloud *a, const PortableThundercloud *initial)
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 || a->height_q16 != 4 * 65536 ||
        a->variant != initial->variant || a->checkpoint != initial->checkpoint ||
        a->placement_order != initial->placement_order)
        return 0;
    if (a->state == 1)
        return a->timer >= 0 && a->timer <= 32 && !a->palette_timer && a->flags == 1 && a->art == 4;
    if (a->state == 16)
        return a->timer == -1 && a->palette_timer == ((a->variant & 1) ? 0 : 1) && a->flags == 1 &&
               a->art == 4;
    if (a->state != 17 || a->timer < 0 || a->timer > 241 ||
        a->palette_timer != ((a->variant & 1) ? 0 : (a->timer + 1) % 242))
        return 0;
    const unsigned processed = a->timer ? (unsigned)a->timer - 1 : 241;
    const unsigned base = a->variant < 2 ? 0 : 4;
    unsigned art = 0;
    if ((processed >= 60 && processed < 64) || (processed >= 94 && processed < 98) ||
        (processed >= 158 && processed < 162) || (processed >= 166 && processed < 170))
        art = 1;
    else if (processed >= 234)
        art = 3;
    else if (processed >= 226)
        art = 2;
    return a->flags == (processed >= 234 ? 3 : 1) && a->art == base + art;
}
static int common_prop_valid(const PortableCourseProp *a, const PortableCourseProp *initial,
                             const int32_t sine[1024])
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 ||
        a->source_code != initial->source_code || a->checkpoint != initial->checkpoint ||
        a->placement_order != initial->placement_order || a->affine_x_q8 != initial->affine_x_q8 ||
        a->art_index || !common_prop_motion_zero(a) ||
        a->contact_phase > (a->source_code == 0x860000 ? 1 : 0))
        return 0;
    if (!a->state)
        return !a->height_q16 && !a->timer && a->affine_y_q8 == initial->affine_y_q8 &&
               a->flags == initial->flags;
    if (a->state != 1 || a->timer < -1 || a->timer > 32 || a->affine_y_q8 != -256)
        return 0;
    if (a->timer == 32)
        return !a->height_q16 && a->flags == initial->flags;
    const uint8_t flags = (uint8_t)(initial->flags & (a->timer < 0 ? ~3u : ~2u));
    return a->flags == flags && a->height_q16 == sine[(unsigned)(a->timer + 1) * 16] * 48;
}
/* Patrol cycle is the next source step. Segment lengths are exact
 * multiples of their animation periods, so stride counters derive directly
 * without replaying the352-step cycle on every contact/view. Protected flight
 * freezes this complete patrol snapshot rather than discarding its phase. */
static int mechakoopa_valid(const PortableCourseProp *a, const PortableCourseProp *initial,
                            const int32_t sine[1024])
{
    const PortableMechakoopaMotion *m = &a->mechakoopa;
    if (a->y_q16 != initial->y_q16 || a->source_code != initial->source_code ||
        a->checkpoint != initial->checkpoint || a->placement_order != initial->placement_order ||
        a->affine_x_q8 != initial->affine_x_q8 || a->art_index || a->contact_phase ||
        !mechakoopa_other_motion_zero(a) ||
        m->velocity_x_q16 != initial->mechakoopa.velocity_x_q16 || m->cycle > 351 ||
        m->has_facing > 1)
        return 0;
    if (!m->has_facing)
    {
        if (m->cycle || m->heading || m->animation_ticks || m->animation_phase ||
            a->x_q16 != initial->x_q16)
            return 0;
    }
    else
    {
        const unsigned elapsed = m->cycle ? m->cycle : 352;
        const unsigned forward = elapsed < 96 ? elapsed : 96;
        const unsigned backward = elapsed <= 176 ? 0 : elapsed < 272 ? elapsed - 176 : 96;
        const int64_t x = initial->x_q16 + ((int64_t)forward - backward) * m->velocity_x_q16;
        unsigned segment, period;
        if (elapsed <= 96)
        {
            segment = elapsed;
            period = 6;
        }
        else if (elapsed <= 176)
        {
            segment = elapsed - 96;
            period = 10;
        }
        else if (elapsed <= 272)
        {
            segment = elapsed - 176;
            period = 6;
        }
        else
        {
            segment = elapsed - 272;
            period = 10;
        }
        const int32_t direction = elapsed <= 176 ? m->velocity_x_q16 : -m->velocity_x_q16;
        if (a->x_q16 != x || m->heading != (direction > 0 ? 0x4000 : 0xc000) ||
            m->animation_ticks != segment % period || m->animation_phase != (segment / period) % 2)
            return 0;
    }
    if (!a->state)
        return !a->height_q16 && !a->timer && a->flags == 3 &&
               a->affine_y_q8 == initial->affine_y_q8;
    if (a->state != 1 || a->timer < -1 || a->timer > 32 || a->affine_y_q8 != -256)
        return 0;
    if (a->timer == 32)
        return !a->height_q16 && a->flags == 1;
    return a->flags == (a->timer < 0 ? 0 : 1) &&
           a->height_q16 == sine[(unsigned)(a->timer + 1) * 16] * 48;
}
static int32_t kamek_bob(uint16_t phase, const int32_t sine[1024])
{
    /* Reachable phases are multiples of1024, so the source signed /64
     * bias cannot cross a table cell. The full signed range remains tested. */
    return 0xc0000 + sine[phase / 64] * 3;
}
static int kamek_patrol_match(const PortableCourseProp *a, const PortableCourseProp *initial,
                              const int32_t sine[1024], unsigned steps, int first, int forward)
{
    const PortableKamekMotion *m = &a->kamek;
    int64_t x;
    int32_t vx;
    const int64_t right = 120 * INT64_C(2048) + 48 * INT64_C(32768);
    if (first)
    {
        const unsigned accelerating = steps < 16 ? steps : 16;
        x = initial->x_q16 + (int64_t)accelerating * ((int64_t)accelerating - 1) / 2 * 2048 +
            (int64_t)(steps - accelerating) * 32768;
        vx = (int32_t)accelerating * 2048;
    }
    else
    {
        const unsigned accelerating = steps < 32 ? steps : 32;
        int64_t displacement = (int64_t)accelerating * 32768 -
                               (int64_t)accelerating * ((int64_t)accelerating - 1) / 2 * 2048 -
                               (int64_t)(steps - accelerating) * 32768;
        x = initial->x_q16 + right + (forward ? -47 * INT64_C(65536) - 32768 : 0) +
            (forward ? -displacement : displacement);
        vx = 32768 - (int32_t)accelerating * 2048;
        if (forward)
            vx = -vx;
    }
    const uint16_t heading = vx ? (vx > 0 ? 0x4000 : 0xc000) : (forward ? 0x4000 : 0xc000);
    const uint16_t phase = (uint16_t)((steps + 1) * 1024u);
    return a->x_q16 == x && m->velocity_x_q16 == vx && m->heading == heading && m->phase == phase &&
           a->height_q16 == kamek_bob(phase, sine);
}
static int kamek_patrol_valid(const PortableCourseProp *a, const PortableCourseProp *initial,
                              const int32_t sine[1024])
{
    const PortableKamekMotion *m = &a->kamek;
    if (a->flags != 3 || a->affine_y_q8 != initial->affine_y_q8 || m->velocity_z_q16)
        return 0;
    if (!m->has_facing)
    {
        if (m->velocity_x_q16 || m->heading || a->x_q16 != initial->x_q16)
            return 0;
        if (!a->state)
            return !a->timer && !m->phase && !a->height_q16;
        return a->state == 2 && a->timer == 64 && m->phase == 1024 &&
               a->height_q16 == kamek_bob(1024, sine);
    }
    if (a->state == 3 && a->timer == 128)
        return kamek_patrol_match(a, initial, sine, 64, 1, 1);
    if (a->state == 1 && a->timer == 128)
        return kamek_patrol_match(a, initial, sine, 128, 0, 0);
    if (a->timer < 1 || a->timer > 127)
        return 0;
    if (a->state == 4)
        return kamek_patrol_match(a, initial, sine, (unsigned)(128 - a->timer), 0, 0);
    if (a->state == 2)
        return (a->timer < 64 &&
                kamek_patrol_match(a, initial, sine, (unsigned)(64 - a->timer), 1, 1)) ||
               kamek_patrol_match(a, initial, sine, (unsigned)(128 - a->timer), 0, 1);
    return 0;
}
static int kamek_valid(const PortableCourseProp *a, const PortableCourseProp *initial,
                       const int32_t sine[1024])
{
    const PortableKamekMotion *m = &a->kamek;
    if (a->y_q16 != initial->y_q16 || a->source_code != initial->source_code ||
        a->checkpoint != initial->checkpoint || a->placement_order != initial->placement_order ||
        a->affine_x_q8 != initial->affine_x_q8 || a->art_index || a->contact_phase ||
        !kamek_other_motion_zero(a) || m->mirrored || m->has_facing > 1 || (m->phase & 1023))
        return 0;
    if (a->state <= 4)
        return kamek_patrol_valid(a, initial, sine);
    if (a->state != 5 && a->state != 6 && a->state != 8)
        return 0;
    unsigned steps = 0;
    if (a->state == 5)
    {
        if (m->velocity_z_q16 || a->flags != 3 || a->affine_y_q8 != initial->affine_y_q8)
            return 0;
    }
    else
    {
        const int64_t delta = INT64_C(0x30000) - m->velocity_z_q16;
        if (delta < 0 || delta % 0x4000 || a->affine_y_q8 != -256 ||
            a->flags != (a->state == 6 ? 1 : 0))
            return 0;
        const int64_t n = delta / 0x4000 + (a->state == 8 ? 1 : 0);
        if (n < 1 || n > 29)
            return 0;
        steps = (unsigned)n;
    }
    PortableCourseProp prior = *a;
    prior.flags = 3;
    prior.affine_y_q8 = initial->affine_y_q8;
    prior.kamek.velocity_z_q16 = 0;
    for (unsigned state = 0; state <= 4; ++state)
    {
        prior.state = (uint8_t)state;
        prior.height_q16 = state == 0 ? 0 : kamek_bob(m->phase, sine);
        if (!kamek_patrol_valid(&prior, initial, sine))
            continue;
        if (a->state == 5)
            return a->height_q16 == prior.height_q16;
        const int64_t height =
            prior.height_q16 + (int64_t)steps * 0x30000 - (int64_t)steps * (steps - 1) / 2 * 0x4000;
        if (a->height_q16 != height)
            return 0;
        if (a->state == 6)
            return height >= 0;
        const unsigned previous = steps - 1;
        const int64_t previous_height = prior.height_q16 + (int64_t)previous * 0x30000 -
                                        (int64_t)previous * ((int64_t)previous - 1) / 2 * 0x4000;
        return height < 0 && previous_height >= 0;
    }
    return 0;
}
static int pier_other_zero(const PortableCourseProp *a)
{
    PortableCourseProp copy = *a;
    memset(&copy.pier_boo, 0, sizeof copy.pier_boo);
    return common_prop_motion_zero(&copy);
}
static int32_t pier_bob(uint16_t phase, const int32_t *sine)
{
    return 0x80000 + 3 * sine[phase / 64];
}
static int pier_valid(const PortableCourseProp *a, const PortableCourseProp *initial,
                      const int32_t *sine)
{
    if (a->x_q16 != initial->x_q16 || a->y_q16 != initial->y_q16 || a->source_code != 0xa30000 ||
        a->timer || a->art_index || a->contact_phase || a->checkpoint != initial->checkpoint ||
        a->placement_order != initial->placement_order || a->affine_x_q8 != initial->affine_x_q8 ||
        !pier_other_zero(a) || (a->pier_boo.phase & 1023))
        return 0;
    const int32_t bob = pier_bob((uint16_t)(a->pier_boo.phase - 1024), sine);
    if (a->state == 0 || a->state == 5 || (a->state == 8 && !a->pier_boo.velocity_z_q16))
        return a->pier_boo.velocity_z_q16 == 0 && a->affine_y_q8 == initial->affine_y_q8 &&
               a->flags == (a->state == 8 ? 0 : 3) &&
               (a->height_q16 == 0x80000 || a->height_q16 == bob);
    if (a->state != 6 && a->state != 8)
        return 0;
    const int64_t d = INT64_C(0xc0000) - a->pier_boo.velocity_z_q16;
    if (d < 0 || d % 0x4000 || a->affine_y_q8 != -256 || a->flags != (a->state == 6 ? 1 : 0))
        return 0;
    int64_t n = d / 0x4000 + (a->state == 8 ? 1 : 0);
    if (n < 1 || n > 128)
        return 0;
    const int64_t delta = n * 0xc0000 - n * (n - 1) / 2 * 0x4000;
    const int64_t before = (n - 1) * 0xc0000 - (n - 1) * (n - 2) / 2 * 0x4000;
    const int32_t starts[2] = {0x80000, bob};
    for (unsigned i = 0; i < 2; ++i)
        if (a->height_q16 == starts[i] + delta &&
            (a->state == 6 ? a->height_q16 >= 0 : a->height_q16 < 0 && starts[i] + before >= 0))
            return 1;
    return 0;
}
static int haunt_empty(const PortableBooAttack *a)
{
    return !a->x_q16 && !a->y_q16 && !a->z_q16 && !a->vx_q16 && !a->vy_q16 && !a->vz_q16 &&
           !a->angle && !a->bob_angle && !a->timer && !a->radius && !a->affine_y_q8 && !a->phase &&
           !a->target && !a->occupied && !a->visible && !a->frame;
}
static int haunt_valid(const PortableMeshCourseActorsConfig *c, const PortableMeshCourseActors *w,
                       unsigned i)
{
    const PortableBooAttack *a = &w->source_haunts[i];
    const int32_t *o = w->source_haunt_origins_q16[i];
    if (!a->occupied)
        return haunt_empty(a) && !o[0] && !o[1] && !o[2] && !w->source_haunt_source_indices[i];
    if (!c->pier_boo_count || a->occupied != 1 || a->visible != 1 || a->phase > 7 ||
        a->target >= 8 || a->frame > 1 || w->source_haunt_source_indices[i] >= c->pier_boo_count ||
        a->radius > c->pier_boo_rules.orbit_ticks || a->timer > c->pier_boo_rules.orbit_ticks ||
        a->affine_y_q8 != (a->phase == 7 ? -256 : 256))
        return 0;
    for (unsigned j = 0; j < 3; ++j)
        if (o[j] < -268435456 || o[j] > 268435456)
            return 0;
    if (a->phase != 7 && (a->vx_q16 || a->vy_q16 || a->vz_q16))
        return 0;
    const int32_t position[3] = {a->x_q16, a->y_q16, a->z_q16};
    for (unsigned j = 0; j < 3; ++j)
    {
        int64_t product = (int64_t)position[j] * c->scale_q16;
        int64_t value =
            o[j] + (product < 0 ? -((-product + 32768) / 65536) : (product + 32768) / 65536);
        if (value < -268435456 || value > 268435456)
            return 0;
    }
    return 1;
}
/* No ignored mutable witness/cache: every meaningful ground scalar is owned
 * and hashed; unused rows and the disabled path must remain exactly zero. */
static int ground_state_valid(const PortableMeshCourseActors *w)
{
    if (w->ground_follow_version > 1)
        return 0;
    const uint32_t counts[4] = {w->count, w->penguin_count, w->mouse_count, w->mechakoopa_count};
    const int32_t *offsets[4] = {w->crab_ground_offset_q16, w->penguin_ground_offset_q16,
        w->mouse_ground_offset_q16, w->mechakoopa_ground_offset_q16};
    for (unsigned f = 0; f < 4; ++f)
        for (unsigned i = 0; i < 64; ++i)
            if ((offsets[f][i] && (!w->ground_follow_version || i >= counts[f])) ||
                offsets[f][i] < -536870912 || offsets[f][i] > 536870912)
                return 0;
    return 1;
}
static int ground_follow(const PortableMeshCourseActorsConfig *, const PortableMeshCourse *,
                         PortableMeshCourseActors *, char *, size_t);
static int world_valid(const PortableMeshCourseActorsConfig *c, const PortableMeshCourseActors *w)
{
    if (!config_valid(c) || !w || w->ground_follow_version != c->ground_follow_version ||
        !ground_state_valid(w) || !portable_mesh_sky_state_valid(&c->sky, &w->sky) ||
        !portable_mesh_course_coins_valid(&c->coins, &w->coins) || w->count != c->count ||
        w->snowman_count != c->snowman_count || w->penguin_count != c->penguin_count ||
        w->mouse_count != c->mouse_count || w->piranha_count != c->piranha_count ||
        w->seagull_count != c->seagull_count || w->lava_count != c->lava_count ||
        w->thwomp_count != c->thwomp_count || w->cannonball_count != c->cannonball_count ||
        w->boo_count != c->boo_count || w->rock_count != c->rock_count ||
        w->teepee_count != c->teepee_count || w->falling_star_count != c->falling_star_count ||
        w->thundercloud_count != c->thundercloud_count ||
        w->common_prop_count != c->common_prop_count ||
        w->mechakoopa_count != c->mechakoopa_count || w->kamek_count != c->kamek_count ||
        w->pier_boo_count != c->pier_boo_count)
        return 0;
    for (unsigned i = 0; i < w->count; ++i)
    {
        PortableCrab a = w->crabs[i];
        const PortableCrab *b = &c->initial[i];
        if (a.variant != b->variant || a.checkpoint != b->checkpoint ||
            a.placement_order != b->placement_order || (a.flags & ~3u) ||
            !portable_crab_advance(&a, c->sine))
            return 0;
    }
    for (unsigned i = 0; i < w->snowman_count; ++i)
    {
        PortableSnowman a = w->snowmen[i];
        const PortableSnowman *b = &c->initial_snowmen[i];
        if (a.x_q16 != b->x_q16 || a.y_q16 != b->y_q16 || a.height_q16 != b->height_q16 ||
            a.checkpoint != b->checkpoint || a.placement_order != b->placement_order ||
            (a.flags & ~3u) || a.timer < 0 || a.timer > 41 || !portable_snowman_advance(&a))
            return 0;
    }
    for (unsigned i = 0; i < w->penguin_count; ++i)
    {
        PortablePenguin a = w->penguins[i];
        const PortablePenguin *b = &c->initial_penguins[i];
        if (a.x_q16 != b->x_q16 || a.y_q16 != b->y_q16 || a.checkpoint != b->checkpoint ||
            a.placement_order != b->placement_order || (a.flags & ~3u) ||
            (a.affine_y_q8 != 256 && a.affine_y_q8 != -256) ||
            !portable_penguin_advance(&a, c->sine, 1024))
            return 0;
        /* Core intentionally skips finished actors. Validate the complete
         * reachable envelope here, including its final -1 timer sentinel. */
        const PortablePenguin *state = &w->penguins[i];
        if (state->flags == 3u)
        {
            if (state->affine_y_q8 != 256 || state->height_q16 != 0 || state->timer < 0 ||
                state->timer > 41)
                return 0;
        }
        else if (state->flags == 1u)
        {
            if (state->affine_y_q8 != -256 || state->timer < 0 || state->timer > 32 ||
                state->height_q16 < -24 * 65536 || state->height_q16 > 24 * 65536)
                return 0;
        }
        else if (state->flags == 0u)
        {
            if (state->affine_y_q8 != -256 || state->timer != -1 ||
                state->height_q16 != c->sine[0] * 48)
                return 0;
        }
        else
            return 0;
    }
    for (unsigned i = 0; i < w->mouse_count; ++i)
        if (!mouse_valid(&w->mice[i], &c->initial_mice[i], c->sine))
            return 0;
    for (unsigned i = 0; i < w->piranha_count; ++i)
        if (!piranha_valid(&w->piranhas[i], &c->initial_piranhas[i]) ||
            (w->piranha_victim[i] != 255 && w->piranha_victim[i] >= 8))
            return 0;
    for (unsigned i = 0; i < w->seagull_count; ++i)
        if (!seagull_valid(&w->seagulls[i], &c->initial_seagulls[i], c->sine))
            return 0;
    for (unsigned i = 0; i < w->lava_count; ++i)
        if (!lava_valid(&w->lava[i], &c->initial_lava[i], c->sine))
            return 0;
    for (unsigned i = 0; i < w->thwomp_count; ++i)
        if (!thwomp_valid(&w->thwomps[i], &c->initial_thwomps[i]))
            return 0;
    for (unsigned i = 0; i < w->cannonball_count; ++i)
        if (!cannonball_valid(&w->cannonballs[i], &c->initial_cannonballs[i],
                              c->cannonball_variants[i]))
            return 0;
    for (unsigned i = 0; i < w->boo_count; ++i)
        if (!boo_valid(&w->boos[i], &c->initial_boos[i], c->sine))
            return 0;
    for (unsigned i = 0; i < w->rock_count; ++i)
        if (!rock_valid(&w->rocks[i], &c->initial_rocks[i]))
            return 0;
    for (unsigned i = 0; i < w->teepee_count; ++i)
        if (!teepee_valid(&w->teepees[i], &c->initial_teepees[i]) ||
            w->teepee_plane_q16[i] < -268435456 || w->teepee_plane_q16[i] > 268435456 ||
            w->teepee_rider_origin_q16[i][0] < -268435456 ||
            w->teepee_rider_origin_q16[i][0] > 268435456 ||
            w->teepee_rider_origin_q16[i][1] < -268435456 ||
            w->teepee_rider_origin_q16[i][1] > 268435456)
            return 0;
    for (unsigned i = 0; i < w->falling_star_count; ++i)
        if (!falling_star_valid(&w->falling_stars[i], &c->initial_falling_stars[i]))
            return 0;
    if (w->thundercloud_palette_phase[0] > 8 || w->thundercloud_palette_phase[1] > 8 ||
        (!w->thundercloud_count &&
         (w->thundercloud_palette_phase[0] || w->thundercloud_palette_phase[1])))
        return 0;
    for (unsigned i = 0; i < w->thundercloud_count; ++i)
        if (!thundercloud_valid(&w->thunderclouds[i], &c->initial_thunderclouds[i]))
            return 0;
    for (unsigned i = 0; i < w->common_prop_count; ++i)
        if (!common_prop_valid(&w->common_props[i], &c->initial_common_props[i], c->sine))
            return 0;
    for (unsigned i = 0; i < w->mechakoopa_count; ++i)
        if (!mechakoopa_valid(&w->mechakoopas[i], &c->initial_mechakoopas[i], c->sine))
            return 0;
    if (!w->pier_boo_count && (w->pier_boo_visual_rng || w->source_haunt_lock_mask))
        return 0;
    for (unsigned i = 0; i < 16; ++i)
        if (!haunt_valid(c, w, i))
            return 0;
    for (unsigned i = 0; i < w->pier_boo_count; ++i)
        if (!pier_valid(&w->pier_boos[i], &c->initial_pier_boos[i], c->sine))
            return 0;
    for (unsigned i = 0; i < w->kamek_count; ++i)
        if (!kamek_valid(&w->kameks[i], &c->initial_kameks[i], c->sine))
            return 0;
    if (!w->boo_count && (w->boo_pending_mask || w->boo_flash_mask))
        return 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        const PortableMeshStrongHitState *a = &w->strong_hit[i];
        if (!portable_mesh_course_strong_hit_valid(a) ||
            (!w->cannonball_count && !w->falling_star_count && !w->thundercloud_count &&
             !w->mechakoopa_count &&
             (a->rate || a->angle || a->pending || a->active || a->direction)))
            return 0;
    }
    for (unsigned i = 0; i < 8; ++i)
        if (!portable_mesh_course_crush_valid(&c->crush_rules, &w->crush[i]))
            return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (w->lava_burn_ticks[i] > 250 ||
            (!w->lava_count && !w->rock_count && w->lava_burn_ticks[i]))
            return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (!capture_valid(&w->capture[i], c, w->tick))
            return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (w->actor_grace[i] > 8 ||
            (!(w->snowman_count || w->penguin_count || w->mouse_count || w->piranha_count ||
               w->seagull_count || w->lava_count || w->thwomp_count || w->cannonball_count ||
               w->boo_count || w->rock_count || w->teepee_count || w->falling_star_count ||
               w->thundercloud_count || w->common_prop_count || w->mechakoopa_count ||
               (w->kamek_count || w->pier_boo_count)) &&
             w->actor_grace[i]))
            return 0;
    return 1;
}
int portable_mesh_course_actors_init_on_course(const PortableMeshCourseActorsConfig *c,
    const PortableMeshCourse *geometry, PortableMeshCourseActors *out, char *error, size_t cap)
{
    if (!out || !config_valid(c) || (c->ground_follow_version && !geometry))
        return fail(error, cap, c ? c->course_id : 0, "invalid init");
    PortableMeshCourseActors w = {.count = c->count,
                                  .ground_follow_version = c->ground_follow_version,
                                  .snowman_count = c->snowman_count,
                                  .penguin_count = c->penguin_count,
                                  .mouse_count = c->mouse_count,
                                  .piranha_count = c->piranha_count,
                                  .seagull_count = c->seagull_count,
                                  .lava_count = c->lava_count,
                                  .thwomp_count = c->thwomp_count,
                                  .cannonball_count = c->cannonball_count,
                                  .boo_count = c->boo_count,
                                  .rock_count = c->rock_count,
                                  .teepee_count = c->teepee_count,
                                  .falling_star_count = c->falling_star_count,
                                  .thundercloud_count = c->thundercloud_count,
                                  .common_prop_count = c->common_prop_count,
                                  .mechakoopa_count = c->mechakoopa_count,
                                  .kamek_count = c->kamek_count,
                                  .pier_boo_count = c->pier_boo_count};
    memcpy(w.crabs, c->initial, c->count * sizeof *w.crabs);
    memcpy(w.snowmen, c->initial_snowmen, c->snowman_count * sizeof *w.snowmen);
    memcpy(w.penguins, c->initial_penguins, c->penguin_count * sizeof *w.penguins);
    memcpy(w.mice, c->initial_mice, c->mouse_count * sizeof *w.mice);
    memcpy(w.piranhas, c->initial_piranhas, c->piranha_count * sizeof *w.piranhas);
    memset(w.piranha_victim, 255, sizeof w.piranha_victim);
    memcpy(w.seagulls, c->initial_seagulls, c->seagull_count * sizeof *w.seagulls);
    memcpy(w.lava, c->initial_lava, c->lava_count * sizeof *w.lava);
    memcpy(w.thwomps, c->initial_thwomps, c->thwomp_count * sizeof *w.thwomps);
    memcpy(w.cannonballs, c->initial_cannonballs, c->cannonball_count * sizeof *w.cannonballs);
    memcpy(w.boos, c->initial_boos, c->boo_count * sizeof *w.boos);
    memcpy(w.rocks, c->initial_rocks, c->rock_count * sizeof *w.rocks);
    w.pier_boo_visual_rng = c->pier_boo_count ? c->actor_visual_seed : 0;
    /* Constructors run ascending authored placement order, independent of contact lists. */
    for (unsigned order = 0; order < 256; ++order)
        for (unsigned i = 0; i < c->pier_boo_count; ++i)
            if (c->initial_pier_boos[i].placement_order == order)
            {
                w.pier_boos[i] = c->initial_pier_boos[i];
                w.pier_boos[i].pier_boo.phase =
                    (uint16_t)(portable_source_random(&w.pier_boo_visual_rng) & 0xfc00);
            }
    memcpy(w.kameks, c->initial_kameks, c->kamek_count * sizeof *w.kameks);
    memcpy(w.mechakoopas, c->initial_mechakoopas, c->mechakoopa_count * sizeof *w.mechakoopas);
    memcpy(w.common_props, c->initial_common_props, c->common_prop_count * sizeof *w.common_props);
    memcpy(w.thunderclouds, c->initial_thunderclouds,
           c->thundercloud_count * sizeof *w.thunderclouds);
    memcpy(w.falling_stars, c->initial_falling_stars,
           c->falling_star_count * sizeof *w.falling_stars);
    memcpy(w.teepees, c->initial_teepees, c->teepee_count * sizeof *w.teepees);
    for (unsigned i = 0; i < c->teepee_count; ++i)
    {
        w.teepee_plane_q16[i] = c->teepee_anchors_q16[i][2];
        memcpy(w.teepee_rider_origin_q16[i], c->teepee_anchors_q16[i],
               sizeof w.teepee_rider_origin_q16[i]);
    }
    if (!portable_mesh_course_coins_init(&c->coins, &w.coins))
        return fail(error, cap, c->course_id, "invalid coin init");
    if (!portable_mesh_sky_init(&c->sky, &w.sky))
        return fail(error, cap, c->course_id, "invalid sky init");
    if (!ground_follow(c, geometry, &w, error, cap))
        return 0;
    *out = w;
    return 1;
}
int portable_mesh_course_actors_init(const PortableMeshCourseActorsConfig *c,
                                     PortableMeshCourseActors *out, char *error, size_t cap)
{
    return portable_mesh_course_actors_init_on_course(c, NULL, out, error, cap);
}
static int pose_checked(const PortableMeshCourseActorsConfig *c, const PortableMeshCourseActors *w,
                        unsigned i, int32_t out[3])
{
    if (!out || i >= portable_mesh_course_actors_count(w))
        return 0;
    int32_t pos[3];
    int32_t ground_offset = 0;
    int64_t delta[3];
    const int32_t *anchor;
    if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count;
        delta[0] = 0;
        delta[1] = 0;
        delta[2] = w->pier_boos[n].height_q16;
        anchor = c->pier_boo_anchors_q16[n];
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count;
        delta[0] = (int64_t)w->kameks[n].x_q16 - c->initial_kameks[n].x_q16;
        delta[1] = 0;
        delta[2] = w->kameks[n].height_q16;
        anchor = c->kamek_anchors_q16[n];
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count;
        delta[0] = (int64_t)w->mechakoopas[n].x_q16 - c->initial_mechakoopas[n].x_q16;
        delta[1] = 0;
        delta[2] = w->mechakoopas[n].height_q16;
        anchor = c->mechakoopa_anchors_q16[n];
        ground_offset = w->mechakoopa_ground_offset_q16[n];
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count - w->common_prop_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count;
        delta[0] = 0;
        delta[1] = 0;
        delta[2] = w->common_props[n].height_q16;
        anchor = c->common_prop_anchors_q16[n];
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count - w->common_prop_count - w->thundercloud_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count;
        delta[0] = 0;
        delta[1] = 0;
        delta[2] = w->thunderclouds[n].height_q16;
        anchor = c->thundercloud_anchors_q16[n];
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count - w->common_prop_count - w->thundercloud_count -
                      w->falling_star_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count +
                     w->falling_star_count;
        delta[0] = (int64_t)w->falling_stars[n].x_q16 - c->initial_falling_stars[n].x_q16;
        delta[1] = (int64_t)w->falling_stars[n].y_q16 - c->initial_falling_stars[n].y_q16;
        delta[2] = w->falling_stars[n].height_q16;
        anchor = c->falling_star_anchors_q16[n];
    }
    else if (i < w->count)
    {
        delta[0] = (int64_t)w->crabs[i].x_q16 - c->initial[i].x_q16;
        delta[1] = (int64_t)w->crabs[i].y_q16 - c->initial[i].y_q16;
        delta[2] = w->crabs[i].height_q16;
        anchor = c->anchors_q16[i];
        ground_offset = w->crab_ground_offset_q16[i];
    }
    else if (i < w->count + w->snowman_count)
    {
        unsigned n = i - w->count;
        delta[0] = (int64_t)w->snowmen[n].x_q16 - c->initial_snowmen[n].x_q16;
        delta[1] = (int64_t)w->snowmen[n].y_q16 - c->initial_snowmen[n].y_q16;
        delta[2] = w->snowmen[n].height_q16;
        anchor = c->snowman_anchors_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count)
    {
        unsigned n = i - w->count - w->snowman_count;
        delta[0] = (int64_t)w->penguins[n].x_q16 - c->initial_penguins[n].x_q16;
        delta[1] = (int64_t)w->penguins[n].y_q16 - c->initial_penguins[n].y_q16;
        delta[2] = w->penguins[n].height_q16;
        anchor = c->penguin_anchors_q16[n];
        ground_offset = w->penguin_ground_offset_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count;
        delta[0] = (int64_t)w->mice[n].x_q16 - c->initial_mice[n].x_q16;
        delta[1] = (int64_t)w->mice[n].y_q16 - c->initial_mice[n].y_q16;
        delta[2] = w->mice[n].height_q16;
        anchor = c->mouse_anchors_q16[n];
        ground_offset = w->mouse_ground_offset_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count + w->piranha_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count;
        delta[0] = (int64_t)w->piranhas[n].x_q16 - c->initial_piranhas[n].x_q16;
        delta[1] = (int64_t)w->piranhas[n].y_q16 - c->initial_piranhas[n].y_q16;
        delta[2] = w->piranhas[n].height_q16;
        anchor = c->piranha_anchors_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count)
    {
        unsigned n =
            i - w->count - w->snowman_count - w->penguin_count - w->mouse_count - w->piranha_count;
        delta[0] = (int64_t)w->seagulls[n].x_q16 - c->initial_seagulls[n].x_q16;
        delta[1] = (int64_t)w->seagulls[n].y_q16 - c->initial_seagulls[n].y_q16;
        delta[2] = w->seagulls[n].height_q16;
        anchor = c->seagull_anchors_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count;
        delta[0] = (int64_t)w->lava[n].x_q16 - c->initial_lava[n].x_q16;
        delta[1] = (int64_t)w->lava[n].y_q16 - c->initial_lava[n].y_q16;
        delta[2] = w->lava[n].height_q16;
        anchor = c->lava_anchors_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count + w->thwomp_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count - w->lava_count;
        delta[0] = (int64_t)w->thwomps[n].x_q16 - c->initial_thwomps[n].x_q16;
        delta[1] = (int64_t)w->thwomps[n].y_q16 - c->initial_thwomps[n].y_q16;
        delta[2] = w->thwomps[n].height_q16;
        anchor = c->thwomp_anchors_q16[n];
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count + w->thwomp_count +
                     w->cannonball_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count - w->lava_count - w->thwomp_count;
        delta[0] = 0;
        delta[1] = 0;
        delta[2] = w->cannonballs[n].height_q16;
        anchor = c->cannonball_anchors_q16[n];
    }
    else if (i < portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                     w->mechakoopa_count - w->common_prop_count - w->thundercloud_count -
                     w->falling_star_count - w->rock_count - w->teepee_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count - w->lava_count - w->thwomp_count -
                     w->cannonball_count;
        delta[0] = 0;
        delta[1] = 0;
        delta[2] = w->boos[n].height_q16;
        anchor = c->boo_anchors_q16[n];
    }
    else if (i < portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                     w->mechakoopa_count - w->common_prop_count - w->thundercloud_count -
                     w->falling_star_count - w->teepee_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count +
                     w->falling_star_count + w->rock_count + w->teepee_count;
        delta[0] = (int64_t)w->rocks[n].x_q16 - c->initial_rocks[n].x_q16;
        delta[1] = (int64_t)w->rocks[n].y_q16 - c->initial_rocks[n].y_q16;
        delta[2] = w->rocks[n].height_q16;
        anchor = c->rock_anchors_q16[n];
    }
    else
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count +
                     w->falling_star_count + w->teepee_count;
        delta[0] = (int64_t)w->teepees[n].x_q16 - c->initial_teepees[n].x_q16;
        delta[1] = (int64_t)w->teepees[n].y_q16 - c->initial_teepees[n].y_q16;
        delta[2] = w->teepees[n].height_q16;
        anchor = c->teepee_anchors_q16[n];
    }
    for (unsigned a = 0; a < 3; ++a)
    {
        int64_t product = delta[a] * c->scale_q16;
        int64_t rounded = product < 0 ? -((-product + 32768) / 65536) : (product + 32768) / 65536;
        int64_t v = rounded + anchor[a] + (a == 2 ? (int64_t)ground_offset : 0);
        if (v < INT32_MIN || v > INT32_MAX ||
            (w->ground_follow_version &&
             (v < -PORTABLE_FIXED_3D_POSITION_LIMIT || v > PORTABLE_FIXED_3D_POSITION_LIMIT)))
            return 0;
        pos[a] = (int32_t)v;
    }
    memcpy(out, pos, sizeof pos);
    return 1;
}
/* Geometry-only support: source patrols do not acquire kart jump, hazard or
 * fall policy. The explicit opt-in requires connected authored floors. */
static int ground_actor(const PortableMeshCourseActorsConfig *c, const PortableMeshCourse *geometry,
                        PortableMeshCourseActors *w, unsigned family, unsigned index,
                        char *error, size_t cap)
{
    const char *names[4] = {"crab", "penguin", "mouse", "Mechakoopa"};
    const int32_t *anchor;
    int32_t *offset;
    int64_t delta[2];
    int grounded;
    if (family == 0)
    {
        anchor = c->anchors_q16[index];
        offset = &w->crab_ground_offset_q16[index];
        delta[0] = (int64_t)w->crabs[index].x_q16 - c->initial[index].x_q16;
        delta[1] = (int64_t)w->crabs[index].y_q16 - c->initial[index].y_q16;
        grounded = (w->crabs[index].flags & 2) != 0;
    }
    else if (family == 1)
    {
        anchor = c->penguin_anchors_q16[index];
        offset = &w->penguin_ground_offset_q16[index];
        delta[0] = (int64_t)w->penguins[index].x_q16 - c->initial_penguins[index].x_q16;
        delta[1] = (int64_t)w->penguins[index].y_q16 - c->initial_penguins[index].y_q16;
        grounded = (w->penguins[index].flags & 2) != 0;
    }
    else if (family == 2)
    {
        anchor = c->mouse_anchors_q16[index];
        offset = &w->mouse_ground_offset_q16[index];
        delta[0] = (int64_t)w->mice[index].x_q16 - c->initial_mice[index].x_q16;
        delta[1] = (int64_t)w->mice[index].y_q16 - c->initial_mice[index].y_q16;
        grounded = (w->mice[index].flags & 2) &&
                   (w->mice[index].variant != 0x8001 || !w->mice[index].hop_phase);
    }
    else
    {
        anchor = c->mechakoopa_anchors_q16[index];
        offset = &w->mechakoopa_ground_offset_q16[index];
        delta[0] = (int64_t)w->mechakoopas[index].x_q16 - c->initial_mechakoopas[index].x_q16;
        delta[1] = 0;
        grounded = (w->mechakoopas[index].flags & 2) && !w->mechakoopas[index].state;
    }
    if (!grounded)
        return 1; /* Freeze the accepted SI base through source hop/Star flight. */
    const int32_t limit = PORTABLE_FIXED_3D_POSITION_LIMIT;
    int32_t target[2], probe[3] = {anchor[0], anchor[1], 0};
    PortableMeshStatus status = PORTABLE_MESH_INVALID;
    const char *reason = "position domain";
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        int64_t product = delta[axis] * c->scale_q16;
        int64_t value = anchor[axis] +
            (product < 0 ? -((-product + 32768) / 65536) : (product + 32768) / 65536);
        if (value < -limit || value > limit)
            goto invalid;
        target[axis] = (int32_t)value;
    }
    if (anchor[2] > limit - 6)
        goto invalid;
    /* One-metre downward authoring contract, plus two LSB encoding reserve
     * and at most four LSB vertical clearance for radius1/cutoff1/4.
     * The final point-floor check below retains exactly the stated contract. */
    probe[2] = anchor[2] + 6;
    PortableMeshSupportHit support;
    PortableMeshQueryStatus found = portable_mesh_query_support(geometry, probe, 1, 65542,
                                                                 1073741824 / 4, &support);
    reason = "initial support";
    if (found != PORTABLE_MESH_QUERY_HIT)
    {
        status = found == PORTABLE_MESH_QUERY_MISS ? PORTABLE_MESH_OUTSIDE : PORTABLE_MESH_INVALID;
        goto invalid;
    }
    PortableMeshTriangle triangle;
    if (!geometry->triangle(geometry->context, support.triangle, &triangle))
        goto invalid;
    /* Same Q30-normal plane/ceil convention as floor_trace. Subtracting the
     * sphere radius from its center would lose slope-dependent clearance. */
    int64_t plane = -((int64_t)anchor[0] - triangle.vertices_q16[0][0]) * support.normal_q30[0] -
                    ((int64_t)anchor[1] - triangle.vertices_q16[0][1]) * support.normal_q30[1];
    int64_t height = plane / support.normal_q30[2];
    if (plane % support.normal_q30[2] > 0)
        ++height;
    height += triangle.vertices_q16[0][2];
    reason = "initial anchor clearance (0..1m, 2 LSB reserve)";
    if (height < -limit || height > limit || height - anchor[2] > 2 ||
        (int64_t)anchor[2] - height > 65536)
        goto invalid;
    PortableMeshFloorWitness start = {.triangle = support.triangle,
                                      .point_q16 = {anchor[0], anchor[1], (int32_t)height}};
    PortableMeshFloorTraceHit hit;
    status = portable_mesh_query_floor_trace(geometry, &start, target, 1073741824 / 4, &hit);
    reason = "connected floor trace";
    if (status != PORTABLE_MESH_OK)
        goto invalid;
    int64_t value = (int64_t)hit.witness.point_q16[2] - height;
    if (value < -536870912 || value > 536870912)
    {
        status = PORTABLE_MESH_INVALID;
        goto invalid;
    }
    *offset = (int32_t)value;
    return 1;
invalid:
    if (error && cap)
        snprintf(error, cap, "3D course=%u source actors: %s[%u] ground-follow %s (status=%d)",
                 c->course_id, names[family], index, reason, (int)status);
    return 0;
}
static int ground_follow(const PortableMeshCourseActorsConfig *c, const PortableMeshCourse *geometry,
                         PortableMeshCourseActors *w, char *error, size_t cap)
{
    if (!c->ground_follow_version)
        return 1;
    if (!geometry)
        return fail(error, cap, c->course_id, "ground-follow requires geometry");
    const uint32_t counts[4] = {w->count, w->penguin_count, w->mouse_count, w->mechakoopa_count};
    for (unsigned family = 0; family < 4; ++family)
        for (unsigned i = 0; i < counts[family]; ++i)
            if (!ground_actor(c, geometry, w, family, i, error, cap))
                return 0;
    return 1;
}
int portable_mesh_course_actors_pose(const PortableMeshCourseActorsConfig *c,
                                     const PortableMeshCourseActors *w, unsigned i, int32_t out[3])
{
    if (!world_valid(c, w))
        return 0;
    return pose_checked(c, w, i, out);
}
int portable_mesh_course_actors_view(const PortableMeshCourseActorsConfig *c,
                                     const PortableMeshCourseActors *w, unsigned i,
                                     PortableMeshCourseActorView *out)
{
    if (!out || !world_valid(c, w) || i >= portable_mesh_course_actors_count(w))
        return 0;
    PortableMeshCourseActorView v = {.affine_x_q8 = 256, .affine_y_q8 = 256};
    if (!pose_checked(c, w, i, v.position_q16))
        return 0;
    if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count;
        const PortableCourseProp *a = &w->pier_boos[n];
        v.object_index = c->pier_boo_object_indices[n];
        v.model_id = c->pier_boo_frame_ids[n][0];
        v.visible = (a->flags & 1) != 0;
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = (uint16_t)a->affine_x_q8;
        v.affine_y_q8 = (uint16_t)(a->affine_y_q8 < 0 ? -a->affine_y_q8 : a->affine_y_q8);
        v.flip_y = a->affine_y_q8 < 0;
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count;
        const PortableCourseProp *a = &w->kameks[n];
        v.object_index = c->kamek_object_indices[n];
        v.model_id = c->kamek_frame_ids[n][0];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = (uint16_t)a->affine_x_q8;
        v.affine_y_q8 = (uint16_t)(a->affine_y_q8 < 0 ? -a->affine_y_q8 : a->affine_y_q8);
        v.flip_y = (uint8_t)(a->affine_y_q8 < 0);
        if (a->kamek.has_facing)
        {
            v.has_view_art = 1;
            v.view_heading = a->kamek.heading;
            v.model_id = c->kamek_frame_ids[n][1];
            v.alternate_model_id = c->kamek_frame_ids[n][0];
        }
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count;
        const PortableCourseProp *a = &w->mechakoopas[n];
        v.object_index = c->mechakoopa_object_indices[n];
        v.model_id = c->mechakoopa_frame_ids[n][0];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = (uint16_t)a->affine_x_q8;
        v.affine_y_q8 = (uint16_t)(a->affine_y_q8 < 0 ? -a->affine_y_q8 : a->affine_y_q8);
        v.flip_y = (uint8_t)(a->affine_y_q8 < 0);
        if (a->mechakoopa.has_facing)
        {
            const unsigned phase = a->mechakoopa.animation_phase;
            v.has_view_art = 1;
            v.view_heading = a->mechakoopa.heading;
            v.model_id = c->mechakoopa_frame_ids[n][phase ? 2 : 3];
            v.alternate_model_id = c->mechakoopa_frame_ids[n][phase ? 0 : 1];
        }
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count - w->common_prop_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count;
        const PortableCourseProp *a = &w->common_props[n];
        v.object_index = c->common_prop_object_indices[n];
        v.model_id = c->common_prop_frame_ids[n][a->contact_phase];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)(v.visible && !a->state);
        v.affine_x_q8 = (uint16_t)a->affine_x_q8;
        v.affine_y_q8 = (uint16_t)(a->affine_y_q8 < 0 ? -a->affine_y_q8 : a->affine_y_q8);
        v.flip_y = (uint8_t)(a->affine_y_q8 < 0);
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count - w->common_prop_count - w->thundercloud_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count;
        const PortableThundercloud *a = &w->thunderclouds[n];
        v.object_index = c->thundercloud_object_indices[n];
        v.model_id =
            c->thundercloud_frame_ids[n]
                                     [w->thundercloud_palette_phase[a->variant / 2] * 8 + a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
    }
    else if (i >= portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                      w->mechakoopa_count - w->common_prop_count - w->thundercloud_count -
                      w->falling_star_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count +
                     w->falling_star_count;
        const PortableFallingStar *a = &w->falling_stars[n];
        v.object_index = c->falling_star_object_indices[n];
        v.model_id = c->falling_star_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = v.affine_y_q8 = a->affine_q8;
        v.rotation_angle = a->angle;
    }
    else if (i < w->count)
    {
        const PortableCrab *a = &w->crabs[i];
        v.object_index = c->object_indices[i];
        v.model_id = c->frame_ids[i][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.flip_y = a->flip_y;
    }
    else if (i < w->count + w->snowman_count)
    {
        unsigned n = i - w->count;
        const PortableSnowman *a = &w->snowmen[n];
        v.object_index = c->snowman_object_indices[n];
        v.model_id = c->snowman_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
    }
    else if (i < w->count + w->snowman_count + w->penguin_count)
    {
        unsigned n = i - w->count - w->snowman_count;
        const PortablePenguin *a = &w->penguins[n];
        v.object_index = c->penguin_object_indices[n];
        v.model_id = c->penguin_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.flip_y = (uint8_t)(a->affine_y_q8 < 0);
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count;
        const PortableMouse *a = &w->mice[n];
        v.object_index = c->mouse_object_indices[n];
        v.model_id = c->mouse_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.flip_y = (uint8_t)(a->affine_y_q8 < 0);
        if (a->variant != 0x8001)
        {
            v.has_facing = 1;
            v.facing_x_q16 = a->facing_x_q16;
            v.facing_y_q16 = a->facing_y_q16;
            v.turn_flip = a->turn_flip;
        }
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count + w->piranha_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count;
        const PortablePiranha *a = &w->piranhas[n];
        v.object_index = c->piranha_object_indices[n];
        v.model_id = c->piranha_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.flip_y = a->flip_y;
        v.affine_x_q8 = a->affine_x_q8;
        v.affine_y_q8 = a->affine_y_q8;
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count)
    {
        unsigned n =
            i - w->count - w->snowman_count - w->penguin_count - w->mouse_count - w->piranha_count;
        const PortableSeagull *a = &w->seagulls[n];
        v.object_index = c->seagull_object_indices[n];
        v.model_id = c->seagull_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.flip_y = a->flip_y;
        v.affine_x_q8 = a->affine_x_q8;
        v.affine_y_q8 = a->affine_y_q8;
        if (a->affine_x_q8 == 256)
        {
            v.has_facing = 1;
            v.facing_x_q16 = 65536;
            v.turn_flip = 1;
            v.facing_half_turn = 1;
        }
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count;
        v.object_index = c->lava_object_indices[n];
        v.model_id = c->lava_frame_ids[n][w->lava[n].art];
        v.visible = (uint8_t)((w->lava[n].flags & 1) != 0);
        v.collidable = (uint8_t)((w->lava[n].flags & 2) != 0);
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count + w->thwomp_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count - w->lava_count;
        v.object_index = c->thwomp_object_indices[n];
        v.model_id = c->thwomp_frame_ids[n][0];
        v.visible = (uint8_t)((w->thwomps[n].flags & 1) != 0);
        v.collidable = (uint8_t)((w->thwomps[n].flags & 2) != 0);
    }
    else if (i < w->count + w->snowman_count + w->penguin_count + w->mouse_count +
                     w->piranha_count + w->seagull_count + w->lava_count + w->thwomp_count +
                     w->cannonball_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count - w->lava_count - w->thwomp_count;
        const PortableCannonball *a = &w->cannonballs[n];
        v.object_index = c->cannonball_object_indices[n];
        v.model_id = c->cannonball_frame_ids[n][a->art];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = a->affine_x_q8;
        v.affine_y_q8 = a->affine_y_q8;
    }
    else if (i < portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                     w->mechakoopa_count - w->common_prop_count - w->thundercloud_count -
                     w->falling_star_count - w->rock_count - w->teepee_count)
    {
        unsigned n = i - w->count - w->snowman_count - w->penguin_count - w->mouse_count -
                     w->piranha_count - w->seagull_count - w->lava_count - w->thwomp_count -
                     w->cannonball_count;
        const PortableBoo *a = &w->boos[n];
        v.object_index = c->boo_object_indices[n];
        v.model_id = c->boo_frame_ids[n][0];
        v.alternate_model_id = c->boo_frame_ids[n][1];
        v.view_heading = a->heading;
        v.has_view_art = 1;
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.flip_y = (uint8_t)(a->affine_y_q8 < 0);
    }
    else if (i < portable_mesh_course_actors_count(w) - w->pier_boo_count - w->kamek_count -
                     w->mechakoopa_count - w->common_prop_count - w->thundercloud_count -
                     w->falling_star_count - w->teepee_count)
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count +
                     w->falling_star_count + w->rock_count + w->teepee_count;
        const PortableCourseProp *a = &w->rocks[n];
        v.object_index = c->rock_object_indices[n];
        v.model_id = c->rock_frame_ids[n][a->lake_rock.frame];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = (uint16_t)a->affine_x_q8;
        v.affine_y_q8 = (uint16_t)a->affine_y_q8;
    }
    else
    {
        unsigned n = i - portable_mesh_course_actors_count(w) + w->pier_boo_count + w->kamek_count +
                     w->mechakoopa_count + w->common_prop_count + w->thundercloud_count +
                     w->falling_star_count + w->teepee_count;
        const PortableCourseProp *a = &w->teepees[n];
        v.object_index = c->teepee_object_indices[n];
        v.model_id = c->teepee_frame_ids[n][a->teepee.frame];
        v.visible = (uint8_t)((a->flags & 1) != 0);
        v.collidable = (uint8_t)((a->flags & 2) != 0);
        v.affine_x_q8 = (uint16_t)a->affine_x_q8;
        v.affine_y_q8 = (uint16_t)a->affine_y_q8;
    }
    *out = v;
    return 1;
}
static int scaled_coordinate(int32_t source, int32_t origin, int32_t scale, int32_t anchor,
                             int32_t *out)
{
    const int64_t product = ((int64_t)source - origin) * scale;
    const int64_t v =
        (product < 0 ? -((-product + 32768) / 65536) : (product + 32768) / 65536) + anchor;
    if (v < INT32_MIN || v > INT32_MAX)
        return 0;
    *out = (int32_t)v;
    return 1;
}
int portable_mesh_course_teepee_rider_view(const PortableMeshCourseActorsConfig *c,
                                           const PortableMeshCourseActors *w, unsigned i,
                                           uint16_t camera, PortableMeshCourseActorView *out)
{
    if (!out || !world_valid(c, w) || i >= c->teepee_count)
        return 0;
    const PortableCourseProp *a = &w->teepees[i];
    PortableMeshCourseActorView v = {.object_index = c->teepee_object_indices[i],
                                     .affine_x_q8 = 256,
                                     .affine_y_q8 = 256,
                                     .visible = (uint8_t)(a->teepee.rider_state != 0)};
    unsigned frame = portable_teepee_rider_art(a, camera);
    if (frame < 4 || frame > 10 ||
        !scaled_coordinate(a->teepee.rider_x_q16, 0, c->scale_q16, w->teepee_rider_origin_q16[i][0],
                           &v.position_q16[0]) ||
        !scaled_coordinate(a->teepee.rider_y_q16, 0, c->scale_q16, w->teepee_rider_origin_q16[i][1],
                           &v.position_q16[1]) ||
        !scaled_coordinate(a->teepee.rider_z_q16, 0, c->scale_q16, w->teepee_plane_q16[i],
                           &v.position_q16[2]))
        return 0;
    v.model_id = c->teepee_frame_ids[i][frame];
    *out = v;
    return 1;
}
static int haunt_pose(const PortableMeshCourseActorsConfig *c, const PortableMeshCourseActors *w,
                      unsigned slot, int32_t out[3])
{
    const PortableBooAttack *a = &w->source_haunts[slot];
    const int32_t delta[3] = {a->x_q16, a->y_q16, a->z_q16};
    for (unsigned j = 0; j < 3; ++j)
    {
        int64_t product = (int64_t)delta[j] * c->scale_q16;
        int64_t scaled = product < 0 ? -((-product + 32768) / 65536) : (product + 32768) / 65536;
        int64_t value = scaled + w->source_haunt_origins_q16[slot][j];
        if (value < -268435456 || value > 268435456)
            return 0;
        out[j] = (int32_t)value;
    }
    return 1;
}
int portable_mesh_course_actors_haunt_view(const PortableMeshCourseActorsConfig *c,
                                           const PortableMeshCourseActors *w, unsigned slot,
                                           PortableMeshCourseActorView *out)
{
    if (!out || slot >= 16 || !world_valid(c, w))
        return 0;
    PortableMeshCourseActorView view = {0};
    const PortableBooAttack *a = &w->source_haunts[slot];
    if (a->occupied)
    {
        const unsigned source = w->source_haunt_source_indices[slot];
        view.object_index = c->pier_boo_object_indices[source];
        view.model_id = c->pier_boo_frame_ids[source][1 + a->frame];
        view.visible = a->visible;
        view.affine_x_q8 = 256;
        view.affine_y_q8 = (uint16_t)(a->affine_y_q8 < 0 ? -a->affine_y_q8 : a->affine_y_q8);
        view.flip_y = a->affine_y_q8 < 0;
        if (!haunt_pose(c, w, slot, view.position_q16))
            return 0;
    }
    *out = view;
    return 1;
}
int portable_mesh_course_actors_prepare_drive(const PortableMeshCourseActorsConfig *c,
                                              PortableMeshCourseActors *w, PortableMeshItems *items,
                                              const PortableMeshKartInput *inputs,
                                              const PortableMeshRacerState *racers, size_t count,
                                              PortableMeshCourseActorEvents *events, char *error,
                                              size_t cap)
{
    if (!world_valid(c, w) || !items || !inputs || !racers || !events || !count || count > 8 ||
        items->status.count != count || items->inventory.count != count || items->prepared)
        return fail(error, cap, c ? c->course_id : 0, "invalid source rider preturn");
    for (unsigned i = 0; i < count; ++i)
        if (inputs[i].buttons > 15 || racers[i].phase > PORTABLE_MESH_RACER_FINISHED ||
            items->status.racers[i].boo_lock_ticks > 32767)
            return fail(error, cap, c->course_id, "invalid source rider clock/input");
    if (!c->teepee_count && !c->pier_boo_count)
        return 1;
    PortableMeshItems next = *items;
    PortableMeshCourseActors actors = *w;
    PortableMeshCourseActorEvents e = *events;
    e.teepee_clock_mask = 0;
    e.source_haunt_clock_mask = 0;
    memset(e.teepee_lock_clock, 0, sizeof e.teepee_lock_clock);
    memset(e.source_haunt_lock_clock, 0, sizeof e.source_haunt_lock_clock);
    for (unsigned i = 0; i < count; ++i)
    {
        const uint8_t bit = (uint8_t)(1u << i);
        uint16_t *lock = &next.status.racers[i].boo_lock_ticks;
        const int rescue = (inputs[i].buttons & PORTABLE_MESH_RESCUE) &&
                           !(racers[i].kart.previous_buttons & PORTABLE_MESH_RESCUE);
        const int source_owned = (actors.source_haunt_lock_mask & bit) != 0;
        if (!*lock || next.status.racers[i].star_ticks || rescue || racers[i].kart.respawn_ticks ||
            racers[i].respawn_grace)
            actors.source_haunt_lock_mask &= (uint8_t)~bit;
        if (!c->teepee_count && !source_owned)
            continue;
        if ((actors.strong_hit[i].active &&
             (c->teepee_count || (items->status.human_mask & bit))) ||
            rescue || racers[i].kart.respawn_ticks || racers[i].respawn_grace ||
            next.status.racers[i].star_ticks)
        {
            *lock = 0;
            actors.source_haunt_lock_mask &= (uint8_t)~bit;
        }
        else if ((c->teepee_count || (items->status.human_mask & bit)) &&
                 (inputs[i].buttons & PORTABLE_MESH_ACCELERATE) &&
                 !(racers[i].kart.previous_buttons & PORTABLE_MESH_ACCELERATE))
            *lock = *lock > 10 ? (uint16_t)(*lock - 10) : 0;
        if (!*lock)
            actors.source_haunt_lock_mask &= (uint8_t)~bit;
        if (racers[i].phase == PORTABLE_MESH_RACER_RACING)
        {
            if (c->teepee_count)
            {
                e.teepee_clock_mask |= bit;
                e.teepee_lock_clock[i] = *lock;
            }
            if (source_owned)
            {
                e.source_haunt_clock_mask |= bit;
                e.source_haunt_lock_clock[i] = *lock;
            }
        }
    }
    *w = actors;
    *items = next;
    *events = e;
    return 1;
}
int portable_mesh_course_actors_advance_events_on_course(const PortableMeshCourseActorsConfig *c,
                                               const PortableMeshCourse *geometry,
                                               PortableMeshCourseActors *w,
                                               const PortableMeshRacerState *racers, size_t count,
                                               PortableMeshCourseActorEvents *events, char *error,
                                               size_t cap)
{
    if (!world_valid(c, w) || (c->ground_follow_version && !geometry) || w->tick == UINT32_MAX || count > 8 || (count && !racers) ||
        (!count && racers))
        return fail(error, cap, c ? c->course_id : 0, "invalid world/tick");
    PortableMeshCourseActors n = *w;
    for (unsigned i = 0; i < n.pier_boo_count; ++i)
    {
        PortableCourseProps singleton = {0};
        singleton.count = 1;
        singleton.actors[0] = n.pier_boos[i];
        int32_t p[3];
        if (!portable_course_props_advance(&singleton, c->sine, 1024) ||
            !pier_valid(&singleton.actors[0], &c->initial_pier_boos[i], c->sine))
            return fail(error, cap, c->course_id, "invalid Pier Boo advance");
        n.pier_boos[i] = singleton.actors[0];
        if (!pose_checked(c, &n, portable_mesh_course_actors_count(&n) - n.pier_boo_count + i, p))
            return fail(error, cap, c->course_id, "invalid Pier Boo pose");
    }
    for (unsigned i = 0; i < n.kamek_count; ++i)
    {
        PortableCourseProps singleton;
        singleton.actors[0] = n.kameks[i];
        singleton.count = 1;
        singleton.initial = NULL;
        int32_t p[3];
        if (!portable_course_props_advance(&singleton, c->sine, 1024) ||
            !kamek_valid(&singleton.actors[0], &c->initial_kameks[i], c->sine))
            return fail(error, cap, c->course_id, "invalid Kamek advance");
        n.kameks[i] = singleton.actors[0];
        if (!pose_checked(
                c, &n, portable_mesh_course_actors_count(&n) - n.pier_boo_count - n.kamek_count + i,
                p))
            return fail(error, cap, c->course_id, "invalid Kamek pose");
    }
    for (unsigned i = 0; i < n.mechakoopa_count; ++i)
    {
        PortableCourseProps singleton;
        singleton.actors[0] = n.mechakoopas[i];
        singleton.count = 1;
        singleton.initial = NULL;
        int32_t p[3];
        if (!portable_course_props_advance(&singleton, c->sine, 1024) ||
            !mechakoopa_valid(&singleton.actors[0], &c->initial_mechakoopas[i], c->sine))
            return fail(error, cap, c->course_id, "invalid Mechakoopa advance");
        n.mechakoopas[i] = singleton.actors[0];
        if (!pose_checked(c, &n,
                          portable_mesh_course_actors_count(&n) - n.pier_boo_count - n.kamek_count -
                              n.mechakoopa_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid Mechakoopa pose");
    }
    for (unsigned i = 0; i < n.common_prop_count; ++i)
    {
        PortableCourseProps singleton;
        singleton.actors[0] = n.common_props[i];
        singleton.count = 1;
        singleton.initial = NULL;
        int32_t p[3];
        if (!portable_course_props_advance(&singleton, c->sine, 1024) ||
            !common_prop_valid(&singleton.actors[0], &c->initial_common_props[i], c->sine))
            return fail(error, cap, c->course_id, "invalid common prop advance");
        n.common_props[i] = singleton.actors[0];
        if (!pose_checked(c, &n,
                          portable_mesh_course_actors_count(&n) - n.pier_boo_count - n.kamek_count -
                              n.mechakoopa_count - n.common_prop_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid common prop pose");
    }
    /* Shared bank palette writes follow the source reversed actor list. */
    unsigned cloud_order[64];
    for (unsigned i = 0; i < n.thundercloud_count; ++i)
    {
        cloud_order[i] = i;
        for (unsigned j = i; j && n.thunderclouds[cloud_order[j - 1]].placement_order <
                                      n.thunderclouds[cloud_order[j]].placement_order;
             --j)
        {
            unsigned swap = cloud_order[j];
            cloud_order[j] = cloud_order[j - 1];
            cloud_order[j - 1] = swap;
        }
    }
    for (unsigned order = 0; order < n.thundercloud_count; ++order)
    {
        const unsigned i = cloud_order[order];
        int32_t p[3];
        if (!portable_thundercloud_advance(&n.thunderclouds[i], n.thundercloud_palette_phase) ||
            !thundercloud_valid(&n.thunderclouds[i], &c->initial_thunderclouds[i]) ||
            !pose_checked(c, &n,
                          portable_mesh_course_actors_count(&n) - n.pier_boo_count - n.kamek_count -
                              n.mechakoopa_count - n.common_prop_count - n.thundercloud_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid thundercloud advance");
    }
    for (unsigned i = 0; i < n.falling_star_count; ++i)
    {
        int32_t p[3];
        if (!portable_falling_star_advance(&n.falling_stars[i]) ||
            !falling_star_valid(&n.falling_stars[i], &c->initial_falling_stars[i]) ||
            !pose_checked(c, &n,
                          portable_mesh_course_actors_count(&n) - n.pier_boo_count - n.kamek_count -
                              n.mechakoopa_count - n.common_prop_count - n.thundercloud_count -
                              n.falling_star_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid falling star advance");
    }
    n.boo_flash_mask = 0;
    if (!portable_mesh_course_coins_advance(&c->coins, &n.coins))
        return fail(error, cap, c->course_id, "invalid coin advance");
    PortableMeshCourseActorEvents emitted = {0};
    for (unsigned i = 0; i < n.count; ++i)
    {
        int32_t pos[3];
        if (!portable_crab_advance(&n.crabs[i], c->sine) || !pose_checked(c, &n, i, pos))
            return fail(error, cap, c->course_id, "invalid crab advance");
    }
    for (unsigned i = 0; i < n.snowman_count; ++i)
        if (!portable_snowman_advance(&n.snowmen[i]))
            return fail(error, cap, c->course_id, "invalid snowman advance");
    for (unsigned i = 0; i < n.penguin_count; ++i)
    {
        int32_t p[3];
        if (!portable_penguin_advance(&n.penguins[i], c->sine, 1024) ||
            !pose_checked(c, &n, n.count + n.snowman_count + i, p))
            return fail(error, cap, c->course_id, "invalid penguin advance");
    }
    for (unsigned i = 0; i < n.mouse_count; ++i)
    {
        int32_t p[3];
        if (!portable_mouse_advance(&n.mice[i], c->sine, 1024) ||
            !mouse_valid(&n.mice[i], &c->initial_mice[i], c->sine) ||
            !pose_checked(c, &n, n.count + n.snowman_count + n.penguin_count + i, p))
            return fail(error, cap, c->course_id, "invalid mouse advance");
    }
    for (unsigned i = 0; i < n.piranha_count; ++i)
    {
        uint16_t song;
        const unsigned victim = n.piranha_victim[i];
        if (count && victim != 255 && victim >= count)
            return fail(error, cap, c->course_id, "piranha victim exceeds racer count");
        const uint32_t flags =
            racers && victim < count && racers[victim].phase == PORTABLE_MESH_RACER_FINISHED ? 0x20u
                                                                                             : 0u;
        if (!portable_piranha_advance(&n.piranhas[i], flags, &song) ||
            !piranha_valid(&n.piranhas[i], &c->initial_piranhas[i]))
            return fail(error, cap, c->course_id, "invalid piranha advance");
        if (song == 378 && racers && victim < count)
            emitted.piranha_bite_mask |= (uint8_t)(1u << victim);
    }
    for (unsigned i = 0; i < n.seagull_count; ++i)
    {
        int32_t p[3];
        if (!portable_seagull_advance(&n.seagulls[i], c->sine) ||
            !seagull_valid(&n.seagulls[i], &c->initial_seagulls[i], c->sine) ||
            !pose_checked(c, &n,
                          n.count + n.snowman_count + n.penguin_count + n.mouse_count +
                              n.piranha_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid seagull advance");
    }
    for (unsigned i = 0; i < n.lava_count; ++i)
    {
        int32_t p[3];
        if (!portable_lava_bubble_advance(&n.lava[i], c->sine) ||
            !lava_valid(&n.lava[i], &c->initial_lava[i], c->sine) ||
            !pose_checked(c, &n,
                          n.count + n.snowman_count + n.penguin_count + n.mouse_count +
                              n.piranha_count + n.seagull_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid lava advance");
    }
    for (unsigned i = 0; i < n.thwomp_count; ++i)
    {
        int landed = 0;
        int32_t p[3];
        if (!portable_thwomp_advance(&n.thwomps[i], &landed) ||
            !thwomp_valid(&n.thwomps[i], &c->initial_thwomps[i]) ||
            !pose_checked(c, &n,
                          n.count + n.snowman_count + n.penguin_count + n.mouse_count +
                              n.piranha_count + n.seagull_count + n.lava_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid thwomp advance");
        if (landed)
            emitted.thwomp_landed |= UINT64_C(1) << i;
    }
    for (unsigned i = 0; i < n.cannonball_count; ++i)
    {
        const PortableCannonball *before = &n.cannonballs[i];
        const int state = before->state + (before->timer == 0);
        if (state == 2)
            emitted.cannonball_launched |= UINT64_C(1) << i;
        if (state == 4)
            emitted.cannonball_exploded |= UINT64_C(1) << i;
        int32_t p[3];
        if (!portable_cannonball_advance(&n.cannonballs[i]) ||
            !cannonball_valid(&n.cannonballs[i], &c->initial_cannonballs[i],
                              c->cannonball_variants[i]) ||
            !pose_checked(c, &n,
                          n.count + n.snowman_count + n.penguin_count + n.mouse_count +
                              n.piranha_count + n.seagull_count + n.lava_count + n.thwomp_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid cannonball advance");
    }
    for (unsigned i = 0; i < n.boo_count; ++i)
    {
        int32_t p[3];
        if (!portable_boo_advance(&n.boos[i], c->sine, 1024) ||
            !boo_valid(&n.boos[i], &c->initial_boos[i], c->sine) ||
            !pose_checked(c, &n,
                          n.count + n.snowman_count + n.penguin_count + n.mouse_count +
                              n.piranha_count + n.seagull_count + n.lava_count + n.thwomp_count +
                              n.cannonball_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid boo advance");
    }
    for (unsigned i = 0; i < n.rock_count; ++i)
    {
        const unsigned state = n.rocks[i].state;
        PortableCourseProps singleton;
        singleton.actors[0] = n.rocks[i];
        singleton.count = 1;
        singleton.initial = NULL;
        int32_t p[3];
        if (!portable_course_props_advance(&singleton, c->sine, 1024) ||
            !rock_valid(&singleton.actors[0], &c->initial_rocks[i]))
            return fail(error, cap, c->course_id, "invalid rock advance");
        n.rocks[i] = singleton.actors[0];
        if (!pose_checked(c, &n,
                          portable_mesh_course_actors_count(&n) - n.pier_boo_count - n.kamek_count -
                              n.mechakoopa_count - n.common_prop_count - n.thundercloud_count -
                              n.falling_star_count - n.rock_count - n.teepee_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid rock pose");
        if (state == 2 || state == 4 || state == 6 || state == 8)
            emitted.rock_cue_masks[(state - 2) / 2] |= UINT64_C(1) << i;
    }
    for (unsigned i = 0; i < 8; ++i)
        if (n.lava_burn_ticks[i])
            --n.lava_burn_ticks[i];
    for (unsigned i = 0; i < 8; ++i)
        if (n.actor_grace[i])
            --n.actor_grace[i];
    if (!ground_follow(c, geometry, &n, error, cap))
        return 0;
    ++n.tick;
    *w = n;
    if (events)
        *events = emitted;
    return 1;
}
int portable_mesh_course_actors_advance_events(const PortableMeshCourseActorsConfig *c,
    PortableMeshCourseActors *w, const PortableMeshRacerState *racers, size_t count,
    PortableMeshCourseActorEvents *events, char *error, size_t cap)
{
    return portable_mesh_course_actors_advance_events_on_course(c, NULL, w, racers, count,
                                                                events, error, cap);
}
int portable_mesh_course_actors_advance(const PortableMeshCourseActorsConfig *c,
                                        PortableMeshCourseActors *w, char *error, size_t cap)
{
    return portable_mesh_course_actors_advance_events(c, w, NULL, 0, NULL, error, cap);
}
static void hash_word(uint64_t *h, uint32_t v, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
    {
        *h ^= (uint8_t)(v >> (8 * i));
        *h *= UINT64_C(1099511628211);
    }
}
uint64_t portable_mesh_course_actors_checksum(const PortableMeshCourseActors *w)
{
    if (!w || w->count > 64 || w->snowman_count > 64 || w->penguin_count > 64 ||
        w->mouse_count > 64 || w->piranha_count > 64 || w->seagull_count > 64 ||
        w->lava_count > 64 || w->thwomp_count > 64 || w->cannonball_count > 64 ||
        w->boo_count > 64 || w->rock_count > 64 || w->teepee_count > 64 ||
        w->falling_star_count > 64 || w->thundercloud_count > 64 || w->common_prop_count > 64 ||
        w->mechakoopa_count > 64 || w->kamek_count > 64 || w->pier_boo_count > 64)
        return 0;
    if (!ground_state_valid(w))
        return 0;
    uint64_t h = UINT64_C(14695981039346656037);
    hash_word(&h, UINT32_C(0x3153414d), 4);
    hash_word(&h, w->count, 4);
    hash_word(&h, w->tick, 4);
    for (unsigned i = 0; i < w->count; ++i)
    {
        const PortableCrab *a = &w->crabs[i];
        hash_word(&h, (uint32_t)a->x_q16, 4);
        hash_word(&h, (uint32_t)a->y_q16, 4);
        hash_word(&h, (uint32_t)a->height_q16, 4);
        hash_word(&h, (uint32_t)a->velocity_x_q16, 4);
        hash_word(&h, (uint32_t)a->velocity_y_q16, 4);
        hash_word(&h, (uint32_t)a->timer, 4);
        hash_word(&h, a->variant, 2);
        hash_word(&h, a->checkpoint, 1);
        hash_word(&h, a->placement_order, 1);
        hash_word(&h, a->flags, 1);
        hash_word(&h, a->art, 1);
        hash_word(&h, a->animation_frame, 1);
        hash_word(&h, a->animation_timer, 1);
        hash_word(&h, a->flip_y, 1);
    }
    if (w->snowman_count)
    {
        hash_word(&h, UINT32_C(0x314d4e53), 4);
        hash_word(&h, w->snowman_count, 4);
        for (unsigned i = 0; i < w->snowman_count; ++i)
        {
            const PortableSnowman *a = &w->snowmen[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->state, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->penguin_count)
    {
        hash_word(&h, UINT32_C(0x31474e50), 4);
        hash_word(&h, w->penguin_count, 4);
        for (unsigned i = 0; i < w->penguin_count; ++i)
        {
            const PortablePenguin *a = &w->penguins[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->mouse_count)
    {
        hash_word(&h, UINT32_C(0x3153554d), 4);
        hash_word(&h, w->mouse_count, 4);
        for (unsigned i = 0; i < w->mouse_count; ++i)
        {
            const PortableMouse *a = &w->mice[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_x_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_y_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint32_t)a->facing_x_q16, 4);
            hash_word(&h, (uint32_t)a->facing_y_q16, 4);
            hash_word(&h, (uint16_t)a->timer, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->variant, 2);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
            hash_word(&h, a->walk_phase, 1);
            hash_word(&h, a->walk_ticks, 1);
            hash_word(&h, a->hop_phase, 1);
            hash_word(&h, a->turn_flip, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->piranha_count)
    {
        hash_word(&h, UINT32_C(0x31524950), 4);
        hash_word(&h, w->piranha_count, 4);
        for (unsigned i = 0; i < w->piranha_count; ++i)
        {
            const PortablePiranha *a = &w->piranhas[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint32_t)a->state, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint32_t)a->phase, 4);
            hash_word(&h, a->affine_x_q8, 2);
            hash_word(&h, a->affine_y_q8, 2);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
            hash_word(&h, a->flip_y, 1);
            hash_word(&h, a->struck_player, 1);
            hash_word(&h, w->piranha_victim[i], 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 8; ++i)
        {
            const PortableMeshCaptureState *a = &w->capture[i];
            hash_word(&h, a->remaining, 4);
            hash_word(&h, a->actor_index_plus1, 4);
            hash_word(&h, a->started_tick, 4);
            hash_word(&h, a->spin, 4);
            for (unsigned j = 0; j < 3; ++j)
                hash_word(&h, (uint32_t)a->pull_q16[j], 4);
            for (unsigned j = 0; j < 3; ++j)
                hash_word(&h, (uint32_t)a->forward_q30[j], 4);
            for (unsigned j = 0; j < 3; ++j)
                hash_word(&h, (uint32_t)a->up_q30[j], 4);
        }
    }
    if (w->seagull_count)
    {
        hash_word(&h, UINT32_C(0x314c4753), 4);
        hash_word(&h, w->seagull_count, 4);
        for (unsigned i = 0; i < w->seagull_count; ++i)
        {
            const PortableSeagull *a = &w->seagulls[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint32_t)a->state, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint32_t)a->phase, 4);
            hash_word(&h, a->affine_x_q8, 2);
            hash_word(&h, a->affine_y_q8, 2);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
            hash_word(&h, a->flip_y, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->lava_count)
    {
        hash_word(&h, UINT32_C(0x3142564c), 4); /* LVB1 */
        hash_word(&h, w->lava_count, 4);
        for (unsigned i = 0; i < w->lava_count; ++i)
        {
            const PortableLavaBubble *a = &w->lava[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_x_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_y_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, a->period, 2);
            hash_word(&h, a->phase, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->lava_burn_ticks[i], 2);
    }
    if (w->thwomp_count)
    {
        hash_word(&h, UINT32_C(0x31505754), 4); /* TWP1 */
        hash_word(&h, w->thwomp_count, 4);
        for (unsigned i = 0; i < w->thwomp_count; ++i)
        {
            const PortableThwomp *a = &w->thwomps[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_x_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_y_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, a->variant, 1);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 8; ++i)
        {
            const PortableMeshCrushState *a = &w->crush[i];
            hash_word(&h, a->contact_timer, 2);
            hash_word(&h, a->affine.inverse_x_q8, 2);
            hash_word(&h, a->affine.inverse_y_q8, 2);
            hash_word(&h, a->affine.mode, 1);
            hash_word(&h, a->affine.row, 1);
            hash_word(&h, a->affine.remaining, 1);
            hash_word(&h, a->affine.pending_mode, 1);
        }
    }
    if (w->cannonball_count)
    {
        hash_word(&h, UINT32_C(0x314c4243), 4); /* CBL1 */
        hash_word(&h, w->cannonball_count, 4);
        for (unsigned i = 0; i < w->cannonball_count; ++i)
        {
            const PortableCannonball *a = &w->cannonballs[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint32_t)a->state, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, a->affine_x_q8, 2);
            hash_word(&h, a->affine_y_q8, 2);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
            hash_word(&h, a->size, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 8; ++i)
        {
            const PortableMeshStrongHitState *a = &w->strong_hit[i];
            hash_word(&h, a->rate, 2);
            hash_word(&h, a->angle, 2);
            hash_word(&h, a->pending, 1);
            hash_word(&h, a->active, 1);
            hash_word(&h, a->direction, 1);
        }
    }
    if (w->boo_count)
    {
        hash_word(&h, UINT32_C(0x314f4f42), 4); /* BOO1 */
        hash_word(&h, w->boo_count, 4);
        for (unsigned i = 0; i < w->boo_count; ++i)
        {
            const PortableBoo *a = &w->boos[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->base_height_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, a->phase, 4);
            hash_word(&h, a->state, 4);
            hash_word(&h, a->heading, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        hash_word(&h, w->boo_pending_mask, 1);
        hash_word(&h, w->boo_flash_mask, 1);
    }
    if (w->rock_count)
    {
        hash_word(&h, UINT32_C(0x314b4352), 4);
        hash_word(&h, w->rock_count, 4);
        for (unsigned i = 0; i < w->rock_count; ++i)
        {
            const PortableCourseProp *a = &w->rocks[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, a->source_code, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_x_q8, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art_index, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, (uint32_t)a->lake_rock.home_x_q16, 4);
            hash_word(&h, (uint32_t)a->lake_rock.home_y_q16, 4);
            hash_word(&h, (uint32_t)a->lake_rock.velocity_z_q16, 4);
            hash_word(&h, (uint16_t)a->lake_rock.timer, 2);
            hash_word(&h, (uint16_t)a->lake_rock.delay, 2);
            hash_word(&h, a->lake_rock.frame, 1);
            hash_word(&h, a->lake_rock.mirrored, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->lava_burn_ticks[i], 2);
    }
    if (w->teepee_count)
    {
        hash_word(&h, UINT32_C(0x31504554), 4);
        hash_word(&h, w->teepee_count, 4);
        for (unsigned i = 0; i < w->teepee_count; ++i)
        {
            const PortableCourseProp *a = &w->teepees[i];
            const PortableTeepeeMotion *m = &a->teepee;
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, a->source_code, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_x_q8, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art_index, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, (uint32_t)m->velocity_x_q16, 4);
            hash_word(&h, (uint32_t)m->velocity_y_q16, 4);
            hash_word(&h, (uint32_t)m->rider_x_q16, 4);
            hash_word(&h, (uint32_t)m->rider_y_q16, 4);
            hash_word(&h, (uint32_t)m->rider_z_q16, 4);
            hash_word(&h, (uint32_t)m->rider_vx_q16, 4);
            hash_word(&h, (uint32_t)m->rider_vy_q16, 4);
            hash_word(&h, (uint32_t)m->rider_vz_q16, 4);
            hash_word(&h, m->rider_spin, 2);
            hash_word(&h, m->rider_spin_rate, 2);
            hash_word(&h, m->rider_gravity, 2);
            hash_word(&h, m->rider_heading, 2);
            hash_word(&h, m->counter, 1);
            hash_word(&h, m->frame, 1);
            hash_word(&h, m->kart, 1);
            hash_word(&h, m->rider_state, 1);
            hash_word(&h, (uint32_t)w->teepee_plane_q16[i], 4);
            hash_word(&h, (uint32_t)w->teepee_rider_origin_q16[i][0], 4);
            hash_word(&h, (uint32_t)w->teepee_rider_origin_q16[i][1], 4);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->falling_star_count)
    {
        hash_word(&h, UINT32_C(0x31545346), 4); /* FST1 */
        hash_word(&h, w->falling_star_count, 4);
        for (unsigned i = 0; i < w->falling_star_count; ++i)
        {
            const PortableFallingStar *a = &w->falling_stars[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->initial_x_q16, 4);
            hash_word(&h, (uint32_t)a->offset_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_x_q16, 4);
            hash_word(&h, (uint32_t)a->velocity_z_q16, 4);
            hash_word(&h, (uint16_t)a->timer, 2);
            hash_word(&h, (uint16_t)a->delay, 2);
            hash_word(&h, a->affine_q8, 2);
            hash_word(&h, a->angle, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->variant, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
        {
            const PortableMeshStrongHitState *a = &w->strong_hit[i];
            hash_word(&h, a->rate, 2);
            hash_word(&h, a->angle, 2);
            hash_word(&h, a->pending, 1);
            hash_word(&h, a->active, 1);
            hash_word(&h, a->direction, 1);
            hash_word(&h, w->actor_grace[i], 1);
        }
    }
    if (w->thundercloud_count)
    {
        hash_word(&h, UINT32_C(0x314c4354), 4); /* TCL1 */
        hash_word(&h, w->thundercloud_count, 4);
        for (unsigned i = 0; i < w->thundercloud_count; ++i)
        {
            const PortableThundercloud *a = &w->thunderclouds[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, (uint32_t)a->state, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint32_t)a->palette_timer, 4);
            hash_word(&h, a->variant, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art, 1);
        }
        hash_word(&h, w->thundercloud_palette_phase[0], 1);
        hash_word(&h, w->thundercloud_palette_phase[1], 1);
        for (unsigned i = 0; i < 8; ++i)
        {
            const PortableMeshStrongHitState *a = &w->strong_hit[i];
            hash_word(&h, a->rate, 2);
            hash_word(&h, a->angle, 2);
            hash_word(&h, a->pending, 1);
            hash_word(&h, a->active, 1);
            hash_word(&h, a->direction, 1);
            hash_word(&h, w->actor_grace[i], 1);
        }
    }
    if (w->common_prop_count)
    {
        hash_word(&h, UINT32_C(0x31504d43), 4); /* CMP1 */
        hash_word(&h, w->common_prop_count, 4);
        for (unsigned i = 0; i < w->common_prop_count; ++i)
        {
            const PortableCourseProp *a = &w->common_props[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, a->source_code, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_x_q8, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art_index, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->contact_phase, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->mechakoopa_count)
    {
        hash_word(&h, UINT32_C(0x3143454d), 4); /* MEC1 */
        hash_word(&h, w->mechakoopa_count, 4);
        for (unsigned i = 0; i < w->mechakoopa_count; ++i)
        {
            const PortableCourseProp *a = &w->mechakoopas[i];
            const PortableMechakoopaMotion *m = &a->mechakoopa;
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, a->source_code, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_x_q8, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art_index, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->contact_phase, 1);
            hash_word(&h, (uint32_t)m->velocity_x_q16, 4);
            hash_word(&h, m->cycle, 2);
            hash_word(&h, m->heading, 2);
            hash_word(&h, m->animation_ticks, 1);
            hash_word(&h, m->animation_phase, 1);
            hash_word(&h, m->has_facing, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 8; ++i)
        {
            const PortableMeshStrongHitState *a = &w->strong_hit[i];
            hash_word(&h, a->rate, 2);
            hash_word(&h, a->angle, 2);
            hash_word(&h, a->pending, 1);
            hash_word(&h, a->active, 1);
            hash_word(&h, a->direction, 1);
        }
    }
    if (w->kamek_count)
    {
        hash_word(&h, UINT32_C(0x314d414b), 4); /* KAM1 */
        hash_word(&h, w->kamek_count, 4);
        for (unsigned i = 0; i < w->kamek_count; ++i)
        {
            const PortableCourseProp *a = &w->kameks[i];
            const PortableKamekMotion *m = &a->kamek;
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, a->source_code, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_x_q8, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art_index, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->contact_phase, 1);
            hash_word(&h, (uint32_t)m->velocity_x_q16, 4);
            hash_word(&h, (uint32_t)m->velocity_z_q16, 4);
            hash_word(&h, m->phase, 2);
            hash_word(&h, m->heading, 2);
            hash_word(&h, m->has_facing, 1);
            hash_word(&h, m->mirrored, 1);
        }
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
    }
    if (w->pier_boo_count)
    {
        hash_word(&h, UINT32_C(0x314f4250), 4); /* PBO1 */
        hash_word(&h, w->pier_boo_count, 4);
        for (unsigned i = 0; i < w->pier_boo_count; ++i)
        {
            const PortableCourseProp *a = &w->pier_boos[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->height_q16, 4);
            hash_word(&h, a->source_code, 4);
            hash_word(&h, (uint32_t)a->timer, 4);
            hash_word(&h, (uint16_t)a->affine_x_q8, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->state, 1);
            hash_word(&h, a->flags, 1);
            hash_word(&h, a->art_index, 1);
            hash_word(&h, a->checkpoint, 1);
            hash_word(&h, a->placement_order, 1);
            hash_word(&h, a->contact_phase, 1);
            hash_word(&h, (uint32_t)a->pier_boo.velocity_z_q16, 4);
            hash_word(&h, a->pier_boo.phase, 2);
        }
        hash_word(&h, w->pier_boo_visual_rng, 4);
        hash_word(&h, w->source_haunt_lock_mask, 1);
        for (unsigned i = 0; i < 8; ++i)
            hash_word(&h, w->actor_grace[i], 1);
        for (unsigned i = 0; i < 16; ++i)
        {
            const PortableBooAttack *a = &w->source_haunts[i];
            hash_word(&h, (uint32_t)a->x_q16, 4);
            hash_word(&h, (uint32_t)a->y_q16, 4);
            hash_word(&h, (uint32_t)a->z_q16, 4);
            hash_word(&h, (uint32_t)a->vx_q16, 4);
            hash_word(&h, (uint32_t)a->vy_q16, 4);
            hash_word(&h, (uint32_t)a->vz_q16, 4);
            hash_word(&h, a->angle, 2);
            hash_word(&h, a->bob_angle, 2);
            hash_word(&h, a->timer, 2);
            hash_word(&h, a->radius, 2);
            hash_word(&h, (uint16_t)a->affine_y_q8, 2);
            hash_word(&h, a->phase, 1);
            hash_word(&h, a->target, 1);
            hash_word(&h, a->occupied, 1);
            hash_word(&h, a->visible, 1);
            hash_word(&h, a->frame, 1);
            for (unsigned j = 0; j < 3; ++j)
                hash_word(&h, (uint32_t)w->source_haunt_origins_q16[i][j], 4);
            hash_word(&h, w->source_haunt_source_indices[i], 1);
        }
    }
    h = portable_mesh_sky_hash(&w->sky, portable_mesh_course_coins_hash(&w->coins, h));
    if (!h)
        return 0;
    if (w->ground_follow_version)
    {
        hash_word(&h, UINT32_C(0x31444e47), 4); /* GND1 */
        hash_word(&h, w->ground_follow_version, 1);
        const uint32_t counts[4] = {w->count, w->penguin_count, w->mouse_count, w->mechakoopa_count};
        const int32_t *offsets[4] = {w->crab_ground_offset_q16, w->penguin_ground_offset_q16,
            w->mouse_ground_offset_q16, w->mechakoopa_ground_offset_q16};
        for (unsigned f = 0; f < 4; ++f)
        {
            hash_word(&h, counts[f], 4);
            for (unsigned i = 0; i < counts[f]; ++i)
                hash_word(&h, (uint32_t)offsets[f][i], 4);
        }
    }
    return h;
}
/* Reflection is an isolated modern snowman response, not a copied source
 * impulse coefficient: tangent radial reflection uses authored wall restitution. */
static int snowman_bump(PortableMeshKartState *k, const int32_t p[3], int32_t restitution)
{
    const int64_t U = 1073741824;
    int64_t radial[3], height = 0;
    for (unsigned j = 0; j < 3; ++j)
    {
        if (k->velocity_q16[j] < -16777216 || k->velocity_q16[j] > 16777216 ||
            k->forward_q30[j] < -U || k->forward_q30[j] > U)
            return 0;
        radial[j] = (int64_t)k->position_q16[j] - p[j];
        height += radial[j] * k->up_q30[j];
    }
    height /= U;
    for (unsigned j = 0; j < 3; ++j)
        radial[j] -= height * k->up_q30[j] / U;
    int32_t normal[3];
    if (!portable_fixed_3d_normalize(radial, normal))
    {
        int64_t vertical = 0;
        for (unsigned j = 0; j < 3; ++j)
            vertical += (int64_t)k->velocity_q16[j] * k->up_q30[j];
        vertical /= U;
        for (unsigned j = 0; j < 3; ++j)
            radial[j] = -(int64_t)k->velocity_q16[j] + vertical * k->up_q30[j] / U;
        if (!portable_fixed_3d_normalize(radial, normal))
            memset(normal, 0, sizeof normal);
    }
    int64_t dot = 0;
    for (unsigned j = 0; j < 3; ++j)
        dot += (int64_t)k->velocity_q16[j] * normal[j];
    dot /= U;
    if (dot < 0)
    {
        int64_t impulse = dot * (65536 + (int64_t)restitution) / 65536;
        for (unsigned j = 0; j < 3; ++j)
        {
            int64_t v = (int64_t)k->velocity_q16[j] - impulse * normal[j] / U;
            if (v < -16777216 || v > 16777216)
                return 0;
            k->velocity_q16[j] = (int32_t)v;
        }
    }
    return portable_mesh_kart_project_travel_speed(k, &k->speed_q16) == PORTABLE_MESH_OK;
}
static int strong_contact_admit(const PortableMeshCourseActorsConfig *c,
                                PortableMeshCourseActors *world,
                                PortableMeshItemStatusOwner *status, const PortableMeshItems *items,
                                const PortableMeshItemsConfig *ic, unsigned racer, char *error,
                                size_t cap)
{
    unsigned seen = 0, short_spin = (ic->human_mask & (1u << racer)) != 0;
    int found = 0;
    for (unsigned rank = 0; rank < ic->count; ++rank)
    {
        const unsigned seat = items->standings[rank];
        if (seat >= ic->count || (seen & (1u << seat)))
            return fail(error, cap, c->course_id, "invalid strong contact standings");
        seen |= 1u << seat;
        if (seat == racer)
            found = 1;
        else if (!found && (ic->human_mask & (1u << seat)))
            short_spin = 1;
    }
    if (!found)
        return fail(error, cap, c->course_id, "missing strong contact victim rank");
    PortableMeshItemDamage damage;
    if (portable_mesh_item_status_damage(status, ic->rules, racer, 1, &damage) !=
            PORTABLE_MESH_ITEM_STATUS_OK ||
        damage != PORTABLE_MESH_DAMAGE_APPLIED ||
        !portable_mesh_course_strong_hit_admit(
            &world->strong_hit[racer], short_spin ? 0x1000 : 0x2000, (uint8_t)(world->tick & 1)))
        return fail(error, cap, c->course_id, "invalid strong contact admission");
    status->racers[racer].spin_ticks = 0; /* Strong owner replaces the weak fallback. */
    return 1;
}
/* FEB8/E490 owns a distinct source spin clock. The modern impulse is the
 * authored MIR response in the kart's support plane, not source SI dynamics. */
static int cannonball_strong_contact(const PortableMeshCourseActorsConfig *c,
                                     PortableMeshCourseActors *world,
                                     PortableMeshItemStatusOwner *status,
                                     const PortableMeshItems *items,
                                     const PortableMeshItemsConfig *ic, PortableMeshRacerState *r,
                                     unsigned racer, const int32_t p[3], char *error, size_t cap)
{
    if (!strong_contact_admit(c, world, status, items, ic, racer, error, cap))
        return 0;
    PortableMeshKartState *k = &r->kart;
    if (!portable_mesh_kart_state_valid(k))
        return fail(error, cap, c->course_id, "invalid cannonball kart");
    const int64_t U = 1073741824;
    int64_t radial[3], offset = 0, vertical = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        radial[axis] = (int64_t)k->position_q16[axis] - p[axis];
        offset += radial[axis] * k->up_q30[axis];
        vertical += (int64_t)k->velocity_q16[axis] * k->up_q30[axis];
    }
    offset /= U;
    vertical /= U;
    for (unsigned axis = 0; axis < 3; ++axis)
        radial[axis] -= offset * k->up_q30[axis] / U;
    int32_t normal[3];
    if (!portable_fixed_3d_normalize(radial, normal))
    {
        int64_t forward_up = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            forward_up -= (int64_t)k->forward_q30[axis] * k->up_q30[axis];
        forward_up /= U;
        for (unsigned axis = 0; axis < 3; ++axis)
            radial[axis] = -(int64_t)k->forward_q30[axis] - forward_up * k->up_q30[axis] / U;
        if (!portable_fixed_3d_normalize(radial, normal))
            return fail(error, cap, c->course_id, "invalid cannonball impulse basis");
    }
    int64_t delta[3], fraction = U;
    const int64_t limit = 16777216;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const int64_t original = k->velocity_q16[axis];
        const int64_t up_velocity = vertical * k->up_q30[axis] / U;
        const int64_t desired = (original - up_velocity) * ic->rules->hit_speed_ratio_q16 / 65536 +
                                (int64_t)normal[axis] * ic->rules->hit_impulse_q16 / U +
                                up_velocity;
        delta[axis] = desired - original;
        /* The original velocity is already valid. Restrict one common Q30
         * fraction of the tangent response, retaining its support-normal
         * component instead of independently clipping world axes. Flooring
         * the bound keeps truncation of each scaled delta inside the limit. */
        int64_t allowed = U;
        if (desired > limit)
            allowed = (limit - original) * U / delta[axis];
        else if (desired < -limit)
            allowed = (-limit - original) * U / delta[axis];
        if (allowed < fraction)
            fraction = allowed;
    }
    k->outside_drift_q16 = 0;
    int32_t travel[3];
    if (portable_mesh_kart_travel_direction(k, travel) != PORTABLE_MESH_OK)
        return 0;
    int64_t speed = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const int64_t velocity = (int64_t)k->velocity_q16[axis] + delta[axis] * fraction / U;
        k->velocity_q16[axis] = (int32_t)velocity;
        speed += velocity * travel[axis];
    }
    speed /= U;
    /* Component-bounded modern impulses may have a larger vector norm. Keep
     * the scalar speed within the same public SI state limit as other hits. */
    k->speed_q16 = (int32_t)(speed < -limit ? -limit : speed > limit ? limit : speed);
    k->drift_charge = 0;
    k->drift_direction = 0;
    k->drift_tier = 0;
    k->outside_drift_q16 = 0;
    memset(k->boost_ticks, 0, sizeof k->boost_ticks);
    k->panel_ticks = 0;
    return portable_mesh_kart_state_valid(k) ||
           fail(error, cap, c->course_id, "invalid cannonball result");
}
static uint16_t source_heading(const int32_t forward[3])
{
    int64_t x = -(int64_t)forward[1], y = forward[0];
    if (!y)
        return x < 0 ? 0x8000 : 0;
    if (!x)
        return y > 0 ? 0x4000 : 0xc000;
    static const int32_t angles[16] = {8192, 4836, 2555, 1297, 651, 326, 163, 81,
                                       41,   20,   10,   5,    3,   1,   1,   0};
    int32_t angle = 0;
    if (x < 0)
    {
        x = -x;
        y = -y;
        angle = 32768;
    }
    for (unsigned i = 0; i < 16 && y; ++i)
    {
        const int64_t divisor = INT64_C(1) << i, old = x;
        if (y > 0)
        {
            x += y / divisor;
            y -= old / divisor;
            angle += angles[i];
        }
        else
        {
            x -= y / divisor;
            y += old / divisor;
            angle -= angles[i];
        }
    }
    return (uint16_t)angle;
}
/* 40570 consumes source Q8 components and a local integer contact anchor.
 * Modern meshes have no EE/F0 source cache: use the current center offset in
 * the same contact plane, preserving the source resultant math and source
 * heading. Reproject its XY result onto the support tangent and retain actual
 * vertical velocity. A common delta fraction bounds extreme authored scales. */
static int source_resultant_rebound(const PortableMeshCourseActorsConfig *c,
                                    PortableMeshKartState *k, const int32_t p[3])
{
    int16_t anchor[2];
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        const int64_t source = ((int64_t)k->position_q16[axis] - p[axis]) / c->scale_q16;
        if (source < INT16_MIN || source > INT16_MAX)
            return 0;
        anchor[axis] = (int16_t)source;
    }
    PortableDriverObjectContactResult result;
    if (portable_driver_object_contact_resultant(
            anchor[0], anchor[1], 0, 0, lava_velocity(k->velocity_q16[0], c->scale_q16),
            lava_velocity(k->velocity_q16[1], c->scale_q16), source_heading(k->forward_q30),
            c->sine, 1024, &result) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return 0;
    const int64_t U = INT64_C(1073741824), limit = 16777216;
    int64_t desired[3] = {(int64_t)result.resultant_x * c->scale_q16 * 1024 / 4389,
                          (int64_t)result.resultant_y * c->scale_q16 * 1024 / 4389, 0};
    int64_t desired_up = 0, original_up = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        desired_up += desired[axis] * k->up_q30[axis];
        original_up += (int64_t)k->velocity_q16[axis] * k->up_q30[axis];
    }
    desired_up /= U;
    original_up /= U;
    int64_t delta[3], fraction = U;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        desired[axis] += (original_up - desired_up) * k->up_q30[axis] / U;
        delta[axis] = desired[axis] - k->velocity_q16[axis];
        int64_t allowed = U;
        if (desired[axis] > limit)
            allowed = (limit - k->velocity_q16[axis]) * U / delta[axis];
        else if (desired[axis] < -limit)
            allowed = (-limit - k->velocity_q16[axis]) * U / delta[axis];
        if (allowed < fraction)
            fraction = allowed;
    }
    int32_t travel[3];
    if (portable_mesh_kart_travel_direction(k, travel) != PORTABLE_MESH_OK)
        return 0;
    int64_t speed = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        k->velocity_q16[axis] += (int32_t)(delta[axis] * fraction / U);
        speed += (int64_t)k->velocity_q16[axis] * travel[axis];
    }
    speed /= U;
    k->speed_q16 = (int32_t)(speed < -limit ? -limit : speed > limit ? limit : speed);
    return portable_mesh_kart_state_valid(k);
}
static int falling_star_rebound(const PortableMeshCourseActorsConfig *c, PortableMeshKartState *k,
                                const int32_t p[3])
{
    if (!source_resultant_rebound(c, k, p))
        return 0;
    k->drift_charge = 0;
    k->drift_direction = 0;
    k->drift_tier = 0;
    k->outside_drift_q16 = 0;
    memset(k->boost_ticks, 0, sizeof k->boost_ticks);
    k->panel_ticks = 0;
    return portable_mesh_kart_state_valid(k);
}
static int source_coordinate(int64_t world, int32_t anchor, int32_t origin, int32_t scale,
                             int32_t *out)
{
    const int64_t product = (world - anchor) * 65536;
    const int64_t v =
        (product < 0 ? -((-product + scale / 2) / scale) : (product + scale / 2) / scale) + origin;
    if (v < INT32_MIN || v > INT32_MAX)
        return 0;
    *out = (int32_t)v;
    return 1;
}
static int teepee_tick(const PortableMeshCourseActorsConfig *c, PortableMeshCourseActors *w,
                       PortableMeshItemStatusOwner *status, const PortableMeshItemsConfig *ic,
                       const PortableMeshRacerState *racers, size_t count,
                       PortableMeshCourseActorEvents *events, char *error, size_t cap)
{
    for (unsigned i = 0; i < c->teepee_count; ++i)
    {
        PortableCourseProp *a = &w->teepees[i];
        const unsigned victim = a->teepee.kart;
        const unsigned previous = a->teepee.rider_state, spawned = a->state == 1;
        PortableTeepeeKart karts[8] = {{0}};
        if (spawned || previous)
        {
            if (victim >= count || !portable_mesh_kart_state_valid(&racers[victim].kart))
                return fail(error, cap, c->course_id, "invalid teepee victim");
            const PortableMeshRacerState *r = &racers[victim];
            const int32_t scale = events->movement_body_scale_q16[victim]
                                      ? events->movement_body_scale_q16[victim]
                                      : 65536;
            if (scale < 3277 || scale > 65536)
                return fail(error, cap, c->course_id, "invalid teepee movement shape");
            const int64_t clearance =
                (int64_t)portable_fixed_3d_mul(ic->kart_rules->kart_radius_q16, scale) +
                portable_fixed_3d_mul(ic->kart_rules->ride_height_q16, scale);
            int64_t foot[3];
            for (unsigned j = 0; j < 3; ++j)
                foot[j] =
                    (int64_t)r->kart.position_q16[j] - clearance * r->kart.up_q30[j] / 1073741824;
            if (r->kart.grounded && (spawned || previous == 1))
            {
                if (foot[2] < -268435456 || foot[2] > 268435456)
                    return fail(error, cap, c->course_id, "teepee support plane budget");
                w->teepee_plane_q16[i] = (int32_t)foot[2];
            }
            PortableTeepeeKart *k = &karts[victim];
            if (spawned || previous == 1)
            {
                if (foot[0] < -268435456 || foot[0] > 268435456 || foot[1] < -268435456 ||
                    foot[1] > 268435456)
                    return fail(error, cap, c->course_id, "teepee rider origin budget");
                w->teepee_rider_origin_q16[i][0] = (int32_t)foot[0];
                w->teepee_rider_origin_q16[i][1] = (int32_t)foot[1];
            }
            /* The unchanged rider controller only needs a local origin; its
             * tent burst continues to integrate independently in source XY. */
            k->x_q16 = k->y_q16 = 0;
            if (!source_coordinate(foot[2], w->teepee_plane_q16[i], 0, c->scale_q16, &k->z_q16))
                return fail(error, cap, c->course_id, "teepee source height budget");
            const int64_t raw_speed =
                (int64_t)r->kart.speed_q16 * 4389 * 256 * 65536 / (262144 * (int64_t)c->scale_q16);
            const uint32_t bits = (uint32_t)raw_speed;
            k->speed_q16 = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
            k->component_x = lava_velocity(r->kart.velocity_q16[0], c->scale_q16);
            k->component_y = lava_velocity(r->kart.velocity_q16[1], c->scale_q16);
            k->heading = source_heading(r->kart.forward_q30);
            k->protection = status->racers[victim].star_ticks ? 1 : 0;
            k->lock = status->racers[victim].boo_lock_ticks;
            k->is_player = (uint8_t)((ic->human_mask & (1u << victim)) != 0);
            k->muted = (uint8_t)(r->phase != PORTABLE_MESH_RACER_RACING);
        }
        PortableCourseProps singleton;
        singleton.actors[0] = *a;
        singleton.count = 1;
        singleton.initial = NULL;
        const int cues =
            portable_course_props_tick_teepees(&singleton, karts, count, c->sine, 1024, w->tick);
        if (cues < 0 || !teepee_valid(&singleton.actors[0], &c->initial_teepees[i]))
            return fail(error, cap, c->course_id, "invalid teepee source update");
        *a = singleton.actors[0];
        if (spawned && a->teepee.rider_state == 1)
        {
            if (portable_mesh_item_status_activate(status, ic->rules, victim,
                                                   PORTABLE_MESH_STATUS_RIDER_LOCK) !=
                PORTABLE_MESH_ITEM_STATUS_OK)
                return fail(error, cap, c->course_id, "teepee shared lock assignment");
            events->teepee_attached |= UINT64_C(1) << i;
            events->teepee_assigned_mask |= (uint8_t)(1u << victim);
        }
        if (a->teepee.rider_state == 2 && previous != 2)
            events->teepee_flung |= UINT64_C(1) << i;
        if (cues)
            events->teepee_release_mask |= (uint8_t)(1u << victim);
        int32_t p[3];
        if (!pose_checked(c, w,
                          portable_mesh_course_actors_count(w) - w->pier_boo_count -
                              w->kamek_count - w->mechakoopa_count - w->common_prop_count -
                              w->thundercloud_count - w->falling_star_count - w->teepee_count + i,
                          p))
            return fail(error, cap, c->course_id, "invalid teepee tent pose");
        if (a->teepee.rider_state && (!scaled_coordinate(a->teepee.rider_x_q16, 0, c->scale_q16,
                                                         w->teepee_rider_origin_q16[i][0], &p[0]) ||
                                      !scaled_coordinate(a->teepee.rider_y_q16, 0, c->scale_q16,
                                                         w->teepee_rider_origin_q16[i][1], &p[1]) ||
                                      !scaled_coordinate(a->teepee.rider_z_q16, 0, c->scale_q16,
                                                         w->teepee_plane_q16[i], &p[2])))
            return fail(error, cap, c->course_id, "invalid teepee rider pose");
    }
    return 1;
}
static int source_haunts_tick(const PortableMeshCourseActorsConfig *c, PortableMeshCourseActors *w,
                              PortableMeshItemStatusOwner *status,
                              const PortableMeshItemsConfig *ic,
                              const PortableMeshRacerState *racers, size_t count,
                              PortableMeshCourseActorEvents *e, char *error, size_t cap)
{
    for (unsigned i = 0; i < 16; ++i)
    {
        PortableBooAttack *a = &w->source_haunts[i];
        if (!a->occupied)
            continue;
        unsigned victim = a->target;
        if (victim >= count)
            return fail(error, cap, c->course_id, "invalid source haunt target");
        const uint8_t bit = (uint8_t)(1u << victim);
        const PortableMeshRacerState *r = &racers[victim];
        if (r->phase != PORTABLE_MESH_RACER_RACING || r->kart.respawn_ticks || r->respawn_grace ||
            (ic->inactive_mask & bit))
        {
            if (a->phase == 2 || a->phase == 3)
                e->source_haunt_music_release_mask |= bit;
            memset(a, 0, sizeof *a);
            memset(w->source_haunt_origins_q16[i], 0, sizeof w->source_haunt_origins_q16[i]);
            w->source_haunt_source_indices[i] = 0;
            continue;
        }
        const unsigned before = a->phase;
        if (before <= 5)
        {
            int32_t scale =
                e->movement_body_scale_q16[victim] ? e->movement_body_scale_q16[victim] : 65536;
            if (scale < 3277 || scale > 65536)
                return fail(error, cap, c->course_id, "invalid source haunt body scale");
            int64_t clearance =
                (int64_t)portable_fixed_3d_mul(ic->kart_rules->kart_radius_q16, scale) +
                portable_fixed_3d_mul(ic->kart_rules->ride_height_q16, scale);
            for (unsigned j = 0; j < 3; ++j)
            {
                int64_t foot =
                    (int64_t)r->kart.position_q16[j] - clearance * r->kart.up_q30[j] / 1073741824;
                if (foot < -268435456 || foot > 268435456)
                    return fail(error, cap, c->course_id, "source haunt origin budget");
                w->source_haunt_origins_q16[i][j] = (int32_t)foot;
            }
        }
        PortableBooAttackTarget target = {
            .component_x = lava_velocity(r->kart.velocity_q16[0], c->scale_q16),
            .component_y = lava_velocity(r->kart.velocity_q16[1], c->scale_q16),
            .heading = source_heading(r->kart.forward_q30),
            .protection = status->racers[victim].star_ticks ? 1 : 0,
            .lock_ticks = status->racers[victim].boo_lock_ticks};
        if (!portable_boo_attack_tick(a, &target, c->sine, 1024, &c->pier_boo_rules))
            return fail(error, cap, c->course_id, "invalid source haunt tick");
        if (before <= 1 && a->phase == 2)
        {
            if (portable_mesh_item_status_source_haunt_lock(status, ic->rules, victim,
                                                            c->pier_boo_rules.lock_ticks) !=
                PORTABLE_MESH_ITEM_STATUS_OK)
                return fail(error, cap, c->course_id, "source haunt lock assignment");
            w->source_haunt_lock_mask |= bit;
            e->source_haunt_assigned_mask |= bit;
        }
        const int music = portable_boo_attack_music_transition((uint8_t)before, a->phase);
        if (music > 0)
            e->source_haunt_music_start_mask |= bit;
        if (music < 0)
            e->source_haunt_music_release_mask |= bit;
        if (!a->occupied)
        {
            memset(a, 0, sizeof *a);
            memset(w->source_haunt_origins_q16[i], 0, sizeof w->source_haunt_origins_q16[i]);
            w->source_haunt_source_indices[i] = 0;
        }
        else
        {
            int32_t pose[3];
            if (!haunt_valid(c, w, i) || !haunt_pose(c, w, i, pose))
                return fail(error, cap, c->course_id, "source haunt pose/state budget");
        }
    }
    return 1;
}
int portable_mesh_course_actors_resolve(const PortableMeshCourseActorsConfig *c,
                                        PortableMeshCourseActors *w, PortableMeshItems *items,
                                        const PortableMeshItemsConfig *ic,
                                        PortableMeshRacerState *racers, size_t count,
                                        PortableMeshCourseActorEvents *events, char *error,
                                        size_t cap)
{
    if (!world_valid(c, w) || !items || !ic || !ic->course || ic->course->id != c->course_id ||
        !portable_mesh_item_rules_valid(ic->rules) ||
        !portable_mesh_kart_rules_valid(ic->kart_rules) || !racers || !events || !count ||
        count > 8 || count != ic->count || items->status.count != count || items->prepared)
        return fail(error, cap, c ? c->course_id : 0, "invalid contact owner");
    PortableMeshCourseActors next = *w;
    PortableMeshItemStatusOwner status = items->status;
    PortableMeshRacerState r[8];
    memcpy(r, racers, count * sizeof *r);
    PortableMeshCourseActorEvents e = {.coins = events->coins,
                                       .piranha_bite_mask = events->piranha_bite_mask,
                                       .capture_mask = events->capture_mask,
                                       .launch_mask = events->launch_mask,
                                       .thwomp_landed = events->thwomp_landed,
                                       .thwomp_recover_mask = events->thwomp_recover_mask,
                                       .cannonball_launched = events->cannonball_launched,
                                       .cannonball_exploded = events->cannonball_exploded};
    e.source_haunt_clock_mask = events->source_haunt_clock_mask;
    memcpy(e.source_haunt_lock_clock, events->source_haunt_lock_clock,
           sizeof e.source_haunt_lock_clock);
    e.observed_item_haunt_mask = events->observed_item_haunt_mask;
    next.source_haunt_lock_mask &= (uint8_t)~events->observed_item_haunt_mask;
    memcpy(e.rock_cue_masks, events->rock_cue_masks, sizeof e.rock_cue_masks);
    e.teepee_clock_mask = events->teepee_clock_mask;
    memcpy(e.teepee_lock_clock, events->teepee_lock_clock, sizeof e.teepee_lock_clock);
    memcpy(e.movement_body_scale_q16, events->movement_body_scale_q16,
           sizeof e.movement_body_scale_q16);
    unsigned order[1152];
    const unsigned total = portable_mesh_course_actors_config_count(c);
    const unsigned old_total = total - c->pier_boo_count - c->kamek_count - c->mechakoopa_count -
                               c->common_prop_count - c->thundercloud_count - c->falling_star_count;
    for (unsigned i = 0; i < total; ++i)
    {
        order[i] = i;
        for (unsigned j = i; j && placement(c, order[j - 1]) < placement(c, order[j]); --j)
        {
            unsigned t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    }
    for (unsigned i = 0; i < count; ++i)
        /* Generic recovering also marks capture/crush-held release turns.
         * Raw manual rescue is cleared by the admitted-turn wrapper. */
        if (r[i].kart.respawn_ticks || r[i].respawn_grace)
        {
            next.lava_burn_ticks[i] = 0;
            memset(&next.crush[i], 0, sizeof next.crush[i]);
            memset(&next.strong_hit[i], 0, sizeof next.strong_hit[i]);
            next.boo_pending_mask &= (uint8_t)~(1u << i);
            next.boo_flash_mask &= (uint8_t)~(1u << i);
        }
    for (unsigned n = 0; n < total; ++n)
    {
        unsigned a = order[n];
        const int pier_boo = a >= total - c->pier_boo_count;
        const int kamek = !pier_boo && a >= total - c->pier_boo_count - c->kamek_count;
        const int mechakoopa =
            !pier_boo && !kamek &&
            a >= total - c->pier_boo_count - c->kamek_count - c->mechakoopa_count;
        const int common_prop = !pier_boo && !kamek && !mechakoopa &&
                                a >= total - c->pier_boo_count - c->kamek_count -
                                         c->mechakoopa_count - c->common_prop_count;
        const int thundercloud = !pier_boo && !kamek && !mechakoopa && !common_prop &&
                                 a >= total - c->pier_boo_count - c->kamek_count -
                                          c->mechakoopa_count - c->common_prop_count -
                                          c->thundercloud_count;
        const int falling_star =
            !pier_boo && !kamek && !mechakoopa && !common_prop && !thundercloud && a >= old_total;
        const int teepee = !pier_boo && !kamek && !mechakoopa && !common_prop && !thundercloud &&
                           !falling_star && a >= old_total - c->teepee_count;
        const int rock = a >= old_total - c->rock_count - c->teepee_count && !teepee &&
                         !falling_star && !thundercloud && !common_prop && !mechakoopa &&
                         !pier_boo && !kamek;
        const int boo = a >= c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                                 c->piranha_count + c->seagull_count + c->lava_count +
                                 c->thwomp_count + c->cannonball_count &&
                        !rock && !teepee && !falling_star && !thundercloud && !common_prop &&
                        !mechakoopa && !pier_boo && !kamek;
        const int cannonball = a >= c->count + c->snowman_count + c->penguin_count +
                                        c->mouse_count + c->piranha_count + c->seagull_count +
                                        c->lava_count + c->thwomp_count &&
                               !boo && !rock && !teepee && !falling_star && !thundercloud &&
                               !common_prop && !mechakoopa && !pier_boo && !kamek;
        const int thwomp = a >= c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                                    c->piranha_count + c->seagull_count + c->lava_count &&
                           !cannonball && !boo && !rock && !teepee && !falling_star &&
                           !thundercloud && !common_prop && !mechakoopa && !pier_boo && !kamek;
        const int lava = a >= c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                                  c->piranha_count + c->seagull_count &&
                         !thwomp && !cannonball && !boo && !rock && !teepee && !falling_star &&
                         !thundercloud && !common_prop && !mechakoopa && !pier_boo && !kamek;
        const int seagull = a >= c->count + c->snowman_count + c->penguin_count + c->mouse_count +
                                     c->piranha_count &&
                            !lava && !thwomp && !cannonball && !boo && !rock && !teepee &&
                            !falling_star && !thundercloud && !common_prop && !mechakoopa &&
                            !pier_boo && !kamek;
        const int piranha = a >= c->count + c->snowman_count + c->penguin_count + c->mouse_count &&
                            !seagull && !lava && !thwomp && !cannonball && !boo && !rock &&
                            !teepee && !falling_star && !thundercloud && !common_prop &&
                            !mechakoopa && !pier_boo && !kamek;
        const int mouse = a >= c->count + c->snowman_count + c->penguin_count && !piranha &&
                          !seagull && !lava && !thwomp && !cannonball && !boo && !rock && !teepee &&
                          !falling_star && !thundercloud && !common_prop && !mechakoopa &&
                          !pier_boo && !kamek;
        const int penguin = a >= c->count + c->snowman_count && !mouse && !piranha && !seagull &&
                            !lava && !thwomp && !cannonball && !boo && !rock && !teepee &&
                            !falling_star && !thundercloud && !common_prop && !mechakoopa &&
                            !pier_boo && !kamek;
        const int snowman = a >= c->count && !penguin && !mouse && !piranha && !seagull && !lava &&
                            !thwomp && !cannonball && !boo && !rock && !teepee && !falling_star &&
                            !thundercloud && !common_prop && !mechakoopa && !pier_boo && !kamek;
        const unsigned index =
            pier_boo       ? a - total + c->pier_boo_count
            : kamek        ? a - total + c->pier_boo_count + c->kamek_count
            : mechakoopa   ? a - total + c->pier_boo_count + c->kamek_count + c->mechakoopa_count
            : common_prop  ? a - total + c->pier_boo_count + c->kamek_count + c->mechakoopa_count +
                                 c->common_prop_count
            : thundercloud ? a - total + c->pier_boo_count + c->kamek_count + c->mechakoopa_count +
                                 c->common_prop_count + c->thundercloud_count
            : falling_star ? a - old_total
            : teepee       ? a - old_total + c->teepee_count
            : rock         ? a - old_total + c->rock_count + c->teepee_count
            : boo ? a - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                        c->piranha_count - c->seagull_count - c->lava_count - c->thwomp_count -
                        c->cannonball_count
            : cannonball ? a - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                               c->piranha_count - c->seagull_count - c->lava_count - c->thwomp_count
            : thwomp     ? a - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                               c->piranha_count - c->seagull_count - c->lava_count
            : lava       ? a - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                               c->piranha_count - c->seagull_count
            : seagull    ? a - c->count - c->snowman_count - c->penguin_count - c->mouse_count -
                               c->piranha_count
            : piranha    ? a - c->count - c->snowman_count - c->penguin_count - c->mouse_count
            : mouse      ? a - c->count - c->snowman_count - c->penguin_count
            : penguin    ? a - c->count - c->snowman_count
            : snowman    ? a - c->count
                         : a;
        uint8_t *flags = pier_boo       ? &next.pier_boos[index].flags
                         : kamek        ? &next.kameks[index].flags
                         : mechakoopa   ? &next.mechakoopas[index].flags
                         : common_prop  ? &next.common_props[index].flags
                         : thundercloud ? &next.thunderclouds[index].flags
                         : falling_star ? &next.falling_stars[index].flags
                         : teepee       ? &next.teepees[index].flags
                         : rock         ? &next.rocks[index].flags
                         : boo          ? &next.boos[index].flags
                         : cannonball   ? &next.cannonballs[index].flags
                         : thwomp       ? &next.thwomps[index].flags
                         : lava         ? &next.lava[index].flags
                         : seagull      ? &next.seagulls[index].flags
                         : piranha      ? &next.piranhas[index].flags
                         : mouse        ? &next.mice[index].flags
                         : penguin      ? &next.penguins[index].flags
                         : snowman      ? &next.snowmen[index].flags
                                        : &next.crabs[index].flags;
        if (common_prop ? (next.common_props[index].state || !(*flags & 1) ||
                           ((next.common_props[index].source_code >> 16) == 8 && !(*flags & 2)))
                        : !(*flags & 2))
            continue;
        int32_t p[3];
        if (!pose_checked(c, &next, a, p))
            return fail(error, cap, c->course_id, "invalid contact pose");
        for (unsigned i = 0; i < count; ++i)
        {
            if (common_prop ? next.common_props[index].state != 0 : !(*flags & 2))
                break;
            if (r[i].phase != PORTABLE_MESH_RACER_RACING || r[i].respawn_grace ||
                r[i].kart.respawn_ticks ||
                (items->controls[i].recovering && !(piranha && next.capture[i].remaining) &&
                 !((boo || teepee) && next.strong_hit[i].active && !next.crush[i].contact_timer &&
                   !next.capture[i].remaining)) ||
                (ic->inactive_mask & (1u << i)) ||
                ((teepee ? 0
                  : pier_boo || kamek || mechakoopa || common_prop
                      ? (!status.racers[i].star_ticks && next.actor_grace[i])
                  : boo ? next.actor_grace[i] > 1
                        : next.actor_grace[i] && !seagull) &&
                 !(piranha && next.capture[i].remaining)))
                continue;
            /* Source crab half-extents are size8 <<14 = two source units.
             * Modern adaptation removes suspension along the kart's own up,
             * retaining its real airborne displacement and scaled body sphere.
             * Match status_prepare_drive: Star suppresses Lightning shrink. */
            const PortableMeshItemRacerStatus *racer_status = &status.racers[i];
            if ((next.crush[i].contact_timer || (next.strong_hit[i].active && !boo && !teepee)) &&
                !racer_status->star_ticks)
                continue;
            const int32_t body_scale =
                teepee ? (e.movement_body_scale_q16[i] ? e.movement_body_scale_q16[i] : 65536)
                : racer_status->lightning_ticks && !racer_status->star_ticks
                    ? ic->rules->lightning_scale_ratio_q16
                    : 65536;
            if (body_scale < 3277 || body_scale > 65536)
                return fail(error, cap, c->course_id, "invalid movement body scale");
            const int32_t body_radius =
                portable_fixed_3d_mul(ic->kart_rules->kart_radius_q16, body_scale);
            const int32_t suspension =
                portable_fixed_3d_mul(ic->kart_rules->ride_height_q16, body_scale);
            int64_t radius = (int64_t)c->scale_q16 * (cannonball ? next.cannonballs[index].size / 4
                                                      : thundercloud ? 8
                                                      : thwomp       ? 5
                                                      : rock         ? 1
                                                                     : 2) +
                             body_radius;
            if (boo || pier_boo)
                radius = body_radius;
            if (radius <= 0 || radius > INT32_MAX)
                return fail(error, cap, c->course_id, "invalid contact radius");
            uint64_t distance = 0;
            int outside = 0;
            for (unsigned j = 0; j < 3; ++j)
            {
                if (r[i].kart.up_q30[j] < -1073741824 || r[i].kart.up_q30[j] > 1073741824)
                    return fail(error, cap, c->course_id, "invalid contact up vector");
                const int64_t offset = (int64_t)suspension * r[i].kart.up_q30[j] / 1073741824;
                int64_t d = (int64_t)r[i].kart.position_q16[j] - offset - p[j];
                if (boo || pier_boo)
                {
                    const int64_t extent =
                        (int64_t)c->scale_q16 * (pier_boo ? (j == 2 ? 8 : 2) : (j == 2 ? 16 : 9));
                    d = (d < 0 ? -d : d) - extent;
                    if (d < 0)
                        d = 0;
                }
                if (d > radius || d < -radius)
                {
                    outside = 1;
                    break;
                }
                distance += (uint64_t)(d * d);
            }
            if (outside || distance > (uint64_t)(radius * radius))
                continue;
            if (pier_boo)
            {
                if (racer_status->boo_invisible_ticks ||
                    (!racer_status->star_ticks && racer_status->hit_grace_ticks))
                    continue;
                e.pier_boo_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    portable_course_prop_damage(&next.pier_boos[index]);
                    e.pier_boo_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    unsigned slot = 0;
                    while (slot < 16 && next.source_haunts[slot].occupied)
                        ++slot;
                    if (slot == 16)
                        return fail(error, cap, c->course_id, "Pier Boo haunt pool exhausted");
                    portable_pier_boo_vanish(&next.pier_boos[index]);
                    next.source_haunts[slot] =
                        (PortableBooAttack){.target = (uint8_t)i, .occupied = 1};
                    next.source_haunt_source_indices[slot] = (uint8_t)index;
                    e.pier_boo_touch_mask |= (uint8_t)(1u << i);
                    next.actor_grace[i] = 8;
                }
                continue;
            }
            if (kamek)
            {
                if (racer_status->boo_invisible_ticks ||
                    (!racer_status->star_ticks && racer_status->hit_grace_ticks))
                    continue;
                e.kamek_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    portable_course_prop_damage(&next.kameks[index]);
                    e.kamek_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    next.actor_grace[i] = 8;
                    e.kamek_touch_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (mechakoopa)
            {
                if (racer_status->boo_invisible_ticks ||
                    (!racer_status->star_ticks && racer_status->hit_grace_ticks))
                    continue;
                e.mechakoopa_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    portable_course_prop_damage(&next.mechakoopas[index]);
                    e.mechakoopa_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    if (ic->human_mask & (1u << i))
                    {
                        PortableMeshItemDamage damage;
                        if (portable_mesh_item_status_damage(&status, ic->rules, i, 1, &damage) !=
                                PORTABLE_MESH_ITEM_STATUS_OK ||
                            damage != PORTABLE_MESH_DAMAGE_APPLIED)
                            return fail(error, cap, c->course_id,
                                        "invalid Mechakoopa weak response");
                    }
                    else
                    {
                        if (!strong_contact_admit(c, &next, &status, items, ic, i, error, cap))
                            return 0;
                        e.mechakoopa_strong_mask |= (uint8_t)(1u << i);
                    }
                    next.actor_grace[i] = 8;
                    e.mechakoopa_hit_mask |= (uint8_t)(1u << i);
                    e.hit_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (common_prop)
            {
                if (racer_status->boo_invisible_ticks ||
                    (!racer_status->star_ticks && racer_status->hit_grace_ticks))
                    continue;
                e.common_prop_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    portable_course_prop_damage(&next.common_props[index]);
                    e.common_prop_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    if (!source_resultant_rebound(c, &r[i].kart, p))
                        return fail(error, cap, c->course_id, "invalid common prop rebound");
                    next.actor_grace[i] = 8;
                    e.common_prop_hit_mask |= (uint8_t)(1u << i);
                    e.bump_mask |= (uint8_t)(1u << i);
                    if (next.common_props[index].source_code == 0x860000)
                    {
                        next.common_props[index].contact_phase ^= 1;
                        if (next.common_props[index].contact_phase)
                            e.common_prop_umbrella_open_mask |= (uint8_t)(1u << i);
                        else
                            e.common_prop_umbrella_close_mask |= (uint8_t)(1u << i);
                    }
                }
                continue;
            }
            if (teepee)
            {
                if (racer_status->boo_invisible_ticks)
                    continue;
                portable_teepee_hit(&next.teepees[index], (uint8_t)i,
                                    lava_velocity(r[i].kart.velocity_q16[0], c->scale_q16),
                                    lava_velocity(r[i].kart.velocity_q16[1], c->scale_q16));
                e.teepee_contacts |= UINT64_C(1) << index;
                if (!next.strong_hit[i].active && next.actor_grace[i] <= 1)
                {
                    if (racer_status->star_ticks)
                    {
                        if (ic->human_mask & (1u << i))
                            e.teepee_protected_mask |= (uint8_t)(1u << i);
                    }
                    else if (!racer_status->hit_grace_ticks && !next.actor_grace[i])
                    {
                        next.actor_grace[i] = 8;
                        if (ic->human_mask & (1u << i))
                            e.teepee_touch_mask |= (uint8_t)(1u << i);
                    }
                }
                continue;
            }
            if (boo)
            {
                e.boo_contacts |= UINT64_C(1) << index;
                next.boo_pending_mask |= (uint8_t)(1u << i);
                /* Normal airborne driving/hop corresponds to source mode1;
                 * actual recovery/held controls are excluded above. */
                if ((ic->human_mask & (1u << i)) && !racer_status->star_ticks &&
                    !racer_status->boo_invisible_ticks && !next.strong_hit[i].active)
                    e.boo_touch_mask |= (uint8_t)(1u << i);
                continue;
            }
            if (seagull)
            {
                if (racer_status->boo_invisible_ticks && !racer_status->star_ticks)
                    continue;
                const int32_t previous = next.seagulls[index].state;
                const int protected_contact = racer_status->star_ticks != 0;
                portable_seagull_contact(&next.seagulls[index], 0x2000, 0,
                                         (int16_t)protected_contact);
                e.seagull_contacts |= UINT64_C(1) << index;
                if (previous != next.seagulls[index].state)
                    e.seagull_changed |= UINT64_C(1) << index;
                if (protected_contact)
                    e.seagull_defeated |= UINT64_C(1) << index;
                if (next.actor_grace[i] <= 1 && racer_status->hit_grace_ticks <= 1 &&
                    (ic->human_mask & (1u << i)))
                {
                    if (protected_contact)
                        e.protected_mask |= (uint8_t)(1u << i);
                    else
                        e.seagull_touch_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (piranha)
            {
                if (!racer_status->star_ticks &&
                    (racer_status->boo_invisible_ticks || racer_status->hit_grace_ticks))
                    continue;
                const int32_t previous = next.piranhas[index].state;
                const int protected_contact = racer_status->star_ticks != 0;
                if (!portable_piranha_contact(&next.piranhas[index], 0x2000, 0,
                                              (int16_t)protected_contact,
                                              (ic->human_mask & (1u << i)) != 0))
                    return fail(error, cap, c->course_id, "invalid piranha contact");
                next.piranha_victim[index] = (uint8_t)i;
                e.piranha_contacts |= UINT64_C(1) << index;
                if (previous != next.piranhas[index].state)
                    e.piranha_changed |= UINT64_C(1) << index;
                if (ic->human_mask & (1u << i))
                    e.piranha_touch_mask |= (uint8_t)(1u << i);
                if (protected_contact)
                    e.piranha_protected |= UINT64_C(1) << index;
                else
                    next.actor_grace[i] = 8;
                continue;
            }
            if (snowman)
            {
                const int protected_contact = racer_status->star_ticks != 0;
                if (!protected_contact &&
                    (racer_status->boo_invisible_ticks || racer_status->hit_grace_ticks))
                    continue;
                const int32_t old_state = next.snowmen[index].state;
                if (!portable_snowman_contact(&next.snowmen[index], 0x2000, 0))
                    return fail(error, cap, c->course_id, "invalid snowman contact");
                e.snowman_contacts |= UINT64_C(1) << index;
                if (next.snowmen[index].state != old_state)
                    e.snowman_changed |= UINT64_C(1) << index;
                if (protected_contact)
                    e.protected_mask |= (uint8_t)(1u << i);
                else
                {
                    if (!snowman_bump(&r[i].kart, p, 32768 /* rebounds at half speed */))
                        return fail(error, cap, c->course_id, "invalid snowman bump");
                    next.actor_grace[i] = 8;
                    e.bump_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (thundercloud)
            {
                if (!racer_status->star_ticks &&
                    (racer_status->boo_invisible_ticks || racer_status->hit_grace_ticks))
                    continue;
                e.thundercloud_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    e.thundercloud_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    if (!strong_contact_admit(c, &next, &status, items, ic, i, error, cap) ||
                        portable_mesh_item_status_activate(&status, ic->rules, i,
                                                           PORTABLE_MESH_STATUS_CLOUD_LIGHTNING) !=
                            PORTABLE_MESH_ITEM_STATUS_OK)
                        return fail(error, cap, c->course_id,
                                    "invalid thundercloud strong/lightning response");
                    next.actor_grace[i] = 8;
                    e.thundercloud_hit_mask |= (uint8_t)(1u << i);
                    e.hit_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (falling_star)
            {
                if (!racer_status->star_ticks &&
                    (racer_status->boo_invisible_ticks || racer_status->hit_grace_ticks))
                    continue;
                e.falling_star_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    e.falling_star_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    if (!falling_star_rebound(c, &r[i].kart, p) ||
                        !strong_contact_admit(c, &next, &status, items, ic, i, error, cap))
                        return fail(error, cap, c->course_id,
                                    "invalid falling star strong response");
                    next.actor_grace[i] = 8;
                    e.falling_star_hit_mask |= (uint8_t)(1u << i);
                    e.hit_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (cannonball)
            {
                if (!racer_status->star_ticks &&
                    (racer_status->boo_invisible_ticks || racer_status->hit_grace_ticks))
                    continue;
                e.cannonball_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    e.cannonball_protected_mask |= (uint8_t)(1u << i);
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    if (!cannonball_strong_contact(c, &next, &status, items, ic, &r[i], i, p, error,
                                                   cap))
                        return 0;
                    next.actor_grace[i] = 8;
                    e.cannonball_hit_mask |= (uint8_t)(1u << i);
                    e.hit_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            if (thwomp)
            {
                if (!racer_status->star_ticks &&
                    (racer_status->boo_invisible_ticks || racer_status->hit_grace_ticks))
                    continue;
                e.thwomp_contacts |= UINT64_C(1) << index;
                if (racer_status->star_ticks)
                {
                    portable_thwomp_contact(&next.thwomps[index], 0x2000, 0, 1,
                                            lava_velocity(r[i].kart.velocity_q16[0], c->scale_q16),
                                            lava_velocity(r[i].kart.velocity_q16[1], c->scale_q16));
                    e.thwomp_defeated |= UINT64_C(1) << index;
                    e.protected_mask |= (uint8_t)(1u << i);
                }
                else if (next.thwomps[index].height_q16 > 65536 &&
                         next.thwomps[index].velocity_z_q16 < 0)
                {
                    if (!portable_mesh_course_crush_admit(&c->crush_rules, &next.crush[i],
                                                          &r[i].kart, error, cap))
                        return 0;
                    e.thwomp_crush_mask |= (uint8_t)(1u << i);
                    e.hit_mask |= (uint8_t)(1u << i);
                }
                else
                {
                    if (!portable_mesh_course_thwomp_rebound(&r[i].kart, c->scale_q16, error, cap))
                        return 0;
                    next.actor_grace[i] = 8;
                    e.thwomp_bump_mask |= (uint8_t)(1u << i);
                    e.bump_mask |= (uint8_t)(1u << i);
                }
                continue;
            }
            PortableMeshItemDamage damage;
            if (portable_mesh_item_status_damage(&status, ic->rules, i, 1, &damage) !=
                PORTABLE_MESH_ITEM_STATUS_OK)
                return fail(error, cap, c->course_id, "invalid contact status");
            if (damage == PORTABLE_MESH_DAMAGE_PROTECTED)
            {
                if (rock)
                {
                    e.rock_contacts |= UINT64_C(1) << index;
                    e.rock_protected_mask |= (uint8_t)(1u << i);
                }
                else if (lava)
                {
                    portable_lava_bubble_contact(
                        &next.lava[index], 0x2000, 0, 1,
                        lava_velocity(r[i].kart.velocity_q16[0], c->scale_q16),
                        lava_velocity(r[i].kart.velocity_q16[1], c->scale_q16));
                    e.lava_contacts |= UINT64_C(1) << index;
                    e.lava_defeated |= UINT64_C(1) << index;
                }
                else if (mouse)
                {
                    portable_mouse_contact(&next.mice[index], 0x2000, 0, 1);
                    e.mouse_contacts |= UINT64_C(1) << index;
                    e.mouse_defeated |= UINT64_C(1) << index;
                }
                else if (penguin)
                {
                    if (!portable_penguin_contact(&next.penguins[index], 0x2000, 0, 1))
                        return fail(error, cap, c->course_id, "invalid penguin contact");
                    e.penguin_contacts |= UINT64_C(1) << index;
                    e.penguin_defeated |= UINT64_C(1) << index;
                }
                else
                    portable_crab_contact(&next.crabs[index], 0x2000, 0, 1);
                e.protected_mask |= (uint8_t)(1u << i);
                if (!penguin && !mouse && !lava && !rock)
                    e.protected_actors |= UINT64_C(1) << index;
            }
            if (damage == PORTABLE_MESH_DAMAGE_APPLIED)
            {
                PortableMeshKartState *k = &r[i].kart;
                const int reproject = k->outside_drift_q16 != 0;
                k->speed_q16 = portable_fixed_3d_mul(k->speed_q16, ic->rules->hit_speed_ratio_q16);
                for (unsigned j = 0; j < 3; ++j)
                    k->velocity_q16[j] =
                        portable_fixed_3d_mul(k->velocity_q16[j], ic->rules->hit_speed_ratio_q16);
                k->drift_charge = 0;
                k->drift_direction = 0;
                k->drift_tier = 0;
                k->outside_drift_q16 = 0;
                if (reproject && portable_mesh_kart_project_travel_speed(k, &k->speed_q16) != PORTABLE_MESH_OK)
                    return fail(error, cap, c->course_id, "contact speed outside SI bounds");
                memset(k->boost_ticks, 0, sizeof k->boost_ticks);
                k->panel_ticks = 0;
                e.hit_mask |= (uint8_t)(1u << i);
                if (rock)
                {
                    e.rock_contacts |= UINT64_C(1) << index;
                    e.rock_hit_mask |= (uint8_t)(1u << i);
                    e.lava_burn_mask |= (uint8_t)(1u << i);
                    next.lava_burn_ticks[i] = 250;
                    next.actor_grace[i] = 8;
                }
                else if (lava)
                {
                    e.lava_contacts |= UINT64_C(1) << index;
                    next.lava_burn_ticks[i] = 250;
                    next.actor_grace[i] = 8;
                    e.lava_burn_mask |= (uint8_t)(1u << i);
                }
                else if (mouse)
                    e.mouse_contacts |= UINT64_C(1) << index;
                else if (penguin)
                    e.penguin_contacts |= UINT64_C(1) << index;
                else
                    e.hit_actors |= UINT64_C(1) << index;
            }
        }
    }
    if (c->pier_boo_count && !source_haunts_tick(c, &next, &status, ic, r, count, &e, error, cap))
        return 0;
    if (!teepee_tick(c, &next, &status, ic, r, count, &e, error, cap))
        return 0;
    /* Consume the one-shot request once per accepted simulation turn, never
     * from a camera draw. Only currently implemented higher palette owners
     * (Star and source burn) defer it; no new 2D palette policy is invented. */
    for (unsigned i = 0; i < count; ++i)
        if ((next.boo_pending_mask & (1u << i)) && !status.racers[i].star_ticks &&
            !next.lava_burn_ticks[i])
        {
            next.boo_pending_mask &= (uint8_t)~(1u << i);
            next.boo_flash_mask |= (uint8_t)(1u << i);
        }
    *w = next;
    items->status = status;
    memcpy(racers, r, count * sizeof *r);
    *events = e;
    return 1;
}
