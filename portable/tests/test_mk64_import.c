#include "mesh_item_rules_asset.h"
#include "model_asset.h"
#include "mk64_internal.h"
#include "course_3d.h"
#include "mesh_collision_index.h"
#include "rom_zip.h"
#include "n64_sequence.h"
#include "sound_set_asset.h"
#include "fx_set_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
static void expect(int yes,const char *what) { if(!yes){fprintf(stderr,"FAIL: %s\n",what);++failures;} }
static void be16(uint8_t *p,uint32_t v) {p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t *p,uint32_t v) {be16(p,v>>16);be16(p+2,v);}
static size_t mio_literals(uint8_t *out,const uint8_t *data,size_t size)
{
    const size_t masks=(size+7)/8,raw=16+masks;
    memcpy(out,"MIO0",4);be32(out+4,(uint32_t)size);be32(out+8,(uint32_t)raw);be32(out+12,(uint32_t)raw);
    memset(out+16,255,masks);memcpy(out+raw,data,size);return raw+size;
}
/* A MIO0 block of `size` zero bytes: one literal, then runs copied from it. */
static size_t mio_zeros(uint8_t *out,size_t size)
{
    uint8_t ops[4096];size_t count=0,done=0;uint16_t pairs[4096];size_t pair_count=0,literals=0;
    while(done<size) {
        const size_t left=size-done;
        if(!done||left<3) { ops[count++]=1;++literals;++done; }
        else { const size_t run=left>18?18:left;ops[count++]=0;pairs[pair_count++]=(uint16_t)((run-3)<<12);done+=run; }
    }
    const size_t masks=(count+7)/8,compressed=16+masks,raw=compressed+pair_count*2;
    memcpy(out,"MIO0",4);be32(out+4,(uint32_t)size);be32(out+8,(uint32_t)compressed);be32(out+12,(uint32_t)raw);
    memset(out+16,0,masks);
    for(size_t i=0;i<count;++i)if(ops[i])out[16+i/8]|=(uint8_t)(128u>>(i%8));
    for(size_t i=0;i<pair_count;++i)be16(out+compressed+i*2,pairs[i]);
    memset(out+raw,0,literals);
    return (raw+literals+3)&~(size_t)3;
}
static void le16(uint8_t *p,uint32_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static uint32_t tri(unsigned a,unsigned b,unsigned c) {return a|(b<<5)|(c<<10);}

/* Original, ROM-free square course. No Nintendo data or signature bypass in
 * the production importer: tests call only its ordinary buffer converters. */
static uint8_t *fixture(void)
{
    uint8_t *rom=calloc(1,MKSC_MK64_ROM_BYTES);if(!rom)return NULL;
    be32(rom,0x80371240);memcpy(rom+0x3B,"NKTE",4);
    uint8_t vertices[64]={0}; /* four 14-byte vertices and zero alignment padding */
    const int32_t xyz[4][3]={{-400,0,-400},{400,0,-400},{400,0,400},{-400,0,400}};
    for(unsigned i=0;i<4;++i) {
        for(unsigned k=0;k<3;++k)be16(vertices+i*14+k*2,(uint32_t)xyz[i][k]);
        be16(vertices+i*14+6,(i==1||i==2)?1024:0);be16(vertices+i*14+8,i>=2?1024:0);
        memset(vertices+i*14+10,255,4);
    }
    const size_t packed=mio_literals(rom+0x130000,vertices,sizeof vertices);
    uint8_t *p=rom+0x130000+packed;
    const uint8_t prefix[]={0x1A,0x50,0x50,0x20,0,0,0x70,0x36,0,0,0x58};
    memcpy(p,prefix,sizeof prefix);le16(p+11,tri(0,2,1));le16(p+13,tri(0,3,2));p[15]=0x2A;p[16]=0xFF;
    uint8_t data[0x240]={0};be32(data+0x100,0x07000000);data[0x104]=1;
    /* A closed path leaving point 0 towards -Z, as every source course does. */
    const int32_t path[5][3]={{300,0,0},{300,0,-300},{-300,0,-300},{-300,0,300},{300,0,300}};
    for(unsigned i=0;i<5;++i)for(unsigned k=0;k<3;++k)be16(data+0x200+i*8+k*2,(uint32_t)path[i][k]);
    be16(data+0x228,0x8000);
    /* One item box spawn {x, y, z, id} and its terminator. */
    be16(data+0x180,(uint32_t)-200);be16(data+0x184,(uint32_t)-200);be16(data+0x188,0x8000);
    const size_t data_size=mio_literals(rom+0x140000,data,sizeof data);
    uint8_t pixels[2048];for(unsigned i=0;i<1024;++i)be16(pixels+i*2,i%2?0x07C1:0xF801);
    const size_t texture_size=mio_literals(rom+MK64_TEXTURE_BASE,pixels,sizeof pixels);
    be32(rom+0x150000,0x0F000000);be32(rom+0x150004,(uint32_t)texture_size);be32(rom+0x150008,2048);
    const uint32_t h[12]={0x140000,0x140000+(uint32_t)data_size,0x130000,0x130000+(uint32_t)packed+17,
        0x150000,0x150020,0x0F000000,4,0x0F000000+(uint32_t)packed,80,0x09000000,0};
    for(unsigned i=0;i<12;++i)be32(rom+MK64_COURSE_TABLE+i*4,h[i]);
    for(unsigned i=0;i<4;++i)be32(rom+0xDD4D0+i*4,i?0x800DC778:0x06000200);
    /* Segment-13 item curves: green shells everywhere, the GP human's first
     * rank half bananas. */
    static uint8_t curves[0x8B78];memset(curves,3,sizeof curves);memset(curves+0x8150,1,50);
    mio_literals(rom+MK64_COMMON_DATA,curves,sizeof curves);
    /* Blank course-select pictures in course order. */
    size_t at=0x7A1418;
    for(unsigned i=0;i<20;++i)at+=mio_zeros(rom+at,128*78*2);
    /* Blank I4 minimap outlines in course order (64x64, Luigi/Kalimari/Rainbow 64x96, Toad's 128x64). */
    at=0x720C0C;
    for(unsigned i=0;i<20;++i)at+=mio_zeros(rom+at,i==8||i==11||i==13?3072:i==10?4096:2048);
    return rom;
}
/* The synthetic cartridge has no audio or effect art; courses reference the
 * sound and effect sets and their music, so stand-ins satisfy the pack
 * references. */
static void fixture_audio(RiCtx *ctx)
{
    RiBuf *stub=ri_buf(ctx);ri_u32(ctx,stub,0);
    ri_section(ctx,114,MK64_SOUND_SET_ID,2,stub);ri_section(ctx,MK64_SONG_KIND,mk64_course_song(0),1,stub);
    ri_section(ctx,116,MK64_FX_SET_ID,1,stub);
}
static void fixture_course(RiCtx *ctx)
{
    ctx->rom=ri_view(mk64_normalize(ctx,ctx->rom));
    const Mk64CourseSource s={"Synthetic N64 square",0,0,0,0x100,{0},0,0,0,0,0,0,0x180,0,0,0,0};
    mk64_shared_sections(ctx);fixture_audio(ctx);mk64_course_convert(ctx,&s);
}
static void fixture_arena(RiCtx *ctx)
{
    ctx->rom=ri_view(mk64_normalize(ctx,ctx->rom));
    const Mk64CourseSource s={"Synthetic N64 arena",0,4,0,0,{0x07000000},1,1,0,0,0,0,0x180,0,8,0,0};
    mk64_shared_sections(ctx);fixture_audio(ctx);mk64_course_convert(ctx,&s);
}
static int build(uint8_t *rom,RiGroup group,MkscRomImportResult *result,char *error)
{ return ri_build_steps(rom,MKSC_MK64_ROM_BYTES,&group,1,NULL,NULL,NULL,result,error,512); }
static void inspect(const MkscRomImportResult *result,int arena)
{
    PortableAssetPackView pack={0};char error[512];PortableCourse3DView course={0};PortableCourse3DMeta meta={0};
    expect(portable_asset_pack_open(result->pack,result->pack_size,&pack)==PORTABLE_ASSET_PACK_OK,"fixture pack opens");
    const int loaded=portable_course_3d_open(&pack,MK64_COURSE_ID(0),&course,error,sizeof error);
    if(!loaded)fprintf(stderr,"course validation: %s\n",error);
    expect(loaded,"converted course passes runtime validation");if(!loaded)return;
    expect(portable_course_3d_meta(&course,&meta) && meta.physics_profile_id==MK64_PHYSICS_ID,
           "courses and arenas drive the imported profile");
    expect(meta.modes==(arena?16u:15u) && meta.laps==(arena?0u:3u),"source mode and lap metadata");
    const PortableCourse3DTable *triangles=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('T','R','I','S'));
    const PortableCourse3DTable *collision=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('C','O','L','T'));
    expect(triangles && triangles->count==2 && collision && collision->count==2,"render and source collision faces");
    PortableNativeImage texture={0};
    expect(portable_course_3d_texture(&course,0x4E001000,&texture),"source RGBA16 texture resolves");
    expect(texture.width==32 && texture.height==32,"texture dimensions");
    if(texture.pixels)expect(ri_le32(texture.pixels)==0xFFFF0000u,"RGBA5551 red converted to ARGB");
    const PortableCourse3DTable *routes=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('R','T','N','D'));
    expect(routes && routes->count==(arena?16u:5u),"source path nodes or the arena patrol");
    const PortableCourse3DTable *boxes=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('I','T','E','M'));
    uint32_t rules=0;
    expect(boxes && boxes->count==1 && portable_course_3d_item_rules(&course,&rules,error,sizeof error) &&
           rules==(arena?MK64_ITEMS_BATTLE_ID:MK64_ITEMS_ID),"source item box with the imported item rules");
    PortableMeshItemRules odds;
    expect(portable_mesh_item_rules_asset_load(&pack,MK64_ITEMS_ID,&odds) &&
           odds.player_weights[0][0].item==PORTABLE_MESH_ITEM_SINGLE_BANANA && odds.player_weights[0][0].weight==50u &&
           odds.player_weights[0][1].item==PORTABLE_MESH_ITEM_GREEN_SHELL && odds.player_weights[0][1].weight==50u &&
           odds.cpu_weights[0][0].item==PORTABLE_MESH_ITEM_GREEN_SHELL && odds.cpu_weights[0][0].weight==100u,
           "GP odds count the segment-13 curve bytes");
    const PortableCourse3DTable *grids=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('G','R','I','D'));
    expect(grids && grids->count==(arena?4u:8u),"GP grid has eight slots, arenas the four battle starts");
    for(uint32_t slot=0;grids && slot<grids->count;++slot) {
        PortableCourse3DGrid grid={0};
        expect(portable_course_3d_grid(&course,slot,&grid),"grid slot");
        if(arena) {
            /* player 1 at (0, d) faces the centre (identity, -Y); player 2 the other way */
            const int32_t want[2][2]={{0,1<<30},{1<<30,0}};
            if(slot<2)expect(grid.quaternion_q30[2]==want[slot][0] && grid.quaternion_q30[3]==want[slot][1],"arena starts face the centre");
            continue;
        }
        expect(grid.quaternion_q30[2]==0 && grid.quaternion_q30[3]==1<<30,"grid faces the source start direction (-Y)");
        /* spawn_players.c: x = path 0 x +/-20 units, 30 + 20n units behind; 17.6 units/metre. */
        const int32_t x=(int32_t)((int64_t)(slot%2?280:320)*65536*5/88),y=(int32_t)((int64_t)(30+20*slot)*65536*5/88);
        expect(grid.position_q16[0]==x && grid.position_q16[1]==y && grid.position_q16[2]==0,"grid slot from source spawn rule");
    }
    if(arena)return;
    const PortableCourse3DTable *gates=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('C','P','N','T'));
    const PortableCourse3DTable *respawns=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('R','E','S','P'));
    expect(gates && gates->count==4 && respawns && respawns->count==4,"distance-spaced lap gates, one respawn each");
    PortableCourse3DCheckpoint finish={0};
    expect(portable_course_3d_checkpoint(&course,0,&finish) && (finish.flags&1u) && finish.normal_q30[1]<0,
           "finish gate at path point 0 faces -Y");
    for(uint32_t i=0;i<collision->count;++i)
        expect(ri_le32(collision->bytes+i*32+20)<4u,"road faces name a respawn");
}
static void texture_variant(uint8_t *rom,uint32_t width,uint32_t color,const char *what)
{
    MkscRomImportResult result={0};char error[512];
    const int built=build(rom,fixture_course,&result,error);expect(built,what);
    PortableAssetPackView pack={0};PortableCourse3DView course={0};PortableNativeImage texture={0};
    if(built && portable_asset_pack_open(result.pack,result.pack_size,&pack)==PORTABLE_ASSET_PACK_OK &&
       portable_course_3d_open(&pack,MK64_COURSE_ID(0),&course,error,sizeof error) &&
       portable_course_3d_texture(&course,0x4E001000,&texture)) {
        expect(texture.width==width && texture.height==32 && ri_le32(texture.pixels)==color,what);
        if(width==64)expect(ri_le32(texture.pixels+63*4)==color,"mirrored texture returns to first column");
    } else expect(0,"variant course validates and texture resolves");
    mksc_rom_import_free(&result);
}
static void bad_course(uint8_t *rom,const char *what)
{
    MkscRomImportResult result;char error[512];
    expect(!build(rom,fixture_course,&result,error),what);
    expect(!result.pack && !result.pack_size,"failed conversion publishes no partial pack");
    mksc_rom_import_free(&result);
}
static void mio_group(RiCtx *ctx) {ri_section(ctx,80,3,1,mk64_mio0(ctx,ctx->rom));}
/* Packed 0x26 is gsSPTexture(..., G_ON) and 0x27 is G_OFF (memory.c). */
static void texture_toggle_group(RiCtx *ctx)
{
    static const uint8_t packed[]={0x26,0x27,0xFF};
    ri_section(ctx,80,3,1,mk64_display_lists(ctx,(RiData){packed,sizeof packed},8));
}
static void texture_toggle_test(void)
{
    MkscRomImportResult result;char error[512];const RiGroup group[]={texture_toggle_group};
    uint8_t unused[4]={0};
    expect(ri_build_steps(unused,sizeof unused,group,1,NULL,NULL,NULL,&result,error,sizeof error),"texture toggles expand");
    PortableAssetPackView pack={0};PortableAssetSectionView section;
    if(result.pack && portable_asset_pack_open(result.pack,result.pack_size,&pack)==PORTABLE_ASSET_PACK_OK &&
       portable_asset_pack_find_section(&pack,80,3,&section)==PORTABLE_ASSET_PACK_OK)
        expect(section.size==16 && mk64_be32(section.bytes)==0xBB000001u && mk64_be32(section.bytes+8)==0xBB000000u,
               "0x26 turns textures on and 0x27 turns them off");
    else expect(0,"texture toggle section");
    mksc_rom_import_free(&result);
}
static void mio_tests(void)
{
    uint8_t bytes[24]={0};memcpy(bytes,"MIO0",4);be32(bytes+4,9);be32(bytes+8,17);be32(bytes+12,19);
    bytes[16]=0x80;be16(bytes+17,0x5000);bytes[19]='A';
    MkscRomImportResult result;char error[512];const RiGroup group[]={mio_group};
    expect(ri_build_steps(bytes,20,group,1,NULL,NULL,NULL,&result,error,sizeof error),"MIO0 overlapping back-reference");
    PortableAssetPackView pack={0};PortableAssetSectionView section;
    if(result.pack && portable_asset_pack_open(result.pack,result.pack_size,&pack)==PORTABLE_ASSET_PACK_OK &&
       portable_asset_pack_find_section(&pack,80,3,&section)==PORTABLE_ASSET_PACK_OK)
        expect(section.size==9 && !memcmp(section.bytes,"AAAAAAAAA",9),"MIO0 byte oracle");
    mksc_rom_import_free(&result);
    const size_t offsets[]={0,8,12,16,17,18};
    const uint8_t values[]={0,255,255,0,255,255};
    for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];++i) {
        const uint8_t old=bytes[offsets[i]];bytes[offsets[i]]=values[i];
        expect(!ri_build_steps(bytes,20,group,1,NULL,NULL,NULL,&result,error,sizeof error),"malformed MIO0 rejected");
        mksc_rom_import_free(&result);bytes[offsets[i]]=old;
    }
    for(size_t size=0;size<20;++size) {
        expect(!ri_build_steps(bytes,size,group,1,NULL,NULL,NULL,&result,error,sizeof error),"truncated MIO0 rejected");
        mksc_rom_import_free(&result);
    }
}
/* Plays a song for up to `ticks`: 1 when it runs without a script fault and sounds. */
static PortableN64Player player;
static int plays(const PortableN64SequenceView *song,int bank,int sound,unsigned ticks,unsigned *ran)
{
    if(bank<0)portable_n64_player_start(&player,song);
    else if(portable_n64_player_start_effect(&player,song,(uint8_t)bank,(uint8_t)sound)!=PORTABLE_N64_OK)return 0;
    int peak=0;unsigned t=0;
    for(;t<ticks && portable_n64_player_playing(&player);++t) {
        int16_t frame[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u]={0};
        if(portable_n64_player_mix(&player,frame)!=PORTABLE_N64_OK) {
            fprintf(stderr,"N64 song fault: %s at 0x%x\n",portable_n64_status_name(player.status),player.failed_at);
            return 0;
        }
        for(unsigned i=0;i<PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u;++i)if(abs(frame[i])>peak)peak=abs(frame[i]);
    }
    if(ran)*ran=t;
    return peak>500;
}
static void audio(const PortableAssetPackView *pack)
{
    char error[256];
    for(unsigned i=0;i<20;++i) {
        PortableCourse3DView course={0};PortableCourse3DMeta meta={0};PortableAssetSectionView section;
        PortableN64SequenceView song;PortableCourse3DProperty set;
        expect(portable_course_3d_open(pack,MK64_COURSE_ID(i),&course,error,sizeof error) &&
               portable_course_3d_meta(&course,&meta) && meta.music_kind==MK64_SONG_KIND &&
               meta.music_id==mk64_course_song(i) &&
               portable_asset_pack_find_section(pack,MK64_SONG_KIND,meta.music_id,&section)==PORTABLE_ASSET_PACK_OK &&
               portable_n64_sequence_open(&song,section.bytes,section.size)==PORTABLE_N64_OK,"course music is an N64 song");
        expect(portable_course_3d_property_find(&course,meta.property_first,meta.property_count,"mksc:sound-set",&set) &&
               set.value.asset.id==MK64_SOUND_SET_ID,"courses race with the MK64 sound set");
        PortableCourse3DProperty fx;
        expect(portable_course_3d_property_find(&course,meta.property_first,meta.property_count,"mksc:fx-set",&fx) &&
               fx.value.asset.kind==PORTABLE_ASSET_SECTION_FX_SET && fx.value.asset.id==MK64_FX_SET_ID,
               "courses race with the MK64 effect set");
    }
    /* render_player_shadow's 13.5-unit kart shadow and the drift, smoke, flame and dust art. */
    PortableFxSet fx;PortableModelMaterial shadow;
    const PortableFxShadow *row=portable_fx_set_find(pack,MK64_FX_SET_ID,&fx)?portable_fx_set_shadow(&fx,0,0):NULL;
    expect(row && row->half_width_q16==row->half_length_q16 && row->half_width_q16>40000 && row->half_width_q16<41000 &&
           portable_model_material_open(pack,row->material,&shadow) && shadow.texture.width==64u &&
           fx.style_count==4u && fx.tier_argb[1]==0x70FFFF00u && fx.tier_argb[2]==0x70FF9600u &&
           portable_fx_set_style(&fx,PORTABLE_FX_STYLE_SMOKE)->frames==4u,"MK64 shadow and particle art");
    /* Every imported sequence runs ten seconds without a script fault. */
    unsigned songs=0;
    for(uint32_t id=2;id<=30;++id) {
        PortableAssetSectionView section;PortableN64SequenceView song;
        if(portable_asset_pack_find_section(pack,MK64_SONG_KIND,id,&section)!=PORTABLE_ASSET_PACK_OK)continue;
        expect(portable_n64_sequence_open(&song,section.bytes,section.size)==PORTABLE_N64_OK,"N64 song opens");
        expect(plays(&song,-1,0,600u,NULL),"N64 song plays");++songs;
    }
    expect(songs==20,"twelve course themes and eight event songs");
    PortableAssetSectionView effects;PortableN64SequenceView sfx;unsigned ran=0;
    expect(portable_asset_pack_find_section(pack,MK64_SONG_KIND,MK64_SONG_ID(0),&effects)==PORTABLE_ASSET_PACK_OK &&
           portable_n64_sequence_open(&sfx,effects.bytes,effects.size)==PORTABLE_N64_OK &&
           plays(&sfx,4,0x03,600u,&ran) && ran<600u,"the countdown effect plays and ends");
    PortableSoundSet sounds;uint16_t boost=0,struck=0,engine=0,go=0;
    expect(portable_sound_set_find(pack,MK64_SOUND_SET_ID,&sounds) && sounds.voice_count==8u &&
           portable_sound_set_voice(&sounds,MK64_DRIVER_ID(7),&boost,&struck) &&
           boost==MK64_EFFECT(2,7u*16u+1u) && struck==MK64_EFFECT(2,7u*16u+8u) &&
           portable_sound_set_engine(&sounds,MK64_DRIVER_ID(7),&engine) && engine==MK64_EFFECT(0,7) &&
           portable_sound_set_cue(&sounds,115,&go) && go==MK64_EFFECT(4,4),"Bowser's voices and engine, the green light");
    for(unsigned i=0;i<8;++i) {
        expect(portable_sound_set_voice(&sounds,MK64_DRIVER_ID(i),&boost,&struck) &&
               plays(&sfx,2,boost&0xff,600u,NULL) && plays(&sfx,2,struck&0xff,600u,NULL),"every driver's voices sound");
    }
}
static int real_rom(const char *path)
{
    FILE *file=fopen(path,"rb");
    if(!file){fprintf(stderr,"cannot open owner ROM\n");return 1;}
    uint8_t *rom=malloc(MKSC_MK64_ROM_BYTES+1u);
    if(!rom){fclose(file);return 1;}
    const size_t size=fread(rom,1,MKSC_MK64_ROM_BYTES+1u,file);
    const int read_ok=!ferror(file);fclose(file);
    MkscRomImportResult result={0};MkscMk64Inventory inventory;char error[512];
    const int ok=read_ok && mksc_mk64_import(rom,size,NULL,NULL,&result,&inventory,error,sizeof error)==1;
    free(rom);
    if(!ok){fprintf(stderr,"MK64 owner import failed: %s\n",read_ok?error:"read error");return 1;}
    PortableAssetPackView pack={0};
    expect(portable_asset_pack_open(result.pack,result.pack_size,&pack)==PORTABLE_ASSET_PACK_OK,"owner pack opens");
    expect(portable_course_3d_validate_pack(&pack,error,sizeof error),error);
    expect(inventory.converted==20 && inventory.courses==16 && inventory.battle_stages==4,"all owner courses converted");
    /* Course lengths the game states, by source course index (Mario Raceway
     * 567 m .. Rainbow Road 2000 m; arenas 0). The primary CPU route over
     * the converted path must measure them at 17.6 units/metre, within 2%. */
    static const uint32_t stated[20]={567,687,777,747,772,734,691,1025,717,527,1036,753,756,2000,1591,0,0,0,893,0};
    unsigned cups[4]={0};
    for(unsigned i=0;i<20;++i) {
        PortableCourse3DView course={0};PortableCourse3DMeta meta={0};
        if(!portable_course_3d_open(&pack,MK64_COURSE_ID(i),&course,error,sizeof error) ||
           !portable_course_3d_meta(&course,&meta)){expect(0,error);continue;}
        const PortableCourse3DTable *boxes=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('I','T','E','M'));
        expect(boxes && boxes->count>=12,"source item boxes");
        /* Mario Raceway: 27 trees, 10 piranha plants and two spinning signs. */
        const PortableCourse3DTable *objects=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('O','B','J','S'));
        if(i==0) {
            expect(objects && objects->count==44,"Mario Raceway scenery placements and five VS bomb karts");
            PortableModelView mesh;
            expect(portable_model_mesh_open(&pack,MK64_OBJECT_MESH_ID(0,0),&mesh) && mesh.triangle_count==2 &&
                   mesh.material_count==1 && mesh.materials[0].texture.width==32 && mesh.materials[0].texture.height==64,
                   "tree billboard mesh with its CI8 texture");
        }
        /* Sky sprites: Luigi Raceway's three cloud shapes, Rainbow Road's five twinkling star classes,
         * none over Bowser's Castle (course_init_cloud). */
        uint32_t sprites=0;
        const PortableCourse3DTable *mats=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('M','A','T','S'));
        for(uint32_t k=0;mats && k<mats->count;++k) {
            PortableCourse3DMaterial material;PortableCourse3DProperty sky;
            sprites+=portable_course_3d_material(&course,k,&material) && (material.flags&32u) &&
                     portable_course_3d_property_find(&course,material.property_first,material.property_count,"mksc:sky",&sky);
        }
        if(i==8)expect(sprites==3,"Luigi Raceway's clouds sit on its sky");
        if(i==13)expect(sprites==5,"Rainbow Road's five star twinkle classes sit on its sky");
        if(i==2)expect(sprites==0,"Bowser's Castle has no sky sprites");
        if(!stated[i]) {
            const PortableCourse3DTable *grids=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('G','R','I','D'));
            expect(meta.physics_profile_id==MK64_PHYSICS_ID && meta.modes==16u && grids && grids->count==4,
                   "arenas fight from the four source starts");
            continue;
        }
        uint32_t profile=0;
        expect(meta.physics_profile_id==MK64_PHYSICS_ID && meta.laps==3 && meta.modes==15u &&
               portable_course_3d_physics_profile(&course,2,&profile,error,sizeof error),"race course drives three laps");
        expect(meta.cup_order<4u && meta.cup_id>=MK64_CUP_ID(0) && meta.cup_id<MK64_CUP_ID(4),"source cup row");
        if(meta.cup_id>=MK64_CUP_ID(0) && meta.cup_id<MK64_CUP_ID(4))cups[meta.cup_id-MK64_CUP_ID(0)]|=1u<<meta.cup_order;
        const PortableCourse3DTable *grid=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('G','R','I','D'));
        const PortableCourse3DTable *gates=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('C','P','N','T'));
        const PortableCourse3DTable *respawns=portable_course_3d_table(&course,PORTABLE_COURSE_3D_TAG('R','E','S','P'));
        expect(grid && grid->count==8,"eight GP starts");
        expect(gates && gates->count>=20 && respawns && respawns->count==gates->count,"lap gates and their respawns");
        PortableCourse3DRoute route={0};PortableCourse3DRouteNode a={0},b={0};
        expect(portable_course_3d_route(&course,0,&route) && route.kind==0 && route.flags==1,"closed primary CPU route");
        double length=0;int32_t low=INT32_MAX;
        for(uint32_t n=0;n<route.node_count;++n) {
            portable_course_3d_route_node(&course,route.node_first+n,&a);
            portable_course_3d_route_node(&course,route.node_first+(n+1)%route.node_count,&b);
            double square=0;for(unsigned k=0;k<3;++k){const double d=(b.position_q16[k]-a.position_q16[k])/65536.0;square+=d*d;}
            length+=__builtin_sqrt(square);if(a.position_q16[2]<low)low=a.position_q16[2];
        }
        if(length<stated[i]*0.98 || length>stated[i]*1.02)
            fprintf(stderr,"course %u route %.1f m, stated %u m\n",i,length,(unsigned)stated[i]);
        expect(length>=stated[i]*0.98 && length<=stated[i]*1.02,"converted path matches the stated course length");
        PortableCourse3DProperty fall;
        if(portable_course_3d_property_find(&course,meta.property_first,meta.property_count,"mksc:fall-height",&fall))
            expect(fall.value.fixed_q16<low,"the whole primary path is above the water level");
        PortableCourse3DChase chase;char chase_error[128];
        expect(portable_course_3d_chase(&course,&chase,chase_error,sizeof chase_error) && chase.has_rig &&
               chase.kart_scale_q16==40960 && chase.fov_turns==7282u,"MK64 kart scale 5.5/8.8 and 40 degree camera");
    }
    expect(cups[0]==15 && cups[1]==15 && cups[2]==15 && cups[3]==15,"four complete source cups");
    /* gen_random_item curves: 1st-place humans draw banana 30, bunch 5 and fake
     * box 10 of 100; last-place CPUs mostly bananas; battle is one row. */
    PortableMeshItemRules gp,battle;
    expect(portable_mesh_item_rules_asset_load(&pack,MK64_ITEMS_ID,&gp) &&
           gp.player_weights[0][0].item==PORTABLE_MESH_ITEM_SINGLE_BANANA && gp.player_weights[0][0].weight==30u &&
           gp.player_weights[0][1].item==PORTABLE_MESH_ITEM_BANANA_BUNCH && gp.player_weights[0][1].weight==5u &&
           gp.player_weights[0][5].item==PORTABLE_MESH_ITEM_FAKE_BOX && gp.player_weights[0][5].weight==10u &&
           gp.player_weights[7][3].item==PORTABLE_MESH_ITEM_STAR && gp.player_weights[7][3].weight==30u &&
           gp.cpu_weights[0][0].weight==60u && gp.cpu_weights[7][5].item==PORTABLE_MESH_ITEM_MUSHROOM,
           "MK64 GP item curves");
    expect(portable_mesh_item_rules_asset_load(&pack,MK64_ITEMS_BATTLE_ID,&battle) &&
           !memcmp(battle.player_weights[0],battle.cpu_weights[7],sizeof battle.player_weights[0]) &&
           battle.player_weights[0][3].item==PORTABLE_MESH_ITEM_TRIPLE_GREEN && battle.player_weights[0][3].weight==20u,
           "MK64 battle item curve");
    /* Item art: shells, banana, fake box, item box and shard (seven models)
     * and fifteen item window icons
     * bound once per profile in a BND1 overlay; shells are 32x32 CI8 frames. */
    PortableAssetSectionView bindings;PortableModelView shell;
    expect(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_MODEL_BINDINGS,1,&bindings)==PORTABLE_ASSET_PACK_OK &&
           bindings.size==8u+66u*44u && ri_le32(bindings.bytes+4)==66u &&
           ri_le32(bindings.bytes+8)==PORTABLE_MODEL_ITEM && ri_le32(bindings.bytes+8+12)==PORTABLE_MODEL_ITEM_FLAG_BILLBOARD &&
           portable_model_mesh_open(&pack,ri_le32(bindings.bytes+8+16),&shell) && shell.material_count==1 &&
           shell.materials[0].texture.width==32,"MK64 item art bound for every profile");
    audio(&pack);
    mksc_rom_import_free(&result);
    printf("mk64 owner import: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--mk64"))return real_rom(argv[2]);
    uint8_t *rom=fixture();if(!rom)return 1;
    char error[512];MkscRomImportResult original={0};
    expect(mksc_mk64_byte_order(rom,MKSC_MK64_ROM_BYTES)==1,"z64 signature");
    expect(!mksc_mk64_identify(rom,MKSC_MK64_ROM_BYTES,error,sizeof error) && strstr(error,"SHA-1"),"synthetic ROM cannot impersonate US cartridge");
    expect(!mksc_mk64_identify(rom,64,error,sizeof error),"truncated cartridge rejected");
    rom[0x3E]='P';
    expect(!mksc_mk64_identify(rom,MKSC_MK64_ROM_BYTES,error,sizeof error) && strstr(error,"US NKTE"),"other region rejected");
    rom[0x3E]='E';
    expect(!mksc_mk64_byte_order(NULL,4),"null byte-order input rejected");
    expect(build(rom,fixture_course,&original,error),"synthetic course converts");
    if(!original.pack)fprintf(stderr,"conversion: %s\n",error);else inspect(&original,0);
    for(unsigned mask=1;mask<=3;mask+=2) {
        for(size_t i=0;i<MKSC_MK64_ROM_BYTES;i+=4) {
            const uint8_t p[4]={rom[i],rom[i+1],rom[i+2],rom[i+3]};
            for(unsigned j=0;j<4;++j)rom[i+j]=p[j^mask];
        }
        expect(mksc_mk64_byte_order(rom,MKSC_MK64_ROM_BYTES)==(mask==1?2:3),"swapped byte-order signature");
        MkscRomImportResult swapped={0};expect(build(rom,fixture_course,&swapped,error),"swapped cartridge converts");
        expect(original.pack && swapped.pack && original.pack_size==swapped.pack_size &&
               !memcmp(original.pack,swapped.pack,original.pack_size),"all byte orders emit identical packs");
        mksc_rom_import_free(&swapped);
        for(size_t i=0;i<MKSC_MK64_ROM_BYTES;i+=4) {
            const uint8_t p[4]={rom[i],rom[i+1],rom[i+2],rom[i+3]};
            for(unsigned j=0;j<4;++j)rom[i+j]=p[j^mask];
        }
    }
    MkscRomImportResult arena={0};expect(build(rom,fixture_arena,&arena,error),"arena converts");
    if(arena.pack)inspect(&arena,1);
    mksc_rom_import_free(&arena);
    if(argc==3 && !strcmp(argv[1],"--write-pack") && original.pack) {
        FILE *file=fopen(argv[2],"wb");expect(file!=NULL,"open fixture output");
        if(file){expect(fwrite(original.pack,1,original.pack_size,file)==original.pack_size,"write fixture pack");expect(!fclose(file),"close fixture pack");}
    }
    if(argc==3 && !strcmp(argv[1],"--write-rom")) {
        FILE *file=fopen(argv[2],"wb");expect(file!=NULL,"open synthetic ROM output");
        if(file){expect(fwrite(rom,1,MKSC_MK64_ROM_BYTES,file)==MKSC_MK64_ROM_BYTES,"write synthetic ROM");expect(!fclose(file),"close synthetic ROM");}
    }
    be32(rom+MK64_COURSE_TABLE+8,0xFFFFFFF0);bad_course(rom,"out-of-ROM header span rejected");be32(rom+MK64_COURSE_TABLE+8,0x130000);
    be32(rom+MK64_COURSE_TABLE+28,32769);bad_course(rom,"oversize vertex count rejected");be32(rom+MK64_COURSE_TABLE+28,4);
    const size_t packed=mk64_be32(rom+MK64_COURSE_TABLE+32)&0xFFFFFFu;
    rom[0x130000+packed+1]=0x51;
    texture_variant(rom,64,0xFFFF0000u,"mirrored RGBA16 texture expands without changing source UV period");
    rom[0x130000+packed+1]=0x50;
    rom[0x130000+packed]=0x1D;rom[0x130000+packed+3]=0x23;
    be16(rom+MK64_TEXTURE_BASE+272,0x80C0);
    texture_variant(rom,32,0xC0808080u,"IA16 preserves intensity and alpha");
    rom[0x130000+packed]=0x1A;rom[0x130000+packed+3]=0x20;be16(rom+MK64_TEXTURE_BASE+272,0xF801);
    rom[0x130000+packed]=0xEE;bad_course(rom,"unknown packed opcode rejected");rom[0x130000+packed]=0x1A;
    rom[0x130000+packed+8]=255;bad_course(rom,"vertex reference outside source rejected");rom[0x130000+packed+8]=0;
    rom[0x130050]=1;bad_course(rom,"nonzero vertex alignment padding rejected");rom[0x130050]=0;
    be32(rom+0xDD4D0,0x06FFFFF8);bad_course(rom,"out-of-segment path rejected");be32(rom+0xDD4D0,0x06000200);
    rom[0x130000+packed+15]=0x2B;rom[0x130000+packed+16]=0;rom[0x130000+packed+17]=0;rom[0x130000+packed+18]=0xFF;
    be32(rom+MK64_COURSE_TABLE+12,0x130000+(uint32_t)packed+19);
    bad_course(rom,"recursive collision display list rejected");
    rom[0x130000+packed+15]=0x2A;rom[0x130000+packed+16]=0xFF;
    be32(rom+MK64_COURSE_TABLE+12,0x130000+(uint32_t)packed+17);
    be32(rom+0x150004,1);bad_course(rom,"truncated texture MIO0 rejected");
    mksc_rom_import_free(&original);free(rom);mio_tests();texture_toggle_test();
    printf("mk64 import: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
