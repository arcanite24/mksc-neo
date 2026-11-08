#include "mesh_sky_progress.h"
#include <stdio.h>
#include <string.h>
#define CHECK(v)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(v))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "sky progress:%d: %s\n", __LINE__, #v);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static void put(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (i * 8));
}

typedef struct
{
    PortableCourse3DView course;
    uint8_t meta[128], properties[6 * 32], gates[4 * 64], strings[256];
    uint32_t offsets[6];
} Fixture;
static void fixture(Fixture *f)
{
    memset(f, 0, sizeof *f);
    static const char *keys[] = {"source:progress-stage-count", "source:progress-raw:2",
                                 "source:progress-raw:0",       "source:progress-raw:1",
                                 "source:progress-raw:1024",    "source:progress-raw:01"};
    unsigned used = 1;
    for (unsigned i = 0; i < 6; ++i)
    {
        f->offsets[i] = used;
        const size_t n = strlen(keys[i]) + 1;
        memcpy(f->strings + used, keys[i], n);
        used += (unsigned)n;
        put(f->properties + i * 32, f->offsets[i]);
    }
    put(f->properties + 8, 3);
    put(f->properties + 40, 92);
    put(f->properties + 72, 0);
    put(f->properties + 104, 7);
    put(f->meta + 100, 4);
    const uint32_t stages[] = {2, 0, 1, 2};
    for (unsigned i = 0; i < 4; ++i)
        put(f->gates + i * 64 + 48, stages[i]);
    f->course.id = 71;
    f->course.table_count = 4;
    f->course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, f->meta};
    f->course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 6, 32, f->properties};
    f->course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, f->strings};
    f->course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'), 4, 64, f->gates};
}
static int binding(void)
{
    Fixture f;
    fixture(&f);
    PortableMeshSkyProgressConfig config;
    char error[192];
    CHECK(portable_mesh_sky_progress_open(&f.course, &config, error, sizeof error));
    CHECK(config.enabled && config.stage_count == 3 && config.source_raw[0] == 0 &&
          config.source_raw[1] == 7 && config.source_raw[2] == 92 && !error[0]);
    const PortableMeshSkyProgressConfig original = config;
    for (unsigned bad = 0; bad < 16; ++bad)
    {
        fixture(&f);
        switch (bad)
        {
        case 0:
            put(f.properties + 8, 0);
            break;
        case 1:
            put(f.properties + 8, 1025);
            break;
        case 2:
            put(f.properties + 4, PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
            break;
        case 3:
            put(f.properties + 32, f.offsets[0]);
            break;
        case 4:
            put(f.properties + 96, f.offsets[2]);
            break;
        case 5:
            put(f.properties + 40, 128);
            break;
        case 6:
            put(f.properties + 72, 1);
            break;
        case 7:
            put(f.properties + 104, 93);
            break;
        case 8:
            put(f.meta + 100, 3);
            break;
        case 9:
            put(f.meta + 96, 1);
            put(f.meta + 100, 3);
            break;
        case 10:
            put(f.properties + 96, f.offsets[4]);
            break;
        case 11:
            put(f.properties + 96, f.offsets[5]);
            break;
        case 12:
            put(f.gates + 48, 3);
            break;
        case 13:
            put(f.gates + 128 + 48, 2);
            break;
        case 14:
            f.course.tables[3].stride = 4;
            break;
        default:
            f.course.tables[3].bytes = NULL;
            break;
        }
        CHECK(!portable_mesh_sky_progress_open(&f.course, &config, error, sizeof error));
        CHECK(strstr(error, "course=71 source progress") &&
              !memcmp(&config, &original, sizeof config));
    }
    fixture(&f);
    put(f.meta + 100, 0);
    CHECK(portable_mesh_sky_progress_open(&f.course, &config, error, sizeof error));
    CHECK(!config.enabled && !config.stage_count);
    return 0;
}
static int accepted_progress(void)
{
    const PortableMeshSkyProgressConfig config = {
        .enabled = 1, .stage_count = 4, .source_raw = {0, 7, 7, 92}};
    PortableMeshSkyProgress world;
    CHECK(portable_mesh_sky_progress_init(&config, &world));
    CHECK(world.high_water == 0x7f00 && world.route_wrap == 127 && world.route_index == 0);
    /* Golden original-region values across initial arming, a skipped raster
     * region, a parallel/hand-pass stage, recovery and a complete lap. */
    static const uint32_t cases[][6] = {{0, 0, 0, 0x7f00, 0, 127},  {0, 0, 1, 0x8000, 0, 128},
                                        {1, 0, 1, 0x8007, 7, 128},  {2, 0, 1, 0x8007, 7, 128},
                                        {3, 0, 1, 0x805c, 92, 128}, {0, 0, 0, 0x805c, 0, 128},
                                        {1, 0, 1, 0x805c, 7, 128},  {0, 1, 0, 0x8100, 0, 129},
                                        {3, 1, 1, 0x815c, 92, 129}, {0, 2, 0, 0x8200, 0, 130}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const uint32_t *row = cases[i];
        const PortableMeshSkyProgressInput input = {row[0], row[1], row[2]};
        CHECK(portable_mesh_sky_progress_update(&config, &world, &world, &input));
        CHECK(world.high_water == row[3] && world.route_index == row[4] &&
              world.route_wrap == row[5]);
    }
    const PortableMeshSkyProgress saved = world;
    const PortableMeshSkyProgressInput invalid[] = {
        {4, 2, 1}, {1, 128, 1}, {0, 2, 2}, {2, 2, 0}, {0, 1, 0}};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
    {
        CHECK(!portable_mesh_sky_progress_update(&config, &world, &world, &invalid[i]));
        CHECK(!memcmp(&world, &saved, sizeof world));
    }
    PortableMeshSkyProgress other;
    CHECK(portable_mesh_sky_progress_init(&config, &other));
    const PortableMeshSkyProgressInput first = {3, 0, 1}, last = {3, 127, 1};
    CHECK(portable_mesh_sky_progress_update(&config, &other, &other, &first));
    CHECK(other.high_water == 0x805c && world.high_water == 0x8200);
    CHECK(portable_mesh_sky_progress_update(&config, &other, &other, &last));
    CHECK(other.high_water == 0xff5c && other.route_wrap == 255);
    CHECK(portable_mesh_sky_progress_init(&config, &world));
    CHECK(world.high_water == 0x7f00);
    PortableMeshSkyProgressConfig absent = {0};
    CHECK(portable_mesh_sky_progress_init(&absent, &world));
    CHECK(portable_mesh_sky_progress_update(&absent, &world, &world, NULL));
    CHECK(!world.high_water && !world.route_wrap && !world.route_index);
    return 0;
}
int main(void)
{
    CHECK(!binding() && !accepted_progress());
    puts("mesh sky progress: PASS stage provenance high-water recovery lap rollback independence");
    return 0;
}
