#include "test_support.h"

static int sky_fractional_pixels(void)
{
    uint8_t tiles[64]={0},map[2048]={0};
    for(unsigned y=0u;y<8u;++y)for(unsigned x=0u;x<8u;++x){
        const uint8_t color=x>=4u?2u:y>=4u?3u:1u;
        tiles[y*4u+x/2u]|=(uint8_t)(color<<((x&1u)*4u));
    }
    const uint16_t palette[4]={0u,0x001fu,0x7c00u,0x03e0u};
    const PortableSkyAssetView sky={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=4u,.front_map=map,.front_map_bytes=sizeof map,
        .front_width_tiles=32u,.mid_map=map,.mid_map_bytes=sizeof map,.mid_width_tiles=32u,
        .back_map=map,.back_map_bytes=sizeof map,.back_width_tiles=32u,.map_height_tiles=32u};
    PortableSkyMotionSnapshot motion={.initialized=1u,.precise=1u,.layer_mask=4u,
        .layer_source={0u,1u,2u},.split_y=-1,.road_line=74};
    uint32_t pixels[35u*32u],baseline[35u*32u],native[64],native_before[64];
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u,.scale=4u,.stride=35u};
    PortableSoftwareFrame one={.pixels=native,.width=8u,.height=8u};
    for(size_t i=0u;i<35u*32u;++i)pixels[i]=0xdeadbeefu;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||
       !portable_sky_render_motion_back(&sky,&one,&motion))return 1;
    memcpy(baseline,pixels,sizeof baseline);memcpy(native_before,native,sizeof native);
    for(unsigned axis=0u;axis<2u;++axis)for(unsigned direction=0u;direction<2u;++direction){
        motion.scroll_x_q16[2]=axis==0u?(direction?-16384:16384):0;
        motion.scroll_y_q16[2]=axis==1u?(direction?-16384:16384):0;
        const PortableSkyMotionSnapshot immutable=motion;
        if(!portable_sky_render_motion_back(&sky,&frame,&motion)||
           !portable_sky_render_motion_back(&sky,&one,&motion)||
           memcmp(native,native_before,sizeof native)||memcmp(&motion,&immutable,sizeof motion))return 1;
        unsigned changed=0u;
        for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<35u;++x){
            const unsigned sx=axis==0u?(x+(direction?31u:1u))%32u:x;
            const unsigned sy=axis==1u?(y+(direction?31u:1u))%32u:y;
            if(pixels[y*35u+x]!=(x<32u?baseline[sy*35u+sx]:0xdeadbeefu))return 1;
            if(x<32u)changed+=pixels[y*35u+x]!=baseline[y*35u+x];
        }
        if(changed==0u)return 1;
    }
    /* A horizontal split moves only the lower band, including wrapped X. */
    motion.scroll_x_q16[2]=motion.scroll_y_q16[2]=0;
    motion.split_y=4;motion.split_layer=2u;motion.split_scroll_x_q16=255*65536+49152;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion))return 1;
    for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<32u;++x)
        if(pixels[y*35u+x]!=baseline[y*35u+(y>=16u?(x+31u)%32u:x)])return 1;
    motion.split_vertical=1u;motion.split_scroll_y_q16=16384;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion))return 1;
    for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<32u;++x)
        if(pixels[y*35u+x]!=baseline[(y>=16u?(y+1u)%32u:y)*35u+x])return 1;
    /* Fractional front placement blends each destination sample independently. */
    motion.layer_mask=1u;motion.split_y=-1;motion.scroll_x_q16[0]=16384;
    motion.blend_mode=PORTABLE_SKY_BLEND_ALPHA;motion.blend_a=motion.blend_b=8u;
    for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<32u;++x)pixels[y*35u+x]=0x0000ffu;
    if(!portable_sky_render_motion_front(&sky,&frame,&motion)||pixels[14u]!=0x7f007fu||
        pixels[15u]!=0x0000ffu||pixels[32u]!=0xdeadbeefu)return 1;
    /* SKY1v1 remains reachable through custom packs and finish-camera yaw.
     * Check each raw-yaw wrapper below its old one-logical-pixel boundary. */
    uint8_t transparent_map[2048]={0};
    for(unsigned i=0u;i<1024u;++i)transparent_map[i*2u]=1u;
    PortableSkyAssetView legacy=sky;
    for(unsigned kind=0u;kind<3u;++kind){
        legacy.mid_map=kind==0u?transparent_map:map;
        const int16_t yaw=kind==0u?64:kind==1u?32:16;
        for(unsigned step=0u;step<2u;++step){
            for(size_t i=0u;i<35u*32u;++i)pixels[i]=0xdeadbeefu;
            const int16_t angle=step?yaw:0;
            int drawn,native_drawn;
            if(kind==0u){
                drawn=portable_sky_render_race_back(&legacy,&frame,angle);
                native_drawn=portable_sky_render_race_back(&legacy,&one,angle);
            }else if(kind==1u){
                drawn=portable_sky_render_middle_yaw(&legacy,&frame,angle);
                native_drawn=portable_sky_render_middle_yaw(&legacy,&one,angle);
            }else{
                drawn=portable_sky_render_race_front(&legacy,&frame,angle);
                native_drawn=portable_sky_render_race_front(&legacy,&one,angle);
            }
            if(!drawn||!native_drawn)return 1;
            if(!step){memcpy(baseline,pixels,sizeof baseline);memcpy(native_before,native,sizeof native);}
            else{
                if(memcmp(native,native_before,sizeof native))return 1;
                unsigned changed=0u;
                for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<35u;++x){
                    if(pixels[y*35u+x]!=(x<32u?baseline[y*35u+(x+1u)%32u]:0xdeadbeefu))return 1;
                    if(x<32u)changed+=pixels[y*35u+x]!=baseline[y*35u+x];
                }
                if(!changed)return 1;
            }
        }
    }
    return 0;
}

static int sky_precise_tile_rows(void)
{
    uint8_t tiles[32]={0},map[4096]={0};
    uint16_t palette[32];
    for(unsigned i=0u;i<32u;++i)palette[i]=(uint16_t)((i&31u)|((i*3u&31u)<<5u)|((i*7u&31u)<<10u));
    for(unsigned y=0u;y<8u;++y)for(unsigned x=0u;x<8u;++x){
        const unsigned color=(x+3u*y)%16u;
        tiles[y*4u+x/2u]|=(uint8_t)(color<<((x&1u)*4u));
    }
    for(unsigned i=0u;i<2048u;++i){
        const uint16_t entry=(uint16_t)((i&1u?0x0400u:0u)|((i/32u)&1u?0x0800u:0u)|
            (i>=1024u?0x1000u:0u));
        map[i*2u]=(uint8_t)entry;map[i*2u+1u]=(uint8_t)(entry>>8u);
    }
    const PortableSkyAssetView sky={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=32u,.front_map=map,.front_map_bytes=sizeof map,
        .front_width_tiles=64u,.mid_map=map,.mid_map_bytes=sizeof map,.mid_width_tiles=64u,
        .back_map=map,.back_map_bytes=sizeof map,.back_width_tiles=64u,.map_height_tiles=32u};
    PortableSkyMotionSnapshot motion={.initialized=1u,.layer_mask=4u,.split_y=-1,
        .road_line=74,.layer_source={0u,1u,2u},.scroll_x={0,0,252},.scroll_y={0,0,7}};
    uint32_t reference[17u*9u],pixels[67u*32u];
    PortableSoftwareFrame one={.pixels=reference,.width=17u,.height=9u};
    PortableSoftwareFrame four={.pixels=pixels,.width=16u,.height=8u,.scale=4u,.stride=67u};
    for(unsigned i=0u;i<17u*9u;++i)reference[i]=0x112233u;
    if(!portable_sky_render_motion_back(&sky,&one,&motion))return 1;
    motion.precise=1u;
    for(unsigned fractional=0u;fractional<2u;++fractional){
        motion.scroll_x_q16[2]=252*65536+(fractional?16384:0);
        motion.scroll_y_q16[2]=7*65536+(fractional?16384:0);
        const PortableSkyMotionSnapshot immutable=motion;
        for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<67u;++x)
            pixels[y*67u+x]=x<64u?0x112233u:0xdeadbeefu;
        if(!portable_sky_render_motion_back(&sky,&four,&motion)||
            memcmp(&motion,&immutable,sizeof motion))return 1;
        for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<67u;++x){
            const uint32_t expected=x<64u?reference[((y+fractional)/4u)*17u+(x+fractional)/4u]:0xdeadbeefu;
            if(pixels[y*67u+x]!=expected)return 1;
        }
    }
    return 0;
}

/* Authored skyline ends at source row74. Keep an intentional transparent
 * column and a foreground stripe crossing the horizon to test all priorities. */
static int sky_road_horizon_pixels(void)
{
    uint8_t tiles[96]={0},map[2048]={0};
    memset(tiles,0x11,32u);
    memset(tiles+32u,0x11,8u); /* second tile: only source rows72/73 opaque */
    for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<32u;++x){
        const unsigned tile=x==0u?2u:x==1u?0u:y<9u?0u:y==9u?1u:2u;
        map[(y*32u+x)*2u]=(uint8_t)tile;
    }
    const uint16_t sky_palette[2]={0u,0x001fu};
    const PortableSkyAssetView sky={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=sky_palette,.palette_count=2u,.front_map=map,.front_map_bytes=sizeof map,
        .front_width_tiles=32u};
    uint8_t road_map[1]={0},road_tiles[64];memset(road_tiles,1,sizeof road_tiles);
    const uint16_t road_palette[3]={0u,0x03e0u,0x7c00u};
    const PortableRoadView road={.map={.map=road_map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u},
        .tileset={.tiles=road_tiles,.tile_bytes=sizeof road_tiles,.palette=road_palette,.palette_count=3u}};
    PortableCameraAffineLine lines[PORTABLE_CAMERA_AFFINE_MAX_LINES*4u]={0};
    const int32_t offsets[]={49152,81920,-16384,0}; /* ascent, descent and signed cut */
    for(unsigned scale=1u;scale<=4u;scale*=2u)for(unsigned wide=0u;wide<2u;++wide){
        const unsigned width=wide?373u:240u,stride=width*scale+7u;
        uint32_t *pixels=malloc((size_t)stride*160u*scale*sizeof *pixels);
        if(!pixels)return 1;
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,
            .scale=(uint8_t)scale,.stride=stride};
        for(unsigned pose=0u;pose<sizeof offsets/sizeof offsets[0];++pose){
            const int32_t offset=offsets[pose];
            const int integer=offset>=0?offset/65536:-((-offset+65535)/65536);
            PortableSkyMotionSnapshot motion={.initialized=1u,.precise=1u,.layer_mask=1u,
                .split_y=-1,.road_line=(int16_t)(76+pose),.draw_road_line_valid=1u,
                .draw_road_line=(int16_t)(74+integer),.draw_road_line_q16=74*65536+offset,
                .scroll_y={(int16_t)-integer,0,0},.scroll_y_q16={-offset,0,0}};
            const unsigned first=scale==1u?(unsigned)(74+integer):
                (unsigned)(((int64_t)(74*65536+offset)*scale+65535)/65536);
            /* First affine record stays8, even when the boundary cuts a row. */
            road_tiles[1]=2u;
            for(unsigned i=0u;i<PORTABLE_CAMERA_AFFINE_MAX_LINES*scale;++i)
                lines[i].dx=i==8u*scale?0:256;
            portable_software_frame_clear(&frame,0u);
            if(portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_MAX_LINES*scale,
                    &frame,&motion)!=PORTABLE_ROAD_RENDER_OK ||
               pixels[(size_t)first*stride]!=0x00ff00u || pixels[(size_t)(first+1u)*stride]!=0x0000ffu){
                free(pixels);return 1;
            }
            road_tiles[1]=1u;memset(lines,0,sizeof lines);
            for(unsigned mode=0u;mode<4u;++mode){
                motion.road_mode=(uint8_t)mode;
                uint8_t before[sizeof motion];memcpy(before,&motion,sizeof motion);
                for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0xdeadbeefu;
                portable_software_frame_clear(&frame,0x0000ffu);
                if(!portable_sky_render_motion_back(&sky,&frame,&motion)||
                   portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_MAX_LINES*scale,
                       &frame,&motion)!=PORTABLE_ROAD_RENDER_OK ||
                   !portable_sky_render_motion_front(&sky,&frame,&motion)||
                   memcmp(before,&motion,sizeof motion)){free(pixels);return 1;}
                for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                    uint32_t expected=0xdeadbeefu;
                    if(x<width*scale){
                        const int world_x=((int)(x/scale)-(int)((width-240u)/2u)+256)%256;
                        const int cutout=world_x<8, stripe=world_x>=8&&world_x<16;
                        const int64_t source_numerator=(int64_t)y*65536-(int64_t)offset*scale;
                        const int64_t divisor=(int64_t)scale*65536;
                        const int source_y=scale==1u?(int)y-integer:
                            (int)(source_numerator/divisor-(source_numerator%divisor<0));
                        const int wrapped_y=(source_y%256+256)%256;
                        if(y<first)expected=!cutout&&(stripe||wrapped_y<74)?0xff0000u:0x0000ffu;
                        else if(mode==0u)expected=stripe?0x7fff00u:0x00ff00u;
                        else if(mode==1u)expected=0x00ff00u;
                        else if(mode==2u)expected=stripe?0xff0000u:0x00ff00u;
                        else expected=stripe?0x5fcf00u:0x00cf5fu;
                    }
                    if(pixels[(size_t)y*stride+x]!=expected){
                        fprintf(stderr,"horizon scale=%u wide=%u pose=%u mode=%u x=%u y=%u first=%u\n",
                            scale,wide,pose,mode,x,y,first);
                        free(pixels);return 1;
                    }
                }
            }
        }
        /* A camera cut may put the first row at0:168 affine rows suffice. */
        PortableSkyMotionSnapshot cut={.initialized=1u,.road_line=74,.draw_road_line_valid=1u};
        if(portable_road_render_motion(&road,lines,96u*scale,&frame,&cut)!=PORTABLE_ROAD_RENDER_CAPACITY||
           portable_road_render_motion(&road,lines,168u*scale,&frame,&cut)!=PORTABLE_ROAD_RENDER_OK){free(pixels);return 1;}
        cut.draw_road_line_q16=160*65536+1;
        if(portable_road_render_motion(&road,lines,168u*scale,&frame,&cut)!=PORTABLE_ROAD_RENDER_INVALID_ARGUMENT||
           portable_sky_render_motion_front(&sky,&frame,&cut)){free(pixels);return 1;}
        free(pixels);
    }
    return 0;
}

static int sky_motion_pixels(void)
{
    if(sky_fractional_pixels()||sky_precise_tile_rows()||sky_road_horizon_pixels())return 1;
    uint8_t tiles[96],front[2048]={0},middle[2048],back[4096];
    memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);memset(tiles+64u,0x33,32u);
    for(size_t index=0u;index<1024u;++index){middle[index*2u]=1u;middle[index*2u+1u]=0u;}
    for(size_t index=0u;index<2048u;++index){back[index*2u]=index<1024u?2u:0u;back[index*2u+1u]=0u;}
    const uint16_t palette[4]={0u,0x001fu,0x03e0u,0x7c00u};
    const PortableSkyAssetView sky={.tiles=tiles,.tile_bytes=sizeof tiles,.palette=palette,.palette_count=4u,
        .front_map=front,.front_map_bytes=sizeof front,.front_width_tiles=32u,
        .mid_map=middle,.mid_map_bytes=sizeof middle,.mid_width_tiles=32u,
        .back_map=back,.back_map_bytes=sizeof back,.back_width_tiles=64u,.map_height_tiles=32u};
    PortableSkyMotionSnapshot motion={.initialized=1u,.layer_mask=4u,.split_y=-1,.road_line=74,
        .layer_source={0u,1u,2u}};
    uint32_t pixels[16u*4u];PortableSoftwareFrame frame;
    if(!portable_software_frame_init(&frame,pixels,16u,4u))return 1;
    portable_software_frame_clear(&frame,0u);
    const PortableSkyMotionSnapshot before=motion;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||memcmp(&before,&motion,sizeof motion))return 1;
    for(size_t index=0u;index<64u;++index)if(pixels[index]!=0x0000ffu)return 1;
    /* Signed wrap crosses the rightmost 64-wide screenblock back to the first. */
    motion.scroll_x[2]=-1;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||pixels[0]!=0xff0000u||pixels[1]!=0x0000ffu)return 1;
    motion.scroll_x[2]=0;motion.split_y=2;motion.split_layer=2u;motion.split_scroll_x=256;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||pixels[0]!=0x0000ffu||pixels[32]!=0xff0000u)return 1;
    /* Sunset's later phase changes BG3 Y at its retained line, leaving X alone. */
    for(size_t index=32u;index<64u;++index)back[index*2u]=0u;
    for(size_t index=1024u;index<2048u;++index)back[index*2u]=1u;
    motion.split_vertical=1u;motion.split_scroll_y=8;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||pixels[0]!=0x0000ffu||pixels[32]!=0xff0000u)return 1;
    for(size_t index=32u;index<64u;++index)back[index*2u]=2u;
    for(size_t index=1024u;index<2048u;++index)back[index*2u]=0u;
    motion.split_vertical=0u;
    motion.split_y=-1;motion.layer_source[2]=0u;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||pixels[0]!=0xff0000u)return 1;
    motion.layer_source[2]=2u;motion.layer_mask=6u;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||pixels[0]!=0x00ff00u)return 1;
    motion.layer_mask=5u;motion.blend_mode=PORTABLE_SKY_BLEND_ALPHA;motion.blend_a=8u;motion.blend_b=8u;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||
       !portable_sky_render_motion_front(&sky,&frame,&motion)||pixels[0]!=0x7f007fu)return 1;
    motion.blend_a=motion.blend_b=16u;
    if(!portable_sky_render_motion_back(&sky,&frame,&motion)||
       !portable_sky_render_motion_front(&sky,&frame,&motion)||pixels[0]!=0xff00ffu)return 1;
    /* Alpha reads each physical road sample and honors padding in split views. */
    uint32_t scaled_pixels[35u*8u];PortableSoftwareFrame scaled={scaled_pixels,16u,4u,NULL,2u,35u,0u,NULL};
    for(size_t y=0u;y<8u;++y)for(size_t x=0u;x<35u;++x)
        scaled_pixels[y*35u+x]=x>=32u?0xdeadbeefu:(x&1u)?0x00ff00u:0x0000ffu;
    motion.blend_a=motion.blend_b=8u;
    if(!portable_sky_render_motion_front(&sky,&scaled,&motion))return 1;
    for(size_t y=0u;y<8u;++y)for(size_t x=0u;x<35u;++x)
        if(scaled_pixels[y*35u+x]!=(x>=32u?0xdeadbeefu:(x&1u)?0x7f7f00u:0x7f007fu))return 1;
    /* 49654/AF74 switches BG priorities and blend on the first road row.
     * Opaque and transparent road pixels must preserve the source ordering. */
    uint32_t composed_pixels[16u*160u];PortableSoftwareFrame composed;
    if(!portable_software_frame_init(&composed,composed_pixels,16u,160u))return 1;
    uint8_t road_map[64],road_tiles[128]={0};memset(road_map,1,sizeof road_map);
    memset(road_tiles+64u,1,64u);
    const uint16_t road_palette[2]={0u,0x03e0u};
    const PortableRoadView road={.map={.map=road_map,.map_bytes=sizeof road_map,.width_tiles=8u,.height_tiles=8u},
        .tileset={.tiles=road_tiles,.tile_bytes=sizeof road_tiles,.palette=road_palette,.palette_count=2u}};
    PortableCameraAffineLine lines[PORTABLE_CAMERA_AFFINE_MAX_LINES]={0};
    motion.blend_mode=PORTABLE_SKY_BLEND_NONE;
    const uint32_t expected[4]={0x7fff00u,0x00ff00u,0xff0000u,0x5fcf00u};
    for(unsigned road_mode=0u;road_mode<4u;++road_mode){
        motion.road_mode=(uint8_t)road_mode;
        const PortableSkyMotionSnapshot immutable=motion;
        portable_software_frame_clear(&composed,0u);
        if(!portable_sky_render_motion_back(&sky,&composed,&motion)||
           portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_LINES,&composed,&motion)!=PORTABLE_ROAD_RENDER_OK||
           !portable_sky_render_motion_front(&sky,&composed,&motion)||
           composed_pixels[73u*16u]!=0xff0000u||composed_pixels[74u*16u]!=expected[road_mode]||
           memcmp(&motion,&immutable,sizeof motion))return 1;
        road_tiles[64u]=0u;
        portable_software_frame_clear(&composed,0u);
        if(!portable_sky_render_motion_back(&sky,&composed,&motion)||
           portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_LINES,&composed,&motion)!=PORTABLE_ROAD_RENDER_OK||
           !portable_sky_render_motion_front(&sky,&composed,&motion)||
           composed_pixels[74u*16u]!=(road_mode==0u?0x7f00ffu:0xff0000u))return 1;
        road_tiles[64u]=1u;
    }
    /* Ceremony lowers the whole stage by80 source scanlines on arrival. */
    motion.road_mode=PORTABLE_SKY_ROAD_FRONT_OPAQUE;motion.road_line=154;
    portable_software_frame_clear(&composed,0u);
    if(portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_LINES,&composed,&motion)!=PORTABLE_ROAD_RENDER_OK||
       composed_pixels[153u*16u]!=0u||composed_pixels[154u*16u]!=0x00ff00u)return 1;
    motion.road_line=71;
    portable_software_frame_clear(&composed,0u);
    if(portable_road_render_motion(&road,lines,96u,&composed,&motion)!=PORTABLE_ROAD_RENDER_CAPACITY||
       portable_road_render_motion(&road,lines,97u,&composed,&motion)!=PORTABLE_ROAD_RENDER_OK||
       composed_pixels[70u*16u]!=0u||composed_pixels[71u*16u]!=0x00ff00u||
       composed_pixels[159u*16u]!=0x00ff00u)return 1;
    motion.road_line=161;
    if(portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_LINES,&composed,&motion)!=PORTABLE_ROAD_RENDER_INVALID_ARGUMENT)return 1;
    motion.road_line=74;
    motion.road_mode=4u;
    if(portable_road_render_motion(&road,lines,PORTABLE_CAMERA_AFFINE_LINES,&composed,&motion)!=PORTABLE_ROAD_RENDER_INVALID_ARGUMENT)return 1;
    motion.road_mode=0u;
    motion.layer_source[0]=3u;
    if(portable_sky_render_motion_front(&sky,&frame,&motion))return 1;
    motion.layer_source[0]=0u;motion.initialized=0u;
    if(portable_sky_render_motion_back(&sky,&frame,&motion))return 1;
    return 0;
}

int run_road_renderer(void)
{
    static uint8_t map[PORTABLE_ROAD_MAP_BYTES];
    static uint8_t tiles[PORTABLE_TRACK_TILESET_TILE_BYTES];
    static uint16_t palette[16];
    static uint32_t pixels[16 * 160];
    PortableRoadView road;
    PortableCameraAffineLine lines[PORTABLE_CAMERA_AFFINE_LINES];
    PortableSoftwareFrame frame;

    memset(map, 1, sizeof(map));
    memset(tiles, 0, sizeof(tiles));
    memset(palette, 0, sizeof(palette));
    /* BG2 is 8bpp: adjacent pixels occupy adjacent bytes in a 64-byte tile. */
    tiles[64] = 1;
    tiles[65] = 2;
    tiles[66] = 0;
    tiles[67] = 16;
    palette[1] = 0x001f;
    palette[2] = 0x03e0;
    road.map.map = map;
    road.map.map_bytes = sizeof(map);
    road.map.width_tiles = PORTABLE_ROAD_MAP_WIDTH_TILES;
    road.map.height_tiles = PORTABLE_ROAD_MAP_HEIGHT_TILES;
    road.map.source_offset = 0;
    road.tileset.tiles = tiles;
    road.tileset.tile_bytes = sizeof(tiles);
    road.tileset.palette = palette;
    road.tileset.palette_count = 16;
    for (size_t index = 0; index < PORTABLE_CAMERA_AFFINE_LINES; ++index)
    {
        lines[index].dx = 0;
        lines[index].dy = 0;
        lines[index].pa = 256;
        lines[index].pb = 0;
        lines[index].pc = 0;
        lines[index].pd = 256;
    }
    if (!portable_software_frame_init(&frame, pixels, 16, 160))
        return 1;
    portable_software_frame_clear(&frame, 0x112233);
    if (portable_road_render(&road, lines, PORTABLE_CAMERA_AFFINE_LINES,
                             &frame) != PORTABLE_ROAD_RENDER_OK ||
        pixels[0] != 0x112233 || pixels[64u * 16u] != 0xff0000 ||
        pixels[64u * 16u + 1u] != 0x00ff00 ||
        pixels[64u * 16u + 2u] != 0x112233 ||
        pixels[64u * 16u + 3u] != 0x112233 ||
        portable_road_render(&road, lines, PORTABLE_CAMERA_AFFINE_LINES - 1u,
                             &frame) != PORTABLE_ROAD_RENDER_CAPACITY ||
        portable_road_render(NULL, lines, PORTABLE_CAMERA_AFFINE_LINES,
                             &frame) != PORTABLE_ROAD_RENDER_INVALID_ARGUMENT)
        return 1;
    /* The result HBlank path displays the affine record for y-2 starting
     * at line 74. Line 72's x offset must therefore appear at line 74. */
    lines[72u - 64u].dx = 256;
    portable_software_frame_clear(&frame, 0x112233);
    if (portable_road_render_result(
            &road, lines, PORTABLE_CAMERA_AFFINE_LINES, &frame) !=
            PORTABLE_ROAD_RENDER_OK ||
        pixels[73u * frame.width] != 0x112233 ||
        pixels[74u * frame.width] != 0x00ff00 ||
        pixels[75u * frame.width] != 0xff0000)
        return 1;
    portable_software_frame_clear(&frame, 0x112233);
    if (portable_road_render_result_offset(
            &road, lines, PORTABLE_CAMERA_AFFINE_LINES, &frame, 1u) !=
            PORTABLE_ROAD_RENDER_OK ||
        pixels[74u * frame.width] != 0x112233 ||
        pixels[75u * frame.width] != 0x00ff00 ||
        pixels[76u * frame.width] != 0xff0000 ||
        portable_road_render_result_offset(
            &road, lines, PORTABLE_CAMERA_AFFINE_LINES, &frame, 25u) !=
            PORTABLE_ROAD_RENDER_INVALID_ARGUMENT)
        return 1;
    palette[1] = UINT16_C(0x000c);
    portable_software_frame_clear(&frame, 0x112233);
    if (portable_road_render(&road, lines, PORTABLE_CAMERA_AFFINE_LINES,
                             &frame) != PORTABLE_ROAD_RENDER_OK ||
        pixels[64u * 16u] != 0x630000)
        return 1;

    /* Every physical row/column is covered at all supported scales. */
    static uint32_t scaled_pixels[16u*160u*64u+2u];
    PortableCameraAffineLine scaled_lines[PORTABLE_CAMERA_AFFINE_LINES*8u];
    for(size_t i=64u;i<128u;++i)tiles[i]=1u;
    for(size_t i=0u;i<PORTABLE_CAMERA_AFFINE_LINES*8u;++i)
        scaled_lines[i]=(PortableCameraAffineLine){.pa=256,.pd=256};
    for(uint8_t scale=1u;scale<=8u;++scale){
        PortableSoftwareFrame scaled={scaled_pixels+1u,16u,160u,NULL,scale,0u,0u,NULL};
        const size_t count=portable_software_frame_pixel_count(&scaled),stride=16u*scale;
        scaled_pixels[0]=scaled_pixels[count+1u]=0xdeadbeefu;
        portable_software_frame_clear(&scaled,0x112233u);
        if(portable_road_render(&road,scaled_lines,96u*scale,&scaled)!=PORTABLE_ROAD_RENDER_OK)return 1;
        for(size_t y=0u;y<160u*scale;++y)for(size_t x=0u;x<stride;++x)
            if(scaled.pixels[y*stride+x]!=(y<64u*scale?0x112233u:0x630000u))return 1;
        portable_software_frame_rect(&scaled,1u,1u,2u,2u,0x102030u);
        portable_software_frame_add_half(&scaled,17u,0x204060u);
        for(size_t y=scale;y<2u*scale;++y)for(size_t x=scale;x<2u*scale;++x)
            if(scaled.pixels[y*stride+x]!=0x204060u)return 1;
        if(scaled_pixels[0]!=0xdeadbeefu||scaled_pixels[count+1u]!=0xdeadbeefu)return 1;
    }
    /* Anchored views share a padded physical row without touching neighbors. */
    static uint32_t wide_pixels[(373u*8u+5u)*3u*8u];
    static uint32_t native_pixels[240u*3u];
    for(uint8_t scale=1u;scale<=8u;++scale){
        const size_t stride=373u*scale+5u,count=stride*3u*scale;
        PortableSoftwareFrame wide={wide_pixels,373u,3u,NULL,scale,stride,0u,NULL};
        for(size_t i=0u;i<count;++i)wide_pixels[i]=0xdeadbeefu;
        portable_software_frame_clear(&wide,0x112233u);
        for(unsigned anchor=0u;anchor<3u;++anchor){
            PortableSoftwareFrame view=portable_software_frame_native_view(&wide,(PortableFrameAnchor)anchor);
            portable_software_frame_clear(&wide,0x112233u);
            portable_software_frame_rect(&view,0u,1u,240u,1u,0xabcdefu);
            const size_t offset=anchor==2u?133u:anchor==1u?66u:0u;
            for(size_t y=0u;y<3u*scale;++y)for(size_t x=0u;x<stride;++x){
                const uint32_t expected=x>=373u*scale?0xdeadbeefu:
                    y>=scale&&y<2u*scale&&x>=offset*scale&&x<(offset+240u)*scale?0xabcdefu:0x112233u;
                if(wide_pixels[y*stride+x]!=expected)return 1;
            }
            if(anchor==1u){
                portable_software_frame_copy_native(&wide,native_pixels);
                for(size_t i=0u;i<720u;++i)if(native_pixels[i]!=(i>=240u&&i<480u?0xabcdefu:0x112233u))return 1;
            }
        }
    }
    if(sky_motion_pixels())return 1;
    puts("portable road renderer: PASS");
    return 0;
}

int run_minimap_asset(void)
{
    static uint8_t data[0x400];
    static uint16_t cells[PORTABLE_MINIMAP_CELL_COUNT];
    static uint8_t tiles[PORTABLE_MINIMAP_TILE_BYTES];
    PortableMinimapAssetBuffers buffers = {
        .cells = cells,
        .cell_capacity = PORTABLE_MINIMAP_CELL_COUNT,
    };
    PortableMinimapAssetView view;

    memset(data, 0, sizeof(data));
    memset(cells, 0, sizeof(cells));
    data[0x40] = 0x00;
    data[0x41] = 0x00;
    data[0x42] = 0x40;
    data[0x43] = 0xc1;
    if (portable_minimap_asset_build(
            data, sizeof(data), 0x40, &buffers, &view) !=
            PORTABLE_MINIMAP_ASSET_OK ||
        view.cells != cells || view.cell_count != PORTABLE_MINIMAP_CELL_COUNT ||
        view.width_tiles != 6 || view.height_tiles != 5 ||
        view.source_offset != 0x40 || cells[0] != 0 || cells[1] != 0xc140)
        return 1;
    buffers.tiles = tiles;
    buffers.tile_capacity = sizeof(tiles);
    data[0x100] = 0x5a;
    if (portable_minimap_asset_build_with_tiles(
            data, sizeof(data), 0x40, 0x100, &buffers, &view) !=
            PORTABLE_MINIMAP_ASSET_OK ||
        view.tiles != tiles || view.tile_bytes != sizeof(tiles) ||
        view.tile_source_offset != 0x100 || tiles[0] != 0x5a)
        return 1;
    buffers.tiles = NULL;
    buffers.tile_capacity = 0;
    buffers.cell_capacity = PORTABLE_MINIMAP_CELL_COUNT - 1u;
    if (portable_minimap_asset_build(data, sizeof(data), 0x40, &buffers,
                                     &view) != PORTABLE_MINIMAP_ASSET_CAPACITY)
        return 1;
    buffers.cell_capacity = PORTABLE_MINIMAP_CELL_COUNT;
    if (portable_minimap_asset_build(data, 0x42, 0x40, &buffers, &view) !=
        PORTABLE_MINIMAP_ASSET_OUT_OF_BOUNDS)
        return 1;
    if (portable_minimap_asset_build(NULL, sizeof(data), 0x40, &buffers,
                                     &view) != PORTABLE_MINIMAP_ASSET_INVALID_ARGUMENT)
        return 1;

    puts("portable minimap asset: PASS");
    return 0;
}

static int trial_course_map_pixels(void)
{
    uint8_t tiles[64u*32u];uint16_t colors[PORTABLE_HUD_PALETTE_COUNT]={0};
    memset(tiles,0x11,sizeof tiles);colors[17u]=0x03e0u;
    PortableGpRaceHudAssetView gp={.course_tiles=tiles};
    PortableHudAssetView hud={.palette=colors,.palette_count=PORTABLE_HUD_PALETTE_COUNT};
    for(unsigned scale=1u;scale<=4u;scale+=3u){
        const unsigned stride=320u*scale+3u;
        uint32_t *pixels=malloc((size_t)stride*160u*scale*sizeof *pixels);
        if(!pixels)return 1;
        PortableSoftwareFrame frame={.pixels=pixels,.width=320u,.height=160u,
            .stride=stride,.scale=(uint8_t)scale};
        for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0x123456u;
        if(!portable_hud_render_course_map(&gp,&hud,&frame)){free(pixels);return 1;}
        for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
            const uint32_t expected=x>=256u*scale&&x<320u*scale&&y>=96u*scale?0x00ff00u:0x123456u;
            if(pixels[(size_t)y*stride+x]!=expected){free(pixels);return 1;}
        }
        free(pixels);
    }
    return tiles[0]!=0x11u || colors[17u]!=0x03e0u;
}

static int speedometer_pixels(void)
{
    uint8_t dial_pixels[48u*40u*4u]={0},needle_pixels[64u*64u*4u]={0};
    int32_t sine[1024]={0};
    sine[128]=sine[384]=23170;sine[640]=sine[896]=-23170;
    const PortableNativeImage dial={.pixels=dial_pixels,.width=48u,.height=40u};
    const PortableNativeImage needle={.pixels=needle_pixels,.width=64u,.height=64u,
        .anchor_x=-32,.anchor_y=-32};
    for(unsigned y=4u;y<30u;++y)for(unsigned x=30u;x<33u;++x){
        const size_t at=(y*64u+x)*4u;needle_pixels[at+2u]=255u;needle_pixels[at+3u]=255u;
    }
    dial_pixels[0]=255u;dial_pixels[3]=255u;
    const unsigned indices[4]={128u,384u,640u,896u};
    const int32_t speeds[4]={0,512*65536,1024*65536,2000*65536};
    for(unsigned scale=1u;scale<=4u;scale+=3u){
        const unsigned stride=320u*scale+7u;
        uint32_t *pixels=malloc((size_t)stride*160u*scale*sizeof *pixels);
        if(!pixels)return 1;
        PortableSoftwareFrame frame={.pixels=pixels,.width=320u,.height=160u,
            .stride=stride,.scale=(uint8_t)scale};
        for(unsigned pose=0u;pose<4u;++pose)for(unsigned reverse=0u;reverse<2u;++reverse){
            for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0x123456u;
            const int32_t speed=reverse?-speeds[pose]:speeds[pose];
            if(!portable_hud_render_speedometer(&dial,&needle,speed,sine,1024u,&frame))return 1;
            const int sin=sine[indices[pose]]/128,cos=sine[(indices[pose]+256u)&1023u]/128;
            for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                uint32_t expected=0x123456u;
                if(x<scale&&y>=112u*scale&&y<113u*scale)expected=0xff0000ffu;
                const int64_t a=((int64_t)x-24*(int64_t)scale)*cos+((int64_t)y-136*(int64_t)scale)*sin;
                const int64_t b=-((int64_t)x-24*(int64_t)scale)*sin+((int64_t)y-136*(int64_t)scale)*cos;
                const int64_t d=256*(int64_t)scale;
                const int64_t sx=(a<0?-((-a+d-1)/d):a/d)+32;
                const int64_t sy=(b<0?-((-b+d-1)/d):b/d)+32;
                if(x<56u*scale&&y>=104u*scale&&sx>=30&&sx<33&&sy>=4&&sy<30)
                    expected=0xffff0000u;
                if(pixels[(size_t)y*stride+x]!=expected){free(pixels);return 1;}
            }
        }
        /* Opaque corners would project outside the normal 64x64 canvas.
         * Source EE764 never requests the enlarged affine canvas: its
         * x55 column is visible while x56 and y103 remain untouched. */
        uint8_t opaque_pixels[64u*64u*4u];
        for(unsigned i=0u;i<64u*64u;++i){
            opaque_pixels[i*4u]=0u;opaque_pixels[i*4u+1u]=0u;
            opaque_pixels[i*4u+2u]=255u;opaque_pixels[i*4u+3u]=255u;
        }
        uint8_t before[sizeof opaque_pixels];memcpy(before,opaque_pixels,sizeof before);
        PortableNativeImage opaque=needle;opaque.pixels=opaque_pixels;
        for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0x123456u;
        if(!portable_hud_render_speedometer(&dial,&opaque,0,sine,1024u,&frame)||
           memcmp(before,opaque_pixels,sizeof before))return 1;
        for(unsigned sub=0u;sub<scale;++sub){
            if(pixels[(size_t)136u*scale*stride+55u*scale+sub]!=0xffff0000u ||
               pixels[(size_t)136u*scale*stride+56u*scale+sub]!=0x123456u ||
               pixels[(size_t)(104u*scale-1u)*stride+24u*scale+sub]!=0x123456u ||
               pixels[(size_t)104u*scale*stride+24u*scale+sub]!=0xffff0000u){free(pixels);return 1;}
        }
        for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=320u*scale;x<stride;++x)
            if(pixels[(size_t)y*stride+x]!=0x123456u){free(pixels);return 1;}
        free(pixels);
    }
    return needle_pixels[(4u*64u+30u)*4u+2u]!=255u || sine[128]!=23170;
}

/* Shell families share physical artwork. The source logical slots select green
 * bank F or red bank E; page 10 is triple, page 11 is single. Check the pixels,
 * independently of the graphic-selection helper's page/selector assertions. */
static int shell_item_hud_pixels(void)
{
    static const struct
    {
        uint16_t item;
        uint32_t color;
    } cases[] = {
        {0x1000u, 0x008400u}, {0x1001u, 0x00ff00u},
        {0x1002u, 0x840000u}, {0x1003u, 0xff0000u},
        {0x2000u, 0x008400u}, {0x2001u, 0x00ff00u},
        {0x2002u, 0x840000u}, {0x2003u, 0xff0000u},
    };
    uint8_t item_tiles[PORTABLE_HUD_ITEM_TILE_BYTES] = {0};
    uint16_t item_cells[PORTABLE_HUD_ITEM_CELL_COUNT] = {0};
    uint16_t selectors[PORTABLE_HUD_ITEM_PALETTE_COUNT] = {0};
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT] = {0};
    uint16_t map[PORTABLE_MINIMAP_CELL_COUNT] = {0};
    const PortableMinimapAssetView minimap = {
        .cells = map, .cell_count = PORTABLE_MINIMAP_CELL_COUNT,
        .width_tiles = PORTABLE_MINIMAP_WIDTH_TILES,
        .height_tiles = PORTABLE_MINIMAP_HEIGHT_TILES,
    };
    const PortableHudAssetView hud = {
        .palette = palette, .palette_count = PORTABLE_HUD_PALETTE_COUNT,
        .item_tiles = item_tiles, .item_tile_bytes = sizeof item_tiles,
        .item_cells = item_cells, .item_cell_count = PORTABLE_HUD_ITEM_CELL_COUNT,
        .item_palette_selectors = selectors, .item_palette_count = 17u,
    };
    memset(item_tiles + 10u * PORTABLE_HUD_ITEM_PAGE_BYTES, 0x11,
           PORTABLE_HUD_ITEM_PAGE_BYTES);
    memset(item_tiles + 11u * PORTABLE_HUD_ITEM_PAGE_BYTES, 0x22,
           PORTABLE_HUD_ITEM_PAGE_BYTES);
    item_cells[5] = 0x70u;
    item_cells[6] = 0x71u;
    item_cells[9] = 0x72u;
    item_cells[10] = 0x73u;
    selectors[10] = selectors[11] = 0xf000u;
    selectors[15] = selectors[16] = 0xe000u;
    palette[5u * 16u + 1u] = 0x03e0u;
    palette[5u * 16u + 2u] = 0x0200u;
    palette[4u * 16u + 1u] = 0x001fu;
    palette[4u * 16u + 2u] = 0x0010u;

    for (unsigned scale = 1u; scale <= 4u; scale *= 2u)
    {
        const size_t stride = 320u * scale + 7u;
        const size_t count = stride * 160u * scale;
        uint32_t *pixels = malloc((count + 2u) * sizeof *pixels);
        if (pixels == NULL)
            return 1;
        PortableSoftwareFrame frame = {
            .pixels = pixels + 1u, .width = 320u, .height = 160u,
            .scale = (uint8_t)scale, .stride = stride,
        };
        for (size_t index = 0u; index < sizeof cases / sizeof cases[0]; ++index)
        {
            PortableHudState state = {
                .lap_count = 3u, .show_gp_item = 1u, .gp_item = cases[index].item,
            };
            const PortableHudState immutable = state;
            for (size_t pixel = 0u; pixel < count + 2u; ++pixel)
                pixels[pixel] = 0xdeadbeefu;
            portable_software_frame_clear(&frame, 0x101828u);
            if (portable_hud_render_with_asset(&minimap, &hud, &state, &frame) !=
                    PORTABLE_HUD_RENDER_OK || memcmp(&state, &immutable, sizeof state))
            {
                free(pixels);
                return 1;
            }
            for (size_t y = 0u; y < 160u * scale; ++y)
                for (size_t x = 0u; x < stride; ++x)
                {
                    /* The 240-wide item panel is centered in the 320-wide view. */
                    const uint32_t expected = x >= 320u * scale ? 0xdeadbeefu :
                        x >= 152u * scale && x < 168u * scale &&
                        y >= 8u * scale && y < 24u * scale ? cases[index].color : 0x101828u;
                    if (frame.pixels[y * stride + x] != expected)
                    {
                        free(pixels);
                        return 1;
                    }
                }
            if (pixels[0] != 0xdeadbeefu || pixels[count + 1u] != 0xdeadbeefu)
            {
                free(pixels);
                return 1;
            }
        }
        free(pixels);
    }
    return 0;
}

int run_hud_renderer(void)
{
    if(speedometer_pixels()||trial_course_map_pixels()||shell_item_hud_pixels())return 1;
    static uint16_t cells[PORTABLE_MINIMAP_CELL_COUNT];
    static uint8_t tiles[PORTABLE_MINIMAP_TILE_BYTES];
    static uint32_t pixels[240 * 160];
    PortableMinimapAssetView minimap = {
        .cells = cells,
        .cell_count = PORTABLE_MINIMAP_CELL_COUNT,
        .width_tiles = 6,
        .height_tiles = 5,
        .source_offset = 0x40,
        .tiles = tiles,
        .tile_bytes = sizeof(tiles),
        .tile_source_offset = 0x80,
    };
    PortableHudState state = {
        .lap = 0,
        .lap_count = 3,
        .position = 1,
        .checkpoint = 0x6f,
        .elapsed_frames = 16,
        .heading = 0,
        .show_time_trial_items = 1,
        .mushrooms_remaining = 3,
    };
    PortableSoftwareFrame frame;

    memset(cells, 0, sizeof(cells));
    memset(tiles, 0, sizeof(tiles));
    cells[1] = 0xc140;
    tiles[0] = 0x01;
    if (!portable_software_frame_init(&frame, pixels, 240, 160))
        return 1;
    portable_software_frame_clear(&frame, 0x101828);
    if (portable_hud_render(&minimap, &state, &frame) !=
            PORTABLE_HUD_RENDER_OK ||
        pixels[3u * 240u + 33u] != 0xf1fa8c ||
        pixels[112u * 240u + 8u] != 0x101828 ||
        pixels[136u * 240u + 24u] != 0x101828 ||
        portable_hud_render(NULL, &state, &frame) !=
            PORTABLE_HUD_RENDER_INVALID_ARGUMENT)
        return 1;

    {
        static uint8_t data[0x1c00];
        static uint8_t hud_tiles[PORTABLE_HUD_CHAR_BYTES];
        static uint16_t hud_palette[PORTABLE_HUD_PALETTE_COUNT];
        static uint16_t hud_digit_tiles[PORTABLE_HUD_DIGIT_COUNT];
        static uint8_t hud_clock_steps[PORTABLE_HUD_CLOCK_STEP_COUNT];
        static uint16_t hud_left_cells[PORTABLE_HUD_LEFT_CELL_COUNT];
        static uint16_t hud_slash_cells[PORTABLE_HUD_SLASH_CELL_COUNT];
        static uint16_t hud_right_cells[PORTABLE_HUD_RIGHT_CELL_COUNT];
        static uint8_t hud_item_tiles[PORTABLE_HUD_ITEM_TILE_BYTES];
        static uint16_t hud_item_cells[PORTABLE_HUD_ITEM_CELL_COUNT];
        static uint16_t hud_item_palettes[PORTABLE_HUD_ITEM_PALETTE_COUNT];
        const PortableHudAssetBuffers buffers = {
            .tiles = hud_tiles,
            .tile_capacity = sizeof(hud_tiles),
            .palette = hud_palette,
            .palette_capacity = sizeof(hud_palette) / sizeof(hud_palette[0]),
            .digit_tiles = hud_digit_tiles,
            .digit_capacity = sizeof(hud_digit_tiles) /
                              sizeof(hud_digit_tiles[0]),
            .clock_steps = hud_clock_steps,
            .clock_capacity = sizeof(hud_clock_steps),
            .left_cells = hud_left_cells,
            .left_capacity = sizeof(hud_left_cells) /
                             sizeof(hud_left_cells[0]),
            .slash_cells = hud_slash_cells,
            .slash_capacity = sizeof(hud_slash_cells) /
                              sizeof(hud_slash_cells[0]),
            .right_cells = hud_right_cells,
            .right_capacity = sizeof(hud_right_cells) /
                              sizeof(hud_right_cells[0]),
            .item_tiles = hud_item_tiles,
            .item_tile_capacity = sizeof(hud_item_tiles),
            .item_palette_selectors=hud_item_palettes,.item_palette_capacity=17u,
            .item_cells = hud_item_cells,
            .item_cell_capacity = sizeof(hud_item_cells) /
                                  sizeof(hud_item_cells[0]),
        };
        PortableHudAssetView hud_asset;

        memset(data, 0, sizeof(data));
        data[0x400] = 0x01;
        data[0x40 + 33u * 2u] = 0x1f;
        data[0x1c0] = 3;
        data[0x1c1] = 0;
        data[0x1c2] = 2;
        data[0x1c3] = 0;
        data[0x1e0] = 1;
        data[0x1e1] = 0;
        data[0x1e2] = 2;
        data[0x1e3] = 0;
        data[0x200] = 3;
        data[0x201] = 0;
        data[0x202] = 2;
        data[0x203] = 0;
        for (size_t index = 0; index < 6; ++index)
        {
            data[0x1c4 + index * 2u] = 0;
            data[0x1c5 + index * 2u] = 0xc0;
            data[0x200 + 4u + index * 2u] = 0;
            data[0x200 + 5u + index * 2u] = 0xc0;
        }
        for (size_t index = 0; index < 2; ++index)
        {
            data[0x1e4 + index * 2u] = 0;
            data[0x1e5 + index * 2u] = 0xc0;
        }
        for (size_t index = 0; index < PORTABLE_HUD_CLOCK_STEP_COUNT; ++index)
            data[0x280 + index] = 1;
        data[0x1400 + 4u * 0x80u] = 0x01;
        data[0x1400 + 5u * 0x80u] = 0x02;
        data[0x1400 + 6u * 0x80u] = 0x03;
        data[0x1400] = 0x04;
        data[0x1400 + 11u * 0x80u] = 0x01;
        data[0x40 + 4u * 32u + 2u] = 0x1f;
        data[0x40 + 4u * 32u + 4u] = 0xe0;
        data[0x40 + 4u * 32u + 5u] = 0x03;
        data[0x40 + 4u * 32u + 6u] = 0x00;
        data[0x40 + 4u * 32u + 7u] = 0x7c;
        data[0x40 + 5u * 32u + 8u] = 0xff;
        data[0x40 + 5u * 32u + 9u] = 0x7f;
        data[0x40 + 5u * 32u + 2u] = 0x00;
        data[0x40 + 5u * 32u + 3u] = 0x7c;
        data[0x1900] = 4;
        data[0x1902] = 4;
        data[0x1904 + (1u * 4u + 1u) * 2u] = 0x70;
        data[0x1905 + (1u * 4u + 1u) * 2u] = 0xf0;
        for(unsigned i=0u;i<17u;++i){
            const uint16_t bank=(i==0u||(i>=9u&&i<=14u))?0xf000u:0xe000u;
            data[0x1a00u+i*2u]=(uint8_t)bank;data[0x1a01u+i*2u]=(uint8_t)(bank>>8u);
        }
        const int hud_status = portable_hud_asset_build(
            data, sizeof(data), 0x400, 0x40, 0x180, 0x280, 0x1c0, 0x1e0,
            0x200, 0x1400, 0x1900, 0x1a00, &buffers, &hud_asset);
        if (hud_status != PORTABLE_HUD_ASSET_OK)
            return 1;

        portable_software_frame_clear(&frame, 0x101828);
        const int render_status = portable_hud_render_with_asset(
            &minimap, &hud_asset, &state, &frame);
        if (render_status != PORTABLE_HUD_RENDER_OK ||
            pixels[32u] != 0xff0000 ||
            pixels[8u * 240u + 112u] != 0xff0000 ||
            hud_asset.tile_source_offset != 0x400 ||
            hud_asset.palette_source_offset != 0x40 ||
            hud_asset.clock_source_offset != 0x280 ||
            hud_asset.clock_step_count != PORTABLE_HUD_CLOCK_STEP_COUNT)
            return 1;
        {
            static const uint32_t expected[4] = {
                UINT32_C(0xff0000), UINT32_C(0x00ff00),
                UINT32_C(0x0000ff), UINT32_C(0xffffff),
            };
            for (uint8_t remaining = 3u;; --remaining)
            {
                state.mushrooms_remaining = remaining;
                portable_software_frame_clear(&frame, 0x101828);
                if (portable_hud_render_with_asset(
                        &minimap, &hud_asset, &state, &frame) !=
                        PORTABLE_HUD_RENDER_OK ||
                    pixels[8u * 240u + 112u] != expected[3u - remaining])
                    return 1;
                if (remaining == 0u)
                    break;
            }
        }
        state.show_time_trial_items = 0u;
        state.show_gp_item = 1u;
        {
            typedef struct
            {
                uint16_t item;
                uint8_t physical_page;
                uint16_t palette_selector;
            } ExpectedGpItemGraphic;
            static const ExpectedGpItemGraphic expected_graphics[] = {
                {0x0002u,13u,0xf000u},
                {0x1000u,11u,0xf000u}, {0x1001u,10u,0xf000u},
                {0x1002u,11u,0xe000u}, {0x1003u,10u,0xe000u},
                {0x1004u, 9u,0xf000u}, {0x1005u, 1u,0xe000u},
                {0x1006u, 8u,0xe000u}, {0x1007u, 6u,0xe000u},
                {0x1008u, 4u,0xe000u}, {0x1009u, 7u,0xe000u},
                {0x100au, 3u,0xe000u}, {0x100bu,13u,0xf000u},
                {0x100cu, 2u,0xe000u}, {0x100du,12u,0xf000u},
                {0x100eu, 7u,0xe000u}, {0x100fu, 5u,0xe000u},
                {0x2000u,11u,0xf000u}, {0x2001u,10u,0xf000u},
                {0x2002u,11u,0xe000u}, {0x2003u,10u,0xe000u},
                {0x2004u, 9u,0xf000u}, {0x2005u, 1u,0xe000u},
                {0x2007u, 6u,0xe000u}, {0x200au, 3u,0xe000u},
                {0x200cu, 2u,0xe000u},
            };
            static const uint16_t unsupported[] = {
                0u, 1u, 3u, 0x100u, 0x1010u, 0x2006u, 0x2008u,
                0x2009u, 0x200bu, 0x200du,
            };
            PortableHudGpItemGraphic graphic;
            for (size_t index = 0u;
                 index < sizeof expected_graphics / sizeof expected_graphics[0];
                 ++index)
            {
                if (!portable_hud_gp_item_graphic(&hud_asset,
                        expected_graphics[index].item, &graphic) ||
                    graphic.physical_page !=
                        expected_graphics[index].physical_page ||
                    graphic.palette_selector !=
                        expected_graphics[index].palette_selector)
                    return 1;
            }
            for (size_t index = 0u;
                 index < sizeof unsupported / sizeof unsupported[0]; ++index)
            {
                if (portable_hud_gp_item_graphic(&hud_asset,
                        unsupported[index], &graphic))
                    return 1;
            }
            if (portable_hud_gp_item_graphic(&hud_asset,0x1008u, NULL))
                return 1;
            hud_item_palettes[15]=0xa000u;hud_item_palettes[11]=0xc000u;
            PortableHudGpItemGraphic first,second;
            if(!portable_hud_gp_item_graphic(&hud_asset,0x1000u,&first)||
               !portable_hud_gp_item_graphic(&hud_asset,0x1002u,&second)||
               first.physical_page!=second.physical_page||
               first.palette_selector!=0xc000u||second.palette_selector!=0xa000u)return 1;
            hud_item_palettes[15]=0xe000u;hud_item_palettes[11]=0xf000u;
        }
        {
            static const uint16_t gp_mushroom_items[3] = {
                UINT16_C(0x1008), UINT16_C(0x100f), UINT16_C(0x1007),
            };
            static const uint32_t expected[3] = {
                UINT32_C(0xff0000), UINT32_C(0x00ff00), UINT32_C(0x0000ff),
            };
            for (size_t index = 0u; index < 3u; ++index)
            {
                state.gp_item = gp_mushroom_items[index];
                portable_software_frame_clear(&frame, 0x101828);
                if (portable_hud_render_with_asset(
                        &minimap, &hud_asset,&state,&frame) !=
                        PORTABLE_HUD_RENDER_OK ||
                    pixels[8u * 240u + 112u] != expected[index])
                    return 1;
            }
        }
        state.gp_item = 0x1000u;
        portable_software_frame_clear(&frame, 0x101828);
        if (portable_hud_render_with_asset(
                &minimap, &hud_asset, &state, &frame) !=
                PORTABLE_HUD_RENDER_OK ||
            pixels[8u * 240u + 112u] != 0x0000ffu)
            return 1;
        /* Changing one owned selector recolors both GP and TT, including
         * logical pages whose graphics share a physical page. */
        hud_item_palettes[4]=0xf000u;
        state.gp_item=0x1008u;
        portable_software_frame_clear(&frame,0x101828);
        if(portable_hud_render_with_asset(&minimap,&hud_asset,&state,&frame)!=0 ||
           pixels[8u*240u+112u]!=0x0000ffu)return 1;
        state.show_gp_item=0u;state.show_time_trial_items=1u;state.mushrooms_remaining=3u;
        portable_software_frame_clear(&frame,0x101828);
        if(portable_hud_render_with_asset(&minimap,&hud_asset,&state,&frame)!=0 ||
           pixels[8u*240u+112u]!=0x0000ffu)return 1;
        PortableHudAssetView missing=hud_asset;
        missing.item_palette_selectors=NULL;missing.item_palette_count=0u;
        if(portable_hud_render_with_asset(&minimap,&missing,&state,&frame)!=PORTABLE_HUD_RENDER_INVALID_ARGUMENT)return 1;
    }

    puts("portable hud renderer: PASS");
    return 0;
}

int run_camera(void)
{
    PortableCameraState state = {0};

    portable_camera_state_update(&state, 0x00123000, -0x00456000,
                                 0x1234);
    if (state.screen_x != 0x78 || state.screen_y != 0x8C ||
        state.distance_q16 != 0x00400000 ||
        state.elevation_q16 != 0x01000000 ||
        state.horizon_q16 != 0x00400000 ||
        state.position_x != 0x00123000 ||
        state.position_y != -0x00456000 ||
        state.temporal_x != 0x00123000 ||
        state.temporal_y != -0x00456000 || state.pitch != 0x347B ||
        state.yaw != 0x1234 || state.temporal_yaw != 0x1234 ||
        state.temporal_pitch != 0 || state.hdma_phase != 0 ||
        state.race_camera_field != 0 || state.projection_y_offset != 0 ||
        state.projection_y_offset_q16 != 0 || state.projection_y_offset_precise != 0)
        return 1;

    state.projection_y_offset=-32;state.projection_y_offset_q16=12345;state.projection_y_offset_precise=1u;
    portable_camera_state_update(&state, -0x100000, 0x200000, 0xF000);
    if (state.position_x != -0x100000 || state.position_y != 0x200000 ||
        state.temporal_x != -0x100000 || state.temporal_y != 0x200000 ||
        state.yaw != 0xF000 || state.temporal_yaw != 0xF000 || state.projection_y_offset != 0 ||
        state.projection_y_offset_q16 != 0 || state.projection_y_offset_precise != 0)
        return 1;

    portable_camera_state_update(NULL, 0, 0, 0);
    puts("portable camera: PASS");
    return 0;
}

int run_camera_projection(void)
{
    PortableCameraState camera = {0};
    PortableCameraProjection projection;
    PortableCameraProjectionResult result;

    portable_camera_state_update(&camera, 0, 0, 0);
    if (portable_camera_projection_build(&camera, 0, 32768, 31414, 9319,
                                         &projection) !=
            PORTABLE_CAMERA_PROJECTION_OK ||
        portable_camera_projection_project(&camera, &projection, 0, 0, 0,
                                           &result) !=
            PORTABLE_CAMERA_PROJECTION_OK)
        return 1;
    if (result.x != 120 || result.y != 140 || result.distance != 256)
        return 1;
    /* Source +380 is signed and translates every projected actor equally;
     * the affine basis and inverse scale remain independent of hop offset. */
    const PortableCameraProjection baseline_projection=projection;
    PortableCameraAffineLine base_lines[PORTABLE_CAMERA_AFFINE_LINES],offset_lines[PORTABLE_CAMERA_AFFINE_LINES];
    uint8_t base_phase=0u;
    if(portable_camera_affine_build(&camera,0,31414,9319,0,32768,base_lines,PORTABLE_CAMERA_AFFINE_LINES,&base_phase))return 1;
    const int8_t offsets[]={-128,-32,-1,0,1,24,127};
    for(unsigned i=0u;i<sizeof offsets/sizeof offsets[0];++i){
        camera.projection_y_offset=offsets[i];
        uint8_t phase=0u;
        if(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection) ||
           memcmp(&projection,&baseline_projection,sizeof projection) ||
           portable_camera_projection_project(&camera,&projection,0,0,0,&result) ||
           result.x!=120 || result.y!=140+offsets[i] || result.distance!=256 ||
           portable_camera_affine_build(&camera,0,31414,9319,0,32768,offset_lines,PORTABLE_CAMERA_AFFINE_LINES,&phase) ||
           memcmp(base_lines,offset_lines,sizeof base_lines))return 1;
    }
    camera.projection_y_offset=0;

    if (portable_camera_projection_project(&camera, &projection,
                                           INT32_C(0x04000000), 0, 0,
                                           &result) !=
        PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE)
        return 1;
    /* The original cam_CalcProjectActor keeps actor-camera differences in
     * signed 32-bit locals before its range check. This is the wrapped pose
     * reached by the bounded P155 no-input host diagnostic; widening the
     * subtraction incorrectly rejects the otherwise valid source-width
     * relative position. */
    portable_camera_state_update(&camera, INT32_C(0x03860000),
                                 INT32_C(-2145233408), 0);
    if (portable_camera_projection_build(&camera, 0, 32768, 31414, 9319,
                                         &projection) !=
            PORTABLE_CAMERA_PROJECTION_OK ||
        portable_camera_projection_project(
            &camera, &projection, INT32_C(0x03860000),
            INT32_C(-2145233408), 0, &result) !=
            PORTABLE_CAMERA_PROJECTION_OK)
        return 1;
    if (portable_camera_projection_build(NULL, 0, 0, 0, 0, &projection) !=
        PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT)
        return 1;
    int16_t height_shift = -1;
    if (portable_camera_projection_height_shift(0x3fff,256,&height_shift) != 0 || height_shift != 0 ||
        portable_camera_projection_height_shift(0x4000,256,&height_shift) != 0 || height_shift != 1 ||
        portable_camera_projection_height_shift(0x7fff,256,&height_shift) != 0 || height_shift != 1 ||
        portable_camera_projection_height_shift(0x76c00,3772,&height_shift) != 0 || height_shift != 1 ||
        portable_camera_projection_height_shift(0x7b800,3784,&height_shift) != 0 || height_shift != 2)
        return 1;
    if (portable_camera_projection_height_shift(-1,256,&height_shift) != PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT ||
        portable_camera_projection_height_shift(1,0,&height_shift) != PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT ||
        portable_camera_projection_height_shift(1,256,NULL) != PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT)
        return 1;
    PortableCameraProjectionResult ground={120,140,256},body;
    if(portable_camera_projection_body(&ground,8*65536,&body) ||
       body.x!=120 || body.y!=108 || body.distance!=256 || ground.y!=140 ||
       portable_camera_projection_body(&ground,-8*65536,&body) || body.y!=172)
        return 1;
    ground.distance=3772;
    if(portable_camera_projection_body(&ground,0x76c00,&body) || body.y!=139 ||
       portable_camera_projection_body(&ground,-0x76c00,&body) || body.y!=141)
        return 1;
    ground.y=INT16_MIN;ground.distance=256;
    if(portable_camera_projection_body(&ground,0x4000,&body) || body.y!=INT16_MAX)
        return 1;
    ground.distance=0;body=ground;
    if(portable_camera_projection_body(&ground,0,&body) ||
       portable_camera_projection_body(&ground,1,&body)!=PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT ||
       portable_camera_projection_body(NULL,0,&body)!=PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT)
        return 1;
    /* Rendering retains fractions while source snapshots stay native pixels. */
    PortableCameraState precise_camera={0};
    precise_camera.screen_x=120;precise_camera.screen_y=140;
    precise_camera.elevation_q16=0x01000000;
    PortableCameraProjection exact_basis={{{65536,0,0},{0,65536,0},{0,0,65536}},0,0,0};
    PortableCameraProjectionPreciseResult precise,previous={0};
    for(int32_t step=-7;step<=7;++step){
        if(portable_camera_projection_project(&precise_camera,&exact_basis,step*8192,0,0,&result) ||
           portable_camera_projection_project_precise(&precise_camera,&exact_basis,step*8192,0,0,&precise) ||
           result.x!=120 || precise.x_q16!=120*65536+step*8192 ||
           precise.y_q16!=140*65536 || precise.distance_q16!=1024*65536)return 1;
        if(step>-7 && precise.x_q16-previous.x_q16!=8192)return 1;
        previous=precise;
    }
    /* Small turns and depth changes no longer wait for a whole-pixel or whole
     * source-distance boundary before the rendering coordinates can change. */
    for(int32_t step=0;step<8;++step){
        exact_basis.basis[1][0]=step*256;
        if(portable_camera_projection_project_precise(&precise_camera,&exact_basis,0,65536,0,&precise) ||
           precise.x_q16!=120*65536+step*256)return 1;
    }
    exact_basis.basis[1][0]=0;exact_basis.basis[0][2]=65536;
    for(int32_t step=0;step<8;++step){
        if(portable_camera_projection_project_precise(&precise_camera,&exact_basis,step*128,0,0,&precise) ||
           precise.distance_q16!=1024*65536-step*512)return 1;
    }
    exact_basis.basis[0][2]=0;precise_camera.projection_y_offset=-8;
    if(portable_camera_projection_project_precise(&precise_camera,&exact_basis,0,0,0,&precise) ||
       precise.y_q16!=132*65536)return 1;
    precise_camera.projection_y_offset_precise=1u;
    const int32_t visible_offsets[]={-81920,-16384,0,8192,49152,81920};
    for(unsigned i=0u;i<sizeof visible_offsets/sizeof visible_offsets[0];++i){
        precise_camera.projection_y_offset_q16=visible_offsets[i];
        if(portable_camera_projection_project_precise(&precise_camera,&exact_basis,0,0,0,&precise) ||
           precise.y_q16!=140*65536+visible_offsets[i] ||
           portable_camera_projection_project(&precise_camera,&exact_basis,0,0,0,&result) ||
           result.y!=132)return 1;
    }
    precise_camera.projection_y_offset_precise=2u;
    if(portable_camera_projection_project_precise(&precise_camera,&exact_basis,0,0,0,&precise)!=
       PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT)return 1;
    precise_camera.projection_y_offset_precise=0u;
    precise=(PortableCameraProjectionPreciseResult){120*65536,140*65536,256*65536};
    if(portable_camera_projection_body_precise(&precise,8192,&previous) ||
       previous.y_q16!=140*65536-32768 || previous.x_q16!=precise.x_q16 ||
       previous.distance_q16!=precise.distance_q16 ||
       portable_camera_projection_body_precise(&precise,-8192,&previous) ||
       previous.y_q16!=140*65536+32768)return 1;
    precise.distance_q16=0;
    if(portable_camera_projection_body_precise(&precise,0,&previous)!=PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT ||
       portable_camera_projection_project_precise(NULL,&exact_basis,0,0,0,&precise)!=PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT ||
       portable_camera_projection_project_precise(&precise_camera,NULL,0,0,0,&precise)!=PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT)return 1;
    precise_camera.elevation_q16=0;
    if(portable_camera_projection_project_precise(&precise_camera,&exact_basis,0,0,0,&precise)!=PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT)return 1;
    puts("portable camera projection: PASS");
    return 0;
}

int run_camera_affine(void)
{
    PortableCameraState camera = {0};
    PortableCameraAffineLine lines[PORTABLE_CAMERA_AFFINE_LINES];
    uint8_t phase = 0;

    portable_camera_state_update(&camera, 0, 0, 0);
    if (portable_camera_affine_build(&camera, 0, 31414, 9319, 0, 32768,
                                     lines, PORTABLE_CAMERA_AFFINE_LINES,
                                     &phase) != PORTABLE_CAMERA_AFFINE_OK)
        return 1;
    if (phase != 1 || lines[0].pa != -19667 || lines[0].pd != 28087 ||
        lines[95].dx != -6120 || lines[95].dy != 3420 ||
        lines[95].pa != 51 || lines[95].pb != 0 || lines[95].pc != 0 ||
        lines[95].pd != 180)
        return 1;
    if (portable_camera_affine_build(&camera, 0, 31414, 9319, 32768, 0,
                                     lines, PORTABLE_CAMERA_AFFINE_LINES,
                                     &phase) != PORTABLE_CAMERA_AFFINE_OK ||
        phase != 0 || lines[95].dx != -3420 || lines[95].dy != -6120 ||
        lines[95].pa != 0 || lines[95].pb != -180 || lines[95].pc != 51 ||
        lines[95].pd != 0)
        return 1;
    if (portable_camera_affine_build(&camera, 0, 31414, 9319, 0, 32768,
                                     lines, PORTABLE_CAMERA_AFFINE_LINES - 1u,
                                     &phase) !=
        PORTABLE_CAMERA_AFFINE_DESTINATION_TOO_SMALL || phase != 0)
        return 1;
    /* Fractional output rows preserve all original native-row samples. */
    PortableCameraAffineLine scaled[PORTABLE_CAMERA_AFFINE_LINES*8u];
    for(uint8_t scale=1u;scale<=8u;++scale){
        uint8_t native_phase=0u,scaled_phase=0u;
        if(portable_camera_affine_build(&camera,0,31414,9319,23170,23170,
            lines,96u,&native_phase)!=PORTABLE_CAMERA_AFFINE_OK||
           portable_camera_affine_build_scaled(&camera,0,31414,9319,23170,23170,
            scaled,96u*scale,&scaled_phase,scale)!=PORTABLE_CAMERA_AFFINE_OK||native_phase!=scaled_phase)return 1;
        for(size_t row=0u;row<96u;++row)
            if(memcmp(&lines[row],&scaled[row*scale],sizeof lines[row])!=0)return 1;
    }
    PortableCameraAffineLine extended[PORTABLE_CAMERA_AFFINE_MAX_LINES*8u];
    for(uint8_t scale=1u;scale<=4u;scale*=2u){
        uint8_t a_phase=0u,b_phase=0u;
        if(portable_camera_affine_build_scaled(&camera,0,31414,9319,0,32768,
            scaled,96u*scale,&a_phase,scale)!=PORTABLE_CAMERA_AFFINE_OK||
           portable_camera_affine_build_scaled_rows(&camera,0,31414,9319,0,32768,
            extended,97u*scale,&b_phase,scale,97u)!=PORTABLE_CAMERA_AFFINE_OK||
           a_phase!=b_phase||memcmp(scaled,extended,96u*scale*sizeof scaled[0]))return 1;
        const PortableCameraAffineLine *extra=&extended[96u*scale];
        if(extra->dx!=-6000||extra->dy!=3560||extra->pa!=50||extra->pd!=178)return 1;
        if(portable_camera_affine_build_scaled_rows(&camera,0,31414,9319,0,32768,
            extended,96u*scale,&b_phase,scale,97u)!=PORTABLE_CAMERA_AFFINE_DESTINATION_TOO_SMALL)return 1;
    }
    camera.screen_y = 0x40;
    if (portable_camera_affine_build(&camera, 0, 32768, 0, 0, 32768,
                                     lines, PORTABLE_CAMERA_AFFINE_LINES,
                                     &phase) != PORTABLE_CAMERA_AFFINE_DIVISION_BY_ZERO)
        return 1;
    puts("portable camera affine: PASS");
    return 0;
}
