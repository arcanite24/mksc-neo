#include "race_scene_renderer.h"
#include "scene_3d.h"
#include "banana_renderer.h"
#include "boo_attack_renderer.h"
#include "green_shell_renderer.h"
#include "red_shell_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"race depth line%d: %s\n",__LINE__,#c);return 1;} } while(0)
#define GUARD UINT32_C(0xdeadbeef)
#define KART UINT32_C(0x00ff00)
#define SCENERY UINT32_C(0xff0000)
#define BOX UINT32_C(0xffff00)

typedef struct {
    uint8_t map[1],road_tiles[64],sky_map[2048],sky_tiles[32],slots[64];
    uint8_t kart_tiles[2048],box_tiles[PORTABLE_ITEM_BOX_TILE_BYTES];
    uint8_t opaque[64u*128u*4u],cutout[64u*128u*4u];
    uint16_t road_palette[16],kart_palette[16],box_palette[16];
    int32_t sine[1024];
    PortableSkyAssetView sky;
    PortableCharacterAssetView character;
    PortableItemBoxAssetView box;
    PortableDriverProjectionRules projection_rules;
    PortableCourseSceneryArt art;
    PortableCourseSceneryObject object;
    PortableCourseSceneryView scenery;
    PortableScene3D depth;
    PortableRaceSceneView scene;
    PortableCameraState camera;
    PortableCameraProjection camera_basis;
} Fixture;

static int fixture_init(Fixture *f)
{
    memset(f,0,sizeof *f);
    memset(f->road_tiles,1,sizeof f->road_tiles);
    memset(f->kart_tiles,0x11,sizeof f->kart_tiles);
    memset(f->box_tiles,0x11,sizeof f->box_tiles);
    f->road_palette[1]=0x4210u;f->kart_palette[1]=31u<<5u;
    f->box_palette[1]=31u|(31u<<5u);
    for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<64u;++x){
        const size_t p=((size_t)y*64u+x)*4u;
        f->opaque[p+2u]=255u;f->opaque[p+3u]=255u;
        memcpy(f->cutout+p,f->opaque+p,4u);
        if(((x/8u+y/8u)&1u)==0u)f->cutout[p+3u]=0u;
    }
    f->sine[256]=32768;f->sine[768]=-32768;
    f->sky=(PortableSkyAssetView){.tiles=f->sky_tiles,.tile_bytes=32u,
        .palette=f->road_palette,.palette_count=16u,.front_map=f->sky_map,
        .mid_map=f->sky_map,.back_map=f->sky_map,.front_map_bytes=2048u,
        .mid_map_bytes=2048u,.back_map_bytes=2048u,.front_width_tiles=32u,
        .mid_width_tiles=32u,.back_width_tiles=32u,.map_height_tiles=32u};
    f->character=(PortableCharacterAssetView){.tiles=f->kart_tiles,.tile_bytes=2048u,
        .palette=f->kart_palette,.palette_count=16u,.width=64u,.height=64u,
        .direction_tiles=f->kart_tiles,.direction_tile_bytes=2048u,.direction_frame_count=1u,
        .view_frame_slots=f->slots,.view_frame_slot_count=64u};
    f->box=(PortableItemBoxAssetView){.tiles=f->box_tiles,.tile_bytes=sizeof f->box_tiles,
        .palette=f->box_palette,.palette_count=16u,.frame_count=8u,.width=32u,.height=32u};
    f->projection_rules=(PortableDriverProjectionRules){{
        {511u,64u,-32,-46,1u},{1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    for(unsigned lod=0;lod<4u;++lod){
        f->art.lod[lod]=(PortableNativeImage){.pixels=f->opaque,.width=64u,.height=128u,
            .anchor_x=-32,.anchor_y=-32};
        f->art.distance_shift[lod]=(uint8_t)lod;
    }
    f->scenery=(PortableCourseSceneryView){.objects=&f->object,.object_count=1u,
        .native_art=&f->art,.native_art_count=1u};
    f->scene=(PortableRaceSceneView){.world_map={.map=f->map,.map_bytes=1u,.width_tiles=1u,.height_tiles=1u},
        .tileset={.tiles=f->road_tiles,.tile_bytes=64u,.palette=f->road_palette,.palette_count=16u},
        .sky=&f->sky,.character=&f->character,.driver_projection=&f->projection_rules,
        .sine=f->sine,.sine_count=1024u,.item_box=&f->box,.scene_3d=&f->depth};
    portable_camera_state_update(&f->camera,0,0,0u);
    CHECK(!portable_camera_projection_build(&f->camera,0,32768,31414,9319,&f->camera_basis));
    return 0;
}

static int find_depth(Fixture *f,int distance,int32_t *y,uint32_t *precise_depth)
{
    for(int quarter=-8192;quarter<=8192;++quarter){
        PortableCameraProjectionResult body;
        if(!portable_camera_projection_project(&f->camera,&f->camera_basis,0,quarter*16384,0,&body)&&
            body.distance==distance){
            PortableCameraProjectionPreciseResult precise;
            CHECK(!portable_camera_projection_project_precise(&f->camera,&f->camera_basis,
                0,quarter*16384,0,&precise));
            *y=quarter*16384;*precise_depth=(uint32_t)precise.distance_q16;return 0;
        }
    }
    CHECK(0);return 1;
}

static int draw(Fixture *f,PortableRacePresentationState *state,PortableSoftwareFrame *frame)
{
    PortableRacePresentationState before;memcpy(&before,state,sizeof before);
    PortableCourseSceneryArt art_before;memcpy(&art_before,&f->art,sizeof art_before);
    PortableSoftwareFrame frame_before;memcpy(&frame_before,frame,sizeof frame_before);
    const size_t count=portable_software_frame_stride(frame)*frame->height*portable_software_frame_scale(frame);
    for(size_t p=0;p<count;++p)frame->pixels[p]=GUARD;
    CHECK(portable_race_scene_render(&f->scene,state,frame));
    CHECK(frame->depth==NULL);
    CHECK(!memcmp(frame,&frame_before,sizeof frame_before));
    CHECK(!memcmp(state,&before,sizeof before)&&!memcmp(&f->art,&art_before,sizeof art_before));
    const size_t width=frame->width*portable_software_frame_scale(frame);
    for(size_t y=0;y<frame->height*portable_software_frame_scale(frame);++y)
        for(size_t x=width;x<portable_software_frame_stride(frame);++x)
            CHECK(frame->pixels[y*portable_software_frame_stride(frame)+x]==GUARD);
    return 0;
}

static int world_depth(void)
{
    static Fixture fixture;CHECK(!fixture_init(&fixture));
    int32_t target_y;uint32_t target_depth;
    CHECK(!find_depth(&fixture,256,&target_y,&target_depth));
    for(unsigned size=0;size<3u;++size){
        /* Return to native after a wide high-resolution target to exercise
         * reuse after both growth and shrink, without retaining frame borrows. */
        const unsigned scale=size==1u?4u:1u;
        const size_t width=size==1u?373u:240u,stride=width*scale+3u,rows=160u*scale,count=stride*rows;
        uint32_t *target=malloc(count*sizeof *target),*opaque=malloc(count*sizeof *opaque);
        uint32_t *cutout=malloc(count*sizeof *cutout),*pixels=malloc(count*sizeof *pixels);
        CHECK(target&&opaque&&cutout&&pixels);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        for(unsigned item=0;item<2u;++item)for(unsigned near=0;near<3u;++near){
            uint32_t scenery_depth;
            if(near==2u){
                /* The scenery enters the queue before the target. Quantized
                 * depth therefore draws the target last; precise depth must
                 * still protect the slightly nearer scenery's opaque pixels. */
                fixture.object.position_y_q16=target_y+1024;
                PortableCameraProjectionResult source;
                PortableCameraProjectionPreciseResult precise;
                CHECK(!portable_camera_projection_project(&fixture.camera,&fixture.camera_basis,
                    0,fixture.object.position_y_q16,0,&source));
                CHECK(source.distance==256);
                CHECK(!portable_camera_projection_project_precise(&fixture.camera,&fixture.camera_basis,
                    0,fixture.object.position_y_q16,0,&precise));
                scenery_depth=(uint32_t)precise.distance_q16;
                CHECK(scenery_depth<target_depth);
            }else CHECK(!find_depth(&fixture,near?192:512,&fixture.object.position_y_q16,&scenery_depth));
            PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
                .camera_position_override=1u,.position_y_q16=target_y,.character_affine_scale=256u,
                .suppress_player=(uint8_t)item,.suppress_native_hud=1u,.suppress_native_minimap=1u};
            if(item){state.item_box_count=1u;state.item_boxes[0]=(PortableRaceItemBoxPresentation){
                .position_y_q16=target_y,.active=1u};}
            fixture.scene.scenery=NULL;frame.pixels=target;CHECK(!draw(&fixture,&state,&frame));
            const uint32_t target_color=item?BOX:KART;
            size_t target_count=0u;
            for(size_t y=0;y<rows;++y)for(size_t x=0;x<width*scale;++x)
                if((target[y*stride+x]&0xffffffu)==target_color){
                    CHECK(fixture.depth.billboard_depth[y*width*scale+x]==target_depth);++target_count;
                }
            CHECK(target_count>0u);
            /* Native source cells contain wheels/shadows below the logical
             * world anchor. A flat course must not cut that authored art. */
            fixture.scene.scene_3d=NULL;frame.pixels=cutout;
            CHECK(!draw(&fixture,&state,&frame));
            fixture.scene.scene_3d=&fixture.depth;
            for(size_t y=0;y<rows;++y)for(size_t x=0;x<width*scale;++x){
                const size_t p=y*stride+x;
                CHECK(((cutout[p]&0xffffffu)==target_color)==
                    ((target[p]&0xffffffu)==target_color));
            }
            PortableRacePresentationState scenery_only=state;
            scenery_only.suppress_player=1u;scenery_only.item_box_count=0u;
            fixture.scene.scenery=&fixture.scenery;
            for(unsigned lod=0;lod<4u;++lod)fixture.art.lod[lod].pixels=fixture.opaque;
            frame.pixels=opaque;CHECK(!draw(&fixture,&scenery_only,&frame));
            for(unsigned lod=0;lod<4u;++lod)fixture.art.lod[lod].pixels=fixture.cutout;
            frame.pixels=cutout;CHECK(!draw(&fixture,&scenery_only,&frame));
            frame.pixels=pixels;CHECK(!draw(&fixture,&state,&frame));
            size_t overlap=0u,holes=0u,ground=0u;
            for(size_t y=0;y<rows;++y)for(size_t x=0;x<width*scale;++x){
                const size_t p=y*stride+x,z=y*width*scale+x;
                const int t=(target[p]&0xffffffu)==target_color;
                const int s=(cutout[p]&0xffffffu)==SCENERY;
                if(t&&s){
                    CHECK((pixels[p]&0xffffffu)==(near?SCENERY:target_color));
                    CHECK(fixture.depth.billboard_depth[z]==(near?scenery_depth:target_depth));++overlap;
                }
                if(t&&(opaque[p]&0xffffffu)==SCENERY&&!s){
                    CHECK((pixels[p]&0xffffffu)==target_color);++holes;
                }
                if((pixels[p]&0xffffffu)==0x848484u){CHECK(fixture.depth.depth[z]!=UINT32_MAX);++ground;}
            }
            CHECK(overlap>0u&&holes>0u&&ground>0u);
            memcpy(cutout,pixels,count*sizeof *pixels);
            CHECK(!draw(&fixture,&state,&frame));CHECK(!memcmp(cutout,pixels,count*sizeof *pixels));
            /* A failure after background setup must retire its depth borrow. */
            state.suppress_player=0u;
            fixture.character.palette_count=0u;
            CHECK(!portable_race_scene_render(&fixture.scene,&state,&frame));CHECK(frame.depth==NULL);
            fixture.character.palette_count=16u;
        }
        free(pixels);free(cutout);free(opaque);free(target);
    }
    portable_scene_3d_close(&fixture.depth);
    CHECK(!fixture.depth.frame&&!fixture.depth.depth);
    for(size_t p=0;p<sizeof fixture.kart_tiles;++p)CHECK(fixture.kart_tiles[p]==0x11u);
    for(size_t p=0;p<sizeof fixture.box_tiles;++p)CHECK(fixture.box_tiles[p]==0x11u);
    for(size_t p=0;p<sizeof fixture.road_tiles;++p)CHECK(fixture.road_tiles[p]==1u);
    CHECK(fixture.road_palette[1]==0x4210u&&fixture.kart_palette[1]==(31u<<5u));
    CHECK(fixture.box_palette[1]==(31u|(31u<<5u)));
    return 0;
}

static int unsupported_scene_fallback(void)
{
    static Fixture fixture;CHECK(!fixture_init(&fixture));
    memset(fixture.sine,0,sizeof fixture.sine);
    fixture.scene.character=NULL;fixture.scene.item_box=NULL;
    PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,
        .suppress_player=1u,.suppress_native_hud=1u,.suppress_native_minimap=1u};
    uint32_t original[240u*160u],fallback[240u*160u];
    PortableSoftwareFrame frame={.pixels=original,.width=240u,.height=160u};
    fixture.scene.scene_3d=NULL;
    CHECK(portable_race_scene_render(&fixture.scene,&state,&frame));
    fixture.scene.scene_3d=&fixture.depth;frame.pixels=fallback;
    CHECK(portable_race_scene_render(&fixture.scene,&state,&frame));
    CHECK(frame.depth==NULL && fixture.depth.frame==NULL && fixture.depth.fallback_reported);
    CHECK(!memcmp(original,fallback,240u*150u*sizeof *original));
    CHECK(memcmp(original+240u*150u,fallback+240u*150u,240u*10u*sizeof *original));
    portable_scene_3d_close(&fixture.depth);
    return 0;
}

static void model_word(uint8_t *p,uint32_t value)
{
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));
}

static int model_identity_and_fallback(void)
{
    static Fixture f;CHECK(!fixture_init(&f));
    int32_t y;uint32_t depth;CHECK(!find_depth(&f,256,&y,&depth));
    uint8_t vertices[80]={0},triangles[16]={0};
    const int32_t points[4][3]={{-8,0,0},{8,0,0},{8,0,16},{-8,0,16}};
    for(unsigned i=0;i<4u;++i)for(unsigned j=0;j<3u;++j)
        model_word(vertices+i*20u+j*4u,(uint32_t)(points[i][j]*65536));
    triangles[2]=1u;triangles[4]=2u;triangles[10]=2u;triangles[12]=3u;
    const uint32_t model_color=UINT32_C(0xffff00ff);
    PortableModelBinding binding={.kind=PORTABLE_MODEL_DRIVER,.target_id=17u,
        .scale_q16={65536,65536,65536},.mesh={.vertices=vertices,.triangles=triangles,
            .vertex_count=4u,.triangle_count=2u,.material_count=1u,.materials={{.argb=model_color}}}};
    PortableModelCatalog catalog={.bindings=&binding,.count=1u};
    f.scene.character_id=17u;f.scene.ghost_character_id=17u;
    f.scene.opponent_character_ids[0]=17u;
    f.scene.opponent_count=1u;
    f.scene.ghost_character=&f.character;f.scene.opponent_characters[0]=&f.character;
    /* Isolate each identity path. A correct player cannot hide a missing
     * opponent or ghost binding in this pixel oracle. */
    for(unsigned pass=0;pass<6u;++pass){
        const unsigned entity=pass/2u,wide=pass%2u;
        const unsigned scale=wide?4u:1u;
        const size_t width=wide?373u:240u,stride=width*scale+3u,count=stride*160u*scale;
        uint32_t *pixels=malloc(count*sizeof *pixels),*original=malloc(count*sizeof *original);
        CHECK(pixels&&original);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
            .camera_position_override=1u,.position_y_q16=y,.character_affine_scale=256u,
            .suppress_native_hud=1u,.suppress_native_minimap=1u};
        state.suppress_player=(uint8_t)(entity!=0u);
        state.ghost_visible=(uint8_t)(entity==1u);
        state.ghost_position_y_q16=y;state.ghost_affine_scale=256u;
        state.opponent_count=entity==2u?1u:0u;
        state.opponents[0]=(PortableRaceOpponentPresentation){.position_y_q16=y,
            .visible=1u,.character_affine_scale=256u};
        uint8_t *pose_valid=entity==0u?&state.art_pose_valid:
            entity==1u?&state.ghost_art_pose_valid:&state.opponents[0].art_pose_valid;
        PortableCharacterViewPose *pose=entity==0u?&state.art_pose:
            entity==1u?&state.ghost_art_pose:&state.opponents[0].art_pose;
        f.scene.models=NULL;CHECK(!draw(&f,&state,&frame));memcpy(original,pixels,count*sizeof *pixels);
        f.scene.models=&catalog;binding.target_id=18u;
        CHECK(!draw(&f,&state,&frame));CHECK(!memcmp(original,pixels,count*sizeof *pixels));
        binding.target_id=17u;CHECK(!draw(&f,&state,&frame));
        size_t model_pixels=0u;
        for(size_t i=0;i<count;++i){
            if(pixels[i]==model_color)++model_pixels;
            CHECK((pixels[i]&0xffffffu)!=KART);
        }
        CHECK(model_pixels>0u);
        CHECK(!f.depth.model_fallback_reported);
        /* Unsupported special contact pose uses the exact original sprite,
         * rather than silently substituting an ordinary upright model. */
        *pose_valid=1u;pose->frame_override=1u;
        f.scene.models=NULL;CHECK(!draw(&f,&state,&frame));memcpy(original,pixels,count*sizeof *pixels);
        f.scene.models=&catalog;CHECK(!draw(&f,&state,&frame));
        CHECK(!memcmp(original,pixels,count*sizeof *pixels));
        /* Invalid runtime transforms produce a complete billboard fallback,
         * and the diagnostic latch survives the next frame begin. */
        *pose_valid=0u;binding.scale_q16[0]=0;
        f.scene.models=NULL;CHECK(!draw(&f,&state,&frame));memcpy(original,pixels,count*sizeof *pixels);
        f.scene.models=&catalog;CHECK(!draw(&f,&state,&frame));
        CHECK(!memcmp(original,pixels,count*sizeof *pixels)&&f.depth.model_fallback_reported);
        binding.scale_q16[0]=65536;f.depth.model_fallback_reported=0u;
        free(original);free(pixels);
    }
    portable_scene_3d_close(&f.depth);return 0;
}

static int scenery_model_identity_and_pose(void)
{
    static Fixture f;CHECK(!fixture_init(&f));
    int32_t y;uint32_t distance_q16;CHECK(!find_depth(&f,256,&y,&distance_q16));
    uint8_t vertices[80]={0},triangles[16]={0};
    const int32_t points[4][3]={{-8,0,0},{8,0,0},{8,0,16},{-8,0,16}};
    for(unsigned i=0;i<4u;++i)for(unsigned j=0;j<3u;++j)
        model_word(vertices+i*20u+j*4u,(uint32_t)(points[i][j]*65536));
    triangles[2]=1u;triangles[4]=2u;triangles[10]=2u;triangles[12]=3u;
    const uint32_t model_color=UINT32_C(0xff00ffff);
    PortableModelBinding binding={.kind=PORTABLE_MODEL_SCENERY,.target_id=41u,.art_bank=0u,
        .scale_q16={65536,65536,65536},.mesh={.vertices=vertices,.triangles=triangles,
            .vertex_count=4u,.triangle_count=2u,.material_count=1u,.materials={{.argb=model_color}}}};
    PortableModelCatalog catalog={.bindings=&binding,.count=1u};
    PortableCourseSceneryArt art[2]={f.art,f.art};
    f.scenery.native_art=art;f.scenery.native_art_count=2u;
    f.object.position_y_q16=y;f.scene.course_id=41u;f.scene.scenery=&f.scenery;
    for(unsigned pass=0;pass<2u;++pass){
        const unsigned scale=pass?4u:1u;
        const size_t width=pass?373u:240u,stride=width*scale+3u,rows=160u*scale,count=stride*rows;
        uint32_t *pixels=malloc(count*sizeof *pixels),*original=malloc(count*sizeof *original);
        CHECK(pixels&&original);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        PortableRacePresentationState state={.phase=PORTABLE_RACE_PHASE_RACING,.lap_count=3u,
            .camera_position_override=1u,.suppress_player=1u,
            .suppress_native_hud=1u,.suppress_native_minimap=1u};
        state.course_props.count=1u;
        state.course_props.actors[0]=(PortableCourseProp){.y_q16=y,.affine_x_q8=256,.affine_y_q8=256,.flags=1u};
        f.scene.models=NULL;CHECK(!draw(&f,&state,&frame));memcpy(original,pixels,count*sizeof *pixels);
        f.scene.models=&catalog;
        for(unsigned mismatch=0;mismatch<2u;++mismatch){
            binding.target_id=mismatch?41u:42u;binding.art_bank=mismatch?1u:0u;
            CHECK(!draw(&f,&state,&frame));CHECK(!memcmp(original,pixels,count*sizeof *pixels));
        }
        binding.target_id=41u;binding.art_bank=0u;CHECK(!draw(&f,&state,&frame));
        size_t visible=0u,top=rows;
        for(size_t py=0;py<rows;++py)for(size_t x=0;x<width*scale;++x){
            if(pixels[py*stride+x]==model_color){++visible;if(py<top)top=py;}
            CHECK((pixels[py*stride+x]&0xffffffu)!=SCENERY);
        }
        CHECK(visible>0u&&!f.depth.model_fallback_reported);
        state.course_props.actors[0].height_q16=2*65536;
        CHECK(!draw(&f,&state,&frame));
        size_t raised_top=rows;
        for(size_t py=0;py<rows;++py)for(size_t x=0;x<width*scale;++x)
            if(pixels[py*stride+x]==model_color&&py<raised_top)raised_top=py;
        /* A rigid source-height translation, independently bounded using the
         * actor distance, can straddle at most one physical row. */
        const uint64_t shift_numerator=UINT64_C(2)*1024u*scale*65536u;
        const size_t shift=(size_t)(shift_numerator/distance_q16);
        CHECK(raised_top<top && (top-raised_top==shift || top-raised_top==shift+1u));
        state.course_props.actors[0].height_q16=32*65536;
        state.course_props.actors[0].affine_y_q8=-256;
        CHECK(!draw(&f,&state,&frame));
        visible=0u;
        for(size_t i=0;i<count;++i)if(pixels[i]==model_color)++visible;
        CHECK(visible>0u&&!f.depth.model_fallback_reported);
        /* A source animation selects an exact variant. No model for variant1
         * means its own billboard art, not the base variant's model. */
        state.course_props.actors[0].height_q16=0;
        state.course_props.actors[0].affine_y_q8=256;
        state.course_props.actors[0].art_index=1u;
        f.scene.models=NULL;CHECK(!draw(&f,&state,&frame));memcpy(original,pixels,count*sizeof *pixels);
        f.scene.models=&catalog;CHECK(!draw(&f,&state,&frame));
        CHECK(!memcmp(original,pixels,count*sizeof *pixels));
        free(original);free(pixels);
    }
    CHECK(binding.mesh.vertices==vertices&&binding.mesh.triangles==triangles);
    portable_scene_3d_close(&f.depth);return 0;
}

/* Items and the Boo share the depth-tested world queue with karts: their
 * opaque pixels must pass the depth test instead of being dropped as clear. */
static int item_depth(void)
{
    static uint8_t tiles[0x4000],cells[PORTABLE_MUSHROOM_BURST_CELL_BYTES];
    static uint16_t palette[128]; /* Boo cells use tile 0x190, palette 7. */
    memset(tiles,0x11,sizeof tiles);
    for(size_t i=0;i<sizeof palette/sizeof *palette;++i)palette[i]=0x001fu;
    for(unsigned a=0;a<18u;++a){uint8_t *cell=cells+PORTABLE_DRIFT_SPARK_CELL_BYTES+a*8u;
        cell[2]=(uint8_t)-8;cell[6]=0x00;cell[7]=0x40;}
    const PortableRaceParticleAssetView asset={tiles,sizeof tiles,palette,sizeof palette/sizeof *palette,
        cells,sizeof cells,NULL,0u,0u,0u};
    enum{W=240u,H=160u};
    static uint32_t pixels[W*H],depth_pixels[W*H];
    for(unsigned kind=0;kind<5u;++kind){
        for(size_t i=0;i<W*H;++i){pixels[i]=GUARD;depth_pixels[i]=UINT32_MAX;}
        PortableSoftwareDepth depth={.pixels=depth_pixels,.stride=W,.value=1000u};
        PortableSoftwareFrame frame={.pixels=pixels,.width=W,.height=H,.depth=&depth};
        int ok=0;
        if(kind==0u)ok=portable_green_shell_render(&asset,&frame,120,80,0u,256u);
        else if(kind==1u)ok=portable_red_shell_render_projected(&asset,&frame,120,80,0u,256u);
        else if(kind==2u)ok=portable_spiny_shell_render_projected(&asset,&frame,120,80,0u,256u);
        else if(kind==3u)ok=portable_banana_render_projected(&asset,&frame,120,80,256u);
        else ok=portable_boo_attack_render(&asset,&frame,120,80,0u,0u);
        CHECK(ok);
        size_t drawn=0u;
        for(size_t i=0;i<W*H;++i)if(pixels[i]!=GUARD){
            CHECK(pixels[i]==UINT32_C(0xffff0000)&&depth_pixels[i]==1000u);++drawn;}
        CHECK(drawn>0u);
    }
    return 0;
}

int main(void)
{
    CHECK(!world_depth());CHECK(!item_depth());CHECK(!unsupported_scene_fallback());CHECK(!model_identity_and_fallback());CHECK(!scenery_model_identity_and_pose());
    puts("portable race depth: PASS");return 0;
}
