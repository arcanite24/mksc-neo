#include "course_mirror.h"

#include <string.h>

static int32_t mirror_q16(int32_t x,uint32_t width)
{
    return (int32_t)(((uint32_t)width<<16u)-1u-(uint32_t)x);
}

static uint16_t mirror_heading(uint32_t heading)
{
    return (uint16_t)(0x10000u-(heading&0xffffu));
}

static void reverse_rows(uint8_t *cells,size_t stride,size_t width,size_t height)
{
    for(size_t y=0u;y<height;++y){
        uint8_t *row=cells+y*stride;
        for (size_t a = 0u, b = width - 1u; a < b; ++a, --b)
        {
            const uint8_t t = row[a];
            row[a] = row[b];
            row[b] = t;
        }
    }
}

static void mirror_points(PortableRacePoint *points,size_t count,int32_t width)
{
    for (size_t i = 0u; i < count; ++i)
        points[i].x = width - points[i].x;
}

static void mirror_props(PortableCourseProp *props,size_t count,uint32_t width)
{
    for(size_t i=0u;i<count;++i){
        PortableCourseProp *a=&props[i];
        const uint32_t family=a->source_code>>16u;
        a->x_q16=mirror_q16(a->x_q16,width);
        if(family==0xa1u){ /* Mechakoopa: variant 0 walks +x, variant 1 walks -x. */
            a->source_code^=1u;
            a->mechakoopa.velocity_x_q16=-a->mechakoopa.velocity_x_q16;
        }
        if(family==0xa0u)a->kamek.mirrored=1u;
        if(family==0xa2u){
            a->lake_rock.home_x_q16=mirror_q16(a->lake_rock.home_x_q16,width);
            a->lake_rock.mirrored=1u;
        }
    }
}

int portable_course_mirror_race(PortableRecoveredPackStorage *st,PortableRecoveredRaceView *race)
{
    if(st==NULL||race==NULL||st->terrain.cells!=st->terrain_cells||
       st->terrain.width==0u||st->raster.cells!=st->raster_cells||
       st->raster.width*16u!=st->terrain.width*8u||st->initial_terrain_cells==NULL)return 0;
    const uint32_t width=(uint32_t)st->terrain.width*8u;
    const size_t terrain_bytes=st->terrain.row_stride*st->terrain.height;
    reverse_rows(st->terrain_cells,st->terrain.row_stride,st->terrain.width,st->terrain.height);
    reverse_rows(st->raster_cells,st->raster.width,st->raster.width,st->raster.height);
    memcpy(st->mirrored_initial_terrain,st->initial_terrain_cells,terrain_bytes);
    reverse_rows(st->mirrored_initial_terrain,st->terrain.row_stride,st->terrain.width,st->terrain.height);
    st->initial_terrain_cells=st->mirrored_initial_terrain;
    race->dependencies.initial_terrain_cells=st->mirrored_initial_terrain;

    PortableRaceRulesView *rules=&race->rules;
    mirror_points(st->route,rules->route_count,(int32_t)width);
    mirror_points(st->gp_route_rows,rules->route_count*rules->gp_route_row_count,(int32_t)width);
    for(unsigned row=0u;row<3u;++row)
        if (rules->gp_sentinel_target_mask & (1u << row))
            st->gp_sentinel_targets[row].x = (int32_t)width - st->gp_sentinel_targets[row].x;
    rules->start_x_q16=mirror_q16(rules->start_x_q16,width);
    rules->start_heading=mirror_heading(rules->start_heading);
    race->initial_driver.pose.position_x=rules->start_x_q16;
    race->initial_driver.motion.angle=(int16_t)rules->start_heading;
    for(size_t i=0u;i<race->gp_grid_count;++i){
        st->gp_grid[i].x_q16=mirror_q16(st->gp_grid[i].x_q16,width);
        st->gp_grid[i].heading=mirror_heading(st->gp_grid[i].heading);
    }
    for(size_t i=0u;i<race->course_object_count;++i)
        st->course_objects[i].position_x_q16=mirror_q16(st->course_objects[i].position_x_q16,width);
    /* Coins sit on their tile origin for the source's cell contact. */
    for(size_t i=0u;i<race->coin_count;++i)
        st->coins[i].position_x_q16=(int32_t)((width-8u-(uint32_t)(st->coins[i].position_x_q16>>16))<<16);

    mirror_props(st->course_props,race->course_prop_count,width);
    for(size_t i=0u;i<race->lava_bubble_count;++i){
        st->lava_bubbles[i].x_q16=mirror_q16(st->lava_bubbles[i].x_q16,width);
        st->lava_bubbles[i].velocity_x_q16=-st->lava_bubbles[i].velocity_x_q16;
    }
    for(size_t i=0u;i<race->thwomp_count;++i){
        st->thwomps[i].x_q16=mirror_q16(st->thwomps[i].x_q16,width);
        st->thwomps[i].velocity_x_q16=-st->thwomps[i].velocity_x_q16;
    }
    for(size_t i=0u;i<race->crab_count;++i){
        st->crabs[i].x_q16=mirror_q16(st->crabs[i].x_q16,width);
        st->crabs[i].velocity_x_q16=-st->crabs[i].velocity_x_q16;
    }
    for(size_t i=0u;i<race->mouse_count;++i){
        PortableMouse *m=&st->mice[i];
        m->x_q16=mirror_q16(m->x_q16,width);
        m->velocity_x_q16=-m->velocity_x_q16;m->facing_x_q16=-m->facing_x_q16;
    }
    for(size_t i=0u;i<race->falling_star_count;++i){
        PortableFallingStar *f=&st->falling_stars[i];
        /* Odd variants arc toward -x; swapping parity keeps the delay. */
        f->x_q16=mirror_q16(f->x_q16,width);f->initial_x_q16=mirror_q16(f->initial_x_q16,width);
        f->variant ^= 1u;
        f->offset_q16 = -f->offset_q16;
        f->velocity_x_q16 = -f->velocity_x_q16;
    }
    for(size_t i=0u;i<race->boo_count;++i){
        st->boos[i].x_q16=mirror_q16(st->boos[i].x_q16,width);
        st->boos[i].heading=mirror_heading(st->boos[i].heading);
    }
    for (size_t i = 0u; i < race->seagull_count; ++i)
        st->seagulls[i].x_q16 = mirror_q16(st->seagulls[i].x_q16, width);
    for (size_t i = 0u; i < race->piranha_count; ++i)
        st->piranhas[i].x_q16 = mirror_q16(st->piranhas[i].x_q16, width);
    for (size_t i = 0u; i < race->snowman_count; ++i)
        st->snowmen[i].x_q16 = mirror_q16(st->snowmen[i].x_q16, width);
    for (size_t i = 0u; i < race->thundercloud_count; ++i)
        st->thunderclouds[i].x_q16 = mirror_q16(st->thunderclouds[i].x_q16, width);
    for (size_t i = 0u; i < race->penguin_count; ++i)
        st->penguins[i].x_q16 = mirror_q16(st->penguins[i].x_q16, width);
    for (size_t i = 0u; i < race->cannonball_count; ++i)
        st->cannonballs[i].x_q16 = mirror_q16(st->cannonballs[i].x_q16, width);

    /* A mirrored course is a different track for ghosts and records. */
    for(size_t i=0u;i<sizeof rules->compatibility.deterministic_digest;++i)
        rules->compatibility.deterministic_digest[i]^=(uint8_t)(0x4du+i);
    return 1;
}

/* GBA text-map entry index for column x of a map 32 or 64 tiles wide. */
static size_t sky_index(size_t width,size_t x,size_t y)
{
    return width==64u?(x/32u)*1024u+y*32u+(x&31u):y*width+x;
}

static void mirror_sky_map(uint8_t *map,size_t bytes,size_t width)
{
    if(map==NULL||width==0u)return;
    const size_t height=bytes/(width*2u);
    for(size_t y=0u;y<height;++y)
        for(size_t a=0u;a<width/2u;++a){
            const size_t i=sky_index(width,a,y)*2u,j=sky_index(width,width-1u-a,y)*2u;
            if(i+1u>=bytes||j+1u>=bytes)continue;
            const uint16_t left=(uint16_t)(map[i]|map[i+1u]<<8u)^0x0400u;
            const uint16_t right=(uint16_t)(map[j]|map[j+1u]<<8u)^0x0400u;
            map[i]=(uint8_t)right;map[i+1u]=(uint8_t)(right>>8u);
            map[j]=(uint8_t)left;map[j+1u]=(uint8_t)(left>>8u);
        }
}

static uint8_t nibble(const uint8_t *tiles,size_t tile,size_t x,size_t y)
{
    const uint8_t packed=tiles[tile*32u+y*4u+x/2u];
    return (uint8_t)((x&1u)?packed>>4u:packed&15u);
}

static void set_nibble(uint8_t *tiles,size_t tile,size_t x,size_t y,uint8_t value)
{
    uint8_t *packed=&tiles[tile*32u+y*4u+x/2u];
    *packed=(uint8_t)((x&1u)?((*packed&0x0fu)|(value<<4u)):((*packed&0xf0u)|value));
}

static void flip_tiles4(uint8_t *tiles,size_t count)
{
    for (size_t t = 0u; t < count; ++t)
        for (size_t y = 0u; y < 8u; ++y)
            for (size_t x = 0u; x < 4u; ++x)
            {
                const uint8_t a = nibble(tiles, t, x, y), b = nibble(tiles, t, 7u - x, y);
                set_nibble(tiles, t, x, y, b);
                set_nibble(tiles, t, 7u - x, y, a);
            }
}

int portable_course_mirror_presentation(PortablePresentationPackStorage *st,
    PortablePresentationPackView *view,const PortableRecoveredRaceView *race)
{
    if(st==NULL||view==NULL||race==NULL||race->dependencies.terrain==NULL)return 0;
    const uint32_t width=(uint32_t)race->dependencies.terrain->width*8u;
    /* The affine road has no flip bits: flip every 8bpp tile row. */
    if(view->tileset.tiles==st->road_tiles)
        reverse_rows(st->road_tiles,8u,8u,view->tileset.tile_bytes/8u);
    if(view->sky.front_map==st->sky_front){
        mirror_sky_map(st->sky_front,view->sky.front_map_bytes,view->sky.front_width_tiles);
        mirror_sky_map(st->sky_mid,view->sky.mid_map_bytes,view->sky.mid_width_tiles);
        mirror_sky_map(st->sky_back,view->sky.back_map_bytes,view->sky.back_width_tiles);
    }
    /* Time Trial minimap: reverse cell columns, flip each 4bpp tile. */
    if(view->minimap.cells==st->minimap_cells&&view->minimap.width_tiles!=0u){
        const size_t w=view->minimap.width_tiles;
        for(size_t y=0u;y<view->minimap.height_tiles;++y)
            for(size_t a=0u,b=w-1u;a<b;++a,--b){
                uint16_t *row=st->minimap_cells+y*w;
                const uint16_t t = row[a];
                row[a] = row[b];
                row[b] = t;
            }
        flip_tiles4(st->minimap_tiles,view->minimap.tile_bytes/32u);
    }
    /* GP course map: an 8x8-tile picture at one pixel per 32 world pixels;
     * markers follow mirrored positions, so the map flips over the world. */
    if(view->gp_race_hud.course_tiles!=NULL){
        const uint8_t *source=view->gp_race_hud.course_tiles;
        size_t span=width/32u;if(span>64u)span=64u;
        memcpy(st->mirrored_gp_course_tiles,source,sizeof st->mirrored_gp_course_tiles);
        for (size_t y = 0u; y < 64u; ++y)
            for (size_t x = 0u; x < span; ++x)
            {
                const size_t from = span - 1u - x;
                set_nibble(st->mirrored_gp_course_tiles, (y / 8u) * 8u + x / 8u, x & 7u, y & 7u,
                           nibble(source, (y / 8u) * 8u + from / 8u, from & 7u, y & 7u));
            }
        view->gp_race_hud.course_tiles=st->mirrored_gp_course_tiles;
    }
    for(size_t i=0u;i<view->scenery.object_count&&view->scenery.objects==st->scenery_objects;++i)
        st->scenery_objects[i].position_x_q16=mirror_q16(st->scenery_objects[i].position_x_q16,width);
    return 1;
}

PortableRecoveredPackStatus portable_course_open_race(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,uint8_t mirror)
{
    const PortableRecoveredPackStatus status=portable_recovered_pack_open_race(pack,selection,storage,race);
    if(status!=PORTABLE_RECOVERED_PACK_OK||!mirror)return status;
    return portable_course_mirror_race(storage,race)?PORTABLE_RECOVERED_PACK_OK:
        PORTABLE_RECOVERED_PACK_UNSUPPORTED_CONTENT;
}

PortableRecoveredPackStatus portable_course_open_presentation(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,const PortableRecoveredPackStorage *race_storage,
    const PortableRecoveredRaceView *race,PortablePresentationPackStorage *storage,
    PortablePresentationPackView *view,uint8_t mirror)
{
    const PortableRecoveredPackStatus status=portable_presentation_pack_open(pack,selection,
        race_storage,race,storage,view);
    if(status!=PORTABLE_RECOVERED_PACK_OK||!mirror)return status;
    return portable_course_mirror_presentation(storage,view,race)?PORTABLE_RECOVERED_PACK_OK:
        PORTABLE_RECOVERED_PACK_UNSUPPORTED_CONTENT;
}
