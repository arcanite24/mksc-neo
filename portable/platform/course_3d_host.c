#include "course_3d_host.h"
#include "course_3d.h"
#include "modern_menu_internal.h"
#include "render_scene.h"
#include "render_gpu.h"
#include "render_gles.h"
#include "render_pose.h"
#include "render_object_animation.h"
#include "render_chase.h"
#include "render_vfx.h"
#include "render_kart_animation.h"
#include "mesh_kart_fx_art.h"
#include "mesh_fx_set.h"
#include "mesh_query.h"
#include "character_view_asset.h"
#include "render_model.h"
#include "mesh_kart_rules_asset.h"
#include "mesh_item_rules_asset.h"
#include "mesh_item_art.h"
#include "mesh_driver_art.h"
#include "mesh_battle_art.h"
#include "mesh_collision_index.h"
#include "mesh_item_scene.h"
#include "mesh_sky_scene.h"
#include "mesh_palette_scene.h"
#include "mesh_time_trial_store.h"
#include "local_players.h"
#include "sdl_gamepad.h"
#include "driver_input.h"
#include "controller_input.h"
#include "recovered_pack.h"
#include "mesh_cup.h"
#include "mesh_cup_store.h"
#include <string.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    uint32_t id;
    const char *name, *cup_name;
    uint32_t modes, sort_key, cup_id, slot, physics_profile_id;
    PortableNativeImage preview; /* META preview picture; zero width without one. */
} PreviewEntry;

typedef struct
{
    PortableRenderInstance instance;
    double scale[3];
    uint8_t billboard, moving;
    unsigned actor_index_plus1, coin_index_plus1, sky_layer_plus1;
    size_t resource; /* its model_resources entry */
    uint32_t property_first, property_count; /* its OBJS properties (mksc:present-in) */
    PortableMeshMotion motion; /* moving: the row's mksc:motion */
    /* mksc:shadow: a decal on the floor below the object, half width and
     * length along its local x and y, gone once the floor drops further
     * than the vanish depth (mksc:shadow-size). */
    PortableMeshShadowDecal *shadow;
    double shadow_size[3];
    uint32_t row_id, route; /* OBJS words 0 and 13 */
    /* mksc:route-window: shown only while (1) or unless (2) a moving object
     * with id `window_follower` on route `window_route` is window_from..to
     * ticks into its cycle */
    uint8_t window;
    uint32_t window_route, window_follower, window_from, window_to;
    struct RacerSkin *skin; /* own copy of a rigged resource's clip, run motion.phase ahead */
} ModelPlacement;

/* A racer's skinned copy of its rigged driver model (see posed_model), or a
 * placed model's looping clip (see posed_scenery). */
typedef struct RacerSkin
{
    const PortableModelBinding *binding;
    const PortableRenderVertex *rest;
    PortableModelRig rig;
    PortableRenderVertex *vertices;
    PortableRenderMesh mesh;
    uint32_t capacity, key, revision;
    double sample;
    uint8_t rigged;
    /* Kart bodies: tire pivots and radii (mesh units) and roll angles. */
    double wheel_center[4][3], wheel_radius[4], wheel_distance, wheel_yaw;
} RacerSkin;

/* A racer's kart (kind 111): the driver binding moved onto its seat and the
 * kart body drawn with the same pose. */
typedef struct
{
    PortableModelBinding driver, body;
    size_t resource; /* kart body */
    double seat[3];  /* mesh units */
    uint8_t present, no_shadow;
} RacerKart;

struct PortableCourse3DHost
{
    const PortableAssetPackView *pack;
    PortableCourse3DView course;
    PortableCourse3DMeta meta;
    PortableMeshCollisionIndex *collision_index;
    PortableRenderResources resources;
    PortableMeshPaletteScene *palette_scene;
    PortableModelCatalog models;
    PortableRenderResources *model_resources;
    uint32_t *model_ids;
    RacerSkin *model_skins; /* per model resource: placed objects' looping clips */
    double *model_flip_z, *model_flip_x; /* Cached local min + max bounds. */
    double (*model_bounds)[6];
    PortableRenderInstance *instances;
    ModelPlacement *placements;
    size_t billboard_count, instance_capacity;
    PortableMeshItemRules item_rules;
    PortableMeshItemArt *item_art;
    PortableMeshBattleArt *battle_art;
    PortableMeshItemScene *item_scene;
    uint32_t item_hud_pixels[16][256];
    uint32_t item_icon_pixels[64 * 64]; /* the platform icon being drawn */
    PortableMeshItemControls item_controls[PORTABLE_MESH_LOCAL_MAX];
    size_t model_count, object_count;
    uint8_t models_prepared;
    const PortableModelBinding *driver_model;
    size_t driver_resource;
    const PortableModelBinding *racer_models[PORTABLE_MESH_RACE_MAX];
    size_t racer_resources[PORTABLE_MESH_RACE_MAX];
    uint32_t chosen_karts[PORTABLE_MESH_LOCAL_MAX]; /* 0: the driver's own */
    uint32_t roster[PORTABLE_DRIVER_3D_CAPACITY]; /* Rival pool; empty: unlimited. */
    size_t roster_count;
    uint32_t race_karts[PORTABLE_MESH_RACE_MAX];    /* resolved; 0 without karts */
    RacerKart racer_karts[PORTABLE_MESH_RACE_MAX];
    RacerSkin skins[PORTABLE_MESH_RACE_MAX], kart_skins[PORTABLE_MESH_RACE_MAX];
    PortableMeshDriverArt *driver_art[PORTABLE_MESH_RACE_MAX];
    uint32_t tumble_mask; /* racers a shell or bomb hit, while their hit spin lasts */
    uint32_t geometry_presence; /* the race the collision index was built for */
    PortableRenderCpu cpu;
    PortableRenderGpu *gpu;
    PortableRenderGles *gles;
    SDL_Renderer *gpu_renderer, *render_renderer;
    void **render_background;
    SDL_Texture *frozen_picture;
    size_t world_width, world_height, freeze_world_width, freeze_world_height;
    uint8_t aid_views;
    PortableRenderVertex kart_vertices[8];
    PortableRenderMaterial kart_material;
    PortableRenderRun kart_run;
    PortableRenderMesh kart_mesh;
    PortableRenderVertex shadow_vertices[4];
    PortableRenderMaterial shadow_materials[8];
    PortableRenderRun shadow_run;
    PortableRenderMesh shadow_meshes[8];
    uint32_t shadow_indices[6];
    uint8_t shadow_pixels[8][64 * 64 * 4];
    uint8_t shadows_prepared;
    uint32_t kart_indices[36];
    int32_t kart_radius;
    uint8_t gpu_attempted, backend_reported, freeze_valid;
    int freeze_width, freeze_height;
    PortableRenderPose previous_pose[PORTABLE_MESH_RACE_MAX], current_pose[PORTABLE_MESH_RACE_MAX];
    PortableRenderChase previous_camera[PORTABLE_MESH_LOCAL_MAX],
        current_camera[PORTABLE_MESH_LOCAL_MAX];
    uint8_t camera_mask, camera_error;
    PortableRenderKartAnimation previous_animation[PORTABLE_MESH_RACE_MAX],
        current_animation[PORTABLE_MESH_RACE_MAX];
    PortableRenderSupport previous_support[PORTABLE_MESH_RACE_MAX], current_support[PORTABLE_MESH_RACE_MAX];
    PortableCharacterViewRules character_rules;
    PortableMeshKartFxArt *kart_fx;
    PortableMeshFxSet *fx_set; /* META mksc:fx-set shadows and particle art */
    uint16_t animation_mask;
    uint8_t animation_error, character_rules_valid;
    PortableRenderVfx vfx;
    PortableMeshCourseActors previous_actors, current_actors;
    PortableMeshItemPose previous_items[PORTABLE_MESH_ITEMS_ACTORS], current_items[PORTABLE_MESH_ITEMS_ACTORS];
    size_t previous_item_count, current_item_count;
    uint32_t world_tick;
    uint8_t world_valid;
    uint32_t presentation_alpha;
    uint8_t pose_valid, pose_ghost, interpolate;
    PreviewEntry *entries;
    size_t count, cursor; /* Full catalog indices, also used by GP cup lookup. */
    size_t *visible;
    size_t visible_count;
    uint32_t tick;
    uint16_t previous;
    uint8_t active, flying, gamepad, wait_for_neutral;
    uint8_t direct; /* Launched from an ordinary cup: ending returns there. */
    uint8_t launch, launch_failed; /* Race start pending for the next tick / refused. */
    double position[3], yaw, pitch;
    PortableSettings settings;
    PortableMeshKartRules rules;
    /* META kart scale and chase framing; rules carry the scaled body. */
    PortableCourse3DChase chase;
    double kart_scale;
    PortableRenderChaseRig chase_rig;
    PortableMeshKartProfile profile; /* rules come from it at physics_class */
    unsigned physics_class;
    PortableMeshKartRules driver_rules[PORTABLE_MESH_RACE_MAX]; /* DRV3-scaled rules */
    PortableMeshTimeTrialSession trial, ghost_trial, replay_trial;
    PortableMeshRaceSession race;
    PortableMeshItems trial_items, ghost_items, replay_items;
    PortableMeshItemsConfig item_config;
    PortableMeshCourseActorsConfig actor_config;
    uint8_t boo_view_sides[64][4];
    uint32_t mechakoopa_view_models[64][4];
    uint32_t kamek_view_models[64][4];
    uint32_t teepee_view_models[64][4];
    PortableMeshCourseActors trial_actors, ghost_actors, replay_actors;
    PortableMeshCourseActorEvents actor_events;
    PortableMeshItemEvents trial_item_events;
    uint32_t item_seed;
    uint8_t item_profile_enabled;
    PortableMeshKartEvents audio_events[PORTABLE_MESH_RACE_MAX];
    uint8_t hazard_reaction[PORTABLE_MESH_RACE_MAX]; /* presentation: the last hazard's PORTABLE_MESH_HAZARD_* */
    uint16_t audio_buttons[PORTABLE_MESH_LOCAL_MAX];
    uint32_t audio_generation, audio_ordinal;
    PortableMeshSoundEmitter sound_emitters[PORTABLE_MESH_SOUND_EMITTER_LIMIT];
    uint32_t sound_emitter_count;
    /* Kind-4 ZONE particle emitters: placed pose and META mksc:fx-set row. */
    PortableRenderPose particle_poses[PORTABLE_MESH_PARTICLE_EMITTER_LIMIT];
    uint32_t particle_emitters[PORTABLE_MESH_PARTICLE_EMITTER_LIMIT], particle_emitter_count;
    uint64_t audio_serial;
    PortableMeshCupSession cup;
    PortableMeshCupIdentity cup_identity;
    char cup_path[4096];
    uint8_t cup_standings_shown, cup_ceremony, ceremony_bounds_valid;
    /* Course intro before the countdown: shot order and tick within it. The
     * race does not tick until it ends or A/Start skips it. */
    uint8_t intro;
    uint32_t intro_shot, intro_tick;
    /* Course light and fog (environment_course: the course they describe). */
    const uint8_t *environment_course;
    PortableRenderEnvironment environment;
    uint32_t course_light_argb, model_ambient, model_emission;
    uint32_t *surface_lights; /* per SURF row: its light colour (ARGB) */
    uint32_t *surface_karts;  /* per SURF row: kart shade, kart add (ARGB), reflect (0/1) */
    uint32_t surface_light_count;
    double ceremony_low[3], ceremony_high[3];
    uint32_t mode, driver_ids[PORTABLE_MESH_RACE_MAX];
    unsigned players, racers, engine_class;
    SDL_Gamepad *pads[PORTABLE_MESH_LOCAL_MAX];
    PortableLocalInput local_inputs[PORTABLE_MESH_LOCAL_MAX];
    uint16_t local_pressed[PORTABLE_MESH_LOCAL_MAX];
    uint64_t local_input_hashes[PORTABLE_MESH_RACE_MAX];
    PortableMeshTimeTrialCompatibility identity;
    PortableMeshReplayPlayback ghost_playback, replay_playback;
    PortableReplayMemoryInput ghost_input, replay_input;
    PortableReplayReader ghost_reader, replay_reader;
    PortableReplayMemoryOutput recording;
    PortableReplayWriter writer;
    uint8_t *ghost_bytes;
    uint32_t *overlay;
    size_t overlay_capacity;
    uint32_t driver_id, last_lap_tick, best_lap_tick, menu_age;
    unsigned action_cursor;
    PortablePauseGraphics graphics; /* Pause menu Graphics page (Select). */
    PortableGraphicsHook graphics_hook;
    uint8_t digest[32], configured, driving, paused, replaying, ghost_active, record_valid;
    char save_directory[4096], ghost_path[4096], best_path[4096];
    char error[256], cup_best_notice[96];
};

static void load_gp_preview_progress(PortableCourse3DHost *host);

/* Collision snapshots own their source-independent callbacks. Session copies
 * borrow this stationary owner; presentation/cache resets do not retire it. */
static void close_geometry(PortableCourse3DHost *host)
{
    portable_mesh_collision_index_close(host->collision_index);
    host->collision_index = NULL;
    host->race.geometry = NULL;
    host->trial.geometry = host->ghost_trial.geometry = host->replay_trial.geometry = NULL;
    host->item_config.geometry = NULL;
}

/* Mode/class-conditional course parts (mksc:present-in) follow the
 * configured race; previews and exploration keep them all. */
static uint32_t race_presence(const PortableCourse3DHost *host)
{
    return portable_course_3d_presence(host->mode, host->engine_class);
}

static int prepare_geometry(PortableCourse3DHost *host)
{
    if (host->collision_index && host->geometry_presence != race_presence(host))
        close_geometry(host);
    if (!host->collision_index)
    {
        host->geometry_presence = race_presence(host);
        host->collision_index = portable_mesh_collision_index_create_present(
            &host->course, host->geometry_presence, host->error, sizeof host->error);
    }
    return host->collision_index != NULL;
}

static const PortableMeshCourse *host_geometry(const PortableCourse3DHost *host)
{
    return portable_mesh_collision_index_course(host->collision_index);
}

static int shared_race(const PortableCourse3DHost *host)
{
    return host->mode == PORTABLE_COURSE_3D_VS || host->mode == PORTABLE_COURSE_3D_QUICK ||
           host->mode == 1u || host->mode == PORTABLE_COURSE_3D_BATTLE;
}
/* The tick moving course objects show: the simulation's, else the viewer's clock. */
static uint32_t motion_tick(const PortableCourse3DHost *host)
{
    if (!host->driving)
        return host->tick;
    return shared_race(host) ? host->race.state.tick
           : host->replaying ? host->replay_trial.state.tick
                             : host->trial.state.tick;
}

/* The collision a kart at `tick` stands on, moving objects posed. */
static const PortableMeshCourse *posed_geometry(const PortableCourse3DHost *host, uint32_t tick,
                                                PortableMeshCollisionPose *storage)
{
    return portable_mesh_collision_index_at(host_geometry(host), tick, storage);
}
static const char *race_name(const PortableCourse3DHost *host)
{
    return host->mode == 1u                          ? "GP"
           : host->mode == PORTABLE_COURSE_3D_QUICK  ? "Quick"
           : host->mode == PORTABLE_COURSE_3D_BATTLE ? "Battle"
                                                     : "VS";
}

static void reset_audio_events(PortableCourse3DHost *host)
{
    host->camera_mask = host->camera_error = 0;
    host->animation_mask = host->animation_error = 0;
    host->world_valid = 0;
    ++host->audio_generation;
    host->audio_serial = 0;
    host->audio_ordinal = 0;
    memset(host->audio_events, 0, sizeof host->audio_events);
    memset(host->audio_buttons, 0, sizeof host->audio_buttons);
    memset(host->item_controls, 0, sizeof host->item_controls);
    memset(&host->trial_item_events, 0, sizeof host->trial_item_events);
    memset(&host->actor_events, 0, sizeof host->actor_events);
}

static void close_local_pads(PortableCourse3DHost *host)
{
    for (unsigned i = 0; i < PORTABLE_MESH_LOCAL_MAX; ++i)
    {
        if (host->pads[i] != NULL)
            SDL_CloseGamepad(host->pads[i]);
        host->pads[i] = NULL;
        host->local_pressed[i] = 0;
    }
}
static void open_local_pads(PortableCourse3DHost *host)
{
    close_local_pads(host);
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    unsigned slots = 0;
    for (int i = 0; i < count && slots < PORTABLE_MESH_LOCAL_MAX; ++i)
        if ((host->pads[slots] = SDL_OpenGamepad(ids[i])) != NULL)
            ++slots;
    SDL_free(ids);
    if (!portable_local_players_assign(host->players, slots, host->local_inputs))
        (void)portable_local_players_assign(2u, slots, host->local_inputs);
}

static uint32_t argb_from_rgba(uint32_t color)
{
    return (color >> 8u) | (color << 24u);
}

static uint32_t rgba_property(const PortableCourse3DView *course, uint32_t first, uint32_t count,
                              const char *key, uint32_t fallback)
{
    PortableCourse3DProperty property;
    return portable_course_3d_property_find(course, first, count, key, &property) &&
                   property.type == PORTABLE_COURSE_3D_PROPERTY_INTEGER
               ? (uint32_t)property.value.integer
               : fallback;
}

/* A META fixed-point property, else the fallback. */
static double fixed_meta(const PortableCourse3DHost *host, const char *key, double fallback)
{
    PortableCourse3DProperty property;
    return portable_course_3d_property_find(&host->course, host->meta.property_first, host->meta.property_count,
                                            key, &property) &&
                   property.type == PORTABLE_COURSE_3D_PROPERTY_FIXED
               ? property.value.fixed_q16 / 65536.0
               : fallback;
}

#define REFLECT_GAP .5 /* metres above the floor (surfaceDistance[2] <= 30 units) */

static uint32_t multiply_argb(uint32_t a, uint32_t b)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 32u; shift += 8u)
        out |= (((a >> shift & 255u) * (b >> shift & 255u) + 127u) / 255u) << shift;
    return out;
}

/* The course's light and fog (ARCHITECTURE "Scene light and fog"): META sun
 * or mksc:light-camera, fog fields with mksc:fog-curve, light colours
 * mksc:light-color-0..3 chosen per SURF row by mksc:light, the SURF kart
 * colours mksc:kart-shade/-add, and the lit model terms
 * mksc:model-ambient/-emission. */
static int course_environment(PortableCourse3DHost *host)
{
    if (host->environment_course == host->course.bytes)
        return 1;
    const PortableCourse3DMeta *meta = &host->meta;
    const uint32_t first = meta->property_first, count = meta->property_count;
    PortableRenderEnvironment *environment = &host->environment;
    memset(environment, 0, sizeof *environment);
    for (unsigned axis = 0; axis < 3u; ++axis)
        environment->light_direction[axis] = -(double)meta->sun_q30[axis] / 1073741824.0;
    PortableCourse3DProperty property;
    if (portable_course_3d_property_find(&host->course, first, count, "mksc:light-camera", &property) &&
        property.type == PORTABLE_COURSE_3D_PROPERTY_INTEGER)
    {
        environment->light_camera = 1u;
        environment->light_camera_turns = property.value.integer / 65536.0;
    }
    uint32_t colors[4];
    for (unsigned i = 0; i < 4u; ++i)
    {
        char key[32];
        snprintf(key, sizeof key, "mksc:light-color-%u", i);
        colors[i] = argb_from_rgba(rgba_property(&host->course, first, count, key, meta->sun_rgba));
    }
    environment->light_argb = colors[0];
    host->course_light_argb = argb_from_rgba(meta->sun_rgba);
    host->model_ambient = rgba_property(&host->course, first, count, "mksc:model-ambient", 0u);
    host->model_ambient = host->model_ambient ? argb_from_rgba(host->model_ambient) : PORTABLE_RENDER_MODEL_AMBIENT;
    host->model_emission = rgba_property(&host->course, first, count, "mksc:model-emission", 0u);
    host->model_emission = host->model_emission ? argb_from_rgba(host->model_emission) : PORTABLE_RENDER_MODEL_EMISSION;
    /* Fog needs META boolean mksc:fog; materials opt in with their own. */
    if (portable_course_3d_property_find(&host->course, first, count, "mksc:fog", &property) &&
        property.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && property.value.boolean)
        environment->fog_argb = argb_from_rgba(meta->fog_rgba);
    environment->fog_near = meta->fog_near_q16 / 65536.0;
    environment->fog_far = meta->fog_far_q16 / 65536.0;
    environment->fog_curve = rgba_property(&host->course, first, count, "mksc:fog-curve", PORTABLE_RENDER_FOG_LINEAR);
    if (environment->fog_curve > PORTABLE_RENDER_FOG_RECIPROCAL)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u META mksc:fog-curve=%u is not 0..3",
                 host->course.id, environment->fog_curve);
        return 0;
    }
    const PortableCourse3DTable *surfaces =
        portable_course_3d_table(&host->course, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'));
    free(host->surface_lights);
    free(host->surface_karts);
    host->surface_light_count = surfaces ? surfaces->count : 0u;
    host->surface_lights = host->surface_light_count ? calloc(host->surface_light_count, sizeof(uint32_t)) : NULL;
    host->surface_karts = host->surface_light_count ? calloc(host->surface_light_count * 3u, sizeof(uint32_t)) : NULL;
    if (host->surface_light_count && (!host->surface_lights || !host->surface_karts))
    {
        host->surface_light_count = 0;
        snprintf(host->error, sizeof host->error, "3D course=%u surface light allocation", host->course.id);
        return 0;
    }
    for (uint32_t i = 0; i < host->surface_light_count; ++i)
    {
        PortableCourse3DSurface surface;
        uint32_t light = 0, shade = UINT32_MAX, add = 0, reflect = 0;
        if (portable_course_3d_surface(&host->course, i, &surface))
        {
            PortableCourse3DProperty flag;
            reflect = portable_course_3d_property_find(&host->course, surface.property_first, surface.property_count,
                                                       "mksc:reflect", &flag) &&
                      flag.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && flag.value.boolean;
            light = rgba_property(&host->course, surface.property_first, surface.property_count, "mksc:light", 0u);
            shade = rgba_property(&host->course, surface.property_first, surface.property_count, "mksc:kart-shade",
                                  UINT32_MAX);
            add = rgba_property(&host->course, surface.property_first, surface.property_count, "mksc:kart-add", 0u);
        }
        host->surface_karts[i * 3u] = argb_from_rgba(shade);
        host->surface_karts[i * 3u + 1u] = argb_from_rgba(add) & UINT32_C(0xffffff);
        host->surface_karts[i * 3u + 2u] = reflect;
        if (light > 3u)
        {
            snprintf(host->error, sizeof host->error, "3D course=%u SURF %u mksc:light=%u is not 0..3",
                     host->course.id, i, light);
            return 0;
        }
        host->surface_lights[i] = colors[light];
    }
    host->environment_course = host->course.bytes;
    return 1;
}

/* Lit model materials take the course's ambient and emission terms. */
static void light_models(PortableCourse3DHost *host)
{
    for (size_t i = 0; i < host->model_count; ++i)
    {
        PortableRenderResources *resource = &host->model_resources[i];
        PortableRenderMaterial *materials = (PortableRenderMaterial *)resource->mesh.materials;
        for (uint32_t m = 0; materials && m < resource->mesh.material_count; ++m)
            if (materials[m].flags & PORTABLE_RENDER_MATERIAL_LIT)
            {
                materials[m].ambient_argb = host->model_ambient;
                materials[m].emission_argb = host->model_emission;
            }
    }
}

static void close_acceleration(PortableCourse3DHost *host)
{
    portable_render_gpu_close(host->gpu);
    portable_render_gles_close(host->gles);
    host->gpu = NULL;
    host->gles = NULL;
    host->frozen_picture = NULL;
    host->world_width = host->world_height = 0u;
    host->freeze_valid = 0;
    host->gpu_attempted = host->backend_reported = 0;
}

/* GPU uploads borrow mesh identities, so retire uploads before decoded owners. */
static void close_models(PortableCourse3DHost *host)
{
    close_acceleration(host);
    for (unsigned i = 0; i < PORTABLE_MESH_RACE_MAX; ++i)
    {
        free(host->skins[i].vertices);
        memset(&host->skins[i], 0, sizeof host->skins[i]);
        free(host->kart_skins[i].vertices);
        memset(&host->kart_skins[i], 0, sizeof host->kart_skins[i]);
    }
    portable_mesh_palette_scene_close(host->palette_scene);
    host->palette_scene = NULL;
    portable_mesh_item_scene_close(host->item_scene);
    portable_mesh_item_art_close(host->item_art);
    portable_mesh_kart_fx_art_close(host->kart_fx);
    host->kart_fx = NULL;
    portable_mesh_fx_set_close(host->fx_set);
    host->fx_set = NULL;
    for (unsigned i = 0; i < PORTABLE_MESH_RACE_MAX; ++i)
    {
        portable_mesh_driver_art_close(host->driver_art[i]);
        host->driver_art[i] = NULL;
    }
    portable_mesh_battle_art_close(host->battle_art);
    host->battle_art = NULL;
    host->item_scene = NULL;
    host->item_art = NULL;
    host->item_profile_enabled = 0;
    host->instance_capacity = 0;
    for (size_t i = 0; i < host->model_count; ++i)
    {
        portable_render_resources_close(&host->model_resources[i]);
        if (host->model_skins) free(host->model_skins[i].vertices);
    }
    free(host->model_resources);
    free(host->model_skins);
    host->model_skins = NULL;
    free(host->model_ids);
    free(host->surface_lights);
    host->surface_lights = NULL;
    free(host->surface_karts);
    host->surface_karts = NULL;
    host->surface_light_count = 0;
    host->environment_course = NULL;
    free(host->model_flip_z);
    free(host->model_flip_x);
    free(host->model_bounds);
    free(host->instances);
    for (size_t i = 0; host->placements && i < host->object_count; ++i)
    {
        if (host->placements[i].skin)
        {
            free(host->placements[i].skin->vertices);
            free(host->placements[i].skin);
        }
        if (host->placements[i].shadow)
        {
            portable_mesh_shadow_decal_close(host->placements[i].shadow);
            free(host->placements[i].shadow);
        }
    }
    free(host->placements);
    portable_model_catalog_close(&host->models);
    host->model_resources = NULL;
    host->model_ids = NULL;
    host->model_flip_z = NULL;
    host->model_flip_x = NULL;
    host->model_bounds = NULL;
    host->instances = NULL;
    host->placements = NULL;
    host->billboard_count = 0;
    host->model_count = host->object_count = 0;
    host->driver_model = NULL;
    for (unsigned i = 0; i < PORTABLE_MESH_RACE_MAX; ++i)
    {
        host->racer_models[i] = NULL;
        host->racer_karts[i].present = 0;
    }
    host->models_prepared = host->freeze_valid = host->ceremony_bounds_valid = 0;
}

static uint32_t model_word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static double model_fixed(const uint8_t *p, double divisor)
{
    const uint32_t word = model_word(p);
    const int64_t value = word <= INT32_MAX ? (int64_t)word : (int64_t)word - INT64_C(4294967296);
    return (double)value / divisor;
}

/* Seats racer's driver binding in its kart and opens the kart body. */
static int attach_kart(PortableCourse3DHost *host, unsigned racer, uint32_t driver_id,
                       const PortableModelBinding *binding)
{
    RacerKart *seated = &host->racer_karts[racer];
    seated->present = 0;
    const uint32_t id = host->race_karts[racer];
    if (!id)
        return 1;
    PortableKart3D kart;
    int32_t seat[3];
    PortableModelBinding body = *binding;
    if (!portable_kart_3d_find(host->pack, id, &kart) || !portable_kart_3d_seat(&kart, driver_id, seat) ||
        !portable_model_mesh_open(host->pack, kart.mesh_id, &body.mesh))
    {
        snprintf(host->error, sizeof host->error, "3D course=%u driver=%u: kart %u has no seat or mesh",
                 host->course.id, driver_id, id);
        return 0;
    }
    body.mesh_id = kart.mesh_id;
    memcpy(body.pivot_q16, kart.pivot_q16, sizeof body.pivot_q16);
    size_t index = 0;
    while (index < host->model_count && host->model_ids[index] != kart.mesh_id)
        ++index;
    if (index == host->model_count)
    {
        if (!portable_render_resources_open_model(&host->model_resources[index], &body, host->pack,
                                                  host->error, sizeof host->error))
            return 0;
        host->model_ids[host->model_count++] = kart.mesh_id;
    }
    seated->driver = *binding;
    seated->body = body;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        seated->driver.pivot_q16[axis] = kart.pivot_q16[axis] - seat[axis];
        seated->seat[axis] = seat[axis] / 65536.0;
    }
    seated->resource = index;
    seated->present = 1;
    seated->no_shadow = (kart.flags & PORTABLE_KART_3D_NO_SHADOW) != 0u;
    host->racer_models[racer] = &seated->driver;
    return 1;
}

/* A placed model whose rig has a looping clip keeps a skinned copy that
 * plays it on the course clock (wheels, paddles). 0 only when out of memory. */
static int open_scenery_skin(PortableCourse3DHost *host, size_t resource, const PortableModelBinding *binding)
{
    RacerSkin *skin = &host->model_skins[resource];
    const PortableRenderMesh *rest = &host->model_resources[resource].mesh;
    memset(skin, 0, sizeof *skin);
    if (!portable_model_rig_open(host->pack, binding->mesh_id, &binding->mesh, &skin->rig))
        return 1;
    const int idle = portable_model_rig_clip(&skin->rig, PORTABLE_MODEL_CLIP_TAG('I', 'D', 'L', 'E'));
    if (idle >= 0 && (skin->rig.clips[idle].flags & PORTABLE_MODEL_CLIP_LOOP))
    {
        skin->rigged = 1;
        skin->key = (uint32_t)idle;
    }
    for (uint32_t clip = 0; !skin->rigged && clip < skin->rig.clip_count; ++clip)
        if (skin->rig.clips[clip].flags & PORTABLE_MODEL_CLIP_LOOP)
        {
            skin->rigged = 1;
            skin->key = clip;
        }
    if (!skin->rigged)
        return 1;
    skin->vertices = malloc(rest->vertex_count * sizeof *skin->vertices);
    if (!skin->vertices)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u model=%u: skin allocation failed",
                 host->course.id, binding->mesh_id);
        return 0;
    }
    skin->capacity = rest->vertex_count;
    skin->mesh = *rest;
    skin->mesh.vertices = skin->vertices;
    skin->sample = -1;
    return 1;
}

/* Kind-4 ZONE particle emitters: their placed poses and effect-set rows. */
static void collect_particle_emitters(PortableCourse3DHost *host)
{
    const PortableCourse3DTable *zones =
        portable_course_3d_table(&host->course, PORTABLE_COURSE_3D_TAG('Z', 'O', 'N', 'E'));
    host->particle_emitter_count = 0;
    for (uint32_t i = 0; zones && i < zones->count &&
                         host->particle_emitter_count < PORTABLE_MESH_PARTICLE_EMITTER_LIMIT; ++i)
    {
        PortableCourse3DEmitterZone zone;
        if (!portable_course_3d_emitter_zone(&host->course, i, &zone))
            continue;
        const double x = zone.quaternion_q30[0] / 1073741824.0, y = zone.quaternion_q30[1] / 1073741824.0,
                     z = zone.quaternion_q30[2] / 1073741824.0, w = zone.quaternion_q30[3] / 1073741824.0;
        PortableRenderPose *pose = &host->particle_poses[host->particle_emitter_count];
        for (unsigned a = 0; a < 3u; ++a)
            pose->position[a] = zone.position_q16[a] / 65536.0;
        /* The rotated local y (forward) and z (up) axes. */
        pose->forward[0] = 2 * (x * y - w * z);
        pose->forward[1] = 1 - 2 * (x * x + z * z);
        pose->forward[2] = 2 * (y * z + w * x);
        pose->up[0] = 2 * (x * z + w * y);
        pose->up[1] = 2 * (y * z - w * x);
        pose->up[2] = 1 - 2 * (x * x + y * y);
        host->particle_emitters[host->particle_emitter_count++] = zone.emitter;
    }
}

/* META asset mksc:fx-set names the kind-116 shadows and particle art. */
static uint32_t fx_set_id(const PortableCourse3DHost *host)
{
    PortableCourse3DProperty property;
    return portable_course_3d_property_find(&host->course, host->meta.property_first, host->meta.property_count,
                                            "mksc:fx-set", &property) &&
                   property.type == PORTABLE_COURSE_3D_PROPERTY_ASSET &&
                   property.value.asset.kind == PORTABLE_ASSET_SECTION_FX_SET
               ? property.value.asset.id
               : 0u;
}

static int prepare_models(PortableCourse3DHost *host)
{
    if (host->models_prepared)
        return 1;
    const PortableCharacterViewRules *character_rules = NULL;
    if (!portable_character_view_asset_load(host->pack, &host->character_rules, &character_rules))
    {
        snprintf(host->error,sizeof host->error,"3D course=%u mode=%u: invalid character view rules",
            host->course.id,host->mode);
        goto failed;
    }
    if (host->driving && !host->kart_fx && !portable_mesh_kart_fx_art_open(host->pack, &host->kart_fx,
        host->error, sizeof host->error)) goto failed;
    if (host->driving && !host->fx_set &&
        !portable_mesh_fx_set_open(host->pack, fx_set_id(host), &host->fx_set, host->error, sizeof host->error))
        goto failed;
    collect_particle_emitters(host);
    host->character_rules_valid = character_rules != NULL;
    const PortableCourse3DTable *objects =
        portable_course_3d_table(&host->course, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
    const size_t count = objects ? objects->count : 0;
    size_t model_capacity = count + 2u * PORTABLE_MESH_RACE_MAX; /* drivers and karts */
    for (unsigned actor = 0; actor < portable_mesh_course_actors_config_count(&host->actor_config); ++actor)
    {
        PortableMeshCourseActorBinding binding;
        if (!portable_mesh_course_actors_binding(&host->actor_config, actor, &binding))
            goto failed;
        model_capacity += binding.frame_count;
    }
    host->model_resources = calloc(model_capacity, sizeof *host->model_resources);
    host->model_ids = calloc(model_capacity, sizeof *host->model_ids);
    host->model_skins = calloc(model_capacity, sizeof *host->model_skins);
    host->model_flip_z = calloc(model_capacity, sizeof *host->model_flip_z);
    host->model_flip_x = calloc(model_capacity, sizeof *host->model_flip_x);
    host->model_bounds = calloc(model_capacity, sizeof *host->model_bounds);
    const PortableCourse3DTable *boxes =
        portable_course_3d_table(&host->course, PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'));
    const size_t item_count =
        host->item_profile_enabled && host->item_art
            ? PORTABLE_MESH_ITEMS_ACTORS + PORTABLE_MESH_ITEM_SCENE_EFFECTS +
                  (host->mode != 2u && boxes ? boxes->count : 0u)
            : 0u;
    host->placements = count ? calloc(count, sizeof *host->placements) : NULL;
    host->object_count = host->placements ? count : 0u; /* close_models frees their skins and shadows */
    if (!host->model_resources || !host->model_ids || !host->model_skins || !host->model_flip_z || !host->model_flip_x || !host->model_bounds || (count && !host->placements) ||
        !portable_model_catalog_open(&host->models, host->pack))
        goto failed;
    host->driver_model =
        portable_model_catalog_find(&host->models, PORTABLE_MODEL_DRIVER, host->driver_id, 0, 0);
    if (host->driver_model)
    {
        if (!portable_render_resources_open_model(&host->model_resources[host->model_count],
                                                  host->driver_model, host->pack, host->error,
                                                  sizeof host->error))
            goto failed;
        host->model_ids[host->model_count] = host->driver_model->mesh_id;
        host->driver_resource = host->model_count++;
    }
    const unsigned racers = shared_race(host) ? host->racers : 1u;
    for (unsigned racer = 0; racer < racers; ++racer)
    {
        const PortableModelBinding *binding = portable_model_catalog_find(
            &host->models, PORTABLE_MODEL_DRIVER, host->driver_ids[racer], 0, 0);
        host->racer_models[racer] = binding;
        if (binding == NULL)
        {
            if (host->driving &&
                !portable_mesh_driver_art_open(
                    host->pack, shared_race(host) ? host->driver_ids[racer] : host->driver_id,
                    &host->driver_art[racer], host->error, sizeof host->error))
                goto failed;
            continue;
        }
        size_t index = 0;
        while (index < host->model_count && host->model_ids[index] != binding->mesh_id)
            ++index;
        if (index == host->model_count)
        {
            if (!portable_render_resources_open_model(&host->model_resources[index], binding,
                                                      host->pack, host->error, sizeof host->error))
                goto failed;
            host->model_ids[host->model_count++] = binding->mesh_id;
        }
        host->racer_resources[racer] = index;
        if (!attach_kart(host, racer, shared_race(host) ? host->driver_ids[racer] : host->driver_id, binding))
            goto failed;
    }
    if (!shared_race(host) && host->racer_karts[0].present)
        host->driver_model = &host->racer_karts[0].driver;
    /* Models and source cells share per-pane steering poses. Reserve bounded
     * wheel sparks/exhaust beside the existing supported shadows. */
    /* Each placed object may also cast one floor shadow. */
    host->instance_capacity = 2u * count + 1u + item_count;
    if (host->driving)
        host->instance_capacity += shared_race(host) ? host->racers : 2u;
    /* Walking actors have independent facing in every camera. */
    const unsigned actor_views = shared_race(host) ? host->players : host->aid_views;
    if (actor_views > 1u)
        for (unsigned layer = 0; layer < 3u; ++layer)
            if (host->actor_config.sky.render_mask & (1u << layer))
                host->instance_capacity += actor_views - 1u;
    if (host->actor_config.sky.enabled && (host->actor_config.sky.palette.enabled ||
        host->actor_config.sky.rules.split_y >= 0))
        host->instance_capacity += actor_views; /* One split back layer per pane. */
    for (unsigned i = 0; i < host->actor_config.mouse_count; ++i)
        if (host->actor_config.initial_mice[i].variant != 0x8001u && actor_views > 1u)
            host->instance_capacity += actor_views - 1u;
    if (actor_views > 1u)
        host->instance_capacity += (host->actor_config.seagull_count + host->actor_config.boo_count +
                                    host->actor_config.falling_star_count + host->actor_config.mechakoopa_count + host->actor_config.kamek_count) * (actor_views - 1u);
    host->instance_capacity += host->actor_config.teepee_count * actor_views;
    if (host->actor_config.pier_boo_count)
        host->instance_capacity += PORTABLE_BOO_ATTACK_CAPACITY;
    if (host->driving && host->mode == PORTABLE_COURSE_3D_BATTLE)
    {
        if (!portable_mesh_battle_art_open(host->pack, &host->battle_art, host->error,
                                           sizeof host->error))
            goto failed;
        host->instance_capacity += host->racers * host->players * 3u;
    }
    if (host->driving)
    {
        /* Per view: kart pose and source-sprite shadow; then sparks/flame. */
        const unsigned views = shared_race(host) ? host->players : host->aid_views;
        /* Per view also a kart body under a model driver. */
        host->instance_capacity += racers * (3u * views + 3u);
        if (!shared_race(host) && host->ghost_active)
            host->instance_capacity += 3u * views + 3u;
    }
    if (host->instance_capacity > PORTABLE_RENDER_INSTANCE_LIMIT)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u: scene instance budget exceeded",
                 host->course.id);
        goto failed;
    }
    if (host->driving)
    {
        /* Kart effects take whatever remains of their budget; the pool simply
         * stops drawing at capacity. */
        const size_t effects = PORTABLE_RENDER_VFX_PARTICLES + PORTABLE_RENDER_VFX_SKIDS;
        const size_t room = PORTABLE_RENDER_INSTANCE_LIMIT - host->instance_capacity;
        host->instance_capacity += effects < room ? effects : room;
    }
    host->instances = calloc(host->instance_capacity, sizeof *host->instances);
    if (!host->instances)
        goto failed;
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t *row = objects->bytes + i * objects->stride;
        PortableCourse3DProperty property;
        const PortableModelBinding *scenery = NULL;
        if (portable_course_3d_property_find(&host->course, model_word(row + 56u),
                                             model_word(row + 60u), "mksc:scenery-art-bank", &property))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                property.value.integer < 0 || property.value.integer > 63)
            {
                snprintf(host->error, sizeof host->error,
                         "3D course=%u object=%u mksc:scenery-art-bank requires integer 0..63",
                         host->course.id, (unsigned)i);
                goto failed;
            }
            const unsigned bank = (unsigned)property.value.integer;
            int controlled = portable_course_3d_property_find(&host->course,
                model_word(row + 56u), model_word(row + 60u), "mksc:controller", &property);
            for (unsigned actor = 0;
                 actor < portable_mesh_course_actors_config_count(&host->actor_config); ++actor)
            {
                PortableMeshCourseActorBinding actor_binding;
                if (!portable_mesh_course_actors_binding(&host->actor_config, actor, &actor_binding))
                    goto failed;
                controlled |= actor_binding.object_index == i;
            }
            if (host->actor_config.sky.enabled)
                for (unsigned layer = 0; layer < 3u; ++layer)
                    controlled |= host->actor_config.sky_bindings.object_indices[layer] == i;
            for (unsigned coin = 0; coin < host->actor_config.coins.placement_count; ++coin)
                controlled |= host->actor_config.coins.placements[coin].object_index == i;
            if (controlled)
            {
                snprintf(host->error, sizeof host->error,
                         "3D course=%u object=%u mksc:scenery-art-bank unsupported on controlled object",
                         host->course.id, (unsigned)i);
                goto failed;
            }
            scenery = portable_model_catalog_find(&host->models, PORTABLE_MODEL_SCENERY,
                                                   host->course.id, bank, 0);
        }
        PortableModelBinding binding = {.mesh_id = model_word(row + 8),
                                        .scale_q16 = {65536, 65536, 65536}};
        if (model_word(row + 4) != PORTABLE_ASSET_SECTION_MODEL_MESH ||
            !portable_model_mesh_open(host->pack, binding.mesh_id, &binding.mesh))
        {
            snprintf(host->error, sizeof host->error,
                     "3D course=%u object=%u model kind=%u id=%u unsupported/invalid",
                     host->course.id, (unsigned)i, model_word(row + 4), binding.mesh_id);
            goto failed;
        }
        double object_transform[12];
        const double x = model_fixed(row + 24, 1073741824.0),
                     y = model_fixed(row + 28, 1073741824.0);
        const double z = model_fixed(row + 32, 1073741824.0),
                     w = model_fixed(row + 36, 1073741824.0);
        const double rotation[9] = {
            1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
        for (unsigned r = 0; r < 3u; ++r)
        {
            for (unsigned c = 0; c < 3u; ++c)
                object_transform[r * 4u + c] =
                    rotation[r * 3u + c] * model_fixed(row + 40 + c * 4u, 65536.0);
            object_transform[r * 4u + 3u] = model_fixed(row + 12 + r * 4u, 65536.0);
        }
        if (!portable_render_model_local_transform(object_transform, object_transform, NULL))
        {
            snprintf(host->error, sizeof host->error,
                     "3D course=%u object=%u required OBJ transform invalid",
                     host->course.id, (unsigned)i);
            goto failed;
        }
        /* Binding composition is optional: its failed transactional output leaves
         * the required OBJ affine intact, before any replacement resource opens. */
        if (scenery && !portable_render_model_local_transform(object_transform, object_transform,
                                                               scenery))
            scenery = NULL;
        /* The explicit OBJ fallback remains a valid required course resource.
         * Optional catalog failures retain it, including its original billboard. */
        if (scenery)
            binding = *scenery;
        size_t resource_index = 0;
        while (resource_index < host->model_count &&
               host->model_ids[resource_index] != binding.mesh_id)
            ++resource_index;
        PortableRenderResources *resource = &host->model_resources[resource_index];
        if (resource_index == host->model_count)
        {
            if (!portable_render_resources_open_model(resource, &binding, host->pack, host->error,
                                                      sizeof host->error))
                goto failed;
            host->model_ids[host->model_count++] = binding.mesh_id;
            if (!open_scenery_skin(host, resource_index, &binding))
                goto failed;
        }
        ModelPlacement *placement = &host->placements[i];
        placement->resource = resource_index;
        placement->property_first = model_word(row + 56u);
        placement->property_count = model_word(row + 60u);
        if (host->actor_config.sky.enabled)
            for (unsigned layer = 0; layer < 3u; ++layer)
                if (host->actor_config.sky_bindings.object_indices[layer] == i)
                    placement->sky_layer_plus1 = layer + 1u;
        for (unsigned coin = 0; coin < host->actor_config.coins.placement_count; ++coin)
            if (host->actor_config.coins.placements[coin].object_index == i)
                placement->coin_index_plus1 = coin + 1u;
        if (portable_course_3d_property_find(&host->course, model_word(row + 56u),
                                             model_word(row + 60u), "mksc:billboard", &property))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
            {
                snprintf(host->error, sizeof host->error,
                         "3D course=%u object=%u mksc:billboard requires boolean", host->course.id,
                         (unsigned)i);
                goto failed;
            }
            placement->billboard = (uint8_t)property.value.boolean;
            host->billboard_count += placement->billboard;
        }
        for (unsigned axis = 0; axis < 3u; ++axis)
            placement->scale[axis] = model_fixed(row + 40u + axis * 4u, 65536.0);
        const int moving = portable_mesh_collision_object_motion(&host->course, (uint32_t)i, &placement->motion);
        if (moving < 0 || (moving && scenery))
        {
            snprintf(host->error, sizeof host->error, "3D course=%u object=%u invalid mksc:motion",
                     host->course.id, (unsigned)i);
            goto failed;
        }
        placement->moving = (uint8_t)moving;
        if (portable_course_3d_property_find(&host->course, placement->property_first, placement->property_count,
                                             "mksc:shadow", &property))
        {
            PortableCourse3DProperty size;
            PortableModelMaterial material;
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                !portable_course_3d_property_find(&host->course, placement->property_first,
                                                  placement->property_count, "mksc:shadow-size", &size) ||
                size.type != PORTABLE_COURSE_3D_PROPERTY_VECTOR || size.value.vector_q16[0] <= 0 ||
                size.value.vector_q16[1] <= 0 || size.value.vector_q16[2] <= 0 ||
                !portable_model_material_open(host->pack, (uint32_t)property.value.integer, &material) ||
                !(placement->shadow = calloc(1, sizeof *placement->shadow)) ||
                !portable_mesh_shadow_decal_open(placement->shadow, &material))
            {
                free(placement->shadow);
                placement->shadow = NULL;
                snprintf(host->error, sizeof host->error,
                         "3D course=%u object=%u mksc:shadow needs a textured material and a positive mksc:shadow-size",
                         host->course.id, (unsigned)i);
                goto failed;
            }
            for (unsigned axis = 0; axis < 3u; ++axis)
                placement->shadow_size[axis] = size.value.vector_q16[axis] / 65536.0;
        }
        placement->row_id = model_word(row);
        placement->route = model_word(row + 52u);
        if (portable_course_3d_property_find(&host->course, placement->property_first, placement->property_count,
                                             "mksc:route-window", &property))
        {
            PortableCourse3DProperty follower, outside = {0};
            const int has_outside = portable_course_3d_property_find(&host->course, placement->property_first,
                placement->property_count, "mksc:route-window-outside", &outside);
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_VECTOR || property.value.vector_q16[0] < 0 ||
                property.value.vector_q16[1] < 0 || property.value.vector_q16[2] < 0 ||
                !portable_course_3d_property_find(&host->course, placement->property_first,
                                                  placement->property_count, "mksc:route-window-follower",
                                                  &follower) ||
                follower.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
                (has_outside && outside.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN))
            {
                snprintf(host->error, sizeof host->error,
                         "3D course=%u object=%u mksc:route-window requires vector (route, from, to), "
                         "integer mksc:route-window-follower and boolean mksc:route-window-outside",
                         host->course.id, (unsigned)i);
                goto failed;
            }
            placement->window = has_outside && outside.value.boolean ? 2u : 1u;
            placement->window_route = (uint32_t)property.value.vector_q16[0];
            placement->window_from = (uint32_t)property.value.vector_q16[1];
            placement->window_to = (uint32_t)property.value.vector_q16[2];
            placement->window_follower = (uint32_t)follower.value.integer;
        }
        /* A moving object's looping clip runs its motion phase ahead, so
         * objects sharing a model keep their own place in the clip. */
        const RacerSkin *shared = &host->model_skins[resource_index];
        if (moving && placement->motion.phase && shared->rigged)
        {
            RacerSkin *own = malloc(sizeof *own);
            PortableRenderVertex *vertices = own ? malloc(shared->capacity * sizeof *vertices) : NULL;
            if (!vertices)
            {
                free(own);
                snprintf(host->error, sizeof host->error, "3D course=%u object=%u: skin allocation failed",
                         host->course.id, (unsigned)i);
                goto failed;
            }
            *own = *shared;
            own->vertices = vertices;
            own->mesh.vertices = vertices;
            own->sample = -1;
            own->revision = 0;
            placement->skin = own;
        }
        PortableRenderInstance *instance = &placement->instance;
        portable_render_instance_identity(instance, &resource->mesh);
        memcpy(instance->transform, object_transform, sizeof object_transform);
        if (scenery)
        {
            host->billboard_count -= placement->billboard;
            placement->billboard = 0;
        }
    }
    for (unsigned actor = 0; actor < portable_mesh_course_actors_config_count(&host->actor_config); ++actor)
    {
        PortableMeshCourseActorBinding actor_binding;
        if (!portable_mesh_course_actors_binding(&host->actor_config, actor, &actor_binding))
            goto failed;
        if (actor_binding.object_index >= count)
            goto failed;
        host->placements[actor_binding.object_index].actor_index_plus1 = actor + 1u;
        for (unsigned frame = 0; frame < actor_binding.frame_count; ++frame)
        {
            const uint32_t id = actor_binding.frame_ids[frame];
            size_t index = 0;
            while (index < host->model_count && host->model_ids[index] != id)
                ++index;
            if (index == host->model_count)
            {
                PortableModelBinding binding = {.mesh_id = id, .scale_q16 = {65536, 65536, 65536}};
                if (!portable_model_mesh_open(host->pack, id, &binding.mesh) ||
                    !portable_render_resources_open_model(&host->model_resources[index], &binding,
                                                          host->pack, host->error,
                                                          sizeof host->error))
                    goto failed;
                host->model_ids[host->model_count++] = id;
            }
        }
    }
    for (size_t i = 0; i < host->model_count; ++i)
    {
        const PortableRenderMesh *mesh = &host->model_resources[i].mesh;
        if (!mesh->vertex_count)
            goto failed;
        for (unsigned axis=0;axis<3;++axis)
        {
            double low=mesh->vertices[0].position[axis], high=low;
            for (uint32_t v=1;v<mesh->vertex_count;++v)
            {
                const double value=mesh->vertices[v].position[axis];
                if (value<low) low=value;
                if (value>high) high=value;
            }
            host->model_bounds[i][axis]=low;
            host->model_bounds[i][axis+3]=high;
        }
        host->model_flip_z[i]=host->model_bounds[i][2]+host->model_bounds[i][5];
        host->model_flip_x[i]=host->model_bounds[i][0]+host->model_bounds[i][3];
    }
    if (host->actor_config.sky.palette.enabled)
    {
        host->palette_scene = portable_mesh_palette_scene_create(&host->actor_config.sky.palette,
            host->pack, host->error, sizeof host->error);
        if (!host->palette_scene || !portable_mesh_palette_scene_mesh(host->palette_scene,
            &host->resources.mesh, 0, host->error, sizeof host->error))
            goto failed;
        for (size_t i = 0; i < host->model_count; ++i)
            if (!portable_mesh_palette_scene_mesh(host->palette_scene,
                &host->model_resources[i].mesh, 0, host->error, sizeof host->error))
                goto failed;
        const PortableMeshPaletteSceneStats stats = portable_mesh_palette_scene_stats(host->palette_scene);
        printf("3D palette resources: course=%u textures=%zu rgba-bytes=%zu meshes=%zu\n",
            host->course.id, stats.unique_textures, stats.expanded_rgba_bytes, stats.mesh_wrappers);
    }
    host->object_count = count;
    host->models_prepared = 1;
    return 1;
failed:
    if (!host->error[0])
        snprintf(host->error, sizeof host->error, "3D course=%u model resources allocation failed",
                 host->course.id);
    close_models(host);
    return 0;
}

/* The mode browser is a projection of the catalog, never a replacement for
 * its indices: GP cup discovery must still inspect every catalog row. */
static void filter_courses(PortableCourse3DHost *host)
{
    host->visible_count = 0;
    int selected = 0;
    for (size_t i = 0; i < host->count; ++i)
        if (!host->configured || (host->entries[i].modes & host->mode))
        {
            size_t at = host->visible_count++;
            while (at)
            {
                const PreviewEntry *before = &host->entries[host->visible[at - 1u]];
                if (before->sort_key < host->entries[i].sort_key ||
                    (before->sort_key == host->entries[i].sort_key &&
                     before->id < host->entries[i].id))
                    break;
                host->visible[at] = host->visible[at - 1u];
                --at;
            }
            host->visible[at] = i;
            selected |= i == host->cursor;
        }
    if (!selected && host->visible_count)
        host->cursor = host->visible[0];
}

static size_t visible_cursor(const PortableCourse3DHost *host)
{
    for (size_t i = 0; i < host->visible_count; ++i)
        if (host->visible[i] == host->cursor)
            return i;
    return 0;
}

static int select_course(PortableCourse3DHost *preview)
{
    if (preview->cursor >= preview->count)
        return 0;
    close_models(preview);
    close_geometry(preview);
    /* close_models retired uploads before releasing borrowed mesh identities. */
    preview->gpu_renderer = NULL;
    preview->gpu_attempted = preview->backend_reported = preview->freeze_valid = 0;
    preview->pose_valid = 0;
    portable_render_resources_close(&preview->resources);
    PortableCourse3DCatalog row;
    if (!portable_course_3d_catalog_at(preview->pack, preview->cursor, &row, preview->error,
                                       sizeof preview->error) ||
        !portable_course_3d_open(preview->pack, row.course_id, &preview->course, preview->error,
                                 sizeof preview->error) ||
        !portable_course_3d_meta(&preview->course, &preview->meta))
        return 0;
    if (!portable_mesh_course_actors_config_open(&preview->course, &preview->actor_config,
                                                 preview->error, sizeof preview->error) ||
        (preview->actor_config.ground_follow_version && !prepare_geometry(preview)) ||
        !portable_mesh_course_actors_init_on_course(&preview->actor_config, host_geometry(preview),
                                          &preview->trial_actors, preview->error, sizeof preview->error))
        return 0;
    load_gp_preview_progress(preview);
    return 1;
}

PortableCourse3DHost *portable_course_3d_host_create(const PortableAssetPackView *pack, char *error,
                                                     size_t capacity)
{
    PortableCourse3DHost *preview = calloc(1u, sizeof *preview);
    if (preview == NULL)
        return NULL;
    preview->pack = pack;
    const char *views = getenv("MKSC_AID_MESH_VIEWS");
    preview->aid_views = views && strcmp(views, "4") == 0 ? 4u : 1u;
    if (!portable_course_3d_validate_pack(pack, error, capacity))
    {
        free(preview);
        return NULL;
    }
    preview->count = portable_course_3d_catalog_count(pack);
    if (preview->count != 0u)
    {
        preview->entries = calloc(preview->count, sizeof *preview->entries);
        preview->visible = calloc(preview->count, sizeof *preview->visible);
        if (preview->entries == NULL || preview->visible == NULL)
        {
            free(preview->entries);
            free(preview->visible);
            free(preview);
            return NULL;
        }
        for (size_t i = 0u; i < preview->count; ++i)
        {
            preview->cursor = i;
            if (!select_course(preview))
            {
                if (error != NULL && capacity != 0u)
                    snprintf(error, capacity, "%s", preview->error);
                free(preview->entries);
                free(preview->visible);
                free(preview);
                return NULL;
            }
            PortableCourse3DCatalog catalog;
            if (!portable_course_3d_catalog_at(pack, i, &catalog, preview->error,
                                               sizeof preview->error))
            {
                portable_course_3d_host_close(preview);
                return NULL;
            }
            preview->entries[i] = (PreviewEntry){preview->course.id, preview->meta.name,
                                                 preview->meta.cup_name, preview->meta.modes,
                                                 catalog.sort_key, catalog.cup_id, catalog.slot,
                                                 preview->meta.physics_profile_id, {0}};
            PortableNativeImage art;
            if (preview->meta.preview_id &&
                portable_course_3d_texture(&preview->course, preview->meta.preview_id, &art) &&
                art.palette == NULL)
                preview->entries[i].preview = art;
        }
        preview->cursor = 0u;
        filter_courses(preview);
        preview->cursor = preview->visible[0];
    }
    if (preview->count != 0u && !select_course(preview))
    {
        if (error != NULL && capacity != 0u)
            snprintf(error, capacity, "%s", preview->error);
        free(preview->entries);
        free(preview->visible);
        free(preview);
        return NULL;
    }
    return preview;
}

void portable_course_3d_host_close(PortableCourse3DHost *preview)
{
    if (preview != NULL)
    {
        close_local_pads(preview);
        close_models(preview);
        close_geometry(preview);
        portable_render_cpu_close(&preview->cpu);
        portable_render_resources_close(&preview->resources);
        free(preview->entries);
        free(preview->visible);
        free(preview->recording.bytes);
        free(preview->ghost_bytes);
        free(preview->overlay);
    }
    free(preview);
}

int portable_course_3d_host_available(const PortableCourse3DHost *preview)
{
    return preview != NULL && preview->count != 0u;
}

int portable_course_3d_host_active(const PortableCourse3DHost *preview)
{
    return preview != NULL && preview->active;
}

void portable_course_3d_host_show(PortableCourse3DHost *preview)
{
    if (!portable_course_3d_host_available(preview))
        return;
    preview->active = 1u;
    preview->flying = 0u;
    preview->previous = UINT16_MAX;
    preview->tick = 0u;
    printf("3D course selector: count=%zu\n", preview->visible_count);
}

void portable_course_3d_host_set_gamepad(PortableCourse3DHost *preview, uint8_t gamepad)
{
    if (preview != NULL)
        preview->gamepad = gamepad;
}

static void reset_camera(PortableCourse3DHost *preview)
{
    PortableCourse3DGrid grid;
    if (!portable_course_3d_grid(&preview->course, 0u, &grid))
        return;
    for (unsigned i = 0u; i < 3u; ++i)
        preview->position[i] = (double)grid.position_q16[i] / 65536.0;
    const double x = (double)grid.quaternion_q30[0] / 1073741824.0;
    const double y = (double)grid.quaternion_q30[1] / 1073741824.0;
    const double z = (double)grid.quaternion_q30[2] / 1073741824.0;
    const double w = (double)grid.quaternion_q30[3] / 1073741824.0;
    const double forward_x = 2.0 * (w * z - x * y);
    const double forward_y = 2.0 * (x * x + z * z) - 1.0;
    const double forward_z = -2.0 * (y * z + w * x);
    preview->position[0] -= forward_x * 8.0;
    preview->position[1] -= forward_y * 8.0;
    preview->position[2] += 5.0;
    preview->yaw = atan2(forward_x, -forward_y);
    preview->pitch = atan2(forward_z, sqrt(forward_x * forward_x + forward_y * forward_y)) - 0.16;
}

int portable_course_3d_host_configure(PortableCourse3DHost *host, const uint8_t digest[32],
                                      uint32_t driver_id, const char *save_directory,
                                      const PortableSettings *settings)
{
    const uint32_t drivers[PORTABLE_MESH_LOCAL_MAX] = {driver_id};
    return portable_course_3d_host_configure_race(host, digest, PORTABLE_COURSE_3D_TRIAL, 1u, 0u,
                                                  drivers, save_directory, settings);
}
int portable_course_3d_host_set_roster(PortableCourse3DHost *host, const uint32_t *driver_ids,
                                       size_t count)
{
    if (!host || host->driving || count > PORTABLE_DRIVER_3D_CAPACITY || (count && !driver_ids))
        return 0;
    if (count)
        memcpy(host->roster, driver_ids, count * sizeof *driver_ids);
    host->roster_count = count;
    return 1;
}
int portable_course_3d_host_set_karts(PortableCourse3DHost *host,
                                      const uint32_t kart_ids[PORTABLE_MESH_LOCAL_MAX])
{
    if (!host || !kart_ids || host->driving)
        return 0;
    memcpy(host->chosen_karts, kart_ids, sizeof host->chosen_karts);
    return 1;
}
int portable_course_3d_host_configure_race(PortableCourse3DHost *host, const uint8_t digest[32],
                                           uint32_t mode, unsigned players, unsigned engine_class,
                                           const uint32_t driver_ids[PORTABLE_MESH_LOCAL_MAX],
                                           const char *save_directory,
                                           const PortableSettings *settings)
{
    if (!host || !digest || !driver_ids || players == 0u || players > PORTABLE_MESH_LOCAL_MAX ||
        engine_class > 2u || host->driving ||
        (mode != 1u && mode != 2u && mode != 4u && mode != 8u && mode != 16u) ||
        (save_directory && strlen(save_directory) >= sizeof host->save_directory))
        return 0;
    for (unsigned i = 0; i < players; ++i)
        if (driver_ids[i] == 0u)
            return 0;
    close_models(host);
    close_local_pads(host);
    memcpy(host->digest, digest, sizeof host->digest);
    host->mode = mode;
    host->players = players;
    host->racers = mode == PORTABLE_COURSE_3D_QUICK || mode == 1u ? 8u
                   : mode == PORTABLE_COURSE_3D_BATTLE            ? 4u
                                                                  : players;
    host->cup_ceremony = host->cup_standings_shown = 0;
    host->engine_class = engine_class;
    memcpy(host->driver_ids, driver_ids, players * sizeof *driver_ids);
    host->driver_id = driver_ids[0];
    memset(host->chosen_karts, 0, sizeof host->chosen_karts);
    snprintf(host->save_directory, sizeof host->save_directory, "%s",
             save_directory ? save_directory : "");
    if (settings)
        host->settings = *settings;
    else
        portable_settings_defaults(&host->settings);
    host->configured = 1u;
    const size_t previous_cursor = host->cursor;
    filter_courses(host);
    if (host->visible_count && host->cursor != previous_cursor && !select_course(host))
        return 0;
    host->error[0] = 0;
    load_gp_preview_progress(host);
    if (shared_race(host))
        open_local_pads(host);
    return 1;
}
unsigned portable_course_3d_host_local_count(const PortableCourse3DHost *host)
{
    return host && shared_race(host) ? host->players : 0u;
}
uint32_t portable_course_3d_host_gp_course_index(const PortableCourse3DHost *host)
{
    return host && host->mode == 1u && host->cup.definition.cup_id && host->cup.course_index < 4u
               ? host->cup.course_index
               : UINT32_MAX;
}
uint32_t portable_course_3d_host_gp_retry_credits(const PortableCourse3DHost *host)
{
    return portable_course_3d_host_gp_course_index(host) == UINT32_MAX ? UINT32_MAX
                                                                        : host->cup.retry_credits;
}
const PortableMeshRaceSession *portable_course_3d_host_race(const PortableCourse3DHost *host)
{
    return portable_course_3d_host_driving(host) && shared_race(host) ? &host->race : NULL;
}
void portable_course_3d_host_device_event(PortableCourse3DHost *host, const SDL_Event *event)
{
    if (!host || !event || !shared_race(host))
        return;
    if (event->type == SDL_EVENT_GAMEPAD_ADDED || event->type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        open_local_pads(host);
        return;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.scancode > 0 &&
        event->key.scancode < SDL_SCANCODE_COUNT)
    {
        bool keys[SDL_SCANCODE_COUNT] = {false};
        keys[event->key.scancode] = true;
        host->local_pressed[0] |=
            portable_keyboard_driving_buttons(keys, SDL_SCANCODE_COUNT, &host->settings);
        host->local_pressed[1] |=
            portable_keyboard2_driving_buttons(keys, SDL_SCANCODE_COUNT, &host->settings);
    }
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        for (unsigned i = 0; i < host->players; ++i)
        {
            const int slot = host->local_inputs[i].pad_slot;
            if (slot < 0 || event->gbutton.which != portable_sdl_gamepad_id(host->pads[slot]))
                continue;
            const uint16_t actions[4] = {PORTABLE_INPUT_ACCELERATE, PORTABLE_INPUT_BRAKE,
                                         PORTABLE_INPUT_HOP, PORTABLE_INPUT_ITEM};
            for (unsigned binding = 0; binding < 4; ++binding)
                if (event->gbutton.button == host->settings.pad_buttons[binding])
                    host->local_pressed[i] |= actions[binding];
        }
}
void portable_course_3d_host_sample_locals(PortableCourse3DHost *host,
                                           PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX],
                                           uint16_t *menu_buttons)
{
    if (inputs == NULL)
        return;
    memset(inputs, 0, PORTABLE_MESH_LOCAL_MAX * sizeof *inputs);
    if (!host || !shared_race(host))
        return;
    uint16_t pads[PORTABLE_MESH_LOCAL_MAX], buttons[PORTABLE_MESH_LOCAL_MAX];
    const uint16_t keys[2] = {portable_sdl_keyboard_driving(), portable_sdl_keyboard2_driving()};
    for (unsigned i = 0; i < PORTABLE_MESH_LOCAL_MAX; ++i)
    {
        pads[i] = portable_sdl_gamepad_driving(host->pads[i]);
        if (menu_buttons)
            *menu_buttons |= portable_sdl_gamepad_frontend(host->pads[i]);
    }
    portable_local_players_buttons(host->local_inputs, host->players, keys[0], keys[1], pads,
                                   buttons);
    for (unsigned i = 0; i < host->players; ++i)
    {
        const uint16_t driving = buttons[i] | host->local_pressed[i];
        host->local_pressed[i] = 0;
        host->item_controls[i] = (PortableMeshItemControls){
            .item_pressed = (uint8_t)((driving & PORTABLE_INPUT_ITEM) != 0u),
            .aim_backward = (uint8_t)((driving & PORTABLE_INPUT_DOWN) != 0u),
            .aim_forward = (uint8_t)((driving & PORTABLE_INPUT_UP) != 0u)};
        if (driving & PORTABLE_INPUT_ACCELERATE)
            inputs[i].buttons |= PORTABLE_MESH_ACCELERATE;
        if (driving & PORTABLE_INPUT_BRAKE)
            inputs[i].buttons |= PORTABLE_MESH_BRAKE;
        if (driving & PORTABLE_INPUT_HOP)
            inputs[i].buttons |= PORTABLE_MESH_HOP;
        const int slot = host->local_inputs[i].pad_slot;
        int16_t x = 0, y = 0;
        if (slot >= 0)
            portable_sdl_gamepad_stick(host->pads[slot], &x, &y);
        const int32_t magnitude = x < 0 ? -(int32_t)x : x;
        const int32_t deadzone = host->settings.steering_deadzone;
        if (magnitude > deadzone)
        {
            const int32_t scaled = (magnitude - deadzone) * 32767 / (32768 - deadzone);
            inputs[i].steering = (int16_t)(x < 0 ? -scaled : scaled);
        }
        const unsigned keyboard = host->local_inputs[i].keyboard;
        uint16_t digital = keyboard ? keys[keyboard - 1u] : 0u;
        if (slot >= 0)
        {
            host->item_controls[i].aim_backward |=
                (uint8_t)(SDL_GetGamepadButton(host->pads[slot], SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
                          y > host->settings.steering_deadzone);
            host->item_controls[i].aim_forward |=
                (uint8_t)(SDL_GetGamepadButton(host->pads[slot], SDL_GAMEPAD_BUTTON_DPAD_UP) ||
                          y < -(int32_t)host->settings.steering_deadzone);
            if (SDL_GetGamepadButton(host->pads[slot], SDL_GAMEPAD_BUTTON_DPAD_LEFT))
                digital |= PORTABLE_INPUT_LEFT;
            if (SDL_GetGamepadButton(host->pads[slot], SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
                digital |= PORTABLE_INPUT_RIGHT;
        }
        if (digital & PORTABLE_INPUT_LEFT)
            inputs[i].steering = -32767;
        if (digital & PORTABLE_INPUT_RIGHT)
            inputs[i].steering = 32767;
    }
}

void portable_course_3d_host_item_input(PortableCourse3DHost *host, unsigned player,
                                        PortableMeshItemControls controls)
{
    if (host && player < host->players && player < PORTABLE_MESH_LOCAL_MAX &&
        controls.item_pressed <= 1u && controls.aim_backward <= 1u && controls.aim_forward <= 1u)
    {
        controls.recovering = 0; /* Movement owns fresh recovery admission. */
        host->item_controls[player] = controls;
    }
}

int portable_course_3d_host_driving(const PortableCourse3DHost *host)
{
    return host && host->active && host->driving;
}

const PortableMeshTimeTrialSession *
portable_course_3d_host_session(const PortableCourse3DHost *host)
{
    return portable_course_3d_host_driving(host) && !shared_race(host) ? &host->trial : NULL;
}

void portable_course_3d_host_set_paused(PortableCourse3DHost *host, int paused)
{
    if (!portable_course_3d_host_driving(host) ||
        (shared_race(host)
             ? host->race.state.results_ready
             : (!host->replaying && host->trial.state.phase == PORTABLE_MESH_TT_FINISHED)) ||
        host->paused == (paused != 0))
        return;
    host->menu_age = 0;
    host->freeze_valid = 0;
    host->pose_valid = 0;
    host->paused = paused != 0;
    if (host->paused)
        host->wait_for_neutral = 1u;
    host->action_cursor = 0;
    host->graphics = (PortablePauseGraphics){0};
    printf("3D %s pause: paused=%u tick=%u\n", shared_race(host) ? race_name(host) : "Time Trial",
           host->paused,
           shared_race(host) ? host->race.state.tick
           : host->replaying ? host->replay_trial.state.tick
                             : host->trial.state.tick);
}

static void trial_error(PortableCourse3DHost *host, const char *detail)
{
    char copy[192];
    snprintf(copy, sizeof copy, "%s", detail);
    const char *mode = shared_race(host)   ? race_name(host)
                       : host->mode == 1u  ? "GP"
                       : host->mode == 4u  ? "Quick"
                       : host->mode == 16u ? "Battle"
                                           : "TT";
    snprintf(host->error, sizeof host->error, "3D course=%u mode=%s state=%u: %s", host->course.id,
             mode, shared_race(host) ? host->race.state.results_ready : host->trial.state.phase,
             copy);
    fprintf(stderr, "%s\n", host->error);
}

/* Imported 3D drivers race with their donor's roster: the player's donor picks
 * the rivals, and each selectable 3D driver may take its donor's seat. */
static int in_roster(const PortableCourse3DHost *host, uint32_t id)
{
    for (size_t i = 0; i < host->roster_count; ++i)
        if (host->roster[i] == id)
            return 1;
    return !host->roster_count;
}
static int listed(const uint32_t ids[8], unsigned count, uint32_t id)
{
    for (unsigned i = 0; i < count; ++i)
        if (ids[i] == id)
            return 1;
    return 0;
}
/* The stock donor rivals, each swapped (by a seeded hash) for a selectable 3D
 * driver of that donor; with a roster, every rival then comes from it: a
 * roster driver of the stock rival's donor, else the next unused one. */
static int rival_drivers(const PortableCourse3DHost *host, uint32_t seed, uint32_t ids[8])
{
    PortableDriver3D row;
    const uint32_t stock = portable_driver_3d_find(host->pack, host->driver_id, &row)
                               ? row.donor
                               : host->driver_id;
    if (!portable_recovered_pack_rival_driver_ids(host->pack, stock, ids))
        return 0;
    ids[0] = host->driver_id;
    PortableDriver3D rows[PORTABLE_DRIVER_3D_CAPACITY];
    const size_t count = portable_driver_3d_list(host->pack, rows, PORTABLE_DRIVER_3D_CAPACITY);
    for (size_t r = 0; r < count; ++r)
    {
        if (!(rows[r].flags & PORTABLE_DRIVER_3D_SELECTABLE) || rows[r].id == host->driver_id ||
            !in_roster(host, rows[r].id))
            continue;
        uint32_t hash = seed ^ rows[r].id * 0x9E3779B9u;
        hash = (hash ^ hash >> 16) * 0x85EBCA6Bu;
        hash = (hash ^ hash >> 13) * 0xC2B2AE35u;
        if (!((hash ^ hash >> 16) & 1u))
            continue;
        for (unsigned i = 1; i < 8u; ++i)
            if (ids[i] == rows[r].donor)
            {
                ids[i] = rows[r].id;
                break;
            }
    }
    for (unsigned i = 1; host->roster_count && i < 8u; ++i)
    {
        if (in_roster(host, ids[i]) && !listed(ids, i, ids[i]))
            continue;
        uint32_t pick = 0u;
        for (size_t r = 0; r < count && !pick; ++r)
            if (rows[r].donor == ids[i] && in_roster(host, rows[r].id) && !listed(ids, 8u, rows[r].id))
                pick = rows[r].id;
        for (size_t r = 0; r < host->roster_count && !pick; ++r)
            if (!listed(ids, 8u, host->roster[r]))
                pick = host->roster[r];
        if (!pick)
            return 0; /* Fewer than eight drivers on the platform. */
        ids[i] = pick;
    }
    return 1;
}

static int32_t ratio(int32_t value, int32_t ratio_q16)
{
    return (int32_t)((int64_t)value * ratio_q16 / 65536);
}

/* Each racer races its chosen kart, else its driver's own; fails naming a
 * missing kart. */
static int resolve_karts(PortableCourse3DHost *host, const uint32_t *driver_ids, uint32_t count)
{
    memset(host->race_karts, 0, sizeof host->race_karts);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t id = i < PORTABLE_MESH_LOCAL_MAX && i < host->players ? host->chosen_karts[i] : 0u;
        if (!id)
            id = portable_kart_3d_default(host->pack, driver_ids[i]);
        PortableKart3D kart;
        if (id && !portable_kart_3d_find(host->pack, id, &kart))
        {
            snprintf(host->error, sizeof host->error, "3D course=%u driver=%u: kart %u is missing",
                     host->course.id, driver_ids[i], id);
            return 0;
        }
        host->race_karts[i] = id;
    }
    return 1;
}

/* The course's META kart scale, which every racer's drawing follows. */
static double kart_scale(const PortableCourse3DHost *host)
{
    return host->kart_scale > 0 ? host->kart_scale : 1.0;
}

/* The course profile and its rules at an engine class with base stats. */
static int load_physics(PortableCourse3DHost *host, uint32_t profile_id, unsigned engine_class)
{
    PortableMeshKartProfile profile;
    PortableMeshKartRules rules;
    int32_t levels[PORTABLE_MESH_KART_STATS];
    if (!portable_mesh_kart_profile_asset_load(host->pack, profile_id, &profile))
        return 0;
    portable_mesh_kart_stat_levels(&profile, 0, NULL, levels);
    PortableCourse3DChase chase;
    if (!portable_mesh_kart_rules_resolve(&profile, engine_class, levels, &rules) ||
        !portable_course_3d_chase(&host->course, &chase, NULL, 0) ||
        !portable_mesh_kart_rules_scale_body(&rules, chase.kart_scale_q16))
        return 0;
    /* The rig is the course's own eye and aim, else the stock rig at the
     * course's kart scale. */
    const double scale = chase.kart_scale_q16 / 65536.0;
    double eye[3], target[3];
    for (unsigned a = 0; a < 3; ++a)
    {
        eye[a] = chase.eye_q16[a] / 65536.0;
        target[a] = chase.target_q16[a] / 65536.0;
    }
    PortableRenderChaseRig rig;
    if (!portable_render_chase_rig(&rig, chase.has_rig ? eye : NULL, chase.has_rig ? target : NULL, scale))
        return 0;
    host->chase = chase;
    host->kart_scale = scale;
    /* Stock courses keep the zero rig, which the camera reads as its own. */
    host->chase_rig = chase.has_rig || chase.kart_scale_q16 != 65536 ? rig : (PortableRenderChaseRig){0};
    host->profile = profile;
    host->rules = rules;
    host->physics_class = engine_class;
    return 1;
}

/* A racer's rules: the course profile at the race's class with the stock
 * driver's points, plus points from a DRV3 driver's and KRT3 kart's ratios
 * (each 0.1 above 1 is two stat levels: speed to every speed stat, handling to
 * every handling stat, acceleration and mass to weight). */
#define RATIO_POINTS_Q16 20
static int32_t ratio_points(int32_t ratio_q16)
{
    return (int32_t)(((int64_t)ratio_q16 - 65536) * RATIO_POINTS_Q16);
}
static int scaled_rules(const PortableCourse3DHost *host, uint32_t driver_id, uint32_t kart_id,
                        PortableMeshKartRules *out)
{
    PortableDriver3D driver = {.mass_q16 = 65536, .speed_q16 = 65536, .acceleration_q16 = 65536,
                               .handling_q16 = 65536};
    PortableKart3D kart = {.mass_q16 = 65536, .speed_q16 = 65536, .acceleration_q16 = 65536,
                           .handling_q16 = 65536};
    const int has_driver = portable_driver_3d_find(host->pack, driver_id, &driver);
    const int has_kart = kart_id && portable_kart_3d_find(host->pack, kart_id, &kart);
    const int32_t speed = ratio_points(ratio(driver.speed_q16, kart.speed_q16));
    const int32_t acceleration = ratio_points(ratio(driver.acceleration_q16, kart.acceleration_q16));
    const int32_t handling = ratio_points(ratio(driver.handling_q16, kart.handling_q16));
    const int32_t weight = ratio_points(ratio(driver.mass_q16, kart.mass_q16));
    int32_t extra[PORTABLE_MESH_KART_STATS] = {0}, levels[PORTABLE_MESH_KART_STATS];
    extra[PORTABLE_MESH_KART_STAT_WEIGHT] = weight;
    extra[PORTABLE_MESH_KART_STAT_ACCELERATION] = acceleration;
    for (unsigned s = PORTABLE_MESH_KART_STAT_SPEED_GROUND; s <= PORTABLE_MESH_KART_STAT_SPEED_ANTIGRAVITY; ++s)
        extra[s] = speed;
    for (unsigned s = PORTABLE_MESH_KART_STAT_HANDLING_GROUND; s <= PORTABLE_MESH_KART_STAT_HANDLING_ANTIGRAVITY; ++s)
        extra[s] = handling;
    /* A DRV3 driver races on its donor's stock points. */
    const uint32_t stock = has_driver ? driver.donor : driver_id;
    portable_mesh_kart_stat_levels(&host->profile, stock, has_driver || has_kart ? extra : NULL, levels);
    PortableMeshKartRules rules;
    if (!portable_mesh_kart_rules_resolve(&host->profile, host->physics_class, levels, &rules) ||
        !portable_mesh_kart_rules_scale_body(&rules, host->chase.kart_scale_q16))
        return 0;
    *out = rules;
    return 1;
}

/* Racers with a DRV3 row or a kart drive the course profile scaled by them. */
static int apply_driver_rules(PortableCourse3DHost *host, const uint32_t *driver_ids, uint32_t count)
{
    char detail[192] = {0};
    if (!resolve_karts(host, driver_ids, count))
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const PortableMeshKartRules *rules =
            scaled_rules(host, driver_ids[i], host->race_karts[i], &host->driver_rules[i])
                ? &host->driver_rules[i]
                : NULL;
        if (rules && i >= host->players)
        {
            /* CPUs drive their class share of both their cap and planned speeds. */
            const int32_t share = host->profile.cpu_speed_ratio_q16[host->physics_class];
            host->driver_rules[i].forward_speed_q16 =
                (int32_t)((int64_t)host->driver_rules[i].forward_speed_q16 * share / 65536);
            host->driver_rules[i].cpu_pace_ratio_q16 = share;
        }
        if (portable_mesh_race_set_racer_rules(&host->race, i, rules, detail, sizeof detail) !=
            PORTABLE_MESH_OK)
        {
            snprintf(host->error, sizeof host->error, "3D course=%u driver=%u: %s", host->course.id,
                     driver_ids[i], detail);
            return 0;
        }
    }
    return 1;
}

static int prepare_items(PortableCourse3DHost *host)
{
    uint32_t id = 0;
    char detail[192] = {0};
    if (!portable_course_3d_item_rules_mode(&host->course, host->mode, &id, detail, sizeof detail))
        goto failed;
    if (!id)
        return 1;
    if (!portable_mesh_item_rules_asset_load(host->pack, id, &host->item_rules) ||
        !portable_mesh_item_art_open(host->pack, &host->item_art, detail, sizeof detail) ||
        !portable_mesh_item_scene_open(host->item_art, &host->item_scene, detail, sizeof detail) ||
        !portable_mesh_item_scene_open_models(host->item_scene, host->pack, id, kart_scale(host), detail,
                                              sizeof detail))
        goto failed;
    host->item_seed = host->course.id ^ UINT32_C(0x4d495231);
    host->item_config = (PortableMeshItemsConfig){
        .course = &host->course, .rules = &host->item_rules,
        .kart_rules = shared_race(host) ? &host->rules : host->trial.rules,
        .mode = host->mode, .count = (uint8_t)(shared_race(host) ? host->racers : 1u),
        .human_mask = (uint8_t)((1u << host->players) - 1u),
        .geometry = host_geometry(host)};
    if (shared_race(host))
    {
        if (portable_mesh_race_enable_items(&host->race, &host->item_rules, host->item_seed, detail,
                                            sizeof detail) != PORTABLE_MESH_OK)
            goto failed;
    }
    else if (portable_mesh_items_init(&host->trial_items, &host->item_config, host->item_seed,
                                      detail, sizeof detail) != PORTABLE_MESH_ITEMS_OK)
        goto failed;
    host->item_profile_enabled = 1;
    printf("3D items start: course=%u mode=%u profile=%u revision=%u racers=%u\n", host->course.id,
           host->mode, host->item_rules.profile_id, host->item_rules.revision,
           host->item_config.count);
    for (unsigned item = 0; item < 16u; ++item)
    {
        const PortableNativeImage *image =
            portable_mesh_item_art_hud(host->item_art, (uint16_t)(0x1000u + item));
        for (unsigned pixel = 0; pixel < 256u; ++pixel)
            host->item_hud_pixels[item][pixel] =
                image ? portable_native_image_pixel(image, pixel % 16u, pixel / 16u) : 0u;
    }
    memset(host->item_controls, 0, sizeof host->item_controls);
    return 1;
failed:
    trial_error(host, detail[0] ? detail : "invalid terrain item profile/artwork");
    return 0;
}

static int prepare_actors(PortableCourse3DHost *host)
{
    char detail[192] = {0};
    if (!portable_mesh_course_actors_config_open(&host->course, &host->actor_config, detail,
                                                 sizeof detail))
        goto failed;
    if (!portable_mesh_course_actors_init_on_course(&host->actor_config, host_geometry(host), &host->trial_actors, detail,
                                          sizeof detail))
        goto failed;
    if (!portable_mesh_course_actors_required(&host->actor_config))
        return 1;
    if (!host->item_profile_enabled && portable_mesh_course_actors_needs_items(&host->actor_config))
    {
        snprintf(detail, sizeof detail, "source controllers require a MIR item/status owner");
        goto failed;
    }
    if (shared_race(host))
    {
        if (portable_mesh_race_enable_actors(&host->race, &host->actor_config, detail,
                                             sizeof detail) != PORTABLE_MESH_OK)
            goto failed;
    }
    memset(&host->actor_events, 0, sizeof host->actor_events);
    if (host->actor_config.sky.enabled)
        printf("3D sky start: course=%u profile=%u revision=%u controller=%u layers=%u\n",
               host->course.id, host->actor_config.sky.profile_id, host->actor_config.sky.revision,
               host->actor_config.sky.rules.controller, host->actor_config.sky.render_mask);
    if (host->actor_config.coins.enabled)
        printf("3D coins start: course=%u mode=%u profile=%u revision=%u placements=%u cap=%u initial=%u\n",
            host->course.id, host->mode, host->actor_config.coins.profile_id,
            host->actor_config.coins.revision, host->actor_config.coins.placement_count,
            host->actor_config.coins.cap, host->actor_config.coins.initial);
    printf("3D source actors start: course=%u crabs=%u profile=%u revision=%u\n", host->course.id,
           host->actor_config.count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.snowman_count)
        printf("3D source snowmen start: course=%u snowmen=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.snowman_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.penguin_count)
        printf("3D source penguins start: course=%u penguins=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.penguin_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.mouse_count)
        printf("3D source mice start: course=%u mice=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.mouse_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.piranha_count)
        printf("3D source piranhas start: course=%u piranhas=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.piranha_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.seagull_count)
        printf("3D source seagulls start: course=%u seagulls=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.seagull_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.lava_count)
        printf("3D source lava bubbles start: course=%u lava_bubbles=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.lava_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.thwomp_count)
        printf("3D source thwomps start: course=%u thwomps=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.thwomp_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.cannonball_count)
        printf("3D source cannonballs start: course=%u cannonballs=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.cannonball_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.boo_count)
        printf("3D source boos start: course=%u boos=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.boo_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.rock_count)
        printf("3D source rocks start: course=%u rocks=%u profile=%u revision=%u\n", host->course.id,
               host->actor_config.rock_count, host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.teepee_count)
        printf("3D source teepees start: course=%u teepees=%u profile=%u revision=%u\n",
               host->course.id, host->actor_config.teepee_count,
               host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.falling_star_count)
        printf("3D source falling stars start: course=%u falling_stars=%u profile=%u revision=%u\n",
               host->course.id, host->actor_config.falling_star_count,
               host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.thundercloud_count)
        printf("3D source thunderclouds start: course=%u thunderclouds=%u profile=%u revision=%u\n",
               host->course.id, host->actor_config.thundercloud_count,
               host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.common_prop_count)
        printf("3D source common props start: course=%u common_props=%u profile=%u revision=%u\n",
               host->course.id, host->actor_config.common_prop_count,
               host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.mechakoopa_count)
        printf("3D source mechakoopas start: course=%u mechakoopas=%u profile=%u revision=%u\n",
               host->course.id, host->actor_config.mechakoopa_count,
               host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.kamek_count)
        printf("3D source kamek start: course=%u kamek=%u profile=%u revision=%u\n",
               host->course.id, host->actor_config.kamek_count,
               host->actor_config.profile_id, host->actor_config.revision);
    if (host->actor_config.pier_boo_count)
        printf("3D source pier boos start: course=%u pier_boos=%u profile=%u revision=%u seed=%u rng=%u\n",
            host->course.id, host->actor_config.pier_boo_count, host->actor_config.profile_id,
            host->actor_config.revision, host->actor_config.actor_visual_seed,
            host->trial_actors.pier_boo_visual_rng);
    memset(host->kamek_view_models, 0, sizeof host->kamek_view_models);
    memset(host->mechakoopa_view_models, 0, sizeof host->mechakoopa_view_models);
    memset(host->teepee_view_models, 0, sizeof host->teepee_view_models);
    memset(host->boo_view_sides,0,sizeof host->boo_view_sides);
    return 1;
failed:
    trial_error(host, detail);
    return 0;
}

static int begin_trial(PortableCourse3DHost *host)
{
    char detail[192] = {0};
    host->driving = host->paused = host->replaying = 0;
    host->freeze_valid = 0;
    host->pose_valid = 0;
    close_models(host);
    if (!host->configured)
    {
        trial_error(host, "missing driver/save configuration");
        return 0;
    }
    if (!load_physics(host, host->meta.physics_profile_id, PORTABLE_MESH_KART_CLASSES - 1u))
    {
        trial_error(host, "missing or invalid physics profile");
        return 0;
    }
    if (!prepare_geometry(host))
    {
        trial_error(host, host->error);
        return 0;
    }
    if (!resolve_karts(host, &host->driver_id, 1u))
    {
        trial_error(host, host->error);
        return 0;
    }
    /* Ghosts copy this session, so they drive the same driver and kart. */
    const PortableMeshKartRules *rules =
        scaled_rules(host, host->driver_id, host->race_karts[0], &host->driver_rules[0])
            ? &host->driver_rules[0]
            : &host->rules;
    if (portable_mesh_time_trial_init(&host->trial, &host->course, rules, 0u, detail,
                                      sizeof detail) != PORTABLE_MESH_OK)
    {
        trial_error(host, detail);
        return 0;
    }
    host->trial.geometry = host_geometry(host);
    if (!prepare_items(host) || !prepare_actors(host))
        return 0;
    host->identity = (PortableMeshTimeTrialCompatibility){host->course.id,
                                                          host->driver_id,
                                                          host->rules.profile_id,
                                                          host->rules.revision,
                                                          4389u,
                                                          262144u,
                                                          {0},
                                                          host->race_karts[0]};
    memcpy(host->identity.digest, host->digest, sizeof host->digest);
    free(host->recording.bytes);
    host->recording = (PortableReplayMemoryOutput){0};
    const size_t recording_capacity =
        portable_mesh_course_actors_required(&host->actor_config)     ? PORTABLE_MESH_REPLAY_ACTORS_MAX_BYTES
        : host->item_profile_enabled ? PORTABLE_MESH_REPLAY_ITEMS_MAX_BYTES
                                     : PORTABLE_MESH_REPLAY_MAX_BYTES;
    host->recording.bytes = malloc(recording_capacity);
    if (!host->recording.bytes)
    {
        trial_error(host, "recording allocation failed");
        return 0;
    }
    host->recording.capacity = recording_capacity;
    host->writer = portable_replay_memory_writer(&host->recording);
    host->record_valid =
        (portable_mesh_course_actors_required(&host->actor_config)
             ? portable_mesh_replay_write_header_actors(
                   &host->writer, &host->identity, &host->trial,
                   host->item_profile_enabled ? &host->trial_items : NULL,
                   host->item_profile_enabled ? &host->item_config : NULL,
                   host->item_profile_enabled ? host->item_seed : 0u, &host->trial_actors,
                   &host->actor_config)
         : host->item_profile_enabled
             ? portable_mesh_replay_write_header_items(&host->writer, &host->identity, &host->trial,
                                                       &host->trial_items, &host->item_config,
                                                       host->item_seed)
             : portable_mesh_replay_write_header(&host->writer, &host->identity, &host->trial)) ==
        PORTABLE_MESH_REPLAY_OK;
    if (!host->record_valid)
    {
        trial_error(host, "recording header failed");
        return 0;
    }
    free(host->ghost_bytes);
    host->ghost_bytes = NULL;
    host->ghost_active = 0;
    host->ghost_path[0] = host->best_path[0] = 0;
    if (host->save_directory[0])
    {
        if (!portable_mesh_store_path(host->save_directory, &host->identity, 1, host->ghost_path,
                                      sizeof host->ghost_path) ||
            !portable_mesh_store_path(host->save_directory, &host->identity, 0, host->best_path,
                                      sizeof host->best_path))
        {
            trial_error(host, "cannot create mesh save paths");
            return 0;
        }
        size_t size = 0;
        const PortableMeshStoreStatus saved =
            portable_mesh_ghost_load(host->ghost_path, &host->identity, &host->ghost_bytes, &size);
        if (saved != PORTABLE_MESH_STORE_OK && saved != PORTABLE_MESH_STORE_NOT_FOUND)
        {
            trial_error(host, portable_mesh_store_status_name(saved));
            return 0;
        }
        if (saved == PORTABLE_MESH_STORE_OK)
        {
            host->ghost_trial = host->trial;
            host->ghost_items = host->trial_items;
            host->ghost_actors = host->trial_actors;
            host->ghost_input = (PortableReplayMemoryInput){host->ghost_bytes, size, 0};
            host->ghost_reader = portable_replay_memory_reader(&host->ghost_input);
            if ((portable_mesh_course_actors_required(&host->actor_config)
                     ? portable_mesh_replay_playback_init_actors(
                           &host->ghost_playback, &host->ghost_reader, &host->identity,
                           &host->ghost_trial,
                           host->item_profile_enabled ? &host->ghost_items : NULL,
                           host->item_profile_enabled ? &host->item_config : NULL,
                           &host->ghost_actors, &host->actor_config)
                 : host->item_profile_enabled
                     ? portable_mesh_replay_playback_init_items(
                           &host->ghost_playback, &host->ghost_reader, &host->identity,
                           &host->ghost_trial, &host->ghost_items, &host->item_config)
                     : portable_mesh_replay_playback_init(
                           &host->ghost_playback, &host->ghost_reader, &host->identity,
                           &host->ghost_trial)) != PORTABLE_MESH_REPLAY_OK)
            {
                trial_error(host, "invalid saved ghost initial state");
                return 0;
            }
            host->ghost_active = 1;
        }
    }
    host->driving = 1;
    host->flying = 0;
    host->paused = host->replaying = 0;
    host->menu_age = host->action_cursor = host->last_lap_tick = host->best_lap_tick = 0;
    host->error[0] = 0;
    reset_audio_events(host);
    printf("3D Time Trial start: course=%u driver=%u profile=%u revision=%u ghost=%u\n",
           host->identity.course_id, host->identity.driver_id, host->identity.profile_id,
           host->identity.profile_revision, host->ghost_active);
    if (host->identity.kart_id)
        printf("3D Time Trial kart: kart=%u\n", host->identity.kart_id);
    return 1;
}

static int begin_local_race(PortableCourse3DHost *host)
{
    char detail[192] = {0};
    host->driving = host->paused = host->replaying = host->ghost_active = 0;
    host->freeze_valid = host->pose_valid = 0;
    uint32_t profile_id;
    if (!portable_course_3d_physics_profile(&host->course, host->engine_class, &profile_id, detail,
                                            sizeof detail))
    {
        trial_error(host, detail);
        return 0;
    }
    if (!load_physics(host, profile_id, host->engine_class))
    {
        trial_error(host, "missing or invalid selected class physics profile");
        return 0;
    }
    if (host->mode == PORTABLE_COURSE_3D_QUICK &&
        !rival_drivers(host, host->course.id, host->driver_ids))
    {
        trial_error(host, "missing or invalid imported donor-class rival roster");
        return 0;
    }
    if (host->mode == PORTABLE_COURSE_3D_BATTLE && host->players < host->racers)
    {
        uint32_t rivals[8];
        if (!rival_drivers(host, host->course.id, rivals))
        {
            trial_error(host, "Battle rival roster missing/invalid");
            return 0;
        }
        unsigned count = host->players;
        for (unsigned candidate = 0; candidate < 8u && count < host->racers; ++candidate)
        {
            unsigned found = 0;
            for (unsigned i = 0; i < count; ++i)
                found |= host->driver_ids[i] == rivals[candidate];
            if (!found)
                host->driver_ids[count++] = rivals[candidate];
        }
        if (count != host->racers)
        {
            trial_error(host, "Battle requires four resolved drivers");
            return 0;
        }
    }
    close_models(host);
    if (!prepare_geometry(host))
    {
        trial_error(host, host->error);
        return 0;
    }
    if (portable_mesh_race_init_mode(&host->race, &host->course, &host->rules, host->mode,
                                     host->players, host->racers, NULL, detail,
                                     sizeof detail) != PORTABLE_MESH_OK)
    {
        trial_error(host, detail);
        return 0;
    }
    host->race.geometry = host_geometry(host);
    if (!prepare_items(host) || !prepare_actors(host))
        return 0;
    if (!apply_driver_rules(host, host->driver_ids, host->racers))
    {
        trial_error(host, host->error);
        return 0;
    }
    /* Shared races have no TT record/ghost writer or save path. Any prior TT buffers stay
     * owned until the next TT initialization or host destruction. */
    host->record_valid = 0;
    host->ghost_path[0] = host->best_path[0] = 0;
    host->menu_age = host->action_cursor = 0;
    host->error[0] = 0;
    host->driving = 1;
    host->flying = 0;
    for (unsigned i = 0; i < host->players; ++i)
    {
        host->local_input_hashes[i] = UINT64_C(14695981039346656037);
        host->local_pressed[i] = 0;
    }
    reset_audio_events(host);
    printf("3D %s start: course=%u players=%u drivers=", race_name(host), host->course.id,
           host->players);
    for (unsigned i = 0; i < host->racers; ++i)
        printf("%s%u", i ? "," : "", host->driver_ids[i]);
    printf(" profile=%u revision=%u\n", host->rules.profile_id, host->rules.revision);
    const char *policy =
        profile_id == host->meta.physics_profile_id ? "authored-profile" : "class-profile";
    if (host->mode == PORTABLE_COURSE_3D_QUICK)
        printf("3D Quick class: selected=%u profile=%u policy=%s racers=%u\n", host->engine_class,
               host->rules.profile_id, policy, host->racers);
    printf(
        "3D %s physics: class=%u base_profile=%u selected_profile=%u revision=%u cap_q16=%d policy=%s\n",
        race_name(host), host->engine_class, host->meta.physics_profile_id, host->rules.profile_id,
        host->rules.revision, host->rules.forward_speed_q16, policy);
    return 1;
}
/* Stage every borrowed view and owned upload source before retiring the
 * visible race. Allocation or content rejection leaves that race intact. */
static PortableCourse3DHost *prepare_gp_course(PortableCourse3DHost *host,
                                               const PortableMeshCupSession *cup)
{
    PortableCourse3DHost *next = calloc(1, sizeof *next);
    if (!next)
    {
        trial_error(host, "GP preparation allocation failed");
        return NULL;
    }
    next->pack = host->pack;
    next->mode = 1u;
    next->players = 1u;
    next->driving = 1u;
    next->racers = 8u;
    next->engine_class = host->engine_class;
    next->driver_id = host->driver_id;
    memcpy(next->driver_ids, cup->driver_ids, sizeof next->driver_ids);
    memcpy(next->chosen_karts, host->chosen_karts, sizeof next->chosen_karts);
    const uint32_t id = cup->definition.course_ids[cup->course_index];
    uint32_t profile;
    uint32_t grids[8];
    for (unsigned i = 0; i < 8u; ++i)
        grids[i] = cup->grid_slots[i];
    int valid =
        portable_course_3d_open(host->pack, id, &next->course, next->error, sizeof next->error) &&
        portable_course_3d_meta(&next->course, &next->meta) && (next->meta.modes & 1u) &&
        portable_course_3d_physics_profile(&next->course, host->engine_class, &profile, next->error,
                                           sizeof next->error) &&
        load_physics(next, profile, host->engine_class) &&
        prepare_geometry(next) &&
        portable_mesh_race_init_mode(&next->race, &next->course, &next->rules, 1u, 1u, 8u, grids,
                                     next->error, sizeof next->error) == PORTABLE_MESH_OK;
    if (valid)
    {
        next->race.geometry = host_geometry(next);
        valid = prepare_items(next) && prepare_actors(next) &&
                apply_driver_rules(next, next->driver_ids, 8u) &&
                portable_render_resources_open_course_present(&next->resources, &next->course,
                                                              race_presence(next), next->error,
                                                              sizeof next->error) &&
                prepare_models(next);
    }
    if (!valid)
    {
        char detail[256];
        snprintf(detail, sizeof detail, "GP course %u preparation: %.180s", id,
                 next->error[0] ? next->error : "invalid course mode/profile");
        trial_error(host, detail);
        close_models(next);
        close_geometry(next);
        portable_render_resources_close(&next->resources);
        free(next);
        return NULL;
    }
    return next;
}
static void discard_gp_course(PortableCourse3DHost *next)
{
    close_models(next);
    close_geometry(next);
    portable_render_resources_close(&next->resources);
    free(next);
}
static void start_intro(PortableCourse3DHost *host)
{
    PortableCourse3DIntroShot shot;
    host->intro = (uint8_t)portable_course_3d_intro_shot(&host->course, 0u, &shot);
    host->intro_shot = host->intro_tick = 0u;
}

/* One presentation tick of the intro; 0 when the race should tick instead. */
static int tick_intro(PortableCourse3DHost *host, uint16_t menu_buttons)
{
    if (!host->intro || !host->driving || host->paused)
        return 0;
    const uint16_t pressed = menu_buttons & (uint16_t)~host->previous;
    host->previous = menu_buttons;
    ++host->tick;
    PortableCourse3DIntroShot shot;
    if (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))
        host->intro = 0;
    else if (!portable_course_3d_intro_shot(&host->course, host->intro_shot, &shot) ||
             ++host->intro_tick < shot.ticks)
        return 1;
    else
    {
        host->intro_tick = 0;
        host->intro = (uint8_t)portable_course_3d_intro_shot(&host->course, ++host->intro_shot, &shot);
    }
    return 1;
}

static int publish_gp_course(PortableCourse3DHost *host, PortableCourse3DHost *next,
                              const PortableMeshCupSession *cup)
{
    /* Preflight before retiring the live generation. The relocation below
     * only changes addresses: both destination descriptors are exact copies. */
    if (!portable_mesh_palette_scene_relocate(next->palette_scene,
        &next->actor_config.sky.palette, &next->resources.mesh, &next->resources.mesh,
        host->error, sizeof host->error))
        return 0;
    close_models(host);
    close_geometry(host);
    portable_render_resources_close(&host->resources);
    host->collision_index = next->collision_index;
    next->collision_index = NULL;
    host->course = next->course;
    host->meta = next->meta;
    host->sound_emitter_count = 0;
    const PortableCourse3DTable *zones =
        portable_course_3d_table(&host->course, PORTABLE_COURSE_3D_TAG('Z', 'O', 'N', 'E'));
    for (uint32_t i = 0; zones && i < zones->count && host->sound_emitter_count < PORTABLE_MESH_SOUND_EMITTER_LIMIT;
         ++i)
    {
        PortableCourse3DSoundZone zone;
        if (!portable_course_3d_sound_zone(&host->course, i, &zone))
            continue;
        PortableMeshSoundEmitter *e = &host->sound_emitters[host->sound_emitter_count++];
        memcpy(e->position_q16, zone.position_q16, sizeof e->position_q16);
        e->distance_q16 = zone.radius_q16;
        e->song = zone.song;
        e->period = zone.period;
    }
    memcpy(host->particle_poses, next->particle_poses, sizeof host->particle_poses);
    memcpy(host->particle_emitters, next->particle_emitters, sizeof host->particle_emitters);
    host->particle_emitter_count = next->particle_emitter_count;
    host->rules = next->rules;
    host->chase = next->chase;
    host->kart_scale = next->kart_scale;
    host->chase_rig = next->chase_rig;
    host->race = next->race;
    /* Preserve the palette config's full immutable byte representation too. */
    memcpy(&host->actor_config, &next->actor_config, sizeof host->actor_config);
    host->trial_actors = next->trial_actors;
    host->race.actor_config = next->race.actor_config ? &host->actor_config : NULL;
    host->race.course = &host->course;
    host->race.rules = &host->rules;
    memcpy(host->driver_rules, next->driver_rules, sizeof host->driver_rules);
    for (unsigned i = 0; i < PORTABLE_MESH_RACE_MAX; ++i)
        if (host->race.racer_rules[i])
            host->race.racer_rules[i] = &host->driver_rules[i];
    host->item_rules = next->item_rules;
    host->race.item_rules = host->race.items_enabled ? &host->item_rules : NULL;
    host->item_profile_enabled = next->item_profile_enabled;
    host->item_seed = next->item_seed;
    host->item_config = next->item_config;
    host->item_config.course = &host->course;
    host->item_config.kart_rules = &host->rules;
    host->item_config.rules = &host->item_rules;
    host->item_art = next->item_art;
    host->item_scene = next->item_scene;
    host->kart_fx = next->kart_fx;
    host->fx_set = next->fx_set;
    host->character_rules = next->character_rules;
    host->character_rules_valid = next->character_rules_valid;
    memcpy(host->item_hud_pixels, next->item_hud_pixels, sizeof host->item_hud_pixels);
    memset(host->item_controls, 0, sizeof host->item_controls);
    host->instance_capacity = next->instance_capacity;
    host->resources = next->resources;
    host->palette_scene = next->palette_scene;
    (void)portable_mesh_palette_scene_relocate(host->palette_scene,
        &host->actor_config.sky.palette, &next->resources.mesh, &host->resources.mesh,
        host->error, sizeof host->error);
    next->palette_scene = NULL;
    host->models = next->models;
    host->model_resources = next->model_resources;
    host->model_ids = next->model_ids;
    host->model_skins = next->model_skins;
    host->model_flip_z = next->model_flip_z;
    host->model_flip_x = next->model_flip_x;
    host->model_bounds = next->model_bounds;
    host->instances = next->instances;
    host->placements = next->placements;
    host->model_count = next->model_count;
    host->object_count = next->object_count;
    host->billboard_count = next->billboard_count;
    host->driver_model = next->driver_model;
    host->driver_resource = next->driver_resource;
    host->models_prepared = next->models_prepared;
    memcpy(host->race_karts, next->race_karts, sizeof host->race_karts);
    memcpy(host->racer_karts, next->racer_karts, sizeof host->racer_karts);
    for (unsigned i = 0; i < PORTABLE_MESH_RACE_MAX; ++i)
        host->racer_models[i] = host->racer_karts[i].present ? &host->racer_karts[i].driver
                                                             : next->racer_models[i];
    memcpy(host->racer_resources, next->racer_resources, sizeof host->racer_resources);
    memcpy(host->driver_art, next->driver_art, sizeof host->driver_art);
    host->cup = *cup;
    memcpy(host->driver_ids, cup->driver_ids, sizeof host->driver_ids);
    host->driving = 1;
    host->paused = host->flying = host->replaying = host->ghost_active = 0;
    host->freeze_valid = host->pose_valid = host->record_valid = 0;
    host->cup_standings_shown = host->cup_ceremony = 0;
    host->menu_age = host->action_cursor = 0;
    start_intro(host);
    host->error[0] = 0;
    host->ghost_path[0] = host->best_path[0] = 0;
    host->local_input_hashes[0] = UINT64_C(14695981039346656037);
    memset(host->local_pressed, 0, sizeof host->local_pressed);
    for (size_t i = 0; i < host->count; ++i)
        if (host->entries[i].id == host->course.id)
            host->cursor = i;
    free(next); /* All prepared resource owners now belong to host. */
    reset_audio_events(host);
    printf("3D GP race: cup=%u index=%u course=%u class=%u profile=%u credits=%u grid=",
           cup->definition.cup_id, cup->course_index, host->course.id, host->engine_class,
           host->rules.profile_id, cup->retry_credits);
    for (unsigned i = 0; i < 8u; ++i)
        printf("%s%u", i ? "," : "", cup->grid_slots[i]);
    printf(" drivers=");
    for (unsigned i = 0; i < 8u; ++i)
        printf("%s%u", i ? "," : "", host->driver_ids[i]);
    printf("\n");
    printf("3D GP grid: course=%u slots=", host->course.id);
    for (unsigned i = 0; i < 8u; ++i)
        printf("%s%u", i ? "," : "", cup->grid_slots[i]);
    printf("\n");
    return 1;
}
static void load_gp_preview_progress(PortableCourse3DHost *host)
{
    host->cup_best_notice[0] = 0;
    if (!host->configured || host->mode != 1u || !host->save_directory[0])
        return;
    PortableMeshCupIdentity identity = {.cup_id = host->meta.cup_id,
                                        .driver_id = host->driver_id,
                                        .engine_class = host->engine_class};
    uint8_t seen = 0;
    for (size_t i = 0; i < host->count; ++i)
    {
        PortableCourse3DCatalog row;
        if (!portable_course_3d_catalog_at(host->pack, i, &row, NULL, 0))
            return;
        if (row.cup_id != identity.cup_id)
            continue;
        if (!(row.modes & 1u) || row.slot >= 4u || (seen & (1u << row.slot)))
            return;
        seen |= (uint8_t)(1u << row.slot);
        identity.course_ids[row.slot] = row.course_id;
    }
    if (seen != 15u)
        return;
    memcpy(identity.digest, host->digest, sizeof identity.digest);
    char path[4096];
    PortableMeshCupRecord best = {0};
    if (portable_mesh_cup_store_path(host->save_directory, &identity, path, sizeof path) &&
        portable_mesh_cup_store_load(path, &identity, &best) == PORTABLE_MESH_CUP_STORE_OK)
    {
        snprintf(host->cup_best_notice, sizeof host->cup_best_notice, "Best: rank %u / %u points",
                 best.standing_rank + 1u, best.points);
        printf("3D GP preview best: cup=%u rank=%u points=%u\n", identity.cup_id,
               best.standing_rank + 1u, best.points);
    }
}

static int begin_gp(PortableCourse3DHost *host)
{
    PortableMeshCupDefinition definition = {0};
    definition.cup_id = host->meta.cup_id;
    uint8_t seen = 0;
    for (size_t i = 0; i < host->count; ++i)
    {
        PortableCourse3DCatalog row;
        if (!portable_course_3d_catalog_at(host->pack, i, &row, host->error, sizeof host->error))
            return 0;
        if (row.cup_id != definition.cup_id)
            continue;
        if (!(row.modes & 1u) || row.slot >= 4u || (seen & (1u << row.slot)))
        {
            trial_error(host, "GP cup requires exactly four GP courses ordered 0..3");
            return 0;
        }
        seen = (uint8_t)(seen | (1u << row.slot));
        definition.course_ids[row.slot] = row.course_id;
    }
    PortableAssetSectionView section;
    PortableGpScoringRules scoring;
    uint32_t drivers[8];
    /* A cup's own scoring section (an imported game's points) wins over
     * the base game's. */
    const int own_scoring = portable_recovered_cup_scoring(host->pack, definition.cup_id, &scoring);
    if (seen != 15u ||
        (!own_scoring &&
         (portable_asset_pack_find_section(host->pack, PORTABLE_ASSET_SECTION_GP_SCORING, 1u,
                                           &section) != PORTABLE_ASSET_PACK_OK ||
          !portable_recovered_gp_scoring(host->pack, &scoring))) ||
        !rival_drivers(host, definition.cup_id, drivers))
    {
        trial_error(host, "GP needs a complete four-course cup, imported scoring and donor roster");
        return 0;
    }
    PortableMeshCupSession cup;
    if (portable_mesh_cup_init(&cup, &definition, drivers, host->engine_class, &scoring,
                               host->error, sizeof host->error) != PORTABLE_MESH_OK)
        return 0;
    /* Every course must be playable, including CPU routes and render sources,
     * before race one starts. No host or save state is published by preflight. */
    for (unsigned i = 0; i < 4u; ++i)
    {
        PortableMeshCupSession check = cup;
        check.course_index = i;
        PortableCourse3DHost *candidate = prepare_gp_course(host, &check);
        if (!candidate)
            return 0;
        discard_gp_course(candidate);
    }
    PortableCourse3DHost *first = prepare_gp_course(host, &cup);
    if (!first)
        return 0;
    PortableMeshCupIdentity identity = {.cup_id = definition.cup_id,
                                        .engine_class = host->engine_class,
                                        .driver_id = host->driver_id};
    memcpy(identity.course_ids, definition.course_ids, sizeof identity.course_ids);
    memcpy(identity.digest, host->digest, sizeof identity.digest);
    char path[4096] = {0};
    if (host->save_directory[0] &&
        !portable_mesh_cup_store_path(host->save_directory, &identity, path, sizeof path))
    {
        discard_gp_course(first);
        trial_error(host, "GP progress directory unavailable");
        return 0;
    }
    PortableMeshCupRecord saved = {0};
    const PortableMeshCupStoreStatus status =
        path[0] ? portable_mesh_cup_store_load(path, &identity, &saved)
                : PORTABLE_MESH_CUP_STORE_NOT_FOUND;
    if (status != PORTABLE_MESH_CUP_STORE_OK && status != PORTABLE_MESH_CUP_STORE_NOT_FOUND)
    {
        discard_gp_course(first);
        trial_error(host, portable_mesh_cup_store_status_name(status));
        return 0;
    }
    if (!publish_gp_course(host, first, &cup))
    {
        discard_gp_course(first);
        return 0;
    }
    host->cup_identity = identity;
    memcpy(host->cup_path, path, sizeof path);
    printf("3D GP cup: cup=%u class=%u courses=%u,%u,%u,%u drivers=", definition.cup_id,
           host->engine_class, definition.course_ids[0], definition.course_ids[1],
           definition.course_ids[2], definition.course_ids[3]);
    for (unsigned i = 0; i < 8u; ++i)
        printf("%s%u", i ? "," : "", drivers[i]);
    printf(" credits=%u\n", cup.retry_credits);
    printf("3D GP load: cup=%u status=%s", definition.cup_id,
           portable_mesh_cup_store_status_name(status));
    if (status == PORTABLE_MESH_CUP_STORE_OK)
        printf(" rank=%u points=%u", saved.standing_rank + 1u, saved.points);
    printf("\n");
    return 1;
}
static void complete_gp(PortableCourse3DHost *host)
{
    host->cup_ceremony = 1;
    host->ceremony_bounds_valid = 0;
    host->menu_age = 0;
    host->freeze_valid = host->pose_valid = 0;
    PortableMeshCupRecord candidate = {.standing_rank = host->cup.standing_rank[0],
                                       .points = host->cup.points[0]};
    for (unsigned i = 0; i < 4u; ++i)
    {
        candidate.finish_ticks[i] = host->cup.race_ticks[i];
        candidate.finish_ranks[i] = host->cup.race_ranks[i];
    }
    if (host->cup_path[0])
    {
        PortableMeshCupRecord saved = {0};
        int changed;
        const PortableMeshCupStoreStatus status = portable_mesh_cup_store_update(
            host->cup_path, &host->cup_identity, &candidate, &saved, &changed);
        if (status != PORTABLE_MESH_CUP_STORE_OK)
            trial_error(host, portable_mesh_cup_store_status_name(status));
        else
            printf("3D GP save: status=ok updated=%d cup=%u rank=%u points=%u path=%s\n", changed,
                   host->cup.definition.cup_id, candidate.standing_rank + 1u, candidate.points,
                   host->cup_path);
    }
    printf("3D GP complete: cup=%u rank=%u points=%u podium=%u,%u,%u\n",
           host->cup.definition.cup_id, host->cup.standing_rank[0] + 1u, host->cup.points[0],
           host->driver_ids[host->cup.standing_order[0]],
           host->driver_ids[host->cup.standing_order[1]],
           host->driver_ids[host->cup.standing_order[2]]);
    load_gp_preview_progress(host);
}
/* Pause menu Graphics page: Select opens it, edits go through the shell's hook, and the world re-renders
 * under the pause so the change shows at once. Returns 1 while the page owns this tick's buttons. */
static int pause_graphics(PortableCourse3DHost *host, uint16_t pressed)
{
    uint16_t cue = 0;
    if (!portable_pause_graphics_tick(&host->graphics, pressed, host->graphics_hook.edit != NULL, &cue))
        return 0;
    const PortableSettings *changed = portable_graphics_pause_update(&host->graphics, &host->graphics_hook);
    if (changed != NULL)
    {
        host->settings = *changed;
        host->freeze_valid = 0;
    }
    return 1;
}

void portable_course_3d_host_set_graphics(PortableCourse3DHost *host, PortableGraphicsHook hook)
{
    if (host)
        host->graphics_hook = hook;
}

static void gp_menu(PortableCourse3DHost *host, uint16_t pressed)
{
    if (host->cup_ceremony)
    {
        if (host->menu_age >= 64u &&
            (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_B |
                        PORTABLE_FRONTEND_BUTTON_START)))
        {
            host->driving = host->cup_ceremony = 0;
            host->freeze_valid = host->pose_valid = 0;
            printf("3D GP return: cup=%u\n", host->cup.definition.cup_id);
        }
        return;
    }
    const int failed = host->cup.stage == PORTABLE_MESH_CUP_RETRY ||
                       host->cup.stage == PORTABLE_MESH_CUP_GAME_OVER;
    const unsigned count = host->paused                        ? (host->cup.retry_credits ? 3u : 2u)
                           : failed && host->cup.retry_credits ? 2u
                                                               : 1u;
    if (pressed & PORTABLE_FRONTEND_BUTTON_UP)
        host->action_cursor = (host->action_cursor + count - 1u) % count;
    if (pressed & PORTABLE_FRONTEND_BUTTON_DOWN)
        host->action_cursor = (host->action_cursor + 1u) % count;
    if (pressed & PORTABLE_FRONTEND_BUTTON_B)
    {
        if (host->paused)
            portable_course_3d_host_set_paused(host, 0);
        else
        {
            host->driving = 0;
            host->pose_valid = host->freeze_valid = 0;
        }
        return;
    }
    if (host->paused && (pressed & PORTABLE_FRONTEND_BUTTON_START))
    {
        portable_course_3d_host_set_paused(host, 0);
        return;
    }
    if (!(pressed & PORTABLE_FRONTEND_BUTTON_A))
        return;
    if (host->paused && host->action_cursor == 0u)
    {
        portable_course_3d_host_set_paused(host, 0);
        return;
    }
    if ((host->paused && host->action_cursor == count - 1u) ||
        (failed && (!host->cup.retry_credits || host->action_cursor == 1u)))
    {
        host->driving = host->paused = 0;
        host->pose_valid = host->freeze_valid = 0;
        return;
    }
    if (!host->paused && !failed && !host->cup_standings_shown)
    {
        host->cup_standings_shown = 1;
        host->menu_age = 0;
        return;
    }
    PortableMeshCupSession next = host->cup;
    const int retry = host->paused || failed;
    char detail[192];
    const PortableMeshStatus status = retry
                                          ? portable_mesh_cup_retry(&next, detail, sizeof detail)
                                          : portable_mesh_cup_advance(&next, detail, sizeof detail);
    if (status != PORTABLE_MESH_OK)
    {
        trial_error(host, detail);
        return;
    }
    if (next.stage == PORTABLE_MESH_CUP_COMPLETE)
    {
        host->cup = next;
        complete_gp(host);
        return;
    }
    PortableCourse3DHost *candidate = prepare_gp_course(host, &next);
    if (!candidate)
        return;
    if (!publish_gp_course(host, candidate, &next))
    {
        discard_gp_course(candidate);
        return;
    }
    printf("3D GP %s: cup=%u course=%u index=%u credits=%u\n", retry ? "retry" : "next",
           next.definition.cup_id, host->course.id, next.course_index + 1u, next.retry_credits);
}
static int begin_selected_race(PortableCourse3DHost *host)
{
    if (!host->configured || !(host->meta.modes & host->mode))
    {
        trial_error(host, "course does not admit selected mode");
        return 0;
    }
    if (host->mode == PORTABLE_COURSE_3D_TRIAL && host->players == 1u)
        return begin_trial(host);
    if (host->mode == 1u && host->players == 1u)
        return begin_gp(host);
    if ((host->mode == PORTABLE_COURSE_3D_VS && host->players >= 2u) ||
        (host->mode == PORTABLE_COURSE_3D_QUICK && host->players == 1u) ||
        host->mode == PORTABLE_COURSE_3D_BATTLE)
        return begin_local_race(host);
    trial_error(host, "selected mode is not available yet");
    return 0;
}
int portable_course_3d_host_launch(PortableCourse3DHost *host, uint32_t course_id, int explore)
{
    if (!host || !host->configured || host->driving || host->active)
        return 0;
    size_t index = 0u;
    while (index < host->count && host->entries[index].id != course_id)
        ++index;
    host->error[0] = 0;
    if (index == host->count)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u is not installed", course_id);
        return 0;
    }
    if (index != host->cursor || host->course.id != course_id)
    {
        host->cursor = index;
        if (!select_course(host))
            return 0;
    }
    host->active = host->direct = 1u;
    host->flying = host->paused = host->launch_failed = 0u;
    host->previous = UINT16_MAX; /* The confirming press is not a host edge. */
    host->tick = 0u;
    if (explore || !host->meta.physics_profile_id)
    {
        reset_camera(host);
        host->flying = host->wait_for_neutral = 1u;
        printf("3D course preview: course=%u name=%s\n", host->course.id, host->meta.name);
        return 1;
    }
    printf("3D cup launch: course=%u cup=%u mode=%u\n", host->course.id, host->meta.cup_id,
           host->mode);
    /* The race starts on the next tick, which also publishes its first poses. */
    host->launch = 1u;
    return 1;
}

int portable_course_3d_host_launch_failed(const PortableCourse3DHost *host)
{
    return host && host->launch_failed;
}

const char *portable_course_3d_host_error(const PortableCourse3DHost *host)
{
    return host ? host->error : "";
}

size_t portable_course_3d_host_listing_count(const PortableCourse3DHost *host)
{
    return host ? host->count : 0u;
}

int portable_course_3d_host_listing(const PortableCourse3DHost *host, size_t index,
                                    PortableCourse3DListing *out)
{
    if (!host || !out || index >= host->count)
        return 0;
    const PreviewEntry *entry = &host->entries[index];
    *out = (PortableCourse3DListing){entry->id, entry->cup_id, entry->slot, entry->modes,
                                     entry->sort_key, entry->name, entry->cup_name,
                                     (uint8_t)(entry->physics_profile_id != 0u)};
    return 1;
}

int portable_course_3d_host_preview_image(const PortableCourse3DHost *host, uint32_t course_id,
                                          PortableNativeImage *out)
{
    if (!host || !out)
        return 0;
    for (size_t i = 0; i < host->count; ++i)
        if (host->entries[i].id == course_id && host->entries[i].preview.width)
        {
            *out = host->entries[i].preview;
            return 1;
        }
    return 0;
}

/* A cup launch ends (results, quit, failed start, closed preview) back on
 * the ordinary course screen rather than on the format browser. */
static void direct_return(PortableCourse3DHost *host)
{
    if (host->direct && host->active && !host->driving && !host->flying && !host->launch)
    {
        host->active = host->direct = 0u;
        host->freeze_valid = host->pose_valid = 0;
        printf("3D cup return: course=%u\n", host->course.id);
    }
}
static void local_race_return(PortableCourse3DHost *host)
{
    printf("3D %s return: course=%u players=%u\n", race_name(host), host->course.id, host->players);
    host->driving = host->paused = 0;
    host->freeze_valid = host->pose_valid = 0;
}
static void local_race_menu(PortableCourse3DHost *host, uint16_t pressed)
{
    if (host->mode == 1u)
    {
        gp_menu(host, pressed);
        return;
    }
    const unsigned count = host->paused ? 3u : 2u;
    if (pressed & PORTABLE_FRONTEND_BUTTON_UP)
        host->action_cursor = (host->action_cursor + count - 1u) % count;
    if (pressed & PORTABLE_FRONTEND_BUTTON_DOWN)
        host->action_cursor = (host->action_cursor + 1u) % count;
    if (pressed & PORTABLE_FRONTEND_BUTTON_B)
    {
        if (host->paused)
            portable_course_3d_host_set_paused(host, 0);
        else
            local_race_return(host);
        return;
    }
    if (host->paused && (pressed & PORTABLE_FRONTEND_BUTTON_START))
    {
        portable_course_3d_host_set_paused(host, 0);
        return;
    }
    if (!(pressed & PORTABLE_FRONTEND_BUTTON_A))
        return;
    if (host->paused && host->action_cursor == 0u)
        portable_course_3d_host_set_paused(host, 0);
    else if (host->action_cursor == count - 1u)
        local_race_return(host);
    else
        (void)begin_local_race(host);
}
static void log_actor_turn(const PortableCourse3DHost *host, const PortableMeshCourseActors *world,
                           const PortableMeshCourseActors *before,
                           const PortableMeshCourseActorEvents *events, uint32_t spin)
{
    const PortableMeshPaletteConfig *palette = &host->actor_config.sky.palette;
    if (palette->enabled && getenv("MKSC_AID_MESH_PALETTE") &&
        strcmp(getenv("MKSC_AID_MESH_PALETTE"), "1") == 0)
    {
        const PortableAmbientState *now = &world->sky.palette.ambient;
        const PortableAmbientState *old = &before->sky.palette.ambient;
        const unsigned sunset = now->profile_lanes[palette->sunset_lane].frame;
        const unsigned timer = palette->timer_lane == UINT16_MAX ? 0 : now->lanes[0].frame;
        if (world->tick == 1u || world->sky.motion.phase != before->sky.motion.phase ||
            world->sky.palette.variant != before->sky.palette.variant ||
            sunset != old->profile_lanes[palette->sunset_lane].frame ||
            (palette->timer_lane != UINT16_MAX && timer != old->lanes[0].frame))
            printf("3D palette state: course=%u tick=%u phase=%u sunset-frame=%u timer-frame=%u variant=%u split=%d\n",
                host->course.id, world->tick, world->sky.motion.phase, sunset, timer,
                world->sky.palette.variant, world->sky.motion.snapshot.split_y);
    }
    if (host->actor_config.coins.enabled)
    {
        const unsigned count = shared_race(host) ? host->race.state.player_count : 1u;
        for (unsigned racer = 0; racer < count; ++racer)
            if (world->tick == 1u || events->coins.picked[racer] || events->coins.lost[racer])
                printf("3D coin state: course=%u tick=%u racer=%u before=%u after=%u lost=%u causes=%u picked=%016llx available=%016llx ratio=%d checksum=%016llx\n",
                    host->course.id, world->tick, racer, before->coins.count[racer],
                    world->coins.count[racer], events->coins.lost[racer], events->coins.causes[racer],
                    (unsigned long long)events->coins.picked[racer],
                    (unsigned long long)world->coins.available,
                    host->actor_config.coins.ratios_q16[world->coins.count[racer]],
                    (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (!portable_mesh_course_actors_count(world))
        return;
    if (world->count && (world->tick == 1u || world->tick == 6u))
        printf("3D source actor state: course=%u tick=%u x=%d y=%d art=%u checksum=%016llx\n",
               host->course.id, world->tick, world->crabs[0].x_q16, world->crabs[0].y_q16,
               world->crabs[0].art,
               (unsigned long long)portable_mesh_course_actors_checksum(world));
    if (world->ground_follow_version && world->count && world->tick <= 380u)
    {
        int32_t pose[3];
        if (portable_mesh_course_actors_pose(&host->actor_config, world, 0u, pose))
            printf("3D actor ground: course=%u tick=%u family=crabs index=0 x=%d y=%d z=%d offset=%d flags=%u\n",
                host->course.id, world->tick, pose[0], pose[1], pose[2],
                world->crab_ground_offset_q16[0], world->crabs[0].flags);
    }
    if (events->hit_actors || events->protected_actors)
        printf("3D source actor contact: course=%u tick=%u hit=%u protected=%u actors=%016llx\n",
               host->course.id, world->tick, events->hit_mask, events->protected_mask,
               (unsigned long long)(events->hit_actors | events->protected_actors));
    for (unsigned i = 0; i < world->snowman_count; ++i)
    {
        const PortableSnowman *snow = &world->snowmen[i], *old = &before->snowmen[i];
        if (world->tick == 1u || snow->state != old->state || snow->art != old->art || snow->flags != old->flags)
            printf("3D source snowman state: course=%u tick=%u index=%u state=%d timer=%d art=%u flags=%u checksum=%016llx\n",
                   host->course.id, world->tick, i, snow->state, snow->timer, snow->art, snow->flags,
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->snowman_contacts || events->snowman_changed)
        printf("3D source snowman contact: course=%u tick=%u contacts=%016llx changed=%016llx bump=%u protected=%u hit=%u spin=%u\n",
               host->course.id, world->tick, (unsigned long long)events->snowman_contacts,
               (unsigned long long)events->snowman_changed, events->bump_mask, events->protected_mask,
               events->hit_mask, spin);
    for (unsigned i = 0; i < world->penguin_count; ++i)
    {
        const PortablePenguin *penguin = &world->penguins[i], *old = &before->penguins[i];
        if (world->tick == 1u || world->tick == 7u || penguin->art != old->art ||
            penguin->flags != old->flags || penguin->height_q16 != old->height_q16 ||
            penguin->affine_y_q8 != old->affine_y_q8)
            printf("3D source penguin state: course=%u tick=%u index=%u timer=%d art=%u flags=%u height=%d flip=%u checksum=%016llx\n",
                   host->course.id, world->tick, i, penguin->timer, penguin->art, penguin->flags,
                   penguin->height_q16, (unsigned)(penguin->affine_y_q8 < 0),
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->penguin_contacts)
        printf("3D source penguin contact: course=%u tick=%u contacts=%016llx defeated=%016llx hit=%u protected=%u\n",
               host->course.id, world->tick, (unsigned long long)events->penguin_contacts,
               (unsigned long long)events->penguin_defeated, events->hit_mask, events->protected_mask);
    for (unsigned i = 0; i < world->mouse_count; ++i)
    {
        const PortableMouse *mouse = &world->mice[i], *old = &before->mice[i];
        if (world->tick == 1u || mouse->art != old->art || mouse->flags != old->flags ||
            mouse->height_q16 != old->height_q16 || mouse->x_q16 != old->x_q16 ||
            mouse->y_q16 != old->y_q16 || mouse->turn_flip != old->turn_flip ||
            mouse->affine_y_q8 != old->affine_y_q8)
            printf("3D source mouse state: course=%u tick=%u index=%u variant=%u timer=%d art=%u flags=%u x=%d y=%d height=%d vx=%d vy=%d vz=%d facing_x=%d facing_y=%d turn=%u flip=%u checksum=%016llx\n",
                   host->course.id, world->tick, i, mouse->variant, mouse->timer, mouse->art,
                   mouse->flags, mouse->x_q16, mouse->y_q16, mouse->height_q16,
                   mouse->velocity_x_q16, mouse->velocity_y_q16, mouse->velocity_z_q16,
                   mouse->facing_x_q16, mouse->facing_y_q16, mouse->turn_flip,
                   (unsigned)(mouse->affine_y_q8 < 0),
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->mouse_contacts)
        printf("3D source mouse contact: course=%u tick=%u contacts=%016llx defeated=%016llx hit=%u protected=%u\n",
               host->course.id, world->tick, (unsigned long long)events->mouse_contacts,
               (unsigned long long)events->mouse_defeated, events->hit_mask, events->protected_mask);

    for (unsigned i = 0; i < world->piranha_count; ++i)
    {
        const PortablePiranha *plant = &world->piranhas[i], *old = &before->piranhas[i];
        if (world->tick == 1u || plant->state != old->state || plant->art != old->art ||
            plant->flags != old->flags || plant->height_q16 != old->height_q16)
            printf("3D source piranha state: course=%u tick=%u index=%u state=%d timer=%d phase=%d art=%u flags=%u height=%d flip=%u victim=%u checksum=%016llx\n",
                   host->course.id, world->tick, i, plant->state, plant->timer, plant->phase,
                   plant->art, plant->flags, plant->height_q16, plant->flip_y,
                   world->piranha_victim[i],
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->piranha_contacts || events->piranha_bite_mask)
        printf("3D source piranha contact: course=%u tick=%u contacts=%016llx changed=%016llx protected=%016llx touch=%u bite=%u\n",
               host->course.id, world->tick, (unsigned long long)events->piranha_contacts,
               (unsigned long long)events->piranha_changed, (unsigned long long)events->piranha_protected,
               events->piranha_touch_mask, events->piranha_bite_mask);
    for (unsigned i = 0; i < world->seagull_count; ++i)
    {
        const PortableSeagull *bird = &world->seagulls[i], *old = &before->seagulls[i];
        if (world->tick == 1u || bird->state != old->state || bird->art != old->art ||
            bird->flags != old->flags || bird->height_q16 != old->height_q16)
            printf("3D source seagull state: course=%u tick=%u index=%u state=%d timer=%d phase=%d art=%u flags=%u height=%d flip=%u checksum=%016llx\n",
                   host->course.id, world->tick, i, bird->state, bird->timer, bird->phase,
                   bird->art, bird->flags, bird->height_q16, bird->flip_y,
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->seagull_contacts)
        printf("3D source seagull contact: course=%u tick=%u contacts=%016llx changed=%016llx protected=%016llx touch=%u star=%u hit=%u grace=%u\n",
               host->course.id, world->tick, (unsigned long long)events->seagull_contacts,
               (unsigned long long)events->seagull_changed, (unsigned long long)events->seagull_defeated,
               events->seagull_touch_mask, events->protected_mask, events->hit_mask, world->actor_grace[0]);
    for (unsigned i = 0; i < world->lava_count; ++i)
    {
        const PortableLavaBubble *bubble = &world->lava[i], *old = &before->lava[i];
        if (world->tick == 1u || bubble->phase != old->phase || bubble->art != old->art ||
            bubble->flags != old->flags || bubble->height_q16 != old->height_q16 ||
            bubble->x_q16 != old->x_q16 || bubble->y_q16 != old->y_q16)
            printf("3D source lava state: course=%u tick=%u index=%u phase=%u timer=%d art=%u flags=%u x=%d y=%d height=%d vx=%d vy=%d vz=%d checksum=%016llx\n",
                   host->course.id, world->tick, i, bubble->phase, bubble->timer, bubble->art,
                   bubble->flags, bubble->x_q16, bubble->y_q16, bubble->height_q16,
                   bubble->velocity_x_q16, bubble->velocity_y_q16, bubble->velocity_z_q16,
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->lava_contacts)
        printf("3D source lava contact: course=%u tick=%u contacts=%016llx defeated=%016llx hit=%u protected=%u burn=%u\n",
               host->course.id, world->tick, (unsigned long long)events->lava_contacts,
               (unsigned long long)events->lava_defeated, events->hit_mask, events->protected_mask,
               events->lava_burn_mask);
    for (unsigned i = 0; i < 8; ++i)
        if (world->lava_burn_ticks[i] != before->lava_burn_ticks[i])
        {
            printf("3D source lava burn: course=%u tick=%u racer=%u remaining=%u\n",
                   host->course.id, world->tick, i, world->lava_burn_ticks[i]);
            /* Name the recovery that cut a burn short (rescue, bomb or respawn). */
            if (!world->lava_burn_ticks[i] && before->lava_burn_ticks[i] > 1u && shared_race(host) &&
                i < host->race.state.player_count)
            {
                const PortableMeshRacerState *racer = &host->race.state.racers[i];
                printf("3D source lava burn clear: course=%u tick=%u racer=%u rescue=%u bomb=%u respawn=%u\n",
                       host->course.id, world->tick, i,
                       (racer->kart.previous_buttons & PORTABLE_MESH_RESCUE) != 0,
                       host->race.bomb_recovery_ticks[i] != 0,
                       racer->kart.respawn_ticks != 0 || racer->respawn_grace != 0);
            }
        }
    for (unsigned i = 0; i < world->thwomp_count; ++i)
    {
        const PortableThwomp *a = &world->thwomps[i], *old = &before->thwomps[i];
        if (world->tick == 1 || memcmp(a, old, sizeof *a))
            printf("3D source thwomp state: course=%u tick=%u index=%u variant=%u state=%u timer=%d flags=%u x=%d y=%d height=%d vx=%d vy=%d vz=%d checksum=%016llx\n",
                   host->course.id, world->tick, i, a->variant, a->state, a->timer, a->flags,
                   a->x_q16, a->y_q16, a->height_q16, a->velocity_x_q16, a->velocity_y_q16,
                   a->velocity_z_q16, (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->thwomp_contacts)
        printf("3D source thwomp contact: course=%u tick=%u contacts=%016llx defeated=%016llx bump=%u crush=%u hit=%u protected=%u\n",
               host->course.id, world->tick, (unsigned long long)events->thwomp_contacts,
               (unsigned long long)events->thwomp_defeated, events->thwomp_bump_mask,
               events->thwomp_crush_mask, events->hit_mask, events->protected_mask);
    if (events->thwomp_landed)
        printf("3D source thwomp landed: course=%u tick=%u actors=%016llx\n", host->course.id,
               world->tick, (unsigned long long)events->thwomp_landed);
    if (events->thwomp_recover_mask)
        printf("3D source thwomp recover: course=%u tick=%u racers=%u\n", host->course.id,
               world->tick, events->thwomp_recover_mask);
    for (unsigned i = 0; i < 8; ++i)
        if (memcmp(&world->crush[i], &before->crush[i], sizeof world->crush[i]))
            printf("3D source thwomp crush: course=%u tick=%u racer=%u timer=%u mode=%u row=%u remaining=%u inverse_x=%u inverse_y=%u\n",
                   host->course.id, world->tick, i, world->crush[i].contact_timer,
                   world->crush[i].affine.mode, world->crush[i].affine.row, world->crush[i].affine.remaining,
                   world->crush[i].affine.inverse_x_q8, world->crush[i].affine.inverse_y_q8);
    for (unsigned i = 0; i < world->cannonball_count; ++i)
    {
        const PortableCannonball *a = &world->cannonballs[i], *old = &before->cannonballs[i];
        if (world->tick == 1 || memcmp(a, old, sizeof *a))
            printf("3D source cannonball state: course=%u tick=%u index=%u state=%d timer=%d flags=%u art=%u size=%u height=%d vz=%d inverse_x=%u inverse_y=%u checksum=%016llx\n",
                   host->course.id, world->tick, i, a->state, a->timer, a->flags, a->art, a->size,
                   a->height_q16, a->velocity_z_q16, a->affine_x_q8, a->affine_y_q8,
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->cannonball_contacts)
        printf("3D source cannonball contact: course=%u tick=%u contacts=%016llx hit=%u protected=%u\n",
               host->course.id, world->tick, (unsigned long long)events->cannonball_contacts,
               events->cannonball_hit_mask, events->cannonball_protected_mask);
    if (events->cannonball_launched || events->cannonball_exploded)
        printf("3D source cannonball events: course=%u tick=%u launched=%016llx exploded=%016llx\n",
               host->course.id, world->tick, (unsigned long long)events->cannonball_launched,
               (unsigned long long)events->cannonball_exploded);
    for (unsigned i = 0; i < 8; ++i)
        if (memcmp(&world->strong_hit[i], &before->strong_hit[i], sizeof world->strong_hit[i]))
            printf("3D source strong hit: course=%u tick=%u racer=%u pending=%u active=%u rate=%u angle=%u direction=%u\n",
                   host->course.id, world->tick, i, world->strong_hit[i].pending, world->strong_hit[i].active,
                   world->strong_hit[i].rate, world->strong_hit[i].angle, world->strong_hit[i].direction);
    for (unsigned i = 0; i < world->boo_count; ++i)
    {
        const PortableBoo *a=&world->boos[i],*old=&before->boos[i];
        if (world->tick==1 || memcmp(a,old,sizeof *a))
            printf("3D source boo state: course=%u tick=%u index=%u state=%u phase=%u height=%d flags=%u heading=%u checksum=%016llx\n",
                   host->course.id,world->tick,i,a->state,a->phase,a->height_q16,a->flags,a->heading,
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->boo_contacts)
        printf("3D source boo contact: course=%u tick=%u contacts=%016llx cue=%u pending=%u flash=%u\n",
               host->course.id,world->tick,(unsigned long long)events->boo_contacts,
               events->boo_touch_mask,world->boo_pending_mask,world->boo_flash_mask);
    if (world->boo_pending_mask!=before->boo_pending_mask || world->boo_flash_mask!=before->boo_flash_mask)
        printf("3D source boo palette: course=%u tick=%u pending=%u flash=%u\n",
               host->course.id,world->tick,world->boo_pending_mask,world->boo_flash_mask);
    for (unsigned i = 0; i < world->rock_count; ++i)
    {
        const PortableCourseProp *a=&world->rocks[i], *old=&before->rocks[i];
        if (world->tick==1 || memcmp(a,old,sizeof *a))
            printf("3D source rock state: course=%u tick=%u index=%u variant=%u state=%u timer=%d frame=%u flags=%u x=%d y=%d height=%d vz=%d affine=%d checksum=%016llx\n",
                   host->course.id,world->tick,i,a->source_code&65535u,a->state,a->lake_rock.timer,a->lake_rock.frame,a->flags,
                   a->x_q16,a->y_q16,a->height_q16,a->lake_rock.velocity_z_q16,a->affine_x_q8,
                   (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->rock_contacts)
        printf("3D source rock contact: course=%u tick=%u contacts=%016llx hit=%u protected=%u burn=%u\n",
               host->course.id,world->tick,(unsigned long long)events->rock_contacts,
               events->rock_hit_mask,events->rock_protected_mask,events->lava_burn_mask);
    for (unsigned kind=0;kind<4;++kind)
        if (events->rock_cue_masks[kind])
            printf("3D source rock cue: course=%u tick=%u song=%u actors=%016llx\n",
                   host->course.id,world->tick,386u+kind,(unsigned long long)events->rock_cue_masks[kind]);
    for (unsigned i = 0; i < world->falling_star_count; ++i)
    {
        const PortableFallingStar *a = &world->falling_stars[i];
        if (world->tick == 1u || memcmp(a, &before->falling_stars[i], sizeof *a))
            printf("3D source falling star state: course=%u tick=%u index=%u state=%u timer=%d frame=%u flags=%u affine=%u angle=%u x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->state, a->timer, a->art, a->flags,
                a->affine_q8, a->angle, a->x_q16, a->y_q16, a->height_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->falling_star_contacts)
        printf("3D source falling star contact: course=%u tick=%u actors=%016llx hit=%u protected=%u\n",
            host->course.id, world->tick, (unsigned long long)events->falling_star_contacts,
            events->falling_star_hit_mask, events->falling_star_protected_mask);
    for (unsigned i = 0; i < world->common_prop_count; ++i)
    {
        const PortableCourseProp *a = &world->common_props[i];
        if (world->tick == 1u || memcmp(a, &before->common_props[i], sizeof *a))
            printf("3D source common prop state: course=%u tick=%u index=%u code=%08x state=%u timer=%d art=%u flags=%u phase=%u affine_x=%d affine_y=%d x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->source_code, a->state, a->timer,
                a->source_code == 0x860000u ? a->contact_phase : 0u, a->flags,
                a->contact_phase, a->affine_x_q8, a->affine_y_q8,
                a->x_q16, a->y_q16, a->height_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->common_prop_contacts)
        printf("3D source common prop contact: course=%u tick=%u actors=%016llx hit=%u protected=%u open=%u close=%u\n",
            host->course.id, world->tick, (unsigned long long)events->common_prop_contacts,
            events->common_prop_hit_mask, events->common_prop_protected_mask,
            events->common_prop_umbrella_open_mask, events->common_prop_umbrella_close_mask);
    const PortableMeshItems *actor_items = shared_race(host) ? &host->race.items : &host->trial_items;
    for (unsigned i = 0; i < world->pier_boo_count; ++i)
    {
        const PortableCourseProp *a = &world->pier_boos[i];
        if (world->tick == 1u || memcmp(a, &before->pier_boos[i], sizeof *a))
            printf("3D source pier boo state: course=%u tick=%u index=%u state=%u phase=%u flags=%u affine_x=%d affine_y=%d vz=%d x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->state, a->pier_boo.phase, a->flags,
                a->affine_x_q8, a->affine_y_q8, a->pier_boo.velocity_z_q16,
                a->x_q16, a->y_q16, a->height_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->pier_boo_contacts || events->source_haunt_assigned_mask ||
        events->source_haunt_music_start_mask || events->source_haunt_music_release_mask)
        printf("3D source pier boo contact: course=%u tick=%u actors=%016llx touch=%u protected=%u assigned=%u clock_mask=%u music_start=%u music_release=%u spin=%u\n",
            host->course.id, world->tick, (unsigned long long)events->pier_boo_contacts,
            events->pier_boo_touch_mask, events->pier_boo_protected_mask,
            events->source_haunt_assigned_mask, events->source_haunt_clock_mask,
            events->source_haunt_music_start_mask, events->source_haunt_music_release_mask,
            actor_items->status.racers[0].spin_ticks);
    if (world->pier_boo_count)
        for (unsigned slot = 0; slot < PORTABLE_BOO_ATTACK_CAPACITY; ++slot)
        {
            const PortableBooAttack *a = &world->source_haunts[slot];
            const int32_t *origin = world->source_haunt_origins_q16[slot];
            if ((world->tick == 1u && a->occupied) ||
                memcmp(a, &before->source_haunts[slot], sizeof *a) ||
                memcmp(origin, before->source_haunt_origins_q16[slot], 3 * sizeof *origin))
                printf("3D source haunt state: course=%u tick=%u slot=%u source=%u target=%u occupied=%u visible=%u phase=%u timer=%u radius=%u angle=%u bob=%u frame=%u affine_y=%d x=%d y=%d z=%d vx=%d vy=%d vz=%d origin_x=%d origin_y=%d origin_z=%d lock=%u checksum=%016llx\n",
                    host->course.id, world->tick, slot, world->source_haunt_source_indices[slot],
                    a->target, a->occupied, a->visible, a->phase, a->timer, a->radius,
                    a->angle, a->bob_angle, a->frame, a->affine_y_q8,
                    a->x_q16, a->y_q16, a->z_q16, a->vx_q16, a->vy_q16, a->vz_q16,
                    origin[0], origin[1], origin[2],
                    a->target < actor_items->status.count ? actor_items->status.racers[a->target].boo_lock_ticks : 0u,
                    (unsigned long long)portable_mesh_course_actors_checksum(world));
        }
    for (unsigned racer = 0; racer < actor_items->status.count; ++racer)
        if ((events->source_haunt_clock_mask | events->source_haunt_assigned_mask) & (1u << racer))
        {
            unsigned music = 0;
            for (unsigned slot = 0; slot < PORTABLE_BOO_ATTACK_CAPACITY; ++slot)
                if (world->source_haunts[slot].occupied && world->source_haunts[slot].target == racer &&
                    (world->source_haunts[slot].phase == 2 || world->source_haunts[slot].phase == 3)) music = 1;
            printf("3D source haunt clock: course=%u tick=%u racer=%u before=%u after=%u owned=%u assigned=%u music=%u\n",
                host->course.id, world->tick, racer, events->source_haunt_lock_clock[racer],
                actor_items->status.racers[racer].boo_lock_ticks,
                (world->source_haunt_lock_mask >> racer) & 1u,
                (events->source_haunt_assigned_mask >> racer) & 1u, music);
        }
    for (unsigned i = 0; i < world->kamek_count; ++i)
    {
        const PortableCourseProp *a = &world->kameks[i];
        if (world->tick == 1u || memcmp(a, &before->kameks[i], sizeof *a))
            printf("3D source kamek state: course=%u tick=%u index=%u state=%u timer=%d phase=%u heading=%u facing=%u flags=%u affine_x=%d affine_y=%d vx=%d vz=%d x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->state, a->timer, a->kamek.phase,
                a->kamek.heading, a->kamek.has_facing, a->flags, a->affine_x_q8,
                a->affine_y_q8, a->kamek.velocity_x_q16, a->kamek.velocity_z_q16,
                a->x_q16, a->y_q16, a->height_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->kamek_contacts)
        printf("3D source kamek contact: course=%u tick=%u actors=%016llx touch=%u protected=%u spin=%u\n",
            host->course.id, world->tick, (unsigned long long)events->kamek_contacts,
            events->kamek_touch_mask, events->kamek_protected_mask,
            actor_items->status.racers[0].spin_ticks);
    for (unsigned i = 0; i < world->mechakoopa_count; ++i)
    {
        const PortableCourseProp *a = &world->mechakoopas[i];
        if (world->tick == 1u || memcmp(a, &before->mechakoopas[i], sizeof *a))
            printf("3D source mechakoopa state: course=%u tick=%u index=%u variant=%u state=%u timer=%d cycle=%u stride_phase=%u stride_ticks=%u heading=%u facing=%u flags=%u affine_x=%d affine_y=%d x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->source_code & 65535u, a->state, a->timer,
                a->mechakoopa.cycle, a->mechakoopa.animation_phase, a->mechakoopa.animation_ticks,
                a->mechakoopa.heading, a->mechakoopa.has_facing, a->flags,
                a->affine_x_q8, a->affine_y_q8, a->x_q16, a->y_q16, a->height_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->mechakoopa_contacts)
        printf("3D source mechakoopa contact: course=%u tick=%u actors=%016llx hit=%u protected=%u strong=%u spin=%u\n",
            host->course.id, world->tick, (unsigned long long)events->mechakoopa_contacts,
            events->mechakoopa_hit_mask, events->mechakoopa_protected_mask,
            events->mechakoopa_strong_mask, actor_items->status.racers[0].spin_ticks);
    for (unsigned i = 0; i < world->thundercloud_count; ++i)
    {
        const PortableThundercloud *a = &world->thunderclouds[i];
        if (world->tick == 1u || memcmp(a, &before->thunderclouds[i], sizeof *a) ||
            memcmp(world->thundercloud_palette_phase, before->thundercloud_palette_phase, 2))
            printf("3D source thundercloud state: course=%u tick=%u index=%u state=%d timer=%d palette_timer=%d art=%u frame=%u flags=%u phase0=%u phase1=%u x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->state, a->timer, a->palette_timer,
                a->art, world->thundercloud_palette_phase[a->variant / 2u] * 8u + a->art,
                a->flags, world->thundercloud_palette_phase[0], world->thundercloud_palette_phase[1],
                a->x_q16, a->y_q16, a->height_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->thundercloud_contacts)
        printf("3D source thundercloud contact: course=%u tick=%u actors=%016llx hit=%u protected=%u\n",
            host->course.id, world->tick, (unsigned long long)events->thundercloud_contacts,
            events->thundercloud_hit_mask, events->thundercloud_protected_mask);
    if (world->thundercloud_count)
        for (unsigned i = 0; i < actor_items->status.count; ++i)
        {
            const unsigned remaining = actor_items->status.racers[i].lightning_ticks;
            if (remaining || world->tick == 1u ||
                ((events->thundercloud_hit_mask | events->thundercloud_protected_mask) & (1u << i)))
                printf("3D source thundercloud shrink: course=%u tick=%u racer=%u remaining=%u\n",
                    host->course.id, world->tick, i, remaining);
        }
    for (unsigned i = 0; i < world->teepee_count; ++i)
    {
        const PortableCourseProp *a = &world->teepees[i], *old = &before->teepees[i];
        const PortableTeepeeMotion *m = &a->teepee;
        if (world->tick == 1u || memcmp(a, old, sizeof *a) || m->rider_state == 1u)
            printf("3D source teepee state: course=%u tick=%u index=%u state=%u counter=%u frame=%u flags=%u victim=%u rider=%u lock=%u x=%d y=%d z=%d checksum=%016llx\n",
                host->course.id, world->tick, i, a->state, m->counter, m->frame, a->flags,
                m->kart, m->rider_state, m->kart < actor_items->status.count ?
                    actor_items->status.racers[m->kart].boo_lock_ticks : 0u,
                m->rider_x_q16, m->rider_y_q16, m->rider_z_q16,
                (unsigned long long)portable_mesh_course_actors_checksum(world));
    }
    if (events->teepee_contacts || events->teepee_attached || events->teepee_flung)
        printf("3D source teepee contact: course=%u tick=%u actors=%016llx touch=%u protected=%u assigned=%u released=%u attached=%016llx flung=%016llx\n",
            host->course.id, world->tick, (unsigned long long)events->teepee_contacts,
            events->teepee_touch_mask, events->teepee_protected_mask,
            events->teepee_assigned_mask, events->teepee_release_mask,
            (unsigned long long)events->teepee_attached, (unsigned long long)events->teepee_flung);
    for (unsigned i = 0; i < 8; ++i)
        if (world->capture[i].remaining)
            printf("3D source capture: course=%u tick=%u racer=%u remaining=%u actor=%u spin=%u hidden=%u\n",
                   host->course.id, world->tick, i, world->capture[i].remaining,
                   world->capture[i].actor_index_plus1, world->capture[i].spin,
                   (unsigned)(world->capture[i].remaining <= 59));
    if (events->capture_mask || events->launch_mask)
        printf("3D source capture event: course=%u tick=%u captured=%u launched=%u\n",
               host->course.id, world->tick, events->capture_mask, events->launch_mask);

}

static void tick_local_race(PortableCourse3DHost *host, const PortableMeshKartInput *inputs,
                            uint16_t menu_buttons)
{
    const uint16_t pressed = menu_buttons & (uint16_t)~host->previous;
    host->previous = menu_buttons;
    ++host->tick;
    ++host->menu_age;
    if (host->paused && host->wait_for_neutral)
    {
        if (menu_buttons == 0u)
            host->wait_for_neutral = 0u;
        return;
    }
    if (host->paused || host->race.state.results_ready)
    {
        if (host->paused && !host->race.state.results_ready && pause_graphics(host, pressed))
            return;
        local_race_menu(host, pressed);
        return;
    }
    if (pressed & PORTABLE_FRONTEND_BUTTON_START)
    {
        portable_course_3d_host_set_paused(host, 1);
        return;
    }
    char detail[192] = {0};
    PortableMeshKartEvents events[PORTABLE_MESH_RACE_MAX];
    const PortableMeshCourseActors actors_before = host->race.actors;
    uint16_t boo_before[PORTABLE_MESH_RACE_MAX] = {0};
    uint16_t held_before[PORTABLE_MESH_RACE_MAX] = {0};
    for (unsigned i = 0; i < host->racers; ++i)
    {
        boo_before[i] = host->race.items.status.racers[i].boo_lock_ticks;
        held_before[i] = host->race.items.inventory.racers[i].held_item;
    }
    if (portable_mesh_race_tick_items(&host->race, inputs, host->item_controls, events, detail,
                                      sizeof detail) != PORTABLE_MESH_OK)
    {
        trial_error(host, detail);
        host->driving = 0;
        return;
    }
    log_actor_turn(host, &host->race.actors, &actors_before, &host->race.actor_events,
                   host->race.items.status.racers[0].spin_ticks);
    /* Bananas spin a kart out; shells and bombs tumble it (presentation). */
    for (uint32_t i = 0; i < host->race.item_events.count; ++i)
    {
        const PortableMeshItemEvent *event = &host->race.item_events.values[i];
        if (event->target < 32u && (event->kind == PORTABLE_MESH_EVENT_BOMB_CONTACT ||
                                    (event->kind == PORTABLE_MESH_EVENT_HIT && event->item != 0x1005u)))
            host->tumble_mask |= UINT32_C(1) << event->target;
        else if (event->target < 32u && event->kind == PORTABLE_MESH_EVENT_HIT)
            host->tumble_mask &= ~(UINT32_C(1) << event->target);
    }
    for (unsigned i = 0; i < host->race.items.status.count && i < 32u; ++i)
        if (!host->race.items.status.racers[i].spin_ticks)
            host->tumble_mask &= ~(UINT32_C(1) << i);
    if (host->race.items_enabled)
        for (uint32_t i = 0; i < host->race.item_events.count; ++i)
        {
            const PortableMeshItemEvent *event = &host->race.item_events.values[i];
            printf(
                "3D item event: course=%u tick=%u kind=%u item=%04x racer=%u target=%u serial=%u\n",
                host->course.id, host->race.state.tick, event->kind, event->item, event->racer,
                event->target, event->serial);
            if (event->kind == PORTABLE_MESH_EVENT_USE && event->racer < host->racers)
                printf("3D held item use: course=%u tick=%u racer=%u item=%04x held_before=%04x boo_before=%u boo_after=%u star_after=%u\n",
                       host->course.id, host->race.state.tick, event->racer, event->item,
                       held_before[event->racer], boo_before[event->racer],
                       host->race.items.status.racers[event->racer].boo_lock_ticks,
                       host->race.items.status.racers[event->racer].star_ticks);
            if (event->kind == PORTABLE_MESH_EVENT_PICKUP)
                printf("3D item pickup: course=%u tick=%u racer=%u placement=%u fraction=%llu\n",
                       host->course.id, host->race.state.tick, event->racer, event->placement,
                       (unsigned long long)event->fraction_q32);
        }
    if (host->mode == PORTABLE_COURSE_3D_BATTLE)
    {
        const PortableMeshBattleEvents *battle = &host->race.battle_events;
        for (unsigned i = 0; i < host->racers; ++i)
            if ((battle->popped_mask | battle->bomb_mask) & (1u << i))
                printf("3D Battle balloons: tick=%u racer=%u remaining=%u bomb=%u\n",
                       host->race.state.tick, i, host->race.battle.balloons[i],
                       host->race.battle.elimination[i].revenge_bomb);
        for (unsigned i = 0; i < host->race.item_events.count; ++i)
            if (host->race.item_events.values[i].kind == PORTABLE_MESH_EVENT_BOMB_CONTACT)
                printf("3D Battle detonate: tick=%u racer=%u\n", host->race.state.tick,
                       host->race.item_events.values[i].target);
    }
    for (unsigned i = 0; i < host->race.item_events.count; ++i)
        if (host->race.item_events.values[i].kind == PORTABLE_MESH_EVENT_HIT &&
            host->race.item_events.values[i].target < PORTABLE_MESH_RACE_MAX)
            host->hazard_reaction[host->race.item_events.values[i].target] = PORTABLE_MESH_HAZARD_SPIN;
    for (unsigned i = 0; i < host->racers; ++i)
        if (events[i].hazard)
        {
            host->hazard_reaction[i] = events[i].hazard_reaction;
            printf("3D hazard hit: course=%u tick=%u racer=%u reaction=%u\n", host->course.id,
                   host->race.state.tick, i, events[i].hazard_reaction);
        }
    memcpy(host->audio_events, events, sizeof events);
    ++host->audio_serial;
    for (unsigned i = 0; i < host->players; ++i)
        host->audio_buttons[i] = inputs[i].buttons;
    for (unsigned i = 0; i < host->players; ++i)
    {
        const uint32_t value = (uint32_t)(uint16_t)inputs[i].steering | (uint32_t)inputs[i].buttons
                                                                            << 16u;
        for (unsigned b = 0; b < 4u; ++b)
            host->local_input_hashes[i] =
                (host->local_input_hashes[i] ^ ((value >> (b * 8u)) & 255u)) *
                UINT64_C(1099511628211);
    }
    if (host->race.state.results_ready)
    {
        if (host->mode == 1u)
        {
            if (portable_mesh_cup_finish(&host->cup, &host->race, detail, sizeof detail) !=
                PORTABLE_MESH_OK)
            {
                trial_error(host, detail);
                host->driving = 0;
                return;
            }
            for (unsigned i = 0; i < 8u; ++i)
                printf("3D GP award: course=%u racer=%u before=%u awarded=%u after=%u\n",
                       host->course.id, i + 1u,
                       (unsigned)(host->cup.points[i] - host->cup.awards[i]), host->cup.awards[i],
                       host->cup.points[i]);
            printf("3D GP finish: cup=%u index=%u course=%u rank=%u stage=%u ticks=%u credits=%u\n",
                   host->cup.definition.cup_id, host->cup.course_index, host->course.id,
                   host->cup.player_rank, (unsigned)host->cup.stage, host->cup.finish_ticks,
                   host->cup.retry_credits);
        }
        host->menu_age = host->action_cursor = 0;
        unsigned finished = 0;
        for (unsigned i = 0; i < host->racers; ++i)
            finished += host->race.state.racers[i].finish_ticks != 0u;
        if (host->mode == PORTABLE_COURSE_3D_BATTLE)
        {
            printf(
                "3D Battle finish: course=%u players=%u tick=%u finished=1 views=%u checksum=%016llx\n",
                host->course.id, host->players, host->race.state.tick, host->players,
                (unsigned long long)portable_mesh_race_battle_checksum(&host->race));
            for (unsigned i = 0; i < host->racers; ++i)
                printf("3D Battle result: course=%u player=%u driver=%u rank=%u human=%u\n",
                       host->course.id, i + 1u, host->driver_ids[i],
                       host->race.state.finish_rank[i], i < host->players);
        }
        if (host->mode == PORTABLE_COURSE_3D_QUICK)
            printf("3D Quick finish: course=%u players=%u tick=%u finished=%u views=%u cpus=%u\n",
                   host->course.id, host->players, host->race.state.tick, finished, host->players,
                   host->racers - host->players);
        else if (host->mode == PORTABLE_COURSE_3D_VS)
            printf("3D VS finish: course=%u players=%u tick=%u finished=%u views=%u\n",
                   host->course.id, host->players, host->race.state.tick, finished, host->players);
        for (unsigned i = 0; i < host->racers && host->mode != PORTABLE_COURSE_3D_BATTLE; ++i)
        {
            printf(
                "3D %s result: course=%u player=%u driver=%u rank=%u retired=%u finish_ticks=%u input=%016llx lap=%u stage=%u\n",
                race_name(host), host->course.id, i + 1u, host->driver_ids[i],
                host->race.state.finish_rank[i], host->race.state.retired[i],
                host->race.state.racers[i].finish_ticks,
                (unsigned long long)(i < host->players ? host->local_input_hashes[i] : 0u),
                host->race.state.racers[i].lap, host->race.state.racers[i].checkpoint);
            if (host->mode == PORTABLE_COURSE_3D_QUICK || host->mode == 1u)
            {
                uint32_t ticks = 0;
                int estimated = 0;
                if (!portable_mesh_race_result_ticks(&host->race, i, &ticks, &estimated))
                {
                    trial_error(host, "invalid frozen result time");
                    return;
                }
                printf(
                    "3D %s time: racer=%u driver=%u rank=%u ticks=%u estimated=%d actual_finish_ticks=%u lap=%u stage=%u cpu_visits=%u\n",
                    race_name(host), i + 1u, host->driver_ids[i], host->race.state.finish_rank[i],
                    ticks, estimated, host->race.state.racers[i].finish_ticks,
                    host->race.state.racers[i].lap, host->race.state.racers[i].checkpoint,
                    host->race.cpu[i].visit_serial);
            }
        }
    }
}

static void finish_trial(PortableCourse3DHost *host)
{
    host->menu_age = host->action_cursor = 0;
    if (host->record_valid &&
        (portable_mesh_course_actors_required(&host->actor_config)
             ? portable_mesh_replay_write_end_actors(
                   &host->writer, &host->trial,
                   host->item_profile_enabled ? &host->trial_items : NULL, &host->trial_actors)
         : host->item_profile_enabled
             ? portable_mesh_replay_write_end_items(&host->writer, &host->trial, &host->trial_items)
             : portable_mesh_replay_write_end(&host->writer, &host->trial)) !=
            PORTABLE_MESH_REPLAY_OK)
    {
        host->record_valid = 0;
        trial_error(host, "recording finalization failed");
    }
    if (host->best_path[0])
    {
        PortableMeshPersonalBest best = {host->trial.state.finish_ticks, host->best_lap_tick},
                                 saved;
        int updated = 0;
        PortableMeshStoreStatus status = portable_mesh_personal_best_update(
            host->best_path, &host->identity, &best, &saved, &updated);
        if (status != PORTABLE_MESH_STORE_OK)
            trial_error(host, portable_mesh_store_status_name(status));
        else if (host->record_valid && best.race_ticks <= saved.race_ticks)
        {
            status = portable_mesh_ghost_save(host->ghost_path, &host->identity,
                                              host->recording.bytes, host->recording.size);
            if (status != PORTABLE_MESH_STORE_OK)
                trial_error(host, portable_mesh_store_status_name(status));
        }
    }
    printf("3D Time Trial finish: course=%u ticks=%u laps=%u checksum=%016llx recording=%u\n",
           host->course.id, host->trial.state.finish_ticks, host->trial.state.lap,
           (unsigned long long)portable_mesh_time_trial_checksum(&host->trial.state),
           host->record_valid);
}

static void begin_replay(PortableCourse3DHost *host)
{
    char detail[192] = {0};
    if (!host->record_valid)
    {
        trial_error(host, "no complete recording");
        return;
    }
    if (portable_mesh_time_trial_init(&host->replay_trial, &host->course, host->trial.rules, 0u, detail,
                                      sizeof detail) != PORTABLE_MESH_OK)
    {
        trial_error(host, detail);
        return;
    }
    host->replay_trial.geometry = host_geometry(host);
    host->replay_input =
        (PortableReplayMemoryInput){host->recording.bytes, host->recording.size, 0};
    host->replay_reader = portable_replay_memory_reader(&host->replay_input);
    host->replay_items = host->trial_items;
    if (host->item_profile_enabled &&
        portable_mesh_items_init(&host->replay_items, &host->item_config, host->item_seed, detail,
                                 sizeof detail) != PORTABLE_MESH_ITEMS_OK)
    {
        trial_error(host, detail);
        return;
    }
    if (!portable_mesh_course_actors_init_on_course(&host->actor_config, host_geometry(host), &host->replay_actors, detail,
                                          sizeof detail))
    {
        trial_error(host, detail);
        return;
    }
    const PortableMeshReplayStatus status =
        portable_mesh_course_actors_required(&host->actor_config)
            ? portable_mesh_replay_playback_init_actors(
                  &host->replay_playback, &host->replay_reader, &host->identity,
                  &host->replay_trial, host->item_profile_enabled ? &host->replay_items : NULL,
                  host->item_profile_enabled ? &host->item_config : NULL, &host->replay_actors,
                  &host->actor_config)
        : host->item_profile_enabled
            ? portable_mesh_replay_playback_init_items(&host->replay_playback, &host->replay_reader,
                                                       &host->identity, &host->replay_trial,
                                                       &host->replay_items, &host->item_config)
            : portable_mesh_replay_playback_init(&host->replay_playback, &host->replay_reader,
                                                 &host->identity, &host->replay_trial);
    if (status != PORTABLE_MESH_REPLAY_OK)
    {
        trial_error(host, portable_mesh_replay_status_name(status));
        return;
    }
    host->freeze_valid = 0;
    host->pose_valid = 0;
    reset_audio_events(host);
    host->replaying = 1;
    host->menu_age = 0;
}

static void trial_menu(PortableCourse3DHost *host, uint16_t pressed)
{
    if (pressed & PORTABLE_FRONTEND_BUTTON_UP)
        host->action_cursor = (host->action_cursor + 2u) % 3u;
    if (pressed & PORTABLE_FRONTEND_BUTTON_DOWN)
        host->action_cursor = (host->action_cursor + 1u) % 3u;
    if (host->paused && (pressed & (PORTABLE_FRONTEND_BUTTON_B | PORTABLE_FRONTEND_BUTTON_START)))
    {
        portable_course_3d_host_set_paused(host, 0);
        return;
    }
    if (!host->paused && (pressed & PORTABLE_FRONTEND_BUTTON_B))
    {
        host->driving = 0;
        return;
    }
    if (!(pressed & PORTABLE_FRONTEND_BUTTON_A))
        return;
    if (host->action_cursor == 2u)
    {
        host->driving = host->paused = 0;
        return;
    }
    if (host->paused && host->action_cursor == 0u)
        portable_course_3d_host_set_paused(host, 0);
    else if (host->paused || host->action_cursor == 0u)
        (void)begin_trial(host);
    else
        begin_replay(host);
}

static void tick_explore(PortableCourse3DHost *preview, uint16_t buttons)
{
    if (!portable_course_3d_host_active(preview))
        return;
    const uint16_t pressed = buttons & (uint16_t)~preview->previous;
    preview->previous = buttons;
    ++preview->tick;
    if (pressed & PORTABLE_FRONTEND_BUTTON_B)
    {
        if (preview->flying)
            preview->flying = 0u;
        else
            preview->active = 0u;
        return;
    }
    if (!preview->flying)
    {
        if (!preview->visible_count)
            return;
        if (pressed & (PORTABLE_FRONTEND_BUTTON_DOWN | PORTABLE_FRONTEND_BUTTON_UP))
        {
            const size_t position = visible_cursor(preview);
            const size_t next =
                (pressed & PORTABLE_FRONTEND_BUTTON_DOWN)
                    ? (position + 1u) % preview->visible_count
                    : (position + preview->visible_count - 1u) % preview->visible_count;
            preview->cursor = preview->visible[next];
            if (!select_course(preview))
            {
                fprintf(stderr, "%s\n", preview->error);
                preview->active = 0u;
                return;
            }
        }
        if (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))
        {
            reset_camera(preview);
            preview->flying = 1u;
            preview->wait_for_neutral = 1u;
            printf("3D course preview: course=%u name=%s\n", preview->course.id,
                   preview->meta.name);
        }
        return;
    }
    if (preview->wait_for_neutral)
    {
        if (buttons == 0u)
            preview->wait_for_neutral = 0u;
        return;
    }
    if (pressed & PORTABLE_FRONTEND_BUTTON_START)
        reset_camera(preview);
    if (buttons & PORTABLE_FRONTEND_BUTTON_LEFT)
        preview->yaw -= 0.025;
    if (buttons & PORTABLE_FRONTEND_BUTTON_RIGHT)
        preview->yaw += 0.025;
    if (buttons & PORTABLE_FRONTEND_BUTTON_L)
        preview->position[2] -= 0.20;
    if (buttons & PORTABLE_FRONTEND_BUTTON_R)
        preview->position[2] += 0.20;
    if (buttons & PORTABLE_FRONTEND_BUTTON_A)
        preview->pitch += 0.015;
    if (buttons & PORTABLE_FRONTEND_BUTTON_SELECT)
        preview->pitch -= 0.015;
    if (preview->pitch > 1.3)
        preview->pitch = 1.3;
    if (preview->pitch < -1.3)
        preview->pitch = -1.3;
    preview->yaw = fmod(preview->yaw, 6.283185307179586);
    const double direction = (buttons & PORTABLE_FRONTEND_BUTTON_UP)     ? 0.35
                             : (buttons & PORTABLE_FRONTEND_BUTTON_DOWN) ? -0.35
                                                                         : 0.0;
    preview->position[0] += sin(preview->yaw) * direction;
    preview->position[1] -= cos(preview->yaw) * direction;
    /* This is a presentation camera, never a physics state or replay stream. */
    for (unsigned i = 0u; i < 3u; ++i)
    {
        if (preview->position[i] > 4096.0)
            preview->position[i] = 4096.0;
        if (preview->position[i] < -4096.0)
            preview->position[i] = -4096.0;
    }
}

static void tick_driving(PortableCourse3DHost *host, PortableMeshKartInput input,
                         uint16_t menu_buttons)
{
    if (!portable_course_3d_host_active(host))
        return;
    if (!host->driving && host->launch)
    {
        host->launch = 0u;
        host->previous = menu_buttons;
        if (begin_selected_race(host))
            start_intro(host);
        else
            host->launch_failed = 1u;
        return;
    }
    if (!host->driving)
    {
        const uint16_t pressed = menu_buttons & (uint16_t)~host->previous;
        if (!host->flying && host->visible_count && host->meta.physics_profile_id &&
            (pressed & PORTABLE_FRONTEND_BUTTON_A))
        {
            host->previous = menu_buttons;
            if (begin_selected_race(host))
                start_intro(host);
        }
        else
            tick_explore(host, menu_buttons);
        return;
    }
    const uint16_t pressed = menu_buttons & (uint16_t)~host->previous;
    host->previous = menu_buttons;
    ++host->tick;
    ++host->menu_age;
    /* A focus/mobile pause can arrive in the same turn as Start or Back.
     * Require release before accepting that same event as a resume action. */
    if (host->paused && host->wait_for_neutral)
    {
        if (menu_buttons == 0u)
            host->wait_for_neutral = 0u;
        return;
    }
    if (host->paused)
    {
        if (!pause_graphics(host, pressed))
            trial_menu(host, pressed);
        return;
    }
    if (host->replaying)
    {
        if (pressed & PORTABLE_FRONTEND_BUTTON_START)
        {
            portable_course_3d_host_set_paused(host, 1);
            return;
        }
        if (pressed & PORTABLE_FRONTEND_BUTTON_B)
        {
            host->replaying = 0;
            reset_audio_events(host);
            return;
        }
        const PortableMeshReplayStatus status = portable_mesh_replay_playback_tick_actor_events(
            &host->replay_playback, NULL, &host->audio_events[0], &host->trial_item_events,
            &host->actor_events);
        if (status == PORTABLE_MESH_REPLAY_OK)
        {
            ++host->audio_serial;
        }
        if (status == PORTABLE_MESH_REPLAY_COMPLETE)
        {
            host->replaying = 0;
            reset_audio_events(host);
            host->menu_age = 0;
            printf("3D Time Trial replay: PASS course=%u ticks=%u\n", host->course.id,
                   host->replay_playback.ticks);
        }
        else if (status != PORTABLE_MESH_REPLAY_OK)
        {
            host->replaying = 0;
            reset_audio_events(host);
            trial_error(host, portable_mesh_replay_status_name(status));
        }
        return;
    }
    if (host->trial.state.phase == PORTABLE_MESH_TT_FINISHED)
    {
        trial_menu(host, pressed);
        return;
    }
    if (pressed & PORTABLE_FRONTEND_BUTTON_START)
    {
        portable_course_3d_host_set_paused(host, 1);
        return;
    }
    char detail[192] = {0};
    const uint32_t old_lap = host->trial.state.lap;
    const PortableMeshCourseActors actors_before = host->trial_actors;
    PortableMeshKartEvents events;
    const PortableMeshItemControls item_controls =
        host->item_profile_enabled ? host->item_controls[0] : (PortableMeshItemControls){0};
    const PortableMeshStatus status =
        portable_mesh_course_actors_required(&host->actor_config)
            ? portable_mesh_time_trial_tick_actors(
                  &host->trial, host->item_profile_enabled ? &host->trial_items : NULL,
                  host->item_profile_enabled ? &host->item_config : NULL, &host->trial_actors,
                  &host->actor_config, input, item_controls, &events, &host->trial_item_events,
                  &host->actor_events, detail, sizeof detail)
        : host->item_profile_enabled
            ? portable_mesh_time_trial_tick_items(
                  &host->trial, &host->trial_items, &host->item_config, input,
                  host->item_controls[0], &events, &host->trial_item_events, detail, sizeof detail)
            : portable_mesh_time_trial_tick(&host->trial, input, &events, detail, sizeof detail);
    if (status != PORTABLE_MESH_OK)
    {
        trial_error(host, detail[0] ? detail : portable_mesh_status_name(status));
        host->driving = 0;
        return;
    }
    log_actor_turn(host, &host->trial_actors, &actors_before, &host->actor_events,
                   host->trial_items.status.racers[0].spin_ticks);
    for (unsigned i = 0; i < host->trial_item_events.count; ++i)
        if (host->trial_item_events.values[i].kind == PORTABLE_MESH_EVENT_HIT)
            host->hazard_reaction[0] = PORTABLE_MESH_HAZARD_SPIN;
    if (events.hazard)
    {
        host->hazard_reaction[0] = events.hazard_reaction;
        printf("3D hazard hit: course=%u tick=%u racer=0 reaction=%u\n", host->course.id,
               host->trial.state.tick, events.hazard_reaction);
    }
    host->audio_events[0] = events;
    host->audio_buttons[0] = input.buttons;
    ++host->audio_serial;
    if (host->record_valid &&
        (portable_mesh_course_actors_required(&host->actor_config)
             ? portable_mesh_replay_write_tick_actors(
                   &host->writer, input, item_controls, &host->trial,
                   host->item_profile_enabled ? &host->trial_items : NULL, &host->trial_actors)
         : host->item_profile_enabled
             ? portable_mesh_replay_write_tick_items(&host->writer, input, host->item_controls[0],
                                                     &host->trial, &host->trial_items)
             : portable_mesh_replay_write_tick(&host->writer, input, &host->trial)) !=
            PORTABLE_MESH_REPLAY_OK)
    {
        host->record_valid = 0;
        trial_error(host, "recording limit or write failure");
    }
    if (host->trial.state.lap != old_lap)
    {
        const uint32_t lap = host->trial.state.race_ticks - host->last_lap_tick;
        if (!host->best_lap_tick || lap < host->best_lap_tick)
            host->best_lap_tick = lap;
        host->last_lap_tick = host->trial.state.race_ticks;
    }
    if (host->ghost_active)
    {
        const PortableMeshReplayStatus ghost =
            portable_mesh_replay_playback_tick(&host->ghost_playback, NULL);
        if (ghost != PORTABLE_MESH_REPLAY_OK)
        {
            host->ghost_active = 0;
            if (ghost != PORTABLE_MESH_REPLAY_COMPLETE)
                trial_error(host, portable_mesh_replay_status_name(ghost));
        }
    }
    if (host->trial.state.phase == PORTABLE_MESH_TT_FINISHED)
        finish_trial(host);
}

static const PortableMeshTimeTrialSession *display_trial(const PortableCourse3DHost *host)
{
    return host->replaying ? &host->replay_trial : &host->trial;
}

/* META asset mksc:sound-set names the kind-114 sounds this course races with. */
static uint32_t sound_set(const PortableCourse3DHost *host)
{
    PortableCourse3DProperty property;
    return portable_course_3d_property_find(&host->course, host->meta.property_first, host->meta.property_count,
                                            "mksc:sound-set", &property) &&
                   property.type == PORTABLE_COURSE_3D_PROPERTY_ASSET &&
                   property.value.asset.kind == PORTABLE_ASSET_SECTION_SOUND_SET
               ? property.value.asset.id
               : 0u;
}

int portable_course_3d_host_audio(const PortableCourse3DHost *host, PortableCourse3DAudioView *out)
{
    if (!host || !out || !host->active || !host->driving)
        return 0;
    const int shared = shared_race(host);
    const PortableMeshRacerState *state =
        shared ? &host->race.state.racers[0] : &display_trial(host)->state;
    const unsigned rank = shared ? host->race.state.finish_rank[0]
                                       ? host->race.state.finish_rank[0]
                                       : host->race.state.standing_rank[0]
                                 : 1u;
    *out = (PortableCourse3DAudioView){
        .pack = host->pack,
        .state = state,
        .rules = shared ? &host->rules : display_trial(host)->rules,
        .items = host->item_profile_enabled ? (shared            ? &host->race.items
                                               : host->replaying ? &host->replay_items
                                                                 : &host->trial_items)
                                            : NULL,
        .item_events = shared ? &host->race.item_events : &host->trial_item_events,
        .actor_events = portable_mesh_course_actors_required(&host->actor_config) ? (shared ? &host->race.actor_events : &host->actor_events) : NULL,
        .source_actor_config = &host->actor_config,
        .source_actors = shared ? &host->race.actors : host->replaying ? &host->replay_actors : &host->trial_actors,
        .racers = shared ? host->race.state.racers : state,
        .driver_ids = host->driver_ids,
        .human_mask = (uint8_t)((1u << host->players) - 1u),
        .events = host->audio_events[0],
        .generation = host->audio_generation,
        .ordinal = host->audio_ordinal,
        .course_id = host->course.id,
        .driver_id = host->driver_id,
        .mode = host->mode,
        .engine_class = host->engine_class,
        .music_kind = host->meta.music_kind,
        .music_id = host->meta.music_id,
        .sound_set_id = sound_set(host),
        .lap_count = host->meta.laps,
        .buttons = host->audio_buttons[0],
        .paused = host->paused,
        .finished =
            shared ? host->race.state.results_ready : state->phase == PORTABLE_MESH_RACER_FINISHED,
        .rank = rank ? (uint8_t)(rank - 1u) : 0u,
        .racer_count = shared ? (uint8_t)host->racers : 1u,
        .emitters = host->sound_emitters,
        .emitter_count = host->sound_emitter_count,
        .ceremony = host->cup_ceremony,
        .revenge_bomb = (uint8_t)(host->mode == PORTABLE_COURSE_3D_BATTLE &&
                                  host->race.battle.elimination[0].revenge_bomb)};
    PortableCourse3DSurface surface;
    if (state->kart.grounded && portable_course_3d_surface(&host->course, state->kart.contact_surface, &surface))
        out->surface_sound = (uint8_t)surface.sound_surface;
    return 1;
}

/* Camera filters advance with accepted ticks; draw only reads these values. */
static int publish_chase(PortableCourse3DHost *host, unsigned cuts)
{
    PortableRenderChase previous[PORTABLE_MESH_LOCAL_MAX], current[PORTABLE_MESH_LOCAL_MAX];
    memcpy(previous, host->previous_camera, sizeof previous);
    memcpy(current, host->current_camera, sizeof current);
    const int shared = shared_race(host);
    const unsigned count = shared ? host->players : 1u;
    const PortableMeshItems *items = shared ? &host->race.items :
        host->replaying ? &host->replay_items : &host->trial_items;
    for (unsigned i = 0; i < count; ++i)
    {
        const PortableMeshRacerState *state = shared ? &host->race.state.racers[i] :
            &display_trial(host)->state;
        const PortableMeshKartState *kart = &state->kart;
        const int cut = (cuts & (1u << i)) || !(host->camera_mask & (1u << i));
        const double clearance = ((double)host->rules.kart_radius_q16 +
            host->rules.ride_height_q16) / 65536.0;
        PortableRenderPose pose;
        PortableRenderSupport support;
        PortableMeshCollisionPose posed;
        const PortableMeshCourse *geometry = posed_geometry(host, state->tick, &posed);
        if (!portable_render_pose_from_kart(&pose, kart) ||
            !portable_render_pose_support(&support, &pose, kart, geometry, clearance))
        {
            snprintf(host->error, sizeof host->error,
                "3D course=%u mode=%u racer=%u: chase support invalid", host->course.id, host->mode, i);
            host->camera_error = 1;
            return 0;
        }
        const int boosting = portable_mesh_kart_boosting(kart) || (host->item_profile_enabled && i < items->status.count &&
                (items->status.racers[i].mushroom_ticks || items->status.racers[i].star_ticks));
        const int64_t top = host->rules.forward_speed_q16 > 0 ? host->rules.forward_speed_q16 : 1;
        int64_t ratio = (int64_t)(kart->speed_q16 < 0 ? -(int64_t)kart->speed_q16 : kart->speed_q16) *
            65536 / top;
        if (ratio > 2 * 65536) ratio = 2 * 65536;
        const int spinning = host->item_profile_enabled && i < items->status.count &&
            items->status.racers[i].spin_ticks;
        int32_t gravity_up[3];
        portable_mesh_kart_gravity_up(kart, gravity_up);
        const PortableRenderChaseInput input = {.foot = support.body, .tick = state->tick,
            .gravity_up = {gravity_up[0] / 1073741824.0, gravity_up[1] / 1073741824.0,
                           gravity_up[2] / 1073741824.0},
            .outside_drift_q16 = kart->outside_drift_q16, .speed_ratio_q16 = (int32_t)ratio,
            .grounded = kart->grounded, .boosting = boosting != 0, .spinning = spinning != 0,
            .cut = cut != 0, .dynamic = host->settings.dynamic_camera != 0,
            .shake = host->settings.camera_shake != 0,
            .immersive = host->settings.immersive_camera != 0, .rig = host->chase_rig};
        char detail[128] = {0};
        PortableRenderChase next;
        const PortableRenderChaseStatus status = portable_render_chase_step(&next,
            &current[i], &input, geometry, detail, sizeof detail);
        if (status != PORTABLE_RENDER_CHASE_OK)
        {
            snprintf(host->error, sizeof host->error,
                "3D course=%u mode=%u racer=%u: chase (%u) %.120s", host->course.id,
                host->mode, i, (unsigned)status, detail);
            host->camera_error = 1;
            return 0;
        }
        if (cut || !current[i].valid || next.tick != current[i].tick)
        {
            previous[i] = cut ? next : current[i];
            current[i] = next;
        }
    }
    memcpy(host->previous_camera, previous, sizeof previous);
    memcpy(host->current_camera, current, sizeof current);
    host->camera_mask = (uint8_t)((1u << count) - 1u);
    host->camera_error = 0;
    return 1;
}

static uint32_t world_alpha(const PortableCourse3DHost *host)
{
    return host->interpolate && host->pose_valid && !host->paused && host->world_valid &&
        host->world_tick == motion_tick(host) ? host->presentation_alpha : 65536u;
}

/* Copies are published once per accepted world tick, independently of draws. */
static void publish_objects(PortableCourse3DHost *host, unsigned cuts)
{
    const uint32_t tick = motion_tick(host);
    const int reset = !host->world_valid || cuts || tick != host->world_tick + 1u;
    if (host->world_valid && tick == host->world_tick && !cuts) return;
    host->previous_actors = host->current_actors;
    memcpy(host->previous_items, host->current_items, sizeof host->previous_items);
    host->previous_item_count = host->current_item_count;
    host->current_actors = shared_race(host) ? host->race.actors
        : host->replaying ? host->replay_actors : host->trial_actors;
    const PortableMeshItems *items = shared_race(host) ? &host->race.items
        : host->replaying ? &host->replay_items : &host->trial_items;
    host->current_item_count = portable_mesh_items_actor_poses(items, host->current_items, PORTABLE_MESH_ITEMS_ACTORS);
    if (reset)
    {
        host->previous_actors = host->current_actors;
        memcpy(host->previous_items, host->current_items, sizeof host->previous_items);
        host->previous_item_count = host->current_item_count;
    }
    host->world_tick = tick;
    host->world_valid = 1;
}

static int publish_animation(PortableCourse3DHost *host, unsigned cuts)
{
    publish_objects(host, cuts);
    if (!host->kart_fx && !portable_mesh_kart_fx_art_open(host->pack, &host->kart_fx,
        host->error, sizeof host->error))
    {
        host->animation_error = 1;
        return 0;
    }
    PortableRenderKartAnimation previous[PORTABLE_MESH_RACE_MAX],
        current[PORTABLE_MESH_RACE_MAX];
    memcpy(previous, host->previous_animation, sizeof previous);
    memcpy(current, host->current_animation, sizeof current);
    const int shared = shared_race(host);
    const unsigned count = shared ? host->racers : host->ghost_active ? 2u : 1u;
    for (unsigned i = 0; i < count; ++i)
    {
        const PortableMeshRacerState *state = shared ? &host->race.state.racers[i] :
            i ? &host->ghost_trial.state : &display_trial(host)->state;
        const PortableMeshKartState *kart = &state->kart;
        const PortableMeshItems *items = shared ? &host->race.items : i ? &host->ghost_items :
            host->replaying ? &host->replay_items : &host->trial_items;
        const int cut = (cuts & (1u << i)) || !(host->animation_mask & (1u << i));
        const unsigned item_index = shared ? i : 0u;
        const PortableMeshCourseActors *actors = shared ? &host->race.actors : i ? &host->ghost_actors :
            host->replaying ? &host->replay_actors : &host->trial_actors;
        const PortableMeshItemRacerStatus *status = host->item_profile_enabled && item_index < items->status.count
            ? &items->status.racers[item_index] : NULL;
        const PortableMeshCaptureState *capture = &actors->capture[item_index];
        const double scale = status && status->lightning_ticks && !status->star_ticks
            ? host->item_rules.lightning_scale_ratio_q16 / 65536.0 : 1;
        PortableMeshKartState supported = *kart;
        if (capture->remaining) supported.grounded = 0;
        PortableRenderPose pose;
        PortableRenderSupport support;
        const double clearance = scale * ((double)host->rules.kart_radius_q16 +
            host->rules.ride_height_q16) / 65536.0;
        PortableMeshCollisionPose posed;
        const PortableMeshCourse *geometry = posed_geometry(host, state->tick, &posed);
        if (!portable_render_pose_from_kart(&pose, kart) ||
            !portable_render_pose_support(&support, &pose, &supported, geometry, clearance))
            goto invalid;
        PortableMeshSurface surface;
        int32_t roughness = 0;
        if (kart->grounded)
        {
            if (!geometry || !geometry->surface ||
                !geometry->surface(geometry->context, kart->contact_surface, &surface))
                goto invalid;
            /* Authored slowdown supplies a generic rough-ground visual ratio. */
            roughness = surface.speed_ratio_q16 < 65536 ? 65536 - surface.speed_ratio_q16 : 0;
            if (roughness > 65536) roughness = 65536;
        }
        const int boosting = portable_mesh_kart_boosting(kart) || (host->item_profile_enabled && item_index < items->status.count &&
                (items->status.racers[item_index].mushroom_ticks || items->status.racers[item_index].star_ticks));
        PortableRenderKartAnimationInput input = {.foot = support.body, .kart = *kart,
            .tick = state->tick, .top_speed_q16 = host->rules.forward_speed_q16,
            .roughness_q16 = roughness, .boosting = boosting != 0, .cut = cut != 0,
            .spark_lifetime = portable_mesh_kart_fx_art_lifetime(host->kart_fx, PORTABLE_MESH_KART_FX_SPARK),
            .flame_lifetime = portable_mesh_kart_fx_art_lifetime(host->kart_fx, PORTABLE_MESH_KART_FX_FLAME)};
        /* A tumbling or squashed racer's hit timer flips or flattens it instead of spinning it. */
        const unsigned reaction = status && status->spin_ticks && items != &host->ghost_items
            ? host->hazard_reaction[item_index] : PORTABLE_MESH_HAZARD_SPIN;
        const int spun = reaction != PORTABLE_MESH_HAZARD_TUMBLE && reaction != PORTABLE_MESH_HAZARD_SQUASH;
        input.status_spin = portable_mesh_kart_spin_turn(kart) * (2 * M_PI / 65536.0) +
            (status && status->spin_ticks && spun ? status->spin_ticks * 4 * M_PI / host->item_rules.hit_spin_ticks : 0) +
            (capture->remaining ? capture->spin * (2 * M_PI / 65536.0) : 0) +
            actors->strong_hit[item_index].angle * (2 * M_PI / 65536.0);
        input.spin_phase = status && status->spin_ticks ? 1.0 - (double)status->spin_ticks / host->item_rules.hit_spin_ticks : 0;
        const PortableDriverAffine *affine = &actors->crush[item_index].affine;
        input.status_scale[0] = input.status_scale[1] = scale * (affine->inverse_x_q8 ? 256.0 / affine->inverse_x_q8 : 1);
        input.status_scale[2] = scale * (affine->inverse_y_q8 ? 256.0 / affine->inverse_y_q8 : 1);
        if (reaction == PORTABLE_MESH_HAZARD_SQUASH)
        {
            /* Flat for most of the hit, springing back over its last quarter;
             * META mksc:squash-width/-height shape it at full flatten. */
            const double left = (double)status->spin_ticks / host->item_rules.hit_spin_ticks;
            const double flat = left > .25 ? 1 : left * 4;
            const double width = fixed_meta(host, "mksc:squash-width", 1.35);
            const double height = fixed_meta(host, "mksc:squash-height", .3);
            input.status_scale[0] *= 1 + (width - 1) * flat;
            input.status_scale[1] *= 1 + (width - 1) * flat;
            input.status_scale[2] *= 1 - (1 - height) * flat;
        }
        PortableRenderKartAnimation next;
        char detail[128] = {0};
        if (!portable_render_kart_animation_step(&next, &current[i], &input, detail, sizeof detail))
        {
            snprintf(host->error, sizeof host->error,
                "3D course=%u mode=%u racer=%u: animation %.120s", host->course.id, host->mode, i, detail);
            host->animation_error = 1;
            return 0;
        }
        if (cut || !current[i].valid || next.tick != current[i].tick)
        {
            previous[i] = cut ? next : current[i];
            current[i] = next;
            host->previous_support[i] = cut ? support : host->current_support[i];
            host->current_support[i] = support;
        }
        continue;
    invalid:
        snprintf(host->error, sizeof host->error,
            "3D course=%u mode=%u racer=%u: animation support invalid", host->course.id, host->mode, i);
        host->animation_error = 1;
        return 0;
    }
    memcpy(host->previous_animation, previous, sizeof previous);
    memcpy(host->current_animation, current, sizeof current);
    host->animation_mask = (uint16_t)((1u << count) - 1u);
    host->animation_error = 0;
    return 1;
}

static int animation_inhibited(const PortableCourse3DHost *host, const PortableMeshItems *items,
                               unsigned racer);

/* Kart effects advance with accepted ticks; drawing rewinds them by alpha.
 * A whole-race cut clears the pool; a single racer's cut skips its emission. */
static void publish_vfx(PortableCourse3DHost *host, unsigned cuts, int reset)
{
    const uint32_t flags = (host->settings.drift_sparks ? PORTABLE_RENDER_VFX_SPARKS : 0u) |
        (host->settings.boost_effects ? PORTABLE_RENDER_VFX_BOOST : 0u) |
        (host->settings.kart_trails ? PORTABLE_RENDER_VFX_TRAILS : 0u);
    const int shared = shared_race(host);
    unsigned count = shared ? host->racers : host->ghost_active ? 2u : 1u;
    if (count > PORTABLE_RENDER_VFX_KARTS) count = PORTABLE_RENDER_VFX_KARTS;
    PortableRenderVfxKart karts[PORTABLE_RENDER_VFX_KARTS];
    memset(karts, 0, sizeof karts);
    const double clearance = ((double)host->rules.kart_radius_q16 + host->rules.ride_height_q16) / 65536.0;
    const double top = host->rules.forward_speed_q16 > 0 ? host->rules.forward_speed_q16 : 65536.0;
    uint32_t tick = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        const PortableMeshRacerState *state = shared ? &host->race.state.racers[i] :
            i ? &host->ghost_trial.state : &display_trial(host)->state;
        const PortableMeshKartState *kart = &state->kart;
        const PortableMeshItems *items = shared ? &host->race.items : i ? &host->ghost_items :
            host->replaying ? &host->replay_items : &host->trial_items;
        const unsigned item_index = shared ? i : 0u;
        if (!i) tick = state->tick;
        PortableRenderPose pose;
        PortableRenderSupport support;
        PortableMeshCollisionPose posed;
        const PortableMeshCourse *geometry = posed_geometry(host, state->tick, &posed);
        if ((cuts & (1u << i)) || animation_inhibited(host, items, item_index) ||
            !portable_render_pose_from_kart(&pose, kart) ||
            !portable_render_pose_support(&support, &pose, kart, geometry, clearance))
            continue;
        PortableMeshSurface surface;
        const int rough = kart->grounded && geometry && geometry->surface &&
            geometry->surface(geometry->context, kart->contact_surface, &surface) &&
            surface.speed_ratio_q16 < 65536;
        const int boosting = portable_mesh_kart_boosting(kart) || (host->item_profile_enabled && item_index < items->status.count &&
                (items->status.racers[item_index].mushroom_ticks || items->status.racers[item_index].star_ticks));
        PortableRenderVfxKart *k = &karts[i];
        k->foot = support.body;
        for (unsigned a = 0; a < 3; ++a)
            k->velocity[a] = kart->velocity_q16[a] / 65536.0;
        k->radius = host->rules.kart_radius_q16 / 65536.0;
        k->scale = kart_scale(host);
        k->speed_ratio = fmin(2, fabs((double)kart->speed_q16) / top);
        k->hidden_views = portable_mesh_item_scene_kart_hidden_views(items, item_index,
            shared ? host->players : 1u);
        k->own_views = shared ? (i < host->players ? 1u << i : 0u) : i ? 0u : 1u;
        k->drift_direction = kart->drift_direction;
        k->active = 1;
        k->grounded = kart->grounded;
        k->drift_tier = kart->drift_tier;
        k->boosting = boosting != 0;
        k->drafting = kart->slip_charge > 0;
        k->rough = (uint8_t)rough;
        PortableCourse3DSurface floor;
        k->wet = (uint8_t)(kart->grounded && portable_course_3d_surface(&host->course, kart->contact_surface, &floor) &&
                           floor.splash);
    }
    portable_render_vfx_set_art(&host->vfx, host->fx_set);
    const int fresh = !host->vfx.valid || reset || tick != host->vfx.tick;
    (void)portable_render_vfx_step(&host->vfx, tick, flags, reset, karts, count);
    /* Placed emitters run on each new tick, sized like the karts. */
    const PortableFxSet *fx_rows = portable_mesh_fx_set_rows(host->fx_set);
    for (uint32_t i = 0; fresh && fx_rows && i < host->particle_emitter_count; ++i)
    {
        const PortableFxEmitter *row = portable_fx_set_emitter(fx_rows, host->particle_emitters[i]);
        if (!row)
            continue;
        PortableFxEmitter scaled = *row;
        const double s = kart_scale(host);
        scaled.size_q16 = (int32_t)(row->size_q16 * s);
        scaled.spread_q16 = (int32_t)(row->spread_q16 * s);
        scaled.gravity_q16 = (int32_t)(row->gravity_q16 * s);
        scaled.radius_q16 = (int32_t)(row->radius_q16 * s);
        for (unsigned a = 0; a < 3u; ++a)
            scaled.velocity_q16[a] = (int32_t)(row->velocity_q16[a] * s);
        (void)portable_render_vfx_emit(&host->vfx, &scaled, &host->particle_poses[i], i * 7919u + 1u);
    }
    const char *trace = SDL_getenv("MKSC_AID_MESH_VFX");
    if (trace && strcmp(trace, "1") == 0)
    {
        unsigned particles, skids;
        portable_render_vfx_counts(&host->vfx, &particles, &skids);
        printf("3D VFX: course=%u tick=%u flags=%u particles=%u skids=%u drift=%d tier=%u boost=%u\n",
            host->course.id, tick, flags, particles, skids, karts[0].drift_direction,
            karts[0].drift_tier, karts[0].boosting);
    }
}

/* Publish presentation pairs only at the accepted tick boundary. Rendering
 * derives local values from these copies and cannot move the race clocks. */
static void host_tick_locals(PortableCourse3DHost *host,
                             const PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX],
                             uint16_t menu_buttons);
static void host_tick_driving(PortableCourse3DHost *host, PortableMeshKartInput input,
                              uint16_t menu_buttons);
void portable_course_3d_host_tick_driving(PortableCourse3DHost *host, PortableMeshKartInput input,
                                          uint16_t menu_buttons)
{
    if (!host)
        return;
    host_tick_driving(host, input, menu_buttons);
    direct_return(host);
}
static void host_tick_driving(PortableCourse3DHost *host, PortableMeshKartInput input,
                              uint16_t menu_buttons)
{
    if (!host || tick_intro(host, menu_buttons))
        return;
    if (shared_race(host))
    {
        const PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX] = {input};
        host_tick_locals(host, inputs, menu_buttons);
        return;
    }
    memset(host->audio_events, 0, sizeof host->audio_events);
    ++host->audio_ordinal;
    const PortableMeshTimeTrialSession *before = display_trial(host);
    const uint32_t old_tick = before->state.tick;
    const uint32_t old_grace = before->state.respawn_grace;
    const uint32_t old_respawn = before->state.kart.respawn_ticks;
    const uint32_t old_ghost_grace = host->ghost_trial.state.respawn_grace;
    const uint32_t old_ghost_respawn = host->ghost_trial.state.kart.respawn_ticks;
    const uint8_t old_replay = host->replaying;
    PortableRenderPose previous[2];
    int valid = host->driving && portable_render_pose_from_kart(&previous[0], &before->state.kart);
    if (valid && host->ghost_active)
        valid = portable_render_pose_from_kart(&previous[1], &host->ghost_trial.state.kart);
    tick_driving(host, input, menu_buttons);
    if (!host->driving)
    {
        host->pose_valid = 0;
        return;
    }
    const PortableMeshTimeTrialSession *current = display_trial(host);
    const int snap = !valid || !host->pose_valid || old_replay != host->replaying || host->paused ||
                     current->state.phase == PORTABLE_MESH_TT_FINISHED ||
                     current->state.respawn_grace > old_grace ||
                     (old_respawn == 0u) != (current->state.kart.respawn_ticks == 0u) ||
                     host->pose_ghost != host->ghost_active;
    if (!snap && old_tick == current->state.tick)
        return;
    PortableRenderPose poses[2];
    valid = portable_render_pose_from_kart(&poses[0], &current->state.kart);
    if (valid && host->ghost_active)
        valid = portable_render_pose_from_kart(&poses[1], &host->ghost_trial.state.kart);
    if (!valid)
    {
        host->pose_valid = 0;
        return;
    }
    const int ghost_snap =
        host->ghost_trial.state.respawn_grace > old_ghost_grace ||
        (old_ghost_respawn == 0u) != (host->ghost_trial.state.kart.respawn_ticks == 0u);
    const unsigned count = host->ghost_active ? 2u : 1u;
    for (unsigned i = 0; i < count; ++i)
    {
        host->current_pose[i] = poses[i];
        host->previous_pose[i] = (snap || (i == 1u && ghost_snap)) ? poses[i] : previous[i];
    }
    host->pose_ghost = host->ghost_active;
    host->pose_valid = 1;
    const int camera_cut = old_replay != host->replaying || current->state.tick < old_tick ||
        current->state.respawn_grace > old_grace ||
        (old_respawn == 0u) != (current->state.kart.respawn_ticks == 0u);
    if (!publish_chase(host, camera_cut ? 1u : 0u))
        fprintf(stderr, "%s\n", host->error); /* Later publishers reuse host->error. */
    if (!publish_animation(host, (camera_cut ? 1u : 0u) | (ghost_snap ? 2u : 0u)))
        fprintf(stderr, "%s\n", host->error);
    publish_vfx(host, ghost_snap ? 2u : 0u, camera_cut);
}

void portable_course_3d_host_tick_locals(
    PortableCourse3DHost *host, const PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX],
    uint16_t menu_buttons)
{
    if (!host || !inputs)
        return;
    host_tick_locals(host, inputs, menu_buttons);
    direct_return(host);
}
static void host_tick_locals(PortableCourse3DHost *host,
                             const PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX],
                             uint16_t menu_buttons)
{
    if (!host || !inputs || tick_intro(host, menu_buttons))
        return;
    if (!shared_race(host))
    {
        host_tick_driving(host, inputs[0], menu_buttons);
        return;
    }
    memset(host->audio_events, 0, sizeof host->audio_events);
    ++host->audio_ordinal;
    PortableRenderPose before[PORTABLE_MESH_RACE_MAX];
    uint32_t grace[PORTABLE_MESH_RACE_MAX], respawn[PORTABLE_MESH_RACE_MAX];
    const uint32_t old_tick = host->race.state.tick;
    int valid = host->driving != 0;
    for (unsigned i = 0; i < host->racers; ++i)
    {
        const PortableMeshRacerState *state = &host->race.state.racers[i];
        grace[i] = state->respawn_grace;
        respawn[i] = state->kart.respawn_ticks;
        if (!portable_render_pose_from_kart(&before[i], &state->kart))
            valid = 0;
    }
    if (host->driving)
        tick_local_race(host, inputs, menu_buttons);
    else
        tick_driving(host, inputs[0], menu_buttons);
    if (!host->driving)
    {
        host->pose_valid = 0;
        return;
    }
    const int shared_cut =
        !valid || !host->pose_valid || host->paused || host->race.state.results_ready;
    if (!shared_cut && old_tick == host->race.state.tick)
        return;
    unsigned camera_cuts = host->race.state.tick < old_tick ? 255u : 0u;
    for (unsigned i = 0; i < host->racers; ++i)
    {
        PortableRenderPose pose;
        if (!portable_render_pose_from_kart(&pose, &host->race.state.racers[i].kart))
        {
            host->pose_valid = 0;
            return;
        }
        const PortableMeshRacerState *state = &host->race.state.racers[i];
        const int discontinuity = valid && (state->respawn_grace > grace[i] ||
            (respawn[i] == 0u) != (state->kart.respawn_ticks == 0u));
        const int snap = shared_cut || discontinuity;
        if (discontinuity) camera_cuts |= 1u << i;
        host->current_pose[i] = pose;
        host->previous_pose[i] = snap ? pose : before[i];
    }
    host->pose_valid = 1;
    host->pose_ghost = 0;
    if (!publish_chase(host, camera_cuts))
        fprintf(stderr, "%s\n", host->error); /* Later publishers reuse host->error. */
    if (!publish_animation(host, camera_cuts))
        fprintf(stderr, "%s\n", host->error);
    publish_vfx(host, camera_cuts, camera_cuts == 255u);
}

void portable_course_3d_host_set_presentation(PortableCourse3DHost *host, uint32_t alpha_q16,
                                              int interpolate, int cut)
{
    if (!host)
        return;
    host->presentation_alpha = alpha_q16 > 65536u ? 65536u : alpha_q16;
    host->interpolate = interpolate != 0;
    if (cut)
        host->pose_valid = 0;
}

static int presented_pose(const PortableCourse3DHost *host, unsigned index,
                          PortableRenderPose *pose)
{
    const int local = shared_race(host);
    const PortableMeshKartState *kart = local   ? &host->race.state.racers[index].kart
                                        : index ? &host->ghost_trial.state.kart
                                                : &display_trial(host)->state.kart;
    if (!host->pose_valid || (!local && index && !host->pose_ghost))
        return portable_render_pose_from_kart(pose, kart);
    return portable_render_pose_interpolate(pose, &host->previous_pose[index],
                                            &host->current_pose[index],
                                            host->interpolate && !host->paused ? host->presentation_alpha : 65536u);
}

/* The box is immutable local geometry; each published kart supplies only its
 * ground-foot transform. Player and ghost share this mesh and differ by tint. */
static void prepare_kart_mesh(PortableCourse3DHost *host)
{
    if (host->kart_mesh.vertices && host->kart_radius == host->rules.kart_radius_q16)
        return;
    portable_render_gpu_forget_meshes(host->gpu);
    portable_render_gles_forget_meshes(host->gles);
    host->ceremony_bounds_valid = 0;
    host->kart_radius = host->rules.kart_radius_q16;
    const double radius = (double)host->kart_radius / 65536.0;
    for (unsigned i = 0; i < 8u; ++i)
        host->kart_vertices[i] = (PortableRenderVertex){
            .position = {radius * ((i & 1u) ? 1.0 : -1.0), radius * ((i & 2u) ? 1.5 : -1.5),
                         (i & 4u) ? 0.65 : 0.20}};
    static const uint32_t faces[36] = {0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
    memcpy(host->kart_indices, faces, sizeof faces);
    host->kart_material = (PortableRenderMaterial){.argb = UINT32_MAX, .flags = 4u};
    host->kart_run = (PortableRenderRun){0, 36, 0};
    host->kart_mesh = (PortableRenderMesh){.vertices = host->kart_vertices,
                                           .indices = host->kart_indices,
                                           .materials = &host->kart_material,
                                           .runs = &host->kart_run,
                                           .vertex_count = 8,
                                           .index_count = 36,
                                           .material_count = 1,
                                           .run_count = 1};
}

static const PortableMeshItems *presented_items(const PortableCourse3DHost *host)
{
    return !host->item_profile_enabled ? NULL
           : shared_race(host)         ? &host->race.items
           : host->replaying           ? &host->replay_items
                                       : &host->trial_items;
}
static const PortableMeshCourseActors *presented_actors(const PortableCourse3DHost *host,
    const PortableMeshItems *items)
{
    if (!host->driving || host->cup_ceremony)
        return NULL;
    return items == &host->ghost_items ? &host->ghost_actors
        : shared_race(host) ? &host->race.actors
        : host->replaying ? &host->replay_actors : &host->trial_actors;
}
static const PortableMeshCaptureState *presented_capture(const PortableCourse3DHost *host,
    const PortableMeshItems *items, unsigned racer)
{
    const PortableMeshCourseActors *actors = presented_actors(host, items);
    return actors && racer < 8 ? &actors->capture[racer] : NULL;
}
static int presented_animation(const PortableCourse3DHost *host, unsigned racer,
                                PortableRenderKartAnimationSnapshot *out)
{
    if (host->animation_error || racer >= PORTABLE_MESH_RACE_MAX ||
        !(host->animation_mask & (1u << racer))) return 0;
    return portable_render_kart_animation_snapshot(out, &host->previous_animation[racer],
        &host->current_animation[racer], host->interpolate && host->pose_valid && !host->paused ?
            host->presentation_alpha : 65536u);
}
static unsigned animation_owner(const PortableCourse3DHost *host, const PortableMeshItems *items,
                                unsigned racer)
{
    return items == &host->ghost_items ? 1u : racer;
}
static int animation_inhibited(const PortableCourse3DHost *host, const PortableMeshItems *items,
                               unsigned racer)
{
    const PortableMeshCourseActors *actors = presented_actors(host, items);
    const PortableMeshItemRacerStatus *status = items && racer < items->status.count ?
        &items->status.racers[racer] : NULL;
    return (status && status->spin_ticks) || (actors && racer < 8 &&
        (actors->crush[racer].contact_timer || actors->crush[racer].affine.mode ||
         actors->strong_hit[racer].active || actors->strong_hit[racer].pending ||
         actors->capture[racer].remaining)) ||
        (host->mode == PORTABLE_COURSE_3D_BATTLE &&
         host->race.battle.elimination[racer].revenge_bomb);
}

/* Opens binding's rig into skin when the model changes; 0 without one. */
static int bind_skin(PortableCourse3DHost *host, RacerSkin *skin, const PortableModelBinding *binding,
                     const PortableRenderMesh *rest)
{
    if (skin->binding != binding || skin->rest != rest->vertices)
    {
        skin->binding = binding;
        skin->rest = rest->vertices;
        skin->key = UINT32_MAX;
        skin->rigged = portable_model_rig_open(host->pack, binding->mesh_id, &binding->mesh, &skin->rig);
        if (skin->rigged && skin->capacity < rest->vertex_count)
        {
            PortableRenderVertex *vertices = realloc(skin->vertices, rest->vertex_count * sizeof *vertices);
            if (!vertices) skin->rigged = 0;
            else
            {
                skin->vertices = vertices;
                skin->capacity = rest->vertex_count;
            }
        }
    }
    return skin->rigged;
}

/* A racer's driver model for this frame. Rigged models skin a per-racer copy
 * at fractional clip frames: spinning out, finished (top three win), the
 * cup ceremony, or else the drive clip by steering. Others draw the rest mesh. */
static const PortableRenderMesh *posed_model(PortableCourse3DHost *host, unsigned racer,
    const PortableModelBinding *binding, size_t resource, const PortableMeshItemRacerStatus *status,
    double steer, double clock, double spin_phase)
{
    const PortableRenderMesh *rest = &host->model_resources[resource].mesh;
    RacerSkin *skin = &host->skins[racer];
    if (!bind_skin(host, skin, binding, rest)) return rest;
    uint32_t tag = PORTABLE_MODEL_CLIP_TAG('D','R','I','V');
    double phase = (1.0 - steer) / 2.0; /* frame 0 leans into a right turn */
    const int finished = shared_race(host)
        ? host->race.state.racers[racer].phase == PORTABLE_MESH_RACER_FINISHED
        : host->trial.state.phase == PORTABLE_MESH_TT_FINISHED;
    if (host->cup_ceremony)
        tag = PORTABLE_MODEL_CLIP_TAG('W','I','N',' ');
    else if (status && status->spin_ticks && host->item_rules.hit_spin_ticks)
    {
        tag = PORTABLE_MODEL_CLIP_TAG('S','P','I','N');
        phase = spin_phase;
    }
    else if (finished)
    {
        const unsigned rank = !shared_race(host) ? 1u : host->race.state.finish_rank[racer]
            ? host->race.state.finish_rank[racer] : host->race.state.standing_rank[racer] + 1u;
        tag = rank <= 3u ? PORTABLE_MODEL_CLIP_TAG('W','I','N',' ') : PORTABLE_MODEL_CLIP_TAG('L','O','S','E');
    }
    int clip = portable_model_rig_clip(&skin->rig, tag);
    if (clip < 0) clip = portable_model_rig_clip(&skin->rig, PORTABLE_MODEL_CLIP_TAG('D','R','I','V'));
    if (clip < 0) return rest;
    const PortableModelClip *c = &skin->rig.clips[clip];
    const double frame = c->flags & PORTABLE_MODEL_CLIP_LOOP ? fmod(clock, c->frames)
        : fmin(fmax(phase, 0.0), 1.0) * (c->frames - 1u);
    const uint32_t key = (uint32_t)clip;
    if (key != skin->key || frame != skin->sample)
    {
        if (!portable_render_model_skin_sample(rest, &skin->rig, (uint32_t)clip, frame, skin->vertices)) return rest;
        skin->mesh = *rest;
        skin->mesh.vertices = skin->vertices;
        skin->mesh.revision = ++skin->revision;
        skin->key = key;
        skin->sample = frame;
    }
    return &skin->mesh;
}

/* Source kart art is authored at .05 m per pixel, about 1.4 m across, while
 * the collision sphere is 1 m wide. Drawing karts at this scale keeps the
 * visible body on its physical footprint, so bumps and track width read true. */
#define KART_ART_SCALE .72
#define WHEEL_STEER .4 /* front tire yaw at full lock, radians */

/* A racer's kart body. A clipless four-node rig names its tires (front pair
 * first): they roll by interpolated accepted distance and the
 * front pair turns with steering. Other bodies draw the rest mesh. */
static const PortableRenderMesh *posed_kart(PortableCourse3DHost *host, unsigned racer, const RacerKart *seated,
                                            double distance, double steer)
{
    const PortableRenderMesh *rest = &host->model_resources[seated->resource].mesh;
    RacerSkin *skin = &host->kart_skins[racer];
    if (!bind_skin(host, skin, &seated->body, rest) || skin->rig.node_count != 4u || skin->rig.clip_count)
        return rest;
    if (skin->key == UINT32_MAX)
    {
        double low[4][3], high[4][3];
        for (unsigned n = 0; n < 4u; ++n)
            for (unsigned a = 0; a < 3u; ++a)
            {
                low[n][a] = HUGE_VAL;
                high[n][a] = -HUGE_VAL;
            }
        for (uint32_t i = 0; i < rest->vertex_count; ++i)
            if (skin->rig.nodes[i] < 4u)
                for (unsigned a = 0; a < 3u; ++a)
                {
                    low[skin->rig.nodes[i]][a] = fmin(low[skin->rig.nodes[i]][a], rest->vertices[i].position[a]);
                    high[skin->rig.nodes[i]][a] = fmax(high[skin->rig.nodes[i]][a], rest->vertices[i].position[a]);
                }
        for (unsigned n = 0; n < 4u; ++n)
        {
            if (!(high[n][2] > low[n][2]))
                return rest;
            for (unsigned a = 0; a < 3u; ++a)
                skin->wheel_center[n][a] = (low[n][a] + high[n][a]) / 2;
            skin->wheel_radius[n] = (high[n][2] - low[n][2]) / 2;
        }
    }
    /* Absolute accepted distance makes redraws, skipped frames and split views identical. */
    const double yaw = WHEEL_STEER * fmin(fmax(steer, -1.0), 1.0);
    if (skin->key == 0 && distance == skin->wheel_distance && yaw == skin->wheel_yaw)
        return &skin->mesh;
    skin->wheel_distance = distance;
    skin->wheel_yaw = yaw;
    double matrices[4][12];
    for (unsigned n = 0; n < 4u; ++n)
    {
        const double angle = fmod(distance / (skin->wheel_radius[n] * .05 * KART_ART_SCALE * kart_scale(host)), 2 * M_PI);
        const double cr = cos(angle), sr = sin(angle);
        const double cy = n < 2u ? cos(yaw) : 1, sy = n < 2u ? sin(yaw) : 0;
        /* Rz(yaw) * Rx(roll) about the tire's centre. */
        const double r[9] = {cy, -sy * cr, sy * sr, sy, cy * cr, -cy * sr, 0, sr, cr};
        const double *c = skin->wheel_center[n];
        for (unsigned row = 0; row < 3u; ++row)
        {
            for (unsigned a = 0; a < 3u; ++a)
                matrices[n][row * 4u + a] = r[row * 3u + a];
            matrices[n][row * 4u + 3u] =
                c[row] - (r[row * 3u] * c[0] + r[row * 3u + 1u] * c[1] + r[row * 3u + 2u] * c[2]);
        }
    }
    if (!portable_render_model_skin_nodes(rest, &skin->rig, (const double (*)[12])matrices, skin->vertices))
        return rest;
    skin->mesh = *rest;
    skin->mesh.vertices = skin->vertices;
    skin->mesh.revision = ++skin->revision;
    skin->key = 0;
    return &skin->mesh;
}

/* `up` without its part along the camera's line of sight (unit length). */
static void facing_camera(double up[3], const PortableRenderView *view)
{
    double along = 0, length = 0;
    for (unsigned a = 0; a < 3u; ++a) along += up[a] * view->forward[a];
    double out[3];
    for (unsigned a = 0; a < 3u; ++a) { out[a] = up[a] - along * view->forward[a]; length += out[a] * out[a]; }
    if (length < 1e-6) return;
    length = sqrt(length);
    for (unsigned a = 0; a < 3u; ++a) up[a] = out[a] / length;
}

static int kart_instance(PortableCourse3DHost *host, PortableRenderInstance *instance,
                         const PortableRenderPose *pose, uint32_t color, unsigned racer,
                         const PortableRenderView *view, unsigned pane, const PortableMeshItems *items,
                         const PortableRenderSupport *support, const PortableRenderMesh **kart_body)
{
    const PortableMeshItemRacerStatus *status =
        items && racer < items->status.count && !host->cup_ceremony ? &items->status.racers[racer]
                                                                    : NULL;
    const double scale = status && status->lightning_ticks && !status->star_ticks
                             ? (double)host->item_rules.lightning_scale_ratio_q16 / 65536.0
                             : 1.0;
    const double clearance =
        scale * (double)((int64_t)host->rules.kart_radius_q16 + host->rules.ride_height_q16) /
        65536.0;
    const int bomb = host->mode == PORTABLE_COURSE_3D_BATTLE &&
                     host->race.battle.elimination[racer].revenge_bomb;
    const PortableModelBinding *binding = bomb                ? NULL
                                          : shared_race(host) ? host->racer_models[racer]
                                                              : host->driver_model;
    const size_t resource =
        shared_race(host) ? host->racer_resources[racer] : host->driver_resource;
    /* The ceremony shows clean karts: no burn, crush or hit spin carried over. */
    const PortableMeshCourseActors *actors = host->cup_ceremony ? NULL : presented_actors(host, items);
    const unsigned burn = actors && racer < 8 && !(status && status->star_ticks) && !bomb
                              ? actors->lava_burn_ticks[racer] : 0;
    const uint8_t burn_bank = burn ? (burn > 10 ? 5u : 6u) : 0u;
    const uint8_t palette_bank = burn_bank ? burn_bank :
        actors && racer < 8 && !bomb && !(status && status->star_ticks) &&
        (actors->boo_flash_mask & (1u << racer)) ? 7u : 0u;
    int palette_tint = palette_bank && binding;
    /* KSP1 effect data: Time Trial ghosts and the Boo user's own view draw
     * the art translucent at the set's alphas instead of tinted/flickering. */
    const PortableDriverSprites *sprites =
        !binding && !bomb ? portable_mesh_driver_art_sprites(host->driver_art[racer]) : NULL;
    const int ghost = items && items == &host->ghost_items;
    const int boo_view = sprites && sprites->boo_alpha && status && status->boo_invisible_ticks && !ghost &&
        (shared_race(host) ? racer == pane && racer < host->players : racer == 0);
    const uint32_t translucent = !sprites ? 0u : ghost && sprites->ghost_alpha ? sprites->ghost_alpha
                                 : boo_view ? sprites->boo_alpha : 0u;
    PortableRenderSupport unseated;
    if (!support)
    {
        if (!portable_render_pose_support(&unseated, pose, NULL, NULL, clearance)) return 0;
        support = &unseated;
    }
    PortableRenderPose facing = support->body;
    PortableRenderKartAnimationSnapshot animation = {.pose = facing, .body_scale = {1,1,1},
        .top_speed_q16 = host->rules.forward_speed_q16};
    const int animated = !host->cup_ceremony && host->driving &&
        !animation_inhibited(host, items, racer);
    if (host->driving && !host->cup_ceremony &&
        !presented_animation(host, animation_owner(host,items,racer), &animation)) return 0;
    if (animated)
    {
        const double right_axis[3] = {
            facing.up[1]*facing.forward[2]-facing.up[2]*facing.forward[1],
            facing.up[2]*facing.forward[0]-facing.up[0]*facing.forward[2],
            facing.up[0]*facing.forward[1]-facing.up[1]*facing.forward[0]};
        for (unsigned a = 0; a < 3; ++a)
        {
            facing.position[a] += facing.up[a] * animation.bob;
            facing.up[a] = facing.up[a]*cos(animation.lean) + right_axis[a]*sin(animation.lean);
        }
    }
    for (unsigned a = 0; a < 3; ++a) facing.position[a] += .003 * facing.up[a];
    const PortableMeshCaptureState *capture = host->cup_ceremony ? NULL : presented_capture(host, items, racer);
    const double spin = host->driving && !host->cup_ceremony ? animation.status_spin : 0;
    const double right[3] = {
        facing.up[1] * facing.forward[2] - facing.up[2] * facing.forward[1],
        facing.up[2] * facing.forward[0] - facing.up[0] * facing.forward[2],
        facing.up[0] * facing.forward[1] - facing.up[1] * facing.forward[0]};
    for (unsigned a = 0; a < 3; ++a)
        facing.forward[a] = facing.forward[a] * cos(spin) + right[a] * sin(spin);
    /* A tumble rolls the racer once forward over its hit timer. */
    const PortableMeshItemRacerStatus *hit = host->driving && !host->cup_ceremony && host->item_profile_enabled &&
        items && items != &host->ghost_items && racer < items->status.count ? &items->status.racers[racer] : NULL;
    if (hit && hit->spin_ticks && host->item_rules.hit_spin_ticks &&
        host->hazard_reaction[shared_race(host) ? racer : 0u] == PORTABLE_MESH_HAZARD_TUMBLE)
    {
        const double flip = 2 * M_PI * (1.0 - (double)hit->spin_ticks / host->item_rules.hit_spin_ticks);
        double turned[2][3];
        for (unsigned a = 0; a < 3; ++a)
        {
            turned[0][a] = facing.forward[a] * cos(flip) + facing.up[a] * sin(flip);
            turned[1][a] = facing.up[a] * cos(flip) - facing.forward[a] * sin(flip);
        }
        memcpy(facing.forward, turned[0], sizeof turned[0]);
        memcpy(facing.up, turned[1], sizeof turned[1]);
    }
    const int status_animated = host->driving && !host->cup_ceremony;
    const double art_scale = KART_ART_SCALE * kart_scale(host);
    const double body_scale[3] = {
        art_scale * (status_animated ? animation.status_scale[0] : 1) * (animated ? animation.body_scale[0] : 1),
        art_scale * (status_animated ? animation.status_scale[1] : 1) * (animated ? animation.body_scale[1] : 1),
        art_scale * (status_animated ? animation.status_scale[2] : 1) * (animated ? animation.body_scale[2] : 1)};
    PortableCharacterViewPose source_pose = {.angle=64};
    double yaw_correction = 0;
    animation.pose = facing;
    const int selected = animated && items != &host->ghost_items &&
        (shared_race(host) ? racer == pane && racer < host->players : racer == 0);
    if (view && !portable_render_kart_animation_view(&animation, view,
        host->character_rules_valid ? &host->character_rules : NULL, selected,
        &source_pose, &yaw_correction)) return 0;
    PortableRenderPose unleaned = support->body;
    for (unsigned a=0;a<3;++a)
        unleaned.position[a] += ((animated ? animation.bob : 0)+.003)*unleaned.up[a];
    PortableRenderInstance baseline;
    double lean_bounds[6], lean_lift = 0;
    if (binding)
    {
        /* Source cells retain authored bins; model yaw crosses those bins
         * continuously between the same accepted steering endpoints. */
        if (animated && view && host->interpolate && !host->paused && host->pose_valid)
        {
            const unsigned owner = animation_owner(host, items, racer);
            const PortableRenderKartAnimation *ends[2] = {
                &host->previous_animation[owner], &host->current_animation[owner]};
            double yaw[2];
            for (unsigned end = 0; end < 2; ++end)
            {
                PortableRenderKartAnimationSnapshot sample = animation;
                sample.steer_q16 = ends[end]->steer_q16;
                sample.speed_q16 = ends[end]->speed_q16;
                sample.top_speed_q16 = ends[end]->top_speed_q16;
                PortableCharacterViewPose ignored;
                if (!portable_render_kart_animation_view(&sample, view,
                    host->character_rules_valid ? &host->character_rules : NULL,
                    selected, &ignored, &yaw[end])) return 0;
            }
            yaw_correction = yaw[0] + host->presentation_alpha / 65536.0 * remainder(yaw[1] - yaw[0], 2 * M_PI);
        }
        const double c = cos(yaw_correction), sn = sin(yaw_correction);
        const double side[3] = {facing.up[1]*facing.forward[2]-facing.up[2]*facing.forward[1],
            facing.up[2]*facing.forward[0]-facing.up[0]*facing.forward[2],
            facing.up[0]*facing.forward[1]-facing.up[1]*facing.forward[0]};
        const double before_side[3] = {
            unleaned.up[1]*unleaned.forward[2]-unleaned.up[2]*unleaned.forward[1],
            unleaned.up[2]*unleaned.forward[0]-unleaned.up[0]*unleaned.forward[2],
            unleaned.up[0]*unleaned.forward[1]-unleaned.up[1]*unleaned.forward[0]};
        for (unsigned a=0;a<3;++a)
        {
            facing.forward[a] = facing.forward[a]*c + side[a]*sn;
            unleaned.forward[a] = unleaned.forward[a]*c + before_side[a]*sn;
        }
        portable_render_instance_identity(instance, posed_model(host, animation_owner(host, items, racer),
            binding, resource, status, animated ? animation.steer_q16 / 65536.0 : 0.0,
            fmax(0, (double)host->tick - 1 + world_alpha(host) / 65536.0), animation.spin_phase));
        if (!portable_render_model_transform_scaled(instance->transform, binding, &facing, 0,
                                                     .05, body_scale))
            return 0;
        baseline=*instance;
        if (animated && !portable_render_model_transform_scaled(baseline.transform,binding,
            &unleaned,0,.05,body_scale)) return 0;
        memcpy(lean_bounds,host->model_bounds[resource],sizeof lean_bounds);
        const RacerKart *seated = !bomb ? &host->racer_karts[shared_race(host) ? racer : 0u] : NULL;
        if (seated && seated->present && kart_body)
            *kart_body = posed_kart(host, animation_owner(host, items, racer), seated,
                                    animation.wheel_distance,
                                    animated ? animation.steer_q16 / 65536.0 : 0.0);
        if (seated && seated->present)
            for (unsigned a=0;a<3;++a)
            {
                /* Kart vertices sit at -seat in the driver's frame. */
                const double low=host->model_bounds[seated->resource][a]-seated->seat[a];
                const double high=host->model_bounds[seated->resource][a+3]-seated->seat[a];
                if (low<lean_bounds[a]) lean_bounds[a]=low;
                if (high>lean_bounds[a+3]) lean_bounds[a+3]=high;
            }
    }
    else
    {
        /* Select art from this camera's line of sight; spin changes the source
         * direction, never the camera-facing plane or the simulation pose. */
        const uint8_t angle = source_pose.angle;
        if (!bomb && animated)
            portable_mesh_driver_art_set_wheel(host->driver_art[racer], animation.wheel_distance);
        if (sprites)
        {
            /* KSP1 rows show the kart's pitch; shell and bomb hits play the
             * tumble frames, mirrored by the side the camera sees, not the spin. */
            const double nose = fmax(-1.0, fmin(1.0, support->body.forward[2]));
            const int tumbling = status_animated && status && status->spin_ticks && items == presented_items(host) &&
                racer < 32u && (host->tumble_mask >> racer & 1u);
            uint8_t side;
            if (portable_mesh_driver_art_set_pose(host->driver_art[racer], animated ? asin(nose) : 0.0,
                    tumbling ? animation.spin_phase : -1.0) &&
                view && portable_mesh_driver_art_angle(&support->body, view, &side))
                source_pose.mirror = side > 63u;
        }
        const PortableRenderMesh *mesh =
            bomb ? portable_mesh_battle_art_bomb_view(
                       host->battle_art, angle,
                       (host->race.state.racers[racer].race_ticks / 10u) & 1u)
                 : translucent ? portable_mesh_driver_art_pose_blend(host->driver_art[racer], &source_pose)
                 : portable_mesh_driver_art_pose_view(host->driver_art[racer], &source_pose, palette_bank);
        if (!mesh && palette_bank)
        {
            mesh = portable_mesh_driver_art_pose_view(host->driver_art[racer], &source_pose, 0);
            palette_tint = 1; /* Authored one-bank art uses the model adaptation. */
        }
        if (!mesh)
        {
            snprintf(host->error, sizeof host->error,
                     "3D course=%u racer=%u source kart art unavailable", host->course.id, racer);
            return 0;
        }
        portable_render_instance_identity(instance, mesh);
        instance->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_UP;
        double anchor = 0;
        if (!bomb && !portable_mesh_driver_art_pose_ground_anchor(host->driver_art[racer], &source_pose,
                                                              &anchor)) return 0;
        if (bomb)
            for (uint32_t v = 0; v < mesh->vertex_count; ++v)
                if (mesh->vertices[v].position[2] < anchor) anchor = mesh->vertices[v].position[2];
        /* Pitch-row art draws the slope itself: like the N64 quad, the
         * sprite then faces the camera and only rolls with the kart. */
        double up[3] = {facing.up[0], facing.up[1], facing.up[2]};
        if (sprites && sprites->row_count > 1u && view)
            facing_camera(up, view);
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            instance->transform[axis * 4u + 2u] = up[axis];
            instance->transform[axis * 4u + 3u] = facing.position[axis] -
                                                anchor * body_scale[2] * up[axis];
            instance->billboard_scale[axis] = body_scale[axis];
        }
        baseline=*instance;
        if (animated)
        {
            if (!portable_mesh_driver_art_pose_bounds(host->driver_art[racer],&source_pose,
                lean_bounds)) return 0;
            double base_up[3] = {unleaned.up[0], unleaned.up[1], unleaned.up[2]};
            if (sprites && sprites->row_count > 1u && view)
                facing_camera(base_up, view);
            for (unsigned a=0;a<3;++a)
            {
                baseline.transform[a*4+2]=base_up[a];
                baseline.transform[a*4+3]=unleaned.position[a]-anchor*body_scale[2]*base_up[a];
            }
        }
    }
    if (animated && view)
    {
        if (!portable_render_kart_animation_foot_lift(&baseline,instance,view,lean_bounds,
            support->body.up,&lean_lift)) return 0;
        for (unsigned a=0;a<3;++a) instance->transform[a*4+3]+=lean_lift*support->body.up[a];
    }
    /* Indexed drivers use the exact source palette. Authored models and one-bank
     * sprites adapt Boo's palette request to a one-turn cool color pulse. */
    instance->tint_argb = palette_tint
        ? (color & UINT32_C(0xff000000)) | (palette_bank == 5 ? UINT32_C(0x303030) :
            palette_bank == 6 ? UINT32_C(0xe0d8d8) : UINT32_C(0xa0b8ff))
        : color;
    if (status && status->star_ticks && sprites && sprites->star_ticks)
        /* KSP1 star colours add to the art, as the N64 combiner's PRIM does. */
        instance->add_argb = sprites->star_add[(items->tick / sprites->star_ticks) & 3u];
    else if (status && status->star_ticks)
    {
        static const uint32_t glow[] = {0xffffb0b0u, 0xffffffb0u, 0xffb0ffffu, 0xffffb0ffu};
        instance->tint_argb = glow[(items->tick / 4u) & 3u];
    }
    if (translucent)
        instance->tint_argb = ((ghost ? UINT32_MAX : instance->tint_argb) & UINT32_C(0xffffff)) | translucent << 24;
    if (!host->cup_ceremony)
        instance->hidden_views = portable_mesh_item_scene_kart_hidden_views(
            items, racer, shared_race(host) ? host->players : 1u);
    if (boo_view)
        instance->hidden_views &= ~(UINT32_C(1) << pane); /* translucent, not flickering */
    if (capture && capture->remaining && capture->remaining <= 59u)
        instance->hidden_views = 15u;
    const char *trace = SDL_getenv("MKSC_AID_MESH_ANIMATION");
    if (trace && strcmp(trace,"1")==0 && !host->cup_ceremony)
        printf("3D kart animation: course=%u racer=%u pane=%u tick=%u draw=%u art=%s "
            "lean=%.9f bob=%.9f scale=%.9f,%.9f,%.9f angle=%u mirror=%u override=%u "
            "steer=%d speed=%d spark=%u flame=%u tier=%u active=%u owner=%u lift=%.9f\n",
            host->course.id,racer,pane,host->current_animation[animation_owner(host,items,racer)].tick,host->tick,
            binding?"model":"sprite",animated?animation.lean:0,animated?animation.bob:0,
            body_scale[0]/art_scale,body_scale[1]/art_scale,body_scale[2]/art_scale,
            source_pose.angle,source_pose.mirror,
            source_pose.frame_override,animation.steer_q16,animation.speed_q16,
            animation.spark_age,animation.flame_age,animation.spark_tier,(unsigned)animated,
            animation_owner(host,items,racer),lean_lift);
    return 1;
}

/* Authored meshes are shared across views. Directional source cells need an
 * immutable instance per camera; hide each copy from the other panes. */
/* Soft contact blob: a multiply texture that is white (no change) at its rim
 * and darkens smoothly toward the centre. Level 7 sits on the road; lower
 * levels fade out as the kart rises. */
static void prepare_shadows(PortableCourse3DHost *host)
{
    if (host->shadows_prepared) return;
    static const uint32_t indices[6] = {0,1,2,0,2,3};
    memcpy(host->shadow_indices,indices,sizeof indices);
    const PortableRenderVertex vertices[4] = {
        {.position={-1,-1,0},.uv={0,0}}, {.position={1,-1,0},.uv={1,0}},
        {.position={1,1,0},.uv={1,1}}, {.position={-1,1,0},.uv={0,1}}};
    memcpy(host->shadow_vertices,vertices,sizeof vertices);
    host->shadow_run=(PortableRenderRun){0,6,0};
    for (unsigned level=0;level<8;++level)
    {
        const double darkness=.7*(level+1)/8.0;
        for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x)
        {
            const double dx=((double)x-31.5)/31.5,dy=((double)y-31.5)/31.5;
            const double r2=dx*dx+dy*dy, r=sqrt(r2);
            /* A dark contact core under the wheels inside a soft penumbra: the
             * core is what reads as the kart touching the road. */
            const double t=r<.55 ? 0 : r>.75 ? 1 : (r-.55)/.2;
            const double core=1-t*t*(3-2*t), soft=r2<1 ? (1-r2)*(1-r2) : 0;
            const double falloff=core*.9+(1-core)*.5*soft;
            /* Cool tint keeps the darkened road from looking muddy. */
            const double shade=1-darkness*falloff;
            const uint32_t color=UINT32_C(0xff000000)|
                (uint32_t)lround(255*(1-darkness*falloff*1.04))<<16|
                (uint32_t)lround(255*shade)<<8|(uint32_t)lround(255*(1-darkness*falloff*.94));
            for (unsigned byte=0;byte<4;++byte)
                host->shadow_pixels[level][(y*64+x)*4+byte]=(uint8_t)(color>>(byte*8));
        }
        host->shadow_materials[level]=(PortableRenderMaterial){.argb=UINT32_MAX,
            .flags=4u|PORTABLE_RENDER_MATERIAL_MULTIPLY,
            .texture={.pixels=host->shadow_pixels[level],.width=64,.height=64}};
        host->shadow_meshes[level]=(PortableRenderMesh){.vertices=host->shadow_vertices,
            .indices=host->shadow_indices,.materials=&host->shadow_materials[level],
            .runs=&host->shadow_run,.vertex_count=4,.index_count=6,.material_count=1,.run_count=1};
    }
    host->shadows_prepared=1;
}

static int append_kart_fx(PortableCourse3DHost *host, PortableRenderFrame *frame,
    const PortableRenderSupport *support, const PortableMeshItems *items, unsigned racer, double scale)
{
    if (animation_inhibited(host,items,racer)) return 1;
    PortableRenderKartAnimationSnapshot animation;
    if (!presented_animation(host,animation_owner(host,items,racer),&animation)) return 0;
    /* Modern particles replace the source sprites on GPU backends; a
     * platform's own art (META mksc:fx-set) always replaces them. */
    const int modern = host->gpu || host->gles || (host->fx_set && portable_mesh_fx_set_rows(host->fx_set)->style_count);
    const PortableRenderMesh *spark = modern && host->settings.drift_sparks ? NULL :
        portable_mesh_kart_fx_art_frame(host->kart_fx, PORTABLE_MESH_KART_FX_SPARK, animation.spark_age);
    const PortableRenderMesh *flame = modern && host->settings.boost_effects ? NULL :
        portable_mesh_kart_fx_art_frame(host->kart_fx, PORTABLE_MESH_KART_FX_FLAME, animation.flame_age);
    const size_t count = (spark ? 2u : 0u) + (flame ? 1u : 0u);
    if (frame->instance_count > host->instance_capacity ||
        count > host->instance_capacity-frame->instance_count) return 0;
    const PortableRenderPose *body = &support->body;
    /* rules carry the course's kart scale; sprite sizes take it here. */
    const double radius = host->rules.kart_radius_q16 / 65536.0 * scale;
    const double size = scale * kart_scale(host);
    const double right[3] = {body->up[1]*body->forward[2]-body->up[2]*body->forward[1],
        body->up[2]*body->forward[0]-body->up[0]*body->forward[2],
        body->up[0]*body->forward[1]-body->up[1]*body->forward[0]};
    for (unsigned i=0;i<3;++i)
    {
        const PortableRenderMesh *mesh = i<2 ? spark : flame;
        if (!mesh) continue;
        PortableRenderInstance *instance=&host->instances[frame->instance_count++];
        portable_render_instance_identity(instance,mesh);
        instance->flags=PORTABLE_RENDER_INSTANCE_BILLBOARD_UP;
        const double lateral = i==0 ? -.8*radius : i==1 ? .8*radius : 0;
        const double rear = i<2 ? .7*radius : 1.4*radius;
        const double rise = i<2 ? .12*size : .3*size;
        for (unsigned a=0;a<3;++a)
        {
            instance->transform[a*4+2]=body->up[a];
            instance->transform[a*4+3]=body->position[a]-rear*body->forward[a]+lateral*right[a]+
                (rise+animation.bob)*body->up[a];
            instance->billboard_scale[a]=.55*size;
        }
        if (i<2)
        {
            static const uint32_t spark_tints[4] = {UINT32_C(0xffffffff), UINT32_C(0xff70c8ff),
                UINT32_C(0xffffb050), UINT32_C(0xffff80f0)};
            instance->tint_argb=spark_tints[animation.spark_tier];
        }
        if (i==0) instance->billboard_scale[0]=-instance->billboard_scale[0];
        instance->hidden_views=portable_mesh_item_scene_kart_hidden_views(items,racer,
            shared_race(host)?host->players:1u);
    }
    const char *trace=SDL_getenv("MKSC_AID_MESH_ANIMATION");
    if (trace && strcmp(trace,"1")==0)
        printf("3D kart FX: course=%u racer=%u tick=%u draw=%u sparks=%u flames=%u tier=%u owner=%u\n",
            host->course.id,racer,host->current_animation[animation_owner(host,items,racer)].tick,host->tick,
            spark?2u:0u,flame?1u:0u,animation.spark_tier,animation_owner(host,items,racer));
    return 1;
}

static int append_kart(PortableCourse3DHost *host, PortableRenderFrame *frame,
                       const PortableRenderPose *pose, uint32_t color, unsigned racer,
                       const PortableMeshItems *items, const PortableMeshKartState *kart)
{
    const int bomb = host->mode == PORTABLE_COURSE_3D_BATTLE &&
                     host->race.battle.elimination[racer].revenge_bomb;
    const int model = !bomb && (shared_race(host) ? host->racer_models[racer] != NULL
                                                  : host->driver_model != NULL);
    const size_t count = frame->view_count;
    const PortableMeshItemRacerStatus *status=items && racer<items->status.count
        ? &items->status.racers[racer] : NULL;
    /* A Bullet Bill ride draws the platform's bullet in place of the kart. */
    if (status && status->bullet_ticks && frame->instance_count < host->instance_capacity &&
        portable_mesh_item_scene_rider(host->item_scene, PORTABLE_MESH_ITEM_BULLET, pose->position,
                                       atan2(pose->forward[1], pose->forward[0]), host->tick,
                                       &host->instances[frame->instance_count]))
    {
        ++frame->instance_count;
        return 1;
    }
    const double scale=status && status->lightning_ticks && !status->star_ticks
        ? host->item_rules.lightning_scale_ratio_q16/65536.0 : 1;
    const double clearance=scale*((double)host->rules.kart_radius_q16+
                                  host->rules.ride_height_q16)/65536.0;
    PortableMeshKartState airborne;
    const PortableMeshCaptureState *capture=presented_capture(host,items,racer);
    if (kart && capture && capture->remaining)
    {
        airborne=*kart;
        airborne.grounded=0;
        kart=&airborne;
    }
    PortableRenderSupport support;
    PortableMeshCollisionPose posed;
    const unsigned owner = animation_owner(host, items, racer);
    const int has_support = host->pose_valid && (host->animation_mask & (1u << owner));
    if (!(has_support ? portable_render_support_interpolate(&support,
            &host->previous_support[owner], &host->current_support[owner],
            host->interpolate && !host->paused ? host->presentation_alpha : 65536u)
        : portable_render_pose_support(&support,pose,kart,posed_geometry(host,motion_tick(host),&posed),clearance)))
    {
        snprintf(host->error,sizeof host->error,"3D course=%u racer=%u presentation support invalid",
                 host->course.id,racer);
        return 0;
    }
    const RacerKart *seated = model ? &host->racer_karts[shared_race(host) ? racer : 0u] : NULL;
    /* The platform's own shadow art (META mksc:fx-set) replaces the blob. */
    PortableMeshFxShadow art;
    const int has_art = portable_mesh_fx_set_shadow(host->fx_set,
        host->race_karts[shared_race(host) ? racer : 0u],
        shared_race(host) ? host->driver_ids[racer] : host->driver_id, &art);
    const double vanish = has_art ? art.vanish * scale * kart_scale(host) : 2.0;
    /* MK64 ice (SURF mksc:reflect) mirrors a kart close above it instead of
     * casting its shadow (render_player_ice_reflection). */
    const int reflect=kart && kart->contact_surface < host->surface_light_count &&
        host->surface_karts[kart->contact_surface*3u+2u] && support.floor_valid && support.gap<REFLECT_GAP;
    const int shadow=!reflect && support.floor_valid && support.gap<vanish &&
        (has_art || !(seated && seated->present && seated->no_shadow));
    const char *trace=getenv("MKSC_AID_MESH_GROUND");
    if (trace && strcmp(trace,"1")==0)
        printf("3D kart seating: course=%u racer=%u tick=%u art=%s foot=%.6f,%.6f,%.6f "
               "normal=%.6f,%.6f,%.6f gap=%.6f grounded=%u shadow=%u\n",
               host->course.id,racer,host->tick,model?"model":"sprite",
               support.body.position[0],support.body.position[1],support.body.position[2],
               support.body.up[0],support.body.up[1],support.body.up[2],support.gap,
               kart?(unsigned)kart->grounded:0u,(unsigned)shadow);
    /* Models get one footprint shadow; flat source sprites need one per camera. */
    const size_t shadows=shadow ? (model ? 1u : count) : 0u;
    if (frame->instance_count > host->instance_capacity ||
        count+shadows > host->instance_capacity - frame->instance_count)
    {
        snprintf(host->error, sizeof host->error,
                 "3D course=%u racer=%u kart instance budget exceeded", host->course.id, racer);
        return 0;
    }
    if (shadows)
    {
        prepare_shadows(host);
        const double height=support.gap<0 ? 0 : support.gap/vanish;
        const unsigned level=(unsigned)((1-height)*7);
        /* A kart-footprint ellipse that tightens as the kart leaves the road;
         * source art shrinks away by its vanish height instead. */
        const double spread=scale*kart_scale(host)*(has_art ? (art.volume ? 1 : 1-height) : 1-.3*height);
        const double lift=1-height;
        const uint32_t hidden=capture && capture->remaining && capture->remaining<=59u ? 15u :
            portable_mesh_item_scene_kart_hidden_views(items,racer,shared_race(host)?host->players:1u);
        for (size_t i=0;i<shadows;++i)
        {
            PortableRenderInstance *instance=&host->instances[frame->instance_count++];
            portable_render_instance_identity(instance,has_art ? art.mesh : &host->shadow_meshes[level]);
            PortableRenderPose floor=support.floor;
            const double *up=floor.up;
            /* Lift clear of coplanar road depth; the blob never writes depth. */
            for (unsigned a=0;a<3;++a) floor.position[a]+=.02*up[a];
            if (!model)
            {
                /* A billboard's lower edge is the kart's near side: push the blob
                 * away from this camera so it tucks under the sprite. */
                double away[3],along=0,length=0;
                for (unsigned a=0;a<3;++a) away[a]=floor.position[a]-frame->views[i].position[a];
                for (unsigned a=0;a<3;++a) along+=away[a]*up[a];
                for (unsigned a=0;a<3;++a) { away[a]-=along*up[a]; length+=away[a]*away[a]; }
                length=sqrt(length);
                /* The core's near edge stays just in front of the tyres, where
                 * it reads as contact; the rest tucks under the sprite. Source
                 * art moves 0.3 of its half length (render_player_shadow shifts
                 * MK64's 6.75-unit shadow 2 units along the view). */
                const double tuck=has_art ? .3*art.half_length*spread : .2*spread*lift;
                if (length>1e-6)
                    for (unsigned a=0;a<3;++a) floor.position[a]+=tuck*away[a]/length;
            }
            if (has_art)
                for (unsigned a=0;a<3;++a) floor.position[a]+=art.forward*spread*floor.forward[a];
            if (!portable_render_pose_actor(&floor,0,instance->transform)) return 0;
            for (unsigned a=0;a<3;++a)
            {
                instance->transform[a*4]*=(has_art ? art.half_width : .74)*spread;
                instance->transform[a*4+1]*=(has_art ? art.half_length : .6)*spread;
            }
            instance->hidden_views=hidden|(model ? 0u : 15u & ~(1u<<i));
        }
    }
    const int body = seated && seated->present;
    if ((body || reflect) && count > host->instance_capacity - frame->instance_count - count)
    {
        snprintf(host->error, sizeof host->error,
                 "3D course=%u racer=%u kart instance budget exceeded", host->course.id, racer);
        return 0;
    }
    for (size_t i = 0; i < count; ++i)
    {
        PortableRenderInstance *instance = &host->instances[frame->instance_count];
        const PortableRenderMesh *posed_body = NULL;
        if (!kart_instance(host, instance, pose, color, racer, &frame->views[i], (unsigned)i, items, &support,
                           &posed_body))
            return 0;
        /* DS karts take the light colour of the collision they last touched. */
        if (kart && kart->contact_surface < host->surface_light_count)
        {
            /* MK64 lamp, lava and shadow zones shade and add to the kart's
             * colour; a star's colours take precedence, as in the source. */
            const uint32_t *zone = &host->surface_karts[kart->contact_surface * 3u];
            instance->light_argb = host->surface_lights[kart->contact_surface];
            instance->tint_argb = multiply_argb(instance->tint_argb, zone[0] | UINT32_C(0xff000000));
            if (!instance->add_argb) instance->add_argb = zone[1];
        }
        instance->hidden_views |= (uint8_t)(15u & ~(1u << i));
        ++frame->instance_count;
        if (body)
        {
            /* The body shares the driver's transform, moved back off the seat. */
            PortableRenderInstance *kart_body = &host->instances[frame->instance_count++];
            *kart_body = *instance;
            kart_body->mesh = posed_body ? posed_body : &host->model_resources[seated->resource].mesh;
            for (unsigned row = 0; row < 3u; ++row)
                for (unsigned axis = 0; axis < 3u; ++axis)
                    kart_body->transform[row * 4u + 3u] -= kart_body->transform[row * 4u + axis] * seated->seat[axis];
        }
        else if (reflect)
        {
            /* The mirror image across the floor at half the kart's alpha. */
            PortableRenderInstance *mirror = &host->instances[frame->instance_count++];
            *mirror = *instance;
            const double *n = support.floor.up;
            for (unsigned column = 0; column < 4u; ++column)
            {
                double along = 0;
                for (unsigned row = 0; row < 3u; ++row)
                    along += n[row] * (mirror->transform[row * 4u + column] -
                                       (column == 3u ? support.floor.position[row] : 0));
                for (unsigned row = 0; row < 3u; ++row) mirror->transform[row * 4u + column] -= 2 * along * n[row];
            }
            const uint32_t alpha = (mirror->tint_argb >> 24) / 2u;
            mirror->tint_argb = (mirror->tint_argb & UINT32_C(0xffffff)) | alpha << 24;
        }
    }
    return append_kart_fx(host,frame,&support,items,racer,scale);
}

/* Source balloon cells attach in world metres. Camera copies share immutable
 * texture/geometry; accepted race clocks own sway, never presentation count. */
static int append_balloons(PortableCourse3DHost *host, PortableRenderFrame *frame,
                           const PortableRenderPose *pose, unsigned racer)
{
    const unsigned count = host->race.battle.balloons[racer];
    const PortableRenderMesh *mesh = portable_mesh_battle_art_balloon(host->battle_art);
    if (!mesh || count > 3u || frame->instance_count > host->instance_capacity ||
        count * frame->view_count > host->instance_capacity - frame->instance_count)
        return 0;
    const PortableMeshCaptureState *capture = presented_capture(host, &host->race.items, racer);
    const uint32_t hidden = capture && capture->remaining && capture->remaining <= 59u ? 15u :
        portable_mesh_item_scene_kart_hidden_views(&host->race.items, racer, host->players);
    const double clock = fmax(0, (double)host->race.state.racers[racer].race_ticks - 1 + world_alpha(host) / 65536.0);
    const double clearance =
        (double)((int64_t)host->rules.kart_radius_q16 + host->rules.ride_height_q16) / 65536.0;
    const double angles[3] = {0, .5890486225480862, -.5890486225480862};
    const double size = kart_scale(host);
    const double phases[3] = {.7853981633974483, 0, 1.5707963267948966};
    const double right[3] = {pose->forward[1] * pose->up[2] - pose->forward[2] * pose->up[1],
                             pose->forward[2] * pose->up[0] - pose->forward[0] * pose->up[2],
                             pose->forward[0] * pose->up[1] - pose->forward[1] * pose->up[0]};
    for (unsigned slot = 0; slot < count; ++slot)
        for (size_t view = 0; view < frame->view_count; ++view)
        {
            PortableRenderInstance *instance = &host->instances[frame->instance_count++];
            portable_render_instance_identity(instance, mesh);
            instance->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
            instance->hidden_views = hidden | (15u & ~(1u << view));
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                instance->billboard_scale[axis] = size;
                instance->transform[axis * 4u + 3u] =
                    pose->position[axis] - clearance * pose->up[axis] +
                    size * (1.2 * (right[axis] * sin(angles[slot]) -
                                   pose->forward[axis] * cos(angles[slot])) +
                            pose->up[axis] * (2.0 + .3 * sin(clock * .04908738521234052 + phases[slot])));
            }
        }
    return 1;
}

/* Only copied presentation instances rotate. Cached authored transforms and
 * the fixed-tick simulation remain unchanged, including between-tick draws. */
static const char *actor_family(const PortableCourse3DHost *host, unsigned index)
{
    return index < host->actor_config.count ? "crabs"
        : index < host->actor_config.count + host->actor_config.snowman_count ? "snowmen"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count ? "penguins"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count + host->actor_config.mouse_count ? "mice"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count + host->actor_config.mouse_count +
                  host->actor_config.piranha_count ? "piranhas"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count + host->actor_config.mouse_count +
                  host->actor_config.piranha_count + host->actor_config.seagull_count ? "seagulls"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count + host->actor_config.mouse_count +
                  host->actor_config.piranha_count + host->actor_config.seagull_count +
                  host->actor_config.lava_count ? "lava_bubbles"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count + host->actor_config.mouse_count +
                  host->actor_config.piranha_count + host->actor_config.seagull_count +
                  host->actor_config.lava_count + host->actor_config.thwomp_count ? "thwomps"
        : index < host->actor_config.count + host->actor_config.snowman_count +
                  host->actor_config.penguin_count + host->actor_config.mouse_count +
                  host->actor_config.piranha_count + host->actor_config.seagull_count +
                  host->actor_config.lava_count + host->actor_config.thwomp_count +
                  host->actor_config.cannonball_count ? "cannonballs"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.rock_count-host->actor_config.teepee_count-host->actor_config.falling_star_count-host->actor_config.thundercloud_count-host->actor_config.common_prop_count-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "boos"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.teepee_count-host->actor_config.falling_star_count-host->actor_config.thundercloud_count-host->actor_config.common_prop_count-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "rocks"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.falling_star_count-host->actor_config.thundercloud_count-host->actor_config.common_prop_count-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "teepees"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.thundercloud_count-host->actor_config.common_prop_count-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "falling_stars"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.common_prop_count-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "thunderclouds"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "common_props"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.kamek_count-host->actor_config.pier_boo_count ? "mechakoopas"
        : index < portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.pier_boo_count ? "kamek" : "pier_boos";
}
/* A placed model's mesh this frame: its looping clip (IDLE when it has one)
 * at one frame per motion tick, between accepted ticks by the presentation
 * alpha, on one clock for every placement; otherwise the rest mesh. */
static const PortableRenderMesh *posed_skin(PortableCourse3DHost *host, RacerSkin *skin,
                                            const PortableRenderMesh *rest, uint32_t tick)
{
    if (!skin->rigged)
        return rest;
    const double clock = portable_render_clip_loop_frame(tick, world_alpha(host),
                                                         skin->rig.clips[skin->key].frames);
    if (clock != skin->sample)
    {
        if (!portable_render_model_skin_sample(rest, &skin->rig, skin->key, clock, skin->vertices))
            return rest;
        skin->mesh.revision = ++skin->revision;
        skin->sample = clock;
    }
    return &skin->mesh;
}

/* A placed object's mksc:shadow decal on the floor below its posed origin,
 * turned with the object's heading; none once that floor is further down
 * than the vanish depth. The decal never writes depth. */
static int append_object_shadow(PortableCourse3DHost *host, PortableRenderFrame *frame,
                                const ModelPlacement *placement, const PortableRenderInstance *posed)
{
    int32_t center[3];
    for (unsigned a = 0; a < 3u; ++a)
    {
        const double value = posed->transform[a * 4u + 3u] + (a == 2u ? .05 : 0);
        if (!isfinite(value) || fabs(value) > 4096)
            return 1;
        center[a] = (int32_t)llround(value * 65536.0);
    }
    PortableMeshCollisionPose storage;
    PortableMeshSupportHit hit;
    const PortableMeshQueryStatus status = portable_mesh_query_support(
        posed_geometry(host, motion_tick(host), &storage), center, 1,
        (int32_t)llround((placement->shadow_size[2] + .05) * 65536.0), 1, &hit);
    if (status == PORTABLE_MESH_QUERY_INVALID)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u: invalid object shadow floor query", host->course.id);
        return 0;
    }
    if (status != PORTABLE_MESH_QUERY_HIT)
        return 1;
    if (frame->instance_count >= host->instance_capacity)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u: object shadow instance budget exceeded",
                 host->course.id);
        return 0;
    }
    PortableRenderPose floor;
    double along = 0;
    for (unsigned a = 0; a < 3u; ++a)
    {
        floor.up[a] = hit.normal_q30[a] / 1073741824.0;
        floor.forward[a] = posed->transform[a * 4u + 1u];
        along += floor.forward[a] * floor.up[a];
    }
    double length = 0;
    for (unsigned a = 0; a < 3u; ++a)
    {
        floor.forward[a] -= along * floor.up[a];
        length += floor.forward[a] * floor.forward[a];
        /* Lift clear of coplanar road depth. */
        floor.position[a] = hit.center_q16[a] / 65536.0 + .02 * floor.up[a];
    }
    if (length < 1e-12)
        return 1; /* facing straight down its own floor normal: no heading */
    for (unsigned a = 0; a < 3u; ++a)
        floor.forward[a] /= sqrt(length);
    PortableRenderInstance *instance = &host->instances[frame->instance_count];
    portable_render_instance_identity(instance, &placement->shadow->mesh);
    if (!portable_render_pose_actor(&floor, 0, instance->transform))
        return 1;
    for (unsigned a = 0; a < 3u; ++a)
    {
        instance->transform[a * 4u] *= placement->shadow_size[0];
        instance->transform[a * 4u + 1u] *= placement->shadow_size[1];
    }
    instance->hidden_views = posed->hidden_views;
    ++frame->instance_count;
    return 1;
}

static const PortableRenderMesh *posed_scenery(PortableCourse3DHost *host, size_t resource)
{
    return posed_skin(host, &host->model_skins[resource], &host->model_resources[resource].mesh,
                      motion_tick(host));
}

static int place_models(PortableCourse3DHost *host, PortableRenderFrame *frame)
{
    const PortableMeshCourseActors *actors = host->driving && shared_race(host) ? &host->race.actors
                                             : host->replaying ? &host->replay_actors
                                                               : &host->trial_actors;
    frame->instance_count = 1u; /* Terrain first; preserve placed-object painter order in every pane. */
    for (size_t object = 0; object < host->object_count; ++object)
    {
        ModelPlacement *placement = &host->placements[object];
        PortableRenderInstance base = placement->instance;
        if (placement->coin_index_plus1 &&
            (!(host->actor_config.coins.modes & host->mode) ||
             !(actors->coins.available & (UINT64_C(1) << (placement->coin_index_plus1 - 1u)))))
            base.hidden_views = 15u;
        /* Objects absent from this race (mksc:present-in) are not drawn. */
        if (!portable_course_3d_present(&host->course, placement->property_first, placement->property_count,
                                        race_presence(host)))
            base.hidden_views = 15u;
        if (placement->window)
        {
            unsigned followers = 0, within = 0;
            for (size_t other = 0; other < host->object_count; ++other)
            {
                const ModelPlacement *follower = &host->placements[other];
                if (!follower->moving || follower->motion.kind != PORTABLE_MESH_MOTION_ROUTE ||
                    follower->route != placement->window_route || follower->row_id != placement->window_follower)
                    continue;
                ++followers;
                within |= (unsigned)portable_mesh_motion_route_within(&follower->motion, motion_tick(host),
                                                                      placement->window_from, placement->window_to);
            }
            if (!followers)
            {
                snprintf(host->error, sizeof host->error,
                         "3D course=%u object=%zu: mksc:route-window names no object 0x%08X on route %u",
                         host->course.id, object, placement->window_follower, placement->window_route);
                return 0;
            }
            if (within == (placement->window == 2u))
                base.hidden_views = 15u;
        }
        if (placement->billboard)
        {
            base.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
            memcpy(base.billboard_scale, placement->scale, sizeof base.billboard_scale);
        }
        if (placement->moving)
            portable_render_motion_sample(&placement->motion, motion_tick(host), world_alpha(host), &base);
        if (!placement->actor_index_plus1 && !placement->sky_layer_plus1 &&
            base.mesh == &host->model_resources[placement->resource].mesh)
            base.mesh = placement->skin ? posed_skin(host, placement->skin, base.mesh,
                                                     motion_tick(host) + placement->motion.phase)
                                        : posed_scenery(host, placement->resource);
        PortableMeshCourseActorView view = {0};
        size_t resource = 0;
        const unsigned actor = placement->actor_index_plus1 - 1u;
        if (placement->actor_index_plus1)
        {
            if (!portable_mesh_course_actors_view(&host->actor_config, actors, actor, &view))
            {
                snprintf(host->error, sizeof host->error,
                         "3D course=%u controller=%s index=%u: invalid presentation state",
                         host->course.id, actor_family(host, actor), actor);
                return 0;
            }
            PortableMeshCourseActorView previous;
            if (world_alpha(host) < 65536u && portable_mesh_course_actors_view(
                &host->actor_config, &host->previous_actors, actor, &previous))
                portable_render_course_actor_sample(&previous, &view, world_alpha(host));
            if (!view.visible)
                base.hidden_views = 15u;
            else
            {
                while (resource < host->model_count && host->model_ids[resource] != view.model_id)
                    ++resource;
                if (resource == host->model_count)
                {
                    snprintf(host->error, sizeof host->error,
                             "3D course=%u controller=%s index=%u: frame model %u not prepared",
                             host->course.id, actor_family(host, actor), actor, view.model_id);
                    return 0;
                }
                base.mesh = &host->model_resources[resource].mesh;
                for (unsigned axis = 0; axis < 3u; ++axis)
                    base.transform[axis * 4u + 3u] = view.position_q16[axis] / 65536.0;
                /* Source affine224 denotes inverse scaling, not 224/256 size. */
                if (!view.affine_x_q8 || !view.affine_y_q8)
                {
                    snprintf(host->error, sizeof host->error,
                             "3D course=%u controller=%s index=%u: zero frame affine",
                             host->course.id, actor_family(host, actor), actor);
                    return 0;
                }
                const double scale_x = 256.0 / view.affine_x_q8;
                const double scale_z = 256.0 / view.affine_y_q8;
                if (base.flags & PORTABLE_RENDER_INSTANCE_BILLBOARD_Z)
                {
                    base.billboard_scale[0] *= scale_x;
                    base.billboard_scale[2] *= scale_z;
                }
                else
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        base.transform[axis * 4u] *= scale_x;
                        base.transform[axis * 4u + 2u] *= scale_z;
                    }
                if (view.flip_y && !portable_render_instance_flip_vertical(&base, host->model_flip_z[resource]))
                {
                    snprintf(host->error, sizeof host->error,
                             "3D course=%u controller=%s index=%u: invalid flipped frame bounds",
                             host->course.id, actor_family(host, actor), actor);
                    return 0;
                }
            }
        }
        const int sky = host->driving && (host->gpu || host->gles) && placement->sky_layer_plus1;
        const size_t copies = sky || (view.visible && (view.has_facing || view.has_view_art || view.rotation_angle)) ? frame->view_count : 1u;
        if (frame->instance_count > host->instance_capacity ||
            copies > host->instance_capacity - frame->instance_count)
        {
            snprintf(host->error, sizeof host->error,
                     "3D course=%u object=%zu: placed instance budget exceeded", host->course.id, object);
            return 0;
        }
        for (size_t pane = 0; pane < copies; ++pane)
        {
            PortableRenderInstance *copy = &host->instances[frame->instance_count++];
            if (sky)
            {
                PortableRenderInstance parts[2];
                unsigned part_count;
                if (!portable_mesh_sky_scene_instances_racer(&host->actor_config.sky, &actors->sky,
                    shared_race(host) ? (unsigned)pane : 0u,
                    placement->sky_layer_plus1 - 1u, &base, &frame->views[pane], (unsigned)pane,
                    parts, &part_count) || part_count > host->instance_capacity - frame->instance_count + 1u)
                {
                    snprintf(host->error, sizeof host->error,
                        "3D course=%u sky profile=%u layer=%u: invalid camera placement",
                        host->course.id, host->actor_config.sky.profile_id, placement->sky_layer_plus1 - 1u);
                    return 0;
                }
                if (world_alpha(host) < 65536u)
                {
                    PortableRenderInstance previous[2];
                    unsigned previous_count;
                    if (portable_mesh_sky_scene_instances_racer(&host->actor_config.sky,
                        &host->previous_actors.sky, shared_race(host) ? (unsigned)pane : 0u,
                        placement->sky_layer_plus1 - 1u, &base, &frame->views[pane], (unsigned)pane,
                        previous, &previous_count) && previous_count == part_count)
                        for (unsigned part = 0; part < part_count; ++part)
                            portable_render_sky_sample(&previous[part], &parts[part], world_alpha(host));
                }
                *copy = parts[0];
                if (part_count == 2)
                {
                    host->instances[frame->instance_count++] = parts[1];
                }
            }
            else if (view.visible && view.has_view_art)
            {
                unsigned side;
                if (!portable_render_view_art_side(&frame->views[pane],view.view_heading,&side)) return 0;
                const uint32_t model = side ? view.alternate_model_id : view.model_id;
                size_t chosen=0;
                while (chosen<host->model_count && host->model_ids[chosen]!=model) ++chosen;
                if (chosen==host->model_count)
                {
                    snprintf(host->error,sizeof host->error,"3D course=%u controller=%s index=%u: alternate model %u not prepared",host->course.id,actor_family(host,actor),actor,model);
                    return 0;
                }
                *copy=base;copy->mesh=&host->model_resources[chosen].mesh;
                if (view.flip_y && chosen != resource &&
                    (!portable_render_instance_flip_vertical(copy, host->model_flip_z[resource]) ||
                     !portable_render_instance_flip_vertical(copy, host->model_flip_z[chosen])))
                {
                    snprintf(host->error,sizeof host->error,"3D course=%u controller=%s index=%u: invalid alternate flipped bounds",host->course.id,actor_family(host,actor),actor);
                    return 0;
                }
                copy->hidden_views |= 15u & ~(1u<<pane);
                if (actor >= portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.kamek_count-host->actor_config.pier_boo_count)
                {
                    const unsigned index=actor-portable_mesh_course_actors_config_count(&host->actor_config)+host->actor_config.kamek_count+host->actor_config.pier_boo_count;
                    if (index>=host->actor_config.kamek_count || pane>=4) return 0;
                    if (host->kamek_view_models[index][pane]!=model)
                    {
                        printf("3D source kamek view: course=%u actor=%u view=%zu side=%u model=%u\n",host->course.id,index,pane,side,model);
                        host->kamek_view_models[index][pane]=model;
                    }
                }
                else if (actor >= portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count)
                {
                    const unsigned index=actor-portable_mesh_course_actors_config_count(&host->actor_config)+host->actor_config.mechakoopa_count+host->actor_config.kamek_count+host->actor_config.pier_boo_count;
                    if (index>=host->actor_config.mechakoopa_count || pane>=4) return 0;
                    if (host->mechakoopa_view_models[index][pane]!=model)
                    {
                        printf("3D source mechakoopa view: course=%u actor=%u view=%zu side=%u model=%u\n",host->course.id,index,pane,side,model);
                        host->mechakoopa_view_models[index][pane]=model;
                    }
                }
                else
                {
                    const unsigned index=actor-(portable_mesh_course_actors_config_count(&host->actor_config)-host->actor_config.rock_count-host->actor_config.boo_count-host->actor_config.teepee_count-host->actor_config.falling_star_count-host->actor_config.thundercloud_count-host->actor_config.common_prop_count-host->actor_config.mechakoopa_count-host->actor_config.kamek_count-host->actor_config.pier_boo_count);
                    if (index>=host->actor_config.boo_count || pane>=4) return 0;
                    if (host->boo_view_sides[index][pane]!=side+1)
                    {
                        printf("3D source boo view: course=%u actor=%u view=%zu side=%u model=%u\n",host->course.id,index,pane,side,model);
                        host->boo_view_sides[index][pane]=(uint8_t)(side+1);
                    }
                }
            }
            else if (view.visible && view.has_facing)
            {
                if (!portable_render_instance_face_for_view(&base, &frame->views[pane],
                        view.facing_x_q16, view.facing_y_q16, view.turn_flip, view.facing_half_turn,
                        host->model_flip_x[resource], copy))
                {
                    snprintf(host->error, sizeof host->error,
                             "3D course=%u controller=%s index=%u: invalid facing frame bounds",
                             host->course.id, actor_family(host, actor), actor);
                    return 0;
                }
                copy->hidden_views |= 15u & ~(1u << pane);
            }
            else
                *copy = base;
            if (view.visible && view.rotation_angle)
            {
                PortableRenderInstance resolved;
                if (!portable_render_instance_for_view(copy, &frame->views[pane], &resolved))
                {
                    snprintf(host->error, sizeof host->error,
                        "3D course=%u controller=%s index=%u: invalid rotated frame",
                        host->course.id, actor_family(host, actor), actor);
                    return 0;
                }
                resolved.flags &= ~(uint32_t)PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
                const double angle = view.rotation_angle * 6.28318530717958647692 / 65536.0;
                const double cosine = cos(angle), sine = sin(angle);
                /* Source image Y points down; local mesh Z points up. Rotate
                 * around the imported anchor after resolving the pane basis. */
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const double x = resolved.transform[axis * 4u];
                    const double z = resolved.transform[axis * 4u + 2u];
                    resolved.transform[axis * 4u] = cosine * x - sine * z;
                    resolved.transform[axis * 4u + 2u] = sine * x + cosine * z;
                }
                resolved.hidden_views |= 15u & ~(1u << pane);
                *copy = resolved;
            }
        }
        if (placement->shadow && base.hidden_views != 15u &&
            !append_object_shadow(host, frame, placement, &base))
            return 0;
        const unsigned teepee_first = portable_mesh_course_actors_config_count(&host->actor_config)
            - host->actor_config.teepee_count - host->actor_config.falling_star_count - host->actor_config.thundercloud_count - host->actor_config.common_prop_count - host->actor_config.mechakoopa_count - host->actor_config.kamek_count - host->actor_config.pier_boo_count;
        if (placement->actor_index_plus1 && actor >= teepee_first &&
            actor < teepee_first + host->actor_config.teepee_count)
        {
            const unsigned index = actor - teepee_first;
            for (size_t pane = 0; pane < frame->view_count; ++pane)
            {
                const PortableRenderView *camera = &frame->views[pane];
                if (!isfinite(camera->forward[0]) || !isfinite(camera->forward[1]))
                {
                    snprintf(host->error, sizeof host->error, "3D course=%u mode=%u teepee=%u: invalid rider camera", host->course.id, host->mode, index);
                    return 0;
                }
                const uint16_t heading = (uint16_t)(int32_t)lround(
                    atan2(camera->forward[0], -camera->forward[1]) * 65536.0 / 6.28318530717958647692);
                PortableMeshCourseActorView rider;
                if (!portable_mesh_course_teepee_rider_view(&host->actor_config, actors, index,
                                                           heading, &rider))
                {
                    snprintf(host->error, sizeof host->error, "3D course=%u mode=%u teepee=%u: invalid rider state", host->course.id, host->mode, index);
                    return 0;
                }
                PortableMeshCourseActorView previous;
                if (actors->teepees[index].teepee.rider_state != 1u &&
                    actors->teepees[index].teepee.rider_state == host->previous_actors.teepees[index].teepee.rider_state &&
                    world_alpha(host) < 65536u && portable_mesh_course_teepee_rider_view(
                    &host->actor_config, &host->previous_actors, index, heading, &previous))
                    portable_render_course_actor_sample(&previous, &rider, world_alpha(host));
                if (!rider.visible) continue;
                size_t chosen = 0;
                while (chosen < host->model_count && host->model_ids[chosen] != rider.model_id) ++chosen;
                if (chosen == host->model_count || frame->instance_count >= host->instance_capacity ||
                    !rider.affine_x_q8 || !rider.affine_y_q8)
                {
                    snprintf(host->error, sizeof host->error, "3D course=%u mode=%u teepee=%u: rider model/affine/instance budget", host->course.id, host->mode, index);
                    return 0;
                }
                PortableRenderInstance *copy = &host->instances[frame->instance_count++];
                *copy = placement->instance;
                copy->mesh = &host->model_resources[chosen].mesh;
                copy->hidden_views = 15u & ~(1u << pane);
                for (unsigned axis = 0; axis < 3u; ++axis)
                    copy->transform[axis * 4u + 3u] = rider.position_q16[axis] / 65536.0;
                /* Carry the accepted rider offset with the same interpolated
                 * owner pose as the kart; rendering never advances its clocks. */
                if (host->driving && actors->teepees[index].teepee.rider_state == 1u)
                {
                    const unsigned victim = actors->teepees[index].teepee.kart;
                    const unsigned racers = shared_race(host) ? host->race.state.player_count : 1u;
                    PortableRenderPose carried;
                    if (victim >= racers || !presented_pose(host, victim, &carried))
                    {
                        snprintf(host->error, sizeof host->error, "3D course=%u mode=%u teepee=%u: invalid carried rider", host->course.id, host->mode, index);
                        return 0;
                    }
                    const PortableMeshKartState *kart = shared_race(host)
                        ? &host->race.state.racers[victim].kart : &display_trial(host)->state.kart;
                    for (unsigned axis = 0; axis < 3u; ++axis)
                        copy->transform[axis * 4u + 3u] +=
                            carried.position[axis] - kart->position_q16[axis] / 65536.0;
                }
                if (placement->billboard)
                {
                    copy->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
                    memcpy(copy->billboard_scale, placement->scale, sizeof copy->billboard_scale);
                    copy->billboard_scale[0] *= 256.0 / rider.affine_x_q8;
                    copy->billboard_scale[2] *= 256.0 / rider.affine_y_q8;
                }
                else for (unsigned axis = 0; axis < 3; ++axis)
                {
                    copy->transform[axis * 4u] *= 256.0 / rider.affine_x_q8;
                    copy->transform[axis * 4u + 2u] *= 256.0 / rider.affine_y_q8;
                }
                if (host->teepee_view_models[index][pane] != rider.model_id + 1u)
                {
                    printf("3D source teepee view: course=%u actor=%u view=%zu model=%u heading=%u\n",
                        host->course.id, index, pane, rider.model_id, heading);
                    host->teepee_view_models[index][pane] = rider.model_id + 1u;
                }
            }
        }
    }
    if (host->actor_config.pier_boo_count)
        for (unsigned slot = 0; slot < PORTABLE_BOO_ATTACK_CAPACITY; ++slot)
        {
            if (!actors->source_haunts[slot].occupied || !actors->source_haunts[slot].visible) continue;
            const unsigned source = actors->source_haunt_source_indices[slot];
            PortableMeshCourseActorView haunt;
            if (source >= host->actor_config.pier_boo_count ||
                !portable_mesh_course_actors_haunt_view(&host->actor_config, actors, slot, &haunt))
            {
                snprintf(host->error, sizeof host->error, "3D course=%u mode=%u haunt=%u: invalid source view", host->course.id, host->mode, slot);
                return 0;
            }
            const PortableBooAttack *old = &host->previous_actors.source_haunts[slot];
            const PortableBooAttack *now = &actors->source_haunts[slot];
            PortableMeshCourseActorView previous;
            if (world_alpha(host) < 65536u && old->occupied && old->target == now->target &&
                old->phase == now->phase && host->previous_actors.source_haunt_source_indices[slot] == source &&
                portable_mesh_course_actors_haunt_view(&host->actor_config, &host->previous_actors, slot, &previous))
                portable_render_course_actor_sample(&previous, &haunt, world_alpha(host));
            if (!haunt.visible) continue;
            size_t chosen = 0;
            while (chosen < host->model_count && host->model_ids[chosen] != haunt.model_id) ++chosen;
            const unsigned object = host->actor_config.pier_boo_object_indices[source];
            if (object >= host->object_count || chosen == host->model_count ||
                frame->instance_count >= host->instance_capacity || !haunt.affine_x_q8 || !haunt.affine_y_q8)
            {
                snprintf(host->error, sizeof host->error, "3D course=%u mode=%u haunt=%u: model/affine/instance budget", host->course.id, host->mode, slot);
                return 0;
            }
            const ModelPlacement *placement = &host->placements[object];
            PortableRenderInstance *copy = &host->instances[frame->instance_count++];
            *copy = placement->instance;
            copy->mesh = &host->model_resources[chosen].mesh;
            for (unsigned axis = 0; axis < 3; ++axis)
                copy->transform[axis * 4u + 3u] = haunt.position_q16[axis] / 65536.0;
            if (host->driving && actors->source_haunts[slot].phase <= 5u)
            {
                const unsigned victim = actors->source_haunts[slot].target;
                const unsigned racers = shared_race(host) ? host->race.state.player_count : 1u;
                PortableRenderPose carried;
                if (victim >= racers || !presented_pose(host, victim, &carried))
                {
                    snprintf(host->error, sizeof host->error, "3D course=%u mode=%u haunt=%u: invalid carried victim", host->course.id, host->mode, slot);
                    return 0;
                }
                const PortableMeshKartState *kart = shared_race(host)
                    ? &host->race.state.racers[victim].kart : &display_trial(host)->state.kart;
                for (unsigned axis = 0; axis < 3u; ++axis)
                    copy->transform[axis * 4u + 3u] +=
                        carried.position[axis] - kart->position_q16[axis] / 65536.0;
            }
            if (placement->billboard)
            {
                copy->flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
                memcpy(copy->billboard_scale, placement->scale, sizeof copy->billboard_scale);
                copy->billboard_scale[0] *= 256.0 / haunt.affine_x_q8;
                copy->billboard_scale[2] *= 256.0 / haunt.affine_y_q8;
            }
            else for (unsigned axis = 0; axis < 3; ++axis)
            {
                copy->transform[axis * 4u] *= 256.0 / haunt.affine_x_q8;
                copy->transform[axis * 4u + 2u] *= 256.0 / haunt.affine_y_q8;
            }
            if (haunt.flip_y && !portable_render_instance_flip_vertical(copy, host->model_flip_z[chosen]))
            {
                snprintf(host->error, sizeof host->error, "3D course=%u mode=%u haunt=%u: invalid flipped bounds", host->course.id, host->mode, slot);
                return 0;
            }
        }
    return 1;
}

static void ensure_acceleration(PortableCourse3DHost *host)
{
    if (host->gpu_renderer != host->render_renderer)
    {
        close_acceleration(host);
        host->gpu_renderer = host->render_renderer;
        host->gpu_attempted = host->backend_reported = 0;
    }
    if (host->render_renderer && !host->gpu_attempted)
    {
        host->gpu_attempted = 1;
        if (SDL_GetGPURendererDevice(host->render_renderer) != NULL)
            host->gpu =
                portable_render_gpu_create(host->render_renderer, host->error, sizeof host->error);
        else if (strcmp(SDL_GetRendererName(host->render_renderer), "opengles2") == 0)
            host->gles =
                portable_render_gles_create(host->render_renderer, host->error, sizeof host->error);
        else
            snprintf(host->error, sizeof host->error, "renderer has no GPU depth backend");
        if (!host->gpu && !host->gles)
            fprintf(stderr, "3D renderer CPU fallback: %s\n", host->error);
    }
}

static unsigned world_scale(const PortableCourse3DHost *host)
{
    return host->gpu && host->settings.hd2d_scale && host->settings.internal_scale < 6u
        ? 6u : host->settings.internal_scale;
}

static int world_extent(PortableCourse3DHost *host, const UiCanvas *canvas, size_t *width,
                        size_t *height)
{
    if (!host->gpu && !host->gles)
    {
        *width = (size_t)canvas->width;
        *height = (size_t)canvas->height;
        return 1;
    }
    int output_width, output_height;
    if (!SDL_GetCurrentRenderOutputSize(host->render_renderer, &output_width, &output_height) ||
        output_width <= 0 || output_height <= 0)
    {
        snprintf(host->error, sizeof host->error, "3D course=%u invalid renderer output size",
                 host->course.id);
        return 0;
    }
    return portable_render_world_extent(
        (size_t)output_width, (size_t)output_height,
        world_scale(host),
        (shared_race(host) && host->players > 1u) || host->aid_views == 4u ? 2u : 1u, width, height,
        host->error, sizeof host->error);
}

/* Ceremony geometry belongs to an independent, flat presentation scene.
 * No finish pose, course mesh or placed object may hide a standing driver. */
static int draw_podium(PortableCourse3DHost *host, PortableRenderFrame *frame,
                       PortableRenderInstance *instances)
{
    prepare_kart_mesh(host);
    frame->instance_count = 0;
    const double heights[3] = {1.2, .75, .4}, sides[3] = {0, -2.1, 2.1};
    const uint32_t colors[3] = {UINT32_C(0xffffd34e), UINT32_C(0xffbac7d3), UINT32_C(0xffba7946)};
    const double radius = (double)host->rules.kart_radius_q16 / 65536.0;
    const double clearance =
        (double)((int64_t)host->rules.kart_radius_q16 + host->rules.ride_height_q16) / 65536.0;
    for (unsigned rank = 0; rank < 3u; ++rank)
    {
        PortableRenderPose pose = {
            .position = {sides[rank], 0, clearance}, .forward = {0, 1, 0}, .up = {0, 0, 1}};
        const double zscale = heights[rank] / .45;
        PortableRenderInstance *base = &instances[frame->instance_count++];
        portable_render_instance_identity(base, &host->kart_mesh);
        base->tint_argb = colors[rank];
        if (!portable_render_pose_actor(&pose, clearance, base->transform))
            return 0;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            base->transform[axis * 4u] *= 1.8 / (2 * radius);
            base->transform[axis * 4u + 1u] *= 2.0 / (3 * radius);
            base->transform[axis * 4u + 2u] *= zscale;
        }
        base->transform[11] -= .2 * zscale;
        pose.position[2] += heights[rank] + .08 * fabs(sin((double)host->menu_age * .065 + rank));
        if (!kart_instance(host, &instances[frame->instance_count++], &pose, UINT32_MAX,
                           host->cup.standing_order[rank], NULL, 0, NULL, NULL, NULL))
            return 0;
    }
    /* Cache this generation's transformed bounds once. Remove the current
     * kart bob and include its full 0..0.08m range, keeping the camera steady.
     * The three table rows end above 46% and the footer starts below 92%. */
    if (!host->ceremony_bounds_valid)
    {
        double low[3] = {4096, 4096, 4096}, high[3] = {-4096, -4096, -4096};
        for (size_t i = 0; i < frame->instance_count; ++i)
        {
            const PortableRenderInstance *instance = &instances[i];
            const double bob =
                (i & 1u) ? .08 * fabs(sin((double)host->menu_age * .065 + (double)(i / 2u))) : 0;
            for (uint32_t v = 0; v < instance->mesh->vertex_count; ++v)
                for (unsigned axis = 0; axis < 3u; ++axis)
                {
                    const double *p = instance->mesh->vertices[v].position;
                    const double *m = instance->transform + axis * 4u;
                    const double value =
                        m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3] - (axis == 2u ? bob : 0);
                    const double maximum = value + ((axis == 2u && (i & 1u)) ? .08 : 0);
                    if (!isfinite(value) || fabs(value) > 4096.0 || fabs(maximum) > 4096.0)
                    {
                        snprintf(host->error, sizeof host->error,
                                 "3D course=%u mode=GP ceremony model bounds invalid",
                                 host->course.id);
                        return 0;
                    }
                    if (value < low[axis])
                        low[axis] = value;
                    if (maximum > high[axis])
                        high[axis] = maximum;
                }
        }
        memcpy(host->ceremony_low, low, sizeof low);
        memcpy(host->ceremony_high, high, sizeof high);
        host->ceremony_bounds_valid = 1;
    }
    const double *low = host->ceremony_low, *high = host->ceremony_high;
    double target[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
        target[axis] = (low[axis] + high[axis]) * .5;
    PortableRenderView *view = &frame->views[0];
    const double cosine = cos(.32), sine = sin(.32), focal = (double)view->height * .85;
    double distance = 4.0;
    for (unsigned corner = 0; corner < 8u; ++corner)
    {
        const double x = ((corner & 1u) ? high[0] : low[0]) - target[0];
        const double y = ((corner & 2u) ? high[1] : low[1]) - target[1];
        const double z = ((corner & 4u) ? high[2] : low[2]) - target[2];
        const double depth = -cosine * y - sine * z;
        const double horizontal = fabs(x) * focal / ((double)view->width * .40) - depth;
        const double vertical =
            fabs(-sine * y + cosine * z) * focal / ((double)view->height * .18) - depth;
        if (horizontal + .5 > distance)
            distance = horizontal + .5;
        if (vertical + .5 > distance)
            distance = vertical + .5;
    }
    view->clear_argb = UINT32_C(0xff122638);
    view->position[0] = target[0];
    view->position[1] = target[1] + distance * cosine;
    view->position[2] = target[2] + distance * sine;
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (!isfinite(view->position[axis]) || fabs(view->position[axis]) > 4096.0)
        {
            snprintf(host->error, sizeof host->error,
                     "3D course=%u mode=GP ceremony camera bounds invalid", host->course.id);
            return 0;
        }
    view->right[0] = 1;
    view->right[1] = view->right[2] = 0;
    view->up[0] = 0;
    view->up[1] = -sine;
    view->up[2] = cosine;
    view->forward[0] = 0;
    view->forward[1] = -cosine;
    view->forward[2] = -sine;
    view->projection = (PortableScene3DProjection){
        focal, focal, view->width * .5, view->height * .70, .10, 4096.0};
    return 1;
}

/* Chase panes: modern framing with the principal point at the centre. Wide
 * stacked panes take focal length from width so the view does not fan out.
 * A course's mksc:camera-fov sets the vertical field of view of a pane at
 * least as tall as .42 of its width; the stock lens is .95 of the height. */
static PortableScene3DProjection chase_projection(const PortableCourse3DHost *host, size_t width, size_t height)
{
    const double lens = host->chase.fov_turns ? .5 / tan(host->chase.fov_turns * (M_PI / 65536.0)) : .95;
    const double focal = lens * fmax((double)height, width * .42);
    return (PortableScene3DProjection){focal, focal, width * .5, height * .5, .10, 4096.0};
}

static int presented_chase(PortableCourse3DHost *host, unsigned index,
                           PortableRenderView *view)
{
    if (host->camera_error || !(host->camera_mask & (1u << index)))
        return 0;
    const uint32_t alpha = host->interpolate && host->pose_valid && !host->paused ?
        host->presentation_alpha : 65536u;
    PortableRenderChaseSnapshot snapshot;
    if (!portable_render_chase_snapshot(&snapshot, &host->previous_camera[index],
        &host->current_camera[index], alpha))
    {
        snprintf(host->error, sizeof host->error,
            "3D course=%u mode=%u racer=%u: chase snapshot invalid", host->course.id, host->mode, index);
        return 0;
    }
    memcpy(view->position, snapshot.position, sizeof view->position);
    memcpy(view->right, snapshot.right, sizeof view->right);
    memcpy(view->up, snapshot.up, sizeof view->up);
    memcpy(view->forward, snapshot.forward, sizeof view->forward);
    view->projection.focal_x *= snapshot.focal_scale;
    view->projection.focal_y *= snapshot.focal_scale;
    const char *trace = SDL_getenv("MKSC_AID_MESH_CAMERA");
    if (trace && strcmp(trace, "1") == 0)
    {
        const PortableMeshRacerState *state = shared_race(host) ?
            &host->race.state.racers[index] : &display_trial(host)->state;
        const PortableMeshKartState *kart = &state->kart;
        const PortableMeshItems *items = presented_items(host);
        const unsigned boosting = portable_mesh_kart_boosting(kart) || (items && index < items->status.count &&
                (items->status.racers[index].mushroom_ticks || items->status.racers[index].star_ticks));
        printf("3D chase camera: course=%u racer=%u tick=%u camera_tick=%u draw=%u "
            "eye=%.9f,%.9f,%.9f target=%.9f,%.9f,%.9f right_z=%.9f "
            "focal=%.9f swing=%.9f grounded=%u boosting=%u\n",
            host->course.id, index, state->tick, host->current_camera[index].tick, host->tick,
            snapshot.position[0], snapshot.position[1], snapshot.position[2],
            snapshot.target[0], snapshot.target[1], snapshot.target[2], snapshot.right[2],
            snapshot.focal_scale, snapshot.swing, kart->grounded, boosting);
    }
    return 1;
}

/* The intro shot's eye: along its route by arc length (or held), aim and
 * field of view eased linearly from the first values to the second. */
static int intro_view(PortableCourse3DHost *host, PortableRenderView *view)
{
    PortableCourse3DIntroShot shot;
    PortableCourse3DRoute route;
    if (!portable_course_3d_intro_shot(&host->course, host->intro_shot, &shot))
    {
        snprintf(host->error, sizeof host->error, "3D course=%u mode=%u intro shot %u invalid", host->course.id,
                 host->mode, host->intro_shot);
        return 0;
    }
    const double alpha = host->interpolate ? host->presentation_alpha / 65536.0 : 1.0;
    const double t = fmin(1.0, ((double)host->intro_tick + alpha) / (double)shot.ticks);
    double eye[3], target[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        eye[axis] = shot.position_q16[axis] / 65536.0;
        target[axis] = (shot.target_q16[0][axis] + t * ((double)shot.target_q16[1][axis] - shot.target_q16[0][axis])) /
                       65536.0;
    }
    if (shot.route != UINT32_MAX && portable_course_3d_route(&host->course, shot.route, &route))
    {
        double length = 0, previous[3] = {0, 0, 0};
        for (int pass = 0; pass < 2; ++pass)
        {
            double travelled = 0, goal = t * length;
            for (uint32_t i = 0; i < route.node_count; ++i)
            {
                PortableCourse3DRouteNode node;
                if (!portable_course_3d_route_node(&host->course, route.node_first + i, &node))
                    return 0;
                const double at[3] = {node.position_q16[0] / 65536.0, node.position_q16[1] / 65536.0,
                                      node.position_q16[2] / 65536.0};
                const double step = i ? sqrt((at[0] - previous[0]) * (at[0] - previous[0]) +
                                             (at[1] - previous[1]) * (at[1] - previous[1]) +
                                             (at[2] - previous[2]) * (at[2] - previous[2]))
                                      : 0.0;
                if (pass && (i == 0 || travelled + step <= goal || step <= 0))
                    memcpy(eye, at, sizeof eye);
                else if (pass && travelled < goal)
                    for (unsigned axis = 0; axis < 3u; ++axis)
                        eye[axis] = previous[axis] + (at[axis] - previous[axis]) * (goal - travelled) / step;
                travelled += step;
                memcpy(previous, at, sizeof previous);
            }
            length = travelled;
        }
    }
    double forward[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const double distance = sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (distance < 1e-3)
        forward[0] = 0, forward[1] = 1, forward[2] = 0;
    else
        for (unsigned axis = 0; axis < 3u; ++axis)
            forward[axis] /= distance;
    /* right = up x forward with world up +Z; up = forward x right. */
    double right[3] = {-forward[1], forward[0], 0};
    const double across = sqrt(right[0] * right[0] + right[1] * right[1]);
    if (across < 1e-6)
        right[0] = 1, right[1] = 0;
    else
        right[0] /= across, right[1] /= across;
    memcpy(view->position, eye, sizeof eye);
    memcpy(view->forward, forward, sizeof forward);
    memcpy(view->right, right, sizeof right);
    view->up[0] = forward[1] * right[2] - forward[2] * right[1];
    view->up[1] = forward[2] * right[0] - forward[0] * right[2];
    view->up[2] = forward[0] * right[1] - forward[1] * right[0];
    const double turns = (shot.fov_turns[0] + t * ((double)shot.fov_turns[1] - shot.fov_turns[0])) / 65536.0;
    const double focal = view->height * .5 / tan(turns * M_PI);
    view->projection = (PortableScene3DProjection){focal, focal, view->width * .5, view->height * .5, .10, 4096.0};
    return 1;
}

static int draw_world(PortableCourse3DHost *host, UiCanvas *canvas)
{
    ensure_acceleration(host);
    size_t width, height;
    if (!world_extent(host, canvas, &width, &height))
        return 0;
    if (!host->cup_ceremony && !host->resources.storage &&
        !portable_render_resources_open_course_present(&host->resources, &host->course, race_presence(host),
                                                       host->error, sizeof host->error))
        return 0;
    if (!prepare_models(host) || !course_environment(host))
        return 0;
    light_models(host);
    PortableRenderInstance *instances = host->instances;
    portable_render_instance_identity(&instances[0], &host->resources.mesh);
    instances[0].light_argb = host->course_light_argb;
    PortableRenderFrame frame = {.instances = instances,
                                 .instance_count = 1u + host->object_count,
                                 .view_count = 1,
                                 .animation_tick = host->tick,
                                 .animation_rewind_q16 = 65536u - world_alpha(host),
                                 .environment = host->environment};
    PortableRenderView *view = &frame.views[0];
    view->width = width;
    view->height = height;
    view->clear_argb = argb_from_rgba(host->meta.sky_rgba) | UINT32_C(0xff000000);
    if (host->palette_scene && host->driving && (host->gpu || host->gles))
    {
        const PortableMeshCourseActors *actors = shared_race(host) ? &host->race.actors :
            host->replaying ? &host->replay_actors : &host->trial_actors;
        const uint8_t *pair = portable_mesh_palette_variant(&host->actor_config.sky.palette,
            actors->sky.palette.variant);
        if (!pair) return 0;
        const uint16_t color = (uint16_t)(pair[128] | (uint16_t)pair[129] << 8);
        const unsigned r = color & 31u, g = (color >> 5) & 31u, b = (color >> 10) & 31u;
        view->clear_argb = UINT32_C(0xff000000) | (r * 8u + r / 4u) << 16 |
            (g * 8u + g / 4u) << 8 | (b * 8u + b / 4u);
    }
    memcpy(view->position, host->position, sizeof view->position);
    const double cy = cos(host->yaw), sy = sin(host->yaw);
    const double cp = cos(host->pitch), sp = sin(host->pitch);
    view->right[0] = cy;
    view->right[1] = sy;
    view->up[0] = -sy * sp;
    view->up[1] = cy * sp;
    view->up[2] = cp;
    view->forward[0] = sy * cp;
    view->forward[1] = -cy * cp;
    view->forward[2] = sp;
    view->projection = (PortableScene3DProjection){height * 0.85, height * 0.85, width * 0.5,
                                                   height * 0.48, 0.10,          4096.0};
    if (host->cup_ceremony)
    {
        if (!draw_podium(host, &frame, instances))
            return 0;
    }
    else if (host->driving && host->intro)
    {
        if (!intro_view(host, view))
            return 0;
    }
    else if (host->driving && shared_race(host))
    {
        frame.view_count = host->players;
        const PortableRenderView base = *view;
        for (unsigned i = 0; i < host->players; ++i)
        {
            PortableRenderView *pane = &frame.views[i];
            *pane = base;
            const int single = host->players == 1u;
            const int stacked = host->players == 2u;
            pane->x = single || stacked || !(i & 1u) ? 0u : width / 2u;
            pane->y = single ? 0u : stacked ? (i ? height / 2u : 0u) : (i & 2u ? height / 2u : 0u);
            pane->width = single || stacked ? width : (i & 1u ? width - width / 2u : width / 2u);
            pane->height = single ? height : pane->y ? height - height / 2u : height / 2u;
            pane->projection = chase_projection(host, pane->width, pane->height);
            if (!presented_chase(host, i, pane))
                return 0;
        }
    }
    else if (host->driving)
    {
        view->projection = chase_projection(host, width, height);
        if (!presented_chase(host, 0u, view))
            return 0;
    }
    if ((host->gpu || host->gles) && host->aid_views == 4u && !shared_race(host))
    {
        const PortableRenderView base = *view;
        frame.view_count = 4u;
        for (size_t i = 0; i < 4u; ++i)
        {
            PortableRenderView *pane = &frame.views[i];
            *pane = base;
            pane->x = (i & 1u) ? width / 2u : 0u;
            pane->y = (i & 2u) ? height / 2u : 0u;
            pane->width = (i & 1u) ? width - width / 2u : width / 2u;
            pane->height = (i & 2u) ? height - height / 2u : height / 2u;
            pane->projection = chase_projection(host, pane->width, pane->height);
            /* Stress independent immutable cameras, never extra race sessions. */
            const double angle = i == 0u ? 0.0 : i == 1u ? .12 : i == 2u ? -.12 : .24;
            const double cosine = cos(angle), sine = sin(angle);
            for (unsigned axis = 0; axis < 3u; ++axis)
            {
                pane->right[axis] = cosine * base.right[axis] + sine * base.forward[axis];
                pane->forward[axis] = cosine * base.forward[axis] - sine * base.right[axis];
            }
        }
    }
    if (!host->cup_ceremony && !place_models(host, &frame))
        return 0;
    if (!host->cup_ceremony && host->driving && host->item_profile_enabled)
    {
        const PortableMeshItems *items = shared_race(host) ? &host->race.items
                                         : host->replaying ? &host->replay_items
                                                           : &host->trial_items;
        if (!portable_mesh_item_scene_append(
                host->item_scene, &host->course, items, &host->item_rules, host->mode, instances,
                host->instance_capacity, &frame.instance_count, host->error, sizeof host->error))
            return 0;
        /* Item-scene order is boxes then nonpending actors. Hide attached
         * inventory with its swallowed owner, retaining independent projectiles. */
        PortableMeshItemPose poses[PORTABLE_MESH_ITEMS_ACTORS];
        const size_t count = portable_mesh_items_actor_poses(items, poses, PORTABLE_MESH_ITEMS_ACTORS);
        size_t visible = 0;
        for (size_t i = 0; i < count; ++i)
            visible += poses[i].phase != PORTABLE_MESH_ACTOR_ORBIT_PENDING;
        size_t instance = frame.instance_count - visible;
        for (size_t i = 0; i < count; ++i)
            if (poses[i].phase != PORTABLE_MESH_ACTOR_ORBIT_PENDING)
            {
                double position[3];
                portable_render_item_sample(host->previous_items, host->previous_item_count,
                    &poses[i], world_alpha(host), position);
                for (unsigned axis = 0; axis < 3; ++axis)
                    instances[instance].transform[axis * 4u + 3u] = position[axis];
                const PortableMeshCaptureState *capture = presented_capture(host, items, poses[i].owner);
                if ((poses[i].phase == PORTABLE_MESH_ACTOR_HELD || poses[i].phase == PORTABLE_MESH_ACTOR_ORBIT) &&
                    capture && capture->remaining && capture->remaining <= 59u)
                    instances[instance].hidden_views = 15u;
                ++instance;
            }
    }
    if (!host->cup_ceremony && host->driving)
    {
        const unsigned count = shared_race(host) ? host->racers : 1u;
        for (unsigned racer = 0; racer < count; ++racer)
        {
            PortableRenderPose pose;
            if (!presented_pose(host, racer, &pose) ||
                !append_kart(host, &frame, &pose, UINT32_MAX, racer, presented_items(host),
                    shared_race(host) ? &host->race.state.racers[racer].kart
                                      : &display_trial(host)->state.kart) ||
                (host->mode == PORTABLE_COURSE_3D_BATTLE &&
                 !append_balloons(host, &frame, &pose, racer)))
                return 0;
        }
        if (host->ghost_active && !host->replaying)
        {
            PortableRenderPose pose;
            if (!presented_pose(host, 1u, &pose) ||
                !append_kart(host, &frame, &pose, UINT32_C(0xff80d8ef), 0u,
                             &host->ghost_items,
                             &host->ghost_trial.state.kart))
                return 0;
        }
    }
    if ((host->gpu || host->gles) && !host->cup_ceremony && host->driving &&
        frame.instance_count < host->instance_capacity)
    {
        const uint32_t alpha = host->interpolate && host->pose_valid && !host->paused ?
            host->presentation_alpha : 65536u;
        frame.instance_count += portable_render_vfx_append(&host->vfx, alpha, frame.views,
            frame.view_count, &instances[frame.instance_count],
            host->instance_capacity - frame.instance_count);
    }
    if (host->gpu || host->gles)
    {
        if (host->palette_scene && host->driving)
        {
            const PortableMeshCourseActors *actors = shared_race(host) ? &host->race.actors :
                host->replaying ? &host->replay_actors : &host->trial_actors;
            for (size_t i = 0; i < frame.instance_count; ++i)
            {
                const PortableRenderMesh *base = instances[i].mesh;
                int prepared = base == &host->resources.mesh;
                for (size_t model = 0; !prepared && model < host->model_count; ++model)
                    prepared = base == &host->model_resources[model].mesh;
                if (prepared)
                {
                    instances[i].mesh = portable_mesh_palette_scene_mesh(host->palette_scene, base,
                        actors->sky.palette.variant, host->error, sizeof host->error);
                    if (!instances[i].mesh) return 0;
                }
            }
        }
        if (host->gpu) portable_render_gpu_set_hd2d(host->gpu, &host->settings);
        SDL_Texture *picture = host->gpu
                                   ? portable_render_gpu_draw(host->gpu, &frame, width, height,
                                                              host->error, sizeof host->error)
                                   : portable_render_gles_draw(host->gles, &frame, width, height,
                                                               host->error, sizeof host->error);
        if (!picture)
            return 0;
        *host->render_background = picture;
        ui_canvas_reset_mode(canvas, 1);
        ui_canvas_clear(canvas, 0u);
    }
    else
    {
        /* A pixel-only world cannot be replayed as UI triangles. Mark this
         * recording incomplete while retaining the existing CPU picture for
         * modal snapshots; the compositor rerenders the whole CPU frame. */
        if (canvas->sink != NULL)
            canvas->sink->failed = 1u;
        PortableSoftwareFrame target;
        if (!portable_software_frame_init(&target, canvas->pixels, width, height))
            return 0;
        target.stride = (size_t)canvas->stride;
        canvas->premultiplied_alpha = 0;
        if (shared_race(host))
            ui_canvas_clear(canvas, view->clear_argb);
        if (!portable_render_cpu_draw(&host->cpu, &frame, &target, host->error, sizeof host->error))
            return 0;
    }
    if ((host->gpu || host->gles) && (host->world_width != width || host->world_height != height))
        printf("3D world target: width=%zu height=%zu views=%zu scale=%u\n", width, height,
               frame.view_count, world_scale(host));
    if (host->mode == PORTABLE_COURSE_3D_QUICK && host->driving &&
        (host->world_width != width || host->world_height != height))
        printf("3D Quick world: course=%u racers=%u views=%zu\n", host->course.id, host->racers,
               frame.view_count);
    host->world_width = width;
    host->world_height = height;
    if (!host->backend_reported)
    {
        const char *driver =
            host->gpu    ? SDL_GetGPUDeviceDriver(SDL_GetGPURendererDevice(host->gpu_renderer))
            : host->gles ? SDL_GetRendererName(host->gpu_renderer)
                         : "software";
        printf("3D renderer: %s driver=%s course=%u\n", (host->gpu || host->gles) ? "GPU" : "CPU",
               driver ? driver : "unknown", host->course.id);
        printf("3D models: backend=%s driver=%u kart=%u objects=%u billboards=%u\n",
               (host->gpu || host->gles) ? "GPU" : "CPU", host->driver_id,
               host->driver_model ? host->driver_model->mesh_id : 0u, (unsigned)host->object_count,
               (unsigned)host->billboard_count);
        unsigned sources = 0, models = 0;
        const unsigned racers = shared_race(host) ? host->racers : host->driving ? 1u : 0u;
        for (unsigned racer = 0; racer < racers; ++racer)
        {
            sources += host->driver_art[racer] != NULL;
            models += host->racer_models[racer] != NULL;
        }
        printf("3D driver art: source=%u models=%u\n", sources, models);
        host->backend_reported = 1;
    }
    return 1;
}

static void format_ticks(char *text, size_t capacity, uint32_t ticks)
{
    const uint64_t centiseconds = (uint64_t)ticks * 4389u * 100u / 262144u;
    snprintf(text, capacity, "%u:%02u.%02u", (unsigned)(centiseconds / 6000u),
             (unsigned)(centiseconds / 100u % 60u), (unsigned)(centiseconds % 100u));
}

static int save_picture(PortableCourse3DHost *host, const UiCanvas *canvas)
{
    const size_t width = (size_t)canvas->width, height = (size_t)canvas->height;
    if (width > SIZE_MAX / height || width * height > SIZE_MAX / sizeof(uint32_t))
        return 0;
    const size_t count = width * height;
    if (count > host->overlay_capacity)
    {
        uint32_t *pixels = realloc(host->overlay, count * sizeof *pixels);
        if (!pixels)
            return 0;
        host->overlay = pixels;
        host->overlay_capacity = count;
    }
    for (size_t y = 0; y < height; ++y)
        memcpy(host->overlay + y * width, canvas->pixels + y * (size_t)canvas->stride,
               width * sizeof(uint32_t));
    return 1;
}

/* Driving HUD laid out per pane at any size, after the modern series: item
 * top-left, clock top-right, coins and lap bottom-left, place or balloons
 * bottom-right and the countdown centred. */
typedef struct
{
    const PortableMeshItems *items;
    const PortableMeshCourseActors *actors;
    const PortableMeshRacerState *racers; /* Minimap dots; racers[player] is this pane's. */
    unsigned racer_count, player, lap, laps, rank, balloons;
    uint32_t race_ticks, countdown_ticks;
    const char *banner; /* Top-centre label, such as Replay. */
    uint8_t counting, battle, revenge, tag; /* tag: name the player in split screen. */
} CourseHud;

static UiTextStyle hud_style(UiFontFace face, float size, uint32_t color, float k)
{
    UiTextStyle style = ui_text_style(face, size, color);
    style.outline = 5.0f * k;
    style.outline_color = UINT32_C(0xff14213d);
    style.shadow_color = UINT32_C(0x60000000);
    style.shadow_y = 4.0f * k;
    style.shadow_blur = 6.0f * k;
    return style;
}

static int draw_item_hud(PortableCourse3DHost *host, UiCanvas *canvas,
                         const PortableMeshItems *items, unsigned player, float x, float y,
                         float edge, float k)
{
    uint16_t item = 0;
    if (!host->item_profile_enabled)
        return 1;
    if (portable_mesh_items_hud(items, player, &item) != PORTABLE_MESH_ITEMS_OK)
        return 0;
    const float radius = edge * .24f;
    UiPaint drop = ui_paint_solid(UINT32_C(0x50000000));
    ui_fill_round_rect(canvas, x, y + 6.0f * k, edge, edge, radius, 0, &drop, 8.0f * k, 1);
    UiPaint fill = ui_paint_linear(0, y, UINT32_C(0xc02a4a8c), 0, y + edge, UINT32_C(0xc0102446));
    ui_fill_round_rect(canvas, x, y, edge, edge, radius, 0, &fill, 1, 1);
    ui_stroke_round_rect(canvas, x, y, edge, edge, radius, 0, fmaxf(2.0f, 6.0f * k), MM_WHITE, 1);
    const PortableNativeImage *icon = portable_mesh_item_scene_icon(host->item_scene, item);
    if (icon && icon->width && icon->height && icon->width <= 64u && icon->height <= 64u)
    {
        /* The platform's own icon, fitted to the slot at its aspect ratio. */
        for (unsigned row = 0; row < icon->height; ++row)
            for (unsigned column = 0; column < icon->width; ++column)
                host->item_icon_pixels[row * icon->width + column] =
                    portable_native_image_pixel(icon, column, row);
        const float fit = edge * .78f / (float)(icon->width > icon->height ? icon->width : icon->height);
        const float w = icon->width * fit, h = icon->height * fit;
        const UiImage image = {host->item_icon_pixels, icon->width, icon->height};
        ui_draw_image(canvas, &image, floorf(x + (edge - w) * .5f), floorf(y + (edge - h) * .5f), w, h, 1,
                      UI_IMAGE_NEAREST, UINT32_MAX, 0);
    }
    else if (item >= 0x1000u && item <= 0x100fu && item != 0x1006u && item != 0x100du)
    {
        /* Whole-texel scaling keeps the 16px source art crisp. */
        const UiImage image = {host->item_hud_pixels[item - 0x1000u], 16, 16};
        const float art = fmaxf(16.0f, floorf(edge * .72f / 16.0f) * 16.0f);
        ui_draw_image(canvas, &image, floorf(x + (edge - art) * .5f), floorf(y + (edge - art) * .5f),
                      art, art, 1, UI_IMAGE_NEAREST, UINT32_MAX, 0);
    }
    return 1;
}

static void draw_coin_hud(const PortableCourse3DHost *host, UiCanvas *canvas,
                          const PortableMeshCourseActors *actors, unsigned racer,
                          float x, float y, float k)
{
    if (!host->actor_config.coins.enabled || !(host->actor_config.coins.modes & host->mode))
        return;
    const float r = 26.0f * k;
    UiPaint rim = ui_paint_solid(UINT32_C(0xffb87400));
    UiPaint face = ui_paint_linear(0, y - r, UINT32_C(0xffffe880), 0, y + r, UINT32_C(0xffffb000));
    ui_fill_circle(canvas, x + r, y, r, &rim, 1, 1);
    ui_fill_circle(canvas, x + r, y, r * .78f, &face, 1, 1);
    ui_stroke_circle(canvas, x + r, y, r, fmaxf(1.5f, 4.0f * k), UINT32_C(0xff14213d), 1);
    char label[24];
    snprintf(label, sizeof label, "\xc3\x97%02u", actors->coins.count[racer]);
    const UiTextStyle style = hud_style(UI_FONT_ITALIC, 50.0f * k, MM_WHITE, k);
    ui_text_draw_centered(canvas, &style, x + r * 2.0f + 10.0f * k, y, UI_ALIGN_LEFT, label, 1);
}

/* The course's minimap art at the pane's right edge with a dot per racer,
 * the pane's own drawn last and largest. */
static void draw_minimap(const PortableCourse3DHost *host, UiCanvas *canvas, const CourseHud *hud,
                         float right, float middle, float k)
{
    PortableCourse3DMinimap map;
    if (!portable_course_3d_minimap(&host->course, &map))
        return;
    const UiImage image = {(const uint32_t *)(const void *)map.image.pixels, map.image.width,
                           map.image.height};
    const float scale = 300.0f * k / (float)(image.width > image.height ? image.width : image.height);
    const float w = (float)image.width * scale, h = (float)image.height * scale;
    const float left = right - w, top = middle - h * .5f;
    ui_draw_image(canvas, &image, left, top, w, h, .85f, UI_IMAGE_SMOOTH, MM_WHITE, 0.0f);
    static const uint32_t tags[4] = {0xffe5262cu, 0xff1f7ae0u, 0xff2fb34au, 0xffe7a300u};
    for (unsigned n = 0; n <= hud->racer_count; ++n)
    {
        const unsigned racer = n < hud->racer_count ? (n < hud->player ? n : n + 1u) : hud->player;
        if (racer >= hud->racer_count)
            continue;
        const int32_t *position = hud->racers[racer].kart.position_q16;
        const float u = (float)((double)position[0] - map.low_q16[0]) / (float)((double)map.high_q16[0] - map.low_q16[0]);
        const float v = (float)((double)position[1] - map.low_q16[1]) / (float)((double)map.high_q16[1] - map.low_q16[1]);
        const int own = racer == hud->player;
        const float r = (own ? 11.0f : 7.0f) * k;
        const UiPaint fill = ui_paint_solid(own ? tags[racer & 3u] : UINT32_C(0xffe6eef8));
        ui_fill_circle(canvas, left + u * w, top + v * h, r, &fill, 1, 1);
        ui_stroke_circle(canvas, left + u * w, top + v * h, r, fmaxf(1.0f, 2.5f * k), UINT32_C(0xff14213d), 1);
    }
}

/* The intro names the course and offers the skip. */
static int draw_intro_hud(const PortableCourse3DHost *host, UiCanvas *canvas)
{
    const float k = fminf(canvas->height / 1080.0f, canvas->width / 1500.0f), margin = 48.0f * k;
    const UiTextStyle name = hud_style(UI_FONT_ITALIC, 76.0f * k, MM_WHITE, k);
    const UiTextStyle hint = hud_style(UI_FONT_ITALIC, 38.0f * k, MM_YELLOW, k);
    ui_text_draw(canvas, &name, margin, canvas->height - margin, UI_ALIGN_LEFT, host->meta.name, 1);
    ui_text_draw(canvas, &hint, canvas->width - margin, canvas->height - margin, UI_ALIGN_RIGHT, "A  Skip", 1);
    return 1;
}

static int draw_course_hud(PortableCourse3DHost *host, UiCanvas *canvas, float x, float y, float w,
                           float h, const CourseHud *hud)
{
    /* Narrow quad panes shrink by width so nothing collides. */
    const float k = fminf(h / 1080.0f, w / 1500.0f), margin = 48.0f * k;
    /* Blooper ink: the platform's ink splat over the pane, fading out over
     * the last second. */
    const PortableMeshItemRacerStatus *inked =
        hud->items && hud->player < hud->items->status.count ? &hud->items->status.racers[hud->player] : NULL;
    const PortableNativeImage *ink = portable_mesh_item_scene_overlay(host->item_scene, PORTABLE_MODEL_ITEM_INK);
    if (inked && inked->ink_ticks && ink && ink->width <= 64u && ink->height <= 64u)
    {
        for (unsigned row = 0; row < ink->height; ++row)
            for (unsigned column = 0; column < ink->width; ++column)
                host->item_icon_pixels[row * ink->width + column] = portable_native_image_pixel(ink, column, row);
        const UiImage image = {host->item_icon_pixels, ink->width, ink->height};
        const float fade = fminf(1.0f, inked->ink_ticks / 60.0f), side = fmaxf(w, h) * .42f;
        static const float spots[3][2] = {{.3f, .45f}, {.68f, .35f}, {.55f, .7f}};
        for (unsigned i = 0; i < 3u; ++i)
            ui_draw_image(canvas, &image, x + w * spots[i][0] - side * .5f, y + h * spots[i][1] - side * .5f, side,
                          side, fade, UI_IMAGE_SMOOTH, UINT32_MAX, 0);
    }
    if (!(hud->battle && hud->revenge) &&
        !draw_item_hud(host, canvas, hud->items, hud->player, x + margin, y + margin, 156.0f * k, k))
        return 0;
    char clock[32];
    format_ticks(clock, sizeof clock, hud->race_ticks);
    const UiTextStyle timer = hud_style(UI_FONT_ITALIC, 62.0f * k, MM_WHITE, k);
    ui_text_draw_centered(canvas, &timer, x + w - margin, y + margin + 34.0f * k, UI_ALIGN_RIGHT,
                          clock, 1);
    if (hud->banner)
    {
        const UiTextStyle style = hud_style(UI_FONT_ITALIC, 48.0f * k, MM_YELLOW, k);
        ui_text_draw_centered(canvas, &style, x + w * .5f, y + margin + 34.0f * k, UI_ALIGN_CENTER,
                              hud->banner, .55f + .45f * sinf((float)host->tick * .08f));
    }
    if (hud->tag)
    {
        char tag[8];
        snprintf(tag, sizeof tag, "P%u", hud->player + 1u);
        static const uint32_t tags[4] = {0xffe5262cu, 0xff1f7ae0u, 0xff2fb34au, 0xffe7a300u};
        const UiTextStyle style = hud_style(UI_FONT_ITALIC, 44.0f * k, tags[hud->player & 3u], k);
        ui_text_draw_centered(canvas, &style, x + w - margin, y + margin + 104.0f * k, UI_ALIGN_RIGHT,
                              tag, 1);
    }
    const float bottom = y + h - margin;
    if (hud->battle)
    {
        char balloons[24];
        snprintf(balloons, sizeof balloons, "%u", hud->balloons);
        const UiTextStyle count = hud_style(UI_FONT_ITALIC, 150.0f * k, UINT32_C(0xffff5a6e), k);
        const UiTextStyle label = hud_style(UI_FONT_ITALIC, 46.0f * k, MM_WHITE, k);
        ui_text_draw(canvas, &label, x + w - margin, bottom, UI_ALIGN_RIGHT,
                     hud->revenge ? "Revenge Bomb" : hud->balloons == 1u ? "balloon" : "balloons", 1);
        ui_text_draw(canvas, &count, x + w - margin, bottom - 52.0f * k, UI_ALIGN_RIGHT, balloons, 1);
    }
    else
    {
        char lap[16];
        snprintf(lap, sizeof lap, "%u/%u", hud->lap, hud->laps);
        const UiTextStyle small = hud_style(UI_FONT_ITALIC, 38.0f * k, MM_YELLOW, k);
        const UiTextStyle big = hud_style(UI_FONT_ITALIC, 76.0f * k, MM_WHITE, k);
        ui_text_draw(canvas, &small, x + margin, bottom, UI_ALIGN_LEFT, "LAP", 1);
        ui_text_draw(canvas, &big, x + margin + ui_text_width(&small, "LAP") + 12.0f * k, bottom,
                     UI_ALIGN_LEFT, lap, 1);
        draw_coin_hud(host, canvas, hud->actors, hud->player, x + margin, bottom - 110.0f * k, k);
        if (hud->rank)
        {
            static const char *const suffixes[4] = {"th", "st", "nd", "rd"};
            static const uint32_t colors[4] = {0xff9fe0ffu, 0xffffd23au, 0xffe6eef8u, 0xfff0a060u};
            const unsigned rank = hud->rank;
            const unsigned tone = rank <= 3u ? rank : 0u;
            const char *suffix = suffixes[rank % 10u <= 3u && rank / 10u != 1u ? rank % 10u : 0u];
            char number[8];
            snprintf(number, sizeof number, "%u", rank);
            const UiTextStyle place = hud_style(UI_FONT_ITALIC, 190.0f * k, colors[tone], k);
            const UiTextStyle tail = hud_style(UI_FONT_ITALIC, 76.0f * k, colors[tone], k);
            const float tail_w = ui_text_width(&tail, suffix);
            ui_text_draw(canvas, &tail, x + w - margin, bottom - 4.0f * k, UI_ALIGN_RIGHT, suffix, 1);
            ui_text_draw(canvas, &place, x + w - margin - tail_w - 4.0f * k, bottom, UI_ALIGN_RIGHT,
                         number, 1);
        }
    }
    draw_minimap(host, canvas, hud, x + w - margin, y + h * .5f, k);
    if (hud->counting || hud->race_ticks < 60u)
    {
        char count[16];
        snprintf(count, sizeof count, "%u", (hud->countdown_ticks + 59u) / 60u);
        /* Each number swells in, then GO! holds through the first second. */
        const float phase = hud->counting ? (float)(hud->countdown_ticks % 60u) / 60.0f
                                          : 1.0f - (float)hud->race_ticks / 60.0f;
        const float grow = hud->counting ? 1.0f + .25f * phase * phase : 1.0f;
        const UiTextStyle style = hud_style(UI_FONT_ITALIC, 220.0f * k * grow,
                                            hud->counting ? MM_WHITE : MM_YELLOW, k * 1.6f);
        ui_text_draw_centered(canvas, &style, x + w * .5f, y + h * .42f, UI_ALIGN_CENTER,
                              hud->counting ? count : "GO!", hud->counting ? 1.0f : fminf(1, phase * 2));
    }
    return 1;
}

static int render_trial(PortableCourse3DHost *host, ModernMenu *menu, UiCanvas *canvas)
{
    const PortableMeshTimeTrialState *state = &display_trial(host)->state;
    const int modal =
        host->paused || (!host->replaying && state->phase == PORTABLE_MESH_TT_FINISHED);
    ensure_acceleration(host);
    size_t width, height;
    if (!world_extent(host, canvas, &width, &height))
        return 0;
    if (!modal)
        host->freeze_valid = 0;
    if (!modal || !host->freeze_valid || host->freeze_width != canvas->width ||
        host->freeze_height != canvas->height || host->freeze_world_width != width ||
        host->freeze_world_height != height)
    {
        if (!draw_world(host, canvas))
            return 0;
        if (modal)
        {
            if (host->gpu || host->gles)
                host->frozen_picture = host->render_background ? *host->render_background : NULL;
            else if (!save_picture(host, canvas))
                return 0;
            host->freeze_width = canvas->width;
            host->freeze_height = canvas->height;
            host->freeze_world_width = width;
            host->freeze_world_height = height;
            host->freeze_valid = 1;
        }
    }
    const int external_background = modal && (host->gpu || host->gles);
    if (modal && host->render_background)
        *host->render_background = external_background ? host->frozen_picture : NULL;
    if (host->intro && !host->replaying)
        return draw_intro_hud(host, canvas);
    if (host->paused || (!host->replaying && state->phase == PORTABLE_MESH_TT_FINISHED))
    {
        if (host->paused)
        {
            ModernMenuOptions graphics;
            if (host->graphics.open)
                portable_graphics_modern_options(&graphics, &host->graphics);
            const ModernMenuPause pause = {
                .frame = external_background ? NULL : host->overlay,
                .frame_width = canvas->width,
                .frame_height = canvas->height,
                .frame_w = (float)canvas->width,
                .frame_h = (float)canvas->height,
                .subtitle = host->meta.name,
                .labels = {"Continue", "Retry", "Change course"},
                .kinds = {MODERN_PAUSE_CONTINUE, MODERN_PAUSE_RETRY, MODERN_PAUSE_CHANGE_COURSE},
                .item_count = 3,
                .cursor = host->action_cursor,
                .tick = host->menu_age,
                .gamepad = host->gamepad,
                .external_background = (uint8_t)external_background,
                .graphics = host->graphics.open ? &graphics : NULL};
            return modern_menu_render_pause(menu, &pause, canvas);
        }
        ModernMenuResults results = {.pack = host->pack,
                                     .frame = external_background ? NULL : host->overlay,
                                     .frame_width = canvas->width,
                                     .frame_height = canvas->height,
                                     .frame_w = (float)canvas->width,
                                     .frame_h = (float)canvas->height,
                                     .title = "Time Trial complete",
                                     .subtitle = host->meta.name,
                                     .course_id = host->course.id,
                                     .row_count = 1,
                                     .labels = {"Retry", "View replay", "Change course"},
                                     .item_count = 3,
                                     .cursor = host->action_cursor,
                                     .disabled_mask = host->record_valid ? 0 : 2,
                                     .notice = host->error[0]
                                                   ? host->error
                                                   : (host->best_path[0]
                                                          ? "3D records saved separately"
                                                          : "Records not saved: no data directory"),
                                     .tick = host->menu_age,
                                     .gamepad = host->gamepad,
                                     .external_background = (uint8_t)external_background};
        results.rows[0].driver_id = host->driver_id;
        results.rows[0].highlighted = 1;
        format_ticks(results.rows[0].value, sizeof results.rows[0].value, state->finish_ticks);
        char best[32];
        format_ticks(best, sizeof best, host->best_lap_tick);
        snprintf(results.rows[0].detail, sizeof results.rows[0].detail, "Best lap %s", best);
        return modern_menu_render_results(menu, &results, canvas);
    }
    const CourseHud hud = {.items = host->replaying ? &host->replay_items : &host->trial_items,
                           .actors = host->replaying ? &host->replay_actors : &host->trial_actors,
                           .racers = state,
                           .racer_count = 1u,
                           .lap = state->lap + 1u > host->meta.laps ? host->meta.laps : state->lap + 1u,
                           .laps = host->meta.laps,
                           .race_ticks = state->race_ticks,
                           .countdown_ticks = state->countdown_ticks,
                           .banner = host->replaying ? "Replay" : NULL,
                           .counting = state->phase == PORTABLE_MESH_TT_COUNTDOWN};
    return draw_course_hud(host, canvas, 0, 0, (float)canvas->width, (float)canvas->height, &hud);
}

static int render_gp_ceremony(PortableCourse3DHost *host, ModernMenu *menu, UiCanvas *canvas)
{
    if (!draw_world(host, canvas))
        return 0;
    const int external = host->gpu || host->gles;
    if (!external && !save_picture(host, canvas))
        return 0;
    static const char *const medals[4] = {"Gold trophy", "Silver trophy", "Bronze trophy",
                                          "Cup complete"};
    const unsigned rank = host->cup.standing_rank[0];
    ModernMenuResults result = {.pack = host->pack,
                                .frame = external ? NULL : host->overlay,
                                .frame_width = canvas->width,
                                .frame_height = canvas->height,
                                .frame_w = (float)canvas->width,
                                .frame_h = (float)canvas->height,
                                .title = medals[rank < 3u ? rank : 3u],
                                .subtitle = host->meta.cup_name,
                                .course_id = host->course.id,
                                .row_count = 3u,
                                .labels = {"Continue"},
                                .item_count = 1u,
                                .tick = host->menu_age,
                                .gamepad = host->gamepad,
                                .notice = host->error[0] ? host->error : NULL,
                                .external_background = (uint8_t)external,
                                .ceremony = 1u};
    for (unsigned row = 0; row < 3u; ++row)
    {
        const unsigned racer = host->cup.standing_order[row];
        result.rows[row].driver_id = host->driver_ids[racer];
        result.rows[row].rank = host->cup.standing_rank[racer] + 1u;
        result.rows[row].highlighted = racer == 0u;
        snprintf(result.rows[row].value, sizeof result.rows[row].value, "%u pts",
                 host->cup.points[racer]);
    }
    return modern_menu_render_results(menu, &result, canvas);
}

static int render_local_race(PortableCourse3DHost *host, ModernMenu *menu, UiCanvas *canvas)
{
    if (host->cup_ceremony)
        return render_gp_ceremony(host, menu, canvas);
    const PortableMeshRaceState *race = &host->race.state;
    const int modal = host->paused || race->results_ready;
    ensure_acceleration(host);
    size_t width, height;
    if (!world_extent(host, canvas, &width, &height))
        return 0;
    if (!modal)
        host->freeze_valid = 0;
    if (!host->freeze_valid || host->freeze_width != canvas->width ||
        host->freeze_height != canvas->height || host->freeze_world_width != width ||
        host->freeze_world_height != height)
    {
        if (!draw_world(host, canvas))
            return 0;
        if (modal)
        {
            if (host->gpu || host->gles)
                host->frozen_picture = *host->render_background;
            else if (!save_picture(host, canvas))
                return 0;
            host->freeze_width = canvas->width;
            host->freeze_height = canvas->height;
            host->freeze_world_width = width;
            host->freeze_world_height = height;
            host->freeze_valid = 1;
        }
    }
    const int external = modal && (host->gpu || host->gles);
    if (modal && host->render_background)
        *host->render_background = external ? host->frozen_picture : NULL;
    if (host->paused)
    {
        const int no_retry = host->mode == 1u && !host->cup.retry_credits;
        char retry[40];
        snprintf(retry, sizeof retry, "Retry (%u left)", host->cup.retry_credits);
        ModernMenuOptions graphics;
        if (host->graphics.open)
            portable_graphics_modern_options(&graphics, &host->graphics);
        const ModernMenuPause pause = {
            .frame = external ? NULL : host->overlay,
            .frame_width = canvas->width,
            .frame_height = canvas->height,
            .frame_w = (float)canvas->width,
            .frame_h = (float)canvas->height,
            .subtitle = host->meta.name,
            .labels = {"Continue",
                       no_retry           ? "Change course"
                       : host->mode == 1u ? retry
                                          : "Retry",
                       "Change course"},
            .kinds = {MODERN_PAUSE_CONTINUE,
                      no_retry ? MODERN_PAUSE_CHANGE_COURSE : MODERN_PAUSE_RETRY,
                      MODERN_PAUSE_CHANGE_COURSE},
            .item_count = no_retry ? 2u : 3u,
            .cursor = host->action_cursor,
            .tick = host->menu_age,
            .gamepad = host->gamepad,
            .external_background = (uint8_t)external,
            .graphics = host->graphics.open ? &graphics : NULL};
        return modern_menu_render_pause(menu, &pause, canvas);
    }
    if (race->results_ready)
    {
        ModernMenuResults results = {
            .pack = host->pack,
            .frame = external ? NULL : host->overlay,
            .frame_width = canvas->width,
            .frame_height = canvas->height,
            .frame_w = (float)canvas->width,
            .frame_h = (float)canvas->height,
            .title = host->mode == PORTABLE_COURSE_3D_QUICK    ? "Quick Run standings"
                     : host->mode == PORTABLE_COURSE_3D_BATTLE ? "Battle standings"
                                                               : "VS standings",
            .subtitle = host->meta.name,
            .course_id = host->course.id,
            .row_count = host->racers,
            .labels = {"Retry", "Change course"},
            .item_count = 2,
            .cursor = host->action_cursor,
            .notice = host->error[0] ? host->error : NULL,
            .tick = host->menu_age,
            .gamepad = host->gamepad,
            .external_background = (uint8_t)external};
        const int gp = host->mode == 1u;
        const int gp_failed = gp && (host->cup.stage == PORTABLE_MESH_CUP_RETRY ||
                                     host->cup.stage == PORTABLE_MESH_CUP_GAME_OVER);
        if (gp)
        {
            const int game_over = host->cup.stage == PORTABLE_MESH_CUP_GAME_OVER;
            results.title = game_over                   ? "Grand Prix / Game over"
                            : gp_failed                 ? "Grand Prix / Retry"
                            : host->cup_standings_shown ? "Cup standings"
                                                        : "Grand Prix results";
            results.subtitle = host->meta.cup_name;
            results.labels[0] =
                game_over   ? "Change course"
                : gp_failed ? "Retry"
                : host->cup_standings_shown
                    ? (host->cup.course_index == 3u ? "Trophy ceremony" : "Next race")
                    : "Standings";
            results.labels[1] = gp_failed && !game_over ? "Change course" : NULL;
            results.item_count = gp_failed && !game_over ? 2u : 1u;
        }
        for (unsigned row = 0; row < host->racers; ++row)
        {
            const unsigned player = gp && host->cup_standings_shown ? host->cup.standing_order[row]
                                                                    : race->finish_order[row];
            if (player >= host->racers)
                return 0;
            ModernMenuResultRow *out = &results.rows[row];
            out->driver_id = host->driver_ids[player];
            out->rank = gp && host->cup_standings_shown ? host->cup.standing_rank[player] + 1u
                                                        : race->finish_rank[player];
            out->highlighted = player == 0u;
            if (host->mode == PORTABLE_COURSE_3D_BATTLE)
            {
                const unsigned balloons = host->race.battle.balloons[player];
                snprintf(out->value, sizeof out->value, "%u balloon%s", balloons,
                         balloons == 1 ? "" : "s");
                snprintf(out->detail, sizeof out->detail, "%s %u / %s",
                         player < host->players ? "Player" : "CPU", player + 1u,
                         balloons ? "Survived" : "Eliminated");
            }
            else if (race->retired[player])
                snprintf(out->value, sizeof out->value, "—");
            else
            {
                uint32_t ticks;
                int estimated;
                if (!portable_mesh_race_result_ticks(&host->race, player, &ticks, &estimated))
                    return 0;
                format_ticks(out->value, sizeof out->value, ticks);
                snprintf(out->detail, sizeof out->detail, "%s %u%s",
                         player < host->players ? "Player" : "CPU", player + 1u,
                         estimated ? " / Estimated" : "");
            }
            if (gp && host->cup_standings_shown)
            {
                snprintf(out->value, sizeof out->value, "%u pts", host->cup.points[player]);
                snprintf(out->detail, sizeof out->detail, "+%u this race",
                         host->cup.awards[player]);
            }
            if (race->retired[player])
                snprintf(out->detail, sizeof out->detail, "Player %u / Did not finish",
                         player + 1u);
        }
        return modern_menu_render_results(menu, &results, canvas);
    }
    if (host->intro)
        return draw_intro_hud(host, canvas);
    /* Each local view reads its own published progress. The canvas clip is
     * copied per pane, so the HUD cannot spill into another driver's camera. */
    for (unsigned player = 0; player < host->players; ++player)
    {
        const int single = host->players == 1u;
        const int stacked = host->players == 2u;
        const int x = single || stacked || !(player & 1u) ? 0 : canvas->width / 2;
        const int y = single    ? 0
                      : stacked ? (player ? canvas->height / 2 : 0)
                                : (player & 2u ? canvas->height / 2 : 0);
        const int w = single || stacked
                          ? canvas->width
                          : (player & 1u ? canvas->width - canvas->width / 2 : canvas->width / 2);
        const int h = single ? canvas->height
                      : y    ? canvas->height - canvas->height / 2
                             : canvas->height / 2;
        UiCanvas pane = *canvas;
        ui_canvas_clip(&pane, x, y, x + w, y + h);
        const PortableMeshRacerState *state = &race->racers[player];
        const int battle = host->mode == PORTABLE_COURSE_3D_BATTLE;
        const CourseHud hud = {
            .items = &host->race.items,
            .actors = &host->race.actors,
            .racers = race->racers,
            .racer_count = host->racers,
            .player = player,
            .lap = state->lap < host->meta.laps ? state->lap + 1u : host->meta.laps,
            .laps = host->meta.laps,
            .rank = host->mode == PORTABLE_COURSE_3D_QUICK || host->mode == 1u ||
                            host->mode == PORTABLE_COURSE_3D_VS
                        ? race->standing_rank[player]
                        : 0u,
            .balloons = host->race.battle.balloons[player],
            .race_ticks = state->race_ticks,
            .countdown_ticks = state->countdown_ticks,
            .counting = state->phase == PORTABLE_MESH_RACER_COUNTDOWN,
            .battle = (uint8_t)battle,
            .revenge = host->race.battle.elimination[player].revenge_bomb,
            .tag = host->players > 1u};
        if (!draw_course_hud(host, &pane, (float)x, (float)y, (float)w, (float)h, &hud))
            return 0;
    }
    return 1;
}

static int render_course(ModernMenu *menu, UiCanvas *canvas, void *owner)
{
    PortableCourse3DHost *preview = owner;
    if (!portable_course_3d_host_active(preview) || canvas == NULL)
        return 0;
    /* Pause/results initialize their own shared animation context. Initializing
     * a different screen here too would restart their reveal every frame. */
    if (preview->driving)
        return shared_race(preview) ? render_local_race(preview, menu, canvas)
                                    : render_trial(preview, menu, canvas);
    const ModernMenuView view = {.gamepad = preview->gamepad};
    MmCtx context;
    mm_context_init(&context, menu, &view, NULL, canvas, preview->tick,
                    UINT32_C(0xc3d00000) + preview->flying);
    if (preview->direct && !preview->flying)
    {
        mm_background(&context, 1); /* A cup race is about to start. */
        return 1;
    }
    if (preview->flying)
    {
        if (!draw_world(preview, canvas))
            return 0;
        mm_header(&context, preview->meta.name, "3D course / free-camera preview");
        const MmHint hints[] = {{MM_BUTTON_B, "Courses"},
                                {MM_BUTTON_START, "Reset camera"},
                                {MM_BUTTON_DPAD, "Move / turn"},
                                {MM_BUTTON_LR, "Height"}};
        mm_footer(&context, NULL, hints, 4u);
        UiTextStyle look = ui_text_style(UI_FONT_BOLD, mm_s(&context, 32.0f), MM_WHITE);
        look.outline = mm_s(&context, 3.0f);
        look.outline_color = MM_INK;
        ui_text_draw_centered(canvas, &look, mm_x(&context, context.right - 52.0f),
                              mm_y(&context, context.bottom - 126.0f), UI_ALIGN_RIGHT,
                              "Confirm / Select: look up / down", 1.0f);
        return 1;
    }
    mm_background(&context, 1);
    const int racing_available = preview->mode == PORTABLE_COURSE_3D_TRIAL || shared_race(preview);
    mm_header(&context, "3D courses",
              preview->mode == 1u ? "Choose a four-course Grand Prix or explore"
              : preview->mode == PORTABLE_COURSE_3D_QUICK  ? "Choose a Quick Run or explore"
              : preview->mode == PORTABLE_COURSE_3D_BATTLE ? "Choose a Battle arena or explore"
              : preview->mode == PORTABLE_COURSE_3D_VS     ? "Choose a local VS race or explore"
              : preview->mode == PORTABLE_COURSE_3D_TRIAL
                  ? "Choose a Time Trial or explore"
                  : "Racing is unavailable for this mode; explore a course");
    if (!preview->visible_count)
    {
        mm_tab(&context, 200.0f, 240.0f, 1520.0f, 104.0f, "No courses for this mode",
               "Choose another mode or install a compatible 3D course", 0, 1.0f, MM_ICON_FLAG, 0u,
               0);
        const MmHint back = {MM_BUTTON_B, "2D courses"};
        mm_footer(&context, NULL, &back, 1u);
        return 1;
    }
    const size_t position = visible_cursor(preview);
    const size_t first = position / 5u * 5u;
    PortableNativeImage art;
    const int picture = portable_course_3d_texture(&preview->course, preview->meta.preview_id, &art) &&
                        art.palette == NULL;
    for (size_t i = first; i < preview->visible_count && i < first + 5u; ++i)
    {
        const size_t actual = preview->visible[i];
        mm_tab(&context, 200.0f, 240.0f + (float)(i - first) * 130.0f, picture ? 1000.0f : 1520.0f, 104.0f,
               preview->entries[actual].name, "3D course", actual == preview->cursor, 1.0f,
               MM_ICON_FLAG, 0u, 0);
    }
    if (picture)
    {
        /* The META preview picture beside the list, fitted to its card. */
        const UiImage image = {(const uint32_t *)(const void *)art.pixels, art.width, art.height};
        mm_panel(&context, 1240.0f, 240.0f, 480.0f, 624.0f, 18.0f, 1.0f);
        const float scale = fminf(440.0f / (float)art.width, 584.0f / (float)art.height);
        const float w = (float)art.width * scale, h = (float)art.height * scale;
        ui_draw_image(canvas, &image, mm_x(&context, 1480.0f - w * .5f), mm_y(&context, 552.0f - h * .5f),
                      mm_s(&context, w), mm_s(&context, h), 1.0f, UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
    }
    PortableCourse3DProperty missing;
    if (portable_course_3d_property_find(&preview->course, preview->meta.property_first,
                                         preview->meta.property_count, "mksc:incomplete", &missing) &&
        missing.type == PORTABLE_COURSE_3D_PROPERTY_STRING && missing.value.string[0])
    {
        /* Imported courses name the parts they cannot run yet. */
        char text[512];
        snprintf(text, sizeof text, "Incomplete: %s", missing.value.string);
        const UiTextStyle style = ui_text_style(UI_FONT_BOLD, mm_s(&context, 24.0f), MM_RED);
        ui_text_draw_wrapped(canvas, &style, mm_x(&context, 1240.0f), mm_y(&context, 900.0f),
                             mm_s(&context, 480.0f), mm_s(&context, 28.0f), 3, UI_ALIGN_LEFT, text, 1.0f);
    }
    const MmHint hints[] = {
        {MM_BUTTON_B, "2D courses"},
        {MM_BUTTON_A, preview->meta.physics_profile_id
                          ? (!racing_available                            ? "Unavailable"
                             : preview->mode == 1u                        ? "Grand Prix"
                             : preview->mode == PORTABLE_COURSE_3D_QUICK  ? "Quick Run"
                             : preview->mode == PORTABLE_COURSE_3D_BATTLE ? "Battle"
                             : preview->mode == PORTABLE_COURSE_3D_VS     ? "VS race"
                                                                          : "Time Trial")
                          : "Explore"},
        {MM_BUTTON_START, "Explore"}};
    mm_footer(&context, preview->meta.cup_name, hints, 3u);
    if (preview->error[0] || preview->cup_best_notice[0])
    {
        mm_panel(&context, 200.0f, 885.0f, 1520.0f, 80.0f, 12.0f, 1.0f);
        const UiTextStyle error = ui_text_style(UI_FONT_BOLD, mm_s(&context, 30.0f),
                                                preview->error[0] ? MM_RED : MM_WHITE);
        ui_text_draw_wrapped(canvas, &error, mm_x(&context, 225.0f), mm_y(&context, 918.0f),
                             mm_s(&context, 1470.0f), mm_s(&context, 32.0f), 2, UI_ALIGN_LEFT,
                             preview->error[0] ? preview->error : preview->cup_best_notice, 1.0f);
    }
    return 1;
}

int portable_course_3d_host_world_active(const PortableCourse3DHost *host)
{
    return host && host->active &&
           (host->flying ||
            (host->driving && !host->paused &&
             (shared_race(host)
                  ? !host->race.state.results_ready
                  : host->replaying || host->trial.state.phase != PORTABLE_MESH_TT_FINISHED)));
}

int portable_course_3d_host_benchmark_info(const PortableCourse3DHost *host, size_t *width,
                                           size_t *height, unsigned *views)
{
    if (!host || shared_race(host) || !width || !height || !views || !host->driving ||
        host->paused || host->replaying || host->trial.state.phase != PORTABLE_MESH_TT_RACING ||
        (!host->gpu && !host->gles) || !host->world_width || !host->world_height)
        return 0;
    *width = host->world_width;
    *height = host->world_height;
    *views = host->aid_views;
    return 1;
}
int portable_course_3d_host_wait_idle(PortableCourse3DHost *host, char *error, size_t capacity)
{
    if (host && host->gpu)
        return portable_render_gpu_wait_idle(host->gpu, error, capacity);
    if (host && host->gles)
        return portable_render_gles_wait_idle(host->gles, error, capacity);
    if (error && capacity)
        snprintf(error, capacity, "3D benchmark: no GPU backend");
    return 0;
}

int portable_course_3d_host_render_layer(ModernMenu *menu, UiCanvas *canvas, void *sdl_renderer,
                                         void **background, void *owner)
{
    PortableCourse3DHost *host = owner;
    if (background)
        *background = NULL;
    if (!host || !canvas || !background)
        return 0;
    host->render_renderer = sdl_renderer;
    host->render_background = background;
    const int result = render_course(menu, canvas, owner);
    if (!result)
        fprintf(stderr, "3D course=%u presentation failed: %s\n", host->course.id,
            host->error[0] ? host->error : "invalid scene or UI state");
    host->render_renderer = NULL;
    host->render_background = NULL;
    return result;
}

int portable_course_3d_host_render(ModernMenu *menu, UiCanvas *canvas, void *owner)
{
    void *background = NULL;
    return portable_course_3d_host_render_layer(menu, canvas, NULL, &background, owner);
}

void portable_course_3d_host_reset_renderer(PortableCourse3DHost *host)
{
    if (!host)
        return;
    if (host->gles)
        portable_render_gles_invalidate(host->gles);
    else
        close_acceleration(host);
    host->freeze_valid = host->pose_valid = host->backend_reported = 0;
}
