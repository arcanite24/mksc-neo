#ifndef PORTABLE_ENGINE_SOUND_ASSET_H
#define PORTABLE_ENGINE_SOUND_ASSET_H
#include "asset_pack.h"
#include "driver_engine_sound.h"
#include "race_audio_timeline.h"
/* ENG1, section68/id1/version3. Older data requires reimport. */
int portable_engine_sound_rules_load(const PortableAssetPackView *pack,
    PortableEngineSoundRules *out);
int portable_surface_sound_rules_load(const PortableAssetPackView *pack,
    uint32_t course_id, PortableSurfaceSoundRules *out);

int portable_hazard_sound_rules_load(const PortableAssetPackView *pack,
    PortableHazardSoundRules *out);

int portable_finish_sound_rules_load(const PortableAssetPackView *pack,
    PortableFinishSoundRules *out);

#endif
