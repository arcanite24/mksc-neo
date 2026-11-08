#include "gp_race_hud_asset.h"
#include "hud_renderer.h"
#include "gp_opening_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"GP race HUD test line %d: %s\n",__LINE__,#condition); \
    return __LINE__; \
} } while (0)

enum { SECTION_BYTES = 16 + 8 + 0x400 + 0x800 + 5 * 4 * 13 * 2 };

static void write32(uint8_t *p,uint32_t value)
{
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(value>>(i*8u));
}

static void write16(uint8_t *p,uint16_t value)
{
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);
}

static int gp_opening_pixels(void)
{
    uint8_t bytes[PORTABLE_GP_OPENING_ASSET_BYTES]={0},before[PORTABLE_GP_OPENING_ASSET_BYTES];
    write32(bytes,0x31544f47u);write32(bytes+4u,0xa00u);
    const unsigned map=8u+0xa00u,palette=map+0x800u,classes=palette+32u;
    write16(bytes+map,1u);write16(bytes+map+18u*64u,7u);
    memset(bytes+8u+7u*32u,0x22,32u);
    write16(bytes+palette+2u,31u);write16(bytes+palette+4u,0x03e0u);
    write16(bytes+palette+6u,0x7c00u);
    for(unsigned i=0u;i<3u;++i)memset(bytes+classes+i*192u,(int)((i+1u)*17u),192u);
    PortableAssetSectionView section={.kind=36u,.content_id=24u,.version=27u,.bytes=bytes,.size=sizeof bytes};
    PortableGpOpeningAssetView asset;
    CHECK(portable_gp_opening_asset_open(&section,&asset));
    CHECK(asset.class_tiles==bytes+classes&&asset.class_count==3u);
    PortableGpEntrySnapshot entry={.panel_scroll=256u,.veil_phase=11u,
        .window_top=24u,.window_bottom=144u,.show_panel=1u};
    uint32_t pixels[320u*160u];PortableSoftwareFrame frame={pixels,320u,160u,NULL,1u,0u,0u,NULL};
    memcpy(before,bytes,sizeof bytes);PortableGpEntrySnapshot original=entry;
    for(unsigned engine=0u;engine<3u;++engine){
        for(unsigned i=0u;i<320u*160u;++i)pixels[i]=0xff123456u;
        CHECK(portable_gp_opening_render(&asset,&entry,(uint8_t)engine,&frame));
        CHECK(pixels[40u]==(engine==0u?0xffff0000u:engine==1u?0xff00ff00u:0xff0000ffu));
        CHECK(pixels[144u*320u+40u]==0xff00ff00u); /* lower name outside world window */
        CHECK(pixels[24u*320u]==0xff123456u&&pixels[0]==0xff000000u);
    }
    CHECK(memcmp(before,bytes,sizeof bytes)==0&&memcmp(&entry,&original,sizeof entry)==0);
    uint32_t *physical=malloc(1293u*640u*sizeof *physical);CHECK(physical!=NULL);
    for(unsigned y=0u;y<640u;++y)for(unsigned x=0u;x<1293u;++x)
        physical[y*1293u+x]=x>=1280u?0xdeadbeefu:(x&1u?0xff202020u:0xff181818u);
    PortableSoftwareFrame scaled={physical,320u,160u,NULL,4u,1293u,0u,NULL};
    CHECK(portable_gp_opening_render(&asset,&entry,0u,&scaled));
    CHECK(physical[160u]==0xffff0000u&&physical[163u]==0xffff0000u);
    CHECK(physical[96u*1293u]==0xff181818u&&physical[96u*1293u+1u]==0xff202020u);
    for(unsigned y=0u;y<640u;++y)for(unsigned x=1280u;x<1293u;++x)
        CHECK(physical[y*1293u+x]==0xdeadbeefu);
    free(physical);
    PortableGpOpeningAssetView saved=asset;
    --section.size;CHECK(!portable_gp_opening_asset_open(&section,&asset));
    CHECK(memcmp(&saved,&asset,sizeof asset)==0);++section.size;
    write16(bytes+map,80u);CHECK(!portable_gp_opening_asset_open(&section,&asset));
    write16(bytes+map,1u);section.version=26u;CHECK(!portable_gp_opening_asset_open(&section,&asset));
    return 0;
}

static int entry_hud_rgb_pixels(void)
{
    const uint32_t transparent=UINT32_C(0x01000000);
    uint32_t source[240u*160u],target[240u*160u];
    PortableSoftwareFrame hud={source,240u,160u,NULL,1u,0u,0u,NULL};
    PortableSoftwareFrame frame={target,240u,160u,NULL,1u,0u,0u,NULL};
    portable_software_frame_clear(&hud,transparent);
    portable_software_frame_clear(&frame,UINT32_C(0xff123456));
    source[16u*240u+40u]=UINT32_C(0x00ff0000);
    source[16u*240u+41u]=0u; /* black is also an opaque HUD pixel */
    source[24u*240u+24u]=UINT32_C(0x0000ff00);
    source[100u*240u+200u]=UINT32_C(0x000000ff);
    CHECK(portable_hud_render_entry_bands(&hud,8u,transparent,&frame));
    CHECK(target[40u]==UINT32_C(0x00ff0000)&&target[41u]==0u);
    CHECK(target[42u]==UINT32_C(0xff123456));
    CHECK(target[24u*240u+8u]==UINT32_C(0x0000ff00));
    CHECK(target[100u*240u+216u]==UINT32_C(0x000000ff));
    uint32_t *physical_source=malloc(973u*640u*sizeof *physical_source);
    uint32_t *physical_target=malloc(981u*640u*sizeof *physical_target);
    CHECK(physical_source&&physical_target);
    PortableSoftwareFrame scaled_hud={physical_source,240u,160u,NULL,4u,973u,0u,NULL};
    PortableSoftwareFrame scaled_frame={physical_target,240u,160u,NULL,4u,981u,0u,NULL};
    for(unsigned i=0u;i<973u*640u;++i)physical_source[i]=transparent;
    for(unsigned i=0u;i<981u*640u;++i)physical_target[i]=0xdeadbeefu;
    physical_source[64u*973u+160u]=UINT32_C(0x00ff0000);
    physical_source[64u*973u+161u]=UINT32_C(0x0000ff00);
    CHECK(portable_hud_render_entry_bands(&scaled_hud,8u,transparent,&scaled_frame));
    CHECK(physical_target[160u]==UINT32_C(0x00ff0000)&&physical_target[161u]==UINT32_C(0x0000ff00));
    CHECK(physical_target[162u]==0xdeadbeefu); /* physical detail and transparency survive */
    for(unsigned y=0u;y<640u;++y)for(unsigned x=960u;x<981u;++x)
        CHECK(physical_target[y*981u+x]==0xdeadbeefu);
    free(physical_source);free(physical_target);
    return 0;
}

static int imported_entrance_cells(void)
{
    uint8_t tiles[4096]={0},course[2048]={0},top[128]={0};
    uint16_t palette[96]={0},item[16],left[6],right[6],slash[2]={0xc00bu,0xc00cu},digits[20]={0};
    for(unsigned i=1u;i<14u;++i){
        memset(tiles+i*32u,(int)(i|(i<<4u)),32u);
        palette[32u+i]=(uint16_t)i;
    }
    for(unsigned row=0u;row<4u;++row)for(unsigned col=0u;col<4u;++col)
        item[row*4u+col]=(uint16_t)(0xc007u+row);
    for(unsigned i=0u;i<6u;++i){left[i]=(uint16_t)(0xc001u+i);right[i]=(uint16_t)(0xc006u-i);}
    digits[1]=2u;digits[3]=3u;digits[11]=4u;digits[13]=5u;
    const PortableGpRaceHudAssetView gp={.course_tiles=course};
    const PortableHudAssetView hud={.tiles=tiles,.tile_bytes=sizeof tiles,.palette=palette,.palette_count=96u,
        .item_cells=item,.item_cell_count=16u,.left_cells=left,.left_cell_count=6u,
        .right_cells=right,.right_cell_count=6u,.slash_cells=slash,.slash_cell_count=2u,
        .digit_tiles=digits,.digit_count=20u};
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    portable_software_frame_clear(&frame,0u);
    CHECK(portable_hud_render_gp_top_entrance(&gp,&hud,top,1343u,&frame));
    CHECK(pixels[104u]==(((10u<<3u)|(10u>>2u))<<16u));
    item[12]=0xc00du;
    portable_software_frame_clear(&frame,0u);
    CHECK(portable_hud_render_gp_top_entrance(&gp,&hud,top,1343u,&frame));
    CHECK(pixels[104u]==(((13u<<3u)|(13u>>2u))<<16u));
    CHECK(portable_hud_render_gp_top_entrance(&gp,&hud,top,1346u,&frame));
    CHECK(pixels[104u]==(((7u<<3u)|(7u>>2u))<<16u));
    CHECK(pixels[32u]==(((1u<<3u)|(1u>>2u))<<16u) && pixels[8u*240u+32u]==(((4u<<3u)|(4u>>2u))<<16u));
    CHECK(pixels[64u]==(((2u<<3u)|(2u>>2u))<<16u) && pixels[72u]==(((11u<<3u)|(11u>>2u))<<16u) && pixels[80u]==(((3u<<3u)|(3u>>2u))<<16u));
    CHECK(pixels[144u]==(((6u<<3u)|(6u>>2u))<<16u));
    return 0;
}

int main(void)
{
    CHECK(gp_opening_pixels()==0);
    CHECK(imported_entrance_cells()==0);
    CHECK(entry_hud_rgb_pixels()==0);
    uint8_t bytes[SECTION_BYTES + PORTABLE_GP_RACE_HUD_RANK_COLOR_COUNT * 2u]={0};
    uint8_t hud_tiles[0x1000]={0};
    uint16_t hud_palette[PORTABLE_HUD_PALETTE_COUNT]={0};
    uint32_t pixels[240u*160u];
    const uint8_t classes[4]={0u,1u,0u,0u};
    PortableAssetSectionView section={
        PORTABLE_ASSET_SECTION_GP_RACE_HUD_BG0,24u,2u,
        0u,0u,bytes,sizeof bytes};
    PortableGpRaceHudAssetView gp;
    PortableHudAssetView hud={0};
    PortableSoftwareFrame frame;
    write32(bytes,UINT32_C(0x31484747));
    write32(bytes+4u,24u);
    write32(bytes+8u,5u);
    write32(bytes+12u,52u);
    bytes[16u+1u]=1u; /* class 1 uses the next portrait page */
    memset(bytes+24u,UINT8_C(0x11),32u);
    memset(bytes+24u+4u*32u,UINT8_C(0x22),32u);
    memset(bytes+24u+0x400u,UINT8_C(0x33),32u);
    write16(bytes+24u+0x400u+0x800u,UINT16_C(0xd080));
    write16(bytes+24u+0x400u+0x800u+52u*2u,UINT16_C(0xd084));
    hud.palette=hud_palette;
    hud.palette_count=PORTABLE_HUD_PALETTE_COUNT;
    hud.tiles=hud_tiles;
    hud.tile_bytes=sizeof hud_tiles;
    hud_palette[3u*16u+1u]=UINT16_C(0x001f); /* bank D red */
    hud_palette[3u*16u+2u]=UINT16_C(0x03e0); /* bank D green */
    hud_palette[1u*16u+3u]=UINT16_C(0x7c00); /* bank B blue */
    CHECK(portable_gp_race_hud_asset_open(&section,&gp));
    CHECK(gp.course_id==24u);
    CHECK(gp.rank_transition_maps==NULL&&gp.rank_flash_colors==NULL&&
          gp.rank_separator_tile==NULL&&gp.coin_hud_colors==NULL&&gp.rank_separator_colors==NULL);
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    memset(pixels,0,sizeof pixels);
    CHECK(portable_hud_render_gp_race_bg0(&gp,&hud,0u,classes,1u,&frame));
    CHECK(pixels[16u*240u]==UINT32_C(0x00ff0000));
    CHECK(pixels[24u*240u+8u]==UINT32_C(0x00ff0000));
    CHECK(pixels[24u*240u+8u+3u*8u]==0u);
    CHECK(pixels[27u*240u+8u]==UINT32_C(0x00ff0000));
    CHECK(pixels[96u*240u+176u]==UINT32_C(0x000000ff));
    /* The winning-place palette flashes on ticks +4..+7, then resets. */
    write16(bytes+24u+0x400u+0x800u,UINT16_C(0xb080));
    bytes[24u]=UINT8_C(0x99);
    hud_palette[1u*16u+9u]=UINT16_C(0x001f);
    memset(pixels,0,sizeof pixels);
    CHECK(portable_hud_render_gp_race_bg0(&gp,&hud,0u,classes,4u,&frame));
    CHECK(pixels[16u*240u]==UINT32_C(0x00ffffff));
    memset(pixels,0,sizeof pixels);
    CHECK(portable_hud_render_gp_race_bg0(&gp,&hud,0u,classes,8u,&frame));
    CHECK(pixels[16u*240u]==UINT32_C(0x00ff0000));
    write16(bytes+24u+0x400u+0x800u,UINT16_C(0xd080));
    bytes[24u]=UINT8_C(0x11);
    memset(pixels,0,sizeof pixels);
    CHECK(portable_hud_render_gp_race_bg0(&gp,&hud,1u,classes,1u,&frame));
    CHECK(pixels[16u*240u]==UINT32_C(0x0000ff00));
    section.size--;
    CHECK(!portable_gp_race_hud_asset_open(&section,&gp));
    section.size++;
    bytes[17u]=8u;
    CHECK(!portable_gp_race_hud_asset_open(&section,&gp));
    bytes[17u]=0u;
    write16(bytes+SECTION_BYTES,UINT16_C(0xffff));
    CHECK(portable_gp_race_hud_asset_open(&section,&gp));
    CHECK(gp.rank_colors==bytes+SECTION_BYTES);
    CHECK(gp.rank_colors[0]==UINT8_C(0xff));
    /* Live HUD selects each rank layout and draws the current coin digits. */
    memset(hud_tiles+0x11u*32u,0x11,32u);
    memset(hud_tiles+0x12u*32u,0x22,32u);
    hud_palette[17u]=UINT16_C(0x001f);
    hud_palette[18u]=UINT16_C(0x03e0);
    for(uint8_t rank=0u;rank<8u;++rank){
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_race(&gp,&hud,rank,classes,12u,0u,&frame));
        CHECK(pixels[8u*240u+8u]==UINT32_C(0x00ff0000));
        CHECK(pixels[8u*240u+16u]==UINT32_C(0x0000ff00));
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_race(&gp,&hud,rank,classes,21u,0u,&frame));
        CHECK(pixels[8u*240u+8u]==UINT32_C(0x0000ff00));
        CHECK(pixels[8u*240u+16u]==UINT32_C(0x00ff0000));
    }
    {
        /* Live rank/coin pixels use immutable owner colors/cells, not the
         * instantaneous rank/coin values or the base asset palette. */
        uint16_t cells[52]={0},rank_colors[4]={0x03e0u,0u,0u,0u};
        const uint16_t coins[3]={0xc020u,0xb011u,0xb012u};
        const uint16_t coin_colors[3]={0x7c00u,0x001fu,0u},separator_colors[2]={0x03e0u,0x7c00u};
        const PortableGpRaceHudAssetView before=gp;
        const uint16_t palette_before=hud_palette[17u];
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_race_owned(&gp,&hud,0u,classes,12u,cells,
            rank_colors,coins,coin_colors,separator_colors,&frame));
        CHECK(pixels[8u*240u+8u]==UINT32_C(0x000000ff));
        CHECK(pixels[8u*240u+16u]==UINT32_C(0x00ff0000));
        CHECK(pixels[16u*240u]==0u); /* zero cells retain the scene */
        cells[0]=0xb080u;bytes[24u]=0x99u;
        CHECK(portable_hud_render_gp_race_owned(&gp,&hud,0u,classes,12u,cells,
            rank_colors,coins,coin_colors,separator_colors,&frame));
        CHECK(pixels[16u*240u]==UINT32_C(0x0000ff00)); /* no sampler clock re-flash */
        bytes[24u]=0x11u;
        CHECK(memcmp(&gp,&before,sizeof gp)==0&&hud_palette[17u]==palette_before);
    }
    CHECK(!portable_hud_render_gp_race(&gp,&hud,8u,classes,12u,0u,&frame));
    CHECK(!portable_hud_render_gp_race(&gp,&hud,0u,classes,100u,0u,&frame));
    for (uint32_t version=1u; version<=36u; ++version) {
        section.version=version;
        CHECK(portable_gp_race_hud_asset_open(&section,&gp)==(version==2u));
    }
    {
        uint8_t animated[sizeof bytes+PORTABLE_GP_RACE_HUD_ANIMATION_BYTES];
        memcpy(animated,bytes,sizeof bytes);
        for(unsigned i=0u;i<PORTABLE_GP_RACE_HUD_ANIMATION_BYTES;++i)
            animated[sizeof bytes+i]=(uint8_t)(i*7u+3u);
        section.version=3u;section.bytes=animated;section.size=sizeof animated;
        CHECK(portable_gp_race_hud_asset_open(&section,&gp));
        CHECK(gp.rank_transition_maps==animated+sizeof bytes);
        CHECK(gp.rank_flash_colors==animated+sizeof bytes+416u);
        CHECK(gp.rank_separator_tile==animated+sizeof bytes+432u);
        CHECK(gp.coin_hud_colors==animated+sizeof bytes+464u);
        CHECK(gp.rank_separator_colors==animated+sizeof bytes+482u);
        CHECK(gp.rank_separator_colors[7u]==animated[sizeof animated-1u]);
        /* Entrance flash reads the imported second palette row. */
        uint8_t top_tiles[128]={0},badge_tiles[352]={0};
        uint16_t digits[20]={0};PortableHudAssetView entrance_hud=hud;
        entrance_hud.digit_tiles=digits;entrance_hud.digit_count=20u;
        /* This case isolates palette flashing; its other imported maps are
         * deliberately transparent authored cells, with complete views. */
        uint16_t entrance_item[16]={0},entrance_left[6]={0},entrance_right[6]={0},entrance_slash[2]={0};
        entrance_hud.item_cells=entrance_item;entrance_hud.item_cell_count=16u;
        entrance_hud.left_cells=entrance_left;entrance_hud.left_cell_count=6u;
        entrance_hud.right_cells=entrance_right;entrance_hud.right_cell_count=6u;
        entrance_hud.slash_cells=entrance_slash;entrance_hud.slash_cell_count=2u;
        const uint16_t entrance_original=hud_palette[2u*16u+9u];
        write16(animated+(size_t)(gp.rank_maps-animated),0xc080u);
        memset(animated+(size_t)(gp.portrait_tiles-animated),0x99,32u);
        write16(animated+(size_t)(gp.rank_colors-animated)+8u,0x001fu);
        write16(animated+(size_t)(gp.rank_flash_colors-animated)+8u,0x03e0u);
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_live_entrance(&gp,&entrance_hud,top_tiles,badge_tiles,
            0u,classes,1353u,0u,&frame));
        CHECK(pixels[16u*240u]==0x0000ff00u);
        write16(animated+(size_t)(gp.rank_flash_colors-animated)+8u,0x7c00u);
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_live_entrance(&gp,&entrance_hud,top_tiles,badge_tiles,
            0u,classes,1353u,0u,&frame));
        CHECK(pixels[16u*240u]==0x000000ffu);
        CHECK(hud_palette[2u*16u+9u]==entrance_original);
        PortableGpRaceHudAssetView before=gp;
        --section.size;
        CHECK(!portable_gp_race_hud_asset_open(&section,&gp));
        CHECK(memcmp(&before,&gp,sizeof gp)==0);
        section.size=sizeof animated;section.version=2u;
        CHECK(!portable_gp_race_hud_asset_open(&section,&gp));
    }
    {
        uint8_t finished[sizeof bytes+PORTABLE_GP_RACE_HUD_ANIMATION_BYTES+PORTABLE_GP_RACE_HUD_FINISH_BYTES]={0};
        memcpy(finished,bytes,sizeof bytes);
        uint8_t *frames=finished+sizeof bytes+PORTABLE_GP_RACE_HUD_ANIMATION_BYTES;
        uint8_t *portraits=frames+8u*36u;
        for(unsigned i=0u;i<8u;++i){
            write32(frames+i*36u,UINT32_C(0x00040004));
            write16(frames+i*36u+4u,UINT16_C(0xb07d));
            write32(portraits+i*12u,UINT32_C(0x00020002));
            write16(portraits+i*12u+4u,UINT16_C(0xd080));
        }
        section.version=4u;section.bytes=finished;section.size=sizeof finished;
        CHECK(portable_gp_race_hud_asset_open(&section,&gp));
        CHECK(gp.finish_frames==frames&&gp.finish_portrait_patterns==portraits);
        {
            uint8_t scaled[sizeof finished+4u];memcpy(scaled,finished,sizeof finished);
            write32(scaled+4u,128u); /* Custom content identity retains authored scale. */
            PortableAssetSectionView custom=section;custom.content_id=128u;
            custom.version=5u;custom.bytes=scaled;custom.size=sizeof scaled;
            for(unsigned shift=20u;shift<=21u;++shift){
                write32(scaled+sizeof finished,shift);
                CHECK(portable_gp_race_hud_asset_open(&custom,&gp)&&gp.minimap_shift==shift);
            }
            const PortableGpRaceHudAssetView prior=gp;
            write32(scaled+sizeof finished,22u);
            CHECK(!portable_gp_race_hud_asset_open(&custom,&gp)&&memcmp(&prior,&gp,sizeof gp)==0);
            custom.size--;
            CHECK(!portable_gp_race_hud_asset_open(&custom,&gp));
            CHECK(portable_gp_race_hud_asset_open(&section,&gp)&&gp.minimap_shift==21u);
        }
        const PortableGpRaceHudAssetView before=gp;
        frames[0]=3u;
        CHECK(!portable_gp_race_hud_asset_open(&section,&gp)&&memcmp(&before,&gp,sizeof gp)==0);
        frames[0]=4u;
        memset(hud_tiles+0x7du*32u,0x33,32u);
        memset(hud_tiles+0x7eu*32u,0x22,32u);
        memset(hud_tiles+0x7fu*32u,0x11,32u);
        write16(frames+4u+3u*2u,UINT16_C(0xb07d)); /* rival extends into selected border */
        write16(frames+3u*36u+4u,UINT16_C(0xb07e));
        write16(frames+7u*36u+4u,UINT16_C(0xb07f));
        PortableGpFinishPortraits finish;
        CHECK(portable_gp_finish_portraits_init(&finish,2u,1u));
        finish.rows[0]=(PortableGpFinishPortrait){.x_tile=0u,.driver_class=0,.racer_index=0u};
        finish.rows[1]=(PortableGpFinishPortrait){.x_tile=3u,.driver_class=1,.racer_index=1u};
        const PortableGpFinishPortraits frozen=finish;
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_finish_portraits(&gp,&hud,&finish,NULL,0u,&frame));
        CHECK(pixels[80u*240u]==UINT32_C(0x000000ff));
        CHECK(pixels[88u*240u+8u]==UINT32_C(0x00ff0000));
        CHECK(pixels[88u*240u+32u]==UINT32_C(0x00ff0000)); /* selected pass draws after rivals */
        CHECK(pixels[80u*240u+24u]==UINT32_C(0x0000ff00)); /* selected overrides overlapping rival */
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_quick_finish_portraits(&gp,&hud,&finish,NULL,&frame));
        CHECK(pixels[80u*240u+24u]==UINT32_C(0x000000ff)); /* Quick leaves the rival border */
        CHECK(pixels[88u*240u+32u]==0u); /* 2009C omits the selected racer entirely */
        CHECK(portable_hud_render_gp_finish_portraits(&gp,&hud,&finish,NULL,6u,&frame));
        CHECK(pixels[80u*240u+24u]==UINT32_C(0x00ff0000)); /* second six-turn border palette */
        CHECK(memcmp(&frozen,&finish,sizeof finish)==0);
        uint32_t *physical=calloc(973u*640u,sizeof *physical);CHECK(physical);
        PortableSoftwareFrame scaled={physical,240u,160u,NULL,4u,973u,0u,NULL};
        for(unsigned y=0u;y<640u;++y)for(unsigned x=960u;x<973u;++x)physical[y*973u+x]=0xdeadbeefu;
        CHECK(portable_hud_render_gp_finish_portraits(&gp,&hud,&finish,NULL,0u,&scaled));
        for(unsigned y=320u;y<324u;++y)for(unsigned x=96u;x<100u;++x)
            CHECK(physical[y*973u+x]==UINT32_C(0x0000ff00));
        for(unsigned y=352u;y<356u;++y)for(unsigned x=32u;x<36u;++x)
            CHECK(physical[y*973u+x]==UINT32_C(0x00ff0000));
        for(unsigned y=0u;y<640u;++y)for(unsigned x=960u;x<973u;++x)
            CHECK(physical[y*973u+x]==0xdeadbeefu);
        free(physical);
        uint8_t custom[48]={0};memcpy(custom,"MKSCIMG1",8u);
        write16(custom+8u,32u);write16(custom+10u,1u);
        write16(custom+12u,2u);write16(custom+14u,2u);
        write32(custom+24u,32u);write32(custom+28u,48u);
        write32(custom+32u,UINT32_C(0xff00ff00));
        PortableNativeImage image;CHECK(portable_native_image_open(custom,sizeof custom,&image));
        const PortableNativeImage *own[8]={&image};
        memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_finish_portraits(&gp,&hud,&finish,own,0u,&frame));
        CHECK(pixels[88u*240u+8u]==UINT32_C(0xff00ff00));
        CHECK(pixels[88u*240u+10u]==0u); /* no donor art under a custom portrait */
        own[0]=NULL;own[1]=&image;memset(pixels,0,sizeof pixels);
        CHECK(portable_hud_render_gp_finish_portraits(&gp,&hud,&finish,own,0u,&frame));
        CHECK(pixels[88u*240u+32u]==UINT32_C(0xff00ff00) && pixels[88u*240u+34u]==0u);
        CHECK(memcmp(&frozen,&finish,sizeof finish)==0);
    }
    puts("GP race HUD: PASS live ranking and finished portrait art");
    return 0;
}
