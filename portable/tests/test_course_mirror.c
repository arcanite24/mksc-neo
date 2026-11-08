#include "course_mirror.h"
#include "content_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"course mirror failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)

/* A 32x16-pixel world: 4x2 terrain cells, 2x1 raster sectors. */
static void build(PortableRecoveredPackStorage *st,PortableRecoveredRaceView *race,const uint8_t *initial)
{
    memset(race,0,sizeof *race);
    for(unsigned i=0u;i<8u;++i)st->terrain_cells[i]=(uint8_t)i;
    st->raster_cells[0]=0x81u;st->raster_cells[1]=2u;
    st->terrain=(PortableTerrainView){st->terrain_cells,4u,4u,2u};
    st->raster=(PortableTrackRaster){st->raster_cells,2u,2u,1u,0u,0u};
    st->initial_terrain_cells=initial;
    race->dependencies.initial_terrain_cells=initial;
    race->dependencies.terrain=&st->terrain;
    st->route[0]=(PortableRacePoint){8,0};st->route[1]=(PortableRacePoint){24,16};
    st->gp_route_rows[0]=(PortableRacePoint){16,8};st->gp_route_rows[1]=(PortableRacePoint){0,8};
    st->gp_sentinel_targets[0]=(PortableRacePoint){4,4};
    race->rules.route=st->route;race->rules.route_count=2u;
    race->rules.gp_route_rows=st->gp_route_rows;race->rules.gp_route_row_count=1u;
    race->rules.gp_sentinel_targets=st->gp_sentinel_targets;race->rules.gp_sentinel_target_mask=1u;
    race->rules.start_x_q16=5*65536+0x8000;race->rules.start_y_q16=3*65536;race->rules.start_heading=0x4000u;
    st->gp_grid[0]=(PortableGpGridRow){8*65536,4*65536,0x1000u};race->gp_grid=st->gp_grid;race->gp_grid_count=1u;
    st->course_objects[0].position_x_q16=12*65536;race->course_objects=st->course_objects;race->course_object_count=1u;
    st->coins[0].position_x_q16=8*65536;race->coins=st->coins;race->coin_count=1u;
    PortableThwomp *thwomp=&st->thwomps[0];
    if(!portable_thwomp_init(thwomp,2*65536,2*65536,0u,4u))thwomp->velocity_x_q16=0;
    race->thwomps=st->thwomps;race->thwomp_count=1u;
    (void)portable_falling_star_init(&st->falling_stars[0],20*65536,4*65536,0u,2u);
    race->falling_stars=st->falling_stars;race->falling_star_count=1u;
    (void)portable_course_prop_init(&st->course_props[0],16*65536,8*65536,0xa10000u,256u,0u);
    (void)portable_course_prop_init(&st->course_props[1],16*65536,8*65536,0xa00000u,256u,0u);
    race->course_props=st->course_props;race->course_prop_count=2u;
    memset(race->rules.compatibility.deterministic_digest,0x11,sizeof race->rules.compatibility.deterministic_digest);
}

static int synthetic(void)
{
    PortableRecoveredPackStorage *st=calloc(1,sizeof *st);
    PortableRecoveredRaceView *race=malloc(sizeof *race);
    static const uint8_t initial[8]={0,1,2,3,4,5,6,7};
    CHECK(st!=NULL&&race!=NULL);
    build(st,race,initial);
    CHECK(st->thwomps[0].velocity_x_q16==-0x2d555);
    CHECK(portable_course_mirror_race(st,race));
    /* Maps reverse row by row; the restart copy is mirrored, never the pack. */
    static const uint8_t mirrored[8]={3,2,1,0,7,6,5,4};
    CHECK(memcmp(st->terrain_cells,mirrored,8u)==0&&memcmp(st->initial_terrain_cells,mirrored,8u)==0);
    CHECK(race->dependencies.initial_terrain_cells==st->mirrored_initial_terrain&&initial[0]==0u);
    CHECK(st->raster_cells[0]==2u&&st->raster_cells[1]==0x81u);
    /* Pixel p maps to 31-p, keeping the mirrored tile; corners map to 32-x. */
    CHECK(race->rules.start_x_q16==(int32_t)(26*65536+0x7fff)&&race->rules.start_y_q16==3*65536);
    CHECK((race->rules.start_x_q16>>19)==3-((5*65536)>>19));
    CHECK(race->initial_driver.pose.position_x==race->rules.start_x_q16);
    CHECK(race->rules.start_heading==0xc000u&&(uint16_t)race->initial_driver.motion.angle==0xc000u);
    CHECK(st->route[0].x==24&&st->route[1].x==8&&st->route[1].y==16);
    CHECK(st->gp_route_rows[0].x==16&&st->gp_route_rows[1].x==32&&st->gp_sentinel_targets[0].x==28);
    CHECK(st->gp_grid[0].x_q16==(int32_t)(32*65536-1-8*65536)&&st->gp_grid[0].heading==0xf000u);
    CHECK(st->course_objects[0].position_x_q16==(int32_t)(32*65536-1-12*65536));
    CHECK(st->coins[0].position_x_q16==16*65536); /* tile 1 origin -> tile 2 origin */
    /* Direction-bearing actors turn around. */
    CHECK(st->thwomps[0].velocity_x_q16==0x2d555);
    CHECK(st->falling_stars[0].variant==3u&&st->falling_stars[0].offset_q16==-40*65536);
    CHECK(st->course_props[0].source_code==0xa10001u&&st->course_props[0].mechakoopa.velocity_x_q16==-32768);
    CHECK(st->course_props[1].kamek.mirrored==1u);
    CHECK(race->rules.compatibility.deterministic_digest[0]!=0x11u);
    /* Mirroring twice restores every position, heading, variant and identity. */
    CHECK(portable_course_mirror_race(st,race));
    CHECK(memcmp(st->terrain_cells,initial,8u)==0&&st->raster_cells[0]==0x81u);
    CHECK(race->rules.start_x_q16==5*65536+0x8000&&race->rules.start_heading==0x4000u);
    CHECK(st->route[0].x==8&&st->gp_route_rows[1].x==0&&st->coins[0].position_x_q16==8*65536);
    CHECK(st->falling_stars[0].variant==2u&&st->course_props[0].source_code==0xa10000u);
    CHECK(race->rules.compatibility.deterministic_digest[5]==0x11u);
    /* Inconsistent raster/terrain widths are refused. */
    st->raster.width=3u;
    CHECK(!portable_course_mirror_race(st,race));
    free(race);free(st);return 0;
}

static int motion(void)
{
    /* Kamek's patrol and the lake rocks' bounce turn toward -x when mirrored. */
    static int32_t sine[1024];
    PortableCourseProp kamek,rock;
    CHECK(portable_course_prop_init(&kamek,100*65536,100*65536,0xa00000u,256u,0u));
    PortableCourseProp mirrored=kamek;mirrored.kamek.mirrored=1u;
    for(int i=0;i<200;++i){CHECK(portable_kamek_advance(&kamek,sine));CHECK(portable_kamek_advance(&mirrored,sine));}
    CHECK(kamek.x_q16-100*65536==-(mirrored.x_q16-100*65536)&&kamek.x_q16!=100*65536);
    CHECK((uint16_t)(kamek.kamek.heading+mirrored.kamek.heading)==0u);
    CHECK(portable_course_prop_init(&rock,100*65536,100*65536,0xa20000u,256u,0u));
    PortableCourseProp rock_mirror=rock;rock_mirror.lake_rock.mirrored=1u;
    (void)portable_lake_rock_bounce(&rock);(void)portable_lake_rock_bounce(&rock_mirror);
    CHECK(rock.x_q16==100*65536+0x4000&&rock_mirror.x_q16==100*65536-0x4000&&rock.y_q16==rock_mirror.y_q16);
    return 0;
}

/* The fixture pack opens identically with mirror off; on, its whole course
 * reflects and presentation follows. */
static int fixture(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL);
    static uint8_t bytes[1u<<22];
    const size_t size=fread(bytes,1u,sizeof bytes,file);fclose(file);
    PortableAssetPackView pack;
    CHECK(portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK);
    PortableContentSelection selection;
    CHECK(portable_content_registry_select(&pack,24u,1u,&selection)==PORTABLE_CONTENT_OK);
    PortableRecoveredPackStorage *plain=malloc(sizeof *plain),*mirror=malloc(sizeof *mirror);
    PortablePresentationPackStorage *plain_art=malloc(sizeof *plain_art),*mirror_art=malloc(sizeof *mirror_art);
    PortableRecoveredRaceView a,b;PortablePresentationPackView va,vb;
    CHECK(plain&&mirror&&plain_art&&mirror_art);
    CHECK(portable_course_open_race(&pack,&selection,plain,&a,0u)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(portable_course_open_race(&pack,&selection,mirror,&b,1u)==PORTABLE_RECOVERED_PACK_OK);
    const size_t w=plain->terrain.width,h=plain->terrain.height;
    const int32_t width=(int32_t)w*8;
    for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x)
        CHECK(plain->terrain_cells[y*w+x]==mirror->terrain_cells[y*w+w-1u-x]);
    for(size_t i=0u;i<a.rules.route_count;++i)CHECK(b.rules.route[i].x==width-a.rules.route[i].x);
    CHECK((uint16_t)(a.rules.start_heading+b.rules.start_heading)==0u);
    CHECK(memcmp(a.rules.compatibility.deterministic_digest,b.rules.compatibility.deterministic_digest,
        sizeof a.rules.compatibility.deterministic_digest)!=0);
    CHECK(portable_course_open_presentation(&pack,&selection,plain,&a,plain_art,&va,0u)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(portable_course_open_presentation(&pack,&selection,mirror,&b,mirror_art,&vb,1u)==PORTABLE_RECOVERED_PACK_OK);
    CHECK(va.tileset.tile_bytes==vb.tileset.tile_bytes&&va.tileset.tile_bytes>=64u);
    for(size_t i=0u;i<va.tileset.tile_bytes;++i)CHECK(va.tileset.tiles[i]==vb.tileset.tiles[(i&~(size_t)7u)+7u-(i&7u)]);
    free(plain);free(mirror);free(plain_art);free(mirror_art);
    return 0;
}

int main(int argc,char **argv)
{
    if(synthetic()||motion())return 1;
    if(argc>1&&fixture(argv[1]))return 1;
    puts("course mirror: PASS maps, route, grid, actors, identity and presentation reflect");
    return 0;
}
