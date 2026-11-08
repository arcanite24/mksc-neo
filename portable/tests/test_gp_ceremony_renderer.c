#include "gp_ceremony_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr,"GP ceremony renderer failed line %d: %s\n",__LINE__,#value);return 1; } } while (0)
static void w32(uint8_t *p,uint32_t v)
{
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static void w16(uint8_t *p,uint16_t value)
{p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);}
static int source_asset(const uint8_t *legacy,size_t legacy_size)
{
    /* Invented one-image art and one-frame schedules exercise the bounded
     * GPC4 representation; no source graphics or coefficient tables. */
    const size_t meta_size=917u,total=legacy_size+meta_size+40u;
    uint8_t *bytes=calloc(total,1u);CHECK(bytes);memcpy(bytes,legacy,legacy_size);w32(bytes,0x34435047u);
    uint8_t *p=bytes+legacy_size;w32(p,0x31524543u);w32(p+4u,150u);w32(p+8u,1u);w32(p+12u,1u);
    size_t at=16u;
    for(unsigned i=0;i<115u+8u+48u;++i){w16(p+at,43u);at+=2u;}
    for(unsigned i=0;i<4u;++i){w16(p+at,i==0u?0u:43u);at+=2u;}
    p[at]=3u;p[at+1u]=5u;p[at+2u]=2u;at+=4u;
    const size_t tracks_at=at;
    for(unsigned i=0;i<8u;++i){w16(p+at+8u,1u);w16(p+at+12u,5u);at+=16u;}
    w16(p+at,43u);w16(p+at+2u,5u);at+=4u;
    for(unsigned i=0;i<3u;++i){w16(p+at,43u);at+=2u;}at+=2u;
    for(unsigned i=0;i<24u;++i){w16(p+at,43u);at+=2u;}
    at+=36u;w16(p+at,43u);w16(p+at+2u,43u);at+=4u;
    at+=92u;
    for(unsigned i=0;i<25u;++i){w16(p+at,43u);at+=2u;}
    for(unsigned i=0;i<24u;++i){w16(p+at,43u);at+=2u;}
    at+=128u;
    ++at;CHECK(at==meta_size);memcpy(p+at,legacy+32u,40u);
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_GP_CEREMONY_GRAPHICS,
        .content_id=1u,.version=4u,.bytes=bytes,.size=total};
    PortableGpCeremonyAssetView asset;
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_OK);
    CHECK(asset.source_presentation&&asset.stage_content_id==150u&&asset.image_count==44u&&asset.motion.frames[0].duration==5u);
    w16(p+tracks_at+8u,0u);
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION);
    w16(p+tracks_at+8u,1u);w16(p+16u,44u);
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION);
    w16(p+16u,43u);section.size--;
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION);
    free(bytes);return 0;
}
static int source_summary_composition(void)
{
    static uint8_t map[64],road_tiles[128],sky_tiles[32],sky_map[2048];
    static uint8_t panel[240u*160u*4u],unlock[120u*8u*4u];
    static uint32_t pixels[240u*160u];
    static int32_t sine[1024];sine[256]=32768;
    memset(map,1,sizeof map);memset(road_tiles+64u,1,64u);memset(sky_tiles,0x11,sizeof sky_tiles);
    const uint16_t colors[2]={0u,0x001fu};
    const PortableSkyAssetView sky={.tiles=sky_tiles,.tile_bytes=sizeof sky_tiles,.palette=colors,.palette_count=2u,
        .front_map=sky_map,.front_map_bytes=sizeof sky_map,.front_width_tiles=32u,
        .mid_map=sky_map,.mid_map_bytes=sizeof sky_map,.mid_width_tiles=32u,
        .back_map=sky_map,.back_map_bytes=sizeof sky_map,.back_width_tiles=32u,.map_height_tiles=32u};
    const PortableRaceSceneView scene={.world_map={.map=map,.map_bytes=sizeof map,.width_tiles=8u,.height_tiles=8u},
        .tileset={.tiles=road_tiles,.tile_bytes=sizeof road_tiles,.palette=colors,.palette_count=2u},
        .sky=&sky,.sine=sine,.sine_count=1024u};
    const PortableRacePresentationState state={.suppress_player=1u,.suppress_racers=1u,.suppress_native_hud=1u,
        .suppress_native_minimap=1u};
    const PortableGpGridRow grid[8]={{0}};
    const PortableGpCeremonyWorldView world={&scene,&state,grid,8u};
    const uint32_t green=0xff00ff00u,blue=0xff0000ffu;
    uint8_t transparent[4]={0},green_bytes[4],blue_bytes[4];
    w32(green_bytes,green);w32(blue_bytes,blue);
    PortableGpCeremonyAssetView asset={.source_presentation=1u,.image_count=5u};
    asset.images[0]=(PortableNativeImage){.pixels=transparent,.width=1u,.height=1u};
    /* Native image payloads are little-endian bytes; frame pixels below are
     * host integers. Do not alias host color words as serialized image art. */
    w32(panel+(72u*240u+4u)*4u,0xffabcdefu);
    w32(panel+(72u*240u+116u)*4u,0xffabcdefu);
    w32(unlock,green);
    asset.images[1]=(PortableNativeImage){.pixels=panel,.width=240u,.height=160u};
    asset.images[2]=(PortableNativeImage){.pixels=unlock,.width=120u,.height=8u};
    asset.images[3]=(PortableNativeImage){.pixels=green_bytes,.width=1u,.height=1u};
    asset.images[4]=(PortableNativeImage){.pixels=blue_bytes,.width=1u,.height=1u};
    CHECK(portable_native_image_pixel(&asset.images[1],4u,72u)==0xffabcdefu);
    CHECK(portable_native_image_pixel(&asset.images[3],0u,0u)==green);
    asset.panels[1]=1u;asset.unlock_image=2u;asset.glyphs[7]=3u;
    for(unsigned i=0;i<10u;++i)asset.coin_digits[i]=4u;
    asset.motion.frame_count=1u;asset.motion.frames[0].duration=5u;
    for(unsigned i=0;i<8u;++i)asset.motion.tracks[i].count=1u;
    PortableGpCeremonySession session={.finishing_points={7u,7u,7u,7u}};
    PortableGpCeremonyPresentation view={.source_enabled=1u,.source_phase=14u,
        .phase=PORTABLE_CEREMONY_WAIT,.stats_reveal=16u,.road_line=73};
    for(unsigned i=0;i<4u;++i)view.coins[i].value=-1;
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    CHECK(portable_gp_ceremony_render(NULL,&asset,&session,&view,NULL,NULL,&world,&frame));
    const uint32_t backdrop=pixels[72u*240u+5u];
    CHECK(pixels[72u*240u+4u]==0xffabcdefu&&pixels[96u*240u+200u]==green);
    /* 266CC draws only the four coin slots, never its initialized point slots. */
    CHECK(pixels[104u*240u+204u]!=blue&&pixels[104u*240u+212u]!=blue);
    view.unlock_reveal=15u;
    CHECK(portable_gp_ceremony_render(NULL,&asset,&session,&view,NULL,NULL,&world,&frame));
    CHECK(pixels[72u*240u+4u]==backdrop&&pixels[72u*240u+116u]==0xffabcdefu);
    view.unlock_reveal=16u;const PortableGpCeremonyPresentation before=view;
    CHECK(portable_gp_ceremony_render(NULL,&asset,&session,&view,NULL,NULL,&world,&frame));
    CHECK(pixels[72u*240u]==green&&pixels[72u*240u+4u]==backdrop&&pixels[72u*240u+116u]==backdrop);
    CHECK(memcmp(&view,&before,sizeof view)==0);
    return 0;
}
int main(int argc,char **argv)
{
    /* The frontend fixture supplies the menu font and window for captions. */
    CHECK(argc==2);
    CHECK(source_summary_composition()==0);
    FILE *file=fopen(argv[1],"rb");
    CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long pack_size=ftell(file);
    uint8_t *pack_bytes=pack_size>0?malloc((size_t)pack_size):NULL;
    CHECK(pack_bytes!=NULL&&fseek(file,0,SEEK_SET)==0&&fread(pack_bytes,(size_t)pack_size,1,file)==1);
    fclose(file);
    PortableAssetPackView pack;
    CHECK(portable_asset_pack_open(pack_bytes,(size_t)pack_size,&pack)==PORTABLE_ASSET_PACK_OK);
    uint8_t bytes[32+43*40]={0};
    w32(bytes,0x33435047);w32(bytes+4,43);w32(bytes+8,120);w32(bytes+12,180);
    w32(bytes+16,32);w32(bytes+20,0xff182848);w32(bytes+24,0xff506830);w32(bytes+28,0xffe8d890);
    for(unsigned index=0;index<43u;++index){
        uint8_t *p=bytes+32+index*40;
        w32(p,36);memcpy(p+4,"MKSCIMG1",8);p[12]=32;p[14]=1;p[16]=1;p[18]=1;
        w32(p+28,32);w32(p+32,36);w32(p+36,0xff000001u+index);
    }
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_GP_CEREMONY_GRAPHICS,
        .content_id=1,.version=3,.bytes=bytes,.size=sizeof bytes};
    CHECK(source_asset(bytes,sizeof bytes)==0);
    PortableGpCeremonyAssetView asset;
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_OK);
    section.size--;
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION);
    section.size++;
    w32(bytes+32,UINT32_MAX);
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION);
    w32(bytes+32,36);section.version=1;
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_INVALID_SECTION);
    section.version=3;
    CHECK(portable_gp_ceremony_asset_open(&section,&asset)==PORTABLE_GP_CEREMONY_ASSET_OK);
    uint8_t *tiles=malloc(29u*0x800u);
    uint32_t pixels[240*160];uint16_t palette[16]={0,31};
    CHECK(tiles!=NULL);memset(tiles,0x11,29u*0x800u);
    PortableCharacterAssetView driver={.palette=palette,.palette_count=16,
        .direction_tiles=tiles,.direction_tile_bytes=29u*0x800u,.direction_frame_count=29};
    const PortableCharacterAssetView *podium[3]={&driver,&driver,&driver};
    PortableSoftwareFrame frame={pixels,240,160,NULL,1u,0u,0u,NULL};
    PortableGpCeremonySession ceremony={.cup_index=4,.engine_class=2,
        .player_standing_rank=6,.performance_rating=7,.finishing_coin_total=123u};
    PortableGpCeremonyPresentation view={.phase=PORTABLE_CEREMONY_WAIT,
        .entrance_ticks=120,.celebration_ticks=180,.fade_ticks=32};
    CHECK(portable_gp_ceremony_render(NULL,&asset,&ceremony,&view,podium,&driver,NULL,&frame));
    /* Selected cup/class/rank/rating and prompt must reach visible pixels. */
    CHECK(pixels[6*240+120]==0xff000018u&&pixels[9*240+216]==0xff000013u);
    CHECK(pixels[34*240+30]==0xff000007u&&pixels[35*240+212]==0xff000010u);
    CHECK(pixels[142*240+120]==0xff000020u);
    CHECK(pixels[132*240+100]==0xff000021u&&pixels[132*240+112]==0xff000023u&&
          pixels[132*240+120]==0xff000024u&&pixels[132*240+128]==0xff000025u);
    /* Unlock notices open a menu-font window whose border starts at (0,40). */
    ceremony.extra_cup_unlocked=1u;
    CHECK(portable_gp_ceremony_render(&pack,&asset,&ceremony,&view,podium,&driver,NULL,&frame));
    CHECK(pixels[40*240]==UINT32_C(0xff8080f0)&&pixels[(40+63)*240]==UINT32_C(0xff8080f0));
    ceremony.extra_cup_unlocked=0u;ceremony.special_cup_unlocked=1u;
    CHECK(portable_gp_ceremony_render(&pack,&asset,&ceremony,&view,podium,&driver,NULL,&frame));
    CHECK(pixels[40*240]==UINT32_C(0xff8080f0)&&pixels[(40+63)*240]!=UINT32_C(0xff8080f0));
    ceremony.special_cup_unlocked=0u;
    CHECK(portable_gp_ceremony_render(&pack,&asset,&ceremony,&view,podium,&driver,NULL,&frame));
    CHECK(pixels[40*240]!=UINT32_C(0xff8080f0));
    /* Podium/loser karts are transformed at physical resolution. Fading the
     * ceremony must not replace their subpixel edges with logical blocks. */
    const size_t physical_stride=971u,physical_count=physical_stride*640u;
    uint32_t *physical=malloc(physical_count*sizeof *physical);
    uint32_t *unfaded=malloc(physical_count*sizeof *unfaded);
    CHECK(physical&&unfaded);
    for(size_t i=0;i<physical_count;++i)physical[i]=0x13579bdfu;
    PortableSoftwareFrame high={physical,240u,160u,NULL,4u,physical_stride,0u,NULL};
    view.phase=PORTABLE_CEREMONY_WAIT;view.tick=16u;
    CHECK(portable_gp_ceremony_render(NULL,&asset,&ceremony,&view,podium,&driver,NULL,&high));
    memcpy(unfaded,physical,physical_count*sizeof *physical);
    int transformed_detail=0;
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x)
        transformed_detail|=physical[y*physical_stride+x]!=physical[(y/4u*4u)*physical_stride+x/4u*4u];
    CHECK(transformed_detail);
    view.phase=PORTABLE_CEREMONY_FADE;
    CHECK(portable_gp_ceremony_render(NULL,&asset,&ceremony,&view,podium,&driver,NULL,&high));
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x){
        /* WAIT alone draws the one-pixel synthetic continue label at142. */
        if(y/4u==142u)continue;
        uint32_t expected=0xff000000u;
        for(unsigned shift=0;shift<24u;shift+=8u)
            expected|=(((unfaded[y*physical_stride+x]>>shift)&255u)/2u)<<shift;
        CHECK(physical[y*physical_stride+x]==expected);
    }
    transformed_detail=0;
    for(unsigned y=0;y<640u;++y)for(unsigned x=0;x<960u;++x)
        transformed_detail|=physical[y*physical_stride+x]!=physical[(y/4u*4u)*physical_stride+x/4u*4u];
    CHECK(transformed_detail);
    for(unsigned y=0;y<640u;++y)for(size_t x=960u;x<physical_stride;++x)
        CHECK(physical[y*physical_stride+x]==0x13579bdfu);
    free(unfaded);free(physical);
    view.phase=PORTABLE_CEREMONY_FINISHED;
    CHECK(portable_gp_ceremony_render(NULL,&asset,&ceremony,&view,podium,&driver,NULL,&frame));
    for(unsigned i=0;i<240u*160u;++i)CHECK(pixels[i]==0xff000000u);
    ceremony.cup_index=5;
    CHECK(!portable_gp_ceremony_render(NULL,&asset,&ceremony,&view,podium,&driver,NULL,&frame));
    free(tiles);free(pack_bytes);puts("native GP ceremony renderer: PASS");return 0;
}
