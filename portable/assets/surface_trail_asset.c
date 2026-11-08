#include "surface_trail_asset.h"
#include <string.h>
static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
int portable_surface_trail_asset_open(const PortableAssetSectionView *section,PortableSurfaceTrailRules *out)
{
    if(!section || !out || !section->bytes || section->version!=1u || section->size!=280u ||
       u32(section->bytes)!=UINT32_C(0x314c5254) || u32(section->bytes+4u)!=256u ||
       section->bytes[277] || section->bytes[278] || section->bytes[279])return 0;
    PortableSurfaceTrailRules rules={0};
    memcpy(rules.material_kind,section->bytes+8u,256u);
    memcpy(rules.frame_cutoffs,section->bytes+264u,10u);
    rules.offroad_material_min=section->bytes[274];
    rules.suppressed_moving_material=section->bytes[275];
    rules.spray_material=section->bytes[276];
    if(!portable_surface_trail_rules_valid(&rules))return 0;
    *out=rules;return 1;
}
int portable_surface_trail_asset_load(const PortableAssetPackView *pack,
    PortableSurfaceTrailRules *storage,const PortableSurfaceTrailRules **out)
{
    if(!pack || !storage || !out)return 0;
    *out=NULL;PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_SURFACE_TRAIL_RULES,1u,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || !portable_surface_trail_asset_open(&section,storage))return 0;
    *out=storage;return 1;
}
