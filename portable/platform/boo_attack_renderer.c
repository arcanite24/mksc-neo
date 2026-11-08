#include "boo_attack_renderer.h"
/* The two 32x32 cells _0807AC54/5C share tile190, palette7. Native
 * coordinates retain their (-16,-16) anchor; the second cell mirrors X. */
int portable_boo_attack_render(const PortableRaceParticleAssetView *a,
    PortableSoftwareFrame *f,int32_t x,int32_t y,uint8_t facing,uint8_t inverted)
{
    if(!a||!f||!f->pixels||!a->tiles||!a->palette||a->tile_base>0x190u||
       a->palette_base>7u||facing>1u||inverted>1u)return 0;
    const size_t base=(size_t)(0x190u-a->tile_base)*32u;
    const size_t palette=(size_t)(7u-a->palette_base)*16u;
    if(base+512u>a->tile_bytes||palette+16u>a->palette_count)return 0;
    for(unsigned row=0u;row<32u;++row)for(unsigned col=0u;col<32u;++col){
        const unsigned sx=facing?31u-col:col,sy=inverted?31u-row:row;
        const size_t offset=base+(sy/8u*4u+sx/8u)*32u+(sy&7u)*4u+(sx&7u)/2u;
        const unsigned color=a->tiles[offset]>>(4u*(sx&1u))&15u;
        const int32_t dx=x-16+(int32_t)col,dy=y-16+(int32_t)row;
        if(!color||dx<0||dy<0||dx>=(int32_t)f->width||dy>=(int32_t)f->height)continue;
        const uint16_t rgb=a->palette[palette+color];
        /* Depth-tested 3D world draws skip alpha-zero pixels; mark them opaque. */
        const uint32_t opaque=f->depth!=NULL&&f->depth->value!=0u?UINT32_C(0xff000000):0u;
        portable_software_frame_store(f,(size_t)dy*f->width+(size_t)dx, opaque |
            (uint32_t)(rgb&31u)*255u/31u<<16u |
            (uint32_t)((rgb>>5u)&31u)*255u/31u<<8u |
            (uint32_t)((rgb>>10u)&31u)*255u/31u);
    }
    return 1;
}
