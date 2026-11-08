#ifndef PORTABLE_GP_CEREMONY_SESSION_H
#define PORTABLE_GP_CEREMONY_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "gp_cup_session.h"
#include "../core/character_audio.h"
#include "ceremony_motion.h"

typedef enum
{
    PORTABLE_GP_CEREMONY_OK = 0,
    PORTABLE_GP_CEREMONY_INVALID_ARGUMENT,
    PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE
} PortableGpCeremonyStatus;

typedef struct
{
    uint8_t cup_index;
    uint8_t track_set;
    uint8_t engine_class;
    uint8_t course_header_index;
    uint8_t selected_driver_class;
    uint8_t player_standing_rank;
    uint8_t podium_driver_classes[3];
    uint8_t performance_rating;
    int16_t driving_score;
    uint16_t finishing_coin_total;
    uint8_t extra_cup_unlocked;
    uint8_t special_cup_unlocked; /* Newly earned fourth GBA gold in this class. */
    uint16_t result_song_id;
    uint8_t state5_next_state;
    uint8_t save_evaluation_pending;
    uint8_t unlock_evaluation_pending;
    uint32_t course_ids[4];
    uint32_t selected_character_id;
    uint32_t race_ticks[4];
    uint32_t race_centiseconds[4];
    uint8_t finishing_ranks[4],finishing_coins[4],finishing_points[4];
    uint8_t voice_override; /* Hosts set 1 + a roster driver's voice class. */
} PortableGpCeremonySession;

PortableGpCeremonyStatus portable_gp_ceremony_session_init(
    PortableGpCeremonySession *session,
    const PortableGpCupSession *final_standings);

/* Presentation time is independent of simulation and source loading states. */
typedef enum {
    PORTABLE_CEREMONY_ENTRANCE, PORTABLE_CEREMONY_CELEBRATION,
    PORTABLE_CEREMONY_WAIT, PORTABLE_CEREMONY_FADE, PORTABLE_CEREMONY_FINISHED
} PortableGpCeremonyPhase;
typedef struct {
    int16_t x,y;
    int8_t value;
    uint8_t phase,tick;
} PortableGpCeremonyCoin;
typedef struct {
    PortableGpCeremonyPhase phase;
    uint32_t tick, entrance_ticks, celebration_ticks, fade_ticks;
    PortableCeremonyMotion motion;
    PortableCeremonyCellState headers[16];
    uint8_t header_modes[16];
    uint32_t source_tick,phase_ticks,overlay_ticks,points_ticks,zoom_wait_ticks,drop_elapsed;
    uint8_t camera_return_tick;
    uint16_t driver_ticks[4];
    uint8_t source_enabled,source_phase,arrival_completed,hop_completed;
    uint8_t driver_script[4],drop_stage,trophy_stage,zoom_stage,fly_stage;
    uint8_t header_active,header_exit,stats_reveal,portrait_mood;
    int8_t camera_vertical_offset;
    int16_t road_line;
    uint8_t loser_darken;
    PortableGpCeremonyCoin coins[4];
    uint16_t coin_total;
    uint8_t coin_stage,unlock_stage,unlock_tick,unlock_reveal,palette_phase;

} PortableGpCeremonyPresentation;
/* GPC4 uses one deterministic source owner. World rules come from the
 * imported hidden ceremony stage; no race or renderer advances its state. */
int portable_gp_ceremony_source_init(PortableGpCeremonyPresentation *view,
    const PortableGpCeremonySession *session,const PortableCeremonyMotionRules *rules,
    const PortableCeremonyWorldRules *world);
int portable_gp_ceremony_source_step(PortableGpCeremonyPresentation *view,
    const PortableGpCeremonySession *session,const PortableCeremonyMotionRules *rules,
    const PortableCeremonyWorldRules *world,const int32_t *sine,size_t sine_count,
    const PortableDriverAffineRules *affine,const PortableCharacterViewRules *character,
    const PortableFinishRules *finish,int confirm_pressed);
/* Script DC3F0: fanfare 55/57, then the result theme 730/1338 ticks later.
 * Returns the song the ceremony starts on music tick `tick`, or 0. */
uint16_t portable_gp_ceremony_song_at(const PortableGpCeremonySession *session,
    uint32_t tick);
/* 8028B90 scripts: crowd 174, flashes 175, podium hops 156, confetti 120,
 * trophy 109, winner voice DBC48, points 101, unlock 110. Writes the songs
 * starting on music tick `tick` and returns how many (at most 3). */
size_t portable_gp_ceremony_cues_at(const PortableGpCeremonySession *session,
    const PortableCeremonyMotionRules *rules,const PortableCharacterAudioRules *audio_rules,
    uint32_t tick,uint16_t songs[3]);
/* Music tick on which the original first accepts confirm (case 11 + 346). */
uint32_t portable_gp_ceremony_input_tick(const PortableGpCeremonySession *session,
    const PortableCeremonyMotionRules *rules);
int portable_gp_ceremony_presentation_init(PortableGpCeremonyPresentation *view,
    uint32_t entrance_ticks, uint32_t celebration_ticks, uint32_t fade_ticks);
void portable_gp_ceremony_presentation_tick(PortableGpCeremonyPresentation *view,
    int confirm_pressed);
/* Optional fast menus: jump the entrance/celebration to the confirm wait.
 * Returns 1 when it skipped; the original ceremony never calls it. */
int portable_gp_ceremony_presentation_skip(PortableGpCeremonyPresentation *view);
#endif
