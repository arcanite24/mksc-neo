#ifndef PORTABLE_RENDER_VFX_H
#define PORTABLE_RENDER_VFX_H

#include "render_pose.h"
#include "render_scene.h"
#include "mesh_fx_set.h"

/* Presentation-only kart effects for 3D courses: glowing drift sparks,
 * mini-turbo bursts, boost flames, wind streaks, tyre smoke, dust and skid
 * marks. The pool advances once per accepted tick from kart snapshots and is
 * read at draw time; nothing flows back into the simulation. The same input
 * sequence always produces the same particles. */
enum
{
    PORTABLE_RENDER_VFX_SPARKS = 1u, /* Drift sparks and mini-turbo bursts. */
    PORTABLE_RENDER_VFX_BOOST = 2u,  /* Boost flames and wind streaks. */
    PORTABLE_RENDER_VFX_TRAILS = 4u, /* Skid marks, tyre smoke and dust. */
    PORTABLE_RENDER_VFX_KARTS = 8,
    PORTABLE_RENDER_VFX_PARTICLES = 768,
    PORTABLE_RENDER_VFX_SKIDS = 384,
    PORTABLE_RENDER_VFX_KINDS = 4,
    PORTABLE_RENDER_VFX_SHADES = 4, /* Multiply tints under colored glows. */
    PORTABLE_RENDER_VFX_SKID_LEVELS = 4,
    PORTABLE_RENDER_VFX_MATERIALS =
        PORTABLE_RENDER_VFX_KINDS + PORTABLE_RENDER_VFX_SHADES + PORTABLE_RENDER_VFX_SKID_LEVELS,
    PORTABLE_RENDER_VFX_TEXTURE = 32
};

typedef struct
{
    PortableRenderPose foot; /* Support foot: road contact, unit up and forward. */
    double velocity[3];      /* Metres per second. */
    double radius;           /* Kart collision radius in metres. */
    double speed_ratio;      /* |speed| over the authored top speed, 0..2. */
    double scale;            /* Drawn kart scale for particle sizes; 0 is 1. */
    uint32_t hidden_views;   /* Views that hide this kart and its effects. */
    uint32_t own_views;      /* Views driven by this kart: wind streaks show only there. */
    int8_t drift_direction;  /* -1 or +1 while drifting. */
    uint8_t active, grounded, drift_tier, boosting, rough;
    uint8_t wet; /* on a splashing floor (SURF mksc:splash): wheel spray instead of dust */
    uint8_t drafting; /* Charging a slipstream: wind streaks without flames. */
} PortableRenderVfxKart;

typedef struct
{
    float position[3], velocity[3], axis[3];
    float size, grow, stretch, gravity, drag;
    uint32_t color; /* Additive RGB at full intensity. */
    uint16_t age, life;
    uint8_t kind, owner;
    uint8_t local; /* Only the owner's own views. */
    uint8_t shade; /* 1-based multiply tint drawn beneath, keeping hue on bright ground. */
    uint8_t style; /* PortableFxStyleSlot drawn with the art set's flipbook; 255: generated art */
} PortableRenderVfxParticle;

typedef struct
{
    float center[3], along[3], normal[3];
    float half_length, half_width;
    uint32_t tick;
    uint8_t owner;
} PortableRenderVfxSkid;

typedef struct
{
    float wheels[2][3];
    uint32_t hidden_views, own_views;
    uint16_t air_ticks;
    uint8_t wheels_valid, drifting, drift_tier, boosting;
} PortableRenderVfxTrack;

typedef struct
{
    PortableRenderVfxParticle particles[PORTABLE_RENDER_VFX_PARTICLES];
    PortableRenderVfxSkid skids[PORTABLE_RENDER_VFX_SKIDS];
    PortableRenderVfxTrack tracks[PORTABLE_RENDER_VFX_KARTS];
    unsigned particle_head, particle_count, skid_head, skid_count;
    float emit_scale; /* The emitting kart's scale while emit() runs. */
    uint32_t tick, flags, random;
    uint8_t valid, prepared;
    /* Optional platform art (META mksc:fx-set); the host owns it. When it
     * has styles, they and its colours replace the generated look and
     * effects whose style the set lacks are not drawn. */
    const PortableMeshFxSet *art;
    /* Runtime-generated quads and textures; meshes borrow these arrays. */
    PortableRenderVertex billboard[4], decal[4];
    uint32_t indices[6];
    PortableRenderRun run;
    PortableRenderMaterial materials[PORTABLE_RENDER_VFX_MATERIALS];
    PortableRenderMesh meshes[PORTABLE_RENDER_VFX_MATERIALS];
    uint8_t pixels[PORTABLE_RENDER_VFX_MATERIALS][PORTABLE_RENDER_VFX_TEXTURE * PORTABLE_RENDER_VFX_TEXTURE * 4];
} PortableRenderVfx;

/* Selects the platform art for later steps and draws; NULL restores the
 * generated look. Changing it clears the pool. */
void portable_render_vfx_set_art(PortableRenderVfx *vfx, const PortableMeshFxSet *art);
/* Spawns one burst of a set emitter at a placed object (world position,
 * unit up and forward) on this step's tick; the host calls it between
 * portable_render_vfx_step calls. Returns particles spawned. */
unsigned portable_render_vfx_emit(PortableRenderVfx *vfx, const PortableFxEmitter *emitter,
                                  const PortableRenderPose *at, uint32_t salt);
/* Forget every particle and mark; keeps generated art. */
void portable_render_vfx_reset(PortableRenderVfx *vfx);
/* Advance one accepted tick. A repeated tick is ignored; any other jump, or a
 * cut, clears the pool first. flags selects the PORTABLE_RENDER_VFX_* groups.
 * karts beyond PORTABLE_RENDER_VFX_KARTS are ignored. Returns 0 only for
 * invalid arguments, leaving the pool unchanged. */
int portable_render_vfx_step(PortableRenderVfx *vfx, uint32_t tick, uint32_t flags, int cut,
                             const PortableRenderVfxKart *karts, unsigned count);
/* Append instances for the live pool, rewound by the presentation fraction so
 * effects stay attached to interpolated karts. Particles fade out as any view's
 * eye nears them. Stops quietly at capacity. */
size_t portable_render_vfx_append(PortableRenderVfx *vfx, uint32_t alpha_q16,
                                  const PortableRenderView *views, size_t view_count,
                                  PortableRenderInstance *instances, size_t capacity);
/* Live particles and skid marks, for traces and tests. */
void portable_render_vfx_counts(const PortableRenderVfx *vfx, unsigned *particles, unsigned *skids);

#endif
