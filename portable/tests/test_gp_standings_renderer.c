#include "gp_standings_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "GP standings renderer failed line %d: %s\n", \
            __LINE__, #condition); exit(1); \
} } while (0)

static uint32_t portrait_color(uint16_t color)
{
    const uint32_t r=color&31u,g=(color>>5u)&31u,b=(color>>10u)&31u;
    return UINT32_C(0xff000000)|((r<<3u)|(r>>2u))<<16u|
        ((g<<3u)|(g>>2u))<<8u|((b<<3u)|(b>>2u));
}

/* Source DB3DC rows contain a mirrored shared A0/A1 border, a complete
 * 2x2 portrait, then the unmirrored border. Distinct authored texels expose
 * a wrong tile, half, crop, flip or donor even at the four interior corners. */
static void all_portrait_interiors(void)
{
    static uint8_t tiles[PORTABLE_GP_STANDINGS_TILE_BYTES],tiles_before[PORTABLE_GP_STANDINGS_TILE_BYTES];
    uint16_t palette[256]={0},background[1024]={0},drivers[8u*18u]={0},footer[9]={0};
    const uint8_t source_bases[8]={0x80u,0x84u,0x9cu,0x8cu,0x94u,0x98u,0x88u,0x90u};
    for(unsigned i=1u;i<16u;++i)palette[13u*16u+i]=(uint16_t)(i|((31u-i)<<5u)|((i*2u)<<10u));
    for(unsigned driver=0u;driver<8u;++driver){
        for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
            const unsigned index=1u+(driver*2u+x+3u*y)%13u;
            const size_t tile=source_bases[driver]+(y/8u)*2u+x/8u;
            tiles[tile*32u+(y&7u)*4u+(x&7u)/2u]|=(uint8_t)(index<<((x&1u)*4u));
        }
        for(unsigned half=0u;half<2u;++half){
            uint16_t *row=drivers+driver*18u+half*9u;
            row[0]=(uint16_t)(0xd4a0u+half);row[3]=(uint16_t)(0xd0a0u+half);
            row[1]=(uint16_t)(0xd000u+source_bases[driver]+half*2u);
            row[2]=(uint16_t)(row[1]+1u);
        }
    }
    for(unsigned half=0u;half<2u;++half)for(unsigned y=0u;y<8u;++y)for(unsigned x=0u;x<8u;++x){
        const unsigned index=x==7u || y==(half?7u:0u)?14u:0u;
        tiles[(0xa0u+half)*32u+y*4u+x/2u]|=(uint8_t)(index<<((x&1u)*4u));
    }
    uint8_t custom_pixels[16u*16u*4u];
    for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
        const size_t offset=(y*16u+x)*4u;
        custom_pixels[offset]=(uint8_t)(x+16u);custom_pixels[offset+1u]=(uint8_t)(y+32u);
        custom_pixels[offset+2u]=0xe0u;custom_pixels[offset+3u]=(uint8_t)((x+y)%3u?255u:0u);
    }
    const PortableNativeImage custom={.pixels=custom_pixels,.width=16u,.height=16u};
    PortableGpStandingsAssetView asset={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=256u,.background_map=background,.background_map_count=1024u,
        .driver_cells=drivers,.driver_count=8u,.driver_cell_count=18u,.footer_cells=footer,.footer_cell_count=9u};
    PortableGpStandingsSession session={.phase=PORTABLE_GP_STANDINGS_TRANSFER};
    for(unsigned racer=0u;racer<8u;++racer){
        session.cup.race_order[racer]=(uint8_t)racer;session.cup.standings_order[racer]=(uint8_t)(7u-racer);
        session.cup.rows[racer].racer_index=(uint8_t)racer;
        session.cup.rows[racer].driver_class=(uint8_t)((racer*3u)%8u);
        session.cup.rows[racer].finish_rank=(uint8_t)racer;session.cup.rows[racer].standing_rank=(uint8_t)(7u-racer);
    }
    memcpy(tiles_before,tiles,sizeof tiles);
    uint16_t drivers_before[8u*18u],palette_before[256],background_before[1024],footer_before[9];
    uint8_t custom_before[sizeof custom_pixels];
    memcpy(drivers_before,drivers,sizeof drivers);memcpy(palette_before,palette,sizeof palette);
    memcpy(background_before,background,sizeof background);memcpy(footer_before,footer,sizeof footer);
    memcpy(custom_before,custom_pixels,sizeof custom_pixels);
    const uint32_t guard=UINT32_C(0xff123456);
    for(unsigned wide=0u;wide<2u;++wide)for(unsigned enlarged=0u;enlarged<2u;++enlarged)
    for(unsigned sorted=0u;sorted<2u;++sorted)for(unsigned own=0u;own<2u;++own){
        const size_t width=wide?373u:240u,scale=enlarged?4u:1u,stride=width*scale+5u;
        const size_t count=stride*160u*scale,margin=(width-240u)/2u;
        uint32_t *pixels=malloc(count*sizeof *pixels);CHECK(pixels!=NULL);
        PortableSoftwareFrame full={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        PortableSoftwareFrame centered=portable_software_frame_native_view(&full,PORTABLE_FRAME_CENTER);
        asset.racer_portraits[5]=own?&custom:NULL;
        session.sorted_order=(uint8_t)sorted;
        session.phase=sorted?PORTABLE_GP_STANDINGS_SORTED_HOLD:PORTABLE_GP_STANDINGS_TRANSFER;
        PortableGpStandingsSession before;PortableGpStandingsAssetView asset_before;
        memcpy(&before,&session,sizeof before);memcpy(&asset_before,&asset,sizeof asset_before);
        PortableTimeBoardRow rows[8]={0};uint8_t points[8]={0},awards[8]={0};
        for(unsigned rank=0u;rank<8u;++rank){
            const uint8_t racer=sorted?session.cup.standings_order[rank]:session.cup.race_order[rank];
            rows[rank]=(PortableTimeBoardRow){.racer_index=racer,.driver_class=session.cup.rows[racer].driver_class,
                .selected=(uint8_t)(racer==0u),.display_rank=(uint8_t)rank};
        }
        const PortableGpTimeBoardFlow board={.state=8u,.visible=1u};
        PortableTimeBoardRow rows_before[8];PortableGpTimeBoardFlow board_before;
        memcpy(rows_before,rows,sizeof rows);memcpy(&board_before,&board,sizeof board);
        for(unsigned screen=0u;screen<2u;++screen){
            for(size_t i=0u;i<count;++i)pixels[i]=guard;
            CHECK(screen?portable_gp_time_board_render(&asset,rows,points,awards,&board,0u,&centered):
                portable_gp_standings_render_over(&asset,&session,&centered));
            for(unsigned rank=0u;rank<8u;++rank)for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<32u;++x){
                const unsigned racer=rows[rank].racer_index,driver=rows[rank].driver_class;
                uint32_t expected=guard;
                if(x>=8u && x<24u){
                    const unsigned px=x-8u;
                    if(own && racer==5u){
                        if((px+y)%3u)expected=UINT32_C(0xffe00000)|((y+32u)<<8u)|(px+16u);
                    }else expected=portrait_color(palette[13u*16u+1u+(driver*2u+px+3u*y)%13u]);
                }else if(x==0u || x==31u || y==0u || y==15u)
                    expected=portrait_color(palette[13u*16u+14u]);
                for(size_t py=0u;py<scale;++py)for(size_t px=0u;px<scale;++px)
                    CHECK(pixels[((24u+rank*16u+y)*scale+py)*stride+(margin+24u+x)*scale+px]==expected);
            }
            for(size_t y=0u;y<160u*scale;++y)for(size_t x=0u;x<stride;++x)
                if(x<margin*scale || x>=(margin+240u)*scale)CHECK(pixels[y*stride+x]==guard);
            CHECK(!memcmp(&session,&before,sizeof session) && !memcmp(&asset,&asset_before,sizeof asset));
            CHECK(!memcmp(tiles,tiles_before,sizeof tiles) && !memcmp(drivers,drivers_before,sizeof drivers) &&
                !memcmp(palette,palette_before,sizeof palette));
            CHECK(!memcmp(background,background_before,sizeof background) && !memcmp(footer,footer_before,sizeof footer) &&
                !memcmp(custom_pixels,custom_before,sizeof custom_pixels) && !memcmp(rows,rows_before,sizeof rows) &&
                !memcmp(&board,&board_before,sizeof board));
        }
        free(pixels);
    }
}

int main(void)
{
    all_portrait_interiors();
    static uint8_t tiles[PORTABLE_GP_STANDINGS_TILE_BYTES];
    static uint16_t palette[PORTABLE_GP_STANDINGS_PALETTE_COUNT];
    static uint16_t background[PORTABLE_GP_STANDINGS_MAP_COUNT];
    static uint16_t drivers[PORTABLE_GP_STANDINGS_DRIVER_COUNT *
                            PORTABLE_GP_STANDINGS_DRIVER_CELLS];
    static uint16_t footer[PORTABLE_GP_STANDINGS_FOOTER_CELLS];
    static uint32_t pixels[240u * 160u];
    PortableSoftwareFrame frame = {pixels, 240u, 160u,NULL,1u,0u,0u,NULL};
    PortableGpStandingsAssetView asset = {
        tiles,sizeof tiles,palette,PORTABLE_GP_STANDINGS_PALETTE_COUNT,
        background,PORTABLE_GP_STANDINGS_MAP_COUNT,drivers,
        PORTABLE_GP_STANDINGS_DRIVER_COUNT,
        PORTABLE_GP_STANDINGS_DRIVER_CELLS,footer,
        PORTABLE_GP_STANDINGS_FOOTER_CELLS,24u,4u,{NULL},{NULL}};
    PortableGpStandingsSession session = {0};
    uint32_t first,second;

    for(size_t tile=0u;tile<sizeof tiles/32u;++tile)
    {
        const uint8_t value=(uint8_t)((tile%15u)+1u);
        memset(tiles+tile*32u,(int)(value|(uint8_t)(value<<4)),32u);
    }
    for(size_t index=0u;index<PORTABLE_GP_STANDINGS_PALETTE_COUNT;++index)
        palette[index]=(uint16_t)(index&UINT16_C(0x7fff));
    for(size_t index=0u;index<PORTABLE_GP_STANDINGS_MAP_COUNT;++index)
        background[index]=UINT16_C(0xc001);
    for(size_t index=0u;index<sizeof drivers/sizeof drivers[0];++index)
        drivers[index]=(uint16_t)(UINT16_C(0xf000)+2u+index%4u);
    for(size_t index=0u;index<PORTABLE_GP_STANDINGS_FOOTER_CELLS;++index)
        footer[index]=(uint16_t)(UINT16_C(0xf000)+6u+index);
    session.phase=PORTABLE_GP_STANDINGS_TRANSFER;
    for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index)
    {
        session.cup.race_order[index]=(uint8_t)index;
        session.cup.standings_order[index]=(uint8_t)(7u-index);
        session.cup.rows[index].standing_rank=(uint8_t)(7u-index);
        session.cup.rows[index].racer_index=(uint8_t)index;
        session.cup.rows[index].driver_class=(uint8_t)index;
        session.cup.rows[index].finish_rank=(uint8_t)index;
        session.displayed_points[index]=(uint8_t)(index+1u);
        session.remaining_award[index]=(uint8_t)(index<4u?9u-index*2u:0u);
    }
    CHECK(portable_gp_standings_render(&asset,&session,&frame));
    first=portable_software_frame_checksum(&frame);
    CHECK(first!=0u);
    ++session.displayed_points[0];
    --session.remaining_award[0];
    CHECK(portable_gp_standings_render(&asset,&session,&frame));
    second=portable_software_frame_checksum(&frame);
    CHECK(second!=0u&&second!=first);
    {
        /* 9B2AC contains example time/digit cells in its body.1DAA4 clears
         * these before the current point rows are drawn, including blanks. */
        const uint16_t before=background[3u*32u+21u];
        background[3u*32u+21u]=UINT16_C(0xf10b);
        background[4u*32u+21u]=UINT16_C(0xf12b);
        background[3u*32u+25u]=UINT16_C(0xf103);
        background[3u*32u+26u]=UINT16_C(0xf10b);
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(portable_software_frame_checksum(&frame)==second);
        background[3u*32u+21u]=before;
        background[4u*32u+21u]=before;
        background[3u*32u+25u]=before;
        background[3u*32u+26u]=before;
        /* Header cells are retained, rather than clearing the entire map. */
        background[28u*32u]=UINT16_C(0xc007);
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(portable_software_frame_checksum(&frame)!=second);
        session.cup.engine_class=1u;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(portable_software_frame_checksum(&frame)==second);
        session.cup.engine_class=0u;
        background[28u*32u]=before;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(portable_software_frame_checksum(&frame)==second);
    }
    {
        /* A roster driver's portrait and name cover cells 1..2 and 4..8 only. */
        static uint8_t name_bytes[32u+40u*16u*4u];
        PortableNativeImage name,portrait;
        memcpy(name_bytes,"MKSCIMG1",8u);
        name_bytes[8]=32u;name_bytes[10]=1u;name_bytes[12]=40u;name_bytes[14]=16u;
        name_bytes[24]=32u;
        name_bytes[28]=(uint8_t)(sizeof name_bytes&255u);name_bytes[29]=(uint8_t)(sizeof name_bytes>>8);
        for(size_t pixel=0u;pixel<40u*16u;++pixel){
            name_bytes[32u+pixel*4u]=0x40u;name_bytes[35u+pixel*4u]=0xffu;
        }
        CHECK(portable_native_image_open(name_bytes,sizeof name_bytes,&name));
        portrait=name;portrait.width=16u;
        asset.racer_names[0]=&name;asset.racer_portraits[0]=&portrait;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(portable_software_frame_checksum(&frame)!=second);
        for(size_t y=24u;y<40u;++y)for(size_t x=24u;x<96u;++x){
            const int covered=(x>=32u&&x<48u)||x>=56u;
            CHECK((pixels[y*240u+x]==UINT32_C(0xff000040))==covered);
        }
        /* Cumulative redraw uses the sorted order/standing rank, with no
         * race-award plus column. Authored art follows its racer identity. */
        session.sorted_order=1u;session.phase=PORTABLE_GP_STANDINGS_SORTED_HOLD;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(pixels[136u*240u+32u]==UINT32_C(0xff000040));
        CHECK(pixels[24u*240u+32u]!=UINT32_C(0xff000040));
        session.sorted_order=0u;session.phase=PORTABLE_GP_STANDINGS_TRANSFER;
        asset.racer_names[0]=NULL;asset.racer_portraits[0]=NULL;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        CHECK(portable_software_frame_checksum(&frame)==second);
    }
    {
        /* Transparent texels in the imported portrait/name/digit banks must
         * all expose one backdrop. Bank zero entries deliberately differ:
         * the old renderer produced grey/green strips and black gaps. */
        memset(tiles,0,sizeof tiles);
        palette[0]=UINT16_C(0x001f);
        for(unsigned bank=1u;bank<16u;++bank)palette[bank*16u]=(uint16_t)(bank<<5);
        const uint32_t backdrop=UINT32_C(0xffff0000);
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==backdrop);
        session.sorted_order=1u;session.phase=PORTABLE_GP_STANDINGS_SORTED_HOLD;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==backdrop);
        session.phase=PORTABLE_GP_STANDINGS_REORDER;session.reorder_stage=17u;
        CHECK(portable_gp_standings_render(&asset,&session,&frame));
        for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==backdrop);
        for(unsigned phase=0u;phase<3u;++phase){
            session.phase=phase==0u?PORTABLE_GP_STANDINGS_TRANSFER:
                phase==1u?PORTABLE_GP_STANDINGS_SORTED_HOLD:PORTABLE_GP_STANDINGS_REORDER;
            for(unsigned i=0u;i<240u*160u;++i)pixels[i]=UINT32_C(0xff000000)|i;
            CHECK(portable_gp_standings_render_over(&asset,&session,&frame));
            for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==(UINT32_C(0xff000000)|i));
        }
        /* Source portrait corner cells contain authored blue index14 marks
         * amid transparent zeroes; preserve those marks over the course. */
        const uint16_t previous_driver=drivers[0];drivers[0]=UINT16_C(0xd002);
        palette[13u*16u+14u]=UINT16_C(0x64a8);tiles[2u*32u+3u]=0xe0u;
        session.phase=PORTABLE_GP_STANDINGS_TRANSFER;session.sorted_order=0u;
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=UINT32_C(0xff123456);
        CHECK(portable_gp_standings_render_over(&asset,&session,&frame));
        CHECK(pixels[24u*240u+24u]==UINT32_C(0xff123456));
        CHECK(pixels[24u*240u+31u]==UINT32_C(0xff4229ce));
        drivers[0]=previous_driver;tiles[2u*32u+3u]=0u;
        PortableGpResultsAssetView results={0};
        results.followup_tiles=tiles;results.followup_tile_bytes=sizeof tiles;
        results.time_board_map=background;results.time_board_map_count=PORTABLE_GP_STANDINGS_MAP_COUNT;
        const PortableTimeBoardRow row={1234u,0u,0u,0u,PORTABLE_TIME_BOARD_TIME,1u};
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=backdrop;
        CHECK(portable_time_board_render(&asset,&results,&row,1u,0u,&frame));
        for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==backdrop);
        /* The source206A4 row copies include the eighth footer scanline.
         * Own roster art moves with its row, while zero tiles keep the race. */
        uint8_t own_data[16u*16u*4u];
        for(unsigned i=0u;i<sizeof own_data;i+=4u){own_data[i]=0u;own_data[i+1u]=255u;own_data[i+2u]=0u;own_data[i+3u]=255u;}
        PortableNativeImage own={.width=16u,.height=16u,.pixels=own_data};
        asset.racer_portraits[0]=&own;
        uint8_t row_x[8]={1u,0u,0u,0u,0u,0u,0u,0u};
        CHECK(portable_time_board_render_slide(&asset,&results,&row,1u,0u,row_x,&frame));
        CHECK(pixels[24u*240u+32u]==backdrop&&pixels[24u*240u+40u]==UINT32_C(0xff00ff00));
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=backdrop;
        row_x[0]=32u;
        CHECK(portable_time_board_render_slide(&asset,&results,&row,1u,0u,row_x,&frame));
        for(unsigned i=0u;i<240u*160u;++i)CHECK(pixels[i]==backdrop);
        for(unsigned scale=1u;scale<=4u;scale+=3u){
            const unsigned stride=240u*scale+7u;
            const size_t total=(size_t)stride*160u*scale;
            uint32_t *physical=malloc(total*sizeof *physical);CHECK(physical!=NULL);
            for(size_t i=0u;i<total;++i)physical[i]=backdrop;
            PortableSoftwareFrame scaled={.pixels=physical,.width=240u,.height=160u,
                .scale=(uint8_t)scale,.stride=stride};
            row_x[0]=1u;
            CHECK(portable_time_board_render_slide(&asset,&results,&row,1u,0u,row_x,&scaled));
            for(unsigned y=0u;y<160u*scale;++y)for(unsigned x=0u;x<stride;++x){
                const int own_pixel=x>=40u*scale&&x<56u*scale&&y>=24u*scale&&y<40u*scale;
                CHECK(physical[(size_t)y*stride+x]==(own_pixel?UINT32_C(0xff00ff00):backdrop));
            }
            free(physical);
        }
        asset.racer_portraits[0]=NULL;
        PortableTimeBoardRow all[8];
        for(unsigned i=0u;i<8u;++i)all[i]=(PortableTimeBoardRow){0u,(uint8_t)i,(uint8_t)i,(uint8_t)i,PORTABLE_TIME_BOARD_EMPTY,(uint8_t)(i==0u)};
        memset(tiles+32u,0x11,32u);palette[1]=31u<<5u;footer[0]=1u;
        row_x[7]=1u;
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=backdrop;
        CHECK(portable_time_board_render_slide(&asset,&results,all,8u,0u,row_x,&frame));
        CHECK(pixels[152u*240u+24u]==backdrop&&pixels[152u*240u+32u]==UINT32_C(0xff00ff00));
        memset(tiles+32u,0,32u);footer[0]=UINT16_C(0xf006);
    }
    {
        /* GP's first board uses the original eight-racer map/tiles, not the
         * TT followup atlas or its lap columns. Zero texels retain the race. */
        PortableGpTimeBoardFlow board={0};
        PortableTimeBoardRow rows[8];
        uint8_t points[8],awards[8];
        for(unsigned i=0u;i<8u;++i){rows[i]=(PortableTimeBoardRow){12345u,(uint8_t)i,(uint8_t)i,(uint8_t)i,PORTABLE_TIME_BOARD_TIME,(uint8_t)(i==0u)};
            points[i]=(uint8_t)(10u+i);awards[i]=(uint8_t)(i<4u?9u-i*2u:0u);}
        memset(tiles,0,sizeof tiles);
        /* Give every numeric glyph a distinct opaque color. */
        for(unsigned digit=0u;digit<13u;++digit){
            const uint8_t ink=(uint8_t)(digit+1u);
            memset(tiles+(0x100u+digit)*32u,ink|(ink<<4),32u);
            memset(tiles+(0x120u+digit)*32u,ink|(ink<<4),32u);
            palette[14u*16u+ink]=(uint16_t)(digit+1u);
            palette[15u*16u+ink]=(uint16_t)(digit+1u);
        }
        board.visible=1u;board.state=8u;
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=UINT32_C(0xff123456);
        const PortableGpTimeBoardFlow saved=board;
        const PortableTimeBoardRow saved_row=rows[0];
        CHECK(portable_gp_time_board_render(&asset,rows,points,awards,&board,0u,&frame));
        CHECK(memcmp(&saved,&board,sizeof board)==0&&memcmp(&saved_row,&rows[0],sizeof saved_row)==0);
        const uint32_t minute=pixels[24u*240u+176u];
        CHECK(minute!=UINT32_C(0xff123456));
        CHECK(pixels[24u*240u+184u]!=minute); /* minute separator */
        CHECK(pixels[24u*240u+208u]!=minute); /* centisecond separator */
        CHECK(pixels[136u*240u+176u]==minute); /* eighth racer */
        CHECK(pixels[24u*240u+104u]==UINT32_C(0xff123456)); /* no TT time columns */
        board.state=9u;board.progress=4u;
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=UINT32_C(0xff123456);
        CHECK(portable_gp_time_board_render(&asset,rows,points,awards,&board,0u,&frame));
        CHECK(pixels[24u*240u+176u]==UINT32_C(0xff123456));
        board.state=10u;board.progress=3u;
        CHECK(portable_gp_time_board_render(&asset,rows,points,awards,&board,0u,&frame));
        CHECK(pixels[24u*240u+184u]!=UINT32_C(0xff123456)); /* cumulative tens */
        CHECK(pixels[24u*240u+208u]!=UINT32_C(0xff123456)); /* award plus */
        CHECK(pixels[136u*240u+208u]==UINT32_C(0xff123456)); /* no award for rank 8 */
        board.state=5u;board.row_x[0]=32u;board.row_x[7]=0u;
        for(unsigned i=0u;i<240u*160u;++i)pixels[i]=UINT32_C(0xff123456);
        CHECK(portable_gp_time_board_render(&asset,rows,points,awards,&board,0u,&frame));
        CHECK(pixels[24u*240u+176u]==UINT32_C(0xff123456));
        CHECK(pixels[136u*240u+176u]==minute);
        board.visible=0u;
        const uint32_t before=portable_software_frame_checksum(&frame);
        CHECK(portable_gp_time_board_render(&asset,rows,points,awards,&board,0u,&frame));
        CHECK(portable_software_frame_checksum(&frame)==before);
    }
    asset.tile_bytes--;
    CHECK(!portable_gp_standings_render(&asset,&session,&frame));
    puts("portable GP standings renderer: PASS");
    return 0;
}
