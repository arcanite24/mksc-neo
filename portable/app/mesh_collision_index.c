#include "mesh_collision_index.h"
#include "mesh_racer.h"
#include "mesh_query.h"
#include "fixed_3d.h"
#include "mesh_motion.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LIMIT PORTABLE_FIXED_3D_POSITION_LIMIT
#define ABSENT UINT32_MAX
#define TAG(a,b,c,d) PORTABLE_COURSE_3D_TAG(a,b,c,d)
enum { TRIANGLE_CAP=262144, VERTEX_CAP=131072, SURFACE_CAP=256, LEAF_SIZE=8 };
typedef struct
{
    PortableMeshTriangle triangle;
    int32_t normal[3], low[3], high[3];
    uint32_t motion; /* ABSENT: static */
} IndexedTriangle;
typedef struct
{
    int32_t low[3], high[3];
    uint32_t minimum_id, maximum_id, left, right, first, count;
} Node;
struct PortableMeshCollisionIndex
{
    PortableMeshCourse course;
    IndexedTriangle *triangles;
    PortableMeshSurface *surfaces;
    Node *nodes;
    uint32_t *order;
    uint32_t surface_count, node_count, motion_count;
    PortableMeshMotion motions[PORTABLE_MESH_COLLISION_MOTION_LIMIT];
    uint32_t object_of[PORTABLE_MESH_COLLISION_MOTION_LIMIT];
    uint32_t hazard_count;
    struct
    {
        int32_t center_q16[3], radius_q16;
        uint32_t motion; /* ABSENT: static */
        uint32_t period, active; /* live while tick % period < active; period 0: always */
        uint8_t reaction; /* PORTABLE_MESH_HAZARD_* */
        uint8_t box;      /* mksc:hazard-box: half extents across, along and up the rest axes */
        int32_t half_q16[3], axes_q30[3][3]; /* rest side, forward and up */
    } hazards[PORTABLE_MESH_COLLISION_HAZARD_LIMIT];
};
static void error_at(char *error,size_t capacity,uint32_t course,const char *table,uint32_t row,
                     const char *reason)
{
    if(error && capacity)snprintf(error,capacity,"3D collision index course=%u %s row=%u: %s",
        (unsigned)course,table,(unsigned)row,reason);
}
static const PortableCourse3DTable *table(const PortableCourse3DView *view,uint32_t tag,
                                        uint32_t stride,uint32_t cap,int required)
{
    const PortableCourse3DTable *t=portable_course_3d_table(view,tag);
    if(!t)return NULL;
    return t->stride==stride && t->count<=cap && (!required || t->count) &&
        (!t->count || t->bytes)?t:NULL;
}
static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
/* Integer property; an absent optional one leaves out unchanged. */
static int integer(const PortableCourse3DView *view,uint32_t first,uint32_t count,const char *key,int32_t *out,
                   int required)
{
    PortableCourse3DProperty p;
    if(!portable_course_3d_property_find(view,first,count,key,&p))return !required;
    if(p.type!=PORTABLE_COURSE_3D_PROPERTY_INTEGER)return 0;
    *out=p.value.integer;return 1;
}
static int cached_triangle(const void *context,uint32_t id,PortableMeshTriangle *out)
{
    const PortableMeshCollisionIndex *index=context;
    if(!index || !out || id>=index->course.triangle_count)return 0;
    *out=index->triangles[id].triangle;return 1;
}
static int cached_surface(const void *context,uint32_t id,PortableMeshSurface *out)
{
    const PortableMeshCollisionIndex *index=context;
    if(!index || !out || id>=index->surface_count)return 0;
    *out=index->surfaces[id];return 1;
}
static int overlaps(const Node *node,const int32_t low[3],const int32_t high[3])
{
    for(unsigned a=0;a<3;++a)if(node->high[a]<low[a] || node->low[a]>high[a])return 0;
    return 1;
}
/* Bounds are rest poses; `motion` keeps only rows of that moving object
 * (ABSENT: static rows), ANY keeps every row. */
#define ANY (ABSENT-1u)
static uint32_t find(const PortableMeshCollisionIndex *index,uint32_t node_id,
                     const int32_t low[3],const int32_t high[3],uint32_t first,uint32_t best,uint32_t motion)
{
    const Node *node=&index->nodes[node_id];
    if(node->maximum_id<first || node->minimum_id>=best || !overlaps(node,low,high))return best;
    if(node->count)
    {
        for(uint32_t i=0;i<node->count;++i)
        {
            const uint32_t id=index->order[node->first+i];
            if(id<first || id>=best)continue;
            const IndexedTriangle *t=&index->triangles[id];
            if(motion!=ANY && t->motion!=motion)continue;
            unsigned a=0;
            while(a<3 && t->high[a]>=low[a] && t->low[a]<=high[a])++a;
            if(a==3)best=id;
        }
        return best;
    }
    uint32_t a=node->left,b=node->right;
    if(index->nodes[b].minimum_id<index->nodes[a].minimum_id){uint32_t swap=a;a=b;b=swap;}
    best=find(index,a,low,high,first,best,motion);
    return find(index,b,low,high,first,best,motion);
}
static int next_triangle(const void *context,const int32_t low[3],const int32_t high[3],
                         uint32_t first,uint32_t *out)
{
    const PortableMeshCollisionIndex *index=context;
    if(!index || !low || !high || !out)return -1;
    /* Bounds may extend past the world while a sphere/probe touches its edge.
     * Integer comparisons require no narrowing, expansion or allocation here. */
    for(unsigned a=0;a<3;++a)if(low[a]>high[a])return -1;
    if(first>=index->course.triangle_count || !index->node_count)return 0;
    const uint32_t best=find(index,0,low,high,first,ABSENT,ANY);
    if(best==ABSENT)return 0;
    *out=best;return 1;
}
static int posed_triangle(const void *context,uint32_t id,PortableMeshTriangle *out)
{
    const PortableMeshCollisionPose *pose=context;
    if(!cached_triangle(pose->index,id,out))return 0;
    const uint32_t motion=pose->index->triangles[id].motion;
    if(motion!=ABSENT)
        for(unsigned v=0;v<3;++v)
        {
            const int32_t rest[3]={out->vertices_q16[v][0],out->vertices_q16[v][1],out->vertices_q16[v][2]};
            portable_mesh_pose_point(&pose->now[motion],rest,out->vertices_q16[v]);
        }
    return 1;
}
static int posed_surface(const void *context,uint32_t id,PortableMeshSurface *out)
{ return cached_surface(((const PortableMeshCollisionPose *)context)->index,id,out); }
/* Static rows answer the box itself; each moving object's rows answer the
 * box carried back into its rest pose. */
static int posed_next(const void *context,const int32_t low[3],const int32_t high[3],uint32_t first,uint32_t *out)
{
    const PortableMeshCollisionPose *pose=context;
    const PortableMeshCollisionIndex *index=pose->index;
    if(!low || !high || !out)return -1;
    for(unsigned a=0;a<3;++a)if(low[a]>high[a])return -1;
    if(first>=index->course.triangle_count)return 0;
    uint32_t best=find(index,0,low,high,first,ABSENT,ABSENT);
    for(uint32_t m=0;m<index->motion_count;++m)
    {
        const PortableMeshPose *p=&pose->now[m];
        int64_t rest_low[3]={INT64_MAX,INT64_MAX,INT64_MAX},rest_high[3]={INT64_MIN,INT64_MIN,INT64_MIN};
        for(unsigned corner=0;corner<8u;++corner)
        {
            int64_t d[3];
            for(unsigned a=0;a<3;++a)
                d[a]=(int64_t)((corner>>a)&1u?high[a]:low[a])-p->pivot_q16[a]-p->offset_q16[a];
            for(unsigned a=0;a<3;++a)
            {
                const int64_t at=(((int64_t)p->rotation_q30[a]*d[0]+(int64_t)p->rotation_q30[3+a]*d[1]+
                                   (int64_t)p->rotation_q30[6+a]*d[2])>>30)+p->pivot_q16[a];
                if(at<rest_low[a])rest_low[a]=at;
                if(at>rest_high[a])rest_high[a]=at;
            }
        }
        int32_t rl[3],rh[3];
        for(unsigned a=0;a<3;++a)
        {
            /* Rounding skin for the rotation. */
            rl[a]=portable_fixed_3d_clamp(rest_low[a]-4);
            rh[a]=portable_fixed_3d_clamp(rest_high[a]+4);
        }
        best=find(index,0,rl,rh,first,best,m);
    }
    if(best==ABSENT)return 0;
    *out=best;return 1;
}
static int posed_carry(const void *context,uint32_t id,int32_t position[3],int32_t forward[3],int32_t velocity[3])
{
    const PortableMeshCollisionPose *pose=context;
    if(id>=pose->index->course.triangle_count)return 0;
    const uint32_t motion=pose->index->triangles[id].motion;
    if(motion!=ABSENT)
        portable_mesh_pose_carry(&pose->before[motion],&pose->now[motion],position,forward,velocity);
    return 1;
}
const PortableMeshCourse *portable_mesh_collision_index_at(const PortableMeshCourse *course,uint32_t tick,
                                                           PortableMeshCollisionPose *storage)
{
    if(!course || course->triangle!=cached_triangle)return course;
    const PortableMeshCollisionIndex *index=course->context;
    if(!index->motion_count || !storage)return course;
    storage->index=index;
    for(uint32_t m=0;m<index->motion_count;++m)
    {
        portable_mesh_motion_pose(&index->motions[m],tick,&storage->now[m]);
        portable_mesh_motion_pose(&index->motions[m],tick?tick-1u:0u,&storage->before[m]);
    }
    storage->course=(PortableMeshCourse){.context=storage,.triangle_count=course->triangle_count,
        .gravity_q16=course->gravity_q16,.triangle=posed_triangle,.surface=posed_surface,
        .next_triangle=posed_next,.carry=posed_carry};
    return &storage->course;
}
uint32_t portable_mesh_collision_hazards(const PortableMeshCourse *course,uint32_t tick,
                                         int32_t centers_q16[][3],int32_t radii_q16[],uint8_t reactions[])
{
    return portable_mesh_collision_hazards_near(course,tick,NULL,centers_q16,radii_q16,reactions);
}
/* A box's point nearest `probe` (posed axes: R times the rest axes). */
static void box_nearest(const PortableMeshPose *pose,const int32_t half_q16[3],const int32_t axes_q30[3][3],
                        const int32_t center_q16[3],const int32_t probe_q16[3],int32_t out_q16[3])
{
    int64_t delta[3],at[3];
    for(unsigned k=0;k<3;++k){delta[k]=(int64_t)probe_q16[k]-center_q16[k];at[k]=center_q16[k];}
    for(unsigned a=0;a<3;++a)
    {
        int64_t axis[3];
        for(unsigned r=0;r<3;++r)
        {
            axis[r]=axes_q30[a][r];
            if(pose)
            {
                axis[r]=0;
                for(unsigned c=0;c<3;++c)axis[r]+=((int64_t)pose->rotation_q30[r*3u+c]*axes_q30[a][c])>>30;
            }
        }
        int64_t along=0;
        for(unsigned r=0;r<3;++r)along+=(delta[r]*axis[r])>>30;
        if(along>half_q16[a])along=half_q16[a];
        if(along< -half_q16[a])along=-half_q16[a];
        for(unsigned r=0;r<3;++r)at[r]+=(along*axis[r])>>30;
    }
    for(unsigned k=0;k<3;++k)out_q16[k]=(int32_t)at[k];
}
uint32_t portable_mesh_collision_hazards_near(const PortableMeshCourse *course,uint32_t tick,const int32_t probe_q16[3],
                                              int32_t centers_q16[][3],int32_t radii_q16[],uint8_t reactions[])
{
    const PortableMeshCollisionIndex *index=
        !course?NULL:course->triangle==cached_triangle?course->context:
        course->triangle==posed_triangle?((const PortableMeshCollisionPose *)course->context)->index:NULL;
    if(!index)return 0;
    for(uint32_t h=0;h<index->hazard_count;++h)
    {
        const uint32_t period=index->hazards[h].period;
        radii_q16[h]=!period || tick%period<index->hazards[h].active?index->hazards[h].radius_q16:0;
        if(reactions)reactions[h]=index->hazards[h].reaction;
        PortableMeshPose pose;
        const int moving=index->hazards[h].motion!=ABSENT;
        if(!moving)memcpy(centers_q16[h],index->hazards[h].center_q16,sizeof centers_q16[h]);
        else
        {
            portable_mesh_motion_pose(&index->motions[index->hazards[h].motion],tick,&pose);
            portable_mesh_pose_point(&pose,index->hazards[h].center_q16,centers_q16[h]);
        }
        /* A box strikes from its point nearest the probe, as a 1 cm sphere. */
        if(index->hazards[h].box && probe_q16 && radii_q16[h])
        {
            int32_t nearest[3];
            box_nearest(moving?&pose:NULL,index->hazards[h].half_q16,index->hazards[h].axes_q30,
                        centers_q16[h],probe_q16,nearest);
            memcpy(centers_q16[h],nearest,sizeof nearest);
            radii_q16[h]=655;
        }
    }
    return index->hazard_count;
}
int portable_mesh_collision_object_motion(const PortableCourse3DView *view,uint32_t object,PortableMeshMotion *out)
{
    const PortableCourse3DTable *objects=portable_course_3d_table(view,TAG('O','B','J','S'));
    if(!view || !out || !objects || objects->stride!=64u || object>=objects->count)return -1;
    const uint8_t *row=objects->bytes+64u*object;
    const uint32_t first=word(row+56u),count=word(row+60u);
    PortableCourse3DProperty p;
    if(!portable_course_3d_property_find(view,first,count,"mksc:motion",&p))return 0;
    if(p.type!=PORTABLE_COURSE_3D_PROPERTY_STRING || !p.value.string)return -1;
    PortableMeshMotion m;
    memset(&m,0,sizeof m);
    for(unsigned a=0;a<3;++a)m.pivot_q16[a]=(int32_t)word(row+12u+4u*a);
    const int spin=!strcmp(p.value.string,"spin"),swing=!strcmp(p.value.string,"swing");
    PortableCourse3DProperty pivot;
    if(portable_course_3d_property_find(view,first,count,"mksc:motion-pivot",&pivot))
    {
        if(pivot.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR)return -1;
        memcpy(m.pivot_q16,pivot.value.vector_q16,sizeof m.pivot_q16);
    }
    if(spin || swing)
    {
        if(!portable_course_3d_property_find(view,first,count,"mksc:motion-axis",&p) ||
           p.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR)return -1;
        for(unsigned a=0;a<3;++a)
        {
            if(p.value.vector_q16[a]< -65536 || p.value.vector_q16[a]>65536)return -1;
            m.axis_q30[a]=p.value.vector_q16[a]*16384;
        }
    }
    int32_t value=0;
    if(!integer(view,first,count,"mksc:motion-phase",&value,0) || value<0)return -1;
    m.phase=(uint32_t)value;
    value=0;
    if(spin)
    {
        m.kind=PORTABLE_MESH_MOTION_SPIN;
        if(!integer(view,first,count,"mksc:spin-rate",&m.rate,1))return -1;
        uint32_t *fields[3]={&m.run,&m.ramp,&m.stop};
        static const char *const keys[3]={"mksc:spin-run","mksc:spin-ramp","mksc:spin-stop"};
        for(unsigned k=0;k<3;++k)
        {
            value=0;
            if(!integer(view,first,count,keys[k],&value,0) || value<0)return -1;
            *fields[k]=(uint32_t)value;
        }
    }
    else if(swing)
    {
        m.kind=PORTABLE_MESH_MOTION_SWING;
        int32_t amplitude,period;
        if(!integer(view,first,count,"mksc:swing-amplitude",&amplitude,1) ||
           !integer(view,first,count,"mksc:swing-period",&period,1) || amplitude<=0 || period<=0)return -1;
        m.amplitude=(uint32_t)amplitude;m.period=(uint32_t)period;
    }
    else if(!strcmp(p.value.string,"route"))
    {
        m.kind=PORTABLE_MESH_MOTION_ROUTE;
        PortableCourse3DRoute route;
        if(!integer(view,first,count,"mksc:route-wait",&value,0) || value<0 ||
           !portable_course_3d_route(view,word(row+52u),&route) || route.node_count<2u ||
           route.node_count>PORTABLE_MESH_MOTION_POINT_LIMIT)return -1;
        m.wait=(uint32_t)value;m.point_count=route.node_count;
        PortableCourse3DProperty face;
        if(portable_course_3d_property_find(view,first,count,"mksc:route-face",&face))
        {
            if(face.type!=PORTABLE_COURSE_3D_PROPERTY_BOOLEAN || face.value.boolean>1u)return -1;
            m.face=face.value.boolean;
        }
        PortableCourse3DRouteNode nodes[PORTABLE_MESH_MOTION_POINT_LIMIT];
        for(uint32_t i=0;i<route.node_count;++i)
        {
            if(!portable_course_3d_route_node(view,route.node_first+i,&nodes[i]) || nodes[i].speed_q16<=0)return -1;
            memcpy(m.points_q16[i],nodes[i].position_q16,sizeof m.points_q16[i]);
        }
        /* Node i's speed (metres/second) covers the leg to the next node. */
        for(uint32_t i=0;i<route.node_count;++i)
        {
            const int32_t *a=nodes[i].position_q16,*b=nodes[(i+1u)%route.node_count].position_q16;
            int64_t d2=0;
            for(unsigned k=0;k<3;++k){const int64_t d=((int64_t)b[k]-a[k])>>4;d2+=d*d;}
            const int64_t distance=(int64_t)portable_fixed_3d_sqrt((uint64_t)d2)<<4;
            /* One tick is 4389/262144 s, as portable_fixed_3d_dt. */
            const int64_t ticks=(distance*262144+(int64_t)nodes[i].speed_q16*4389/2)/((int64_t)nodes[i].speed_q16*4389);
            m.travel[i]=ticks<1?1u:ticks>PORTABLE_MESH_MOTION_TICK_LIMIT?PORTABLE_MESH_MOTION_TICK_LIMIT:(uint32_t)ticks;
        }
    }
    else return -1;
    /* Optional chain sway: integer mksc:sway-link (1..16), vector
     * mksc:sway-axis, integers mksc:sway-amplitude/-period/-lag and fixed
     * mksc:sway-length (see PortableMeshMotion). */
    value=0;
    if(!integer(view,first,count,"mksc:sway-link",&value,0) || value<0)return -1;
    m.sway_link=(uint32_t)value;
    if(m.sway_link)
    {
        int32_t amplitude,period,lag=0;
        PortableCourse3DProperty axis,length;
        if(!integer(view,first,count,"mksc:sway-amplitude",&amplitude,1) ||
           !integer(view,first,count,"mksc:sway-period",&period,1) ||
           !integer(view,first,count,"mksc:sway-lag",&lag,0) || amplitude<=0 || period<=0 || lag<0 ||
           !portable_course_3d_property_find(view,first,count,"mksc:sway-axis",&axis) ||
           axis.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR ||
           !portable_course_3d_property_find(view,first,count,"mksc:sway-length",&length) ||
           length.type!=PORTABLE_COURSE_3D_PROPERTY_FIXED)return -1;
        for(unsigned a=0;a<3;++a)
        {
            if(axis.value.vector_q16[a]< -65536 || axis.value.vector_q16[a]>65536)return -1;
            m.sway_axis_q30[a]=axis.value.vector_q16[a]*16384;
        }
        m.sway_amplitude=(uint32_t)amplitude;m.sway_period=(uint32_t)period;m.sway_lag=(uint32_t)lag;
        m.sway_length_q16=length.value.fixed_q16;
    }
    if(!portable_mesh_motion_valid(&m))return -1;
    *out=m;
    return 1;
}
static int less(const PortableMeshCollisionIndex *index,uint32_t a,uint32_t b,unsigned axis)
{
    const IndexedTriangle *x=&index->triangles[a],*y=&index->triangles[b];
    const int64_t ac=(int64_t)x->low[axis]+x->high[axis];
    const int64_t bc=(int64_t)y->low[axis]+y->high[axis];
    return ac<bc || (ac==bc && a<b);
}
/* Stable iterative merge sorting uses caller-owned build scratch. Comparator
 * context is explicit, so simultaneous index construction has no globals. */
static void sort(PortableMeshCollisionIndex *index,uint32_t *scratch,uint32_t first,
                  uint32_t count,unsigned axis)
{
    for(uint32_t width=1;width<count;width*=2)
    {
        for(uint32_t start=0;start<count;start+=2*width)
        {
            uint32_t middle=start+width<count?start+width:count;
            const uint32_t end=middle+width<count?middle+width:count;
            uint32_t a=start,b=middle,out=start;
            while(a<middle && b<end)
                scratch[first+out++]=less(index,index->order[first+b],index->order[first+a],axis)
                    ?index->order[first+b++]:index->order[first+a++];
            while(a<middle)scratch[first+out++]=index->order[first+a++];
            while(b<end)scratch[first+out++]=index->order[first+b++];
        }
        memcpy(index->order+first,scratch+first,(size_t)count*sizeof *scratch);
    }
}
static uint32_t build(PortableMeshCollisionIndex *index,uint32_t *scratch,uint32_t first,uint32_t count)
{
    const uint32_t id=index->node_count++;
    Node *node=&index->nodes[id];
    node->minimum_id=ABSENT;
    for(unsigned a=0;a<3;++a){node->low[a]=INT32_MAX;node->high[a]=INT32_MIN;}
    for(uint32_t i=first;i<first+count;++i)
    {
        const uint32_t triangle=index->order[i];
        if(triangle<node->minimum_id)node->minimum_id=triangle;
        if(triangle>node->maximum_id)node->maximum_id=triangle;
        for(unsigned a=0;a<3;++a)
        {
            const IndexedTriangle *t=&index->triangles[triangle];
            if(t->low[a]<node->low[a])node->low[a]=t->low[a];
            if(t->high[a]>node->high[a])node->high[a]=t->high[a];
        }
    }
    if(count<=LEAF_SIZE){node->first=first;node->count=count;return id;}
    unsigned axis=0;
    for(unsigned a=1;a<3;++a)
        if((int64_t)node->high[a]-node->low[a]>(int64_t)node->high[axis]-node->low[axis])axis=a;
    sort(index,scratch,first,count,axis);
    const uint32_t half=count/2;
    node->left=build(index,scratch,first,half);
    node->right=build(index,scratch,first+half,count-half);
    return id;
}
void portable_mesh_collision_index_close(PortableMeshCollisionIndex *index)
{
    if(index){free(index->triangles);free(index->surfaces);free(index->nodes);free(index->order);free(index);}
}
const PortableMeshCourse *portable_mesh_collision_index_course(const PortableMeshCollisionIndex *index)
{ return index?&index->course:NULL; }
/* The motion slot of OBJS row `object`: 1 found or added, 0 static, -1 invalid, -2 full. */
static int motion_slot(PortableMeshCollisionIndex *index,const PortableCourse3DView *view,uint32_t object,
                       uint32_t *out)
{
    *out=ABSENT;
    for(uint32_t m=0;m<index->motion_count;++m)
        if(index->object_of[m]==object){*out=m;return 1;}
    PortableMeshMotion motion;
    const int moving=portable_mesh_collision_object_motion(view,object,&motion);
    if(moving<=0)return moving;
    if(index->motion_count==PORTABLE_MESH_COLLISION_MOTION_LIMIT)return -2;
    index->object_of[index->motion_count]=object;
    index->motions[index->motion_count]=motion;
    *out=index->motion_count++;
    return 1;
}
/* OBJS rows with mksc:hazard-radius (fixed metres) hit karts touching a
 * sphere about mksc:hazard-center (rest pose; default the row position). */
/* v rotated by the unit quaternion q (x, y, z, w; Q30). */
static void rotate_q30(const int32_t q[4],const int32_t v[3],int32_t out[3])
{
    /* t = 2 (q x v); out = v + w t + q x t */
    int64_t t[3],u[3];
    for(unsigned i=0;i<3;++i)
        t[i]=2*(((int64_t)q[(i+1)%3]*v[(i+2)%3]-(int64_t)q[(i+2)%3]*v[(i+1)%3])>>30);
    for(unsigned i=0;i<3;++i)
        u[i]=((int64_t)q[(i+1)%3]*t[(i+2)%3]-(int64_t)q[(i+2)%3]*t[(i+1)%3])>>30;
    for(unsigned i=0;i<3;++i)out[i]=(int32_t)(v[i]+((q[3]*t[i])>>30)+u[i]);
}
static int hazards(PortableMeshCollisionIndex *index,const PortableCourse3DView *view,uint32_t presence,
                   char *error,size_t capacity)
{
    const PortableCourse3DTable *objects=portable_course_3d_table(view,TAG('O','B','J','S'));
    if(!objects)return 1;
    if(objects->stride!=64u){error_at(error,capacity,view->id,"OBJS",0,"invalid stride");return 0;}
    for(uint32_t object=0;object<objects->count;++object)
    {
        const uint8_t *row=objects->bytes+64u*object;
        const uint32_t first=word(row+56u),count=word(row+60u);
        PortableCourse3DProperty p;
        if(!portable_course_3d_property_find(view,first,count,"mksc:hazard-radius",&p))continue;
        /* Objects absent from this race (mksc:present-in) never strike. */
        if(!portable_course_3d_present(view,first,count,presence))continue;
        if(index->hazard_count==PORTABLE_MESH_COLLISION_HAZARD_LIMIT)
        {error_at(error,capacity,view->id,"OBJS",object,"too many hazards");return 0;}
        if(p.type!=PORTABLE_COURSE_3D_PROPERTY_FIXED || p.value.fixed_q16<=0 || p.value.fixed_q16>64*65536)
        {error_at(error,capacity,view->id,"OBJS",object,"invalid mksc:hazard-radius");return 0;}
        index->hazards[index->hazard_count].radius_q16=p.value.fixed_q16;
        int32_t *center=index->hazards[index->hazard_count].center_q16;
        for(unsigned a=0;a<3;++a)center[a]=(int32_t)word(row+12u+4u*a);
        if(portable_course_3d_property_find(view,first,count,"mksc:hazard-center",&p))
        {
            if(p.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR)
            {error_at(error,capacity,view->id,"OBJS",object,"invalid mksc:hazard-center");return 0;}
            memcpy(center,p.value.vector_q16,sizeof p.value.vector_q16);
        }
        for(unsigned a=0;a<3;++a)
            if(center[a]< -LIMIT || center[a]>LIMIT)
            {error_at(error,capacity,view->id,"OBJS",object,"hazard outside fixed domain");return 0;}
        int32_t period=0,active=0;
        if(!integer(view,first,count,"mksc:hazard-period",&period,0) ||
           !integer(view,first,count,"mksc:hazard-active",&active,0) || period<0 ||
           period>PORTABLE_MESH_MOTION_TICK_LIMIT || (period && (active<=0 || active>period)) || (!period && active))
        {error_at(error,capacity,view->id,"OBJS",object,"invalid mksc:hazard-period/active");return 0;}
        uint8_t reaction=PORTABLE_MESH_HAZARD_BUMP;
        if(portable_course_3d_property_find(view,first,count,"mksc:spin-bumper",&p))
        {
            if(p.type!=PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
            {error_at(error,capacity,view->id,"OBJS",object,"invalid mksc:spin-bumper");return 0;}
            if(p.value.boolean)reaction=PORTABLE_MESH_HAZARD_SPIN_BUMPER;
        }
        if(portable_course_3d_property_find(view,first,count,"mksc:hazard-reaction",&p))
        {
            static const char *const names[]={"bump",NULL,"spin","tumble","squash"};
            uint8_t found=UINT8_MAX;
            for(uint8_t k=0;k<sizeof names/sizeof names[0];++k)
                if(names[k] && p.type==PORTABLE_COURSE_3D_PROPERTY_STRING && p.value.string &&
                   !strcmp(p.value.string,names[k]))found=k;
            if(found==UINT8_MAX || reaction==PORTABLE_MESH_HAZARD_SPIN_BUMPER)
            {error_at(error,capacity,view->id,"OBJS",object,"invalid mksc:hazard-reaction");return 0;}
            reaction=found;
        }
        index->hazards[index->hazard_count].box=0;
        if(portable_course_3d_property_find(view,first,count,"mksc:hazard-box",&p))
        {
            int32_t q[4];
            for(unsigned a=0;a<4;++a)q[a]=(int32_t)word(row+24u+4u*a);
            const int64_t norm=((int64_t)q[0]*q[0]+(int64_t)q[1]*q[1]+(int64_t)q[2]*q[2]+(int64_t)q[3]*q[3])>>30;
            int ok=p.type==PORTABLE_COURSE_3D_PROPERTY_VECTOR && norm>(1<<30)-(1<<20) && norm<(1<<30)+(1<<20);
            for(unsigned a=0;a<3;++a)
                ok&=p.value.vector_q16[a]>0 && p.value.vector_q16[a]<=index->hazards[index->hazard_count].radius_q16;
            if(!ok){error_at(error,capacity,view->id,"OBJS",object,"invalid mksc:hazard-box");return 0;}
            static const int32_t unit[3][3]={{1<<30,0,0},{0,1<<30,0},{0,0,1<<30}};
            for(unsigned a=0;a<3;++a)
            {
                index->hazards[index->hazard_count].half_q16[a]=p.value.vector_q16[a];
                rotate_q30(q,unit[a],index->hazards[index->hazard_count].axes_q30[a]);
            }
            index->hazards[index->hazard_count].box=1;
        }
        index->hazards[index->hazard_count].reaction=reaction;
        index->hazards[index->hazard_count].period=(uint32_t)period;
        index->hazards[index->hazard_count].active=(uint32_t)active;
        const int slot=motion_slot(index,view,object,&index->hazards[index->hazard_count].motion);
        if(slot<0)
        {error_at(error,capacity,view->id,"OBJS",object,slot==-2?"too many moving objects":"invalid mksc:motion");return 0;}
        ++index->hazard_count;
    }
    return 1;
}
PortableMeshCollisionIndex *portable_mesh_collision_index_create(const PortableCourse3DView *view,
                                                                 char *error,size_t capacity)
{
    return portable_mesh_collision_index_create_present(view,PORTABLE_COURSE_3D_PRESENT_ALL,error,capacity);
}
PortableMeshCollisionIndex *portable_mesh_collision_index_create_present(const PortableCourse3DView *view,
    uint32_t presence,char *error,size_t capacity)
{
    uint32_t present=0;
    if(!view || !view->id || view->table_count>32u){error_at(error,capacity,0,"course",0,"missing view");return NULL;}
    const PortableCourse3DTable *triangles=table(view,TAG('C','O','L','T'),32,TRIANGLE_CAP,1);
    const PortableCourse3DTable *vertices=table(view,TAG('C','O','L','V'),12,VERTEX_CAP,1);
    const PortableCourse3DTable *surfaces=table(view,TAG('S','U','R','F'),48,SURFACE_CAP,1);
    const PortableCourse3DTable *respawns=portable_course_3d_table(view,TAG('R','E','S','P'));
    PortableCourse3DMeta meta;
    if(!triangles || !vertices || !surfaces ||
       !table(view,TAG('M','E','T','A'),128,1,1) || !portable_course_3d_meta(view,&meta) ||
       meta.gravity_q16<0 || meta.gravity_q16>1024*65536 ||
       (respawns && (!table(view,TAG('R','E','S','P'),48,1024,0))))
    {error_at(error,capacity,view->id,"tables",0,"invalid collision tables/metadata");return NULL;}
    PortableMeshCollisionIndex *index=calloc(1,sizeof *index);
    uint32_t *scratch=NULL;
    if(!index)goto allocation;
    index->course=(PortableMeshCourse){.context=index,.triangle_count=triangles->count,
        .gravity_q16=meta.gravity_q16,.triangle=cached_triangle,.surface=cached_surface,
        .next_triangle=next_triangle};
    index->surface_count=surfaces->count;
    index->triangles=calloc(triangles->count,sizeof *index->triangles);
    index->surfaces=calloc(surfaces->count,sizeof *index->surfaces);
    index->nodes=calloc((size_t)triangles->count*2u,sizeof *index->nodes);
    index->order=malloc((size_t)triangles->count*sizeof *index->order);
    scratch=malloc((size_t)triangles->count*sizeof *scratch);
    if(!index->triangles || !index->surfaces || !index->nodes || !index->order || !scratch)goto allocation;
    for(uint32_t i=0;i<surfaces->count;++i)
    {
        PortableCourse3DSurface row;
        if(!portable_course_3d_surface(view,i,&row) || row.type>9u || row.flags || row.friction_q16<0 ||
           row.speed_ratio_q16<0 || row.rolling_resistance_q16<0 || row.restitution_q16<0 ||
           row.restitution_q16>65536 || row.boost_q16<0 || row.launch_q16<0 || row.gravity_ratio_q16<0)
        {error_at(error,capacity,view->id,"SURF",i,"invalid coefficients/type");goto failure;}
        portable_mesh_surface_from_course(&row,&index->surfaces[i]);
    }
    for(uint32_t i=0;i<triangles->count;++i)
    {
        PortableCourse3DCollision row;
        IndexedTriangle *out=&index->triangles[i];
        if(!portable_course_3d_collision(view,i,&row) || row.surface>=surfaces->count || row.flags ||
           (row.respawn!=ABSENT && (!respawns || row.respawn>=respawns->count)) ||
           row.gravity_zone!=ABSENT)
        {error_at(error,capacity,view->id,"COLT",i,"invalid reference/flags or unsupported gravity zone");goto failure;}
        out->triangle.surface=row.surface;out->triangle.respawn=row.respawn;out->motion=ABSENT;
        if(row.owner_object!=ABSENT)
        {
            /* Rows owned by a moving object hold its rest pose. */
            const int slot=motion_slot(index,view,row.owner_object,&out->motion);
            if(slot==-1){error_at(error,capacity,view->id,"OBJS",row.owner_object,"invalid mksc:motion");goto failure;}
            if(slot==-2){error_at(error,capacity,view->id,"COLT",i,"too many moving objects");goto failure;}
        }
        for(unsigned a=0;a<3;++a){out->low[a]=INT32_MAX;out->high[a]=INT32_MIN;}
        for(unsigned v=0;v<3;++v)
        {
            if(row.vertices[v]>=vertices->count ||
               !portable_course_3d_collision_vertex(view,row.vertices[v],out->triangle.vertices_q16[v]))
            {error_at(error,capacity,view->id,"COLT",i,"invalid vertex reference");goto failure;}
            for(unsigned a=0;a<3;++a)
            {
                const int32_t p=out->triangle.vertices_q16[v][a];
                if(p< -LIMIT || p>LIMIT)
                {error_at(error,capacity,view->id,"COLT",i,"position outside fixed domain");goto failure;}
                if(p<out->low[a])out->low[a]=p;
                if(p>out->high[a])out->high[a]=p;
            }
        }
        if(!portable_mesh_query_normal(&out->triangle,out->normal))
        {error_at(error,capacity,view->id,"COLT",i,"degenerate triangle");goto failure;}
        /* Rows of a surface absent from this race keep their ID but leave the tree. */
        PortableCourse3DSurface surface;
        if(portable_course_3d_surface(view,row.surface,&surface) &&
           portable_course_3d_present(view,surface.property_first,surface.property_count,presence))
            index->order[present++]=i;
    }
    if(!hazards(index,view,presence,error,capacity))goto failure;
    if(present)(void)build(index,scratch,0,present);
    free(scratch);
    if(error && capacity)error[0]=0;
    return index;
allocation:
    error_at(error,capacity,view->id,"allocation",0,"cannot allocate bounded collision index");
failure:
    free(scratch);portable_mesh_collision_index_close(index);return NULL;
}
