#include "render_pose.h"
#include "render_object_animation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int close_number(double a, double b)
{
    return fabs(a - b) < 1e-8;
}

static double dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int unit_frame(const PortableRenderPose *pose)
{
    return close_number(dot(pose->up, pose->up), 1.0) &&
           close_number(dot(pose->forward, pose->forward), 1.0) &&
           close_number(dot(pose->up, pose->forward), 0.0);
}

static int extracted_terrain(void)
{
    PortableMeshKartState kart = {0};
    kart.position_q16[0] = 2 * 65536;
    kart.position_q16[1] = -3 * 65536;
    kart.position_q16[2] = 4 * 65536;
    kart.up_q30[1] = 644245094; /* Ramp normal (0,.6,.8). */
    kart.up_q30[2] = 858993459;
    kart.forward_q30[1] = -1073741824;
    unsigned char before[sizeof kart];
    memcpy(before, &kart, sizeof kart);
    PortableRenderPose pose;
    CHECK(portable_render_pose_from_kart(&pose, &kart));
    CHECK(unit_frame(&pose));
    CHECK(pose.position[0] == 2.0 && pose.position[1] == -3.0 && pose.position[2] == 4.0);
    CHECK(close_number(pose.forward[1], -.8) && close_number(pose.forward[2], .6));
    CHECK(memcmp(before, &kart, sizeof kart) == 0);

    /* A bank rotates camera right and actor side in opposite conventions. */
    pose = (PortableRenderPose){.position = {2, -3, 4}, .forward = {0, -1, 0},
                                .up = {.6, 0, .8}};
    PortableRenderView view = {.x = 17, .y = 23, .width = 640, .height = 360,
                               .projection = {300, 301, 320, 172, .1, 1000},
                               .clear_argb = UINT32_C(0xff123456)};
    CHECK(portable_render_pose_chase(&pose, 5.0, 3.0, 0.0, &view));
    CHECK(close_number(view.position[0], 3.8) && close_number(view.position[1], 2.0) &&
          close_number(view.position[2], 6.4));
    CHECK(close_number(view.right[0], .8) && close_number(view.right[2], -.6));
    CHECK(view.x == 17 && view.y == 23 && view.width == 640 && view.height == 360 &&
          view.projection.focal_y == 301 && view.clear_argb == UINT32_C(0xff123456));
    CHECK(portable_render_pose_chase(&pose, 5.0, 3.0, -.30, &view));
    CHECK(close_number(dot(view.right, view.up), 0) &&
          close_number(dot(view.up, view.forward), 0) &&
          close_number(dot(view.right, view.forward), 0));
    CHECK(close_number(dot(view.up, view.up), 1) &&
          close_number(dot(view.forward, view.forward), 1));
    double transform[12];
    CHECK(portable_render_pose_actor(&pose, .75, transform));
    CHECK(close_number(transform[0], -.8) && close_number(transform[8], .6));
    CHECK(close_number(transform[3], 1.55) && transform[7] == -3.0 &&
          close_number(transform[11], 3.4));
    CHECK(transform[5] == -1.0 && close_number(transform[2], .6) &&
          close_number(transform[10], .8));
    return 0;
}

static int interpolation(void)
{
    const PortableRenderPose a = {.position = {0, 2, 4}, .forward = {0, -1, 0}, .up = {0, 0, 1}};
    const PortableRenderPose b = {.position = {8, 6, 2}, .forward = {.6, -.8, 0},
                                  .up = {.48, .36, .8}};
    PortableRenderPose pose;
    CHECK(portable_render_pose_interpolate(&pose, &a, &b, 32768u));
    CHECK(pose.position[0] == 4 && pose.position[1] == 4 && pose.position[2] == 3);
    CHECK(unit_frame(&pose) && pose.up[0] > 0 && pose.up[2] > .8 &&
          pose.forward[0] > 0 && pose.forward[0] < .6);
    CHECK(portable_render_pose_interpolate(&pose, &a, &b, 0u));
    for (unsigned axis = 0; axis < 3u; ++axis)
        CHECK(close_number(pose.position[axis], a.position[axis]) &&
              close_number(pose.forward[axis], a.forward[axis]) &&
              close_number(pose.up[axis], a.up[axis]));
    CHECK(portable_render_pose_interpolate(&pose, &a, &b, UINT32_MAX));
    for (unsigned axis = 0; axis < 3u; ++axis)
        CHECK(close_number(pose.position[axis], b.position[axis]) &&
              close_number(pose.forward[axis], b.forward[axis]) &&
              close_number(pose.up[axis], b.up[axis]));
    PortableRenderPose opposite = a;
    opposite.position[0] = 16;
    opposite.forward[1] = 1;
    CHECK(portable_render_pose_interpolate(&pose, &a, &opposite, 32768u));
    CHECK(pose.position[0] == 16 && pose.forward[1] == 1 && unit_frame(&pose));
    opposite = a;
    opposite.up[2] = -1;
    CHECK(portable_render_pose_interpolate(&pose, &a, &opposite, 32768u));
    CHECK(pose.up[2] == -1 && unit_frame(&pose));
    pose = a;
    CHECK(portable_render_pose_interpolate(&pose, &pose, &b, 32768u));
    CHECK(pose.position[0] == 4 && unit_frame(&pose));
    return 0;
}

static int malformed_transaction(void)
{
    const PortableRenderPose good = {.forward = {0, -1, 0}, .up = {0, 0, 1}};
    PortableRenderPose out = good, bad = good;
    unsigned char before[sizeof out];
    memcpy(before, &out, sizeof out);
    bad.up[2] = NAN;
    CHECK(!portable_render_pose_interpolate(&out, &good, &bad, 123u));
    CHECK(memcmp(before, &out, sizeof out) == 0);
    PortableMeshKartState kart = {0};
    CHECK(!portable_render_pose_from_kart(&out, &kart));
    CHECK(memcmp(before, &out, sizeof out) == 0);
    bad = good;
    bad.forward[1] = 0;
    bad.forward[2] = 1; /* Parallel axes have no defined terrain heading. */
    CHECK(!portable_render_pose_interpolate(&out, &good, &bad, 32768u));
    PortableRenderView view = {0};
    unsigned char view_before[sizeof view];
    memcpy(view_before, &view, sizeof view);
    CHECK(!portable_render_pose_chase(&good, -1, 3, -.3, &view));
    CHECK(memcmp(view_before, &view, sizeof view) == 0);
    double transform[12] = {1, 2, 3};
    double saved[12];
    memcpy(saved, transform, sizeof saved);
    CHECK(!portable_render_pose_actor(&bad, .75, transform));
    CHECK(memcmp(saved, transform, sizeof saved) == 0);
    CHECK(!portable_render_pose_actor(&good, INFINITY, transform));
    CHECK(!portable_render_pose_from_kart(NULL, &kart));
    CHECK(!portable_render_pose_interpolate(&out, NULL, &good, 0));
    return 0;
}

typedef struct { PortableMeshTriangle rows[2]; } SupportFixture;
static int support_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    if (index >= 2) return 0;
    *out = ((const SupportFixture *)context)->rows[index];
    return 1;
}
static int support_surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    (void)context;
    if (index) return 0;
    *out = (PortableMeshSurface){0};
    return 1;
}
static int support_seating(void)
{
    SupportFixture fixture = {0};
    for (unsigned i = 0; i < 2; ++i)
    {
        const int32_t vertices[3][3] = {{-655360,-655360,0}, {655360,-655360,0},
                                      {-655360,655360,0}};
        memcpy(fixture.rows[i].vertices_q16, vertices, sizeof vertices);
        for (unsigned v = 0; v < 3; ++v) fixture.rows[i].vertices_q16[v][2] = (int32_t)i * 262144;
    }
    PortableMeshCourse course = {.context=&fixture,.triangle_count=2,
                                 .triangle=support_triangle,.surface=support_surface};
    PortableMeshKartState kart = {.grounded=1,.contact_triangle=0};
    PortableRenderPose pose = {.position={0,0,.6},.forward={0,-1,0},.up={0,0,1}};
    PortableRenderSupport support;
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.6));
    CHECK(support.floor_valid && close_number(support.body.position[2],0) && !support.gap);
    CHECK(close_number(support.floor.position[2],0)); /* Never select the tunnel roof. */
    pose.position[2]=4.6; kart.contact_triangle=1;
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.6));
    CHECK(close_number(support.body.position[2],4) && close_number(support.floor.position[2],4));
    kart.grounded=0; pose.position[2]=2.6;
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.6));
    CHECK(close_number(support.body.position[2],2) && support.floor_valid);
    CHECK(fabs(support.floor.position[2])<.0001 && fabs(support.gap-2)<.0001);
    pose.position[2]=8;
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.6) && !support.floor_valid);
    for (unsigned v=0;v<3;++v)
        fixture.rows[0].vertices_q16[v][2]=fixture.rows[0].vertices_q16[v][0]/2;
    const double n[3]={-1/sqrt(5.0),0,2/sqrt(5.0)};
    for (unsigned a=0;a<3;++a) pose.position[a]=.6*n[a];
    kart.grounded=1; kart.contact_triangle=0;
    const PortableMeshKartState saved=kart;
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.6));
    CHECK(unit_frame(&support.body) && support.floor_valid);
    for (unsigned a=0;a<3;++a) CHECK(fabs(support.body.position[a])<1e-8);
    CHECK(fabs(dot(support.body.up,n)-1)<1e-8 && !memcmp(&saved,&kart,sizeof kart));
    /* A shrunken body's clearance uses the same accepted floor. */
    for (unsigned a=0;a<3;++a) pose.position[a]=.3*n[a];
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.3));
    for (unsigned a=0;a<3;++a) CHECK(fabs(support.body.position[a])<1e-8);
    PortableRenderSupport before=support;
    kart.contact_triangle=3;
    CHECK(!portable_render_pose_support(&support,&pose,&kart,&course,.3));
    CHECK(!memcmp(&before,&support,sizeof support));
    CHECK(!portable_render_pose_support(&support,&pose,&saved,&course,NAN));
    CHECK(!memcmp(&before,&support,sizeof support));
    /* Presentation honors a steep floor already admitted by authored physics. */
    for (unsigned v=0;v<3;++v)
        fixture.rows[0].vertices_q16[v][2]=fixture.rows[0].vertices_q16[v][0]*2;
    for (unsigned a=0;a<3;++a) pose.position[a]=.6*(a==0?-2:a==2?1:0)/sqrt(5.0);
    CHECK(portable_render_pose_support(&support,&pose,&saved,&course,.6));
    CHECK(support.body.up[2]<.5 && support.floor_valid);
    /* Perched on a wall's top edge, the kart's contact is the wall: seat it
     * on the floor probed below instead of failing presentation. */
    const int32_t flat[3][3] = {{-655360,-655360,0}, {655360,-655360,0}, {-655360,655360,0}};
    const int32_t wall[3][3] = {{0,-655360,0}, {0,655360,0}, {0,-655360,655360}};
    memcpy(fixture.rows[0].vertices_q16, flat, sizeof flat);
    memcpy(fixture.rows[1].vertices_q16, wall, sizeof wall);
    pose = (PortableRenderPose){.position={-.5,0,.6},.forward={0,-1,0},.up={0,0,1}};
    kart.contact_triangle=1;
    CHECK(portable_render_pose_support(&support,&pose,&kart,&course,.6));
    CHECK(support.floor_valid && fabs(support.floor.position[2])<.0001 && support.body.up[2]>.999);
    return 0;
}

static int object_motion(void)
{
    PortableMeshItemPose a = {.serial=9, .item=0x1000, .owner=1, .phase=2}, b = a;
    b.position_q16[0] = 4*65536;
    double position[3];
    portable_render_item_sample(&a, 1, &b, 32768, position);
    CHECK(position[0] == 2);
    ++b.serial; /* Recycled slot must not streak from the retired actor. */
    portable_render_item_sample(&a, 1, &b, 0, position);
    CHECK(position[0] == 4);
    b.serial = a.serial; b.phase++;
    portable_render_item_sample(&a, 1, &b, 0, position);
    CHECK(position[0] == 4);
    PortableMeshCourseActorView old = {.object_index=7, .visible=1, .affine_x_q8=256,
        .affine_y_q8=256, .rotation_angle=65000}, now = old;
    now.position_q16[2] = 2*65536; now.rotation_angle = 536; now.model_id=999;
    portable_render_course_actor_sample(&old, &now, 32768);
    CHECK(now.position_q16[2] == 65536 && now.rotation_angle == 0 && now.model_id == 999);
    old.visible = 0;
    portable_render_course_actor_sample(&old, &now, 0);
    CHECK(now.position_q16[2] == 65536);
    PortableRenderSupport sa = {.body={.forward={0,1,0},.up={0,0,1}}, .floor_valid=1}, sb, so;
    sa.floor = sa.body; sb = sa;
    sb.body.position[2] = sb.floor.position[2] = 2;
    CHECK(portable_render_support_interpolate(&so, &sa, &sb, 32768));
    CHECK(so.body.position[2] == 1 && so.floor.position[2] == 1);
    sb.floor_valid = 0;
    CHECK(portable_render_support_interpolate(&so, &sa, &sb, 32768));
    CHECK(!so.floor_valid && so.body.position[2] == 1);
    PortableRenderInstance sky_a = {0}, sky_b = {0};
    sky_a.uv_offset[0] = .9; sky_b.uv_offset[0] = .1;
    sky_a.transform[11] = 2; sky_b.transform[11] = 4;
    portable_render_sky_sample(&sky_a, &sky_b, 32768);
    CHECK(close_number(sky_b.uv_offset[0], 0) && sky_b.transform[11] == 3);
    sky_a.clip_y_extent = 160;
    portable_render_sky_sample(&sky_a, &sky_b, 0);
    CHECK(sky_b.transform[11] == 3); /* New split layout snaps. */
    PortableMeshMotion motion = {.kind=PORTABLE_MESH_MOTION_ROUTE, .point_count=2,
        .points_q16={{0,0,0},{10*65536,0,0}}, .travel={10,10}};
    CHECK(portable_mesh_motion_valid(&motion));
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance, NULL);
    portable_render_motion_sample(&motion, 2, 32768, &instance);
    CHECK(close_number(instance.transform[3], 1.5));
    CHECK(close_number(instance.transform[0], 1));
    portable_render_instance_identity(&instance, NULL);
    portable_render_motion_sample(&motion, 2, UINT32_MAX, &instance);
    CHECK(close_number(instance.transform[3], 2));
    motion = (PortableMeshMotion){.kind=PORTABLE_MESH_MOTION_SPIN,
        .axis_q30={0,0,1073741824}, .pivot_q16={2*65536,0,0}, .rate=67108864};
    CHECK(portable_mesh_motion_valid(&motion));
    portable_render_instance_identity(&instance, NULL);
    instance.transform[3] = 3;
    portable_render_motion_sample(&motion, 1, 32768, &instance);
    CHECK(fabs(instance.transform[3] - (2 + cos(3.141592653589793 / 64))) < 1e-4);
    CHECK(fabs(fabs(instance.transform[7]) - sin(3.141592653589793 / 64)) < 1e-4);
    CHECK(fabs(instance.transform[0]*instance.transform[0] + instance.transform[4]*instance.transform[4] - 1) < 1e-6);
    return 0;
}

int main(void)
{
    CHECK(object_motion() == 0);
    CHECK(extracted_terrain() == 0);
    CHECK(interpolation() == 0);
    CHECK(malformed_transaction() == 0);
    CHECK(support_seating() == 0);
    return 0;
}
