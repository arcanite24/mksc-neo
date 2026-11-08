#include "mk64_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* US ROM layout facts, not replacement game content. Course order is
 * n64decomp/mk64 courses/courseTable.c; collision entries are selected by
 * src/racing/render_courses.c (58cfcb022e10f83bc3b889d7e97508cae6837098).
 * Offsets: https://github.com/micro500/mariokart64/wiki/Surface-Map
 * Header/path tables: that wiki's Level-Data-in-ROM and Course-Paths.
 * The opaque field marks courses whose renderer sets only G_RM_AA_ZB_OPA_SURF
 * before their static lists; grid_on_axis, the one course whose GP grid
 * spawn_players.c centres on x = 0; then the water level (fall height,
 * source units) render_courses.c sets where collision.c func_802AAB4C uses
 * it everywhere (regional levels: Bowser, Koopa, Sherbet, DK); last, the
 * item box spawn list actors.c spawn_course_actors names. Every
 * vertex, texture, surface entry and path point is read from the ROM. */
static const Mk64CourseSource courses[] = {
    {"N64 Mario Raceway",0,1,3,0x9650,{0},0,0,0,0,0,0,0x9498,1,0,0,0},
    {"N64 Choco Mountain",1,1,2,0x72D0,{0},0,0,0,0,1,-80,0x7250,1,0,0,0},
    {"N64 Bowser's Castle",2,2,3,0x93D8,{0},0,0,0,0,0,0,0x9370,1,0,0,0},
    {"N64 Banshee Boardwalk",3,3,2,0xB458,{0},0,0,0,0,1,-80,0xB3D0,1,0,0,0},
    {"N64 Yoshi Valley",4,3,1,0x18240,{0},0,0,1,0,0,0,0x18110,1,0,0,0},
    {"N64 Frappe Snowland",5,1,1,0x79A0,{0},0,0,1,0,1,-50,0x7810,1,0,0,0},
    {"N64 Koopa Troopa Beach",6,0,2,0x18FD8,{0},0,0,0,0,0,0,0x18E78,1,0,0,0},
    {"N64 Royal Raceway",7,2,2,0xDC28,{0},0,0,0,0,1,-60,0xDB80,1,0,0,0},
    {"N64 Luigi Raceway",8,0,0,0xFF28,{0},0,0,0,0,0,0,0xFDE8,1,0,0,0},
    {"N64 Moo Moo Farm",9,0,1,0x144B8,{0},0,0,0,0,0,0,0x143E0,1,0,0,0},
    {"N64 Toad's Turnpike",10,1,0,0x23B68,{0},0,0,0,1,0,0,0x23AE0,1,0,0,0},
    {"N64 Kalimari Desert",11,0,3,0x23070,{0},0,0,0,0,0,0,0x22E88,1,0,0,0},
    {"N64 Sherbet Land",12,2,1,0x9C20,{0},0,0,1,0,0,0,0x9B80,1,0,0,0},
    {"N64 Rainbow Road",13,3,3,0x16440,{0},0,0,1,0,1,0,0x16338,1,0,0,0},
    {"N64 Wario Stadium",14,2,0,0xCC38,{0},0,0,0,0,0,0,0xCB40,1,0,0,0},
    {"N64 Block Fort",15,4,0,0,{0x070015C0},1,1,0,0,0,0,0x38,1,200,5,0},
    {"N64 Skyscraper",16,4,1,0,{0x07001110,0x07000258},1,1,0,0,0,0,0x80,1,400,480,0},
    {"N64 Double Deck",17,4,2,0,{0x07000738},1,1,0,0,0,0,0x28,1,160,55,0},
    {"N64 DK's Jungle Parkway",18,3,0,0x14338,{0},0,0,0,0,0,0,0x13EC0,1,0,0,0},
    {"N64 Big Donut",19,4,3,0,{0x07001018,0x07000450,0x07000AC0,0x07000B58,0x07000230},1,6,1,0,0,0,0x58,1,575,200,1},
};

static int verify_canonical(RiData rom,char *error,size_t capacity)
{
    const char *reason=NULL;
    if (memcmp(rom.data+0x3B,"NKT",3) || rom.data[0x3E]!='E' || rom.data[0x3F]!=0)
        reason="Mario Kart 64: only the US NKTE revision 0 cartridge is supported";
    else {
        uint8_t digest[20];char hex[41];mksc_sha1(rom.data,rom.size,digest);mksc_hex(digest,20,hex);
        if (strcmp(hex,MKSC_MK64_SHA1))reason="Mario Kart 64: US ROM SHA-1 mismatch (modified or corrupt dump)";
    }
    if (error && capacity)snprintf(error,capacity,"%s",reason?reason:"");
    return reason==NULL;
}
static void identify(RiCtx *ctx)
{
    if (ctx->rom.size!=MKSC_MK64_ROM_BYTES)
        ri_fail(ctx,"Mario Kart 64: expected a 12 MiB US cartridge (.z64/.v64/.n64)");
    ctx->rom=ri_view(mk64_normalize(ctx,ctx->rom));
    char error[256];
    if (!verify_canonical(ctx->rom,error,sizeof error))ri_fail(ctx,"%s",error);
}

int mksc_mk64_identify(const uint8_t *rom,size_t size,char *error,size_t capacity)
{
    const int order=mksc_mk64_byte_order(rom,size);
    if (size!=MKSC_MK64_ROM_BYTES || !order) {
        if (error && capacity)snprintf(error,capacity,"Mario Kart 64: expected a 12 MiB N64 cartridge (.z64/.v64/.n64)");
        return 0;
    }
    uint8_t *canonical=malloc(size);
    if (!canonical) {
        if (error && capacity)snprintf(error,capacity,"Mario Kart 64: out of memory");
        return 0;
    }
    const size_t mask=order==2?1u:order==3?3u:0u;
    for(size_t i=0;i<size;++i)canonical[i]=rom[i^mask];
    const int result=verify_canonical((RiData){canonical,size},error,capacity);
    free(canonical);return result;
}

static void convert_one(RiCtx *ctx,void *source)
{
    mk64_course_convert(ctx,(const Mk64CourseSource *)source);
}

static void convert(RiCtx *ctx)
{
    MkscMk64Inventory *inventory=ctx->job;
    mk64_shared_sections(ctx);
    mk64_drivers(ctx);
    mk64_effects(ctx);
    /* The 3D item scene draws MK64's own items for every MK64 rules profile. */
    static const uint32_t item_profiles[]={MK64_ITEMS_ID,MK64_ITEMS_VS_ID,MK64_ITEMS_BATTLE_ID};
    RiBuf *rows=ri_buf(ctx);
    const uint32_t row_count=mk64_item_art(ctx,item_profiles,3,rows);
    RiBuf *bindings=ri_buf(ctx);
    ri_put(ctx,bindings,"BND1",4);ri_u32(ctx,bindings,row_count);ri_put_buf(ctx,bindings,rows);
    ri_section(ctx,98,1,2,bindings);
    mk64_sounds(ctx);
    for (unsigned i=0;i<20;++i) {
        ri_step(ctx,i,20);
        char reason[384];
        if (!ri_try(ctx,convert_one,(void *)&courses[i],reason,sizeof reason))
            ri_fail(ctx,"Mario Kart 64 %s: %s",courses[i].name,reason);
        if (courses[i].battle)++inventory->battle_stages;else ++inventory->courses;
        ++inventory->converted;
    }
    RiBuf *stamp=ri_buf(ctx);ri_put(ctx,stamp,"MK64IMP1",8);
    ri_u32(ctx,stamp,MKSC_MK64_IMPORT_VERSION);ri_u32(ctx,stamp,20);
    ri_section(ctx,80,3,1,stamp);ri_step(ctx,20,20);
}

int mksc_mk64_import(const uint8_t *rom,size_t size,MkscImportProgress progress,void *context,
                     MkscRomImportResult *result,MkscMk64Inventory *inventory,char *error,size_t capacity)
{
    MkscMk64Inventory candidate={0};const RiGroup groups[]={identify,convert};
    if (inventory)memset(inventory,0,sizeof *inventory);
    const int status=ri_build_steps(rom,size,groups,2,&candidate,progress,context,result,error,capacity);
    if (status==1 && inventory)*inventory=candidate;
    return status;
}
