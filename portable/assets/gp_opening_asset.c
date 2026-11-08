#include "gp_opening_asset.h"

static uint32_t le32(const uint8_t *p)
{return p[0]|(uint32_t)p[1]<<8u|(uint32_t)p[2]<<16u|(uint32_t)p[3]<<24u;}

int portable_gp_opening_asset_open(const PortableAssetSectionView *section,
    PortableGpOpeningAssetView *out)
{
    if(!section||!out||!section->bytes||section->kind!=PORTABLE_ASSET_SECTION_GP_OPENING_TITLE||
       section->version!=27u||section->size!=PORTABLE_GP_OPENING_ASSET_BYTES||
       le32(section->bytes)!=UINT32_C(0x31544f47)||
       le32(section->bytes+4u)!=PORTABLE_GP_OPENING_TILE_BYTES)return 0;
    PortableGpOpeningAssetView view={0};
    view.tiles=section->bytes+8u;
    view.tilemap=view.tiles+PORTABLE_GP_OPENING_TILE_BYTES;
    view.palette=view.tilemap+PORTABLE_GP_OPENING_MAP_BYTES;
    view.class_tiles=view.palette+32u;
    view.class_count=PORTABLE_GP_OPENING_CLASS_COUNT;
    for(unsigned index=0u;index<PORTABLE_GP_OPENING_MAP_BYTES;index+=2u){
        unsigned cell=view.tilemap[index]|(unsigned)view.tilemap[index+1u]<<8u;
        if((cell&1023u)>=PORTABLE_GP_OPENING_TILE_BYTES/32u)return 0;
    }
    *out=view;return 1;
}
