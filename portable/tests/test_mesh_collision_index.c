#include "mesh_collision_index.h"
#include "mesh_query.h"
#include "mesh_course_fixture.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define CHECK(x) do { if(!(x)){fprintf(stderr,"collision index line%d: %s\n",__LINE__,#x);return 1;} } while(0)
static unsigned triangle_reads;
static int counted_triangle(const void *context,uint32_t id,PortableMeshTriangle *out)
{
    const PortableMeshCourse *original=context;
    ++triangle_reads;
    return original->triangle(original->context,id,out);
}
static int counted_surface(const void *context,uint32_t id,PortableMeshSurface *out)
{
    const PortableMeshCourse *original=context;
    return original->surface(original->context,id,out);
}
static int counted_next(const void *context,const int32_t low[3],const int32_t high[3],
                        uint32_t first,uint32_t *out)
{
    const PortableMeshCourse *original=context;
    return original->next_triangle(original->context,low,high,first,out);
}
static PortableMeshCourse counted(const PortableMeshCourse *course,int accelerated)
{
    return (PortableMeshCourse){.context=course,.triangle_count=course->triangle_count,
        .gravity_q16=course->gravity_q16,.triangle=counted_triangle,.surface=counted_surface,
        .next_triangle=accelerated?counted_next:NULL};
}
/* Authored coplanar duplicates test canonical-ID ties even after spatial BVH
 * reordering; distant translated copies exercise culling and source lifetime. */
static void spread(PortableTestMeshCourse *f,uint8_t vertices[48*32],uint8_t triangles[64*32])
{
    test_mesh_course_init(f);
    for(unsigned patch=0;patch<32;++patch)
    {
        memcpy(vertices+48*patch,f->vertices,48);
        memcpy(triangles+64*patch,f->triangles,64);
        const int32_t x=patch<2?0:(int32_t)patch*80*Q;
        for(unsigned v=0;v<4;++v)
        {
            const int32_t base=(v==1 || v==2)?20*Q:-20*Q;
            test_mesh_word(vertices+48*patch+12*v,(uint32_t)(base+x));
        }
        for(unsigned t=0;t<2;++t)
        {
            const unsigned ids[2][3]={{0,1,2},{0,2,3}};
            for(unsigned a=0;a<3;++a)
                test_mesh_word(triangles+64*patch+32*t+4*a,patch*4+ids[t][a]);
        }
    }
    f->course.tables[2].bytes=vertices;f->course.tables[2].count=128;
    f->course.tables[3].bytes=triangles;f->course.tables[3].count=64;
}
static int queries_and_lifetime(void)
{
    PortableTestMeshCourse fixture;
    uint8_t vertices[48*32],triangles[64*32];spread(&fixture,vertices,triangles);
    char error[192];
    PortableMeshCollisionIndex *index=portable_mesh_collision_index_create(&fixture.course,error,sizeof error);
    if(!index)fprintf(stderr,"%s\n",error);
    CHECK(index);
    const PortableMeshCourse *original=portable_mesh_collision_index_course(index);
    PortableMeshCourse linear=counted(original,0),fast=counted(original,1);
    const int32_t center[3]={0,0,2*Q},before[3]={0,0,3*Q},after[3]={0,0,-Q};
    PortableMeshSupportHit a={0},b={0};
    triangle_reads=0;
    CHECK(portable_mesh_query_support(&linear,center,Q/2,3*Q,536870912,&a)==PORTABLE_MESH_QUERY_HIT);
    const unsigned full=triangle_reads;
    triangle_reads=0;
    CHECK(portable_mesh_query_support(&fast,center,Q/2,3*Q,536870912,&b)==PORTABLE_MESH_QUERY_HIT);
    CHECK(!memcmp(&a,&b,sizeof a) && a.triangle==0 && triangle_reads<full && full==64);
    PortableMeshFaceHit c={0},d={0};
    CHECK(portable_mesh_query_sweep_faces(&linear,before,after,Q/2,&c)==PORTABLE_MESH_QUERY_HIT);
    CHECK(portable_mesh_query_sweep_faces(&fast,before,after,Q/2,&d)==PORTABLE_MESH_QUERY_HIT);
    CHECK(!memcmp(&c,&d,sizeof c) && c.triangle==0);
    int32_t low[3]={-20*Q,-20*Q,0},high[3]={20*Q,20*Q,0};
    uint32_t out=99;
    for(uint32_t first=0;first<4;++first)
    {
        CHECK(original->next_triangle(original->context,low,high,first,&out)==1);
        CHECK(out==first);
    }
    out=99;
    CHECK(original->next_triangle(original->context,low,high,4,&out)==0 && out==99);
    low[0]=high[0]+1;
    CHECK(original->next_triangle(original->context,low,high,0,&out)==-1 && out==99);
    /* The source can disappear or mutate: every callback reads the snapshot. */
    memset(vertices,0,sizeof vertices);memset(triangles,0,sizeof triangles);
    memset(fixture.surface,0,sizeof fixture.surface);memset(fixture.meta,0,sizeof fixture.meta);
    CHECK(portable_mesh_query_support(&fast,center,Q/2,3*Q,536870912,&b)==PORTABLE_MESH_QUERY_HIT);
    CHECK(!memcmp(&a,&b,sizeof a));
    PortableMeshTriangle untouched={0};
    CHECK(!original->triangle(original->context,64,&untouched));
    CHECK(!portable_mesh_collision_index_course(NULL));
    portable_mesh_collision_index_close(index);portable_mesh_collision_index_close(NULL);
    return 0;
}
static int malformed_preflight(void)
{
    PortableTestMeshCourse fixture;uint8_t vertices[48*32],triangles[64*32];
    char error[192];
    spread(&fixture,vertices,triangles);
    /* A far row that no local query would visit still rejects publication. */
    test_mesh_word(triangles+63*32+12,1);
    CHECK(!portable_mesh_collision_index_create(&fixture.course,error,sizeof error));
    CHECK(strstr(error,"COLT row=63"));
    spread(&fixture,vertices,triangles);
    test_mesh_word(triangles+63*32,128);
    CHECK(!portable_mesh_collision_index_create(&fixture.course,error,sizeof error));
    CHECK(strstr(error,"vertex reference"));
    spread(&fixture,vertices,triangles);
    memcpy(vertices+127*12,vertices+126*12,12);
    CHECK(!portable_mesh_collision_index_create(&fixture.course,error,sizeof error));
    CHECK(strstr(error,"degenerate"));
    spread(&fixture,vertices,triangles);test_mesh_word(fixture.surface,10);
    CHECK(!portable_mesh_collision_index_create(&fixture.course,error,sizeof error));
    CHECK(strstr(error,"SURF row=0"));
    spread(&fixture,vertices,triangles);test_mesh_word(triangles+28,0);
    CHECK(!portable_mesh_collision_index_create(&fixture.course,error,sizeof error));
    CHECK(strstr(error,"invalid mksc:motion"));
    return 0;
}
static int corrected_endpoint(void)
{
    PortableTestMeshCourse f;test_mesh_course_init(&f);
    uint8_t vertices[48*3*12]={0},triangles[48*32]={0};
    /* The same one-way correction chain used by test_mesh_kart: each face
     * pushes into the next canonical face, rather than trapping the kart
     * between opposing walls. Here the candidate owner is the actual BVH. */
    for(unsigned i=0;i<48;++i)
    {
        const int32_t x=(int32_t)i*Q*2/5;
        const int32_t points[3][3]={{x,-10*Q,-10*Q},{x,10*Q,-10*Q},{x,0,10*Q}};
        for(unsigned v=0;v<3;++v)
        {
            for(unsigned a=0;a<3;++a)
                test_mesh_word(vertices+(i*3+v)*12+a*4,(uint32_t)points[v][a]);
            test_mesh_word(triangles+i*32+v*4,i*3+v);
        }
        test_mesh_word(triangles+i*32+20,UINT32_MAX);
        test_mesh_word(triangles+i*32+24,UINT32_MAX);
        test_mesh_word(triangles+i*32+28,UINT32_MAX);
    }
    f.course.tables[2].bytes=vertices;f.course.tables[2].count=48*3;
    f.course.tables[3].bytes=triangles;f.course.tables[3].count=48;
    test_mesh_word(f.meta+84,0);
    test_mesh_word(f.surface,3);
    char error[192];
    PortableMeshCollisionIndex *index=portable_mesh_collision_index_create(&f.course,error,sizeof error);
    CHECK(index);
    const PortableMeshCourse *source=portable_mesh_collision_index_course(index);
    PortableMeshCourse linear=*source;linear.next_triangle=NULL;
    const int32_t position[3]={0,0,0},forward[3]={0,-1073741824,0};
    PortableMeshKartState a,b;
    CHECK(portable_mesh_kart_init(&a,&f.rules,position,forward)==PORTABLE_MESH_OK);
    b=a;
    PortableMeshKartEvents ea={0},eb={0};
    CHECK(portable_mesh_kart_tick(&a,&f.rules,&linear,(PortableMeshKartInput){0},&ea)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&b,&f.rules,source,(PortableMeshKartInput){0},&eb)==PORTABLE_MESH_OK);
    CHECK(a.position_q16[0]>19*Q && ea.wall);
    CHECK(!memcmp(&a,&b,sizeof a) && !memcmp(&ea,&eb,sizeof ea));
    portable_mesh_collision_index_close(index);return 0;
}

static int jump_ceiling_snapshot(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    const uint8_t strings[]="mksc:jump-max-distance";
    uint8_t property[32]={0};
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_FIXED);
    test_mesh_word(property+8u,4*Q);
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S','T','R','S'),sizeof strings,1,strings};
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('P','R','O','P'),1,32,property};
    test_mesh_word(f.surface,5);
    test_mesh_word(f.surface+28u,9*Q);
    test_mesh_word(f.surface+44u,1);
    char error[192];
    PortableMeshCollisionIndex *index=portable_mesh_collision_index_create(&f.course,error,sizeof error);
    CHECK(index);
    const PortableMeshCourse *cached=portable_mesh_collision_index_course(index);
    PortableMeshSurface decoded;
    CHECK(cached->surface(cached->context,0,&decoded) && decoded.jump_max_distance_q16==4*Q);
    PortableMeshRacer a,b;
    CHECK(portable_mesh_time_trial_init(&a,&f.course,&f.rules,0,error,sizeof error)==PORTABLE_MESH_OK);
    a.state.phase=PORTABLE_MESH_RACER_RACING;
    a.state.countdown_ticks=0;
    a.state.kart.grounded=1;
    a.state.kart.contact_surface=0;
    a.state.kart.velocity_q16[1]=-5*Q;
    a.state.kart.speed_q16=5*Q; /* Supported traction retains this longitudinal speed. */
    memcpy(&b,&a,sizeof b);
    b.geometry=cached;
    PortableMeshRacerTurn ea,eb;
    CHECK(portable_mesh_racer_move(&a,(PortableMeshKartInput){0},&ea,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_racer_move(&b,(PortableMeshKartInput){0},&eb,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&a.state,&b.state,sizeof a.state) && !memcmp(&ea,&eb,sizeof ea));
    CHECK(ea.events.hopped && a.state.kart.velocity_q16[2]>3*Q && a.state.kart.velocity_q16[2]<4*Q);
    test_mesh_word(property+8u,0); /* Published BVH owns its decoded property. */
    CHECK(cached->surface(cached->context,0,&decoded) && decoded.jump_max_distance_q16==4*Q);
    CHECK(!portable_mesh_collision_index_create(&f.course,error,sizeof error));
    CHECK(strstr(error,"SURF row=0"));
    portable_mesh_collision_index_close(index);
    return 0;
}

static int motion_pivot_property(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    const uint8_t names[] = "mksc:motion\0spin\0mksc:motion-axis\0mksc:spin-rate\0mksc:motion-pivot";
    uint8_t object[64] = {0}, properties[4 * 32] = {0};
    const uint32_t rows[4][5] = {{0, 3, 12, 0, 0}, {17, 7, 0, 0, Q},
                                {34, 0, 10000000, 0, 0}, {49, 7, 2*Q, 3*Q, 4*Q}};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned word = 0; word < 5; ++word)
            test_mesh_word(properties + row * 32 + word * 4, rows[row][word]);
    test_mesh_word(object + 12, 10 * Q);
    test_mesh_word(object + 60, 4);
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S','T','R','S'), sizeof names, 1, names};
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('O','B','J','S'), 1, 64, object};
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('P','R','O','P'), 4, 32, properties};
    PortableMeshMotion motion;
    CHECK(portable_mesh_collision_object_motion(&f.course, 0, &motion) == 1);
    CHECK(motion.pivot_q16[0] == 2*Q && motion.pivot_q16[1] == 3*Q && motion.pivot_q16[2] == 4*Q);
    test_mesh_word(properties + 3*32 + 4, 0);
    test_mesh_word(properties + 3*32 + 12, 0);
    test_mesh_word(properties + 3*32 + 16, 0);
    CHECK(portable_mesh_collision_object_motion(&f.course, 0, &motion) == -1);
    test_mesh_word(object + 60, 3); /* Old objects retain their placement pivot. */
    CHECK(portable_mesh_collision_object_motion(&f.course, 0, &motion) == 1);
    CHECK(motion.pivot_q16[0] == 10*Q && !motion.pivot_q16[1] && !motion.pivot_q16[2]);
    return 0;
}

/* A SURF row's mksc:present-in keeps its collision only in the named races. */
static int race_presence(void)
{
    CHECK(portable_course_3d_presence(1u, 0u) == 1u && portable_course_3d_presence(2u, 2u) == 1u << 6 &&
          portable_course_3d_presence(16u, 1u) == 1u << 17 &&
          portable_course_3d_presence(0u, 0u) == PORTABLE_COURSE_3D_PRESENT_ALL &&
          portable_course_3d_presence(4u, 4u) == PORTABLE_COURSE_3D_PRESENT_ALL);
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    const uint8_t names[] = "mksc:present-in";
    uint8_t property[32] = {0};
    test_mesh_word(property + 4, PORTABLE_COURSE_3D_PROPERTY_INTEGER);
    test_mesh_word(property + 8, 0x11F1u); /* 50cc races and every Time Trial */
    test_mesh_word(f.surface + 44, 1);
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S','T','R','S'), sizeof names, 1, names};
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('P','R','O','P'), 1, 32, property};
    const int32_t low[3] = {-Q, -Q, -Q}, high[3] = {Q, Q, Q};
    const uint32_t races[4] = {portable_course_3d_presence(1u, 0u), portable_course_3d_presence(2u, 2u),
                               portable_course_3d_presence(1u, 1u), PORTABLE_COURSE_3D_PRESENT_ALL};
    for (unsigned i = 0; i < 4u; ++i)
    {
        char error[192];
        PortableMeshCollisionIndex *index =
            portable_mesh_collision_index_create_present(&f.course, races[i], error, sizeof error);
        CHECK(index);
        const PortableMeshCourse *course = portable_mesh_collision_index_course(index);
        uint32_t found = UINT32_MAX;
        PortableMeshTriangle row;
        CHECK(course->next_triangle(course->context, low, high, 0, &found) == (i == 2u ? 0 : 1));
        CHECK(course->triangle(course->context, 0, &row)); /* absent rows keep their IDs */
        portable_mesh_collision_index_close(index);
    }
    return 0;
}

/* mksc:hazard-box strikes from the turned box's point nearest the kart, and
 * a row absent from the race (mksc:present-in) never strikes. */
static int box_hazard(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    const uint8_t names[] = "mksc:hazard-radius\0mksc:hazard-box\0mksc:present-in";
    uint8_t object[64] = {0}, properties[3 * 32] = {0};
    const uint32_t rows[3][5] = {{0, PORTABLE_COURSE_3D_PROPERTY_FIXED, 3*Q, 0, 0},
                                 {19, PORTABLE_COURSE_3D_PROPERTY_VECTOR, Q, 3*Q, Q},
                                 {35, PORTABLE_COURSE_3D_PROPERTY_INTEGER, 0xFFFFFu & ~0xF0u, 0, 0}};
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned word = 0; word < 5; ++word)
            test_mesh_word(properties + row * 32 + word * 4, rows[row][word]);
    test_mesh_word(object + 12, 10 * Q);
    test_mesh_word(object + 32, 759250125u); /* a quarter turn about up: forward runs along -x */
    test_mesh_word(object + 36, 759250125u);
    test_mesh_word(object + 60, 3);
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S','T','R','S'), sizeof names, 1, names};
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('O','B','J','S'), 1, 64, object};
    f.course.tables[f.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('P','R','O','P'), 3, 32, properties};
    char error[192];
    int32_t centers[1][3], radii[1];
    uint8_t reactions[1];
    PortableMeshCollisionIndex *index =
        portable_mesh_collision_index_create_present(&f.course, portable_course_3d_presence(2u, 2u), error, sizeof error);
    if (!index) fprintf(stderr, "%s\n", error);
    CHECK(index);
    const PortableMeshCourse *course = portable_mesh_collision_index_course(index);
    CHECK(portable_mesh_collision_hazards(course, 0, centers, radii, reactions) == 0);
    portable_mesh_collision_index_close(index);
    index = portable_mesh_collision_index_create_present(&f.course, portable_course_3d_presence(1u, 2u), error, sizeof error);
    CHECK(index);
    course = portable_mesh_collision_index_course(index);
    const int32_t inside[3] = {10*Q + 5*Q/2, Q/2, 0}, beside[3] = {10*Q, 5*Q/2, Q/4};
    CHECK(portable_mesh_collision_hazards_near(course, 0, inside, centers, radii, reactions) == 1);
    for (unsigned k = 0; k < 3; ++k) CHECK(centers[0][k] >= inside[k] - 4 && centers[0][k] <= inside[k] + 4);
    CHECK(radii[0] == 655);
    CHECK(portable_mesh_collision_hazards_near(course, 0, beside, centers, radii, reactions) == 1);
    CHECK(centers[0][0] >= 10*Q - 4 && centers[0][0] <= 10*Q + 4 && centers[0][1] >= Q - 4 && centers[0][1] <= Q + 4 &&
          centers[0][2] >= Q/4 - 4 && centers[0][2] <= Q/4 + 4);
    CHECK(portable_mesh_collision_hazards(course, 0, centers, radii, reactions) == 1 && radii[0] == 3*Q &&
          centers[0][0] == 10*Q);
    portable_mesh_collision_index_close(index);
    test_mesh_word(properties + 32 + 12, 4*Q); /* a box wider than its radius is malformed */
    CHECK(!portable_mesh_collision_index_create_present(&f.course, portable_course_3d_presence(1u, 2u), error, sizeof error));
    CHECK(strstr(error, "mksc:hazard-box"));
    return 0;
}

int main(void)
{
    CHECK(!motion_pivot_property());
    CHECK(!race_presence());
    CHECK(!box_hazard());
    CHECK(!jump_ceiling_snapshot());
    CHECK(!queries_and_lifetime());CHECK(!malformed_preflight());CHECK(!corrected_endpoint());
    puts("mesh collision index: PASS");return 0;
}
