#ifndef PORTABLE_RACE_AUDIO_TIMELINE_H
#define PORTABLE_RACE_AUDIO_TIMELINE_H

/* The stock final-lap music effect's song (race music effect 32). */
#define PORTABLE_RACE_FINAL_LAP_SONG 19u

#include <stddef.h>
#include <stdint.h>
#include "race_session.h"

typedef struct
{
    uint16_t song_id;
    uint8_t player;
} PortableRaceAudioEvent;

typedef struct
{
    uint16_t frame;
    uint16_t course_song;
    uint16_t engine_song;
    uint8_t active;
    uint8_t countdown_cues;
    uint16_t music_wait;
} PortableRaceAudioTimeline;

/* 522A4/523DC own music priority independently of sound-effect players.
 * Requests remain latched beneath higher priorities until explicitly released. */
typedef struct {
    uint8_t effects;
    uint32_t star_uses,lightning_uses;
} PortableRaceMusic;
typedef struct {
    uint16_t song_id,tempo_scale;
} PortableRaceMusicEvent;
typedef struct {
    uint32_t star_uses,lightning_uses;
    uint16_t course_song;
    uint8_t star_active,lightning_active,finished,final_lap,boo_active;
} PortableRaceMusicView;
int portable_race_music_request(PortableRaceMusic *music,uint8_t effect,
    uint8_t finished,uint16_t course_song,PortableRaceMusicEvent *event);
int portable_race_music_release(PortableRaceMusic *music,uint8_t effect,
    uint8_t finished,uint16_t course_song,uint8_t final_lap,PortableRaceMusicEvent *event);
size_t portable_race_music_step(PortableRaceMusic *music,const PortableRaceMusicView *view,
    PortableRaceMusicEvent events[6]);

/* C38C finish scripts normalized from EE5E4/EE604/EE624/EE644. */
typedef struct { uint16_t cue,delay,music; } PortableFinishSoundSequence;
typedef struct {
    PortableFinishSoundSequence sequences[4]; /* first, placed, failed, last */
    uint16_t battle_songs[2];
    uint16_t lap_song,final_lap_wait; /* B956 and EC790 */
} PortableFinishSoundRules;
typedef struct {
    uint16_t countdown,music;
    uint8_t started;
} PortableFinishAudio;
typedef struct { uint16_t stop_mask,song; } PortableFinishAudioControl;
/* B7B8 ordinary-lap cue and EC790 final-lap music script. Pause by not
 * stepping; effect32 remains latched even beneath another music request. */
typedef struct { uint16_t laps_completed,countdown; } PortableLapAudio;
typedef struct { uint16_t song; uint8_t request,release; } PortableLapAudioControl;
void portable_lap_audio_step(PortableLapAudio *state,
    const PortableFinishSoundRules *rules,uint16_t laps_completed,
    uint16_t lap_count,uint8_t finished,PortableLapAudioControl *out);

/* The one-shot finish transition survives phase5 while its script waits. */
void portable_finish_audio_step(PortableFinishAudio *state,
    const PortableFinishSoundRules *rules,uint8_t finished,uint8_t phase,
    uint16_t flags,uint8_t rank,uint8_t racer_count,uint8_t record_changed,
    PortableFinishAudioControl *out);

/* C38C actor-height pulse; C000 is the wrong-way sign, E000 retrieval.
 * The original C80 counter resets outside phase3 or at y <= -24. */
typedef struct { uint16_t counter; } PortableLakituAudio;
uint16_t portable_lakitu_audio_step(PortableLakituAudio *state,
    uint8_t scene_phase,uint16_t actor_state,int16_t actor_y);

typedef struct {
    int32_t x_q16,y_q16;
    uint8_t active;
} PortableStarSoundSource;
typedef struct { uint8_t cooldown; } PortableNearbyStarAudio;
typedef struct {
    uint16_t volume;
    uint8_t start,apply_volume;
} PortableNearbyStarControl;
/* 418BC: nearest other invincible racer, measured from the camera anchor.
 * Out-of-range/disabled listeners retain the original cooldown. */
void portable_nearby_star_audio_step(PortableNearbyStarAudio *state,
    int32_t listener_x_q16,int32_t listener_y_q16,uint32_t listener_flags,
    const PortableStarSoundSource *sources,size_t count,PortableNearbyStarControl *out);

int portable_race_audio_timeline_begin(PortableRaceAudioTimeline *timeline,
                                       uint16_t course_song,
                                       uint16_t engine_song);
size_t portable_race_audio_timeline_step(PortableRaceAudioTimeline *timeline,
    uint16_t mode_flags,PortableRaceAudioEvent events[2]);
/* EF210 pairs song114 with light pictures1/3 at remaining120/60, then
 * song115 with picture5/GO. Course music follows80 simulation turns later.
 * Prelude/repeated presentation snapshots do not move the countdown owner. */
size_t portable_race_audio_countdown_step(PortableRaceAudioTimeline *timeline,
    PortableRacePhase phase,uint16_t remaining,int paused,PortableRaceAudioEvent events[2]);


#endif
