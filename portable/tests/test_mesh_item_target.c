#include "mesh_item_target.h"
#include "fixed_3d.h"
#include "mesh_course_fixture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "item target line %d: %s\n", __LINE__, #x); return 1; } } while (0)

typedef struct
{
    PortableTestMeshCourse base;
    uint8_t routes[64], nodes[80 * 32], edges[80 * 4];
} Fixture;
static PortableMeshItemRules rules(void)
{
    PortableMeshItemRules r = {.profile_id = 19, .revision = 1};
#define MINIMUM(name, minimum, maximum) r.name = minimum;
    PORTABLE_MESH_ITEM_FIELDS(MINIMUM)
#undef MINIMUM
    for (unsigned i = 0; i < 10; ++i)
        r.roulette.preview[i] = (uint16_t)(0x1000u + i);
    for (unsigned i = 0; i < 27; ++i)
        r.roulette.timeline[i] = 2;
    for (unsigned i = 0; i < 8; ++i)
    {
        r.player_weights[i][0] = (PortableGpItemWeight){0x1000, 1};
        r.cpu_weights[i][0] = (PortableGpItemWeight){0x1001, 1};
    }
    r.shell_speed_q16 = 12 * Q;
    r.shell_acceleration_q16 = Q;
    r.shell_homing_range_q16 = 20 * Q;
    r.shell_homing_turn_rate_q16 = Q;
    return r;
}
static PortableMeshItemMotionState motion(int32_t x, int32_t y, int32_t z)
{
    PortableMeshItemMotionState m = {.position_q16 = {x,y,z}, .velocity_q16 = {2*Q,0,0},
        .support_normal_q30 = {0,0,U}, .mode = PORTABLE_MESH_ITEM_SHELL, .grounded = 1};
    return m;
}
static void init(Fixture *f, unsigned route_count, unsigned node_count, unsigned edge_count)
{
    memset(f, 0, sizeof *f);
    test_mesh_course_init(&f->base);
    PortableCourse3DView *c = &f->base.course;
    c->tables[c->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R','T','H','D'),route_count,32,f->routes};
    c->tables[c->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R','T','N','D'),node_count,32,f->nodes};
    c->tables[c->table_count++] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R','T','E','D'),edge_count,4,f->edges};
}
static void route(Fixture *f, unsigned id, unsigned first, unsigned count)
{
    uint8_t *p = f->routes + id * 32;
    test_mesh_word(p,1); test_mesh_word(p+4,first); test_mesh_word(p+8,count);
    test_mesh_word(p+12,first);
}
static void node(Fixture *f, unsigned i, int32_t x, int32_t y, int32_t z,
                 unsigned first, unsigned count)
{
    uint8_t *p = f->nodes + i * 32;
    test_mesh_word(p,(uint32_t)x); test_mesh_word(p+4,(uint32_t)y);
    test_mesh_word(p+8,(uint32_t)z); test_mesh_word(p+12,Q/8);
    test_mesh_word(p+16,4*Q); test_mesh_word(p+20,first); test_mesh_word(p+24,count);
}
static void edge(Fixture *f, unsigned i, unsigned to) { test_mesh_word(f->edges+i*4,to); }
static void straight(Fixture *f)
{
    init(f,1,3,2); route(f,0,0,3);
    node(f,0,0,0,Q,0,1); node(f,1,4*Q,0,Q,1,1); node(f,2,8*Q,0,Q,2,0);
    edge(f,0,1); edge(f,1,2);
}
static int stacked_and_connected(void)
{
    Fixture f; init(&f,2,6,4); route(&f,0,0,3); route(&f,1,3,3);
    for (unsigned layer=0; layer<2; ++layer)
        for (unsigned i=0; i<3; ++i)
        {
            node(&f,layer*3+i,(int32_t)i*4*Q,0,(int32_t)(1+layer*4)*Q,
                 layer*2+i,i<2);
            if (i<2) edge(&f,layer*2+i,layer*3+i+1);
        }
    PortableMeshItemRules r=rules(); CHECK(portable_mesh_item_rules_valid(&r));
    PortableMeshItemMotionState m=motion(Q,0,Q);
    PortableMeshItemTargetCursor c,next;
    CHECK(portable_mesh_item_target_init(&c,&f.base.course,UINT32_MAX,&m)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(c.route==0 && c.from_node==0 && c.to_node==1);
    m.position_q16[2]=5*Q;
    CHECK(portable_mesh_item_target_init(&next,&f.base.course,UINT32_MAX,&m)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.route==1);
    /* Retained connectivity cannot jump to the closer stacked route. */
    int32_t v[3];
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.route==0 && next.to_node==1);
    m.position_q16[0]=5*Q; m.position_q16[2]=Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.from_node==1 && next.to_node==2 && next.transition_count==1);
    /* Terminal status is a caller policy, with neither output published. */
    c=next; m.position_q16[0]=9*Q;
    PortableMeshItemTargetCursor before; memcpy(&before,&next,sizeof before);
    int32_t oldv[3]; memcpy(oldv,v,sizeof oldv);
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_END_ROUTE);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    return 0;
}
static int branches_and_rollback(void)
{
    Fixture f; init(&f,1,4,3); route(&f,0,0,4);
    node(&f,0,0,0,Q,0,1); node(&f,1,2*Q,0,Q,1,2);
    node(&f,2,4*Q,2*Q,Q,3,0); node(&f,3,4*Q,-2*Q,Q,3,0);
    edge(&f,0,1); edge(&f,1,2); edge(&f,2,3);
    PortableMeshItemRules r=rules(); PortableMeshItemMotionState m=motion(Q,0,Q);
    PortableMeshItemTargetCursor c,next;
    CHECK(portable_mesh_item_target_init(&c,&f.base.course,0,&m)==PORTABLE_MESH_ITEM_TARGET_OK);
    m.position_q16[0]=2*Q;
    int32_t v[3],target[3]={4*Q,-2*Q,Q};
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,0,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.to_node==3 && next.selected_edge==2);
    target[1]=0;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,0,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.to_node==2 && next.selected_edge==1);
    PortableMeshItemTargetCursor before; memcpy(&before,&next,sizeof before);
    int32_t oldv[3]; memcpy(oldv,v,sizeof oldv);
    edge(&f,2,999); /* Invalid late branch must be rejected before any publish. */
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,0,v)==PORTABLE_MESH_ITEM_TARGET_INVALID);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    edge(&f,2,3); test_mesh_word(f.nodes+3*32,INT32_MAX);
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,0,v)==PORTABLE_MESH_ITEM_TARGET_INVALID);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    return 0;
}
/* Replace the floor with a vertical/overhead rectangular mesh obstruction. */
static void barrier(Fixture *f, int ceiling)
{
    const int32_t wall[4][3]={{-3*Q,Q,-Q},{3*Q,Q,-Q},{3*Q,Q,4*Q},{-3*Q,Q,4*Q}};
    const int32_t roof[4][3]={{-3*Q,-3*Q,2*Q},{3*Q,-3*Q,2*Q},{3*Q,3*Q,2*Q},{-3*Q,3*Q,2*Q}};
    for(unsigned i=0;i<4;++i) for(unsigned a=0;a<3;++a)
        test_mesh_word(f->base.vertices+i*12+a*4,(uint32_t)(ceiling?roof[i][a]:wall[i][a]));
}
static int direct_and_bounded_turn(void)
{
    Fixture f; straight(&f);
    PortableMeshItemRules r=rules(); PortableMeshItemMotionState m=motion(0,0,Q);
    PortableMeshItemTargetCursor c={0},next;
    int32_t target[3]={0,3*Q,Q},v[3];
    Fixture before; memcpy(&before,&f,sizeof before);
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.direct && v[0]>0 && v[1]>0 && v[2]==0);
    /* Independent geometric bound in radians; one Q16 angle unit margin is
       larger than integer output normalization error at this fixture speed. */
    const double angle=atan2((double)v[1],(double)v[0]);
    const double budget=6.2831853071795864769*4389.0/262144.0;
    CHECK(angle>0 && angle<=budget);
    const int64_t squared=portable_fixed_3d_dot(v,v);
    const int32_t expected=2*Q+portable_fixed_3d_dt(Q);
    CHECK(llabs((long long)portable_fixed_3d_sqrt((uint64_t)squared)-expected)<=2);
    CHECK(!memcmp(&f,&before,sizeof f));
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,0,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(!next.direct && v[1]==0);
    barrier(&f,0);
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(!next.direct); /* Wall blocks otherwise close same-stage target. */
    straight(&f); barrier(&f,1); target[1]=Q; target[2]=3*Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(!next.direct); /* Full XYZ ceiling sweep, not XY-only proximity. */
    straight(&f); target[0]=-3*Q; target[1]=0; target[2]=Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.direct && v[0]>0 && v[1]>0 && v[2]==0); /* Antipodal stays tangent. */
    m.velocity_q16[2]=9*Q; target[0]=3*Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(v[0]>=expected-2 && v[0]<=expected+2 && v[1]==0 && v[2]==0);
    /* Banked support: reversing along X must not turn away from its plane. */
    m.velocity_q16[2]=0;
    const int64_t bank[3]={0,3,4};
    CHECK(portable_fixed_3d_normalize(bank,m.support_normal_q30));
    target[0]=-3*Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(llabs(portable_fixed_3d_dot(v,m.support_normal_q30)/U)<=2);
    CHECK(v[0]>0 && v[1]>0 && v[2]<0);
    m.support_normal_q30[1]=0; m.support_normal_q30[2]=U;
    /* Air pursuit retains a real vertical component; ground projection is
       a declared input, not an unconditional world-Z flattening. */
    m.grounded=0; target[0]=3*Q; target[2]=3*Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(v[2]>0);
    m.grounded=1; target[2]=Q;
    m.velocity_q16[0]=100*Q; m.velocity_q16[2]=0;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(portable_fixed_3d_dot(v,v)<=(int64_t)r.shell_speed_q16*r.shell_speed_q16);
    return 0;
}
static int missing_and_budget(void)
{
    Fixture f; init(&f,0,0,0);
    PortableMeshItemRules r=rules(); PortableMeshItemMotionState m=motion(0,0,Q);
    PortableMeshItemTargetCursor c={0},next,before;
    memset(&next,0xa5,sizeof next); memcpy(&before,&next,sizeof before);
    int32_t v[3]={11,22,33},oldv[3]; memcpy(oldv,v,sizeof oldv);
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_NO_ROUTE);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    int32_t target[3]={3*Q,0,Q};
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_OK);
    CHECK(next.direct && !next.initialized);
    target[0]=0; target[2]=2*Q;
    memcpy(&before,&next,sizeof before); memcpy(oldv,v,sizeof oldv);
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,target,1,v)==PORTABLE_MESH_ITEM_TARGET_NO_DIRECTION);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    init(&f,1,68,67); route(&f,0,0,68);
    for(unsigned i=0;i<68;++i) { node(&f,i,(int32_t)i*Q/8,0,Q,i,i<67); if(i<67)edge(&f,i,i+1); }
    CHECK(portable_mesh_item_target_init(&c,&f.base.course,0,&m)==PORTABLE_MESH_ITEM_TARGET_OK);
    m.position_q16[0]=9*Q;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_BUDGET);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    c.course_id++;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_INVALID);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    c.course_id--;
    r.shell_speed_q16=0;
    CHECK(portable_mesh_item_target_steer(&next,&c,&f.base.course,&r,&m,NULL,0,v)==PORTABLE_MESH_ITEM_TARGET_INVALID);
    CHECK(!memcmp(&next,&before,sizeof next) && !memcmp(v,oldv,sizeof v));
    return 0;
}
int main(void)
{
    return stacked_and_connected() || branches_and_rollback() || direct_and_bounded_turn() || missing_and_budget();
}
