#ifndef PORTABLE_GAME_HOST_INTERNAL_H
#define PORTABLE_GAME_HOST_INTERNAL_H

/* Shared implementation details of the native SDL host. */
#include "asset_pack.h"
#include "settings.h"
#ifdef PORTABLE_ENABLE_SDL3
#include "sdl_gamepad.h"
#endif
#include "recovered_pack.h"
#include "recovered_race_smoke.h"
#include "presentation_pack.h"
#include "course_mirror.h"
#include "mod_identity.h"
#include "content_registry.h"
#include "time_trial_flow.h"
#include "frontend_flow.h"
#include "frontend_input_replay.h"
#include "frontend_presentation.h"
#include "frame_scheduler.h"
#include "race_audio_timeline.h"
#include "recovered_time_trial_host.h"
#include "recovered_time_trial_replay.h"
#include "time_trial_ghost_store.h"
#include "time_trial_ghost_playback.h"
#include "time_trial_result.h"
#include "gp_race_roster.h"
#include "gp_race_session.h"
#include "gp_race_presentation.h"
#include "gp_start_lakitu.h"
#include "gp_rank_hud.h"
#include "gp_coin_hud.h"
#include "gp_item_hud.h"
#include "gp_race_results.h"
#include "gp_cup_session.h"
#include "gp_failure_menu.h"
#include "gp_result_clock.h"
#include "gp_ceremony_session.h"
#include "gp_progress_store.h"
#include "gp_ceremony_asset.h"
#include "gp_ceremony_renderer.h"
#include "gp_standings_session.h"
#include "gp_standings_renderer.h"
#include "gp_results_flow.h"
#include "gp_results_session.h"
#include "gp_results_renderer.h"
#include "race_particle_renderer.h"
#include "item_box_renderer.h"
#include "camera_projection.h"
#include "camera_state.h"
#include "character_renderer.h"
#include "hud_renderer.h"
#include "driver_vector_effect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef PORTABLE_ENABLE_SDL3
#include "audio_command_stream.h"
#include "audio_session.h"
#include "engine_control.h"
#include "engine_sound_asset.h"
#include "frontend_audio.h"
#include "sdl_audio_queue.h"
#include <SDL3/SDL.h>
#endif

#include "frontend_config.h"
#include "script_host.h"

#ifdef PORTABLE_ENABLE_SDL3
#include "mesh_item_audio.h"
#include "sound_set_asset.h"
#include "course_3d_host.h"
/* Semantic mesh audio input: SI quantities never masquerade as recovered
 * driver fields. Pack bytes stay borrowed through mesh end/rebind. */
typedef struct
{
    const PortableAssetPackView *pack;
    uint32_t course_id, driver_id, music_kind, music_id, mode;
    uint32_t sound_set_id;   /* kind-114 replacement sounds, or 0 */
    uint32_t driver_ids[8]; /* each racer's driver, for a sound set's voices */
    uint8_t voice_class, engine_class;
    uint8_t items_enabled, human_mask, donor[8];
} PortableMeshAudioConfig;
typedef struct
{
    uint32_t tick, phase, countdown_remaining, laps_completed, lap_count;
    int32_t speed_q16, target_speed_q16;
    uint16_t buttons; /* Mesh semantic accelerate1/brake2/hop4/rescue8. */
    uint8_t hopped, landed, boost_released, panel_started, respawning, paused;
    uint8_t drifting, drift_tier; /* grounded drift and its mini-turbo tier 0..3 */
    uint8_t finished, rank, racer_count, record_changed, revenge_bomb;
    uint8_t grounded, surface_sound; /* the floor's SURF mksc:sound-surface class, 0 none */
    uint8_t start_boost, hazard_hit; /* rocket start boost active, this tick's OBJS hazard contact */
    /* Positional sounds (SURF-free): ZONE kind-3 and OBJS mksc:sound emitters
     * posed at this tick, heard from listener_q16 (the first human). */
    const PortableMeshSoundEmitter *emitters;
    uint32_t emitter_count;
    int32_t listener_q16[3];
    const PortableMeshItems *items;
    const PortableMeshItemEvents *item_events;
    const PortableMeshCourseActorEvents *actor_events;
    const PortableMeshCourseActorsConfig *source_actor_config;
    const PortableMeshCourseActors *source_actors;
    const PortableMeshRacerState *racers;
    uint8_t human_mask;
} PortableMeshAudioFrame;
enum { PORTABLE_MESH_AUDIO_COUNTDOWN = 0, PORTABLE_MESH_AUDIO_RACING = 1,
       PORTABLE_MESH_AUDIO_FINISHED = 2 };
typedef struct
{
    uint32_t last_tick, mode;
    uint16_t engine_song;
    uint8_t active, seen_tick, respawning, engine_player;
    uint8_t items_enabled, human_mask, donor[8], haunt_active;
    uint16_t mushroom_voices[8][2];
    PortableMeshItemAudio item_audio;
    /* A course's kind-114 sound set: stock cues it replaces start its songs
     * on the stock cue's player; drift and spark cues exist only with it. */
    PortableSoundSet sound_set;
    uint8_t has_sound_set, drift_player, spark_player, drifting, drift_tier;
    uint16_t driver_engine; /* the set's engine for the human driver, 0: its engine cues */
    uint16_t final_lap_song; /* the set's final-lap jingle, held until it ends */
    uint16_t drift_song, run_song, emitter_song; /* held surface/positional songs */
    uint8_t run_player, emitter_player, last_rank, finish_voiced, start_seen;
    uint32_t emitter_index, emitter_started, voice_tick;
    uint32_t driver_ids[8]; /* each racer's driver, for a set's voices */
} PortableMeshAudioState;
typedef struct
{
    PortableAudioSession session;
    PortableSdlAudioQueue queue;
    uint32_t command_sequence;
    PortableMeshAudioState mesh_audio;
    FILE *capture_file;
    uint32_t capture_frames;
    uint16_t capture_peak;
    uint8_t capture_attempted;
    PortableRaceAudioTimeline race_timeline;
    uint32_t race_course_id;
    uint32_t race_character_id;
    int race_paused;
    uint32_t horn_listener_flags; /* last local +0x48 seen by 7FD8's horn gate */
    PortableRaceMusic race_music;
    uint8_t haunt_music; /* TT Broken Pier haunt effect2 latch. */
    PortableFinishAudio finish_audio;
    PortableFinishAudio result_finish_audio;
    PortableRaceMusic result_race_music;
    uint8_t result_replay_active;
    PortableLapAudio lap_audio;
    PortableFinishSoundRules finish_rules;
    PortableNearbyStarAudio nearby_star_audio;
    PortableLakituAudio lakitu_audio;
    uint32_t lakitu_pulses;
    uint32_t course_loop_starts;
    uint32_t lap_cues,final_lap_starts,final_lap_restores;
    PortableEngineControl engine_control;
    PortableNearbyEngineState nearby_engine;
    PortableEngineSoundRules engine_rules;
    PortableSurfaceSoundRules surface_rules;
    PortableHazardSoundRules hazard_rules;
    PortableSurfaceAudioState surface_audio;
    uint8_t race_engine_class;
    uint32_t last_charged_audio_cue_count;
    uint32_t last_contact_audio_serial;
    uint32_t last_boost_voice_serial;
    uint32_t last_fall_audio_serial[PORTABLE_GP_OPPONENT_COUNT];
    uint32_t last_cannonball_audio_serial;
    uint32_t last_piranha_audio_serial;
    uint32_t last_thwomp_audio_serial;
    uint32_t last_prop_audio_serial;
    uint32_t last_impact_audio_serial;
} RouteAudioHost;

typedef struct
{
    const PortableAssetPackView *pack;
    PortableCourseContent course;
    PortableRecoveredPackStorage *storage;
    PortablePresentationPackStorage *presentation_storage;
    PortableRecoveredRaceView race;
    PortablePresentationPackView presentation;
} GhostRaceContext;

typedef struct
{
    uint8_t quit_requested;
    uint8_t results_completed;
    uint8_t next_mode;
    uint32_t scene_request;
    PortableGpCupSession cup_session;
    uint8_t cup_session_ready;
    uint32_t marker_clock,marker_display_clock;
} PortableGpRaceHostOutcome;

/* Bounded, application-owned input replay. No pack/save fields contain pointers. */
#define PORTABLE_GP_REPLAY_MAX_TICKS 131072u
typedef struct {
    uint16_t buttons[PORTABLE_GP_REPLAY_MAX_TICKS];
    uint32_t count;
    uint64_t digest;
    uint8_t driving_profile;
    uint8_t deterministic_digest[PORTABLE_ASSET_PACK_DIGEST_SIZE];
    PortableQuickRunResults results;
    uint8_t complete, overflow;
} PortableGpInputRecording;

/* Scene configuration borrows the pack, cup state, audio and surface
 * until end. The application retains event polling and frame invocation. */
typedef struct {
    const PortableAssetPackView *pack;
    uint32_t course_id,character_id;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    const PortableGpCupSession *previous_cup;
    uint8_t course_in_cup,course_header_index;
    RouteAudioHost *audio;
    uint8_t engine_class;
    uint8_t cup_index;
    uint8_t retry_credits;
    uint8_t track_set; /* PortableTrackSet */
    uint8_t input_aid; /* Existing dev cup command supplies buttons only. */
    uint8_t interpolate;
    uint16_t render_fps;
    uint8_t quick_run;
    PortableQuickRunOptions quick_run_options;
    PortableGpInputRecording *recording;
    uint8_t playback, replay_aid;
    uint8_t test_seeded_standings; /* Test build only: final-race ceremony fixture. */
    uint8_t internal_scale,fit_scaling;
    uint16_t aspect_width;
    uint8_t battle; /* Arena combat; track_set alone no longer implies it. */
    PortableScriptHost *scripts; /* Optional mod scripts; borrowed until end. */
    uint8_t rumble,input_display; /* Presentation-only options. */
    uint8_t fix_doppler_wrap; /* Audio-only original-bug fix. */
    uint8_t fast_menus; /* Start skips the post-race board (never while recording). */
    uint32_t marker_clock,marker_display_clock;
    uint8_t modern_menus; /* Pause shows the modern overlay instead of the source list. */
    uint8_t artwork_lod; /* Presentation-only authored detail policy. */
    uint8_t race_renderer; /* 0 Original, 1 host-owned 3D presentation. */
    PortableGraphicsHook graphics; /* Optional: the pause menu's Graphics page (Select). */
} PortableGpHostScene;
typedef struct PortableGpHost PortableGpHost;
PortableGpHost *portable_gp_host_begin(const PortableGpHostScene *configuration);
void portable_gp_host_event(PortableGpHost *host,const SDL_Event *event);
/* 0 active, 1 complete, -1 failed. */
int portable_gp_host_frame(PortableGpHost *host);
int portable_gp_host_end(PortableGpHost *host,PortableGpRaceHostOutcome *outcome);

/* Transactional mixer-only rebind. Does not replace/close the SDL queue or
 * capture owner. Call before returning to menu with the shell's 2D pack. */
int route_audio_rebind_pack(RouteAudioHost *audio, const PortableAssetPackView *pack);
int route_audio_mesh_begin(RouteAudioHost *audio, const PortableMeshAudioConfig *configuration);
/* One call per accepted audio tick, including results delay ticks. Duplicates
 * do not mix; paused calls only apply pause/resume transitions. tick is the
 * caller's accepted ordinal, not a frozen finished simulation clock. */
int route_audio_mesh_tick(RouteAudioHost *audio, const PortableMeshAudioFrame *frame);

int route_audio_render(void *user,uint16_t buttons,int prestart,
                              const PortableRecoveredTimeTrialSnapshot *snapshot);
int route_audio_render_gp(RouteAudioHost *audio,uint16_t buttons,
                                 const PortableGpRaceSnapshot *snapshot);
int route_audio_set_paused(void *user,int paused);
int route_audio_begin_race(RouteAudioHost *audio,uint32_t course_id,
                                  uint32_t character_id, uint8_t engine_class);
int route_audio_restart(void *user);
int route_audio_result_replay(void *user,int active);
int route_audio_cue(void *user,uint16_t song);
int route_audio_horn(void *user);
int route_audio_return_menu(RouteAudioHost *audio);
int route_audio_capture_close(RouteAudioHost *audio);
int route_audio_menu_cue(RouteAudioHost *audio,uint16_t song_id);
int route_audio_begin_ceremony(RouteAudioHost *audio,uint16_t song_id);
int route_audio_fade_ceremony(RouteAudioHost *audio);
uint64_t frontend_now_ns(uint64_t frequency);
int run_battle_menu_smoke(const PortableAssetPackView *pack,uint8_t arena,uint32_t character,uint8_t engine_class,const PortableSettingsOverrides *overrides);
int run_quick_run_menu_smoke(const PortableAssetPackView *pack,uint8_t cup,
    uint32_t character,uint8_t engine_class,uint8_t options_mask,uint8_t replay_aid,const PortableSettingsOverrides *overrides);
int run_gp_cup_menu_smoke(const PortableAssetPackView *pack,uint8_t cup,
    uint32_t character,uint8_t engine_class,int interpolate,uint16_t render_fps,const PortableSettingsOverrides *settings);
int run_game_shell_smoke(const PortableAssetPackView *pack,const char *save_directory,
    uint32_t character,const PortableSettingsOverrides *overrides);
int run_direct_time_trial_sdl(const PortableAssetPackView *pack,
                                     uint32_t course_id,
                                     uint32_t character_id);
/* Result scenes borrow all art and surfaces until end; frame takes the
 * application's monotonic clock and never polls events or waits. */
typedef struct PortableGpResultsHost PortableGpResultsHost;
PortableGpResultsHost *portable_quick_results_begin(const PortableUiTextBank *ui_text,const uint32_t *race_pixels,
    uint8_t replay_available,RouteAudioHost *audio,
    SDL_Renderer *renderer,SDL_Texture *texture,const PortableGpResultsAssetView *window,
    const PortableGpStandingsAssetView *asset,const PortableQuickRunResults *results);
PortableGpResultsHost *portable_battle_results_begin(const PortableUiTextBank *ui_text,const uint32_t *race_pixels,
    RouteAudioHost *audio,SDL_Renderer *renderer,SDL_Texture *texture,const PortableGpResultsAssetView *window,
    const PortableGpStandingsAssetView *asset,const PortableBattleResults *results,uint8_t engine_class);
void portable_quick_results_aid(PortableGpResultsHost *host,uint8_t replay);
void portable_gp_results_allow_skip(PortableGpResultsHost *host);
PortableGpResultsHost *portable_gp_retry_begin(const PortableAssetPackView *pack,
    const uint32_t *race_pixels,uint8_t retry_credits,
    SDL_Renderer *renderer,SDL_Texture *texture,RouteAudioHost *audio);
void portable_gp_results_skip_transfer(PortableGpResultsHost *host);
PortableGpResultsHost *portable_gp_standings_begin(const PortableUiTextBank *ui_text,
    const PortableGpStandingsAssetView *asset,const PortableGpCupSession *cup,
    SDL_Renderer *renderer,SDL_Texture *texture,RouteAudioHost *audio);
void portable_gp_results_host_event(PortableGpResultsHost *host,const SDL_Event *event);
int portable_gp_results_host_frame(PortableGpResultsHost *host,uint64_t now_ns);
/* A NULL frame advances the retained race on a fixed result tick; otherwise
 * the callback renders an immutable scene beneath the standings layers. */
int portable_gp_results_failure_advances_portraits(const PortableGpResultsHost *host);
int portable_gp_results_failure_exiting(const PortableGpResultsHost *host);
int portable_gp_results_failure_hud_visible(const PortableGpResultsHost *host);
/* Seed Quick2123C's palette phase from the accepted race clock. */
void portable_gp_results_source_clock(PortableGpResultsHost *host,uint32_t clock);
void portable_gp_results_failure_palette(PortableGpResultsHost *host,
    const PortableGpResultsAssetView *asset);
void portable_gp_results_set_race_background(PortableGpResultsHost *host,
    int (*background)(void *,PortableSoftwareFrame *),void *context);
void portable_gp_results_host_modern(PortableGpResultsHost *host,
    const PortableAssetPackView *pack,uint8_t enabled,uint32_t selected_driver_id,uint32_t course_id);
int portable_gp_results_host_video(PortableGpResultsHost *host,uint8_t scale,
    uint16_t aspect_width,int fit,const PortableSoftwareFrame *race_frame);
int portable_gp_results_host_end(PortableGpResultsHost *host,
    uint8_t *next_mode,uint8_t *quit_requested);
typedef struct PortableGpCeremonyHost PortableGpCeremonyHost;
/* selected_character is the player's character manifest ID (0: the stock
 * driver of the ceremony's selected class). */
PortableGpCeremonyHost *portable_gp_ceremony_host_begin(
    const PortableAssetPackView *pack,const PortableGpCeremonySession *ceremony,
    uint32_t selected_character,SDL_Window *window,SDL_Renderer *renderer,SDL_Texture *texture,RouteAudioHost *audio);
/* Fast menus: A/Start skips the podium entrance and celebration. */
void portable_gp_ceremony_host_allow_skip(PortableGpCeremonyHost *host);
void portable_gp_ceremony_host_modern(PortableGpCeremonyHost *host,uint8_t enabled);
void portable_gp_ceremony_host_aspect(PortableGpCeremonyHost *host,uint16_t width);
int portable_gp_ceremony_host_video(PortableGpCeremonyHost *host,uint8_t scale,int fit);
void portable_gp_ceremony_host_event(PortableGpCeremonyHost *host,const SDL_Event *event);
int portable_gp_ceremony_host_frame(PortableGpCeremonyHost *host,uint64_t now_ns);
int portable_gp_ceremony_host_end(PortableGpCeremonyHost *host,uint8_t *quit_requested);
int run_gp_race_sdl_scene(const PortableAssetPackView *pack,
    uint32_t course_id,uint32_t character_id,SDL_Window *host_window,
    SDL_Renderer *host_renderer,SDL_Texture *host_texture,
    const PortableGpCupSession *previous_cup,uint8_t course_in_cup,
    uint8_t course_header_index,
    RouteAudioHost *audio,
    PortableGpRaceHostOutcome *outcome);
int run_gp_race_sdl(const PortableAssetPackView *pack,
                           uint32_t course_id,uint32_t character_id);
int run_time_trial_sdl_settings(const PortableAssetPackView *pack,const PortableSettingsOverrides *overrides);

/* Two to four local players in VS (laps) or Battle (arena) on one race
 * session. Borrows the pack, renderer and audio until the run returns. */
typedef struct {
    const PortableAssetPackView *pack;
    uint32_t course_id;
    uint8_t players,battle,engine_class,input_aid,fit_scaling;
    uint32_t characters[4];
    PortableQuickRunOptions options;
    SDL_Renderer *renderer;
    RouteAudioHost *audio; /* Optional. */
    uint8_t rumble,input_display; /* Presentation-only options. */
    uint8_t fix_doppler_wrap; /* Audio-only original-bug fix. */
    PortableScriptHost *scripts; /* Borrowed for this shared race. */
    uint8_t internal_scale; /* Physical sampling resolution of every split view. */
    uint8_t modern_menus; /* Pause shows the modern overlay instead of the source list. */
    uint8_t artwork_lod; /* Same presentation policy for every split view. */
    uint8_t race_renderer; /* Every split reuses the host's 3D workspace. */
    uint16_t aspect_width,render_fps; /* Width is per pane; zero follows its output aspect. */
    uint8_t interpolate;
    PortableGraphicsHook graphics; /* Optional: the pause menu's Graphics page (Select). */
} PortableMultiRaceScene;
typedef struct { uint8_t completed,quit_requested; } PortableMultiRaceOutcome;
/* Blocks until results close, Escape leaves or the app quits; 0 on success. */
int run_multi_race_sdl(const PortableMultiRaceScene *scene,PortableMultiRaceOutcome *outcome);
/* Presses SCRIPT (A B S=Start E=Select U D L R l=L r=R, '.' waits) on settled
 * menu screens, writing each screen to FRAMES/NNN.ppm when FRAMES is set. */
int run_menu_tour_smoke(const PortableAssetPackView *pack,const char *save_directory,
    const char *script,const char *frames,const PortableSettingsOverrides *overrides);
int run_local_multiplayer_smoke(const PortableAssetPackView *pack,uint8_t battle,uint8_t players,
    uint32_t course_id,const PortableSettingsOverrides *overrides);

#endif
#endif
