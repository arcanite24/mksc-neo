#include "asset_pack.h"
#include "ambient_asset.h"
#include "gp_item_inventory.h"
#include "recovered_pack.h"
#include "presentation_pack.h"
#include "content_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed line %d\n",__LINE__); return 1; } } while (0)

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}

static int test_finish_lakitu_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);CHECK(sections!=NULL);
    uint32_t slot=pack->section_count;
    for(uint32_t i=0u;i<pack->section_count;++i){
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        if(sections[i].kind==PORTABLE_ASSET_SECTION_RECOVERY_MOTION&&sections[i].content_id==1u)slot=i;
    }
    /* Invented two-frame schedule has a complete, independently authored phase sum. */
    uint8_t payload[184]={0x4c,0x4b,0x43,0x31,16,0,0,0};
    memcpy(payload+136u,"FLK1",4u);payload[140]=2u;payload[142]=20u;
    payload[144]=3u;payload[146]=5u;
    payload[152]=21u;payload[154]=20u;payload[156]=28u;payload[158]=8u;
    sections[slot]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_RECOVERY_MOTION,
        .content_id=1u,.version=2u,.bytes=payload,.size=160u};
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;
    if(slot==pack->section_count)++layered.section_count;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->finish_lakitu_rules==&storage->finish_lakitu_storage);
    CHECK(race->finish_lakitu_rules->row_count==2u&&race->finish_lakitu_rules->bob_ticks==3u);
    CHECK(race->finish_lakitu_rules->frames[1].art_bank==28u&&race->finish_lakitu_rules->frames[1].duration==8u);
    CHECK(race->item_art_intro_rules==NULL);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {136u,0u},{140u,33u},{142u,19u},{148u,1u},{152u,20u},{153u,1u},{154u,19u},{156u,29u},{158u,0u}};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        uint8_t old=payload[invalid[i].offset];payload[invalid[i].offset]=invalid[i].value;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[invalid[i].offset]=old;
    }
    --sections[slot].size;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    memcpy(payload+160u,"IAC1",4u);payload[164u]=8u;
    for(unsigned i=0u;i<8u;++i)payload[168u+i*2u]=(uint8_t)(3u+i*2u);
    sections[slot].size=sizeof payload;sections[slot].version=3u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->item_art_intro_rules==&storage->item_art_intro_storage);
    for(unsigned i=0u;i<8u;++i)CHECK(race->item_art_intro_rules->waits[i]==3u+i*2u);
    static const struct {size_t offset;uint8_t value;} bad_intro[]={
        {160u,0u},{164u,7u},{166u,1u},{168u,0u},{169u,33u}};
    for(unsigned i=0u;i<sizeof bad_intro/sizeof bad_intro[0];++i){
        const uint8_t old=payload[bad_intro[i].offset];payload[bad_intro[i].offset]=bad_intro[i].value;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[bad_intro[i].offset]=old;
    }
    --sections[slot].size;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    sections[slot].size=136u;sections[slot].version=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->finish_lakitu_rules==NULL&&race->recovery_bob_offsets==payload+8u);
    CHECK(race->item_art_intro_rules==NULL);
    free(sections);
    return 0;
}

static int test_time_trial_record_targets(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->time_trial_record_defaults==NULL);
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    uint8_t payload[32]={0x54,0x54,0x52,0x31,6,0,0,0};
    for(unsigned row=0u;row<6u;++row){payload[8u+row*4u]=(uint8_t)row;payload[10u+row*4u]=(uint8_t)(row<5u?100u+row*10u:10u);}
    PortableAssetSectionView *asset=&sections[pack->section_count];
    *asset=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_TIME_TRIAL_RECORD_DEFAULTS,
        .content_id=selection->course.id,.version=1u,.bytes=payload,.size=sizeof payload};
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;++layered.section_count;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->time_trial_record_defaults==&storage->time_trial_record_defaults);
    CHECK(race->time_trial_record_defaults->rows[0].centiseconds==100u);
    CHECK(race->time_trial_record_defaults->rows[5].centiseconds==10u);
    CHECK(race->time_trial_record_defaults->rows[5].character_class==5u);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,5u},{8u,8u},{9u,1u},{10u,0u},{14u,99u},{31u,255u}};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        uint8_t old=payload[invalid[i].offset];payload[invalid[i].offset]=invalid[i].value;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[invalid[i].offset]=old;
    }
    --asset->size;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ++asset->size;asset->version=2u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    asset->version=1u;++asset->content_id;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->time_trial_record_defaults==NULL);
    free(sections);
    return 0;
}

static int test_driver_view_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->view_rules==NULL);
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t index=0u;index<pack->section_count;++index)
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
    uint8_t payload[38]={0x44,0x56,0x52,0x31,15,0,0,0};
    for(unsigned index=0u;index<15u;++index){payload[8u+index]=(uint8_t)index;payload[23u+index]=(uint8_t)(14u-index);}
    PortableAssetSectionView *asset=&sections[pack->section_count];
    *asset=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_DRIVER_VIEW_RULES,
        .content_id=1u,.version=1u,.bytes=payload,.size=sizeof payload};
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->view_rules==&storage->view_rules);
    for(unsigned index=0u;index<15u;++index)
        CHECK(race->view_rules->response_limit[index]==index && race->view_rules->speed_limit[index]==14u-index);
    static const struct {size_t offset;uint8_t value;} invalid[]={{0u,0u},{4u,14u},{8u,19u},{37u,19u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t old=payload[invalid[index].offset];payload[invalid[index].offset]=invalid[index].value;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[invalid[index].offset]=old;
    }
    --asset->size;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ++asset->size;asset->version=2u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->view_rules==NULL);
    return 0;
}

static void ambient_write32(uint8_t *p,uint32_t value)
{p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);p[2]=(uint8_t)(value>>16u);p[3]=(uint8_t)(value>>24u);}

static int test_impact_effect_assets(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,PortablePresentationPackStorage *art)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->impact_effect_rules);
    uint8_t payload[20]={0};ambient_write32(payload,0x31584649u);ambient_write32(payload+4u,6u);
    for(unsigned i=0u;i<6u;++i)payload[8u+i*2u]=(uint8_t)((i+4u)*2u);
    PortableImpactEffectRules decoded;
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_IMPACT_EFFECT_RULES,.content_id=1u,
        .version=1u,.bytes=payload,.size=sizeof payload};
    CHECK(portable_impact_effect_asset_open(&section,&decoded) && decoded.script_ticks[5]==18u);
    static const struct {size_t offset;uint8_t value;} invalid[]={{0u,0u},{4u,5u},{8u,0u},{19u,33u}};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        const uint8_t old=payload[invalid[i].offset];payload[invalid[i].offset]=invalid[i].value;
        CHECK(!portable_impact_effect_asset_open(&section,&decoded));payload[invalid[i].offset]=old;
    }
    --section.size;CHECK(!portable_impact_effect_asset_open(&section,&decoded));++section.size;
    section.version=2u;CHECK(!portable_impact_effect_asset_open(&section,&decoded));section.version=1u;
    uint8_t images[6][32u+9u*8u+0x2000u+160u+18u]={0};
    PortableAssetSectionView *sections=calloc(pack->section_count+7u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=section;
    for(unsigned group=0u;group<6u;++group){
        const unsigned count=group+4u;
        const uint32_t header[8]={0x31435846u,0x2000u,80u,count,0x100u,3u,count*2u,0u};
        for(unsigned i=0u;i<8u;++i)ambient_write32(images[group]+i*4u,header[i]);
        for(unsigned i=0u;i<count;++i){
            uint8_t *frame=images[group]+32u+i*8u;
            ambient_write32(frame,i*2u);frame[4]=2u;frame[6]=1u;
        }
        sections[pack->section_count+1u+group]=(PortableAssetSectionView){
            .kind=PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,.content_id=17u+group,.version=1u,
            .bytes=images[group],.size=32u+count*8u+0x2000u+160u+count*2u};
    }
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=7u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0 &&
        race->impact_effect_rules==&storage->impact_effect_storage &&
        race->dependencies.impact_effect_rules==race->impact_effect_rules && race->impact_effect_rules->script_ticks[2]==12u);
    PortablePresentationPackView view;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==0 &&
        view.impact_effect_rules==race->impact_effect_rules && view.impact_effect_art[0].frame_count==4u &&
        view.impact_effect_art[5].frame_count==9u);
    payload[8]=9u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0);
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    payload[8]=8u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0);
    --sections[pack->section_count+6u].size;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->impact_effect_rules);
    CHECK(portable_presentation_pack_open(pack,selection,storage,race,art,&view)==0 && !view.impact_effect_art[0].frame_count);
    return 0;
}

static int test_coin_effect_assets(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,PortablePresentationPackStorage *art)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->coin_effect_rules);
    uint8_t payload[104]={0};
    ambient_write32(payload,0x31584643u);ambient_write32(payload+4u,9u);
    ambient_write32(payload+8u,3u);ambient_write32(payload+12u,4u);
    ambient_write32(payload+16u,UINT32_C(0xfffffffc));ambient_write32(payload+52u,3u*65536u);
    payload[88]=0u;payload[89]=1u;payload[90]=153u;payload[92]=102u;
    payload[94]=27u;payload[96]=64u;payload[98]=10u;payload[100]=2u;
    PortableCoinEffectRules decoded;
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_COIN_EFFECT_RULES,.content_id=1u,
        .version=1u,.bytes=payload,.size=sizeof payload};
    CHECK(portable_coin_effect_asset_open(&section,&decoded) && decoded.horizontal_step[0]==-4 &&
        decoded.upward_speed_q16[0]==3*65536 && decoded.bounce_volumes[0]==256u && decoded.script_ticks[1]==64u);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,8u},{8u,2u},{12u,3u},{16u,0xbfu},{55u,1u},{89u,2u},{94u,0u},{97u,33u},{102u,1u}};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        const uint8_t old=payload[invalid[i].offset];payload[invalid[i].offset]=invalid[i].value;
        CHECK(!portable_coin_effect_asset_open(&section,&decoded));payload[invalid[i].offset]=old;
    }
    --section.size;CHECK(!portable_coin_effect_asset_open(&section,&decoded));++section.size;
    section.version=2u;CHECK(!portable_coin_effect_asset_open(&section,&decoded));section.version=1u;
    uint8_t images[3][32u+32u*8u+0x2000u+160u+64u]={0};
    PortableAssetSectionView *sections=calloc(pack->section_count+4u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=section;
    for(unsigned group=0u;group<3u;++group){
        const unsigned count=group==0u?32u:group==1u?5u:1u;
        const uint32_t header[8]={0x31435846u,0x2000u,80u,count,0x100u,3u,count*2u,0u};
        for(unsigned i=0u;i<8u;++i)ambient_write32(images[group]+i*4u,header[i]);
        for(unsigned i=0u;i<count;++i){
            uint8_t *frame=images[group]+32u+i*8u;
            ambient_write32(frame,i*2u);frame[4]=2u;frame[6]=1u;
        }
        sections[pack->section_count+1u+group]=(PortableAssetSectionView){
            .kind=PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,.content_id=14u+group,.version=1u,
            .bytes=images[group],.size=32u+count*8u+0x2000u+160u+count*2u};
    }
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=4u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0 &&
        race->coin_effect_rules==&storage->coin_effect_storage && race->coin_effect_rules->horizontal_step[0]==-4);
    PortablePresentationPackView view;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==0 &&
        view.coin_effect_rules==race->coin_effect_rules && view.coin_effect_art[0].frame_count==32u &&
        view.coin_effect_art[1].frame_count==5u && view.coin_effect_art[2].cell_bytes==2u);
    images[0][32u+31u*8u+6u]=2u; /* art and simulation must agree on authored lifespan */
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    images[0][32u+31u*8u+6u]=1u;
    ambient_write32(images[0]+12u,33u);
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ambient_write32(images[0]+12u,32u);
    images[2][32u+8u+0x2000u+160u]=1u; /* cell count no longer fits its two-byte frame */
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->coin_effect_rules);
    CHECK(portable_presentation_pack_open(pack,selection,storage,race,art,&view)==0 && !view.coin_effect_art[0].frame_count);
    return 0;
}

static int test_modern_drift_profile(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    const PortableRaceCompatibilityId original=race->rules.compatibility;
    CHECK(race->modern_drift_rules==NULL&&race->dependencies.modern_drift_rules==NULL);
    uint8_t bytes[48]={0};ambient_write32(bytes,0x3152444du);
    ambient_write32(bytes+4u,0x12345678u);ambient_write32(bytes+8u,7u);
    for(unsigned tier=0u;tier<3u;++tier){
        bytes[12u+tier*2u]=(uint8_t)(11u+tier*19u);
        bytes[18u+tier*2u]=(uint8_t)(9u+tier*8u);
        const unsigned cap=270u+tier*21u;
        bytes[24u+tier*2u]=(uint8_t)cap;bytes[25u+tier*2u]=(uint8_t)(cap>>8u);
        ambient_write32(bytes+32u+tier*4u,(tier+2u)*65536u);
    }
    bytes[30u]=123u;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);
    CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=(PortableAssetSectionView){
        .kind=PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES,.content_id=1u,.version=1u,
        .flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,.bytes=bytes,.size=sizeof bytes};
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;++layered.section_count;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->modern_drift_rules==NULL&&memcmp(&original,&race->rules.compatibility,sizeof original)==0);
    layered.driving_profile=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->modern_drift_rules==&storage->modern_drift_rules&&
          race->dependencies.modern_drift_rules==race->modern_drift_rules);
    CHECK(race->rules.compatibility.rules_id==0x12345678u&&race->rules.compatibility.rules_revision==7u);
    CHECK(race->rules.compatibility.handling_profile_id==2u&&
          race->rules.compatibility.handling_revision==original.handling_revision&&
          race->rules.compatibility.course_id==original.course_id&&
          memcmp(race->rules.compatibility.deterministic_digest,pack->deterministic_digest,32u)!=0);
    CHECK(race->modern_drift_rules->charge_ticks[2]==49u&&
          race->modern_drift_rules->acceleration_q16[1]==3*65536);
    /* Profile selection stays incompatible when author IDs coincide and
     * both views borrow exactly the same deterministic section digest. */
    ambient_write32(bytes+4u,original.rules_id);ambient_write32(bytes+8u,original.rules_revision);
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->rules.compatibility.rules_id==original.rules_id&&
          race->rules.compatibility.rules_revision==original.rules_revision&&
          memcmp(race->rules.compatibility.deterministic_digest,original.deterministic_digest,32u)!=0);
    ambient_write32(bytes+4u,0x12345678u);ambient_write32(bytes+8u,7u);
    bytes[44u]=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    layered.driving_profile=0u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK&&
          race->modern_drift_rules==NULL&&memcmp(&original,&race->rules.compatibility,sizeof original)==0);
    bytes[44u]=0u;layered.driving_profile=1u;--layered.section_count;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_MISSING_SECTION);
    layered.driving_profile=3u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_UNSUPPORTED_CONTENT);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    return 0;
}

static int test_surface_trail_assets(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,PortablePresentationPackStorage *art)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->surface_trail_rules);
    uint8_t rules[280]={0};ambient_write32(rules,0x314c5254u);ambient_write32(rules+4u,256u);
    memset(rules+8u,1,256u);rules[8u+0x54u]=3u; /* authored override, not a stock lookup */
    for(unsigned i=0u;i<5u;++i){rules[264u+i*2u]=7u;rules[265u+i*2u]=21u;}
    rules[274]=0x60u;rules[275]=0x61u;rules[276]=0x62u;
    PortableSurfaceTrailRules decoded;
    PortableAssetSectionView rule={.kind=PORTABLE_ASSET_SECTION_SURFACE_TRAIL_RULES,.content_id=1u,
        .version=1u,.bytes=rules,.size=sizeof rules};
    CHECK(portable_surface_trail_asset_open(&rule,&decoded) && decoded.material_kind[0x54]==3u &&
        decoded.frame_cutoffs[4][0]==7u && decoded.spray_material==0x62u);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,255u},{8u,0u},{263u,6u},{264u,0u},{265u,7u},{273u,30u},{277u,1u}};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        const uint8_t old=rules[invalid[i].offset];rules[invalid[i].offset]=invalid[i].value;
        CHECK(!portable_surface_trail_asset_open(&rule,&decoded));rules[invalid[i].offset]=old;
    }
    --rule.size;CHECK(!portable_surface_trail_asset_open(&rule,&decoded));++rule.size;
    rule.version=2u;CHECK(!portable_surface_trail_asset_open(&rule,&decoded));rule.version=1u;
    uint8_t images[5][32u+24u+0x2000u+160u+24u]={0};
    PortableAssetSectionView *sections=calloc(pack->section_count+6u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=rule;
    for(unsigned group=0u;group<5u;++group){
        const unsigned count=group==4u?1u:3u;
        const uint32_t header[8]={0x31435846u,0x2000u,80u,count,0x100u,3u,count*8u,0u};
        for(unsigned i=0u;i<8u;++i)ambient_write32(images[group]+i*4u,header[i]);
        for(unsigned i=0u;i<count;++i){
            uint8_t *frame=images[group]+32u+i*8u;
            ambient_write32(frame,i*8u);frame[4]=8u;frame[6]=1u;
            images[group][32u+count*8u+0x2000u+160u+i*8u]=1u;
        }
        sections[pack->section_count+1u+group]=(PortableAssetSectionView){
            .kind=PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,.content_id=9u+group,.version=1u,
            .bytes=images[group],.size=32u+count*8u+0x2000u+160u+count*8u};
    }
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=6u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0 &&
        race->surface_trail_rules==&storage->trail_storage && race->surface_trail_rules->material_kind[0x54]==3u);
    PortablePresentationPackView view;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==0 &&
        view.surface_trail_rules==race->surface_trail_rules && view.surface_trail_art[0].frame_count==3u &&
        view.surface_trail_art[4].frame_count==1u && view.surface_trail_art[3].frames[2].offset==16u);
    --sections[pack->section_count+1u].size;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ++sections[pack->section_count+1u].size;rules[263]=0u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->surface_trail_rules);
    CHECK(portable_presentation_pack_open(pack,selection,storage,race,art,&view)==0 &&
        !view.surface_trail_art[0].frame_count && !view.surface_trail_art[4].frame_count);
    return 0;
}

static void recovery_fx_art(uint8_t *bytes,unsigned drip)
{
    const uint32_t header[8]={0x31435846u,0x2000u,80u,drip?1u:3u,0x100u,3u,drip?8u:174u,0u};
    for(unsigned i=0u;i<8u;++i)ambient_write32(bytes+i*4u,header[i]);
    static const uint8_t sizes[3]={26u,68u,80u},counts[3]={4u,11u,13u};
    unsigned offset=0u;
    uint8_t *cells=bytes+32u+header[3]*8u+0x2000u+160u;
    for(unsigned i=0u;i<header[3];++i){
        uint8_t *frame=bytes+32u+i*8u;
        ambient_write32(frame,offset);frame[4]=drip?8u:sizes[i];frame[6]=drip?8u:(uint8_t)(4u-i);
        cells[offset]=drip?1u:counts[i];offset+=frame[4];
    }
}
static int test_recovery_fx_assets(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,PortablePresentationPackStorage *art)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0);
    CHECK(race->recovery_fx_rules==NULL && !race->icy_recovery);
    uint8_t rules[24]={0},surface[60]={0};
    ambient_write32(rules,0x31584652u);ambient_write32(rules+4u,3u);
    rules[8]=17u;rules[9]=23u;rules[10]=31u;rules[11]=0xf0u;rules[19]=16u;
    ambient_write32(surface,0x31465253u);surface[4]=2u;surface[6]=16u;surface[56]=8u;
    PortableRecoveryFxRules decoded;
    PortableAssetSectionView rule={.kind=PORTABLE_ASSET_SECTION_RECOVERY_FX_RULES,.content_id=1u,
        .version=1u,.bytes=rules,.size=sizeof rules};
    CHECK(portable_recovery_fx_asset_open(&rule,&decoded) && decoded.freeze_durations[1]==23u &&
        decoded.drip_dy[0]==-16 && decoded.drip_dy[8]==16);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,2u},{8u,0u},{10u,128u},{11u,0xefu},{19u,17u},{20u,1u}};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        const uint8_t old=rules[invalid[i].offset];rules[invalid[i].offset]=invalid[i].value;
        CHECK(!portable_recovery_fx_asset_open(&rule,&decoded));rules[invalid[i].offset]=old;
    }
    --rule.size;CHECK(!portable_recovery_fx_asset_open(&rule,&decoded));++rule.size;
    rule.version=2u;CHECK(!portable_recovery_fx_asset_open(&rule,&decoded));rule.version=1u;
    uint8_t thaw[32u+24u+0x2000u+160u+174u]={0},drip[32u+8u+0x2000u+160u+8u]={0};
    recovery_fx_art(thaw,0u);recovery_fx_art(drip,1u);
    PortableAssetSectionView *sections=calloc(pack->section_count+4u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=rule;
    sections[pack->section_count+1u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_SURFACE_SOUND_RULES,
        .content_id=selection->course.id,.version=2u,.bytes=surface,.size=sizeof surface};
    for(unsigned i=0u;i<2u;++i)sections[pack->section_count+2u+i]=(PortableAssetSectionView){
        .kind=PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,.content_id=7u+i,.version=1u,
        .bytes=i?drip:thaw,.size=i?sizeof drip:sizeof thaw};
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=4u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0 && race->icy_recovery &&
        race->recovery_fx_rules==&storage->recovery_fx_storage && race->recovery_fx_rules->freeze_durations[2]==31u);
    PortablePresentationPackView view;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==0 &&
        view.recovery_fx_rules==race->recovery_fx_rules && view.recovery_thaw.frame_count==3u &&
        view.recovery_thaw.cell_bytes==174u && view.recovery_thaw.frames[2].duration==2u &&
        view.recovery_drip.frame_count==1u && view.recovery_drip.frames[0].duration==8u);
    surface[56]=9u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0 && !race->icy_recovery);
    surface[57]=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    surface[57]=0u;surface[4]=1u;sections[pack->section_count+1u].version=1u;
    sections[pack->section_count+1u].size=56u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==0 && !race->icy_recovery);
    --sections[pack->section_count+2u].size;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==0 && !race->recovery_fx_rules);
    CHECK(portable_presentation_pack_open(pack,selection,storage,race,art,&view)==0 &&
        !view.recovery_thaw.frame_count && !view.recovery_drip.frame_count);
    return 0;
}

static int test_weather_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->weather_profile==NULL);
    uint8_t payload[48u+3u*100u]={0};
    ambient_write32(payload,0x31485457u);ambient_write32(payload+4u,1u);
    payload[8]=1u;payload[9]=7u;
    for(unsigned i=0u;i<8u;++i){payload[12u+i]=(uint8_t)(i%4u);payload[20u+i]=(uint8_t)((i+1u)%4u);}
    for(unsigned art=0u;art<3u;++art){
        ambient_write32(payload+28u+art*4u,100u);
        uint8_t *image=payload+48u+art*100u;
        memcpy(image,"MKSCIMG2",8u);image[8]=32u;image[10]=2u;image[12]=8u;image[14]=8u;
        image[16]=0xfeu;image[17]=0xffu;image[20]=2u;image[24]=32u;image[28]=100u;
        memset(image+32u,1u,64u);image[98]=(uint8_t)(art+1u);
    }
    PortableAssetSectionView asset={.kind=PORTABLE_ASSET_SECTION_WEATHER_PROFILE,
        .content_id=selection->course.id,.version=1u,.bytes=payload,.size=sizeof payload};
    PortableWeatherAssetView weather;
    CHECK(portable_weather_asset_open(&asset,&weather));
    CHECK(weather.rules.enabled && weather.rules.count==7u && weather.rules.backward[7]==3u &&
        weather.rules.forward[7]==0u && weather.images[2].anchor_x==-2 && weather.images[2].palette_count==2u);
    CHECK(portable_native_image_pixel(&weather.images[0],0u,0u)!=
        portable_native_image_pixel(&weather.images[2],0u,0u));
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,2u},{8u,2u},{9u,0u},{9u,13u},{10u,1u},{12u,4u},{27u,4u},
        {28u,99u},{40u,1u},{44u,1u},{48u+12u,16u},{48u+16u,0xf7u},{48u+32u,2u}};
    for(size_t i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        const uint8_t old=payload[invalid[i].offset];payload[invalid[i].offset]=invalid[i].value;
        CHECK(!portable_weather_asset_open(&asset,&weather));payload[invalid[i].offset]=old;
    }
    --asset.size;CHECK(!portable_weather_asset_open(&asset,&weather));++asset.size;
    asset.version=2u;CHECK(!portable_weather_asset_open(&asset,&weather));asset.version=1u;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=asset;
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->weather_profile==&storage->weather_storage && race->weather_profile->rules.count==7u);
    payload[12]=4u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    payload[12]=0u;
    payload[8]=payload[9]=0u;memset(payload+28u,0,12u);asset.size=48u;
    CHECK(portable_weather_asset_open(&asset,&weather) && !weather.rules.enabled && !weather.rules.count);
    asset.size=sizeof payload;CHECK(!portable_weather_asset_open(&asset,&weather));
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->weather_profile==NULL);
    return 0;
}

static int test_finish_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->finish_rules==NULL);
    uint8_t payload[116]={0};
    ambient_write32(payload,0x314e4946u);ambient_write32(payload+4u,8u);
    payload[9]=2u;payload[11]=0x40u;payload[12]=9u;payload[14]=17u;payload[16]=5u;
    for(unsigned driver=0u;driver<8u;++driver){
        uint8_t *table=payload+20u+driver*12u;
        ambient_write32(table,2u);
        table[4]=24u;table[5]=3u;table[6]=1u;
        table[8]=(uint8_t)(26u+driver);table[9]=(uint8_t)(driver+1u);table[10]=driver==7u?255u:1u;
    }
    PortableFinishAssetStorage finish;
    PortableAssetSectionView asset={.kind=PORTABLE_ASSET_SECTION_FINISH_CAMERA_RULES,
        .content_id=1u,.version=1u,.bytes=payload,.size=sizeof payload};
    CHECK(portable_finish_asset_open(&asset,&finish));
    CHECK(finish.rules.camera.orbit_step==512u && finish.rules.camera.orbit_end==16384u &&
          finish.rules.camera.front_hold_ticks==9u && finish.rules.camera.final_hold_ticks==17u &&
          finish.rules.camera.eligible_rank_max==5u && finish.rules.schedules[7].count==2u &&
          finish.rules.schedules[7].rows==finish.rows[7] && finish.rows[7][1].next==255u);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,7u},{9u,0u},{9u,0x81u},{11u,0u},{11u,0x81u},
        {12u,0u},{14u,0u},{16u,8u},{17u,1u},{20u,0u},{20u,33u},
        {24u,34u},{25u,0u},{25u,128u},{26u,2u},{27u,1u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t old=payload[invalid[index].offset];payload[invalid[index].offset]=invalid[index].value;
        CHECK(!portable_finish_asset_open(&asset,&finish));payload[invalid[index].offset]=old;
    }
    --asset.size;CHECK(!portable_finish_asset_open(&asset,&finish));++asset.size;
    asset.version=2u;CHECK(!portable_finish_asset_open(&asset,&finish));asset.version=1u;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t index=0u;index<pack->section_count;++index)
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=asset;
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->finish_rules==&storage->finish_storage.rules && race->finish_rules->schedules[3].rows[1].frame==29u);
    payload[26]=2u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->finish_rules==NULL);
    return 0;
}

static int test_ambient_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->ambient_rules==NULL && race->ambient_seed==NULL);
    uint8_t payload[356]={0};
    static const uint32_t header[8]={0x31424d41u,1u,1u,1u,1u,1u,260u,0u};
    for(unsigned index=0u;index<8u;++index)ambient_write32(payload+index*4u,header[index]);
    /* Invented timer lane: one road patch and one startup object patch. */
    payload[32]=1u;payload[38]=1u;
    payload[64]=2u;payload[68]=1u;
    payload[74]=1u;payload[76]=1u;ambient_write32(payload+80u,256u);
    payload[84]=2u;payload[86]=103u;payload[88]=1u;ambient_write32(payload+92u,258u);
    for(unsigned color=0u;color<128u;++color){payload[96u+color*2u]=(uint8_t)color;payload[97u+color*2u]=1u;}
    payload[352]=0x34u;payload[353]=0x12u;payload[354]=0x78u;payload[355]=0x56u;
    PortableAssetSectionView asset={.kind=PORTABLE_ASSET_SECTION_AMBIENT_RULES,
        .content_id=selection->course.id,.version=1u,.bytes=payload,.size=sizeof payload};
    PortableAmbientAssetStorage *ambient=malloc(sizeof *ambient);CHECK(ambient!=NULL);
    CHECK(portable_ambient_asset_open(&asset,ambient));
    CHECK(ambient->rules.lane_count==1u && ambient->rules.frames[0].duration==2u &&
          ambient->rules.patches[0].offset==1u && ambient->rules.init_patches[0].offset==103u &&
          ambient->seed.object[127]==383u);
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,2u},{8u,17u},{13u,2u},{17u,4u},
        {20u,33u},{28u,5u},{32u,2u},{33u,2u},{34u,6u},{35u,2u},
        {38u,0u},{42u,1u},{48u,2u},{64u,0u},{68u,4u},{69u,1u},{70u,1u},
        {72u,7u},{73u,1u},{74u,64u},{76u,0u},{78u,1u},{81u,2u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t old=payload[invalid[index].offset];payload[invalid[index].offset]=invalid[index].value;
        CHECK(!portable_ambient_asset_open(&asset,ambient));payload[invalid[index].offset]=old;
    }
    --asset.size;CHECK(!portable_ambient_asset_open(&asset,ambient));++asset.size;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t index=0u;index<pack->section_count;++index)
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
    sections[pack->section_count]=asset;
    PortableAssetPackView layered=*pack;layered.resolved_sections=sections;layered.section_count+=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->ambient_rules==&storage->ambient_storage.rules && race->ambient_seed==&storage->ambient_storage.seed);
    PortableAssetSectionView road,sky;
    CHECK(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,selection->course.road,&road)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_SKY_GRAPHICS,selection->course.sky,&sky)==PORTABLE_ASSET_PACK_OK);
    const uint8_t *road_palette=road.bytes+24u+read32(road.bytes+4u);
    const uint8_t *sky_palette=sky.bytes+64u+read32(sky.bytes+4u);
    CHECK(race->ambient_seed->road[0]==((uint16_t)road_palette[0]|(uint16_t)((uint16_t)road_palette[1]<<8u)) &&
          race->ambient_seed->sky[0]==((uint16_t)sky_palette[0]|(uint16_t)((uint16_t)sky_palette[1]<<8u)));
    /* Globally valid image patches still must fit the composed sky's actual
     * tile/map spans. Check frame and startup patches, including dormant art. */
    static const unsigned image_size_fields[4]={4u,12u,16u,20u};
    static const uint32_t global_image_bounds[4]={0x4000u,0x1000u,0x1000u,0x1000u};
    for(unsigned target=0u;target<4u;++target){
        const uint32_t bytes=read32(sky.bytes+image_size_fields[target]);
        CHECK(bytes>0u && bytes<=global_image_bounds[target]);
        if(bytes==global_image_bounds[target])continue;
        for(unsigned initial=0u;initial<2u;++initial){
            uint8_t *patch=payload+(initial?84u:72u);
            const uint8_t old_target=patch[0],old_lo=patch[2],old_hi=patch[3];
            patch[0]=(uint8_t)(PORTABLE_AMBIENT_SKY_TILES+target);
            patch[2]=(uint8_t)bytes;patch[3]=(uint8_t)(bytes>>8u);
            CHECK(portable_ambient_asset_open(&asset,ambient));
            CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
            patch[2]=(uint8_t)(bytes-1u);patch[3]=(uint8_t)((bytes-1u)>>8u);
            CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
            patch[0]=old_target;patch[2]=old_lo;patch[3]=old_hi;
        }
    }
    /* A composed palette replacement, rather than AMB1's source seed, wins. */
    uint8_t *road_copy=malloc(road.size);CHECK(road_copy!=NULL);memcpy(road_copy,road.bytes,road.size);
    const size_t palette_offset=24u+read32(road.bytes+4u);road_copy[palette_offset]=0x45u;road_copy[palette_offset+1u]=0x23u;
    for(uint32_t index=0u;index<pack->section_count;++index)
        if(sections[index].kind==road.kind && sections[index].content_id==road.content_id)sections[index].bytes=road_copy;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->ambient_seed->road[0]==0x2345u);
    payload[34]=6u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(road_copy);free(sections);free(ambient);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    return 0;
}

static int test_sky_motion_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,PortablePresentationPackStorage *art)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->sky_motion_rules==NULL);
    PortableAssetPackView layered=*pack;
    PortableAssetSectionView *sections=calloc(pack->section_count,sizeof *sections);
    CHECK(sections!=NULL);
    PortableAssetSectionView *sky=NULL;
    for(uint32_t index=0u;index<pack->section_count;++index){
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
        if(sections[index].kind==PORTABLE_ASSET_SECTION_SKY_GRAPHICS &&
           sections[index].content_id==selection->course.sky)sky=&sections[index];
    }
    CHECK(sky!=NULL && sky->version==1u && sky->size>=64u);
    uint8_t *payload=calloc(sky->size+40u,1u);CHECK(payload!=NULL);
    memcpy(payload,sky->bytes,sky->size);payload[60]=40u;
    uint8_t *motion=payload+sky->size;
    memcpy(motion,"SMR1",4u);motion[4]=1u;
    motion[8]=1u;motion[9]=7u;motion[13]=4u;motion[14]=5u;motion[15]=6u;
    motion[16]=17u;motion[22]=23u;motion[28]=0xffu;motion[29]=0xffu;
    motion[34]=2u;motion[36]=2u;motion[37]=0u;motion[38]=1u;motion[39]=2u;
    sky->bytes=payload;sky->size+=40u;sky->version=2u;
    layered.resolved_sections=sections;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->sky_motion_rules==&storage->sky_motion_rules &&
          race->sky_motion_rules->controller==1u && race->sky_motion_rules->x_bias[0]==17 &&
          race->sky_motion_rules->road_mode==2u);
    PortablePresentationPackView presentation;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&presentation)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(presentation.sky.motion_rules==&art->sky_motion_rules &&
          presentation.sky.motion_rules->yaw_shift[2]==6u &&
          presentation.sky.motion_rules->layer_source[0]==2u && presentation.sky.motion_rules->road_mode==2u);
    motion[8]=8u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&presentation)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    motion[8]=1u;sky->version=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(payload);free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(portable_presentation_pack_open(pack,selection,storage,race,art,&presentation)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->sky_motion_rules==NULL && presentation.sky.motion_rules==NULL);
    return 0;
}

static int test_driver_art_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->art_motion_rules==NULL && race->dependencies.art_motion_rules==NULL);
    PortableAssetPackView layered=*pack;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);
    CHECK(sections!=NULL);
    for(uint32_t index=0u;index<pack->section_count;++index)
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
    uint8_t payload[56]={0x44,0x41,0x52,0x31,16,0,0,0};
    for(unsigned index=0u;index<16u;++index){
        payload[8u+index*2u]=(uint8_t)(index*7u);
        payload[40u+index]=(uint8_t)(index%3u==0u?3u:7u);
    }
    PortableAssetSectionView *asset=&sections[pack->section_count];
    *asset=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_DRIVER_ART_RULES,
        .content_id=1u,.version=1u,.size=sizeof payload,.bytes=payload};
    layered.resolved_sections=sections;layered.section_count=pack->section_count+1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->art_motion_rules==&storage->art_motion_rules &&
          race->dependencies.art_motion_rules==race->art_motion_rules);
    for(unsigned index=0u;index<16u;++index){
        CHECK(race->art_motion_rules->speed_spans[index]==index*7u &&
              race->art_motion_rules->refresh_masks[index]==(index%3u==0u?3u:7u));
    }
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,15u},{9u,8u},{40u,0u},{55u,8u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t original=payload[invalid[index].offset];
        payload[invalid[index].offset]=invalid[index].value;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[invalid[index].offset]=original;
    }
    asset->size-=1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    asset->size+=1u;asset->version=2u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    return 0;
}

static int test_driver_projection_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,const PortableRecoveredPackStorage *race_storage,
    const PortableRecoveredRaceView *race,PortablePresentationPackStorage *storage)
{
    PortablePresentationPackView view;
    CHECK(portable_presentation_pack_open(pack,selection,race_storage,race,storage,&view)==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(view.driver_projection==NULL);
    PortableAssetPackView layered=*pack;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);
    CHECK(sections!=NULL);
    for(uint32_t index=0u;index<pack->section_count;++index)
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
    uint8_t payload[44]={0x44,0x50,0x52,0x31,3,0,0,0};
    for(unsigned index=0u;index<3u;++index){
        uint8_t *row=payload+8u+index*12u;
        /* Synthetic mod geometry, not an extracted source table. */
        row[0]=(uint8_t)(index+1u);row[1]=1u;
        row[2]=(uint8_t)(64u>>index);row[4]=(uint8_t)(12u-index);row[6]=(uint8_t)(20u-index);
        row[8]=1u;
    }
    PortableAssetSectionView *asset=&sections[pack->section_count];
    *asset=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES,
        .content_id=1u,.version=1u,.size=sizeof payload,.bytes=payload};
    layered.resolved_sections=sections;layered.section_count=pack->section_count+1u;
    CHECK(portable_presentation_pack_open(&layered,selection,race_storage,race,storage,&view)==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(view.driver_projection==&storage->driver_projection);
    for(unsigned index=0u;index<3u;++index){
        const PortableDriverProjectionRow *row=&view.driver_projection->rows[index];
        CHECK(row->max_distance==257u+index && row->size==(64u>>index) &&
              row->offset_x==12-(int)index && row->offset_y==20-(int)index && row->affine_double_size==1u);
    }
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,2u},{9u,128u},{10u,32u},{13u,1u},{15u,1u},
        {16u,2u},{17u,1u},{18u,1u},{20u,1u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t original=payload[invalid[index].offset];
        payload[invalid[index].offset]=invalid[index].value;
        CHECK(portable_presentation_pack_open(&layered,selection,race_storage,race,storage,&view)==
              PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[invalid[index].offset]=original;
    }
    asset->size-=1u;
    CHECK(portable_presentation_pack_open(&layered,selection,race_storage,race,storage,&view)==
          PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    asset->size+=1u;asset->version=2u;
    CHECK(portable_presentation_pack_open(&layered,selection,race_storage,race,storage,&view)==
          PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_presentation_pack_open(pack,selection,race_storage,race,storage,&view)==
          PORTABLE_RECOVERED_PACK_OK);
    return 0;
}

static int test_driver_affine_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->affine_rules==NULL && race->dependencies.affine_rules==NULL);
    PortableAssetPackView layered=*pack;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);
    CHECK(sections!=NULL);
    for(uint32_t index=0u;index<pack->section_count;++index)
        CHECK(portable_asset_pack_section_at(pack,index,&sections[index])==PORTABLE_ASSET_PACK_OK);
    uint8_t payload[78]={0x41,0x46,0x52,0x31,5,0,0,0};
    static const uint8_t modes[5]={1u,4u,5u,6u,7u};
    for(size_t table=0u;table<5u;++table){
        uint8_t *entry=payload+8u+table*14u;
        entry[0]=modes[table];entry[4]=1u;
        entry[8]=0xe0u;entry[9]=0u;entry[10]=0x80u;entry[11]=1u;
        entry[12]=(uint8_t)(table+1u);
    }
    PortableAssetSectionView *asset=&sections[pack->section_count];
    *asset=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_DRIVER_AFFINE_RULES,
        .content_id=1u,.version=1u,.size=sizeof payload,.bytes=payload};
    layered.resolved_sections=sections;layered.section_count=pack->section_count+1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->affine_rules==&storage->affine_rules && race->dependencies.affine_rules==race->affine_rules);
    for(size_t table=0u;table<5u;++table){
        const PortableDriverAffineTable *decoded=&race->affine_rules->tables[table];
        CHECK(decoded->mode==modes[table] && decoded->count==1u && decoded->rows==storage->affine_rows[table]);
        CHECK(decoded->rows[0].inverse_x_q8==224u && decoded->rows[0].inverse_y_q8==384u &&
              decoded->rows[0].duration==table+1u);
    }
    static const struct {size_t offset;uint8_t value;} invalid[]={
        {0u,0u},{4u,4u},{8u,4u},{12u,0u},{12u,33u},{16u,0u},{20u,0u},{20u,128u},{21u,1u},{22u,1u}};
    for(size_t index=0u;index<sizeof invalid/sizeof invalid[0];++index){
        const uint8_t original=payload[invalid[index].offset];
        payload[invalid[index].offset]=invalid[index].value;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[invalid[index].offset]=original;
    }
    asset->size=sizeof payload-1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    asset->size=sizeof payload;asset->version=2u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    return 0;
}

static int test_roulette_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race)
{
    PortableAssetPackView layered=*pack;
    PortableAssetSectionView *sections=calloc(pack->section_count+1u,sizeof *sections);
    CHECK(sections!=NULL);
    for(uint32_t i=0u;i<pack->section_count;++i)
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    uint8_t payload[176]={0x52,0x4c,0x54,0x31,10,0,0,0};
    for(unsigned i=0u;i<10u;++i){payload[8u+i*2u]=(uint8_t)i;payload[9u+i*2u]=0x10u;}
    for(unsigned i=0u;i<27u;++i)payload[28u+i*2u]=(uint8_t)(1u+i%3u);
    for(unsigned i=0u;i<8u;++i)payload[82u+i*2u]=(uint8_t)(4u+4u*i);
    for(unsigned i=0u;i<16u;++i)payload[98u+i*4u]=(uint8_t)(i+1u);
    for(unsigned i=0u;i<14u;++i)payload[162u+i]=(uint8_t)(i%4u);
    PortableAssetSectionView *asset=&sections[pack->section_count];
    *asset=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS,
        .content_id=12u,.version=1u,.size=sizeof payload,.bytes=payload};
    layered.resolved_sections=sections;layered.section_count=pack->section_count+1u;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race->roulette_rules==&storage->roulette_rules && race->roulette_rules->preview[9]==0x1009u &&
          race->roulette_rules->timeline[26]==3u && race->roulette_rules->contact_weight[7][1]==16u);
    const unsigned invalid_offsets[]={0u,4u,9u,28u,84u,98u,162u};
    for(unsigned i=0u;i<sizeof invalid_offsets/sizeof invalid_offsets[0];++i){
        const unsigned offset=invalid_offsets[i];const uint8_t saved=payload[offset];
        payload[offset]=offset==162u?4u:0u;
        CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[offset]=saved;
    }
    asset->bytes=NULL;
    CHECK(portable_recovered_pack_open_race(&layered,selection,storage,race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    free(sections);
    CHECK(portable_recovered_pack_open_race(pack,selection,storage,race)==PORTABLE_RECOVERED_PACK_OK);
    return 0;
}

static int player_odds_parity(const PortableGpPlayerItemRules *rules,
    const PortableGpRouletteRules *roulette)
{
    CHECK(rules!=NULL && roulette!=NULL);
    for(uint32_t seed=0u;seed<128u;++seed)for(int32_t threshold=-1;threshold<=8;++threshold){
        uint32_t actual=seed,expected=seed;
        const unsigned row=(portable_gp_item_random(&expected)&1u)?0u:1u;
        const uint16_t item=portable_gp_item_select_weighted(rules->table[row],16u,threshold,&expected);
        CHECK(portable_gp_item_select_player(rules,roulette,1u,0u,threshold,&actual)==item && actual==expected);
        for(unsigned gap=0u;gap<9u;++gap){
            actual=expected=seed;
            const uint32_t distance=gap==0u?0u:(uint32_t)roulette->gap_upper[gap-1u]+1u;
            const uint16_t selected=portable_gp_item_select_weighted(rules->table[2u+gap],16u,threshold,&expected);
            CHECK(portable_gp_item_select_player(rules,roulette,0u,distance,threshold,&actual)==selected && actual==expected);
        }
    }
    return 0;
}

static int test_imported_arenas(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL);
    CHECK(fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);CHECK(length>0);
    CHECK(fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    PortablePresentationPackStorage *art=malloc(sizeof *art);
    CHECK(bytes!=NULL && storage!=NULL && art!=NULL);
    CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length && fclose(file)==0);
    PortableAssetPackView pack;PortableAssetSectionView catalog;
    CHECK(portable_asset_pack_open(bytes,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_COURSE_CATALOG,
          3u,&catalog)==PORTABLE_ASSET_PACK_OK);
    CHECK(catalog.version==2u && catalog.size==56u && read32(catalog.bytes)==0x31544143u &&
          read32(catalog.bytes+4u)==4u);
    for(unsigned arena=0u;arena<4u;++arena){
        const uint32_t id=read32(catalog.bytes+8u+arena*12u);
        for(unsigned driver=1u;driver<=8u;++driver){
            PortableContentSelection selection;PortableRecoveredRaceView race;
            PortablePresentationPackView presentation;
            CHECK(portable_content_registry_select(&pack,id,driver,&selection)==PORTABLE_CONTENT_OK);
            CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
            PortableAssetSectionView player_odds;
            if(arena==0u&&driver==1u&&portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS,1u,&player_odds)==PORTABLE_ASSET_PACK_OK)
                CHECK(player_odds_parity(race.gp_player_items,race.roulette_rules)==0);
            PortableAssetSectionView material;
            CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,id,&material)==PORTABLE_ASSET_PACK_OK);
            if (material.version>=13u) {
                CHECK(race.battle_items!=NULL && race.battle_items->box_hidden_ticks==120u);
                CHECK(race.battle_items->table[0][0].item==0x1007u &&
                      race.battle_items->table[0][0].weight==10u &&
                      race.battle_items->table[1][0].weight==25u &&
                      race.battle_items->table[2][0].weight==35u);
            } else CHECK(race.battle_items==NULL);
            CHECK(race.rules.progress_kind==PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1 &&
                  race.rules.finish_kind==PORTABLE_FINISH_NONE && race.rules.lap_count==0u &&
                  race.gp_grid_count==4u && race.course_object_count!=0u);
            static const uint16_t headings[4]={0u,0x8000u,0x4000u,0xc000u};
            for(unsigned start=0u;start<4u;++start)
                CHECK(race.gp_grid[start].heading==headings[start]);
            CHECK(race.initial_driver.pose.position_x==race.gp_grid[0].x_q16 &&
                  race.initial_driver.pose.position_y==race.gp_grid[0].y_q16);
            CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,art,&presentation)==
                  PORTABLE_RECOVERED_PACK_OK);
            CHECK(presentation.view_rules==race.view_rules && presentation.finish_rules==race.finish_rules &&
                presentation.weather_profile==race.weather_profile);
            if(presentation.driver_projection!=NULL){
                static const uint16_t distances[3]={511u,1023u,4096u};
                static const int16_t source_y[3]={-46,-23,-11};
                for(unsigned lod=0u;lod<3u;++lod){
                    const PortableDriverProjectionRow *row=&presentation.driver_projection->rows[lod];
                    CHECK(row->max_distance==distances[lod] && row->size==(64u>>lod) &&
                          row->offset_x==-(32>>lod) && row->offset_y==source_y[lod] &&
                          row->affine_double_size==1u);
                }
            }
        }
    }
    free(art);free(storage);free(bytes);
    puts("imported arenas: PASS four arenas, four source starts, eight drivers, shared content/art loader");
    return 0;
}

static int test_hud_palette_asset(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    const PortableRecoveredRaceView *race,PortablePresentationPackStorage *art)
{
    PortableAssetSectionView base;CHECK(portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_HUD_GRAPHICS,selection->course.hud,&base)==0);
    CHECK(base.version==1u);
    uint8_t *bytes=malloc((size_t)base.size+34u);CHECK(bytes!=NULL);
    memcpy(bytes,base.bytes,(size_t)base.size);bytes[40]=17u;bytes[41]=bytes[42]=bytes[43]=0u;
    for(unsigned i=0u;i<17u;++i){bytes[base.size+i*2u]=0u;bytes[base.size+i*2u+1u]=(uint8_t)(0xa0u+(i%6u)*16u);}
    PortableAssetSectionView *sections=calloc(pack->section_count,sizeof *sections);CHECK(sections!=NULL);
    PortableAssetSectionView *replacement=NULL;
    for(uint32_t i=0u;i<pack->section_count;++i){
        CHECK(portable_asset_pack_section_at(pack,i,&sections[i])==0);
        if(sections[i].kind==PORTABLE_ASSET_SECTION_HUD_GRAPHICS&&sections[i].content_id==selection->course.hud){
            replacement=&sections[i];replacement->bytes=bytes;replacement->size=base.size+34u;replacement->version=2u;
        }
    }
    CHECK(replacement!=NULL);PortableAssetPackView layered=*pack;layered.resolved_sections=sections;
    PortablePresentationPackView view;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==0);
    CHECK(view.hud.item_palette_selectors==art->hud_item_palette_selectors &&view.hud.item_palette_count==17u);
    CHECK(view.hud.item_palette_selectors[4]==0xe000u&&view.hud.item_palette_selectors[16]==0xe000u);
    bytes[base.size+8u]=1u;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    bytes[base.size+8u]=0u;--replacement->size;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    replacement->size=base.size+34u;bytes[40]=16u;
    CHECK(portable_presentation_pack_open(&layered,selection,storage,race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    CHECK(portable_presentation_pack_open(pack,selection,storage,race,art,&view)==0&&
        view.hud.item_palette_selectors==NULL&&view.hud.item_palette_count==0u);
    free(sections);free(bytes);return 0;
}

int main(int argc,char **argv)
{
    if(argc==3 && strcmp(argv[1],"--arenas")==0)return test_imported_arenas(argv[2]);
    FILE *f;
    long n;
    uint8_t *bytes;
    PortableAssetPackView pack;
    PortableRecoveredPackStorage *storage;
    PortableRecoveredRaceView race;
    PortableAssetSectionView route;
    PortableAssetSectionView motion;
    PortablePresentationPackStorage *presentation;
    PortablePresentationPackView presentation_view;
    PortableAssetSectionView road;
    PortableAssetSectionView manifest;
    PortableContentSelection selection;
    PortableCharacterContent character;
    CHECK(argc==4);
    f=fopen(argv[1],"rb");CHECK(f!=NULL);CHECK(fseek(f,0,SEEK_END)==0);
    n=ftell(f);CHECK(n>0);CHECK(fseek(f,0,SEEK_SET)==0);
    bytes=malloc((size_t)n);storage=malloc(sizeof *storage);
    presentation=malloc(sizeof *presentation);
    CHECK(bytes!=NULL&&storage!=NULL&&presentation!=NULL);CHECK(fread(bytes,1,(size_t)n,f)==(size_t)n);
    CHECK(fclose(f)==0);
    CHECK(portable_asset_pack_open(bytes,(size_t)n,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_content_registry_select(&pack,0,0,&selection)==PORTABLE_CONTENT_OK);
    CHECK(selection.course.id==24&&selection.character.id==1);
    CHECK(portable_content_registry_find_character_class(&pack,3u,&character)==
          PORTABLE_CONTENT_OK);
    CHECK(character.id==1u&&character.character_class==3u);
    CHECK(portable_content_registry_find_character_class(&pack,2u,&character)==
          PORTABLE_CONTENT_NOT_FOUND);
    CHECK(portable_content_registry_find_character_class(&pack,8u,&character)==
          PORTABLE_CONTENT_INCOMPATIBLE);
    CHECK(portable_content_registry_select(&pack,24,1,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_content_registry_select(&pack,25,1,&selection)==PORTABLE_CONTENT_NOT_FOUND);
    CHECK(portable_content_registry_select(&pack,24,1,&selection)==PORTABLE_CONTENT_OK);
    CHECK(test_driver_affine_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_hud_palette_asset(&pack,&selection,storage,&race,presentation)==0);
    CHECK(test_roulette_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_driver_art_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_driver_view_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_finish_lakitu_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_time_trial_record_targets(&pack,&selection,storage,&race)==0);
    CHECK(test_finish_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_weather_asset(&pack,&selection,storage,&race)==0);
    CHECK(test_modern_drift_profile(&pack,&selection,storage,&race)==0);
    CHECK(test_ambient_asset(&pack,&selection,storage,&race)==0);
    {
        PortableAssetPackView mouse_pack=pack;
        PortableAssetSectionView *sections=calloc(pack.section_count+1u,sizeof *sections);
        CHECK(sections!=NULL);
        for(uint32_t i=0;i<pack.section_count;++i)
            CHECK(portable_asset_pack_section_at(&pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        uint8_t mouse_bytes[20]={0x4d,0x55,0x53,0x31,1,0,0,0,
            0,0,100,0,0,0,100,0,1,0x80,2,17};
        sections[pack.section_count]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_MICE,
            .content_id=24u,.version=1u,.size=sizeof mouse_bytes,.bytes=mouse_bytes};
        mouse_pack.resolved_sections=sections;mouse_pack.section_count=pack.section_count+1u;
        CHECK(portable_recovered_pack_open_race(&mouse_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.mouse_count==1u && race.mice[0].variant==0x8001u && race.mice[0].flags==3u &&
            race.mice[0].checkpoint==2u && race.mice[0].placement_order==17u);
        CHECK(portable_presentation_pack_open(&mouse_pack,&selection,storage,&race,presentation,
            &presentation_view)==PORTABLE_RECOVERED_PACK_MISSING_SECTION);
        mouse_bytes[16]=2u;
        CHECK(portable_recovered_pack_open_race(&mouse_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        mouse_bytes[16]=1u;mouse_bytes[18]=128u;
        CHECK(portable_recovered_pack_open_race(&mouse_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        mouse_bytes[18]=2u;mouse_bytes[4]=65u;
        CHECK(portable_recovered_pack_open_race(&mouse_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);
    }
    {
        PortableAssetPackView cloud_pack=pack;
        PortableAssetSectionView *sections=calloc(pack.section_count+1u,sizeof *sections);
        CHECK(sections!=NULL);
        for(uint32_t i=0;i<pack.section_count;++i)
            CHECK(portable_asset_pack_section_at(&pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        uint8_t cloud_bytes[20]={0x54,0x43,0x44,0x31,1,0,0,0,
            0,0,100,0,0,0,100,0,1,0,2,17};
        sections[pack.section_count]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_THUNDERCLOUDS,
            .content_id=24u,.version=1u,.size=sizeof cloud_bytes,.bytes=cloud_bytes};
        cloud_pack.resolved_sections=sections;cloud_pack.section_count=pack.section_count+1u;
        CHECK(portable_recovered_pack_open_race(&cloud_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.thundercloud_count==1u && race.thunderclouds[0].variant==1u && race.thunderclouds[0].flags==1u &&
            race.thunderclouds[0].checkpoint==2u && race.thunderclouds[0].placement_order==17u);
        CHECK(portable_presentation_pack_open(&cloud_pack,&selection,storage,&race,presentation,
            &presentation_view)==PORTABLE_RECOVERED_PACK_MISSING_SECTION);
        cloud_bytes[16]=4u;
        CHECK(portable_recovered_pack_open_race(&cloud_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        cloud_bytes[16]=1u;cloud_bytes[18]=128u;
        CHECK(portable_recovered_pack_open_race(&cloud_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        cloud_bytes[18]=2u;cloud_bytes[4]=65u;
        CHECK(portable_recovered_pack_open_race(&cloud_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);
    }
    {
        PortableAssetPackView star_pack=pack;
        PortableAssetSectionView *sections=calloc(pack.section_count+1u,sizeof *sections);
        CHECK(sections!=NULL);
        for(uint32_t i=0;i<pack.section_count;++i)
            CHECK(portable_asset_pack_section_at(&pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        uint8_t star_bytes[20]={0x53,0x54,0x52,0x31,1,0,0,0,
            0,0,100,0,0,0,100,0,1,0,2,17};
        sections[pack.section_count]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_FALLING_STARS,
            .content_id=24u,.version=1u,.size=sizeof star_bytes,.bytes=star_bytes};
        star_pack.resolved_sections=sections;star_pack.section_count=pack.section_count+1u;
        CHECK(portable_recovered_pack_open_race(&star_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.falling_star_count==1u && race.falling_stars[0].variant==1u && race.falling_stars[0].flags==0u &&
            race.falling_stars[0].checkpoint==2u && race.falling_stars[0].placement_order==17u);
        CHECK(portable_presentation_pack_open(&star_pack,&selection,storage,&race,presentation,
            &presentation_view)==PORTABLE_RECOVERED_PACK_MISSING_SECTION);
        star_bytes[16]=6u;
        CHECK(portable_recovered_pack_open_race(&star_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        star_bytes[16]=1u;star_bytes[18]=128u;
        CHECK(portable_recovered_pack_open_race(&star_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        star_bytes[18]=2u;star_bytes[4]=65u;
        CHECK(portable_recovered_pack_open_race(&star_pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);
    }
    /* A bank is all-or-none. Reject partial banks, bad totals and nonzero
     * padding before exposing any table to the deterministic session. */
    {
        PortableAssetPackView bank=pack;
        PortableAssetSectionView *sections=calloc(pack.section_count+19u,sizeof *sections);
        uint8_t payloads[11][72]={{0}};CHECK(sections!=NULL);
        for(uint32_t i=0u;i<pack.section_count;++i)
            CHECK(portable_asset_pack_section_at(&pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        for(unsigned i=0u;i<11u;++i){
            const uint8_t header[]={0x47,0x50,0x57,0x31,16,0,0,0,0,0x10,1,0,0,0,255,0};
            memcpy(payloads[i],header,sizeof header);
            sections[pack.section_count+i]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS,
                .content_id=i+1u,.version=1u,.size=72u,.bytes=payloads[i]};
        }
        bank.resolved_sections=sections;bank.section_count=pack.section_count+1u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        bank.section_count=pack.section_count+11u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.gp_player_items!=NULL&&race.gp_player_items->table[10][0].item==0x1000u);
        payloads[10][16]=1u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payloads[10][16]=0u;payloads[0][10]=0u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payloads[0][10]=1u;
        uint8_t scoring[112]={0x47,0x50,0x54,0x31,8,0,0,0,18,12,6,2,0,0,0,0,0,0,0,0};
        sections[pack.section_count+11u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_GP_SCORING,
            .content_id=1u,.version=1u,.size=16u,.bytes=scoring};
        bank.section_count=pack.section_count+12u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.gp_scoring!=NULL&&race.gp_scoring->finish_points[0]==18u);
        CHECK(race.gp_scoring->initial_retry_credits==3u);
        sections[pack.section_count+11u].version=2u;sections[pack.section_count+11u].size=20u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.gp_scoring->initial_retry_credits==0u);
        scoring[16]=3u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK&&race.gp_scoring->initial_retry_credits==3u);
        scoring[16]=4u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        scoring[16]=0u;sections[pack.section_count+11u].size=19u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        sections[pack.section_count+11u].size=20u;
        scoring[8]=64u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        scoring[8]=18u;scoring[4]=7u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        scoring[4]=8u;
        sections[pack.section_count+11u].version=3u;
        sections[pack.section_count+11u].size=sizeof scoring;
        ambient_write32(scoring+20u,11u);
        ambient_write32(scoring+68u,200u);
        ambient_write32(scoring+80u,UINT32_C(0xfffffff9));
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.gp_scoring->has_quality_rules && race.gp_scoring->quality.rank_bonus[0][0]==11 &&
              race.gp_scoring->quality.speed_target[0]==200 && race.gp_scoring->quality.driver_bonus[0]==-7);
        ambient_write32(scoring+68u,10001u);
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        ambient_write32(scoring+68u,200u);
        uint8_t mushroom[]={0x4d,0x55,0x53,0x31,5,0,0,0,3,0,0,0,2,0,0,0,4,0,0,0,7,0,0,0,0,0,0xe8,3};
        sections[pack.section_count+12u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_MUSHROOM_RULES,.content_id=1u,.version=1u,.size=28u,.bytes=mushroom};
        bank.section_count=pack.section_count+13u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.mushroom_rules->boost_ticks==3u&&race.dependencies.mushroom_rules->single_actor_ticks==2u&&race.dependencies.mushroom_rules->golden_actor_ticks==4u&&race.dependencies.mushroom_rules->acceleration_units==7u&&race.dependencies.mushroom_rules->max_speed_q16==1000u*65536u);
        mushroom[8]=0u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        mushroom[8]=3u;mushroom[16]=0u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        mushroom[16]=4u;sections[pack.section_count+12u].size=27u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        sections[pack.section_count+12u].size=28u;
        uint8_t status[]={0x53,0x41,0x54,0x31,3,0,0,0,90,0,0,0,60,0,0,0,120,0,0,0};
        sections[pack.section_count+13u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_ITEM_STATUS_RULES,.content_id=1u,.version=1u,.size=20u,.bytes=status};
        bank.section_count=pack.section_count+14u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.item_status_rules->star_player_ticks==90u&&race.dependencies.item_status_rules->star_cpu_ticks==60u&&race.dependencies.item_status_rules->lightning_ticks==120u);
        status[16]=47u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        status[16]=120u;status[11]=1u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        status[11]=0u;
        uint8_t boo[]={0x42,0x4f,0x4f,0x31,7,0,0,0,90,0,0,0,60,0,0,0,16,0,0,0,0,12,0,0,0,8,0,0,0,0,1,0,0,64,0,0};
        sections[pack.section_count+14u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_BOO_RULES,.content_id=1u,.version=1u,.size=36u,.bytes=boo};bank.section_count=pack.section_count+15u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.boo_rules->invisibility_ticks==90u&&race.dependencies.boo_rules->lock_ticks==60u&&race.dependencies.boo_rules->orbit_ticks==16u&&race.dependencies.boo_rules->orbit_step==3072u&&race.dependencies.boo_rules->bob_step==2048u&&race.dependencies.boo_rules->repel_velocity_q16==65536u&&race.dependencies.boo_rules->repel_gravity_q16==16384u);
        boo[16]=0u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        boo[16]=16u;boo[30]=0u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        boo[30]=1u;
        const uint32_t green_values[]={0x31485347u,8u,384u,60u,4u,2u,8u,60u,98304u,12288u};
        uint8_t green[40];for(unsigned field=0u;field<10u;++field)for(unsigned byte=0u;byte<4u;++byte)green[field*4u+byte]=(uint8_t)(green_values[field]>>(byte*8u));
        sections[pack.section_count+15u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_GREEN_SHELL_RULES,.content_id=1u,.version=1u,.size=40u,.bytes=green};bank.section_count=pack.section_count+16u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.green_shell_rules->launch_speed_bonus==384u&&race.dependencies.green_shell_rules->max_speed==60u&&race.dependencies.green_shell_rules->collision_grace_ticks==4u&&race.dependencies.green_shell_rules->wall_contact_budget==2u&&race.dependencies.green_shell_rules->release_lock_ticks==8u&&race.dependencies.green_shell_rules->cpu_hold_ticks==60u&&race.dependencies.green_shell_rules->destruction_velocity_q16==98304u&&race.dependencies.green_shell_rules->destruction_gravity_q16==12288u);
        green[20]=8u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        green[20]=2u;green[39]=1u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        green[39]=0u;
        const uint32_t red_values[]={0x31485352u,16u,384u,60u,4u,6u,8u,60u,80u,90u,100u,8u,24u,24u,180u,10u,98304u,12288u};
        uint8_t red[72];for(unsigned field=0u;field<18u;++field)for(unsigned byte=0u;byte<4u;++byte)red[field*4u+byte]=(uint8_t)(red_values[field]>>(byte*8u));
        sections[pack.section_count+16u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_RED_SHELL_RULES,.content_id=1u,.version=1u,.size=72u,.bytes=red};bank.section_count=pack.section_count+17u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.red_shell_rules->max_launch_speed==60u&&race.dependencies.red_shell_rules->route_budget==6u&&race.dependencies.red_shell_rules->flight_speed_150==100u&&race.dependencies.red_shell_rules->trap_lifetime_ticks==180u&&race.dependencies.red_shell_rules->destruction_gravity_q16==12288u);
        red[24]=25u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        red[24]=8u;red[43]=1u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        red[43]=0u;
        const uint32_t banana_values[]={0x314e4142u,9u,262144u,24u,155648u,12288u,4u,8u,60u,65536u,16384u};
        uint8_t banana[44];for(unsigned field=0u;field<11u;++field)for(unsigned byte=0u;byte<4u;++byte)banana[field*4u+byte]=(uint8_t)(banana_values[field]>>(byte*8u));
        sections[pack.section_count+17u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_BANANA_RULES,.content_id=1u,.version=1u,.size=44u,.bytes=banana};bank.section_count=pack.section_count+18u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.banana_rules->throw_speed_q16==262144u&&race.dependencies.banana_rules->cpu_flight_ticks==24u&&race.dependencies.banana_rules->throw_velocity_q16==155648u&&race.dependencies.banana_rules->destruction_gravity_q16==16384u);
        banana[28]=28u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        banana[28]=8u;banana[19]=1u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);


        banana[19]=0u;
        const uint32_t orbit_values[]={0x3142524fu,3u,22u,768u,4u};
        uint8_t orbit[20];for(unsigned field=0u;field<5u;++field)for(unsigned byte=0u;byte<4u;++byte)orbit[field*4u+byte]=(uint8_t)(orbit_values[field]>>(byte*8u));
        sections[pack.section_count+18u]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_SHELL_ORBIT_RULES,.content_id=1u,.version=1u,.size=20u,.bytes=orbit};bank.section_count=pack.section_count+19u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.shell_orbit_rules->radius_multiplier==22u && race.dependencies.shell_orbit_rules->angle_step==768u && race.dependencies.shell_orbit_rules->animation_period==4u);
        orbit[8]=0u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        orbit[8]=22u;orbit[15]=1u;CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);
    }
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.recovery_bob_offsets!=NULL);
    CHECK(race.lava_bubble_count==1u&&race.lava_bubbles[0].timer==100&&
          race.lava_bubbles[0].period==100&&race.lava_bubbles[0].checkpoint==9u && race.lava_bubbles[0].placement_order==7u);
    PortableAssetSectionView bubbles;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_LAVA_BUBBLES,
          24u,&bubbles)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)bubbles.bytes)[16]=13u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)bubbles.bytes)[16]=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.thwomp_count==1u&&race.thwomps[0].timer==33&&
          race.thwomps[0].variant==1u&&race.thwomps[0].checkpoint==9u && race.thwomps[0].placement_order==9u);
    PortableAssetSectionView thwomps;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_THWOMPS,
          24u,&thwomps)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)thwomps.bytes)[16]=12u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)thwomps.bytes)[16]=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.crab_count==1u && race.crabs[0].variant==0x8001u &&
          race.crabs[0].velocity_y_q16==0x8000 && race.crabs[0].art==2u &&
          race.crabs[0].placement_order==11u);
    PortableAssetSectionView crabs;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_CRABS,
          24u,&crabs)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)crabs.bytes)[16]=3u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)crabs.bytes)[16]=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.seagull_count==1u && race.seagulls[0].affine_x_q8==224u &&
          race.seagulls[0].placement_order==13u);
    PortableAssetSectionView seagulls;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_SEAGULLS,
          24u,&seagulls)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)seagulls.bytes)[16]=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)seagulls.bytes)[16]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.piranha_count==1u && race.piranhas[0].affine_x_q8==224u &&
          race.piranhas[0].placement_order==13u);
    PortableAssetSectionView piranhas;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_PIRANHAS,
          24u,&piranhas)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)piranhas.bytes)[16]=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)piranhas.bytes)[16]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.snowman_count==1u && race.snowmen[0].state==17 && race.snowmen[0].flags==3u &&
          race.snowmen[0].placement_order==13u);
    PortableAssetSectionView snowmen;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_SNOWMEN,
          24u,&snowmen)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)snowmen.bytes)[16]=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)snowmen.bytes)[16]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.penguin_count==1u && race.penguins[0].timer==0 && race.penguins[0].flags==3u &&
          race.penguins[0].placement_order==13u);
    PortableAssetSectionView penguins;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_PENGUINS,
          24u,&penguins)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)penguins.bytes)[16]=2u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)penguins.bytes)[16]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.boo_count==1u && race.boos[0].state==16u && race.boos[0].flags==3u &&
          race.boos[0].placement_order==13u);
    PortableAssetSectionView boos;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_BOOS,
          24u,&boos)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)boos.bytes)[16]=2u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)boos.bytes)[16]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.cannonball_count==1u && race.cannonballs[0].timer==96 &&
          race.cannonballs[0].placement_order==13u);
    PortableAssetSectionView cannonballs;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_CANNONBALLS,
          24u,&cannonballs)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)cannonballs.bytes)[16]=4u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)cannonballs.bytes)[16]=3u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.dependencies.ramp_speed_limit_q16==INT32_C(0x03a00000));
    CHECK(race.dependencies.wall_replacement_tile == 8u);
    CHECK(race.dependencies.wall_contact_song == 169u);
    CHECK(race.dependencies.track_material->protected_hop_threshold_count==6u&&
          race.dependencies.track_material->protected_hop_thresholds[0]==592u&&
          race.dependencies.track_material->protected_hop_thresholds[5]==656u);
    CHECK(race.dependencies.owned_terrain_cells == storage->terrain_cells &&
          race.dependencies.initial_terrain_cells != storage->terrain_cells);
    PortableAssetSectionView material;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,24u,&material)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)material.bytes)[277]=8u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)material.bytes)[277]=2u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    ((uint8_t *)material.bytes)[267]=128u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)material.bytes)[267]=3u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    ((uint8_t *)material.bytes)[269] = 1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)material.bytes)[269] = 0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    ((uint8_t *)material.bytes)[274] = 1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)material.bytes)[274] = 0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    PortableAssetSectionView recovery;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_RECOVERY_MOTION,1u,&recovery)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)recovery.bytes)[4]=15u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)recovery.bytes)[4]=16u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    ((uint8_t *)recovery.bytes)[8]=65u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)recovery.bytes)[8]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.rules.route_count==47&&race.rules.start_route_index==46);
    CHECK(race.course_object_count==2u&&
          race.course_objects==storage->course_objects&&
          race.course_objects[0].position_x_q16==(INT32_C(61)<<19u)&&
          race.course_objects[0].position_y_q16==(INT32_C(57)<<19u)&&
          race.course_objects[0].kind==UINT16_C(0x8800)&&
          race.course_objects[0].source_parameter==9u&&
          race.course_objects[0].collision_half_extent==0x1eu);
    CHECK(race.rules.ai_modes==storage->route_ai_modes&&
          race.rules.ai_mode_count==47&&race.rules.ai_modes[0]==2u&&
          race.rules.ai_modes[1]==0u);
    CHECK(race.rules.compatibility.character_class==3u&&
          race.initial_driver.auto_acceleration.character_class==3u);
    CHECK(race.dependencies.forward_local_table[0]==1300&&
          race.dependencies.forward_local_table[15]==1315&&
          race.gp_forward_local_table[0]==300&&
          race.gp_forward_local_table[15]==315);
    CHECK(portable_asset_pack_find_section(
              &pack,PORTABLE_ASSET_SECTION_MOTION_TABLES,1,&motion)==
          PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)motion.bytes)[24]^=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==
          PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)motion.bytes)[24]^=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.rules.rescue_penalty_ticks==180&&race.dependencies.sine_count==1024);
    /* RUL1 v1 remains structural only; v2 supplies the required odds contract. */
    CHECK(race.rules.item_strength_threshold==0u);
    CHECK(race.dependencies.mushroom_rules==NULL && race.dependencies.item_status_rules==NULL &&
          race.dependencies.boo_rules==NULL && race.dependencies.green_shell_rules==NULL &&
          race.dependencies.red_shell_rules==NULL && race.dependencies.banana_rules==NULL &&
          race.dependencies.shell_orbit_rules==NULL);
    {
        PortableAssetPackView bank=pack;
        PortableAssetSectionView *sections=calloc(pack.section_count,sizeof *sections);
        uint8_t rules_v2[72];size_t rules_index=pack.section_count;
        CHECK(sections!=NULL);
        for(uint32_t i=0u;i<pack.section_count;++i){
            CHECK(portable_asset_pack_section_at(&pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
            if(sections[i].kind==PORTABLE_ASSET_SECTION_RACE_RULES&&sections[i].content_id==24u)rules_index=i;
        }
        CHECK(rules_index<pack.section_count&&sections[rules_index].size==68u);
        memcpy(rules_v2,sections[rules_index].bytes,68u);memset(rules_v2+68,0,4u);rules_v2[68]=4u;
        sections[rules_index].bytes=rules_v2;sections[rules_index].version=2u;sections[rules_index].size=72u;
        bank.resolved_sections=sections;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK&&
              race.rules.item_strength_threshold==4u);
        rules_v2[68]=9u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        rules_v2[68]=4u;rules_v2[70]=1u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        rules_v2[70]=0u;sections[rules_index].size=68u;
        CHECK(portable_recovered_pack_open_race(&bank,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);
        CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    }
    CHECK(race.dependencies.forward_limit_thresholds==NULL);
    selection.character.character_class=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.rules.compatibility.character_class==0u&&
          race.initial_driver.auto_acceleration.character_class==0u);
    selection.character.character_class=3u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
                                           &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(presentation_view.scenery.object_count==1u&&
          presentation_view.scenery.native_art_count==1u);
    CHECK(presentation_view.scenery.objects[0].position_y_q16==12*65536&&
          presentation_view.scenery.objects[0].source_parameter==9u &&
          presentation_view.scenery.objects[0].affine_q8==224u &&
          presentation_view.scenery.objects[0].source_code==0x0c0000u);
    CHECK(race.course_prop_count==1u && race.course_props[0].source_code==0xc0000u &&
        race.course_props[0].affine_x_q8==224 && race.course_props[0].flags==1u &&
        race.course_props[0].checkpoint==9u && race.course_props[0].placement_order==14u);
    for(unsigned lod=0u;lod<4u;++lod){
        const PortableCourseSceneryArt *art=presentation_view.scenery.native_art;
        CHECK(art->distance_shift[lod]==lod&&art->lod[lod].width==8u&&
              art->lod[lod].height==16u&&art->lod[lod].anchor_y==-16);
        CHECK(portable_native_image_pixel(&art->lod[lod],0,0)==0xff102030u+lod);
    }
    PortableAssetSectionView scenery;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_COURSE_SCENERY,
                                          24u,&scenery)==PORTABLE_ASSET_PACK_OK);
    /* A beach prop needs its adjacent alternate art as well as base art. */
    uint8_t *code=(uint8_t *)scenery.bytes+28u;
    const uint8_t saved_code[4]={code[0],code[1],code[2],code[3]};
    code[0]=code[1]=code[3]=0u;code[2]=0x86u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
        &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    code[0]=code[1]=code[3]=0u;code[2]=0xa0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
        &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    code[2]=0xa1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    memcpy(code,saved_code,4u);
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    const size_t bad_offsets[]={25u,27u,32u,36u,44u};
    for(size_t i=0;i<sizeof bad_offsets/sizeof bad_offsets[0];++i){
        uint8_t *byte=(uint8_t *)scenery.bytes+bad_offsets[i];
        const uint8_t old=*byte;*byte=255u;
        CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,
              presentation,&presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        *byte=old;
    }
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
                                         &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(presentation_view.seagull_art!=NULL &&
          portable_native_image_pixel(&presentation_view.seagull_art[2].lod[3],0,0)==0xff112202u);
    CHECK(presentation_view.piranha_art!=NULL &&
          portable_native_image_pixel(&presentation_view.piranha_art[4].lod[3],0,0)==0xff112204u);
    CHECK(presentation_view.snowman_art!=NULL &&
          portable_native_image_pixel(&presentation_view.snowman_art[1].lod[3],0,0)==0xff112201u);
    CHECK(presentation_view.penguin_art!=NULL &&
          portable_native_image_pixel(&presentation_view.penguin_art[5].lod[3],0,0)==0xff112205u);
    CHECK(presentation_view.boo_art!=NULL &&
          portable_native_image_pixel(&presentation_view.boo_art[1].lod[3],0,0)==0xff112201u);
    CHECK(presentation_view.cannonball_art[4].lod[0].width==67u);
    CHECK(presentation_view.cannonball_art!=NULL &&
          portable_native_image_pixel(&presentation_view.cannonball_art[6].lod[3],0,0)==0xff112206u);
    CHECK(presentation_view.crab_art!=NULL);
    CHECK(portable_native_image_pixel(&presentation_view.crab_art[3].lod[2],0,0)==0xff112203u);
    PortableAssetSectionView crab_art;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_CRAB_ART,
          24u,&crab_art)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)crab_art.bytes)[8]=4u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)crab_art.bytes)[8]=0u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(presentation_view.thwomp_art!=NULL);
    CHECK(portable_native_image_pixel(&presentation_view.thwomp_art[0].lod[2],0,0)==0xff112200u);
    CHECK(presentation_view.lava_bubble_art!=NULL);
    CHECK(portable_native_image_pixel(&presentation_view.lava_bubble_art[1].lod[2],0,0)==0xff112201u);
    PortableAssetSectionView thwomp_art;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_THWOMP_ART,
          24u,&thwomp_art)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)thwomp_art.bytes)[8]=4u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)thwomp_art.bytes)[8]=0u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    PortableAssetSectionView bubble_art;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_LAVA_BUBBLE_ART,
          24u,&bubble_art)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)bubble_art.bytes)[8]=4u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)bubble_art.bytes)[8]=0u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(presentation_view.geometry.nr_checkpoints==47);
    CHECK(presentation_view.world_map.map==storage->terrain_cells);
    CHECK(presentation_view.character.width==64);
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_ROUTE,24,&route)==PORTABLE_ASSET_PACK_OK);
    CHECK(route.version==2u);
    ((uint8_t *)route.bytes)[0]^=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    ((uint8_t *)route.bytes)[0]^=1u;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,24,&road)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)road.bytes)[0]^=1u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
                                           &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_COURSE_MANIFEST,24,&manifest)==PORTABLE_ASSET_PACK_OK);
    ((uint8_t *)manifest.bytes)[0]^=1u;
    CHECK(portable_content_registry_select(&pack,24,1,&selection)==PORTABLE_CONTENT_BAD_MANIFEST);
    free(bytes);
    f=fopen(argv[2],"rb");CHECK(f!=NULL);CHECK(fseek(f,0,SEEK_END)==0);
    n=ftell(f);CHECK(n>0);CHECK(fseek(f,0,SEEK_SET)==0);
    bytes=malloc((size_t)n);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1,(size_t)n,f)==(size_t)n);CHECK(fclose(f)==0);
    CHECK(portable_asset_pack_open(bytes,(size_t)n,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_content_registry_select(&pack,24,1,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==
          PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.dependencies.forward_local_table[0]==77&&
          race.dependencies.forward_local_table[15]==92&&
          race.gp_forward_local_table[0]==77&&
          race.gp_forward_local_table[15]==92);
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
                                         &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(presentation_view.scenery.native_art==NULL&&
          presentation_view.scenery.object_count==1u&&
          presentation_view.scenery.objects[0].position_x_q16==12*65536&&
          presentation_view.scenery.objects[0].position_y_q16==20*65536&&
          presentation_view.scenery.objects[0].source_parameter==9u);
    CHECK(test_driver_projection_asset(&pack,&selection,storage,&race,presentation)==0);
    CHECK(test_sky_motion_asset(&pack,&selection,storage,&race,presentation)==0);
    CHECK(test_recovery_fx_assets(&pack,&selection,storage,&race,presentation)==0);
    CHECK(test_surface_trail_assets(&pack,&selection,storage,&race,presentation)==0);
    CHECK(test_coin_effect_assets(&pack,&selection,storage,&race,presentation)==0);
    CHECK(test_impact_effect_assets(&pack,&selection,storage,&race,presentation)==0);
    CHECK(presentation_view.coin_pickup.frame_count==7u &&
          presentation_view.coin_pickup.cell_bytes==50u);
    CHECK(presentation_view.entry_splash.frame_count==3u &&
          presentation_view.entry_splash.cell_bytes==24u &&
          presentation_view.entry_splash.frames[0].duration==8u);
    CHECK(presentation_view.mushroom_burst.frame_count==12u &&
          presentation_view.mushroom_burst.cell_bytes==528u);
    {
        PortableAssetPackView older=pack;
        PortableAssetSectionView *sections=calloc(pack.section_count,sizeof *sections);
        CHECK(sections!=NULL);
        uint32_t count=0u;
        PortableAssetSectionView splash={0};
        for(uint32_t index=0u;index<pack.section_count;++index){
            PortableAssetSectionView section;
            CHECK(portable_asset_pack_section_at(&pack,index,&section)==PORTABLE_ASSET_PACK_OK);
            if(section.kind==PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS&&section.content_id==6u)
                splash=section;
            else sections[count++]=section;
        }
        CHECK(splash.bytes!=NULL && count+1u==pack.section_count);
        older.resolved_sections=sections;older.section_count=count;
        CHECK(portable_presentation_pack_open(&older,&selection,storage,&race,presentation,
            &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(presentation_view.entry_splash.frame_count==0u &&
              presentation_view.entry_splash.tiles==NULL);
        /* A present malformed section must fail rather than act like old content. */
        sections[count]=splash;sections[count].size-=1u;
        older.section_count=count+1u;
        CHECK(portable_presentation_pack_open(&older,&selection,storage,&race,presentation,
            &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);
        CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
            &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    }
    PortableAssetSectionView burst;
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,
          2u,&burst)==PORTABLE_ASSET_PACK_OK);
    uint8_t *frame_offset=(uint8_t *)burst.bytes+32u;
    frame_offset[0]=0xffu;frame_offset[1]=0xffu;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    frame_offset[0]=0u;frame_offset[1]=0u;
    CHECK(portable_presentation_pack_open(&pack,&selection,storage,&race,presentation,
          &presentation_view)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.rules.cpu_tuning==NULL);
    free(bytes);
    f=fopen(argv[3],"rb");CHECK(f!=NULL);CHECK(fseek(f,0,SEEK_END)==0);
    n=ftell(f);CHECK(n>0);CHECK(fseek(f,0,SEEK_SET)==0);
    bytes=malloc((size_t)n);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1,(size_t)n,f)==(size_t)n);CHECK(fclose(f)==0);
    CHECK(portable_asset_pack_open(bytes,(size_t)n,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_content_registry_select(&pack,24,1,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.rules.cpu_tuning==&storage->cpu_tuning);
    CHECK(race.rules.gp_sentinel_targets==storage->gp_sentinel_targets &&
          race.rules.gp_sentinel_target_mask==1u &&
          race.rules.gp_sentinel_targets[0].x==80 && race.rules.gp_sentinel_targets[0].y==96);
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_ROUTE,24u,&route)==PORTABLE_ASSET_PACK_OK);
    uint8_t *sentinel_mask=(uint8_t *)route.bytes+route.size-12u;
    *sentinel_mask=2u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    *sentinel_mask=1u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    for(unsigned driver=0u;driver<8u;++driver)
    {
        for(unsigned bin=0u;bin<16u;++bin)
            CHECK(race.rules.cpu_tuning->acceleration[driver][bin]==
                  (uint8_t)((driver*16u+bin)*7u+3u));
        for(unsigned row=0u;row<5u;++row)
            for(unsigned mode=0u;mode<4u;++mode)
                CHECK(race.rules.cpu_tuning->target_speeds[driver][row][mode]==
                      (uint8_t)((0x80u+driver*20u+row*4u+mode)*7u+3u));
    }
    for(unsigned row=0u;row<5u;++row)
        for(unsigned bin=0u;bin<8u;++bin)
            CHECK(race.rules.cpu_tuning->heading_steps[row][bin]==
                  (uint8_t)((0x120u+row*8u+bin)*7u+3u));
    CHECK(race.gp_cpu_item_engine_count==3u && race.gp_cpu_items==storage->gp_cpu_items);
    for(unsigned engine=0u;engine<3u;++engine)
        for(unsigned variant=0u;variant<3u;++variant)
            for(unsigned driver=0u;driver<8u;++driver)
                for(unsigned rank=0u;rank<8u;++rank)
                    CHECK(race.gp_cpu_items[engine].item[variant][driver][rank]==
                        0x1000u+(((engine*3u+variant)*8u+driver)*8u+rank)%13u);
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,24u,&material)==PORTABLE_ASSET_PACK_OK);
    CHECK(material.version==16u);
    CHECK(race.battle_items == &storage->battle_items &&
          race.battle_items->box_hidden_ticks == 120u && race.battle_items->elimination_ticks == 10u &&
          race.battle_items->ai.cruise_speed==256u && race.battle_items->ai.turn_speed==128u &&
          race.battle_items->combat.range==128u && race.battle_items->combat.aim_angle==4096u &&
          race.battle_items->combat.interval==48u && race.battle_items->ai.clearance==6u && race.battle_items->ai.lookahead==64u &&
          race.battle_items->table[0][0].item == 0x1007u &&
          race.battle_items->table[1][0].weight == 25u &&
          race.battle_items->table[2][0].weight == 35u);
    const size_t battle_bad_offsets[] = {3680u,3683u,3720u+16u+2u,3720u+20u,3800u,3804u,3805u,3809u,3812u,3817u,3821u,3824u,3828u,3811u,3832u,3835u,3837u,3840u,3843u};
    const uint8_t battle_bad_values[] = {0xffu,0xffu,0u,1u,0u,0u,1u,0u,0u,0u,0u,0u,0u,1u,0u,1u,0u,0u,1u};
    for (size_t invalid=0u; invalid<sizeof battle_bad_values; ++invalid) {
        uint8_t *value=(uint8_t *)material.bytes+battle_bad_offsets[invalid];
        const uint8_t saved=*value;
        *value=battle_bad_values[invalid];
        CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        *value=saved;
        CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    }
    CHECK(race.dependencies.spatial_pitch_count==1089u &&
          race.dependencies.spatial_pitch_table[0]==-544 &&
          race.dependencies.spatial_pitch_table[1088]==544);
    CHECK(race.dependencies.mushroom_voice_songs[0]==208u &&
          race.dependencies.mushroom_voice_songs[1]==209u &&
          race.initial_driver.item_cue_flags_58==UINT16_C(0x10));
    uint8_t *voice=(uint8_t *)material.bytes+1470u;
    const uint8_t saved_voice[2]={voice[0],voice[1]};
    voice[0]=0u;voice[1]=0u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    voice[0]=saved_voice[0];voice[1]=saved_voice[1];
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.gp_cpu_items[0].shell_orbit_delay_valid &&
        race.gp_cpu_items[2].shell_orbit_delay[0][1]==17u && race.gp_cpu_items[1].shell_orbit_delay[1][2]==34u);
    CHECK(race.gp_cpu_items[0].red_release_valid && race.gp_cpu_items[2].red_release[1][3]==1u);
    CHECK(race.gp_cpu_items[0].banana_release_valid && race.gp_cpu_items[2].banana_release[1][1]==1u);
    CHECK(race.gp_cpu_items[0].green_release_valid && race.gp_cpu_items[2].green_release[0][0]==2u);
    uint8_t *red_decision=(uint8_t *)material.bytes+1456u;
    const uint8_t saved_red=*red_decision;*red_decision=4u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    *red_decision=saved_red;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    uint8_t *green_decision=(uint8_t *)material.bytes+1448u;
    const uint8_t saved_green=*green_decision;*green_decision=4u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    *green_decision=saved_green;
    uint8_t *decision=(uint8_t *)material.bytes+1440u;
    const uint8_t saved_decision=*decision;*decision=4u;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    *decision=saved_decision;
    uint8_t *last_cpu_item=(uint8_t *)material.bytes+1438u;
    uint8_t saved_cpu_item[2]={last_cpu_item[0],last_cpu_item[1]};
    last_cpu_item[0]=0xffu;last_cpu_item[1]=0xffu;
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    last_cpu_item[0]=saved_cpu_item[0];last_cpu_item[1]=saved_cpu_item[1];
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(race.gp_engine_count==3u && race.gp_engines==storage->gp_engines);
    for (unsigned engine=0u;engine<3u;++engine)
    {
        const PortableGpEngineProfile *profile=&race.gp_engines[engine];
        CHECK(profile->player_base_speed_q16==(int32_t)(900u+engine*100u)*65536);
        CHECK(profile->player_forward_limits[0]==-1 &&
              profile->player_forward_limits[15]==(int16_t)(700u+engine*100u));
        CHECK(profile->cpu.acceleration[7][15]==(uint8_t)(127u*7u+3u+engine));
        CHECK(profile->cpu.target_speeds[7][4][3]==(uint8_t)(287u*7u+3u+engine));
        CHECK(profile->cpu.heading_steps[4][7]==(uint8_t)(327u*7u+3u+engine));
        CHECK(profile->angle_targets[415]==(uint8_t)(415u*7u+3u+engine));
    }
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_MOTION_TABLES,1u,&motion)==PORTABLE_ASSET_PACK_OK);
    CHECK(motion.version==6u);
    CHECK(race.dependencies.source_auto_deceleration_table==NULL &&
        race.dependencies.source_auto_deceleration_count==0u &&
        race.dependencies.response_rules==NULL);
    {
        uint8_t *payload=malloc((size_t)motion.size+428u);CHECK(payload!=NULL);
        memcpy(payload,motion.bytes,(size_t)motion.size);
        for(unsigned i=0u;i<8u;++i){
            const uint16_t value=(uint16_t)(-5-(int)i*9);
            payload[motion.size+i*2u]=(uint8_t)value;
            payload[motion.size+i*2u+1u]=(uint8_t)(value>>8u);
        }
        PortableAssetSectionView *sections=calloc(pack.section_count,sizeof *sections);CHECK(sections!=NULL);
        PortableAssetSectionView *replacement=NULL;
        for(uint32_t i=0u;i<pack.section_count;++i){
            CHECK(portable_asset_pack_section_at(&pack,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
            if(sections[i].kind==PORTABLE_ASSET_SECTION_MOTION_TABLES && sections[i].content_id==1u){
                replacement=&sections[i];replacement->bytes=payload;
                replacement->size=motion.size+16u;replacement->version=7u;
            }
        }
        CHECK(replacement!=NULL);
        PortableAssetPackView layered=pack;layered.resolved_sections=sections;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.source_auto_deceleration_count==8u &&
            race.dependencies.source_auto_deceleration_table==storage->source_auto_deceleration);
        for(unsigned i=0u;i<8u;++i)
            CHECK(race.dependencies.source_auto_deceleration_table[i]==-5-(int)i*9);
        payload[motion.size]=1u;payload[motion.size+1u]=0u;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        payload[motion.size]=0xfbu;payload[motion.size+1u]=0xffu;
        --replacement->size;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        replacement->size=motion.size;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        /* Version8 owns every response coefficient and both signed-word
         * selectors; authored values prove decoding without donor ROM bytes. */
        uint8_t *response=payload+motion.size+16u;
        memset(response,0,224u);
        for(unsigned row=0u;row<8u;++row){
            for(unsigned column=0u;column<8u;++column){
                const uint16_t value=(uint16_t)(column==0u?-3:(int)(row+column+1u));
                response[(row*8u+column)*2u]=(uint8_t)value;
                response[(row*8u+column)*2u+1u]=(uint8_t)(value>>8u);
            }
        }
        for(unsigned i=0u;i<16u;++i)response[128u+i*4u]=2u;
        response[192u+3u*4u]=0xffu;
        response[193u+3u*4u]=response[194u+3u*4u]=response[195u+3u*4u]=0xffu;
        for(unsigned row=0u;row<8u;++row)for(unsigned i=0u;i<8u;++i)
            response[224u+row*8u+i]=(uint8_t)((row+i)&7u);
        memset(response+288u,0,124u);
        response[396u+1u]=2u;response[396u+15u]=5u;
        replacement->version=8u;replacement->size=motion.size+428u;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(race.dependencies.response_rules==&storage->driver_response_rules &&
            race.dependencies.response_rules->rows[7][7]==15 &&
            race.dependencies.response_rules->rows[0][0]==-3 &&
            race.dependencies.response_rules->selector_rows[15]==2 &&
            race.dependencies.response_rules->character_offsets[3]==-1);
        /* Custom manifest IDs retain their donor class only for imported
         * rival ordering; neither menu slots nor stock ID arithmetic is used. */
        {
            PortableAssetSectionView original;
            CHECK(portable_asset_pack_find_section(&layered,
                PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST, selection.character.id,
                &original) == PORTABLE_ASSET_PACK_OK);
            PortableAssetSectionView *roster_sections = calloc(layered.section_count + 8u,
                                                               sizeof *roster_sections);
            CHECK(roster_sections != NULL);
            uint32_t roster_count = 0;
            for (uint32_t i = 0; i < layered.section_count; ++i) {
                PortableAssetSectionView entry;
                CHECK(portable_asset_pack_section_at(&layered, i, &entry) == PORTABLE_ASSET_PACK_OK);
                if (entry.kind != PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST &&
                    entry.kind != PORTABLE_ASSET_SECTION_DRIVER_ROSTER)
                    roster_sections[roster_count++] = entry;
            }
            uint8_t manifests[8][24];
            for (unsigned c = 0; c < 8u; ++c) {
                const uint32_t id = c == 3u ? 200u : 1001u + c;
                memcpy(manifests[c], original.bytes, 24u);
                for (unsigned b = 0; b < 4u; ++b) {
                    manifests[c][4u+b] = (uint8_t)(id >> (8u*b));
                    manifests[c][20u+b] = (uint8_t)(c >> (8u*b));
                }
                roster_sections[roster_count++] = (PortableAssetSectionView){
                    .kind=PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST, .content_id=id,
                    .version=1u, .size=24u, .bytes=manifests[c]};
            }
            PortableAssetPackView rivals = layered;
            rivals.resolved_sections = roster_sections;
            rivals.section_count = roster_count;
            uint32_t ids[8];
            CHECK(portable_recovered_pack_rival_driver_ids(&rivals, 200u, ids));
            const uint32_t expected[8] = {200u,1003u,1002u,1001u,1008u,1007u,1006u,1005u};
            CHECK(memcmp(ids, expected, sizeof ids) == 0);
            uint32_t before[8]; memcpy(before, ids, sizeof before);
            const uint8_t saved = response[224u+3u*8u];
            response[224u+3u*8u] = response[224u+3u*8u+1u];
            CHECK(!portable_recovered_pack_rival_driver_ids(&rivals, 200u, ids));
            CHECK(memcmp(ids, before, sizeof ids) == 0);
            response[224u+3u*8u] = saved;
            uint32_t motion_slot = 0;
            for (uint32_t i = 0; i < roster_count; ++i)
                if (roster_sections[i].kind == PORTABLE_ASSET_SECTION_MOTION_TABLES &&
                    roster_sections[i].content_id == 1u) motion_slot = i;
            --roster_sections[motion_slot].size;
            CHECK(!portable_recovered_pack_rival_driver_ids(&rivals, 200u, ids));
            CHECK(memcmp(ids, before, sizeof ids) == 0);
            ++roster_sections[motion_slot].size;
            free(roster_sections);
        }
        response[128u]=8u;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        response[128u]=2u;response[0]=1u;response[1]=0u;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        response[0]=0xfdu;response[1]=0xffu;--replacement->size;
        CHECK(portable_recovered_pack_open_race(&layered,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        free(sections);free(payload);
        CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    }
    uint8_t *engine_base=(uint8_t *)motion.bytes+motion.size-36u;
    uint8_t saved_base[4];
    for (unsigned i=0u;i<4u;++i) {saved_base[i]=engine_base[i];engine_base[i]=0u;}
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
    for (unsigned i=0u;i<4u;++i) engine_base[i]=saved_base[i];
    CHECK(portable_recovered_pack_open_race(&pack,&selection,storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    free(presentation);free(storage);free(bytes);puts("portable recovered pack: PASS");return 0;
}
