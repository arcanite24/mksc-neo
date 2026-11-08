#include "scene_3d.h"
#include "scene_3d_workers.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int portable_scene_3d_begin(PortableScene3D *scene, PortableSoftwareFrame *frame)
{
    if (!scene || !frame || !frame->pixels || !frame->width || !frame->height) return 0;
    const size_t scale = portable_software_frame_scale(frame);
    if (frame->width > SIZE_MAX / scale || frame->height > SIZE_MAX / scale) return 0;
    const size_t width = frame->width * scale, height = frame->height * scale;
    const size_t stride = frame->stride ? frame->stride : width;
    if (stride < width || height > SIZE_MAX / stride ||
        height * stride > SIZE_MAX / sizeof(uint32_t) || height > SIZE_MAX / width ||
        width * height > SIZE_MAX / (2u * sizeof(uint32_t))) return 0;
    const size_t count = width * height;
    if (count > scene->capacity) {
        uint32_t *depth = realloc(scene->depth, count * 2u * sizeof *depth);
        if (!depth) return 0;
        scene->depth = depth;
        scene->capacity = count;
    }
    scene->width = width;
    scene->height = height;
    scene->billboard_depth = scene->depth + scene->capacity;
    scene->frame = frame;
    scene->clip_top = 0u;
    scene->clip_bottom = height;
    scene->source_weight = 16u;
    scene->destination_weight = 0u;
    scene->model_pass = 0u;
    memset(scene->depth,0xff,count*sizeof *scene->depth);
    memset(scene->billboard_depth,0xff,count*sizeof *scene->billboard_depth);
    return 1;
}

void portable_scene_3d_close(PortableScene3D *scene)
{
    if (!scene) return;
    portable_scene_3d_workers_close(scene);
    free(scene->depth);
    memset(scene, 0, sizeof *scene);
}

int portable_scene_3d_depth_store(PortableScene3D *scene, size_t x, size_t y,
    uint32_t depth_q16, uint32_t argb)
{
    if (!scene || !scene->frame || !scene->depth || x >= scene->width ||
        y >= scene->height || y < scene->clip_top || y >= scene->clip_bottom ||
        depth_q16 == UINT32_MAX || !(argb >> 24) || scene->model_pass>1u ||
        (scene->model_pass&&!scene->billboard_depth)) return 0;
    uint32_t *depth = &scene->depth[y * scene->width + x];
    if (depth_q16 > *depth) return 0;
    uint32_t *object=scene->model_pass?&scene->billboard_depth[y*scene->width+x]:NULL;
    if(object && depth_q16>*object)return 0;
    *depth = depth_q16;
    if(object)*object=depth_q16;
    uint32_t *pixel = &scene->frame->pixels[y * portable_software_frame_stride(scene->frame) + x];
    if (scene->source_weight != 16u || scene->destination_weight != 0u) {
        uint32_t mixed = UINT32_C(0xff000000);
        for (unsigned shift = 0u; shift < 24u; shift += 8u) {
            unsigned channel = (((argb >> shift) & 255u) * scene->source_weight +
                ((*pixel >> shift) & 255u) * scene->destination_weight) / 16u;
            if (channel > 255u) channel = 255u;
            mixed |= (uint32_t)channel << shift;
        }
        argb = mixed;
    }
    *pixel = argb;
    return 1;
}

/* Convex clipping adds at most one vertex per plane: a triangle clipped by
 * near/far and four viewport edges fits in nine. Reserve room explicitly. */
enum { CLIP_CAPACITY = 16 };
typedef struct { double x, y, inverse_z, u_over_z, v_over_z; } ScreenVertex;

static int vertex_finite(const PortableScene3DVertex *v)
{
    return isfinite(v->x) && isfinite(v->y) && isfinite(v->z) &&
           isfinite(v->u) && isfinite(v->v);
}

static double crossing_fraction(double a, double b)
{
    a = fabs(a); b = fabs(b);
    /* Opposite signed distances can overflow when subtracted directly. */
    if (a > b) return 1.0 / (1.0 + b / a);
    const double ratio = a / b;
    return ratio / (1.0 + ratio);
}

static PortableScene3DVertex camera_lerp(PortableScene3DVertex a,
    PortableScene3DVertex b, double t)
{
    const double s = 1.0 - t;
    return (PortableScene3DVertex){a.x*s+b.x*t, a.y*s+b.y*t,
        a.z*s+b.z*t, a.u*s+b.u*t, a.v*s+b.v*t};
}

static int clip_camera(const PortableScene3DVertex *in, size_t count,
    PortableScene3DVertex *out, double z, int far_plane, size_t *out_count)
{
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        const PortableScene3DVertex a = in[i], b = in[(i+1u)%count];
        const double da = far_plane ? z-a.z : a.z-z;
        const double db = far_plane ? z-b.z : b.z-z;
        if (!isfinite(da) || !isfinite(db)) return 0;
        if (da >= 0.0) out[used++] = a;
        if ((da >= 0.0) != (db >= 0.0)) {
            if (used >= CLIP_CAPACITY) return 0;
            PortableScene3DVertex v = camera_lerp(a,b,crossing_fraction(da,db));
            v.z = z;
            if (!vertex_finite(&v)) return 0;
            out[used++] = v;
        }
        if (used >= CLIP_CAPACITY) return 0;
    }
    *out_count = used;
    return 1;
}

static ScreenVertex screen_lerp(ScreenVertex a, ScreenVertex b, double t)
{
    const double s = 1.0-t;
    return (ScreenVertex){a.x*s+b.x*t, a.y*s+b.y*t,
        a.inverse_z*s+b.inverse_z*t, a.u_over_z*s+b.u_over_z*t,
        a.v_over_z*s+b.v_over_z*t};
}

static int screen_finite(ScreenVertex v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.inverse_z) &&
           isfinite(v.u_over_z) && isfinite(v.v_over_z);
}

static size_t clip_screen(const ScreenVertex *in, size_t count,
    ScreenVertex *out, unsigned plane, double bound)
{
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        const ScreenVertex a=in[i], b=in[(i+1u)%count];
        double da=plane<2u?a.x:a.y, db=plane<2u?b.x:b.y;
        da = (plane&1u)?bound-da:da;
        db = (plane&1u)?bound-db:db;
        if (da>=0.0) out[used++]=a;
        if ((da>=0.0)!=(db>=0.0)) {
            if (used>=CLIP_CAPACITY) return CLIP_CAPACITY;
            ScreenVertex v=screen_lerp(a,b,crossing_fraction(da,db));
            if (plane<2u) v.x=(plane&1u)?bound:0.0;
            else v.y=(plane&1u)?bound:0.0;
            out[used++]=v;
        }
        if (used>=CLIP_CAPACITY) return CLIP_CAPACITY;
    }
    return used;
}

static double edge(ScreenVertex a, ScreenVertex b, double x, double y)
{
    return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}

static int top_left(ScreenVertex a, ScreenVertex b)
{
    return b.y<a.y || (b.y==a.y && b.x>a.x);
}

/* Limit work to a conservative horizontal slice, without changing the edge
 * expressions or interpolants used for accepted pixels. Near-horizontal or
 * numerically uncertain edges keep the original bounding-box row. */
static int triangle_row_span(const ScreenVertex vertices[3],double y,
    double coordinate_error,size_t *first,size_t *last)
{
    double minimum=INFINITY,maximum=-INFINITY,padding=2.0;
    unsigned crossings=0u;
    for(unsigned i=0;i<3u;++i){
        const ScreenVertex a=vertices[i],b=vertices[(i+1u)%3u];
        const double low=fmin(a.y,b.y),high=fmax(a.y,b.y);
        if(y<low || y>high){
            if(y>=low-coordinate_error && y<=high+coordinate_error)return 1;
            continue;
        }
        const double dy=b.y-a.y;
        if(dy==0.0 || fabs(dy)<=coordinate_error)return 1;
        const double slope=(b.x-a.x)/dy;
        const double error=coordinate_error*(1.0+fabs(slope));
        const double x=a.x+(y-a.y)*slope;
        if(!isfinite(x) || !isfinite(error) || error>1.0)return 1;
        if(x<minimum)minimum=x;
        if(x>maximum)maximum=x;
        if(2.0+error>padding)padding=2.0+error;
        ++crossings;
    }
    if(!crossings)return 0;
    /* Outward padding includes pixel-center/top-left ties and the bound on
     * subtraction, division and edge-evaluation roundoff. Clamp before cast. */
    const double start=floor(fmax((double)*first,minimum-padding));
    const double end=ceil(fmin((double)*last,maximum+padding));
    if(start>=end)return 0;
    *first=(size_t)start;*last=(size_t)end;
    return 1;
}

/* These symmetric exponent bounds keep ratios/products normal and finite.
 * Extreme generic triangles retain the original normalized divisions. */
static int reciprocal_numerator(double value)
{
    const double magnitude=fabs(value);
    return value==0.0 || (magnitude>=0x1p-500 && magnitude<=0x1p500);
}

static void raster_triangle(PortableScene3D *scene, ScreenVertex a,
    ScreenVertex b, ScreenVertex c, PortableScene3DSampler sampler, const void *userdata)
{
    double area=edge(a,b,c.x,c.y);
    if (area==0.0) return;
    if (area<0.0) { const ScreenVertex swap=b; b=c; c=swap; area=-area; }
    double min_x=fmin(a.x,fmin(b.x,c.x)), max_x=fmax(a.x,fmax(b.x,c.x));
    double min_y=fmin(a.y,fmin(b.y,c.y)), max_y=fmax(a.y,fmax(b.y,c.y));
    /* Clipping made these finite and bounded before any integer conversion. */
    const size_t x0=(size_t)floor(fmax(0.0,min_x));
    const size_t y0=(size_t)floor(fmax((double)scene->clip_top,min_y));
    const size_t x1=(size_t)ceil(fmin((double)scene->width,max_x));
    const size_t y1=(size_t)ceil(fmin(fmin((double)scene->height,
        (double)scene->clip_bottom),max_y));
    const int ab=top_left(a,b), bc=top_left(b,c), ca=top_left(c,a);
    const ScreenVertex vertices[3]={a,b,c};
    const int reciprocal_area=area>=0x1p-500 && area<=0x1p500;
    const double coordinate_error=64.0*DBL_EPSILON*fmax(1.0,
        fmax(fmax(fabs(min_x),fabs(max_x)),fmax(fabs(min_y),fabs(max_y))));
    for (size_t y=y0; y<y1; ++y) {
        size_t first=x0,last=x1;
        if(!triangle_row_span(vertices,(double)y+0.5,coordinate_error,&first,&last))continue;
        for (size_t x=first; x<last; ++x) {
            const double px=(double)x+0.5, py=(double)y+0.5;
            const double wa=edge(b,c,px,py), wb=edge(c,a,px,py), wc=edge(a,b,px,py);
            if (wa<0.0 || (wa==0.0&&!bc) || wb<0.0 || (wb==0.0&&!ca) ||
                wc<0.0 || (wc==0.0&&!ab)) continue;
            const double sz=wa*a.inverse_z+wb*b.inverse_z+wc*c.inverse_z;
            if(!(sz>0.0) || !isfinite(sz))continue;
            const double su=wa*a.u_over_z+wb*b.u_over_z+wc*c.u_over_z;
            const double sv=wa*a.v_over_z+wb*b.v_over_z+wc*c.v_over_z;
            double z,u,v;
            if(reciprocal_area && sz>=0x1p-500 && sz<=0x1p500 &&
                reciprocal_numerator(su) && reciprocal_numerator(sv)){
                const double reciprocal=1.0/sz;
                z=area*reciprocal;
                u=su*reciprocal;
                v=sv*reciprocal;
            }else{
                const double iz=sz/area;
                if(!(iz>0.0) || !isfinite(iz))continue;
                z=1.0/iz;
                u=su/(area*iz);
                v=sv/(area*iz);
            }
            if (!isfinite(u) || !isfinite(v)) continue;
            const double depth=z*65536.0;
            const uint32_t q16=depth>=(double)(UINT32_MAX-1u)?UINT32_MAX-1u:(uint32_t)depth;
            (void)portable_scene_3d_depth_store(scene,x,y,q16,sampler(userdata,u,v));
        }
    }
}

static int prepare_triangle(const PortableScene3D *scene,
    const PortableScene3DProjection *p, const PortableScene3DVertex vertices[3],
    ScreenVertex screen[2][CLIP_CAPACITY],size_t *out_count)
{
    *out_count=0u;
    if (!scene || !scene->frame || !scene->depth || !p || !vertices ||
        scene->clip_top>scene->clip_bottom || scene->clip_bottom>scene->height ||
        !isfinite(p->focal_x) || !isfinite(p->focal_y) || !isfinite(p->center_x) ||
        !isfinite(p->center_y) || !isfinite(p->near_z) || !isfinite(p->far_z) ||
        p->focal_x<=0.0 || p->focal_y<=0.0 || p->near_z<=0.0 || p->far_z<=p->near_z) return 0;
    PortableScene3DVertex camera[2][CLIP_CAPACITY];
    for (size_t i=0;i<3u;++i) {
        if (!vertex_finite(&vertices[i])) return 0;
        camera[0][i]=vertices[i];
    }
    size_t count=3u;
    if (!clip_camera(camera[0],count,camera[1],p->near_z,0,&count) ||
        !clip_camera(camera[1],count,camera[0],p->far_z,1,&count)) return 0;
    if (!count) return 1;
    for (size_t i=0;i<count;++i) {
        const PortableScene3DVertex v=camera[0][i];
        const double iz=1.0/v.z;
        screen[0][i]=(ScreenVertex){p->center_x+p->focal_x*(v.x*iz),
            p->center_y-p->focal_y*(v.y*iz),iz,v.u*iz,v.v*iz};
        const ScreenVertex q=screen[0][i];
        if (!screen_finite(q)) return 0;
    }
    for (unsigned plane=0;plane<4u;++plane) {
        const unsigned from=plane&1u, to=from^1u;
        const double bound=plane<2u?(double)scene->width:(double)scene->height;
        count=clip_screen(screen[from],count,screen[to],plane,bound);
        if (count>=CLIP_CAPACITY) return 0;
        if (!count) return 1;
        for (size_t i=0;i<count;++i) if (!screen_finite(screen[to][i])) return 0;
    }
    *out_count=count;
    return 1;
}

int portable_scene_3d_triangle_validate(const PortableScene3D *scene,
    const PortableScene3DProjection *projection,const PortableScene3DVertex vertices[3])
{
    ScreenVertex screen[2][CLIP_CAPACITY];size_t count;
    return prepare_triangle(scene,projection,vertices,screen,&count);
}

int portable_scene_3d_triangle(PortableScene3D *scene,
    const PortableScene3DProjection *projection,const PortableScene3DVertex vertices[3],
    PortableScene3DSampler sampler,const void *userdata)
{
    if(!sampler)return 0;
    ScreenVertex screen[2][CLIP_CAPACITY];size_t count;
    if(!prepare_triangle(scene,projection,vertices,screen,&count))return 0;
    for(size_t i=1u;i+1u<count;++i)
        raster_triangle(scene,screen[0][0],screen[0][i],screen[0][i+1u],sampler,userdata);
    return 1;
}

int portable_scene_3d_quad_readonly(PortableScene3D *scene,
    const PortableScene3DProjection *projection, const PortableScene3DVertex vertices[4],
    PortableScene3DSampler sampler, const void *userdata)
{
    if (!vertices || !sampler) return 0;
    const PortableScene3DVertex first[3] = {vertices[0], vertices[1], vertices[2]};
    const PortableScene3DVertex second[3] = {vertices[0], vertices[2], vertices[3]};
    if (!portable_scene_3d_triangle_validate(scene, projection, first) ||
        !portable_scene_3d_triangle_validate(scene, projection, second)) return 0;
    const int parallel = portable_scene_3d_workers_quad(scene, projection, vertices, sampler, userdata);
    if (parallel >= 0) return parallel;
    return portable_scene_3d_triangle(scene, projection, first, sampler, userdata) &&
        portable_scene_3d_triangle(scene, projection, second, sampler, userdata);
}
