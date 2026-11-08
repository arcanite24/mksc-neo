#ifdef PORTABLE_TEST_GLES
#include "render_gles.h"
#include "ui_gles_font.h"
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#include <SDL3/SDL_opengles2.h>
#undef SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#ifdef _WIN32
#define TEST_GL_APIENTRY __stdcall
#else
#define TEST_GL_APIENTRY
#endif
enum
{
    TEST_GL_VENDOR = 0x1f00,
    TEST_GL_RENDERER = 0x1f01,
    TEST_GL_VERSION = 0x1f02
};
#define PortableRenderGpu PortableRenderGles
#define portable_render_gpu_create portable_render_gles_create
#define portable_render_gpu_draw portable_render_gles_draw
#define portable_render_gpu_read_pixels portable_render_gles_read_pixels
#define portable_render_gpu_wait_idle portable_render_gles_wait_idle
#define portable_render_gpu_forget_meshes portable_render_gles_forget_meshes
#define portable_render_gpu_geometry_count portable_render_gles_geometry_count
#define portable_render_gpu_close portable_render_gles_close
#else
#include "render_gpu.h"
#include "ui_gpu.h"
#include "ui_font.h"
#include "modern_menu_host.h"
#endif
#include "ui_geometry.h"
#include "ui_font.h"
#include "modern_menu_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            fprintf(stderr, "GPU test line %d: %s (%s)\n", __LINE__, #value, SDL_GetError());      \
            result = 1;                                                                            \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)

static PortableRenderView view(size_t x, size_t y, size_t width, size_t height, uint32_t clear)
{
    const PortableRenderView result = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .right = {1, 0, 0},
        .up = {0, 1, 0},
        .forward = {0, 0, 1},
        .projection = {12, 12, (double)width / 2, (double)height / 2, 0.1, 100},
        .clear_argb = clear};
    return result;
}

#ifndef PORTABLE_TEST_GLES
/* World-only finish: individual controls, exact restoration, resize and pane isolation.
 * Timings include command submission + GPU completion, never claim hardware timestamps. */
static int hd2d_oracle(PortableRenderGpu *gpu, int benchmark)
{
    int result = 0;
    char error[256] = {0};
    const size_t width = benchmark == 2 ? 1707 : benchmark ? 1280 : 256;
    const size_t height = benchmark == 2 ? 960 : benchmark ? 720 : 144;
    uint32_t *pixels = calloc(width * height, sizeof *pixels);
    uint32_t *baseline = calloc(width * height, sizeof *baseline);
    uint8_t texels[32*32*4];
    for (unsigned y=0; y<32; ++y) for (unsigned x=0; x<32; ++x)
    {
        const unsigned at=(y*32+x)*4;
        texels[at]=((x/4+y/4)&1) ? 245 : 50;
        texels[at+1]=((x/4+y/4)&1) ? 220 : 70;
        texels[at+2]=100; texels[at+3]=255;
    }
    const PortableRenderVertex vertices[] = {
        {.position={-100,-100,9},.uv={0,0}}, {.position={100,-100,9},.uv={1,0}},
        {.position={100,100,9},.uv={1,1}}, {.position={-100,100,9},.uv={0,1}},
        {.position={-2,-2,8}}, {.position={2,-2,8}}, {.position={0,2,8}}};
    const uint32_t indices[] = {0,1,2,0,2,3,4,5,6};
    const PortableRenderMaterial material = {.argb=UINT32_MAX, .flags=4,
        .texture={.pixels=texels,.width=32,.height=32}};
    const PortableRenderRun run={0,9,0};
    const PortableRenderMesh mesh={.vertices=vertices,.vertex_count=7,.indices=indices,.index_count=9,
        .materials=&material,.material_count=1,.runs=&run,.run_count=1};
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance,&mesh);
    instance.hidden_views=2;
    instance.flags=PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
    instance.billboard_scale[0]=instance.billboard_scale[1]=instance.billboard_scale[2]=1;
    PortableRenderFrame frame={.instances=&instance,.instance_count=1,.view_count=2};
    frame.views[0]=view(0,0,width/2,height,0xff000000u);
    frame.views[1]=view(width/2,0,width-width/2,height,0xff102040u);
    frame.views[0].projection.focal_x=frame.views[0].projection.focal_y=(double)height;
    PortableSettings settings={0};
    CHECK(pixels && baseline);
    portable_render_gpu_forget_meshes(gpu);
    portable_render_gpu_set_hd2d(gpu,NULL);
    CHECK(portable_render_gpu_draw(gpu,&frame,width,height,error,sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu,baseline,width,error,sizeof error));
    static const char *const names[]={"off","tilt-shift","bloom","ao","grade","haze","lighting","all"};
    for (unsigned effect=0;effect<8;++effect)
    {
        memset(&settings,0,sizeof settings);
        if (effect==1 || effect==7) settings.hd2d_dof=1;
        if (effect==2 || effect==7) settings.hd2d_bloom=1;
        if (effect==3 || effect==7) settings.hd2d_ao=1;
        if (effect==4 || effect==7) settings.hd2d_grade=1;
        if (effect==5 || effect==7) settings.hd2d_fog=1;
        if (effect==6 || effect==7) settings.hd2d_light=1;
        portable_render_gpu_set_hd2d(gpu,&settings);
        CHECK(portable_render_gpu_draw(gpu,&frame,width,height,error,sizeof error));
        CHECK(portable_render_gpu_read_pixels(gpu,pixels,width,error,sizeof error));
        size_t changed=0;
        for (size_t y=0;y<height;++y) for (size_t x=0;x<width/2;++x)
            changed += pixels[y*width+x]!=baseline[y*width+x];
        if (effect>0) CHECK(changed>0);
        /* No glow/blur/AO or fog leaks into a depth-clear adjacent pane. */
        if (effect<4 || effect==5 || effect==6)
            for (size_t y=0;y<height;++y) CHECK(pixels[y*width+width/2]==0xff102040u);
        if (benchmark)
        {
            for (unsigned warm=0;warm<16;++warm)
                CHECK(portable_render_gpu_draw(gpu,&frame,width,height,error,sizeof error));
            CHECK(portable_render_gpu_wait_idle(gpu,error,sizeof error));
            const Uint64 start=SDL_GetTicksNS();
            for (unsigned batch=0;batch<12;++batch)
            {
                for (unsigned j=0;j<10;++j)
                    CHECK(portable_render_gpu_draw(gpu,&frame,width,height,error,sizeof error));
                CHECK(portable_render_gpu_wait_idle(gpu,error,sizeof error));
            }
            printf("HD2D completed workload %s %zux%zu: %.3f ms/frame (CPU submission + GPU, 120 frames)\n",
                names[effect], width, height, (double)(SDL_GetTicksNS()-start)/120000000.0);
        }
    }
    portable_render_gpu_set_hd2d(gpu,NULL);
    CHECK(portable_render_gpu_draw(gpu,&frame,width,height,error,sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu,pixels,width,error,sizeof error));
    CHECK(memcmp(pixels,baseline,width*height*sizeof *pixels)==0);
    puts("HD2D: PASS individual effects, split-view isolation, exact off restoration");
done:
    portable_render_gpu_set_hd2d(gpu,NULL);
    portable_render_gpu_forget_meshes(gpu);
    free(pixels); free(baseline);
    return result;
}
#endif

/* Immutable palette/material wrappers must not duplicate large terrain VBOs.
 * The same oracle is compiled for SDL_GPU and hardware GLES. */
static int palette_geometry_oracle(PortableRenderGpu *gpu)
{
    int result = 0;
    char error[256] = {0};
    enum { VARIANTS = 195, WIDTH = 64, HEIGHT = 32 };
    const PortableRenderVertex vertices[6] = {
        {.position = {-3, -2, 4}}, {.position = {-1, -2, 4}}, {.position = {-2, 2, 4}},
        {.position = {1, -2, 4}}, {.position = {3, -2, 4}}, {.position = {2, 2, 4}}};
    const uint32_t indices[6] = {0, 1, 2, 3, 4, 5};
    const uint8_t texel[1] = {1};
    uint8_t palettes[VARIANTS][4] = {{0}};
    PortableRenderMaterial materials[VARIANTS][2] = {0};
    PortableRenderMesh meshes[VARIANTS];
    const PortableRenderRun normal[2] = {{0, 3, 0}, {3, 3, 1}};
    const PortableRenderRun reversed[2] = {{0, 3, 1}, {3, 3, 0}};
    const PortableRenderRun whole = {0, 6, 0}, left = {0, 3, 0};
    for (unsigned i = 0; i < VARIANTS; ++i)
    {
        const uint16_t color = (uint16_t)(i == 0 ? 31 : i == 1 ? 31u << 5 :
                               i == 2 ? 31u << 10 : i == 3 ? 31u | (31u << 5) :
                               i + 1u);
        palettes[i][2] = (uint8_t)color;
        palettes[i][3] = (uint8_t)(color >> 8);
        materials[i][0] = (PortableRenderMaterial){.argb = UINT32_MAX, .flags = 4,
            .texture = {.pixels = texel, .width = 1, .height = 1,
                        .palette = palettes[i], .palette_count = 2}};
        meshes[i] = (PortableRenderMesh){.vertices = vertices, .indices = indices,
            .materials = materials[i], .runs = &whole, .vertex_count = 6, .index_count = 6,
            .material_count = 1, .run_count = 1};
    }
    materials[0][1] = materials[1][0];
    materials[1][1] = materials[3][0];
    materials[1][0] = materials[2][0];
    meshes[0].materials = materials[0]; meshes[0].material_count = 2;
    meshes[0].runs = normal; meshes[0].run_count = 2;
    meshes[1].materials = materials[1]; meshes[1].material_count = 2;
    meshes[1].runs = reversed; meshes[1].run_count = 2;
    PortableRenderInstance pair[2];
    portable_render_instance_identity(&pair[0], &meshes[0]);
    portable_render_instance_identity(&pair[1], &meshes[1]);
    pair[0].hidden_views = 14; pair[1].hidden_views = 13;
    PortableRenderFrame frame = {.instances = pair, .instance_count = 2, .view_count = 2};
    frame.views[0] = view(0, 0, 32, 32, UINT32_C(0xff112233));
    frame.views[1] = view(32, 0, 32, 32, UINT32_C(0xff223344));
    uint32_t pixels[WIDTH * HEIGHT];
    portable_render_gpu_forget_meshes(gpu);
    CHECK(!portable_render_gpu_geometry_count(gpu));
    for (unsigned warm = 0; warm < 3; ++warm)
    {
        CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
        CHECK(portable_render_gpu_geometry_count(gpu) == 1);
        CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
        CHECK(pixels[17 * WIDTH + 10] == UINT32_C(0xffff0000));
        CHECK(pixels[17 * WIDTH + 22] == UINT32_C(0xff00ff00));
        CHECK(pixels[17 * WIDTH + 42] == UINT32_C(0xffffff00));
        CHECK(pixels[17 * WIDTH + 54] == UINT32_C(0xff0000ff));
    }
    /* Many immutable palette/material states share these buffers.
     * Only textures/materials multiply, never geometry. */
    frame.instance_count = frame.view_count = 1;
    pair[0].hidden_views = 0;
    PortableRenderMaterial culled_materials[2] = {materials[0][0], materials[0][1]};
    culled_materials[0].flags = culled_materials[1].flags = 0;
    PortableRenderMesh culled = meshes[0]; culled.materials = culled_materials;
    pair[0].mesh = &culled;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 1);
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[17 * WIDTH + 10] == frame.views[0].clear_argb);
    for (unsigned i = 2; i < VARIANTS; ++i)
    {
        pair[0].mesh = &meshes[i];
        CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
        CHECK(portable_render_gpu_geometry_count(gpu) == 1);
    }
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    const uint32_t retained = portable_native_image_pixel(&materials[VARIANTS - 1][0].texture, 0, 0);
    CHECK(pixels[17 * WIDTH + 10] == retained);
    PortableRenderRun invalid_run = {0, 3, 9};
    PortableRenderMesh invalid = meshes[0]; invalid.runs = &invalid_run; invalid.run_count = 1;
    pair[1].mesh = &invalid; pair[1].hidden_views = 0;
    frame.instance_count = 2;
    CHECK(!portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 1);
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[17 * WIDTH + 10] == retained);
    frame.instance_count = 1;
    PortableRenderMesh subset = meshes[0];
    subset.index_count = 3; subset.runs = &left; subset.run_count = subset.material_count = 1;
    pair[0].mesh = &subset;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 2);
    PortableRenderMesh vertex_subset = subset; vertex_subset.vertex_count = 3;
    pair[0].mesh = &vertex_subset;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 3);
    PortableRenderVertex copied_vertices[6];
    memcpy(copied_vertices, vertices, sizeof vertices);
    PortableRenderMesh distinct = meshes[0]; distinct.vertices = copied_vertices;
    pair[0].mesh = &distinct;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 4);
    uint32_t copied_indices[6]; memcpy(copied_indices, indices, sizeof indices);
    PortableRenderMesh distinct_indices = meshes[0]; distinct_indices.indices = copied_indices;
    pair[0].mesh = &distinct_indices;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 5);
    portable_render_gpu_forget_meshes(gpu);
    CHECK(!portable_render_gpu_geometry_count(gpu));
    frame.instance_count = frame.view_count = 2;
    pair[0].mesh = &meshes[0]; pair[0].hidden_views = 14;
    pair[1].mesh = &meshes[1]; pair[1].hidden_views = 13;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 1);
#ifdef PORTABLE_TEST_GLES
    portable_render_gles_invalidate(gpu);
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu) == 1);
#endif
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[17 * WIDTH + 10] == UINT32_C(0xffff0000));
    CHECK(pixels[17 * WIDTH + 54] == UINT32_C(0xff0000ff));
    CHECK(vertices[0].position[0] == -3 && indices[5] == 5 && texel[0] == 1 &&
          palettes[0][2] == 31 && palettes[1][2] == 0xe0 && palettes[1][3] == 3);
    puts("GPU geometry cache: PASS indexed palette variants shared buffers warm runs reset rollback");
done:
    portable_render_gpu_forget_meshes(gpu);
    return result;
}

/* A race's sprite frames outnumber every cache: entries undrawn for a second
 * make room, while the course mesh drawn each frame stays resident. */
static int cache_eviction_oracle(PortableRenderGpu *gpu)
{
    int result = 0;
    char error[256] = {0};
    enum { SPRITES = 4300, SIZE = 8 };
    PortableRenderVertex (*vertices)[3] = calloc(SPRITES, sizeof *vertices);
    uint8_t (*texels)[4] = calloc(SPRITES, sizeof *texels);
    PortableRenderMaterial *materials = calloc(SPRITES, sizeof *materials);
    PortableRenderMesh *meshes = calloc(SPRITES + 1u, sizeof *meshes);
    const uint32_t indices[3] = {0, 1, 2};
    const PortableRenderRun run = {0, 3, 0};
    const PortableRenderMaterial course_material = {.argb = UINT32_C(0xff00ff00), .flags = 4};
    const PortableRenderVertex course[3] = {{.position = {-9, -9, 5}}, {.position = {9, -9, 5}},
                                            {.position = {0, 9, 5}}};
    uint32_t pixels[SIZE * SIZE];
    CHECK(vertices && texels && materials && meshes);
    meshes[SPRITES] = (PortableRenderMesh){.vertices = course, .indices = indices, .materials = &course_material,
        .runs = &run, .vertex_count = 3, .index_count = 3, .material_count = 1, .run_count = 1};
    for (unsigned i = 0; i < SPRITES; ++i)
    {
        vertices[i][0] = (PortableRenderVertex){.position = {-1, -1, 4}};
        vertices[i][1] = (PortableRenderVertex){.position = {1, -1, 4}};
        vertices[i][2] = (PortableRenderVertex){.position = {0, 1, 4}};
        texels[i][0] = (uint8_t)i; texels[i][2] = 0xff; texels[i][3] = 0xff;
        materials[i] = (PortableRenderMaterial){.argb = UINT32_MAX, .flags = 4,
            .texture = {.pixels = texels[i], .width = 1, .height = 1}};
        meshes[i] = meshes[SPRITES];
        meshes[i].vertices = vertices[i];
        meshes[i].materials = &materials[i];
    }
    PortableRenderInstance instances[2];
    portable_render_instance_identity(&instances[0], &meshes[SPRITES]);
    PortableRenderFrame frame = {.instances = instances, .instance_count = 2, .view_count = 1};
    frame.views[0] = view(0, 0, SIZE, SIZE, UINT32_C(0xff000000));
    portable_render_gpu_forget_meshes(gpu);
    for (unsigned i = 0; i < SPRITES; ++i)
    {
        portable_render_instance_identity(&instances[1], &meshes[i]);
        CHECK(portable_render_gpu_draw(gpu, &frame, SIZE, SIZE, error, sizeof error));
        CHECK(portable_render_gpu_geometry_count(gpu) <= PORTABLE_RENDER_INSTANCE_LIMIT);
    }
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, SIZE, error, sizeof error));
    const uint32_t centre = pixels[(SIZE / 2) * SIZE + SIZE / 2];
    CHECK(centre != UINT32_C(0xff000000) && centre != UINT32_C(0xff00ff00));
    CHECK(pixels[SIZE * (SIZE - 1)] == UINT32_C(0xff00ff00) || pixels[0] == UINT32_C(0xff00ff00));
    puts("GPU cache eviction: PASS sprite frames past every cache limit, course mesh resident");
done:
    portable_render_gpu_forget_meshes(gpu);
    free(vertices); free(texels); free(materials); free(meshes);
    return result;
}

/* Immutable geometry proof is published only after complete frame validation
 * and upload. New keys and descriptor changes cannot bypass preflight. */
static int geometry_validation_oracle(PortableRenderGpu *gpu)
{
    int result=0;
    char error[192];
    PortableRenderVertex vertices[3]={
        {.position={-1,-1,4}}, {.position={1,-1,4}}, {.position={0,1,4}}};
    PortableRenderVertex other[3];memcpy(other,vertices,sizeof other);
    const uint32_t indices[3]={0,1,2};
    PortableRenderMaterial material={.argb=UINT32_C(0xffff0000),.flags=4};
    PortableRenderRun run={0,3,0};
    PortableRenderMesh mesh={.vertices=vertices,.indices=indices,.materials=&material,.runs=&run,
        .vertex_count=3,.index_count=3,.material_count=1,.run_count=1};
    PortableRenderMesh unknown=mesh;unknown.vertices=other;
    PortableRenderInstance instances[2];
    portable_render_instance_identity(&instances[0],&mesh);
    portable_render_instance_identity(&instances[1],&unknown);
    PortableRenderFrame frame={.instances=instances,.instance_count=1,.view_count=1};
    frame.views[0]=view(0,0,32,32,UINT32_C(0xff112233));
    uint32_t pixels[1024],retained[1024];
    portable_render_gpu_forget_meshes(gpu);
    CHECK(portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu)==1);
    CHECK(portable_render_gpu_read_pixels(gpu,retained,32,error,sizeof error));
    /* Entire frame rejects before uploading even its earlier valid new key. */
    PortableRenderVertex bad_vertices[3];memcpy(bad_vertices,vertices,sizeof bad_vertices);
    bad_vertices[0].position[0]=NAN;
    PortableRenderMesh invalid=mesh;invalid.vertices=bad_vertices;
    instances[0].mesh=&unknown;instances[1].mesh=&invalid;frame.instance_count=2;
    CHECK(!portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu)==1);
    CHECK(portable_render_gpu_read_pixels(gpu,pixels,32,error,sizeof error));
    CHECK(!memcmp(pixels,retained,sizeof pixels));
    instances[0].mesh=&mesh;frame.instance_count=1;
    mesh.vertices=other;
    CHECK(!portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    CHECK(strstr(error,"geometry changed"));
    mesh.vertices=vertices;--mesh.vertex_count;
    CHECK(!portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    ++mesh.vertex_count;material.flags=48;
    CHECK(!portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    material.flags=4;run.material=1;
    CHECK(!portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    run.material=0;
    CHECK(portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu)==1);
    /* Publishing changed arrays explicitly retires uploaded geometry first. */
    portable_render_gpu_forget_meshes(gpu);vertices[0].uv[0]=NAN;
    CHECK(!portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu)==0);
    vertices[0].uv[0]=0;
    CHECK(portable_render_gpu_draw(gpu,&frame,32,32,error,sizeof error));
    CHECK(portable_render_gpu_geometry_count(gpu)==1);
    puts("GPU immutable geometry validation: PASS warm proof new-key rollback descriptor forget");
done:
    portable_render_gpu_forget_meshes(gpu);
    return result;
}

/* Each pane partitions source rows independently, even at nonintegral scale.
 * Reversing draw order cannot cover the other interval or leak the last
 * instance's scissor into the next view/frame. */
static int clip_oracle(PortableRenderGpu *gpu)
{
    int result = 0;
    char error[192];
    enum { WIDTH = 77, HEIGHT = 67 };
    const PortableRenderVertex vertices[] = {
        {{-100,-100,4},{0,0,-1},{0,0}}, {{100,-100,4},{0,0,-1},{1,0}},
        {{100,100,4},{0,0,-1},{1,1}}, {{-100,100,4},{0,0,-1},{0,1}}};
    const uint32_t indices[] = {0,1,2,0,2,3};
    const PortableRenderRun run = {0,6,0};
    const PortableRenderMaterial red = {.argb = UINT32_C(0xffff0000), .flags = 4};
    const PortableRenderMaterial green = {.argb = UINT32_C(0xff00ff00), .flags = 4};
    PortableRenderMesh meshes[2] = {
        {.vertices=vertices,.indices=indices,.materials=&red,.runs=&run,
         .vertex_count=4,.index_count=6,.material_count=1,.run_count=1},
        {.vertices=vertices,.indices=indices,.materials=&green,.runs=&run,
         .vertex_count=4,.index_count=6,.material_count=1,.run_count=1}};
    PortableRenderInstance instances[2];
    portable_render_instance_identity(&instances[0], &meshes[0]);
    portable_render_instance_identity(&instances[1], &meshes[1]);
    instances[0].clip_y_end = instances[1].clip_y_begin = 24;
    instances[0].clip_y_extent = instances[1].clip_y_end = instances[1].clip_y_extent = 160;
    PortableRenderFrame frame = {.instances=instances,.instance_count=2,.view_count=4};
    frame.views[0]=view(0,0,38,33,0xff112233);
    frame.views[1]=view(38,0,39,33,0xff223344);
    frame.views[2]=view(0,33,38,34,0xff334455);
    frame.views[3]=view(38,33,39,34,0xff445566);
    uint32_t pixels[WIDTH * HEIGHT], retained[WIDTH * HEIGHT];
    portable_render_gpu_forget_meshes(gpu);
    for (unsigned order = 0; order < 2; ++order)
    {
        CHECK(portable_render_gpu_draw(gpu,&frame,WIDTH,HEIGHT,error,sizeof error));
        CHECK(portable_render_gpu_read_pixels(gpu,pixels,WIDTH,error,sizeof error));
        for (unsigned pane = 0; pane < 4; ++pane)
        {
            const PortableRenderView *camera = &frame.views[pane];
            const size_t boundary = (24u * camera->height + 159u) / 160u;
            for (size_t y=0; y<camera->height; ++y)
                for (size_t x=0; x<camera->width; ++x)
                    CHECK(pixels[(camera->y+y)*WIDTH+camera->x+x] ==
                          (y<boundary ? red.argb : green.argb));
        }
        PortableRenderInstance swap=instances[0]; instances[0]=instances[1]; instances[1]=swap;
    }
    memcpy(retained,pixels,sizeof pixels);
    instances[1].clip_y_end = 161;
    CHECK(!portable_render_gpu_draw(gpu,&frame,WIDTH,HEIGHT,error,sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu,pixels,WIDTH,error,sizeof error));
    CHECK(!memcmp(pixels,retained,sizeof pixels));
    instances[0].clip_y_begin=instances[0].clip_y_end=instances[0].clip_y_extent=0;
    frame.instance_count=1;
    CHECK(portable_render_gpu_draw(gpu,&frame,WIDTH,HEIGHT,error,sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu,pixels,WIDTH,error,sizeof error));
    for (unsigned i=0;i<WIDTH*HEIGHT;++i) CHECK(pixels[i]==red.argb);
    puts("GPU source-row clipping: PASS four panes fractional scales ordering rollback reset");
done:
    portable_render_gpu_forget_meshes(gpu);
    return result;
}

static void ui_oracle_scaled(UiCanvas *canvas, unsigned scene, float font_scale)
{
    ui_canvas_reset_mode(canvas, 1);
    ui_canvas_clear(canvas, 0u);
    ui_canvas_clip(canvas, 4, 3, 122, 91);
    if (scene == 0u)
    {
        UiPaint paint = ui_paint_linear(12, 8, UINT32_C(0xc0e02080), 87, 55, UINT32_C(0x6010b0f0));
        ui_paint_stop(&paint, .35f, UINT32_C(0x90ffff20));
        ui_fill_round_rect(canvas, 12.25f, 8.5f, 70, 42, 9, .25f, &paint, 2, .7f);
        ui_stroke_round_rect(canvas, 9, 5, 76, 48, 11, .25f, 2.25f, UINT32_C(0x8040ff80), 1);
    }
    else if (scene == 1u)
    {
        const UiPaint paint = ui_paint_solid(UINT32_C(0xb0e08030));
        ui_fill_ellipse(canvas, 34.25f, 30.75f, 25, 13, &paint, 2.5f, .8f);
        ui_fill_circle(canvas, 91.5f, 34.25f, 20, &paint, 1, .7f);
        ui_stroke_circle(canvas, 54.5f, 69.5f, 17.25f, 3.5f, UINT32_C(0xc020e0ff), .6f);
    }
    else if (scene == 2u)
    {
        const UiPoint points[4] = {{-4, 13}, {73.25f, 5.5f}, {100.5f, 75.25f}, {10, 86}};
        const UiPaint paint = ui_paint_solid(UINT32_C(0x9080c030));
        ui_fill_convex(canvas, points, 4, &paint, 2.25f, .8f);
    }
    else if (scene == 3u)
    {
        const uint32_t pixels[4] = {UINT32_C(0x80ff2040), UINT32_C(0x0000ff00),
            UINT32_C(0xc04020ff), UINT32_C(0xffffffff)};
        const UiImage image = {pixels, 2, 2};
        ui_draw_image(canvas, &image, 8.25f, 7.5f, 36, 32, .8f, UI_IMAGE_SMOOTH, UINT32_C(0xc0ffd080), 7);
        ui_draw_image(canvas, &image, 53.5f, 7.25f, 29, 31, .7f,
            UI_IMAGE_NEAREST | UI_IMAGE_GRAY, UINT32_MAX, 0);
        ui_draw_image_region(canvas, &image, .25f, .5f, 1.5f, 1.5f, 18.5f, 52.25f, 55, 32, .9f,
            UI_IMAGE_SILHOUETTE, UINT32_C(0x9070f020), 5);
    }
    else if (scene == 4u)
    {
        ui_fill_rect(canvas, 11, 10, 86, 62, UINT32_C(0x80e03050), .75f);
        ui_tint_rect(canvas, 22, 15, 106, 81, UINT32_C(0xff30d080), .4f);
    }
    else
    {
        UiTextStyle style = ui_text_style((UiFontFace)(scene - 5u), 42 * font_scale, UINT32_C(0xc0e0a030));
        style.outline = 2.25f * font_scale;
        style.outline_color = UINT32_C(0xb02090e0);
        style.shadow_color = UINT32_C(0x90000080);
        style.shadow_x = 1.375f * font_scale;
        style.shadow_y = 2.625f * font_scale;
        style.shadow_blur = 4.25f * font_scale;
        ui_text_draw(canvas, &style, -3.75f, 59.625f, UI_ALIGN_LEFT,
            "AV\xc3\xa9\xe2\x86\x92", .9f);
    }
}

#ifndef PORTABLE_TEST_GLES
static void ui_oracle_scene(UiCanvas *canvas, unsigned scene)
{
    ui_oracle_scaled(canvas, scene, 1.0f);
}

static int ui_pixel_oracles(SDL_Renderer *renderer, UiGpu *gpu, UiDrawList *list)
{
    int result = 0;
    SDL_Surface *surface = NULL;
    uint32_t reference[128 * 96];
    const SDL_FRect destination = {0, 0, 128, 96};
    char error[256] = {0};
    for (unsigned scene = 0u; scene < 5u + UI_FONT_COUNT; ++scene)
    {
        UiCanvas cpu, recorded;
        ui_canvas_init(&cpu, reference, 128, 96, 128);
        ui_oracle_scene(&cpu, scene);
        ui_draw_list_reset(list);
        ui_canvas_init(&recorded, NULL, 128, 96, 128);
        ui_canvas_bind_sink(&recorded, ui_draw_list_sink(list));
        ui_oracle_scene(&recorded, scene);
        CHECK(ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
        CHECK(ui_gpu_draw(gpu, &destination, error, sizeof error));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = converted;
        CHECK(surface != NULL);
        const uint32_t *pixels = surface->pixels;
        const size_t stride = (size_t)surface->pitch / sizeof *pixels;
        for (size_t y = 0; y < 96; ++y)
            for (size_t x = 0; x < 128; ++x)
                for (unsigned shift = 0; shift <= 24; shift += 8)
                {
                    const int actual = (int)((pixels[y * stride + x] >> shift) & 255u);
                    const int expected = (int)((reference[y * 128 + x] >> shift) & 255u);
                    const int difference = actual > expected ? actual - expected : expected - actual;
                    /* Controlled primitive cases bound UNORM filtering/blend
                     * and the CPU gradient LUT's byte rounding separately from
                     * exact hard-fill/interior checks above. No whole-menu
                     * image is accepted using this tolerance. */
                    if (difference > 3)
                        fprintf(stderr, "UI oracle scene=%u x=%zu y=%zu channel=%u cpu=%08x gpu=%08x\n",
                            scene, x, y, shift, reference[y * 128 + x], pixels[y * stride + x]);
                    CHECK(difference <= 3);
                    if (x < 4 || x >= 122 || y < 3 || y >= 91) CHECK(actual == 0);
                }
        SDL_DestroySurface(surface);
        surface = NULL;
        CHECK(ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
        CHECK(ui_gpu_uploaded_bytes(gpu) == 0u);
        CHECK(ui_gpu_draw(gpu, &destination, error, sizeof error));
        CHECK(SDL_FlushRenderer(renderer));
    }
    puts("GPU UI pixels: PASS isolated shapes gradients images tint all-font passes clips warm caches");
done:
    if (surface != NULL) SDL_DestroySurface(surface);
    return result;
}

static int ui_cache_eviction(SDL_Renderer *renderer, UiGpu *gpu, UiDrawList *list)
{
    int result = 0;
    char error[256] = {0};
    uint32_t *source = malloc(512u * 512u * sizeof *source);
    SDL_Surface *surface = NULL;
    CHECK(source != NULL);
    const UiImage image = {source, 512, 512};
    const SDL_FRect destination = {0, 0, 128, 96};
    for (unsigned frame = 0; frame < 11; ++frame)
    {
        const unsigned revision = frame == 10u ? 0u : frame;
        const uint32_t color = UINT32_C(0xff001040) | ((revision * 23u) << 16);
        for (size_t i = 0; i < 512u * 512u; ++i) source[i] = color;
        ui_draw_list_reset(list);
        UiCanvas canvas;
        ui_canvas_init_transparent(&canvas, NULL, 128, 96, 128);
        ui_canvas_bind_sink(&canvas, ui_draw_list_sink(list));
        ui_canvas_clear(&canvas, 0u);
        ui_draw_image(&canvas, &image, 0, 0, 128, 96, 1, UI_IMAGE_NEAREST, UINT32_MAX, 0);
        CHECK(ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
        /* This churn exceeds the total cache budget. The first revision must
         * upload again at the end; the currently prepared white/image pair
         * must remain pinned while old entries are retired. */
        CHECK(ui_gpu_uploaded_bytes(gpu) >= 512u * 512u * 4u);
        CHECK(ui_gpu_draw(gpu, &destination, error, sizeof error));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = converted;
        CHECK(surface != NULL);
        uint32_t actual;
        memcpy(&actual, (const unsigned char *)surface->pixels + 48u * (size_t)surface->pitch + 64u * 4u,
            sizeof actual);
        for (unsigned shift = 0; shift <= 24; shift += 8)
        {
            const int a = (int)((actual >> shift) & 255u), b = (int)((color >> shift) & 255u);
            CHECK(abs(a - b) <= 1);
        }
        SDL_DestroySurface(surface);
        surface = NULL;
        CHECK(ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
        CHECK(ui_gpu_uploaded_bytes(gpu) == 0u);
        CHECK(ui_gpu_draw(gpu, &destination, error, sizeof error));
        CHECK(SDL_FlushRenderer(renderer));
    }
    puts("GPU UI cache: PASS same-address revisions bounded eviction current-frame pinning warm re-use");
done:
    if (surface != NULL) SDL_DestroySurface(surface);
    free(source);
    return result;
}

static int ui_only_layer(ModernMenu *menu, UiCanvas *canvas, void *renderer,
                          void **background, void *owner)
{
    (void)menu; (void)renderer; (void)owner;
    *background = NULL;
    ui_canvas_clear(canvas, UINT32_C(0xff204060));
    ui_fill_rect(canvas, 4, 4, 20, 20, UINT32_C(0xffff8000), 1);
    return 1;
}

static int ui_pixel_only_layer(ModernMenu *menu, UiCanvas *canvas, void *renderer,
                               void **background, void *owner)
{
    (void)menu; (void)renderer; (void)owner;
    *background = NULL;
    if (canvas->sink != NULL) canvas->sink->failed = 1u;
    if (canvas->pixels == NULL) return 0;
    for (int y = 0; y < canvas->height; ++y)
        for (int x = 0; x < canvas->width; ++x)
            canvas->pixels[(size_t)y * (size_t)canvas->stride + (size_t)x] = UINT32_C(0xff550088);
    return 1;
}

static int ui_failed_overlay(ModernMenu *menu, UiCanvas *canvas, void *renderer,
                              void **background, void *owner)
{
    (void)menu; (void)renderer;
    *background = owner;
    ui_canvas_reset_mode(canvas, 1);
    ui_canvas_clear(canvas, 0u);
    ui_fill_rect(canvas, 4, 4, 20, 20, UINT32_C(0xffff8000), 1);
    if (canvas->sink != NULL) canvas->sink->failed = 1u;
    return 1;
}

static int ui_host_lifecycle(SDL_Renderer *renderer)
{
    int result = 0;
    ModernMenuHost host = {0};
    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_Texture *background = NULL;
    SDL_Texture *output = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 3840, 2160);
    uint32_t *race = NULL;
    SDL_Surface *capture = NULL;
    CHECK(output != NULL && SDL_SetRenderTarget(renderer, output));
    ModernMenuSetup setup = {.screen = MODERN_SETUP_IMPORT, .tick = 32u};
    CHECK(modern_menu_host_present_setup(&host, renderer, &setup));
    CHECK(host.accelerated_frame && host.width == 3840 && host.height == 2160);
    CHECK(host.pixels == NULL && !host.pixels_valid);
    CHECK(SDL_SetRenderTarget(renderer, host.composed_target));
    capture = SDL_RenderReadPixels(renderer, NULL); /* Explicit test capture only. */
    CHECK(capture != NULL && capture->w == 3840 && capture->h == 2160);
    SDL_DestroySurface(capture);
    capture = NULL;
    CHECK(host.pixels == NULL && !host.pixels_valid);
    CHECK(SDL_SetRenderTarget(renderer, previous));
    SDL_DestroyTexture(output);
    output = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 320, 180);
    CHECK(output != NULL && SDL_SetRenderTarget(renderer, output));
    host.fixed_width = 320;
    host.fixed_height = 180;
    race = malloc(240u * 160u * sizeof *race);
    CHECK(race != NULL);
    for (size_t i = 0; i < 240u * 160u; ++i) race[i] = UINT32_C(0xff104080);
    PortableSoftwareFrame frame;
    CHECK(portable_software_frame_init(&frame, race, 240, 160));
    PortablePauseMenu pause;
    memset(&pause, 0, sizeof pause);
    pause.item_count = 2u;
    pause.items[0] = PORTABLE_PAUSE_ACTION_CONTINUE;
    pause.items[1] = PORTABLE_PAUSE_ACTION_QUIT;
    CHECK(modern_menu_host_present_pause(&host, renderer, &frame, 1, &pause, "GPU lifecycle", NULL));
    CHECK(host.accelerated_frame && host.race_texture != NULL && !host.pixels_valid);
    CHECK(host.race[0] == UINT32_C(0xff104080));
    for (size_t i = 0; i < 240u * 160u; ++i) race[i] = UINT32_C(0xffe01020);
    SDL_Event reset = {.type = SDL_EVENT_RENDER_TARGETS_RESET};
    modern_menu_host_event(&host, &reset);
    CHECK(host.race_texture == NULL && host.pending_background == NULL);
    CHECK(modern_menu_host_present_pause(&host, renderer, &frame, 1, &pause, "GPU lifecycle", NULL));
    CHECK(host.accelerated_frame && host.race[0] == UINT32_C(0xff104080));
    CHECK(host.race_texture != NULL && !host.pixels_valid);
    modern_menu_host_pause_closed(&host);
    ModernMenuResults results = {.title = "Lifecycle results", .subtitle = "GPU retained card",
        .tick = 100u, .row_count = 1u, .item_count = 1u, .labels = {"Retry"}};
    results.rows[0].driver_id = 1u;
    results.rows[0].rank = 1u;
    memcpy(results.rows[0].value, "1:23.45", 8u);
    CHECK(modern_menu_host_present_results(&host, renderer, &frame, 1, &results, NULL));
    CHECK(host.accelerated_frame && host.card_target != NULL && host.card_pixels == NULL);
    CHECK(!host.pixels_valid);
    reset.type = SDL_EVENT_RENDER_DEVICE_RESET;
    modern_menu_host_event(&host, &reset);
    ++results.tick;
    CHECK(modern_menu_host_present_results(&host, renderer, &frame, 1, &results, NULL));
    CHECK(host.accelerated_frame && host.card_target != NULL && host.card_pixels == NULL);
    CHECK(!host.pixels_valid);
    modern_menu_host_results_closed(&host);
    modern_menu_host_content_changed(&host);
    CHECK(host.race_texture == NULL && host.card_target == NULL && host.pending_background == NULL);
    CHECK(modern_menu_host_present_setup(&host, renderer, &setup));
    CHECK(host.accelerated_frame && !host.pixels_valid);
    CHECK(modern_menu_host_present_layer(&host, renderer, ui_only_layer, NULL));
    CHECK(host.accelerated_frame && !host.pixels_valid);
    CHECK(modern_menu_host_present_layer(&host, renderer, ui_pixel_only_layer, NULL));
    CHECK(!host.accelerated_frame && host.pixels_valid && host.pixels[0] == UINT32_C(0xff550088));
    background = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 320, 180);
    CHECK(background != NULL && SDL_SetRenderTarget(renderer, background));
    CHECK(SDL_SetRenderDrawColor(renderer, 16, 64, 128, 255) && SDL_RenderClear(renderer));
    CHECK(SDL_SetRenderTarget(renderer, output));
    CHECK(modern_menu_host_present_layer(&host, renderer, ui_failed_overlay, background));
    CHECK(!host.accelerated_frame && !host.pixels_valid);
    CHECK(modern_menu_host_write_ppm(&host, "ui-fallback.ppm"));
    CHECK(host.pixels_valid && host.pixels[0] == UINT32_C(0xff104080));
    CHECK(host.pixels[10u * 320u + 10u] == UINT32_C(0xffff8000));
    puts("GPU UI host: PASS native4K no-live-readback frozen pause results cards resize device-reset content-reset");
done:
    (void)remove("ui-fallback.ppm");
    SDL_DestroyTexture(background);
    if (capture != NULL) SDL_DestroySurface(capture);
    modern_menu_host_close(&host);
    free(race);
    (void)SDL_SetRenderTarget(renderer, previous);
    if (output != NULL) SDL_DestroyTexture(output);
    return result;
}

static int ui_geometry(SDL_Renderer *renderer)
{
    int result = 0;
    char error[256] = {0};
    UiGpu *gpu = NULL;
    UiDrawList *list = NULL;
    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 128, 96);
    SDL_Surface *surface = NULL;
    CHECK(target != NULL && SDL_SetRenderTarget(renderer, target));
    gpu = ui_gpu_create(renderer, error, sizeof error);
    CHECK(gpu != NULL);
    list = ui_draw_list_create(4096u, 16u * 1024u * 1024u);
    CHECK(list != NULL);
    UiCanvas canvas;
    ui_canvas_init(&canvas, NULL, 128, 96, 128);
    ui_canvas_bind_sink(&canvas, ui_draw_list_sink(list));
    ui_canvas_clip(&canvas, 4, 4, 120, 90);
    ui_canvas_clear(&canvas, UINT32_C(0xff123456)); /* Clear ignores this clip. */
    ui_fill_rect(&canvas, 10, 10, 20, 20, UINT32_C(0xffe01040), 1);
    const UiPaint paint = ui_paint_solid(UINT32_C(0xff10c080));
    ui_fill_round_rect(&canvas, 40, 10, 30, 25, 7, .15f, &paint, 1, 1);
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, 32, UINT32_MAX);
    ui_text_draw(&canvas, &style, 15.25f, 78.5f, UI_ALIGN_LEFT, "AV", 1);
    uint32_t texels[2] = {UINT32_C(0xffff0000), UINT32_C(0x0000ff00)};
    const UiImage image = {texels, 2, 1};
    ui_draw_image(&canvas, &image, 90, 10, 16, 16, 1, UI_IMAGE_SMOOTH, UINT32_MAX, 0);
    const SDL_FRect destination = {0, 0, 128, 96};
    CHECK(ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
    CHECK(ui_gpu_uploaded_bytes(gpu) > 0u);
    CHECK(ui_gpu_draw(gpu, &destination, error, sizeof error));
    CHECK(SDL_FlushRenderer(renderer));
    surface = SDL_RenderReadPixels(renderer, NULL);
    CHECK(surface != NULL);
    SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surface);
    surface = converted;
    CHECK(surface != NULL);
    const uint32_t *pixels = surface->pixels;
    const size_t stride = (size_t)surface->pitch / sizeof *pixels;
    CHECK(pixels[0] == UINT32_C(0xff123456));
    CHECK(pixels[15 * stride + 15] == UINT32_C(0xffe01040));
    CHECK(pixels[20 * stride + 55] == UINT32_C(0xff10c080));
    unsigned white = 0;
    for (size_t y = 45; y < 85; ++y)
        for (size_t x = 10; x < 70; ++x) white += pixels[y * stride + x] == UINT32_MAX;
    CHECK(white > 20u);
    /* Premultiplied filtering cannot leak the invisible green texel. */
    CHECK(((pixels[15 * stride + 98] >> 8) & 255u) <= 0x34u);
    CHECK(ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
    CHECK(ui_gpu_uploaded_bytes(gpu) == 0u);
    CHECK(ui_gpu_draw(gpu, &destination, error, sizeof error));
    CHECK(SDL_FlushRenderer(renderer));
    ui_draw_list_reset(list);
    CHECK(!ui_gpu_prepare(gpu, list, 128, 96, error, sizeof error));
    CHECK(!ui_gpu_draw(gpu, &destination, error, sizeof error));
    CHECK(ui_pixel_oracles(renderer, gpu, list) == 0);
    CHECK(ui_cache_eviction(renderer, gpu, list) == 0);
    CHECK(ui_host_lifecycle(renderer) == 0);
    printf("GPU UI: PASS hard fills rounded interior fractional glyphs premultiplied image warm-cache rollback\n");
done:
    if (surface != NULL) SDL_DestroySurface(surface);
    ui_gpu_close(gpu);
    ui_draw_list_close(list);
    ui_font_release();
    (void)SDL_SetRenderTarget(renderer, previous);
    if (target != NULL) SDL_DestroyTexture(target);
    return result;
}
#endif

/* Glyph coverage may differ at antialias edges, but not in genuinely flat
 * RGBA regions. Sum absolute errors so opposite edges cannot cancel. */
static int glyph_coverage_matches(const uint32_t *reference, const SDL_Surface *surface,
                                  size_t width, size_t height)
{
    if (surface == NULL || surface->w != (int)width || surface->h != (int)height ||
        surface->pitch < (int)(width * sizeof(uint32_t)))
        return 0;
    const uint32_t *pixels = surface->pixels;
    const size_t stride = (size_t)surface->pitch / sizeof *pixels;
    uint64_t expected_channels[4] = {0}, absolute_errors[4] = {0};
    size_t interiors = 0u;
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
        {
            const uint32_t expected = reference[y * width + x], actual = pixels[y * stride + x];
            const unsigned alpha = actual >> 24;
            if (((actual >> 16) & 255u) > alpha || ((actual >> 8) & 255u) > alpha ||
                (actual & 255u) > alpha)
                return 0;
            int flat =
                (expected >> 24) > 0u && x > 0u && y > 0u && x + 1u < width && y + 1u < height;
            for (unsigned channel = 0u; channel < 4u; ++channel)
            {
                const unsigned shift = channel * 8u;
                const int value = (int)((expected >> shift) & 255u);
                expected_channels[channel] += (unsigned)value;
                absolute_errors[channel] += (unsigned)abs((int)((actual >> shift) & 255u) - value);
                if (flat)
                {
                    const size_t offsets[4] = {y * width + x - 1u, y * width + x + 1u,
                                               (y - 1u) * width + x, (y + 1u) * width + x};
                    for (unsigned neighbor = 0u; neighbor < 4u; ++neighbor)
                        if (abs(value - (int)((reference[offsets[neighbor]] >> shift) & 255u)) > 1)
                            flat = 0;
                }
            }
            if (flat)
            {
                ++interiors;
                for (unsigned shift = 0u; shift <= 24u; shift += 8u)
                    if (abs((int)((actual >> shift) & 255u) - (int)((expected >> shift) & 255u)) >
                        4)
                    {
                        fprintf(stderr, "Geometry glyph interior x=%zu y=%zu cpu=%08x gpu=%08x\n",
                                x, y, expected, actual);
                        return 0;
                    }
            }
        }
    if (interiors <= 20u)
        return 0;
    for (unsigned channel = 0u; channel < 4u; ++channel)
        if (absolute_errors[channel] > expected_channels[channel] * 3u / 100u + 1280u)
        {
            fprintf(stderr, "Geometry glyph absolute channel=%u error=%llu expected=%llu\n",
                    channel, (unsigned long long)absolute_errors[channel],
                    (unsigned long long)expected_channels[channel]);
            return 0;
        }
    return 1;
}

#ifdef PORTABLE_TEST_GLES
// clang-format off
#define TEST_FONT_GL(X)                                                                         \
    X(void, GetIntegerv, (GLenum, GLint *))                                                     \
    X(void, GetBooleanv, (GLenum, GLboolean *))                                                 \
    X(GLboolean, IsEnabled, (GLenum))                                                           \
    X(void, Enable, (GLenum))                                                                   \
    X(void, Disable, (GLenum))                                                                  \
    X(void, ActiveTexture, (GLenum))                                                            \
    X(void, BindTexture, (GLenum, GLuint))                                                      \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                         \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                          \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                            \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                \
    X(void, BlendEquationSeparate, (GLenum, GLenum))                                            \
    X(void, UseProgram, (GLuint))                                                               \
    X(void, GenBuffers, (GLsizei, GLuint *))                                                    \
    X(void, DeleteBuffers, (GLsizei, const GLuint *))                                           \
    X(void, BindBuffer, (GLenum, GLuint))                                                       \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum))                             \
    X(void, GetVertexAttribiv, (GLuint, GLenum, GLint *))                                       \
    X(void, GetVertexAttribPointerv, (GLuint, GLenum, void **))                                 \
    X(void, EnableVertexAttribArray, (GLuint))                                                  \
    X(void, DisableVertexAttribArray, (GLuint))                                                 \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))     \
    X(const GLubyte *, GetString, (GLenum))                                                     \
    X(GLenum, GetError, (void))
// clang-format on

typedef struct
{
#define DECLARE_TEST_GL(result, name, args) result(TEST_GL_APIENTRY *name) args;
    TEST_FONT_GL(DECLARE_TEST_GL)
#undef DECLARE_TEST_GL
    void(TEST_GL_APIENTRY *GenSamplers)(GLsizei, GLuint *);
    void(TEST_GL_APIENTRY *DeleteSamplers)(GLsizei, const GLuint *);
    void(TEST_GL_APIENTRY *BindSampler)(GLuint, GLuint);
    void(TEST_GL_APIENTRY *SamplerParameteri)(GLuint, GLenum, GLint);
} TestFontGl;

typedef struct
{
    GLint values[18], attributes[2][6], sampler;
    void *pointers[2];
    GLboolean mask[4], enabled[5];
} TestFontState;
static const GLenum test_font_capabilities[5] = {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST,
                                                 GL_CULL_FACE, GL_STENCIL_TEST};

static void test_font_state(const TestFontGl *gl, TestFontState *state, int samplers, int restore)
{
    const GLenum fields[6] = {GL_BLEND_SRC_RGB,   GL_BLEND_DST_RGB,      GL_BLEND_SRC_ALPHA,
                              GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA};
    const GLenum attributes[6] = {
        GL_VERTEX_ATTRIB_ARRAY_ENABLED, GL_VERTEX_ATTRIB_ARRAY_SIZE,
        GL_VERTEX_ATTRIB_ARRAY_TYPE,    GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
        GL_VERTEX_ATTRIB_ARRAY_STRIDE,  GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING};
    if (!restore)
    {
        gl->GetIntegerv(GL_CURRENT_PROGRAM, state->values);
        gl->GetIntegerv(GL_ARRAY_BUFFER_BINDING, state->values + 1);
        gl->GetIntegerv(GL_ACTIVE_TEXTURE, state->values + 2);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->GetIntegerv(GL_TEXTURE_BINDING_2D, state->values + 3);
        if (samplers)
            gl->GetIntegerv(0x8919u, &state->sampler);
        gl->ActiveTexture((GLenum)state->values[2]);
        gl->GetIntegerv(GL_VIEWPORT, state->values + 4);
        gl->GetIntegerv(GL_SCISSOR_BOX, state->values + 8);
        for (unsigned i = 0; i < 6u; ++i)
            gl->GetIntegerv(fields[i], state->values + 12u + i);
        gl->GetBooleanv(GL_COLOR_WRITEMASK, state->mask);
        for (unsigned i = 0; i < 5u; ++i)
            state->enabled[i] = gl->IsEnabled(test_font_capabilities[i]);
        for (GLuint i = 0; i < 2u; ++i)
        {
            for (unsigned j = 0; j < 6u; ++j)
                gl->GetVertexAttribiv(i, attributes[j], &state->attributes[i][j]);
            gl->GetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state->pointers[i]);
        }
        return;
    }
    for (GLuint i = 0; i < 2u; ++i)
    {
        const GLint *a = state->attributes[i];
        gl->BindBuffer(GL_ARRAY_BUFFER, (GLuint)a[5]);
        gl->VertexAttribPointer(i, a[1], (GLenum)a[2], (GLboolean)a[3], a[4], state->pointers[i]);
        if (a[0])
            gl->EnableVertexAttribArray(i);
        else
            gl->DisableVertexAttribArray(i);
    }
    gl->UseProgram((GLuint)state->values[0]);
    gl->BindBuffer(GL_ARRAY_BUFFER, (GLuint)state->values[1]);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, (GLuint)state->values[3]);
    if (samplers)
        gl->BindSampler(0u, (GLuint)state->sampler);
    gl->ActiveTexture((GLenum)state->values[2]);
    gl->Viewport(state->values[4], state->values[5], state->values[6], state->values[7]);
    gl->Scissor(state->values[8], state->values[9], state->values[10], state->values[11]);
    gl->BlendFuncSeparate((GLenum)state->values[12], (GLenum)state->values[13],
                          (GLenum)state->values[14], (GLenum)state->values[15]);
    gl->BlendEquationSeparate((GLenum)state->values[16], (GLenum)state->values[17]);
    gl->ColorMask(state->mask[0], state->mask[1], state->mask[2], state->mask[3]);
    for (unsigned i = 0; i < 5u; ++i)
        if (state->enabled[i])
            gl->Enable(test_font_capabilities[i]);
        else
            gl->Disable(test_font_capabilities[i]);
}

static int ui_gles_font_interop(SDL_Renderer *renderer)
{
    int result = 0, saved = 0, samplers = 0;
    TestFontGl gl = {0};
    TestFontState initial = {0}, hostile = {0}, restored = {0};
    GLuint buffer = 0u, sampler = 0u;
    UiGlesFont *font = NULL;
    SDL_Surface *surface = NULL;
    SDL_Texture *previous = SDL_GetRenderTarget(renderer), *target = NULL, *sdf = NULL,
                *green = NULL;
    char error[256] = {0};
#define LOAD_TEST_GL(result_type, name, args)                                                      \
    do                                                                                             \
    {                                                                                              \
        SDL_FunctionPointer address = SDL_GL_GetProcAddress("gl" #name);                           \
        CHECK(address != NULL);                                                                    \
        _Static_assert(sizeof address == sizeof gl.name, "GL pointer width");                      \
        memcpy(&gl.name, &address, sizeof address);                                                \
    } while (0);
    TEST_FONT_GL(LOAD_TEST_GL)
#undef LOAD_TEST_GL
    const char *version = (const char *)gl.GetString(GL_VERSION);
    CHECK(version != NULL);
    samplers = strncmp(version, "OpenGL ES 3", 11u) == 0;
    if (samplers)
    {
#define LOAD_SAMPLER(name)                                                                         \
    do                                                                                             \
    {                                                                                              \
        SDL_FunctionPointer address = SDL_GL_GetProcAddress("gl" #name);                           \
        CHECK(address != NULL);                                                                    \
        memcpy(&gl.name, &address, sizeof address);                                                \
    } while (0)
        LOAD_SAMPLER(GenSamplers);
        LOAD_SAMPLER(DeleteSamplers);
        LOAD_SAMPLER(BindSampler);
        LOAD_SAMPLER(SamplerParameteri);
#undef LOAD_SAMPLER
    }
    target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 128, 96);
    sdf = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 6, 6);
    green = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    uint32_t field[36] = {0};
    for (unsigned y = 1; y < 5u; ++y)
        for (unsigned x = 1; x < 5u; ++x)
            field[y * 6u + x] = UINT32_MAX;
    const uint32_t green_pixel = UINT32_C(0xff20c040);
    CHECK(target && sdf && green && SDL_SetRenderTarget(renderer, target));
    CHECK(SDL_UpdateTexture(sdf, NULL, field, 6 * 4) &&
          SDL_UpdateTexture(green, NULL, &green_pixel, 4));
    const GLuint green_name = (GLuint)SDL_GetNumberProperty(
        SDL_GetTextureProperties(green), SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
    CHECK(green_name != 0u && SDL_FlushRenderer(renderer));
    test_font_state(&gl, &initial, samplers, 0);
    saved = 1;
    const SDL_Vertex vertices[6] = {
        {{20, 20}, {1, 1, 1, 1}, {0, 0}}, {{68, 20}, {1, 1, 1, 1}, {4, 0}},
        {{68, 68}, {1, 1, 1, 1}, {4, 4}}, {{20, 20}, {1, 1, 1, 1}, {0, 0}},
        {{68, 68}, {1, 1, 1, 1}, {4, 4}}, {{20, 68}, {1, 1, 1, 1}, {0, 4}}};
    font = ui_gles_font_create(renderer);
    CHECK(font != NULL);
    gl.GenBuffers(1, &buffer);
    CHECK(buffer != 0u);
    if (samplers)
    {
        gl.GenSamplers(1, &sampler);
        CHECK(sampler != 0u);
        gl.SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    for (unsigned generation = 0; generation < 2u; ++generation)
    {
        CHECK(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) && SDL_RenderClear(renderer) &&
              SDL_FlushRenderer(renderer));
        CHECK(ui_gles_font_prepare(font, vertices, 6u, error, sizeof error));
        gl.UseProgram(0);
        gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof vertices, vertices, GL_STATIC_DRAW);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(SDL_Vertex),
                               (const void *)0);
        gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(SDL_Vertex),
                               (const void *)offsetof(SDL_Vertex, tex_coord));
        gl.EnableVertexAttribArray(0);
        gl.DisableVertexAttribArray(1);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, green_name);
        if (samplers)
            gl.BindSampler(0u, sampler);
        gl.ActiveTexture(GL_TEXTURE3);
        gl.Viewport(7, 11, 83, 61);
        gl.Scissor(5, 9, 71, 53);
        gl.ColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        gl.BlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        for (unsigned i = 0; i < 5u; ++i)
            gl.Enable(test_font_capabilities[i]);
        CHECK(gl.GetError() == GL_NO_ERROR);
        test_font_state(&gl, &hostile, samplers, 0);
        CHECK(ui_gles_font_prepare(font, vertices, 6u, error, sizeof error));
        test_font_state(&gl, &restored, samplers, 0);
        CHECK(memcmp(hostile.values, restored.values, sizeof hostile.values) == 0);
        CHECK(memcmp(hostile.attributes, restored.attributes, sizeof hostile.attributes) == 0);
        CHECK(memcmp(hostile.pointers, restored.pointers, sizeof hostile.pointers) == 0);
        CHECK(memcmp(hostile.mask, restored.mask, sizeof hostile.mask) == 0);
        CHECK(memcmp(hostile.enabled, restored.enabled, sizeof hostile.enabled) == 0);
        CHECK(hostile.sampler == restored.sampler);
        const SDL_FRect full = {0, 0, 128, 96};
        const SDL_Rect clip = {0, 0, 128, 96};
        CHECK(ui_gles_font_begin(font, &full, 128, 96, error, sizeof error));
        CHECK(ui_gles_font_draw(font, sdf, 0, 6, &clip, 4, 4, 12, 0, 1, UINT32_C(0xff80a040), 1));
        CHECK(ui_gles_font_end(font));
        test_font_state(&gl, &restored, samplers, 0);
        CHECK(memcmp(hostile.values, restored.values, sizeof hostile.values) == 0);
        CHECK(memcmp(hostile.attributes, restored.attributes, sizeof hostile.attributes) == 0);
        CHECK(memcmp(hostile.pointers, restored.pointers, sizeof hostile.pointers) == 0);
        CHECK(memcmp(hostile.mask, restored.mask, sizeof hostile.mask) == 0);
        CHECK(memcmp(hostile.enabled, restored.enabled, sizeof hostile.enabled) == 0);
        CHECK(hostile.sampler == restored.sampler && SDL_GL_GetCurrentContext() != NULL);
        test_font_state(&gl, &initial, samplers, 1);
        CHECK(SDL_FlushRenderer(renderer));
        CHECK(SDL_SetTextureBlendMode(green, SDL_BLENDMODE_NONE));
        const SDL_FRect sentinel = {90, 70, 12, 12};
        CHECK(SDL_RenderTexture(renderer, green, NULL, &sentinel));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = converted;
        CHECK(surface != NULL);
        const uint32_t *pixels = surface->pixels;
        const size_t stride = (size_t)surface->pitch / sizeof *pixels;
        CHECK(pixels[44u * stride + 44u] == UINT32_C(0xff80a040));
        CHECK(pixels[75u * stride + 95u] == green_pixel);
        SDL_DestroySurface(surface);
        surface = NULL;
        ui_gles_font_forget(font);
    }
    puts("GLES font interop: PASS hostile-state sampler SDL-overlay forget-reprepare");
done:
    if (font)
    {
        (void)ui_gles_font_end(font);
        ui_gles_font_close(font);
    }
    if (saved)
        test_font_state(&gl, &initial, samplers, 1);
    if (sampler)
        gl.DeleteSamplers(1, &sampler);
    if (buffer)
        gl.DeleteBuffers(1, &buffer);
    (void)SDL_FlushRenderer(renderer);
    SDL_DestroySurface(surface);
    (void)SDL_SetRenderTarget(renderer, previous);
    SDL_DestroyTexture(green);
    SDL_DestroyTexture(sdf);
    SDL_DestroyTexture(target);
    return result;
}
#undef TEST_FONT_GL
#endif

static int ui_geometry_fallback(SDL_Renderer *renderer)
{
    int result = 0;
    char error[256] = {0};
    UiGeometry *geometry = NULL;
    UiDrawList *list = NULL;
    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 128, 96);
    SDL_Surface *surface = NULL;
    uint32_t *large_reference = NULL;
    CHECK(target != NULL && SDL_SetRenderTarget(renderer, target));
    geometry = ui_geometry_create(renderer, error, sizeof error);
    CHECK(geometry != NULL);
    list = ui_draw_list_create(4096u, 16u * 1024u * 1024u);
    CHECK(list != NULL);
    uint32_t reference[128 * 96];
    const SDL_FRect destination = {0, 0, 128, 96};
    for (unsigned scene = 0; scene < 5u + UI_FONT_COUNT * 3u; ++scene)
    {
        const unsigned glyph_group = scene < 5u ? 0u : (scene - 5u) / UI_FONT_COUNT;
        const unsigned oracle_scene = scene < 5u ? scene : 5u + (scene - 5u) % UI_FONT_COUNT;
        const float font_scale = (float)(1u << glyph_group);
        UiCanvas cpu, recorded;
        ui_canvas_init(&cpu, reference, 128, 96, 128);
        ui_oracle_scaled(&cpu, oracle_scene, font_scale);
        ui_draw_list_reset(list);
        ui_canvas_init(&recorded, NULL, 128, 96, 128);
        ui_canvas_bind_sink(&recorded, ui_draw_list_sink(list));
        ui_oracle_scaled(&recorded, oracle_scene, font_scale);
        CHECK(ui_geometry_prepare(geometry, list, 128, 96, error, sizeof error));
        CHECK(ui_geometry_draw(geometry, &destination, error, sizeof error));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = converted;
        CHECK(surface != NULL);
        const uint32_t *pixels = surface->pixels;
        const size_t stride = (size_t)surface->pitch / sizeof *pixels;
        uint64_t expected_coverage = 0, actual_coverage = 0;
        uint64_t expected_channels[4] = {0}, absolute_errors[4] = {0};
        size_t interiors = 0;
        for (size_t y = 0; y < 96; ++y)
            for (size_t x = 0; x < 128; ++x)
            {
                const uint32_t actual = pixels[y * stride + x], expected = reference[y * 128 + x];
                const unsigned alpha = actual >> 24;
                CHECK(((actual >> 16) & 255u) <= alpha && ((actual >> 8) & 255u) <= alpha &&
                    (actual & 255u) <= alpha);
                expected_coverage += expected >> 24;
                actual_coverage += alpha;
                for (unsigned channel = 0; channel < 4u; ++channel)
                {
                    const unsigned shift = channel * 8u;
                    const int expected_byte = (int)((expected >> shift) & 255u);
                    expected_channels[channel] += (unsigned)expected_byte;
                    absolute_errors[channel] += (unsigned)abs(
                        (int)((actual >> shift) & 255u) - expected_byte);
                }
                if (x < 4 || x >= 122 || y < 3 || y >= 91) CHECK(actual == 0u);
                if (x > 4 && x < 121 && y > 3 && y < 90)
                {
                    const int a = (int)(expected >> 24);
                    const size_t offsets[4] = {y * 128 + x - 1, y * 128 + x + 1,
                        (y - 1) * 128 + x, (y + 1) * 128 + x};
                    int flat = a > 0;
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        flat &= abs(a - (int)(reference[offsets[i]] >> 24)) <= 1;
                        /* An opaque outline can hide a colored fill edge.
                         * Glyph flat interiors require flat color as well as
                         * alpha; all edge pixels remain in the error budget. */
                        if (scene >= 5u)
                            for (unsigned shift = 0; shift < 24u; shift += 8u)
                                flat &= abs((int)((expected >> shift) & 255u) -
                                    (int)((reference[offsets[i]] >> shift) & 255u)) <= 1;
                    }
                    if (flat)
                    {
                        ++interiors;
                        for (unsigned shift = 0; shift <= 24; shift += 8)
                        {
                            const int difference = abs((int)((actual >> shift) & 255u) -
                                (int)((expected >> shift) & 255u));
                            if (difference > 4)
                                fprintf(stderr, "Geometry interior scene=%u x=%zu y=%zu cpu=%08x gpu=%08x\n",
                                    scene, x, y, expected, actual);
                            CHECK(difference <= 4);
                        }
                    }
                }
            }
        CHECK(interiors > 20u);
        const uint64_t coverage_error = expected_coverage > actual_coverage
            ? expected_coverage - actual_coverage : actual_coverage - expected_coverage;
        if (coverage_error > expected_coverage * 3u / 100u + 1280u)
            fprintf(stderr, "Geometry coverage scene=%u expected=%llu actual=%llu\n", scene,
                (unsigned long long)expected_coverage, (unsigned long long)actual_coverage);
        /* GLES2 contour/phase approximations may change antialias edges, but
         * cannot change flat interiors, clip bounds or integrated ink mass. */
        CHECK(coverage_error <= expected_coverage * 3u / 100u + 1280u);
        if (scene >= 5u)
            for (unsigned channel = 0; channel < 4u; ++channel)
            {
                if (absolute_errors[channel] > expected_channels[channel] * 3u / 100u + 1280u)
                    fprintf(stderr, "Geometry glyph error scene=%u channel=%u absolute=%llu expected=%llu\n",
                        scene, channel, (unsigned long long)absolute_errors[channel],
                        (unsigned long long)expected_channels[channel]);
                /* Absolute error cannot cancel between opposite text edges,
                 * unlike the original integrated-alpha-only comparison. */
                CHECK(absolute_errors[channel] <= expected_channels[channel] * 3u / 100u + 1280u);
            }
        SDL_DestroySurface(surface);
        surface = NULL;
        CHECK(ui_geometry_prepare(geometry, list, 128, 96, error, sizeof error));
        CHECK(ui_geometry_uploaded_bytes(geometry) == 0u);
        CHECK(ui_geometry_draw(geometry, &destination, error, sizeof error));
        CHECK(SDL_FlushRenderer(renderer));
    }
    /* Subpixel animation changes placement, not the physical glyph coverage
     * resource. A bobbing logo must not rasterize/upload a new phase per tick. */
    uint64_t previous_ink = 0u;
    for (unsigned frame = 0; frame < 4u; ++frame)
    {
        UiCanvas moving;
        ui_draw_list_reset(list);
        ui_canvas_init_transparent(&moving, NULL, 128, 96, 128);
        ui_canvas_bind_sink(&moving, ui_draw_list_sink(list));
        ui_canvas_clear(&moving, 0u);
        const UiTextStyle text = ui_text_style(UI_FONT_BOLD, 38, UINT32_C(0xc0e0a030));
        ui_text_draw(&moving, &text, 12.1875f + (float)frame * .1875f,
                     62.3125f + (float)frame * .3125f, UI_ALIGN_LEFT, "Move", .9f);
        UiCanvas moving_cpu;
        ui_canvas_init_transparent(&moving_cpu, reference, 128, 96, 128);
        ui_canvas_clear(&moving_cpu, 0u);
        ui_text_draw(&moving_cpu, &text, 12.1875f + (float)frame * .1875f,
                     62.3125f + (float)frame * .3125f, UI_ALIGN_LEFT, "Move", .9f);
        CHECK(ui_geometry_prepare(geometry, list, 128, 96, error, sizeof error));
#ifdef PORTABLE_TEST_GLES
        if (frame != 0u)
            CHECK(ui_geometry_uploaded_bytes(geometry) == 0u);
#endif
        CHECK(ui_geometry_draw(geometry, &destination, error, sizeof error));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *motion_pixels = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = motion_pixels;
        CHECK(surface != NULL);
        CHECK(glyph_coverage_matches(reference, surface, 128u, 96u));
        uint64_t ink = UINT64_C(14695981039346656037);
        for (int y = 0; y < surface->h; ++y)
            for (int x = 0; x < surface->w * 4; ++x)
                ink = (ink ^ ((const unsigned char *)surface->pixels)
                    [(size_t)y * (size_t)surface->pitch + (size_t)x]) * UINT64_C(1099511628211);
        if (frame != 0u) CHECK(ink != previous_ink);
        previous_ink = ink;
        SDL_DestroySurface(surface);
        surface = NULL;
    }
    /* The immutable-source hint is per prepare, never a persistent promise
     * that the same recorder arena address still contains the same glyph. */
    unsigned char field[16];
    for (unsigned frame = 0; frame < 3u; ++frame)
    {
        memset(field, frame == 1u ? 0 : 255, sizeof field);
        UiCanvas glyph;
        ui_draw_list_reset(list);
        ui_canvas_init_transparent(&glyph, NULL, 128, 96, 128);
        ui_canvas_bind_sink(&glyph, ui_draw_list_sink(list));
        ui_canvas_clear(&glyph, 0u);
        UiDrawCommand command = {.kind = UI_DRAW_GLYPH,
            .data.glyph = {.field = field, .width = 4, .height = 4,
                .x = 20, .y = 20, .scale = 12, .softness = 1,
                .alpha = 1, .color = UINT32_MAX}};
        CHECK(ui_canvas_emit(&glyph, &command));
        CHECK(ui_geometry_prepare(geometry, list, 128, 96, error, sizeof error));
        if (frame == 1u) CHECK(ui_geometry_uploaded_bytes(geometry) > 0u);
        if (frame == 2u) CHECK(ui_geometry_uploaded_bytes(geometry) == 0u);
        CHECK(ui_geometry_draw(geometry, &destination, error, sizeof error));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *glyph_pixels = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = glyph_pixels;
        CHECK(surface != NULL);
        const uint32_t center = ((const uint32_t *)surface->pixels)
            [44u * (size_t)surface->pitch / sizeof(uint32_t) + 44u];
        CHECK(center == (frame == 1u ? 0u : UINT32_MAX));
        SDL_DestroySurface(surface);
        surface = NULL;
    }
    /* Keep the clipped size matrix above, and also observe the whole large
     * string: accents, arrow, italic overhang, outline and blurred shadow. */
    CHECK(SDL_SetRenderTarget(renderer, previous));
    SDL_DestroyTexture(target);
    target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 1024, 384);
    CHECK(target != NULL && SDL_SetRenderTarget(renderer, target));
    large_reference = malloc(1024u * 384u * sizeof *large_reference);
    CHECK(large_reference != NULL);
    const char *large_text = "AV\xc3\xa9\xe2\x86\x92";
    const SDL_FRect large_destination = {0, 0, 1024, 384};
    for (unsigned face = 0u; face < UI_FONT_COUNT; ++face)
    {
        UiTextStyle style = ui_text_style((UiFontFace)face, 168, UINT32_C(0xc0e0a030));
        style.outline = 9.0f;
        style.outline_color = UINT32_C(0xb02090e0);
        style.shadow_color = UINT32_C(0x90000080);
        style.shadow_x = 5.5f;
        style.shadow_y = 10.5f;
        style.shadow_blur = 17.0f;
        CHECK(ui_text_width(&style, large_text) + 128.0f < 1024.0f);
        UiCanvas complete_cpu, complete_recorded;
        ui_canvas_init_transparent(&complete_cpu, large_reference, 1024, 384, 1024);
        ui_canvas_clear(&complete_cpu, 0u);
        ui_text_draw(&complete_cpu, &style, 64.25f, 260.625f, UI_ALIGN_LEFT, large_text, .9f);
        /* No reference ink touches the border, proving this is the complete
         * glyph/effect picture rather than another clipping-only fragment. */
        for (size_t y = 0u; y < 384u; ++y)
            for (size_t x = 0u; x < 1024u; ++x)
                if (x < 16u || x >= 1008u || y < 16u || y >= 368u)
                    CHECK(large_reference[y * 1024u + x] == 0u);
        ui_draw_list_reset(list);
        ui_canvas_init_transparent(&complete_recorded, NULL, 1024, 384, 1024);
        ui_canvas_bind_sink(&complete_recorded, ui_draw_list_sink(list));
        ui_canvas_clear(&complete_recorded, 0u);
        ui_text_draw(&complete_recorded, &style, 64.25f, 260.625f, UI_ALIGN_LEFT, large_text, .9f);
        CHECK(ui_geometry_prepare(geometry, list, 1024, 384, error, sizeof error));
        CHECK(ui_geometry_draw(geometry, &large_destination, error, sizeof error));
        surface = SDL_RenderReadPixels(renderer, NULL);
        CHECK(surface != NULL);
        SDL_Surface *complete_pixels = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = complete_pixels;
        CHECK(surface != NULL);
        CHECK(glyph_coverage_matches(large_reference, surface, 1024u, 384u));
        SDL_DestroySurface(surface);
        surface = NULL;
    }
    free(large_reference);
    large_reference = NULL;
    /* Real 4K footer shadow: a long, shallow convex contour has sharp offset
     * corners between the original radial samples. It must not exhaust the
     * subdivision depth, and sampled coverage still follows the CPU canvas. */
    CHECK(SDL_SetRenderTarget(renderer, previous));
    SDL_DestroyTexture(target);
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 3840, 2160);
    CHECK(target != NULL && SDL_SetRenderTarget(renderer, target));
    const UiPoint footer[4] = {{-10, 1956}, {3850, 1956}, {3850, 2160}, {-10, 2160}};
    UiPoint cropped[4];
    for (unsigned i = 0; i < 4u; ++i)
        cropped[i] = (UiPoint){footer[i].x, footer[i].y - 1940};
    const UiPaint footer_paint = ui_paint_solid(UINT32_C(0xc0e08030));
    UiCanvas cpu, recorded;
    ui_canvas_init_transparent(&cpu, reference, 128, 96, 128);
    ui_canvas_clear(&cpu, 0u);
    ui_fill_convex(&cpu, cropped, 4, &footer_paint, 36, .8f);
    ui_draw_list_reset(list);
    ui_canvas_init_transparent(&recorded, NULL, 3840, 2160, 3840);
    ui_canvas_bind_sink(&recorded, ui_draw_list_sink(list));
    ui_canvas_clear(&recorded, 0u);
    ui_fill_convex(&recorded, footer, 4, &footer_paint, 36, .8f);
    CHECK(ui_geometry_prepare(geometry, list, 3840, 2160, error, sizeof error));
    const SDL_FRect full = {0, 0, 3840, 2160};
    CHECK(ui_geometry_draw(geometry, &full, error, sizeof error));
    const SDL_Rect region = {0, 1940, 128, 96};
    surface = SDL_RenderReadPixels(renderer, &region);
    CHECK(surface != NULL && surface->w == 128 && surface->h == 96);
    SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surface);
    surface = converted;
    CHECK(surface != NULL);
    for (size_t y = 0; y < 96u; ++y)
        for (size_t x = 0; x < 128u; ++x)
        {
            const uint32_t actual = ((const uint32_t *)surface->pixels)
                [y * (size_t)surface->pitch / sizeof(uint32_t) + x];
            for (unsigned shift = 0; shift <= 24; shift += 8)
                CHECK(abs((int)((actual >> shift) & 255u) -
                    (int)((reference[y * 128u + x] >> shift) & 255u)) <= 4);
        }
#ifdef PORTABLE_TEST_GLES
    /* Invalid glyph uniforms reject the whole recorded preflight before any
     * clear/draw reaches the current target. A subsequent valid frame works. */
    for (size_t y = 0; y < 96u; ++y)
        memcpy(reference + y * 128u,
               (const uint32_t *)surface->pixels + y * (size_t)surface->pitch / sizeof(uint32_t),
               128u * sizeof(uint32_t));
    for (unsigned invalid = 0; invalid < 2u; ++invalid)
    {
        ui_draw_list_reset(list);
        ui_canvas_init_transparent(&recorded, NULL, 3840, 2160, 3840);
        ui_canvas_bind_sink(&recorded, ui_draw_list_sink(list));
        ui_canvas_clear(&recorded, 0u);
        UiDrawCommand malformed = {.kind = UI_DRAW_GLYPH,
                                   .data.glyph = {.field = field,
                                                  .width = 4,
                                                  .height = 4,
                                                  .x = 20,
                                                  .y = 20,
                                                  .scale = 12,
                                                  .softness = 1,
                                                  .outline = invalid == 0u ? NAN : 0.0f,
                                                  .alpha = invalid == 1u ? NAN : 1.0f,
                                                  .color = UINT32_MAX}};
        CHECK(ui_canvas_emit(&recorded, &malformed));
        CHECK(!ui_geometry_prepare(geometry, list, 3840, 2160, error, sizeof error));
        SDL_DestroySurface(surface);
        surface = SDL_RenderReadPixels(renderer, &region);
        CHECK(surface != NULL);
        SDL_Surface *unchanged = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(surface);
        surface = unchanged;
        CHECK(surface != NULL);
        for (size_t y = 0; y < 96u; ++y)
            CHECK(memcmp(reference + y * 128u,
                         (const uint32_t *)surface->pixels +
                             y * (size_t)surface->pitch / sizeof(uint32_t),
                         128u * sizeof(uint32_t)) == 0);
    }
    ui_draw_list_reset(list);
    ui_canvas_init_transparent(&recorded, NULL, 3840, 2160, 3840);
    ui_canvas_bind_sink(&recorded, ui_draw_list_sink(list));
    ui_canvas_clear(&recorded, 0u);
    ui_fill_convex(&recorded, footer, 4, &footer_paint, 36, .8f);
    CHECK(ui_geometry_prepare(geometry, list, 3840, 2160, error, sizeof error));
    CHECK(ui_geometry_draw(geometry, &full, error, sizeof error));
    CHECK(ui_gles_font_interop(renderer) == 0);
#endif
    puts(
        "Geometry UI: PASS isolated interiors coverage clips premultiplied images all-font passes warm caches 4K convex corners");
done:
    free(large_reference);
    if (surface != NULL)
        SDL_DestroySurface(surface);
    ui_geometry_close(geometry);
    ui_draw_list_close(list);
    ui_font_release();
    (void)SDL_SetRenderTarget(renderer, previous);
    if (target != NULL) SDL_DestroyTexture(target);
    return result;
}

/* Opt-in measurement, not a timing assertion: identical warmed presentation
 * ticks at fixed resolution, without captures or simulation. Completion is
 * reported separately from CPU submission; neither proves display pacing. */
static int ui_benchmark_wait(SDL_Renderer *renderer)
{
    if (!SDL_FlushRenderer(renderer)) return 0;
#ifdef PORTABLE_TEST_GLES
    SDL_FunctionPointer address = SDL_GL_GetProcAddress("glFinish");
    void (TEST_GL_APIENTRY *finish)(void);
    if (address == NULL) return 0;
    _Static_assert(sizeof address == sizeof finish, "GL function pointer width");
    memcpy(&finish, &address, sizeof finish);
    finish();
    return 1;
#else
    return SDL_WaitForGPUIdle(SDL_GetGPURendererDevice(renderer));
#endif
}

static int ui_benchmark(SDL_Renderer *renderer)
{
    int result = 0;
    ModernMenuHost host = {0};
    SDL_Texture *previous = SDL_GetRenderTarget(renderer), *output = NULL;
    const int sizes[2][2] = {{1280, 720}, {3840, 2160}};
    enum { WARM_FRAMES = 16, MEASURE_FRAMES = 32 };
    for (unsigned size = 0; size < 2u; ++size)
    {
        output = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_TARGET, sizes[size][0], sizes[size][1]);
        CHECK(output != NULL && SDL_SetRenderTarget(renderer, output));
        host.fixed_width = sizes[size][0];
        host.fixed_height = sizes[size][1];
        ModernMenuSetup setup = {.screen = MODERN_SETUP_IMPORT, .tick = 32u};
        /* Establish the renderer before selecting the CPU comparison path. */
        CHECK(modern_menu_host_present_setup(&host, renderer, &setup));
        CHECK(host.accelerated_frame && ui_benchmark_wait(renderer));
        for (unsigned cpu = 0; cpu < 2u; ++cpu)
        {
            host.force_cpu = (uint8_t)cpu;
            uint64_t submitted = 0u, completed = 0u;
            size_t resource_uploads = 0u;
            for (unsigned frame = 0; frame < WARM_FRAMES + MEASURE_FRAMES; ++frame)
            {
                setup.tick = 32u + frame;
                const uint64_t start = SDL_GetPerformanceCounter();
                CHECK(modern_menu_host_present_setup(&host, renderer, &setup));
                const uint64_t submit = SDL_GetPerformanceCounter();
                CHECK(host.accelerated_frame == (cpu == 0u));
                CHECK(ui_benchmark_wait(renderer));
                const uint64_t finish = SDL_GetPerformanceCounter();
                if (frame >= WARM_FRAMES)
                {
                    submitted += submit - start;
                    completed += finish - start;
                    if (!cpu && host.geometry_ui != NULL)
                        resource_uploads += ui_geometry_uploaded_bytes(host.geometry_ui);
#ifndef PORTABLE_TEST_GLES
                    else if (!cpu && host.gpu_ui != NULL)
                        resource_uploads += ui_gpu_uploaded_bytes(host.gpu_ui);
#endif
                }
            }
            const double milliseconds = 1000.0 / (double)SDL_GetPerformanceFrequency() /
                MEASURE_FRAMES;
            printf("UI benchmark: %dx%d backend=%s frames=%d submission_ms=%.3f completed_ms=%.3f resource_upload_bytes=%zu captures=0\n",
                host.width, host.height, cpu ? "cpu" : "accelerated", MEASURE_FRAMES,
                (double)submitted * milliseconds, (double)completed * milliseconds, resource_uploads);
        }
        CHECK(SDL_SetRenderTarget(renderer, previous));
        SDL_DestroyTexture(output);
        output = NULL;
        modern_menu_host_close(&host);
        memset(&host, 0, sizeof host);
    }
done:
    modern_menu_host_close(&host);
    (void)SDL_SetRenderTarget(renderer, previous);
    SDL_DestroyTexture(output);
    return result;
}

int main(int argc, char **argv)
{
    int result = 0;
    const int ui_only = argc == 2 && strcmp(argv[1], "--ui-only") == 0;
    const int benchmark = argc == 2 && strcmp(argv[1], "--ui-benchmark") == 0;
#ifndef PORTABLE_TEST_GLES
    const int hd_benchmark = argc == 2 && strcmp(argv[1], "--hd2d-benchmark-high") == 0 ? 2 :
        argc == 2 && strcmp(argv[1], "--hd2d-benchmark") == 0;
#else
    const int hd_benchmark = 0;
#endif
    if (argc != 1 && !ui_only && !benchmark && !hd_benchmark)
    {
        fputs("usage: render test [--ui-only|--ui-benchmark|--hd2d-benchmark|--hd2d-benchmark-high]\n", stderr);
        return 2;
    }
#ifdef PORTABLE_TEST_GLES
    SDL_Window *window = NULL;
#else
    SDL_GPUDevice *device = NULL;
#endif
    SDL_Renderer *renderer = NULL;
    SDL_Texture *target = NULL;
    PortableRenderGpu *gpu = NULL;
    char error[256];
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "GPU test unavailable: SDL video: %s\n", SDL_GetError());
        return 77;
    }
#ifdef PORTABLE_TEST_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, ui_only || benchmark ? 2 : 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    window = SDL_CreateWindow(ui_only ? "Geometry GLES2 test" : "Scene GLES3 test",
        65, 49, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window != NULL) renderer = SDL_CreateRenderer(window, "opengles2");
    if (renderer == NULL)
    {
        fprintf(stderr, "GLES test unavailable: ES%d renderer: %s\n", ui_only ? 2 : 3, SDL_GetError());
        result = 77;
        goto done;
    }
    /* Record the actual device: opengles2 alone can also mean software Mesa. */
    SDL_FunctionPointer get_string_address = SDL_GL_GetProcAddress("glGetString");
    const uint8_t *(TEST_GL_APIENTRY *get_string)(uint32_t);
    CHECK(get_string_address != NULL);
    _Static_assert(sizeof get_string_address == sizeof get_string, "GL function pointer width");
    memcpy(&get_string, &get_string_address, sizeof get_string);
    const uint8_t *vendor = get_string(TEST_GL_VENDOR);
    const uint8_t *device_renderer = get_string(TEST_GL_RENDERER);
    const uint8_t *version = get_string(TEST_GL_VERSION);
    CHECK(vendor != NULL && device_renderer != NULL && version != NULL);
    printf("GLES device: vendor=%s renderer=%s version=%s\n", (const char *)vendor,
           (const char *)device_renderer, (const char *)version);
#else
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                     SDL_GPU_SHADERFORMAT_MSL,
                                 !(benchmark || hd_benchmark), NULL);
    if (device == NULL)
    {
        fprintf(stderr, "GPU test unavailable: device: %s\n", SDL_GetError());
        result = 77;
        goto done;
    }
    renderer = SDL_CreateGPURenderer(device, NULL);
    CHECK(renderer != NULL);
#endif
    if (benchmark)
    {
        CHECK(ui_benchmark(renderer) == 0);
        goto done;
    }
    if (ui_only)
    {
        CHECK(ui_geometry_fallback(renderer) == 0);
        printf("Geometry UI only: PASS driver=%s\n", SDL_GetRendererName(renderer));
        goto done;
    }
    enum
    {
        WIDTH = 65,
        HEIGHT = 49
    };
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, WIDTH,
                               HEIGHT);
    CHECK(target != NULL && SDL_SetRenderTarget(renderer, target));
    gpu = portable_render_gpu_create(renderer, error, sizeof error);
    if (gpu == NULL)
        fprintf(stderr, "%s\n", error);
    CHECK(gpu != NULL);
#ifndef PORTABLE_TEST_GLES
    if (hd_benchmark)
    {
        printf("HD2D benchmark driver=%s validation=off\n", SDL_GetGPUDeviceDriver(device));
        CHECK(hd2d_oracle(gpu, hd_benchmark) == 0);
        goto done;
    }
#endif
    CHECK(palette_geometry_oracle(gpu) == 0);
    CHECK(cache_eviction_oracle(gpu) == 0);
    CHECK(clip_oracle(gpu) == 0);
    CHECK(geometry_validation_oracle(gpu) == 0);

    const PortableRenderVertex vertices[3] = {
        {.position = {-1, -1, 4}}, {.position = {1, -1, 4}}, {.position = {0, 1, 4}}};
    const uint32_t indices[3] = {0, 1, 2};
    PortableRenderMaterial material = {.argb = UINT32_C(0xffff0000), .flags = 4u};
    const PortableRenderRun run = {0, 3, 0};
    PortableRenderMesh mesh = {.vertices = vertices,
                               .indices = indices,
                               .materials = &material,
                               .runs = &run,
                               .vertex_count = 3,
                               .index_count = 3,
                               .material_count = 1,
                               .run_count = 1};
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance, &mesh);
    PortableRenderFrame frame = {.instances = &instance, .instance_count = 1, .view_count = 4};
    frame.views[0] = view(0, 0, 32, 24, UINT32_C(0xff112233));
    frame.views[1] = view(32, 0, 33, 24, UINT32_C(0xff223344));
    frame.views[2] = view(0, 24, 32, 25, UINT32_C(0xff334455));
    frame.views[3] = view(32, 24, 33, 25, UINT32_C(0xff445566));
    PortableRenderFrame before;
    memcpy(&before, &frame, sizeof before);
    uint32_t pixels[WIDTH * HEIGHT];
    SDL_Texture *picture =
        portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error);
    if (picture == NULL)
        fprintf(stderr, "%s\n", error);
    CHECK(picture != NULL);
    CHECK(portable_render_gpu_wait_idle(gpu, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    for (size_t i = 0; i < 4u; ++i)
    {
        const PortableRenderView *pane = &frame.views[i];
        CHECK(pixels[(pane->y + 1u) * WIDTH + pane->x + 1u] == pane->clear_argb);
        CHECK(pixels[(pane->y + pane->height / 2u) * WIDTH + pane->x + pane->width / 2u] ==
              UINT32_C(0xffff0000));
    }
    CHECK(memcmp(&before, &frame, sizeof frame) == 0);
    CHECK(vertices[0].position[2] == 4 && indices[2] == 2);
    /* Same immutable geometry, independently visible in alternating panes.
     * This shared test body exercises both SDL_GPU and the GLES adapter. */
    PortableRenderInstance selected[2] = {instance, instance};
    selected[0].hidden_views = 10u;
    selected[1].hidden_views = 5u;
    selected[1].tint_argb = UINT32_C(0xff00ffff); /* Red material becomes black. */
    const PortableRenderInstance selected_before[2] = {selected[0], selected[1]};
    frame.instances = selected;
    frame.instance_count = 2;
    frame.view_count = 2;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    for (size_t i = 0; i < frame.view_count; ++i)
    {
        const PortableRenderView *pane = &frame.views[i];
        CHECK(pixels[(pane->y + pane->height / 2u) * WIDTH + pane->x + pane->width / 2u] ==
              ((i & 1u) ? UINT32_C(0xff000000) : UINT32_C(0xffff0000)));
    }
    CHECK(!memcmp(selected, selected_before, sizeof selected));
    /* Adjacent per-camera directional copies retain object painter order.
     * Identical cameras must agree when a later ordinary quad is coplanar.
     * This body runs on both SDL_GPU and the GLES adapter. */
    {
        const PortableRenderVertex flat[4] = {
            {.position = {-1, 0, -1}}, {.position = {1, 0, -1}},
            {.position = {1, 0, 1}}, {.position = {-1, 0, 1}}};
        const uint32_t flat_indices[6] = {0, 1, 2, 0, 2, 3};
        const PortableRenderMaterial white = {.argb = UINT32_MAX, .flags = 4u};
        const PortableRenderRun flat_run = {0, 6, 0};
        const PortableRenderMesh flat_mesh = {.vertices = flat,
            .indices = flat_indices, .materials = &white, .runs = &flat_run,
            .vertex_count = 4, .index_count = 6, .material_count = 1, .run_count = 1};
        PortableRenderInstance directional, ordered[3];
        portable_render_instance_identity(&directional, &flat_mesh);
        directional.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
        directional.billboard_scale[0] = directional.billboard_scale[1] =
            directional.billboard_scale[2] = 1;
        directional.tint_argb = UINT32_C(0xffff0000);
        const PortableRenderInstance directional_before = directional;
        PortableRenderFrame ordered_frame = {.instances = ordered,
            .instance_count = 3, .view_count = 2};
        for (size_t pane = 0; pane < 2; ++pane)
        {
            ordered_frame.views[pane] = view(pane * 32, 0, 32, HEIGHT, UINT32_C(0xff112233));
            PortableRenderView *camera = &ordered_frame.views[pane];
            camera->position[1] = 4;
            camera->up[1] = 0;
            camera->up[2] = 1;
            camera->forward[1] = -1;
            camera->forward[2] = 0;
            CHECK(portable_render_instance_face_for_view(&directional, camera,
                32768, 0, 0, 0, 0, &ordered[pane]));
            ordered[pane].hidden_views = 15u & ~(1u << pane);
        }
        portable_render_instance_identity(&ordered[2], &flat_mesh);
        ordered[2].tint_argb = UINT32_C(0xff00ff00);
        const PortableRenderInstance ordered_before[3] = {ordered[0], ordered[1], ordered[2]};
        CHECK(portable_render_gpu_draw(gpu, &ordered_frame, WIDTH, HEIGHT, error, sizeof error));
        CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
        CHECK(pixels[24u * WIDTH + 16u] == UINT32_C(0xff00ff00));
        CHECK(pixels[24u * WIDTH + 48u] == UINT32_C(0xff00ff00));
        CHECK(!memcmp(ordered, ordered_before, sizeof ordered));
        CHECK(!memcmp(&directional, &directional_before, sizeof directional));
        /* Reverse logical object order: both cameras now see directional red,
         * proving the oracle checks depth ties rather than hidden geometry. */
        ordered[0] = ordered_before[2];
        ordered[1] = ordered_before[0];
        ordered[2] = ordered_before[1];
        CHECK(portable_render_gpu_draw(gpu, &ordered_frame, WIDTH, HEIGHT, error, sizeof error));
        CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
        CHECK(pixels[24u * WIDTH + 16u] == UINT32_C(0xffff0000));
        CHECK(pixels[24u * WIDTH + 48u] == UINT32_C(0xffff0000));
        portable_render_gpu_forget_meshes(gpu);
    }
    frame.view_count = 4;
    selected[0].hidden_views = selected[1].hidden_views = 15u;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    for (size_t i = 0; i < 4; ++i)
    {
        const PortableRenderView *pane = &frame.views[i];
        CHECK(pixels[(pane->y + pane->height / 2u) * WIDTH + pane->x + pane->width / 2u] ==
              pane->clear_argb);
    }
    selected[0].hidden_views = 16u;
    CHECK(!portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error));
    frame.instances = &instance;
    frame.instance_count = 1;
    picture = portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error);
    CHECK(picture);
    /* A normal SDL draw samples the wrapped offscreen scene on the same device. */
    CHECK(SDL_RenderTexture(renderer, picture, NULL, NULL));
    /* UI geometry must remain usable after direct scene depth/cull/FBO state. */
    const SDL_Vertex overlay[3] = {
        {.position = {1, 1}, .color = {0, 1, 0, 1}},
        {.position = {8, 1}, .color = {0, 1, 0, 1}},
        {.position = {1, 8}, .color = {0, 1, 0, 1}}};
    CHECK(SDL_RenderGeometry(renderer, NULL, overlay, 3, NULL, 0));
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
    CHECK(surface != NULL);
    SDL_Surface *argb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surface);
    CHECK(argb != NULL);
    const uint32_t composed =
        ((const uint32_t *)((const uint8_t *)argb->pixels + 12u * (size_t)argb->pitch))[16];
    const uint32_t ui_pixel =
        ((const uint32_t *)((const uint8_t *)argb->pixels + 2u * (size_t)argb->pitch))[2];
    SDL_DestroySurface(argb);
    CHECK(composed == UINT32_C(0xffff0000));
    CHECK(ui_pixel == UINT32_C(0xff00ff00));
#ifdef PORTABLE_TEST_GLES
    CHECK(SDL_FlushRenderer(renderer));
#else
    CHECK(SDL_RenderPresent(renderer));
#endif

    /* The triangle faces away in the CPU convention; culling must agree. */
    material.flags = 0u;
    portable_render_gpu_forget_meshes(gpu);
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) != NULL);
    CHECK(portable_render_gpu_wait_idle(gpu, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[12u * WIDTH + 16u] == frame.views[0].clear_argb);

    /* Nearer blue geometry is covered only by surviving red cutout texels. */
    uint8_t texels[16] = {0, 0, 255, 0, 0, 0, 255, 255,
                          0, 255, 0, 255, 0, 255, 0, 255};
    PortableRenderVertex quad[4] = {{.position = {-2, -2, 2}, .uv = {0, 0}},
                                    {.position = {2, -2, 2}, .uv = {1, 0}},
                                    {.position = {2, 2, 2}, .uv = {1, 1}},
                                    {.position = {-2, 2, 2}, .uv = {0, 1}}};
    const uint32_t quad_indices[6] = {0, 1, 2, 0, 2, 3};
    PortableRenderMaterial cutout = {.argb = UINT32_MAX,
                                     .flags = 1u | 4u | 16u,
                                     .alpha_cutoff_q16 = 32768,
                                     .texture = {.pixels = texels, .width = 2, .height = 2}};
    const PortableRenderRun quad_run = {0, 6, 0};
    PortableRenderMesh quad_mesh = {.vertices = quad,
                                    .indices = quad_indices,
                                    .materials = &cutout,
                                    .runs = &quad_run,
                                    .vertex_count = 4,
                                    .index_count = 6,
                                    .material_count = 1,
                                    .run_count = 1};
    material.argb = UINT32_C(0xff0000ff);
    material.flags = 4u;
    PortableRenderInstance pair[2];
    portable_render_instance_identity(&pair[0], &mesh);
    portable_render_instance_identity(&pair[1], &quad_mesh);
    frame.instances = pair;
    frame.instance_count = 2;
    frame.view_count = 1;
    frame.views[0] = view(0, 0, WIDTH, HEIGHT, UINT32_C(0xff112233));
    frame.views[0].projection.focal_x = frame.views[0].projection.focal_y = 24;
    portable_render_gpu_forget_meshes(gpu);
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) != NULL);
    CHECK(portable_render_gpu_wait_idle(gpu, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[27u * WIDTH + 30u] == UINT32_C(0xff0000ff));
    CHECK(pixels[27u * WIDTH + 35u] == UINT32_C(0xffff0000));
    /* Distinct rows expose transfer padding accidentally sampled as texels. */
    CHECK(pixels[12u * WIDTH + 30u] == UINT32_C(0xff00ff00));
    /* Scroll one instance through an already cached texture. Signed U wraps;
     * V clamps, cutout and depth still apply, and no resource is edited. */
    PortableRenderVertex retained_quad[4];
    uint8_t retained_texels[sizeof texels];
    memcpy(retained_quad, quad, sizeof quad);
    memcpy(retained_texels, texels, sizeof texels);
    for (unsigned direction = 0; direction < 2u; ++direction)
    {
        pair[1].uv_offset[0] = direction ? -0.5 : 0.5;
        CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) != NULL);
        CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
        CHECK(pixels[27u * WIDTH + 30u] == UINT32_C(0xffff0000));
        CHECK(pixels[27u * WIDTH + 35u] == UINT32_C(0xff0000ff));
    }
    pair[1].uv_offset[0] = 0;
    pair[1].uv_offset[1] = 0.5;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) != NULL);
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[27u * WIDTH + 30u] == UINT32_C(0xff00ff00));
    CHECK(pixels[27u * WIDTH + 35u] == UINT32_C(0xff00ff00));
    pair[1].uv_offset[1] = 0;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) != NULL);
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[27u * WIDTH + 30u] == UINT32_C(0xff0000ff));
    CHECK(pixels[27u * WIDTH + 35u] == UINT32_C(0xffff0000));
    CHECK(memcmp(retained_quad, quad, sizeof quad) == 0);
    CHECK(memcmp(retained_texels, texels, sizeof texels) == 0);
    pair[1].uv_offset[0] = NAN;
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) == NULL);
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[27u * WIDTH + 30u] == UINT32_C(0xff0000ff));
    CHECK(pixels[27u * WIDTH + 35u] == UINT32_C(0xffff0000));
    pair[1].uv_offset[0] = 0;
    /* Byte rounding precedes cutout and each tint in the source sampler.
     * Floating multiplication would wrongly fill the left hole here. */
    portable_render_gpu_forget_meshes(gpu);
    texels[3] = 200u;
    texels[6] = 201u;
    cutout.argb = UINT32_C(0xa3a3ffff);
    pair[1].tint_argb = UINT32_C(0xffbfffff);
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) != NULL);
    CHECK(portable_render_gpu_wait_idle(gpu, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[27u * WIDTH + 30u] == UINT32_C(0xff0000ff));
    CHECK(pixels[27u * WIDTH + 35u] == UINT32_C(0xff5f0000));
    /* A late invalid pane cannot replace the last good GPU picture. */
    const uint32_t retained = pixels[27u * WIDTH + 35u];
    frame.view_count = 2;
    frame.views[1] = frame.views[0];
    CHECK(portable_render_gpu_draw(gpu, &frame, WIDTH, HEIGHT, error, sizeof error) == NULL);
    CHECK(portable_render_gpu_wait_idle(gpu, error, sizeof error));
    CHECK(portable_render_gpu_read_pixels(gpu, pixels, WIDTH, error, sizeof error));
    CHECK(pixels[27u * WIDTH + 35u] == retained);
    /* Resize retains immutable resources and honors caller capture row padding. */
    enum { RESIZED_WIDTH = 73, RESIZED_HEIGHT = 55, RESIZED_STRIDE = 75 };
    frame.view_count = 1;
    frame.views[0] = view(0, 0, RESIZED_WIDTH, RESIZED_HEIGHT, UINT32_C(0xff123456));
    uint32_t resized[RESIZED_STRIDE * RESIZED_HEIGHT + 2];
    for (size_t i = 0; i < sizeof resized / sizeof resized[0]; ++i)
        resized[i] = UINT32_C(0xdeadbeef);
    picture = portable_render_gpu_draw(gpu, &frame, RESIZED_WIDTH, RESIZED_HEIGHT,
                                       error, sizeof error);
    CHECK(picture != NULL);
    CHECK(portable_render_gpu_read_pixels(gpu, resized + 1, RESIZED_STRIDE, error, sizeof error));
    CHECK(resized[1 + RESIZED_STRIDE + 1] == frame.views[0].clear_argb);
    CHECK(resized[0] == UINT32_C(0xdeadbeef));
    CHECK(resized[RESIZED_STRIDE * RESIZED_HEIGHT + 1] == UINT32_C(0xdeadbeef));
    for (size_t y = 0; y < RESIZED_HEIGHT; ++y)
        for (size_t x = RESIZED_WIDTH; x < RESIZED_STRIDE; ++x)
            CHECK(resized[1 + y * RESIZED_STRIDE + x] == UINT32_C(0xdeadbeef));
    CHECK(SDL_RenderTexture(renderer, picture, NULL, NULL));
    CHECK(SDL_FlushRenderer(renderer));
#ifndef PORTABLE_TEST_GLES
    CHECK(hd2d_oracle(gpu, 0) == 0);
    CHECK(ui_geometry(renderer) == 0);
#endif
    CHECK(ui_geometry_fallback(renderer) == 0);
    printf(
        "GPU scene: PASS driver=%s views=4 depth cutout culling wrapped-composition immutable uv-offsets rollback resize stride\n",
#ifdef PORTABLE_TEST_GLES
        SDL_GetRendererName(renderer));
#else
        SDL_GetGPUDeviceDriver(device));
#endif
done:
    portable_render_gpu_close(gpu);
    if (target != NULL)
        SDL_DestroyTexture(target);
    if (renderer != NULL)
        SDL_DestroyRenderer(renderer);
#ifdef PORTABLE_TEST_GLES
    SDL_DestroyWindow(window);
#else
    if (device != NULL)
        SDL_DestroyGPUDevice(device);
#endif
    SDL_Quit();
    return result;
}
