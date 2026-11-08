#ifndef PORTABLE_MESH_ITEM_AUDIO_H
#define PORTABLE_MESH_ITEM_AUDIO_H

#include "mesh_items.h"
#include "mesh_course_actors.h"
#include "gp_item_hud.h"
#include "race_audio_timeline.h"

enum
{
    PORTABLE_MESH_ITEM_AUDIO_CUES = PORTABLE_MESH_ITEMS_EVENTS * 2 + 16,
    /* The caller resolves the imported song's own player (voices). */
    PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER = 255
};
typedef struct
{
    uint16_t song, volume; /* 0..256 */
    uint8_t player, require_idle;
} PortableMeshItemAudioCue;
typedef struct
{
    PortableMeshItemAudioCue cues[PORTABLE_MESH_ITEM_AUDIO_CUES];
    size_t count;
    PortableRaceMusicView music;
    uint8_t haunt_active;
    uint8_t stop_164_mask; /* Accepted human crush edges; host stops an active164 only. */
} PortableMeshItemAudioOutput;
typedef struct
{
    uint64_t ordinal;
    PortableGpItemHud hud[8];
    uint16_t held[8];
    uint32_t star_uses, lightning_uses;
    uint8_t initialized;
    uint8_t cloud_lightning_active_mask; /* Presentation-only accepted shrink edges on cloud courses. */
} PortableMeshItemAudio;
typedef struct
{
    const PortableMeshItems *world;
    const PortableMeshItemEvents *events;
    const PortableMeshRacerState *racers; /* world.inventory.count entries */
    const uint16_t (*mushroom_voices)[2]; /* eight donor rows: normal, small */
    uint64_t ordinal;                     /* Accepted world turn, never presentation frames. */
    uint16_t course_song;
    uint8_t human_mask, primary_human, donor[8], final_lap, finished;
    const PortableMeshCourseActorEvents *actor_events; /* Optional accepted source contacts. */
    const PortableMeshCourseActorsConfig *source_actor_config;
    const PortableMeshCourseActors *source_actors;
    const uint16_t (*impact_voices)[2][2]; /* Optional donor/rival/normal-or-small HAZ1 table. */
} PortableMeshItemAudioView;

/* Zero-init/reset on race/retry. Only fully resolved worlds (prepared=0) are
 * accepted, including initialization. Same ordinal is silent; older ordinals reject.
 * No SDL, playback, simulation writes or clocks. Failure preserves both outputs.
 * Actor cues use nearest-human XYZ attenuation: full within4m, silent at64m;
 * this SI presentation policy deliberately does not reinterpret source pixels.
 * Thwomp landings use live human listeners only and suppress volumes<=24.
 * Rock cues require live humans and volume>63;386 additionally tests ground
 * depth in their accepted chase frame and marks its audio player idle-only. */
int portable_mesh_item_audio_step(PortableMeshItemAudio *, const PortableMeshItemAudioView *,
                                  PortableMeshItemAudioOutput *, char *error, size_t capacity);
#endif
