#include "engine_sound_asset.h"
#include <stddef.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8u);
}
static int16_t read_i16(const uint8_t *bytes)
{
    const uint16_t value = read_u16(bytes);
    return (int16_t)(value < 0x8000u ? (int32_t)value : (int32_t)value - 65536);
}
int portable_engine_sound_rules_load(const PortableAssetPackView *pack,
    PortableEngineSoundRules *out)
{
    PortableAssetSectionView section;
    if (out == NULL ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_ENGINE_SOUND_RULES,
            1u, &section) != PORTABLE_ASSET_PACK_OK ||
        section.version != 3u || section.size != 380u ||
        memcmp(section.bytes, "ENG1\x03\x00\x08\x00\x10\x00\x04\x00\x06\x00\x04\x00", 16u) != 0)
        return 0;
    PortableEngineSoundRules next = {0};
    const uint8_t *bytes = section.bytes + 16u;
    for (size_t i = 0u; i < PORTABLE_ENGINE_CLASS_COUNT; ++i, bytes += 12u) {
        next.profiles[i] = (PortableEngineRevProfile){read_i16(bytes),
            read_i16(bytes+2u),read_i16(bytes+4u),read_i16(bytes+6u),read_i16(bytes+8u)};
        if (next.profiles[i].limit <= 0 || next.profiles[i].acceleration < 0 ||
            next.profiles[i].release > 0 || next.profiles[i].skid > 0 ||
            next.profiles[i].drift > 0 || read_u16(bytes+10u) != 0u) return 0;
    }
    for (size_t driver = 0u; driver < PORTABLE_ENGINE_DRIVER_COUNT; ++driver)
        for (size_t bucket = 0u; bucket < PORTABLE_ENGINE_VOLUME_BUCKETS; ++bucket, bytes += 2u) {
            next.volumes[driver][bucket] = read_u16(bytes);
            if (next.volumes[driver][bucket] > 256u) return 0;
        }
    uint16_t *songs[] = {next.idle_songs,next.coast_songs,next.throttle_songs,next.nearby_songs};
    for (size_t table = 0u; table < 4u; ++table)
        for (size_t driver = 0u; driver < PORTABLE_ENGINE_DRIVER_COUNT; ++driver, bytes += 2u) {
            songs[table][driver] = read_u16(bytes);
            if (songs[table][driver] == 0u) return 0;
        }
    for (size_t i = 0u; i < 4u; ++i, bytes += 2u) {
        next.brake_songs[i] = read_u16(bytes);
        if (next.brake_songs[i] == 0u) return 0;
    }
    *out = next;
    return 1;
}

int portable_surface_sound_rules_load(const PortableAssetPackView *pack,
    uint32_t course_id, PortableSurfaceSoundRules *out)
{
    PortableAssetSectionView section;
    if(out==NULL||portable_asset_pack_find_section(pack,
       PORTABLE_ASSET_SECTION_SURFACE_SOUND_RULES,course_id,&section)!=PORTABLE_ASSET_PACK_OK||
       section.version!=2u||section.size!=60u||
       memcmp(section.bytes,"SRF1\x02\x00\x10\x00",8u)!=0)return 0;
    PortableSurfaceSoundRules next={0};
    for(size_t i=0u;i<16u;++i){
        next.songs[i]=read_u16(section.bytes+8u+i*2u);
        next.restart_ticks[i]=section.bytes[40u+i];
        if(next.songs[i]>=403u||next.restart_ticks[i]>15u)return 0;
    }
    next.theme=section.bytes[56];
    *out=next;
    return 1;
}

int portable_hazard_sound_rules_load(const PortableAssetPackView *pack,
    PortableHazardSoundRules *out)
{
    PortableAssetSectionView section;
    if(out==NULL||portable_asset_pack_find_section(pack,
       PORTABLE_ASSET_SECTION_HAZARD_SOUND_RULES,1u,&section)!=PORTABLE_ASSET_PACK_OK||
       section.version!=1u||section.bytes==NULL||
       !((section.size==140u&&read_u16(section.bytes+4u)==2u)||
         (section.size==204u&&read_u16(section.bytes+4u)==3u)||
         (section.size==334u&&read_u16(section.bytes+4u)==4u))||
       memcmp(section.bytes,"HAZ1",4u)!=0||
       memcmp(section.bytes+6u,"\x08\x00\x02\x00\x02\x00",6u)!=0)return 0;
    PortableHazardSoundRules next={0};
    const uint8_t *bytes=section.bytes+12u;
    for(size_t table=0u;table<2u;++table)
      for(size_t driver=0u;driver<8u;++driver)
        for(size_t listener=0u;listener<2u;++listener)
            for(size_t size=0u;size<2u;++size,bytes+=2u){
                const uint16_t song=read_u16(bytes);
                if(song==0u||song>=403u)return 0;
                if(table)next.impact_voices[driver][listener][size]=song;
                else next.voices[driver][listener][size]=song;
            }
    if(section.size>=204u)for(unsigned table=0u;table<2u;++table)
            for (unsigned driver = 0u; driver < 8u; ++driver)
                for (unsigned size = 0u; size < 2u; ++size, bytes += 2u)
                {
                    const uint16_t song = read_u16(bytes);
                    if (song == 0u || song >= 403u)
                        return 0;
                    if (table)
                        next.laugh[driver][size] = song;
                    else
                        next.taunt[driver][size] = song;
                }
    if(section.size==334u){
        PortableCharacterAudioRules *c=&next.character_cues;
        for (unsigned i = 0; i < 8u; ++i, bytes += 2u)
            c->lock_voices[i] = read_u16(bytes);
        for (unsigned table = 0; table < 2u; ++table)
            for (unsigned i = 0; i < 8u; ++i, bytes += 4u)
            {
                PortableCharacterRevCue *cue = table ? &c->launch_revs[i] : &c->menu_revs[i];
                *cue = (PortableCharacterRevCue){read_u16(bytes), read_u16(bytes + 2u)};
            }
        for (unsigned i = 0; i < 9u; ++i, bytes += 2u)
            c->menu_horns[i] = read_u16(bytes);
        for (unsigned i = 0; i < 8u; ++i, bytes += 2u)
            c->race_horns[i] = read_u16(bytes);
        for (unsigned i = 0; i < 8u; ++i, bytes += 2u)
            c->ceremony_voices[i] = read_u16(bytes);
        if(!portable_character_audio_rules_valid(c))return 0;
    }
    *out=next;
    return 1;
}

int portable_finish_sound_rules_load(const PortableAssetPackView *pack,
    PortableFinishSoundRules *out)
{
    PortableAssetSectionView section;
    if(!out||portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FINISH_SOUND_RULES,
        1u,&section)!=PORTABLE_ASSET_PACK_OK||section.version!=1u||section.size!=40u||
        memcmp(section.bytes,"FIN1\x02\x00\x04\x00",8u)!=0)return 0;
    PortableFinishSoundRules next={0};
    for(unsigned i=0u;i<4u;++i){
        const uint8_t *row=section.bytes+8u+i*6u;
        next.sequences[i]=(PortableFinishSoundSequence){read_u16(row),read_u16(row+2u),read_u16(row+4u)};
        if(next.sequences[i].cue>=403u||next.sequences[i].delay==0u||
            next.sequences[i].music==0u||next.sequences[i].music>=403u)return 0;
    }
    for(unsigned i=0u;i<2u;++i){
        next.battle_songs[i]=read_u16(section.bytes+32u+i*2u);
        if(next.battle_songs[i]==0u||next.battle_songs[i]>=403u)return 0;
    }
    next.lap_song=read_u16(section.bytes+36u);
    next.final_lap_wait=read_u16(section.bytes+38u);
    if(next.lap_song==0u||next.lap_song>=403u||next.final_lap_wait==0u)return 0;
    *out=next;return 1;
}
