#include "asset_pack.h"
#include "pause_menu_renderer.h"
#include "frontend_flow.h"
#include "frontend_config.h"
#include "frontend_presentation.h"
#include "menu_chrome_renderer.h"
#include "software_frame.h"
#include "native_image.h"
#include "frontend_motion_asset.h"
#include "frontend_image_renderer.h"
#include "menu_font.h"
#include "mod_menu_renderer.h"
#include "menu_banner.h"
#include "title_renderer.h"
#include "course_menu_renderer.h"
#include "menu_palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#condition); \
    return 1; } } while (0)

static uint8_t *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");long n;uint8_t *bytes;
    if(!f||fseek(f,0,SEEK_END)||(n=ftell(f))<0||fseek(f,0,SEEK_SET))
    {if(f)fclose(f);return NULL;}
    bytes=malloc((size_t)n);
    if(!bytes||fread(bytes,(size_t)n,1,f)!=1||fclose(f))
    {free(bytes);return NULL;}
    *size=(size_t)n;return bytes;
}

static void motion_u32(uint8_t *p,uint32_t v)
{ for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(v>>(i*8u)); }

static int test_indexed_popup_palette(void)
{
    uint16_t first[16]={0},second[16]={0},factors[64]={0},resolved[16];
    first[1]=0x001fu;second[1]=0x03e0u;factors[1]=16384u;factors[2]=32768u;
    const PortableMenuPaletteProfile profile={first,second,factors};
    CHECK(portable_menu_palette_colors(&profile,1u,resolved)&&resolved[1]==0x01efu);
    CHECK(portable_menu_palette_colors(&profile,2u,resolved)&&resolved[1]==0x03e0u);
    CHECK(portable_menu_palette_colors(&profile,64u,resolved)&&resolved[1]==0x001fu);
    uint8_t indexed[99]={0},argb[36]={0};
    memcpy(indexed,"MKSCIMG2",8u);indexed[8]=32u;indexed[10]=2u;
    indexed[12]=3u;indexed[14]=1u;indexed[20]=32u;
    motion_u32(indexed+24u,32u);motion_u32(indexed+28u,sizeof indexed);
    indexed[32]=1u;indexed[33]=17u;indexed[34]=255u;
    indexed[35u+17u*2u+1u]=124u;
    memcpy(argb,"MKSCIMG1",8u);argb[8]=32u;argb[10]=1u;argb[12]=argb[14]=1u;
    motion_u32(argb+24u,32u);motion_u32(argb+28u,sizeof argb);motion_u32(argb+32u,UINT32_C(0xff123456));
    const uint8_t saved[3]={indexed[32],indexed[33],indexed[34]};
    const PortableAssetSectionView sections[]={
        {.kind=15u,.content_id=500u,.version=1u,.bytes=indexed,.size=sizeof indexed},
        {.kind=15u,.content_id=501u,.version=1u,.bytes=argb,.size=sizeof argb}};
    const PortableAssetPackView pack={.resolved_sections=sections,.section_count=2u};
    uint16_t colors[16]={0};uint8_t mask[16];memset(mask,1,sizeof mask);
    for(unsigned scale=1u;scale<=4u;scale*=2u){
        uint32_t pixels[123u*8u];const size_t stride=30u*scale+3u;
        for(size_t i=0u;i<sizeof pixels/sizeof *pixels;++i)pixels[i]=UINT32_C(0xdeadbeef);
        PortableSoftwareFrame frame={pixels,30u,2u,NULL,(uint8_t)scale,stride,0u,NULL};
        colors[1]=0x001fu;
        CHECK(portable_frontend_image_draw_palette(&pack,500u,&frame,5,0,0,0,30,2,colors,16u,mask)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[5u*scale]==UINT32_C(0xffff0000));
        CHECK(pixels[6u*scale]==UINT32_C(0xff0000ff));
        CHECK(pixels[7u*scale]==UINT32_C(0xdeadbeef));
        colors[1]=0x03e0u;
        CHECK(portable_frontend_image_draw_palette(&pack,500u,&frame,5,0,0,0,30,2,colors,16u,mask)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[5u*scale]==UINT32_C(0xff00ff00)&&pixels[6u*scale]==UINT32_C(0xff0000ff));
        CHECK(portable_frontend_image_draw_palette(&pack,501u,&frame,20,0,0,0,30,2,colors,16u,mask)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[20u*scale]==UINT32_C(0xff123456));
        for(size_t y=0u;y<2u*scale;++y)for(size_t x=30u*scale;x<stride;++x)
            CHECK(pixels[y*stride+x]==UINT32_C(0xdeadbeef));
        CHECK(memcmp(saved,indexed+32u,sizeof saved)==0);
    }
    return 0;
}

static int test_heading_owner(void)
{
    PortableMenuHeadingRules rules={0};int32_t sine[1024]={0};
    sine[256]=32768;sine[768]=-32768;
    for(unsigned group=0;group<4u;++group){rules.groups[group].count=2u;
        rules.groups[group].letters[0]=(PortableMenuHeadingLetter){500u+group*2u,20,24};
        rules.groups[group].letters[1]=(PortableMenuHeadingLetter){501u+group*2u,40,24};}
    PortableMenuHeading heading={0};
    CHECK(portable_menu_heading_start(&heading,&rules,0u,0));
    CHECK(heading.cells[0].x==256&&heading.cells[0].mode==1u);
    CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].mode==2u&&heading.cells[0].ticks==0u&&heading.cells[1].mode==1u);
    for(unsigned i=0;i<12u;++i)CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].x==8&&heading.cells[0].mode==2u&&heading.cells[0].ticks==12u);
    CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].mode==3u&&heading.cells[0].ticks==0u);
    for(unsigned i=0;i<8u;++i)CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].inverse_x==320);
    for(unsigned i=0;i<80u;++i)CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].x==20&&heading.cells[1].x==40);
    portable_menu_heading_exit(&heading);
    CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].mode==18u&&heading.cells[0].ticks==0u);
    for(unsigned i=0;i<40u;++i)CHECK(portable_menu_heading_tick(&heading,sine));
    CHECK(heading.cells[0].mode==20u&&heading.cells[0].x==-30);
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_MODE,.players=1u};
    CHECK(portable_frontend_flow_set_heading_rules(&flow,&rules,sine));
    CHECK(flow.heading.initialized&&flow.heading.cells[0].x==20);
    const uint32_t checksum=portable_frontend_flow_checksum(&flow);
    PortableFrontendFlow changed=flow;++changed.heading.cells[0].x;
    CHECK(portable_frontend_flow_checksum(&changed)!=checksum);
    changed=flow;++changed.medallion.pulse;
    CHECK(portable_frontend_flow_checksum(&changed)!=checksum);
    flow.pending_page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    flow.transition_ticks=flow.transition_total_ticks=20u;flow.transition_out_ticks=10u;
    for(unsigned i=0;i<9u;++i)CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(flow.heading.group==0u&&flow.heading.exiting);
    const PortableMenuHeading snapshot=flow.heading;
    for(unsigned i=0;i<3u;++i)CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(!flow.heading.initialized);
    CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(flow.heading.group==1u&&!flow.heading.exiting&&flow.heading.cells[0].mode==2u);
    CHECK(snapshot.group==0u&&snapshot.exiting);
    flow.short_transitions=1u;
    CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(flow.heading.group==1u&&flow.heading.cells[0].x==20&&flow.heading.cells[0].inverse_x==256);
    PortableMenuMedallion medallion={0};
    sine[128]=20000;sine[896]=-20000;
    CHECK(portable_menu_medallion_tick(&medallion,0u,0u,sine));
    CHECK(medallion.inverse[0]==243u&&medallion.inverse[1]==256u);
    portable_menu_medallion_retarget(&medallion,1u);
    CHECK(medallion.inverse[0]==243u&&medallion.inverse[1]==256u&&medallion.grow_tick==0u);
    const unsigned expected[4]={243u,224u,204u,192u};
    for(unsigned i=0;i<4u;++i){CHECK(portable_menu_medallion_tick(&medallion,0u,1u,sine));
        CHECK(medallion.inverse[0]==256u&&medallion.inverse[1]==expected[i]);}
    CHECK(portable_menu_medallion_tick(&medallion,0u,1u,sine));
    CHECK(medallion.pulse==1u&&medallion.grow_tick==4u);
    return 0;
}

static int test_cup_panel_and_confirmation_pixels(void)
{
    const uint32_t ids[]={PORTABLE_COURSE_BACKGROUND,PORTABLE_COURSE_HEADING,
        PORTABLE_COURSE_PANEL,PORTABLE_COURSE_CUP,PORTABLE_COURSE_CUP+1u,
        PORTABLE_COURSE_CUP+2u,PORTABLE_COURSE_CUP+3u,PORTABLE_COURSE_CUP+4u,
        PORTABLE_COURSE_CUP_NAME,PORTABLE_COURSE_PREVIEW,PORTABLE_COURSE_PREVIEW+1u,
        PORTABLE_COURSE_NAME,PORTABLE_COURSE_NAME+1u,PORTABLE_COURSE_NAME+2u,
        PORTABLE_COURSE_NAME+3u,PORTABLE_COURSE_SELECTED_NAME,
        PORTABLE_COURSE_SELECTED_NAME+1u,PORTABLE_COURSE_OK};
    enum { COUNT=sizeof ids/sizeof ids[0] };
    uint8_t images[COUNT][36]={0};PortableAssetSectionView sections[COUNT+1u];
    for(unsigned i=0u;i<COUNT;++i){
        memcpy(images[i],"MKSCIMG1",8u);images[i][8]=32u;images[i][10]=1u;
        images[i][12]=images[i][14]=1u;images[i][24]=32u;images[i][28]=36u;
        motion_u32(images[i]+32u,0xff000000u|((i+1u)*0x00010101u));
        sections[i]=(PortableAssetSectionView){.kind=15u,.content_id=ids[i],.version=1u,
            .bytes=images[i],.size=sizeof images[i]};
    }
    /* Cell anchors are authored data, not inferred from the preview cover. */
    images[2][16]=192u;images[2][17]=255u;images[2][18]=184u;images[2][19]=255u;
    motion_u32(images[2]+32u,0xff80c040u);
    /* Invented overlap isolates heading-versus-panel priority. */
    images[1][16]=127u;images[1][18]=1u;
    uint8_t panel[40];memcpy(panel,images[2],36u);panel[12]=2u;panel[28]=40u;
    motion_u32(panel+36u,0xff80c040u);sections[2].bytes=panel;sections[2].size=sizeof panel;
    images[17][16]=images[17][18]=240u;images[17][17]=images[17][19]=255u;
    uint8_t *background=calloc(1u,32u+256u*256u*4u);CHECK(background!=NULL);
    memcpy(background,"MKSCIMG1",8u);background[8]=32u;background[10]=1u;
    background[13]=background[15]=1u;motion_u32(background+24u,32u);
    motion_u32(background+28u,32u+256u*256u*4u);
    motion_u32(background+32u+(8u*256u+129u)*4u,0xff004080u);
    sections[0].bytes=background;sections[0].size=32u+256u*256u*4u;
    PortableAssetPackView pack={.resolved_sections=sections,.section_count=COUNT};
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_COURSE,.players=1u,
        .gp_preview_track=1u,.cup_access={15u,15u,15u}};
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    PortableFrontendFlow before=flow;
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[8u*240u+128u]==0xff020202u); /* foreground heading */
    CHECK(pixels[8u*240u+129u]==0xff509058u); /* source10/16 panel over6/16 backdrop */
    /* Captured cup panel pairs distinguish expanded-channel blending from
     * rounding to RGB555 again before expansion. */
    const uint32_t blend_pairs[][3]={
        {0xffffdeb5u,0xff63ef7bu,0xffc4e49fu},
        {0xffffb58cu,0xff63ef7bu,0xffc4ca85u},
        {0xffffb58cu,0xffceffc6u,0xffecd0a1u}};
    for(unsigned pair=0u;pair<3u;++pair){
        motion_u32(panel+36u,blend_pairs[pair][0]);
        motion_u32(background+32u+(8u*256u+129u)*4u,blend_pairs[pair][1]);
        CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[8u*240u+129u]==blend_pairs[pair][2]);
    }
    motion_u32(panel+36u,0xff80c040u);
    motion_u32(background+32u+(8u*256u+129u)*4u,0xff004080u);
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[120u*240u+88u]==0xff080808u); /* locked stock trophy stays visible */
    CHECK(pixels[16u*240u+144u]==0xff0b0b0bu); /* cycling second cover */
    CHECK(pixels[96u*240u+120u]==0xff111111u); /* selected second name */
    CHECK(pixels[40u*240u+64u]==0xff090909u);
    CHECK(memcmp(&flow,&before,sizeof flow)==0&&flow.course_track==0u);
    /* New packs select authored unavailable art; access changes restore normal art.
     * The preceding assertion also covers missing-variant compatibility. */
    uint8_t locked_image[36];memcpy(locked_image,images[7],sizeof locked_image);
    motion_u32(locked_image+32u,0xff102030u);
    sections[COUNT]=(PortableAssetSectionView){.kind=15u,
        .content_id=PORTABLE_COURSE_LOCKED_CUP+4u,.version=1u,
        .bytes=locked_image,.size=sizeof locked_image};
    pack.section_count=COUNT+1u;
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[120u*240u+88u]==0xff102030u);
    flow.cup_access[0]=31u;
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[120u*240u+88u]==0xff080808u);
    flow.cup_access[0]=15u;
    flow.course_phase=1u;flow.tick=0u;
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[128u*240u+204u]==0xff121212u); /* source OK anchor */
    flow.tick=16u;
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[128u*240u+204u]==0xff121212u); /* source mask ignores bit4 */
    flow.tick=32u;
    CHECK(portable_course_menu_render(&pack,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[128u*240u+204u]==0xff000000u);
    uint32_t *physical=malloc(973u*640u*sizeof *physical);CHECK(physical!=NULL);
    for(unsigned i=0u;i<973u*640u;++i)physical[i]=0xdeadbeefu;
    PortableSoftwareFrame scaled={physical,240u,160u,NULL,4u,973u,0u,NULL};
    before=flow;
    CHECK(portable_course_menu_render(&pack,&flow,&scaled)==PORTABLE_FRONTEND_PRESENTATION_OK);
    for(unsigned y=32u;y<36u;++y)for(unsigned x=516u;x<520u;++x)
        CHECK(physical[y*973u+x]==0xff509058u);
    CHECK(physical[32u*973u+512u]==0xff020202u); /* heading priority survives4x */
    for(unsigned y=0u;y<640u;++y)for(unsigned x=960u;x<973u;++x)
        CHECK(physical[y*973u+x]==0xdeadbeefu);
    CHECK(memcmp(&flow,&before,sizeof flow)==0);
    free(physical);
    free(background);
    return 0;
}

static int test_common_heading_anchor(void)
{
    const uint32_t ids[]={PORTABLE_MENU_BACKGROUND,PORTABLE_MENU_HEADING,
        PORTABLE_MENU_MODE,PORTABLE_MENU_MODE+1u,PORTABLE_MENU_MODE+2u,PORTABLE_MENU_MODE+3u,
        PORTABLE_MENU_CLASS,PORTABLE_MENU_CLASS+1u,PORTABLE_MENU_CLASS+2u,
        PORTABLE_MENU_TRIAL,PORTABLE_MENU_TRIAL+1u};
    enum { COUNT=sizeof ids/sizeof ids[0] };
    uint8_t images[COUNT][48]={0};PortableAssetSectionView sections[COUNT+1u];
    for(unsigned i=0;i<COUNT;++i){
        memcpy(images[i],"MKSCIMG1",8u);images[i][8]=32u;images[i][10]=1u;
        images[i][12]=images[i][14]=2u;motion_u32(images[i]+24u,32u);motion_u32(images[i]+28u,48u);
        sections[i]=(PortableAssetSectionView){.kind=15u,.content_id=ids[i],.version=1u,.bytes=images[i],.size=48u};
    }
    /* A strip's authored left/top plus its transparent corner isolate the
     * source3FD0 per-letter anchor from the native draw position. */
    images[1][16]=146u;images[1][17]=255u; /* -110 */
    images[1][18]=247u;images[1][19]=255u; /* -9 */
    motion_u32(images[1]+44u,0xffff00ffu);
    const PortableAssetPackView pack={.resolved_sections=sections,.section_count=COUNT};
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_MODE,.players=1u};
    uint32_t *pixels=malloc(973u*640u*sizeof *pixels);CHECK(pixels);
    for(unsigned scale=1u;scale<=4u;scale*=2u){
        PortableSoftwareFrame frame={pixels,240u,160u,NULL,(uint8_t)scale,973u,0u,NULL};
        for(unsigned mode=0;mode<3u;++mode){
            flow.mode_phase=(uint8_t)(mode==1u);
            const PortableFrontendPage page=mode==2u?PORTABLE_FRONTEND_PAGE_TIME_TRIAL:PORTABLE_FRONTEND_PAGE_MODE;
            const PortableFrontendFlow before=flow;
            CHECK(portable_menu_render(&pack,&flow,page,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
            for(unsigned y=8u*scale;y<9u*scale;++y)for(unsigned x=12u*scale;x<13u*scale;++x)
                CHECK(pixels[y*973u+x]==0xffff00ffu);
            CHECK(pixels[16u*scale*973u+19u*scale]!=0xffff00ffu);
            CHECK(memcmp(&flow,&before,sizeof flow)==0);
        }
    }
    /* FEMv3 keeps a strip-only mod visible through a group-local fallback;
     * matching stock art continues to use the animated individual glyph. */
    uint8_t metadata[PORTABLE_FRONTEND_MOTION_BYTES+88u]={0};
    motion_u32(metadata,0x314d4546u);motion_u32(metadata+4u,9u);
    motion_u32(metadata+8u,14u);motion_u32(metadata+12u,5u);
    motion_u32(metadata+4280u,0x3147484du);motion_u32(metadata+4284u,4u);
    for(unsigned group=0;group<4u;++group){
        const unsigned strip=group==0u?1u:0u;uint32_t digest=UINT32_C(2166136261);
        for(unsigned i=0;i<48u;++i)digest=(digest^images[strip][i])*UINT32_C(16777619);
        uint8_t *row=metadata+4288u+group*20u;
        motion_u32(row,1u);motion_u32(row+4u,ids[strip]);motion_u32(row+8u,digest);
        motion_u32(row+12u,PORTABLE_MENU_TRIAL+1u);row[16]=(uint8_t)(30u+group);row[18]=20u;
    }
    motion_u32(images[COUNT-1u]+32u,0xff00ffffu);
    sections[COUNT]=(PortableAssetSectionView){.kind=92u,.content_id=1u,.version=3u,.bytes=metadata,.size=sizeof metadata};
    PortableAssetPackView layered=pack;layered.section_count=COUNT+1u;
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;flow.mode_phase=0u;
    CHECK(portable_frontend_configure_motion(&layered,&flow)&&flow.heading_static_mask==0u);
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,973u,0u,NULL};
    portable_software_frame_clear(&frame,0u);
    CHECK(portable_frontend_heading_draw(&layered,&flow,&frame,0u,PORTABLE_MENU_HEADING,121,16)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[20u*973u+30u]==0xff00ffffu&&pixels[8u*973u+12u]!=0xffff00ffu);
    motion_u32(images[1]+44u,0xffff0000u);
    CHECK(portable_frontend_configure_motion(&layered,&flow)&&flow.heading_static_mask==1u);
    portable_software_frame_clear(&frame,0u);
    CHECK(portable_frontend_heading_draw(&layered,&flow,&frame,0u,PORTABLE_MENU_HEADING,121,16)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[8u*973u+12u]==0xffff0000u&&pixels[20u*973u+30u]!=0xff00ffffu);
    CHECK(portable_menu_heading_start(&flow.heading,&flow.heading_rules,1u,1));
    CHECK(portable_frontend_heading_draw(&layered,&flow,&frame,1u,PORTABLE_MENU_HEADING,121,16)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[20u*973u+31u]==0xff00ffffu);
    metadata[4288u]=17u;
    PortableFrontendMotion malformed;
    CHECK(!portable_frontend_motion_open(metadata,sizeof metadata,&malformed));
    free(pixels);return 0;
}

static int test_title_cursor_pixels(void)
{
    const uint32_t ids[14]={PORTABLE_TITLE_BACKGROUND,PORTABLE_TITLE_FRAME,
        PORTABLE_TITLE_UP,PORTABLE_TITLE_DOWN,PORTABLE_TITLE_LABEL,
        PORTABLE_TITLE_LABEL+1u,PORTABLE_TITLE_LABEL+2u,
        PORTABLE_TITLE_SELECTED_LABEL,PORTABLE_TITLE_SELECTED_LABEL+1u,PORTABLE_TITLE_SELECTED_LABEL+2u,
        PORTABLE_TITLE_BACKGROUND_FRAME+1u,PORTABLE_TITLE_BACKGROUND_FRAME+2u,
        PORTABLE_TITLE_BACKGROUND_FRAME+3u,PORTABLE_TITLE_BACKGROUND_FRAME+4u};
    uint8_t images[14][36]={0};PortableAssetSectionView sections[14];
    for(unsigned i=0u;i<14u;++i){
        memcpy(images[i],"MKSCIMG1",8u);images[i][8]=32u;images[i][10]=1u;
        images[i][12]=images[i][14]=1u;images[i][24]=32u;images[i][28]=36u;
        motion_u32(images[i]+32u,i>=7u&&i<10u?0xff00ff00u:i>=4u&&i<7u?0xffff0000u:0u);
        sections[i]=(PortableAssetSectionView){.kind=15u,.content_id=ids[i],.version=1u,
            .bytes=images[i],.size=sizeof images[i]};
    }
    PortableAssetPackView pack={.resolved_sections=sections,.section_count=14u};
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU,.players=1u,.page_ticks=70u};
    int32_t samples[8];for(unsigned i=0u;i<8u;++i)samples[i]=(int32_t)(i+1u)*4096;
    CHECK(portable_frontend_flow_set_title_cursor_sine(&flow,samples));
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_DOWN));
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    for(unsigned tick=0u;tick<=9u;++tick){
        PortableFrontendFlow before=flow;
        CHECK(portable_title_render(&pack,&flow,flow.page,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
        const int y=tick>=8u?116:132-(int)tick*2;
        CHECK(pixels[(unsigned)(y+16)*240u+120u]==
            (y+16<147?UINT32_C(0xff00ff00):UINT32_C(0xff000000)));
        if(y>=117)CHECK(pixels[(unsigned)y*240u+120u]==UINT32_C(0xffff0000));
        if(tick<9u)CHECK(portable_frontend_flow_tick(&flow,0u));
    }
    PortableFrontendPresentationScratch scratch={0};
    flow.pending_page=PORTABLE_FRONTEND_PAGE_MODE;flow.transition_out_ticks=18u;
    flow.transition_total_ticks=132u;flow.transition_ticks=130u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==UINT32_C(0xff080808)); /* black→white source level1: RGB555(1,1,1) */
    return 0;
}

static int test_menu_background_layer_order(void)
{
    enum { IMAGE_BYTES=32+256*256*4 };
    const uint32_t ids[5]={PORTABLE_MENU_BACK_PATTERN,PORTABLE_MENU_PATTERN,
        PORTABLE_MENU_AFFINE,PORTABLE_MENU_WHEEL,PORTABLE_MENU_ORBIT};
    uint8_t *images=calloc(5u,IMAGE_BYTES);
    uint32_t *pixels=malloc(973u*640u*sizeof *pixels);
    CHECK(images!=NULL&&pixels!=NULL);
    PortableAssetSectionView sections[8];
    for(unsigned i=0u;i<5u;++i){
        uint8_t *image=images+i*IMAGE_BYTES;
        memcpy(image,"MKSCIMG1",8u);image[8]=32u;image[10]=1u;
        image[13]=image[15]=1u;motion_u32(image+24u,32u);motion_u32(image+28u,IMAGE_BYTES);
        for(unsigned y=0u;y<256u;++y)for(unsigned x=0u;x<256u;++x){
            uint32_t color=i==0u?0xff00ff00u:i==1u&&!(x&1u)?0xff0000ffu:0u;
            if(i==2u&&y==112u&&x==96u)color=0xff00ffffu;
            if(i==2u&&y==112u&&x==97u)color=0xffff0000u;
            motion_u32(image+32u+(y*256u+x)*4u,color);
        }
        sections[i]=(PortableAssetSectionView){.kind=15u,.content_id=ids[i],
            .version=1u,.bytes=image,.size=IMAGE_BYTES};
    }
    uint8_t metadata[PORTABLE_FRONTEND_MOTION_BYTES]={0};
    motion_u32(metadata,0x314d4546u);motion_u32(metadata+4u,9u);
    motion_u32(metadata+8u,14u);motion_u32(metadata+12u,5u);
    motion_u32(metadata+164u+256u*4u,32768u);
    motion_u32(metadata+164u+272u*4u,32768u);
    motion_u32(metadata+164u+768u*4u,(uint32_t)-32768);
    sections[5]=(PortableAssetSectionView){.kind=92u,.content_id=1u,.version=2u,
        .bytes=metadata,.size=sizeof metadata};
    PortableAssetPackView pack={.resolved_sections=sections,.section_count=6u};
    PortableFrontendFlow flow={0},before=flow;
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    CHECK(portable_menu_background_render(&pack,&flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==0xff00ff7bu&&pixels[1]==0xff00ff00u); /* BG0 alpha8 over BG1 alpha16 */
    CHECK(pixels[80u*240u+200u]==0xff00ffffu); /* BG2 source origin96,112 */
    CHECK(pixels[80u*240u+202u]==0xffff0000u); /* Q8 inverse213 */
    CHECK(memcmp(&flow,&before,sizeof flow)==0);
    frame.scale=4u;frame.stride=973u;
    for(unsigned i=0u;i<973u*640u;++i)pixels[i]=0xdeadbeefu;
    CHECK(portable_menu_background_render(&pack,&flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[320u*973u+804u]==0xff00ffffu);
    CHECK(pixels[320u*973u+806u]==0xffff0000u); /* not a4x4 block copy */
    for(unsigned y=0u;y<640u;++y)for(unsigned x=960u;x<973u;++x)
        CHECK(pixels[y*973u+x]==0xdeadbeefu);
    flow.tick=4u;
    CHECK(portable_menu_background_render(&pack,&flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==0xff00ff00u&&pixels[4]==0xff00ff7bu); /* only BG0 scrolls */
    uint8_t *atlas=calloc(1u,32u+256u*1792u*4u);CHECK(atlas!=NULL);
    memcpy(atlas,images,32u);atlas[15]=7u;motion_u32(atlas+28u,32u+256u*1792u*4u);
    for(unsigned row=0u;row<7u;++row)for(unsigned p=0u;p<256u*256u;++p)
        motion_u32(atlas+32u+(row*256u*256u+p)*4u,0xff102000u+row);
    sections[6]=(PortableAssetSectionView){.kind=15u,.content_id=PORTABLE_COURSE_BACKGROUND,
        .version=1u,.bytes=atlas,.size=32u+256u*1792u*4u};
    sections[7]=sections[2];sections[7].content_id=PORTABLE_COURSE_AFFINE;
    motion_u32(images+2u*IMAGE_BYTES+32u+(112u*256u+96u)*4u,0xff80a000u);
    pack.section_count=8u;frame.scale=1u;frame.stride=0u;flow.tick=0u;
    for(unsigned phase=0u;phase<7u;++phase){
        flow.page_ticks=phase*8u;before=flow;
        CHECK(portable_menu_background_render(&pack,&flow,PORTABLE_COURSE_BACKGROUND,PORTABLE_COURSE_BACKGROUND,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[0]==0xff102000u+phase); /* cup BG1 seven-palette cycle */
        CHECK(pixels[80u*240u+200u]==0xff80a000u); /* cup BG2, not global cyan */
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
    }
    /* A semi-transparent OBJ selects BG layers beneath other OBJ, not the
     * flattened gray wheel. The cup panel uses this same sample view. */
    uint8_t *wheel=images+3u*IMAGE_BYTES;
    wheel[16]=wheel[18]=128u;wheel[17]=wheel[19]=255u;
    for(unsigned p=0u;p<256u*256u;++p)motion_u32(wheel+32u+p*4u,0xffc6c6c6u);
    flow.page_ticks=0u;
    CHECK(portable_menu_background_render(&pack,&flow,PORTABLE_COURSE_BACKGROUND,PORTABLE_COURSE_BACKGROUND,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[80u*240u+200u]==0xffc6c6c6u);
    PortableNativeImage pattern_view,affine_view;PortableFrontendMotion motion;
    CHECK(portable_native_image_open(atlas,sections[6].size,&pattern_view));
    CHECK(portable_native_image_open(sections[7].bytes,sections[7].size,&affine_view));
    CHECK(portable_frontend_motion_load(&pack,&motion));
    PortableMenuBackgroundLayers layers;
    portable_menu_background_layers_init(&layers,&pattern_view,&affine_view,&flow,&motion);
    CHECK(portable_menu_background_layers_sample(&layers,200u,80u,1u)==0xff80a000u);
    free(atlas);
    free(pixels);free(images);return 0;
}

static int test_source_menu_motion(void)
{
    uint8_t bytes[PORTABLE_FRONTEND_MOTION_BYTES]={0};
    PortableFrontendMotion motion={0},before;
    motion_u32(bytes,0x314d4546u);motion_u32(bytes+4,9u);
    motion_u32(bytes+8,14u);motion_u32(bytes+12,5u);
    /* Invented anchors and sine samples test signed widths and source curve
     * arithmetic; no extracted table is embedded in this fixture. */
    bytes[16]=0x9c;bytes[17]=0xff; /* entry X=-100 */
    bytes[20]=100;bytes[22]=80;
    motion_u32(bytes+164u+128u*4u,16384u);
    motion_u32(bytes+164u+256u*4u,32768u);
    motion_u32(bytes+164u+110u*4u,16384u);
    CHECK(portable_frontend_motion_open(bytes,sizeof bytes,&motion));
    CHECK(portable_menu_curve10(&motion,240,0,0,20)==240);
    CHECK(portable_menu_curve10(&motion,240,0,10,20)==30);
    CHECK(portable_menu_curve10(&motion,240,0,20,20)==0);
    CHECK(portable_menu_curve10(&motion,240,0,-1,20)==240);
    int x,y;portable_menu_character_anchor(&motion,0u,0u,&x,&y);
    CHECK(x==-200&&y==-80);
    portable_menu_character_anchor(&motion,0u,8u,&x,&y);
    CHECK(x==-100&&y==-40);
    portable_menu_character_anchor(&motion,0u,16u,&x,&y);
    CHECK(x==0&&y==0);
    CHECK(portable_menu_asr15(-1)==-1&&portable_menu_asr15(32767)==0);
    CHECK(portable_menu_preview_offset(&motion,16u)==0);
    CHECK(portable_frontend_motion_open(bytes,PORTABLE_FRONTEND_MOTION_LEGACY_BYTES,&motion));
    CHECK(motion.size==PORTABLE_FRONTEND_MOTION_LEGACY_BYTES);
    CHECK(portable_frontend_motion_open(bytes,sizeof bytes,&motion));
    before=motion;
    CHECK(!portable_frontend_motion_open(bytes,sizeof bytes-1u,&motion));
    CHECK(memcmp(&motion,&before,sizeof motion)==0);
    motion_u32(bytes+164u,32769u);
    CHECK(!portable_frontend_motion_open(bytes,sizeof bytes,&motion));
    motion_u32(bytes+164u,0u);bytes[17]=0x7f;
    CHECK(!portable_frontend_motion_open(bytes,sizeof bytes,&motion));
    return 0;
}

static int physical_differs_from_replica(const uint32_t *native,const uint32_t *physical,size_t stride)
{
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x)
        if(physical[y*stride+x]!=native[(y/4u)*240u+x/4u])return 1;
    return 0;
}

static int test_menu_physical_sampling(const PortableAssetPackView *pack,const PortableFrontendMotion *motion)
{
    const size_t stride=973u,count=stride*640u;
    uint32_t *native=malloc(240u*160u*4u),*physical=malloc(count*4u),*before=malloc(count*4u);
    CHECK(native&&physical&&before);
    const uint32_t canary=0x13579bdfu;
    for(size_t i=0;i<count;++i)physical[i]=canary;
    PortableSoftwareFrame one={native,240u,160u,NULL,1u,0u,0u,NULL};
    PortableSoftwareFrame four={physical,240u,160u,NULL,4u,stride,0u,NULL};
    portable_software_frame_clear(&one,0xffabcdefu);portable_software_frame_clear(&four,0xffabcdefu);
    CHECK(portable_frontend_image_draw_scaled(pack,PORTABLE_MENU_PATTERN,&one,20,30,0,0,240,160,300u)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_frontend_image_draw_scaled(pack,PORTABLE_MENU_PATTERN,&four,20,30,0,0,240,160,300u)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(native[30u*240u+22u]==0xff010000u);
    CHECK(physical_differs_from_replica(native,physical,stride));
    /* Mode/cup pulse, rotating portrait/star and preview zoom all share the
     * affine sampler, with an authored pivot distinct from image origin. */
    const unsigned transforms[][2]={{192u,0u},{272u,0u},{370u,0u},{384u,960u},{2048u,0u}};
    for(unsigned i=0;i<sizeof transforms/sizeof transforms[0];++i){
        portable_software_frame_clear(&one,0xffabcdefu);portable_software_frame_clear(&four,0xffabcdefu);
        CHECK(portable_frontend_image_draw_affine(pack,PORTABLE_MENU_PATTERN,&one,motion,17,9,30,40,transforms[i][0],transforms[i][1],240)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(portable_frontend_image_draw_affine(pack,PORTABLE_MENU_PATTERN,&four,motion,17,9,30,40,transforms[i][0],transforms[i][1],240)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(physical_differs_from_replica(native,physical,stride));
    }
    PortableFrontendFlow flow={0},unchanged;
    flow.tick=9u;unchanged=flow;
    CHECK(portable_menu_background_render(pack,&flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,&one)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_menu_background_render(pack,&flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,&four)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(memcmp(&flow,&unchanged,sizeof flow)==0);
    CHECK(physical_differs_from_replica(native,physical,stride));
    /* The actual incoming mode transition applies brightness8 after drawing
     * the transformed layers. It must blend each physical pixel separately. */
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;flow.page_ticks=8u;
    flow.motion_stage=1u;flow.motion_tick=8u;flow.motion_delay=85u;flow.motion_player_entry=1u;
    CHECK(portable_menu_render(pack,&flow,flow.page,&four)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_menu_chrome_render(pack,&flow,flow.page,&four)==PORTABLE_FRONTEND_PRESENTATION_OK);
    memcpy(before,physical,count*4u);
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;flow.pending_page=PORTABLE_FRONTEND_PAGE_MODE;
    flow.transition_out_ticks=18u;flow.transition_total_ticks=132u;flow.transition_ticks=106u;
    PortableFrontendPresentationScratch scratch={0};unchanged=flow;
    CHECK(portable_frontend_presentation_render(pack,&flow,&four,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(memcmp(&flow,&unchanged,sizeof flow)==0);
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x){
        uint32_t expected=0xff000000u;
        for(unsigned shift=0;shift<24u;shift+=8u){
            unsigned channel=((((before[y*stride+x]>>shift)&255u)>>3u)+31u)/2u;
            expected|=(channel<<3u|channel>>2u)<<shift;
        }
        CHECK(physical[y*stride+x]==expected);
    }
    /* Dimming also consumes a real high-resolution underlay, without sampling
     * just the first pixel of each logical4x4 block. */
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x)
        physical[y*stride+x]=(x&1u)?0xfff01020u:0xff20f010u;
    memcpy(before,physical,count*4u);portable_menu_dim(&four);
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x){
        uint32_t expected=0xff000000u;
        for(unsigned shift=0;shift<24u;shift+=8u){
            unsigned channel=(((before[y*stride+x]>>shift)&255u)>>3u)*3u>>2u;
            expected|=(channel<<3u|channel>>2u)<<shift;
        }
        CHECK(physical[y*stride+x]==expected);
    }
    for(unsigned y=0;y<640u;++y)for(size_t x=960u;x<stride;++x)CHECK(physical[y*stride+x]==canary);
    free(before);free(physical);free(native);return 0;
}

/* Bounded native-image fixtures keep foreground and periodic background art separate. */
static uint8_t *wide_test_image(unsigned width,unsigned height)
{
    const size_t size=32u+(size_t)width*height*4u;
    uint8_t *image=calloc(1u,size);
    if(!image)return NULL;
    memcpy(image,"MKSCIMG1",8u);image[8]=32u;image[10]=1u;
    image[12]=(uint8_t)width;image[13]=(uint8_t)(width>>8u);
    image[14]=(uint8_t)height;image[15]=(uint8_t)(height>>8u);
    motion_u32(image+24u,32u);motion_u32(image+28u,(uint32_t)size);
    return image;
}

static uint32_t wide_pattern_color(unsigned x,unsigned y)
{
    const unsigned red=x&31u,blue=y&31u;
    return 0xff000000u|((red*8u+red/4u)<<16u)|(blue*8u+blue/4u);
}

static uint64_t frontend_payload_hash(const PortableAssetPackView *pack)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    for(uint32_t index=0u;index<pack->section_count;++index){
        PortableAssetSectionView section;
        if(portable_asset_pack_section_at(pack,index,&section)!=PORTABLE_ASSET_PACK_OK)return 0u;
        for(size_t byte=0u;byte<section.size;++byte)
            hash=(hash^section.bytes[byte])*UINT64_C(1099511628211);
    }
    return hash;
}

static int test_extended_menu_pages(const PortableAssetPackView *base)
{
    enum { EXTRA=7, IMAGE_BYTES=32+256*256*4 };
    const uint32_t ids[EXTRA]={PORTABLE_MENU_BACK_PATTERN,PORTABLE_MENU_PATTERN,
        PORTABLE_MENU_AFFINE,PORTABLE_COURSE_BACKGROUND,PORTABLE_COURSE_AFFINE,0x23804u,0x23805u};
    PortableAssetSectionView *sections=calloc(base->section_count+EXTRA,sizeof *sections);
    CHECK(sections);
    for(uint32_t i=0u;i<base->section_count;++i)
        CHECK(portable_asset_pack_section_at(base,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    uint32_t section_count=base->section_count;
    uint8_t *images[EXTRA]={0};
    for(unsigned i=0u;i<EXTRA;++i){
        images[i]=wide_test_image(256u,256u);CHECK(images[i]);
        for(unsigned y=0u;y<256u;++y)for(unsigned x=0u;x<256u;++x)
            motion_u32(images[i]+32u+(y*256u+x)*4u,
                i==0u?0xff00ff00u:i==2u||i==4u?0u:
                i==6u?(x>=104u && x<232u && y>=16u && y<144u?
                    ((y/16u)&1u?0xff848484u:0xffa5a5a5u):0u):wide_pattern_color(x,y));
        uint32_t slot=0u;
        while(slot<section_count && !(sections[slot].kind==15u && sections[slot].content_id==ids[i]))
            ++slot;
        if(slot==section_count)++section_count;
        sections[slot]=(PortableAssetSectionView){.kind=15u,
            .content_id=ids[i],.version=1u,.bytes=images[i],.size=IMAGE_BYTES};
    }
    PortableNativeImage texture;
    CHECK(portable_native_image_open(images[1],IMAGE_BYTES,&texture));
    PortableMenuBackgroundLayers layers={.pattern=&texture,.scroll_x=2,.scroll_y=-3};
    const int coordinates[]={-200,-1,0,1,82};
    for(unsigned scale=1u;scale<=4u;++scale)for(unsigned i=0u;i<5u;++i){
        const int x=coordinates[i];
        const int logical=x>=0?x/(int)scale:-((-x+(int)scale-1)/(int)scale);
        CHECK(portable_menu_background_layers_sample_at(&layers,x,0,scale)==
            wide_pattern_color((unsigned)((logical+2+512)%256),253u));
        layers.pattern=NULL;layers.affine=&texture;layers.affine_a=256;
        CHECK(portable_menu_background_layers_sample_at(&layers,x,0,scale)==
            wide_pattern_color((unsigned)((logical-104+512)%256),32u));
        layers.pattern=&texture;layers.affine=NULL;
    }
    PortableAssetPackView pack=*base;pack.resolved_sections=sections;pack.section_count=section_count;
    const PortableAssetPackView saved_pack=pack;
    const uint64_t payload_hash=frontend_payload_hash(&pack);
    const PortableFrontendPage pages[]={PORTABLE_FRONTEND_PAGE_MODE,PORTABLE_FRONTEND_PAGE_MODE,
        PORTABLE_FRONTEND_PAGE_TIME_TRIAL,PORTABLE_FRONTEND_PAGE_CHARACTER,
        PORTABLE_FRONTEND_PAGE_COURSE,PORTABLE_FRONTEND_PAGE_RECORDS};
    const unsigned widths[]={284u,373u};
    for(unsigned output=0u;output<2u;++output)for(unsigned scale=1u;scale<=4u;++scale){
        const unsigned width=widths[output],stride=width*scale+7u,rows=160u*scale;
        const size_t count=(size_t)stride*rows;
        uint32_t *storage=malloc((count+2u)*sizeof *storage);
        uint32_t *native=malloc((size_t)240u*160u*scale*scale*sizeof *native);
        CHECK(storage && native);
        PortableSoftwareFrame wide={storage+1u,width,160u,NULL,(uint8_t)scale,stride,0u,NULL};
        PortableSoftwareFrame narrow={native,240u,160u,NULL,(uint8_t)scale,0u,0u,NULL};
        const unsigned center=(width-240u)/2u*scale;
        for(unsigned page=0u;page<sizeof pages/sizeof pages[0];++page)
        for(unsigned phase=0u;phase<4u;++phase){
            /* All transition states at native sampling; larger guarded surfaces
             * cover settled and incoming pages without repeating the matrix. */
            if(scale>1u && (phase==1u || phase==2u))continue;
            PortableFrontendFlow flow={.page=pages[page],.players=1u,.mode_phase=(uint8_t)(page==1u)};
            flow.tick=flow.page_ticks=phase==0u?0u:9u;
            if(phase>=2u){
                flow.pending_page=pages[page]==PORTABLE_FRONTEND_PAGE_CHARACTER?
                    PORTABLE_FRONTEND_PAGE_MODE:PORTABLE_FRONTEND_PAGE_CHARACTER;
                flow.transition_total_ticks=64u;flow.transition_out_ticks=33u;
                flow.transition_ticks=phase==2u?39u:16u;
            }
            const PortableFrontendFlow before=flow;
            PortableFrontendPresentationScratch off={0},on={.extended_backgrounds=1u};
            for(size_t pixel=0u;pixel<count+2u;++pixel)storage[pixel]=0xdeadbeefu;
            CHECK(portable_frontend_presentation_render(&pack,&flow,&wide,&off)==
                PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
            CHECK(storage[1]==0xdeadbeefu);
            const PortableFrontendPresentationStatus native_status=
                portable_frontend_presentation_render(&pack,&flow,&narrow,&off);
            if(native_status!=PORTABLE_FRONTEND_PRESENTATION_OK)
                fprintf(stderr,"wide native fixture: status=%s page=%u class=%u width=%u scale=%u tick=%u transition=%u pending=%u phase=%u\n",
                    portable_frontend_presentation_status_name(native_status),(unsigned)flow.page,
                    (unsigned)flow.mode_phase,width,scale,(unsigned)flow.tick,
                    (unsigned)flow.transition_ticks,(unsigned)flow.pending_page,phase);
            CHECK(native_status==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(portable_frontend_presentation_render(&pack,&flow,&wide,&on)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(!memcmp(&flow,&before,sizeof flow));
            CHECK(!memcmp(&pack,&saved_pack,sizeof pack));
            for(unsigned y=0u;y<rows;++y){
                CHECK(!memcmp(wide.pixels+(size_t)y*stride+center,
                    native+(size_t)y*240u*scale,240u*scale*sizeof *native));
                for(unsigned x=width*scale;x<stride;++x)CHECK(wide.pixels[(size_t)y*stride+x]==0xdeadbeefu);
            }
            CHECK(storage[0]==0xdeadbeefu && storage[count+1u]==0xdeadbeefu);
            if(phase==0u){
                const uint32_t checksum=portable_software_frame_checksum(&narrow);
                CHECK(portable_frontend_presentation_render(&pack,&flow,&narrow,&on)==PORTABLE_FRONTEND_PRESENTATION_OK);
                CHECK(portable_software_frame_checksum(&narrow)==checksum);
            }
            if(phase<3u){
                /* Independent signed wrapping oracle: the left margin begins
                 * before source X0, including the three-times physical surface. */
                const int scroll=(int)(flow.tick/4u),negative=-(int)((flow.tick+3u)/4u);
                for(unsigned y=0u;y<rows;++y)for(unsigned x=0u;x<center;++x){
                    int sx=(int)(x/scale)-(int)((width-240u)/2u)+scroll;
                    int sy=(int)(y/scale)+negative;
                    sx=(sx%256+256)%256;sy=(sy%256+256)%256;
                    uint32_t color=wide_pattern_color((unsigned)sx,(unsigned)sy);
                    if(pages[page]!=PORTABLE_FRONTEND_PAGE_COURSE && pages[page]!=PORTABLE_FRONTEND_PAGE_RECORDS){
                        const unsigned red=((unsigned)sx&31u)/2u,blue=((unsigned)sy&31u)/2u;
                        color=0xff00ff00u|((red*8u+red/4u)<<16u)|(blue*8u+blue/4u);
                    }
                    if(phase==2u){
                        uint32_t faded=0xff000000u;
                        for(unsigned channel=0u;channel<24u;channel+=8u){
                            const unsigned value=((color>>channel)&255u)>>3u;
                            const unsigned dimmed=(value*4u+31u*12u)/16u; /* source white fade level12 */
                            faded|=(dimmed*8u+dimmed/4u)<<channel;
                        }
                        color=faded;
                    }
                    if(wide.pixels[(size_t)y*stride+x]!=color){
                        fprintf(stderr,"wide background: page=%u class=%u width=%u scale=%u tick=%u transition=%u phase=%u x=%u y=%u expected=%08x actual=%08x\n",
                            (unsigned)flow.page,flow.mode_phase,width,scale,flow.tick,
                            flow.transition_ticks,phase,x,y,(unsigned)color,
                            (unsigned)wide.pixels[(size_t)y*stride+x]);
                        CHECK(0);
                    }
                }
            }
        }
        free(native);free(storage);
    }
    CHECK(frontend_payload_hash(&pack)==payload_hash);
    for(unsigned i=0u;i<EXTRA;++i)free(images[i]);
    free(sections);
    return 0;
}

static int test_extended_title_layers(void)
{
    uint8_t *flat=wide_test_image(240u,160u),*sky=wide_test_image(256u,256u);
    uint8_t *clouds=wide_test_image(256u,512u),*night=wide_test_image(256u,2048u);
    CHECK(flat && sky && clouds && night);
    for(unsigned i=0u;i<240u*160u;++i)motion_u32(flat+32u+i*4u,0xff123456u);
    for(unsigned y=0u;y<256u;++y)for(unsigned x=0u;x<256u;++x)
        motion_u32(sky+32u+(y*256u+x)*4u,wide_pattern_color(x,y));
    for(unsigned y=0u;y<2048u;++y)for(unsigned x=0u;x<256u;++x)
        motion_u32(night+32u+(y*256u+x)*4u,0xff100000u+(y/256u)*0x0800u);
    PortableAssetSectionView sections[]={
        {.kind=15u,.content_id=PORTABLE_TITLE_BACKGROUND,.version=1u,.bytes=flat,.size=32u+240u*160u*4u},
        {.kind=15u,.content_id=PORTABLE_TITLE_WIDE_SKY,.version=1u,.bytes=sky,.size=32u+256u*256u*4u},
        {.kind=15u,.content_id=PORTABLE_TITLE_WIDE_CLOUDS,.version=1u,.bytes=clouds,.size=32u+256u*512u*4u},
        {.kind=15u,.content_id=PORTABLE_TITLE_WIDE_SKY+4u,.version=1u,.bytes=night,.size=32u+256u*2048u*4u},
        {.kind=15u,.content_id=PORTABLE_TITLE_WIDE_CLOUDS+4u,.version=1u,.bytes=clouds,.size=32u+256u*512u*4u}};
    PortableAssetPackView pack={.resolved_sections=sections,.section_count=5u};
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_TITLE_INTRO};
    PortableFrontendPresentationScratch scratch={.extended_backgrounds=1u};
    uint32_t *storage=malloc(((373u*4u+7u)*640u+2u)*sizeof *storage);CHECK(storage);
    uint32_t *pixels=storage+1u;
    const PortableFrontendFlow before=flow;
    const PortableAssetPackView saved_pack=pack;
    for(unsigned width=284u;width<=373u;width+=89u)for(unsigned scale=1u;scale<=4u;++scale){
        const unsigned stride=width*scale+7u,rows=160u*scale,center=(width-240u)/2u;
        PortableSoftwareFrame frame={pixels,width,160u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(size_t i=0u;i<(size_t)stride*rows+2u;++i)storage[i]=0xdeadbeefu;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        for(unsigned y=0u;y<rows;++y)for(unsigned x=0u;x<stride;++x){
            const unsigned source_x=(unsigned)(((int)(x/scale)-(int)center+256)%256);
            const uint32_t expected=x>=width*scale?0xdeadbeefu:
                x>=center*scale&&x<(center+240u)*scale?0xff123456u:
                wide_pattern_color(source_x,(y/scale+80u)%256u);
            CHECK(pixels[(size_t)y*stride+x]==expected);
        }
        CHECK(storage[0]==0xdeadbeefu && storage[(size_t)stride*rows+1u]==0xdeadbeefu);
    }
    CHECK(!memcmp(&flow,&before,sizeof flow) && !memcmp(&pack,&saved_pack,sizeof pack));
    PortableSoftwareFrame frame={pixels,284u,160u,NULL,3u,284u*3u+7u,0u,NULL};
    const uint64_t hash=frontend_payload_hash(&pack);
    flow.title_variant=2u;flow.tick=8u;
    CHECK(portable_title_background_render(&pack,NULL,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==0xff100000u);
    flow.tick=9u;
    CHECK(portable_title_background_render(&pack,NULL,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==0xff100800u);
    CHECK(frontend_payload_hash(&pack)==hash);
    flow.title_variant=0u;flow.tick=0u;
    pack.section_count=2u; /* An explicit sky without its paired cloud layer is invalid. */
    CHECK(portable_title_background_render(&pack,NULL,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION);
    pack.section_count=3u;clouds[15]=1u; /* Header claims256 rows, payload still contains512. */
    CHECK(portable_title_background_render(&pack,NULL,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION);
    clouds[15]=2u;
    uint8_t metadata[32]={0};memcpy(metadata,"MKSCWUI1\1\0\0\0",12u);
    PortableAssetSectionView companion_sections[5]={sections[0],sections[1],sections[2],
        {.kind=15u,.content_id=PORTABLE_TITLE_WIDE_METADATA,.version=1u,.bytes=metadata,.size=sizeof metadata}};
    PortableAssetPackView companion={.resolved_sections=companion_sections,.section_count=4u};
    pack.section_count=1u;scratch.title_background_pack=&companion;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==wide_pattern_color(21u,80u));
    CHECK(pixels[(size_t)143u*3u*frame.stride]==wide_pattern_color(21u,223u));
    CHECK(pixels[(size_t)144u*3u*frame.stride]==0xff123456u);
    CHECK(pixels[(size_t)159u*3u*frame.stride]==0xff123456u);
    uint8_t *custom=malloc(sections[0].size);CHECK(custom);memcpy(custom,flat,sections[0].size);
    motion_u32(custom+32u,0xffabcdefu);sections[0].bytes=custom;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==0xff000000u && pixels[22u*3u]==0xffabcdefu);
    sections[0].bytes=flat;
    /* One overridden alternate flag frame disables companion wings throughout
     * the scene, including ticks that still show the unchanged first frame. */
    PortableAssetSectionView alternate[2]={sections[0],sections[0]};
    alternate[1].content_id=PORTABLE_TITLE_BACKGROUND_FRAME+2u;
    alternate[1].bytes=custom;
    companion_sections[4]=alternate[1];companion_sections[4].bytes=flat;
    companion.section_count=5u;pack.resolved_sections=alternate;pack.section_count=2u;
    for(unsigned phase=0u;phase<5u;++phase){
        flow.tick=phase*9u;
        portable_software_frame_clear(&frame,0xff556677u);
        CHECK(portable_title_background_render(&pack,&companion,&flow,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[0]==0xff556677u);
    }
    /* Legacy packs with no alternate-variant art use normal surroundings,
     * just as their centered title renderer falls back to the normal image. */
    pack.resolved_sections=sections;pack.section_count=1u;companion.section_count=4u;
    flow.tick=0u;
    for(unsigned variant=1u;variant<=2u;++variant){
        flow.title_variant=(uint8_t)variant;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[0]==wide_pattern_color(21u,80u) && pixels[22u*3u]==0xff123456u);
    }
    free(custom);
    free(storage);free(night);free(clouds);free(sky);free(flat);
    return 0;
}

static int test_menu_ambient_pages(const PortableAssetPackView *base)
{
    enum { EXTRA=9, IMAGE_BYTES=32+256*256*4 };
    uint8_t metadata[PORTABLE_FRONTEND_MOTION_BYTES]={0};
    motion_u32(metadata,0x314d4546u);motion_u32(metadata+4,9u);
    motion_u32(metadata+8,14u);motion_u32(metadata+12,5u);
    for(unsigned tab=0u;tab<5u;++tab){
        metadata[144u+tab*4u]=12u;metadata[146u+tab*4u]=(uint8_t)(48u+tab*24u);
    }
    for(unsigned row=0u;row<14u;++row){
        metadata[88u+row*4u]=(uint8_t)(68u+(row%3u)*60u);
        metadata[90u+row*4u]=(uint8_t)(64u+(row%3u)*28u);
    }
    /* A triangular Q15 fixture provides signed, varying source-domain values. */
    for(unsigned i=0;i<1024u;++i){
        int v=i<256u?(int)i*128:i<768u?65536-(int)i*128:(int)i*128-131072;
        motion_u32(metadata+164u+i*4u,(uint32_t)v);
    }
    PortableAssetSectionView *sections=calloc(base->section_count+EXTRA,sizeof *sections);
    uint8_t *images=calloc(EXTRA-1u,IMAGE_BYTES);
    uint32_t *pixels=malloc(240u*160u*4u),*saved=malloc(240u*160u*4u);
    CHECK(sections&&images&&pixels&&saved);
    for(unsigned i=0;i<base->section_count;++i)
        CHECK(portable_asset_pack_section_at(base,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    const uint32_t ids[8]={PORTABLE_MENU_PATTERN,PORTABLE_MENU_AFFINE,
        PORTABLE_MENU_WHEEL,PORTABLE_MENU_ORBIT,0x23804u,0x23805u,0x20700u,0x20701u};
    for(unsigned i=0;i<8u;++i){
        uint8_t *image=images+i*IMAGE_BYTES;
        memcpy(image,"MKSCIMG1",8u);image[8]=32u;image[10]=1u;
        image[13]=image[15]=1u;motion_u32(image+24u,32u);motion_u32(image+28u,IMAGE_BYTES);
        for(unsigned y=0;y<256u;++y)for(unsigned x=0;x<256u;++x){
            uint32_t color=i==0u||i==4u?0xff000000u|(x<<16)|(y<<8):0u;
            if(i>=6u&&x<8u&&y<8u)color=i==6u?0xffff0000u:0xff00ff00u;
            motion_u32(image+32u+(y*256u+x)*4u,color);
        }
        sections[base->section_count+i]=(PortableAssetSectionView){
            .kind=15u,.content_id=ids[i],.version=1u,.bytes=image,.size=IMAGE_BYTES};
    }
    sections[base->section_count+8u]=(PortableAssetSectionView){
        .kind=92u,.content_id=1u,.version=2u,.bytes=metadata,.size=sizeof metadata};
    PortableAssetPackView pack=*base;pack.resolved_sections=sections;pack.section_count+=EXTRA;
    PortableFrontendFlow flow={0},before;PortableSoftwareFrame frame;
    PortableFrontendPresentationScratch scratch={0};
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    const PortableFrontendPage pages[]={PORTABLE_FRONTEND_PAGE_MODE,
        PORTABLE_FRONTEND_PAGE_TIME_TRIAL,PORTABLE_FRONTEND_PAGE_CHARACTER,PORTABLE_FRONTEND_PAGE_RECORDS};
    for(unsigned page=0;page<sizeof pages/sizeof pages[0];++page){
        flow.page=pages[page];flow.course_cup=0u;flow.course_track=0u;
        uint64_t checksums[4];
        for(unsigned step=0;step<4u;++step){
            flow.tick=flow.page_ticks=step*8u;before=flow;
            CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(memcmp(&flow,&before,sizeof flow)==0);
            checksums[step]=portable_software_frame_checksum(&frame);
            memcpy(saved,pixels,240u*160u*4u);
            CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(memcmp(saved,pixels,240u*160u*4u)==0);
            if(step)CHECK(checksums[step]!=checksums[step-1u]);
            /* Corner is pattern-only: exact signed floor scroll, not easing. */
            CHECK(pixels[0]==(0xff000000u|((step*2u)<<16)|((256u-step*2u)&255u)<<8));
        }
    }
    /* Cup textures use the same wrapped sampler; fixed labels stay separate. */
    flow.tick=9u;before=flow;
    CHECK(portable_menu_background_render(&pack,&flow,PORTABLE_MENU_PATTERN,PORTABLE_MENU_BACKGROUND,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==0xff02fd00u);
    CHECK(memcmp(&flow,&before,sizeof flow)==0);
    /* Exact source inverse: reciprocal scale240 would sample sourceX16,
     * while the source Q8 inverse272 correctly samples sourceX15 here. */
    CHECK(portable_frontend_image_draw_affine(&pack,PORTABLE_MENU_PATTERN,&frame,
        &(PortableFrontendMotion){metadata,sizeof metadata},0,0,3,2,272u,0u,240)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[2u*240u+18u]==0xff0f0000u);
    CHECK(portable_frontend_image_draw_atlas(&pack,PORTABLE_MENU_PATTERN,&frame,
        16u,1u,17,9,0,0,240,160)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[9u*240u+17u]==0xff001000u);
    CHECK(portable_frontend_image_draw_atlas(&pack,PORTABLE_MENU_PATTERN,&frame,
        0u,0u,0,0,0,0,240,160)==PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    CHECK(portable_frontend_image_draw_atlas(&pack,PORTABLE_MENU_PATTERN,&frame,
        16u,16u,0,0,0,0,240,160)==PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;flow.title_variant=2u;
    flow.title_ship_ticks=857u;flow.title_star_state=0u;flow.tick=flow.page_ticks=0u;
    before=flow;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(memcmp(&flow,&before,sizeof flow)==0);
    CHECK(pixels[32u*240u]==0xffff0000u);
    uint32_t fixed_label=pixels[132u*240u+120u];
    flow.title_ship_ticks=865u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[32u*240u]!=0xffff0000u);
    CHECK(pixels[132u*240u+120u]==fixed_label);
    flow.title_star_state=2u;flow.title_star_counter=16u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    int green=0;for(unsigned y=30u;y<60u;++y)for(unsigned x=10u;x<35u;++x)green|=pixels[y*240u+x]==0xff00ff00u;
    CHECK(green);
    /* Give the affine background a nonuniform source texture; other layers
     * remain transparent, so its transform is tested in isolation. */
    memcpy(images+IMAGE_BYTES+32u,images+32u,256u*256u*4u);
    PortableFrontendMotion physical_motion={metadata,sizeof metadata};
    CHECK(test_menu_physical_sampling(&pack,&physical_motion)==0);
    free(saved);free(pixels);free(images);free(sections);return 0;
}

static int test_independent_menu_layers(const PortableAssetPackView *base)
{
    uint8_t metadata[PORTABLE_FRONTEND_MOTION_BYTES]={0},unchanged[PORTABLE_FRONTEND_MOTION_BYTES];
    motion_u32(metadata,0x314d4546u);motion_u32(metadata+4,9u);
    motion_u32(metadata+8,14u);motion_u32(metadata+12,5u);
    motion_u32(metadata+164u+256u*4u,32768u);
    PortableAssetSectionView *sections=calloc(base->section_count+1u,sizeof *sections);
    uint32_t *pixels=malloc(240u*160u*4u),*first_pixels=malloc(240u*160u*4u);
    CHECK(sections&&pixels&&first_pixels);
    for(unsigned i=0u;i<base->section_count;++i)
        CHECK(portable_asset_pack_section_at(base,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
    for(unsigned slot=0u;slot<9u;++slot){
        PortableAssetSectionView section;PortableNativeImage image;
        CHECK(portable_asset_pack_find_section(base,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,0x22010u+slot,&section)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_native_image_open(section.bytes,section.size,&image));
        int x=image.anchor_x+16,y=image.anchor_y+16;
        uint8_t *row=metadata+16u+slot*8u;
        unsigned values[4]={(unsigned)(x+80),(unsigned)y,(unsigned)x,(unsigned)y};
        for(unsigned i=0u;i<4u;++i){row[i*2u]=(uint8_t)values[i];row[i*2u+1u]=(uint8_t)(values[i]>>8);}
    }
    for(unsigned tab=0u;tab<5u;++tab){
        metadata[144u+tab*4u]=12u;metadata[146u+tab*4u]=(uint8_t)(48u+tab*24u);
    }
    sections[base->section_count]=(PortableAssetSectionView){
        .kind=PORTABLE_ASSET_SECTION_FRONTEND_MOTION,.content_id=1u,.version=2u,.bytes=metadata,.size=sizeof metadata};
    PortableAssetPackView pack=*base;pack.resolved_sections=sections;++pack.section_count;
    PortableFrontendFlow flow={0},before;PortableSoftwareFrame frame;
    PortableFrontendPresentationScratch scratch={0};
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;flow.character_cursor=1u;
    flow.motion_stage=1u;flow.motion_tick=0u;
    memcpy(unchanged,metadata,sizeof metadata);before=flow;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(memcmp(&flow,&before,sizeof flow)==0);memcpy(first_pixels,pixels,240u*160u*4u);
    flow.motion_tick=16u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==first_pixels[0]); /* Background stays anchored. */
    CHECK(pixels[8u*240u+128u]==first_pixels[8u*240u+128u]); /* Heading stays anchored. */
    CHECK(pixels[56u*240u+136u]!=first_pixels[56u*240u+136u]); /* Authored portrait moves independently. */
    CHECK(memcmp(metadata,unchanged,sizeof metadata)==0);
    flow.short_transitions=1u;flow.motion_tick=0u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    /* Short skips the entry transform, not just its outer fade timer. */
    uint32_t settled=pixels[56u*240u+136u];
    flow.motion_stage=0u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[56u*240u+136u]==settled);
    free(first_pixels);free(pixels);free(sections);return 0;
}

static int test_source_exit_brightness(const PortableAssetPackView *pack)
{
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame;
    PortableFrontendPresentationScratch scratch={0};PortableFrontendFlow flow={0};
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;flow.mode_phase=1u;
    flow.transition_from_mode_phase=1u;flow.pending_page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    flow.transition_out_ticks=66u;flow.transition_total_ticks=84u;
    /* Source stage16 begins at age33 with counter0. Counter2/4 give
     * brightness1/2 at ages35/37; a generic final32-turn fade is late. */
    flow.transition_ticks=84u-35u;
    CHECK(portable_frontend_presentation_render(pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==UINT32_C(0xff18294a)); /* fixture BG112244, brightness1 */
    flow.transition_ticks=84u-37u;
    CHECK(portable_frontend_presentation_render(pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==UINT32_C(0xff293952)); /* brightness2 */
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;flow.pending_page=PORTABLE_FRONTEND_PAGE_MODE;
    flow.transition_out_ticks=18u;flow.transition_total_ticks=132u;
    flow.transition_ticks=132u-2u;
    CHECK(portable_frontend_presentation_render(pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==UINT32_C(0xff08294a)); /* title state29 counter1 */
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_INTRO;flow.pending_page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;
    flow.transition_out_ticks=11u;flow.transition_total_ticks=28u;
    flow.transition_ticks=28u-1u;
    CHECK(portable_frontend_presentation_render(pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==UINT32_C(0xff08294a)); /* title state22 floor(16/10) */
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.pending_page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    flow.transition_out_ticks=1u;flow.transition_total_ticks=19u;flow.transition_ticks=19u;
    CHECK(portable_frontend_presentation_render(pack,&flow,&frame,&scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(pixels[0]==UINT32_C(0xff112244)); /* C23C cleanup never brightens */
    return 0;
}

static int test_native_image(void)
{
    uint8_t bytes[40]={0};
    PortableNativeImage image={0},before;
    memcpy(bytes,"MKSCIMG1",8u);
    bytes[8]=32u;bytes[10]=1u;bytes[12]=2u;bytes[14]=1u;
    bytes[16]=0xffu;bytes[17]=0xffu;bytes[24]=32u;bytes[28]=40u;
    bytes[32]=0x33u;bytes[33]=0x22u;bytes[34]=0x11u;bytes[35]=0xffu;
    CHECK(portable_native_image_open(bytes,sizeof bytes,&image));
    CHECK(image.anchor_x==-1 && image.width==2u && image.height==1u);
    CHECK(portable_native_image_pixel(&image,0u,0u)==UINT32_C(0xff112233));
    CHECK(portable_native_image_pixel(&image,1u,0u)==0u);
    CHECK(portable_native_image_pixel(&image,2u,0u)==0u);
    before=image;
    CHECK(!portable_native_image_open(bytes,39u,&image));
    CHECK(memcmp(&image,&before,sizeof image)==0);
    bytes[12]=0u; CHECK(!portable_native_image_open(bytes,sizeof bytes,&image));
    bytes[12]=2u;bytes[24]=33u; CHECK(!portable_native_image_open(bytes,sizeof bytes,&image));
    CHECK(!portable_native_image_open(NULL,sizeof bytes,&image));
    /* Equal fallback RGB colors still have distinct source palette identities. */
    uint8_t indexed[38]={0};
    memcpy(indexed,"MKSCIMG2",8u);indexed[8]=32;indexed[10]=2;
    indexed[12]=2;indexed[14]=1;indexed[20]=2;indexed[22]=16;
    indexed[24]=32;indexed[28]=38;indexed[32]=16;indexed[33]=17;
    indexed[34]=31;indexed[36]=31;
    CHECK(portable_native_image_open(indexed,sizeof indexed,&image));
    CHECK(portable_native_image_pixel(&image,0,0)==0xffff0000u);
    uint16_t palette[18]={0};palette[16]=0x03e0;palette[17]=0x7c00;
    CHECK(portable_native_image_pixel_palette(&image,0,0,palette,18)==0xff00ff00u);
    CHECK(portable_native_image_pixel_palette(&image,1,0,palette,18)==0xff0000ffu);
    CHECK(portable_native_image_pixel_palette(&image,1,0,palette,17)==0xffff0000u);
    uint8_t dirty[18]={0};dirty[16]=1;
    CHECK(portable_native_image_pixel_palette_masked(&image,0,0,palette,18,dirty)==0xff00ff00u);
    CHECK(portable_native_image_pixel_palette_masked(&image,1,0,palette,18,dirty)==0xffff0000u);
    CHECK(portable_native_image_pixel_palette_masked(&image,1,0,palette,18,NULL)==0xff0000ffu);
    before=image;indexed[32]=15;
    CHECK(!portable_native_image_open(indexed,sizeof indexed,&image));
    CHECK(memcmp(&image,&before,sizeof image)==0);
    indexed[32]=255;CHECK(portable_native_image_open(indexed,sizeof indexed,&image));
    CHECK(portable_native_image_pixel_palette(&image,0,0,palette,18)==0u);
    indexed[22]=127;CHECK(!portable_native_image_open(indexed,sizeof indexed,&image));
    return 0;
}

static int test_native_animation(void)
{
    uint8_t bytes[48]={0};
    PortableNativeAnimation animation={0},before;
    memcpy(bytes,"MKSCANI1",8u);
    bytes[8]=32u; bytes[10]=1u; bytes[12]=2u; bytes[14]=1u;
    bytes[16]=5u; bytes[20]=32u; bytes[24]=48u;
    bytes[32]=7u; bytes[36]=2u; bytes[40]=9u; bytes[44]=3u;
    CHECK(portable_native_animation_open(bytes,sizeof bytes,&animation));
    CHECK(portable_native_animation_frame(&animation,0u)==7u);
    CHECK(portable_native_animation_frame(&animation,1u)==7u);
    CHECK(portable_native_animation_frame(&animation,2u)==9u);
    CHECK(portable_native_animation_frame(&animation,4u)==9u);
    CHECK(portable_native_animation_frame(&animation,5u)==7u);
    bytes[14]=0u;
    CHECK(portable_native_animation_open(bytes,sizeof bytes,&animation));
    CHECK(portable_native_animation_frame(&animation,UINT32_MAX)==9u);
    before=animation;
    CHECK(!portable_native_animation_open(bytes,sizeof bytes-1u,&animation));
    CHECK(memcmp(&before,&animation,sizeof before)==0);
    bytes[36]=0u;
    CHECK(!portable_native_animation_open(bytes,sizeof bytes,&animation));
    memset(bytes+36u,0xff,4u);
    CHECK(!portable_native_animation_open(bytes,sizeof bytes,&animation));
    bytes[36]=2u;bytes[37]=bytes[38]=bytes[39]=0u;bytes[20]=33u;
    CHECK(!portable_native_animation_open(bytes,sizeof bytes,&animation));
    CHECK(!portable_native_animation_open(NULL,48u,&animation));
    CHECK(portable_native_animation_frame(NULL,0u)==0u);
    return 0;
}

static int test_pause(const PortableAssetPackView *pack, const char *directory)
{
    uint32_t pixels[240u*160u], checksums[5];
    PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    for(unsigned tick=0;tick<=12;++tick) {
        for(unsigned i=0;i<240u*160u;++i) pixels[i]=UINT32_C(0xff335577);
        CHECK(portable_pause_menu_render(pack,0u,(uint8_t)tick,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        unsigned expansion=tick>4u?tick-4u:0u;if(expansion>8u)expansion=8u;
        unsigned hw=5u*expansion/8u*8u,hh=6u*expansion/8u*8u;
        uint32_t backdrop=UINT32_C(0xff335577);
        if(expansion){
            backdrop=UINT32_C(0xff000000);
            for(unsigned channel=0u;channel<3u;++channel){
                unsigned value=6u+4u*channel;
                value=value>expansion?value-expansion:0u;
                backdrop|=((value<<3u)|(value>>2u))<<(16u-channel*8u);
            }
        }
        for(unsigned y=0;y<160;++y) for(unsigned x=0;x<240;++x)
            if(x<120u-hw || x>=120u+hw || y<80u-hh || y>=80u+hh)
                CHECK(pixels[y*240u+x]==backdrop);
        if(tick==4u)CHECK(pixels[0]==UINT32_C(0xff335577));
        if(tick==8u)CHECK(pixels[0]==UINT32_C(0xff103152));
        if(tick==12u)CHECK(pixels[0]==UINT32_C(0xff001031));
        if(directory) {
            char output[1024];
            CHECK(snprintf(output,sizeof output,"%s/pause-open-%u.ppm",directory,tick)>0);
            CHECK(portable_software_frame_write_ppm(&frame,output));
        }
        if(tick<12u){
            const uint32_t checksum=portable_software_frame_checksum(&frame);
            for(unsigned i=0u;i<240u*160u;++i)pixels[i]=UINT32_C(0xff335577);
            CHECK(portable_pause_menu_render(pack,1u,(uint8_t)tick,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(portable_software_frame_checksum(&frame)==checksum); /* labels/cursor not installed */
        }
    }
    {
        uint32_t *physical=malloc(973u*640u*sizeof *physical);CHECK(physical!=NULL);
        for(unsigned y=0u;y<640u;++y)for(unsigned x=0u;x<973u;++x)
            physical[y*973u+x]=x>=960u?0xdeadbeefu:(x&1u?0xff8ca5c6u:0xff84a5c6u);
        PortableSoftwareFrame scaled={physical,240u,160u,NULL,4u,973u,0u,NULL};
        CHECK(portable_pause_menu_render(pack,0u,5u,&scaled)==PORTABLE_FRONTEND_PRESENTATION_OK);
        for(unsigned y=0u;y<640u;++y)for(unsigned x=0u;x<973u;++x)
            CHECK(physical[y*973u+x]==(x>=960u?0xdeadbeefu:(x&1u?0xff849cbdu:0xff7b9cbdu)));
        free(physical);
    }
    for(unsigned row=0;row<5;++row) {
        for(unsigned i=0;i<240u*160u;++i) pixels[i]=UINT32_C(0xff335577);
        CHECK(portable_pause_menu_render(pack,(uint8_t)row,12u,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        checksums[row]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0;prior<row;++prior) CHECK(checksums[row]!=checksums[prior]);
        for(unsigned i=0;i<240u*160u;++i)pixels[i]=UINT32_C(0xff335577);
        CHECK(portable_pause_menu_render(pack,(uint8_t)row,255u,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(portable_software_frame_checksum(&frame)==checksums[row]);
        if(directory) {
            char output[1024];
            CHECK(snprintf(output,sizeof output,"%s/pause-cursor-%u.ppm",directory,row)>0);
            CHECK(portable_software_frame_write_ppm(&frame,output));
        }
    }
    CHECK(portable_pause_menu_render(pack,5u,12u,&frame)==PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    CHECK(portable_pause_menu_render(NULL,0u,12u,&frame)==PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    PortableAssetPackView empty={0};
    CHECK(portable_pause_menu_render(&empty,0u,12u,&frame)==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION);
    return 0;
}

static int test_imported_mod_menu(const char *path)
{
    size_t size;
    uint8_t *bytes=read_file(path,&size);
    PortableAssetPackView pack;
    CHECK(bytes!=NULL&&portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK);
    uint8_t *saved=malloc(size);CHECK(saved!=NULL);memcpy(saved,bytes,size);
    const unsigned scales[]={1u,3u,4u},widths[]={284u,373u};
    const uint32_t sentinel=UINT32_C(0x12345678);
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU,.players=1u};
    for(unsigned si=0u;si<3u;++si)for(unsigned wi=0u;wi<2u;++wi){
        const size_t scale=scales[si],width=widths[wi],height=160u*scale;
        const size_t stride=width*scale+7u,native_stride=240u*scale+5u;
        uint32_t *pixels=malloc(stride*height*sizeof *pixels);
        uint32_t *native=malloc(native_stride*height*sizeof *native);
        uint32_t *first=malloc(stride*height*sizeof *first);
        CHECK(pixels!=NULL&&native!=NULL&&first!=NULL);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,
            .scale=(uint8_t)scale,.stride=stride};
        PortableSoftwareFrame center={.pixels=native,.width=240u,.height=160u,
            .scale=(uint8_t)scale,.stride=native_stride};
        size_t changed=0u;
        for(unsigned sample=0u;sample<2u;++sample){
            for(size_t i=0u;i<stride*height;++i)pixels[i]=sentinel;
            for(size_t i=0u;i<native_stride*height;++i)native[i]=sentinel;
            flow.tick=sample?80u:17u;
            const PortableFrontendFlow before=flow;
            CHECK(portable_mod_menu_render(&pack,&flow,NULL,0u,0,0,NULL,0u,0,&center)==
                  PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(portable_mod_menu_render(&pack,&flow,NULL,0u,0,0,NULL,0u,0,&frame)==
                  PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(memcmp(&flow,&before,sizeof flow)==0);
            const PortableSoftwareFrame view=portable_software_frame_native_view(&frame,PORTABLE_FRAME_CENTER);
            const size_t left=(size_t)(view.pixels-pixels);
            size_t left_colored=0u,right_colored=0u;
            for(size_t y=0u;y<height;++y){
                for(size_t x=0u;x<240u*scale;++x)
                    CHECK(view.pixels[y*stride+x]==native[y*native_stride+x]);
                for(size_t x=0u;x<width*scale;++x){
                    CHECK(pixels[y*stride+x]!=sentinel);
                    if(x<left||x>=left+240u*scale){
                        if((pixels[y*stride+x]&UINT32_C(0x00ffffff))!=0u){
                            if(x<left)++left_colored;else ++right_colored;
                        }
                        if(sample&&pixels[y*stride+x]!=first[y*stride+x])++changed;
                    }
                }
                for(size_t x=width*scale;x<stride;++x)CHECK(pixels[y*stride+x]==sentinel);
                for(size_t x=240u*scale;x<native_stride;++x)CHECK(native[y*native_stride+x]==sentinel);
            }
            CHECK(left_colored!=0u&&right_colored!=0u);
            if(!sample)memcpy(first,pixels,stride*height*sizeof *pixels);
        }
        CHECK(changed!=0u);
        free(first);free(native);free(pixels);
    }
    CHECK(memcmp(bytes,saved,size)==0);free(saved);free(bytes);
    puts("imported mod menu: PASS centered native pixels, animated wings, 1x/3x/4x padded stride, immutable pack");
    return 0;
}

static int test_imported_banner(const char *path)
{
    size_t size;
    uint8_t *bytes=read_file(path,&size);
    PortableAssetPackView opened;
    CHECK(bytes!=NULL&&portable_asset_pack_open(bytes,size,&opened)==PORTABLE_ASSET_PACK_OK);
    const PortableAssetPackView *pack=&opened;
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    {
        PortableFrontendMotion motion;
        int32_t sine[1024];
        CHECK(portable_frontend_motion_load(pack,&motion));
        for(unsigned i=0u;i<1024u;++i)sine[i]=portable_menu_sine(&motion,i);
        /* Original title-to-mode age64 has twelve banner callbacks: its
         * anchored cell is x98, then overshoots at16 and returns at24. */
        CHECK(portable_menu_banner_x(0u,sine)==272);
        CHECK(portable_menu_banner_x(12u,sine)==98);
        CHECK(portable_menu_banner_x(16u,sine)==41);
        CHECK(portable_menu_banner_x(24u,sine)==47);
        CHECK(portable_menu_banner_x(32u,sine)==32);
        PortableFrontendFlow banner={.page=PORTABLE_FRONTEND_PAGE_MODE,.players=1u,
            .heading_configured=1u};
        memcpy(banner.heading_sine,sine,sizeof sine);
        uint32_t *settled=calloc(240u*160u,sizeof *settled);CHECK(settled!=NULL);
        PortableSoftwareFrame fixed;
        CHECK(portable_software_frame_init(&fixed,settled,240u,160u));
        CHECK(portable_menu_chrome_render(pack,&banner,banner.page,&fixed)==PORTABLE_FRONTEND_PRESENTATION_OK);
        memset(pixels,0,sizeof pixels);
        banner.motion_stage=1u;banner.motion_player_entry=1u;banner.motion_tick=45u;
        CHECK(portable_menu_chrome_render(pack,&banner,banner.page,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        /* Accepted input is included in the capture's age: title age64
         * reaches incoming motion45 and the twelfth banner callback. */
        unsigned opaque=0u;
        for(unsigned y=132u;y<160u;++y)for(unsigned x=0u;x<240u;++x){
            const uint32_t expected=x>=66u?settled[y*240u+x-66u]:0u;
            CHECK(pixels[y*240u+x]==expected);opaque+=(expected!=0u);
        }
        CHECK(opaque!=0u);free(settled);
    }
    free(bytes);
    puts("Imported menu banner: PASS source coordinates, overshoot and accepted-tick pixels");
    return 0;
}

static int test_rom_title(const char *path,const char *output_directory)
{
    size_t size;
    uint8_t *bytes=read_file(path,&size);
    PortableAssetPackView pack;
    PortableFrontendFlow flow={0},before;
    PortableFrontendPresentationScratch *scratch=calloc(1u,sizeof *scratch);
    uint32_t pixels[240u*160u],checksums[9];
    PortableSoftwareFrame frame;
    char output[1024];
    CHECK(bytes!=NULL && scratch!=NULL);
    CHECK(portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;
    for(unsigned phase=0u;phase<5u;++phase) {
        flow.tick=phase*9u;before=flow;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
        checksums[phase]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<phase;++prior) CHECK(checksums[phase]!=checksums[prior]);
    }
    /* Sunset and night palettes; night stars change on a flag-phase-0 frame. */
    flow.tick=0u;flow.title_variant=1u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    checksums[5]=portable_software_frame_checksum(&frame);
    flow.title_variant=2u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    checksums[6]=portable_software_frame_checksum(&frame);
    flow.tick=45u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    checksums[7]=portable_software_frame_checksum(&frame);
    CHECK(checksums[5]!=checksums[0]&&checksums[6]!=checksums[0]&&checksums[6]!=checksums[5]&&
          checksums[7]!=checksums[6]);
    flow.title_variant=0u;
    flow.tick=0u;
    for(unsigned cursor=0u;cursor<4u;++cursor) {
        flow.title_cursor=(uint8_t)cursor;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        checksums[cursor]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<cursor;++prior) CHECK(checksums[cursor]!=checksums[prior]);
        CHECK(snprintf(output,sizeof output,"%s/title-%u.ppm",output_directory,cursor)>0);
        CHECK(portable_software_frame_write_ppm(&frame,output));
    }
    for(unsigned page=0u;page<3u;++page) {
        flow.page=page==2u ? PORTABLE_FRONTEND_PAGE_TIME_TRIAL : PORTABLE_FRONTEND_PAGE_MODE;
        flow.mode_phase=(uint8_t)(page==1u);
        for(unsigned cursor=0u;cursor<(page==2u ? 2u : page==0u ? 4u : 3u);++cursor) {
            flow.mode_cursor=flow.race_speed=flow.time_trial_cursor=(uint8_t)cursor;
            before=flow;
            CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(memcmp(&flow,&before,sizeof flow)==0);
            checksums[cursor]=portable_software_frame_checksum(&frame);
            for(unsigned prior=0u;prior<cursor;++prior) CHECK(checksums[cursor]!=checksums[prior]);
            CHECK(snprintf(output,sizeof output,"%s/menu-%u-%u.ppm",output_directory,page,cursor)>0);
            CHECK(portable_software_frame_write_ppm(&frame,output));
        }
    }
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;flow.mode_phase=0u;flow.mode_cursor=2u;
    flow.quick_options_open=1u;
    for(unsigned row=0u;row<3u;++row) {
        flow.quick_options_cursor=(uint8_t)row;
        before=flow;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
        CHECK(pixels[44u*240u+44u]==UINT32_C(0xff203040));
        checksums[row]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<row;++prior) CHECK(checksums[row]!=checksums[prior]);
        CHECK(snprintf(output,sizeof output,"%s/quick-options-%u.ppm",output_directory,row)>0);
        CHECK(portable_software_frame_write_ppm(&frame,output));
    }
    /* Time Trial's two GHOST DISPLAY rows; brackets follow the values. */
    flow.mode_cursor=1u;flow.quick_options_cursor=0u;
    for(unsigned bits=0u;bits<4u;++bits) {
        flow.ghost_display_off=(uint8_t)bits;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[52u*240u+44u]==UINT32_C(0xff102030));
        checksums[bits]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<bits;++prior) CHECK(checksums[bits]!=checksums[prior]);
    }
    flow.ghost_display_off=0u;flow.quick_options_open=0u;
    /* The SETTINGS hint marks the two rows that open a window. */
    for(unsigned cursor=0u;cursor<3u;++cursor) {
        flow.mode_cursor=(uint8_t)cursor;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK((pixels[120u*240u+20u]==UINT32_C(0xffe02040))==(cursor!=0u));
    }
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    for(unsigned cursor=0u;cursor<9u;++cursor) {
        flow.character_cursor=(uint8_t)cursor;
        flow.character_locked=0u;flow.page_ticks=flow.tick=0u;
        before=flow;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
        checksums[cursor]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<cursor;++prior) CHECK(checksums[cursor]!=checksums[prior]);
        CHECK(snprintf(output,sizeof output,"%s/character-%u.ppm",output_directory,cursor)>0);
        CHECK(portable_software_frame_write_ppm(&frame,output));
        for(unsigned tick=0u;tick<256u;++tick) {
            flow.page_ticks=flow.tick=tick;before=flow;
            CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(memcmp(&flow,&before,sizeof flow)==0);
        }
        flow.character_locked=1u;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(snprintf(output,sizeof output,"%s/character-locked-%u.ppm",output_directory,cursor)>0);
        CHECK(portable_software_frame_write_ppm(&frame,output));
    }
    flow.mode_cursor=1u;
    flow.extra_cup_access[2]=31u;
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;
    for(unsigned phase=0u;phase<3u;++phase) {
        flow.course_phase=(uint8_t)phase;
        for(unsigned selection=0u;selection<40u;++selection) {
            flow.course_cup=(uint8_t)(selection/4u);
            flow.course_track=(uint8_t)(selection%4u);
            before=flow;
            CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            CHECK(memcmp(&flow,&before,sizeof flow)==0);
            CHECK(snprintf(output,sizeof output,"%s/course-%u-%u.ppm",output_directory,phase,selection)>0);
            CHECK(portable_software_frame_write_ppm(&frame,output));
        }
    }
    CHECK(test_pause(&pack,output_directory)==0);
    free(scratch);free(bytes);
    puts("native ROM menus: PASS title=3 flag_phases=5 mode=4 class=3 trial=2 characters=9 character_ticks=256 locked=9 courses=40 course_phases=3 pause_choices=5 pause_open_ticks=12 immutable=1");
    return 0;
}

int main(int argc,char **argv)
{
    if(argc==3&&strcmp(argv[1],"--imported-mod-menu")==0)return test_imported_mod_menu(argv[2]);
    if(argc==3&&strcmp(argv[1],"--imported-banner")==0)return test_imported_banner(argv[2]);
    if(test_indexed_popup_palette()!=0||test_heading_owner()!=0||test_common_heading_anchor()!=0||test_cup_panel_and_confirmation_pixels()!=0||test_menu_background_layer_order()!=0||test_extended_title_layers()!=0||test_title_cursor_pixels()!=0||test_source_menu_motion()!=0||test_native_image()!=0 || test_native_animation()!=0)return 1;
    if(argc==3)return test_rom_title(argv[1],argv[2]);
    size_t size;uint8_t *bytes,*original;PortableAssetPackView pack;
    PortableFrontendFlow flow={0};
    PortableFrontendFlow before;
    PortableFrontendPresentationScratch *scratch=calloc(1u,sizeof *scratch);
    uint32_t pixels[240u*160u];PortableSoftwareFrame frame;
    uint32_t first,second,transition,title_checksums[3],character_checksums[9];
    uint32_t gp_class_checksums[3],course_checksums[40];
    uint32_t animation_checksums[16u];
    CHECK(argc==2&&scratch!=NULL);
    bytes=read_file(argv[1],&size);CHECK(bytes!=NULL);
    original=malloc(size);CHECK(original!=NULL);
    memcpy(original,bytes,size);
    CHECK(portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_software_frame_init(&frame,pixels,240u,160u));
    CHECK(test_independent_menu_layers(&pack)==0);
    CHECK(test_menu_ambient_pages(&pack)==0);
    CHECK(test_extended_menu_pages(&pack)==0);
    CHECK(test_source_exit_brightness(&pack)==0);
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;
    for(unsigned selection=0u;selection<3u;++selection)
    {
        flow.title_cursor=(uint8_t)selection;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK);
        title_checksums[selection]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<selection;++prior)
            CHECK(title_checksums[selection]!=title_checksums[prior]);
    }
    /* Packs without the sunset/night art keep the normal title. */
    flow.title_variant=2u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK&&
          portable_software_frame_checksum(&frame)==title_checksums[2]);
    flow.title_variant=0u;
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;flow.mode_cursor=0u;
    before=flow;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(memcmp(&flow,&before,sizeof flow)==0);
    first=portable_software_frame_checksum(&frame);
    flow.mode_cursor=1u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    second=portable_software_frame_checksum(&frame);
    CHECK(first!=0u&&second!=0u&&first!=second);
    flow.mode_cursor=0u;flow.mode_phase=1u;
    for(unsigned selection=0u;selection<3u;++selection)
    {
        flow.race_speed=(uint8_t)selection;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK);
        gp_class_checksums[selection]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<selection;++prior)
            CHECK(gp_class_checksums[selection]!=gp_class_checksums[prior]);
    }
    flow.mode_phase=0u;flow.mode_cursor=1u;
    /* A long title reverse-transition still uses the renderer's 0..16 blend
     * scale. At progress tick one the scaled amount is zero, so the source
     * mode frame must remain byte-identical instead of overflowing colors. */
    flow.pending_page=PORTABLE_FRONTEND_PAGE_TITLE_INTRO;
    flow.transition_out_ticks=33u;
    flow.transition_total_ticks=PORTABLE_FRONTEND_TITLE_MODE_TICKS;
    flow.transition_ticks=PORTABLE_FRONTEND_TITLE_MODE_TICKS-1u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_software_frame_checksum(&frame)==second);
    flow.pending_page=0;flow.transition_total_ticks=0u;flow.transition_ticks=0u;
    flow.mode_cursor=3u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    flow.mode_cursor=4u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    flow.mode_cursor=1u;
    flow.extra_cup_access[2]=31u;
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;
    for(unsigned selection=0u;selection<40u;++selection)
    {
        flow.course_cup=(uint8_t)(selection/4u);
        flow.course_track=(uint8_t)(selection%4u);
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK);
        course_checksums[selection]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<selection;++prior)
            CHECK(course_checksums[selection]!=course_checksums[prior]);
    }
    flow.course_cup=10u;flow.course_track=0u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    {
        /* Custom cups without art fall back to stock cup art, a blank map
         * and course-ID labels instead of failing. */
        static const PortableFrontendCourseSlot custom[4]={{128u,11u,0u,1u},
            {129u,11u,1u,1u},{130u,11u,2u,1u},{131u,11u,3u,1u}};
        const PortableFrontendConfig saved=flow.config;
        flow.config.courses=custom;flow.config.course_count=4u;
        flow.course_cup=PORTABLE_FRONTEND_CUSTOM_CUP;flow.course_track=1u;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK);
        const uint32_t custom_checksum=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<40u;++prior)CHECK(custom_checksum!=course_checksums[prior]);
        flow.course_track=2u;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK&&portable_software_frame_checksum(&frame)!=custom_checksum);
        flow.course_cup=PORTABLE_FRONTEND_CUSTOM_CUP+4u;flow.course_track=0u;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK);
        flow.course_cup=PORTABLE_FRONTEND_COURSE_CUPS;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
        flow.config=saved;
    }
    {
        /* The records list scrolls eight of its rows and has no chrome. */
        PortableFrontendCourseSlot listed[12];
        for(unsigned index=0u;index<12u;++index)
            listed[index]=(PortableFrontendCourseSlot){24u+index,(uint8_t)(index/4u),(uint8_t)(index%4u),1u};
        PortableFrontendFlow records=flow;
        records.config.courses=listed;records.config.course_count=12u;
        records.page=PORTABLE_FRONTEND_PAGE_RECORDS;records.course_phase=2u;records.mode_cursor=1u;
        records.extra_cup_access[2]=0u;records.course_cup=0u;records.course_track=0u;records.records_top=0u;
        uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
        CHECK(portable_frontend_flow_records_list(&records,entries)>=8u);
        CHECK(portable_menu_chrome_tabs(&records,PORTABLE_FRONTEND_PAGE_RECORDS)==0u);
        uint32_t list[3];
        CHECK(portable_frontend_presentation_render(&pack,&records,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        list[0]=portable_software_frame_checksum(&frame);
        records.course_track=1u;
        CHECK(portable_frontend_presentation_render(&pack,&records,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        list[1]=portable_software_frame_checksum(&frame);
        records.records_top=1u;
        CHECK(portable_frontend_presentation_render(&pack,&records,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        list[2]=portable_software_frame_checksum(&frame);
        CHECK(list[0]!=list[1]&&list[1]!=list[2]&&list[0]!=list[2]);
    }
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    flow.pending_page=0;flow.transition_total_ticks=0u;flow.transition_ticks=0u;
    flow.character_locked=0u;flow.page_ticks=0u;
    for(unsigned selection=0u;selection<9u;++selection)
    {
        flow.character_cursor=(uint8_t)selection;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
              PORTABLE_FRONTEND_PRESENTATION_OK);
        character_checksums[selection]=portable_software_frame_checksum(&frame);
        for(unsigned prior=0u;prior<selection;++prior)
            CHECK(character_checksums[selection]!=character_checksums[prior]);
    }
    for(unsigned selection=0u;selection<9u;++selection)
    {
        unsigned distinct_animation_frames=0u;
        flow.character_cursor=(uint8_t)selection;
        for(unsigned phase=0u;
            phase<16u;++phase)
        {
            int seen=0;
            flow.tick=phase*4u;
            flow.page_ticks=flow.tick;
            CHECK(portable_frontend_presentation_render(
                      &pack,&flow,&frame,scratch)==
                  PORTABLE_FRONTEND_PRESENTATION_OK);
            animation_checksums[phase]=
                portable_software_frame_checksum(&frame);
            for(unsigned prior=0u;prior<phase;++prior)
                if(animation_checksums[phase]==animation_checksums[prior])
                    seen=1;
            if(!seen)++distinct_animation_frames;
        }
        CHECK(distinct_animation_frames>=4u);
    }
    flow.character_cursor=0u;flow.character_locked=1u;flow.selected_character=1u;
    flow.tick=105u;flow.event_count=2u;
    flow.character_selection_tick=90u;flow.character_lock_tick=100u;
    flow.character_animation_timing=3u;
    flow.events[0]=(PortableFrontendEvent){90u,0u,
        PORTABLE_FRONTEND_EVENT_MOVE,PORTABLE_FRONTEND_PAGE_CHARACTER};
    flow.events[1]=(PortableFrontendEvent){100u,1u,
        PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK,PORTABLE_FRONTEND_PAGE_CHARACTER};
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_software_frame_checksum(&frame)!=character_checksums[0]);
    const uint32_t locked_before_consume=portable_software_frame_checksum(&frame);
    portable_frontend_flow_consume_events(&flow);
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_software_frame_checksum(&frame)==locked_before_consume);
    flow.event_count=0u;flow.character_locked=0u;flow.selected_character=0u;
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;
    flow.mode_cursor=1u;flow.pending_page=PORTABLE_FRONTEND_PAGE_TIME_TRIAL;
    flow.time_trial_cursor=0u;
    flow.transition_out_ticks=33u;
    flow.transition_total_ticks=PORTABLE_FRONTEND_MODE_TRIAL_TICKS;
    flow.transition_ticks=PORTABLE_FRONTEND_MODE_TRIAL_TICKS-flow.transition_out_ticks-10u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    transition=portable_software_frame_checksum(&frame);
    CHECK(transition!=first&&transition!=second);
    flow.page=PORTABLE_FRONTEND_PAGE_MODE;
    flow.mode_cursor=0u;flow.mode_phase=1u;flow.race_speed=0u;
    flow.pending_page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    flow.transition_out_ticks=66u;
    flow.transition_total_ticks=PORTABLE_FRONTEND_GP_CHARACTER_TICKS;
    flow.transition_ticks=PORTABLE_FRONTEND_GP_CHARACTER_TICKS/2u;
    CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_OK);
    CHECK(portable_software_frame_checksum(&frame)!=gp_class_checksums[0]);
    /* Old packs settle missing motion metadata, but still switch independent
     * scenes rather than copying two translated full-page screenshots. */
    uint32_t *source_pixels=malloc(sizeof pixels),*target_pixels=malloc(sizeof pixels);
    CHECK(source_pixels!=NULL&&target_pixels!=NULL);
    for(unsigned reverse=0u;reverse<2u;++reverse){
        flow.page=reverse?PORTABLE_FRONTEND_PAGE_COURSE:PORTABLE_FRONTEND_PAGE_CHARACTER;
        flow.pending_page=reverse?PORTABLE_FRONTEND_PAGE_CHARACTER:PORTABLE_FRONTEND_PAGE_COURSE;
        flow.course_cup=0u;flow.course_track=0u;flow.course_phase=0u;
        flow.transition_ticks=0u;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        memcpy(source_pixels,pixels,sizeof pixels);
        PortableFrontendFlow target_flow=flow;target_flow.page=flow.pending_page;
        CHECK(portable_frontend_presentation_render(&pack,&target_flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        memcpy(target_pixels,pixels,sizeof pixels);
        flow.transition_out_ticks=reverse?50u:100u;
        flow.transition_total_ticks=flow.transition_out_ticks+18u;
        flow.transition_ticks=flow.transition_total_ticks;
        before=flow;
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
        /* The entering idle helper may hide the SETTINGS hint while input
         * locks, but the background and top-left heading never translate. */
        CHECK(pixels[0]==source_pixels[0]);
        CHECK(pixels[6u*240u+3u]==source_pixels[6u*240u+3u]);
        flow.transition_ticks=2u; /* Incoming frame16 is fully settled. */
        CHECK(portable_frontend_presentation_render(&pack,&flow,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[0]==target_pixels[0]);
        CHECK(pixels[6u*240u+3u]==target_pixels[6u*240u+3u]);
        CHECK(pixels[144u*240u+32u]==target_pixels[144u*240u+32u]);
    }
    free(source_pixels);free(target_pixels);
    {
        /* Local multiplayer uses ROM art only: N-P tab in the title link
         * frame, the VS/Battle link medallions, per-player cursors/badges,
         * and the choosing player's banner. */
        PortableFrontendFlow local=flow;
        uint32_t counts[3],turns[3];
        local.transition_ticks=0u;local.pending_page=0;local.page_ticks=40u;
        local.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;local.title_cursor=1u;
        local.player_count_open=1u;local.max_players=4u;
        for(unsigned players=2u;players<=4u;++players){
            local.players=(uint8_t)players;
            CHECK(portable_frontend_presentation_render(&pack,&local,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            counts[players-2u]=portable_software_frame_checksum(&frame);
            for(unsigned prior=2u;prior<players;++prior)CHECK(counts[players-2u]!=counts[prior-2u]);
        }
        local.player_count_open=0u;local.players=3u;
        local.page=PORTABLE_FRONTEND_PAGE_MODE;local.mode_phase=0u;local.mode_cursor=2u;
        CHECK(portable_frontend_presentation_render(&pack,&local,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        const uint32_t vs=portable_software_frame_checksum(&frame);
        local.mode_cursor=3u;
        CHECK(portable_frontend_presentation_render(&pack,&local,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(portable_software_frame_checksum(&frame)!=vs);
        CHECK(portable_menu_chrome_tabs(&local,PORTABLE_FRONTEND_PAGE_MODE)==1u);
        local.page=PORTABLE_FRONTEND_PAGE_CHARACTER;local.mode_cursor=2u;local.character_cursor=4u;
        local.character_locked=0u;local.character_page=0u;
        PortableFrontendDriverSlot stock[9];
        for(unsigned slot=0u;slot<9u;++slot)stock[slot]=(PortableFrontendDriverSlot){slot==4u?0u:slot+1u,1u,0u,0u,0u,0u,0u};
        local.config.drivers=stock;local.config.driver_count=9u;
        local.player_characters[0]=1u;local.player_characters[1]=9u;
        CHECK(portable_menu_chrome_tabs(&local,PORTABLE_FRONTEND_PAGE_CHARACTER)==3u);
        for(unsigned turn=0u;turn<3u;++turn){
            local.character_player=(uint8_t)turn;
            CHECK(portable_frontend_presentation_render(&pack,&local,&frame,scratch)==PORTABLE_FRONTEND_PRESENTATION_OK);
            turns[turn]=portable_software_frame_checksum(&frame);
            for(unsigned prior=0u;prior<turn;++prior)CHECK(turns[turn]!=turns[prior]);
        }
        local.page=PORTABLE_FRONTEND_PAGE_COURSE;local.course_phase=0u;local.course_cup=0u;
        CHECK(portable_menu_chrome_tabs(&local,PORTABLE_FRONTEND_PAGE_COURSE)==4u);
        local.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;
        CHECK(portable_menu_chrome_tabs(&local,PORTABLE_FRONTEND_PAGE_TITLE_MENU)==0u);
    }
    CHECK(portable_frontend_presentation_render(NULL,&flow,&frame,scratch)==
          PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT);
    CHECK(test_pause(&pack,NULL)==0);
    CHECK(memcmp(bytes,original,size)==0);
    free(original);free(scratch);free(bytes);
    puts("portable frontend presentation: PASS");return 0;
}
