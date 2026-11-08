#include "green_shell_renderer.h"

#include "race_item_cell_renderer.h"

int portable_green_shell_render(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y,
    uint8_t animation, uint16_t inverse)
{
    if (asset==NULL || asset->cells==NULL || asset->cell_bytes<304u || animation>=6u)return 0;
    const uint8_t *cell=asset->cells+PORTABLE_DRIFT_SPARK_CELL_BYTES+animation*8u;
    const uint16_t attr1=(uint16_t)((uint16_t)cell[4]|(uint16_t)cell[5]<<8u);
    const uint16_t attr2=(uint16_t)((uint16_t)cell[6]|(uint16_t)cell[7]<<8u);
    const int8_t offset=(int8_t)(cell[2]<128u?(int)cell[2]:(int)cell[2]-256);
    return portable_race_item_cell_render(asset,frame,x,y,
        (uint16_t)(attr2 & 0x3ffu),(uint8_t)(attr2>>12u),
        offset,(uint8_t)((attr1 & 0x1000u) != 0u),inverse);
}

int portable_green_shell_render_projected(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y,
    uint8_t animation, uint16_t depth)
{
    const uint16_t inverse = portable_race_item_inverse(depth);
    return inverse != 0u && portable_green_shell_render(asset,frame,x,y,animation,inverse);
}
