#include "mk64_internal.h"
#include "mkds_model.h" /* shared table, image and property writers */
#include "fixed_3d.h"
#include "mesh_motion.h"
#include <stdio.h>
#include <string.h>

/* Course scenery placed by actors.c spawn_course_actors: each family is a
 * source actor type drawn by its render function (src/actors/<name>/render.inc.c)
 * from segment-6 display lists, with the texture and TLUT the function loads
 * first, and placed from a segment-6 spawn list or at the position the code
 * names. Every vertex, texture, palette and position is read from the ROM;
 * this table only names where (n64decomp/mk64 58cfcb0). */
/* How a kart reacts to a collidable actor (mksc:hazard-reaction): the
 * original's spinout, launch (vehicles, eggs), Thwomp squish or a plain bump. */
enum { REACT_BUMP, REACT_SPIN, REACT_TUMBLE, REACT_SQUASH };
/* Source objects (update_objects.c, func_8005A070), vehicles
 * (update_vehicles) and render-call animations step once a frame, which is
 * two engine ticks in 1P (gTickSpeed 2); actors step every tick. */
#define FRAME_TICKS 2u

static const char *const reaction_names[]={"bump","spin","tumble","squash"};

typedef struct Mk64Prop {
    uint8_t course;
    uint8_t actor;       /* source actor type (actor_types.h) */
    int16_t id;          /* spawn someId this family takes, -1 for every entry */
    uint32_t spawns;     /* segment-6 ActorSpawnData list, or 0: one placement at `at` */
    int16_t at[3];
    uint32_t lists[2];   /* segment-6 display lists drawn in order */
    uint32_t texture;    /* CI8 32x64 block the render function loads, or 0 */
    uint32_t tlut;       /* pal256 TLUT the render function loads, or 0 */
    uint8_t billboard;   /* render_course_actors' camera-yaw matrix */
    uint8_t radius;      /* collidable boundingBoxSize (actor_init), 0 when not */
    uint8_t spin;        /* degrees a frame about world up (update_actor_*_sign) */
    int16_t yaw;         /* source rot[1] of every placement */
    uint8_t stride;      /* spawn row bytes: 8 (ActorSpawnData), 10 (UnkActorSpawnData) */
    uint8_t edge;        /* the render function sets G_RM_AA_ZB_TEX_EDGE before the lists */
    uint8_t reaction;    /* REACT_* when collidable */
} Mk64Prop;

#define TREE_TLUT 0x0D004C68u /* common_tlut_trees_import */
static const Mk64Prop prop_families[] = {
    /* Mario Raceway: trees, piranha plants (frame 0 at 0x03009800, see flipbooks), signs */
    {0,2,-1,0x9570,{0},{0x06006A68},0,TREE_TLUT,1,3,0,0,8,0,0},
    {0,8,-1,0x9518,{0},{0x06006990},0x03009800,0,1,5,0,0,8,0,REACT_SPIN},
    {0,0x17,-1,0,{150,40,-1300},{0x06009330},0,0,0,0,1,0,8,0,0},
    {0,0x17,-1,0,{2520,0,1240},{0x06009330},0,0,0,0,1,0,8,0,0},
    /* Bowser's Castle: bushes (spawn_foliage type 0x21) */
    {2,0x21,-1,0x9290,{0},{0x060090C8},0,TREE_TLUT,1,3,0,0,8,0,0},
    /* Banshee Boardwalk: the trash bin (init_bb_trash_bin, object model dl_trash_bin) */
    {3,0x90,-1,0,{-1765,45,70},{0x0600A9D8},0,0,0,0,0,0,8,0,0},
    /* Yoshi Valley: trees */
    {4,3,-1,0x180A0,{0},{0x06015B48},0,TREE_TLUT,1,3,0,0,8,0,0},
    /* Frappe Snowland: snow trees */
    {5,0x1D,-1,0x7718,{0},{0x060075A0},0,0,1,3,0,0,8,0,0},
    /* Koopa Troopa Beach: palm trees, trunk then leaves per variant */
    {6,0x19,0,0x18F70,{0},{0x060186A8,0x060185E8},0,0,0,3,0,0,8,0,0},
    {6,0x19,1,0x18F70,{0},{0x060189F8,0x06018938},0,0,0,3,0,0,8,0,0},
    {6,0x19,2,0x18F70,{0},{0x06018D48,0x06018C88},0,0,0,3,0,0,8,0,0},
    /* Royal Raceway: castle trees (id 6), trees (id 7), piranha plants */
    {7,0x1C,6,0xDA78,{0},{0x0600D578},0,TREE_TLUT,1,0,0,0,8,0,0},
    {7,4,7,0xDA78,{0},{0x0600D4A0},0,TREE_TLUT,1,3,0,0,8,0,0},
    {7,8,-1,0xD9F0,{0},{0x0600D850},0x0300A000,0,1,5,0,0,8,0,REACT_SPIN},
    /* Luigi Raceway: trees (type 0x1A, func_80299864) */
    {8,0x1A,-1,0xFE80,{0},{0x0600FC70},0,TREE_TLUT,1,3,0,0,8,0,0},
    /* Moo Moo Farm: trees */
    {9,0x13,-1,0x14330,{0},{0x06013F20},0,TREE_TLUT,1,0,0,0,8,0,0},
    /* Kalimari Desert: three cacti */
    {11,0x1E,5,0x22F08,{0},{0x06008528},0,0,1,3,0,0,8,0,0},
    {11,0x1F,6,0x22F08,{0},{0x06008628},0,0,1,3,0,0,8,0,0},
    {11,0x20,7,0x22F08,{0},{0x06008728},0,0,1,3,0,0,8,0,0},
    /* Wario Stadium: signs */
    {14,0x22,-1,0,{-131,83,286},{0x0600CA60},0,0,0,0,1,0,8,0,0},
    {14,0x22,-1,0,{-2353,72,-1608},{0x0600CA60},0,0,0,0,1,0,8,0,0},
    {14,0x22,-1,0,{-2622,79,739},{0x0600CA60},0,0,0,0,1,0,8,0,0},
    /* Moo Moo Farm: cows (render_cows: someId picks dl_cow1..5) */
    {9,0x12,0,0x14200,{0},{0x06013C00},0,0,1,0,0,0,8,1,0},
    {9,0x12,1,0x14200,{0},{0x06013CA0},0,0,1,0,0,0,8,1,0},
    {9,0x12,2,0x14200,{0},{0x06013D20},0,0,1,0,0,0,8,1,0},
    {9,0x12,3,0x14200,{0},{0x06013DA0},0,0,1,0,0,0,8,1,0},
    {9,0x12,4,0x14200,{0},{0x06013E20},0,0,1,0,0,0,8,1,0},
    /* DK's Jungle Parkway (render_palm_trees, UnkActorSpawnData rows):
     * three billboard trees and the palm, someId 0/4/5/6 */
    {18,0x19,0,0x13F78,{0},{0x06010CC0},0,0,1,0,0,0,10,1,0},
    {18,0x19,4,0x13F78,{0},{0x06011DC8},0,0,1,0,0,0,10,1,0},
    {18,0x19,5,0x13F78,{0},{0x06012EF0},0,0,1,0,0,0,10,1,0},
    {18,0x19,6,0x13F78,{0},{0x060138D0},0,0,0,0,0,0,10,1,0},
};

/* init_actors_and_load_textures: segment 3 starts with the shell and
 * finish-banner textures (0x9000 bytes), then each course's own, in order,
 * as other_textures offsets (MIO0 blocks). */
static const uint32_t actor_textures[20][12] = {
    [0] = {0x4F45C,0x56408,0x5662C,0x5688C,0x56AD0,0x56CF0,0x56EC8,0x57084,0x57288,0x57590},
    [2] = {0x50FCC},
    [4] = {0x4F7A4},
    [5] = {0x513CC,0x51820},
    [7] = {0x4FB3C,0x50D50,0x56408,0x5662C,0x5688C,0x56AD0,0x56CF0,0x56EC8,0x57084,0x57288,0x57590},
    [8] = {0x50468,0x50678},
    /* trees, then the five cows' left and right halves (init_course_objects) */
    [9] = {0x4FE28,0x50118,0x51C54,0x51FD8,0x5232C,0x526B8,0x52A20,0x52D3C,0x5300C,0x532F8,0x5363C,0x53950},
    [11] = {0x53C34,0x53F74,0x54270,0x54518,0x5488C},
};
#define ACTOR_TEXTURE_BASE 0x9000u

RiData mk64_actor_textures(RiCtx *ctx, uint32_t index)
{
    RiBuf *out=ri_buf_zero(ctx,ACTOR_TEXTURE_BASE);
    for (unsigned i=0;i<12 && index<20 && actor_textures[index][i];++i) {
        const size_t at=MK64_TEXTURE_BASE+actor_textures[index][i];
        if (at>=ctx->rom.size)ri_fail(ctx,"MK64: actor texture outside the cartridge");
        RiBuf *image=mk64_mio0(ctx,ri_span(ctx,ctx->rom,at,ctx->rom.size-at));
        ri_put_buf(ctx,out,image);
        while (out->size%16)ri_put(ctx,out,"",1);
        ri_buf_free(ctx,image);
    }
    return ri_view(out);
}

/* One N64 texel as ARGB: RGBA16/32, CI4/8 through an RGBA16 TLUT, IA and I. */
static uint32_t rgba16(uint32_t v)
{
    const uint32_t r=(v>>11)&31,g=(v>>6)&31,b=(v>>1)&31;
    return (v&1?0xFF000000u:0) | ((r<<3)|(r>>2))<<16 | ((g<<3)|(g>>2))<<8 | (b<<3)|(b>>2);
}
static uint32_t texel(const uint8_t *data,uint32_t fmt,uint32_t siz,size_t i,const uint8_t *tlut,uint32_t palette)
{
    uint32_t v;
    if (siz==0)v=(data[i/2]>>(i%2?0:4))&15u;
    else if (siz==1)v=data[i];
    else if (siz==2)v=mk64_be16(data+i*2);
    else v=mk64_be32(data+i*4);
    if (fmt==0)return siz==3 ? (v&255u)<<24 | v>>8 : rgba16(v);
    if (fmt==2)return rgba16(mk64_be16(tlut+((siz==0?palette*16u:0u)+v)*2));
    if (fmt==3) {
        uint32_t i8,a;
        if (siz==0) { i8=(v>>1)*255/7; a=v&1?255u:0u; }
        else if (siz==1) { i8=(v>>4)*17; a=(v&15)*17; }
        else { i8=v>>8; a=v&255u; }
        return a<<24 | i8*0x010101u;
    }
    const uint32_t i8=siz==0?v*17:v;
    return i8<<24 | i8*0x010101u;
}

typedef struct Draw {
    MkdsModel m;
    const Mk64Segments *segments;
    uint8_t vertex[32][16];
    uint8_t loaded[32];
    uint32_t image, tlut, texture;    /* last SETTIMG, TLUT and loaded block sources */
    uint32_t fmt, siz, palette, cms, cmt, width, height, textured, alpha;
    uint32_t keys[16][11], key_count;
    unsigned operations;
    int32_t offset[3];                /* source units added to vertices (a placed part) */
    uint32_t shade[16][5];            /* per material: summed vertex RGBA and count (item art tint) */
    uint32_t geometry;                /* G_SETGEOMETRYMODE bits (G_TEXTURE_GEN) */
    uint8_t transformed;              /* xf places vertices (an armature limb) */
    int64_t xf[12];                   /* source space: Q30 rotation rows, Q16 source-unit translation */
    uint32_t tint;                    /* ARGB material tint, 0: white (translucent sprites) */
    uint8_t light[2][3];              /* G_MV_L0 (diffuse) and G_MV_L1 (ambient) colours */
    uint32_t blend;                   /* extra MTL1 flags (MKDS_MATERIAL_BLEND) */
    uint32_t combine;                 /* SETCOMBINE cycle 1 reads SHADE: COMBINE_* bits */
} Draw;
#define COMBINE_SET 1u
#define COMBINE_SHADE_RGB 2u
#define COMBINE_SHADE_ALPHA 4u
#define G_TEXTURE_GEN 0x00040000u
#define G_LIGHTING 0x00020000u

static const uint8_t *resolve(Draw *d,uint32_t address,size_t size)
{
    const unsigned segment=(address>>24)&15u;
    if (address>>28 || !d->segments->segment[segment].data)
        ri_fail(d->m.ctx,"%s: unresolved segment address 0x%08X",d->m.where,(unsigned)address);
    return ri_at(d->m.ctx,d->segments->segment[segment],address&0xFFFFFFu,size);
}

/* A lit face's colour seen half-on: ambient plus half the diffuse light. */
static uint32_t lit_color(const Draw *d)
{
    uint32_t c=0xFF000000u;
    for (unsigned k=0;k<3;++k) {
        const uint32_t v=d->light[1][k]+d->light[0][k]/2u;
        c|=(v>255u?255u:v)<<(16-8*k);
    }
    return c;
}
static uint32_t draw_material(Draw *d)
{
    MkdsModel *m=&d->m;
    /* untextured lit faces (G_LIGHTING) take their lights' colour */
    const uint32_t lit=!d->textured && (d->geometry&G_LIGHTING) ? lit_color(d) : 0u;
    const uint32_t key[11]={d->textured?d->texture:MKDS_ABSENT,d->fmt,d->siz,d->width,d->height,
                            d->fmt==2?d->tlut:0,d->palette,d->cms,d->cmt,d->alpha,lit};
    for (uint32_t i=0;i<d->key_count;++i)
        if (!memcmp(key,d->keys[i],sizeof key))return i;
    if (d->key_count==16)ri_fail(m->ctx,"%s: more than 16 materials",m->where);
    uint32_t image=MKDS_ABSENT,flags=0;
    if (d->textured) {
        if (!d->width || !d->height || d->width>256 || d->height>256 || d->siz>3)
            ri_fail(m->ctx,"%s: unsupported texture tile %ux%u",m->where,(unsigned)d->width,(unsigned)d->height);
        const size_t bytes=((size_t)d->width*d->height*(d->siz?8u<<(d->siz-1):4u)+7)/8;
        const uint8_t *source=resolve(d,d->texture,bytes);
        const uint8_t *tlut=d->fmt==2?resolve(d,d->tlut,512):NULL;
        const uint32_t mirror_u=d->cms&1,mirror_v=d->cmt&1;
        const uint32_t width=d->width<<mirror_u,height=d->height<<mirror_v;
        RiBuf *pixels=ri_buf_zero(m->ctx,(size_t)width*height*4);
        for (uint32_t y=0;y<height;++y) for (uint32_t x=0;x<width;++x) {
            const uint32_t sx=x<d->width?x:width-1-x,sy=y<d->height?y:height-1-y;
            uint32_t color=texel(source,d->fmt,d->siz,(size_t)sy*d->width+sx,tlut,d->palette);
            if (!d->alpha)color|=0xFF000000u;
            ri_set_pixel(pixels,(size_t)y*width+x,color);
        }
        image=mkds_image(m,pixels,width,height);
        ri_buf_free(m->ctx,pixels);
        flags=(d->cms&2?0u:1u)|(d->cmt&2?0u:2u);
    }
    const uint32_t row[12]={image,MKDS_ABSENT,MKDS_ABSENT,MKDS_ABSENT,0xFFFFFFFFu,0,65536,65536,65536,
                            4u|8u|flags,0,0};
    mkds_words(m,MKDS_T_MATS,row,12);
    memcpy(d->keys[d->key_count],key,sizeof key);
    return d->key_count++;
}

static void draw_triangle(Draw *d,unsigned a,unsigned b,unsigned c)
{
    MkdsModel *m=&d->m;
    const unsigned slots[3]={a,c,b}; /* the axis swap reverses winding */
    MkdsCorner corners[3]; uint32_t row[4];
    for (unsigned k=0;k<3;++k) {
        if (slots[k]%2 || slots[k]/2>=32 || !d->loaded[slots[k]/2])
            ri_fail(m->ctx,"%s: triangle uses unloaded vertex",m->where);
        const uint8_t *v=d->vertex[slots[k]/2];
        int64_t at[3]={mk64_i16(v)+d->offset[0],mk64_i16(v+2)+d->offset[1],mk64_i16(v+4)+d->offset[2]};
        if (d->transformed) {
            int64_t moved[3];
            for (unsigned r=0;r<3;++r)
                moved[r]=((d->xf[r*3]*at[0]+d->xf[r*3+1]*at[1]+d->xf[r*3+2]*at[2])>>14)+d->xf[9+r]; /* Q16 */
            for (unsigned r=0;r<3;++r)at[r]=moved[r];
        } else for (unsigned r=0;r<3;++r)at[r]*=65536;
        corners[k].position[0]=(int32_t)(at[0]*5/88);
        corners[k].position[1]=(int32_t)(at[2]*5/88);
        corners[k].position[2]=(int32_t)(at[1]*5/88);
        const int32_t w=(int32_t)(d->width?d->width:32)<<(d->cms&1),h=(int32_t)(d->height?d->height:32)<<(d->cmt&1);
        corners[k].uv[0]=mk64_i16(v+8)*2048/w;
        corners[k].uv[1]=mk64_i16(v+10)*2048/h;
        if (d->geometry&G_TEXTURE_GEN) {
            /* G_TEXTURE_GEN maps the vertex normal (the colour bytes) over
             * the texture, here in model space. */
            corners[k].uv[0]=((int8_t)v[12]+128)*256;
            corners[k].uv[1]=(128-(int8_t)v[13])*256;
        }
        /* Unlit faces whose combiner reads SHADE (G_CC_SHADE frames, the
         * G_CC_MODULATEIA chassis and cab of the Kalimari train) show their
         * vertex colour; lit ones keep normals there. Without a combiner,
         * untextured faces take it too. */
        const uint32_t shade=d->combine&COMBINE_SET ? d->combine : d->textured ? 0u : COMBINE_SHADE_RGB|COMBINE_SHADE_ALPHA;
        corners[k].color=0xFFFFFFFFu;
        if (!(d->geometry&G_LIGHTING)) {
            /* MkdsCorner colours are RGBA */
            if (shade&COMBINE_SHADE_RGB)corners[k].color=(uint32_t)v[12]<<24|(uint32_t)v[13]<<16|(uint32_t)v[14]<<8|255u;
            if (shade&COMBINE_SHADE_ALPHA)corners[k].color=(corners[k].color&0xFFFFFF00u)|v[15];
        }
        corners[k].node=NITRO_NO_NODE;
    }
    int64_t e[2][3],cross[3]; int32_t normal[3];
    for (unsigned i=0;i<3;++i) {
        e[0][i]=(int64_t)corners[1].position[i]-corners[0].position[i];
        e[1][i]=(int64_t)corners[2].position[i]-corners[0].position[i];
    }
    for (unsigned i=0;i<3;++i)cross[i]=e[0][(i+1)%3]*e[1][(i+2)%3]-e[0][(i+2)%3]*e[1][(i+1)%3];
    if (!portable_fixed_3d_normalize(cross,normal))return;
    for (unsigned k=0;k<3;++k)row[k]=mkds_render_vertex(m,&corners[k],normal);
    row[3]=draw_material(d);
    for (unsigned k=0;k<3;++k) {
        const uint8_t *v=d->vertex[slots[k]/2];
        for (unsigned channel=0;channel<4;++channel)d->shade[row[3]][channel]+=v[12+channel];
        ++d->shade[row[3]][4];
    }
    mkds_words(m,MKDS_T_TRIS,row,4);
}

/* Raw F3DEX (gbi.h, F3DEX_GBI): geometry, nested lists, texture image,
 * tile and TLUT loads and the render mode; lighting and syncs are ignored. */
static void draw_list(Draw *d,uint32_t address,unsigned depth)
{
    MkdsModel *m=&d->m;
    if (depth>8)ri_fail(m->ctx,"%s: display-list nesting limit",m->where);
    for (;;) {
        if (++d->operations>100000u)ri_fail(m->ctx,"%s: display-list cycle/budget",m->where);
        if (address&7)ri_fail(m->ctx,"%s: misaligned display list 0x%08X",m->where,(unsigned)address);
        const uint8_t *p=resolve(d,address,8);
        const uint32_t a=mk64_be32(p),b=mk64_be32(p+4),op=a>>24;
        address+=8;
        switch (op) {
        case 0xB8: return;
        case 0x06:
            draw_list(d,b,depth+1);
            if (a&0x10000u)return;
            break;
        case 0x04: {
            const unsigned count=((a&65535u)+1u)/0x410u,first=(a>>17)&127u;
            if (!count || first+count>32)ri_fail(m->ctx,"%s: invalid vertex load",m->where);
            const uint8_t *v=resolve(d,b,(size_t)count*16);
            for (unsigned i=0;i<count;++i) { memcpy(d->vertex[first+i],v+i*16,16); d->loaded[first+i]=1; }
            break;
        }
        case 0xBF: draw_triangle(d,(b>>16)&255u,(b>>8)&255u,b&255u); break;
        case 0xB1:
            draw_triangle(d,(a>>16)&255u,(a>>8)&255u,a&255u);
            draw_triangle(d,(b>>16)&255u,(b>>8)&255u,b&255u);
            break;
        case 0xBB: d->textured=a&1u; break;
        case 0xB7: d->geometry|=b; break;
        case 0xB6: d->geometry&=~b; break;
        case 0xFD: d->image=b; break;
        case 0xF3: case 0xF4: d->texture=d->image; break;
        case 0xF0: d->tlut=d->image; break;
        case 0xF5:
            if (b>>24==0) {
                d->fmt=(a>>21)&7u; d->siz=(a>>19)&3u; d->palette=(b>>20)&15u;
                d->cmt=(b>>18)&3u; d->cms=(b>>8)&3u;
            }
            break;
        case 0xF2:
            if (b>>24==0) { d->width=(((b>>12)&4095u)-((a>>12)&4095u))/4+1; d->height=((b&4095u)-(a&4095u))/4+1; }
            break;
        case 0xB9:
            /* G_RM_*_TEX_EDGE selects coverage by alpha; XLU modes blend,
             * XLU_DECAL (ZMODE_DEC) too: the Kalimari train redraws its
             * cutout wheels in it, which would otherwise come out square. */
            if ((a&0xFFFFu)==0x031Du)d->alpha=(b&0x1000u) || ((b&0x4000u) && (b&0x800u)) ? 16u : 0u;
            break;
        case 0x03:
            /* G_MOVEMEM lights: their colour bytes */
            if (((a>>16)&255u)==0x86u || ((a>>16)&255u)==0x88u)
                memcpy(d->light[((a>>16)&255u)==0x88u],resolve(d,b,3),3);
            break;
        case 0xFC: {
            /* G_SETCOMBINE cycle 1: colour (a-b)*c+d, alpha likewise; mux 4 is SHADE */
            const unsigned ca=(a>>20)&15u,cc=(a>>15)&31u,cb=(b>>28)&15u,cd=(b>>15)&7u;
            const unsigned aa=(a>>12)&7u,ac=(a>>9)&7u,ab=(b>>12)&7u,ad=(b>>9)&7u;
            d->combine=COMBINE_SET|(ca==4u||cb==4u||cc==4u||cd==4u ? COMBINE_SHADE_RGB : 0u)|
                       (aa==4u||ab==4u||ac==4u||ad==4u ? COMBINE_SHADE_ALPHA : 0u);
            break;
        }
        case 0x01: case 0xBA: case 0xBC: case 0xBD: case 0xBE:
        case 0xE6: case 0xE7: case 0xE8: case 0xF8: case 0xF9: case 0xFA: case 0xFB:  case 0x00:
            break;
        default:
            ri_fail(m->ctx,"%s: unsupported display-list command 0x%02X",m->where,(unsigned)op);
        }
    }
}

/* MSH1 over the drawn tables: 20-byte vertices (Q16 metres, UV), 8-byte
 * triangles and one MTL1 per MATS row (images embedded, white tint). */
static void write_mesh_shaded(Draw *d,uint32_t mesh_id,uint32_t material_base,uint32_t extra_flags,int shaded);
static void write_mesh(Draw *d,uint32_t mesh_id,uint32_t material_base)
{
    write_mesh_shaded(d,mesh_id,material_base,0,0);
}
/* shaded: untextured materials take their vertices' mean colour as tint. */
static void write_mesh_shaded(Draw *d,uint32_t mesh_id,uint32_t material_base,uint32_t extra_flags,int shaded)
{
    MkdsModel *m=&d->m;RiCtx *ctx=m->ctx;
    const MkdsTable *vert=&m->tables[MKDS_T_VERT],*tris=&m->tables[MKDS_T_TRIS],*mats=&m->tables[MKDS_T_MATS];
    if (!tris->count || tris->count>4096 || vert->count>2048)
        ri_fail(ctx,"%s: %u vertices and %u triangles (meshes hold 1..4096 triangles over 2048 vertices)",
                m->where,(unsigned)vert->count,(unsigned)tris->count);
    RiBuf *mesh=ri_buf(ctx);
    ri_put(ctx,mesh,"MSH1",4);
    ri_words(ctx,mesh,3,vert->count,tris->count,mats->count);
    for (uint32_t slot=0;slot<mats->count;++slot) {
        const uint8_t *row=mats->bytes->data+slot*mats->stride;
        const uint32_t texture=ri_le32(row),flags=ri_le32(row+36)&3u;
        const RiBuf *image=texture!=MKDS_ABSENT?m->textures[texture-m->texture_base].image:NULL;
        uint32_t tint=d->tint?d->tint:0xFFFFFFFFu;
        if (!image && d->keys[slot][10])tint=d->keys[slot][10];
        if (shaded && !image && d->shade[slot][4]) {
            const uint32_t n=d->shade[slot][4];
            tint=(d->shade[slot][3]/n)<<24 | (d->shade[slot][0]/n)<<16 | (d->shade[slot][1]/n)<<8 | d->shade[slot][2]/n;
        }
        ri_section(ctx,MKDS_KIND_MATERIAL,material_base|slot,1,
                   mkds_material_section(m,tint,flags|extra_flags|d->blend,image));
        ri_u32(ctx,mesh,material_base|slot);
    }
    for (uint32_t i=0;i<vert->count;++i) {
        ri_put(ctx,mesh,vert->bytes->data+i*32,12);
        ri_put(ctx,mesh,vert->bytes->data+i*32+24,8);
    }
    for (uint32_t i=0;i<tris->count;++i)
        for (unsigned k=0;k<4;++k)ri_u16(ctx,mesh,(uint16_t)ri_le32(tris->bytes->data+i*16+k*4));
    ri_section(ctx,MKDS_KIND_MESH,mesh_id,1,mesh);
}

/* Texture flipbooks: the frames a render function steps through, from a
 * frame-0 texture block, `stride` bytes apart. The loop shows frames
 * first..first+count-1 for `ticks` ticks each, the last for `last` ticks.
 * piranha_plant/update.inc.c runs timers 6..60 and render.inc.c draws
 * timer/6 (at most 8) while a camera is near; the Koopa Troopa Beach crab's
 * func_80082B34 walks frames 0..3, two ticks each (func_80072E54). */
typedef struct Mk64Flipbook { uint8_t course, actor; uint16_t stride; uint8_t first, count, ticks, last; } Mk64Flipbook;
static const Mk64Flipbook flipbooks[] = {
    {0,8,0x800,1,8,6,13}, {7,8,0x800,1,8,6,13}, {6,0x80,0x1000,0,4,2,2},
};
static const Mk64Flipbook *flipbook(uint32_t course,uint32_t actor)
{
    for (size_t i=0;i<sizeof flipbooks/sizeof flipbooks[0];++i)
        if (flipbooks[i].course==course && flipbooks[i].actor==actor)return &flipbooks[i];
    return NULL;
}

/* RIG1 (kind 109) for a written mesh: vertex nodes (NULL: all static) with
 * one looping clip of `frames` x node_count poses, and material swaps. */
static void write_rig(Draw *d,uint32_t mesh_id,const uint8_t *nodes,uint32_t node_count,uint32_t frames,
                      RiBuf *poses,RiBuf *swaps,uint32_t swap_count)
{
    RiCtx *ctx=d->m.ctx;
    const uint32_t vertices=d->m.tables[MKDS_T_VERT].count;
    RiBuf *rig=ri_buf(ctx);
    ri_put(ctx,rig,"RIG1",4);
    ri_words(ctx,rig,7,vertices,node_count?node_count:1u,frames?1u:0u,frames*node_count,swap_count,0u,0u);
    for (uint32_t i=0;i<vertices;++i)ri_u8(ctx,rig,nodes?nodes[i]:255u);
    ri_put_zero(ctx,rig,(4u-vertices%4u)%4u);
    if (frames)ri_words(ctx,rig,4,0x504F4F4Cu /* "LOOP" */,frames,0u,1u);
    if (poses)ri_put_buf(ctx,rig,poses);
    if (swaps)ri_put_buf(ctx,rig,swaps);
    ri_section(ctx,109,mesh_id,1,rig);
}

/* The flipbook's other frames as material slots beside the drawn one (call
 * before write_mesh), and their swap rows. Returns the swap count. */
static uint32_t flipbook_swaps(Draw *d,const Mk64Flipbook *book,uint32_t frame0,RiBuf *swaps)
{
    const uint32_t drawn=frame0+book->first*book->stride;
    uint32_t slot=d->key_count;
    for (uint32_t i=0;i<d->key_count;++i)if (d->keys[i][0]==drawn)slot=i;
    if (slot==d->key_count)ri_fail(d->m.ctx,"%s: flipbook frame is not drawn",d->m.where);
    uint32_t key[11];memcpy(key,d->keys[slot],sizeof key);
    const uint32_t period=((book->count-1u)*book->ticks+book->last)*FRAME_TICKS; /* frames */
    for (uint32_t frame=1;frame<book->count;++frame) {
        d->textured=1;d->texture=drawn+frame*book->stride;d->fmt=key[1];d->siz=key[2];d->width=key[3];d->height=key[4];
        d->tlut=key[5];d->palette=key[6];d->cms=key[7];d->cmt=key[8];d->alpha=key[9];
        ri_words(d->m.ctx,swaps,4,slot,draw_material(d),period,frame*book->ticks*FRAME_TICKS);
    }
    return book->count-1u;
}

/* Lamps that alternate: each row turns a drawn texture into another from
 * `start` until the period ends. railroad_crossing/update.inc.c counts
 * someTimer 1..40 while a train is near and render.inc.c draws
 * dl_crossing_right_active below 20, dl_crossing_left_active from 20. */
typedef struct Mk64Lamp {
    uint8_t course, actor, period, count;
    struct { uint32_t drawn, alternate; uint8_t start; } swaps[4];
} Mk64Lamp;
static const Mk64Lamp lamps[] = {
    {11,0x27,40,4,{{0x0600D7E8,0x060097E8,19},{0x0600F7E8,0x0600B7E8,19}, /* right: active to inactive */
                   {0x060087E8,0x0600C7E8,19},{0x0600A7E8,0x0600E7E8,19}}}, /* left: inactive to active */
};

static uint32_t lamp_swaps(Draw *d,uint32_t course,uint32_t actor,RiBuf *swaps)
{
    for (size_t i=0;i<sizeof lamps/sizeof lamps[0];++i) {
        const Mk64Lamp *lamp=&lamps[i];
        if (lamp->course!=course || lamp->actor!=actor)continue;
        for (uint32_t k=0;k<lamp->count;++k) {
            uint32_t slot=d->key_count;
            for (uint32_t j=0;j<d->key_count;++j)if (d->keys[j][0]==lamp->swaps[k].drawn)slot=j;
            if (slot==d->key_count)ri_fail(d->m.ctx,"%s: lamp texture is not drawn",d->m.where);
            uint32_t key[11];memcpy(key,d->keys[slot],sizeof key);
            d->textured=1;d->texture=lamp->swaps[k].alternate;d->fmt=key[1];d->siz=key[2];d->width=key[3];
            d->height=key[4];d->tlut=key[5];d->palette=key[6];d->cms=key[7];d->cmt=key[8];d->alpha=key[9];
            ri_words(d->m.ctx,swaps,4,slot,draw_material(d),(uint32_t)lamp->period*FRAME_TICKS,
                     (uint32_t)lamp->swaps[k].start*FRAME_TICKS);
        }
        return lamp->count;
    }
    return 0;
}

/* Rolling stock (vehicle_utils.inc.c, src/actors/train/render.inc.c): a
 * model is display lists placed at source offsets; rolling parts turn about
 * their offset by the actor's wheelRot step a frame plus their own phase. */
typedef struct Mk64Part { uint32_t list; int16_t at[3]; uint8_t roll, phase; /* degrees */ } Mk64Part;
typedef struct Mk64Model {
    uint8_t actor;
    uint16_t box[3];       /* collision box: twice is_collide_with_vehicle's half width, half length and height */
    uint8_t reaction;      /* vehicles launch the kart (func_80013D20); the paddle boat walls it */
    int8_t roll;           /* wheelRot degrees a frame (update.inc.c) */
    uint32_t scale;        /* Q16 (the car's mtxf_scale) */
    Mk64Part parts[14];
} Mk64Model;
#define BODY(list) {list,{0,0,0},0,0}
#define WHEEL_SETUP BODY(0x06022D28)
#define WHEEL(x,y,z,phase) {0x06022DB8,{x,y,z},1,phase},{0x06022DB8,{-(x),y,z},1,phase}
#define DRIVER(x,y,z,phase) {0x06022D70,{x,y,z},1,phase},{0x06022D70,{-(x),y,z},1,phase}
static const Mk64Model models[] = {
    {0x0E,{40,120,40},REACT_TUMBLE,-9,65536,{BODY(0x0601C0F0),BODY(0x0601B978),WHEEL_SETUP,WHEEL(17,6,32,0),WHEEL(17,6,16,2),
                       DRIVER(17,12,-12,6),DRIVER(17,12,-34,4)}},
    {0x0F,{40,60,40},REACT_TUMBLE,-7,65536,{BODY(0x0601F228),WHEEL_SETUP,WHEEL(17,6,8,0),WHEEL(17,6,-8,6)}},
    {0x10,{40,60,40},REACT_TUMBLE,-9,65536,{BODY(0x06020A20),BODY(0x06020A08),WHEEL_SETUP,WHEEL(17,6,28,0),WHEEL(17,6,12,3),
                       WHEEL(17,6,-8,8),WHEEL(17,6,-24,2)}},
    /* src/actors/paddle_boat: hull, railings, paddle wheel */
    {0x25,{120,400,120},REACT_BUMP,5,65536,{BODY(0x0600E700),BODY(0x0600E048),{0x0600FC18,{0,16,-255},1,0}}},
    /* Toad's Turnpike (course_offsets.c toads_turnpike_dl_0..9, nearest detail):
     * box trucks in three box liveries, school bus, tanker truck, car */
#define TURNPIKE(body,rear) BODY(0x0D005398),BODY(body),BODY(0x0D0053B0),BODY(rear)
    {0x24,{25,110,40},REACT_TUMBLE,0,65536,{BODY(0x06023858),TURNPIKE(0x06019518,0x06019020)}},
    {0x24,{25,110,40},REACT_TUMBLE,0,65536,{BODY(0x060238A0),TURNPIKE(0x06019518,0x06019020)}},
    {0x24,{25,110,40},REACT_TUMBLE,0,65536,{BODY(0x060238E8),TURNPIKE(0x06019518,0x06019020)}},
    {0x27,{25,140,40},REACT_TUMBLE,0,65536,{TURNPIKE(0x0601BE48,0x0601B778)}},
    {0x28,{25,110,40},REACT_TUMBLE,0,65536,{TURNPIKE(0x0601EB48,0x0601E458)}},
    {0x2B,{17,23,40},REACT_TUMBLE,0,6554,{TURNPIKE(0x06021E28,0x06021780)}},
};
enum { LOCOMOTIVE, TENDER, PASSENGER_CAR, PADDLE_BOAT, BOX_TRUCK, SCHOOL_BUS=BOX_TRUCK+3, TANKER_TRUCK, CAR };

/* A convoy family: `count` convoys spread evenly over the path's resampled
 * points from `first`; each car steps `gap` points further, rear to front,
 * and the convoy runs `speed` source units a frame at source height `height`,
 * or with `floor` at the floor under the path's first point
 * (init_vehicles_trains, init_vehicles_ferry). */
typedef struct Mk64Convoy {
    uint8_t course, count, cars_count;
    uint16_t first;
    uint32_t path;         /* segment-6 TrackPathPoint list */
    uint16_t speed;        /* source units a frame, Q8 */
    uint8_t floor;
    int16_t height;
    struct { uint8_t model, gap; } cars[8];
} Mk64Convoy;
static const Mk64Convoy convoys[] = {
    /* Kalimari Desert: two trains of five passenger cars, a tender and the locomotive */
    {11,2,7,160,0x6C60,5*256,1,0,{{PASSENGER_CAR,4},{PASSENGER_CAR,4},{PASSENGER_CAR,4},{PASSENGER_CAR,4},
                           {PASSENGER_CAR,4},{TENDER,3},{LOCOMOTIVE,4}}},
    /* DK's Jungle Parkway: one paddle boat at 1.67 units a frame, at -40 */
    {18,1,1,0,0x7520,427,0,-40,{{PADDLE_BOAT,0}}},
};

/* Toad's Turnpike traffic (initialize_toads_turnpike_vehicle): `count`
 * vehicles spread over race path 0 from `first`; vehicle i drives lane
 * i % 3, 0.6 of cpu_maximum_separation (50) left, centre and right (the
 * source picks lanes at random), box trucks taking liveries in spawn order.
 * Each engine class has its own set (mksc:present-in; Time Trial runs at
 * 100cc): the right lane drives at cc * 90 / 216 + 4.583 units a frame and
 * the others 1.667 slower, every lane slow at 50cc, and Extra drives the
 * course backwards. */
typedef struct Mk64Traffic {
    uint8_t course, count, model, liveries;
    uint16_t first;
    int16_t lane;
} Mk64Traffic;
static const Mk64Traffic traffic[] = {
    {10,7,BOX_TRUCK,3,0,30},
    {10,7,SCHOOL_BUS,1,75,30},
    {10,7,TANKER_TRUCK,1,50,30},
    {10,7,CAR,1,25,30},
};
/* fast and slow lanes per engine class, Q8 units a frame */
static const uint16_t traffic_speed[4][2] = {{747,747},{1280,853},{1387,960},{1493,1067}};

#define PATH_LIMIT 2048
#define ORBIT_NODES 32
/* generate_2d_path: a quadratic B-spline through the source points,
 * resampled every 20 units of arc, in Q8 integer arithmetic. */
static uint32_t path_2d(RiCtx *ctx,RiData data,uint32_t offset,int32_t (*out)[2],const char *where)
{
    int64_t src[512][2];uint32_t n=0;
    for (;;++n) {
        if (n==512)ri_fail(ctx,"%s: unterminated vehicle path",where);
        const uint8_t *p=ri_at(ctx,data,offset+n*8,8);
        if (mk64_be16(p)==0x8000u)break;
        src[n][0]=(int64_t)(int16_t)mk64_be16(p)*256;src[n][1]=(int64_t)(int16_t)mk64_be16(p+4)*256;
    }
    if (n<4)ri_fail(ctx,"%s: vehicle path too short",where);
    --n; /* GET_PATH_LENGTH counts to the terminator; the caller passes i - 1 */
    uint32_t count=0;int64_t run=0,last[2]={src[0][0],src[0][1]};
    for (uint32_t i=0;i<n;++i) {
        const int64_t *a=src[i],*b=src[(i+1)%n],*c=src[(i+2)%n];
        int64_t ab[2]={b[0]-a[0],b[1]-a[1]},bc[2]={c[0]-b[0],c[1]-b[1]};
        const int64_t span=(int64_t)portable_fixed_3d_sqrt((uint64_t)(ab[0]*ab[0]+ab[1]*ab[1]))+
                           (int64_t)portable_fixed_3d_sqrt((uint64_t)(bc[0]*bc[0]+bc[1]*bc[1]));
        const int64_t step=span?(838861+span/2)/span:65536; /* 0.05 / span, Q16 */
        for (int64_t j=0;j<=65536;j+=step<1?1:step) {
            const int64_t u=65536-j,w1=u*u/2,w2=u*j+(int64_t)32768*65536,w3=j*j/2;
            int64_t at[2];
            for (unsigned k=0;k<2;++k)at[k]=(w1*a[k]+w2*b[k]+w3*c[k])/((int64_t)65536*65536);
            const int64_t dx=at[0]-last[0],dz=at[1]-last[1];
            run+=(int64_t)portable_fixed_3d_sqrt((uint64_t)(dx*dx+dz*dz));
            last[0]=at[0];last[1]=at[1];
            if (run>20*256 || (i==0 && j==0)) {
                if (count==PATH_LIMIT)ri_fail(ctx,"%s: vehicle path too long",where);
                out[count][0]=(int32_t)(at[0]/256);out[count][1]=(int32_t)(at[1]/256);
                ++count;run=0;
            }
        }
    }
    return count;
}

/* Q16 metres between two engine points, as mesh_collision_index measures legs */
static int64_t span(const int32_t a[3],const int32_t b[3])
{
    int64_t d2=0;
    for (unsigned k=0;k<3;++k){const int64_t d=((int64_t)b[k]-a[k])>>4;d2+=d*d;}
    return (int64_t)portable_fixed_3d_sqrt((uint64_t)d2)<<4;
}

/* Vehicles on one resampled path at one speed share one RTHD closed route:
 * nodes evenly spread over the path from its first point (as many as a
 * route motion holds), each leg taking the ticks its arc takes at `speed`
 * source units a frame (Q8). A vehicle at path point `at` runs that route
 * `phase` ticks ahead, so the cars of a train keep their spacing. */
typedef struct VehicleRoute {
    const int32_t (*path)[3];
    uint32_t length, id, count;
    uint16_t speed;
    uint32_t node[PORTABLE_MESH_MOTION_POINT_LIMIT + 1];   /* path points; node[count] = length */
    uint32_t ticks[PORTABLE_MESH_MOTION_POINT_LIMIT];      /* as mesh_collision_index rebuilds them */
} VehicleRoute;

static void vehicle_route(MkdsModel *m,VehicleRoute *r)
{
    r->count=r->length/2<PORTABLE_MESH_MOTION_POINT_LIMIT?r->length/2:PORTABLE_MESH_MOTION_POINT_LIMIT;
    for (uint32_t i=0;i<=r->count;++i)r->node[i]=(uint32_t)((uint64_t)i*r->length/r->count);
    r->id=m->tables[MKDS_T_RTHD].count;
    const uint32_t first=m->tables[MKDS_T_RTND].count;
    const uint32_t header[8]={2,first,r->count,first,1,0,0,0};
    mkds_words(m,MKDS_T_RTHD,header,8);
    const int64_t per_tick=(int64_t)mk64_metres(r->speed)/256/FRAME_TICKS; /* Q16 metres; speed is a frame's */
    for (uint32_t i=0;i<r->count;++i) {
        const int32_t *a=r->path[r->node[i]],*b=r->path[r->node[i+1]%r->length];
        int64_t arc=0;
        for (uint32_t at=r->node[i];at<r->node[i+1];++at)arc+=span(r->path[at],r->path[(at+1)%r->length]);
        const int64_t distance=span(a,b);
        const int64_t ticks=per_tick>0?(arc+per_tick/2)/per_tick:0;
        int64_t rate=ticks>0?distance*262144/(ticks*4389):65536; /* metres/second, as portable_fixed_3d_dt */
        if (rate<1)rate=1;
        const int64_t back=(distance*262144+rate*4389/2)/(rate*4389);
        r->ticks[i]=back<1?1u:back>PORTABLE_MESH_MOTION_TICK_LIMIT?PORTABLE_MESH_MOTION_TICK_LIMIT:(uint32_t)back;
        const uint32_t edge=m->tables[MKDS_T_RTED].count,next=first+(i+1)%r->count;
        const uint32_t row[8]={(uint32_t)a[0],(uint32_t)a[1],(uint32_t)a[2],2u<<16,(uint32_t)rate,edge,1,0};
        mkds_words(m,MKDS_T_RTND,row,8);mkds_words(m,MKDS_T_RTED,&next,1);
    }
}

/* Ticks from the route's first node to path point `at`. */
static uint32_t route_phase(const VehicleRoute *r,uint32_t at)
{
    uint32_t phase=0,leg=0;
    while (r->node[leg+1]<=at) phase+=r->ticks[leg++];
    return phase+(uint32_t)((uint64_t)r->ticks[leg]*(at-r->node[leg])/(r->node[leg+1]-r->node[leg]));
}

/* Rest pose turned about world up so the model's forward (source +Z, engine
 * +Y) runs along `to` - `from`. */
static void facing(const int32_t from[3],const int32_t to[3],int32_t q[4])
{
    int64_t d[3]={(int64_t)to[0]-from[0],(int64_t)to[1]-from[1],0};
    int32_t f[3];
    if (!portable_fixed_3d_normalize(d,f)) { q[0]=q[1]=q[2]=0;q[3]=1<<30;return; }
    /* +X to (f.y, -f.x) is +Y to f: the half-way vector's components. */
    int32_t half[3];
    const int64_t halfway[3]={(int64_t)(1<<30)+f[1],-(int64_t)f[0],0};
    if (!portable_fixed_3d_normalize(halfway,half)) { half[0]=0;half[1]=1<<30; }
    q[0]=q[1]=0;q[2]=half[1];q[3]=half[0];
}

/* The mksc:hazard-reaction property for a non-bump reaction; returns the
 * number of properties written. */
static uint32_t hazard_reaction(MkdsModel *course,uint8_t reaction)
{
    if (reaction==REACT_BUMP)return 0;
    mkds_property(course,"mksc:hazard-reaction",MKDS_PROPERTY_STRING,mkds_string(course,reaction_names[reaction]),0);
    return 1;
}

/* Route-moving vehicles share one mesh per model. */
typedef struct Placer {
    MkdsModel *course;
    const Mk64Segments *segments;
    uint32_t index,*meshes,*hazards;
    uint32_t mesh[sizeof models/sizeof models[0]];
    VehicleRoute routes[16];
    uint32_t route_count;
    const VehicleRoute *last;  /* the last place()'s route and motion phase */
    uint32_t last_phase;
} Placer;
static uint32_t presence_property(MkdsModel *course,uint32_t mask);
/* presence: mksc:present-in mask, 0 for every race */
static void place(Placer *p,unsigned which,const int32_t (*path)[3],uint32_t length,uint32_t at,uint16_t speed,
                  uint32_t presence)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    const Mk64Model *model=&models[which];
    if (p->mesh[which]==MKDS_ABSENT) {
        if (*p->meshes==256)ri_fail(ctx,"%s: more than 256 scenery meshes",course->where);
        Draw d;memset(&d,0,sizeof d);
        char name[112];snprintf(name,sizeof name,"%s vehicle actor 0x%02X",course->where,(unsigned)model->actor);
        mkds_model_init(&d.m,ctx,name,0,NULL,0,0);
        d.segments=p->segments;d.textured=1;d.width=d.height=32;
        uint8_t nodes[2048];uint32_t node_count=0;const Mk64Part *rolling[14];
        for (unsigned part=0;part<14 && model->parts[part].list;++part) {
            for (unsigned k=0;k<3;++k)d.offset[k]=model->parts[part].at[k];
            const uint32_t before=d.m.tables[MKDS_T_VERT].count;
            draw_list(&d,model->parts[part].list,0);
            const uint32_t after=d.m.tables[MKDS_T_VERT].count;
            if (after>2048)ri_fail(ctx,"%s: more than 2048 vertices",name);
            if (model->parts[part].roll)rolling[node_count]=&model->parts[part];
            memset(nodes+before,model->parts[part].roll?(int)node_count:255,after-before);
            node_count+=model->parts[part].roll;
        }
        p->mesh[which]=MK64_OBJECT_MESH_ID(p->index,*p->meshes);
        write_mesh(&d,p->mesh[which],MK64_OBJECT_MATERIAL_ID(p->index,*p->meshes,0));
        ++*p->meshes;
        if (node_count) {
            /* One loop of whole degrees a frame, FRAME_TICKS ticks each; the
             * engine's x axis is the source x axis, and the y/z swap turns
             * wheelRot a into -a. */
            const uint32_t step=(uint32_t)(model->roll<0?-model->roll:model->roll);
            uint32_t frames=360;
            for (uint32_t g=step;g;) { const uint32_t r=frames%g; frames=g; g=r; }
            frames=360/frames*FRAME_TICKS;
            RiBuf *poses=ri_buf(ctx);
            for (uint32_t f=0;f<frames;++f)
                for (uint32_t n=0;n<node_count;++n) {
                    const int32_t a=(int32_t)(((int32_t)rolling[n]->phase*FRAME_TICKS+(int32_t)model->roll*(int32_t)f)*
                                              0x10000/(360*(int32_t)FRAME_TICKS));
                    int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)-a,&sine,&cosine);
                    const int64_t c=sine>>14,k=cosine>>14; /* Q16 */
                    const int64_t y=mk64_metres(rolling[n]->at[2]),z=mk64_metres(rolling[n]->at[1]);
                    const int32_t row[12]={65536,0,0,0, 0,(int32_t)k,(int32_t)-c,(int32_t)(y-((k*y-c*z)>>16)),
                                           0,(int32_t)c,(int32_t)k,(int32_t)(z-((c*y+k*z)>>16))};
                    for (unsigned i=0;i<12;++i)ri_u32(ctx,poses,(uint32_t)row[i]);
                }
            write_rig(&d,p->mesh[which],nodes,node_count,frames,poses,NULL,0);
        }
    }
    VehicleRoute *r=NULL;
    for (uint32_t i=0;i<p->route_count;++i)
        if (p->routes[i].path==path && p->routes[i].speed==speed)r=&p->routes[i];
    if (!r) {
        if (p->route_count==sizeof p->routes/sizeof p->routes[0])ri_fail(ctx,"%s: too many vehicle routes",course->where);
        r=&p->routes[p->route_count++];
        r->path=path;r->length=length;r->speed=speed;
        vehicle_route(course,r);
    }
    int32_t q[4];facing(path[0],path[r->node[1]],q);
    p->last=r;p->last_phase=route_phase(r,at);
    if (++*p->hazards>64)ri_fail(ctx,"%s: more than 64 collidable actors",course->where);
    const uint32_t first=course->tables[MKDS_T_PROP].count;
    mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"route"),0);
    mkds_property(course,"mksc:route-wait",MKDS_PROPERTY_INTEGER,0,0);
    mkds_property(course,"mksc:route-face",MKDS_PROPERTY_BOOLEAN,1,0);
    mkds_property(course,"mksc:motion-phase",MKDS_PROPERTY_INTEGER,p->last_phase,0);
    /* a box: across, along and up the vehicle, about its foot */
    int32_t half[3],radius=0;
    for (unsigned k=0;k<3;++k) { half[k]=mk64_metres(model->box[k])/2; if (half[k]>radius)radius=half[k]; }
    mkds_property(course,"mksc:hazard-radius",MKDS_PROPERTY_FIXED,(uint32_t)radius,0);
    mkds_property_vector(course,"mksc:hazard-box",half);
    uint32_t count=6+hazard_reaction(course,model->reaction);
    if (presence)count+=presence_property(course,presence);
    const uint32_t row[16]={0x4E360000u|model->actor,MKDS_KIND_MESH,p->mesh[which],
        (uint32_t)path[0][0],(uint32_t)path[0][1],(uint32_t)path[0][2],
        (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],model->scale,model->scale,model->scale,r->id,first,count};
    mkds_words(course,MKDS_T_OBJS,row,16);
}

/* Walkers (update_objects.c init_ktb_crab, code_8006E9C0.c): sprite objects
 * that pace between a start and a patrol point read from a ROM table of
 * s16 (startX, patrolX, startZ, patrolZ) rows, resting `wait` ticks at each
 * end, `lift` units above the floor; drawn by draw_2d_texture_at on the
 * 64x64 hedgehog quad (segment 13 0x60B0) facing the camera, here as two
 * crossed quads (moving objects are not billboards). */
typedef struct Mk64Walker {
    uint8_t course, count, actor, radius; /* actor: 0x80 + object family (no source actor type) */
    uint32_t spawns;          /* ROM address of the spawn rows */
    uint32_t texture, tlut;   /* CI8 64x64 frame 0 and its pal256 TLUT */
    uint32_t scale;           /* Q16 sizeScaling */
    uint16_t speed, lift;     /* source units a frame, Q8 */
    uint16_t wait;
    uint8_t reaction;
    uint32_t patrols;         /* 0: crab rows; else ROM Vec3s patrol points, the spawns (x, y, z, id) rows */
    uint16_t spread;          /* Q8 units a frame added per (index % 6) */
} Mk64Walker;
static const Mk64Walker walkers[] = {
    /* Koopa Troopa Beach crabs: gCrabSpawns, 0.15 scale, 0.8 units a frame, 60-frame rests */
    {6,10,0x80,5,0xE6B80u,0x0600D828,0x0600D628,9830,205,640,60,REACT_SPIN,0,0},
    /* Yoshi Valley hedgehogs (func_8008311C): gHedgehogSpawns and gHedgehogPatrolPoints,
     * 0.2 scale, 0.5 + 0.1 (i % 6) units a frame, 6 above the floor, 60-frame rests */
    {4,15,0x81,2,0xE6A14u,0x06014B08,0x06014908,13107,128,1536,60,REACT_SPIN,0xE6A8Cu,26},
};

static void walker_family(Placer *p,const Mk64Walker *w,Mk64Floor floor,void *context)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    if (*p->meshes==256)ri_fail(ctx,"%s: more than 256 scenery meshes",course->where);
    Draw d;memset(&d,0,sizeof d);
    char where[96];snprintf(where,sizeof where,"%s walker 0x%02X",course->where,(unsigned)w->actor);
    mkds_model_init(&d.m,ctx,where,0,NULL,0,0);
    d.segments=p->segments;d.textured=1;d.texture=w->texture;d.tlut=w->tlut;d.fmt=2;d.siz=1;
    d.width=d.height=64;d.alpha=16;
    const uint8_t *quad=resolve(&d,0x0D0060B0u,8*16);
    /* the hedgehog quad's outer corners; texture rows run top to bottom */
    const unsigned corner[4]={0,1,6,7};
    for (unsigned k=0;k<4;++k) {
        memcpy(d.vertex[k],quad+corner[k]*16,16);d.loaded[k]=1;
        const int top=(int16_t)mk64_be16(d.vertex[k]+2)>0,right=(int16_t)mk64_be16(d.vertex[k])>0;
        const uint16_t s=right?64*32:0,t=top?0:64*32;
        d.vertex[k][8]=(uint8_t)(s>>8);d.vertex[k][9]=(uint8_t)s;d.vertex[k][10]=(uint8_t)(t>>8);d.vertex[k][11]=(uint8_t)t;
    }
    for (unsigned k=0;k<4;++k) { /* the crossing copy: x becomes z */
        memcpy(d.vertex[4+k],d.vertex[k],16);d.loaded[4+k]=1;
        memcpy(d.vertex[4+k]+4,d.vertex[k],2);d.vertex[4+k][0]=d.vertex[4+k][1]=0;
    }
    draw_triangle(&d,0,2,4);draw_triangle(&d,0,4,6);draw_triangle(&d,8,10,12);draw_triangle(&d,8,12,14);
    const Mk64Flipbook *book=flipbook(p->index,w->actor);
    RiBuf *swaps=ri_buf(ctx);
    const uint32_t swap_count=book?flipbook_swaps(&d,book,w->texture,swaps):0u;
    const uint32_t mesh=MK64_OBJECT_MESH_ID(p->index,*p->meshes);
    write_mesh(&d,mesh,MK64_OBJECT_MATERIAL_ID(p->index,*p->meshes,0));
    if (swap_count)write_rig(&d,mesh,NULL,0,0,NULL,swaps,swap_count);
    ++*p->meshes;
    for (uint32_t i=0;i<w->count;++i) {
        const uint8_t *row=ri_at(ctx,ctx->rom,w->spawns+i*8,8);
        int32_t ends[2][2]={{(int16_t)mk64_be16(row),(int16_t)mk64_be16(row+4)},
                            {(int16_t)mk64_be16(row+2),(int16_t)mk64_be16(row+6)}};
        if (w->patrols) {
            const uint8_t *patrol=ri_at(ctx,ctx->rom,w->patrols+i*6,6);
            ends[0][1]=(int16_t)mk64_be16(row+4);
            ends[1][0]=(int16_t)mk64_be16(patrol);ends[1][1]=(int16_t)mk64_be16(patrol+4);
        }
        const int64_t rate=(int64_t)mk64_metres(w->speed+(i%6)*w->spread)/256*262144/(4389*FRAME_TICKS); /* metres/second */
        int32_t at[2][3];
        for (unsigned e=0;e<2;++e) {
            at[e][0]=mk64_metres(ends[e][0]);at[e][1]=mk64_metres(ends[e][1]);at[e][2]=mk64_metres(2000);
            if (!floor(context,at[e],&at[e][2]))ri_fail(ctx,"%s: no floor under walker %u",where,i);
            at[e][2]+=mk64_metres(w->lift)/256;
        }
        const uint32_t route=course->tables[MKDS_T_RTHD].count,first=course->tables[MKDS_T_RTND].count;
        const uint32_t header[8]={2,first,2,first,1,0,0,0};
        mkds_words(course,MKDS_T_RTHD,header,8);
        for (unsigned e=0;e<2;++e) {
            const uint32_t edge=course->tables[MKDS_T_RTED].count,next=first+(e+1)%2;
            const uint32_t node[8]={(uint32_t)at[e][0],(uint32_t)at[e][1],(uint32_t)at[e][2],2u<<16,
                                    (uint32_t)(rate>0?rate:1),edge,1,0};
            mkds_words(course,MKDS_T_RTND,node,8);mkds_words(course,MKDS_T_RTED,&next,1);
        }
        if (++*p->hazards>64)ri_fail(ctx,"%s: more than 64 collidable actors",course->where);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"route"),0);
        mkds_property(course,"mksc:route-wait",MKDS_PROPERTY_INTEGER,w->wait*FRAME_TICKS,0);
        mkds_property(course,"mksc:hazard-radius",MKDS_PROPERTY_FIXED,(uint32_t)mk64_metres(w->radius),0);
        const uint32_t count=3+hazard_reaction(course,w->reaction);
        const uint32_t obj[16]={0x4E360000u|w->actor,MKDS_KIND_MESH,mesh,(uint32_t)at[0][0],(uint32_t)at[0][1],
            (uint32_t)at[0][2],0,0,0,1u<<30,w->scale,w->scale,w->scale,route,props,count};
        mkds_words(course,MKDS_T_OBJS,obj,16);
    }
}

/* Orbiters (yoshi_egg/update.inc.c): a model circling `radius` units
 * around `center` (spawn_course_actors' position plus init_actor's 70 on z),
 * `step` angle units a frame, on the floor, while it turns `spin` whole
 * degrees a frame about its own up axis (eggRot) through a looping RIG1
 * clip; collidable like the actor's boundingBoxSize. */
typedef struct Mk64Orbiter {
    uint8_t course, actor, radius;  /* radius: hazard sphere */
    uint32_t list;
    int16_t center[3];
    uint16_t path_radius, step;
    int8_t spin;
    uint8_t reaction;
} Mk64Orbiter;
static const Mk64Orbiter orbiters[] = {
    /* Yoshi Valley egg: dl_16D70 (the near model), 0.5 degrees a frame, eggRot -3 degrees a frame */
    {4,0x09,20,0x06016D70,{-2300,0,704},70,0x5B,-3,REACT_TUMBLE},
};

static void orbiter_family(Placer *p,const Mk64Orbiter *o,Mk64Floor floor,void *context)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    if (*p->meshes==256)ri_fail(ctx,"%s: more than 256 scenery meshes",course->where);
    Draw d;memset(&d,0,sizeof d);
    char where[96];snprintf(where,sizeof where,"%s orbiter 0x%02X",course->where,(unsigned)o->actor);
    mkds_model_init(&d.m,ctx,where,0,NULL,0,0);
    d.segments=p->segments;d.textured=1;d.width=d.height=32;
    draw_list(&d,o->list,0);
    const uint32_t mesh=MK64_OBJECT_MESH_ID(p->index,*p->meshes);
    write_mesh(&d,mesh,MK64_OBJECT_MATERIAL_ID(p->index,*p->meshes,0));
    ++*p->meshes;
    if (o->spin) {
        /* Source yaw about up turns the other way in engine axes. */
        const uint32_t step=(uint32_t)(o->spin<0?-o->spin:o->spin);
        uint32_t frames=360;
        for (uint32_t g=step;g;) { const uint32_t r=frames%g; frames=g; g=r; }
        frames=360/frames*FRAME_TICKS;
        RiBuf *poses=ri_buf(ctx);
        for (uint32_t f=0;f<frames;++f) {
            int32_t sine,cosine;
            portable_fixed_3d_sincos((uint16_t)(-(int32_t)o->spin*(int32_t)f*0x10000/(360*(int32_t)FRAME_TICKS)),&sine,&cosine);
            const int32_t c=cosine>>14,s=sine>>14; /* Q16 */
            const int32_t row[12]={c,-s,0,0, s,c,0,0, 0,0,65536,0};
            for (unsigned i=0;i<12;++i)ri_u32(ctx,poses,(uint32_t)row[i]);
        }
        uint8_t *nodes=ri_buf_zero(ctx,d.m.tables[MKDS_T_VERT].count+1u)->data;
        write_rig(&d,mesh,nodes,1,frames,poses,NULL,0);
    }
    /* pos = center + radius (sin r, cos r) in source x/z; the route walks it
     * as a closed polygon at the arc speed. */
    const uint32_t route=course->tables[MKDS_T_RTHD].count,first=course->tables[MKDS_T_RTND].count;
    const uint32_t header[8]={2,first,ORBIT_NODES,first,1,0,0,0};
    mkds_words(course,MKDS_T_RTHD,header,8);
    /* arc length a frame in Q8 source units: radius * 2pi * step / 65536 */
    const int64_t arc=(int64_t)o->path_radius*o->step*256*710/113/32768;
    const int64_t rate=(int64_t)mk64_metres((int32_t)arc)/256*262144/(4389*FRAME_TICKS); /* metres/second, as walkers */
    int32_t start[3]={0};
    for (uint32_t n=0;n<ORBIT_NODES;++n) {
        int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(n*65536u/ORBIT_NODES),&sine,&cosine);
        int32_t at[3]={mk64_metres(o->center[0]+(int32_t)(((int64_t)sine*o->path_radius)>>30)),
                       mk64_metres(o->center[2]+(int32_t)(((int64_t)cosine*o->path_radius)>>30)),mk64_metres(2000)};
        if (!floor(context,at,&at[2]))ri_fail(ctx,"%s: no floor under orbit point %u",where,n);
        at[2]+=mk64_metres(o->center[1]);
        if (!n)memcpy(start,at,sizeof at);
        const uint32_t edge=course->tables[MKDS_T_RTED].count,next=first+(n+1)%ORBIT_NODES;
        const uint32_t node[8]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],2u<<16,(uint32_t)(rate>0?rate:1),edge,1,0};
        mkds_words(course,MKDS_T_RTND,node,8);mkds_words(course,MKDS_T_RTED,&next,1);
    }
    if (++*p->hazards>64)ri_fail(ctx,"%s: more than 64 collidable actors",course->where);
    const uint32_t props=course->tables[MKDS_T_PROP].count;
    mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"route"),0);
    mkds_property(course,"mksc:route-wait",MKDS_PROPERTY_INTEGER,0,0);
    mkds_property(course,"mksc:hazard-radius",MKDS_PROPERTY_FIXED,(uint32_t)mk64_metres(o->radius),0);
    const uint32_t count=3+hazard_reaction(course,o->reaction);
    const uint32_t obj[16]={0x4E360000u|o->actor,MKDS_KIND_MESH,mesh,(uint32_t)start[0],(uint32_t)start[1],
        (uint32_t)start[2],0,0,0,1u<<30,65536,65536,65536,route,props,count};
    mkds_words(course,MKDS_T_OBJS,obj,16);
}

/* A closed route of source-unit legs: leg k runs from point k to point
 * k+1 at rates[k] source units a tick (Q8), or over ticks[k] ticks when
 * set; a zero rate rests `rest[k]` ticks instead, as a one-centimetre leg.
 * Returns the RTHD index. */
#define LOOP_POINTS PORTABLE_MESH_MOTION_POINT_LIMIT
typedef struct Mk64Loop {
    int32_t at[LOOP_POINTS][3]; /* engine Q16 */
    uint16_t rate[LOOP_POINTS], rest[LOOP_POINTS], ticks[LOOP_POINTS];
    uint32_t count;
} Mk64Loop;
static void loop_point(MkdsModel *course,Mk64Loop *l,const int32_t at[3],uint16_t rate,uint16_t rest)
{
    if (l->count==LOOP_POINTS)ri_fail(course->ctx,"%s: actor route longer than %u points",course->where,LOOP_POINTS);
    memcpy(l->at[l->count],at,sizeof l->at[0]);l->rate[l->count]=rate;l->rest[l->count]=rest;l->ticks[l->count]=0;
    ++l->count;
}
/* A point whose leg to the next takes `ticks` ticks. */
static void loop_timed(MkdsModel *course,Mk64Loop *l,const int32_t at[3],uint32_t ticks)
{
    if (!ticks || ticks>PORTABLE_MESH_MOTION_TICK_LIMIT)ri_fail(course->ctx,"%s: actor leg of %u ticks",course->where,(unsigned)ticks);
    loop_point(course,l,at,0,0);l->ticks[l->count-1]=(uint16_t)ticks;
}
/* metres/second (Q16) that covers `distance` (Q16 metres) in `ticks` */
static int64_t loop_rate(int64_t distance,uint32_t ticks)
{
    const int64_t rate=(distance*262144+(int64_t)ticks*4389/2)/((int64_t)ticks*4389);
    return rate>0?rate:1;
}
static uint32_t loop_route(MkdsModel *course,const Mk64Loop *l)
{
    /* Each point is one node, or two (the second 1 cm up) when it rests
     * or stays put for a timed leg. */
    uint8_t stay[LOOP_POINTS];
    uint32_t nodes=0;
    for (uint32_t k=0;k<l->count;++k) {
        stay[k]=l->rest[k] || (l->ticks[k] && span(l->at[k],l->at[(k+1)%l->count])==0);
        nodes+=stay[k]?2u:1u;
    }
    if (nodes<2 || nodes>PORTABLE_MESH_MOTION_POINT_LIMIT)
        ri_fail(course->ctx,"%s: actor route of %u nodes",course->where,(unsigned)nodes);
    const uint32_t route=course->tables[MKDS_T_RTHD].count,first=course->tables[MKDS_T_RTND].count;
    const uint32_t header[8]={2,first,nodes,first,1,0,0,0};
    mkds_words(course,MKDS_T_RTHD,header,8);
    uint32_t written=0;
    for (uint32_t k=0;k<l->count;++k) {
        const int32_t *to=l->at[(k+1)%l->count];
        for (unsigned half=0;half<(stay[k]?2u:1u);++half) {
            int32_t at[3];memcpy(at,l->at[k],sizeof at);
            if (half)at[2]+=655;
            int64_t rate;
            if (stay[k] && !half) {
                const uint32_t hold=l->rest[k]?l->rest[k]:(l->ticks[k]>1?l->ticks[k]-1u:1u);
                rate=loop_rate(655,hold);                                     /* 1 cm over the stay */
            } else if (l->ticks[k]) {
                const uint32_t left=stay[k] && !l->rest[k]?1u:l->ticks[k];
                rate=loop_rate(span(at,to),left);
            } else rate=(int64_t)mk64_metres(l->rate[k])/256*262144/4389;      /* metres/second */
            const uint32_t edge=course->tables[MKDS_T_RTED].count,next=first+(written+1)%nodes;
            const uint32_t node[8]={(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],2u<<16,(uint32_t)(rate>0?rate:1),edge,1,0};
            mkds_words(course,MKDS_T_RTND,node,8);mkds_words(course,MKDS_T_RTED,&next,1);
            ++written;
        }
    }
    return route;
}

/* Bowser's Castle Thwomps (update_objects.c func_80081210): ThwompSpawn rows
 * (startX, startZ, behaviour 1..6, variant) from the 150cc list Time Trial
 * uses, drawn with dl_thwomp after render_object_thwomps_model loads the
 * 16x64 CI8 face and its TLUT; boundingBoxSize 12, and contact squishes.
 * Behaviours, as tick-driven loops (the source waits for a kart to come
 * near; here it slams on a fixed cycle):
 *   1, 4 hover 30 up, rise to 45 at 1.5 a frame, drop at 2, rest, climb back
 *        at 0.5 (func_8007E63C); 4 staggers by variant (timer 2/60/120/180);
 *   2    patrol a 200 x 100 rectangle, 4 a frame along x and 2 along z,
 *        turning 16 ticks and slamming at each corner (func_8007EFBC/F280);
 *   3    run ahead along x at 5 a frame for 160 ticks weaving 40 either side
 *        on z, slam, and come back (func_8007FB48, func_8007FEA4);
 *   5    slide 250 along -z and back at 1 or 1.5 a frame, 70 up (func_8008078C);
 *   6    stand 1.5 times larger, 10 up (func_800802C0). */
typedef struct Mk64Thwomps {
    uint8_t course, count;
    uint32_t spawns;          /* ROM address of the ThwompSpawn rows */
    uint32_t list, face, tlut;
    uint8_t radius;
} Mk64Thwomps;
static const Mk64Thwomps thwomps[] = {
    {2,12,0xE64ECu,0x06009088,0x06007138,0x06006F38,12},
};

static void thwomp_point(const int16_t origin[3],int32_t dx,int32_t dy,int32_t dz,int32_t out[3])
{
    out[0]=mk64_metres(origin[0]+dx);out[1]=mk64_metres(origin[2]+dz);out[2]=mk64_metres(origin[1]+dy);
}
/* Rest `turn` frames hovering, rise 15 at 1.5 a frame, drop to the floor at 2,
 * rest 56 ticks (func_80072AAC's settle), climb back at 0.5 and go on at
 * `then` (Q8 units a frame). */
static void thwomp_slam(MkdsModel *course,Mk64Loop *l,const int16_t o[3],int32_t dx,int32_t dz,int32_t hover,
                        uint16_t turn,uint16_t then)
{
    int32_t at[3];
    thwomp_point(o,dx,hover,dz,at);loop_point(course,l,at,384,turn);
    thwomp_point(o,dx,hover+15,dz,at);loop_point(course,l,at,512,0);
    thwomp_point(o,dx,0,dz,at);loop_point(course,l,at,128,56);
    thwomp_point(o,dx,hover,dz,at);loop_point(course,l,at,then,0);
}

static void thwomp_family(Placer *p,const Mk64Thwomps *t)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    if (*p->meshes==256)ri_fail(ctx,"%s: more than 256 scenery meshes",course->where);
    Draw d;memset(&d,0,sizeof d);
    char where[96];snprintf(where,sizeof where,"%s Thwomp",course->where);
    mkds_model_init(&d.m,ctx,where,0,NULL,0,0);
    d.segments=p->segments;d.textured=1;d.texture=t->face;d.tlut=t->tlut;d.fmt=2;d.siz=1;d.width=16;d.height=64;
    draw_list(&d,t->list,0);
    const uint32_t mesh=MK64_OBJECT_MESH_ID(p->index,*p->meshes);
    write_mesh(&d,mesh,MK64_OBJECT_MATERIAL_ID(p->index,*p->meshes,0));
    ++*p->meshes;
    for (uint32_t i=0;i<t->count;++i) {
        const uint8_t *row=ri_at(ctx,ctx->rom,t->spawns+i*8,8);
        const int16_t origin[3]={(int16_t)mk64_be16(row),0,(int16_t)mk64_be16(row+2)};
        const uint32_t behaviour=mk64_be16(row+4),variant=mk64_be16(row+6);
        Mk64Loop l;memset(&l,0,sizeof l);
        int32_t at[3];uint32_t phase=0,scale=65536;uint16_t yaw=0xC000;
        switch (behaviour) {
        case 1: case 4:
            thwomp_slam(course,&l,origin,0,0,30,60,384);
            phase=behaviour==4?(variant?variant*60u:2u):variant*60u;
            break;
        case 2: {
            const int32_t sx=variant?-200:200,sz=variant?100:-100;
            const int32_t corner[4][2]={{sx,0},{sx,sz},{0,sz},{0,0}};
            for (unsigned k=0;k<4;++k)
                thwomp_slam(course,&l,origin,corner[k][0],corner[k][1],30,16,(uint16_t)(k%2?1024:512));
            break;
        }
        case 3:
            for (int32_t step=0;step<4;++step) {
                thwomp_point(origin,step*200,15,step%2?(step==1?40:-40):0,at);loop_point(course,&l,at,1280,0);
            }
            thwomp_slam(course,&l,origin,800,0,15,0,1280);
            phase=variant*80u;
            break;
        case 5: {
            const int16_t high[3]={origin[0],70,origin[2]};
            const uint16_t rate=variant?384:256;
            thwomp_point(high,0,0,0,at);loop_point(course,&l,at,rate,0);
            thwomp_point(high,0,0,-250,at);loop_point(course,&l,at,rate,0);
            yaw=0x4000;
            break;
        }
        case 6:
            thwomp_point(origin,0,10,0,at);loop_point(course,&l,at,0,0);
            scale=98304;
            break;
        default:
            ri_fail(ctx,"%s: Thwomp %u has unknown behaviour %u",where,(unsigned)i,(unsigned)behaviour);
        }
        /* the rates and rests above are a frame's: FRAME_TICKS ticks */
        for (uint32_t k=0;k<l.count;++k) { l.rate[k]=(uint16_t)(l.rate[k]/FRAME_TICKS); l.rest[k]=(uint16_t)(l.rest[k]*FRAME_TICKS); }
        phase*=FRAME_TICKS;
        const uint32_t route=l.count>1?loop_route(course,&l):MKDS_ABSENT;
        if (++*p->hazards>64)ri_fail(ctx,"%s: more than 64 collidable actors",course->where);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        uint32_t count=0;
        if (route!=MKDS_ABSENT) {
            mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"route"),0);
            mkds_property(course,"mksc:route-wait",MKDS_PROPERTY_INTEGER,0,0);
            mkds_property(course,"mksc:motion-phase",MKDS_PROPERTY_INTEGER,phase,0);
            count=3;
        }
        mkds_property(course,"mksc:hazard-radius",MKDS_PROPERTY_FIXED,
                      (uint32_t)((int64_t)mk64_metres(t->radius)*scale>>16),0);
        count+=1+hazard_reaction(course,REACT_SQUASH);
        int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(-(int32_t)yaw/2),&sine,&cosine);
        const uint32_t obj[16]={0x4E3600C0u|behaviour,MKDS_KIND_MESH,mesh,(uint32_t)l.at[0][0],(uint32_t)l.at[0][1],
            (uint32_t)l.at[0][2],0,0,(uint32_t)sine,(uint32_t)cosine,scale,scale,scale,route,props,count};
        mkds_words(course,MKDS_T_OBJS,obj,16);
    }
}

/* ---- Shared pieces of the object families below ---- */


/* Deterministic stand-in for random_int: a seeded LCG, so placements the
 * source randomises at spawn are fixed per course. */
typedef struct Mk64Random { uint32_t state; } Mk64Random;
static uint32_t random_below(Mk64Random *r,uint32_t n)
{
    r->state=r->state*1103515245u+12345u;
    return n?((r->state>>16)&0x7FFFu)%n:0u;
}

/* RIG1 poses are row-major 3x4 Q16 transforms in engine axes. A source-space
 * affine (Q30 rotation rows, Q16 source-unit translation) maps to engine axes
 * by the y/z swap and to metres by 5/88. */
static void pose_from_source(const int64_t a[12],int32_t out[12])
{
    static const unsigned axis[3]={0,2,1};
    for (unsigned r=0;r<3;++r) {
        for (unsigned c=0;c<3;++c)out[r*4+c]=(int32_t)(a[axis[r]*3+axis[c]]>>14);
        out[r*4+3]=(int32_t)(a[9+axis[r]]*5/88);
    }
}
static void pose_put(RiCtx *ctx,RiBuf *poses,const int32_t m[12])
{
    for (unsigned i=0;i<12;++i)ri_u32(ctx,poses,(uint32_t)m[i]);
}
/* Uniform scale (Q16) about an engine point (Q16 metres); 0 hides. */
static void pose_scale(RiCtx *ctx,RiBuf *poses,int32_t scale,const int32_t pivot[3])
{
    int32_t m[12]={0};
    for (unsigned r=0;r<3;++r) {
        m[r*4+r]=scale;
        m[r*4+3]=(int32_t)(pivot[r]-((int64_t)pivot[r]*scale>>16));
    }
    pose_put(ctx,poses,m);
}
/* Rotation by `angle` (0x10000 a turn) about the engine axis `axis` through
 * an engine point, times a uniform scale. */
static void pose_turn(RiCtx *ctx,RiBuf *poses,unsigned axis,uint16_t angle,int32_t scale,const int32_t pivot[3])
{
    int32_t sine,cosine;portable_fixed_3d_sincos(angle,&sine,&cosine);
    const int64_t c=(int64_t)(cosine>>14)*scale>>16,s=(int64_t)(sine>>14)*scale>>16;
    int64_t r[9]={scale,0,0,0,scale,0,0,0,scale};
    const unsigned i=(axis+1)%3,j=(axis+2)%3;
    r[i*3+i]=c;r[i*3+j]=-s;r[j*3+i]=s;r[j*3+j]=c;
    int32_t m[12];
    for (unsigned row=0;row<3;++row) {
        int64_t moved=0;
        for (unsigned col=0;col<3;++col) { m[row*4+col]=(int32_t)r[row*3+col]; moved+=r[row*3+col]*pivot[col]; }
        m[row*4+3]=(int32_t)(pivot[row]-(moved>>16));
    }
    pose_put(ctx,poses,m);
}

/* An OBJS row: actor tag, mesh, engine position, source yaw (0x10000 a
 * turn, about up), uniform Q16 scale, route (or ABSENT) and properties. */
static void object_row(MkdsModel *course,uint32_t actor,uint32_t mesh,const int32_t at[3],uint16_t yaw,uint32_t scale,
                       uint32_t route,uint32_t props,uint32_t count)
{
    /* Source yaw about up turns the other way in engine axes. */
    int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(-(int32_t)yaw/2),&sine,&cosine);
    const uint32_t row[16]={0x4E360000u|actor,MKDS_KIND_MESH,mesh,(uint32_t)at[0],(uint32_t)at[1],(uint32_t)at[2],
        0,0,(uint32_t)sine,(uint32_t)cosine,scale,scale,scale,route,props,count};
    mkds_words(course,MKDS_T_OBJS,row,16);
}
/* Route motion properties; returns their count. */
static uint32_t route_properties(MkdsModel *course,uint32_t phase,int face)
{
    mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"route"),0);
    mkds_property(course,"mksc:route-wait",MKDS_PROPERTY_INTEGER,0,0);
    mkds_property(course,"mksc:motion-phase",MKDS_PROPERTY_INTEGER,phase,0);
    if (!face)return 3;
    mkds_property(course,"mksc:route-face",MKDS_PROPERTY_BOOLEAN,1,0);
    return 4;
}
/* A collidable sphere of `radius` source units; returns the property count. */
static uint32_t hazard_properties(Placer *p,uint32_t radius,uint8_t reaction)
{
    MkdsModel *course=p->course;
    if (++*p->hazards>64)ri_fail(course->ctx,"%s: more than 64 collidable actors",course->where);
    mkds_property(course,"mksc:hazard-radius",MKDS_PROPERTY_FIXED,(uint32_t)mk64_metres((int32_t)radius),0);
    return 1+hazard_reaction(course,reaction);
}
/* mksc:present-in: bit mode slot * 4 + class (GP 0, TT 1, Quick Run 2, VS 3, battle 4). */
#define PRESENT_ALL_CLASSES(slot) (15u << ((slot) * 4u))
#define PRESENT_NOT_TIME_TRIAL (0xFFFFFu & ~PRESENT_ALL_CLASSES(1))
/* one engine class in Grand Prix, Quick Run and VS */
#define PRESENT_CLASS(cc) ((1u<<(cc)) | (1u<<(8u+(cc))) | (1u<<(12u+(cc))))
static uint32_t presence_property(MkdsModel *course,uint32_t mask)
{
    mkds_property(course,"mksc:present-in",MKDS_PROPERTY_INTEGER,mask,0);
    return 1;
}

/* A new object mesh slot and its Draw over the course segments. */
static void family_draw(Placer *p,Draw *d,const char *what)
{
    MkdsModel *course=p->course;
    if (*p->meshes==256)ri_fail(course->ctx,"%s: more than 256 scenery meshes",course->where);
    memset(d,0,sizeof *d);
    char where[112];snprintf(where,sizeof where,"%s %s",course->where,what);
    mkds_model_init(&d->m,course->ctx,where,0,NULL,0,0);
    d->segments=p->segments;d->textured=1;d->width=d->height=32;
}
static uint32_t family_mesh(Placer *p,Draw *d)
{
    const uint32_t mesh=MK64_OBJECT_MESH_ID(p->index,*p->meshes);
    write_mesh(d,mesh,MK64_OBJECT_MATERIAL_ID(p->index,*p->meshes,0));
    ++*p->meshes;
    return mesh;
}

/* A sprite (draw_2d_texture_at and friends): a w x h source-unit quad of the
 * bound texture in the source x/y plane, centred at (x, y), `depth` toward
 * the viewer, the image upright. Returns the vertex count before it. */
static uint32_t sprite_quad(Draw *d,int32_t w,int32_t h,int32_t x,int32_t y,int32_t depth)
{
    const uint32_t before=d->m.tables[MKDS_T_VERT].count;
    const int32_t sw=(int32_t)(d->width<<(d->cms&1))*32,th=(int32_t)(d->height<<(d->cmt&1))*32;
    for (unsigned k=0;k<4;++k) {
        uint8_t *v=d->vertex[k];memset(v,0,16);
        const int right=k&1,top=k<2;
        const int16_t vx=(int16_t)(x+(right?w-w/2:-w/2)),vy=(int16_t)(y+(top?h-h/2:-h/2)),vz=(int16_t)-depth;
        const uint16_t s=(uint16_t)(right?sw:0),t=(uint16_t)(top?0:th);
        v[0]=(uint8_t)((uint16_t)vx>>8);v[1]=(uint8_t)vx;v[2]=(uint8_t)((uint16_t)vy>>8);v[3]=(uint8_t)vy;
        v[4]=(uint8_t)((uint16_t)vz>>8);v[5]=(uint8_t)vz;
        v[8]=(uint8_t)(s>>8);v[9]=(uint8_t)s;v[10]=(uint8_t)(t>>8);v[11]=(uint8_t)t;
        memset(v+12,255,4);d->loaded[k]=1;
    }
    draw_triangle(d,0,4,2);draw_triangle(d,2,4,6);
    return before;
}
/* Bind a CI texture block (fmt 2) or another format for sprite_quad. */
static void sprite_texture(Draw *d,uint32_t fmt,uint32_t siz,uint32_t width,uint32_t height,uint32_t texture,uint32_t tlut,
                           uint32_t cms,uint32_t alpha)
{
    d->textured=1;d->fmt=fmt;d->siz=siz;d->width=width;d->height=height;d->texture=texture;d->tlut=tlut;
    d->palette=0;d->cms=cms;d->cmt=0;d->alpha=alpha;
}

/* Armatures (animation.c render_armature): a stream of s32 words, entries
 * {type, size words, ...}: 0 a limb {0, 7, 0, Gfx*, x, y, z} pushed as a
 * child of the matrix stack's top after popping the previous limb (unless
 * a 1 came first), 2 pops, 3 ends. An Animation {u32, u32, s16 length,
 * u16, s16 *angles, specs *} gives per channel {u16 limit, u16 index}:
 * the value at frame t is angles[index + (t < limit ? t : 0)]; channel 0 is
 * the root offset added to the first limb, then one per limb, its x, y and
 * z rotations (mtxf_translate_rotate2: rotate about x, then y, then z). */
#define LIMB_LIMIT 32
typedef struct Mk64Armature {
    uint32_t count;
    struct { uint8_t type; uint32_t list; int32_t at[3]; } entry[LIMB_LIMIT*2];
} Mk64Armature;
static void armature_read(Draw *d,uint32_t skeleton,Mk64Armature *a)
{
    memset(a,0,sizeof *a);
    for (uint32_t at=skeleton;;) {
        if (a->count==LIMB_LIMIT*2)ri_fail(d->m.ctx,"%s: armature longer than %u entries",d->m.where,LIMB_LIMIT*2);
        const uint8_t *w=resolve(d,at,8);
        const uint32_t type=mk64_be32(w),size=mk64_be32(w+4);
        if (type>3 || size<2 || size>7 || (type==0 && size!=7))
            ri_fail(d->m.ctx,"%s: armature entry %u {%u, %u}",d->m.where,(unsigned)a->count,(unsigned)type,(unsigned)size);
        a->entry[a->count].type=(uint8_t)type;
        if (!type) {
            const uint8_t *limb=resolve(d,at,28);
            a->entry[a->count].list=mk64_be32(limb+12);
            for (unsigned k=0;k<3;++k)a->entry[a->count].at[k]=(int32_t)mk64_be32(limb+16+k*4);
        }
        ++a->count;
        if (type==3)return;
        at+=size*4;
    }
}
/* Animation frames (length) of an Animation. */
static uint32_t animation_length(Draw *d,uint32_t animation)
{
    const uint32_t length=mk64_be16(resolve(d,animation,20)+8);
    if (!length || length>512)ri_fail(d->m.ctx,"%s: animation of %u frames",d->m.where,(unsigned)length);
    return length;
}
/* Each limb's matrix (source space, Q30 rotation rows, Q16 translation) at
 * `frame` of `animation`; returns the limb count. */
static uint32_t armature_pose(Draw *d,const Mk64Armature *a,uint32_t animation,uint32_t frame,int64_t (*out)[12])
{
    const uint8_t *header=resolve(d,animation,20);
    const uint32_t angles=mk64_be32(header+12),specs=mk64_be32(header+16);
    int64_t stack[LIMB_LIMIT+1][12];
    memset(stack[0],0,sizeof stack[0]);stack[0][0]=stack[0][4]=stack[0][8]=(int64_t)1<<30;
    uint32_t depth=1,limbs=0,channel=0;int keep=0;
    for (uint32_t e=0;e<a->count;++e) {
        const uint8_t type=a->entry[e].type;
        if (type==3)break;
        if (type==1) { keep=1; continue; }
        if (type==2 || !keep) {
            if (depth<2)ri_fail(d->m.ctx,"%s: armature pops its base",d->m.where);
            --depth;
            if (type==2)continue;
        }
        keep=0;
        if (limbs==LIMB_LIMIT || depth>LIMB_LIMIT)ri_fail(d->m.ctx,"%s: armature deeper than %u",d->m.where,LIMB_LIMIT);
        int32_t value[2][3];
        for (unsigned set=0;set<(channel?1u:2u);++set) {
            const uint8_t *spec=resolve(d,specs+(channel+set)*12,12);
            for (unsigned k=0;k<3;++k) {
                const uint32_t limit=mk64_be16(spec+k*4),index=mk64_be16(spec+k*4+2);
                value[set][k]=mk64_i16(resolve(d,angles+(index+(frame<limit?frame:0u))*2,2));
            }
        }
        int64_t pos[3];
        for (unsigned k=0;k<3;++k)pos[k]=(int64_t)a->entry[e].at[k]+(channel?0:value[0][k]);
        const int32_t *angle=value[channel?0:1];
        channel+=channel?1u:2u;
        int32_t sx,cx,sy,cy,sz,cz;
        portable_fixed_3d_sincos((uint16_t)angle[0],&sx,&cx);
        portable_fixed_3d_sincos((uint16_t)angle[1],&sy,&cy);
        portable_fixed_3d_sincos((uint16_t)angle[2],&sz,&cz);
        #define Q(a,b) (((int64_t)(a)*(b))>>30)
        /* column form of mtxf_translate_rotate2's row-vector matrix */
        const int64_t m[12]={Q(cy,cz),Q(Q(sx,sy),cz)-Q(cx,sz),Q(Q(cx,sy),cz)+Q(sx,sz),
                             Q(cy,sz),Q(Q(sx,sy),sz)+Q(cx,cz),Q(Q(cx,sy),sz)-Q(sx,cz),
                             -(int64_t)sy,Q(sx,cy),Q(cx,cy),pos[0]<<16,pos[1]<<16,pos[2]<<16};
        #undef Q
        const int64_t *parent=stack[depth-1];
        int64_t *child=stack[depth];
        for (unsigned r=0;r<3;++r) {
            for (unsigned c=0;c<3;++c)
                child[r*3+c]=(parent[r*3]*m[c]+parent[r*3+1]*m[3+c]+parent[r*3+2]*m[6+c])>>30;
            child[9+r]=((parent[r*3]*m[9]+parent[r*3+1]*m[10]+parent[r*3+2]*m[11])>>30)+parent[9+r];
        }
        memcpy(out[limbs++],child,sizeof stack[0]);
        ++depth;
    }
    return limbs;
}
/* rest -> now, both source-space limb matrices, as an engine RIG1 pose */
static void limb_pose(RiCtx *ctx,RiBuf *poses,const int64_t now[12],const int64_t rest[12])
{
    int64_t m[12];
    for (unsigned r=0;r<3;++r)
        for (unsigned c=0;c<3;++c) /* now * rest^T */
            m[r*3+c]=(now[r*3]*rest[c*3]+now[r*3+1]*rest[c*3+1]+now[r*3+2]*rest[c*3+2])>>30;
    for (unsigned r=0;r<3;++r)
        m[9+r]=now[9+r]-((m[r*3]*rest[9]+m[r*3+1]*rest[10]+m[r*3+2]*rest[11])>>30);
    int32_t out[12];pose_from_source(m,out);pose_put(ctx,poses,out);
}
/* One frame of an armature clip: animation index into the list, frame. */
typedef struct Mk64ArmatureFrame { uint8_t animation; uint8_t frame; } Mk64ArmatureFrame;
/* An armature mesh drawn at schedule[0] whose looping clip shows
 * schedule[t] at tick t; `setup` lists run first (texture state). */
static uint32_t armature_mesh(Placer *p,const char *what,uint32_t skeleton,uint32_t animations,
                              const Mk64ArmatureFrame *schedule,uint32_t ticks,const uint32_t *setup)
{
    RiCtx *ctx=p->course->ctx;
    /* limb lists set their own texture state; the setup lists may enable one */
    Draw d;family_draw(p,&d,what);d.textured=0;
    Mk64Armature a;armature_read(&d,skeleton,&a);
    uint32_t animation[8];
    for (unsigned k=0;k<8;++k) {
        animation[k]=mk64_be32(resolve(&d,animations+k*4,4));
        if (animation[k]>>24!=6)animation[k]=0;
    }
    for (uint32_t t=0;t<ticks;++t)
        if (schedule[t].animation>=8 || !animation[schedule[t].animation] ||
            schedule[t].frame>=animation_length(&d,animation[schedule[t].animation]))
            ri_fail(ctx,"%s: armature frame %u/%u is not in the animation list",d.m.where,
                    (unsigned)schedule[t].animation,(unsigned)schedule[t].frame);
    int64_t (*rest)[12]=(int64_t (*)[12])(void *)ri_buf_zero(ctx,2*LIMB_LIMIT*sizeof *rest)->data,(*now)[12]=rest+LIMB_LIMIT;
    const uint32_t limbs=armature_pose(&d,&a,animation[schedule[0].animation],schedule[0].frame,rest);
    for (unsigned k=0;setup && setup[k];++k)draw_list(&d,setup[k],0);
    uint8_t nodes[2048];uint32_t node_of[LIMB_LIMIT],node_count=0;
    for (uint32_t e=0,limb=0;e<a.count;++e) {
        if (a.entry[e].type)continue;
        node_of[limb]=255;
        if (a.entry[e].list) {
            const uint32_t before=d.m.tables[MKDS_T_VERT].count;
            d.transformed=1;memcpy(d.xf,rest[limb],sizeof d.xf);
            draw_list(&d,a.entry[e].list,0);
            const uint32_t after=d.m.tables[MKDS_T_VERT].count;
            if (after>2048)ri_fail(ctx,"%s: more than 2048 vertices",d.m.where);
            node_of[limb]=node_count;
            memset(nodes+before,(int)node_count,after-before);
            ++node_count;
        }
        ++limb;
    }
    d.transformed=0;
    const uint32_t mesh=family_mesh(p,&d);
    RiBuf *poses=ri_buf(ctx);
    for (uint32_t t=0;t<ticks;++t) {
        armature_pose(&d,&a,animation[schedule[t].animation],schedule[t].frame,now);
        for (uint32_t limb=0;limb<limbs;++limb)
            if (node_of[limb]!=255)limb_pose(ctx,poses,now[limb],rest[limb]);
    }
    write_rig(&d,mesh,nodes,node_count,ticks,poses,NULL,0);
    return mesh;
}
/* A clip that loops one animation, `per_frame` ticks a frame. */
static uint32_t armature_loop(Placer *p,const char *what,uint32_t skeleton,uint32_t animations,uint32_t animation,
                              uint32_t per_frame,uint32_t step,const uint32_t *setup)
{
    Draw d;memset(&d,0,sizeof d);d.segments=p->segments;
    mkds_model_init(&d.m,p->course->ctx,what,0,NULL,0,0);
    const uint32_t length=animation_length(&d,mk64_be32(resolve(&d,animations+animation*4,4)));
    /* `step` frames an update: the loop closes after length / gcd updates */
    uint32_t g=length;for (uint32_t b=step;b;) { const uint32_t r=g%b; g=b; b=r; }
    const uint32_t updates=length/g,ticks=updates*per_frame;
    Mk64ArmatureFrame *schedule=(Mk64ArmatureFrame *)(void *)ri_buf_zero(p->course->ctx,ticks*sizeof *schedule)->data;
    for (uint32_t t=0;t<ticks;++t) { schedule[t].animation=(uint8_t)animation; schedule[t].frame=(uint8_t)((t/per_frame*step)%length); }
    return armature_mesh(p,what,skeleton,animations,schedule,ticks,setup);
}

/* Spline paths (spline.h, func_8008B478): s16 count, then control points
 * {x, y, z, frames}; a closed uniform cubic B-spline whose segment from
 * point k takes about point k's frames. Sampled as `per` route points a
 * segment, offset by `origin` (source units). */
static void spline_route(MkdsModel *course,Mk64Loop *l,RiData data,uint32_t at,const int32_t origin[3],
                         const int32_t turn[4],unsigned per,Mk64Floor floor,void *context,int32_t lift)
{
    RiCtx *ctx=course->ctx;
    const uint32_t count=(uint32_t)mk64_i16(ri_at(ctx,data,at,2));
    if (count<4 || count>64)ri_fail(ctx,"%s: spline of %u points",course->where,(unsigned)count);
    int32_t point[64][4];
    for (uint32_t k=0;k<count;++k) {
        const uint8_t *row=ri_at(ctx,data,at+2+k*8,8);
        for (unsigned c=0;c<4;++c)point[k][c]=mk64_i16(row+c*2);
    }
    for (uint32_t k=0;k<count;++k)
        for (unsigned s=0;s<per;++s) {
            /* B-spline basis at u = s/per, Q16 */
            const int64_t u=(int64_t)s*65536/per,v=65536-u;
            const int64_t b[4]={v*v/65536*v/65536/6,(u*u/65536*u/65536)/2-u*u/65536+43691,
                                -(u*u/65536*u/65536)/2+u*u/65536/2+u/2+10923,u*u/65536*u/65536/6};
            int64_t pos[3]={0,0,0};
            for (unsigned j=0;j<4;++j)
                for (unsigned c=0;c<3;++c)pos[c]+=b[j]*point[(k+j)%count][c];
            /* turn: optional yaw (sine, cosine Q30) of the offsets about up */
            int64_t x=pos[0],z=pos[2];
            if (turn) { x=(pos[0]*turn[1]+pos[2]*turn[0])>>30; z=(pos[2]*turn[1]-pos[0]*turn[0])>>30; }
            int32_t engine[3]={(int32_t)((((int64_t)origin[0]<<16)+x)*5/88),(int32_t)((((int64_t)origin[2]<<16)+z)*5/88),
                               (int32_t)((((int64_t)origin[1]<<16)+pos[1])*5/88)};
            if (floor) {
                int32_t probe[3]={engine[0],engine[1],mk64_metres(2000)},ground;
                if (floor(context,probe,&ground))engine[2]=ground+mk64_metres(lift);
            }
            const uint32_t frames=(uint32_t)(point[k][3]>0?point[k][3]:1);
            loop_timed(course,l,engine,(frames*FRAME_TICKS+per/2)/per>0?(frames*FRAME_TICKS+per/2)/per:1u);
        }
}

/* ---- Animated families (update_objects.c, render_objects.c) ---- */

/* Rainbow Road Chain Chomps (update_chain_chomps, func_8000D940): three
 * armature models (d_rainbow_road_unk4, its 20-frame animation at one frame
 * a frame) at 0.03 scale that run race path 0 against the race direction
 * at 4 units a frame from points 500 + 300 i, 15 below the path; contact
 * within 10 units tumbles (func_80089CBC). The lane sway between the path
 * edges is not reproduced. */
typedef struct Mk64Chomps {
    uint8_t course, count;
    uint16_t first, spacing;
    uint32_t skeleton, animations;
    uint32_t scale;          /* Q16 sizeScaling */
    uint16_t speed;          /* source units a frame, Q8 */
    int16_t height, centre;  /* source units: below the path, ball centre above the root */
    uint8_t radius;
} Mk64Chomps;
static const Mk64Chomps chomps[] = {
    {13,3,500,300,0x06016110,0x0601610C,1966,1024,-15,17,10},
};

/* Yoshi Valley flags (func_80083080, func_80055228): armatures at their
 * D_800E5DF4 rows {x, y, z, yaw}, 0.027 scale, one frame a frame. */
typedef struct Mk64Flags {
    uint8_t course, count;
    uint32_t rows;           /* ROM s16 {x, y, z, yaw} */
    uint32_t skeleton, animations, scale;
} Mk64Flags;
static const Mk64Flags flags[] = {
    {4,4,0xE69F4u,0x06014798,0x06014794,1769},
};

/* Sherbet Land penguins (update_penguins): one armature, three roles.
 * The emperor (func_80084430, 1P only in the source) walks a spline around
 * its origin at 0.2 scale; circlers (func_800845C8 1..8, func_80088038)
 * walk circles of radius r at `step` angle units a frame at 0.08 scale,
 * pairs half a lap apart, two animation frames a frame; sliders (9..14,
 * func_80084D2C) turn about, waddle, belly-slide to a top speed, slow, stand
 * up and turn back, at 0.04 scale. Circlers and sliders bump karts within
 * 4 units (func_80089820, ENEMY_BONK). */
#define PENGUIN_SKELETON 0x06008EB0u
#define PENGUIN_ANIMATIONS 0x06009AC8u
static const struct { int16_t x, z; uint8_t radius; int16_t step; } penguin_circles[4] = {
    {-2960,1521,100,0x150},{-2490,1612,80,0x100},{-2098,1624,80,-0x100},{-2080,1171,80,0x150},
};
static const struct { int16_t x, z; uint16_t heading; uint8_t top; } penguin_sliders[6] = {
    /* top speed in tenths of a unit a frame */
    {146,-380,0x9000,10},{380,-766,0x5000,15},{-2300,-210,0xC000,25},{-2500,-250,0x4000,25},
    {-535,875,0x8000,25},{-250,953,0x9000,25},
};
#define PENGUIN_EMPEROR_SPLINE 0xE719Cu /* D_800E659C */

/* func_800417B4: a yaw stepped toward a target. */
static uint16_t step_angle(uint16_t from,uint16_t to)
{
    if ((from>>8)==(to>>8))return to;
    const uint32_t d=(uint32_t)(to-from)&0xFFFFu;
    static const struct { uint32_t below; int32_t step; } steps[]={
        {0x400,0x80},{0x800,0x200},{0x4000,0x400},{0x8000,0x700},{0xC000,-0x700},{0xF800,-0x400},{0xFC00,-0x200},{0x10000,-0x80}};
    for (unsigned i=0;i<8;++i)if (d<steps[i].below)return (uint16_t)(from+steps[i].step);
    return to;
}
/* f32_step_towards in Q16; returns 1 on arrival */
static int step_towards(int32_t *value,int32_t target,int32_t step)
{
    if (*value<target) { *value+=step; if (*value>=target) { *value=target; return 1; } }
    else if (*value>target) { *value-=step; if (*value<=target) { *value=target; return 1; } }
    return 0;
}
/* One slider frame: position (Q16 source units from the start), heading, clip and frame. */
typedef struct SliderFrame { int64_t x, z; uint16_t heading; uint8_t animation, frame; } SliderFrame;
/* Simulates func_80084D2C/func_80084B7C from a turn toward `heading`;
 * returns the frames of one full out-and-back cycle. */
static uint32_t penguin_slide(int32_t top,uint16_t heading,SliderFrame *out,uint32_t capacity)
{
    static const uint8_t last[3]={18,24,24}; /* get_animation_length */
    int state=2,active=0,ae=1,bits=0x14,timer=-1;
    uint32_t frame=0,step=2,animation=0,count=0,turns=0;
    int32_t speed=0;int64_t x=0,z=0;
    uint16_t direction=(uint16_t)(heading+0x8000),target=heading;
    for (uint32_t t=0;t<4000;++t) {
        if (state==2) {
            if (!active) { frame=0; active=1; }
            else { frame+=step; if (frame>last[animation])frame=0; }
        } else if (state==3) {
            if (!active) { frame=0; active=1; }
            else if (++frame>last[animation]) { frame=last[animation]; active=0; state=4; }
        } else if (state==4) { bits&=~2; state=5; }
        const int before=ae;
        #define TIMER(n) (timer<0?(timer=(n),0):0, --timer<0?(timer=-1,1):0)
        switch (ae) {
        case 1:
            direction=step_angle(direction,target);
            if (direction==target) { step=4; speed=26214; ae=2; timer=-1; }
            break;
        case 2:
            step_towards(&speed,52429,1311);
            if (TIMER(15)) { bits|=3; step=1; animation=1; state=3; active=0; ae=3; }
            break;
        case 3:
            step_towards(&speed,top,9830);
            if (!(bits&2) && speed==top) { ae=4; timer=-1; }
            break;
        case 4:
            if (TIMER(30)) { bits&=~1; ae=5; }
            break;
        case 5:
            step_towards(&speed,26214,13107);
            if (TIMER(10)) { bits|=2; animation=2; state=3; active=0; ae=6; }
            break;
        case 6:
            if (!(bits&2)) { animation=0; frame=0; target=(uint16_t)(target+0x8000); state=2; active=0; ae=1; timer=-1; }
            break;
        }
        #undef TIMER
        if (before!=1 && ae==1)++turns;
        if (turns>=1 && turns<3) {
            if (count==capacity)return 0;
            out[count++]=(SliderFrame){x,z,direction,(uint8_t)animation,(uint8_t)frame};
        }
        if (turns==3)return count;
        int32_t sine,cosine;portable_fixed_3d_sincos(direction,&sine,&cosine);
        x+=(int64_t)speed*(sine>>14)>>16;z+=(int64_t)speed*(cosine>>14)>>16;
    }
    return 0;
}

/* Koopa Troopa Beach seagulls (update_seagulls): armature birds at 0.2
 * scale, five around each centre (offsets random_int x, z in -100..99, y in
 * 0..19), each on spline D_800E633C[i % 4], flapping one frame a frame. */
typedef struct Mk64Gulls {
    uint8_t course, count;
    int16_t centre[2][3];
    uint32_t splines[4];
    uint32_t skeleton, animations, scale;
} Mk64Gulls;
static const Mk64Gulls gulls[] = {
    {6,10,{{-985,15,1200},{328,20,2541}},{0xE6C34u,0xE6CF0u,0xE6DB4u,0xE6E80u},0x060169B0,0x06016B60,13107},
};

/* Frappe Snowland snowmen (update_snowmen, render_object_snowmans):
 * gSnowmanSpawns {x, y, z, group}; the body (y + 3) and head (y + 8, drawn in
 * front) are 64x64 CI8 sprites at 0.1 scale, the head rocking +-0x1000 in
 * 0x400 steps every two frames. Contact within 2 units tumbles (the source
 * also bursts the snowman and rebuilds it 300 frames later, which waits on a
 * kart and is not reproduced). */
typedef struct Mk64Snowmen {
    uint8_t course, count;
    uint32_t rows, head, body, tlut;
    uint32_t scale;
    uint8_t radius;
} Mk64Snowmen;
static const Mk64Snowmen snowmen[] = {
    {5,19,0xE6AE8u,0x06004D20,0x06005D20,0x06004B20,6554,2},
};

/* Moo Moo Farm moles (update_moles, func_80081D34): 31 holes (gMoleSpawns)
 * in three groups that keep `active` moles up at once (Time Trial counts,
 * gCCSelection 100cc: 5, 8, 8). A mole rises 9 at 0.7 a frame, peeks 11
 * frames, drops to 3, then jumps (3.6 up, 0.25 gravity) for 29 frames
 * stepping its sprite 1..6 every two frames, and waits random_int(30) + 5
 * frames under its hole. Here each hole repeats that on a fixed cycle, the
 * holes of a group evenly staggered. Contact within 6 units tumbles. */
typedef struct Mk64Moles {
    uint8_t course;
    uint32_t holes;          /* ROM Vec3s */
    uint8_t groups[3][2];    /* first hole, count */
    uint8_t active[3];
    uint32_t frames, tlut;   /* CI8 32x64 frame 0, 0x800 apart; pal256 */
    uint32_t scale;
    uint8_t radius;
} Mk64Moles;
static const Mk64Moles moles[] = {
    {9,0xE6F60u,{{0,8},{8,11},{19,12}},{5,8,8},0x0600FE70,0x0600FC70,9830,6},
};

/* Choco Mountain falling rocks (src/actors/falling_rock): each spawns 10
 * above its row and falls under 0.1 gravity (at most 2 a frame), bouncing off
 * the floor (velocity along the normal removed, then 1.2 times its fall
 * speed back up) and rolling (rot x += vz * 5461 / 20); below -80 it waits
 * 60, 120 or 180 ticks hidden and starts again. Simulated here on the
 * course floor (walls are not consulted). Contact within 10 squishes. */
typedef struct Mk64Rocks {
    uint8_t course;
    uint32_t spawns, list;
    uint8_t radius;
    int16_t floor;           /* D_8015F8E4 */
} Mk64Rocks;
static const Mk64Rocks rocks[] = {
    {1,0x06007230,0x06006FE0,10,-80},
};

/* A sprite frame schedule: frame `frame` (or 255, hidden) for `ticks`. */
typedef struct Mk64Run { uint8_t frame; uint16_t ticks; } Mk64Run;

/* One mesh of `count` sprite frames (each its own node) whose looping clip
 * shows frame schedule[t] (255 hides all) at tick t; roll (0x10000 a turn,
 * NULL for none) turns the sprite in its plane about `pivot` (source units). */
static uint32_t sprite_frames(Placer *p,Draw *d,const uint8_t *schedule,uint32_t ticks,uint32_t count,
                              int32_t w,int32_t h,int32_t x,int32_t y,const uint32_t *textures,const uint32_t *tluts,
                              uint32_t fmt,uint32_t siz,uint32_t tw,uint32_t th,uint32_t cms,uint32_t alpha,
                              const uint16_t *roll)
{
    RiCtx *ctx=p->course->ctx;
    uint8_t nodes[2048];
    for (uint32_t k=0;k<count;++k) {
        sprite_texture(d,fmt,siz,tw,th,textures[k],tluts?tluts[k]:0,cms,alpha);
        const uint32_t before=sprite_quad(d,w,h,x,y,0);
        memset(nodes+before,(int)k,d->m.tables[MKDS_T_VERT].count-before);
    }
    const uint32_t mesh=family_mesh(p,d);
    RiBuf *poses=ri_buf(ctx);
    const int32_t pivot[3]={mk64_metres(x),0,mk64_metres(y)};
    for (uint32_t t=0;t<ticks;++t)
        for (uint32_t k=0;k<count;++k) {
            const int32_t scale=schedule[t]==k?65536:0;
            if (roll)pose_turn(ctx,poses,1,roll[t],scale,pivot);
            else pose_scale(ctx,poses,scale,pivot);
        }
    write_rig(d,mesh,nodes,count,ticks,poses,NULL,0);
    return mesh;
}

/* A path walked against its direction: reversed copy. */
static int32_t (*reversed_path(RiCtx *ctx,const int32_t (*path)[3],uint32_t count,int32_t lift))[3]
{
    int32_t (*out)[3]=(int32_t (*)[3])(void *)ri_buf_zero(ctx,(size_t)count*sizeof *out)->data;
    for (uint32_t i=0;i<count;++i) {
        memcpy(out[i],path[(count-i)%count],sizeof out[i]);
        out[i][2]+=lift;
    }
    return out;
}

/* The shared route of vehicles/actors on `path` at `speed` (Q8 units a frame). */
static VehicleRoute *path_route(Placer *p,const int32_t (*path)[3],uint32_t length,uint16_t speed)
{
    for (uint32_t i=0;i<p->route_count;++i)
        if (p->routes[i].path==path && p->routes[i].speed==speed)return &p->routes[i];
    if (p->route_count==sizeof p->routes/sizeof p->routes[0])ri_fail(p->course->ctx,"%s: too many vehicle routes",p->course->where);
    VehicleRoute *r=&p->routes[p->route_count++];
    r->path=path;r->length=length;r->speed=speed;
    vehicle_route(p->course,r);
    return r;
}

static void chomp_family(Placer *p,const Mk64Chomps *c,const int32_t (*race)[3],uint32_t race_count)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    if (!race || race_count<(uint32_t)c->first+(uint32_t)c->spacing*c->count)ri_fail(ctx,"%s: Chain Chomps need race path 0",course->where);
    const uint32_t mesh=armature_loop(p,"Chain Chomp",c->skeleton,c->animations,0,FRAME_TICKS,1,NULL);
    const int32_t (*path)[3]=(const int32_t (*)[3])reversed_path(ctx,race,race_count,mk64_metres(c->height));
    VehicleRoute *r=path_route(p,path,race_count,c->speed);
    for (uint32_t i=0;i<c->count;++i) {
        const uint32_t at=(race_count-(c->first+c->spacing*i))%race_count;
        int32_t q[4];facing(path[0],path[r->node[1]],q);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        uint32_t count=route_properties(course,route_phase(r,at),1);
        const int32_t centre[3]={path[0][0],path[0][1],path[0][2]+mk64_metres(c->centre)};
        mkds_property_vector(course,"mksc:hazard-center",centre);++count;
        count+=hazard_properties(p,c->radius,REACT_TUMBLE);
        const uint32_t row[16]={0x4E3600D0u|i,MKDS_KIND_MESH,mesh,(uint32_t)path[0][0],(uint32_t)path[0][1],(uint32_t)path[0][2],
            (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],c->scale,c->scale,c->scale,r->id,props,count};
        mkds_words(course,MKDS_T_OBJS,row,16);
    }
}

static void flag_family(Placer *p,const Mk64Flags *f)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    const uint32_t mesh=armature_loop(p,"flag",f->skeleton,f->animations,0,FRAME_TICKS,1,NULL);
    for (uint32_t i=0;i<f->count;++i) {
        const uint8_t *row=ri_at(ctx,ctx->rom,f->rows+i*8,8);
        const int32_t at[3]={mk64_metres(mk64_i16(row)),mk64_metres(mk64_i16(row+4)),mk64_metres(mk64_i16(row+2))};
        object_row(course,0xE0,mesh,at,(uint16_t)mk64_be16(row+6),f->scale,MKDS_ABSENT,course->tables[MKDS_T_PROP].count,0);
    }
}

/* The engine point over source (x, z) at source height y, or on the floor. */
static void ground_point(Mk64Floor floor,void *context,int32_t x,int32_t y,int32_t z,int snap,int32_t out[3])
{
    out[0]=mk64_metres(x);out[1]=mk64_metres(z);out[2]=mk64_metres(y);
    int32_t probe[3]={out[0],out[1],mk64_metres(y+200)},ground;
    if (snap && floor(context,probe,&ground))out[2]=ground;
}

static void penguin_family(Placer *p,Mk64Floor floor,void *context)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    /* the emperor: clip 0, one frame a frame, along its spline */
    {
        const uint32_t mesh=armature_loop(p,"emperor penguin",PENGUIN_SKELETON,PENGUIN_ANIMATIONS,0,FRAME_TICKS,1,NULL);
        Mk64Loop l;memset(&l,0,sizeof l);
        const int32_t origin[3]={-383,2,-690};
        spline_route(course,&l,ctx->rom,PENGUIN_EMPEROR_SPLINE,origin,NULL,2,NULL,NULL,0);
        const uint32_t route=loop_route(course,&l);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        const uint32_t count=route_properties(course,0,1);
        int32_t q[4];facing(l.at[0],l.at[1],q);
        const uint32_t row[16]={0x4E3600E1u,MKDS_KIND_MESH,mesh,(uint32_t)l.at[0][0],(uint32_t)l.at[0][1],(uint32_t)l.at[0][2],
            (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],13107,13107,13107,route,props,count};
        mkds_words(course,MKDS_T_OBJS,row,16);
    }
    /* circlers: two animation frames a frame */
    {
        const uint32_t mesh=armature_loop(p,"circling penguin",PENGUIN_SKELETON,PENGUIN_ANIMATIONS,0,FRAME_TICKS,2,NULL);
        for (unsigned i=0;i<8;++i) {
            const unsigned c=i/2;
            const int32_t step=penguin_circles[c].step,radius=penguin_circles[c].radius;
            const uint32_t lap=65536u/(uint32_t)(step<0?-step:step)*FRAME_TICKS;
            Mk64Loop l;memset(&l,0,sizeof l);
            for (unsigned n=0;n<ORBIT_NODES;++n) {
                /* offset (sin a, cos a) * r, a stepping by `step` */
                const uint16_t angle=(uint16_t)((int32_t)n*(step<0?-1:1)*(65536/ORBIT_NODES));
                int32_t sine,cosine;portable_fixed_3d_sincos(angle,&sine,&cosine);
                const int32_t at[3]={mk64_metres(penguin_circles[c].x+(int32_t)(((int64_t)sine*radius)>>30)),
                                     mk64_metres(penguin_circles[c].z+(int32_t)(((int64_t)cosine*radius)>>30)),mk64_metres(-80)};
                loop_timed(course,&l,at,lap/ORBIT_NODES);
            }
            const uint32_t route=loop_route(course,&l);
            const uint32_t props=course->tables[MKDS_T_PROP].count;
            /* odd penguins start half a lap on (unk_0C4 = i << 15) */
            uint32_t count=route_properties(course,(i+1)%2?lap/2:0u,1);
            count+=hazard_properties(p,4,REACT_BUMP);
            int32_t q[4];facing(l.at[0],l.at[1],q);
            const uint32_t row[16]={0x4E3600E2u,MKDS_KIND_MESH,mesh,(uint32_t)l.at[0][0],(uint32_t)l.at[0][1],(uint32_t)l.at[0][2],
                (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],5243,5243,5243,route,props,count};
            mkds_words(course,MKDS_T_OBJS,row,16);
        }
    }
    /* sliders: one simulated cycle each, its clip frames held two ticks */
    SliderFrame *frames=(SliderFrame *)(void *)ri_buf_zero(ctx,1024*sizeof *frames)->data;
    for (unsigned i=0;i<6;++i) {
        const uint32_t n=penguin_slide((int32_t)penguin_sliders[i].top*65536/10,penguin_sliders[i].heading,frames,1024);
        if (n<16 || n*FRAME_TICKS>PORTABLE_MESH_MOTION_TICK_LIMIT)ri_fail(ctx,"%s: penguin %u does not settle",course->where,i+9);
        Mk64ArmatureFrame *schedule=(Mk64ArmatureFrame *)(void *)ri_buf_zero(ctx,n*FRAME_TICKS*sizeof *schedule)->data;
        for (uint32_t t=0;t<n*FRAME_TICKS;++t) { schedule[t].animation=frames[t/FRAME_TICKS].animation; schedule[t].frame=frames[t/FRAME_TICKS].frame; }
        char what[32];snprintf(what,sizeof what,"sliding penguin %u",i+9);
        const uint32_t mesh=armature_mesh(p,what,PENGUIN_SKELETON,PENGUIN_ANIMATIONS,schedule,n*FRAME_TICKS,NULL);
        int32_t base[3];ground_point(floor,context,penguin_sliders[i].x,0,penguin_sliders[i].z,1,base);
        Mk64Loop l;memset(&l,0,sizeof l);
        const uint32_t every=4; /* frames a route point */
        for (uint32_t f=0;f<n;f+=every) {
            const int32_t at[3]={base[0]+(int32_t)(frames[f].x*5/88),base[1]+(int32_t)(frames[f].z*5/88),base[2]};
            loop_timed(course,&l,at,(f+every<=n?every:n-f)*FRAME_TICKS);
        }
        const uint32_t route=loop_route(course,&l);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        uint32_t count=route_properties(course,0,1);
        count+=hazard_properties(p,4,REACT_BUMP);
        /* rest heading: the cycle's first */
        object_row(course,0xE3,mesh,l.at[0],frames[0].heading,2621,route,props,count);
    }
}

static void gull_family(Placer *p,const Mk64Gulls *g)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    const uint32_t mesh=armature_loop(p,"seagull",g->skeleton,g->animations,0,FRAME_TICKS,1,NULL);
    Mk64Random random={0x5EA6u+p->index};
    for (uint32_t i=0;i<g->count;++i) {
        const int16_t *centre=g->centre[i<g->count/2u?0:1];
        const int32_t origin[3]={centre[0]+(int32_t)random_below(&random,200)-100,centre[1]+(int32_t)random_below(&random,20),
                                 centre[2]+(int32_t)random_below(&random,200)-100};
        Mk64Loop l;memset(&l,0,sizeof l);
        spline_route(course,&l,ctx->rom,g->splines[i%4],origin,NULL,2,NULL,NULL,0);
        const uint32_t route=loop_route(course,&l);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        const uint32_t count=route_properties(course,1+random_below(&random,109),1);
        int32_t q[4];facing(l.at[0],l.at[1],q);
        const uint32_t row[16]={0x4E3600E4u,MKDS_KIND_MESH,mesh,(uint32_t)l.at[0][0],(uint32_t)l.at[0][1],(uint32_t)l.at[0][2],
            (uint32_t)q[0],(uint32_t)q[1],(uint32_t)q[2],(uint32_t)q[3],g->scale,g->scale,g->scale,route,props,count};
        mkds_words(course,MKDS_T_OBJS,row,16);
    }
}

static void snowman_family(Placer *p,const Mk64Snowmen *s)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    Draw d;family_draw(p,&d,"snowman");
    uint8_t nodes[2048];
    sprite_texture(&d,2,1,64,64,s->body,s->tlut,0,16);
    uint32_t before=sprite_quad(&d,64,64,0,0,0);
    memset(nodes+before,255,d.m.tables[MKDS_T_VERT].count-before);
    sprite_texture(&d,2,1,64,64,s->head,s->tlut,0,16);
    before=sprite_quad(&d,64,64,0,50,12);
    memset(nodes+before,0,d.m.tables[MKDS_T_VERT].count-before);
    const uint32_t mesh=family_mesh(p,&d);
    /* the head rocks 0 .. +0x1000 .. -0x1000 .. 0 by 0x400 every two frames */
    RiBuf *poses=ri_buf(ctx);
    const int32_t pivot[3]={0,0,mk64_metres(50)};
    const uint32_t ticks=16*2*FRAME_TICKS;
    for (uint32_t t=0;t<ticks;++t) {
        const int32_t k=(int32_t)(t/(2*FRAME_TICKS));
        const int32_t roll=k<4?k*0x400:k<12?0x1000-(k-4)*0x400:-0x1000+(k-12)*0x400;
        pose_turn(ctx,poses,1,(uint16_t)roll,65536,pivot);
    }
    write_rig(&d,mesh,nodes,1,ticks,poses,NULL,0);
    for (uint32_t i=0;i<s->count;++i) {
        const uint8_t *row=ri_at(ctx,ctx->rom,s->rows+i*8,8);
        const int32_t at[3]={mk64_metres(mk64_i16(row)),mk64_metres(mk64_i16(row+4)),mk64_metres(mk64_i16(row+2)+3)};
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
        const uint32_t count=1+hazard_properties(p,s->radius,REACT_TUMBLE);
        object_row(course,0xE5,mesh,at,0,s->scale,MKDS_ABSENT,props,count);
    }
}

/* Mole offsets above its hole origin (source units, Q8) at frame n of the
 * pop and the sprite frame shown; returns 0 past the pop. */
#define MOLE_POP_FRAMES (13+11+6+29)
static int mole_pop(uint32_t n,int32_t *offset,uint8_t *frame)
{
    *frame=0;
    if (n<13) { *offset=(int32_t)(n*179); return 1; }                    /* 0.7 a frame up to 9 */
    if (n<24) { *offset=9*256; return 1; }
    if (n<30) { *offset=9*256-(int32_t)(n-24)*256; return 1; }           /* down to 3 */
    if (n<MOLE_POP_FRAMES) {
        const int32_t j=(int32_t)(n-30)+1;                                /* v -= 0.25 before each move */
        *offset=3*256+j*922-j*(j+1)*32;
        if (*offset<0)*offset=0;
        *frame=(uint8_t)(1+(n-30)/2>6?6:1+(n-30)/2);
        return 1;
    }
    *offset=0;
    return 0;
}

static void mole_family(Placer *p,const Mk64Moles *m)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    uint32_t textures[7],tluts[7];
    for (unsigned k=0;k<7;++k) { textures[k]=m->frames+k*0x800u; tluts[k]=m->tlut; }
    for (unsigned g=0;g<3;++g) {
        const uint32_t first=m->groups[g][0],holes=m->groups[g][1];
        /* each mole slot: a pop plus random_int(30) + 5 frames (19.5 mean) */
        const uint32_t period=((MOLE_POP_FRAMES*2+39)*holes/m->active[g]+1)/2*FRAME_TICKS;
        uint8_t *schedule=ri_buf_zero(ctx,period)->data;
        const uint32_t rest=period-MOLE_POP_FRAMES*FRAME_TICKS;
        for (uint32_t t=0;t<period;++t) {
            int32_t offset;uint8_t frame=0;
            if (t>=rest)mole_pop((t-rest)/FRAME_TICKS,&offset,&frame);
            schedule[t]=frame;
        }
        Draw d;family_draw(p,&d,"mole");
        const uint32_t mesh=sprite_frames(p,&d,schedule,period,7,64,64,0,0,textures,tluts,2,1,32,64,1,16,NULL);
        for (uint32_t h=0;h<holes;++h) {
            const uint8_t *row=ri_at(ctx,ctx->rom,m->holes+(first+h)*6,6);
            const int32_t hole[3]={mk64_metres(mk64_i16(row)),mk64_metres(mk64_i16(row+4)),mk64_metres(mk64_i16(row+2)-9)};
            Mk64Loop l;memset(&l,0,sizeof l);
            loop_timed(course,&l,hole,rest);
            for (uint32_t n=0;n<MOLE_POP_FRAMES;n+=2) {
                int32_t offset;uint8_t frame;mole_pop(n,&offset,&frame);
                const int32_t at[3]={hole[0],hole[1],hole[2]+(int32_t)((int64_t)mk64_metres(offset)/256)};
                loop_timed(course,&l,at,(n+2<=MOLE_POP_FRAMES?2u:1u)*FRAME_TICKS);
            }
            const uint32_t route=loop_route(course,&l);
            const uint32_t props=course->tables[MKDS_T_PROP].count;
            mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
            /* the holes of a group pop evenly staggered */
            uint32_t count=1+route_properties(course,(h*period/holes)%period?period-(h*period/holes)%period:0u,0);
            count+=hazard_properties(p,m->radius,REACT_TUMBLE);
            object_row(course,0xE6,mesh,hole,0,m->scale,route,props,count);
        }
    }
}

/* One falling rock's bounces, sampled; returns the ticks until it drops
 * below the floor limit (or `capacity`). positions: Q16 source units a frame. */
static uint32_t rock_fall(Mk64Floor floor,void *context,const int32_t spawn[3],int16_t limit,
                          int64_t (*at)[3],int16_t (*turn)[2],uint32_t capacity)
{
    int64_t pos[3]={(int64_t)spawn[0]<<16,((int64_t)spawn[1]+10)<<16,(int64_t)spawn[2]<<16},v[3]={0,0,0};
    int16_t rx=0,rz=0;
    for (uint32_t t=0;t<capacity;++t) {
        if (pos[1]<((int64_t)limit<<16))return t;
        rx=(int16_t)(rx+(int16_t)(v[2]*5461/20>>16));
        rz=(int16_t)(rz+(int16_t)(v[0]*5461/20>>16));
        v[1]-=6554;if (v[1]<-131072)v[1]=-131072;
        for (unsigned k=0;k<3;++k)pos[k]+=v[k];
        const int64_t before=v[1];
        /* the floor plane under the rock: height and normal from neighbours */
        int32_t h[3],ok=1;
        static const int32_t probe[3][2]={{0,0},{4,0},{0,4}};
        for (unsigned k=0;k<3;++k) {
            const int32_t engine[3]={(int32_t)(((pos[0]>>16)+probe[k][0])*65536*5/88),(int32_t)(((pos[2]>>16)+probe[k][1])*65536*5/88),
                                     (int32_t)(((pos[1]>>16)+30)*65536*5/88)};
            int32_t ground;
            ok&=floor(context,engine,&ground);
            h[k]=(int32_t)((int64_t)ground*88/5); /* Q16 source units */
        }
        if (ok) {
            /* n = normalize(-dh/dx, 4, -dh/dz) scaled to Q30 */
            const int64_t raw[3]={-((int64_t)h[1]-h[0]),(int64_t)4<<16,-((int64_t)h[2]-h[0])};
            int32_t n[3];
            if (portable_fixed_3d_normalize(raw,n)) {
                const int64_t distance=((pos[1]-h[0])*n[1]>>30)-((int64_t)10<<16);
                if (distance<0) {
                    for (unsigned k=0;k<3;++k)pos[k]-=distance*n[k]>>30;
                    const int64_t dot=(v[0]*n[0]+v[1]*n[1]+v[2]*n[2])>>30;
                    const int64_t keep=distance< -229376?3:2; /* halves; -3.5 units */
                    for (unsigned k=0;k<3;++k)v[k]-=dot*n[k]*keep/2>>30;
                    v[1]=-before*6/5;
                }
            }
        }
        for (unsigned k=0;k<3;++k)at[t][k]=pos[k];
        turn[t][0]=rx;turn[t][1]=rz;
    }
    return capacity;
}

#define ROCK_FRAMES 360u
static void rock_family(Placer *p,const Mk64Rocks *r,Mk64Floor floor,void *context)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    static const uint16_t waits[3]={60,120,180};
    int64_t (*at)[3]=(int64_t (*)[3])(void *)ri_buf_zero(ctx,ROCK_FRAMES*sizeof *at)->data;
    int16_t (*turn)[2]=(int16_t (*)[2])(void *)ri_buf_zero(ctx,ROCK_FRAMES*sizeof *turn)->data;
    for (uint32_t entry=0;;++entry) {
        if (entry==8)ri_fail(ctx,"%s: unterminated falling rock spawns",course->where);
        const uint8_t *row=ri_at(ctx,p->segments->segment[6],(r->spawns&0xFFFFFFu)+entry*8,8);
        if (mk64_be16(row)==0x8000u)break;
        const int32_t spawn[3]={mk64_i16(row),mk64_i16(row+2),mk64_i16(row+4)};
        const uint32_t id=mk64_be16(row+6);
        if (id>2)ri_fail(ctx,"%s: falling rock %u has id %u",course->where,(unsigned)entry,(unsigned)id);
        /* The source rock bounces on until it drops below the course;
         * without its wall bounces it can stay on the road, so it is gone
         * ROCK_FRAMES frames after its spawn at the latest. */
        const uint32_t fall=rock_fall(floor,context,spawn,r->floor,at,turn,ROCK_FRAMES);
        if (fall<8)ri_fail(ctx,"%s: falling rock %u does not fall",course->where,(unsigned)entry);
        /* one source frame is FRAME_TICKS ticks */
        const uint32_t period=(fall+waits[id])*FRAME_TICKS;
        Draw d;family_draw(p,&d,"falling rock");
        draw_list(&d,r->list,0);
        uint8_t nodes[2048];memset(nodes,0,sizeof nodes);
        const uint32_t mesh=family_mesh(p,&d);
        RiBuf *poses=ri_buf(ctx);
        const int32_t pivot[3]={0,0,0};
        for (uint32_t tick=0;tick<period;++tick) {
            const uint32_t t=tick/FRAME_TICKS;
            if (t>=fall) { pose_scale(ctx,poses,0,pivot); continue; }
            /* roll: x then z (source), engine x and y */
            int32_t sx,cx,sz,cz;
            portable_fixed_3d_sincos((uint16_t)turn[t][0],&sx,&cx);
            portable_fixed_3d_sincos((uint16_t)turn[t][1],&sz,&cz);
            const int64_t m[12]={
                (int64_t)cz,-(int64_t)sz*cx>>30,(int64_t)sz*sx>>30,
                (int64_t)sz,(int64_t)cz*cx>>30,-(int64_t)cz*sx>>30,
                0,(int64_t)sx,(int64_t)cx,0,0,0};
            int32_t out[12];pose_from_source(m,out);pose_put(ctx,poses,out);
        }
        write_rig(&d,mesh,nodes,1,period,poses,NULL,0);
        Mk64Loop l;memset(&l,0,sizeof l);
        for (uint32_t t=0;t<fall;t+=4) {
            const int32_t point[3]={(int32_t)(at[t][0]*5/88),(int32_t)(at[t][2]*5/88),(int32_t)(at[t][1]*5/88)};
            loop_timed(course,&l,point,(t+4<=fall?4u:fall-t)*FRAME_TICKS);
        }
        /* hidden, back at the spawn for the wait */
        const int32_t home[3]={mk64_metres(spawn[0]),mk64_metres(spawn[2]),mk64_metres(spawn[1]+10)};
        loop_timed(course,&l,home,waits[id]*FRAME_TICKS);
        const uint32_t route=loop_route(course,&l);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        uint32_t count=route_properties(course,0,0);
        count+=hazard_properties(p,r->radius,REACT_SQUASH);
        object_row(course,0x1A,mesh,l.at[0],0,65536,route,props,count);
    }
}

/* Railroad crossings (spawn_course_actors, func_80013054): crossing `id`
 * shows dl_crossing_both_inactive until a locomotive's path point is past
 * `fraction` - 0.1 of the train path and before `fraction` + 0.01 + 0.01
 * per car (six in one-player races), then flashes (the lamps table). Both
 * variants stand at each crossing; mksc:route-window shows one or the
 * other by where the train route's locomotives are. The bell is not
 * reproduced. */
#define CROSSING_ACTIVE 0x06010AE8u   /* dl_crossing_right_active */
#define CROSSING_INACTIVE 0x06010D38u /* dl_crossing_both_inactive */
#define CROSSING_CARS 6u
typedef struct Mk64Crossing { uint8_t course, id; int16_t at[3], yaw; } Mk64Crossing;
static const Mk64Crossing crossings[] = {
    {11,1,{-1680,2,35},0}, {11,1,{-1600,2,35},0}, {11,0,{-2459,2,2263},-8192}, {11,0,{-2467,2,2375},-8192},
};
static const uint32_t crossing_fraction[2]={42299348,72017354}; /* 1e-8 of the path */

static void crossing_family(Placer *p,const VehicleRoute *train,uint32_t locomotive)
{
    MkdsModel *course=p->course;
    uint32_t mesh[2];
    for (unsigned active=0;active<2;++active) {
        Draw d;family_draw(p,&d,active?"railroad crossing (flashing)":"railroad crossing");
        draw_list(&d,active?CROSSING_ACTIVE:CROSSING_INACTIVE,0);
        RiBuf *swaps=ri_buf(course->ctx);
        const uint32_t count=active?lamp_swaps(&d,p->index,0x27,swaps):0u;
        if (active && !count)ri_fail(course->ctx,"%s: crossing lamps missing",course->where);
        mesh[active]=family_mesh(p,&d);
        if (count)write_rig(&d,mesh[active],NULL,0,0,NULL,swaps,count);
    }
    for (size_t i=0;i<sizeof crossings/sizeof crossings[0];++i) {
        const Mk64Crossing *c=&crossings[i];
        if (c->course!=p->index)continue;
        const int64_t length=train->length,low=(int64_t)crossing_fraction[c->id]-10000000,
                      high=(int64_t)crossing_fraction[c->id]+1000000+1000000*(int64_t)CROSSING_CARS;
        const uint32_t from=(uint32_t)(low*length/100000000+1),to=(uint32_t)((high*length+99999999)/100000000);
        if (low<0 || to>=train->length)ri_fail(course->ctx,"%s: crossing window leaves the train path",course->where);
        const int32_t window[3]={(int32_t)train->id,(int32_t)route_phase(train,from),(int32_t)route_phase(train,to)};
        const int32_t at[3]={mk64_metres(c->at[0]),mk64_metres(c->at[2]),mk64_metres(c->at[1])};
        for (unsigned active=0;active<2;++active) {
            const uint32_t first=mkds_property_vector(course,"mksc:route-window",window);
            mkds_property(course,"mksc:route-window-follower",MKDS_PROPERTY_INTEGER,0x4E360000u|locomotive,0);
            mkds_property(course,"mksc:route-window-outside",MKDS_PROPERTY_BOOLEAN,!active,0);
            object_row(course,0x27,mesh[active],at,(uint16_t)c->yaw,65536,MKDS_ABSENT,first,3);
        }
    }
}

/* VS bomb karts (set_bomb_kart_spawn_positions, update_bomb_karts,
 * func_80056BF0): gBombKartSpawns[course] holds seven 24-byte rows {u16
 * race path 0 point, u16 state, f32 speed, f32 x, f32 z, ...}. State 1
 * circles 25 units around the point 4 degrees a frame one way, state 2 the
 * other, state 3 stands on it; each bounces sin(20 degrees * (b % 18)) over
 * the point's height + 3.5. The bomb is common_texture_bomb's 32x32 CI8
 * frames 0 1 2 3 2 1 (D_800E471C, a step every two frames) on the 32-unit
 * quad at 0.25, one unit up, camera-facing; a kart within 5 units tumbles.
 * VS only (mksc:present-in). Not reproduced: the explosion that removes a
 * struck bomb (it waits on a kart), its fuse sparks and its four wheels. */
#define BOMB_SPAWNS 0xDD808u
#define BOMB_FRAMES 0x0D029858u
#define BOMB_TLUT 0x0D02A858u
#define BOMB_CIRCLE_FRAMES 90u   /* 360 / 4 degrees */
#define BOMB_BOUNCE_FRAMES 18u
static void bomb_family(Placer *p,const int32_t (*race)[3],uint32_t race_count)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    if (p->index>=20 || !race)return;
    uint32_t mesh=MKDS_ABSENT;
    for (uint32_t i=0;i<7;++i) {
        const uint8_t *row=ri_at(ctx,ctx->rom,BOMB_SPAWNS+(p->index*7+i)*24,24);
        const uint32_t point=mk64_be16(row),state=mk64_be16(row+2);
        if (!state)continue;
        if (state>3)ri_fail(ctx,"%s: bomb kart %u has state %u",course->where,(unsigned)i,(unsigned)state);
        if (point>=race_count)ri_fail(ctx,"%s: bomb kart %u past race path 0",course->where,(unsigned)i);
        if (mesh==MKDS_ABSENT) {
            static const uint8_t order[6]={0,1,2,3,2,1};
            uint8_t schedule[6*2*FRAME_TICKS];
            for (uint32_t t=0;t<sizeof schedule;++t)schedule[t]=order[t/(2*FRAME_TICKS)];
            uint32_t textures[4],tluts[4];
            for (unsigned k=0;k<4;++k) { textures[k]=BOMB_FRAMES+k*0x400u; tluts[k]=BOMB_TLUT; }
            Draw d;family_draw(p,&d,"bomb kart");
            mesh=sprite_frames(p,&d,schedule,sizeof schedule,4,32,32,0,0,textures,tluts,2,1,32,32,0,16,NULL);
        }
        const int32_t *centre=race[point];
        Mk64Loop l;memset(&l,0,sizeof l);
        const uint32_t frames=state==3?BOMB_BOUNCE_FRAMES:BOMB_CIRCLE_FRAMES;
        for (uint32_t n=0;n<frames;++n) {
            /* circleTimer steps before the move: frame n stands at step n + 1 */
            const uint32_t degrees=state==1?(360u-4u*((n+1)%90u))%360u:4u*((n+1)%90u);
            int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(degrees*65535u/360u),&sine,&cosine);
            int32_t bounce;portable_fixed_3d_sincos((uint16_t)((n%BOMB_BOUNCE_FRAMES)*0x13FFECu/360u),&bounce,&(int32_t){0});
            const int32_t at[3]={centre[0]+(state==3?0:(int32_t)((int64_t)mk64_metres(25)*cosine>>30)),
                                 centre[1]+(state==3?0:(int32_t)((int64_t)mk64_metres(25)*sine>>30)),
                                 centre[2]+mk64_metres(4)+mk64_metres(1)/2+(int32_t)((int64_t)mk64_metres(1)*bounce>>30)};
            loop_timed(course,&l,at,FRAME_TICKS);
        }
        const uint32_t route=loop_route(course,&l);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
        uint32_t count=1+route_properties(course,0,0);
        count+=hazard_properties(p,5,REACT_TUMBLE);
        count+=presence_property(course,PRESENT_ALL_CLASSES(3));
        object_row(course,0xE8,mesh,l.at[0],0,16384,route,props,count);
    }
}

/* Banshee Boardwalk Boos (update_boos, func_8007C7B4): two flocks of five
 * appear when the leading kart passes race path points 0xC9 and 0x1FF and
 * fly around it (each at a random offset, x -30..29, y -10..9, z -40..39,
 * on spline D_800E5D9C[i], 6.5 units over the kart's ground), until it
 * passes 0x119 or 0x26D. Here each flock flies its splines around its
 * trigger point. They are the 48x40 CI8 ghost frames (MIO0 at 0x712DC0,
 * gTextureGhosts) under d_course_banshee_boardwalk_boo_tlut at 0.15,
 * camera-facing and translucent (primAlpha between 80 and 120: 100 here),
 * frame 0 (the source turns them to their flight). No collision. */
#define BOO_FRAMES_ROM 0x712DC0u
#define BOO_TLUT 0x06005C80u
static const uint32_t boo_splines[5]={0xE6588u,0xE6644u,0xE6708u,0xE67D4u,0xE6890u};
static const uint16_t boo_flocks[2]={0xCD,0x203};
static void boo_family(Placer *p,const int32_t (*race)[3],uint32_t race_count)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    if (!race || race_count<=boo_flocks[1])ri_fail(ctx,"%s: Boos need race path 0",course->where);
    Mk64Segments segments=*p->segments;
    segments.segment[8]=ri_view(mk64_mio0(ctx,ri_span(ctx,ctx->rom,BOO_FRAMES_ROM,ctx->rom.size-BOO_FRAMES_ROM)));
    if (segments.segment[8].size<29u*48u*40u)ri_fail(ctx,"%s: Boo frames are %zu bytes",course->where,segments.segment[8].size);
    Draw d;family_draw(p,&d,"Boo");
    d.segments=&segments;
    sprite_texture(&d,2,1,48,40,0x08000000u,BOO_TLUT,0,16);
    d.tint=0x64FFFFFFu;d.blend=MKDS_MATERIAL_BLEND;
    sprite_quad(&d,48,40,0,0,0);
    const uint32_t mesh=family_mesh(p,&d);
    Mk64Random random={0xB00u};
    for (unsigned flock=0;flock<2;++flock) {
        const int32_t *point=race[boo_flocks[flock]];
        for (unsigned i=0;i<5;++i) {
            const int32_t x=(int32_t)random_below(&random,60)-30,y=(int32_t)random_below(&random,20)-10;
            const int32_t z=(int32_t)random_below(&random,80)-40;
            const int32_t origin[3]={(int32_t)((int64_t)point[0]*88/5>>16)+x,(int32_t)((int64_t)point[2]*88/5>>16)+7+y,
                                     (int32_t)((int64_t)point[1]*88/5>>16)+z};
            Mk64Loop l;memset(&l,0,sizeof l);
            spline_route(course,&l,ctx->rom,boo_splines[i],origin,NULL,2,NULL,NULL,0);
            const uint32_t route=loop_route(course,&l);
            const uint32_t props=course->tables[MKDS_T_PROP].count;
            mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
            const uint32_t count=1+route_properties(course,0,0);
            object_row(course,0xEA,mesh,l.at[0],0,9830,route,props,count);
        }
    }
}

/* Luigi Raceway's hot-air balloon (init_hot_air_balloon, func_80085534,
 * func_80055CCC): dl_F960 then dl_F650 at (-176, 0, -2323), turning
 * 0x100 a frame about up. It sinks from 300 to 18, then forever eases its
 * climb rate between 0, 1 and -1 by 0.05 a frame with 90-frame holds
 * (from about -23 to 88). It shows after the first lap and never in Time
 * Trial; here it shows from the start, bobbing. Not reproduced: the item
 * box it carries and the far billboard (dl_FBE0). */
#define BALLOON_TURN 0x100u
static uint32_t balloon_bob(int32_t *heights,uint32_t capacity)
{
    /* Q16 source units; returns the frames of one bob after it settles */
    int32_t y=300*65536,v=-2*65536,timer=-1;unsigned state=1,before=1;
    uint32_t count=0;int recording=0;
    for (uint32_t t=0;t<4000;++t) {
        #define EASE(to) (v<(to)?(v=v+3277>=(to)?(to):v+3277):v>(to)?(v=v-3277<=(to)?(to):v-3277):0)
        #define HOLD(n) (timer<0?(timer=(n),0):0, --timer<0?(timer=-1,1):0)
        switch (state) {
        case 1: if (y<=18*65536)state=2; break;
        case 2: EASE(0); if (!v)state=3; break;
        case 3: if (HOLD(1))state=4; break;
        case 4: EASE(65536); if (v==65536)state=5; break;
        case 5: if (HOLD(90))state=6; break;
        case 6: EASE(0); if (!v)state=7; break;
        case 7: EASE(-65536); if (v==-65536)state=8; break;
        case 8: if (HOLD(90))state=9; break;
        case 9: EASE(0); if (HOLD(90))state=3; break;
        }
        #undef EASE
        #undef HOLD
        y+=v;
        /* one bob runs from entering state 3 to entering it again */
        if (state==3 && before!=3) { if (recording)return count; recording=1; }
        before=state;
        if (recording) { if (count==capacity)return 0; heights[count++]=y; }
    }
    return 0;
}
static void balloon_family(Placer *p,const uint32_t lists[2],const int16_t at[3])
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    int32_t *heights=(int32_t *)(void *)ri_buf_zero(ctx,1024*sizeof *heights)->data;
    const uint32_t frames=balloon_bob(heights,1024);
    if (frames<100)ri_fail(ctx,"%s: the balloon does not settle",course->where);
    Draw d;family_draw(p,&d,"hot-air balloon");
    draw_list(&d,lists[0],0);draw_list(&d,lists[1],0);
    const uint32_t mesh=family_mesh(p,&d);
    RiBuf *poses=ri_buf(ctx);
    for (uint32_t t=0;t<frames*FRAME_TICKS;++t) {
        const int32_t m[12]={65536,0,0,0, 0,65536,0,0, 0,0,65536,(int32_t)((int64_t)heights[t/FRAME_TICKS]*5/88)};
        pose_put(ctx,poses,m);
    }
    uint8_t *nodes=ri_buf_zero(ctx,d.m.tables[MKDS_T_VERT].count+1u)->data;
    write_rig(&d,mesh,nodes,1,frames*FRAME_TICKS,poses,NULL,0);
    const int32_t place[3]={mk64_metres(at[0]),mk64_metres(at[2]),mk64_metres(at[1])};
    const uint32_t props=course->tables[MKDS_T_PROP].count;
    const int32_t up[3]={0,0,65536};
    mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"spin"),0);
    mkds_property_vector(course,"mksc:motion-axis",up);
    /* source yaw turns the other way about engine up */
    mkds_property(course,"mksc:spin-rate",MKDS_PROPERTY_INTEGER,(uint32_t)-(int32_t)((BALLOON_TURN<<16)/FRAME_TICKS),0);
    const uint32_t count=3+presence_property(course,PRESENT_NOT_TIME_TRIAL);
    object_row(course,0xEB,mesh,place,0,65536,MKDS_ABSENT,props,count);
}

/* Rainbow Road's neon signs (update_neon, render_object_neon): ten 64x64
 * CI8 signs drawn camera-facing on the hedgehog quad at 8x, cutout. Three
 * animate by palette (func_80085CA0, func_80085E38, func_80085F74 over the
 * object timer helpers func_80072E54 cycle, func_80072D3C alternate,
 * set_and_run_timer_object wait, func_80072B48 hide, func_80072C00 blink and
 * func_80072F88 count down), one palette of five 0x200-byte TLUTs a frame;
 * seven stand still with their own TLUT at D_800E6734's positions. */
typedef struct NeonState { int16_t state, index, timer, count, toggle; uint8_t started, cycling, hidden; } NeonState;
static void neon_next(NeonState *o) { o->started=0;o->cycling=0;++o->state; }
static void neon_cycle(NeonState *o,int16_t from,int16_t to,int16_t step,int16_t every,int16_t times)
{
    if (!o->cycling) { o->index=from;o->timer=every;o->count=times;o->started=1;o->cycling=1;return; }
    if (--o->timer>0)return;
    o->timer=every;o->index=(int16_t)(o->index+step);
    if (step>0?o->index<=to:o->index>=to)return;
    if (o->count>0)--o->count;
    if (o->count==0) { o->index=to;neon_next(o); } else o->index=from;
}
static void neon_alternate(NeonState *o,int16_t a,int16_t b,int16_t every,int16_t times,int blink)
{
    if (!o->cycling) { o->timer=every;if (!blink)o->index=a; else o->index=a;o->toggle=1;o->count=times;o->cycling=1;return; }
    if (--o->timer>=0)return;
    o->timer=every;--o->toggle;
    if (blink)o->hidden=!(o->toggle&1); else o->index=(o->toggle&1)?a:b;
    if (o->toggle>=0)return;
    o->toggle=1;
    if (o->count>0)--o->count;
    if (o->count==0) { o->cycling=0;neon_next(o); }
}
static void neon_wait(NeonState *o,int16_t frames,int hide)
{
    if (!o->started) { o->started=1;o->timer=frames;if (hide) { o->hidden=1;o->index=0; } }
    if (--o->timer<0) { if (hide)o->hidden=0; neon_next(o); }
}
/* one frame of sign `sign` (0 mushroom, 1 Mario, 2 Boo) */
static void neon_step(NeonState *o,unsigned sign)
{
    const int16_t s=o->state;
    if (s==1) { o->state=2;o->started=0;o->cycling=0;return; }
    if (sign==0)
        switch (s) {
        case 2: case 5: neon_cycle(o,0,4,1,12,5); break;
        case 3: neon_alternate(o,3,4,4,10,0); break;
        case 4: case 6: neon_wait(o,20,0); break;
        case 7: neon_alternate(o,3,4,0,20,0); break;
        default: o->state=2;o->started=0;o->cycling=0; break;
        }
    else if (sign==1)
        switch (s) {
        case 2: neon_cycle(o,0,4,1,12,1); break;
        case 3: neon_alternate(o,3,4,12,1,0); break;
        case 4: neon_wait(o,12,1); break;
        default: o->state=2;o->started=0;o->cycling=0; break;
        }
    else
        switch (s) {
        case 2: neon_cycle(o,0,4,1,5,1); break;
        case 3: case 5: neon_wait(o,30,0); break;
        case 4: neon_alternate(o,4,0,0,7,1); break;
        case 6: neon_cycle(o,3,0,-1,5,1); break;
        case 7: neon_wait(o,15,1); break;
        default: o->state=2;o->started=0;o->cycling=0; break;
        }
}
static const struct { int16_t at[3]; uint32_t texture, tlut; } neon_signs[3] = {
    {{-1431,827,-2957},0x06008000u,0x06005400u}, /* mushroom */
    {{799,1193,-5891},0x06009000u,0x06005E00u},  /* Mario */
    {{-2013,555,0},0x0600A000u,0x06006800u},     /* Boo */
};
#define NEON_STATIC_POSITIONS 0xE7334u /* D_800E6734: f32 x, y, z */
#define NEON_STATIC_TEXTURES 0x0600B000u
#define NEON_STATIC_TLUTS 0x06007200u
#define NEON_FRAMES 3000u
static void neon_row(Placer *p,uint32_t mesh,const int32_t source[3])
{
    MkdsModel *course=p->course;
    const int32_t at[3]={mk64_metres(source[0]),mk64_metres(source[2]),mk64_metres(source[1])};
    const uint32_t props=course->tables[MKDS_T_PROP].count;
    mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
    object_row(course,0xEC,mesh,at,0,8u*65536u,MKDS_ABSENT,props,1);
}
static void neon_family(Placer *p)
{
    MkdsModel *course=p->course;RiCtx *ctx=course->ctx;
    uint8_t *frames=ri_buf_zero(ctx,NEON_FRAMES)->data;
    for (unsigned sign=0;sign<3;++sign) {
        /* one loop: from one return to state 2 to the next */
        NeonState o;memset(&o,0,sizeof o);o.state=1;
        uint32_t first=0,last=0;
        for (uint32_t t=0;t<NEON_FRAMES && !last;++t) {
            const int16_t before=o.state;
            neon_step(&o,sign);
            frames[t]=o.hidden?255u:(uint8_t)o.index;
            if (before>2 && o.state==2) { if (first)last=t; else first=t; }
        }
        if (!last || frames[first+1]>4)ri_fail(ctx,"%s: neon sign %u does not loop",course->where,sign);
        const uint32_t ticks=(last-first)*FRAME_TICKS;
        uint8_t *schedule=ri_buf_zero(ctx,ticks)->data;
        for (uint32_t t=0;t<ticks;++t)schedule[t]=frames[first+1+t/FRAME_TICKS];
        uint32_t textures[5],tluts[5];
        for (unsigned k=0;k<5;++k) { textures[k]=neon_signs[sign].texture; tluts[k]=neon_signs[sign].tlut+k*0x200u; }
        Draw d;family_draw(p,&d,"neon sign");
        const uint32_t mesh=sprite_frames(p,&d,schedule,ticks,5,64,64,0,0,textures,tluts,2,1,64,64,0,16,NULL);
        const int32_t at[3]={neon_signs[sign].at[0],neon_signs[sign].at[1],neon_signs[sign].at[2]};
        neon_row(p,mesh,at);
    }
    for (unsigned n=0;n<7;++n) {
        const uint8_t *row=ri_at(ctx,ctx->rom,NEON_STATIC_POSITIONS+n*12,12);
        int32_t at[3];
        for (unsigned k=0;k<3;++k) {
            const uint32_t bits=mk64_be32(row+k*4);
            float value;memcpy(&value,&bits,sizeof value);
            if (!(value>-32768.0f && value<32768.0f))ri_fail(ctx,"%s: neon sign %u position",course->where,n+3);
            at[k]=(int32_t)value;
        }
        Draw d;family_draw(p,&d,"neon sign");
        sprite_texture(&d,2,1,64,64,NEON_STATIC_TEXTURES+n*0x1000u,NEON_STATIC_TLUTS+n*0x200u,0,16);
        sprite_quad(&d,64,64,0,0,0);
        neon_row(p,family_mesh(p,&d),at);
    }
}

/* Train and ferry smoke (update_train_smoke, update_ferries_smoke): a puff
 * (I8 0x0D029458) on the 32-unit quad leaves
 * the locomotive's chimney (0, 65, 25) every 5 frames, grey, and the
 * paddle boat's stacks (+-30, 180, 45) every 10 frames, white; each rises
 * 1.1 units a frame slowing by 0.03, grows from 0.5 to 1.0 and fades 7 of
 * 255 alpha a frame for 35 frames. As billboards: the puff of age a rides
 * the vehicle's route a frames behind the stack, at age a's height, size and
 * alpha, which matches the trail the source leaves at every spawn. */
#define SMOKE_TEXTURE 0x0D029458u
#define SMOKE_LIFE 35u
typedef struct Mk64Smoke { uint8_t actor, every, sides; int16_t side, up, ahead; uint8_t grey; } Mk64Smoke;
static const Mk64Smoke smokes[] = {
    {0x0E,5,1,0,65,25,80},    /* locomotive */
    {0x25,10,2,30,180,45,230}, /* paddle boat */
};
static void smoke_trail(Placer *p,const Mk64Smoke *s,const VehicleRoute *r,uint32_t phase)
{
    MkdsModel *course=p->course;
    uint32_t total=0;
    for (uint32_t i=0;i<r->count;++i)total+=r->ticks[i];
    /* source units a frame along the route */
    const int64_t speed=(int64_t)r->speed; /* Q8 */
    const int64_t ahead=speed?(int64_t)s->ahead*256*FRAME_TICKS/speed:0; /* ticks */
    for (uint32_t age=0;age<SMOKE_LIFE;age+=s->every) {
        Draw d;family_draw(p,&d,"smoke");
        sprite_texture(&d,4,1,32,32,SMOKE_TEXTURE,0,0,16);
        const uint32_t alpha=255u-7u*age;
        d.tint=alpha<<24|(uint32_t)s->grey*0x010101u;d.blend=MKDS_MATERIAL_BLEND;
        /* height: 1.1 a - 0.015 a (a - 1); size 0.5 to 1.0 of 32 units */
        const int32_t rise=(int32_t)((110*age-(3*age*(age-1))/2)/100);
        const int32_t size=(int32_t)(16+16*age/SMOKE_LIFE);
        for (unsigned side=0;side<s->sides;++side)
            sprite_quad(&d,size,size,s->sides>1?(side?s->side:-s->side):0,s->up+rise,0);
        const uint32_t mesh=family_mesh(p,&d);
        const int64_t lag=(int64_t)phase+ahead-(int64_t)age*FRAME_TICKS;
        const uint32_t at=(uint32_t)(((lag%(int64_t)total)+(int64_t)total)%(int64_t)total);
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
        const uint32_t count=1+route_properties(course,at,0);
        object_row(course,0xED,mesh,r->path[0],0,65536,r->id,props,count);
    }
}

/* Bowser's Castle fire breath (code_8006E9C0.c gFireBreathsSpawns and the
 * fire objects; orange translucent I8 puffs 0x0D02BC58 on the 32-unit quad):
 * the big statue at (-68, 80, -1840) breathes 20 puffs at yaw 0x2100, 8
 * units a frame, pitch 0xC00 for 15 frames then 0x400 lower a frame,
 * growing 0.1 a frame from 1; the four small statues (gFireBreathsSpawns)
 * breathe 10 puffs at yaw 0 or 0x8000, 4 a frame, pitch held 3 frames,
 * from 0.5 to 1. Puff i leaves 2i frames after the first and lives 23
 * frames; the statues fire at frames 0, 90 and 180 of a 480-frame cycle.
 * Visual only (no collision). One mesh and route per statue size; each
 * puff is a placement whose motion phase delays it. */
#define FIRE_TEXTURE 0x0D02BC58u
#define FIRE_SPAWNS 0xE6328u
#define FIRE_CYCLE 480u
#define FIRE_LIFE 23u
typedef struct Mk64Fire { uint8_t puffs, speed, hold; uint32_t scale0, grow; /* Q16, grow a frame */ } Mk64Fire;
static const Mk64Fire fire_kinds[2]={{20,8,15,65536,6554},{10,4,3,32768,1638}};
static const uint16_t fire_shots[3]={0,90,180};
static uint32_t fire_mesh(Placer *p,const Mk64Fire *k)
{
    RiCtx *ctx=p->course->ctx;
    Draw d;family_draw(p,&d,"fire breath");
    sprite_texture(&d,4,1,32,32,FIRE_TEXTURE,0,0,16);
    d.tint=0xC0FF9020u;d.blend=MKDS_MATERIAL_BLEND;
    sprite_quad(&d,32,32,0,0,0);
    const uint32_t mesh=family_mesh(p,&d);
    RiBuf *poses=ri_buf(ctx);
    const int32_t pivot[3]={0,0,0};
    for (uint32_t t=0;t<FIRE_CYCLE*FRAME_TICKS;++t) {
        const uint32_t f=t/FRAME_TICKS;int32_t scale=0;
        for (unsigned s=0;s<3;++s)
            if (f>=fire_shots[s] && f<fire_shots[s]+FIRE_LIFE) {
                const int64_t grown=(int64_t)k->scale0+(int64_t)k->grow*(f-fire_shots[s]);
                scale=(int32_t)(k->scale0<65536 && grown>65536?65536:grown);
            }
        pose_scale(ctx,poses,scale,pivot);
    }
    uint8_t *nodes=ri_buf_zero(ctx,d.m.tables[MKDS_T_VERT].count+1u)->data;
    write_rig(&d,mesh,nodes,1,FIRE_CYCLE*FRAME_TICKS,poses,NULL,0);
    return mesh;
}
static void fire_statue(Placer *p,const Mk64Fire *k,uint32_t mesh,const int32_t mouth[3],uint16_t yaw)
{
    MkdsModel *course=p->course;
    Mk64Loop l;memset(&l,0,sizeof l);
    const int32_t home[3]={mk64_metres(mouth[0]),mk64_metres(mouth[2]),mk64_metres(mouth[1])};
    int32_t ys,yc;portable_fixed_3d_sincos(yaw,&ys,&yc);
    for (unsigned s=0;s<3;++s) {
        const uint32_t next=s<2?fire_shots[s+1]:FIRE_CYCLE+fire_shots[0];
        int64_t at[3]={(int64_t)mouth[0]<<16,(int64_t)mouth[1]<<16,(int64_t)mouth[2]<<16};
        uint16_t pitch=0xC00;
        for (uint32_t a=0;a<FIRE_LIFE;++a) {
            const int32_t point[3]={(int32_t)((at[0]>>16)*65536*5/88),(int32_t)((at[2]>>16)*65536*5/88),
                                    (int32_t)((at[1]>>16)*65536*5/88)};
            loop_timed(course,&l,point,FRAME_TICKS);
            if (a>=k->hold)pitch=(uint16_t)(pitch-0x400u);
            int32_t ps,pc;portable_fixed_3d_sincos(pitch,&ps,&pc);
            const int64_t v=(int64_t)k->speed<<16;
            at[0]+=v*((int64_t)pc*ys>>30)>>30;at[1]+=v*ps>>30;at[2]+=v*((int64_t)pc*yc>>30)>>30;
        }
        loop_timed(course,&l,home,(next-fire_shots[s]-FIRE_LIFE)*FRAME_TICKS);
    }
    const uint32_t route=loop_route(course,&l);
    for (uint32_t i=0;i<k->puffs;++i) {
        const uint32_t props=course->tables[MKDS_T_PROP].count;
        mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0);
        /* puff i runs 2i frames behind puff 0 */
        const uint32_t phase=(FIRE_CYCLE*FRAME_TICKS-2u*i*FRAME_TICKS)%(FIRE_CYCLE*FRAME_TICKS);
        const uint32_t count=1+route_properties(course,phase,0);
        object_row(course,0xEE,mesh,home,0,65536,route,props,count);
    }
}
static void fire_family(Placer *p)
{
    RiCtx *ctx=p->course->ctx;
    const int32_t big[3]={-68,80,-1840};
    fire_statue(p,&fire_kinds[0],fire_mesh(p,&fire_kinds[0]),big,0x2100);
    const uint32_t small=fire_mesh(p,&fire_kinds[1]);
    for (unsigned n=0;n<4;++n) {
        const uint8_t *row=ri_at(ctx,ctx->rom,FIRE_SPAWNS+n*6,6);
        const int32_t mouth[3]={mk64_i16(row),mk64_i16(row+2),mk64_i16(row+4)};
        fire_statue(p,&fire_kinds[1],small,mouth,n%2?0x8000:0);
    }
}

/* turns<<32 per tick for whole degrees a tick */
#define DEGREE_RATE 11930465

void mk64_objects(MkdsModel *course,const Mk64Segments *segments,uint32_t index,Mk64Floor floor,Mk64Floor surface,
                  void *context,
                  const int32_t (*race)[3],uint32_t race_count)
{
    RiCtx *ctx=course->ctx;
    uint32_t meshes=0,hazards=0;
    for (size_t family=0;family<sizeof prop_families/sizeof prop_families[0];++family) {
        const Mk64Prop *prop=&prop_families[family];
        if (prop->course!=index)continue;
        if (meshes==256)ri_fail(ctx,"%s: more than 256 scenery meshes",course->where);
        Draw d;memset(&d,0,sizeof d);
        char where[96];snprintf(where,sizeof where,"%s actor 0x%02X",course->where,(unsigned)prop->actor);
        mkds_model_init(&d.m,ctx,where,0,NULL,0,0);
        d.segments=segments;d.textured=1;d.width=d.height=32;
        const Mk64Flipbook *book=prop->texture?flipbook(index,prop->actor):NULL;
        if (prop->texture) {
            d.texture=prop->texture+(book?book->first*book->stride:0u); d.fmt=2; d.siz=1; d.width=32; d.height=64;
        }
        d.tlut=prop->tlut;d.alpha=prop->edge?16u:0u;
        for (unsigned i=0;i<2 && prop->lists[i];++i)draw_list(&d,prop->lists[i],0);
        RiBuf *swaps=ri_buf(ctx);
        const uint32_t swap_count=book?flipbook_swaps(&d,book,prop->texture,swaps):0u;
        const uint32_t mesh_id=MK64_OBJECT_MESH_ID(index,meshes);
        write_mesh(&d,mesh_id,MK64_OBJECT_MATERIAL_ID(index,meshes,0));
        if (swap_count)write_rig(&d,mesh_id,NULL,0,0,NULL,swaps,swap_count);
        ++meshes;
        for (uint32_t entry=0;;++entry) {
            int16_t source[3];
            if (prop->spawns) {
                if (entry==512)ri_fail(ctx,"%s: unterminated spawn list",where);
                const uint8_t *p=ri_at(ctx,segments->segment[6],prop->spawns+entry*prop->stride,prop->stride);
                if (mk64_be16(p)==0x8000u)break;
                /* func_80298D10 keeps UnkActorSpawnData ids in the low nibble */
                const uint32_t id=mk64_be16(p+6)&(prop->stride==10?15u:255u);
                if (prop->id>=0 && (int32_t)id!=prop->id)continue;
                for (unsigned axis=0;axis<3;++axis)source[axis]=(int16_t)mk64_i16(p+axis*2);
            } else if (entry) break;
            else memcpy(source,prop->at,sizeof source);
            /* spawn_foliage: an actor buried under the floor rises onto it. */
            int32_t at[3]={mk64_metres(source[0]),mk64_metres(source[2]),mk64_metres(source[1])},ground;
            if (prop->spawns && floor(context,at,&ground) && ground>at[2])at[2]=ground;
            const uint32_t first=course->tables[MKDS_T_PROP].count;
            uint32_t count=0;
            if (prop->billboard) { mkds_property(course,"mksc:billboard",MKDS_PROPERTY_BOOLEAN,1,0); ++count; }
            if (prop->spin) {
                const int32_t up[3]={0,0,65536};
                mkds_property(course,"mksc:motion",MKDS_PROPERTY_STRING,mkds_string(course,"spin"),0);
                mkds_property_vector(course,"mksc:motion-axis",up);
                mkds_property(course,"mksc:spin-rate",MKDS_PROPERTY_INTEGER,(uint32_t)(prop->spin*DEGREE_RATE/FRAME_TICKS),0);
                count+=3;
            }
            if (prop->radius) {
                if (++hazards>64)ri_fail(ctx,"%s: more than 64 collidable actors",course->where);
                mkds_property(course,"mksc:hazard-radius",MKDS_PROPERTY_FIXED,(uint32_t)mk64_metres(prop->radius),0);
                count+=1+hazard_reaction(course,prop->reaction);
            }
            /* Source yaw about up turns the other way in engine axes. */
            int32_t sine,cosine;portable_fixed_3d_sincos((uint16_t)(-(int32_t)prop->yaw/2),&sine,&cosine);
            const uint32_t row[16]={0x4E360000u|prop->actor,MKDS_KIND_MESH,mesh_id,(uint32_t)at[0],(uint32_t)at[1],
                (uint32_t)at[2],0,0,(uint32_t)sine,(uint32_t)cosine,65536,65536,65536,MKDS_ABSENT,first,count};
            mkds_words(course,MKDS_T_OBJS,row,16);
        }
    }
    Placer placer={course,segments,index,&meshes,&hazards,{0},{{0}},0,NULL,0};
    for (size_t i=0;i<sizeof models/sizeof models[0];++i)placer.mesh[i]=MKDS_ABSENT;
    for (size_t family=0;family<sizeof convoys/sizeof convoys[0];++family) {
        const Mk64Convoy *convoy=&convoys[family];
        if (convoy->course!=index)continue;
        char where[96];snprintf(where,sizeof where,"%s convoy %u",course->where,(unsigned)family);
        int32_t (*flat)[2]=(int32_t (*)[2])(void *)ri_buf_zero(ctx,PATH_LIMIT*sizeof *flat)->data;
        const uint32_t length=path_2d(ctx,segments->segment[6],convoy->path,flat,where);
        if (length<64)ri_fail(ctx,"%s: vehicle path too short",where);
        /* get_surface_height(x, 2000, z) under the first point */
        int32_t probe[3]={mk64_metres(flat[0][0]),mk64_metres(flat[0][1]),mk64_metres(2000)},ground=mk64_metres(convoy->height);
        if (convoy->floor && !floor(context,probe,&ground))ri_fail(ctx,"%s: no floor under the vehicle path",where);
        int32_t (*path)[3]=(int32_t (*)[3])(void *)ri_buf_zero(ctx,(size_t)length*sizeof *path)->data;
        for (uint32_t i=0;i<length;++i) {
            path[i][0]=mk64_metres(flat[i][0]);path[i][1]=mk64_metres(flat[i][1]);path[i][2]=ground;
        }
        for (uint32_t train=0;train<convoy->count;++train) {
            uint32_t at=(train*length/convoy->count+convoy->first)%length;
            for (unsigned car=0;car<convoy->cars_count;++car) {
                at=(at+convoy->cars[car].gap)%length;
                place(&placer,convoy->cars[car].model,(const int32_t (*)[3])path,length,at,convoy->speed,0);
                for (size_t k=0;k<sizeof smokes/sizeof smokes[0];++k)
                    if (smokes[k].actor==models[convoy->cars[car].model].actor)
                        smoke_trail(&placer,&smokes[k],placer.last,placer.last_phase);
            }
        }
        if (models[convoy->cars[convoy->cars_count-1u].model].actor==models[LOCOMOTIVE].actor)
            crossing_family(&placer,placer.last,models[LOCOMOTIVE].actor);
    }
    for (size_t family=0;family<sizeof walkers/sizeof walkers[0];++family)
        if (walkers[family].course==index)walker_family(&placer,&walkers[family],floor,context);
    for (size_t family=0;family<sizeof orbiters/sizeof orbiters[0];++family)
        if (orbiters[family].course==index)orbiter_family(&placer,&orbiters[family],floor,context);
    for (size_t family=0;family<sizeof thwomps/sizeof thwomps[0];++family)
        if (thwomps[family].course==index)thwomp_family(&placer,&thwomps[family]);
    for (size_t family=0;family<sizeof chomps/sizeof chomps[0];++family)
        if (chomps[family].course==index)chomp_family(&placer,&chomps[family],race,race_count);
    for (size_t family=0;family<sizeof flags/sizeof flags[0];++family)
        if (flags[family].course==index)flag_family(&placer,&flags[family]);
    if (index==12)penguin_family(&placer,floor,context);
    for (size_t family=0;family<sizeof gulls/sizeof gulls[0];++family)
        if (gulls[family].course==index)gull_family(&placer,&gulls[family]);
    for (size_t family=0;family<sizeof snowmen/sizeof snowmen[0];++family)
        if (snowmen[family].course==index)snowman_family(&placer,&snowmen[family]);
    for (size_t family=0;family<sizeof moles/sizeof moles[0];++family)
        if (moles[family].course==index)mole_family(&placer,&moles[family]);
    for (size_t family=0;family<sizeof rocks/sizeof rocks[0];++family)
        if (rocks[family].course==index)rock_family(&placer,&rocks[family],surface,context);
    bomb_family(&placer,race,race_count);
    if (index==3)boo_family(&placer,race,race_count);
    if (index==13)neon_family(&placer);
    if (index==2)fire_family(&placer);
    if (index==8) {
        static const uint32_t balloon[2]={0x0600F960u,0x0600F650u};
        static const int16_t balloon_at[3]={-176,0,-2323};
        balloon_family(&placer,balloon,balloon_at);
    }
    /* set_track_offset_position: lanes sit along (-dz, dx) of each leg;
     * every traffic family on a course drives the same three lanes. */
    int32_t (*lanes[3])[3]={NULL,NULL,NULL},(*backwards[3])[3]={NULL,NULL,NULL};int16_t lane_offset=0;
    for (size_t family=0;family<sizeof traffic/sizeof traffic[0];++family) {
        const Mk64Traffic *flow=&traffic[family];
        if (flow->course!=index)continue;
        if (!race || race_count<64)ri_fail(ctx,"%s: traffic needs race path 0",course->where);
        if (lanes[0] && flow->lane!=lane_offset)ri_fail(ctx,"%s: traffic families disagree on lanes",course->where);
        for (unsigned lane=0,fresh=!lanes[0];lane<3 && fresh;++lane) {
            lanes[lane]=(int32_t (*)[3])(void *)ri_buf_zero(ctx,(size_t)race_count*sizeof *lanes[lane])->data;
            const int64_t shift=(int64_t)mk64_metres(flow->lane)*((int64_t)lane-1);
            for (uint32_t i=0;i<race_count;++i) {
                const int32_t *a=race[i],*b=race[(i+1)%race_count];
                int64_t d[3]={(int64_t)b[0]-a[0],(int64_t)b[1]-a[1],0};int32_t u[3]={0,0,0};
                portable_fixed_3d_normalize(d,u);
                lanes[lane][i][0]=a[0]-(int32_t)(shift*u[1]>>30);
                lanes[lane][i][1]=a[1]+(int32_t)(shift*u[0]>>30);
                lanes[lane][i][2]=a[2];
            }
        }
        lane_offset=flow->lane;
        if (!backwards[0])
            for (unsigned lane=0;lane<3;++lane)backwards[lane]=reversed_path(ctx,(const int32_t (*)[3])lanes[lane],race_count,0);
        /* the classes' sets strike apart: count the largest */
        const uint32_t before=hazards;uint32_t most=hazards;
        for (unsigned cc=0;cc<4;++cc) {
            hazards=before;
            const uint32_t presence=cc==1?PRESENT_CLASS(cc)|PRESENT_ALL_CLASSES(1):PRESENT_CLASS(cc);
            for (uint32_t i=0;i<flow->count;++i) {
                const uint32_t at=(i*race_count/flow->count+flow->first)%race_count,lane=i%3;
                place(&placer,(unsigned)(flow->model+i%flow->liveries),
                      (const int32_t (*)[3])(cc==3?backwards[lane]:lanes[lane]),race_count,
                      cc==3?(race_count-at)%race_count:at,traffic_speed[cc][lane==2?0:1],presence);
            }
            if (hazards>most)most=hazards;
        }
        hazards=most;
    }
}

/* Item art (src/actors/{green_shell,blue_and_red_shells,banana,fake_item_box,
 * item_box}/render.inc.c): segment-13 display lists drawn as each render
 * function draws them. Shells are 32x32 CI8 frames the game DMAs into
 * segment 3 (init_actors_and_load_textures: eight green, then four blue,
 * 0x400 bytes each) under the green or blue TLUT; red is the green TLUT with
 * red and green swapped (init_red_shell_texture), here segment 4. */
enum { ITEM_SHELL_FRAMES=0x4CBE0u, ITEM_SHELL_GREEN_FRAMES=8, ITEM_SHELL_BLUE_FRAMES=4, ITEM_SHELL_FRAME=0x400u };
typedef struct Mk64ItemArt { uint8_t slot, flags; uint32_t texture, tlut; uint32_t lists[2]; } Mk64ItemArt;
enum { ART_BILLBOARD=1, ART_SPIN=2, ART_BOX=32, ART_SHARD=33 };
static const Mk64ItemArt item_art[] = {
    {0x00,ART_BILLBOARD,0x03000000,0x0D004E38,{0x0D005338}},  /* green shell, frame 0 */
    {0x02,ART_BILLBOARD,0x03000000,0x04000000,{0x0D005338}},  /* red shell */
    {0x04,ART_BILLBOARD,0x03002000,0x0D005038,{0x0D005338}},  /* blue spiny shell */
    {0x05,ART_BILLBOARD,0,0,{0x0D004B48}},                    /* common_model_banana */
    {0x10,ART_SPIN,0,0,{0x0D002F80,0x0D003090}},              /* fake box: inverted mark, box */
    {ART_BOX,ART_SPIN,0,0,{0x0D003090,0x0D003008}},           /* item box and its question mark */
    {ART_SHARD,0,0,0,{0x0D003158}},                           /* one shattered piece */
};
/* Item window pictures (segment 13): 40x32 CI8 at 0x203D8 + n * 0x500 under
 * the TLUT at 0x1E0D8 + n * 0x200, n in enum ITEMS order from the banana. */
enum { ITEM_WINDOW_TEXTURE=0x0D0203D8u, ITEM_WINDOW_TLUT=0x0D01E0D8u, ITEM_WINDOW_W=40, ITEM_WINDOW_H=32, ITEM_ICON=40 };
static const struct { uint8_t item, window; } item_icons[] = {
    {0x00,8},{0x01,9},{0x02,10},{0x03,11},{0x04,6},{0x05,0},{0x07,2},{0x08,4},
    {0x09,5},{0x0A,12},{0x0B,7},{0x0C,13},{0x0F,3},{0x10,14},{0x15,1},
};
#define MK64_ITEM_MESH_ID(slot) (0x4E490000u | (slot))
#define MK64_ITEM_MATERIAL_ID(slot) (0x4E4A0000u | (slot) << 4)

uint32_t mk64_item_art(RiCtx *ctx,const uint32_t *profiles,size_t profile_count,RiBuf *bindings)
{
    Mk64Segments segments={{{0}}};
    segments.segment[13]=ri_view(mk64_mio0(ctx,ri_span(ctx,ctx->rom,MK64_COMMON_DATA,MK64_COMMON_DATA_END-MK64_COMMON_DATA)));
    RiBuf *frames=ri_buf(ctx);
    size_t at=MK64_TEXTURE_BASE+ITEM_SHELL_FRAMES;
    for (unsigned i=0;i<ITEM_SHELL_GREEN_FRAMES+ITEM_SHELL_BLUE_FRAMES;++i,at=mk64_mio0_next(ctx,at)) {
        RiBuf *frame=mk64_mio0(ctx,ri_span(ctx,ctx->rom,at,ctx->rom.size-at));
        if (frame->size!=ITEM_SHELL_FRAME)ri_fail(ctx,"MK64: shell frame %u is %zu bytes, not 0x400",i,frame->size);
        ri_put_buf(ctx,frames,frame);ri_buf_free(ctx,frame);
    }
    segments.segment[3]=ri_view(frames);
    const uint8_t *green=ri_at(ctx,segments.segment[13],0x4E38,512);
    RiBuf *red=ri_buf_zero(ctx,512);
    for (unsigned i=0;i<256;++i) {
        const uint32_t v=mk64_be16(green+i*2);
        const uint32_t swapped=((v&0xF800u)>>5)|((v&0x07C0u)<<5)|(v&0x3Eu)|(v&1u);
        red->data[i*2]=(uint8_t)(swapped>>8);red->data[i*2+1]=(uint8_t)swapped;
    }
    segments.segment[4]=ri_view(red);
    uint32_t rows=0;
    for (size_t i=0;i<sizeof item_art/sizeof item_art[0];++i) {
        const Mk64ItemArt *art=&item_art[i];
        Draw d;memset(&d,0,sizeof d);
        char where[64];snprintf(where,sizeof where,"MK64 item art slot %u",(unsigned)art->slot);
        mkds_model_init(&d.m,ctx,where,0,NULL,0,0);
        d.segments=&segments;d.width=d.height=32;
        if (art->texture) { d.textured=1;d.texture=art->texture;d.tlut=art->tlut;d.fmt=2;d.siz=1;d.cms=d.cmt=2;d.alpha=16; }
        for (unsigned k=0;k<2 && art->lists[k];++k)draw_list(&d,art->lists[k],0);
        const uint32_t mesh_id=MK64_ITEM_MESH_ID(art->slot);
        write_mesh_shaded(&d,mesh_id,MK64_ITEM_MATERIAL_ID(art->slot),art->slot>=ART_BOX||art->slot==0x10?MKDS_MATERIAL_BLEND:0u,1);
        /* Boxes rest on their spawn point: the pivot is the lowest vertex. */
        int32_t lowest=0;
        const MkdsTable *vert=&d.m.tables[MKDS_T_VERT];
        for (uint32_t v=0;v<vert->count;++v) {
            const int32_t z=(int32_t)ri_le32(vert->bytes->data+v*32+8);
            lowest=v==0||z<lowest?z:lowest;
        }
        if (!(art->flags&ART_BILLBOARD) && art->slot!=ART_BOX && art->slot!=0x10)lowest=0;
        for (size_t profile=0;profile<profile_count;++profile,++rows)
            ri_words(ctx,bindings,11,3u,profiles[profile],(uint32_t)art->slot,(uint32_t)art->flags,mesh_id,
                     65536u,65536u,65536u,0u,0u,(uint32_t)lowest);
    }
    /* HUD icons: one textured quad per item window picture. */
    for (size_t i=0;i<sizeof item_icons/sizeof item_icons[0];++i) {
        const uint32_t slot=ITEM_ICON+item_icons[i].item;
        Draw d;memset(&d,0,sizeof d);
        char where[64];snprintf(where,sizeof where,"MK64 item window %u",(unsigned)item_icons[i].window);
        mkds_model_init(&d.m,ctx,where,0,NULL,0,0);
        d.segments=&segments;d.textured=1;d.fmt=2;d.siz=1;d.width=ITEM_WINDOW_W;d.height=ITEM_WINDOW_H;
        d.cms=d.cmt=2;d.alpha=16;
        d.texture=ITEM_WINDOW_TEXTURE+item_icons[i].window*0x500u;d.tlut=ITEM_WINDOW_TLUT+item_icons[i].window*0x200u;
        static const int16_t corner[4][2]={{0,0},{ITEM_WINDOW_W,0},{0,ITEM_WINDOW_H},{ITEM_WINDOW_W,ITEM_WINDOW_H}};
        for (unsigned k=0;k<4;++k) {
            uint8_t *v=d.vertex[k];memset(v,0,16);
            v[0]=(uint8_t)((uint16_t)corner[k][0]>>8);v[1]=(uint8_t)corner[k][0];
            v[2]=(uint8_t)((uint16_t)(ITEM_WINDOW_H-corner[k][1])>>8);v[3]=(uint8_t)(ITEM_WINDOW_H-corner[k][1]);
            const uint16_t s=(uint16_t)(corner[k][0]<<5),t=(uint16_t)(corner[k][1]<<5);
            v[8]=(uint8_t)(s>>8);v[9]=(uint8_t)s;v[10]=(uint8_t)(t>>8);v[11]=(uint8_t)t;
            memset(v+12,255,4);d.loaded[k]=1;
        }
        draw_triangle(&d,0,4,2);draw_triangle(&d,2,4,6);
        const uint32_t mesh_id=MK64_ITEM_MESH_ID(slot);
        write_mesh_shaded(&d,mesh_id,MK64_ITEM_MATERIAL_ID(slot),0,0);
        for (size_t profile=0;profile<profile_count;++profile,++rows)
            ri_words(ctx,bindings,11,3u,profiles[profile],slot,0u,mesh_id,65536u,65536u,65536u,0u,0u,0u);
    }
    return rows;
}
