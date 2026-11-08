#include "race_ground_3d.h"

#include <float.h>
#include <math.h>

/* UVs are world pixels. A flat plane may extend beyond the map, just as the
 * source affine BG wraps; no course-specific world boundary is introduced. */
typedef struct {
    const PortableRoadMapView *map;
    const PortableTrackTilesetView *tileset;
    size_t width, height;
    uint64_t width_mask, height_mask;
    double uv_tolerance;
    uint32_t palette[256];
} GroundTexture;

typedef struct {
    double basis[3][3], inverse[3][3], reference[3], elevation;
} GroundCamera;

static uint32_t road_color(uint16_t color)
{
    const uint32_t r=color&31u, g=(color>>5u)&31u, b=(color>>10u)&31u;
    return UINT32_C(0xff000000) | (((r<<3u)|(r>>2u))<<16u) |
        (((g<<3u)|(g>>2u))<<8u) | (b<<3u) | (b>>2u);
}

static int texture_prepare(const PortableRaceSceneView *scene, GroundTexture *texture)
{
    const PortableRoadMapView *map=&scene->world_map;
    const PortableTrackTilesetView *tiles=&scene->tileset;
    if (!map->map || !map->width_tiles || !map->height_tiles ||
        map->height_tiles>SIZE_MAX/map->width_tiles ||
        map->width_tiles*map->height_tiles>map->map_bytes ||
        map->width_tiles>SIZE_MAX/8u || map->height_tiles>SIZE_MAX/8u ||
        !tiles->tiles || tiles->tile_bytes<64u || tiles->tile_bytes%64u ||
        !tiles->palette || !tiles->palette_count || tiles->palette_count>256u) return 0;
    const size_t tile_count=tiles->tile_bytes/64u;
    for (size_t i=0;i<map->width_tiles*map->height_tiles;++i)
        if (map->map[i]>=tile_count) return 0;
    texture->map=map;
    texture->tileset=tiles;
    texture->width=map->width_tiles*8u;
    texture->height=map->height_tiles*8u;
    texture->width_mask=(texture->width&(texture->width-1u))==0u?texture->width-1u:0u;
    texture->height_mask=(texture->height&(texture->height-1u))==0u?texture->height-1u:0u;
    for (size_t i=0;i<256u;++i)
        texture->palette[i]=i && i<tiles->palette_count?road_color(tiles->palette[i]):0u;
    return 1;
}

/* Keep this conversion small enough to inline independently of the general
 * modulo path. Stock power-of-two maps then need only rounding and a mask. */
static inline int64_t ground_texel(double position, double tolerance)
{
    /* Clipped interpolation may place an exact texel boundary a few ulps
     * below its integer. Checked UV uncertainty keeps conversion in int64. */
    int64_t pixel=(int64_t)position;
    if(position<(double)pixel)--pixel;
    if(1.0-(position-(double)pixel)<=tolerance)++pixel;
    return pixel;
}

static size_t wrap_nonpower_texel(int64_t pixel, size_t extent)
{
    const uint64_t magnitude=(uint64_t)(pixel<0?-pixel:pixel);
    const size_t wrapped=(size_t)(magnitude%(uint64_t)extent);
    return pixel<0 && wrapped!=0u?extent-wrapped:wrapped;
}

static uint32_t sample_ground(const void *userdata, double u, double v)
{
    const GroundTexture *texture=userdata;
    const int64_t pixel_x=ground_texel(u,texture->uv_tolerance);
    const int64_t pixel_y=ground_texel(v,texture->uv_tolerance);
    const size_t x=texture->width_mask?(size_t)((uint64_t)pixel_x&texture->width_mask):
        wrap_nonpower_texel(pixel_x,texture->width);
    const size_t y=texture->height_mask?(size_t)((uint64_t)pixel_y&texture->height_mask):
        wrap_nonpower_texel(pixel_y,texture->height);
    const size_t tile=texture->map->map[(y/8u)*texture->map->width_tiles+x/8u];
    const size_t offset=tile*64u+(y&7u)*8u+(x&7u);
    return texture->palette[texture->tileset->tiles[offset]];
}

static int camera_prepare(const PortableCameraState *camera,
    const PortableCameraProjection *projection, GroundCamera *out)
{
    if (camera->elevation_q16<=0) return 0;
    /* Source basis rows belong to world axes; transpose to project a world
     * displacement. Invert the actual imported integer basis, not ideal trig. */
    for (unsigned row=0;row<3u;++row) for (unsigned col=0;col<3u;++col)
        out->basis[row][col]=(double)projection->basis[col][row]/65536.0;
    double (*m)[3]=out->basis;
    const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-
        m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+
        m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if (!isfinite(det) || det==0.0) return 0;
    out->inverse[0][0]=(m[1][1]*m[2][2]-m[1][2]*m[2][1])/det;
    out->inverse[0][1]=(m[0][2]*m[2][1]-m[0][1]*m[2][2])/det;
    out->inverse[0][2]=(m[0][1]*m[1][2]-m[0][2]*m[1][1])/det;
    out->inverse[1][0]=(m[1][2]*m[2][0]-m[1][0]*m[2][2])/det;
    out->inverse[1][1]=(m[0][0]*m[2][2]-m[0][2]*m[2][0])/det;
    out->inverse[1][2]=(m[0][2]*m[1][0]-m[0][0]*m[1][2])/det;
    out->inverse[2][0]=(m[1][0]*m[2][1]-m[1][1]*m[2][0])/det;
    out->inverse[2][1]=(m[0][1]*m[2][0]-m[0][0]*m[2][1])/det;
    out->inverse[2][2]=(m[0][0]*m[1][1]-m[0][1]*m[1][0])/det;
    out->reference[0]=(double)projection->reference_x/65536.0;
    out->reference[1]=(double)projection->reference_y/65536.0;
    out->reference[2]=(double)projection->reference_z/65536.0;
    out->elevation=(double)camera->elevation_q16/65536.0;
    return 1;
}

static void world_from_camera(const GroundCamera *camera, double x, double y,
    double z, double world[3])
{
    const double p[3]={x,-y,camera->elevation*(1.0-z/1024.0)};
    for (unsigned row=0;row<3u;++row) {
        world[row]=camera->reference[row];
        for (unsigned col=0;col<3u;++col) world[row]+=camera->inverse[row][col]*p[col];
    }
}

static PortableScene3DVertex ground_vertex(const GroundCamera *camera, double x, double y)
{
    const double relative[3]={x-camera->reference[0],y-camera->reference[1],-camera->reference[2]};
    double p[3]={0.0,0.0,0.0};
    for (unsigned row=0;row<3u;++row) for (unsigned col=0;col<3u;++col)
        p[row]+=camera->basis[row][col]*relative[col];
    return (PortableScene3DVertex){p[0],-p[1],
        (camera->elevation-p[2])*1024.0/camera->elevation,x,y};
}

PortableRaceGround3DStatus portable_race_ground_3d_render(PortableScene3D *target,
    const PortableRaceSceneView *scene, const PortableCameraState *camera,
    const PortableCameraProjection *projection, int prior_physical_y,
    double near_z, double far_z)
{
    if (!target || !target->frame || !target->depth || !target->width || !target->height ||
        !scene || !camera || !projection || !isfinite(near_z) || !isfinite(far_z) ||
        near_z<=0.0 || far_z<=near_z) return PORTABLE_RACE_GROUND_3D_INVALID;
    GroundTexture texture;
    if (!texture_prepare(scene,&texture)) return PORTABLE_RACE_GROUND_3D_CONTENT;
    GroundCamera transform;
    if (!camera_prepare(camera,projection,&transform)) return PORTABLE_RACE_GROUND_3D_UNSUPPORTED;
    const double scale=(double)portable_software_frame_scale(target->frame);
    const PortableScene3DProjection perspective={1024.0*scale,1024.0*scale,
        (double)camera->screen_x*scale,(double)camera->screen_y*scale+prior_physical_y,near_z,far_z};
    double minimum[2]={INFINITY,INFINITY}, maximum[2]={-INFINITY,-INFINITY};
    /* Every point in the clipped frustum is a convex combination of these
     * corners. Their worldXY bounds therefore enclose its ground intersection. */
    for (unsigned corner=0;corner<8u;++corner) {
        const double z=(corner&4u)?far_z:near_z;
        const double sx=(corner&1u)?(double)target->width:0.0;
        const double sy=(corner&2u)?(double)target->height:0.0;
        double world[3];
        world_from_camera(&transform,(sx-perspective.center_x)*z/perspective.focal_x,
            (perspective.center_y-sy)*z/perspective.focal_y,z,world);
        for (unsigned axis=0;axis<2u;++axis) {
            if (!isfinite(world[axis])) return PORTABLE_RACE_GROUND_3D_UNSUPPORTED;
            if (world[axis]<minimum[axis]) minimum[axis]=world[axis];
            if (world[axis]>maximum[axis]) maximum[axis]=world[axis];
        }
    }
    if (minimum[0]==maximum[0] || minimum[1]==maximum[1]) return PORTABLE_RACE_GROUND_3D_UNSUPPORTED;
    /* Error comes from the clipped quad's world-coordinate scale, including
     * cancellation near world zero, rather than the final UV magnitude. */
    const double extent=fmax(1.0,fmax(fmax(fabs(minimum[0]),fabs(maximum[0])),
        fmax(fabs(minimum[1]),fabs(maximum[1]))));
    texture.uv_tolerance=64.0*DBL_EPSILON*extent;
    /* A valid custom basis can be too ill-conditioned to preserve even one
     * source Q16 step. Use the explicit fallback instead of snapping away
     * meaningful fractional world motion. */
    if(texture.uv_tolerance>=1.0/65536.0)
        return PORTABLE_RACE_GROUND_3D_UNSUPPORTED;
    const PortableScene3DVertex vertices[4]={
        ground_vertex(&transform,minimum[0],minimum[1]),
        ground_vertex(&transform,maximum[0],minimum[1]),
        ground_vertex(&transform,maximum[0],maximum[1]),
        ground_vertex(&transform,minimum[0],maximum[1])};
    for (unsigned i=0;i<4u;++i)
        if (!isfinite(vertices[i].x) || !isfinite(vertices[i].y) || !isfinite(vertices[i].z))
            return PORTABLE_RACE_GROUND_3D_UNSUPPORTED;
    if (!portable_scene_3d_quad_readonly(target,&perspective,vertices,sample_ground,&texture))
        return PORTABLE_RACE_GROUND_3D_UNSUPPORTED;
    return PORTABLE_RACE_GROUND_3D_OK;
}
