#ifndef PORTABLE_COURSE_3D_HOST_H
#define PORTABLE_COURSE_3D_HOST_H

#include "graphics_options.h"
#include "asset_pack.h"
#include "native_image.h"
#include "modern_menu.h"
#include "mesh_time_trial.h"
#include "mesh_race.h"
#include "settings.h"

typedef struct PortableCourse3DHost PortableCourse3DHost;
typedef union SDL_Event SDL_Event;
/* A positional sound posed at the audio tick: a kind-3 ZONE or an OBJS row
 * with integer mksc:sound (START_SONG argument) heard within distance_q16
 * metres, restarted every period ticks (0: whenever it ends). */
typedef struct
{
    int32_t position_q16[3], distance_q16;
    uint16_t song, period;
} PortableMeshSoundEmitter;
enum { PORTABLE_MESH_SOUND_EMITTER_LIMIT = 128, PORTABLE_MESH_PARTICLE_EMITTER_LIMIT = 64 };
typedef struct
{
    const PortableAssetPackView *pack;
    const PortableMeshRacerState *state;
    const PortableMeshKartRules *rules;
    const PortableMeshItems *items;
    const PortableMeshItemEvents *item_events;
    const PortableMeshCourseActorEvents *actor_events;
    const PortableMeshCourseActorsConfig *source_actor_config;
    const PortableMeshCourseActors *source_actors;
    const PortableMeshRacerState *racers;
    const uint32_t *driver_ids;
    uint8_t human_mask;
    PortableMeshKartEvents events;
    uint32_t generation, ordinal, course_id, driver_id, mode, engine_class;
    uint32_t music_kind, music_id, lap_count;
    uint32_t sound_set_id; /* kind-114 set from META mksc:sound-set, or 0 */
    uint16_t buttons;
    uint8_t paused, finished, rank, racer_count, ceremony, revenge_bomb;
    uint8_t surface_sound; /* first human's floor: SURF mksc:sound-surface, 0 none or airborne */
    const PortableMeshSoundEmitter *emitters;
    uint32_t emitter_count;
} PortableCourse3DAudioView;
enum
{
    PORTABLE_COURSE_3D_TRIAL = 2,
    PORTABLE_COURSE_3D_QUICK = 4,
    PORTABLE_COURSE_3D_VS = 8,
    PORTABLE_COURSE_3D_BATTLE = 16
};
/* Content stays borrowed until close; configuration identity/path are copied. */
PortableCourse3DHost *portable_course_3d_host_create(const PortableAssetPackView *pack, char *error,
                                                     size_t capacity);
int portable_course_3d_host_configure(PortableCourse3DHost *host, const uint8_t digest[32],
                                      uint32_t driver_id, const char *save_directory,
                                      const PortableSettings *settings);
int portable_course_3d_host_configure_race(PortableCourse3DHost *host, const uint8_t digest[32],
                                           uint32_t mode, unsigned players, unsigned engine_class,
                                           const uint32_t driver_ids[PORTABLE_MESH_LOCAL_MAX],
                                           const char *save_directory,
                                           const PortableSettings *settings);
/* After configuring: each local player's kart (kind 111); 0 races the
 * driver's own kart. Unknown karts fail when the race starts. */
/* The drivers rivals may be drawn from (the player's content platform);
 * count 0 lifts the limit. Fails while driving or above 64 drivers. */
int portable_course_3d_host_set_roster(PortableCourse3DHost *host, const uint32_t *driver_ids,
                                       size_t count);
int portable_course_3d_host_set_karts(PortableCourse3DHost *host,
                                      const uint32_t kart_ids[PORTABLE_MESH_LOCAL_MAX]);
unsigned portable_course_3d_host_local_count(const PortableCourse3DHost *host);
/* Catalog rows for the ordinary cup screens; strings borrow the pack. */
typedef struct
{
    uint32_t course_id, cup_id, slot, modes, sort_key;
    const char *name, *cup_name;
    uint8_t raceable; /* A physics profile: Confirm races rather than explores. */
} PortableCourse3DListing;
size_t portable_course_3d_host_listing_count(const PortableCourse3DHost *host);
int portable_course_3d_host_listing(const PortableCourse3DHost *host, size_t index,
                                    PortableCourse3DListing *out);
/* META preview picture (ARGB view into the pack), if the course has one. */
int portable_course_3d_host_preview_image(const PortableCourse3DHost *host, uint32_t course_id,
                                          PortableNativeImage *out);
/* After configure_race: race (or, for explore or an unprofiled course, fly
 * through) one course in the configured mode; GP takes its whole cup. The
 * host becomes active, starts the race on its next tick and deactivates
 * itself when that ends. 0 leaves it inactive with portable_course_3d_host_error
 * describing the refusal. */
int portable_course_3d_host_launch(PortableCourse3DHost *host, uint32_t course_id, int explore);
/* The launched race refused to start on its first tick (see error). */
int portable_course_3d_host_launch_failed(const PortableCourse3DHost *host);
const char *portable_course_3d_host_error(const PortableCourse3DHost *host);
/* Read-only cup position; UINT32_MAX outside an active GP owner. */
uint32_t portable_course_3d_host_gp_course_index(const PortableCourse3DHost *host);
/* Retry credits left in the live 3D GP cup, or UINT32_MAX outside one. */
uint32_t portable_course_3d_host_gp_retry_credits(const PortableCourse3DHost *host);
const PortableMeshRaceSession *portable_course_3d_host_race(const PortableCourse3DHost *host);
/* Accepted fixed-tick audio view, borrowed until the next host tick. Rendering
 * never changes its generation, ordinal, state or event observations. */
int portable_course_3d_host_audio(const PortableCourse3DHost *, PortableCourse3DAudioView *);
void portable_course_3d_host_device_event(PortableCourse3DHost *host, const SDL_Event *event);
void portable_course_3d_host_sample_locals(PortableCourse3DHost *host,
                                           PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX],
                                           uint16_t *menu_buttons);
/* Held semantic item controls sampled with movement; no simulation is advanced. */
void portable_course_3d_host_item_input(PortableCourse3DHost *, unsigned player,
                                       PortableMeshItemControls controls);
void portable_course_3d_host_tick_locals(
    PortableCourse3DHost *host, const PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX],
    uint16_t menu_buttons);
/* One call per admitted fixed tick; never called by rendering. */
void portable_course_3d_host_tick_driving(PortableCourse3DHost *host, PortableMeshKartInput input,
                                          uint16_t menu_buttons);
void portable_course_3d_host_set_paused(PortableCourse3DHost *host, int paused);
/* Optional: lets the pause menu's Graphics page edit and save settings through the shell. */
void portable_course_3d_host_set_graphics(PortableCourse3DHost *host, PortableGraphicsHook hook);
int portable_course_3d_host_driving(const PortableCourse3DHost *host);
const PortableMeshTimeTrialSession *
portable_course_3d_host_session(const PortableCourse3DHost *host);
/* SDL renderer reset invalidates only presentation caches. */
void portable_course_3d_host_reset_renderer(PortableCourse3DHost *host);
void portable_course_3d_host_close(PortableCourse3DHost *preview);
int portable_course_3d_host_available(const PortableCourse3DHost *preview);
int portable_course_3d_host_active(const PortableCourse3DHost *preview);
void portable_course_3d_host_show(PortableCourse3DHost *preview);
void portable_course_3d_host_set_gamepad(PortableCourse3DHost *preview, uint8_t gamepad);
/* Compatible with the shared modern host canvas callback. */
int portable_course_3d_host_render(ModernMenu *menu, UiCanvas *canvas, void *owner);
/* Renderer and background output are borrowed only during this call. The
 * returned GPU texture remains borrowed from this host until resize/close.
 * Close the host before destroying its parent SDL renderer. */
int portable_course_3d_host_render_layer(ModernMenu *menu, UiCanvas *canvas, void *sdl_renderer,
                                         void **background, void *owner);
int portable_course_3d_host_world_active(const PortableCourse3DHost *host);
/* Diagnostic timing only: ready, unpaused ordinary racing on a real GPU path. */
int portable_course_3d_host_benchmark_info(const PortableCourse3DHost *host, size_t *width,
                                           size_t *height, unsigned *views);
int portable_course_3d_host_wait_idle(PortableCourse3DHost *host, char *error, size_t capacity);
/* Render-only scheduler fraction. A cut snaps to the accepted current pose;
 * this setter never advances or modifies simulation/replay state. */
void portable_course_3d_host_set_presentation(PortableCourse3DHost *host, uint32_t alpha_q16,
                                              int interpolate, int cut);

#endif
