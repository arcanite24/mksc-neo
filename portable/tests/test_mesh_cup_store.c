#include "mesh_cup_store.h"
#include "test_temp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static int file_bytes(const char *path, uint8_t bytes[128])
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    size_t count = fread(bytes, 1, 128u, file);
    int extra = fgetc(file);
    int failed = ferror(file);
    return fclose(file) == 0 && count == 128u && extra == EOF && !failed;
}
int main(void)
{
    char directory[128], path[4096], other[4096];
    CHECK(test_temp_path(directory, sizeof directory, "mesh-cup-store", 1) == 0);
    PortableMeshCupIdentity identity = {.cup_id = UINT32_C(4000000000),
                                        .driver_id = 70000u,
                                        .engine_class = 2u,
                                        .course_ids = {65536u, 65537u, 65538u, 65539u}};
    identity.digest[0] = 19u;
    identity.digest[31] = 255u;
    CHECK(portable_mesh_cup_store_path(directory, &identity, path, sizeof path));
    PortableMeshCupRecord candidate = {.standing_rank = 2,
                                       .points = 24,
                                       .finish_ticks = {300000u, 2000u, 4000u, 3000u},
                                       .finish_ranks = {2, 1, 0, 3}};
    PortableMeshCupRecord out, before;
    memset(&out, 0xa5, sizeof out);
    before = out;
    int updated = 73;
    CHECK(portable_mesh_cup_store_load(path, &identity, &out) == PORTABLE_MESH_CUP_STORE_NOT_FOUND);
    CHECK(memcmp(&out, &before, sizeof out) == 0);
    CHECK(portable_mesh_cup_store_update(path, &identity, &candidate, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_OK);
    CHECK(updated == 1 && memcmp(&out, &candidate, sizeof out) == 0);
    uint8_t original[128], now[128];
    CHECK(file_bytes(path, original));
    CHECK(original[0] == 'M' && original[8] == 1u && original[12] == 128u && original[16] == 0u &&
          original[19] == 238u);
    PortableMeshCupIdentity mismatch = identity;
    mismatch.digest[7] = 1u;
    out = before;
    updated = 73;
    CHECK(portable_mesh_cup_store_load(path, &mismatch, &out) ==
          PORTABLE_MESH_CUP_STORE_INCOMPATIBLE);
    CHECK(portable_mesh_cup_store_update(path, &mismatch, &candidate, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_INCOMPATIBLE);
    CHECK(memcmp(&out, &before, sizeof out) == 0 && updated == 73);
    CHECK(file_bytes(path, now) && memcmp(now, original, 128u) == 0);
    /* Exhaust exclusive sibling names to force publication failure while a
     * valid previous medal exists. Neither it nor competing writers change. */
    char temporary[4160];
    for (unsigned i = 0; i < 32u; ++i)
    {
        CHECK(snprintf(temporary, sizeof temporary, "%s.tmp-%lu-%u", path,
                       (unsigned long)test_process_id(), i) > 0);
        FILE *staging = fopen(temporary, "wb");
        CHECK(staging != NULL);
        CHECK(fputc(77, staging) == 77 && fclose(staging) == 0);
    }
    PortableMeshCupRecord better = candidate;
    better.standing_rank = 1u;
    CHECK(portable_mesh_cup_store_update(path, &identity, &better, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_IO_ERROR);
    CHECK(memcmp(&out, &before, sizeof out) == 0 && updated == 73);
    CHECK(file_bytes(path, now) && memcmp(now, original, 128u) == 0);
    for (unsigned i = 0; i < 32u; ++i)
    {
        CHECK(snprintf(temporary, sizeof temporary, "%s.tmp-%lu-%u", path,
                       (unsigned long)test_process_id(), i) > 0);
        FILE *staging = fopen(temporary, "rb");
        CHECK(staging != NULL);
        CHECK(fgetc(staging) == 77 && fgetc(staging) == EOF && fclose(staging) == 0);
        CHECK(remove(temporary) == 0);
    }
    PortableMeshCupRecord worse = candidate;
    worse.standing_rank = 3;
    CHECK(portable_mesh_cup_store_update(path, &identity, &worse, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_OK);
    CHECK(updated == 0 && memcmp(&out, &candidate, sizeof out) == 0);
    CHECK(file_bytes(path, now) && memcmp(now, original, 128u) == 0);
    PortableMeshCupRecord invalid = candidate;
    invalid.finish_ranks[1] = 4u;
    out = before;
    updated = 73;
    CHECK(portable_mesh_cup_store_update(path, &identity, &invalid, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_INVALID);
    CHECK(memcmp(&out, &before, sizeof out) == 0 && updated == 73);
    CHECK(file_bytes(path, now) && memcmp(now, original, 128u) == 0);
    CHECK(portable_mesh_cup_store_path(directory, &mismatch, other, sizeof other) &&
          strcmp(path, other));
    CHECK(portable_mesh_cup_store_update(other, &mismatch, &candidate, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_OK);
    CHECK(portable_mesh_cup_store_load(other, &mismatch, &out) == PORTABLE_MESH_CUP_STORE_OK);
    CHECK(remove(other) == 0);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    original[116] = 1u;
    CHECK(fwrite(original, 1, sizeof original, file) == sizeof original && fclose(file) == 0);
    out = before;
    updated = 73;
    CHECK(portable_mesh_cup_store_update(path, &identity, &candidate, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_INVALID);
    CHECK(memcmp(&out, &before, sizeof out) == 0 && updated == 73);
    CHECK(remove(path) == 0);
    CHECK(snprintf(other, sizeof other, "%s/missing/record.cup", directory) > 0);
    CHECK(portable_mesh_cup_store_update(other, &identity, &candidate, &out, &updated) ==
          PORTABLE_MESH_CUP_STORE_IO_ERROR);
    CHECK(memcmp(&out, &before, sizeof out) == 0 && updated == 73);
    char subdirectory[4096];
    CHECK(snprintf(subdirectory, sizeof subdirectory, "%s/mesh3d", directory) > 0);
    CHECK(test_rmdir(subdirectory) == 0);
    CHECK(test_rmdir(directory) == 0);
    puts("portable mesh cup store: PASS");
    return 0;
}
