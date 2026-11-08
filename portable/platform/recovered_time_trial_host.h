#ifndef PORTABLE_RECOVERED_TIME_TRIAL_HOST_H
#define PORTABLE_RECOVERED_TIME_TRIAL_HOST_H

#include "graphics_options.h"
#include <stddef.h>
#include <stdint.h>

#include "race_scene_renderer.h"
#include "asset_pack.h"
#include "recovered_pack.h"
#include "recovered_time_trial_session.h"
#include "gp_results_asset.h"
#include "script_host.h"

typedef struct
{
    const PortableRecoveredRaceView *race;
    PortableRaceSceneView presentation;
    const PortableAssetPackView *frontend_pack;
    const PortableGpResultsAssetView *lap_board; /* Optional ROM lap board art. */
    const uint8_t *result_course_name_tiles; /* Optional768-byte source title glyphs. */
    /* Optional caller-owned local path for a semantic-tick recording. */
    const char *record_path;
    /* Optional local diagnostic state for an external acceptance driver. */
    const char *telemetry_path;
    /* Optional local diagnostic capture of the first settled pause frame. */
    const char *pause_capture_path;
    /* Optional caller-owned SDL window, renderer, 240x160 ARGB texture and
       controller slot. All four must be present together. */
    void *host_window;
    void *host_renderer;
    void *host_texture;
    void *host_controller_slot;
    /* Optional app-owned resolver for a saved ghost's source driver class and
       roster driver (0 for the class's stock driver). The returned race view
       must remain valid for the host call. */
    void *ghost_user;
    int (*ghost_prepare)(
        void *user, uint8_t driver_class, uint8_t roster_driver,
        const PortableRecoveredRaceView **race,
        const PortableCharacterAssetView **character);
    /* Optional source-observed presentation prelude. Simulation and semantic
       recording remain frozen for this many 60 Hz presentation ticks. */
    uint16_t prelude_ticks;
    uint16_t render_fps; /* 0 uncapped; effective only with interpolation. */
    uint8_t interpolate;
    uint8_t internal_scale,fit_scaling;
    uint16_t aspect_width;
    uint8_t ghost_received_slot; /* 0 local, 1..2 received; results always save locally. */
    uint8_t require_selected_ghost; /* Menu advertised it; do not silently omit it. */
    uint8_t ghost_hidden; /* GHOST DISPLAY turned this ghost off: race without playback. */
    const char *save_directory; /* Optional borrowed test-save directory, ending in a separator. */
    uint8_t input_aid; /* Bounded developer route: ordinary controls, accelerated clock. */
    uint8_t rumble,input_display; /* Presentation-only options. */
    uint8_t modern_menus; /* Pause shows the modern overlay instead of the source box. */
    PortableScriptHost *scripts; /* Optional mod scripts; borrowed until end. */
    /* Optional presentation-only audio sink. It advances from a separate
       60 Hz host clock and cannot tick race semantics. */
    void *audio_user;
    int (*audio_set_paused)(void *user,int paused);
    int (*audio_restart)(void *user);
    /* Retain the completed result music while its recording plays back. */
    int (*audio_result_replay)(void *user,int active);
    int (*audio_cue)(void *user,uint16_t song); /* Optional menu cue. */
    int (*audio_horn)(void *user); /* Optional Select horn. */
    int (*audio_render_frame)(
        void *user, uint16_t buttons, int prestart,
        const PortableRecoveredTimeTrialSnapshot *snapshot);
    uint32_t marker_clock,marker_display_clock;
    uint8_t artwork_lod; /* Presentation only; recordings and ghosts ignore it. */
    uint8_t race_renderer; /* 0 Original, 1 host-owned 3D presentation. */
    PortableGraphicsHook graphics; /* Optional: the pause menu's Graphics page (Select). */
} PortableRecoveredTimeTrialHostScene;

typedef enum
{
    PORTABLE_TIME_TRIAL_RETURN_NONE = 0,
    PORTABLE_TIME_TRIAL_RETURN_COURSE,
    PORTABLE_TIME_TRIAL_RETURN_CHARACTER,
    PORTABLE_TIME_TRIAL_RETURN_MODE
} PortableRecoveredTimeTrialReturn;

typedef struct
{
    uint32_t result_ticks;
    uint32_t replays_completed;
    uint32_t marker_clock,marker_display_clock;
    int finished;
    int return_to_selection;
    int quit_requested;
    PortableRecoveredTimeTrialReturn return_destination;
} PortableRecoveredTimeTrialHostOutcome;

int portable_recovered_time_trial_render_with_ghost(
    const PortableRecoveredTimeTrialHostScene *scene,
    const PortableRecoveredTimeTrialSnapshot *snapshot,
    const PortableRecoveredTimeTrialSnapshot *ghost_snapshot,
    const PortableCharacterAssetView *ghost_character,
    PortableSoftwareFrame *frame,
    uint8_t pause_cursor,
    uint8_t pause_open_ticks);

int portable_recovered_time_trial_host_run(
    const PortableRecoveredTimeTrialHostScene *scene,
    PortableRecoveredTimeTrialHostOutcome *outcome);

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
typedef struct PortableTimeTrialHost PortableTimeTrialHost;

/* Scene data and borrowed surface remain valid until end. Event/frame never
 * poll or own the application's loop. Frame: 0 active, 1 ended, -1 failed. */
PortableTimeTrialHost *portable_time_trial_host_begin(
    const PortableRecoveredTimeTrialHostScene *configuration);
void portable_time_trial_host_event(PortableTimeTrialHost *host,
                                    const SDL_Event *event);
int portable_time_trial_host_frame(PortableTimeTrialHost *host);
int portable_time_trial_host_end(PortableTimeTrialHost *host,
                                 PortableRecoveredTimeTrialHostOutcome *outcome);
#endif

#endif
