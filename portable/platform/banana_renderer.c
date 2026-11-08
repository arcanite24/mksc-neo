#include "banana_renderer.h"
#include "race_item_cell_renderer.h"

int portable_banana_render(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint16_t inverse)
{
    /* One source cell at 7AC44: tile 138, bank 4, offset(-8,-13). */
    return portable_race_item_cell_render(asset,frame,x,y,0x138u,4u,-13,0u,inverse);
}

int portable_banana_render_projected(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint16_t depth)
{
    const uint16_t inverse = portable_race_item_inverse(depth);
    return inverse != 0u && portable_banana_render(asset,frame,x,y,inverse);
}
