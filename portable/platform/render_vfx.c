#include "render_vfx.h"

#include <math.h>
#include <string.h>

#define VFX_DT (1.0f / 59.7275f)
#define VFX_SKID_LIFE 300u
#define VFX_SKID_FADE 150u

enum
{
    KIND_GLOW,
    KIND_STREAK,
    KIND_PUFF,
    KIND_FLAME
};

/* Mini-turbo tiers: charging white-gold, then blue, orange and purple. Shade
 * k (1-based) tints beneath tier k; shade 4 sits under boost flames. */
static const uint32_t tier_colors[4] = {0xffe2b0u, 0x3c9cffu, 0xff8a1cu, 0xd860ffu};
static const uint32_t shade_colors[PORTABLE_RENDER_VFX_SHADES] = {0x1c68ffu, 0xff6a00u, 0xb030ffu,
                                                                  0xff7a10u};

static uint32_t next_random(PortableRenderVfx *v)
{
    uint32_t x = v->random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->random = x;
    return x;
}

static float uniform(PortableRenderVfx *v, float low, float high)
{
    return low + (high - low) * (float)(next_random(v) >> 8) / 16777216.0f;
}

static float clampf(float x, float low, float high)
{
    return x < low ? low : x > high ? high : x;
}

static void store(uint8_t *pixel, float r, float g, float b)
{
    pixel[0] = (uint8_t)lroundf(clampf(b, 0, 1) * 255);
    pixel[1] = (uint8_t)lroundf(clampf(g, 0, 1) * 255);
    pixel[2] = (uint8_t)lroundf(clampf(r, 0, 1) * 255);
    pixel[3] = 255;
}

/* Texture rows run top to bottom; billboard top is local +Z. */
static float texel_intensity(unsigned kind, unsigned x, unsigned y)
{
    const float u = ((float)x + .5f) / (PORTABLE_RENDER_VFX_TEXTURE / 2.0f) - 1;
    const float w = ((float)y + .5f) / (PORTABLE_RENDER_VFX_TEXTURE / 2.0f) - 1;
    const float r2 = u * u + w * w;
    switch (kind)
    {
    case KIND_GLOW:
        return clampf(expf(-r2 * 3.5f) - expf(-3.5f) + .7f * expf(-r2 * 22), 0, 1);
    case KIND_STREAK:
        return clampf(expf(-u * u * 9) * (1 - w * w) * (1 - w * w) + .5f * expf(-r2 * 30), 0, 1);
    case KIND_PUFF:
    {
        if (r2 >= 1)
            return 0;
        uint32_t h = x * 374761393u + y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        const float noise = (float)(h & 255u) / 255.0f;
        return (1 - r2) * (1 - r2) * (.78f + .22f * noise);
    }
    default:
    {
        /* Flame: a teardrop with its narrow tip at the top. */
        const float along = (w + 1) * .5f;
        const float half = .18f + .7f * sqrtf(along);
        const float body = expf(-(u / half) * (u / half) * 2.5f);
        const float taper = along < .8f ? along / .8f : (1 - along) / .2f;
        return clampf(body * sqrtf(clampf(taper, 0, 1)) * 1.15f, 0, 1);
    }
    }
}

static void prepare(PortableRenderVfx *v)
{
    if (v->prepared)
        return;
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(v->indices, indices, sizeof indices);
    const PortableRenderVertex billboard[4] = {{.position = {-.5, 0, -.5}, .uv = {0, 1}},
                                               {.position = {.5, 0, -.5}, .uv = {1, 1}},
                                               {.position = {.5, 0, .5}, .uv = {1, 0}},
                                               {.position = {-.5, 0, .5}, .uv = {0, 0}}};
    const PortableRenderVertex decal[4] = {{.position = {-1, -1, 0}, .uv = {0, 0}},
                                           {.position = {1, -1, 0}, .uv = {1, 0}},
                                           {.position = {1, 1, 0}, .uv = {1, 1}},
                                           {.position = {-1, 1, 0}, .uv = {0, 1}}};
    memcpy(v->billboard, billboard, sizeof billboard);
    memcpy(v->decal, decal, sizeof decal);
    v->run = (PortableRenderRun){0, 6, 0};
    const unsigned size = PORTABLE_RENDER_VFX_TEXTURE;
    for (unsigned m = 0; m < PORTABLE_RENDER_VFX_MATERIALS; ++m)
    {
        const unsigned shade_index = m - PORTABLE_RENDER_VFX_KINDS;
        const int shade = m >= PORTABLE_RENDER_VFX_KINDS && shade_index < PORTABLE_RENDER_VFX_SHADES;
        const int skid = m >= PORTABLE_RENDER_VFX_KINDS + PORTABLE_RENDER_VFX_SHADES;
        /* Rubber marks darken the road; older levels fade toward white. */
        const float darkness =
            skid ? .36f * (1 - (float)(m - PORTABLE_RENDER_VFX_KINDS - PORTABLE_RENDER_VFX_SHADES) /
                                   PORTABLE_RENDER_VFX_SKID_LEVELS)
                 : 0;
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x)
            {
                uint8_t *pixel = &v->pixels[m][(y * size + x) * 4];
                if (skid)
                {
                    const float u = ((float)x + .5f) / (size / 2.0f) - 1;
                    const float edge = clampf((1 - fabsf(u)) * 3, 0, 1);
                    const float shade = 1 - darkness * edge;
                    store(pixel, shade, shade, shade * 1.02f);
                }
                else if (shade)
                {
                    /* White rim, saturated core: multiplying keeps only the hue. */
                    const uint32_t color = shade_colors[shade_index];
                    const float i = .85f * texel_intensity(KIND_GLOW, x, y);
                    store(pixel, 1 - i * (1 - (float)((color >> 16) & 255u) / 255),
                          1 - i * (1 - (float)((color >> 8) & 255u) / 255),
                          1 - i * (1 - (float)(color & 255u) / 255));
                }
                else
                {
                    const float i = texel_intensity(m, x, y);
                    store(pixel, i, i, i);
                }
            }
        v->materials[m] = (PortableRenderMaterial){
            .argb = UINT32_MAX,
            .flags = 4u | (skid || shade ? PORTABLE_RENDER_MATERIAL_MULTIPLY : PORTABLE_RENDER_MATERIAL_ADDITIVE),
            .texture = {.pixels = v->pixels[m], .width = size, .height = size}};
        v->meshes[m] = (PortableRenderMesh){.vertices = skid ? v->decal : v->billboard,
                                            .indices = v->indices,
                                            .materials = &v->materials[m],
                                            .runs = &v->run,
                                            .vertex_count = 4,
                                            .index_count = 6,
                                            .material_count = 1,
                                            .run_count = 1};
    }
    v->prepared = 1;
}

void portable_render_vfx_reset(PortableRenderVfx *v)
{
    if (!v)
        return;
    v->particle_head = v->particle_count = v->skid_head = v->skid_count = 0;
    memset(v->tracks, 0, sizeof v->tracks);
    v->valid = 0;
}

static PortableRenderVfxParticle *spawn(PortableRenderVfx *v, unsigned kind, unsigned owner,
                                        const double position[3], const double velocity[3],
                                        uint32_t color, float size, unsigned life)
{
    PortableRenderVfxParticle *p = &v->particles[v->particle_head];
    v->particle_head = (v->particle_head + 1u) % PORTABLE_RENDER_VFX_PARTICLES;
    if (v->particle_count < PORTABLE_RENDER_VFX_PARTICLES)
        ++v->particle_count;
    *p = (PortableRenderVfxParticle){.size = size * (v->emit_scale > 0 ? v->emit_scale : 1.0f), .drag = 1, .color = color,
                                     .life = (uint16_t)(life ? life : 1), .kind = (uint8_t)kind,
                                     .owner = (uint8_t)owner, .style = 255u};
    for (unsigned a = 0; a < 3; ++a)
    {
        p->position[a] = (float)position[a];
        p->velocity[a] = (float)velocity[a];
    }
    p->axis[2] = 1;
    return p;
}

static void set_axis(PortableRenderVfxParticle *p, const double axis[3])
{
    const double length = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (length < 1e-6)
        return;
    for (unsigned a = 0; a < 3; ++a)
        p->axis[a] = (float)(axis[a] / length);
}

/* A particle drawn with the art set's flipbook for slot, or NULL when the set
 * has no art for it (the effect is then not drawn at all). kind picks the
 * fade curve; argb's alpha scales the art's opacity. */
static PortableRenderVfxParticle *spawn_art(PortableRenderVfx *v, unsigned slot, unsigned kind, unsigned owner,
                                            const double position[3], const double velocity[3], uint32_t argb,
                                            float size, unsigned life)
{
    if (!portable_mesh_fx_set_style(v->art, slot))
        return NULL;
    PortableRenderVfxParticle *p = spawn(v, kind, owner, position, velocity, argb, size, life);
    p->style = (uint8_t)slot;
    return p;
}

static void add_skid(PortableRenderVfx *v, unsigned owner, const float from[3], const double to[3],
                     const double normal[3])
{
    double along[3], length = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        along[a] = to[a] - from[a];
        length += along[a] * along[a];
    }
    length = sqrt(length);
    if (length < .02 || length > 2.5)
        return;
    PortableRenderVfxSkid *s = &v->skids[v->skid_head];
    v->skid_head = (v->skid_head + 1u) % PORTABLE_RENDER_VFX_SKIDS;
    if (v->skid_count < PORTABLE_RENDER_VFX_SKIDS)
        ++v->skid_count;
    for (unsigned a = 0; a < 3; ++a)
    {
        s->center[a] = (float)((from[a] + to[a]) * .5 + normal[a] * .015);
        s->along[a] = (float)(along[a] / length);
        s->normal[a] = (float)normal[a];
    }
    s->half_length = (float)(length * .5 + .004);
    s->half_width = .09f;
    s->tick = v->tick;
    s->owner = (uint8_t)owner;
}

/* The platform's own effects from its art set: drift sparks in its tier
 * colours, exhaust flames, drift smoke, rough-ground dust and landing puffs.
 * Effects the set has no art for are left out; wind streaks stay ours. */
static void emit_art(PortableRenderVfx *v, unsigned owner, const PortableRenderVfxKart *k,
                     const PortableRenderVfxTrack *t, const double wheels[2][3], const double tails[2][3],
                     const double exhaust[3], const double right[3], int drifting, unsigned tier)
{
    const PortableFxSet *rows = portable_mesh_fx_set_rows(v->art);
    const double *up = k->foot.up, *forward = k->foot.forward, *foot = k->foot.position;
    const double r = k->radius;
    double p[3], vel[3];
    const PortableMeshFxStyle *spark_style = portable_mesh_fx_set_style(v->art, PORTABLE_FX_STYLE_SPARK);
    const int held = spark_style && (spark_style->flags & PORTABLE_FX_STYLE_WHEEL);
    if (v->flags & PORTABLE_RENDER_VFX_SPARKS)
    {
        if (drifting && held)
        {
            if (!(v->tick % 6u))
            {
                const unsigned w = k->drift_direction > 0 ? 0u : 1u;
                (void)spawn_art(v, PORTABLE_FX_STYLE_SPARK, KIND_GLOW, owner, tails[w], k->velocity,
                                rows->tier_argb[tier], .35f, 6);
            }
        }
        else if (drifting)
            for (unsigned w = 0; w < 2; ++w)
            {
                if (tier)
                    (void)spawn_art(v, PORTABLE_FX_STYLE_GLOW, KIND_GLOW, owner, tails[w], k->velocity,
                                    rows->tier_argb[tier], .7f + .12f * tier, 1);
                const unsigned sparks = tier ? 2u : 1u;
                for (unsigned i = 0; i < sparks; ++i)
                {
                    const double side = w ? 1 : -1;
                    const double back = uniform(v, 1.5, 3.5), out = uniform(v, .3, 1.8), lift = uniform(v, 1, 2.6);
                    for (unsigned a = 0; a < 3; ++a)
                        vel[a] = .9 * k->velocity[a] - back * forward[a] + side * out * right[a] + lift * up[a];
                    PortableRenderVfxParticle *spark = spawn_art(v, PORTABLE_FX_STYLE_SPARK, KIND_GLOW, owner,
                        tails[w], vel, rows->tier_argb[tier], tier ? uniform(v, .3f, .42f) : uniform(v, .2f, .28f),
                        (unsigned)uniform(v, 8, 14));
                    if (spark)
                    {
                        spark->gravity = 9;
                        spark->drag = .95f;
                    }
                }
            }
        if (k->boosting && !t->boosting && t->drifting && t->drift_tier)
        {
            const uint32_t color = rows->tier_argb[t->drift_tier > 3 ? 3 : t->drift_tier];
            PortableRenderVfxParticle *flash = spawn_art(v, PORTABLE_FX_STYLE_GLOW, KIND_GLOW, owner, exhaust,
                                                         k->velocity, color, 1.4f, 8);
            if (flash)
                flash->grow = -.6f;
            for (unsigned i = 0; i < 12; ++i)
            {
                const float angle = (float)i * .5235988f + uniform(v, -.1f, .1f);
                const double speed = uniform(v, 2.5, 5.5);
                for (unsigned a = 0; a < 3; ++a)
                    vel[a] = k->velocity[a] + speed * (cosf(angle) * right[a] +
                                                       (.4 + .6 * fabsf(sinf(angle))) * up[a] - .6 * forward[a]);
                PortableRenderVfxParticle *spark = spawn_art(v, PORTABLE_FX_STYLE_SPARK, KIND_GLOW, owner, exhaust,
                                                             vel, color, .32f, (unsigned)uniform(v, 10, 16));
                if (spark)
                {
                    spark->gravity = 8;
                    spark->drag = .95f;
                }
            }
        }
    }
    if ((v->flags & PORTABLE_RENDER_VFX_BOOST) && k->boosting)
    {
        for (unsigned pipe = 0; pipe < 2; ++pipe)
        {
            for (unsigned a = 0; a < 3; ++a)
            {
                p[a] = exhaust[a] + (pipe ? .38 : -.38) * r * right[a] + uniform(v, -.03f, .03f) * right[a];
                vel[a] = .85 * k->velocity[a] - uniform(v, 1, 2) * forward[a] + uniform(v, .3, .8) * up[a];
            }
            PortableRenderVfxParticle *flame = spawn_art(v, PORTABLE_FX_STYLE_FLAME, KIND_GLOW, owner, p, vel,
                rows->flame_argb[(v->tick + pipe) & 1u], uniform(v, .42f, .54f), (unsigned)uniform(v, 5, 8));
            if (flame)
                flame->grow = -.35f;
        }
        (void)spawn_art(v, PORTABLE_FX_STYLE_GLOW, KIND_GLOW, owner, exhaust, k->velocity, rows->flame_argb[0],
                        1.2f, 1);
    }
    if ((v->flags & PORTABLE_RENDER_VFX_BOOST) && (k->boosting || k->drafting) && k->own_views)
        for (unsigned i = 0; i < 2; ++i)
        {
            const double side = (next_random(v) & 1u) ? 1 : -1;
            const double ahead = uniform(v, 5, 12), out = uniform(v, 1.3f, 3.4f), high = uniform(v, .2f, 2.6f);
            const double still[3] = {0, 0, 0};
            for (unsigned a = 0; a < 3; ++a)
                p[a] = foot[a] + ahead * forward[a] + side * out * right[a] + high * up[a];
            PortableRenderVfxParticle *wind = spawn(v, KIND_STREAK, owner, p, still, 0x9aa6b4u, .035f, 12);
            set_axis(wind, forward);
            wind->stretch = 2.2f;
            wind->local = 1;
        }
    if (v->flags & PORTABLE_RENDER_VFX_TRAILS)
    {
        if (drifting && (v->tick & 1u))
        {
            const unsigned w = (v->tick >> 1) & 1u;
            for (unsigned a = 0; a < 3; ++a)
                vel[a] = .25 * k->velocity[a] + uniform(v, .4f, 1.1f) * up[a] + uniform(v, -.4f, .4f) * right[a];
            PortableRenderVfxParticle *smoke = spawn_art(v, PORTABLE_FX_STYLE_SMOKE, KIND_PUFF, owner, wheels[w],
                                                         vel, rows->smoke_argb, .45f, (unsigned)uniform(v, 18, 26));
            if (smoke)
            {
                smoke->grow = 1.6f;
                smoke->drag = .92f;
                smoke->gravity = -.4f;
            }
        }
        if (k->wet && k->grounded && k->speed_ratio > .2 && portable_mesh_fx_set_style(v->art, PORTABLE_FX_STYLE_SPLASH))
            for (unsigned w = 0; w < 2u; ++w)
            {
                for (unsigned a = 0; a < 3; ++a)
                    vel[a] = .35 * k->velocity[a] + uniform(v, 1.5f, 3) * up[a] +
                             (w ? 1 : -1) * uniform(v, .3f, 1.2f) * right[a];
                PortableRenderVfxParticle *spray = spawn_art(v, PORTABLE_FX_STYLE_SPLASH, KIND_PUFF, owner,
                                                             wheels[w], vel, 0xE0FFFFFFu, .3f,
                                                             (unsigned)uniform(v, 14, 20));
                if (spray)
                {
                    spray->grow = .4f;
                    spray->gravity = 9.8f;
                }
            }
        else if (k->rough && k->grounded && k->speed_ratio > .2 && (next_random(v) & 3u) != 0)
        {
            const unsigned w = next_random(v) & 1u;
            for (unsigned a = 0; a < 3; ++a)
                vel[a] = .15 * k->velocity[a] + uniform(v, .5f, 1.5f) * up[a] - forward[a];
            const unsigned slot = portable_mesh_fx_set_style(v->art, PORTABLE_FX_STYLE_DUST) ? PORTABLE_FX_STYLE_DUST
                                                                                             : PORTABLE_FX_STYLE_SMOKE;
            PortableRenderVfxParticle *dust = spawn_art(v, slot, KIND_PUFF, owner, wheels[w], vel, rows->dust_argb,
                                                        .42f, (unsigned)uniform(v, 16, 24));
            if (dust)
            {
                dust->grow = 1.8f;
                dust->drag = .9f;
                dust->gravity = 1;
            }
        }
        if (k->grounded && t->air_ticks > 12)
            for (unsigned i = 0; i < 8; ++i)
            {
                const float angle = (float)i * .7853982f;
                for (unsigned a = 0; a < 3; ++a)
                {
                    const double out = cosf(angle) * right[a] + sinf(angle) * forward[a];
                    p[a] = foot[a] + .6 * r * out + .05 * up[a];
                    vel[a] = .5 * k->velocity[a] + uniform(v, 1.5f, 3) * out + .6 * up[a];
                }
                PortableRenderVfxParticle *puff = spawn_art(v, PORTABLE_FX_STYLE_SMOKE, KIND_PUFF, owner, p, vel,
                                                            rows->dust_argb, .4f, 18);
                if (puff)
                {
                    puff->grow = 1.4f;
                    puff->drag = .88f;
                }
            }
    }
}

static void emit(PortableRenderVfx *v, unsigned owner, const PortableRenderVfxKart *k,
                 PortableRenderVfxTrack *t)
{
    const double *up = k->foot.up, *forward = k->foot.forward, *foot = k->foot.position;
    const double right[3] = {up[1] * forward[2] - up[2] * forward[1],
                             up[2] * forward[0] - up[0] * forward[2],
                             up[0] * forward[1] - up[1] * forward[0]};
    const double r = k->radius;
    v->emit_scale = k->scale > 0 ? (float)k->scale : 1.0f;
    double wheels[2][3], tails[2][3], exhaust[3], p[3], vel[3];
    for (unsigned a = 0; a < 3; ++a)
    {
        for (unsigned w = 0; w < 2; ++w)
        {
            wheels[w][a] = foot[a] - .8 * r * forward[a] + (w ? .95 : -.95) * r * right[a] +
                           .05 * up[a];
            /* Sparks leave the back of the tyre, clear of the body. */
            tails[w][a] = wheels[w][a] - .35 * r * forward[a] + .12 * up[a];
        }
        exhaust[a] = foot[a] - 1.05 * r * forward[a] + .45 * r * up[a];
    }
    const int drifting = k->drift_direction && k->grounded && k->speed_ratio > .25;
    const unsigned tier = k->drift_tier > 3 ? 3u : k->drift_tier;
    /* A set without particle styles (shadows only) keeps the generated art. */
    if (v->art && portable_mesh_fx_set_rows(v->art)->style_count)
    {
        emit_art(v, owner, k, t, (const double(*)[3])wheels, (const double(*)[3])tails, exhaust, right, drifting,
                 tier);
        goto track;
    }
    if (v->flags & PORTABLE_RENDER_VFX_SPARKS)
    {
        if (drifting)
            for (unsigned w = 0; w < 2; ++w)
            {
                if (tier)
                {
                    /* Wheel flare that pulses while the charge holds. */
                    const float pulse = .85f + .15f * sinf((float)(v->tick + w * 7u) * 1.3f);
                    PortableRenderVfxParticle *flare = spawn(v, KIND_GLOW, owner, tails[w],
                        k->velocity, tier_colors[tier], (.8f + .16f * tier) * pulse, 1);
                    flare->shade = (uint8_t)tier;
                }
                const unsigned sparks = tier ? 3u + (tier > 1) : (next_random(v) & 1u);
                for (unsigned i = 0; i < sparks; ++i)
                {
                    const double side = w ? 1 : -1;
                    double spray[3];
                    const double back = uniform(v, 2, 4.5), out = uniform(v, .4, 2.6),
                                 lift = uniform(v, 1.4, 3.6);
                    for (unsigned a = 0; a < 3; ++a)
                    {
                        spray[a] = -back * forward[a] + side * out * right[a] + lift * up[a];
                        vel[a] = .9 * k->velocity[a] + spray[a];
                    }
                    PortableRenderVfxParticle *spark = spawn(v, KIND_STREAK, owner, tails[w], vel,
                        tier ? tier_colors[tier] : 0xa08860u, tier ? .1f : .06f,
                        (unsigned)uniform(v, 8, 15));
                    set_axis(spark, spray);
                    spark->stretch = tier ? .45f : .25f;
                    spark->gravity = 14;
                    spark->drag = .96f;
                }
            }
        if (k->boosting && !t->boosting && t->drifting && t->drift_tier)
        {
            /* Mini-turbo release: a flash and a ring of tier-colored sparks. */
            const uint32_t color = tier_colors[t->drift_tier > 3 ? 3 : t->drift_tier];
            PortableRenderVfxParticle *flash = spawn(v, KIND_GLOW, owner, exhaust, k->velocity,
                                                     color, 1.6f, 8);
            flash->grow = -.6f;
            flash->shade = t->drift_tier > 3 ? 3 : t->drift_tier;
            for (unsigned i = 0; i < 20; ++i)
            {
                const float angle = (float)i * .314159f + uniform(v, -.1f, .1f);
                double spray[3];
                const double speed = uniform(v, 3, 7);
                for (unsigned a = 0; a < 3; ++a)
                {
                    spray[a] = speed * (cosf(angle) * right[a] + (.4 + .6 * fabsf(sinf(angle))) * up[a] -
                                        .6 * forward[a]);
                    vel[a] = k->velocity[a] + spray[a];
                }
                PortableRenderVfxParticle *spark = spawn(v, KIND_STREAK, owner, exhaust, vel, color,
                                                         .09f, (unsigned)uniform(v, 10, 18));
                set_axis(spark, spray);
                spark->stretch = .35f;
                spark->gravity = 9;
                spark->drag = .95f;
            }
        }
    }
    if ((v->flags & PORTABLE_RENDER_VFX_BOOST) && k->boosting)
    {
        /* Twin exhaust jets lean up and back so the chase camera sees their
         * length instead of looking straight down the flame axis. */
        const float pulse = .85f + .15f * sinf((float)v->tick * 1.7f);
        for (unsigned pipe = 0; pipe < 2; ++pipe)
        {
            double axis[3], pipe_at[3];
            for (unsigned a = 0; a < 3; ++a)
            {
                pipe_at[a] = exhaust[a] + (pipe ? .38 : -.38) * r * right[a];
                axis[a] = up[a] - .55 * forward[a];
            }
            for (unsigned i = 0; i < 2; ++i)
            {
                const double back = uniform(v, 1.5, 3), lift = uniform(v, .6, 1.4);
                const double jitter = uniform(v, -.4f, .4f);
                for (unsigned a = 0; a < 3; ++a)
                {
                    p[a] = pipe_at[a] + uniform(v, -.03f, .03f) * right[a];
                    vel[a] = .85 * k->velocity[a] - back * forward[a] + lift * up[a] +
                             jitter * right[a];
                }
                PortableRenderVfxParticle *flame =
                    spawn(v, KIND_FLAME, owner, p, vel, i ? 0xff8a20u : 0xffd868u,
                          uniform(v, .38f, .52f) * pulse, (unsigned)uniform(v, 6, 10));
                set_axis(flame, axis);
                flame->stretch = flame->size * 2.1f;
                flame->shade = (uint8_t)(i ? 4 : 0);
                flame->grow = -.45f;
                flame->gravity = -2;
            }
            PortableRenderVfxParticle *core =
                spawn(v, KIND_GLOW, owner, pipe_at, k->velocity, 0xffa040u, .62f * pulse, 1);
            core->shade = 4;
        }
        /* Soft halo that lights the road behind the kart. */
        spawn(v, KIND_GLOW, owner, exhaust, k->velocity, 0x6a2c0cu, 1.9f * pulse, 1);
        if (!t->boosting)
        {
            PortableRenderVfxParticle *flash = spawn(v, KIND_GLOW, owner, exhaust, k->velocity,
                                                     0xffb050u, 1.5f, 7);
            flash->grow = -.5f;
            flash->shade = 4;
        }
    }
    if ((v->flags & PORTABLE_RENDER_VFX_BOOST) && (k->boosting || k->drafting) && k->own_views)
        for (unsigned i = 0; i < 2; ++i)
        {
            /* Wind streaks pinned in the world rush past the camera. */
            const double side = (next_random(v) & 1u) ? 1 : -1;
            const double ahead = uniform(v, 5, 12), out = uniform(v, 1.3f, 3.4f),
                         high = uniform(v, .2f, 2.6f);
            const double still[3] = {0, 0, 0};
            for (unsigned a = 0; a < 3; ++a)
                p[a] = foot[a] + ahead * forward[a] + side * out * right[a] + high * up[a];
            PortableRenderVfxParticle *wind =
                spawn(v, KIND_STREAK, owner, p, still, 0x9aa6b4u, .035f, 12);
            set_axis(wind, forward);
            wind->stretch = 2.2f;
            wind->local = 1;
        }
    if (v->flags & PORTABLE_RENDER_VFX_TRAILS)
    {
        if (drifting)
        {
            const unsigned w = v->tick & 1u;
            for (unsigned a = 0; a < 3; ++a)
                vel[a] = .25 * k->velocity[a] + uniform(v, .4f, 1.1f) * up[a] +
                         uniform(v, -.4f, .4f) * right[a];
            PortableRenderVfxParticle *smoke = spawn(v, KIND_PUFF, owner, wheels[w], vel, 0x5c5c5cu,
                                                     .34f, (unsigned)uniform(v, 20, 30));
            smoke->grow = 2.6f;
            smoke->drag = .92f;
            smoke->gravity = -.4f;
            if (t->wheels_valid && t->drifting && !k->rough)
                for (unsigned i = 0; i < 2; ++i)
                    add_skid(v, owner, t->wheels[i], wheels[i], up);
        }
        if (k->rough && k->grounded && k->speed_ratio > .2 && (next_random(v) & 3u) != 0)
        {
            const unsigned w = next_random(v) & 1u;
            for (unsigned a = 0; a < 3; ++a)
                vel[a] = .15 * k->velocity[a] + uniform(v, .5f, 1.5f) * up[a] - forward[a];
            PortableRenderVfxParticle *dust = spawn(v, KIND_PUFF, owner, wheels[w], vel, 0x483c2au,
                                                    .32f, (unsigned)uniform(v, 18, 26));
            dust->grow = 2.6f;
            dust->drag = .9f;
            dust->gravity = 1;
        }
        if (k->grounded && t->air_ticks > 12)
            for (unsigned i = 0; i < 10; ++i)
            {
                const float angle = (float)i * .6283185f;
                for (unsigned a = 0; a < 3; ++a)
                {
                    const double out = cosf(angle) * right[a] + sinf(angle) * forward[a];
                    p[a] = foot[a] + .6 * r * out + .05 * up[a];
                    vel[a] = .5 * k->velocity[a] + uniform(v, 1.5f, 3) * out + .6 * up[a];
                }
                PortableRenderVfxParticle *puff = spawn(v, KIND_PUFF, owner, p, vel, 0x3c3c3cu,
                                                        .3f, 18);
                puff->grow = 1.8f;
                puff->drag = .88f;
            }
    }
track:
    for (unsigned w = 0; w < 2; ++w)
        for (unsigned a = 0; a < 3; ++a)
            t->wheels[w][a] = (float)wheels[w][a];
    t->wheels_valid = 1;
    t->drifting = (uint8_t)drifting;
    t->drift_tier = (uint8_t)tier;
    t->boosting = k->boosting;
    t->air_ticks = k->grounded ? 0 : (uint16_t)(t->air_ticks < 65535u ? t->air_ticks + 1u : 65535u);
    t->hidden_views = k->hidden_views & 15u;
    t->own_views = k->own_views & 15u;
}

void portable_render_vfx_set_art(PortableRenderVfx *v, const PortableMeshFxSet *art)
{
    if (!v || v->art == art)
        return;
    v->art = art;
    portable_render_vfx_reset(v);
}

unsigned portable_render_vfx_emit(PortableRenderVfx *v, const PortableFxEmitter *e, const PortableRenderPose *at,
                                  uint32_t salt)
{
    if (!v || !v->valid || !e || !at || !portable_mesh_fx_set_style(v->art, e->slot) || (v->tick + salt) % e->interval)
        return 0;
    const double *up = at->up, *forward = at->forward;
    const double right[3] = {forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2],
                             forward[0] * up[1] - forward[1] * up[0]};
    const uint32_t saved = v->random;
    v->random ^= salt * 2246822519u;
    if (!v->random)
        v->random = 1;
    const double spread = e->spread_q16 / 65536.0, radius = e->radius_q16 / 65536.0;
    unsigned spawned = 0;
    for (uint32_t i = 0; i < e->burst; ++i)
    {
        double position[3], velocity[3];
        const double dx = uniform(v, -1, 1) * radius, dy = uniform(v, -1, 1) * radius;
        const double sx = uniform(v, -1, 1) * spread, sy = uniform(v, -1, 1) * spread,
                     sz = uniform(v, -1, 1) * spread;
        for (unsigned a = 0; a < 3; ++a)
        {
            position[a] = at->position[a] + dx * right[a] + dy * forward[a];
            velocity[a] = (e->velocity_q16[0] / 65536.0 + sx) * right[a] +
                          (e->velocity_q16[1] / 65536.0 + sy) * forward[a] +
                          (e->velocity_q16[2] / 65536.0 + sz) * up[a];
        }
        PortableRenderVfxParticle *p = spawn_art(v, e->slot, KIND_PUFF, PORTABLE_RENDER_VFX_KARTS, position,
                                                 velocity, e->argb, (float)(e->size_q16 / 65536.0), e->life);
        if (!p)
            break;
        p->grow = (float)(e->grow_q16 / 65536.0);
        p->gravity = (float)(e->gravity_q16 / 65536.0);
        ++spawned;
    }
    v->random = saved ^ (salt + 1u);
    if (!v->random)
        v->random = 1;
    return spawned;
}

int portable_render_vfx_step(PortableRenderVfx *v, uint32_t tick, uint32_t flags, int cut,
                             const PortableRenderVfxKart *karts, unsigned count)
{
    if (!v || (count && !karts))
        return 0;
    prepare(v);
    if (v->valid && !cut && tick == v->tick)
        return 1;
    if (!v->valid || cut || tick != v->tick + 1u)
        portable_render_vfx_reset(v);
    v->tick = tick;
    v->valid = 1;
    v->flags = flags;
    v->random = tick * 2654435761u ^ 0x9e3779b9u;
    if (!v->random)
        v->random = 1;
    for (unsigned i = 0; i < v->particle_count; ++i)
    {
        PortableRenderVfxParticle *p = &v->particles[i];
        if (p->age >= p->life)
            continue;
        ++p->age;
        p->velocity[2] -= p->gravity * VFX_DT;
        for (unsigned a = 0; a < 3; ++a)
        {
            p->velocity[a] *= p->drag;
            p->position[a] += p->velocity[a] * VFX_DT;
        }
    }
    if (count > PORTABLE_RENDER_VFX_KARTS)
        count = PORTABLE_RENDER_VFX_KARTS;
    for (unsigned i = 0; i < count; ++i)
    {
        PortableRenderVfxTrack *t = &v->tracks[i];
        const PortableRenderVfxKart *k = &karts[i];
        if (!k->active || !isfinite(k->radius) || k->radius <= 0 || !isfinite(k->speed_ratio) ||
            !isfinite(k->scale) || k->scale < 0 || k->scale > 4)
        {
            memset(t, 0, sizeof *t);
            continue;
        }
        emit(v, i, k, t);
    }
    v->emit_scale = 1;
    return 1;
}

static uint32_t scaled(uint32_t color, float intensity)
{
    intensity = clampf(intensity, 0, 1);
    uint32_t out = UINT32_C(0xff000000);
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= (uint32_t)lroundf((float)((color >> shift) & 255u) * intensity) << shift;
    return out;
}

size_t portable_render_vfx_append(PortableRenderVfx *v, uint32_t alpha_q16,
                                  const PortableRenderView *views, size_t view_count,
                                  PortableRenderInstance *instances, size_t capacity)
{
    if (!v || !v->valid || !v->prepared || !instances || (view_count && !views))
        return 0;
    const float alpha = (float)(alpha_q16 > 65536u ? 65536u : alpha_q16) / 65536.0f;
    const float rewind = (1 - alpha) * VFX_DT;
    size_t used = 0;
    for (unsigned i = 0; i < v->skid_count && used < capacity; ++i)
    {
        const PortableRenderVfxSkid *s = &v->skids[i];
        const uint32_t age = v->tick - s->tick;
        const uint32_t hidden = v->tracks[s->owner].hidden_views;
        if (age >= VFX_SKID_LIFE || hidden == 15u)
            continue;
        unsigned level = 0;
        if (age >= VFX_SKID_LIFE - VFX_SKID_FADE)
            level = 1u + (age - (VFX_SKID_LIFE - VFX_SKID_FADE)) * (PORTABLE_RENDER_VFX_SKID_LEVELS - 1u) /
                             VFX_SKID_FADE;
        if (level >= PORTABLE_RENDER_VFX_SKID_LEVELS)
            level = PORTABLE_RENDER_VFX_SKID_LEVELS - 1u;
        PortableRenderInstance *instance = &instances[used++];
        portable_render_instance_identity(
            instance, &v->meshes[PORTABLE_RENDER_VFX_KINDS + PORTABLE_RENDER_VFX_SHADES + level]);
        const float across[3] = {s->normal[1] * s->along[2] - s->normal[2] * s->along[1],
                                 s->normal[2] * s->along[0] - s->normal[0] * s->along[2],
                                 s->normal[0] * s->along[1] - s->normal[1] * s->along[0]};
        for (unsigned a = 0; a < 3; ++a)
        {
            instance->transform[a * 4] = across[a] * s->half_width;
            instance->transform[a * 4 + 1] = s->along[a] * s->half_length;
            instance->transform[a * 4 + 2] = s->normal[a];
            instance->transform[a * 4 + 3] = s->center[a];
        }
        instance->hidden_views = hidden;
    }
    /* Multiply shades first so no glow is darkened by another's tint. */
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < v->particle_count && used < capacity; ++i)
        {
            const PortableRenderVfxParticle *p = &v->particles[i];
            const PortableMeshFxStyle *style =
                p->style != 255u ? portable_mesh_fx_set_style(v->art, p->style) : NULL;
            if (p->age >= p->life || (!pass && (!p->shade || style)) || (p->style != 255u && !style))
                continue;
            const PortableRenderVfxTrack *t = p->owner < PORTABLE_RENDER_VFX_KARTS ? &v->tracks[p->owner] : NULL;
            const uint32_t hidden = t ? (t->hidden_views | (p->local ? 15u & ~t->own_views : 0u)) & 15u : 0u;
            if (hidden == 15u)
                continue;
            const float f = clampf(((float)p->age + alpha) / (float)p->life, 0, 1);
            float intensity;
            if (p->kind == KIND_PUFF)
                intensity = clampf(f * 6, 0, 1) * powf(1 - f, 1.3f);
            else if (p->local)
                intensity = sinf(3.14159265f * f);
            else if (p->life <= 2)
                intensity = 1;
            else
                intensity = powf(1 - f, 1.2f);
            const float size = fmaxf(.02f, p->size * (1 + p->grow * f));
            float position[3];
            for (unsigned a = 0; a < 3; ++a)
                position[a] = p->position[a] - p->velocity[a] * rewind;
            /* Never fill a camera with a puff it is passing through. */
            for (size_t view = 0; view < view_count; ++view)
            {
                float d2 = 0;
                for (unsigned a = 0; a < 3; ++a)
                {
                    const float d = position[a] - (float)views[view].position[a];
                    d2 += d * d;
                }
                const float near = size + .8f;
                if (d2 < (near + 2) * (near + 2))
                    intensity *= clampf((sqrtf(d2) - near) / 2, 0, 1);
            }
            if (intensity <= 0)
                continue;
            PortableRenderInstance *instance = &instances[used++];
            if (style)
            {
                /* Flipbook frames play across the particle's life. */
                unsigned frame = (unsigned)(f * (float)style->frame_count);
                if (frame >= style->frame_count)
                    frame = style->frame_count - 1u;
                portable_render_instance_identity(instance, style->frames[frame]);
                for (unsigned a = 0; a < 3; ++a)
                    instance->transform[a * 4 + 3] = position[a];
                instance->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
                instance->billboard_scale[0] = instance->billboard_scale[1] = instance->billboard_scale[2] = size;
                const float opacity = intensity * (float)(p->color >> 24) / 255.0f;
                instance->tint_argb = style->flags & PORTABLE_FX_STYLE_ADDITIVE
                                          ? scaled(p->color, opacity)
                                          : (uint32_t)lroundf(clampf(opacity, 0, 1) * 255) << 24 | (p->color & 0xffffffu);
                instance->hidden_views = hidden;
                continue;
            }
            portable_render_instance_identity(
                instance, &v->meshes[pass ? p->kind : PORTABLE_RENDER_VFX_KINDS + p->shade - 1u]);
            for (unsigned a = 0; a < 3; ++a)
            {
                instance->transform[a * 4 + 3] = position[a];
                instance->transform[a * 4 + 2] = p->axis[a];
            }
            if (p->kind == KIND_STREAK || p->kind == KIND_FLAME)
            {
                instance->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_UP;
                instance->billboard_scale[0] = instance->billboard_scale[1] = size;
                instance->billboard_scale[2] = p->kind == KIND_FLAME ? fmaxf(.02f, p->stretch * (1 + p->grow * f))
                                                                     : p->stretch;
            }
            else
            {
                instance->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
                instance->billboard_scale[0] = instance->billboard_scale[1] =
                    instance->billboard_scale[2] = size;
            }
            instance->tint_argb = pass ? scaled(p->color, intensity) : UINT32_MAX;
            instance->hidden_views = hidden;
        }
    return used;
}

void portable_render_vfx_counts(const PortableRenderVfx *v, unsigned *particles, unsigned *skids)
{
    unsigned live = 0, marks = 0;
    if (v && v->valid)
    {
        for (unsigned i = 0; i < v->particle_count; ++i)
            live += v->particles[i].age < v->particles[i].life;
        for (unsigned i = 0; i < v->skid_count; ++i)
            marks += v->tick - v->skids[i].tick < VFX_SKID_LIFE;
    }
    if (particles)
        *particles = live;
    if (skids)
        *skids = marks;
}
