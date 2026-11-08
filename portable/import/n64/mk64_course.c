#include "mk64_internal.h"
#include "mkds_model.h" /* shared C3D1 table/texture writer, no DS decoding here */
#include "mkds_course.h" /* shared modern physics profile */
#include "mkds_rules.h" /* shared MIR1 odds writer */
#include "mesh_item_rules.h"
#include "fixed_3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABSENT UINT32_MAX
#define LIMIT 262144u
#define GATE_LIMIT 256u
#define GATE_SPACING (18 << 16)     /* path distance between lap gates */
#define GATE_HALF_WIDTH (24 << 16)  /* wider than any source road */
#define GATE_HALF_HEIGHT (10 << 16) /* clear of stacked sections */
#define NODE_SPACING (4 << 16)      /* CPU route node spacing */
#define RESPAWN_RADIUS (2 << 16)
#define FLOOR_PROBE (3 << 16)       /* spawn_player probes 50 units above */
#define GRID_SLOTS 8u
#define ITEM_RADIUS (3 << 15)
#define ITEM_RESPAWN_TICKS 120u
#define ITEM_HOVER ((int32_t)((int64_t)866 * 65536 * 5 / 8800)) /* 8.66 source units */

typedef struct MaterialKey {
    uint32_t offset, width, height, format, wrap, mirror, alpha, scroll, presence;
} MaterialKey;
typedef struct Course {
    MkdsModel model;
    RiData vertices, lists, data, textures;
    uint32_t vertex_count, cache[32], surface;
    uint32_t texture_offset, width, height, format, wrap, mirror, textured, shade, alpha;
    uint32_t scroll; /* 1 + the Scroll row whose tile size is in effect, else 0 */
    MaterialKey materials[256];
    uint32_t material_count;
    uint32_t operations;
    uint8_t wall[256]; /* per SURF row: walls name no respawn */
    int32_t lowest; /* gCourseMinY: lowest source height of a collision face */
    /* Race courses: the source paths (engine Q16 metres) and lap gates.
     * Gates are ordered by stage; branch gates share their stage. */
    int32_t (*paths[4])[3];
    uint32_t path_count[4], gate_count, stage_count;
    uint32_t gate_path[GATE_LIMIT], gate_point[GATE_LIMIT], gate_stage[GATE_LIMIT], stage_first[GATE_LIMIT + 1];
    /* Texture state a segment-6 caller sets before a packed list, by list
     * command index (see entry_states). */
    struct TexState *entry;
    uint8_t *has_entry;
    /* Kept faces (Layered) by layer: opaque, then cutout and translucent. */
    RiBuf *later[3];
    /* Every kept face, for the coplanar tests in triangle(). */
    RiBuf *faces;
    uint32_t owners[16]; /* per scrolls[] row: its SETTILESIZE address, else ABSENT */
    uint32_t decal; /* converting a list the course renderer draws with G_CC_DECALRGBA */
    uint32_t far; /* converting a far-view list (far_lists): no faces */
    uint32_t material_property, material_property_count; /* every MATS row's span */
    /* Lists only some races draw (render_courses.c nullify_displaylist): the
     * mksc:present-in mask of the list being converted, else ABSENT. */
    uint32_t presence, course_index;
    uint32_t present_property, present_mask; /* MATS span with mksc:present-in appended, else ABSENT */
    uint32_t surface_source[256], surface_flags[256], present_surface[256];
    uint32_t two_sided[256]; /* per one-sided SURF row: its two-sided twin, else ABSENT */
    uint32_t zone_surface[256][4]; /* per SURF row and kart zone: its variant, else ABSENT */
    /* Per source vertex: its vertex_tints row + 1, else 0. */
    uint8_t *tint;
    RiBuf *stand_in_cells; /* drop_stand_ins: collision floor faces by grid cell */
} Course;

/* Choco Mountain's guardrails (packed lists 0, 98, 178, 280, 340, 3C8) are
 * drawn and collide only at 50cc and in Time Trial. Bits are race slot * 4
 * + class (portable_course_3d_presence): Grand Prix, Quick Run and VS at
 * 50cc and every Time Trial. */
#define PRESENT_50CC_OR_TIME_TRIAL 0x11F1u
static const struct { uint32_t course, list, presence; } conditional_lists[]={
    {1,0x07000000u,PRESENT_50CC_OR_TIME_TRIAL},{1,0x07000098u,PRESENT_50CC_OR_TIME_TRIAL},
    {1,0x07000178u,PRESENT_50CC_OR_TIME_TRIAL},{1,0x07000280u,PRESENT_50CC_OR_TIME_TRIAL},
    {1,0x07000340u,PRESENT_50CC_OR_TIME_TRIAL},{1,0x070003C8u,PRESENT_50CC_OR_TIME_TRIAL}};
static uint32_t list_presence(uint32_t course,uint32_t address)
{
    for (size_t i=0;i<sizeof conditional_lists/sizeof *conditional_lists;++i)
        if (conditional_lists[i].course==course && conditional_lists[i].list==address)return conditional_lists[i].presence;
    return ABSENT;
}

/* Far-view lists: render_course_segments draws, per camera section and
 * heading, only that section's lists, and some lists are the distant view
 * of road the camera is not on. A static mesh would draw them over the road
 * they stand in for, so they are not converted. They carry no collision
 * (vertex flag 4). DK's Jungle Parkway's 0x6C38 is the start straight seen
 * from section 17 (dl_51B0), 7 to 10 units above the start pad. Kalimari
 * Desert's untextured 0x70F8 (called by dl_71C8) is drawn only while the
 * camera has no floor under it or sits within 3 units of one
 * (render_kalimari_desert, func_80290C20); otherwise it shows as olive
 * patches in the sand. */
static const struct { uint32_t course, list; } far_lists[]={{18,0x07006C38u},{11,0x070070F8u}};
static int far_list(uint32_t course,uint32_t address)
{
    for (size_t i=0;i<sizeof far_lists/sizeof *far_lists;++i)
        if (far_lists[i].course==course && far_lists[i].list==address)return 1;
    return 0;
}

/* Vertex colours the course init rewrites (render_courses.c
 * init_course_vertices, collision.c find_vtx_and_set_colours): every vertex
 * a list loads, through its nested calls, takes the alpha and, unless red is
 * 0, the colour. Source vertex alpha is otherwise 255 (memory.c unpacks
 * cn[3] = 0xFF). Rows marked translucent are lists the course's second pass
 * (render_courses.c func_8029122C) draws with G_RM_AA_ZB_XLU_INTER and
 * G_CC_MODULATEIA, so the alpha shows; Rainbow Road's stay opaque. */
typedef struct VertexTint {
    uint8_t course, alpha, r, g, b, translucent;
    uint32_t list;
} VertexTint;
static const VertexTint vertex_tints[]={
    {2,0x32,0,0,0,1,0x07001350},                                      /* Bowser's Castle */
    {3,128,0,0,0,1,0x07000878},                                       /* Banshee Boardwalk */
    {6,150,255,255,255,1,0x0700ADE0},{6,150,255,255,255,1,0x0700A540}, /* Koopa Troopa Beach */
    {6,150,255,255,255,1,0x07009E70},{6,150,255,255,255,1,0x07000358},
    {12,180,255,255,255,1,0x07001EB8},{12,150,255,255,255,1,0x07002308}, /* Sherbet Land */
    {13,150,255,255,255,0,0x07002068},{13,150,255,255,255,0,0x07001E18}, /* Rainbow Road */
    {13,255,255,255,0,0,0x07001318},{13,150,255,255,255,0,0x07001FB8},
    {14,100,255,255,255,1,0x07000C50},{14,100,255,255,255,1,0x07000BD8}, /* Wario Stadium */
    {14,100,255,255,255,1,0x07000B60},{14,100,255,255,255,1,0x07000AE8},
    {14,100,255,255,255,1,0x07000CC8},{14,100,255,255,255,1,0x07000D50},
    {14,100,255,255,255,1,0x07000DD0},{14,100,255,255,255,1,0x07000E48},
    {18,120,255,255,255,1,0x07003FA8},                                /* DK's Jungle Parkway */
};
static void tint_list(Course *c,uint32_t address,uint8_t row,unsigned depth)
{
    MkdsModel *m=&c->model;
    if (depth>8 || address>>24!=7) ri_fail(m->ctx,"%s: tinted list 0x%08X is not a segment-7 list",m->where,(unsigned)address);
    for (size_t at=address&0xFFFFFFu;;at+=8) {
        const uint8_t *p=ri_at(m->ctx,c->lists,at,8);
        const uint32_t a=mk64_be32(p),b=mk64_be32(p+4),op=a>>24;
        if (op==0xB8) return;
        if (op==6) { tint_list(c,b,row,depth+1); if (a&0x10000u) return; }
        else if (op==4) {
            const uint32_t count=((a&65535u)+1u)/0x410u,first=(b&0xFFFFFFu)/16;
            if (first+count>c->vertex_count) ri_fail(m->ctx,"%s: tinted vertex span",m->where);
            memset(c->tint+first,row,count);
        }
    }
}
static void vertex_tint_init(Course *c)
{
    c->tint=ri_buf_zero(c->model.ctx,c->vertex_count)->data;
    for (size_t i=0;i<sizeof vertex_tints/sizeof *vertex_tints;++i)
        if (vertex_tints[i].course==c->course_index) tint_list(c,vertex_tints[i].list,(uint8_t)(i+1),0);
}

/* Course renderer environment (render_courses.c): fog colour and
 * gSPFogPosition range (0..1000 of the depth buffer) where G_FOG is set,
 * the course's perspective near/far planes (skybox_and_splitscreen.c
 * set_perspective_and_aspect_ratio) and the segment-7 lists drawn under
 * G_CC_DECALRGBA, whose texels ignore vertex colour. */
typedef struct CourseEnvironment {
    uint8_t course;
    uint32_t fog_rgb;
    uint16_t fog_min, fog_max, near, far;
    uint32_t decal_lists[3];
} CourseEnvironment;
static const CourseEnvironment environments[]={
    {1,0xFFFFFFu,0x3E3,0x3E8,2,1500,{0x07000448,0x070005D8,0x07000718}}, /* Choco Mountain */
    {10,0x2B0D04u,993,1000,9,4500,{0x07000000,0x07000068,0x070000D8}},   /* Toad's Turnpike */
};
static const CourseEnvironment *course_environment(uint32_t index)
{
    for (size_t i=0;i<sizeof environments/sizeof environments[0];++i)
        if (environments[i].course==index) return &environments[i];
    return NULL;
}
/* View depth (Q16 metres) where guPerspective(near, far) puts the screen
 * depth fraction s/1000: d = 2fn / ((f+n) - z(f-n)) with z = s/500 - 1. */
static int32_t fog_depth(uint32_t s, uint32_t near, uint32_t far)
{
    const int64_t denominator=500*((int64_t)far+near)-((int64_t)s-500)*((int64_t)far-near);
    return mk64_metres((int32_t)mkds_round_div((int64_t)2*far*near*500,denominator));
}

typedef struct TexState {
    uint32_t texture_offset, width, height, format, wrap, mirror, textured, shade, alpha, scroll;
} TexState;
static void tex_save(const Course *c, TexState *t)
{
    *t=(TexState){c->texture_offset,c->width,c->height,c->format,c->wrap,c->mirror,c->textured,c->shade,c->alpha,c->scroll};
}
static void tex_load(Course *c, const TexState *t)
{
    c->texture_offset=t->texture_offset;c->width=t->width;c->height=t->height;c->format=t->format;
    c->wrap=t->wrap;c->mirror=t->mirror;c->textured=t->textured;c->shade=t->shade;c->alpha=t->alpha;
    c->scroll=t->scroll;
}
/* Entry states keep UNSET for what the caller leaves to earlier drawing. */
#define UNSET 0xFFFFFFFEu
static void tex_merge(Course *c, const TexState *t)
{
    uint32_t *to[10]={&c->texture_offset,&c->width,&c->height,&c->format,&c->wrap,&c->mirror,&c->textured,
                      &c->shade,&c->alpha,&c->scroll};
    const uint32_t from[10]={t->texture_offset,t->width,t->height,t->format,t->wrap,t->mirror,t->textured,
                             t->shade,t->alpha,t->scroll};
    for (unsigned i=0;i<10;++i) if (from[i]!=UNSET) *to[i]=from[i];
}

/* Water and conveyor textures (course_update_water): each logic tick a
 * course counter steps and wraps, and becomes the upper-left t (10.2
 * texels) of the first SETTILESIZE at or after a list address. Koopa
 * Troopa Beach's random shore jitter is not carried; Banshee Boardwalk's
 * lake (dl_B278) is lake() below. */
typedef struct Scroll {
    uint8_t course;
    int8_t step; /* positive: wraps above 255 to 0; negative: below 0 to 255 */
    uint32_t address;
} Scroll;
static const Scroll scrolls[]={
    {6,9,0x07009D58},{6,3,0x07009CD0},
    {7,-20,0x0700A6A8},{7,-20,0x0700A648},
    {18,2,0x07003DD0},{18,2,0x07003E40},{18,2,0x07003EB0},{18,2,0x07003F30},{18,2,0x070036A8},{18,-20,0x07009880},
};
#define SCROLL_COUNT (sizeof scrolls/sizeof scrolls[0])
_Static_assert(SCROLL_COUNT<=16,"Course.owners holds every scroll row");
static int32_t step(const Scroll *scroll, int32_t counter)
{
    counter+=scroll->step;
    return scroll->step>0 && counter>255 ? 0 : scroll->step<0 && counter<0 ? 255 : counter;
}
/* After a SETTILESIZE at a segment address: which scroll (if any) owns it. */
static void tile_size(Course *c, const uint32_t *owners, uint32_t address)
{
    c->scroll=0;
    for (unsigned i=0;i<SCROLL_COUNT;++i) if (owners[i]==address) c->scroll=i+1;
}

/* 17.6 source units per metre, measured: each primary path's length over the
 * course length the game states (567 m Mario Raceway .. 2000 m Rainbow Road)
 * gives 17.59-17.81, median 17.63 (test_mk64_import.c re-measures it). The
 * game multiplies course vertex heights by vtxStretchY, which it never moves
 * off 1.0 (code_800029B0.c), so heights take the same scale. */
static int32_t metres(int32_t v) { return mk64_metres(v); }
/* Tenths of a source unit in Q16 metres. */
static int32_t tenths(int32_t v) { return (int32_t)((int64_t)v*65536*5/880); }
/* Kart size and the 1P chase camera (n64decomp/mk64 58cfcb0, in tenths of a
 * unit). gKartBoundingBoxSizeTable gives most racers a 5.5-unit sphere, a
 * 0.3125 m radius and 0.625 of the stock kart, and a player's position sits
 * that far over the road. camera_init places the eye at unk_30 (0, 9.5, -50)
 * from the player and aims at unk_3C (0, 0, 70), or (0, 11.6, -38.5) and
 * (0, 0, 19.2) in battle; gCameraZoom is a 40-degree vertical view. */
static uint32_t chase_properties(MkdsModel *m,int battle)
{
    const int32_t eye[3]={0,tenths(battle?-385:-500),tenths(MK64_KART_SPHERE_TENTHS+(battle?116:95))};
    const int32_t target[3]={0,tenths(battle?192:700),tenths(MK64_KART_SPHERE_TENTHS)};
    mkds_property(m,"mksc:kart-scale",MKDS_PROPERTY_FIXED,(uint32_t)(tenths(MK64_KART_SPHERE_TENTHS)*2),0);
    /* func_80022CA8: a squished sprite's top drops 18 -> 6.5 units, its width stays. */
    mkds_property(m,"mksc:squash-height",MKDS_PROPERTY_FIXED,(uint32_t)mkds_round_div((int64_t)65*65536,180),0);
    mkds_property(m,"mksc:squash-width",MKDS_PROPERTY_FIXED,65536u,0);
    mkds_property_vector(m,"mksc:camera-eye",eye);
    mkds_property_vector(m,"mksc:camera-target",target);
    mkds_property(m,"mksc:camera-fov",MKDS_PROPERTY_INTEGER,(uint32_t)mkds_round_div((int64_t)40*65536,360),0);
    return 6u;
}
static void position(const uint8_t *v, int32_t out[3])
{
    out[0]=metres(mk64_i16(v)); out[1]=metres(mk64_i16(v+4)); out[2]=metres(mk64_i16(v+2));
}
static uint32_t material(Course *c)
{
    MkdsModel *m=&c->model;
    MaterialKey key={c->textured && !c->shade ? c->texture_offset : ABSENT,
                     c->width,c->height,c->format,c->wrap,c->mirror,c->alpha,
                     c->textured && !c->shade ? c->scroll : 0,c->presence};
    for (uint32_t i=0;i<c->material_count;++i)
        if (!memcmp(&key,&c->materials[i],sizeof key)) return i;
    if (c->material_count==256) ri_fail(m->ctx,"%s: material limit",m->where);
    uint32_t image=ABSENT;
    if (key.offset!=ABSENT) {
        if (key.offset>>24!=5 || !key.width || !key.height || key.width>64 || key.height>64 ||
            (key.format!=0 && key.format!=3)) ri_fail(m->ctx,"%s: unsupported texture layout",m->where);
        const uint8_t *source=ri_at(m->ctx,c->textures,key.offset&0xFFFFFFu,(size_t)key.width*key.height*2);
        const uint32_t width=key.width*((key.mirror&1)?2u:1u),height=key.height*((key.mirror&2)?2u:1u);
        RiBuf *pixels=ri_buf_zero(m->ctx,(size_t)width*height*4);
        for (uint32_t y=0;y<height;++y) for (uint32_t x=0;x<width;++x) {
            const uint32_t sx=x<key.width?x:width-1-x,sy=y<key.height?y:height-1-y;
            const uint32_t v=mk64_be16(source+((size_t)sy*key.width+sx)*2);
            uint32_t color;
            if (key.format==3) color=(v&255u)<<24 | (v>>8)*0x010101u;
            else {
                const uint32_t r=(v>>11)&31,g=(v>>6)&31,b=(v>>1)&31;
                color=(v&1?0xFF000000u:0) | ((r<<3)|(r>>2))<<16 | ((g<<3)|(g>>2))<<8 | (b<<3)|(b>>2);
            }
            ri_set_pixel(pixels,(size_t)y*width+x,key.alpha?color:color|0xFF000000u);
        }
        image=mkds_image(m,pixels,width,height);
        ri_buf_free(m->ctx,pixels);
    }
    uint32_t property=c->material_property,property_count=c->material_property_count;
    if (key.presence!=ABSENT) {
        /* The course's material properties again, then the race mask; one mask per course. */
        if (c->present_property!=ABSENT && c->present_mask!=key.presence)
            ri_fail(m->ctx,"%s: one race mask per course",m->where);
        if (c->present_property==ABSENT) {
            c->present_mask=key.presence;
            c->present_property=mkds_property(m,"mksc:texture-filter",MKDS_PROPERTY_INTEGER,2,0);
            if (c->material_property_count>1u)mkds_property(m,"mksc:fog",MKDS_PROPERTY_BOOLEAN,1,0);
            mkds_property(m,"mksc:present-in",MKDS_PROPERTY_INTEGER,key.presence,0);
        }
        property=c->present_property;++property_count;
    }
    const uint32_t row[12]={image,ABSENT,ABSENT,ABSENT,0xFFFFFFFFu,0,65536,65536,65536,
                            4u|8u|key.alpha|c->wrap,property,property_count};
    mkds_words(m,MKDS_T_MATS,row,12);
    if (key.scroll) {
        /* One frame per counter value, from the course's first tick. */
        const Scroll *scroll=&scrolls[key.scroll-1];
        const int32_t height=(int32_t)key.height*((key.mirror&2)?2:1);
        const uint32_t first=m->tables[MKDS_T_TXSR].count;
        int32_t counter=step(scroll,0);uint32_t frames=0;
        const int32_t start=counter;
        do {
            const int32_t matrix[6]={65536,0,0,65536,0,-(int32_t)((int64_t)counter*16384/height)};
            mkds_words(m,MKDS_T_TXSR,(const uint32_t *)(const void *)matrix,6);
            ++frames;counter=step(scroll,counter);
        } while (counter!=start && frames<512);
        const uint32_t animation[8]={c->material_count,0,frames,first,frames,0,0,0};
        mkds_words(m,MKDS_T_TXAN,animation,8);
    }
    c->materials[c->material_count]=key;
    return c->material_count++;
}

/* The respawn row of the path stretch (gate i to i+1) nearest a face's
 * centroid; a row per gate, see routes(). */
static uint32_t stretch(const Course *c, const MkdsCorner corners[3])
{
    if (!c->gate_count) return ABSENT;
    int64_t center[3];
    for (unsigned axis=0;axis<3;++axis)
        center[axis]=((int64_t)corners[0].position[axis]+corners[1].position[axis]+corners[2].position[axis])/3;
    int64_t best=INT64_MAX; uint32_t result=0;
    for (uint32_t gate=0;gate<c->gate_count;++gate)
    for (uint32_t next=c->stage_first[(c->gate_stage[gate]+1)%c->stage_count],
                  end=c->stage_first[(c->gate_stage[gate]+1)%c->stage_count+1];next<end;++next) {
        const int32_t *a=c->paths[c->gate_path[gate]][c->gate_point[gate]];
        const int32_t *b=c->paths[c->gate_path[next]][c->gate_point[next]];
        int64_t ab[3],ap[3],length=0,along=0,distance=0;
        for (unsigned axis=0;axis<3;++axis) {
            ab[axis]=((int64_t)b[axis]-a[axis])>>8; ap[axis]=(center[axis]-a[axis])>>8;
            length+=ab[axis]*ab[axis]; along+=ab[axis]*ap[axis];
        }
        for (unsigned axis=0;axis<3;++axis) {
            int64_t offset=ap[axis];
            if (length>0 && along>0) offset-=along>=length?ab[axis]:ab[axis]*along/length;
            distance+=offset*offset;
        }
        if (distance<best) { best=distance; result=gate; }
    }
    return result;
}

/* Overlapping layers. A course stores alternative triangulations of the
 * same surface in different section lists (render_course_segments draws only
 * the lists for the camera's section and heading), and lays cutout or
 * translucent overlays exactly on opaque ground. Drawn together they
 * z-fight, and where an alternative's outline differs slightly from the kept
 * triangulation's (Koopa Troopa Beach's sloping shore, a fan in one list and
 * one triangle in another) a sliver of it shows through as a seam. A face
 * whose sample points all lie on earlier kept faces of its own layer (within
 * SURFACE_GAP of their plane, normals within SURFACE_DOT) is the same surface
 * again and is dropped; overlays are lifted off opaque ground (see lift()),
 * as the N64 drew them after the ground. */
#define SURFACE_DOT 1020054733  /* 0.95, Q30 */
#define SURFACE_GAP ((8 << 16) / 100) /* 8 cm */
#define OVERLAY_LIFT ((3 << 16) / 200) /* 1.5 cm, under the 2 cm kart shadow */
#define SAMPLE_STEPS 6u /* 21 interior samples */
typedef struct Face {
    int32_t p[3][3], normal[3], low[3], high[3];
    uint32_t layer; /* 0 opaque, 1 cutout, 2 translucent */
} Face;
typedef struct Layered { MkdsCorner corners[3]; int32_t normal[3]; uint32_t material, solid; } Layered;

/* Whether point q (Q16 metres) lies on face a: near its plane and inside it
 * seen along a's dominant normal axis. */
static int on_face(const Face *a, const int64_t q[3])
{
    int64_t d=0;
    for (unsigned i=0;i<3;++i) d+=(int64_t)a->normal[i]*(q[i]-a->p[0][i]);
    if (llabs(d>>30)>SURFACE_GAP) return 0;
    unsigned drop=0;
    for (unsigned k=1;k<3;++k) if (llabs(a->normal[k])>llabs(a->normal[drop])) drop=k;
    const unsigned u=(drop+1)%3,v=(drop+2)%3;
    int64_t sign=0;
    for (unsigned k=0;k<3;++k) {
        const int32_t *p0=a->p[k],*p1=a->p[(k+1)%3];
        const int64_t e=(((int64_t)p1[u]-p0[u])>>4)*((q[v]-p0[v])>>4)-(((int64_t)p1[v]-p0[v])>>4)*((q[u]-p0[u])>>4);
        if (e && sign && (e>0)!=(sign>0)) return 0;
        if (e) sign=e;
    }
    return 1;
}
/* Hundredths of f's sample points lying on kept faces of `layer`. */
static int64_t covered(const Course *c, const Face *f, uint32_t layer)
{
    const Face *kept=(const Face *)(const void *)c->faces->data;
    const size_t count=c->faces->size/sizeof *kept;
    int64_t sample[SAMPLE_STEPS*(SAMPLE_STEPS+1)/2][3];unsigned n=0;
    for (unsigned i=0;i<SAMPLE_STEPS;++i)
        for (unsigned j=0;i+j<SAMPLE_STEPS;++j,++n) {
            const int64_t w1=3*i+1,w2=3*j+1,w0=3*SAMPLE_STEPS-w1-w2; /* thirds of a step, never on an edge */
            for (unsigned k=0;k<3;++k)
                sample[n][k]=(w0*f->p[0][k]+w1*f->p[1][k]+w2*f->p[2][k])/(3*SAMPLE_STEPS);
        }
    uint32_t hit=0;
    for (size_t i=0;i<count && hit!=(1u<<n)-1u;++i) {
        const Face *a=&kept[i];
        if (a->layer!=layer) continue;
        int apart=0;
        for (unsigned k=0;k<3;++k)
            apart|=a->low[k]>f->high[k]+SURFACE_GAP || f->low[k]>a->high[k]+SURFACE_GAP;
        if (apart || (((int64_t)a->normal[0]*f->normal[0]+(int64_t)a->normal[1]*f->normal[1]+
                       (int64_t)a->normal[2]*f->normal[2])>>30)<SURFACE_DOT) continue;
        for (unsigned k=0;k<n;++k) if (!(hit>>k&1u) && on_face(a,sample[k])) hit|=1u<<k;
    }
    unsigned total=0;
    for (unsigned k=0;k<n;++k) total+=hit>>k&1u;
    return (int64_t)total*100/n;
}
static void emit(Course *c, const MkdsCorner corners[3], const int32_t normal[3], uint32_t material_index)
{
    MkdsModel *m=&c->model;
    if (m->tables[MKDS_T_TRIS].count>=LIMIT) ri_fail(m->ctx,"%s: triangle limit",m->where);
    uint32_t row[4];
    for (unsigned k=0;k<3;++k) row[k]=mkds_render_vertex(m,&corners[k],normal);
    row[3]=material_index;
    mkds_words(m,MKDS_T_TRIS,row,4);
}

/* T-junctions. Neighbouring faces often meet with a vertex of one lying
 * inside an edge of the other (alternative section lists, strips of
 * different density, or translucent ice ending on an opaque road's edge);
 * rasterised, such edges leave pixel cracks. Each face is emitted as a fan
 * about its centroid through every course vertex, of any layer, lying on
 * its edges (within WELD_GAP: source vertices are whole units, so a corner
 * meant to sit on an edge can miss it by a fraction of a unit), so both
 * sides of a seam share vertices. */
#define WELD_GAP 0.03 /* metres */
#define WELD_CELL (4 << 16)
static uint32_t weld_hash(int32_t x,int32_t y,int32_t z,uint32_t mask)
{
    return ((uint32_t)x*73856093u ^ (uint32_t)y*19349663u ^ (uint32_t)z*83492791u)&mask;
}
static MkdsCorner corner_lerp(const MkdsCorner *a,const MkdsCorner *b,const int32_t at[3],double t)
{
    MkdsCorner r=*a;
    memcpy(r.position,at,sizeof r.position);
    for (unsigned k=0;k<2;++k) r.uv[k]=a->uv[k]+(int32_t)lround((b->uv[k]-a->uv[k])*t);
    r.color=0;
    for (unsigned k=0;k<32;k+=8) {
        const int32_t x=(int32_t)(a->color>>k&255u),y=(int32_t)(b->color>>k&255u);
        r.color|=(uint32_t)(x+(int32_t)lround((y-x)*t))<<k;
    }
    return r;
}
static Face face_of(const Layered *face,uint32_t layer)
{
    Face f;memcpy(f.normal,face->normal,sizeof f.normal);f.layer=layer;
    for (unsigned a=0;a<3;++a) {
        f.low[a]=f.high[a]=face->corners[0].position[a];
        for (unsigned v=0;v<3;++v) {
            f.p[v][a]=face->corners[v].position[a];
            if (f.p[v][a]<f.low[a]) f.low[a]=f.p[v][a];
            if (f.p[v][a]>f.high[a]) f.high[a]=f.p[v][a];
        }
    }
    return f;
}

/* Lifts an overlay corner OVERLAY_LIFT along its face's dominant axis, so
 * every face sharing the corner moves it alike, unless the corner lies on the
 * outline (a corner or an edge, so T-junctions across layers count) of opaque
 * ground the overlay meets side by side rather than lies on: lifting there
 * would open a crack, seen at grazing angles as a dotted line (Sherbet
 * Land's ice beside the road). */
typedef struct Ground { const Layered *faces; size_t count; } Ground;
static int near_segment(const int32_t a[3],const int32_t b[3],const int32_t p[3],double gap)
{
    double d[3],w[3],length=0,along=0,square=0;
    for (unsigned k=0;k<3;++k) {
        d[k]=(b[k]-a[k])/65536.0; w[k]=(p[k]-a[k])/65536.0;
        length+=d[k]*d[k]; along+=w[k]*d[k]; square+=w[k]*w[k];
    }
    double t=length>0 ? along/length : 0;
    if (t<0) t=0;
    if (t>1) t=1;
    return square-2*t*along+t*t*length<=gap*gap;
}
static void lift(MkdsCorner *corner,const Layered *face,const Ground *ground)
{
    int32_t *p=corner->position;
    const int32_t gap=(int32_t)(WELD_GAP*65536)+1;
    int64_t centre[3];
    for (unsigned a=0;a<3;++a)
        centre[a]=((int64_t)face->corners[0].position[a]+face->corners[1].position[a]+face->corners[2].position[a])/3;
    for (size_t i=0;i<ground->count;++i) {
        const MkdsCorner *g=ground->faces[i].corners;
        int outside=0;
        for (unsigned a=0;a<3;++a) {
            int32_t lo=g[0].position[a],hi=lo;
            for (unsigned k=1;k<3;++k) {
                if (g[k].position[a]<lo) lo=g[k].position[a];
                if (g[k].position[a]>hi) hi=g[k].position[a];
            }
            outside|=p[a]<lo-gap || p[a]>hi+gap;
        }
        if (outside) continue;
        int on_outline=0;
        for (unsigned k=0;k<3 && !on_outline;++k)
            on_outline=near_segment(g[k].position,g[(k+1)%3].position,p,WELD_GAP);
        if (!on_outline) continue;
        const Face under=face_of(&ground->faces[i],0);
        if (!on_face(&under,centre)) return;
    }
    const int32_t *normal=face->normal;
    unsigned axis=0;
    for (unsigned a=1;a<3;++a) if (llabs(normal[a])>llabs(normal[axis])) axis=a;
    p[axis]+=normal[axis]<0 ? -OVERLAY_LIFT : OVERLAY_LIFT;
}

/* Splits faces at T-junctions, then lifts overlay corners (ground non-null)
 * after the split so inserted corners move with the rest. */
static void emit_welded(Course *c,const Layered *faces,size_t count,const Ground *ground,
                        const int32_t (*points)[3],size_t corners)
{
    RiCtx *ctx=c->model.ctx;
    uint32_t mask=1;
    while (mask<corners*2) mask<<=1;
    uint32_t *head=(uint32_t *)(void *)ri_buf_zero(ctx,(size_t)mask*4)->data;
    uint32_t *next=(uint32_t *)(void *)ri_buf_zero(ctx,(corners+1)*4)->data;
    --mask;
    for (size_t i=0;i<corners;++i) {
        const int32_t *p=points[i];
        const uint32_t h=weld_hash(p[0]/WELD_CELL-(p[0]<0),p[1]/WELD_CELL-(p[1]<0),p[2]/WELD_CELL-(p[2]<0),mask);
        next[i+1]=head[h];head[h]=(uint32_t)i+1;
    }
    for (size_t f=0;f<count;++f) {
        const Layered *face=&faces[f];
        MkdsCorner ring[3*65];unsigned n=0;
        for (unsigned e=0;e<3;++e) {
            const MkdsCorner *a=&face->corners[e],*b=&face->corners[(e+1)%3];
            ring[n++]=*a;
            double d[3],length=0;
            for (unsigned k=0;k<3;++k) { d[k]=(b->position[k]-a->position[k])/65536.0; length+=d[k]*d[k]; }
            if (length<1e-6) continue;
            double found[64];int32_t where[64][3];unsigned hits=0;
            int32_t low[3],high[3];
            for (unsigned k=0;k<3;++k) {
                const int32_t lo=a->position[k]<b->position[k]?a->position[k]:b->position[k];
                const int32_t hi=a->position[k]<b->position[k]?b->position[k]:a->position[k];
                low[k]=(lo-WELD_CELL/64)/WELD_CELL-(lo<0);high[k]=(hi+WELD_CELL/64)/WELD_CELL-(hi<0);
            }
            if ((int64_t)(high[0]-low[0]+1)*(high[1]-low[1]+1)*(high[2]-low[2]+1)>65536) continue;
            for (int32_t x=low[0];x<=high[0];++x) for (int32_t y=low[1];y<=high[1];++y) for (int32_t z=low[2];z<=high[2];++z)
                for (uint32_t i=head[weld_hash(x,y,z,mask)];i;i=next[i]) {
                    const int32_t *p=points[i-1];
                    if (p[0]/WELD_CELL-(p[0]<0)!=x || p[1]/WELD_CELL-(p[1]<0)!=y || p[2]/WELD_CELL-(p[2]<0)!=z) continue;
                    double w[3],along=0,square=0;
                    for (unsigned k=0;k<3;++k) { w[k]=(p[k]-a->position[k])/65536.0; along+=w[k]*d[k]; square+=w[k]*w[k]; }
                    const double t=along/length;
                    if (t<=1e-4 || t>=1-1e-4 || square-along*t>WELD_GAP*WELD_GAP) continue;
                    unsigned j=0;
                    while (j<hits && found[j]<t) ++j;
                    if ((j<hits && found[j]-t<1e-5) || (j && t-found[j-1]<1e-5) || hits==64) continue;
                    memmove(found+j+1,found+j,(hits-j)*sizeof *found);
                    memmove(where+j+1,where+j,(hits-j)*sizeof *where);
                    found[j]=t;memcpy(where[j],p,sizeof where[j]);++hits;
                }
            for (unsigned j=0;j<hits;++j) ring[n++]=corner_lerp(a,b,where[j],found[j]);
        }
        if (ground) for (unsigned k=0;k<n;++k) lift(&ring[k],face,ground);
        if (n==3) { emit(c,ring,face->normal,face->material); continue; }
        MkdsCorner centre=face->corners[0];
        int64_t uv[2]={0},position[3]={0},channel[4]={0};
        for (unsigned k=0;k<3;++k) {
            for (unsigned a=0;a<3;++a) position[a]+=face->corners[k].position[a];
            for (unsigned a=0;a<2;++a) uv[a]+=face->corners[k].uv[a];
            for (unsigned a=0;a<4;++a) channel[a]+=face->corners[k].color>>(a*8)&255u;
        }
        for (unsigned a=0;a<3;++a) centre.position[a]=(int32_t)(position[a]/3);
        for (unsigned a=0;a<2;++a) centre.uv[a]=(int32_t)(uv[a]/3);
        centre.color=0;
        for (unsigned a=0;a<4;++a) centre.color|=(uint32_t)(channel[a]/3)<<(a*8);
        if (ground) lift(&centre,face,ground);
        for (unsigned k=0;k<n;++k) {
            const MkdsCorner part[3]={centre,ring[k],ring[(k+1)%n]};
            emit(c,part,face->normal,face->material);
        }
    }
}

/* Kart colour zones (render_player.c func_800235AC): the combiner draws
 * a kart TEXEL0 * (1 - ENV) + PRIM, ENV and PRIM eased toward a target that
 * depends on the racer's nearest path-0 point (lamps, lava) or on ground
 * whose three vertex flags are 3 (shadow, env 0x6F6F6F). Collision faces take
 * the zone of the path point nearest their centroid, as SURF mksc:kart-shade
 * (255 - ENV) and mksc:kart-add (PRIM), both RGBA. */
enum { ZONE_NONE, ZONE_LAMP, ZONE_LAVA, ZONE_SHADOW };
static const struct { uint32_t shade, add; } kart_zones[4]={
    {0xFFFFFFFFu,0},{0xFFFF1FFFu,0x1C0000FFu},{0xFFBFBFFFu,0x340000FFu},{0x909090FFu,0}};
typedef struct KartZone { uint8_t course, zone; uint16_t first, end; } KartZone;
static const KartZone kart_zone_ranges[]={
    /* Luigi Raceway's tunnel lights win over shadow ground. */
    {8,ZONE_LAMP,0x14F,0x158},{8,ZONE_LAMP,0x15E,0x164},{8,ZONE_LAMP,0x169,0x170},{8,ZONE_LAMP,0x174,0x17A},
    {8,ZONE_LAMP,0x17E,0x184},
    /* Bowser's Castle: lava first, then the lamps. */
    {2,ZONE_LAVA,0x15,0x2A},{2,ZONE_LAVA,0x14D,0x15C},{2,ZONE_LAVA,0x1D1,0x1E4},
    {2,ZONE_LAMP,0xF1,0xF5},{2,ZONE_LAMP,0xFB,0xFF},{2,ZONE_LAMP,0x105,0x109},{2,ZONE_LAMP,0x10F,0x113},
    {2,ZONE_LAMP,0x145,0x14A},{2,ZONE_LAMP,0x15E,0x163},
    /* Banshee Boardwalk's lamps (the source's 0x288..0x269 range is empty). */
    {3,ZONE_LAMP,0xD,0x15},{3,ZONE_LAMP,0x29,0x39},{3,ZONE_LAMP,0x46,0x4E},{3,ZONE_LAMP,0x5F,0x67},
    {3,ZONE_LAMP,0x7B,0x86},{3,ZONE_LAMP,0x9D,0xA6},{3,ZONE_LAMP,0xB9,0xC3},{3,ZONE_LAMP,0xD7,0xE1},
    {3,ZONE_LAMP,0x10E,0x119},{3,ZONE_LAMP,0x154,0x15F},{3,ZONE_LAMP,0x1EF,0x1F7},{3,ZONE_LAMP,0x202,0x209},
    {3,ZONE_LAMP,0x216,0x21D},{3,ZONE_LAMP,0x230,0x23A},{3,ZONE_LAMP,0x24C,0x256},{3,ZONE_LAMP,0x274,0x27E},
};

static unsigned zone_at(const Course *c,unsigned first_zone,unsigned last_zone,const MkdsCorner corners[3])
{
    if (!c->paths[0] || c->path_count[0]==0) return ZONE_NONE;
    int64_t centre[3];
    for (unsigned a=0;a<3;++a)
        centre[a]=((int64_t)corners[0].position[a]+corners[1].position[a]+corners[2].position[a])/3;
    uint32_t nearest=0; int64_t best=INT64_MAX;
    for (uint32_t i=0;i<c->path_count[0];++i) {
        int64_t square=0;
        for (unsigned a=0;a<3;++a) { const int64_t d=(c->paths[0][i][a]-centre[a])>>4; square+=d*d; }
        if (square<best) { best=square;nearest=i; }
    }
    for (size_t i=0;i<sizeof kart_zone_ranges/sizeof *kart_zone_ranges;++i) {
        const KartZone *z=&kart_zone_ranges[i];
        if (z->course==c->course_index && z->zone>=first_zone && z->zone<=last_zone &&
            nearest>=z->first && nearest<z->end) return z->zone;
    }
    return ZONE_NONE;
}

static unsigned kart_zone(const Course *c,const uint32_t index[3],const MkdsCorner corners[3])
{
    if (c->course_index==8 && zone_at(c,ZONE_LAMP,ZONE_LAMP,corners)) return ZONE_LAMP;
    unsigned shadow=1;
    for (unsigned k=0;k<3;++k) {
        const uint8_t *v=c->vertices.data+(size_t)index[k]*14;
        shadow&=((v[10]&3u)|(v[11]&3u)<<2)==3u;
    }
    if (shadow) return ZONE_SHADOW;
    return c->course_index==8 ? ZONE_NONE : zone_at(c,ZONE_LAMP,ZONE_LAVA,corners);
}

static uint32_t surface(Course *c,unsigned source,unsigned flags,uint32_t presence,unsigned zone);
#define UP_FLOOR 966367641 /* 0.9, Q30 */
#define UP_WALL 107374182  /* 0.1, Q30 */
static void triangle(Course *c, uint32_t bits, int collision)
{
    MkdsModel *m=&c->model;
    const unsigned cache_index[3]={(bits>>16)&255u,bits&255u,(bits>>8)&255u}; /* axis swap reverses winding */
    uint32_t index[3]; MkdsCorner corners[3]; int translucent=0;
    for (unsigned k=0;k<3;++k) {
        if (cache_index[k]%2 || cache_index[k]/2>=32 || c->cache[cache_index[k]/2]>=c->vertex_count)
            ri_fail(m->ctx,"%s: triangle uses unloaded vertex",m->where);
        index[k]=c->cache[cache_index[k]/2];
        const uint8_t *v=ri_at(m->ctx,c->vertices,(size_t)index[k]*14,14);
        position(v,corners[k].position);
        const int32_t w=(int32_t)(c->width?c->width:32)*((c->mirror&1)?2:1);
        const int32_t h=(int32_t)(c->height?c->height:32)*((c->mirror&2)?2:1);
        corners[k].uv[0]=mk64_i16(v+6)*2048/w;
        corners[k].uv[1]=mk64_i16(v+8)*2048/h;
        /* G_CC_DECALRGBA shows bare texels; translucent faces show their vertex alpha. */
        const VertexTint *t=c->tint[index[k]]?&vertex_tints[c->tint[index[k]]-1]:NULL;
        if (t && t->translucent) translucent=1;
        corners[k].color=c->decal?0xFFFFFFFFu:t && t->r?(uint32_t)t->r<<24|(uint32_t)t->g<<16|(uint32_t)t->b<<8|t->alpha:
                         (uint32_t)(v[10]&252u)<<24 | (uint32_t)(v[11]&252u)<<16 | (uint32_t)v[12]<<8 | (t?t->alpha:255u);
        corners[k].has_normal=0;
        corners[k].node=NITRO_NO_NODE;
    }
    int64_t a[3],b[3],cross[3]; int32_t normal[3];
    for (unsigned i=0;i<3;++i) {
        a[i]=(int64_t)corners[1].position[i]-corners[0].position[i];
        b[i]=(int64_t)corners[2].position[i]-corners[0].position[i];
    }
    for (unsigned i=0;i<3;++i) cross[i]=a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];
    if (!portable_fixed_3d_normalize(cross,normal)) return; /* source degenerates are not faces */
    if (!collision && c->far) return;
    if (collision) {
        /* add_collision_triangle: three flag-4 corners are scenery the kart
         * passes through; section flag 0x8000 keeps walls only (|up| <= 0.9),
         * 0x2000 floors only (|up| >= 0.1); 0x4000 faces (flag 0x200) stop
         * karts from the front only unless their corners share flag 1-3. */
        unsigned flag[3];
        for (unsigned k=0;k<3;++k) {
            const uint8_t *v=c->vertices.data+(size_t)index[k]*14;
            flag[k]=(v[10]&3u)|(v[11]&3u)<<2;
        }
        const int same=flag[0]==flag[1] && flag[1]==flag[2];
        if (same && flag[0]==4u) return;
        const unsigned flags=c->surface_flags[c->surface];
        const int64_t up=llabs((int64_t)normal[2]);
        if ((flags&0x8000u) && up>UP_FLOOR) return;
        if ((flags&0x2000u) && up<UP_WALL) return;
        if (m->tables[MKDS_T_COLT].count>=LIMIT) ri_fail(m->ctx,"%s: triangle limit",m->where);
        for (unsigned k=0;k<3;++k) {
            const int32_t y=mk64_i16(c->vertices.data+(size_t)index[k]*14+2);
            if (y<c->lowest) c->lowest=y;
        }
        uint32_t surf=c->surface;
        if ((flags&0x4000u) && same && flag[0]>=1u && flag[0]<=3u) {
            if (c->two_sided[surf]==ABSENT)
                c->two_sided[surf]=surface(c,c->surface_source[surf],flags&~0x4000u,ABSENT,0);
            surf=c->two_sided[surf];
        }
        if (c->presence!=ABSENT) {
            /* The same surface, present only in the list's races. */
            if (c->present_surface[surf]==ABSENT)
                c->present_surface[surf]=surface(c,c->surface_source[surf],c->surface_flags[surf],c->presence,0);
            surf=c->present_surface[surf];
        }
        const unsigned zone=kart_zone(c,index,corners);
        if (zone) {
            if (c->zone_surface[surf][zone]==ABSENT)
                c->zone_surface[surf][zone]=surface(c,c->surface_source[surf],c->surface_flags[surf],c->presence,zone);
            surf=c->zone_surface[surf][zone];
        }
        const uint32_t row[8]={index[0],index[1],index[2],surf,0,
                               c->wall[surf]?ABSENT:stretch(c,corners),ABSENT,ABSENT};
        mkds_words(m,MKDS_T_COLT,row,8);
        return;
    }
    /* Tinted water-pass lists blend at their vertex alpha. */
    const uint32_t alpha=c->alpha;
    if (translucent && c->textured && !c->shade) c->alpha=32u;
    else if (!(c->alpha==32u && c->textured && !c->shade))
        for (unsigned k=0;k<3;++k) corners[k].color|=255u;
    Face f;
    f.layer=c->alpha && c->textured && !c->shade ? (c->alpha==32u ? 2u : 1u) : 0u;
    memcpy(f.normal,normal,sizeof f.normal);
    for (unsigned i=0;i<3;++i) {
        f.low[i]=f.high[i]=corners[0].position[i];
        for (unsigned k=0;k<3;++k) {
            f.p[k][i]=corners[k].position[i];
            if (f.p[k][i]<f.low[i]) f.low[i]=f.p[k][i];
            if (f.p[k][i]>f.high[i]) f.high[i]=f.p[k][i];
        }
    }
    if (covered(c,&f,f.layer)>=100) { c->alpha=alpha; return; }
    ri_put(m->ctx,c->faces,&f,sizeof f);
    unsigned scenery=1; /* three flag-4 corners: drawn, never collided */
    for (unsigned k=0;k<3;++k) {
        const uint8_t *v=c->vertices.data+(size_t)index[k]*14;
        scenery&=((v[10]&3u)|(v[11]&3u)<<2)==4u;
    }
    Layered later={{corners[0],corners[1],corners[2]},{normal[0],normal[1],normal[2]},material(c),!scenery};
    ri_put(m->ctx,c->later[f.layer],&later,sizeof later);
    c->alpha=alpha;
}

static void state(Course *c, const uint8_t *p, int collision)
{
    const uint32_t a=mk64_be32(p),b=mk64_be32(p+4),op=a>>24;
    if (op==4) {
        const unsigned count=((a&65535u)+1u)/0x410u,first=(a>>17)&127u;
        if (b>>24!=4 || (b&15) || !count || first+count>32 || (b&0xFFFFFFu)/16+count>c->vertex_count)
            ri_fail(c->model.ctx,"%s: invalid F3DEX vertex span",c->model.where);
        for (unsigned i=0;i<count;++i) c->cache[first+i]=(b&0xFFFFFFu)/16+i;
    } else if (op==0xBF) triangle(c,b,collision);
    else if (op==0xB1) { triangle(c,a&0xFFFFFFu,collision); triangle(c,b,collision); }
    else if (op==0xB5) {
        triangle(c,(b>>8)&0xFFFFFFu,collision);
        triangle(c,((b>>8)&0xFF0000u)|(b&0xFF00u)|(b&255u),collision);
    }
    else if (!collision) {
        if (op==0xFD) { c->texture_offset=b;c->format=(a>>21)&7u; }
        else if (op==0xF2) { c->width=((b>>12)&4095u)/4+1; c->height=(b&4095u)/4+1; }
        else if (op==0xF5 && (b>>24)==0) {
            c->wrap=((b&(2u<<8))?0u:1u)|((b&(2u<<18))?0u:2u);
            c->mirror=((b&(1u<<8))?1u:0u)|((b&(1u<<18))?2u:0u);
        } else if (op==0xBB) c->textured=a&1u;
        else if (op==0xFC) c->shade=b==0x17;
        /* Opaque surfaces ignore texel alpha, tex-edge cuts it out and
         * translucent modes blend (memory.c unpack_render_mode_*). */
        else if (op==0xB9) c->alpha=(b==0x2F || b==0x55)?32u:b==0x19?16u:0u;
    }
}

/* Course renderers (render_courses.c) set texture, combine and render mode
 * in segment-6 lists before calling packed segment-7 lists that rely on it:
 * Mario Raceway's road is one texture loaded once for every asphalt section.
 * The state each segment-6 call site sets, including what earlier packed
 * calls in the same list set, enters the called list; what it does not set
 * stays as storage order left it (Koopa Troopa Beach's sea). Real
 * combine and render modes map to the conversion markers state() reads. */
static int raw_command(uint32_t a, uint32_t b)
{
    const uint32_t op=a>>24;
    switch (op) {
    case 0x04: return (b>>24)==4 || (b>>24)==6;
    case 0x06: return (a==0x06000000u || a==0x06010000u) && ((b>>24)==6 || (b>>24)==7);
    case 0xB6: case 0xB7: case 0xE6: case 0xE7: case 0xE8: return !(a&0xFFFFFFu);
    case 0x01: case 0x03: case 0xB1: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE:
    case 0xBF: case 0xF0: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF8: case 0xF9: case 0xFA:
    case 0xFB: case 0xFC: case 0xFD: return 1;
    default: return 0;
    }
}
static void raw_state(Course *c, uint32_t a, uint32_t b)
{
    const uint32_t op=a>>24;
    if (op==0xFC) { b=(a==0xFCFFFFFFu && b==0xFFFE793Cu)?0x17u:0x15u; a=0xFC000000u; } /* G_CC_SHADE */
    else if (op==0xB9) {
        if ((a&0xFFFFu)!=0x031Du)return; /* render mode word only */
        b=(b&0x4000u) && (b&0xC00u)==0x800u ? 0x2Fu : (b&0x1000u) ? 0x19u : 0x18u;
    } else if (op==0xFD) { if (b>>24!=5)return; }
    else if (op!=0xF2 && op!=0xF5 && op!=0xBB)return;
    const uint8_t p[8]={(uint8_t)(a>>24),(uint8_t)(a>>16),(uint8_t)(a>>8),(uint8_t)a,
                        (uint8_t)(b>>24),(uint8_t)(b>>16),(uint8_t)(b>>8),(uint8_t)b};
    state(c,p,0);
}
static void packed_state(Course *c, size_t at, unsigned depth)
{
    for (unsigned n=0;n<65536 && at+8<=c->lists.size;++n,at+=8) {
        const uint8_t *p=c->lists.data+at;
        const uint32_t a=mk64_be32(p),b=mk64_be32(p+4),op=a>>24;
        if (op==0xB8)return;
        if (op==6) {
            if (depth<8 && b>>24==7)packed_state(c,b&0xFFFFFFu,depth+1);
            if (a&0x10000u)return;
        } else if (op!=4 && op!=0xBF && op!=0xB1 && op!=0xB5)state(c,p,0);
        if (op==0xF2)tile_size(c,c->owners,0x07000000u|(uint32_t)at);
    }
}
/* A segment-6 list called for its state (Koopa Troopa Beach's dl_48 loads
 * the sea texture before the packed sea sections). */
static void raw_list(Course *c, size_t at, unsigned depth)
{
    for (unsigned n=0;n<4096 && at+8<=c->data.size;++n,at+=8) {
        const uint32_t a=mk64_be32(c->data.data+at),b=mk64_be32(c->data.data+at+4);
        if (a>>24==0xB8)return;
        if (a>>24==6) {
            if (depth<8 && b>>24==7)packed_state(c,b&0xFFFFFFu,depth+1);
            if (depth<8 && b>>24==6)raw_list(c,b&0xFFFFFFu,depth+1);
            if (a&0x10000u)return;
        } else {
            raw_state(c,a,b);
            if (a>>24==0xF2)tile_size(c,c->owners,0x06000000u|(uint32_t)at);
        }
    }
}
static void entry_states(Course *c)
{
    MkdsModel *m=&c->model;
    const size_t slots=c->lists.size/8;
    c->entry=(TexState *)(void *)ri_buf_zero(m->ctx,slots*sizeof *c->entry)->data;
    c->has_entry=ri_buf_zero(m->ctx,slots)->data;
    TexState saved;tex_save(c,&saved);
    for (size_t p=0;p+8<=c->data.size;p+=8) {
        const uint32_t a=mk64_be32(c->data.data+p),b=mk64_be32(c->data.data+p+4),target=b&0xFFFFFFu;
        if (a!=0x06000000u || b>>24!=7 || target%8 || target>=c->lists.size || c->has_entry[target/8])continue;
        size_t start=p;
        while (start>=8) {
            const uint32_t pa=mk64_be32(c->data.data+start-8),pb=mk64_be32(c->data.data+start-4);
            if (pa>>24==0xB8 || !raw_command(pa,pb))break;
            start-=8;
        }
        static const TexState unset={UNSET,UNSET,UNSET,UNSET,UNSET,UNSET,UNSET,UNSET,UNSET,UNSET};
        tex_load(c,&unset);
        for (size_t q=start;q<p;q+=8) {
            const uint32_t qa=mk64_be32(c->data.data+q),qb=mk64_be32(c->data.data+q+4);
            if (qa>>24==6 && qb>>24==7)packed_state(c,qb&0xFFFFFFu,1);
            else if (qa>>24==6 && qb>>24==6)raw_list(c,qb&0xFFFFFFu,1);
            else raw_state(c,qa,qb);
            if (qa>>24==0xF2)tile_size(c,c->owners,0x06000000u|(uint32_t)q);
        }
        tex_save(c,&c->entry[target/8]);c->has_entry[target/8]=1;
    }
    tex_load(c,&saved);
}

/* Collision lists are graphs, with shared vertex cache and nested segment-6
 * and segment-7 calls. Bound both depth and total work; cycles fail loudly. */
static void collision_list(Course *c,uint32_t address)
{
    uint32_t stack[32],presence[32]; unsigned depth=0;
    for (unsigned i=0;i<32;++i)c->cache[i]=ABSENT;
    c->presence=list_presence(c->course_index,address);
    for (;;) {
        if (++c->operations>1000000u) ri_fail(c->model.ctx,"%s: collision display-list cycle/budget",c->model.where);
        const unsigned segment=address>>24;
        if ((segment!=6 && segment!=7) || (address&7)) ri_fail(c->model.ctx,"%s: invalid display-list address",c->model.where);
        RiData data=segment==6?c->data:c->lists;
        const uint8_t *p=ri_at(c->model.ctx,data,address&0xFFFFFFu,8);
        const uint32_t a=mk64_be32(p),b=mk64_be32(p+4),op=a>>24;
        address+=8;
        if (op==0xB8) { if (!depth)break; --depth;address=stack[depth];c->presence=presence[depth]; }
        else if (op==6) {
            if (!(a&0x10000u)) {
                if (depth==32)ri_fail(c->model.ctx,"%s: display-list nesting limit",c->model.where);
                presence[depth]=c->presence;stack[depth++]=address;
            }
            if (list_presence(c->course_index,b)!=ABSENT)c->presence=list_presence(c->course_index,b);
            address=b;
        } else if (op==4 || op==0xBF || op==0xB1 || op==0xB5) state(c,p,1);
        else if (op!=0 && op!=0xBB && op!=0xBC && op!=0xB6 && op!=0xB7 && op!=0xBE && op!=3 &&
                 op!=0xB9 && op!=0xBA && op!=0xE6 && op!=0xE7 && op!=0xE8 && op!=0xF2 && op!=0xF3 &&
                 op!=0xF5 && op!=0xFC && op!=0xFD)
            ri_fail(c->model.ctx,"%s: unsupported collision command 0x%02X",c->model.where,(unsigned)op);
    }
    c->presence=ABSENT;
}

/* Banshee Boardwalk's lake: render_banshee_boardwalk draws dl_B278, an
 * octagon 250 units across, under the camera at height -82 with the
 * segment-6 texture at 0xAA78 (RGBA16 32x32, 0.32 texels a unit) scrolling
 * one quarter texel a tick, shaded 0xA6 at the camera to 0x19 at its rim.
 * Here it is a plane under the whole course in the octagon's shade 5 m out
 * (beside the chase camera's kart), since content cannot follow the camera. */
static const struct { uint32_t course, texture; int16_t height; uint8_t shade; } lakes[]={{3,0xAA78u,-82,0x70}};
#define LAKE_CELL (64*65536) /* engine metres per grid cell, Q16 */
#define LAKE_HORIZON ((int64_t)2000*65536) /* skirt reach past the grid, Q16 metres (C3D1 positions stay within 4096 m) */
static void lake(Course *c,uint32_t course)
{
    size_t which=0;
    while (which<sizeof lakes/sizeof *lakes && lakes[which].course!=course)++which;
    if (which==sizeof lakes/sizeof *lakes)return;
    MkdsModel *m=&c->model;
    if (c->material_count==256) ri_fail(m->ctx,"%s: material limit",m->where);
    const uint8_t *source=ri_at(m->ctx,c->data,lakes[which].texture,32u*32u*2u);
    RiBuf *pixels=ri_buf_zero(m->ctx,32u*32u*4u);
    for (size_t i=0;i<32u*32u;++i) {
        const uint32_t v=mk64_be16(source+i*2),r=(v>>11)&31,g=(v>>6)&31,b=(v>>1)&31;
        ri_set_pixel(pixels,i,0xFF000000u|((r<<3)|(r>>2))<<16|((g<<3)|(g>>2))<<8|((b<<3)|(b>>2)));
    }
    const uint32_t image=mkds_image(m,pixels,32,32);
    ri_buf_free(m->ctx,pixels);
    const uint32_t material_index=c->material_count;
    const uint32_t row[12]={image,ABSENT,ABSENT,ABSENT,0xFFFFFFFFu,0,65536,65536,65536,4u|8u|3u,
                            c->material_property,c->material_property_count};
    mkds_words(m,MKDS_T_MATS,row,12);
    c->materials[c->material_count++]=(MaterialKey){ABSENT-2u,0,0,0,0,0,0,0,ABSENT}; /* reserved: never matched */
    /* The upper-left t steps 1/4 texel a tick and wraps after 256 ticks. */
    const uint32_t first=m->tables[MKDS_T_TXSR].count;
    for (int32_t counter=0;counter<256;++counter) {
        const int32_t matrix[6]={65536,0,0,65536,0,-(int32_t)((int64_t)counter*16384/32)};
        mkds_words(m,MKDS_T_TXSR,(const uint32_t *)(const void *)matrix,6);
    }
    const uint32_t animation[8]={material_index,0,256,first,256,0,0,0};
    mkds_words(m,MKDS_T_TXAN,animation,8);
    /* A grid over the course's extent and one cell beyond. */
    int32_t low[2]={INT32_MAX,INT32_MAX},high[2]={INT32_MIN,INT32_MIN};
    for (uint32_t i=0;i<c->vertex_count;++i) {
        int32_t p[3];position(ri_at(m->ctx,c->vertices,(size_t)i*14,14),p);
        for (unsigned a=0;a<2;++a) { if(p[a]<low[a])low[a]=p[a]; if(p[a]>high[a])high[a]=p[a]; }
    }
    if (low[0]>high[0])return;
    const int32_t z=metres(lakes[which].height),normal[3]={0,0,PORTABLE_FIXED_3D_UNIT};
    const uint32_t shade=(uint32_t)lakes[which].shade*0x01010100u|255u;
    const int64_t x0=(int64_t)low[0]-LAKE_CELL,y0=(int64_t)low[1]-LAKE_CELL;
    const int64_t columns=((int64_t)high[0]-low[0])/LAKE_CELL+3,rows=((int64_t)high[1]-low[1])/LAKE_CELL+3;
    for (int64_t j=0;j<rows;++j)
        for (int64_t i=0;i<columns;++i) {
            MkdsCorner q[4];
            for (unsigned k=0;k<4;++k) {
                const int64_t x=x0+(i+(k==1||k==2))*(int64_t)LAKE_CELL,y=y0+(j+(k>=2))*(int64_t)LAKE_CELL;
                /* 0.32 texels a source unit: one 32-texel repeat per 100 units. */
                q[k]=(MkdsCorner){{(int32_t)x,(int32_t)y,z},{(int32_t)(x*88/500),(int32_t)(y*88/500)},{0,0,0},0,shade,NITRO_NO_NODE};
            }
            const MkdsCorner a[3]={q[0],q[1],q[2]},b[3]={q[0],q[2],q[3]};
            emit(c,a,normal,material_index);emit(c,b,normal,material_index);
        }
    /* Four skirts carry the water to the horizon, as the source's lake
     * follows the camera. */
    const int64_t x1=x0+columns*(int64_t)LAKE_CELL,y1=y0+rows*(int64_t)LAKE_CELL;
    const int64_t skirts[4][4]={{x0-LAKE_HORIZON,y0-LAKE_HORIZON,x1+LAKE_HORIZON,y0},
                                {x0-LAKE_HORIZON,y1,x1+LAKE_HORIZON,y1+LAKE_HORIZON},
                                {x0-LAKE_HORIZON,y0,x0,y1},{x1,y0,x1+LAKE_HORIZON,y1}};
    for (unsigned k=0;k<4;++k) {
        MkdsCorner q[4];
        for (unsigned v=0;v<4;++v) {
            const int64_t x=skirts[k][v==1||v==2?2:0],y=skirts[k][v>=2?3:1];
            q[v]=(MkdsCorner){{(int32_t)x,(int32_t)y,z},{(int32_t)(x*88/500),(int32_t)(y*88/500)},{0,0,0},0,shade,NITRO_NO_NODE};
        }
        const MkdsCorner a[3]={q[0],q[1],q[2]},b[3]={q[0],q[2],q[3]};
        emit(c,a,normal,material_index);emit(c,b,normal,material_index);
    }
}

/* The course-select picture (gTextureCoursePreview*): 128x78 RGBA16 MIO0
 * blocks packed in course index order. */
#define PREVIEWS 0x7A1418u
static uint32_t preview(Course *c,const Mk64CourseSource *s)
{
    RiCtx *ctx=c->model.ctx;
    size_t at=PREVIEWS;
    for (uint32_t i=0;i<s->index;++i)at=mk64_mio0_next(ctx,at);
    RiBuf *image=mk64_mio0(ctx,ri_span(ctx,ctx->rom,at,ctx->rom.size-at));
    if (image->size!=128u*78u*2u)ri_fail(ctx,"%s: course preview is not 128x78 RGBA16",c->model.where);
    RiBuf *pixels=ri_buf_zero(ctx,128u*78u*4u);
    for (size_t i=0;i<128u*78u;++i) {
        const uint32_t v=mk64_be16(image->data+i*2),r=(v>>11)&31,g=(v>>6)&31,b=(v>>1)&31;
        ri_set_pixel(pixels,i,0xFF000000u|((r<<3)|(r>>2))<<16|((g<<3)|(g>>2))<<8|((b<<3)|(b>>2)));
    }
    const uint32_t id=mkds_image(&c->model,pixels,128,78);
    ri_buf_free(ctx,pixels);ri_buf_free(ctx,image);
    return id;
}

/* The minimap: gCourseOutlineTextures, I4 MIO0 blocks in course order from
 * OUTLINES, drawn in the PRIM colour with the intensity as coverage. The
 * game places a racer at texel (gMiniMapX + x * scale, gMiniMapY + z * scale)
 * (code_8006E9C0.c, render_objects.c draw_minimap_character), so the art's
 * edges are those texels' source positions. Frappe Snowland and Sherbet
 * Land draw it blue. */
#define OUTLINES 0x720C0Cu
static const struct { uint8_t x, y, wide, blue; uint16_t scale; /* 1/10000 */ } outlines[20]={
    {6,28,0,0,220},{19,37,0,0,220},{12,48,0,0,174},{55,39,0,0,160},{61,38,0,0,180},{36,40,0,1,160},
    {40,21,0,0,140},{37,50,0,0,140},{45,60,0,0,155},{18,36,0,0,155},{57,44,1,0,130},{55,27,0,0,150},
    {52,33,0,1,150},{39,55,0,0,103},{53,35,0,0,155},{32,32,0,0,335},{32,32,0,0,445},{32,32,0,0,285},
    {29,47,0,0,155},{32,31,0,0,257}};
static uint32_t minimap(Course *c,const Mk64CourseSource *s,int32_t low[3],int32_t high[3])
{
    RiCtx *ctx=c->model.ctx;
    if (s->index>=20u)ri_fail(ctx,"%s: no course outline",c->model.where);
    size_t at=OUTLINES;
    for (uint32_t i=0;i<s->index;++i)at=mk64_mio0_next(ctx,at);
    RiBuf *image=mk64_mio0(ctx,ri_span(ctx,ctx->rom,at,ctx->rom.size-at));
    const uint32_t width=outlines[s->index].wide?128u:64u,height=(uint32_t)image->size*2u/width;
    if (height<64u || height>96u || image->size*2u!=width*height)
        ri_fail(ctx,"%s: course outline is not a 64- or 128-wide I4 image",c->model.where);
    const uint32_t prim=outlines[s->index].blue?0x4864FFu:0xFFFFFFu;
    RiBuf *pixels=ri_buf_zero(ctx,(size_t)width*height*4u);
    for (size_t i=0;i<(size_t)width*height;++i) {
        const uint32_t v=(image->data[i/2]>>(i%2?0:4))&15u;
        ri_set_pixel(pixels,i,v?(v*17u)<<24|prim:0u);
    }
    const uint32_t id=mkds_image(&c->model,pixels,width,height);
    ri_buf_free(ctx,pixels);ri_buf_free(ctx,image);
    /* Texel t lies at source (t - origin) / scale; engine x, y = source x, z. */
    const int64_t scale=outlines[s->index].scale,origin[2]={outlines[s->index].x,outlines[s->index].y},
        size[2]={width,height};
    for (unsigned a=0;a<2;++a) {
        low[a]=(int32_t)mkds_round_div(-origin[a]*10000*65536*5,scale*88);
        high[a]=(int32_t)mkds_round_div((size[a]-origin[a])*10000*65536*5,scale*88);
    }
    low[2]=high[2]=0;
    return id;
}

static uint32_t surface(Course *c,unsigned source,unsigned flags,uint32_t presence,unsigned zone)
{
    /* Modern adaptation, retaining the original source type as a property. */
    uint32_t type=0,speed=65536,friction=65536,boost=0;
    if (source==7 || source==8 || source==11 || source==13) { type=1;speed=39322; }
    else if (source==9) { type=2;friction=19661; }
    else if (source==12) type=3;
    else if (source==0xFD)type=8;
    else if (source==0xFC || source==0xFE) { type=4;boost=196608; }
    /* RAMP (0xFF, Koopa Troopa Beach's rock ramps) has no effect in the
     * source: the slope alone throws a fast kart; a launch would pin a slow one. */
    else if (source==0xFF) {}
    else if (source>17)ri_fail(c->model.ctx,"%s: unknown surface %u",c->model.where,source);
    const uint32_t property=mkds_property(&c->model,"mk64:surface",MKDS_PROPERTY_INTEGER,source,0);
    mkds_property(&c->model,"mk64:surface-flags",MKDS_PROPERTY_INTEGER,flags,0);
    uint32_t count=2;
    /* render_player_ice_reflection mirrors karts on ICE. */
    if (source==9) { mkds_property(&c->model,"mksc:reflect",MKDS_PROPERTY_BOOLEAN,1,0);++count; }
    if (presence!=ABSENT) { mkds_property(&c->model,"mksc:present-in",MKDS_PROPERTY_INTEGER,presence,0);++count; }
    if (flags&0x4000u) { mkds_property(&c->model,"mksc:one-sided",MKDS_PROPERTY_BOOLEAN,1,0);++count; }
    if (zone) {
        mkds_property(&c->model,"mksc:kart-shade",MKDS_PROPERTY_INTEGER,kart_zones[zone].shade,0);
        mkds_property(&c->model,"mksc:kart-add",MKDS_PROPERTY_INTEGER,kart_zones[zone].add,0);
        count+=2;
    }
    const uint32_t row[12]={type,0,friction,speed,0,0,boost,0,65536,0,property,count};
    const uint32_t result=c->model.tables[MKDS_T_SURF].count;
    if (result>=256)ri_fail(c->model.ctx,"%s: surface limit",c->model.where);
    c->wall[result]=type==3;
    c->surface_source[result]=source;c->surface_flags[result]=flags;c->present_surface[result]=c->two_sided[result]=ABSENT;
    for (unsigned k=0;k<4;++k) c->zone_surface[result][k]=ABSENT;
    mkds_words(&c->model,MKDS_T_SURF,row,12);return result;
}

/* Source path `path` (0-3) as a segment-6 offset and point count; 0 for
 * the shared null path (an absent alternative). */
static int source_path(Course *c,const Mk64CourseSource *s,unsigned path,size_t *offset,uint32_t *count)
{
    MkdsModel *m=&c->model;
    const uint32_t address=mk64_be32(ri_at(m->ctx,m->ctx->rom,0xDD4D0u+s->index*16+path*4,4));
    if (address>>24!=6) {
        if (address!=0x800DC778u)ri_fail(m->ctx,"%s: invalid source path pointer",m->where);
        return 0;
    }
    *offset=address&0xFFFFFFu;
    uint32_t n=0;
    while (n<3000 && mk64_be16(ri_at(m->ctx,c->data,*offset+n*8,8))!=0x8000u)++n;
    if (n<2 || n==3000)ri_fail(m->ctx,"%s: source route missing terminator or points",m->where);
    *count=n;return 1;
}

static int64_t distance(const int32_t a[3],const int32_t b[3])
{
    uint64_t square=0;
    for (unsigned axis=0;axis<3;++axis) {
        const int64_t d=(int64_t)b[axis]-a[axis];
        if (d>(INT64_C(1)<<30) || d<-(INT64_C(1)<<30))return INT64_C(1)<<30;
        square+=(uint64_t)(d*d);
    }
    return (int64_t)portable_fixed_3d_sqrt(square);
}

/* The lap: gates every GATE_SPACING of primary path distance from point 0,
 * the finish line the game starts its grid behind. Alternative paths (CPU
 * branches through Yoshi Valley's maze, Koopa Troopa Beach's tunnel) get a
 * parallel gate of the same stage wherever they run apart from the primary
 * gate. Read before collision so faces can name the stretch they belong to. */
static void track(Course *c,const Mk64CourseSource *s)
{
    MkdsModel *m=&c->model;
    if (s->battle)return;
    for (unsigned path=0;path<4;++path) {
        size_t offset;uint32_t count;
        if (!source_path(c,s,path,&offset,&count)) {
            if (!path)ri_fail(m->ctx,"%s: missing primary source route",m->where);
            continue;
        }
        c->paths[path]=(int32_t(*)[3])(void *)ri_buf_zero(m->ctx,(size_t)count*sizeof *c->paths[path])->data;
        c->path_count[path]=count;
        for (uint32_t i=0;i<count;++i)position(c->data.data+offset+i*8,c->paths[path][i]);
    }
    int32_t (*primary)[3]=c->paths[0];
    const uint32_t count=c->path_count[0];
    uint32_t points[GATE_LIMIT],stages=0;
    int64_t total=0,since=0,walked=0;
    for (uint32_t i=0;i<count;++i)total+=distance(primary[i],primary[(i+1)%count]);
    points[stages++]=0;
    for (uint32_t i=1;i<count;++i) {
        const int64_t step=distance(primary[i-1],primary[i]);
        walked+=step;since+=step;
        if (since>=GATE_SPACING && total-walked>=GATE_SPACING/2) {
            if (stages==GATE_LIMIT)ri_fail(m->ctx,"%s: lap gate limit",m->where);
            points[stages++]=i;since=0;
        }
    }
    if (stages<3)ri_fail(m->ctx,"%s: primary source route is too short for a lap",m->where);
    /* Each alternative point's progress is the nearest primary point within
     * a forward window of the previous one, so progress never runs back. */
    uint32_t *progress[4]={0};
    for (unsigned path=1;path<4;++path) {
        if (!c->paths[path])continue;
        progress[path]=(uint32_t *)(void *)ri_buf_zero(m->ctx,(size_t)c->path_count[path]*sizeof(uint32_t))->data;
        for (uint32_t j=0,previous=0;j<c->path_count[path];++j) {
            int64_t best=INT64_MAX;uint32_t chosen=previous;
            for (uint32_t k=previous;k<count && k<previous+128u;++k) {
                const int64_t d=distance(c->paths[path][j],primary[k]);
                if (d<best) { best=d; chosen=k; }
            }
            progress[path][j]=previous=chosen;
        }
    }
    for (uint32_t stage=0;stage<stages;++stage) {
        c->stage_first[stage]=c->gate_count;
        c->gate_path[c->gate_count]=0;c->gate_point[c->gate_count]=points[stage];c->gate_stage[c->gate_count++]=stage;
        for (unsigned path=1;path<4 && stage;++path) {
            if (!progress[path])continue;
            uint32_t j=0;
            while (j<c->path_count[path] && progress[path][j]<points[stage])++j;
            if (j==c->path_count[path])continue;
            int apart=1;
            for (uint32_t g=c->stage_first[stage];g<c->gate_count;++g)
                apart&=distance(c->paths[path][j],c->paths[c->gate_path[g]][c->gate_point[g]])>GATE_SPACING/2;
            if (!apart)continue;
            if (c->gate_count==GATE_LIMIT)ri_fail(m->ctx,"%s: lap gate limit",m->where);
            c->gate_path[c->gate_count]=path;c->gate_point[c->gate_count]=j;c->gate_stage[c->gate_count++]=stage;
        }
    }
    c->stage_count=stages;c->stage_first[stages]=c->gate_count;
}

/* Horizontal unit direction of source path `path` through point i. */
static void heading(const Course *c,unsigned path,uint32_t i,int32_t out[3])
{
    const uint32_t n=c->path_count[path],k=n/4<4?(n/4?n/4:1):4;
    const int32_t *a=c->paths[path][(i+n-k)%n],*b=c->paths[path][(i+k)%n];
    const int64_t delta[3]={(int64_t)b[0]-a[0],(int64_t)b[1]-a[1],0};
    if (!portable_fixed_3d_normalize(delta,out))ri_fail(c->model.ctx,"%s: duplicate route points",c->model.where);
}

/* Yaw about +Z facing `forward` (engine forward is (sin, -cos, 0)). */
static void yaw_quaternion(const int32_t forward[3],int32_t q[4])
{
    int32_t half[3];
    const int64_t halfway[3]={(int64_t)(1<<30)-forward[1],forward[0],0};
    if (!portable_fixed_3d_normalize(halfway,half)) { half[0]=0; half[1]=1<<30; }
    q[0]=q[1]=0;q[2]=half[1];q[3]=half[0];
}

/* Height of the highest face under (x, y) at most FLOOR_PROBE above z, as
 * the game's spawn_player probes the surface from above the path; wall
 * surfaces count only with `walls` (falling rocks bounce off cliffs). */
static int surface_at(const Course *c,const int32_t at[3],int32_t *out,int walls)
{
    const MkdsTable *faces=&c->model.tables[MKDS_T_COLT],*points=&c->model.tables[MKDS_T_COLV];
    const int64_t top=(int64_t)at[2]+FLOOR_PROBE,px=at[0]>>8,py=at[1]>>8;
    int found=0;int64_t best=0;
    for (uint32_t f=0;f<faces->count;++f) {
        const uint8_t *row=faces->bytes->data+(size_t)f*32;
        if (!walls && c->wall[ri_le32(row+12)])continue;
        int64_t v[3][3];
        for (unsigned k=0;k<3;++k) {
            const uint8_t *p=points->bytes->data+(size_t)ri_le32(row+k*4)*12;
            for (unsigned axis=0;axis<3;++axis)v[k][axis]=(int32_t)ri_le32(p+axis*4)>>8;
        }
        const int64_t e0=(v[1][0]-v[0][0])*(py-v[0][1])-(v[1][1]-v[0][1])*(px-v[0][0]);
        const int64_t e1=(v[2][0]-v[1][0])*(py-v[1][1])-(v[2][1]-v[1][1])*(px-v[1][0]);
        const int64_t e2=(v[0][0]-v[2][0])*(py-v[2][1])-(v[0][1]-v[2][1])*(px-v[2][0]);
        if (!((e0>=0 && e1>=0 && e2>=0) || (e0<=0 && e1<=0 && e2<=0)))continue;
        const int64_t a[3]={v[1][0]-v[0][0],v[1][1]-v[0][1],v[1][2]-v[0][2]};
        const int64_t b[3]={v[2][0]-v[0][0],v[2][1]-v[0][1],v[2][2]-v[0][2]};
        const int64_t n[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        if (!n[2])continue;
        const int64_t z=(v[0][2]-(n[0]*(px-v[0][0])+n[1]*(py-v[0][1]))/n[2])<<8;
        if (z<=top && (!found || z>best)) { best=z; found=1; }
    }
    if (found)*out=(int32_t)best;
    return found;
}
/* Far-view stand-ins: render_course_segments draws, per camera section,
 * either a list that collides or a coarser one that does not, almost never
 * both (Kalimari Desert's sand banks stand up to 1 m over the collided sand
 * they replace in distant views; Moo Moo Farm's and Royal Raceway's fields
 * likewise). Drawn together, the stand-in buries karts on the real ground,
 * so an opaque, upward face that is neither collision nor flag-4 scenery is
 * not converted when its centre lies STAND_IN_LOW to STAND_IN_HIGH over
 * collided floor and no corner dips under it. */
#define STAND_IN_LOW ((5 << 16) / 100) /* 5 cm */
#define STAND_IN_HIGH (2 << 16)
#define STAND_IN_CELL 22 /* 64 m grid cells, Q16 */
static int collided_floor_under(const Course *c,const uint32_t *cells,const uint32_t *start,int32_t origin[2],
                                uint32_t span[2],const int32_t at[3],int64_t *gap)
{
    const MkdsTable *faces=&c->model.tables[MKDS_T_COLT],*points=&c->model.tables[MKDS_T_COLV];
    const int64_t cx=((int64_t)at[0]-origin[0])>>STAND_IN_CELL,cy=((int64_t)at[1]-origin[1])>>STAND_IN_CELL;
    if (cx<0 || cy<0 || cx>=span[0] || cy>=span[1])return 0;
    const uint32_t cell=(uint32_t)(cy*span[0]+cx);
    int found=0;int64_t best=0;
    const int64_t px=at[0]>>8,py=at[1]>>8;
    for (uint32_t i=start[cell];i<start[cell+1];++i) {
        const uint8_t *row=faces->bytes->data+(size_t)cells[i]*32;
        int64_t v[3][3];
        for (unsigned k=0;k<3;++k) {
            const uint8_t *q=points->bytes->data+(size_t)ri_le32(row+k*4)*12;
            for (unsigned axis=0;axis<3;++axis)v[k][axis]=(int32_t)ri_le32(q+axis*4)>>8;
        }
        const int64_t e0=(v[1][0]-v[0][0])*(py-v[0][1])-(v[1][1]-v[0][1])*(px-v[0][0]);
        const int64_t e1=(v[2][0]-v[1][0])*(py-v[1][1])-(v[2][1]-v[1][1])*(px-v[1][0]);
        const int64_t e2=(v[0][0]-v[2][0])*(py-v[2][1])-(v[0][1]-v[2][1])*(px-v[2][0]);
        if (!((e0>=0 && e1>=0 && e2>=0) || (e0<=0 && e1<=0 && e2<=0)))continue;
        const int64_t a[3]={v[1][0]-v[0][0],v[1][1]-v[0][1],v[1][2]-v[0][2]};
        const int64_t b[3]={v[2][0]-v[0][0],v[2][1]-v[0][1],v[2][2]-v[0][2]};
        const int64_t n[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        if (!n[2])continue;
        const int64_t z=(v[0][2]-(n[0]*(px-v[0][0])+n[1]*(py-v[0][1]))/n[2])<<8;
        const int64_t d=(int64_t)at[2]-z;
        if (d<-STAND_IN_LOW)continue; /* above the point */
        if (!found || d<best) { best=d; found=1; }
    }
    *gap=best;
    return found;
}
static void drop_stand_ins(Course *c,RiBuf *opaque)
{
    RiCtx *ctx=c->model.ctx;
    const MkdsTable *faces=&c->model.tables[MKDS_T_COLT],*points=&c->model.tables[MKDS_T_COLV];
    if (!faces->count)return;
    int32_t low[2]={INT32_MAX,INT32_MAX},high[2]={INT32_MIN,INT32_MIN};
    for (uint32_t i=0;i<points->count;++i)
        for (unsigned axis=0;axis<2;++axis) {
            const int32_t v=(int32_t)ri_le32(points->bytes->data+(size_t)i*12+axis*4);
            if (v<low[axis])low[axis]=v;
            if (v>high[axis])high[axis]=v;
        }
    uint32_t span[2];
    for (unsigned axis=0;axis<2;++axis)span[axis]=(uint32_t)((((int64_t)high[axis]-low[axis])>>STAND_IN_CELL)+1);
    if ((uint64_t)span[0]*span[1]>65536u)ri_fail(ctx,"%s: collision extent too large",c->model.where);
    const uint32_t count=span[0]*span[1];
    /* Floor faces by cell (counting pass, then fill). */
    RiBuf *start_buf=ri_buf_zero(ctx,((size_t)count+1)*4),*fill_buf=ri_buf_zero(ctx,((size_t)count+1)*4);
    uint32_t *start=(uint32_t *)(void *)start_buf->data,*fill=(uint32_t *)(void *)fill_buf->data;
    for (int pass=0;pass<2;++pass) {
        for (uint32_t f=0;f<faces->count;++f) {
            const uint8_t *row=faces->bytes->data+(size_t)f*32;
            if (c->wall[ri_le32(row+12)])continue;
            int32_t lo[2]={INT32_MAX,INT32_MAX},hi[2]={INT32_MIN,INT32_MIN};
            for (unsigned k=0;k<3;++k)
                for (unsigned axis=0;axis<2;++axis) {
                    const int32_t v=(int32_t)ri_le32(points->bytes->data+(size_t)ri_le32(row+k*4)*12+axis*4);
                    if (v<lo[axis])lo[axis]=v;
                    if (v>hi[axis])hi[axis]=v;
                }
            for (uint32_t y=(uint32_t)(((int64_t)lo[1]-low[1])>>STAND_IN_CELL);y<=(uint32_t)(((int64_t)hi[1]-low[1])>>STAND_IN_CELL);++y)
                for (uint32_t x=(uint32_t)(((int64_t)lo[0]-low[0])>>STAND_IN_CELL);x<=(uint32_t)(((int64_t)hi[0]-low[0])>>STAND_IN_CELL);++x) {
                    const uint32_t cell=y*span[0]+x;
                    if (pass)((uint32_t *)(void *)c->stand_in_cells->data)[fill[cell]++]=f;
                    else ++start[cell+1];
                }
        }
        if (!pass) {
            for (uint32_t i=0;i<count;++i)start[i+1]+=start[i];
            memcpy(fill,start,(size_t)count*4);
            c->stand_in_cells=ri_buf_zero(ctx,(size_t)start[count]*4+4);
        }
    }
    const uint32_t *cells=(const uint32_t *)(const void *)c->stand_in_cells->data;
    Layered *face=(Layered *)(void *)opaque->data;
    const size_t total=opaque->size/sizeof *face;
    size_t kept=0;
    for (size_t i=0;i<total;++i) {
        int stand_in=face[i].solid && face[i].normal[2]>=UP_WALL;
        int64_t centre[3]={0,0,0};
        for (unsigned k=0;k<3;++k)for (unsigned axis=0;axis<3;++axis)centre[axis]+=face[i].corners[k].position[axis];
        for (unsigned k=0;stand_in && k<4;++k) {
            /* the centre, then each corner pulled a tenth of the way in */
            int32_t at[3];
            for (unsigned axis=0;axis<3;++axis)
                at[axis]=k==3 ? (int32_t)(centre[axis]/3) :
                    (int32_t)((face[i].corners[k].position[axis]*27LL+centre[axis])/30);
            int64_t gap;
            if (!collided_floor_under(c,cells,start,low,span,at,&gap))stand_in=k<3; /* a corner past the ground */
            else stand_in=gap>=(k==3 ? STAND_IN_LOW : -STAND_IN_LOW) && gap<=STAND_IN_HIGH;
        }
        if (!stand_in)face[kept++]=face[i];
    }
    opaque->size=kept*sizeof *face;
}

static int floor_at(const Course *c,const int32_t at[3],int32_t *out)
{
    return surface_at(c,at,out,0);
}

/* Arenas (spawn_players.c, 3P/4P battle): players one to four start at
 * source (0, d), (0, -d), (-d, 0) and (d, 0) at height h, facing the
 * centre, or along the ring on Big Donut; forward is (-sin a, cos a).
 * The source has no battle CPUs, so the patrol the engine's CPUs follow
 * is synthesized on the floor: Big Donut's ring of 16 nodes through the
 * starts; elsewhere each arm from the centre to a start has an out lane
 * and an in lane d/8 either side (a nearest-segment pilot cannot
 * tell overlapping legs apart), turning across the centre to the next. */
static void arena(Course *c,const Mk64CourseSource *s)
{
    MkdsModel *m=&c->model;
    if (s->arena_distance<=0) ri_fail(m->ctx,"%s: no arena spawn data",m->where);
    const int32_t d=s->arena_distance;
    const int32_t spot[4][2]={{0,d},{0,-d},{-d,0},{d,0}};
    const uint16_t yaw[2][4]={{0x8000,0,0xC000,0x4000},{0xC000,0x4000,0,0x8000}};
    for (unsigned slot=0;slot<4;++slot) {
        int32_t at[3]={metres(spot[slot][0]),metres(spot[slot][1]),metres(s->arena_height)};
        if (!floor_at(c,at,&at[2])) ri_fail(m->ctx,"%s: no floor under battle start %u",m->where,slot+1);
        /* The identity faces source rotation 0x8000 (engine -Y); turn about up from there. */
        int32_t sine,cosine;portable_fixed_3d_sincos(yaw[s->arena_ring][slot],&sine,&cosine);
        const int64_t f0[3]={0,-(1<<30),0},f[3]={-(int64_t)sine,cosine,0};
        const int64_t h[3]={f0[0]+f[0],f0[1]+f[1],0};
        int32_t half[3],q[4]={0,0,1<<30,0};
        if (portable_fixed_3d_normalize(h,half)) {
            q[2]=(int32_t)((f0[0]*half[1]-f0[1]*half[0])>>30); /* sin(phi/2): (f0 x half).z */
            q[3]=(int32_t)((f0[0]*half[0]+f0[1]*half[1])>>30); /* cos(phi/2): f0 . half */
        }
        const uint32_t row[8]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],
                               (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],0};
        mkds_words(m,MKDS_T_GRID,row,8);
    }
    const uint32_t first=m->tables[MKDS_T_RTND].count;
    const uint32_t continuous=mkds_property(m,"mksc:route-pilot-continuous",MKDS_PROPERTY_BOOLEAN,1,0);
    int32_t nodes[16][2];
    for (uint32_t i=0;i<16;++i) {
        if (s->arena_ring) {
            int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(i*4096u),&sine,&cosine);
            nodes[i][0]=(int32_t)((int64_t)metres(d)*cosine>>30);nodes[i][1]=(int32_t)((int64_t)metres(d)*sine>>30);
        } else {
            /* arms start 1, 3, 2, 4 (counter-clockwise); lanes lie toward the next arm */
            static const uint8_t order[4]={0,2,1,3};
            const int32_t *u=spot[order[i/4]],*v=spot[order[(i/4+1)%4]];
            static const int32_t along[4]={3,10,10,3},side[4]={1,1,-1,-1}; /* tenths, eighths of d */
            const int32_t reach=d*along[i%4]/10,lateral=d*side[i%4]/8;
            for (unsigned k=0;k<2;++k) nodes[i][k]=metres(u[k]*reach/d+v[k]*lateral/d);
        }
    }
    const uint32_t count=16;

    const uint32_t header[8]={0,first,count,first,1,continuous,1,0};
    mkds_words(m,MKDS_T_RTHD,header,8);
    for (uint32_t i=0;i<count;++i) {
        int32_t at[3]={nodes[i][0],nodes[i][1],metres(s->arena_height)};
        if (!floor_at(c,at,&at[2])) ri_fail(m->ctx,"%s: no floor under battle patrol node %u",m->where,i);
        const uint32_t edge=m->tables[MKDS_T_RTED].count,next=first+(i+1)%count;
        const uint32_t row[8]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],2u<<16,0,edge,1,0};
        mkds_words(m,MKDS_T_RTND,row,8);mkds_words(m,MKDS_T_RTED,&next,1);
    }
}

/* Lap gates, one respawn per gate, CPU routes over every source path, and
 * the GP grid of spawn_players.c: two columns 20 units either side of path
 * point 0 (the course axis where the source names it), slots 30 + 20n units
 * behind it, all facing the source start direction (rotation 0x8000, -Z). */
static void routes(Course *c,const Mk64CourseSource *s)
{
    MkdsModel *m=&c->model;
    for (uint32_t gate=0;gate<c->gate_count;++gate) {
        const int32_t *at=c->paths[c->gate_path[gate]][c->gate_point[gate]];
        const uint32_t stage=c->gate_stage[gate],next=(stage+1)%c->stage_count;
        int32_t normal[3],q[4],floor=at[2];
        heading(c,c->gate_path[gate],c->gate_point[gate],normal);
        const uint32_t row[16]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],
            (uint32_t)normal[0],(uint32_t)normal[1],0,(uint32_t)-normal[1],(uint32_t)normal[0],0,
            GATE_HALF_WIDTH,GATE_HALF_HEIGHT,stage,stage,m->tables[MKDS_T_CPED].count,
            c->stage_first[next+1]-c->stage_first[next],stage?4u:5u};
        mkds_words(m,MKDS_T_CPNT,row,16);
        for (uint32_t successor=c->stage_first[next];successor<c->stage_first[next+1];++successor)
            mkds_words(m,MKDS_T_CPED,&successor,1);
        floor_at(c,at,&floor);yaw_quaternion(normal,q);
        const uint32_t respawn[12]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)floor,
            (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],gate,RESPAWN_RADIUS,0,0,0};
        mkds_words(m,MKDS_T_RESP,respawn,12);
    }
    const uint32_t continuous=s->battle?0u:mkds_property(m,"mksc:route-pilot-continuous",MKDS_PROPERTY_BOOLEAN,1,0);
    for (unsigned path=0;path<4 && !s->battle;++path) {
        if (!c->paths[path])continue;
        int32_t (*p)[3]=c->paths[path];
        const uint32_t count=c->path_count[path];
        uint8_t *keep=ri_buf_zero(m->ctx,count)->data;
        uint32_t nodes=1;keep[0]=1;
        for (uint32_t i=1,last=0;i<count;++i)
            if (distance(p[last],p[i])>=NODE_SPACING && distance(p[i],p[0])>=NODE_SPACING/2) {
                keep[i]=1;last=i;++nodes;
            }
        if (nodes<3)ri_fail(m->ctx,"%s: source path %u is too short for a CPU route",m->where,path);
        const uint32_t first=m->tables[MKDS_T_RTND].count;
        const uint32_t header[8]={0,first,nodes,first,1,continuous,1,0};
        mkds_words(m,MKDS_T_RTHD,header,8);
        for (uint32_t i=0,node=0;i<count;++i) {
            if (!keep[i])continue;
            const uint32_t edge=m->tables[MKDS_T_RTED].count,next=first+(++node)%nodes;
            const uint32_t row[8]={(uint32_t)p[i][0],(uint32_t)p[i][1],(uint32_t)p[i][2],2u<<16,0,edge,1,0};
            mkds_words(m,MKDS_T_RTND,row,8);mkds_words(m,MKDS_T_RTED,&next,1);
        }
    }
    if (s->battle) {
        arena(c,s);
        return;
    }
    size_t offset;uint32_t count;
    source_path(c,s,0,&offset,&count);
    const uint8_t *start=ri_at(m->ctx,c->data,offset,8);
    const int32_t x=s->grid_on_axis?0:mk64_i16(start),y=mk64_i16(start+2),z=mk64_i16(start+4);
    for (uint32_t slot=0;slot<GRID_SLOTS;++slot) {
        int32_t at[3]={metres(x+(slot%2?-20:20)),metres(z+30+20*(int32_t)slot),metres(y)};
        floor_at(c,at,&at[2]);
        const uint32_t row[8]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],0,0,0,1u<<30,0};
        mkds_words(m,MKDS_T_GRID,row,8);
    }
}

static int course_floor(void *context,const int32_t at[3],int32_t *out)
{
    return floor_at((const Course *)context,at,out);
}
static int course_surface(void *context,const int32_t at[3],int32_t *out)
{
    return surface_at((const Course *)context,at,out,1);
}

/* The skybox (skybox_and_splitscreen.c course_set_skybox_colours): the
 * screen's upper half shades from the top to the bottom colour of the
 * course's sTopSkyBoxColors row and the lower half through its
 * sBottomSkyBoxColors row (12-byte s16 rows whose low bytes are the
 * channels). As a camera-anchored band: the screen edge sits about 20
 * degrees off the horizon at the source field of view. */
#define SKY_COLORS 0x1220E0u
#define SKY_RADIUS (400 << 16)
static uint32_t sky_color(const uint8_t *row)
{
    return (uint32_t)row[1]<<24 | (uint32_t)row[3]<<16 | (uint32_t)row[5]<<8 | 255u;
}
/* Sky sprites (code_8006E9C0.c course_init_cloud, update_objects.c
 * course_update_clouds): CloudData rows (BE u16 rotY, posY, scalePercent,
 * subType; ended by rotY 0xFFFF) place 64x32 I4 clouds, or 16x16 I4 stars,
 * at a fixed world yaw (-rotY) posY screen rows above the horizon, scaled
 * by scalePercent. They become textured quads on the sky sphere: one screen
 * row is 1/6 degree (gCameraZoom 40 over 240 rows). Mario Raceway sizes its
 * clouds from Kalimari Desert's rows but places them by Luigi Raceway's. */
#define CLOUD_LUIGI 0xE7638u
#define CLOUD_YOSHI 0xE76A8u /* also Moo Moo Farm */
#define CLOUD_KOOPA 0xE7700u
#define CLOUD_ROYAL 0xE7738u
#define CLOUD_SHERBET 0xE77A8u
#define CLOUD_KALIMARI 0xE7810u
#define STARS_TOAD 0xE7880u /* also Rainbow Road */
#define STARS_WARIO 0xE79E0u
#define STAR_TEXTURE 0x293D8u /* common data, 16x16 I4 */
static const struct { uint8_t course; uint32_t place,look,texture; uint8_t clouds; } sky_sprites[]={
    {0,CLOUD_LUIGI,CLOUD_KALIMARI,0x71903Cu,4},{4,CLOUD_YOSHI,CLOUD_YOSHI,0x717A84u,3},
    {6,CLOUD_KOOPA,CLOUD_KOOPA,0x71887Cu,4},{7,CLOUD_ROYAL,CLOUD_ROYAL,0x718C44u,4},
    {8,CLOUD_LUIGI,CLOUD_LUIGI,0x718388u,3},{9,CLOUD_YOSHI,CLOUD_YOSHI,0x717A84u,3},
    {10,STARS_TOAD,STARS_TOAD,0,0},{11,CLOUD_KALIMARI,CLOUD_KALIMARI,0x71903Cu,4},
    {12,CLOUD_SHERBET,CLOUD_SHERBET,0x717F00u,3},{13,STARS_TOAD,STARS_TOAD,0,0},
    {14,STARS_WARIO,STARS_WARIO,0,0},
};
/* Star alphas by index % 5 (update_stars): func_80073CB0 with step 0xFF and
 * no delay flips each star between its two alphas every object frame. */
static const uint8_t star_alpha[5][2]={{0x28,0xB4},{0x80,0xFF},{0x50,0xC8},{0,0x9B},{0x5A,0x80}};

/* An I4 texture as white light whose coverage is the texel times alpha/255. */
static uint32_t i4_image(Course *c,const uint8_t *texels,uint32_t width,uint32_t height,uint32_t alpha)
{
    MkdsModel *m=&c->model;
    RiBuf *pixels=ri_buf_zero(m->ctx,(size_t)width*height*4);
    for (size_t i=0;i<(size_t)width*height;++i) {
        const uint32_t v=(texels[i/2]>>(i%2?0:4)&15u)*17u;
        ri_set_pixel(pixels,i,(v*alpha/255u)<<24|v*0x010101u);
    }
    const uint32_t image=mkds_image(m,pixels,width,height);
    ri_buf_free(m->ctx,pixels);
    return image;
}

static uint32_t sky_material(Course *c,uint32_t image,uint32_t property)
{
    MkdsModel *m=&c->model;
    if (c->material_count==256) ri_fail(m->ctx,"%s: material limit",m->where);
    const uint32_t row[12]={image,ABSENT,ABSENT,ABSENT,0xFFFFFFFFu,0,65536,65536,65536,4u|8u|32u,property,1};
    mkds_words(m,MKDS_T_MATS,row,12);
    c->materials[c->material_count]=(MaterialKey){ABSENT-1u,0,0,0,0,0,0,0,ABSENT}; /* reserved: never matched */
    return c->material_count++;
}

static void sky_sprite_quads(Course *c,const Mk64CourseSource *s,uint32_t property)
{
    MkdsModel *m=&c->model;
    size_t entry=0;
    while (entry<sizeof(sky_sprites)/sizeof(sky_sprites[0]) && sky_sprites[entry].course!=s->index) ++entry;
    if (entry==sizeof(sky_sprites)/sizeof(sky_sprites[0])) return;
    const uint32_t clouds=sky_sprites[entry].clouds,width=clouds?64u:16u,height=clouds?32u:16u;
    uint32_t materials[5];
    if (clouds) {
        const uint32_t at=sky_sprites[entry].texture;
        RiBuf *sheet=mk64_mio0(m->ctx,ri_span(m->ctx,m->ctx->rom,at,m->ctx->rom.size-at));
        if (sheet->size<clouds*1024u) ri_fail(m->ctx,"%s: cloud textures are %zu bytes",m->where,sheet->size);
        for (uint32_t k=0;k<clouds;++k) materials[k]=sky_material(c,i4_image(c,sheet->data+k*1024u,64,32,255),property);
        ri_buf_free(m->ctx,sheet);
    } else {
        RiBuf *common=mk64_mio0(m->ctx,ri_span(m->ctx,m->ctx->rom,MK64_COMMON_DATA,MK64_COMMON_DATA_END-MK64_COMMON_DATA));
        const uint8_t *star=ri_at(m->ctx,ri_view(common),STAR_TEXTURE,128);
        /* One alpha per 30 Hz game frame: two ticks each. */
        for (uint32_t k=0;k<5;++k) {
            const uint32_t low=i4_image(c,star,16,16,star_alpha[k][0]),high=i4_image(c,star,16,16,star_alpha[k][1]);
            const uint32_t first=m->tables[MKDS_T_TXPT].count;
            const uint32_t dim[2]={0,low},bright[2]={2,high};
            mkds_words(m,MKDS_T_TXPT,dim,2);
            mkds_words(m,MKDS_T_TXPT,bright,2);
            materials[k]=sky_material(c,low,property);
            const uint32_t animation[8]={materials[k],1,4,first,2,0,0,0};
            mkds_words(m,MKDS_T_TXAN,animation,8);
        }
        ri_buf_free(m->ctx,common);
    }
    const int64_t one=PORTABLE_FIXED_3D_UNIT; /* Q30 */
    const int32_t normal[3]={0,0,PORTABLE_FIXED_3D_UNIT};
    for (uint32_t i=0;;++i) {
        const uint8_t *place=ri_at(m->ctx,m->ctx->rom,sky_sprites[entry].place+i*8u,8);
        const uint8_t *look=ri_at(m->ctx,m->ctx->rom,sky_sprites[entry].look+i*8u,8);
        if (mk64_be16(place)==0xFFFFu || mk64_be16(look)==0xFFFFu) break;
        if (i>=64) ri_fail(m->ctx,"%s: unterminated sky sprite list",m->where);
        const uint32_t subtype=mk64_be16(look+6),scale=mk64_be16(look+4);
        if (clouds && subtype>=clouds) ri_fail(m->ctx,"%s: cloud %u uses texture %u of %u",m->where,i,subtype,clouds);
        /* World yaw -rotY (MK64 x, z become our x, y); rows / 6 degrees up. */
        int32_t sy,cy,sp,cp,sw,cw,sh,ch;
        portable_fixed_3d_sincos((uint16_t)(0u-mk64_be16(place)),&sy,&cy);
        portable_fixed_3d_sincos((uint16_t)mkds_round_div((int64_t)mk64_i16(look+2)*65536,6*360),&sp,&cp);
        portable_fixed_3d_sincos((uint16_t)mkds_round_div((int64_t)width*scale*65536,100*6*360*2),&sw,&cw);
        portable_fixed_3d_sincos((uint16_t)mkds_round_div((int64_t)height*scale*65536,100*6*360*2),&sh,&ch);
        const int64_t d[3]={(int64_t)cp*sy/one,(int64_t)cp*cy/one,sp};
        const int64_t right[3]={-cy,sy,0}; /* up x d, unit */
        const int64_t up[3]={-(int64_t)sp*sy/one,-(int64_t)sp*cy/one,cp}; /* d x right */
        const int64_t half_w=(int64_t)SKY_RADIUS*sw/cw,half_h=(int64_t)SKY_RADIUS*sh/ch; /* Q16 */
        static const int8_t ox[4]={-1,1,1,-1},oy[4]={1,1,-1,-1};
        uint32_t corner[4];
        for (unsigned k=0;k<4;++k) {
            MkdsCorner v={.color=0xFFFFFFFFu,.node=NITRO_NO_NODE};
            for (unsigned a=0;a<3;++a)
                v.position[a]=(int32_t)(((int64_t)SKY_RADIUS*d[a]+ox[k]*half_w*right[a]+oy[k]*half_h*up[a])/one);
            v.uv[0]=ox[k]<0?0:65536;
            v.uv[1]=oy[k]>0?0:65536;
            corner[k]=mkds_render_vertex(m,&v,normal);
        }
        const uint32_t material=clouds?materials[subtype]:materials[i%5];
        const uint32_t a[4]={corner[0],corner[1],corner[2],material},b[4]={corner[0],corner[2],corner[3],material};
        mkds_words(m,MKDS_T_TRIS,a,4);
        mkds_words(m,MKDS_T_TRIS,b,4);
    }
}

static uint32_t sky(Course *c,const Mk64CourseSource *s)
{
    MkdsModel *m=&c->model;
    const uint8_t *top=ri_at(m->ctx,m->ctx->rom,SKY_COLORS+s->index*12,12);
    const uint8_t *bottom=ri_at(m->ctx,m->ctx->rom,SKY_COLORS+21*12+s->index*12,12);
    /* elevation (sine, Q16) and colour of each ring, zenith to nadir */
    const int32_t rise[6]={65536,22415,0,0,-22415,-65536};
    const uint32_t colors[6]={sky_color(top),sky_color(top),sky_color(top+6),sky_color(bottom),sky_color(bottom+6),sky_color(bottom+6)};
    const uint32_t property=mkds_property(m,"mksc:sky",MKDS_PROPERTY_BOOLEAN,1,0);
    if (c->material_count==256) ri_fail(m->ctx,"%s: material limit",m->where);
    const uint32_t material=c->material_count;
    const uint32_t row[12]={ABSENT,ABSENT,ABSENT,ABSENT,0xFFFFFFFFu,0,65536,65536,65536,4u|8u,property,1};
    mkds_words(m,MKDS_T_MATS,row,12);
    c->materials[c->material_count++]=(MaterialKey){ABSENT-1u,0,0,0,0,0,0,0,ABSENT}; /* reserved: never matched */
    const int32_t normal[3]={0,0,PORTABLE_FIXED_3D_UNIT};
    for (unsigned band=0;band<5;++band) {
        if (band==2)continue; /* the horizon seam between the two halves */
        for (unsigned step=0;step<16;++step) {
            uint32_t corner[4];
            for (unsigned k=0;k<4;++k) {
                const unsigned ring=band+(k>>1),around=(step+((k+1)>>1&1))%16;
                int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(around*4096u),&sine,&cosine); /* Q30 */
                const int64_t up=rise[ring],flat=(int64_t)portable_fixed_3d_sqrt((uint64_t)(((int64_t)65536*65536)-up*up));
                const int64_t across=(int64_t)SKY_RADIUS*flat/65536;
                MkdsCorner v={.position={(int32_t)(across*cosine/PORTABLE_FIXED_3D_UNIT),(int32_t)(across*sine/PORTABLE_FIXED_3D_UNIT),
                               (int32_t)((int64_t)SKY_RADIUS*up/65536)},.color=colors[ring],.node=NITRO_NO_NODE};
                corner[k]=mkds_render_vertex(m,&v,normal);
            }
            const uint32_t a[4]={corner[0],corner[1],corner[2],material},b[4]={corner[0],corner[2],corner[3],material};
            if (band!=0) mkds_words(m,MKDS_T_TRIS,a,4); /* the poles close each cap */
            if (band!=4) mkds_words(m,MKDS_T_TRIS,b,4);
        }
    }
    sky_sprite_quads(c,s,property);
    return sky_color(top);
}

/* Placed scenery with the segments its display lists address. */
static void scenery(Course *c,const Mk64CourseSource *s)
{
    RiCtx *ctx=c->model.ctx;
    Mk64Segments segments={{{0}}};
    segments.segment[3]=mk64_actor_textures(ctx,s->index);segments.segment[5]=c->textures;
    segments.segment[6]=c->data;segments.segment[7]=c->lists;
    segments.segment[13]=ri_view(mk64_mio0(ctx,ri_span(ctx,ctx->rom,MK64_COMMON_DATA,MK64_COMMON_DATA_END-MK64_COMMON_DATA)));
    mk64_objects(&c->model,&segments,s->index,course_floor,course_surface,c,(const int32_t (*)[3])c->paths[0],c->path_count[0]);
}

/* Item odds (gen_random_item): segment-13 curves of 100 item bytes per rank,
 * one picked at random. Grand Prix humans and CPUs have eight ranks, VS one
 * curve per player count (the engine's VS takes the 4P one; its CPUs, which
 * the original VS lacks, keep the GP CPU odds), and battle one row. */
enum
{
    MK64_CURVE_GP_HUMAN = 0x8150,
    MK64_CURVE_GP_CPU = 0x8470,
    MK64_CURVE_VS_4P = 0x8984,
    MK64_CURVE_BATTLE = 0x8B14,
    MK64_CURVE_ROW = 100
};
/* enum ITEMS (defines.h) to engine items; 0 is ITEM_NONE. */
static const uint16_t mk64_engine_items[16]={0,PORTABLE_MESH_ITEM_SINGLE_BANANA,PORTABLE_MESH_ITEM_BANANA_BUNCH,
    PORTABLE_MESH_ITEM_GREEN_SHELL,PORTABLE_MESH_ITEM_TRIPLE_GREEN,PORTABLE_MESH_ITEM_RED_SHELL,
    PORTABLE_MESH_ITEM_TRIPLE_RED,PORTABLE_MESH_ITEM_SPINY_SHELL,PORTABLE_MESH_ITEM_LIGHTNING,
    PORTABLE_MESH_ITEM_FAKE_BOX,PORTABLE_MESH_ITEM_STAR,PORTABLE_MESH_ITEM_BOO,PORTABLE_MESH_ITEM_MUSHROOM,
    PORTABLE_MESH_ITEM_DOUBLE_MUSHROOM,PORTABLE_MESH_ITEM_TRIPLE_MUSHROOM,PORTABLE_MESH_ITEM_GOLDEN_MUSHROOM};
static void curve_row(RiCtx *ctx,RiData common,uint32_t at,MkdsItemOdds row)
{
    const uint8_t *curve=ri_at(ctx,common,at,MK64_CURVE_ROW);
    unsigned counts[16]={0},written=0;
    for(unsigned i=0;i<MK64_CURVE_ROW;++i){
        if(curve[i]>15u)ri_fail(ctx,"MK64 item curve at segment 13 +0x%X holds item %u",(unsigned)at,curve[i]);
        ++counts[curve[i]];
    }
    memset(row,0,sizeof(MkdsItemOdds));
    for(unsigned item=1;item<16u;++item)if(counts[item])row[written++]=mk64_engine_items[item]|(uint32_t)counts[item]<<16;
}
void mk64_shared_sections(RiCtx *ctx)
{
    mkds_physics_section(ctx,MK64_PHYSICS_ID);
    const RiData common=ri_view(mk64_mio0(ctx,ri_span(ctx,ctx->rom,MK64_COMMON_DATA,MK64_COMMON_DATA_END-MK64_COMMON_DATA)));
    MkdsItemOdds human[8],cpu[8],versus[8],battle[8];
    for(unsigned rank=0;rank<8u;++rank){
        curve_row(ctx,common,MK64_CURVE_GP_HUMAN+rank*MK64_CURVE_ROW,human[rank]);
        curve_row(ctx,common,MK64_CURVE_GP_CPU+rank*MK64_CURVE_ROW,cpu[rank]);
        curve_row(ctx,common,MK64_CURVE_VS_4P+(rank<3u?rank:3u)*MK64_CURVE_ROW,versus[rank]);
        curve_row(ctx,common,MK64_CURVE_BATTLE,battle[rank]);
    }
    mkds_item_odds_section(ctx,MK64_ITEMS_ID,human,cpu);
    mkds_item_odds_section(ctx,MK64_ITEMS_VS_ID,versus,cpu);
    mkds_item_odds_section(ctx,MK64_ITEMS_BATTLE_ID,battle,battle);
}

/* Item boxes as ITEM rows: each ActorSpawnData {x, y, z, id} until x is
 * -0x8000. update_actor_item_box settles a box 8.66 units above its spawn
 * height; a spawn buried under the floor rises to it first. */
static void item_boxes(Course *c,const Mk64CourseSource *s)
{
    MkdsModel *m=&c->model;
    for (uint32_t i=0;;++i) {
        if (i==256)ri_fail(m->ctx,"%s: unterminated item box spawn list",m->where);
        const uint8_t *p=ri_at(m->ctx,c->data,s->item_boxes+i*8,8);
        if (mk64_be16(p)==0x8000u)break;
        int32_t at[3],floor;position(p,at);
        if (floor_at(c,at,&floor) && floor>at[2])at[2]=floor;
        at[2]+=ITEM_HOVER;
        const uint32_t row[8]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],ITEM_RADIUS,0,ITEM_RESPAWN_TICKS,0,0};
        mkds_words(m,MKDS_T_ITEM,row,8);
    }
    if (!m->tables[MKDS_T_ITEM].count)ri_fail(m->ctx,"%s: empty item box spawn list",m->where);
}

void mk64_course_convert(RiCtx *ctx,const Mk64CourseSource *s)
{
    static const int required[]={MKDS_T_STRS,MKDS_T_META,MKDS_T_VERT,MKDS_T_VCOL,MKDS_T_TRIS,MKDS_T_MATS,
        MKDS_T_COLV,MKDS_T_COLT,MKDS_T_SURF,MKDS_T_CPNT,MKDS_T_CPED,MKDS_T_RTHD,MKDS_T_RTND,MKDS_T_RTED,MKDS_T_GRID};
    Course c={0};char where[64];snprintf(where,sizeof where,"MK64 %s",s->name);
    mkds_model_init(&c.model,ctx,where,0x4E000000u|((s->index+1)<<12),required,sizeof required/sizeof required[0],0);
    const uint8_t *header=ri_at(ctx,ctx->rom,MK64_COURSE_TABLE+s->index*48,48);
    const uint32_t data_begin=mk64_be32(header),data_end=mk64_be32(header+4);
    const uint32_t vertex_begin=mk64_be32(header+8),packed_end=mk64_be32(header+12);
    const uint32_t table_begin=mk64_be32(header+16),table_end=mk64_be32(header+20);
    const uint32_t vertex_origin=mk64_be32(header+24),vertex_count=mk64_be32(header+28);
    const uint32_t packed_address=mk64_be32(header+32),last_command=mk64_be32(header+36);
    const uint32_t texture_table=mk64_be32(header+40);
    if (data_begin>=data_end || vertex_begin>=packed_end || table_begin>=table_end ||
        vertex_origin!=0x0F000000u || packed_address>>24!=15 || texture_table>>24!=9 ||
        !vertex_count || vertex_count>32768 || mk64_be16(header+46))
        ri_fail(ctx,"%s: invalid course header",where);
    c.data=ri_view(mk64_mio0(ctx,ri_span(ctx,ctx->rom,data_begin,data_end-data_begin)));
    RiData combined=ri_span(ctx,ctx->rom,vertex_begin,packed_end-vertex_begin);
    const size_t packed=packed_address&0xFFFFFFu;
    c.vertices=ri_view(mk64_mio0(ctx,ri_span(ctx,combined,0,packed)));c.vertex_count=vertex_count;
    const size_t vertex_bytes=(size_t)c.vertex_count*14;
    /* Some extraction/linker pipelines retain the final zero alignment pad. */
    if(c.vertices.size<vertex_bytes || c.vertices.size-vertex_bytes>15)
        ri_fail(ctx,"%s: vertex count/size mismatch",where);
    for(size_t i=vertex_bytes;i<c.vertices.size;++i)
        if(c.vertices.data[i])ri_fail(ctx,"%s: nonzero vertex alignment padding",where);
    c.lists=ri_view(mk64_display_lists(ctx,ri_span(ctx,combined,packed,combined.size-packed),last_command));
    RiData offsets=ri_span(ctx,ctx->rom,table_begin,table_end-table_begin);RiBuf *textures=ri_buf(ctx);
    size_t at=texture_table&0xFFFFFFu;unsigned texture_count=0;
    for (;;) {
        const uint8_t *p=ri_at(ctx,offsets,at,16);at+=16;
        const uint32_t address=mk64_be32(p),size=mk64_be32(p+4),length=mk64_be32(p+8);
        if(!address)break;
        if(address>>24!=15 || ++texture_count>128 || !size || !length || length>8192 || length%16 || mk64_be32(p+12))
            ri_fail(ctx,"%s: invalid texture table entry",where);
        RiBuf *image=mk64_mio0(ctx,ri_span(ctx,ctx->rom,MK64_TEXTURE_BASE+(address&0xFFFFFFu),size));
        if(image->size!=length)ri_fail(ctx,"%s: texture size mismatch",where);
        ri_put_buf(ctx,textures,image);ri_buf_free(ctx,image);
    }
    c.course_index=s->index;c.presence=c.present_property=ABSENT;c.lowest=INT16_MAX;
    c.textures=ri_view(textures);c.width=c.height=32;c.wrap=3;c.textured=1;c.alpha=s->opaque?0u:16u;c.texture_offset=0x05000000;
    for(unsigned i=0;i<32;++i)c.cache[i]=ABSENT;
    for(unsigned i=0;i<SCROLL_COUNT;++i) {
        c.owners[i]=ABSENT;
        if(scrolls[i].course!=s->index)continue;
        RiData list=scrolls[i].address>>24==6?c.data:c.lists;
        for(size_t command=scrolls[i].address&0xFFFFFFu;;command+=8) {
            const uint8_t op=*ri_at(ctx,list,command,8);
            if(op==0xB8)ri_fail(ctx,"%s: scrolled list without tile size",where);
            if(op==0xF2) { c.owners[i]=(scrolls[i].address&0xFF000000u)|(uint32_t)command;break; }
        }
    }
    /* Every course material samples with the N64's three-point filter and,
     * where the renderer sets G_FOG, fades into the fog. */
    const CourseEnvironment *environment=course_environment(s->index);
    c.material_property=mkds_property(&c.model,"mksc:texture-filter",MKDS_PROPERTY_INTEGER,2,0);
    c.material_property_count=1;
    if(environment && ++c.material_property_count)mkds_property(&c.model,"mksc:fog",MKDS_PROPERTY_BOOLEAN,1,0);
    entry_states(&c);
    vertex_tint_init(&c);
    c.later[0]=ri_buf(ctx);c.later[1]=ri_buf(ctx);c.later[2]=ri_buf(ctx);c.faces=ri_buf(ctx);
    /* Segment 7 holds the static course mesh. Each triangle command is
     * converted once in storage order, from its callers' entry state;
     * aggregate DL calls do not duplicate it. */
    for(size_t i=0;i<c.lists.size;i+=8) {
        if(c.has_entry[i/8])tex_merge(&c,&c.entry[i/8]);
        if(!i || c.lists.data[i-8]==0xB8)c.far=(uint32_t)far_list(c.course_index,0x07000000u|(uint32_t)i);
        if(list_presence(c.course_index,0x07000000u|(uint32_t)i)!=ABSENT)c.presence=list_presence(c.course_index,0x07000000u|(uint32_t)i);
        for(unsigned k=0;environment && k<3;++k)
            if(environment->decal_lists[k]==(0x07000000u|(uint32_t)i))c.decal=1;
        state(&c,c.lists.data+i,0);
        const uint8_t op=c.lists.data[i];
        if(op==0xF2)tile_size(&c,c.owners,0x07000000u|(uint32_t)i);
        else if(op==0xB8)c.scroll=0,c.decal=0,c.presence=ABSENT;
    }
    lake(&c,s->index);
    for(uint32_t i=0;i<c.vertex_count;++i) {
        int32_t p[3];position(c.vertices.data+i*14,p);
        const uint32_t row[3]={(uint32_t)p[0],(uint32_t)p[1],(uint32_t)p[2]};mkds_words(&c.model,MKDS_T_COLV,row,3);
    }
    track(&c,s);
    if(s->battle) {
        c.surface=surface(&c,s->arena_surface,0,ABSENT,0);
        for(unsigned i=0;i<5 && s->collision_lists[i];++i)collision_list(&c,s->collision_lists[i]);
    } else {
        uint32_t surfaces[2048];for(unsigned i=0;i<2048;++i)surfaces[i]=ABSENT;
        size_t offset=s->surfaces;
        for(unsigned i=0;;++i,offset+=8) {
            const uint8_t *p=ri_at(ctx,c.data,offset,8);const uint32_t address=mk64_be32(p);
            if(!address)break;
            if(i>=4096)ri_fail(ctx,"%s: unterminated surface map",where);
            const unsigned flags=mk64_be16(p+6),key=(unsigned)p[4]*8+(flags>>13);
            if(flags&0x1FFFu)ri_fail(ctx,"%s: unsupported surface flags",where);
            if(surfaces[key]==ABSENT)surfaces[key]=surface(&c,p[4],flags,ABSENT,0);
            c.surface=surfaces[key];collision_list(&c,address);
        }
        /* course_generate_collision_mesh adds lists outside the section
         * table with SURFACE_DEFAULT (0xFF): Mario Raceway's dl_1140 and its
         * one-player dl_8E8 (split screen uses dl_2D68). Synthetic test
         * courses with a shorter segment 7 have neither. */
        static const struct { uint32_t course, list; } extra[]={{0,0x07001140u},{0,0x070008E8u}};
        for(size_t i=0;i<sizeof extra/sizeof *extra;++i) {
            if(extra[i].course!=s->index || (extra[i].list&0xFFFFFFu)>=c.lists.size)continue;
            if(surfaces[0xFFu*8u]==ABSENT)surfaces[0xFFu*8u]=surface(&c,0xFFu,0,ABSENT,0);
            c.surface=surfaces[0xFFu*8u];collision_list(&c,extra[i].list);
        }
    }
    if(!c.model.tables[MKDS_T_COLT].count)ri_fail(ctx,"%s: empty collision mesh",where);
    drop_stand_ins(&c,c.later[0]);
    {
        const Ground lifted={(const Layered *)(const void *)c.later[0]->data,c.later[0]->size/sizeof(Layered)};
        RiBuf *points=ri_buf(ctx);
        for(unsigned k=0;k<3;++k)
            for(size_t i=0;i<c.later[k]->size/sizeof(Layered);++i)
                for(unsigned v=0;v<3;++v)
                    ri_put(ctx,points,((const Layered *)(const void *)c.later[k]->data)[i].corners[v].position,12);
        for(unsigned k=0;k<3;++k)
            emit_welded(&c,(const Layered *)(const void *)c.later[k]->data,c.later[k]->size/sizeof(Layered),k?&lifted:NULL,
                        (const int32_t (*)[3])(const void *)points->data,points->size/12);
    }
    if(!c.model.tables[MKDS_T_TRIS].count)ri_fail(ctx,"%s: empty course mesh",where);
    routes(&c,s);
    item_boxes(&c,s);
    if(s->scenery)scenery(&c,s);
    const uint32_t backdrop=s->scenery?sky(&c,s):0x5080B0FFu;
    const uint32_t id=MK64_COURSE_ID(s->index),cup=MK64_CUP_ID(s->cup),modes=s->battle?16u:15u;
    const char *cups[]={"Mushroom Cup","Flower Cup","Star Cup","Special Cup","Battle Stages"};
    const uint32_t prop=mkds_property(&c.model,"mksc:texture-bundle",MKDS_PROPERTY_ASSET,108,id);
    mkds_property(&c.model,"mksc:incomplete",MKDS_PROPERTY_STRING,mkds_string(&c.model,
        s->battle?"Source arena, item boxes, sky and the four original starts; CPUs patrol a synthesized loop (the source has none)."
                 :"Kart colour zones change at once instead of easing."),0);
    uint32_t properties=7;
    int32_t map_low[3],map_high[3];
    const uint32_t map=minimap(&c,s,map_low,map_high);
    mkds_property_vector(&c.model,"mksc:minimap-low",map_low);
    mkds_property_vector(&c.model,"mksc:minimap-high",map_high);
    mkds_property(&c.model,"mksc:item-rules",MKDS_PROPERTY_ASSET,PORTABLE_MESH_ITEM_RULES_KIND,s->battle?MK64_ITEMS_BATTLE_ID:MK64_ITEMS_ID);
    mkds_property(&c.model,"mksc:sound-set",MKDS_PROPERTY_ASSET,114u,MK64_SOUND_SET_ID);
    if(!s->battle && ++properties)
        mkds_property(&c.model,"mksc:item-rules-vs",MKDS_PROPERTY_ASSET,PORTABLE_MESH_ITEM_RULES_KIND,MK64_ITEMS_VS_ID);
    mkds_property(&c.model,"mksc:fx-set",MKDS_PROPERTY_ASSET,116u,MK64_FX_SET_ID);
    /* func_8002C4F8 calls Lakitu a kart height under func_802AAB4C's level:
     * the course's constant, else (course_generate_collision_mesh)
     * gCourseMinY - 10 on these courses; regional levels keep the default. */
    static const uint8_t lowest_level[]={0,4,8,9,10,11,14,15,17};
    int below_lowest=0;
    for(size_t i=0;i<sizeof lowest_level;++i)below_lowest|=lowest_level[i]==s->index;
    if((s->water || below_lowest) && ++properties)
        mkds_property(&c.model,"mksc:fall-height",MKDS_PROPERTY_FIXED,
                      (uint32_t)metres(s->water?s->water_level:c.lowest-10),0);
    properties+=chase_properties(&c.model,s->battle);
    /* N64 fog is linear in screen depth, so in 1/view depth. */
    uint32_t fog_rgba=backdrop&0xFFFFFF00u,fog_near=0,fog_far=512u<<16;
    if(environment) {
        properties+=2;
        mkds_property(&c.model,"mksc:fog",MKDS_PROPERTY_BOOLEAN,1,0);
        mkds_property(&c.model,"mksc:fog-curve",MKDS_PROPERTY_INTEGER,3,0);
        fog_rgba=environment->fog_rgb<<8|255u;
        fog_near=(uint32_t)fog_depth(environment->fog_min,environment->near,environment->far);
        fog_far=(uint32_t)fog_depth(environment->fog_max,environment->near,environment->far);
    }
    const uint32_t meta[32]={mkds_string(&c.model,s->name),mkds_string(&c.model,"Nintendo"),mkds_string(&c.model,cups[s->cup]),
        modes,s->battle?0u:3u,cup,s->slot,MK64_SONG_KIND,mk64_course_song(s->index),preview(&c,s),map,ABSENT,backdrop,fog_rgba,fog_near,fog_far,
        0xFFFFFFFF,0,0,(uint32_t)-1073741824,0xFFFFFFFF,(uint32_t)mkds_round_div((int64_t)281635*65536,10000),MK64_PHYSICS_ID,0,prop,properties};
    mkds_words(&c.model,MKDS_T_META,meta,32);
    RiBuf *catalog=ri_buf(ctx);ri_put(ctx,catalog,"C3C1",4);
    ri_words(ctx,catalog,9,1u,1u,24u,id,cup,s->slot,modes,s->cup*4+s->slot,0u);
    RiBuf *blob=mkds_model_blob(&c.model,"C3D1",id),*bundle=mkds_model_bundle(&c.model);
    ri_section(ctx,99,id,1,blob);ri_section(ctx,100,id,1,catalog);ri_section(ctx,108,id,1,bundle);
}
