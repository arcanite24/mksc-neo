#include "gp_standings_renderer.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t rgba(uint16_t color)
{
    const uint32_t r5=color&31u,g5=(color>>5)&31u,b5=(color>>10)&31u;
    return UINT32_C(0xff000000)|((r5<<3)|(r5>>2))<<16|
           ((g5<<3)|(g5>>2))<<8|((b5<<3)|(b5>>2));
}

static void put(uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT],
                size_t x,size_t y,uint16_t cell)
{
    if(x<32u&&y<32u)map[y*32u+x]=cell;
}

static void draw_name(const PortableGpStandingsAssetView *asset,
    uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT],size_t screen_rank,
    uint8_t racer,uint8_t driver_class,uint8_t player,uint8_t display_rank)
{
    const uint16_t bank=player?UINT16_C(0xe000):UINT16_C(0xf000);
    const size_t top_y=3u+screen_rank*2u,bottom_y=top_y+1u;
    const uint16_t *name=asset->driver_cells+driver_class*asset->driver_cell_count;
    put(map,1u,top_y,(uint16_t)(bank+UINT16_C(0x101)+display_rank));
    put(map,1u,bottom_y,(uint16_t)(bank+UINT16_C(0x121)+display_rank));
    const int own_portrait=racer<8u&&asset->racer_portraits[racer]!=NULL;
    const int own_name=racer<8u&&asset->racer_names[racer]!=NULL;
    for(size_t index=0u;index<9u;++index)
    {
        if((own_portrait&&(index==1u||index==2u))||(own_name&&index>=4u))continue;
        uint16_t top=name[index],bottom=name[9u+index];
        if(index>=4u)
        {
            top=(uint16_t)((top&UINT16_C(0x0fff))|bank);
            bottom=(uint16_t)((bottom&UINT16_C(0x0fff))|bank);
        }
        put(map,3u+index,top_y,top);
        put(map,3u+index,bottom_y,bottom);
    }
}

static void blit(PortableSoftwareFrame *frame,const PortableNativeImage *image,
                 size_t left,size_t top,size_t width)
{
    for(size_t y=0u;y<image->height&&y<16u;++y)
        for(size_t x=0u;x<image->width&&x<width;++x){
            const uint32_t pixel=portable_native_image_pixel(image,x,y);
            if((pixel>>24)==255u&&top+y<frame->height&&left+x<frame->width)
                portable_software_frame_store(frame,(top+y)*frame->width+left+x,pixel);
        }
}

/* A roster driver's own art where draw_name left its cells empty. */
static void draw_name_image(const PortableGpStandingsAssetView *asset,
    PortableSoftwareFrame *frame,size_t screen_rank,uint8_t racer)
{
    const size_t top=(3u+screen_rank*2u)*8u;
    if(racer>=8u)return;
    if(asset->racer_portraits[racer]!=NULL)blit(frame,asset->racer_portraits[racer],4u*8u,top,16u);
    if(asset->racer_names[racer]!=NULL)blit(frame,asset->racer_names[racer],7u*8u,top,40u);
}

static void draw_row(const PortableGpStandingsAssetView *asset,
                     const PortableGpStandingsSession *session,
                     uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT],
                     size_t screen_rank)
{
    const uint8_t racer=(session->sorted_order?session->cup.standings_order:session->cup.race_order)[screen_rank];
    const PortableGpStandingRow *row=&session->cup.rows[racer];
    const uint16_t bank=racer==0u?UINT16_C(0xe000):UINT16_C(0xf000);
    const size_t top_y=3u+screen_rank*2u,bottom_y=top_y+1u;
    const uint8_t shown=session->displayed_points[racer];
    const uint8_t award=session->remaining_award[racer];
    draw_name(asset,map,screen_rank,racer,row->driver_class,(uint8_t)(racer==0u),
        session->sorted_order?row->standing_rank:row->finish_rank);
    put(map,23u,top_y,(uint16_t)(bank+UINT16_C(0x100)+shown/10u));
    put(map,24u,top_y,(uint16_t)(bank+UINT16_C(0x100)+shown%10u));
    put(map,25u,top_y,0u);
    put(map,23u,bottom_y,(uint16_t)(bank+UINT16_C(0x120)+shown/10u));
    put(map,24u,bottom_y,(uint16_t)(bank+UINT16_C(0x120)+shown%10u));
    put(map,25u,bottom_y,0u);
    if(!session->sorted_order && row->finish_rank<=3u)
    {
        put(map,26u,top_y,(uint16_t)(bank+UINT16_C(0x10c)));
        put(map,27u,top_y,(uint16_t)(bank+UINT16_C(0x100)+award));
        put(map,26u,bottom_y,(uint16_t)(bank+UINT16_C(0x12c)));
        put(map,27u,bottom_y,(uint16_t)(bank+UINT16_C(0x120)+award));
    }
}

static int tile_pixel(const uint8_t *tiles,size_t tile_bytes,const uint16_t *palette,
    size_t palette_count,uint16_t cell,unsigned px,unsigned py,uint32_t *out);
static int cell_pixel(const PortableGpStandingsAssetView *asset,uint16_t cell,
                      unsigned px,unsigned py,uint32_t *out)
{
    return tile_pixel(asset->tiles,asset->tile_bytes,asset->palette,asset->palette_count,cell,px,py,out);
}

static int tile_pixel(const uint8_t *tiles,size_t tile_bytes,const uint16_t *palette_colors,
    size_t palette_count,uint16_t cell,unsigned px,unsigned py,uint32_t *out)
{
    const unsigned tile=cell&0x3ffu;
    const unsigned palette=(cell>>12)&15u;
    unsigned x=(cell&0x400u)?7u-px:px;
    unsigned y=(cell&0x800u)?7u-py:py;
    size_t offset=(size_t)tile*32u+(size_t)y*4u+x/2u;
    unsigned color;
    if(offset>=tile_bytes)return 0;
    color=(tiles[offset]>>(4u*(x&1u)))&15u;
    /* Index zero is transparent in every bank; it must leave the composed
     * backdrop intact rather than exposing each bank's unused zero entry. */
    if(color==0u)return 0;
    if(palette*16u+color>=palette_count)return 0;
    *out=rgba(palette_colors[palette*16u+color]);
    return 1;
}

static void paint_map(const PortableGpStandingsAssetView *asset,
    const uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT],PortableSoftwareFrame *frame)
{
    for(size_t y=0u;y<160u;++y)for(size_t x=0u;x<240u;++x){
        uint32_t color=0u;
        (void)cell_pixel(asset,map[(y/8u)*32u+x/8u],
            (unsigned)(x&7u),(unsigned)(y&7u),&color);
        portable_software_frame_store(frame,y*frame->width+x,color);
    }
}

int portable_gp_standings_render_over(
    const PortableGpStandingsAssetView *asset,
    const PortableGpStandingsSession *session,
    PortableSoftwareFrame *frame)
{
    uint16_t overlay[PORTABLE_GP_STANDINGS_MAP_COUNT];
    if(asset==NULL||session==NULL||frame==NULL||frame->pixels==NULL||
       frame->width!=240u||frame->height!=160u||
       asset->tiles==NULL||asset->tile_bytes!=PORTABLE_GP_STANDINGS_TILE_BYTES||
       asset->palette==NULL||
       asset->palette_count!=PORTABLE_GP_STANDINGS_PALETTE_COUNT||
       asset->background_map==NULL||
       asset->background_map_count!=PORTABLE_GP_STANDINGS_MAP_COUNT||
       asset->driver_cells==NULL||
       asset->driver_count!=PORTABLE_GP_STANDINGS_DRIVER_COUNT||
       asset->driver_cell_count!=PORTABLE_GP_STANDINGS_DRIVER_CELLS||
       asset->footer_cells==NULL||
       asset->footer_cell_count!=PORTABLE_GP_STANDINGS_FOOTER_CELLS)
        return 0;
    /* 1DAA4 loads9B2AC then clears rows3..19 before DD58 writes the
     * current rows. These are map replacements, including blank cells, not
     * a transparent overlay over the template's example times. */
    memcpy(overlay,asset->background_map,sizeof overlay);
    /* 1DB58 reads race+800 (engine class), not the cup index. The hidden
     * rows retain all three class labels for per-race selection. */
    if(session->cup.engine_class>=3u)return 0;
    for(unsigned x=0u;x<3u;++x){
        overlay[1u*32u+15u+x]=asset->background_map[28u*32u+session->cup.engine_class*3u+x];
        overlay[2u*32u+15u+x]=asset->background_map[29u*32u+session->cup.engine_class*3u+x];
    }
    memset(overlay+3u*32u,0,17u*32u*sizeof *overlay);
    for(size_t rank=0u;rank<PORTABLE_GP_RACER_COUNT;++rank)
    {
        const uint8_t racer=(session->sorted_order?session->cup.standings_order:session->cup.race_order)[rank];
        if(racer>=PORTABLE_GP_RACER_COUNT||
           session->cup.rows[racer].driver_class>=asset->driver_count)return 0;
        draw_row(asset,session,overlay,rank);
    }
    for(size_t index=0u;index<asset->footer_cell_count;++index)
        put(overlay,3u+index,19u,asset->footer_cells[index]);
    uint32_t *logical_pixels = malloc(240u * 160u * sizeof *logical_pixels);
    if (logical_pixels == NULL)
        return 0;
    PortableSoftwareFrame logical={logical_pixels,240u,160u,frame->ui_text,1u,0u,0u,NULL};
    paint_map(asset,overlay,&logical);
    for(size_t rank=0u;rank<PORTABLE_GP_RACER_COUNT;++rank)
        draw_name_image(asset,&logical,rank,
            (session->sorted_order?session->cup.standings_order:session->cup.race_order)[rank]);
    uint8_t rows[20];for(unsigned y=0u;y<20u;++y)rows[y]=(uint8_t)y;
    if(session->phase==PORTABLE_GP_STANDINGS_REORDER){
        const unsigned stage=session->reorder_stage,progress=session->reorder_progress;
        if(stage==16u){
            for(unsigned i=0u;i<progress && i<14u;++i){
                if(!(i&1u))rows[4u+i]=rows[3u+i];
                rows[3u+i]=255u;
            }
        }else if(stage>=17u && stage<24u){
            for(unsigned y=3u;y<20u;++y)rows[y]=255u;
            if(stage==17u || stage==21u){
                const unsigned first=stage==17u?16u:17u;
                const unsigned offset=stage==17u?progress:5u-progress;
                for(unsigned y=first;y<20u;++y)if(y+offset<20u)rows[y+offset]=(uint8_t)y;
            }else if(stage==22u){
                for(unsigned y=17u;y<20u;++y)rows[y]=(uint8_t)y;
                for(unsigned i=4u;i<progress && i<=17u;++i){
                    if(!(i&1u))rows[20u-i]=(uint8_t)(19u-i);
                    else{rows[20u-i]=(uint8_t)(20u-i);rows[21u-i]=(uint8_t)(21u-i);}
                }
            }
        }
    }
    for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x){
        if(rows[y/8u]==255u)continue;
        const uint32_t pixel=logical_pixels[((unsigned)rows[y/8u]*8u+(y&7u))*240u+x];
        if(pixel>>24)portable_software_frame_store(frame,y*240u+x,pixel);
    }
    free(logical_pixels);
    return 1;
}

int portable_gp_standings_render(const PortableGpStandingsAssetView *asset,
    const PortableGpStandingsSession *session,PortableSoftwareFrame *frame)
{
    if(asset==NULL||asset->palette==NULL||frame==NULL||frame->pixels==NULL)return 0;
    portable_software_frame_clear(frame,rgba(asset->palette[0]));
    return portable_gp_standings_render_over(asset,session,frame);
}

/* sub_802095C mode 0: m'ss"cc in two-cell digits at columns 13..19, as in
 * sub_8022xxx's board; separators are tiles 0x10a/0x10b. */
static void draw_board_time(uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT],
    size_t screen_rank,uint32_t centiseconds,uint16_t bank)
{
    if(centiseconds>UINT32_C(0xea5f))centiseconds=UINT32_C(0xea5f);
    const uint32_t seconds=centiseconds/100u,minutes=seconds/60u;
    const uint16_t cells[7]={(uint16_t)(minutes%10u),0x0au,(uint16_t)(seconds%60u/10u),
        (uint16_t)(seconds%10u),0x0bu,(uint16_t)(centiseconds%100u/10u),(uint16_t)(centiseconds%10u)};
    const size_t top_y=3u+screen_rank*2u;
    for(size_t column=0u;column<7u;++column){
        put(map,13u+column,top_y,(uint16_t)(bank+UINT16_C(0x100)+cells[column]));
        put(map,13u+column,top_y+1u,(uint16_t)(bank+UINT16_C(0x120)+cells[column]));
    }
}

static int valid_board_asset(const PortableGpStandingsAssetView *asset,const PortableSoftwareFrame *frame)
{
    return asset!=NULL&&frame!=NULL&&frame->pixels!=NULL&&frame->width==240u&&frame->height==160u&&
        asset->tiles!=NULL&&asset->tile_bytes==PORTABLE_GP_STANDINGS_TILE_BYTES&&asset->palette!=NULL&&
        asset->palette_count==PORTABLE_GP_STANDINGS_PALETTE_COUNT&&asset->background_map!=NULL&&
        asset->background_map_count==PORTABLE_GP_STANDINGS_MAP_COUNT&&asset->driver_cells!=NULL&&
        asset->driver_count==PORTABLE_GP_STANDINGS_DRIVER_COUNT&&
        asset->driver_cell_count==PORTABLE_GP_STANDINGS_DRIVER_CELLS&&asset->footer_cells!=NULL&&
        asset->footer_cell_count==PORTABLE_GP_STANDINGS_FOOTER_CELLS;
}

int portable_time_board_render_slide(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results,const PortableTimeBoardRow *rows,unsigned count,
    uint8_t engine_class,const uint8_t row_x[8],PortableSoftwareFrame *frame)
{
    if(!valid_board_asset(asset,frame)||results==NULL||results->time_board_map==NULL||
       results->time_board_map_count!=PORTABLE_GP_STANDINGS_MAP_COUNT||results->followup_tiles==NULL||
       results->followup_tile_bytes<PORTABLE_GP_STANDINGS_TILE_BYTES||
       rows==NULL||count==0u||count>PORTABLE_GP_RACER_COUNT||engine_class>=3u)
        return 0;
    /* sub_8020770's VRAM: AAE3C/AB7FC, then 9AA2C from tile 0x4e, the course
     * name at tile 0x60 and the shared name/digit banks the standings share. */
    uint8_t tiles[PORTABLE_GP_STANDINGS_TILE_BYTES];
    memcpy(tiles,results->followup_tiles,0x9c0u);
    memcpy(tiles+0x9c0u,asset->tiles+0x20u,0x240u);
    memcpy(tiles+0xc00u,asset->tiles+0x400u,0x400u);
    memcpy(tiles+0x1000u,asset->tiles+0x1000u,PORTABLE_GP_STANDINGS_TILE_BYTES-0x1000u);
    uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT];
    memcpy(map,results->time_board_map,sizeof map);
    /* The class label comes from hidden rows 28/29. */
    for(size_t column=0u;column<3u;++column){
        put(map,13u+column,0u,map[28u*32u+engine_class*3u+column]);
        put(map,13u+column,1u,map[29u*32u+engine_class*3u+column]);
    }
    /* 2123C case 6 clears rows 3..19 (the staged window too); case 7 redraws each row. */
    memset(map+3u*32u,0,17u*32u*sizeof *map);
    for(unsigned rank=0u;rank<count;++rank){
        const PortableTimeBoardRow *row=&rows[rank];
        if(row->driver_class>=asset->driver_count||row->selected>1u)return 0;
        const uint8_t player=row->selected;
        const uint16_t bank=player?UINT16_C(0xe000):UINT16_C(0xf000);
        if(row->display_rank>=PORTABLE_GP_RACER_COUNT||row->kind>PORTABLE_TIME_BOARD_COUNT||
           (row->kind==PORTABLE_TIME_BOARD_COUNT&&row->value>9u))return 0;
        draw_name(asset,map,rank,row->racer_index,row->driver_class,player,row->display_rank);
        if(row->kind==PORTABLE_TIME_BOARD_TIME)draw_board_time(map,rank,row->value,bank);
        else if(row->kind==PORTABLE_TIME_BOARD_COUNT){
            put(map,13u,3u+rank*2u,(uint16_t)(bank+UINT16_C(0x100)+row->value));
            put(map,13u,4u+rank*2u,(uint16_t)(bank+UINT16_C(0x120)+row->value));
        }
    }
    /* sub_802095C draws the DB69C footer with the eighth row. */
    if(count==PORTABLE_GP_RACER_COUNT)
        for(size_t index=0u;index<asset->footer_cell_count;++index)
            put(map,3u+index,19u,asset->footer_cells[index]);
    for(size_t y=0u;y<160u;++y)for(size_t x=0u;x<240u;++x){
        uint32_t color=rgba(asset->palette[0]);
        const unsigned rank=y<24u?0u:y>=152u?7u:(unsigned)(y/8u-3u)/2u;
        const unsigned shift=row_x&&y>=24u?row_x[rank]*8u:0u;
        if(x>=shift&&tile_pixel(tiles,sizeof tiles,asset->palette,asset->palette_count,
            map[(y/8u)*32u+(x-shift)/8u],(unsigned)((x-shift)&7u),(unsigned)(y&7u),&color))
            portable_software_frame_store(frame,y*frame->width+x,color);
    }
    for(unsigned rank=0u;rank<count;++rank){
        const uint8_t racer=rows[rank].racer_index;
        const size_t shift=row_x?row_x[rank]*8u:0u,top=(3u+rank*2u)*8u;
        if(racer<8u){
            if(asset->racer_portraits[racer])blit(frame,asset->racer_portraits[racer],32u+shift,top,16u);
            if(asset->racer_names[racer])blit(frame,asset->racer_names[racer],56u+shift,top,40u);
        }
    }
    return 1;
}

int portable_time_board_render(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results,const PortableTimeBoardRow *rows,unsigned count,
    uint8_t engine_class,PortableSoftwareFrame *frame)
{return portable_time_board_render_slide(asset,results,rows,count,engine_class,NULL,frame);}

int portable_battle_board_render(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results_asset,const PortableBattleResults *results,
    uint8_t engine_class,PortableSoftwareFrame *frame)
{
    PortableTimeBoardRow rows[4];
    if(results==NULL||results->count<2u||results->count>4u)return 0;
    for(unsigned row=0u;row<results->count;++row){
        const PortableBattleResultRow *r=&results->rows[row];
        if(r->balloons>3u)return 0;
        rows[row]=(PortableTimeBoardRow){r->balloons,r->racer_index,r->driver_class,r->rank,PORTABLE_TIME_BOARD_COUNT,(uint8_t)(r->racer_index==0u)};
    }
    return portable_time_board_render(asset,results_asset,rows,results->count,engine_class,frame);
}

int portable_vs_board_render(const PortableGpStandingsAssetView *asset,
    const PortableGpResultsAssetView *results_asset,const PortableVsResults *results,
    uint8_t engine_class,PortableSoftwareFrame *frame)
{
    PortableTimeBoardRow rows[PORTABLE_GP_MAX_LOCALS];
    if(results==NULL||results->count==0u||results->count>PORTABLE_GP_MAX_LOCALS)return 0;
    for(unsigned row=0u;row<results->count;++row){
        const PortableVsResultRow *r=&results->rows[row];
        rows[row]=(PortableTimeBoardRow){r->centiseconds,r->racer_index,r->driver_class,(uint8_t)row,
            (uint8_t)(r->retired?PORTABLE_TIME_BOARD_EMPTY:PORTABLE_TIME_BOARD_TIME),(uint8_t)(r->racer_index==0u)};
    }
    return portable_time_board_render(asset,results_asset,rows,results->count,engine_class,frame);
}

int portable_gp_time_board_render(const PortableGpStandingsAssetView *asset,
    const PortableTimeBoardRow rows[8],const uint8_t points[8],const uint8_t awards[8],
    const PortableGpTimeBoardFlow *flow,uint8_t engine_class,PortableSoftwareFrame *frame)
{
    if(!valid_board_asset(asset,frame)||!rows||!points||!awards||!flow||engine_class>=3u)return 0;
    if(!flow->visible)return 1;
    uint16_t map[PORTABLE_GP_STANDINGS_MAP_COUNT];memcpy(map,asset->background_map,sizeof map);
    for(unsigned x=0u;x<3u;++x){map[32u+15u+x]=asset->background_map[28u*32u+engine_class*3u+x];
        map[64u+15u+x]=asset->background_map[29u*32u+engine_class*3u+x];}
    memset(map+3u*32u,0,17u*32u*sizeof *map);
    for(unsigned rank=0u;rank<8u;++rank){
        const PortableTimeBoardRow *row=&rows[rank];
        if(row->driver_class>=8u||row->racer_index>=8u||row->display_rank>=8u||row->selected>1u)return 0;
        const uint16_t bank=row->selected?0xe000u:0xf000u;
        draw_name(asset,map,rank,row->racer_index,row->driver_class,row->selected,row->display_rank);
        if(flow->state<10u){
            uint32_t time=row->value;if(time>0xea5fu)time=0xea5fu;
            const uint32_t seconds=time/100u;
            const uint16_t digits[7]={(uint16_t)((seconds/60u)%10u),10u,(uint16_t)((seconds%60u)/10u),
                (uint16_t)(seconds%10u),11u,(uint16_t)((time%100u)/10u),(uint16_t)(time%10u)};
            for(unsigned x=0u;x<7u;++x){put(map,22u+x,3u+2u*rank,(uint16_t)(bank+0x100u+digits[x]));
                put(map,22u+x,4u+2u*rank,(uint16_t)(bank+0x120u+digits[x]));}
        }else{
            const uint8_t value=points[row->racer_index],award=awards[row->racer_index];
            const uint16_t digits[5]={(uint16_t)(value/10u),(uint16_t)(value%10u),0u,12u,award};
            for(unsigned x=0u;x<5u;++x)if(x!=2u&&(x<3u||row->display_rank<4u)){
                put(map,23u+x,3u+2u*rank,(uint16_t)(bank+0x100u+digits[x]));
                put(map,23u+x,4u+2u*rank,(uint16_t)(bank+0x120u+digits[x]));}
        }
    }
    if(flow->state==9u)for(unsigned y=3u;y<19u;++y)for(unsigned i=1u;i<=flow->progress&&i<=4u;++i){
        put(map,20u+i,y,0u);put(map,29u-i,y,0u);}
    if(flow->state==10u){const unsigned padding=4u-flow->progress;
        for(unsigned y=3u;y<19u;++y)for(unsigned i=0u;i<padding;++i){put(map,20u+i,y,0u);put(map,29u-i,y,0u);}}
    for(unsigned i=0u;i<asset->footer_cell_count;++i)put(map,3u+i,19u,asset->footer_cells[i]);
    uint32_t *pixels = calloc(240u * 160u, sizeof *pixels);
    if (pixels == NULL)
        return 0;
    PortableSoftwareFrame logical={.pixels=pixels,.width=240u,.height=160u};
    paint_map(asset,map,&logical);
    for(unsigned rank=0u;rank<8u;++rank)draw_name_image(asset,&logical,rank,rows[rank].racer_index);
    for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x){
        const unsigned rank=y>=24u&&y<152u?(y-24u)/16u:8u;
        const unsigned offset=rank<8u&&flow->state==5u?flow->row_x[rank]*8u:0u;
        if(x<offset)continue;
        const uint32_t pixel=pixels[y*240u+x-offset];
        if(pixel>>24u)portable_software_frame_store(frame,y*240u+x,pixel);
    }
    free(pixels);
    return 1;
}
