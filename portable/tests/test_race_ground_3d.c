#include "race_ground_3d.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"ground3d line%d: %s\n",__LINE__,#c); return 1; } } while (0)
#define BACKGROUND UINT32_C(0xff102030)
#define GUARD UINT32_C(0xdeadbeef)

typedef struct {
    uint8_t map[6], tiles[6u*64u];
    uint16_t palette[256];
    PortableRaceSceneView scene;
} Fixture;

static void fixture_init(Fixture *fixture)
{
    memset(fixture,0,sizeof *fixture);
    for (unsigned i=0;i<6u;++i) {
        fixture->map[i]=(uint8_t)i;
        for (unsigned y=0;y<8u;++y) for (unsigned x=0;x<8u;++x)
            fixture->tiles[i*64u+y*8u+x]=(uint8_t)(1u+i*17u+x+y*3u);
    }
    /* A genuine palette-zero hole must leave both sky and depth untouched. */
    fixture->tiles[5u*64u+2u*8u+2u]=0u;
    for (unsigned i=0;i<256u;++i)
        fixture->palette[i]=(uint16_t)((i&31u)|(((i*3u)&31u)<<5u)|(((i*7u)&31u)<<10u));
    fixture->scene.world_map=(PortableRoadMapView){.map=fixture->map,.map_bytes=6u,
        .width_tiles=3u,.height_tiles=2u};
    fixture->scene.tileset=(PortableTrackTilesetView){.tiles=fixture->tiles,
        .tile_bytes=sizeof fixture->tiles,.palette=fixture->palette,.palette_count=256u};
}

static int64_t floor_ratio(int64_t n, int64_t d)
{
    return n>=0?n/d:-((-n+d-1)/d);
}

static unsigned wrapped(int64_t value, unsigned extent)
{
    const int64_t remainder=value%(int64_t)extent;
    return (unsigned)(remainder<0?remainder+extent:remainder);
}

static uint32_t expected_color(const Fixture *fixture, int64_t world_x, int64_t world_y)
{
    const unsigned columns=(unsigned)fixture->scene.world_map.width_tiles;
    const unsigned rows=(unsigned)fixture->scene.world_map.height_tiles;
    const unsigned x=wrapped(world_x,columns*8u), y=wrapped(world_y,rows*8u);
    const unsigned tile=fixture->map[(y/8u)*columns+x/8u];
    const unsigned index=fixture->tiles[tile*64u+(y%8u)*8u+x%8u];
    if (!index) return BACKGROUND;
    const unsigned color=fixture->palette[index];
    /* Five-bit replication, expressed independently of the renderer shifts. */
    const unsigned red=color&31u, green=(color>>5u)&31u, blue=(color>>10u)&31u;
    return UINT32_C(0xff000000) | ((red*8u+red/4u)<<16u) |
        ((green*8u+green/4u)<<8u) | (blue*8u+blue/4u);
}

static int projection_wrap_and_scale(void)
{
    Fixture fixture; fixture_init(&fixture);
    PortableScene3D target={0};
    uint32_t pixels[(8u*4u+3u)*8u*4u+1u];
    const unsigned scales[]={1u,2u,3u,4u};
    for (unsigned pass=0;pass<24u;++pass) {
        /* Both-mask fast path and mixed modulo/mask maps use the same oracle. */
        fixture.scene.world_map.width_tiles=pass<12u?3u:2u;
        fixture.scene.world_map.map_bytes=fixture.scene.world_map.width_tiles*2u;
        const Fixture immutable=fixture;
        const unsigned scale=scales[pass%4u], width=8u*scale, stride=width+3u;
        /* Exact texel ties and their nearest source-Q16 neighbors must remain
         * distinct; numerical tie handling cannot swallow real fractions. */
        const int reference_delta=(int)((pass%12u)/4u)-1;
        for (size_t i=0;i<sizeof pixels/sizeof pixels[0];++i) pixels[i]=GUARD;
        for (unsigned y=0;y<width;++y) for (unsigned x=0;x<width;++x) pixels[y*stride+x]=BACKGROUND;
        PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u,
            .scale=(uint8_t)scale,.stride=stride};
        CHECK(portable_scene_3d_begin(&target,&frame));
        const PortableCameraState camera={.screen_x=4,.screen_y=4,.elevation_q16=64*65536};
        const PortableCameraProjection basis={.basis={{65536,0,0},{0,65536,0},{0,0,65536}},
            .reference_x=-2*65536-16384+reference_delta,.reference_y=-65536-49152};
        const int prior_shift=3;
        CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,
            prior_shift,1.0,65535.0)==PORTABLE_RACE_GROUND_3D_OK);
        for (unsigned y=0;y<width;++y) for (unsigned x=0;x<width;++x) {
            const int64_t wx=floor_ratio((4*(int64_t)x+2-25*(int64_t)scale)*65536+
                4*(int64_t)scale*reference_delta,4*(int64_t)scale*65536);
            const int64_t wy=floor_ratio(4*(int64_t)y+2-4*prior_shift-23*(int64_t)scale,4*(int64_t)scale);
            const uint32_t expected=expected_color(&fixture,wx,wy);
            if (pixels[y*stride+x]!=expected)
                fprintf(stderr,"ground wrap scale%u pixel%u,%u world%lld,%lld expected%08x actual%08x depth%u\n",
                    scale,x,y,(long long)wx,(long long)wy,(unsigned)expected,
                    (unsigned)pixels[y*stride+x],(unsigned)target.depth[y*width+x]);
            CHECK(pixels[y*stride+x]==expected);
            const uint32_t expected_depth=expected==BACKGROUND?UINT32_MAX:1024u*65536u;
            const int64_t delta=(int64_t)target.depth[y*width+x]-(int64_t)expected_depth;
            /* Perspective reciprocal rounding may land one Q16 unit either
             * side of the analytic depth. Transparent holes remain exact. */
            const int depth_matches=expected==BACKGROUND?delta==0:delta>=-1 && delta<=1;
            if (!depth_matches)
                fprintf(stderr,"ground depth scale%u reference_delta%d pixel%u,%u expected%u actual%u delta%lld\n",
                    scale,reference_delta,x,y,(unsigned)expected_depth,(unsigned)target.depth[y*width+x],
                    (long long)delta);
            CHECK(depth_matches);
        }
        for (unsigned y=0;y<width;++y) for (unsigned x=width;x<stride;++x) CHECK(pixels[y*stride+x]==GUARD);
        CHECK(pixels[(size_t)stride*width]==GUARD);
        CHECK(!memcmp(&fixture,&immutable,sizeof fixture));
    }
    portable_scene_3d_close(&target);
    return 0;
}

static int perspective_plane_and_clip(void)
{
    Fixture fixture; fixture_init(&fixture);
    uint32_t pixels[64];
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u};
    PortableScene3D target={0};
    CHECK(portable_scene_3d_begin(&target,&frame));
    for (unsigned i=0;i<64u;++i) pixels[i]=BACKGROUND;
    /* Py=8, Pz=-worldY, so screenY=64/(8+worldY).
     * Fractional world references avoid ambiguous texel-edge ties. This
     * supplies an independent inverse-ray oracle for perspective UVs. */
    const PortableCameraState camera={.screen_x=4,.screen_y=0,.elevation_q16=8*65536};
    const PortableCameraProjection basis={.basis={{65536,0,0},{0,0,-65536},{0,65536,0}},
        .reference_x=-8192,.reference_y=-24576,.reference_z=-8*65536};
    target.clip_top=2u;
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,
        1.0,65535.0)==PORTABLE_RACE_GROUND_3D_OK);
    for (unsigned y=0;y<8u;++y) for (unsigned x=0;x<8u;++x) {
        const int64_t denominator=2*(int64_t)y+1;
        const int64_t wx=floor_ratio(64*(2*(int64_t)x+1-8)-denominator,8*denominator);
        const int64_t wy=floor_ratio(1024-67*denominator,8*denominator);
        const uint32_t expected=y<2u?BACKGROUND:expected_color(&fixture,wx,wy);
        CHECK(pixels[y*8u+x]==expected);
        if (expected==BACKGROUND) CHECK(target.depth[y*8u+x]==UINT32_MAX);
        else {
            const uint32_t depth=(uint32_t)((UINT64_C(16384)*65536u)/(uint64_t)denominator);
            const uint32_t actual=target.depth[y*8u+x];
            CHECK(actual>=depth-1u && actual<=depth+1u);
        }
    }
    portable_scene_3d_close(&target);
    return 0;
}

static int invalid_content_and_camera(void)
{
    Fixture fixture; fixture_init(&fixture);
    uint32_t pixels[64];
    for (unsigned i=0;i<64u;++i) pixels[i]=GUARD;
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u};
    PortableScene3D target={0}; CHECK(portable_scene_3d_begin(&target,&frame));
    PortableCameraState camera={.screen_x=4,.screen_y=4,.elevation_q16=65536};
    PortableCameraProjection basis={.basis={{65536,0,0},{0,65536,0},{0,0,65536}}};
    fixture.scene.world_map.map_bytes=5u;
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,1.0,65535.0)==PORTABLE_RACE_GROUND_3D_CONTENT);
    fixture.scene.world_map.map_bytes=6u; fixture.map[0]=6u;
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,1.0,65535.0)==PORTABLE_RACE_GROUND_3D_CONTENT);
    fixture.map[0]=0u; camera.elevation_q16=0;
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,1.0,65535.0)==PORTABLE_RACE_GROUND_3D_UNSUPPORTED);
    camera.elevation_q16=65536; memset(&basis,0,sizeof basis);
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,1.0,65535.0)==PORTABLE_RACE_GROUND_3D_UNSUPPORTED);
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,2.0,1.0)==PORTABLE_RACE_GROUND_3D_INVALID);
    /* Far camera Z maps to world Y, amplifying uncertainty in sampled UVs. */
    basis=(PortableCameraProjection){.basis={{1,0,0},{0,0,1},{0,1,0}}};
    camera.elevation_q16=1024*65536;
    CHECK(portable_race_ground_3d_render(&target,&fixture.scene,&camera,&basis,0,1.0,65535.0)==PORTABLE_RACE_GROUND_3D_UNSUPPORTED);
    for (unsigned i=0;i<64u;++i) CHECK(pixels[i]==GUARD && target.depth[i]==UINT32_MAX);
    portable_scene_3d_close(&target);
    return 0;
}

int main(void)
{
    if (projection_wrap_and_scale() || perspective_plane_and_clip() || invalid_content_and_camera()) return 1;
    puts("portable race ground3d: PASS");
    return 0;
}
