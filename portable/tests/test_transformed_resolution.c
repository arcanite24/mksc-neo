#include "race_scene_renderer.h"
#include "shell_cells_fixture.h"
#include "character_renderer.h"
#include "artwork_lod.h"
#include "race_particle_renderer.h"
#include "sky_renderer.h"
#include "lightning_effect_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"transformed resolution line %d: %s\n",__LINE__,#c);return 1;} } while(0)
enum { WIDTH=240,HEIGHT=160,PAD=3 };
typedef struct {
    int32_t pan_q16;
    uint8_t tiles[4096],box_tiles[PORTABLE_ITEM_BOX_TILE_BYTES],rgba[64*64*4],indices[64*64];
    uint8_t palette_bytes[32],map[2048],road_tile[64],road_map[1],slots[64];
    uint16_t palette[64];
    int32_t sine[1024];
    PortableSkyAssetView sky;
    PortableCharacterAssetView character;
    PortableItemBoxAssetView box;
    PortableRaceParticleFrame step;
    PortableRaceParticleAssetView particle;
    PortableCourseSceneryArt scenery_art[5];
    PortableCourseSceneryObject object;
    PortableCourseSceneryView scenery;
    PortableDriverProjectionRules projection;
    PortableRaceSceneView scene;
} Fixture;
static void fixture_init(Fixture *f)
{
    memset(f,0,sizeof *f);
    f->palette[1]=31u;f->palette[2]=31u<<5;f->palette[3]=31u<<10;
    for(unsigned i=0;i<32u;++i)f->palette_bytes[i]=(uint8_t)(f->palette[i/2u]>>((i&1u)*8u));
    for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x){
        const unsigned color=(x+2u*y)%4u;
        const size_t tile=((y/8u)*8u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        f->tiles[tile]|=(uint8_t)(color<<((x&1u)*4u));
        f->indices[y*64u+x]=(uint8_t)color;
        const size_t p=(y*64u+x)*4u;
        f->rgba[p]=(uint8_t)(color==3u?255u:0u);
        f->rgba[p+1u]=(uint8_t)(color==2u?255u:0u);
        f->rgba[p+2u]=(uint8_t)(color==1u?255u:0u);
        f->rgba[p+3u]=(uint8_t)(color?255u:0u);
    }
    memcpy(f->tiles+2048,f->tiles,2048u);
    memcpy(f->box_tiles,f->tiles,sizeof f->box_tiles);
    f->sine[256]=32768;f->sine[768]=-32768;f->sine[128]=23170;f->sine[384]=23170;
    f->sky=(PortableSkyAssetView){.tiles=f->road_tile,.tile_bytes=32u,.palette=f->palette,.palette_count=16u,
        .front_map=f->map,.mid_map=f->map,.back_map=f->map,.front_map_bytes=2048u,.mid_map_bytes=2048u,
        .back_map_bytes=2048u,.front_width_tiles=32u,.mid_width_tiles=32u,.back_width_tiles=32u,.map_height_tiles=32u};
    f->character=(PortableCharacterAssetView){.tiles=f->tiles,.tile_bytes=2048u,.palette=f->palette,.palette_count=16u,
        .width=64u,.height=64u,.direction_tiles=f->tiles,.direction_tile_bytes=4096u,.direction_frame_count=2u,
        .view_frame_slots=f->slots,.view_frame_slot_count=64u};
    f->box=(PortableItemBoxAssetView){.tiles=f->box_tiles,.tile_bytes=sizeof f->box_tiles,.palette=f->palette,
        .palette_count=16u,.frame_count=8u,.width=32u,.height=32u};
    static const uint8_t cells[8]={1u,0u,0xfdu,0u,0xfbu,1u,0u,0u};
    f->step=(PortableRaceParticleFrame){0u,8u,32u};
    f->particle=(PortableRaceParticleAssetView){.tiles=f->tiles,.tile_bytes=sizeof f->tiles,.palette=f->palette,
        .palette_count=64u,.cells=cells,.cell_bytes=sizeof cells,.frames=&f->step,.frame_count=1u};
    f->projection=(PortableDriverProjectionRules){{{511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    for(unsigned a=0u;a<5u;++a)for(unsigned lod=0u;lod<4u;++lod){
        f->scenery_art[a].lod[lod]=(PortableNativeImage){.pixels=f->rgba,.width=64u,.height=64u,.anchor_x=-32,.anchor_y=-46};
        f->scenery_art[a].distance_shift[lod]=(uint8_t)lod;
    }
    f->object=(PortableCourseSceneryObject){.position_x_q16=8192,.position_y_q16=3*65536+16384,.affine_q8=211u};
    f->scenery=(PortableCourseSceneryView){.objects=&f->object,.object_count=1u,.native_art=f->scenery_art,.native_art_count=1u,
        .tiles=f->tiles,.palette=f->palette,.lod32_tiles=f->tiles,.lod8_tiles=f->tiles,.lod64_tiles=f->tiles};
    f->scene=(PortableRaceSceneView){.world_map={.map=f->road_map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u},
        .tileset={.tiles=f->road_tile,.tile_bytes=64u,.palette=f->palette,.palette_count=16u},
        .sky=&f->sky,.character=&f->character,.ghost_character=&f->character,.driver_projection=&f->projection,
        .sine=f->sine,.sine_count=1024u,.item_box=&f->box};
}

typedef enum { ROAD,KART,CPU,GHOST,SCENERY_ARGB,SCENERY_INDEXED,SCENERY_TILES,PIPE,
    ROTATED_ACTOR,BOX,GREEN,RED,BANANA,BALLOON,BOMB,PARTICLE,IMPACT,COIN,SPRAY,SPLASH,SMOKE,THAW,DRIP,TRAIL,CHARGED,BOOST,LIGHTNING,MINIMAP,WEATHER,SKY_FLAT,CASE_COUNT } Case;
static const char *const names[CASE_COUNT]={"road","kart","cpu/local","ghost","scenery-argb","scenery-indexed",
    "scenery-tile-mips","pipe","rotated-actor","item-box","green-shell","red-shell","banana","battle-balloon","battle-bomb",
    "scaled-particle","impact","coin","surface-spray","entry-splash","smoke-world-anchor","recovery-thaw-world-anchor","recovery-drip-world-anchor",
    "surface-trail-q7","charged-world-anchor","boost-world-anchor","lightning-flat","minimap-marker-anchor","weather-flat","sky-flat"};
static int draw(Fixture *f,Case which,PortableSoftwareFrame *frame)
{
    portable_software_frame_clear(frame,0x101828u);
    const PortableCameraProjectionResult body={.x=120,.y=110,.distance=211};
    if(which==ROAD){
        uint8_t map[1]={0},tiles[64];
        for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<8u;++x)tiles[y*8u+x]=(uint8_t)(1u+(x+2u*y)%3u);
        const PortableRoadView road={.map={.map=map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u},
            .tileset={.tiles=tiles,.tile_bytes=64u,.palette=f->palette,.palette_count=16u}};
        PortableCameraAffineLine lines[PORTABLE_CAMERA_AFFINE_LINES*4u];
        for(size_t i=0;i<sizeof lines/sizeof lines[0];++i)lines[i]=(PortableCameraAffineLine){.dx=73,.dy=119,.pa=193,.pc=107};
        return portable_road_render(&road,lines,PORTABLE_CAMERA_AFFINE_LINES*frame->scale,frame)==PORTABLE_ROAD_RENDER_OK;
    }
    if(which==PARTICLE)return portable_race_particle_render_scaled(&f->particle,frame,120,110,2u,211u);
    if(which==IMPACT){
        PortableRaceParticleAssetView art[6];for(unsigned i=0;i<6u;++i)art[i]=f->particle;
        const PortableImpactEffects fx={.slots={{.x=120,.y=110,.inverse=211u,.age=2u,.active=1u}}};
        return portable_race_scene_render_impact_effects(art,&fx,frame);
    }
    if(which==COIN){
        PortableRaceParticleAssetView art[3];for(unsigned i=0;i<3u;++i)art[i]=f->particle;
        const PortableCoinEffects fx={.slots={{.kind=PORTABLE_COIN_EFFECT_BOUNCE,.visible=1u,.x=120,.y=110,.y_q16=110*65536+32768,.age=2u,.inverse=211u}}};
        return portable_race_scene_render_coin_effects(art,&fx,frame);
    }
    if(which==SPRAY){
        PortableRaceParticleAssetView art[5];for(unsigned i=0;i<5u;++i)art[i]=f->particle;
        const PortableSurfaceTrail trails={.extra_spray=1u,.visible=1u};
        const PortableSurfaceTrailRules rules={0};
        return portable_race_scene_render_surface_trails(art,&rules,&trails,&body,frame);
    }
    if(which==SPLASH)return portable_race_particle_render_scaled(&f->particle,frame,120,110,2u,211u);
    if(which==TRAIL){
        PortableRaceParticleAssetView art[5];for(unsigned i=0;i<5u;++i)art[i]=f->particle;
        const PortableSurfaceTrail trails={.visible=1u,.slots={{.x_q7=120*128+32,.y_q7=110*128+63,.age=2u,.kind=1u}}};
        const PortableSurfaceTrailRules rules={.frame_cutoffs={{9u,19u}}};
        return portable_race_scene_render_surface_trails(art,&rules,&trails,&body,frame);
    }
    if(which==LIGHTNING){
        PortableRaceParticleAssetView palette=f->particle;palette.palette_base=3u;
        const PortableLightningEffectAssetView lightning={.tiles=f->tiles,.tile_bytes=2048u,.frame_count=4u,.tile_base=0x1a0u};
        return portable_lightning_effect_render_positioned(&lightning,&palette,frame,
            120*65536+f->pan_q16,110*65536+f->pan_q16,0u);
    }
    if(which==MINIMAP){
        const PortableGpMinimapMarkerAssetView marker={.tiles=f->tiles,.palette=f->palette};
        const int32_t x[8]={(1<<19)+f->pan_q16*32},y[8]={(1<<20)+f->pan_q16*32};
        return portable_gp_results_render_minimap_markers(&marker,x,y,0u,0u,NULL,frame);
    }
    if(which==SKY_FLAT){
        PortableSkyAssetView sky=f->sky;sky.tiles=f->tiles;sky.tile_bytes=2048u;
        return portable_sky_render(&sky,frame,3,5);
    }
    PortableRaceSceneView scene=f->scene;
    PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,.camera_position_override=1u,
        .position_x_q16=8192,.position_y_q16=3*65536+16384,.suppress_player=1u,.suppress_native_hud=1u,
        .suppress_native_minimap=1u,.character_affine_scale=211u,.driver_affine_x=193u,.driver_affine_y=211u};
    PortableCourseSceneryView scenery=f->scenery;
    PortableCourseSceneryObject object=f->object;
    PortableCourseSceneryArt art=f->scenery_art[0];
    PortableWeatherAssetView weather={0};
    uint8_t transparent_tiles[4096]={0};
    PortableCharacterAssetView transparent_character=f->character;
    transparent_character.tiles=transparent_tiles;
    transparent_character.direction_tiles=transparent_tiles;
    PortableRecoveryFxRules recovery={0};
    if(which==SMOKE){scene.lava_smoke=&f->particle;state.lava_smoke.visible=1u;
        state.lava_smoke.puffs[0]=(PortableLavaSmokePuff){.dx=3,.dy=-4,.age=2u};
    }else if(which==THAW||which==DRIP){
        scene.recovery_fx_rules=&recovery;scene.recovery_thaw=&f->particle;scene.recovery_drip=&f->particle;
        state.recovery_fx.visible=1u;
        if(which==THAW)state.recovery_fx.thaw_age=2u;
        else state.recovery_fx.drips[0]=(PortableRecoveryDrip){.dx=3,.dy=4,.age=2u};
    }else if(which==CHARGED){scene.charged_exit_particle=&f->particle;state.charged_particle_age=2u;
    }else if(which==BOOST){scene.mushroom_burst=&f->particle;state.boost_particle_age=2u;
    }else if(which==BALLOON){
        scene.character=&transparent_character;scene.battle_balloon=&art.lod[0];
        state.show_battle=1u;state.balloons_remaining=1u;state.suppress_player=0u;
    }else if(which==BOMB){
        scene.battle_bomb=&f->character;state.revenge_bomb=1u;state.suppress_player=0u;
    }else if(which==KART)state.suppress_player=0u;
    else if(which==CPU){scene.opponent_count=1u;scene.opponent_characters[0]=&f->character;
        state.opponent_count=1u;state.opponents[0]=(PortableRaceOpponentPresentation){.visible=1u,.position_y_q16=state.position_y_q16,
            .character_affine_scale=211u,.driver_affine_x=193u,.driver_affine_y=211u};}
    else if(which==GHOST){state.ghost_visible=1u;state.ghost_position_y_q16=state.position_y_q16;state.ghost_affine_scale=211u;
        state.ghost_affine_x=193u;state.ghost_affine_y=211u;}
    else if(which>=SCENERY_ARGB&&which<=PIPE){
        scenery.objects=&object;scenery.native_art=&art;
        if(which==SCENERY_INDEXED)for(unsigned lod=0u;lod<4u;++lod){
            art.lod[lod].pixels=f->indices;art.lod[lod].palette=f->palette_bytes;art.lod[lod].palette_count=16u;}
        if(which==SCENERY_TILES){scenery.native_art=NULL;scenery.native_art_count=0u;}
        if(which==PIPE)object.source_code=0x10000u;
        scene.scenery=&scenery;
    }else if(which==ROTATED_ACTOR){
        scene.falling_star_art=f->scenery_art;state.falling_stars.count=1u;
        state.falling_stars.actors[0]=(PortableFallingStar){.x_q16=8192,.y_q16=state.position_y_q16,
            .flags=1u,.affine_q8=211u,.angle=8192u};
    }else if(which==BOX){state.item_box_count=1u;
        state.item_boxes[0]=(PortableRaceItemBoxPresentation){.position_y_q16=state.position_y_q16,.active=1u};
    }else if(which>=GREEN&&which<=BANANA){
        PortableRaceParticleAssetView item=f->particle;item.tile_base=0x100u;item.palette_base=3u;
        item.cells=shell_test_cells();item.cell_bytes=304u;
        scene.charged_exit_particle=&item;
        /* Item banks3/4/5 share authored colors and independently authored cells. */
        for(unsigned bank=1u;bank<4u;++bank)memcpy(f->palette+16u*bank,f->palette,32u);
        const PortableRaceItemPresentation value={.y_q16=state.position_y_q16,.visible=1u,.kind=0x8402u};
        if(which==GREEN)state.green_shells[0]=value;
        else if(which==RED)state.red_shells[0]=value;
        else state.bananas[0]=value;
        return portable_race_scene_render(&scene,&state,frame);
    }else if(which==WEATHER){
        weather.images[0]=(PortableNativeImage){.pixels=f->rgba,.width=8u,.height=8u};scene.weather_profile=&weather;
        state.snow_weather.enabled=1u;state.snow_weather.count=1u;state.snow_weather.precise=1u;
        state.snow_weather.particles[0]=(PortableSnowWeatherSprite){.x=120u,.y=90u,.visible=1u,
            .active=1u,.x_q16=120*65536+f->pan_q16,.y_q16=90*65536+f->pan_q16};
    }
    const PortableRacePresentationState before=state;
    const int result=portable_race_scene_render(&scene,&state,frame);
    return result&&!memcmp(&state,&before,sizeof state);
}
static int frame_depth_writes(void)
{
    for(unsigned scale=1u;scale<=4u;scale*=2u){
        const size_t stride=3u*scale+3u,zstride=3u*scale+5u,rows=2u*scale;
        uint32_t pixels[120],depth_pixels[136];
        for(size_t p=0;p<120u;++p)pixels[p]=0xdeadbeefu;
        for(size_t p=0;p<136u;++p)depth_pixels[p]=0xcafebabeu;
        for(size_t y=0;y<rows;++y)for(size_t x=0;x<3u*scale;++x){
            pixels[y*stride+x]=0x00112233u;depth_pixels[y*zstride+x]=UINT32_MAX;
        }
        PortableSoftwareDepth depth={depth_pixels,zstride,100u,NULL};
        PortableSoftwareFrame frame={.pixels=pixels,.width=3u,.height=2u,
            .scale=(uint8_t)scale,.stride=stride,.depth=&depth};
        portable_software_frame_store_physical(&frame,0u,0u,0x00010203u);
        CHECK(pixels[0]==0x00112233u&&depth_pixels[0]==UINT32_MAX);
        depth_pixels[scale*zstride+scale]=50u;
        portable_software_frame_store(&frame,4u,0xffaabbccu);
        for(size_t y=scale;y<2u*scale;++y)for(size_t x=scale;x<2u*scale;++x){
            const int blocked=y==scale&&x==scale;
            CHECK(pixels[y*stride+x]==(blocked?0x00112233u:0xffaabbccu));
            CHECK(depth_pixels[y*zstride+x]==(blocked?50u:100u));
        }
        depth.value=200u;portable_software_frame_store(&frame,4u,0xffabcdefu);
        CHECK(pixels[scale*stride+scale]==0x00112233u);
        for(size_t y=scale;y<2u*scale;++y)for(size_t x=scale;x<2u*scale;++x)
            CHECK(pixels[y*stride+x]==(y==scale&&x==scale?0x00112233u:0xffaabbccu));
        depth.value=100u;portable_software_frame_store(&frame,4u,0xff112244u);
        for(size_t y=scale;y<2u*scale;++y)for(size_t x=scale;x<2u*scale;++x)
            CHECK(pixels[y*stride+x]==(y==scale&&x==scale?0x00112233u:0xff112244u));
        depth.value=25u;portable_software_frame_store(&frame,4u,0xff020406u);
        for(size_t y=scale;y<2u*scale;++y)for(size_t x=scale;x<2u*scale;++x){
            CHECK(pixels[y*stride+x]==0xff020406u);
            CHECK(depth_pixels[y*zstride+x]==25u);
        }
        depth.value=40u;portable_software_frame_add_half(&frame,4u,0xff040608u);
        CHECK(pixels[scale*stride+scale]==0xff020406u);
        depth.value=25u;portable_software_frame_add_half(&frame,4u,0xff040608u);
        for(size_t y=scale;y<2u*scale;++y)for(size_t x=scale;x<2u*scale;++x)
            CHECK(pixels[y*stride+x]==0xff04070au);
        portable_software_frame_add_half(&frame,4u,0x00040810u);
        CHECK(pixels[scale*stride+scale]==0xff04070au);
        /* Equal depth accepts painter-order ties; partial alpha is nonzero. */
        portable_software_frame_store_physical(&frame,scale,scale,0x01070809u);
        CHECK(pixels[scale*stride+scale]==0x01070809u);
        portable_software_frame_store_rgb_physical(&frame,0u,0u,0u);
        CHECK(pixels[0]==0xff000000u&&depth_pixels[0]==25u);
        depth_pixels[0]=UINT32_MAX;
        depth.value=0u;portable_software_frame_store_rgb_physical(&frame,0u,0u,0x00030405u);
        CHECK(pixels[0]==0x00030405u&&depth_pixels[0]==UINT32_MAX);
        PortableSoftwareFrame overlay=portable_software_frame_native_view(&frame,PORTABLE_FRAME_CENTER);
        CHECK(overlay.depth==NULL&&frame.depth==&depth);
        for(size_t y=0;y<rows;++y){
            for(size_t x=3u*scale;x<stride;++x)CHECK(pixels[y*stride+x]==0xdeadbeefu);
            for(size_t x=3u*scale;x<zstride;++x)CHECK(depth_pixels[y*zstride+x]==0xcafebabeu);
        }
        /* An attached inactive depth view has exactly the legacy RGB path. */
        uint32_t inactive[120],ordinary[120];
        memset(inactive,0,sizeof inactive);memset(ordinary,0,sizeof ordinary);
        frame.pixels=inactive;
        PortableSoftwareFrame plain=frame;plain.pixels=ordinary;plain.depth=NULL;
        portable_software_frame_store(&frame,4u,0x00123456u);
        portable_software_frame_store(&plain,4u,0x00123456u);
        portable_software_frame_add_half(&frame,4u,0x00040608u);
        portable_software_frame_add_half(&plain,4u,0x00040608u);
        CHECK(!memcmp(inactive,ordinary,sizeof inactive));
        CHECK(portable_software_frame_init(&frame,pixels,3u,2u)&&frame.depth==NULL);
    }
    uint32_t wide_pixels[1492u*4u]={0},wide_depth[1492u*4u]={0};
    PortableSoftwareDepth depth={wide_depth,1492u,1u,NULL};
    PortableSoftwareFrame wide={.pixels=wide_pixels,.width=373u,.height=1u,.scale=4u,.depth=&depth};
    for(unsigned anchor=0;anchor<3u;++anchor){
        PortableSoftwareFrame view=portable_software_frame_native_view(&wide,(PortableFrameAnchor)anchor);
        CHECK(view.depth==NULL&&view.width==240u&&view.stride==1492u);
        portable_software_frame_store(&view,0u,0x00112233u);
    }
    for(size_t p=0;p<1492u*4u;++p)CHECK(wide_depth[p]==0u);
    return 0;
}

/* Each authored texture has a distinct color. This checks the selected art
 * independently of its sampling equations, while the complete coverage mask
 * checks that texture policy cannot change the source cell's geometry. */
static int artwork_lod_policy(void)
{
    /* Equal-size custom images are not a detail upgrade. Keep the source
     * image, even when the physical footprint exceeds all authored sizes. */
    const unsigned equal_sizes[4]={8u,8u,8u,8u};
    const unsigned equal_highest[4]={64u,64u,16u,8u};
    for(uint8_t policy=0u;policy<3u;++policy){
        CHECK(portable_artwork_lod_select(policy,equal_sizes,4u,2u,64u)==2u);
        CHECK(portable_artwork_lod_select(policy,equal_highest,4u,1u,16u)==1u);
    }
    CHECK(portable_artwork_lod_select(1u,equal_highest,4u,2u,16u)==0u);
    CHECK(portable_artwork_lod_select(2u,equal_highest,4u,2u,16u)==0u);
    uint8_t full[PORTABLE_CHARACTER_FRAME_BYTES],mid[PORTABLE_CHARACTER_LOD32_FRAME_BYTES];
    uint8_t far[PORTABLE_CHARACTER_LOD16_FRAME_BYTES],slots[64]={0};
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    memset(full,0x33,sizeof full);
    memset(mid,0x22,sizeof mid);
    memset(far,0x11,sizeof far);
    const PortableCharacterAssetView character={.tiles=full,.tile_bytes=sizeof full,
        .palette=palette,.palette_count=16u,.width=64u,.height=64u,
        .direction_tiles=full,.direction_tile_bytes=sizeof full,.direction_frame_count=1u,
        .lod32_tiles=mid,.lod16_tiles=far,.view_frame_slots=slots,.view_frame_slot_count=64u};
    PortableCharacterAssetView before;
    memcpy(&before,&character,sizeof before);
    static const struct {
        uint8_t size,scale;
        int16_t inverse;
        uint8_t texture[3];
    } cases[]={
        {16u,1u,512,{16u,32u,64u}},
        {16u,2u,512,{16u,32u,64u}},
        {16u,4u,512,{32u,64u,64u}},
        {16u,4u,511,{64u,64u,64u}},
        {16u,4u,1024,{16u,32u,64u}},
        {32u,1u,512,{32u,64u,64u}},
        {32u,4u,1024,{32u,64u,64u}},
        {64u,4u,512,{64u,64u,64u}}
    };
    for(size_t c=0;c<sizeof cases/sizeof cases[0];++c)
    for(unsigned wide=0u;wide<2u;++wide){
        const size_t width=wide?373u:240u,scale=cases[c].scale;
        const size_t stride=width*scale+PAD,count=stride*HEIGHT*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels);
        uint8_t *coverage=calloc(count,sizeof *coverage);
        CHECK(pixels&&coverage);
        for(unsigned policy=0u;policy<3u;++policy){
            for(size_t p=0;p<count;++p)pixels[p]=0xdeadbeefu;
            PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=HEIGHT,
                .scale=(uint8_t)scale,.stride=stride,.artwork_lod=(uint8_t)policy};
            portable_software_frame_clear(&frame,0x101828u);
            CHECK(portable_character_render_slot_oam_lod(&character,&frame,-1,48,0u,
                cases[c].size,cases[c].inverse,cases[c].inverse,
                (int16_t)(cases[c].size/2u),1));
            const uint32_t expected=cases[c].texture[policy]==16u?0xff0000u:
                cases[c].texture[policy]==32u?0x00ff00u:0x0000ffu;
            size_t painted=0u;
            for(size_t y=0;y<HEIGHT*scale;++y)for(size_t x=0;x<stride;++x){
                const size_t p=y*stride+x;
                if(x>=width*scale){CHECK(pixels[p]==0xdeadbeefu);continue;}
                const int ink=pixels[p]!=0x101828u;
                if(ink){CHECK(pixels[p]==expected);++painted;}
                if(policy==0u)coverage[p]=(uint8_t)ink;
                else CHECK(coverage[p]==ink);
            }
            CHECK(painted>0u);
            CHECK(!memcmp(&character,&before,sizeof before));
        }
        free(coverage);free(pixels);
    }
    for(size_t p=0;p<sizeof full;++p)CHECK(full[p]==0x33u);
    for(size_t p=0;p<sizeof mid;++p)CHECK(mid[p]==0x22u);
    for(size_t p=0;p<sizeof far;++p)CHECK(far[p]==0x11u);
    /* A sparse custom driver has one full-size pose and no alternate LODs.
     * All policies must retain its transparent texels and exact output. */
    for(size_t p=0;p<sizeof full;p+=3u)full[p]=0u;
    uint8_t full_before[sizeof full];memcpy(full_before,full,sizeof full);
    PortableCharacterAssetView sparse=character;
    sparse.lod16_tiles=NULL;sparse.lod32_tiles=NULL;
    for(unsigned scale=1u;scale<=4u;scale*=4u){
        const size_t stride=373u*scale+PAD,count=stride*HEIGHT*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels),*reference=malloc(count*sizeof *reference);
        CHECK(pixels&&reference);
        for(unsigned policy=0;policy<3u;++policy){
            for(size_t p=0;p<count;++p)pixels[p]=0xdeadbeefu;
            PortableSoftwareFrame frame={.pixels=pixels,.width=373u,.height=HEIGHT,
                .scale=(uint8_t)scale,.stride=stride,.artwork_lod=(uint8_t)policy};
            portable_software_frame_clear(&frame,0x101828u);
            CHECK(portable_character_render_slot_oam_lod(&sparse,&frame,120,60,0u,16u,
                -385,385,8,1));
            if(policy==0u)memcpy(reference,pixels,count*sizeof *pixels);
            else CHECK(!memcmp(reference,pixels,count*sizeof *pixels));
        }
        CHECK(!memcmp(full,full_before,sizeof full));
        free(reference);free(pixels);
    }
    CHECK(mid[0]==0x22u&&mid[sizeof mid-1u]==0x22u);
    CHECK(far[0]==0x11u&&far[sizeof far-1u]==0x11u);
    /* Special poses have no corresponding entry in the 25-frame LOD banks. */
    static uint8_t poses[26u*PORTABLE_CHARACTER_FRAME_BYTES];
    static uint8_t poses_mid[25u*PORTABLE_CHARACTER_LOD32_FRAME_BYTES];
    static uint8_t poses_far[25u*PORTABLE_CHARACTER_LOD16_FRAME_BYTES];
    memset(poses,0x33,sizeof poses);
    memset(poses_mid,0x22,sizeof poses_mid);memset(poses_far,0x11,sizeof poses_far);
    sparse.direction_tiles=poses;sparse.direction_tile_bytes=sizeof poses;
    sparse.direction_frame_count=26u;sparse.lod32_tiles=poses_mid;sparse.lod16_tiles=poses_far;
    uint32_t pose_pixels[67u*64u*4u];
    for(unsigned policy=0;policy<3u;++policy){
        PortableSoftwareFrame frame={.pixels=pose_pixels,.width=16u,.height=64u,
            .scale=4u,.stride=67u,.artwork_lod=(uint8_t)policy};
        portable_software_frame_clear(&frame,0x101828u);
        CHECK(portable_character_render_slot_oam_lod(&sparse,&frame,0,0,25u,16u,512,512,8,1));
        size_t painted=0u;
        for(size_t y=0;y<256u;++y)for(size_t x=0;x<64u;++x){
            const uint32_t color=pose_pixels[y*67u+x];
            CHECK(color==0x101828u||color==0x0000ffu);
            painted+=color==0x0000ffu;
        }
        CHECK(painted>0u);
    }
    return 0;
}

static int artwork_lod_native_fractional_sampling(void)
{
    uint8_t tiles[2048]={0},low_tiles[128]={0},slots[64]={0};
    const uint16_t palette[16]={0u,31u,31u<<5u,31u<<10u};
    const uint32_t colors[4]={0x101828u,0xff0000u,0x00ff00u,0x0000ffu};
    for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x){
        const unsigned index=(x/5u+y/7u)%4u;
        const size_t offset=((y/8u)*8u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        tiles[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x){
        const unsigned index=(4u*x/5u+4u*y/7u)%4u;
        const size_t offset=((y/8u)*2u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        low_tiles[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    uint8_t before[sizeof tiles];memcpy(before,tiles,sizeof before);
    const PortableCharacterAssetView character={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,.width=64u,.height=64u,
        .direction_tiles=tiles,.direction_tile_bytes=sizeof tiles,.direction_frame_count=1u,
        .lod16_tiles=low_tiles,.view_frame_slots=slots,.view_frame_slot_count=64u};
    uint32_t pixels[67u*64u],original[67u*64u];
    for(unsigned policy=0;policy<3u;++policy){
        for(size_t p=0;p<67u*64u;++p)pixels[p]=0xdeadbeefu;
        PortableSoftwareFrame frame={.pixels=pixels,.width=64u,.height=64u,
            .scale=1u,.stride=67u,.artwork_lod=(uint8_t)policy};
        portable_software_frame_clear(&frame,0x101828u);
        CHECK(portable_character_render_slot_oam_lod(&character,&frame,10,20,0u,16u,384,384,8,1));
        unsigned differing=0u,painted=0u;
        for(int y=0;y<64;++y)for(int x=0;x<67;++x){
            uint32_t expected=x>=64?0xdeadbeefu:0x101828u;
            const int dx=x-10-8,dy=y-20-8;
            /* Source16 geometry uses floor(3*delta/2). Enhanced64 samples
             * floor(6*delta) directly, retaining the intervening texels. */
            const int nx=8+(dx>=0?3*dx/2:-((-3*dx+1)/2));
            const int ny=8+(dy>=0?3*dy/2:-((-3*dy+1)/2));
            const int sx=policy?32+6*dx:4*nx,sy=policy?32+6*dy:4*ny;
            if(x>=10&&x<26&&y>=20&&y<36&&nx>=0&&nx<16&&ny>=0&&ny<16&&
                sx>=0&&sx<64&&sy>=0&&sy<64)
                expected=colors[(sx/5+sy/7)%4];
            CHECK(pixels[(size_t)y*67u+(size_t)x]==expected);
            painted+=expected!=0x101828u&&expected!=0xdeadbeefu;
            if(policy)differing+=pixels[(size_t)y*67u+(size_t)x]!=original[(size_t)y*67u+(size_t)x];
        }
        CHECK(painted>0u);
        if(policy)CHECK(differing>0u);
        else memcpy(original,pixels,sizeof original);
        CHECK(!memcmp(before,tiles,sizeof before));
    }
    return 0;
}

static int artwork_lod_scenery(void)
{
    static Fixture fixture;fixture_init(&fixture);
    static uint8_t rgba[4][64u*64u*4u],indices[4][64u*64u],tiles[4][2048u];
    const uint32_t colors[4]={0xff0000ffu,0xff00ff00u,0xffff0000u,0xffffff00u};
    fixture.palette[4]=31u|(31u<<5u);
    for(unsigned p=0;p<32u;++p)
        fixture.palette_bytes[p]=(uint8_t)(fixture.palette[p/2u]>>((p&1u)*8u));
    for(unsigned lod=0;lod<4u;++lod){
        const unsigned size=64u>>lod,index=lod==0u?3u:lod==1u?2u:lod==2u?1u:4u;
        memset(tiles[lod],(int)(index*17u),sizeof tiles[lod]);
        memset(indices[lod],(int)index,sizeof indices[lod]);
        for(unsigned p=0;p<size*size;++p)for(unsigned byte=0;byte<4u;++byte)
            rgba[lod][p*4u+byte]=(uint8_t)(colors[lod]>>(byte*8u));
    }
    PortableCameraState camera;PortableCameraProjection projection;
    portable_camera_state_update(&camera,0,0,0u);
    CHECK(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection)==0);
    int found=0;int32_t world_y=0;
    for(int quarter=-8192;quarter<=8192&&!found;++quarter){
        PortableCameraProjectionResult body;
        if(!portable_camera_projection_project(&camera,&projection,0,quarter*16384,0,&body)&&body.distance==1536){
            found=1;world_y=quarter*16384;
        }
    }
    CHECK(found);
    for(unsigned kind=0;kind<4u;++kind)for(unsigned scale=1u;scale<=4u;scale*=4u)
    for(unsigned sparse=0;sparse<2u;++sparse){
        PortableCourseSceneryArt art={0};
        for(unsigned lod=0;lod<4u;++lod){
            const unsigned size=64u>>lod;
            art.lod[lod]=(PortableNativeImage){.pixels=kind==1u?indices[lod]:rgba[lod],
                .palette=kind==1u?fixture.palette_bytes:NULL,.palette_count=kind==1u?16u:0u,
                .width=(uint16_t)size,.height=(uint16_t)size,
                .anchor_x=-(int16_t)(size/2u),.anchor_y=-(int16_t)(size/2u)};
            art.distance_shift[lod]=(uint8_t)lod;
        }
        if(sparse)art.lod[1].pixels=NULL;
        const PortableCourseSceneryObject object={.position_y_q16=world_y,.art_index=0u};
        const PortableCourseSceneryView scenery={.objects=&object,.object_count=1u,
            .native_art=kind==2u?NULL:&art,.native_art_count=kind==2u?0u:1u,
            .palette=fixture.palette,.tiles=tiles[2],.lod64_tiles=tiles[0],
            .lod32_tiles=sparse?NULL:tiles[1],.lod8_tiles=tiles[3]};
        PortableRaceSceneView scene=fixture.scene;
        scene.scenery=kind==3u?NULL:&scenery;
        scene.falling_star_art=kind==3u?&art:NULL;
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
            .camera_position_override=1u,.suppress_player=1u,.suppress_native_hud=1u,.suppress_native_minimap=1u};
        if(kind==3u){
            state.falling_stars.count=1u;
            state.falling_stars.actors[0]=(PortableFallingStar){.y_q16=world_y,.flags=1u,
                .affine_q8=256u,.angle=8192u};
        }
        PortableRacePresentationState state_before;memcpy(&state_before,&state,sizeof state_before);
        PortableCourseSceneryArt art_before;memcpy(&art_before,&art,sizeof art_before);
        const size_t stride=373u*scale+PAD,count=stride*HEIGHT*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels);
        uint8_t *coverage=calloc(count,sizeof *coverage);CHECK(pixels&&coverage);
        for(unsigned policy=0;policy<3u;++policy){
            for(size_t p=0;p<count;++p)pixels[p]=0xdeadbeefu;
            PortableSoftwareFrame frame={.pixels=pixels,.width=373u,.height=HEIGHT,
                .scale=(uint8_t)scale,.stride=stride,.artwork_lod=(uint8_t)policy};
            CHECK(portable_race_scene_render(&scene,&state,&frame));
            const uint32_t expected=policy==0u?0xff0000u:
                policy==1u&&scale==1u&&!sparse?0x00ff00u:0x0000ffu;
            size_t painted=0u;
            for(size_t y=0;y<HEIGHT*scale;++y)for(size_t x=0;x<stride;++x){
                const size_t p=y*stride+x;const uint32_t rgb=pixels[p]&0xffffffu;
                if(x>=373u*scale){CHECK(pixels[p]==0xdeadbeefu);continue;}
                const int ink=rgb==0xff0000u||rgb==0x00ff00u||rgb==0x0000ffu||rgb==0xffff00u;
                if(ink){CHECK(rgb==expected);++painted;}
                if(!policy)coverage[p]=(uint8_t)ink;
                else CHECK(coverage[p]==ink);
            }
            CHECK(painted>0u);
            CHECK(!memcmp(&state_before,&state,sizeof state));
            CHECK(!memcmp(&art_before,&art,sizeof art));
        }
        free(coverage);free(pixels);
    }
    return 0;
}

static int artwork_lod_race_roles(void)
{
    static Fixture fixture;fixture_init(&fixture);
    uint8_t full[2048],mid[512],far[128];
    memset(full,0x33,sizeof full);memset(mid,0x22,sizeof mid);memset(far,0x11,sizeof far);
    fixture.character.direction_tiles=full;fixture.character.tiles=full;
    fixture.character.direction_frame_count=1u;fixture.character.direction_tile_bytes=sizeof full;
    fixture.character.lod32_tiles=mid;fixture.character.lod16_tiles=far;
    fixture.scene.opponent_characters[0]=&fixture.character;
    PortableCameraState camera;PortableCameraProjection projection;
    portable_camera_state_update(&camera,0,0,0u);
    CHECK(portable_camera_projection_build(&camera,0,32768,31414,9319,&projection)==0);
    int found=0;int32_t world_y=0;
    for(int quarter=-8192;quarter<=8192&&!found;++quarter){
        PortableCameraProjectionResult body;
        if(!portable_camera_projection_project(&camera,&projection,0,quarter*16384,0,&body)&&body.distance==1024){
            found=1;world_y=quarter*16384;
        }
    }
    CHECK(found);
    for(unsigned scale=1u;scale<=4u;scale*=4u)for(unsigned policy=0;policy<3u;++policy){
        const size_t stride=373u*scale+PAD,count=stride*HEIGHT*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels),*reference=malloc(count*sizeof *reference);
        CHECK(pixels&&reference);
        PortableSoftwareFrame frame={.pixels=pixels,.width=373u,.height=HEIGHT,
            .scale=(uint8_t)scale,.stride=stride,.artwork_lod=(uint8_t)policy};
        const uint32_t expected=scale==4u||policy==2u?0x0000ffu:policy==1u?0x00ff00u:0xff0000u;
        for(unsigned role=0;role<3u;++role){
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                .camera_position_override=1u,.position_y_q16=world_y,.character_affine_scale=256u,
                .suppress_player=(uint8_t)(role!=0u),.suppress_native_hud=1u,.suppress_native_minimap=1u,
                .opponent_count=(uint8_t)(role==1u),.ghost_visible=(uint8_t)(role==2u),
                .ghost_position_y_q16=world_y,.ghost_affine_scale=256u};
            state.opponents[0]=(PortableRaceOpponentPresentation){.visible=1u,.position_y_q16=world_y,
                .character_affine_scale=256u};
            fixture.scene.opponent_count=(uint8_t)(role==1u);
            PortableRacePresentationState before;memcpy(&before,&state,sizeof before);
            for(size_t p=0;p<count;++p)pixels[p]=0xdeadbeefu;
            CHECK(portable_race_scene_render(&fixture.scene,&state,&frame));
            CHECK(!memcmp(&before,&state,sizeof before));
            size_t painted=0u;
            for(size_t y=0;y<HEIGHT*scale;++y)for(size_t x=0;x<stride;++x){
                const uint32_t color=pixels[y*stride+x];
                if(x>=373u*scale){CHECK(color==0xdeadbeefu);continue;}
                if(color==0xff0000u||color==0x00ff00u||color==0x0000ffu){
                    CHECK(color==expected);++painted;
                }
            }
            CHECK(painted>0u);
            if(role==0u)memcpy(reference,pixels,count*sizeof *pixels);
            else CHECK(!memcmp(reference,pixels,count*sizeof *pixels));
        }
        free(reference);free(pixels);
    }
    return 0;
}

/* Moving art may retain source texels, but its anchor must move by one
 * physical pixel when the source position moves by a quarter pixel at 4x. */
static int subpixel_pan(void)
{
    static Fixture fixture;
    const Case layers[] = {SKY_FLAT, WEATHER, LIGHTNING, MINIMAP};
    for (size_t layer = 0; layer < sizeof layers / sizeof layers[0]; ++layer)
    {
        fixture_init(&fixture);
        for (unsigned scale = 1; scale <= 4; scale += 3)
        {
            const size_t stride = WIDTH * scale + PAD;
            const size_t count = stride * HEIGHT * scale;
            uint32_t *before = malloc(count * sizeof *before);
            uint32_t *after = malloc(count * sizeof *after);
            CHECK(before && after);
            uint32_t *images[] = {before, after};
            for (unsigned step = 0; step < 2; ++step)
            {
                fixture.pan_q16 = (int32_t)step * 16384;
                for (size_t pixel = 0; pixel < count; ++pixel)
                    images[step][pixel] = 0xdeadbeefu;
                PortableSoftwareFrame frame = {.pixels=images[step], .width=WIDTH,
                    .height=HEIGHT, .scale=(uint8_t)scale, .stride=stride};
                if (layers[layer] == SKY_FLAT)
                {
                    PortableSkyAssetView sky = fixture.sky;
                    sky.tiles = fixture.tiles;
                    sky.tile_bytes = 2048;
                    const PortableSkyMotionSnapshot motion = {.initialized=1, .precise=1,
                        .layer_mask=4, .layer_source={0,1,2}, .split_y=-1, .road_line=74,
                        .scroll_x={0,0,3}, .scroll_y={0,0,5},
                        .scroll_x_q16={0,0,3*65536+fixture.pan_q16},
                        .scroll_y_q16={0,0,5*65536+fixture.pan_q16}};
                    portable_software_frame_clear(&frame, 0x101828u);
                    CHECK(portable_sky_render_motion_back(&sky, &frame, &motion));
                }
                else
                    CHECK(draw(&fixture, layers[layer], &frame));
                for (unsigned row = 0; row < HEIGHT * scale; ++row)
                    for (size_t x = WIDTH * scale; x < stride; ++x)
                        CHECK(images[step][row * stride + x] == 0xdeadbeefu);
            }
            const int changed = memcmp(before, after, count * sizeof *before) != 0;
            CHECK(scale == 1 ? !changed : changed);
            printf("subpixel pan layer=%s scale=%u moved=%d\n", names[layers[layer]], scale, changed);
            free(before);
            free(after);
        }
    }
    puts("portable subpixel pan: PASS sky weather lightning markers; native unchanged");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--subpixel-pan") == 0)
        return subpixel_pan();
    if (argc != 1)
        return 2;
    CHECK(frame_depth_writes()==0);
    CHECK(artwork_lod_policy()==0);
    CHECK(artwork_lod_native_fractional_sampling()==0);
    CHECK(artwork_lod_scenery()==0);
    CHECK(artwork_lod_race_roles()==0);
    static Fixture fixture;
    for(unsigned which=0u;which<CASE_COUNT;++which){
        printf("checking transformed layer=%s\n",names[which]);
        fflush(stdout);
        fixture_init(&fixture);
        uint32_t *images[2]={NULL,NULL};
        size_t strides[2]={WIDTH+PAD,WIDTH*4u+PAD};
        for(unsigned pass=0u;pass<2u;++pass){
            const unsigned scale=pass?4u:1u;
            const size_t count=strides[pass]*HEIGHT*scale;
            images[pass]=malloc(count*sizeof *images[pass]);CHECK(images[pass]);
            for(size_t i=0u;i<count;++i)images[pass][i]=0xdeadbeefu;
            PortableSoftwareFrame frame={.pixels=images[pass],.width=WIDTH,.height=HEIGHT,.scale=(uint8_t)scale,.stride=strides[pass]};
            CHECK(draw(&fixture,(Case)which,&frame));
            unsigned painted=0u;
            for(unsigned y=0u;y<HEIGHT*scale;++y){
                for(unsigned x=0u;x<WIDTH*scale;++x){
                    const uint32_t color=images[pass][y*strides[pass]+x]&0xffffffu;
                    painted+=(color==0xff0000u||color==0x00ff00u||color==0x0000ffu);
                }
                for(unsigned x=WIDTH*scale;x<strides[pass];++x)CHECK(images[pass][y*strides[pass]+x]==0xdeadbeefu);
            }
            CHECK(painted>0u);
        }
        unsigned differing=0u;
        for(unsigned y=0u;y<HEIGHT*4u;++y)for(unsigned x=0u;x<WIDTH*4u;++x)
            differing+=images[1][y*strides[1]+x]!=images[0][(y/4u)*strides[0]+x/4u];
        const int flat=which==WEATHER||which==SKY_FLAT||which==LIGHTNING;
        printf("transformed layer=%s sampling=%s differing=%u\n",names[which],flat?"flat-source-by-design":"physical",differing);
        CHECK(flat?differing==0u:differing>0u);
        free(images[0]);free(images[1]);
    }
    puts("portable transformed resolution: PASS");return 0;
}
