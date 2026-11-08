#include "sound_set_asset.h"

#include <string.h>

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

int portable_sound_set_open(const PortableAssetSectionView *section, PortableSoundSet *out)
{
    if (!section || !out || section->kind != PORTABLE_ASSET_SECTION_SOUND_SET ||
        section->version < 1u || section->version > 3u ||
        !section->bytes || section->size < 24u || memcmp(section->bytes, PORTABLE_SOUND_SET_MAGIC, 8))
        return 0;
    const uint8_t *p = section->bytes;
    const uint32_t version = section->version;
    const PortableSoundSet s = {.cues = p + 24, .cue_count = le16(p + 12), .voice_count = le16(p + 14),
                                .voice_size = version == 3u ? 12u + 2u * PORTABLE_SOUND_VOICE_EVENTS
                                              : version == 2u ? 12u : 8u,
                                .engine_pitch_range = (uint16_t)le16(p + 16),
                                .engine_pitch_base = (int16_t)(uint16_t)le16(p + 18),
                                .final_lap_tempo = (uint16_t)le16(p + 20)};
    if (le16(p + 8) != 24u || le16(p + 10) != version || (version == 1u && le16(p + 18)) ||
        (version < 3u && le16(p + 20)) || le16(p + 22) || (s.final_lap_tempo && s.final_lap_tempo < 128u) ||
        s.final_lap_tempo > 512u ||
        s.cue_count > PORTABLE_SOUND_SET_ROW_LIMIT || s.voice_count > PORTABLE_SOUND_SET_ROW_LIMIT ||
        section->size != 24u + (size_t)s.cue_count * 4u + (size_t)s.voice_count * s.voice_size)
        return 0;
    for (uint32_t i = 0; version >= 2u && i < s.voice_count; ++i)
        if (le16(s.cues + (size_t)s.cue_count * 4u + i * s.voice_size + 10u))
            return 0;
    for (uint32_t i = 0; i < s.cue_count; ++i)
        for (uint32_t j = 0; j < i; ++j)
            if (le16(s.cues + i * 4u) == le16(s.cues + j * 4u))
                return 0;
    *out = s;
    out->voices = s.cues + (size_t)s.cue_count * 4u;
    return 1;
}

int portable_sound_set_find(const PortableAssetPackView *pack, uint32_t id, PortableSoundSet *out)
{
    PortableAssetSectionView section;
    return pack && portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_SOUND_SET, id, &section) ==
                       PORTABLE_ASSET_PACK_OK &&
           portable_sound_set_open(&section, out);
}

int portable_sound_set_cue(const PortableSoundSet *set, uint16_t cue, uint16_t *song)
{
    for (uint32_t i = 0; set && i < set->cue_count; ++i)
        if (le16(set->cues + i * 4u) == cue)
        {
            if (song)
                *song = (uint16_t)le16(set->cues + i * 4u + 2u);
            return 1;
        }
    return 0;
}

int portable_sound_set_voice(const PortableSoundSet *set, uint32_t driver_id, uint16_t *boost, uint16_t *struck)
{
    for (uint32_t i = 0; set && i < set->voice_count; ++i)
        if (le32(set->voices + i * set->voice_size) == driver_id)
        {
            *boost = (uint16_t)le16(set->voices + i * set->voice_size + 4u);
            *struck = (uint16_t)le16(set->voices + i * set->voice_size + 6u);
            return 1;
        }
    return 0;
}

int portable_sound_set_engine(const PortableSoundSet *set, uint32_t driver_id, uint16_t *engine)
{
    for (uint32_t i = 0; set && set->voice_size >= 12u && i < set->voice_count; ++i)
        if (le32(set->voices + i * set->voice_size) == driver_id && le16(set->voices + i * set->voice_size + 8u))
        {
            *engine = (uint16_t)le16(set->voices + i * set->voice_size + 8u);
            return 1;
        }
    return 0;
}

int portable_sound_set_voice_event(const PortableSoundSet *set, uint32_t driver_id, PortableSoundVoiceEvent event,
                                   uint16_t *song)
{
    if (!set || set->voice_size != 12u + 2u * PORTABLE_SOUND_VOICE_EVENTS || (unsigned)event >= PORTABLE_SOUND_VOICE_EVENTS)
        return 0;
    for (uint32_t i = 0; i < set->voice_count; ++i)
        if (le32(set->voices + i * set->voice_size) == driver_id)
        {
            const uint16_t value = (uint16_t)le16(set->voices + i * set->voice_size + 12u + 2u * (unsigned)event);
            if (value && song)
                *song = value;
            return value != 0u;
        }
    return 0;
}
