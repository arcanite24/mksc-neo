#include "gp_opening_renderer.h"
#include "gp_opening_veil.h"

static uint16_t le16(const uint8_t *p)
{return (uint16_t)((unsigned)p[0]|(unsigned)p[1]<<8u);}

static uint32_t panel_pixel(const PortableGpOpeningAssetView *asset,
    unsigned engine_class,unsigned x,unsigned y,unsigned scroll)
{
    const unsigned source_x=(x+scroll)&511u;
    if(source_x<256u)return 0u;
    const unsigned entry=le16(asset->tilemap+2u*((y/8u)*32u+source_x/8u-32u));
    const unsigned tile=entry&1023u;
    if(tile==0u||tile>=PORTABLE_GP_OPENING_TILE_BYTES/32u)return 0u;
    const unsigned sx=entry&0x400u?7u-(source_x&7u):source_x&7u;
    const unsigned sy=entry&0x800u?7u-(y&7u):y&7u;
    const uint8_t *pixels=tile>=1u&&tile<=6u?
        asset->class_tiles+engine_class*PORTABLE_GP_OPENING_CLASS_BYTES+(tile-1u)*32u:
        asset->tiles+tile*32u;
    const unsigned index=(pixels[sy*4u+sx/2u]>>(4u*(sx&1u)))&15u;
    if(index==0u)return 0u;
    const unsigned color=le16(asset->palette+index*2u);
    uint32_t result=UINT32_C(0xff000000);
    for(unsigned channel=0u;channel<3u;++channel){
        const unsigned value=(color>>(channel*5u))&31u;
        result|=((value<<3u)|(value>>2u))<<(16u-channel*8u);
    }
    return result;
}

int portable_gp_opening_render(const PortableGpOpeningAssetView *asset,
    const PortableGpEntrySnapshot *entry,uint8_t engine_class,PortableSoftwareFrame *frame)
{
    if(!asset||!entry||!frame||!frame->pixels||!asset->tiles||!asset->tilemap||
       !asset->palette||!asset->class_tiles||engine_class>=asset->class_count||
       entry->panel_scroll>512u||entry->veil_phase>11u||
       entry->window_top>entry->window_bottom||entry->window_bottom>160u)return 0;
    const size_t scale=portable_software_frame_scale(frame);
    for(size_t y=0u;y<frame->height*scale;++y){
        const unsigned row=(unsigned)(y/scale);
        if(row<entry->window_top||row>=entry->window_bottom)
            for(size_t x=0u;x<frame->width*scale;++x)
                portable_software_frame_store_physical(frame,x,y,UINT32_C(0xff000000));
    }
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    if(entry->show_panel){
        for(unsigned y=0u;y<160u&&y<centered.height;++y)for(unsigned x=0u;x<240u&&x<centered.width;++x){
            const unsigned scroll=y<40u?entry->panel_scroll:512u-entry->panel_scroll;
            const uint32_t pixel=panel_pixel(asset,engine_class,x,y,scroll);
            if(pixel)portable_software_frame_store(&centered,(size_t)y*centered.width+x,pixel);
        }
    }
    for(size_t y=0u;y<frame->height*scale;++y)
        if(portable_gp_opening_veil_row_hidden(entry->veil_phase,(unsigned)(y/scale)))
            for(size_t x=0u;x<frame->width*scale;++x)
                portable_software_frame_store_physical(frame,x,y,UINT32_C(0xff000000));
    return 1;
}
