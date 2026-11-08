#include "model_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"model renderer line%d: %s\n",__LINE__,#c);return 1;} } while(0)
#define RED UINT32_C(0xffff0000)
#define BLUE UINT32_C(0xff0000ff)
#define GUARD UINT32_C(0x12345678)

typedef struct {
    uint8_t vertices[80],triangles[16];
    int32_t sine[1024];
    PortableModelBinding binding;
    PortableModelPose pose;
    PortableCameraState camera;
    PortableCameraProjection basis;
    PortableDriverProjectionRules rules;
} Fixture;

static void word(uint8_t *p,uint32_t value)
{
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));
}

static void fixture_init(Fixture *f)
{
    memset(f,0,sizeof *f);
    /* An orthogonal source camera: screen-right=X, screen-up=Z, forward=-Y.
     * The foot at Y=-192 has source distance256 with elevation256. */
    f->camera=(PortableCameraState){.screen_x=120,.screen_y=120,.elevation_q16=256*65536};
    f->basis.basis[0][0]=65536;f->basis.basis[1][2]=-65536;f->basis.basis[2][1]=-65536;
    f->sine[256]=32768;f->sine[768]=-32768;
    f->pose=(PortableModelPose){.position_y_q16=-192*65536,.base_inverse_q8=256,
        .inverse_x_q8=256,.inverse_y_q8=256};
    f->rules=(PortableDriverProjectionRules){{{511u,64u,-32,-46,1u},
        {1023u,32u,-16,-23,1u},{4096u,16u,-8,-11,1u}}};
    const int32_t points[4][5]={{-4,0,0,0,1},{4,0,0,1,1},{4,0,8,1,0},{-4,0,8,0,0}};
    for(unsigned i=0;i<4u;++i)for(unsigned j=0;j<5u;++j)
        word(f->vertices+i*20u+j*4u,(uint32_t)(points[i][j]*65536));
    const uint8_t indices[6]={0,1,2,0,2,3};
    for(unsigned i=0;i<6u;++i)f->triangles[(i/3u)*8u+(i%3u)*2u]=indices[i];
    f->binding=(PortableModelBinding){.kind=PORTABLE_MODEL_DRIVER,.target_id=1u,
        .scale_q16={65536,65536,65536},.mesh={.vertices=f->vertices,.triangles=f->triangles,
            .vertex_count=4u,.triangle_count=2u,.material_count=1u,.materials={{.argb=RED}}}};
}

static int render(Fixture *f,PortableScene3D *scene,PortableSoftwareFrame *frame)
{
    const size_t scale=portable_software_frame_scale(frame);
    const PortableScene3DProjection p={1024.0*(double)scale,1024.0*(double)scale,
        120.0*(double)scale,120.0*(double)scale,1.0,65535.0};
    return portable_model_render(scene,&f->binding,&f->pose,&f->camera,&f->basis,
        &p,&f->rules,f->sine,1024u);
}

static int geometry_and_ownership(void)
{
    Fixture f;fixture_init(&f);
    for(unsigned pass=0;pass<3u;++pass){
        const unsigned scale=pass==1u?4u:1u;
        const size_t width=pass==1u?373u:240u,stride=width*scale+3u,rows=160u*scale,count=stride*rows;
        uint32_t *pixels=malloc(count*sizeof *pixels),*base=malloc(count*sizeof *base);
        CHECK(pixels&&base);
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,.scale=(uint8_t)scale,.stride=stride};
        PortableScene3D scene={0};
        for(size_t i=0;i<count;++i)pixels[i]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame));
        Fixture before;memcpy(&before,&f,sizeof before);
        CHECK(render(&f,&scene,&frame));
        CHECK(!memcmp(&before,&f,sizeof f)&&!scene.model_pass);
        size_t coverage=0u;
        for(size_t y=0;y<rows;++y)for(size_t x=0;x<stride;++x){
            const int inside=x>=104u*scale&&x<136u*scale&&y>=88u*scale&&y<120u*scale;
            CHECK(pixels[y*stride+x]==(inside?RED:GUARD));
            if(inside){
                CHECK(scene.depth[y*width*scale+x]==256u*65536u);
                CHECK(scene.billboard_depth[y*width*scale+x]==256u*65536u);++coverage;
            }
        }
        CHECK(coverage==1024u*scale*scale);
        memcpy(base,pixels,count*sizeof *pixels);
        /* Signed authored binding scale reflects geometry without losing any
         * coverage or depending on triangle winding. */
        f.binding.scale_q16[0]=-65536;
        for(size_t i=0;i<count;++i)pixels[i]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
        CHECK(!memcmp(base,pixels,count*sizeof *pixels));
        f.binding.scale_q16[0]=65536;
        f.pose.height_q16=2*65536;
        for(size_t i=0;i<count;++i)pixels[i]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
        for(size_t y=0;y<rows;++y)for(size_t x=0;x<stride;++x){
            const int inside=x>=104u*scale&&x<136u*scale&&y>=80u*scale&&y<112u*scale;
            CHECK(pixels[y*stride+x]==(inside?RED:GUARD));
        }
        f.pose.height_q16=0;f.pose.inverse_y_q8=-256;
        for(size_t i=0;i<count;++i)pixels[i]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
        CHECK(pixels[121u*scale*stride+120u*scale]==RED);
        CHECK(pixels[119u*scale*stride+120u*scale]==GUARD);
        f.pose.inverse_y_q8=256;
        f.pose.source_driver=1u;f.pose.bob_offset=2;f.pose.inverse_x_q8=512;
        for(size_t i=0;i<count;++i)pixels[i]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
        for(size_t y=0;y<rows;++y)for(size_t x=0;x<stride;++x){
            const int inside=x>=112u*scale&&x<128u*scale&&y>=90u*scale&&y<122u*scale;
            CHECK(pixels[y*stride+x]==(inside?RED:GUARD));
        }
        f.pose.source_driver=0u;f.pose.bob_offset=0;f.pose.inverse_x_q8=256;
        /* A late invalid face must not leave the first valid triangle behind. */
        f.triangles[10]=99u;
        for(size_t i=0;i<count;++i)pixels[i]=GUARD;
        CHECK(portable_scene_3d_begin(&scene,&frame));
        CHECK(!render(&f,&scene,&frame));
        for(size_t i=0;i<count;++i)CHECK(pixels[i]==GUARD);
        for(size_t i=0;i<width*scale*rows;++i)
            CHECK(scene.depth[i]==UINT32_MAX&&scene.billboard_depth[i]==UINT32_MAX);
        f.triangles[10]=2u;
        portable_scene_3d_close(&scene);free(base);free(pixels);
    }
    return 0;
}

static int materials_and_clipping(void)
{
    Fixture f;fixture_init(&f);
    uint32_t pixels[240u*160u];
    PortableSoftwareFrame frame={.pixels=pixels,.width=240u,.height=160u};
    PortableScene3D scene={0};
    /* Two texels with alpha on opposite sides of the documented cutoff. */
    uint8_t texture[8]={255,255,255,127,255,255,255,128};
    f.binding.mesh.materials[0].texture=(PortableNativeImage){.pixels=texture,.width=2u,.height=1u};
    for(size_t i=0;i<240u*160u;++i)pixels[i]=BLUE;
    CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
    CHECK(pixels[100u*240u+108u]==BLUE&&scene.depth[100u*240u+108u]==UINT32_MAX);
    CHECK(pixels[100u*240u+128u]==RED&&scene.billboard_depth[100u*240u+128u]!=UINT32_MAX);
    /* UV outside the unit interval clamps; wrapping changes the selected texel. */
    for(unsigned i=0;i<4u;++i)word(f.vertices+i*20u+12u,UINT32_C(81920));
    CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
    CHECK(pixels[100u*240u+108u]==RED);
    f.binding.mesh.materials[0].flags=PORTABLE_MODEL_WRAP_U;
    for(size_t i=0;i<240u*160u;++i)pixels[i]=BLUE;
    CHECK(portable_scene_3d_begin(&scene,&frame)&&render(&f,&scene,&frame));
    CHECK(pixels[100u*240u+128u]==BLUE);
    f.binding.mesh.materials[0].texture.pixels=NULL;f.binding.mesh.materials[0].flags=0;
    const PortableScene3DProjection clipped={1024,1024,120,120,257,65535};
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(portable_model_render(&scene,&f.binding,&f.pose,&f.camera,&f.basis,&clipped,&f.rules,f.sine,1024));
    for(size_t i=0;i<240u*160u;++i)CHECK(scene.depth[i]==UINT32_MAX);
    /* A face straddling the near plane clips, rather than making the whole
     * replacement fall back or producing an unbounded triangle. */
    word(f.vertices+4u,(uint32_t)(-2*65536));
    word(f.vertices+24u,(uint32_t)(2*65536));
    const PortableScene3DProjection crossing={1024,1024,120,120,256,65535};
    for(size_t i=0;i<240u*160u;++i)pixels[i]=BLUE;
    CHECK(portable_scene_3d_begin(&scene,&frame));
    CHECK(portable_model_render(&scene,&f.binding,&f.pose,&f.camera,&f.basis,&crossing,&f.rules,f.sine,1024));
    size_t visible=0u;
    for(size_t i=0;i<240u*160u;++i)if(scene.depth[i]!=UINT32_MAX){
        CHECK(scene.depth[i]>=256u*65536u);++visible;
    }
    CHECK(visible>0u);
    portable_scene_3d_close(&scene);return 0;
}

int main(void)
{
    CHECK(!geometry_and_ownership());CHECK(!materials_and_clipping());
    puts("portable model renderer: PASS");return 0;
}
