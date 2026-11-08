#ifndef PORTABLE_DRIVING_PROFILE_H
#define PORTABLE_DRIVING_PROFILE_H
#include "asset_pack.h"
#include "modern_drift_asset.h"
/* Owned optional base layer. Create before user mods, which may override the
 * authored rules section. Original mode uses its original pack directly. */
typedef struct PortableDrivingProfile PortableDrivingProfile;
/* selection: 1 Modern drift, 2 Modern with air tricks. */
PortableDrivingProfile *portable_driving_profile_create(const PortableAssetPackView *base,uint8_t selection);
PortableDrivingProfile *portable_driving_profile_create_modern(const PortableAssetPackView *base);
const PortableAssetPackView *portable_driving_profile_pack(const PortableDrivingProfile *profile);
void portable_driving_profile_free(PortableDrivingProfile *profile);
/* 1 modern, 0 original, -1 invalid selected profile. Does not publish globals. */
int portable_driving_profile_rules(const PortableAssetPackView *pack,PortableModernDriftRules *rules);
/* Publish only after the application commits its effective content. */
int portable_driving_profile_publish(const PortableAssetPackView *pack);
/* Pure candidate save-domain lookup; never publishes global identity. */
int portable_driving_profile_progress_suffix(const PortableAssetPackView *pack,
    uint8_t mirror,char suffix[48]);
#endif
