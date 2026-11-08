#include "driving_profile.h"
#include "mod_identity.h"
#include "modern_drift_payload.h"
#include "modern_air_payload.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct PortableDrivingProfile {
    PortableAssetPackView pack;
    PortableAssetSectionView sections[4096];
};

PortableDrivingProfile *portable_driving_profile_create(const PortableAssetPackView *base,uint8_t selection)
{
    if(base==NULL || base->driving_profile!=0u || selection<1u || selection>2u)return NULL;
    PortableDrivingProfile *profile=calloc(1u,sizeof *profile);
    if(profile==NULL)return NULL;
    const PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES,
        .content_id=1u,.version=selection,.flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,
        .semantic_revision=selection,
        .bytes=selection==2u?portable_modern_air_payload:portable_modern_drift_payload,
        .size=selection==2u?sizeof portable_modern_air_payload:sizeof portable_modern_drift_payload};
    const PortableAssetPackView builtin={.resolved_sections=&section,.section_count=1u};
    const PortableAssetPackView *layers[]={base,&builtin};
    if(portable_asset_pack_compose(layers,2u,profile->sections,4096u,&profile->pack)!=PORTABLE_ASSET_PACK_OK){
        free(profile);return NULL;
    }
    profile->pack.driving_profile=selection;
    PortableModernDriftRules rules;
    if(portable_driving_profile_rules(&profile->pack,&rules)!=1){free(profile);return NULL;}
    return profile;
}

PortableDrivingProfile *portable_driving_profile_create_modern(const PortableAssetPackView *base)
{return portable_driving_profile_create(base,1u);}

const PortableAssetPackView *portable_driving_profile_pack(const PortableDrivingProfile *profile)
{return profile!=NULL?&profile->pack:NULL;}
void portable_driving_profile_free(PortableDrivingProfile *profile){free(profile);}

int portable_driving_profile_rules(const PortableAssetPackView *pack,PortableModernDriftRules *rules)
{
    if(pack==NULL || rules==NULL)return -1;
    if(pack->driving_profile==0u)return 0;
    if(pack->driving_profile>2u)return -1;
    PortableAssetSectionView section;
    PortableModernDriftRules decoded;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES,1u,&section)!=PORTABLE_ASSET_PACK_OK ||
        !portable_modern_drift_asset_open(&section,&decoded) ||
        (pack->driving_profile==2u && decoded.air.launch_mask==0u))return -1;
    *rules=decoded;return 1;
}

int portable_driving_profile_publish(const PortableAssetPackView *pack)
{
    PortableModernDriftRules rules;
    const int active=portable_driving_profile_rules(pack,&rules);
    if(active<0)return 0;
    if(!active){portable_mod_identity_set_driving_profile(NULL);return 1;}
    PortableAssetSectionView section;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES,1u,&section)!=PORTABLE_ASSET_PACK_OK)return 0;
    uint8_t digest[32];
    portable_asset_pack_sha256(&section.bytes,&section.size,1u,digest);
    portable_mod_identity_set_driving_profile(digest);
    return 1;
}

int portable_driving_profile_progress_suffix(const PortableAssetPackView *pack,
    uint8_t mirror,char suffix[48])
{
    if(suffix==NULL || mirror>1u)return 0;
    PortableModernDriftRules rules;
    const int active=portable_driving_profile_rules(pack,&rules);
    if(active<0)return 0;
    char next[48];
    const char *prefix=mirror?"-mirror":"";
    if(!active)snprintf(next,sizeof next,"%s",prefix);
    else{
        PortableAssetSectionView section;
        if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES,1u,&section)!=PORTABLE_ASSET_PACK_OK)return 0;
        uint8_t digest[32];
        portable_asset_pack_sha256(&section.bytes,&section.size,1u,digest);
        const char hex[]="0123456789abcdef";
        char fingerprint[17];
        for(unsigned i=0u;i<8u;++i){fingerprint[i*2u]=hex[digest[i]>>4u];fingerprint[i*2u+1u]=hex[digest[i]&15u];}
        fingerprint[16]='\0';
        snprintf(next,sizeof next,"%s-modern-%s",prefix,fingerprint);
    }
    memcpy(suffix,next,strlen(next)+1u);
    return 1;
}
