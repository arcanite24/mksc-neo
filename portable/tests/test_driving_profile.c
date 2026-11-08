#include "driving_profile.h"
#include "mod_identity.h"
#include "gp_progress_store.h"

#include <stdio.h>
#include <string.h>

_Static_assert(PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES == 95u, "stable drift section kind");

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"driving profile line %d: %s\n",__LINE__,#condition);return 1; \
} } while(0)

static void put_u16(uint8_t *bytes,uint16_t value)
{
    bytes[0]=(uint8_t)value;bytes[1]=(uint8_t)(value>>8u);
}
static void put_u32(uint8_t *bytes,uint32_t value)
{
    put_u16(bytes,(uint16_t)value);put_u16(bytes+2u,(uint16_t)(value>>16u));
}

static int decoder_checks(const PortableAssetSectionView *valid)
{
    PortableModernDriftRules rules;
    memset(&rules,0xa5,sizeof rules);
    unsigned char before[sizeof rules];memcpy(before,&rules,sizeof rules);
    uint8_t payload[48];
    for(unsigned test=0u;test<22u;++test){
        memcpy(payload,valid->bytes,sizeof payload);
        PortableAssetSectionView section=*valid;section.bytes=payload;
        switch(test){
        case 0u:section.kind=PORTABLE_ASSET_SECTION_HUD_GRAPHICS;break;
        case 1u:section.content_id=2u;break;
        case 2u:section.version=2u;break;
        case 3u:section.size=47u;break;
        case 4u:section.size=49u;break;
        case 5u:section.bytes=NULL;break;
        case 6u:payload[0]^=1u;break;
        case 7u:put_u32(payload+44u,1u);break;
        case 8u:put_u32(payload+4u,0u);break;
        case 9u:put_u32(payload+8u,0u);break;
        case 10u:put_u16(payload+12u,0u);break;
        case 11u:memcpy(payload+14u,payload+12u,2u);break;
        case 12u:put_u16(payload+16u,8193u);break;
        case 13u:put_u16(payload+18u,0u);break;
        case 14u:put_u16(payload+24u,255u);break;
        case 15u:put_u16(payload+30u,257u);break;
        case 16u:put_u32(payload+32u,UINT32_C(0xffffffff));break;
        case 17u:put_u32(payload+40u,128u*65536u+1u);break;
        case 18u:put_u16(payload+22u,8193u);break;
        case 19u:put_u16(payload+28u,1025u);break;
        case 20u:put_u16(payload+30u,0u);break;
        default:put_u32(payload+36u,0u);break;
        }
        CHECK(!portable_modern_drift_asset_open(&section,&rules));
        CHECK(memcmp(before,&rules,sizeof rules)==0);
    }
    CHECK(!portable_modern_drift_asset_open(NULL,&rules));
    CHECK(!portable_modern_drift_asset_open(valid,NULL));
    CHECK(portable_modern_drift_asset_open(valid,&rules));
    return 0;
}

static int namespace_checks(const PortableAssetPackView *original,
    const PortableAssetPackView *modern,const PortableAssetPackView *custom)
{
    portable_mod_identity_set_current(NULL);portable_mod_identity_set_mirror(0u);
    CHECK(portable_driving_profile_publish(original));
    CHECK(strcmp(portable_mod_identity_record_suffix(),"")==0&&
        strcmp(portable_mod_identity_progress_suffix(),"")==0);
    CHECK(portable_driving_profile_publish(modern));
    char modern_record[PORTABLE_RECORD_SUFFIX_CAPACITY],modern_progress[PORTABLE_RECORD_SUFFIX_CAPACITY];
    snprintf(modern_record,sizeof modern_record,"%s",portable_mod_identity_record_suffix());
    snprintf(modern_progress,sizeof modern_progress,"%s",portable_mod_identity_progress_suffix());
    CHECK(strncmp(modern_record,"-modern-",8u)==0&&strlen(modern_record)==24u&&
        strcmp(modern_record,modern_progress)==0);
    char candidate_suffix[48];
    CHECK(portable_driving_profile_progress_suffix(custom,1u,candidate_suffix));
    CHECK(strncmp(candidate_suffix,"-mirror-modern-",15u)==0);
    CHECK(strcmp(modern_progress,portable_mod_identity_progress_suffix())==0);
    CHECK(portable_driving_profile_progress_suffix(modern,0u,candidate_suffix)&&
        strcmp(candidate_suffix,modern_progress)==0);
    CHECK(portable_driving_profile_progress_suffix(original,1u,candidate_suffix)&&
        strcmp(candidate_suffix,"-mirror")==0);
    CHECK(!portable_driving_profile_progress_suffix(original,2u,candidate_suffix)&&
        strcmp(candidate_suffix,"-mirror")==0);
    CHECK(portable_driving_profile_publish(custom));
    CHECK(strcmp(portable_mod_identity_record_suffix(),modern_record)!=0&&
        strcmp(portable_mod_identity_progress_suffix(),modern_progress)!=0);
    CHECK(portable_driving_profile_publish(original));
    CHECK(portable_mod_identity_record_suffix()[0]=='\0'&&portable_mod_identity_progress_suffix()[0]=='\0');

    CHECK(portable_driving_profile_publish(modern));portable_mod_identity_set_mirror(1u);
    char mirrored[PORTABLE_RECORD_SUFFIX_CAPACITY];
    snprintf(mirrored,sizeof mirrored,"%s",portable_mod_identity_progress_suffix());
    CHECK(strncmp(mirrored,"-mirror-modern-",15u)==0&&strcmp(mirrored,modern_progress)!=0);
    CHECK(strcmp(mirrored,portable_mod_identity_record_suffix())==0);
    CHECK(portable_driving_profile_publish(custom));
    char custom_progress[PORTABLE_RECORD_SUFFIX_CAPACITY];
    snprintf(custom_progress,sizeof custom_progress,"%s",portable_mod_identity_progress_suffix());
    PortableModIdentity mods={.api_version=PORTABLE_MOD_API_VERSION,.count=PORTABLE_MOD_LIMIT};
    for(unsigned i=0u;i<PORTABLE_MOD_LIMIT;++i){
        snprintf(mods.mods[i].id,sizeof mods.mods[i].id,"authored-%02u",i);
        snprintf(mods.mods[i].version,sizeof mods.mods[i].version,"1.0.0");
        for(unsigned byte=0u;byte<8u;++byte)mods.mods[i].fingerprint[byte]=(uint8_t)(i+byte);
    }
    char before[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY],after[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY];
    CHECK(portable_mod_identity_format(&mods,before,sizeof before));
    CHECK(strncmp(before,"MODS1 1 32 ",11u)==0);
    portable_mod_identity_set_current(&mods);
    CHECK(portable_mod_identity_current()->count==32u);
    CHECK(portable_driving_profile_publish(custom));
    CHECK(portable_mod_identity_format(portable_mod_identity_current(),after,sizeof after));
    CHECK(strcmp(before,after)==0); /* A profile consumes no mod identity slots or wire fields. */
    /* GP progress and received-ghost slots retain their shared user-mod policy;
     * only profile/mirror separate those paths. Records additionally key mods. */
    CHECK(strcmp(custom_progress,portable_mod_identity_progress_suffix())==0);
    const char *modded=portable_mod_identity_record_suffix();
    CHECK(strncmp(modded,"-mirror-modern-",15u)==0&&strstr(modded,"-mods-")!=NULL&&
        strcmp(modded,mirrored)!=0);
    CHECK(portable_driving_profile_publish(original));
    CHECK(strncmp(portable_mod_identity_record_suffix(),"-mirror-mods-",13u)==0);
    portable_mod_identity_set_current(NULL);
    CHECK(strcmp(portable_mod_identity_record_suffix(),"-mirror")==0&&
        strcmp(portable_mod_identity_progress_suffix(),"-mirror")==0);
    portable_mod_identity_set_mirror(0u);
    CHECK(portable_driving_profile_publish(original));
    CHECK(portable_mod_identity_record_suffix()[0]=='\0'&&portable_mod_identity_progress_suffix()[0]=='\0');
    return 0;
}

/* These modes author only isolated test files; profile suffixes come from the
 * same pure content helper used by candidate validation. */
static int progress_fixture(const char *root,int corrupt)
{
    char path[4096];
    if(corrupt){
        const uint8_t byte=0u;
        const PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_HUD_GRAPHICS,
            .content_id=1u,.version=1u,.bytes=&byte,.size=1u};
        const PortableAssetPackView base={.resolved_sections=&section,.section_count=1u};
        PortableDrivingProfile *profile=portable_driving_profile_create_modern(&base);
        CHECK(profile!=NULL);char suffix[48];
        CHECK(portable_driving_profile_progress_suffix(portable_driving_profile_pack(profile),0u,suffix));
        int length=snprintf(path,sizeof path,"%s/progress.bin%s",root,suffix);
        CHECK(length>0&&(size_t)length<sizeof path);
        FILE *file=fopen(path,"wb");CHECK(file!=NULL);
        CHECK(fputs("invalid target GP progress\n",file)>=0&&fclose(file)==0);
        portable_driving_profile_free(profile);
        puts("driving profile fixture: PASS corrupt modern target authored");
        return 0;
    }
    int length=snprintf(path,sizeof path,"%s/data/settings.json",root);
    CHECK(length>0&&(size_t)length<sizeof path);
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    CHECK(fputs("{\"version\":1,\"mods\":[]}\n",file)>=0&&fclose(file)==0);
    portable_mod_identity_set_current(NULL);portable_mod_identity_set_mirror(0u);
    portable_mod_identity_set_driving_profile(NULL);
    PortableGpProgress progress;portable_gp_progress_init(&progress);
    progress.grade[0][0][0]=2; /* Nonempty stock progress must survive both switches. */
    length=snprintf(path,sizeof path,"%s/progress.bin",root);
    CHECK(length>0&&(size_t)length<sizeof path);
    CHECK(portable_gp_progress_save(path,&progress)==PORTABLE_GP_PROGRESS_OK);
    file=fopen(path,"rb");CHECK(file!=NULL);
    char snapshot[4096];length=snprintf(snapshot,sizeof snapshot,"%s/stock-before.bin",root);
    CHECK(length>0&&(size_t)length<sizeof snapshot);
    FILE *copy=fopen(snapshot,"wb");CHECK(copy!=NULL);
    int value;while((value=fgetc(file))!=EOF)CHECK(fputc(value,copy)!=EOF);
    CHECK(!ferror(file)&&fclose(file)==0&&fclose(copy)==0);
    puts("driving profile fixture: PASS empty settings and authored stock progress");
    return 0;
}

static int capture_checks(const char *directory)
{
    const char *modes[]={"tt","gp"},*stages[]={"charge","boost"};
    for(unsigned mode=0u;mode<2u;++mode)for(unsigned stage=0u;stage<2u;++stage)
        for(unsigned tier=1u;tier<=3u;++tier){
            char path[4096];int length=snprintf(path,sizeof path,"%s/%s-drift-%s-%u.ppm",
                directory,modes[mode],stages[stage],tier);
            CHECK(length>0&&(size_t)length<sizeof path);
            FILE *file=fopen(path,"rb");CHECK(file!=NULL);
            static const char header[]="P6\n240 160\n255\n";
            char actual[sizeof header-1u];
            CHECK(fread(actual,1u,sizeof actual,file)==sizeof actual&&
                  memcmp(actual,header,sizeof actual)==0);
            unsigned colored=0u;
            for(size_t byte=0u;byte<240u*160u*3u;++byte){
                const int value=fgetc(file);CHECK(value!=EOF);colored|=(unsigned)value;
            }
            CHECK(colored!=0u&&fgetc(file)==EOF&&!ferror(file)&&fclose(file)==0);
        }
    puts("modern drift captures: PASS all TT/GP charge and boost tiers 1/2/3 valid native frames");
    return 0;
}

static int air_capture_checks(const char *directory)
{
    const char *modes[]={"tt","gp"},*stages[]={"ready","trick","boost"};
    for(unsigned mode=0u;mode<2u;++mode)for(unsigned stage=0u;stage<3u;++stage){
        char path[4096];int length=snprintf(path,sizeof path,"%s/%s-air-%s.ppm",directory,modes[mode],stages[stage]);
        CHECK(length>0 && (size_t)length<sizeof path);
        FILE *file=fopen(path,"rb");CHECK(file!=NULL);
        const char header[]="P6\n240 160\n255\n";char actual[sizeof header-1u];
        CHECK(fread(actual,1u,sizeof actual,file)==sizeof actual && !memcmp(actual,header,sizeof actual));
        unsigned colored=0u;
        for(size_t n=0u;n<240u*160u*3u;++n){const int byte=fgetc(file);CHECK(byte!=EOF);colored|=(unsigned)byte;}
        CHECK(colored && fgetc(file)==EOF && !ferror(file) && fclose(file)==0);
    }
    puts("modern air captures: PASS TT/GP ready, trick and landing boost");return 0;
}

int main(int argc,char **argv)
{
    if(argc==3 && strcmp(argv[1],"--expect-air-captures")==0)return air_capture_checks(argv[2]);
    if(argc==3 && strcmp(argv[1],"--expect-captures")==0)return capture_checks(argv[2]);
    if(argc==3 && strcmp(argv[1],"--fixture")==0)return progress_fixture(argv[2],0);
    if(argc==3 && strcmp(argv[1],"--corrupt-target")==0)return progress_fixture(argv[2],1);
    CHECK(argc==1);
    const uint8_t payload=0u;
    const PortableAssetSectionView hud={.kind=PORTABLE_ASSET_SECTION_HUD_GRAPHICS,
        .content_id=1u,.version=1u,.flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,
        .semantic_revision=1u,.bytes=&payload,.size=1u};
    PortableAssetPackView original={.resolved_sections=&hud,.section_count=1u};
    unsigned char before[sizeof original];memcpy(before,&original,sizeof original);
    PortableModernDriftRules rules;memset(&rules,0xa5,sizeof rules);
    unsigned char rules_before[sizeof rules];memcpy(rules_before,&rules,sizeof rules);
    CHECK(portable_driving_profile_rules(&original,&rules)==0&&
        memcmp(rules_before,&rules,sizeof rules)==0);
    PortableDrivingProfile *profile=portable_driving_profile_create_modern(&original);
    PortableDrivingProfile *repeated=portable_driving_profile_create_modern(&original);
    CHECK(profile!=NULL&&repeated!=NULL);
    CHECK(memcmp(before,&original,sizeof original)==0&&payload==0u);
    const PortableAssetPackView *modern=portable_driving_profile_pack(profile);
    CHECK(original.driving_profile==0u&&modern->driving_profile==1u&&modern->section_count==2u);
    CHECK(modern==portable_driving_profile_pack(profile)&&
        memcmp(modern->deterministic_digest,portable_driving_profile_pack(repeated)->deterministic_digest,32u)==0&&
        memcmp(modern->pack_digest,portable_driving_profile_pack(repeated)->pack_digest,32u)==0);
    CHECK(memcmp(original.deterministic_digest,modern->deterministic_digest,32u)!=0);
    PortableAssetSectionView section,retained;
    CHECK(portable_asset_pack_find_section(modern,95u,1u,&section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.version==1u&&section.size==48u&&(section.flags&PORTABLE_ASSET_SECTION_DETERMINISTIC));
    CHECK(portable_asset_pack_find_section(modern,hud.kind,1u,&retained)==PORTABLE_ASSET_PACK_OK&&
        retained.bytes==hud.bytes&&retained.size==hud.size&&retained.bytes[0]==0u);
    CHECK(portable_driving_profile_rules(modern,&rules)==1&&rules.profile_id==UINT32_C(1296302081)&&
        rules.revision==1u&&rules.charge_ticks[0]==30u&&rules.charge_ticks[1]==90u&&rules.charge_ticks[2]==180u&&
        rules.boost_ticks[0]==20u&&rules.boost_ticks[1]==40u&&rules.boost_ticks[2]==60u&&
        rules.cap_ratio_256[0]==288u&&rules.cap_ratio_256[1]==304u&&rules.cap_ratio_256[2]==320u&&
        rules.minimum_speed_ratio_256==128u&&rules.acceleration_q16[0]==196608&&
        rules.acceleration_q16[1]==262144&&rules.acceleration_q16[2]==327680);
    CHECK(decoder_checks(&section)==0);
    CHECK(rules.air.launch_mask==0u);
    PortableDrivingProfile *air_owner=portable_driving_profile_create(&original,2u);
    CHECK(air_owner!=NULL);
    const PortableAssetPackView *air=portable_driving_profile_pack(air_owner);
    PortableAssetSectionView air_section;
    CHECK(air->driving_profile==2u && portable_driving_profile_rules(air,&rules)==1 &&
        rules.air.launch_mask==3u && rules.air.minimum_air_ticks==2u &&
        rules.air.trick_window_ticks==45u && rules.air.boost_ticks==30u && rules.revision==2u);
    CHECK(portable_asset_pack_find_section(air,95u,1u,&air_section)==PORTABLE_ASSET_PACK_OK &&
        air_section.version==2u && air_section.size==64u);
    char modern_suffix[48],air_suffix[48];
    CHECK(portable_driving_profile_progress_suffix(modern,0u,modern_suffix) &&
        portable_driving_profile_progress_suffix(air,0u,air_suffix) && strcmp(modern_suffix,air_suffix)!=0);
    for(unsigned fault=0u;fault<11u;++fault){
        uint8_t bad[64];memcpy(bad,air_section.bytes,sizeof bad);
        PortableAssetSectionView damaged=air_section;damaged.bytes=bad;
        switch(fault){
        case 0u:damaged.size=63u;break;
        case 1u:damaged.version=1u;break;
        case 2u:put_u16(bad+48u,0u);break;
        case 3u:put_u16(bad+50u,1u);break;
        case 4u:put_u16(bad+52u,8193u);break;
        case 5u:put_u16(bad+54u,255u);break;
        case 6u:put_u32(bad+56u,UINT32_MAX);break;
        case 7u:put_u16(bad+60u,0u);break;
        case 8u:bad[62u]=0u;break;
        case 9u:bad[62u]=4u;break;
        default:bad[63u]=1u;break;
        }
        PortableModernDriftRules untouched=rules;
        CHECK(!portable_modern_drift_asset_open(&damaged,&rules) && !memcmp(&untouched,&rules,sizeof rules));
    }
    const PortableAssetPackView old_override={.resolved_sections=&section,.section_count=1u};
    const PortableAssetPackView *air_layers[]={air,&old_override};
    PortableAssetSectionView air_storage[2];PortableAssetPackView downgraded;
    CHECK(portable_asset_pack_compose(air_layers,2u,air_storage,2u,&downgraded)==PORTABLE_ASSET_PACK_OK);
    PortableModernDriftRules unchanged=rules;
    CHECK(portable_driving_profile_rules(&downgraded,&rules)==-1 && !memcmp(&rules,&unchanged,sizeof rules));
    CHECK(portable_driving_profile_create(&original,0u)==NULL && portable_driving_profile_create(&original,3u)==NULL);
    portable_driving_profile_free(air_owner);
    uint8_t custom_bytes[48];memcpy(custom_bytes,section.bytes,sizeof custom_bytes);
    put_u32(custom_bytes+8u,2u);put_u16(custom_bytes+12u,31u);
    PortableAssetSectionView override=section;override.bytes=custom_bytes;
    const PortableAssetPackView layer={.resolved_sections=&override,.section_count=1u};
    const PortableAssetPackView *layers[]={modern,&layer};
    PortableAssetSectionView storage[2];PortableAssetPackView custom;
    CHECK(portable_asset_pack_compose(layers,2u,storage,2u,&custom)==PORTABLE_ASSET_PACK_OK);
    CHECK(custom.driving_profile==1u&&portable_driving_profile_rules(&custom,&rules)==1&&
        rules.revision==2u&&rules.charge_ticks[0]==31u);
    CHECK(memcmp(custom.deterministic_digest,modern->deterministic_digest,32u)!=0);
    const uint8_t art_byte=1u;
    const PortableAssetSectionView art={.kind=PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
        .content_id=77u,.version=1u,.bytes=&art_byte,.size=1u};
    const PortableAssetPackView art_layer={.resolved_sections=&art,.section_count=1u};
    const PortableAssetPackView *art_layers[]={modern,&art_layer};
    PortableAssetSectionView art_storage[3];PortableAssetPackView decorated;
    CHECK(portable_asset_pack_compose(art_layers,2u,art_storage,3u,&decorated)==PORTABLE_ASSET_PACK_OK);
    CHECK(decorated.driving_profile==1u&&portable_driving_profile_rules(&decorated,&rules)==1&&
        memcmp(decorated.deterministic_digest,modern->deterministic_digest,32u)==0);
    const PortableAssetPackView *original_layers[]={&original,&layer};
    PortableAssetSectionView original_storage[2];PortableAssetPackView still_original;
    CHECK(portable_asset_pack_compose(original_layers,2u,original_storage,2u,&still_original)==PORTABLE_ASSET_PACK_OK);
    memcpy(rules_before,&rules,sizeof rules);
    CHECK(still_original.driving_profile==0u&&portable_driving_profile_rules(&still_original,&rules)==0&&
        memcmp(rules_before,&rules,sizeof rules)==0); /* Content presence never selects the profile. */
    CHECK(namespace_checks(&original,modern,&custom)==0);
    CHECK(portable_driving_profile_create_modern(modern)==NULL&&
        portable_driving_profile_create_modern(NULL)==NULL&&portable_driving_profile_pack(NULL)==NULL);
    portable_driving_profile_free(repeated);portable_driving_profile_free(profile);
    CHECK(memcmp(before,&original,sizeof original)==0);
    puts("portable driving profile: PASS");return 0;
}
