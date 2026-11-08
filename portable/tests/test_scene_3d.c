#include "scene_3d.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#endif
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"scene3d line%d: %s\n",__LINE__,#condition); return 1; \
} } while (0)

#define GUARD UINT32_C(0x12345678)
#define RED UINT32_C(0xffff0000)
#define GREEN UINT32_C(0xff00ff00)
#define BLUE UINT32_C(0xff0000ff)

static uint32_t solid(const void *userdata, double u, double v)
{
    (void)u; (void)v;
    return *(const uint32_t *)userdata;
}

static uint32_t hole(const void *userdata, double u, double v)
{
    (void)userdata; (void)v;
    return u<0.5?0u:RED;
}

static uint32_t uv_color(const void *userdata, double u, double v)
{
    (void)userdata;
    return 0xff000000u | ((uint32_t)(u*255.0)<<16u) | ((uint32_t)(v*255.0)<<8u);
}

static uint32_t counted(const void *userdata, double u, double v)
{
    (void)u; (void)v;
    unsigned *count=*(unsigned *const *)userdata;
    ++*count;
    return GREEN;
}

static void constant_depth_triangle(PortableScene3DVertex out[3], double z)
{
    out[0]=(PortableScene3DVertex){1.0*z,-1.0*z,z,0.0,0.0};
    out[1]=(PortableScene3DVertex){7.0*z,-1.0*z,z,1.0,0.0};
    out[2]=(PortableScene3DVertex){1.0*z,-7.0*z,z,0.0,1.0};
}

static int depth_and_coverage(void)
{
    uint32_t pixels[11u*8u+1u];
    for (size_t i=0;i<sizeof pixels/sizeof pixels[0];++i) pixels[i]=GUARD;
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u,.stride=11u};
    PortableScene3D scene={0};
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    PortableScene3DVertex triangle[3];
    const uint32_t blue=BLUE, red=RED, green=GREEN;
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(scene.width==8u && scene.height==8u && scene.capacity>=64u);
    for (size_t i=0;i<64u;++i) CHECK(scene.depth[i]==UINT32_MAX);
    constant_depth_triangle(triangle,4.0);
    CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,solid,&blue));
    CHECK(pixels[2u*11u+2u]==BLUE && scene.depth[2u*8u+2u]==4u*65536u);
    CHECK(pixels[0]==GUARD && pixels[7u*11u+7u]==GUARD);
    constant_depth_triangle(triangle,1.0);
    CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,hole,NULL));
    CHECK(pixels[2u*11u+2u]==BLUE && scene.depth[2u*8u+2u]==4u*65536u);
    CHECK(pixels[1u*11u+5u]==RED && scene.depth[1u*8u+5u]==65536u);
    constant_depth_triangle(triangle,2.0);
    CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,solid,&green));
    CHECK(pixels[2u*11u+2u]==GREEN && pixels[1u*11u+5u]==RED);
    CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,solid,&red));
    CHECK(pixels[2u*11u+2u]==RED); /* Equal-depth later draw wins. */
    CHECK(!portable_scene_3d_depth_store(&scene,2u,2u,0u,0u));
    CHECK(!portable_scene_3d_depth_store(&scene,8u,0u,1u,RED));
    CHECK(!portable_scene_3d_depth_store(&scene,0u,0u,UINT32_MAX,RED));
    for (size_t y=0;y<8u;++y) for (size_t x=8u;x<11u;++x) CHECK(pixels[y*11u+x]==GUARD);
    CHECK(pixels[88]==GUARD);

    CHECK(portable_scene_3d_begin(&scene,&frame));
    unsigned calls=0u;
    unsigned *counter=&calls;
    constant_depth_triangle(triangle,2.0);
    CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,counted,&counter));
    const PortableScene3DVertex other[3]={{14.0,-2.0,2.0,0.0,0.0},
        {14.0,-14.0,2.0,0.0,0.0},{2.0,-14.0,2.0,0.0,0.0}};
    CHECK(portable_scene_3d_triangle(&scene,&projection,other,counted,&counter));
    CHECK(calls==36u); /* Six by six square, no gap or shared-edge double sample. */
    for (size_t y=1u;y<7u;++y) for (size_t x=1u;x<7u;++x)
        CHECK(pixels[y*11u+x]==GREEN && scene.depth[y*8u+x]==2u*65536u);
    portable_scene_3d_close(&scene);
    CHECK(!scene.depth && !scene.frame && !scene.capacity && !scene.width && !scene.height);
    portable_scene_3d_close(&scene);
    return 0;
}

static int perspective_and_clipping(void)
{
    uint32_t pixels[64]={0};
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u};
    PortableScene3D scene={0};
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    CHECK(portable_scene_3d_begin(&scene,&frame));
    /* Screen corners0,0 /8,0 /0,8 with independent depths1,2,4.
     * At1.5,1.5 barycentrics40/64,12/64,12/64 give UV6/49,3/49. */
    const PortableScene3DVertex perspective[3]={{0.0,0.0,1.0,0.0,0.0},
        {16.0,0.0,2.0,1.0,0.0},{0.0,-32.0,4.0,0.0,1.0}};
    CHECK(portable_scene_3d_triangle(&scene,&projection,perspective,uv_color,NULL));
    CHECK(pixels[9]==(0xff000000u|(31u<<16u)|(15u<<8u)));
    CHECK(scene.depth[9]==4194304u/49u);
    memset(pixels,0,sizeof pixels);
    CHECK(portable_scene_3d_begin(&scene,&frame));
    const PortableScene3DVertex near_clip[3]={{0.0,0.0,0.5,0.0,0.0},
        {16.0,0.0,2.0,1.0,0.0},{0.0,-16.0,2.0,0.0,1.0}};
    const uint32_t red=RED, blue=BLUE;
    CHECK(portable_scene_3d_triangle(&scene,&projection,near_clip,solid,&red));
    CHECK(pixels[9]==0u && pixels[2u*8u+4u]==RED);
    for (size_t i=0;i<64u;++i)
        CHECK(scene.depth[i]==UINT32_MAX || (scene.depth[i]>=65536u && scene.depth[i]<=8u*65536u));
    uint32_t before[64]; memcpy(before,pixels,sizeof before);
    PortableScene3DVertex far_clip[3]; constant_depth_triangle(far_clip,9.0);
    CHECK(portable_scene_3d_triangle(&scene,&projection,far_clip,solid,&blue));
    CHECK(!memcmp(before,pixels,sizeof before));
    CHECK(portable_scene_3d_begin(&scene,&frame));
    const PortableScene3DVertex partial_far[3]={{0.0,0.0,2.0,0.0,0.0},
        {72.0,0.0,9.0,0.0,0.0},{0.0,-16.0,2.0,0.0,0.0}};
    CHECK(portable_scene_3d_triangle(&scene,&projection,partial_far,solid,&blue));
    CHECK(pixels[9]==BLUE && scene.depth[9]>=2u*65536u && scene.depth[9]<=8u*65536u);
    const PortableScene3DVertex viewport[3]={{-4.0,4.0,2.0,0.0,0.0},
        {24.0,4.0,2.0,0.0,0.0},{-4.0,-24.0,2.0,0.0,0.0}};
    CHECK(portable_scene_3d_triangle(&scene,&projection,viewport,solid,&blue));
    CHECK(pixels[0]==BLUE && scene.depth[0]==2u*65536u);
    /* Clipping huge but finite offscreen coordinates must avoid integer casts
     * until the polygon is bounded to the physical viewport. */
    const PortableScene3DVertex huge[3]={{-1e100,-1e100,2.0,0.0,0.0},
        {1e100,-1e100,2.0,0.0,0.0},{0.0,1e100,2.0,0.0,0.0}};
    CHECK(portable_scene_3d_triangle(&scene,&projection,huge,solid,&blue));
    portable_scene_3d_close(&scene);
    return 0;
}

static int ownership_and_invalid_values(void)
{
    uint32_t pixels[19u*16u+1u];
    for (size_t i=0;i<sizeof pixels/sizeof pixels[0];++i) pixels[i]=GUARD;
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u,.scale=2u,.stride=19u};
    PortableScene3D scene={0};
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(scene.width==16u && scene.height==16u && scene.capacity>=256u);
    CHECK(portable_scene_3d_depth_store(&scene,15u,15u,1u,RED));
    CHECK(pixels[15u*19u+15u]==RED && scene.depth[255]==1u);
    uint32_t *allocation=scene.depth;
    const size_t capacity=scene.capacity;
    PortableSoftwareFrame invalid=frame; invalid.width=SIZE_MAX;
    CHECK(!portable_scene_3d_begin(&scene,&invalid));
    CHECK(scene.depth==allocation && scene.frame==&frame && scene.capacity==capacity && scene.depth[255]==1u);
    invalid=frame; invalid.stride=15u;
    CHECK(!portable_scene_3d_begin(&scene,&invalid));
    PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    PortableScene3DVertex vertices[3]; constant_depth_triangle(vertices,2.0);
    const uint32_t red=RED;
    vertices[0].u=NAN;
    CHECK(!portable_scene_3d_triangle(&scene,&projection,vertices,solid,&red));
    vertices[0].u=0.0; projection.focal_x=INFINITY;
    CHECK(!portable_scene_3d_triangle(&scene,&projection,vertices,solid,&red));
    projection.focal_x=1.0; projection.far_z=projection.near_z;
    CHECK(!portable_scene_3d_triangle(&scene,&projection,vertices,solid,&red));
    CHECK(scene.depth[255]==1u);
    frame.scale=1u;
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(scene.depth==allocation && scene.capacity==capacity && scene.width==8u && scene.height==8u);
    CHECK(scene.billboard_depth==allocation+capacity);
    for (size_t i=0;i<64u;++i)
        CHECK(scene.depth[i]==UINT32_MAX && scene.billboard_depth[i]==UINT32_MAX);
    /* Shrinking must not move O into G's unused capacity. Growing again must
     * clear both active planes, including previously inactive tail entries. */
    scene.depth[capacity-1u]=7u;scene.billboard_depth[capacity-1u]=9u;
    frame.scale=2u;CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(scene.depth==allocation && scene.capacity==capacity &&
        scene.billboard_depth==allocation+capacity);
    for(size_t i=0;i<256u;++i)
        CHECK(scene.depth[i]==UINT32_MAX && scene.billboard_depth[i]==UINT32_MAX);
    for (size_t y=0;y<16u;++y) for (size_t x=16u;x<19u;++x) CHECK(pixels[y*19u+x]==GUARD);
    CHECK(pixels[304]==GUARD);
    portable_scene_3d_close(&scene);
    return 0;
}

static int row_clip_and_blend(void)
{
    uint32_t pixels[64];
    for(size_t i=0u;i<64u;++i)pixels[i]=BLUE;
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u};
    PortableScene3D scene={0};
    CHECK(portable_scene_3d_begin(&scene,&frame));
    scene.clip_top=2u;scene.clip_bottom=5u;
    scene.source_weight=13u;scene.destination_weight=6u;
    CHECK(!portable_scene_3d_depth_store(&scene,3u,1u,65536u,RED));
    CHECK(!portable_scene_3d_depth_store(&scene,3u,5u,65536u,RED));
    CHECK(portable_scene_3d_depth_store(&scene,3u,2u,65536u,RED));
    CHECK(pixels[19u]==UINT32_C(0xffcf005f));
    CHECK(scene.depth[11u]==UINT32_MAX && scene.depth[43u]==UINT32_MAX);
    pixels[20u]=UINT32_C(0xffffffff);
    CHECK(portable_scene_3d_depth_store(&scene,4u,2u,65536u,UINT32_C(0xffffffff)));
    CHECK(pixels[20u]==UINT32_C(0xffffffff)); /* Source blend saturates. */
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(scene.clip_top==0u && scene.clip_bottom==8u && scene.source_weight==16u &&
        scene.destination_weight==0u);
    scene.clip_top=2u;scene.clip_bottom=5u;
    unsigned calls=0u;unsigned *counter=&calls;
    PortableScene3DVertex first[3];constant_depth_triangle(first,2.0);
    const PortableScene3DVertex second[3]={{14.0,-2.0,2.0,0.0,0.0},
        {14.0,-14.0,2.0,0.0,0.0},{2.0,-14.0,2.0,0.0,0.0}};
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    CHECK(portable_scene_3d_triangle(&scene,&projection,first,counted,&counter));
    CHECK(portable_scene_3d_triangle(&scene,&projection,second,counted,&counter));
    CHECK(calls==18u); /* No texture samples outside the clipped 6x3 region. */
    scene.clip_top=9u;
    CHECK(!portable_scene_3d_triangle(&scene,&projection,first,counted,&counter));
    CHECK(calls==18u);
    portable_scene_3d_close(&scene);
    return 0;
}

static int thin_and_horizontal_coverage(void)
{
    enum { WIDTH=128,HEIGHT=64,STRIDE=131 };
    uint32_t pixels[STRIDE*HEIGHT];
    PortableSoftwareFrame frame={.pixels=pixels,.width=WIDTH,.height=HEIGHT,.stride=STRIDE};
    PortableScene3D scene={0};
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    const uint32_t red=RED;
    /* Screen-space fixtures have analytically known pixel-center coverage.
     * The first triangle occupies one pixel per row despite its large box;
     * the third crosses the viewport, and the fourth is almost horizontal. */
    static const double coordinates[4][3][2]={
        {{0.5,0.5},{127.5,63.5},{126.5,63.5}},
        {{2.5,3.5},{20.5,3.5},{2.5,8.5}},
        {{-0.5,-0.5},{128.5,63.5},{127.5,63.5}},
        {{1.5,6.5},{30.5,6.500000000001},{30.5,20.5}}
    };
    for(unsigned kind=0;kind<4u;++kind)for(unsigned reverse=0;reverse<2u;++reverse){
        for(size_t p=0;p<STRIDE*HEIGHT;++p)pixels[p]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame));
        PortableScene3DVertex triangle[3];
        for(unsigned i=0;i<3u;++i){
            const unsigned index=reverse?2u-i:i;
            triangle[i]=(PortableScene3DVertex){coordinates[kind][index][0]*2.0,
                -coordinates[kind][index][1]*2.0,2.0,0.0,0.0};
        }
        PortableScene3DVertex before[3];memcpy(before,triangle,sizeof before);
        CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,solid,&red));
        CHECK(!memcmp(before,triangle,sizeof before));
        unsigned painted=0u;
        for(unsigned y=0;y<HEIGHT;++y)for(unsigned x=0;x<STRIDE;++x){
            int expected=0;
            if(kind==0u)expected=y>0u&&y<63u&&x==2u*y;
            if(kind==1u)expected=y>=3u&&y<8u&&x>=2u&&5u*(x-2u)+18u*(y-3u)<90u;
            if(kind==2u)expected=y<63u&&x==2u*y+1u;
            if(kind==3u)expected=y>6u&&y<=20u&&x>=1u&&x<30u&&14u*(x-1u)>=29u*(y-6u);
            if(x>=WIDTH)expected=0;
            CHECK(pixels[y*STRIDE+x]==(expected?RED:GUARD));
            if(x<WIDTH){
                CHECK((scene.depth[y*WIDTH+x]!=UINT32_MAX)==expected);
                CHECK(scene.billboard_depth[y*WIDTH+x]==UINT32_MAX);
            }
            painted+=(unsigned)expected;
        }
        CHECK(painted>0u);
    }
    portable_scene_3d_close(&scene);
    return 0;
}

typedef struct {
    double u[64],v[64];
    size_t count;
} SampleTrace;

static uint32_t trace_sample(const void *userdata,double u,double v)
{
    SampleTrace *trace=*(SampleTrace *const *)userdata;
    if(trace->count<64u){trace->u[trace->count]=u;trace->v[trace->count]=v;}
    ++trace->count;
    return RED;
}

static int analytic_interpolation_and_extreme_fallback(void)
{
    uint32_t pixels[11u*8u];
    for(size_t p=0;p<88u;++p)pixels[p]=GUARD;
    PortableSoftwareFrame frame={.pixels=pixels,.width=8u,.height=8u,.stride=11u};
    PortableScene3D scene={0};
    SampleTrace trace={0};SampleTrace *trace_pointer=&trace;
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    const PortableScene3DVertex triangle[3]={{0.0,0.0,1.0,0.0,0.0},
        {16.0,0.0,2.0,1.0,0.0},{0.0,-32.0,4.0,0.0,1.0}};
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(portable_scene_3d_triangle(&scene,&projection,triangle,trace_sample,&trace_pointer));
    size_t index=0;
    for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<11u;++x){
        const int inside=x<8u&&x+y<7u;
        CHECK(pixels[y*11u+x]==(inside?RED:GUARD));
        if(inside){
            /* Exact screen barycentrics yield these rational functions.
             * Integer numerators are exact;16epsilon bounds reciprocal,
             * multiplication and reference-division roundoff conservatively. */
            const unsigned denominator=59u-4u*x-6u*y;
            const double expected_u=(double)(4u*x+2u)/denominator;
            const double expected_v=(double)(2u*y+1u)/denominator;
            CHECK(index<trace.count);
            CHECK(fabs(trace.u[index]-expected_u)<=16.0*DBL_EPSILON);
            CHECK(fabs(trace.v[index]-expected_v)<=16.0*DBL_EPSILON);
            CHECK(scene.depth[y*8u+x]==(UINT32_C(64)<<16u)/denominator);
            ++index;
        }
    }
    CHECK(trace.count==28u&&index==trace.count);
    CHECK(portable_scene_3d_begin(&scene,&frame));
    memset(&trace,0,sizeof trace);
    for(size_t p=0;p<88u;++p)pixels[p]=GUARD;
    const double z=ldexp(1.0,930),step=ldexp(1.0,-50);
    const PortableScene3DProjection extreme={1.0,1.0,0.0,0.0,z/2.0,z*2.0};
    const PortableScene3DVertex tiny[3]={{0.5*z,-0.5*z,z,0.25,0.75},
        {(0.5+step)*z,-0.5*z,z,0.25,0.75},
        {0.5*z,-(0.5+step)*z,z,0.25,0.75}};
    /* Screen area2^-100 times inverseZ2^-930 is subnormal2^-1030.
     * Its reciprocal overflows, but normalization before inversion is finite.
     * Binary powers keep the fallback's UV ratios exactly representable. */
    volatile double unnormalized=ldexp(1.0,-1030);
    volatile double reciprocal_overflow=1.0/unnormalized;
    CHECK(unnormalized>0.0&&!isfinite(reciprocal_overflow));
    CHECK(portable_scene_3d_triangle(&scene,&extreme,tiny,trace_sample,&trace_pointer));
    CHECK(trace.count==1u&&trace.u[0]==0.25&&trace.v[0]==0.75);
    CHECK(pixels[0]==RED&&scene.depth[0]==UINT32_MAX-1u);
    for(size_t p=1;p<88u;++p)CHECK(pixels[p]==GUARD);
    portable_scene_3d_close(&scene);
    return 0;
}

static int mixed_depth_planes(void)
{
    uint32_t pixels[9]={0};
    PortableSoftwareFrame frame={.pixels=pixels,.width=3u,.height=3u};
    PortableScene3D scene={0};
    scene.model_fallback_reported=1u;
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(scene.model_fallback_reported==1u && scene.model_pass==0u);
    CHECK(portable_scene_3d_depth_store(&scene,1u,1u,5u*65536u,BLUE));
    CHECK(scene.billboard_depth[4]==UINT32_MAX);
    PortableSoftwareDepth objects={.pixels=scene.billboard_depth,.stride=3u,.value=2u*65536u};
    frame.depth=&objects;
    portable_software_frame_store_physical(&frame,1,1,RED);
    CHECK(pixels[4]==RED && scene.depth[4]==5u*65536u);
    scene.model_pass=1u;
    CHECK(!portable_scene_3d_depth_store(&scene,1u,1u,3u*65536u,GREEN));
    CHECK(scene.depth[4]==5u*65536u && scene.billboard_depth[4]==2u*65536u);
    CHECK(!portable_scene_3d_depth_store(&scene,1u,1u,65536u,0u));
    CHECK(scene.depth[4]==5u*65536u && pixels[4]==RED);
    CHECK(portable_scene_3d_depth_store(&scene,1u,1u,65536u,GREEN));
    CHECK(scene.depth[4]==65536u && scene.billboard_depth[4]==65536u);
    objects.value=4u*65536u;
    portable_software_frame_store_physical(&frame,1,1,RED);
    CHECK(pixels[4]==GREEN);
    objects.value=65536u;
    portable_software_frame_store_physical(&frame,1,1,RED);
    CHECK(pixels[4]==RED); /* Later equal-depth object wins. */
    CHECK(portable_scene_3d_depth_store(&scene,1u,1u,65536u,BLUE));
    CHECK(pixels[4]==BLUE);
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(!scene.model_pass && scene.model_fallback_reported);
    CHECK(portable_scene_3d_depth_store(&scene,1u,1u,65536u,BLUE));
    objects.value=5u*65536u;
    portable_software_frame_store_physical(&frame,1,1,RED);
    CHECK(pixels[4]==RED); /* Ground deliberately does not clip billboards. */
    scene.model_pass=1u;
    CHECK(!portable_scene_3d_depth_store(&scene,1u,1u,3u*65536u,GREEN));
    CHECK(scene.depth[4]==65536u && scene.billboard_depth[4]==5u*65536u);
    CHECK(pixels[4]==RED); /* Rejected model cannot partially alter O. */
    frame.depth=NULL;
    portable_scene_3d_close(&scene);
    return 0;
}

typedef struct { unsigned calls; uint32_t color; } FragmentSample;

static uint32_t fragment_sample(const void *userdata,double u,double v)
{
    FragmentSample *sample=(FragmentSample *)userdata;
    (void)u;(void)v;++sample->calls;return sample->color;
}

static int fragment_square(PortableScene3D *scene,double z,FragmentSample *sample)
{
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,0x1p-30,8.0};
    PortableScene3DVertex first[3];constant_depth_triangle(first,z);
    const PortableScene3DVertex second[3]={{7.0*z,-z,z,0.0,0.0},
        {7.0*z,-7.0*z,z,0.0,0.0},{z,-7.0*z,z,0.0,0.0}};
    return portable_scene_3d_triangle(scene,&projection,first,fragment_sample,sample)&&
        portable_scene_3d_triangle(scene,&projection,second,fragment_sample,sample);
}

static int raster_fragment_contract(void)
{
    uint32_t storage[7u+11u*8u+9u];
    PortableSoftwareFrame frame={.pixels=storage+7u,.width=8u,.height=8u,.stride=11u};
    PortableScene3D scene={0};
    /* Exercise the raster's validated store, rather than only its public
     * checked counterpart. Clipped square coverage is independently 6x4. */
    for(unsigned test=0u;test<10u;++test){
        const uint32_t background=test==7u?UINT32_C(0xffffffff):test==8u?BLUE:GUARD;
        for(size_t i=0;i<sizeof storage/sizeof storage[0];++i)storage[i]=GUARD;
        for(size_t y=0;y<8u;++y)for(size_t x=0;x<8u;++x)frame.pixels[y*11u+x]=background;
        CHECK(portable_scene_3d_begin(&scene,&frame));
        scene.clip_top=2u;scene.clip_bottom=6u;
        scene.model_pass=(uint8_t)(test!=0u&&test!=9u);
        const uint32_t initial_g=test==2u?65536u:test==3u?2u*65536u:5u*65536u;
        const uint32_t initial_o=test==1u?65536u:test==3u?2u*65536u:5u*65536u;
        uint32_t *object_depth=scene.billboard_depth;
        for(size_t i=0;i<64u;++i){scene.depth[i]=initial_g;object_depth[i]=initial_o;}
        if(test==9u)scene.billboard_depth=NULL; /* Ground must never need O. */
        FragmentSample sample={.color=test==4u?0u:test==5u?UINT32_C(0x01020304):
            test==6u?UINT32_C(0x7faabbcc):test==7u?UINT32_C(0xffffffff):test==8u?RED:GREEN};
        if(test==7u||test==8u){scene.source_weight=13u;scene.destination_weight=6u;}
        CHECK(fragment_square(&scene,2.0,&sample));CHECK(sample.calls==24u);
        const int accepted=test!=1u&&test!=2u&&test!=4u;
        const uint32_t output=test==8u?UINT32_C(0xffcf005f):sample.color;
        for(size_t y=0;y<8u;++y)for(size_t x=0;x<11u;++x){
            const int covered=x>=1u&&x<7u&&y>=2u&&y<6u;
            const uint32_t expected=x>=8u?GUARD:covered&&accepted?output:background;
            CHECK(frame.pixels[y*11u+x]==expected);
            if(x<8u){
                CHECK(scene.depth[y*8u+x]==(covered&&accepted?2u*65536u:initial_g));
                CHECK(object_depth[y*8u+x]==
                    (covered&&accepted&&scene.model_pass?2u*65536u:initial_o));
            }
        }
        for(size_t i=0;i<7u;++i)CHECK(storage[i]==GUARD);
        for(size_t i=95u;i<104u;++i)CHECK(storage[i]==GUARD);
    }
    CHECK(portable_scene_3d_begin(&scene,&frame));
    scene.clip_top=2u;scene.clip_bottom=6u;scene.model_pass=1u;
    FragmentSample near={.color=RED},far={.color=BLUE};
    CHECK(fragment_square(&scene,1.0,&near)&&near.calls==24u);
    CHECK(fragment_square(&scene,4.0,&far)&&far.calls==24u);
    CHECK(frame.pixels[2u*11u+2u]==RED && scene.depth[2u*8u+2u]==65536u &&
        scene.billboard_depth[2u*8u+2u]==65536u);
    /* Positive camera depth smaller than one Q16 unit is nearest depth0,
     * not the billboard Frame.depth.value0 disabled convention. */
    FragmentSample tiny={.color=GREEN};
    CHECK(fragment_square(&scene,0x1p-20,&tiny)&&tiny.calls==24u);
    CHECK(frame.pixels[2u*11u+2u]==GREEN && scene.depth[2u*8u+2u]==0u &&
        scene.billboard_depth[2u*8u+2u]==0u);
    CHECK(!portable_scene_3d_depth_store(&scene,2u,2u,UINT32_MAX,BLUE));
    CHECK(frame.pixels[2u*11u+2u]==GREEN && scene.depth[2u*8u+2u]==0u &&
        scene.billboard_depth[2u*8u+2u]==0u);
    portable_scene_3d_close(&scene);return 0;
}

typedef struct {
    PortableScene3D *scene;
    unsigned visits[27u*8u],calls,last_x,last_y,row_calls,max_row_calls;
    int have_last,invalid;
} OrderedFragments;

static uint32_t ordered_fragment_sample(const void *userdata,double u,double v)
{
    OrderedFragments *sample=(OrderedFragments *)userdata;
    /* The authored rectangle has affine UVs: pixel centers are exactly
     * (1+24u,1+6v). This oracle does not depend on triangle batching. */
    if(!isfinite(u)||!isfinite(v)||u<0.0||u>=1.0||v<0.0||v>=1.0){
        sample->invalid=1;return 0u;
    }
    const unsigned x=(unsigned)floor(1.0+24.0*u),y=(unsigned)floor(1.0+6.0*v);
    if(x<1u||x>=25u||y<2u||y>=5u||
        fabs(u-((double)x-0.5)/24.0)>64.0*DBL_EPSILON||
        fabs(v-((double)y-0.5)/6.0)>64.0*DBL_EPSILON){
        sample->invalid=1;return 0u;
    }
    if(sample->have_last && (y<sample->last_y||(y==sample->last_y&&x<=sample->last_x)))
        sample->invalid=1;
    sample->row_calls=sample->have_last&&y==sample->last_y?sample->row_calls+1u:1u;
    if(sample->row_calls>sample->max_row_calls)sample->max_row_calls=sample->row_calls;
    sample->last_x=x;sample->last_y=y;sample->have_last=1;
    ++sample->visits[y*27u+x];++sample->calls;
    /* Stores must consult state AFTER their own callback, not state captured
     * before a geometry batch or inherited from the previous fragment. */
    sample->scene->model_pass=(uint8_t)(x%3u!=0u);
    sample->scene->source_weight=(uint8_t)(x%3u==2u?13u:16u);
    sample->scene->destination_weight=(uint8_t)(x%3u==2u?6u:0u);
    return x%3u==2u?RED:GREEN;
}

static int ordered_wide_fragments(void)
{
    uint32_t storage[7u+31u*8u+9u];
    for(size_t i=0;i<sizeof storage/sizeof storage[0];++i)storage[i]=GUARD;
    PortableSoftwareFrame frame={.pixels=storage+7u,.width=27u,.height=8u,.stride=31u};
    for(size_t y=0;y<8u;++y)for(size_t x=0;x<27u;++x)frame.pixels[y*31u+x]=BLUE;
    PortableScene3D scene={0};CHECK(portable_scene_3d_begin(&scene,&frame));
    scene.clip_top=2u;scene.clip_bottom=5u;
    for(size_t y=0;y<8u;++y)for(size_t x=0;x<27u;++x){
        scene.depth[y*27u+x]=5u*65536u;
        scene.billboard_depth[y*27u+x]=(x%3u==2u?5u:1u)*65536u;
    }
    OrderedFragments sample={.scene=&scene};
    const PortableScene3DProjection projection={1.0,1.0,0.0,0.0,1.0,8.0};
    const PortableScene3DVertex first[3]={{2.0,-2.0,2.0,0.0,0.0},
        {50.0,-2.0,2.0,1.0,0.0},{2.0,-14.0,2.0,0.0,1.0}};
    const PortableScene3DVertex second[3]={{50.0,-2.0,2.0,1.0,0.0},
        {50.0,-14.0,2.0,1.0,1.0},{2.0,-14.0,2.0,0.0,1.0}};
    CHECK(portable_scene_3d_triangle(&scene,&projection,first,ordered_fragment_sample,&sample));
    CHECK(!sample.invalid&&sample.max_row_calls>16u);
    sample.have_last=0; /* Submission order restarts at the next triangle. */
    CHECK(portable_scene_3d_triangle(&scene,&projection,second,ordered_fragment_sample,&sample));
    CHECK(!sample.invalid&&sample.calls==24u*3u);
    for(size_t y=0;y<8u;++y)for(size_t x=0;x<31u;++x){
        const int covered=x>=1u&&x<25u&&y>=2u&&y<5u;
        const int accepted=covered&&x%3u!=1u;
        const uint32_t expected=x>=27u?GUARD:!accepted?BLUE:
            x%3u==0u?GREEN:UINT32_C(0xffcf005f);
        CHECK(frame.pixels[y*31u+x]==expected);
        if(x<27u){
            CHECK(sample.visits[y*27u+x]==(covered?1u:0u));
            CHECK(scene.depth[y*27u+x]==(accepted?2u:5u)*65536u);
            const uint32_t initial_object=(x%3u==2u?5u:1u)*65536u;
            CHECK(scene.billboard_depth[y*27u+x]==
                (covered&&x%3u==2u?2u*65536u:initial_object));
        }
    }
    for(size_t i=0;i<7u;++i)CHECK(storage[i]==GUARD);
    for(size_t i=255u;i<264u;++i)CHECK(storage[i]==GUARD);
    portable_scene_3d_close(&scene);return 0;
}

typedef struct { uint32_t colors[5]; } ReadonlyPattern;

static uint32_t readonly_pattern(const void *userdata,double u,double v)
{
    const ReadonlyPattern *pattern=userdata;
    const int64_t value=(int64_t)floor(u)+3*(int64_t)floor(v);
    const unsigned index=(unsigned)((value%5+5)%5);
    return pattern->colors[index];
}

static int64_t readonly_floor16(int64_t numerator)
{
    return numerator>=0?numerator/16:-((-numerator+15)/16);
}

static uint32_t readonly_mix(uint32_t source,uint32_t destination,int weighted)
{
    if(!weighted)return source;
    uint32_t result=UINT32_C(0xff000000);
    for(unsigned shift=0;shift<24u;shift+=8u){
        const unsigned channel=(((source>>shift)&255u)*13u+((destination>>shift)&255u)*6u)/16u;
        result|=(uint32_t)(channel>255u?255u:channel)<<shift;
    }
    return result;
}

static int readonly_quad_contract(void)
{
#ifdef PORTABLE_ENABLE_SDL3
    CHECK(SDL_Init(0));
#endif
    const size_t maximum_stride=538u+7u,maximum_rows=526u,maximum_count=maximum_stride*maximum_rows;
    uint32_t *serial_pixels=malloc((maximum_count+16u)*sizeof *serial_pixels);
    uint32_t *quad_pixels=malloc((maximum_count+16u)*sizeof *quad_pixels);
    CHECK(serial_pixels&&quad_pixels);
    PortableScene3D serial={0},quad={0};
    void *pool=NULL;
    PortableSoftwareFrame sf={0},qf={0};
    for(unsigned pass=0;pass<4u;++pass){
        const unsigned large=pass%2u,model=pass/2u,scale=large?2u:1u;
        const size_t logical_width=large?269u:27u,logical_height=large?263u:8u;
        const size_t width=logical_width*scale,height=logical_height*scale,stride=width+7u,count=stride*height;
        for(size_t p=0;p<maximum_count+16u;++p)serial_pixels[p]=quad_pixels[p]=GUARD;
        sf=(PortableSoftwareFrame){.pixels=serial_pixels+7u,.width=logical_width,.height=logical_height,
            .scale=(uint8_t)scale,.stride=stride};
        qf=sf;qf.pixels=quad_pixels+7u;
        CHECK(portable_scene_3d_begin(&serial,&sf)&&portable_scene_3d_begin(&quad,&qf));
        if(pool)CHECK(quad.worker_pool==pool);
        const size_t top=large?3u:2u,bottom=height-(large?5u:2u);
        serial.clip_top=quad.clip_top=top;serial.clip_bottom=quad.clip_bottom=bottom;
        serial.model_pass=quad.model_pass=(uint8_t)model;
        serial.source_weight=quad.source_weight=(uint8_t)(large?13u:16u);
        serial.destination_weight=quad.destination_weight=(uint8_t)(large?6u:0u);
        for(size_t y=0;y<height;++y)for(size_t x=0;x<width;++x){
            sf.pixels[y*stride+x]=qf.pixels[y*stride+x]=BLUE;
            serial.depth[y*width+x]=quad.depth[y*width+x]=(x%7u==0u?1u:x%7u==1u?2u:5u)*65536u;
            serial.billboard_depth[y*width+x]=quad.billboard_depth[y*width+x]=
                (x%7u==2u?1u:x%7u==3u?2u:5u)*65536u;
        }
        const double right=(double)width,bottom_y=(double)height;
        const PortableScene3DVertex vertices[4]={{0,0,2,-13.0625,7.0625},
            {right*2,0,2,right/4-13.0625,7.0625},
            {right*2,-bottom_y*2,2,right/4-13.0625,bottom_y/2+7.0625},
            {0,-bottom_y*2,2,-13.0625,bottom_y/2+7.0625}};
        const PortableScene3DVertex first[3]={vertices[0],vertices[1],vertices[2]};
        const PortableScene3DVertex second[3]={vertices[0],vertices[2],vertices[3]};
        const PortableScene3DProjection projection={1,1,0,0,1,8};
        ReadonlyPattern pattern={{0u,GREEN,UINT32_C(0x01020304),UINT32_C(0x7f112233),UINT32_C(0xffffffff)}};
        for(unsigned draw=0;draw<2u;++draw){
            ReadonlyPattern before;memcpy(&before,&pattern,sizeof before);
            CHECK(portable_scene_3d_triangle(&serial,&projection,first,readonly_pattern,&pattern));
            CHECK(portable_scene_3d_triangle(&serial,&projection,second,readonly_pattern,&pattern));
            CHECK(portable_scene_3d_quad_readonly(&quad,&projection,vertices,readonly_pattern,&pattern));
            CHECK(!memcmp(&before,&pattern,sizeof pattern));
            CHECK(!memcmp(serial_pixels,quad_pixels,(maximum_count+16u)*sizeof *serial_pixels));
            CHECK(!memcmp(serial.depth,quad.depth,width*height*sizeof *serial.depth));
            CHECK(!memcmp(serial.billboard_depth,quad.billboard_depth,width*height*sizeof *serial.depth));
            pattern.colors[1]=RED; /* Legal update only after the joined call. */
        }
#if defined(PORTABLE_ENABLE_SDL3) && !defined(__EMSCRIPTEN__)
        if(large&&SDL_GetNumLogicalCPUCores()>1){CHECK(quad.worker_pool!=NULL&&quad.worker_pool_attempted);}
#else
        CHECK(quad.worker_pool==NULL);
#endif
        if(quad.worker_pool){if(pool)CHECK(pool==quad.worker_pool);pool=quad.worker_pool;}
        for(size_t y=0;y<height;++y)for(size_t x=0;x<stride;++x){
            const int64_t wx=readonly_floor16(4*(int64_t)x-207),wy=readonly_floor16(8*(int64_t)y+117);
            const unsigned index=(unsigned)(((wx+3*wy)%5+5)%5);
            const uint32_t source=pattern.colors[index];
            const int accepted=y>=top&&y<bottom&&(source>>24u)!=0u&&x%7u!=0u&&(!model||x%7u!=2u);
            uint32_t expected=BLUE;
            if(accepted){
                const uint32_t initial_source=index==1u?GREEN:source;
                expected=readonly_mix(source,readonly_mix(initial_source,BLUE,(int)large),(int)large);
            }
            CHECK(qf.pixels[y*stride+x]==(x>=width?GUARD:expected));
            if(x<width){
                const uint32_t g=quad.depth[y*width+x],o=quad.billboard_depth[y*width+x];
                CHECK(accepted?(g>=131071u&&g<=131072u):
                    g==(x%7u==0u?1u:x%7u==1u?2u:5u)*65536u);
                CHECK(accepted&&model?o==g:o==(x%7u==2u?1u:x%7u==3u?2u:5u)*65536u);
            }
        }
        for(size_t p=0;p<7u;++p)CHECK(quad_pixels[p]==GUARD);
        for(size_t p=count+7u;p<maximum_count+16u;++p)CHECK(quad_pixels[p]==GUARD);
        /* The second triangle alone is invalid. Preflight must reject the
         * entire quad, preserving an already drawn scene and both Z planes. */
        PortableScene3DVertex invalid[4];memcpy(invalid,vertices,sizeof invalid);invalid[3].u=NAN;
        pattern.colors[1]=UINT32_C(0xff804020);
        CHECK(!portable_scene_3d_quad_readonly(&quad,&projection,invalid,readonly_pattern,&pattern));
        CHECK(!memcmp(serial_pixels,quad_pixels,(maximum_count+16u)*sizeof *serial_pixels));
        CHECK(!memcmp(serial.depth,quad.depth,width*height*sizeof *serial.depth));
        CHECK(!memcmp(serial.billboard_depth,quad.billboard_depth,width*height*sizeof *serial.depth));
    }
    portable_scene_3d_close(&quad);portable_scene_3d_close(&serial);
    CHECK(!quad.worker_pool&&!quad.frame&&!quad.depth&&!quad.billboard_depth);
    /* Closing the pool/depth owner must not free its borrowed frame/sampler. */
    quad_pixels[0]=RED;serial_pixels[0]=GREEN;
    CHECK(quad_pixels[0]==RED&&serial_pixels[0]==GREEN);
    free(quad_pixels);free(serial_pixels);
#ifdef PORTABLE_ENABLE_SDL3
    SDL_Quit();
#endif
    return 0;
}

int main(void)
{
    if (readonly_quad_contract() || ordered_wide_fragments() || raster_fragment_contract() || mixed_depth_planes() || depth_and_coverage() || perspective_and_clipping() || ownership_and_invalid_values() ||
        row_clip_and_blend() || thin_and_horizontal_coverage() ||
        analytic_interpolation_and_extreme_fallback()) return 1;
    puts("portable scene3d: PASS");
    return 0;
}
