#include "gp_results_renderer.h"
#include "gp_standings_renderer.h"
#include "presentation_pack.h"
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return __LINE__; \
} } while (0)

static void pattern(uint8_t *out, uint16_t width, uint16_t height,
                    uint16_t first)
{
    out[0] = (uint8_t)width; out[1] = (uint8_t)(width >> 8u);
    out[2] = (uint8_t)height; out[3] = (uint8_t)(height >> 8u);
    for (uint16_t index = 0u; index < width * height; ++index)
    {
        const uint16_t value = (uint16_t)(first + index);
        out[4u + index * 2u] = (uint8_t)value;
        out[5u + index * 2u] = (uint8_t)(value >> 8u);
    }
}

static int fractional_race_marker_pixels(void)
{
    uint8_t tiles[64]={0};
    for(unsigned tile=0u;tile<2u;++tile)for(unsigned y=0u;y<8u;++y)
        for(unsigned x=0u;x<8u;++x)if(x!=y)
            tiles[tile*32u+y*4u+x/2u]|=(uint8_t)((tile+1u)<<((x&1u)*4u));
    const uint16_t palette[16]={0,0x001f,0x03e0};
    const PortableGpMinimapMarkerAssetView art={tiles,palette,palette};
    for(unsigned scale=1u;scale<=4u;scale*=2u)for(unsigned shift=20u;shift<=21u;++shift)
        for(unsigned clipped=0u;clipped<2u;++clipped)for(unsigned move=0u;move<2u;++move){
            const int32_t unit=(int32_t)(1u<<shift);
            PortableRaceMinimapMarker entries[2]={
                {.x_q16=(clipped?-173:3)*unit+unit/4+(int32_t)move*(unit/4),
                 .y_q16=(clipped?-93:4)*unit+unit/2,.selected=1u},
                {.source_flags=0x8000u}};
            entries[1].x_q16=entries[0].x_q16+unit/4;
            entries[1].y_q16=entries[0].y_q16+unit/4;
            uint8_t before[sizeof entries];memcpy(before,entries,sizeof entries);
            const unsigned width=373u,stride=width*scale+11u;
            uint32_t *pixels=malloc((size_t)stride*160u*scale*sizeof *pixels);CHECK(pixels);
            for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0x123456u;
            PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,
                .stride=stride,.scale=(uint8_t)scale};
            CHECK(portable_gp_results_render_race_markers(&art,entries,2u,(uint8_t)shift,0u,&frame));
            for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                uint32_t expected=0x123456u;
                /* Independent quotient/remainder floor and source-texel oracle.
                 * The right native viewport clips even on a wider surface. */
                for(int i=1;i>=0;--i){
                    const int64_t qx=(int64_t)entries[i].x_q16*scale;
                    const int64_t qy=(int64_t)entries[i].y_q16*scale;
                    const int ox=(int)(qx/unit-(qx%unit<0))+172*(int)scale;
                    const int oy=(int)(qy/unit-(qy%unit<0))+92*(int)scale;
                    const int rx=(int)x-(int)((width-240u)*scale),ry=(int)y;
                    const int dx=rx-ox,dy=ry-oy;
                    if(rx>=0&&rx<240*(int)scale&&dy>=0&&dx>=0&&
                       dx<8*(int)scale&&dy<8*(int)scale&&dx/(int)scale!=dy/(int)scale)
                        expected=i?0x00ff00u:0xff0000u;
                }
                CHECK(pixels[(size_t)y*stride+x]==expected);
            }
            CHECK(memcmp(before,entries,sizeof entries)==0);
            free(pixels);
        }
    return 0;
}

static int source_race_marker_pixels(void)
{
    uint8_t tiles[64];memset(tiles,0x11,32u);memset(tiles+32u,0x22,32u);
    const uint16_t palette5[16]={0,0x001f,0x03e0},palette7[16]={0,0x7c00,0x7fff};
    const PortableGpMinimapMarkerAssetView art={tiles,palette5,palette7};
    PortableRaceMinimapMarker entries[2]={
        {.x_q16=256*65536,.y_q16=256*65536,.source_flags=0x8000u,.selected=1u},
        {.x_q16=640*65536,.y_q16=640*65536,.source_flags=0x8000u}};
    for(unsigned scale=1u;scale<=4u;scale+=3u){
        const unsigned stride=320u*scale+9u;
        uint32_t *pixels=malloc((size_t)stride*160u*scale*sizeof *pixels);CHECK(pixels);
        PortableSoftwareFrame frame={.pixels=pixels,.width=320u,.height=160u,
            .stride=stride,.scale=(uint8_t)scale};
        for(unsigned shift=20u;shift<=21u;++shift)for(unsigned blink=0u;blink<2u;++blink){
            for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0x123456u;
            CHECK(portable_gp_results_render_race_markers(&art,entries,2u,(uint8_t)shift,blink*16u,&frame));
            const unsigned sx=252u+(shift==20u?16u:8u),sy=92u+(shift==20u?16u:8u);
            const unsigned gx=252u+(shift==20u?40u:20u),gy=92u+(shift==20u?40u:20u);
            for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                uint32_t expected=0x123456u;
                if(x>=sx*scale&&x<(sx+8u)*scale&&y>=sy*scale&&y<(sy+8u)*scale)
                    expected=blink?0xff0000u:0x0000ffu;
                if(x>=gx*scale&&x<(gx+8u)*scale&&y>=gy*scale&&y<(gy+8u)*scale)
                    expected=0xffffffu; /* ghost8000 chooses tile119, palette7 */
                CHECK(pixels[(size_t)y*stride+x]==expected);
            }
        }
        entries[1].class_flags=8u;
        for(size_t i=0u;i<(size_t)stride*160u*scale;++i)pixels[i]=0x123456u;
        CHECK(portable_gp_results_render_race_markers(&art,entries,2u,21u,0u,&frame));
        CHECK(pixels[112u*scale*stride+272u*scale]==0x123456u); /* 4C8 visibility gate */
        entries[0].class_flags=8u;
        CHECK(portable_gp_results_render_race_markers(&art,entries,2u,21u,0u,&frame));
        CHECK(pixels[112u*scale*stride+272u*scale]==0x00ff00u);
        entries[0].class_flags=entries[1].class_flags=0u;
        CHECK(entries[1].source_flags==0x8000u&&tiles[0]==0x11u&&palette7[1]==0x7c00u);
        free(pixels);
    }
    return 0;
}

/* Independent pixel-coordinate specification of 801E248's HUD copies. */
static int lap_hud_source(unsigned x,unsigned y,unsigned p,unsigned width,unsigned *sx,unsigned *sy)
{
    const int tx=(int)x/8,ty=(int)y/8;
    *sx=x;*sy=y;
    if(ty>=2 && ty<15 && tx<4-(int)p){*sx=x+p*8u;return 1;}
    if(tx>=4 && tx<17 && ty<4-(int)p){*sy=y+p*8u;return 1;}
    const int map_x=(int)x-(int)(width-240u);
    if(map_x>=176+(int)p*8 && ty>=12+(int)p){
        *sx=(unsigned)map_x-p*8u;*sy=y-p*8u;return 1;
    }
    if(tx>=1 && tx<6 && ty>=16 && ty<19)return 1;
    return tx<3 && ty==1;
}
static int quick_selected_palette(void)
{
    uint8_t tiles[PORTABLE_GP_RESULTS_TILE_BYTES]={0};
    uint16_t palette[256]={0},background[1024]={0},names[8u*18u]={0},footer[9]={0};
    uint16_t window_map[640]={0},scroll[384]={0},base[16]={0},pulse[16]={0},factors[64]={0};
    uint32_t pixels[240u*160u];
    memset(tiles+32u,0x11,32u);
    for(unsigned i=0x100u;i<0x140u;++i)memset(tiles+i*32u,0x11,32u);
    for(unsigned y=9u;y<11u;++y)for(unsigned x=22u;x<29u;++x)window_map[y*32u+x]=UINT16_C(0xf001);
    base[1]=31u<<5u;pulse[1]=31u;factors[1]=32768u;palette[15u*16u+1u]=31u<<10u;
    PortableGpResultsAssetView window={.followup_tiles=tiles,.followup_tile_bytes=sizeof tiles,
        .followup_palette=palette,.followup_palette_count=256u,.followup_map=window_map,.followup_map_count=640u,
        .scroll_map=scroll,.scroll_map_count=384u,.menu_palette=pulse,.menu_palette_count=16u,
        .menu_pulse_base_palette=base,.menu_pulse_base_palette_count=16u,
        .menu_pulse_factors=factors,.menu_pulse_factor_count=64u,.time_board_map=background,.time_board_map_count=1024u};
    PortableGpStandingsAssetView art={.tiles=tiles,.tile_bytes=PORTABLE_GP_STANDINGS_TILE_BYTES,
        .palette=palette,.palette_count=256u,.background_map=background,.background_map_count=1024u,
        .driver_cells=names,.driver_count=8u,.driver_cell_count=18u,.footer_cells=footer,.footer_cell_count=9u};
    const PortableTimeBoardRow rows[3]={{0u,0u,0u,0u,PORTABLE_TIME_BOARD_TIME,0u},
        {0u,3u,3u,1u,PORTABLE_TIME_BOARD_TIME,0u},{0u,5u,5u,2u,PORTABLE_TIME_BOARD_TIME,1u}};
    const PortableTimeBoardRow frozen=rows[2];
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u};
    for(unsigned phase=0u;phase<2u;++phase){
        CHECK(portable_gp_results_menu_colors(&window,phase,palette+14u*16u));
        memset(pixels,0,sizeof pixels);
        CHECK(portable_time_board_render(&art,&window,rows,3u,0u,&frame));
        CHECK(portable_gp_results_render_action_window(&window,0u,0u,phase,&frame));
        const uint32_t expected=phase?UINT32_C(0x00ff0000):UINT32_C(0x0000ff00);
        CHECK((pixels[56u*240u+104u]&UINT32_C(0xffffff))==expected);
        CHECK((pixels[72u*240u+176u]&UINT32_C(0xffffff))==expected);
        CHECK(pixels[24u*240u+104u]==UINT32_C(0xff0000ff)); /* seat0 is not selected */
    }
    CHECK(!memcmp(&frozen,&rows[2],sizeof frozen));
    CHECK(!portable_gp_results_menu_colors(NULL,0u,palette));
    return 0;
}

static int quick_window_pixels(const PortableGpResultsAssetView *base)
{
    PortableGpResultsAssetView asset=*base;
    uint8_t tiles[PORTABLE_GP_RESULTS_TILE_BYTES]={0};
    uint16_t map[640]={0},palette[256]={0};
    memset(tiles+32u,0x11,32u);memset(tiles+64u,0x22,32u);
    palette[1]=31u;palette[2]=31u<<5u;
    for(unsigned y=8u;y<20u;++y)for(unsigned x=20u;x<30u;++x)map[y*32u+x]=1u;
    map[8u*32u+20u]=2u;
    asset.followup_tiles=tiles;asset.followup_map=map;asset.followup_palette=palette;
    const uint32_t untouched=UINT32_C(0xff123456);
    for(unsigned scale=1u;scale<=4u;scale+=3u){
    const unsigned stride=240u*scale+11u;
    const size_t count=(size_t)stride*160u*scale;
    uint32_t *pixels=malloc(count*sizeof *pixels);CHECK(pixels!=NULL);
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u,
        .scale=(uint8_t)scale,.stride=stride};
    for(unsigned step=0u;step<=6u;++step){
        for(size_t i=0u;i<count;++i)pixels[i]=untouched;
        CHECK(portable_gp_results_render_action_window_open(&asset,(uint8_t)step,0u,&frame));
        const unsigned w=step<5u?step:5u;
        for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
            const int inside=x>=(25u-w)*8u*scale&&x<(25u+w)*8u*scale&&y>=(14u-step)*8u*scale&&y<(14u+step)*8u*scale;
            CHECK((pixels[(size_t)y*stride+x]!=untouched)==inside);
        }
        if(step)CHECK(pixels[(size_t)(14u-step)*8u*scale*stride+(25u-w)*8u*scale]==UINT32_C(0x0000ff00));
    }
    CHECK(!portable_gp_results_render_action_window_open(&asset,7u,0u,&frame));
    free(pixels);
    }
    CHECK(map[8u*32u+20u]==2u&&palette[1]==31u);
    return 0;
}

static int test_lap_hud_regions(const PortableGpResultsAssetView *source)
{
    PortableGpResultsAssetView asset=*source;
    const uint16_t empty[1024]={0};asset.background_map=empty;
    PortableLapBoardHud *hud=malloc(sizeof *hud);CHECK(hud!=NULL);
    for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x)
        hud->pixels[y*240u+x]=(x%8u==0u)?PORTABLE_LAP_BOARD_HUD_TRANSPARENT:
            UINT32_C(0x00400000)+y*256u+x;
    const uint16_t times[6]={101u,202u,303u,404u,505u,1515u};
    const unsigned stages[4]={1u,3u,7u,8u};
    PortableGpResultsFlow valid_flow={.reveal_progress=1u};
    PortableSoftwareFrame undersized={.pixels=hud->pixels,.width=239u,.height=160u};
    CHECK(!portable_gp_results_render_laps_with_hud(&asset,&valid_flow,times,3u,NULL,&undersized));
    undersized.width=240u;undersized.height=159u;
    CHECK(!portable_gp_results_render_laps_with_hud(&asset,&valid_flow,times,3u,NULL,&undersized));
    for(unsigned lap=3u;lap<=5u;lap+=2u)for(unsigned si=0;si<4u;++si)
        for(unsigned scale=1u;scale<=2u;++scale){
        const unsigned width=scale==1u?240u:373u;
        const size_t count=(size_t)width*160u*scale*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels),*baseline=malloc(count*sizeof *baseline);
        CHECK(pixels!=NULL && baseline!=NULL);
        for(size_t i=0;i<count;++i)pixels[i]=baseline[i]=UINT32_C(0x00234567);
        PortableSoftwareFrame full={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale};
        PortableSoftwareFrame original=full;original.pixels=baseline;
        PortableGpResultsFlow flow;portable_gp_results_flow_init(&flow);flow.state=2u;
        while(flow.reveal_progress<stages[si])
            CHECK(portable_gp_results_flow_tick_reveal(&flow,(uint8_t)lap)==PORTABLE_GP_RESULTS_OK);
        /* Deliberately put authored board art inside the source-copy areas. */
        flow.header_x=4u;flow.header_y=0u;
        flow.rows[0].x=1u;flow.rows[0].y=2u;
        const PortableGpResultsFlow before=flow;
        CHECK(portable_gp_results_render_laps_with_hud(&asset,&flow,times,(uint8_t)lap,NULL,&original));
        CHECK(portable_gp_results_render_laps_with_hud(&asset,&flow,times,(uint8_t)lap,hud,&full));
        CHECK(!memcmp(&flow,&before,sizeof flow));
        for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<width;++x){
            uint32_t expected=portable_software_frame_read(&original,(size_t)y*width+x);
            unsigned sx,sy;
            if(lap_hud_source(x,y,stages[si],width,&sx,&sy)){
                expected=hud->pixels[sy*240u+sx];
                if(expected==PORTABLE_LAP_BOARD_HUD_TRANSPARENT)expected=UINT32_C(0x00234567);
            }
            for(unsigned py=0;py<scale;++py)for(unsigned px=0;px<scale;++px){
                const uint32_t actual=pixels[((size_t)y*scale+py)*width*scale+x*scale+px];
                if(actual!=expected){
                    fprintf(stderr,"lap HUD pixel lap=%u progress=%u scale=%u x=%u y=%u sub=%u,%u actual=%08x expected=%08x\n",
                        lap,stages[si],scale,x,y,px,py,(unsigned)actual,(unsigned)expected);
                    CHECK(actual==expected);
                }
            }
        }
        const uint32_t digest=portable_software_frame_checksum(&full);
        /* Restore the world before repainting the same immutable board. */
        for(size_t i=0;i<count;++i)pixels[i]=UINT32_C(0x00234567);
        CHECK(portable_gp_results_render_laps_with_hud(&asset,&flow,times,(uint8_t)lap,hud,&full));
        CHECK(portable_software_frame_checksum(&full)==digest);
        free(baseline);free(pixels);
    }
    free(hud);return 0;
}

static int test_rom_results(const char *path,const char *directory)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL);
    CHECK(fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length&&fclose(file)==0);
    PortableAssetPackView pack;
    PortableContentSelection selection;
    PortableRecoveredPackStorage *race_storage=calloc(1,sizeof *race_storage);
    PortablePresentationPackStorage *art=calloc(1,sizeof *art);
    PortableRecoveredRaceView race;
    PortablePresentationPackView view;
    CHECK(race_storage!=NULL&&art!=NULL);
    CHECK(portable_asset_pack_open(bytes,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_content_registry_select(&pack,24u,1u,&selection)==PORTABLE_CONTENT_OK);
    CHECK(portable_recovered_pack_open_race(&pack,&selection,race_storage,&race)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(portable_presentation_pack_open(&pack,&selection,race_storage,&race,art,&view)==PORTABLE_RECOVERED_PACK_OK);
    /* Captured result revisions must fail instead of selecting a different
     * presentation for a particular driver, rank or recorded lap time. */
    for(uint32_t index=0;index<pack.section_count;++index){
        PortableAssetSectionView section;
        CHECK(portable_asset_pack_section_at(&pack,index,&section)==PORTABLE_ASSET_PACK_OK);
        if(section.kind!=PORTABLE_ASSET_SECTION_GP_RESULTS_GRAPHICS)continue;
        uint8_t *version=bytes+pack.section_table_offset+index*PORTABLE_ASSET_PACK_SECTION_SIZE+8u;
        CHECK(version[0]==8u);
        for(uint8_t revision=0;revision<=9u;++revision){
            if(revision==8u)continue;
            version[0]=revision;
            CHECK(portable_presentation_pack_open(&pack,&selection,race_storage,&race,art,&view)==
                PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
        }
        version[0]=8u;
        uint8_t *tail=bytes+(section.bytes-bytes)+section.size-13u;
        for(unsigned field=0u;field<2u;++field){
            const unsigned offset=field?8u:0u;const uint8_t saved=tail[offset];
            tail[offset]=(uint8_t)(field?5u:8u);
            CHECK(portable_presentation_pack_open(&pack,&selection,race_storage,&race,art,&view)==PORTABLE_RECOVERED_PACK_BAD_PAYLOAD);
            tail[offset]=saved;
        }
        PortableAssetSectionView *sections=calloc(pack.section_count,sizeof *sections);CHECK(sections!=NULL);
        for(uint32_t j=0u;j<pack.section_count;++j)
            CHECK(portable_asset_pack_section_at(&pack,j,&sections[j])==PORTABLE_ASSET_PACK_OK);
        sections[index].version=7u;sections[index].size-=13u;
        PortableAssetPackView legacy=pack;legacy.resolved_sections=sections;
        CHECK(portable_presentation_pack_open(&legacy,&selection,race_storage,&race,art,&view)==PORTABLE_RECOVERED_PACK_OK);
        CHECK(view.gp_results.record_identities==NULL&&view.gp_results.menu_option_codes==NULL);
        free(sections);
    }
    CHECK(portable_presentation_pack_open(&pack,&selection,race_storage,&race,art,&view)==PORTABLE_RECOVERED_PACK_OK);
    uint32_t pixels[240u*160u],checksums[8];
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    for(uint8_t rank=0;rank<8u;++rank){
        PortableGpResultsFlow flow;
        portable_gp_results_flow_init(&flow);
        for(unsigned tick=0;flow.state<2u;++tick){
            CHECK(tick<200u);
            CHECK(portable_gp_results_flow_tick_entry(&flow,0u,0u)==PORTABLE_GP_RESULTS_OK);
        }
        while(flow.reveal_progress<8u)
            CHECK(portable_gp_results_flow_tick_reveal(&flow,3u)==PORTABLE_GP_RESULTS_OK);
        const uint16_t times[PORTABLE_GP_RESULTS_MAX_ROWS]={(uint16_t)(1234u+rank*111u),
            (uint16_t)(2345u+rank*123u),(uint16_t)(3456u+rank*137u),
            (uint16_t)(7035u+rank*371u)};
        portable_software_frame_clear(&frame,UINT32_C(0xff335577));
        CHECK(portable_gp_results_render(&view.gp_results,&flow,times,UINT8_MAX,&frame));
        CHECK(portable_gp_results_render_coins(&view.gp_results,(uint8_t)(rank*13u),&frame));
        CHECK(portable_gp_results_render_rank(&view.gp_results,rank,&frame));
        checksums[rank]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0;prior<rank;++prior)CHECK(checksums[rank]!=checksums[prior]);
        char output[1024];
        CHECK(snprintf(output,sizeof output,"%s/results-%u.ppm",directory,rank)>0);
        CHECK(portable_software_frame_write_ppm(&frame,output));
        /* Rendering must not consume reveal ticks or alter the input times. */
        CHECK(flow.reveal_progress==8u&&times[0]==1234u+rank*111u);
    }
    free(art);free(race_storage);free(bytes);
    puts("ROM result art and dynamic race values: PASS");
    return 0;
}

static int physical_lakitu_pixels(void)
{
    uint8_t art[PORTABLE_GP_RESULTS_LAKITU_ART_BYTES]={0};
    uint8_t record[PORTABLE_GP_RESULTS_LAKITU_FRAME_BYTES]={0};
    const uint16_t palette[16]={0u,31u,31u<<5u};
    record[1]=1u;record[2]=40u;record[3]=3u;record[4]=30u;record[5]=0x40u;
    record[6]=0x40u;record[7]=0x75u;
    const int16_t matrix[4]={193,61,-47,211};
    for(unsigned i=0;i<4u;++i){record[20u+i*2u]=(uint8_t)matrix[i];record[21u+i*2u]=(uint8_t)((uint16_t)matrix[i]>>8u);}
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x){
        const unsigned index=(x+2u*y)%3u;
        const size_t offset=((y/8u)*2u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        art[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    const PortableGpResultsAssetView asset={.lakitu_art=art,.lakitu_art_count=1u,.lakitu_palette=palette};
    const PortableGpResultsAssetView before=asset;
    static uint32_t pixels[963u*640u],native[240u*160u];
    for(unsigned scale=1;scale<=4u;scale*=4u){
        const unsigned stride=240u*scale+3u;
        PortableSoftwareFrame frame={pixels,240u,160u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(unsigned i=0;i<stride*160u*scale;++i)pixels[i]=0x123456u;
        CHECK(portable_gp_results_render_lakitu_record(&asset,record,&frame));
        CHECK(!memcmp(&asset,&before,sizeof asset));
        unsigned differs=0u;
        for(unsigned y=0;y<160u*scale;++y)for(unsigned x=0;x<stride;++x){
            uint32_t expected=0x123456u;
            const int dx=(int)x-30*(int)scale,dy=(int)y-40*(int)scale;
            const int64_t cx=dx-16*(int)scale,cy=dy-16*(int)scale;
            const int sx=8+(int)portable_software_frame_floor_div(193*cx+61*cy,256*(int)scale);
            const int sy=8+(int)portable_software_frame_floor_div(-47*cx+211*cy,256*(int)scale);
            if(x<240u*scale && dx>=0 && dy>=0 && dx<32*(int)scale && dy<32*(int)scale && sx>=0 && sx<16 && sy>=0 && sy<16){
                const unsigned index=(unsigned)(sx+2*sy)%3u;
                if(index)expected=index==1u?0xff0000u:0x00ff00u;
            }
            CHECK(pixels[y*stride+x]==expected);
            if(x<240u*scale){
                if(scale==1u)native[y*240u+x]=pixels[y*stride+x];
                else differs+=pixels[y*stride+x]!=native[y/scale*240u+x/scale];
            }
        }
        CHECK(scale==1u || differs>0u);
    }
    return 0;
}

static void custom_pixel(uint8_t *bytes,unsigned x,unsigned y,unsigned width,uint32_t color)
{
    const size_t offset=((size_t)y*width+x)*4u;
    for(unsigned byte=0u;byte<4u;++byte)bytes[offset+byte]=(uint8_t)(color>>(8u*byte));
}

static int custom_record_pixels(const PortableGpResultsAssetView *source)
{
    uint8_t portrait_bytes[16u*16u*4u],name_bytes[40u*16u*4u];
    for(unsigned y=0u;y<16u;++y){
        for(unsigned x=0u;x<16u;++x)custom_pixel(portrait_bytes,x,y,16u,
            x==y?0u:UINT32_C(0xff109060)+(x+y));
        for(unsigned x=0u;x<40u;++x)custom_pixel(name_bytes,x,y,40u,
            (x+y)%3u?UINT32_C(0xffd04090)+(x+2u*y):0u);
    }
    const PortableNativeImage portrait={.pixels=portrait_bytes,.width=16u,.height=16u};
    const PortableNativeImage name={.pixels=name_bytes,.width=40u,.height=16u};
    PortableGpResultsRecord records[6]={{.flags=0u,.time_centiseconds=100u,.roster_driver_id=9u},
        {.flags=1u,.time_centiseconds=200u},{.flags=2u},{.flags=3u},{.flags=4u},{.flags=0x10u}};
    PortableGpResultsFlow flow;portable_gp_results_flow_init(&flow);flow.state=6u;
    CHECK(portable_gp_results_flow_tick_record_setup(&flow,records,source->record_identities)==PORTABLE_GP_RESULTS_OK&&
        flow.record_rows[0].roster_driver_id==9u);
    const uint32_t background=UINT32_C(0x00314259),guard=UINT32_C(0xa1b2c3d4);
    for(unsigned sliding=0u;sliding<3u;++sliding){
        if(sliding)for(unsigned tick=0u;tick<16u;++tick)
            CHECK(portable_gp_results_flow_tick_record_slide(&flow,0u)==PORTABLE_GP_RESULTS_OK);
        const unsigned offset=sliding==1u?176u:sliding==2u?48u:0u; /* y4 starts38 tiles; test16/32 source steps */
        for(unsigned scale=1u;scale<=4u;scale+=3u){
            const size_t stride=240u*scale+7u,count=stride*160u*scale;
            uint32_t *stock=malloc(count*sizeof *stock),*custom=malloc(count*sizeof *custom);
            CHECK(stock&&custom);
            PortableSoftwareFrame stock_frame={.pixels=stock,.width=240u,.height=160u,.scale=(uint8_t)scale,.stride=stride};
            PortableSoftwareFrame custom_frame=stock_frame;custom_frame.pixels=custom;
            for(size_t i=0u;i<count;++i)stock[i]=(i%stride<240u*scale)?background:guard;
            CHECK(sliding?portable_gp_results_render_record_slide(source,&flow,&stock_frame):
                portable_gp_results_render_record_setup(source,&flow,&stock_frame));
            for(unsigned variant=0u;variant<4u;++variant){
                PortableGpResultsAssetView asset=*source;
                asset.record_art[0]=(PortableGpResultsRecordArt){9u,variant&1u?&portrait:NULL,variant&2u?&name:NULL};
                PortableGpResultsAssetView before;memcpy(&before,&asset,sizeof asset);
                PortableGpResultsFlow frozen;memcpy(&frozen,&flow,sizeof flow);
                for(size_t i=0u;i<count;++i)custom[i]=(i%stride<240u*scale)?background:guard;
                CHECK(sliding?portable_gp_results_render_record_slide(&asset,&flow,&custom_frame):
                    portable_gp_results_render_record_setup(&asset,&flow,&custom_frame));
                unsigned changes=0u;
                for(size_t y=0u;y<160u*scale;++y)for(size_t x=0u;x<stride;++x){
                    uint32_t expected=stock[y*stride+x];
                    const unsigned sx=(unsigned)(x/scale),sy=(unsigned)(y/scale);
                    const PortableNativeImage *image=NULL;unsigned left=0u;
                    if(x<240u*scale&&sy>=32u&&sy<48u){
                        if((variant&1u)&&sx>=16u+offset&&sx<32u+offset){image=&portrait;left=16u+offset;}
                        if((variant&2u)&&sx>=48u+offset&&sx<88u+offset){image=&name;left=48u+offset;}
                    }
                    if(image){const uint32_t pixel=portable_native_image_pixel(image,sx-left,sy-32u);
                        expected=(pixel>>24u)?pixel:background;}
                    CHECK(custom[y*stride+x]==expected);changes+=custom[y*stride+x]!=stock[y*stride+x];
                }
                CHECK((variant==0u)==(changes==0u));
                CHECK(memcmp(&before,&asset,sizeof asset)==0&&memcmp(&frozen,&flow,sizeof flow)==0);
            }
            free(stock);free(custom);
        }
    }
    return 0;
}

int main(int argc,char **argv)
{
    CHECK(source_race_marker_pixels()==0);
    CHECK(fractional_race_marker_pixels()==0);
    if(argc==3)return test_rom_results(argv[1],argv[2]);
    CHECK(argc==1);
    CHECK(physical_lakitu_pixels()==0);

    uint8_t tiles[PORTABLE_GP_RESULTS_TILE_BYTES];
    uint16_t palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    uint16_t digits[PORTABLE_GP_RESULTS_DIGIT_COUNT];
    uint8_t headers[PORTABLE_GP_RESULTS_HEADER_COUNT *
                    PORTABLE_GP_RESULTS_HEADER_BYTES] = {0};
    uint8_t rows[PORTABLE_GP_RESULTS_ROW_COUNT *
                 PORTABLE_GP_RESULTS_ROW_BYTES] = {0};
    uint16_t row_bytes[PORTABLE_GP_RESULTS_ROW_COUNT] = {12u,12u,12u,32u};
    uint16_t background[PORTABLE_GP_RESULTS_MAP_COUNT] = {0};
    uint8_t followup_tiles[PORTABLE_GP_RESULTS_TILE_BYTES];
    uint16_t followup_palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    uint16_t followup_map[PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT] = {0};
    uint16_t scroll_map[PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT] = {0};
    uint16_t record_patterns[PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT *
                             PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS];
    uint8_t object_tiles[PORTABLE_GP_RESULTS_OBJECT_TILE_BYTES] = {0};
    uint16_t object_palette[PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT] = {0};
    uint16_t menu_palette[PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT];
    uint16_t menu_pulse_base_palette[PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT];
    uint16_t menu_pulse_factors[PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT];
    uint8_t rank_tiles[PORTABLE_GP_RESULTS_RANK_COUNT *
                       PORTABLE_GP_RESULTS_RANK_TILE_BYTES] = {0};
    uint16_t rank_palette[PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT] = {0};
    uint8_t rank_colors[PORTABLE_GP_RESULTS_RANK_COUNT*4u*2u] = {0};
    uint16_t times[PORTABLE_GP_RESULTS_MAX_ROWS] = {1234u,5678u,9012u,3456u};
    uint32_t pixels[240u * 160u];
    PortableSoftwareFrame frame;
    PortableGpResultsFlow flow;
    /* DB7C8: five 2x1 lap labels and the 14x1 divider. */
    uint8_t compact_rows[PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT * 32u] = {0};
    uint16_t compact_bytes[PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT];
    for (unsigned row = 0u; row < PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT; ++row) {
        const unsigned width = row < 5u ? 2u : 14u;
        uint8_t *pattern = compact_rows + row * 32u;
        pattern[0] = (uint8_t)width; pattern[2] = 1u;
        for (unsigned cell = 0u; cell < width; ++cell) {
            pattern[4u + cell * 2u] = (uint8_t)(0x30u + row);
            pattern[5u + cell * 2u] = 0xc0u;
        }
        compact_bytes[row] = (uint16_t)(4u + width * 2u);
    }
    PortableGpResultsAssetView asset = {
        tiles,sizeof tiles,palette,PORTABLE_GP_RESULTS_PALETTE_COUNT,
        digits,PORTABLE_GP_RESULTS_DIGIT_COUNT,headers,
        PORTABLE_GP_RESULTS_HEADER_COUNT,rows,row_bytes,
        PORTABLE_GP_RESULTS_ROW_COUNT,background,
        PORTABLE_GP_RESULTS_MAP_COUNT,followup_tiles,sizeof followup_tiles,
        followup_palette,PORTABLE_GP_RESULTS_PALETTE_COUNT,followup_map,
        PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT,scroll_map,
        PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT,record_patterns,
        PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT,object_tiles,
        sizeof object_tiles,object_palette,
        PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT,menu_palette,
        PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,menu_pulse_base_palette,
        PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,menu_pulse_factors,
        PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT,rank_tiles,
        sizeof rank_tiles,rank_palette,PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT,
        compact_rows,compact_bytes,PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT,NULL,0u,NULL,0u,NULL,NULL,NULL,{{0}}};
    const uint8_t record_map[8]={7,6,5,4,3,2,1,0},option_map[5]={0,3,1,2,4};
    asset.record_identities=record_map;asset.menu_option_codes=option_map;
    uint32_t first_checksum;
    uint32_t wipe_first_checksum;
    for (size_t index = 0u; index < sizeof tiles; ++index)
        tiles[index] = (uint8_t)(((index / 32u) % 15u) + 1u) * UINT8_C(0x11);
    memcpy(followup_tiles,tiles,sizeof followup_tiles);
    memset(tiles,0,32u); /* tile zero is transparent over the race scene */
    for (size_t index = 0u; index < PORTABLE_GP_RESULTS_PALETTE_COUNT; ++index)
        palette[index] = (uint16_t)(index & UINT16_C(0x7fff));
    rank_palette[1] = UINT16_C(0x001f);
    rank_palette[9] = UINT16_C(0x001f);
    rank_colors[0] = UINT8_C(0xff);
    rank_colors[1] = UINT8_C(0xff);
    rank_tiles[0] = UINT8_C(0x09);
    rank_tiles[PORTABLE_GP_RESULTS_RANK_TILE_BYTES] = UINT8_C(0x10);
    memcpy(followup_palette,palette,sizeof followup_palette);
    for(size_t index=0u;index<PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT;++index)
    {
        menu_palette[index]=(uint16_t)(UINT16_C(0x7c00)+index);
        menu_pulse_base_palette[index]=(uint16_t)(UINT16_C(0x001f)+index);
    }
    for(size_t index=0u;index<PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT;
        ++index)menu_pulse_factors[index]=(uint16_t)(index*512u);
    for(size_t index=0u;index<sizeof record_patterns/sizeof *record_patterns;
        ++index)record_patterns[index]=(uint16_t)(UINT16_C(0xf080)+index%32u);
    for (size_t index = 0u; index < PORTABLE_GP_RESULTS_DIGIT_COUNT; ++index)
        digits[index] = (uint16_t)(0x20u + index);
    pattern(headers,3u,2u,UINT16_C(0xc030));
    pattern(headers+PORTABLE_GP_RESULTS_HEADER_BYTES,3u,2u,UINT16_C(0xc040));
    for (size_t index = 0u; index < 3u; ++index)
        pattern(rows+index*PORTABLE_GP_RESULTS_ROW_BYTES,2u,2u,
                (uint16_t)(UINT16_C(0xc050)+index*4u));
    pattern(rows+3u*PORTABLE_GP_RESULTS_ROW_BYTES,14u,1u,UINT16_C(0xc060));
    background[16u*32u+1u]=UINT16_C(0xc070);
    for(size_t row=1u;row<=9u;++row)
        for(size_t column=0u;column<32u;++column)
            background[row*32u+column]=UINT16_C(0xc070);
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    for(size_t index=0u;index<240u*160u;++index)
        pixels[index]=UINT32_C(0x00123456);
    portable_gp_results_flow_init(&flow);
    flow.state=2u;
    CHECK(portable_gp_results_flow_tick_reveal(&flow,3u)==PORTABLE_GP_RESULTS_OK);
    CHECK(portable_gp_results_flow_tick_reveal(&flow,3u)==PORTABLE_GP_RESULTS_OK);
    CHECK(portable_gp_results_render(&asset,&flow,times,0u,&frame));
    pixels[128u*240u+8u]=0u;
    pixels[128u*240u+9u]=0u;
    CHECK(portable_gp_results_render_rank(&asset,0u,&frame));
    CHECK(pixels[128u*240u+8u]==UINT32_C(0x00ff0000));
    CHECK(pixels[128u*240u+9u]==0u);
    CHECK(portable_gp_results_render_rank_with_colors(
        &asset,0u,rank_colors,&frame));
    CHECK(pixels[128u*240u+8u]==UINT32_C(0x00ffffff));
    pixels[128u*240u+8u]=0u;
    pixels[128u*240u+9u]=0u;
    CHECK(portable_gp_results_render_rank(&asset,1u,&frame));
    CHECK(pixels[128u*240u+8u]==0u);
    CHECK(pixels[128u*240u+9u]!=0u);
    CHECK(!portable_gp_results_render_rank(&asset,8u,&frame));
    /* The coin retains HUD bank C instead of the rank-label replacement. */
    palette[12u*16u+3u]=UINT16_C(0x03ff);
    rank_palette[3]=0u;
    CHECK(portable_gp_results_render_coins(&asset,6u,&frame));
    CHECK((pixels[8u*240u]&UINT32_C(0x00ffffff))==UINT32_C(0x00ffff00));
    const uint32_t six_coin_pixel=pixels[8u*240u+16u];
    CHECK(portable_gp_results_render_coins(&asset,7u,&frame));
    CHECK(pixels[8u*240u+16u]!=six_coin_pixel);
    CHECK(!portable_gp_results_render_coins(&asset,100u,&frame));
    CHECK(pixels[159u*240u+239u]==UINT32_C(0x00123456));
    first_checksum=portable_software_frame_checksum(&frame);
    CHECK(first_checksum!=0u);
    for(unsigned progress=2u;progress<=8u;++progress){
        CHECK(portable_gp_results_flow_tick_reveal(&flow,3u)==PORTABLE_GP_RESULTS_OK);
        CHECK(portable_gp_results_flow_tick_reveal(&flow,3u)==PORTABLE_GP_RESULTS_OK);
    }
    CHECK(flow.row_count==4u&&flow.rows[3].x==8u);
    for(size_t index=0u;index<240u*160u;++index)
        pixels[index]=UINT32_C(0x00123456);
    CHECK(portable_gp_results_render(&asset,&flow,times,0u,&frame));
    CHECK(portable_software_frame_checksum(&frame)!=first_checksum);
    {
        const uint32_t original_checksum=portable_software_frame_checksum(&frame);
        times[0]=4321u;
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render(&asset,&flow,times,0u,&frame));
        CHECK(portable_software_frame_checksum(&frame)!=original_checksum);
        times[0]=1234u;
    }
    flow.state=4u;
    flow.exit_wipe_progress=0u;
    CHECK(portable_gp_results_flow_tick_exit_wipe(&flow,0u)==
          PORTABLE_GP_RESULTS_OK);
    for(size_t index=0u;index<240u*160u;++index)
        pixels[index]=UINT32_C(0x00123456);
    CHECK(portable_gp_results_render_exit_wipe(&asset,&flow,times,3u,0u,&frame));
    wipe_first_checksum=portable_software_frame_checksum(&frame);
    for(unsigned tick=2u;tick<=13u;++tick)
        CHECK(portable_gp_results_flow_tick_exit_wipe(&flow,0u)==
              PORTABLE_GP_RESULTS_OK);
    for(size_t index=0u;index<240u*160u;++index)
        pixels[index]=UINT32_C(0x00123456);
    CHECK(portable_gp_results_render_exit_wipe(&asset,&flow,times,3u,0u,&frame));
    CHECK(portable_software_frame_checksum(&frame)!=wipe_first_checksum);
    CHECK(!portable_gp_results_render_exit_wipe(&asset,&flow,times,4u,0u,&frame));
    flow.state=5u;
    followup_map[0]=UINT16_C(0xc070);
    CHECK(portable_gp_results_flow_tick_followup_setup(&flow)==
          PORTABLE_GP_RESULTS_OK);
    for(size_t index=0u;index<240u*160u;++index)
        pixels[index]=UINT32_C(0x00123456);
    CHECK(portable_gp_results_render_followup_setup(&asset,&flow,&frame));
    CHECK(pixels[0]!=UINT32_C(0x00123456));
    asset.followup_map_count=0u;
    CHECK(!portable_gp_results_render_followup_setup(&asset,&flow,&frame));
    asset.followup_map_count=PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT;
    {
        PortableGpResultsRecord records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT]={
            {0u,1234u,0u,0u},{1u,5678u,1u,0u},{2u,9012u,0u,0u},
            {3u,3456u,0u,0u},{4u,7890u,1u,0u},{UINT32_C(0x10),59999u,0u,0u}};
        CHECK(portable_gp_results_flow_tick_record_setup(&flow,records,asset.record_identities)==
              PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_record_setup(&asset,&flow,&frame));
        CHECK(pixels[(size_t)4u*8u*240u+8u]!=UINT32_C(0x00123456));
        asset.record_pattern_count=0u;
        CHECK(!portable_gp_results_render_record_setup(&asset,&flow,&frame));
        asset.record_pattern_count=PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT;
        CHECK(portable_gp_results_flow_tick_record_slide(&flow,0u)==
              PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_record_slide(&asset,&flow,&frame));
        CHECK(portable_software_frame_checksum(&frame)!=0u);
        for(unsigned tick=2u;tick<=32u;++tick)
            CHECK(portable_gp_results_flow_tick_record_slide(&flow,0u)==
                  PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_record_slide(&asset,&flow,&frame));
        CHECK(pixels[(size_t)4u*8u*240u+8u]!=UINT32_C(0x00123456));
        for(unsigned tick=33u;tick<=59u;++tick)
            CHECK(portable_gp_results_flow_tick_record_slide(&flow,0u)==
                  PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_record_slide(&asset,&flow,&frame));
        CHECK(pixels[(size_t)4u*8u*240u+8u]!=UINT32_C(0x00123456));
        CHECK(portable_gp_results_flow_tick_record_slide(&flow,0u)==
              PORTABLE_GP_RESULTS_OK);
        CHECK(flow.state==8u);
        CHECK(portable_gp_results_flow_tick_menu_reveal(&flow,0u)==
              PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_menu_reveal(&asset,&flow,&frame));
        for(unsigned progress=2u;progress<=5u;++progress)
            CHECK(portable_gp_results_flow_tick_menu_reveal(&flow,0u)==
                  PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_menu_reveal(&asset,&flow,&frame));
        CHECK(pixels[(size_t)8u*8u*240u+20u*8u]!=UINT32_C(0x00123456));
        CHECK(portable_gp_results_flow_tick_menu_reveal(&flow,0u)==
              PORTABLE_GP_RESULTS_OK);
        CHECK(flow.state==9u);
        CHECK(portable_gp_results_flow_tick_menu_setup(&flow,UINT16_C(2))==
              PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_menu_setup(&asset,&flow,&frame));
        CHECK(pixels[(size_t)11u*8u*240u+22u*8u]!=
              UINT32_C(0x00123456));
        CHECK(portable_gp_results_flow_tick_menu_record_setup(&flow)==
              PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_record_setup(&asset,&flow,&frame));
        CHECK(pixels[(size_t)4u*8u*240u+8u]!=UINT32_C(0x00123456));
        CHECK(portable_gp_results_flow_tick_menu_selection(
                  &flow,0u,0u,UINT16_C(2),asset.menu_option_codes)==PORTABLE_GP_RESULTS_OK);
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_menu_selection(
                  &asset,&flow,UINT16_C(2),0u,&frame));
        {
            const uint32_t selected_checksum=
                portable_software_frame_checksum(&frame);
            for(size_t index=0u;index<240u*160u;++index)
                pixels[index]=UINT32_C(0x00123456);
            CHECK(portable_gp_results_render_menu_selection(
                      &asset,&flow,UINT16_C(2),32u,&frame));
            CHECK(portable_software_frame_checksum(&frame)!=selected_checksum);
            CHECK(portable_gp_results_flow_tick_menu_selection(
                      &flow,UINT16_C(0x80),0u,UINT16_C(2),asset.menu_option_codes)==
                  PORTABLE_GP_RESULTS_OK);
            for(size_t index=0u;index<240u*160u;++index)
                pixels[index]=UINT32_C(0x00123456);
            CHECK(portable_gp_results_render_menu_selection(
                      &asset,&flow,UINT16_C(2),1u,&frame));
            CHECK(portable_software_frame_checksum(&frame)!=selected_checksum);
            CHECK(portable_gp_results_flow_tick_menu_selection(
                      &flow,0u,0u,UINT16_C(2),asset.menu_option_codes)==PORTABLE_GP_RESULTS_OK);
            for(size_t index=0u;index<240u*160u;++index)
                pixels[index]=UINT32_C(0x00123456);
            CHECK(portable_gp_results_render_menu_selection(
                      &asset,&flow,UINT16_C(2),32u,&frame));
            CHECK(flow.menu_disabled_pending);
        }
        flow.state=13u;
        flow.scene_request_pending=1u;
        flow.menu_selection=2u;
        for(size_t index=0u;index<240u*160u;++index)
            pixels[index]=UINT32_C(0x00123456);
        CHECK(portable_gp_results_render_terminal(
                  &asset,&flow,UINT16_C(2),33u,&frame));
        CHECK(portable_software_frame_checksum(&frame)!=0u);
        flow.scene_request_pending=0u;
        CHECK(!portable_gp_results_render_terminal(
                  &asset,&flow,UINT16_C(2),33u,&frame));
        flow.scene_request_pending=1u;
        asset.menu_palette_count=0u;
        CHECK(!portable_gp_results_render_menu_setup(&asset,&flow,&frame));
        asset.menu_palette_count=PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT;
        flow.menu_reveal_progress=0u;
        CHECK(!portable_gp_results_render_menu_reveal(&asset,&flow,&frame));
        flow.slide_band_count=0u;
        CHECK(!portable_gp_results_render_record_slide(&asset,&flow,&frame));
    }
    flow.reveal_progress=0u;
    CHECK(!portable_gp_results_render(&asset,&flow,times,0u,&frame));
    CHECK(!portable_gp_results_render(NULL,&flow,times,0u,&frame));
    {
        uint8_t lakitu_art[PORTABLE_GP_RESULTS_LAKITU_ART_BYTES]={0};
        uint8_t lakitu_record[PORTABLE_GP_RESULTS_LAKITU_FRAME_BYTES]={0};
        uint16_t lakitu_palette[16]={0};
        lakitu_art[0]=UINT8_C(0x01);
        lakitu_palette[1]=UINT16_C(0x001f);
        lakitu_record[1]=2u;
        /* An affine 8x8 cell with identity matrix at (30,40). */
        lakitu_record[2]=40u;
        lakitu_record[3]=1u;
        lakitu_record[4]=30u;
        lakitu_record[6]=0x40u;
        lakitu_record[7]=0x75u;
        lakitu_record[8]=100u;
        lakitu_record[10]=200u;
        lakitu_record[12]=0x50u;
        lakitu_record[13]=0x75u;
        lakitu_record[21]=1u;
        lakitu_record[27]=1u;
        asset.lakitu_art=lakitu_art;
        asset.lakitu_art_count=1u;
        asset.lakitu_palette=lakitu_palette;
        for(size_t index=0u;index<240u*160u;++index)pixels[index]=0u;
        CHECK(portable_gp_results_render_lakitu_record(
            &asset,lakitu_record,&frame));
        CHECK(pixels[40u*240u+30u]==UINT32_C(0x00ff0000));
    }
    {
        uint8_t marker_tiles[64];
        uint16_t player_palette[16]={0};
        uint16_t rival_palette[16]={0};
        int32_t marker_x[8],marker_y[8];
        PortableGpMinimapMarkerAssetView markers={
            marker_tiles,player_palette,rival_palette};
        memset(marker_tiles,UINT8_C(0x11),32u);
        memset(marker_tiles+32u,UINT8_C(0x22),32u);
        player_palette[1]=UINT16_C(0x001f); /* red */
        player_palette[2]=UINT16_C(0x03e0); /* green */
        rival_palette[1]=UINT16_C(0x7c00); /* blue */
        for(size_t index=0u;index<8u;++index){
            marker_x[index]=(int32_t)(48u<<21u);
            marker_y[index]=(int32_t)(48u<<21u);
        }
        marker_x[0]=(int32_t)(8u<<21u);
        marker_y[0]=(int32_t)(8u<<21u);
        for(size_t index=0u;index<240u*160u;++index)pixels[index]=0u;
        CHECK(portable_gp_results_render_minimap_markers_count(
            &markers,marker_x,marker_y,1u,1u,0u,NULL,&frame));
        CHECK(pixels[100u*240u+180u]==UINT32_C(0x000000ff)); /* tile118, palette7 */
        CHECK(pixels[140u*240u+220u]==0u);
        CHECK(!portable_gp_results_render_minimap_markers_count(
            &markers,marker_x,marker_y,0u,1u,0u,NULL,&frame));
        CHECK(portable_gp_results_render_minimap_markers(
            &markers,marker_x,marker_y,16u,1u,NULL,&frame));
        CHECK(pixels[100u*240u+180u]==UINT32_C(0x00ff0000)); /* tile unchanged, palette5 */
        CHECK(pixels[140u*240u+220u]==UINT32_C(0x000000ff));
        {
            uint32_t flags[8]={0};
            marker_x[7]=(int32_t)(28u<<21u);marker_y[7]=(int32_t)(18u<<21u);
            flags[7]=UINT32_C(0x20);
            for(size_t i=0u;i<240u*160u;++i)pixels[i]=0u;
            CHECK(portable_gp_results_render_minimap_markers(
                &markers,marker_x,marker_y,37u,1u,flags,&frame));
            CHECK(pixels[110u*240u+200u]==UINT32_C(0x0000ff00));
            CHECK(pixels[140u*240u+220u]==UINT32_C(0x000000ff));
        }
    }
    {
        const uint16_t five_times[6]={1000u,1100u,1200u,1300u,1400u,6000u};
        uint16_t changed[6];memcpy(changed,five_times,sizeof changed);
        /* Bank 12 carries the board digits; give each index its own colour. */
        for(unsigned index=0u;index<PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT;++index)
            rank_palette[index]=(uint16_t)(index*UINT16_C(0x0842));
        portable_gp_results_flow_init(&flow);
        flow.state=2u;
        while(flow.reveal_progress<8u)
            CHECK(portable_gp_results_flow_tick_reveal(&flow,5u)==PORTABLE_GP_RESULTS_OK);
        /* 801E248 past three laps: rows from y2 one apart, divider at x-1. */
        CHECK(flow.row_count==6u&&flow.rows[0].y==2u&&flow.rows[4].y==6u&&
              flow.rows[5].racer_index==5u&&flow.rows[5].x+1u==flow.rows[4].x);
        CHECK(portable_gp_results_render_laps(&asset,&flow,five_times,5u,&frame));
        const uint32_t five_checksum=portable_software_frame_checksum(&frame);
        changed[4]=1500u;
        CHECK(portable_gp_results_render_laps(&asset,&flow,changed,5u,&frame)&&
              portable_software_frame_checksum(&frame)!=five_checksum);
        CHECK(!portable_gp_results_render_laps(&asset,&flow,five_times,6u,&frame));
    }
    CHECK(quick_selected_palette()==0);
    CHECK(quick_window_pixels(&asset)==0);
    CHECK(test_lap_hud_regions(&asset)==0);
    CHECK(custom_record_pixels(&asset)==0);
    puts("GP results renderer: PASS reveal wipe followup records slide and menu setup");
    return 0;
}
