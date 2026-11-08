#include "race_particle_renderer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t tiles[32u * 12u] = {0};
    uint16_t palette[16] = {0};
    uint8_t cells[8u * 12u] = {0};
    PortableRaceParticleFrame frames[12];
    uint32_t pixels[16u * 16u];
    PortableSoftwareFrame output = {pixels, 16u, 16u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset = {
        tiles, sizeof tiles, palette, 16u, cells, sizeof cells,
        frames, 5u, 0x100u, 3u};

    tiles[0] = 1u;
    palette[1] = 0x001fu;
    for (size_t index = 0; index < 5u; ++index)
    {
        const size_t offset = index * 8u;
        cells[offset] = 1u;
        cells[offset + 7u] = 0x31u; /* palette 3, tile 0x100 */
        frames[index] = (PortableRaceParticleFrame){
            (uint32_t)offset, 8u, (uint16_t)(index == 0u ? 2u : 1u)};
    }
    memset(pixels, 0, sizeof pixels);
    if (!portable_race_particle_render(&asset, &output, 4, 5, 1u) ||
        pixels[5u * 16u + 4u] != 0u)
    {
        fprintf(stderr, "age 1 failed: %08x\n", pixels[5u * 16u + 4u]);
        return 1;
    }
    {
    int result = portable_race_particle_render(&asset, &output, 4, 5, 2u);
    if (!result ||
        pixels[5u * 16u + 4u] != 0xff0000u)
    {
        fprintf(stderr, "age 2 failed: result=%d pixel=%08x\n", result,
                pixels[5u * 16u + 4u]);
        return 1;
    }
    }
    memset(pixels, 0, sizeof pixels);
    if (!portable_race_particle_render(&asset, &output, 4, 5, 22u) ||
        pixels[5u * 16u + 4u] != 0u)
    {
        fprintf(stderr, "age 22 failed: %08x\n", pixels[5u * 16u + 4u]);
        return 1;
    }
    /* The BD34 sequence has twelve frames and exactly fifty display ticks.
     * Distinct tile colors make every duration+1 boundary observable. */
    asset.frame_count = 12u;
    for (size_t index = 0u; index < 12u; ++index)
    {
        const size_t offset = index * 8u;
        cells[offset] = 1u;
        cells[offset + 6u] = (uint8_t)index;
        cells[offset + 7u] = 0x31u;
        tiles[index * 32u] = (uint8_t)(index + 1u);
        palette[index + 1u] = (uint16_t)(index + 1u);
        frames[index] = (PortableRaceParticleFrame){
            (uint32_t)offset, 8u, (uint16_t)(index == 2u || index == 9u ? 4u : 3u)};
    }
    uint8_t age = 2u;
    for (size_t index = 0u; index < 12u; ++index)
        for (unsigned tick = 0u; tick <= frames[index].duration; ++tick, ++age)
        {
            memset(pixels, 0, sizeof pixels);
            if (!portable_race_particle_render(&asset, &output, 4, 5, age) ||
                pixels[5u * 16u + 4u] != ((index + 1u) * 255u / 31u << 16u))
                return 1;
        }
    memset(pixels, 0, sizeof pixels);
    if (age != 52u || !portable_race_particle_render(&asset, &output, 4, 5, age) ||
        pixels[5u * 16u + 4u] != 0u)
        return 1;
    /* Entry splash uses three nine-tick spans and inverse affine scale128.
     * A solid8x8 source cell becomes16x16 without changing asset bytes. */
    asset.frame_count=3u;
    memset(tiles,0x11,sizeof tiles);
    for(size_t index=0u;index<3u;++index){
        const size_t offset=index*8u;
        memset(cells+offset,0,8u);
        cells[offset]=1u;cells[offset+7u]=0x31u;
        frames[index]=(PortableRaceParticleFrame){(uint32_t)offset,8u,8u};
    }
    for(uint8_t splash_age=1u;splash_age<=29u;++splash_age){
        memset(pixels,0,sizeof pixels);
        if(!portable_race_particle_render_scaled(&asset,&output,0,0,splash_age,128u))return 1;
        const uint32_t expected=splash_age>=2u&&splash_age<=28u?(255u/31u<<16):0u;
        if(pixels[0]!=expected||pixels[255]!=expected)return 1;
    }
    memset(pixels,0,sizeof pixels);
    if(!portable_race_particle_render_scaled(&asset,&output,-8,-8,2u,128u)||
       pixels[0]!=(255u/31u<<16)||pixels[7u*16u+7u]==0u||pixels[8u*16u+8u]!=0u)return 1;
    if(portable_race_particle_render_scaled(&asset,&output,0,0,2u,0u))return 1;
    puts("portable race particles: PASS");
    return 0;
}
