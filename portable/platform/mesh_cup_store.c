#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "mesh_cup_store.h"
#include "file_replace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define store_open _open
#define store_close _close
#define store_fdopen _fdopen
#define store_pid _getpid
#else
#include <unistd.h>
#define store_open open
#define store_close close
#define store_fdopen fdopen
#define store_pid getpid
#endif

#define CUP_BYTES 128u
static const uint8_t cup_magic[8] = {'M', 'K', 'S', 'C', '3', 'C', 'U', 'P'};
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static void write_u32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (8u * i));
}
static uint64_t checksum(const uint8_t *bytes, size_t size)
{
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t i = 0u; i < size; ++i)
        value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    return value;
}

static int identity_valid(const PortableMeshCupIdentity *identity)
{
    if (!identity || !identity->cup_id || !identity->driver_id || identity->engine_class > 2u)
        return 0;
    for (unsigned i = 0; i < 4u; ++i)
    {
        if (!identity->course_ids[i])
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (identity->course_ids[j] == identity->course_ids[i])
                return 0;
    }
    return 1;
}
static int record_valid(const PortableMeshCupRecord *record)
{
    if (!record || record->standing_rank > 7u || record->points > 252u)
        return 0;
    for (unsigned i = 0; i < 4u; ++i)
        if (!record->finish_ticks[i] || record->finish_ranks[i] > 3u)
            return 0;
    return 1;
}
int portable_mesh_cup_store_path(const char *directory, const PortableMeshCupIdentity *identity,
                                 char *out, size_t capacity)
{
    if (!directory || !*directory || !out || !identity_valid(identity))
        return 0;
    char subdirectory[4096], path[4096], digest[65];
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32u; ++i)
    {
        digest[i * 2u] = hex[identity->digest[i] >> 4u];
        digest[i * 2u + 1u] = hex[identity->digest[i] & 15u];
    }
    digest[64] = 0;
    int count = snprintf(subdirectory, sizeof subdirectory, "%s/mesh3d", directory);
    if (count < 0 || (size_t)count >= sizeof subdirectory)
        return 0;
    count = snprintf(path, sizeof path, "%s/cup-%u-class-%u-driver-%u-%s.cup", subdirectory,
                     identity->cup_id, identity->engine_class, identity->driver_id, digest);
    if (count < 0 || (size_t)count >= sizeof path || (size_t)count >= capacity)
        return 0;
#ifdef _WIN32
    const int created = _mkdir(subdirectory);
    struct _stat info;
    if (created != 0 &&
        (errno != EEXIST || _stat(subdirectory, &info) != 0 || !(info.st_mode & _S_IFDIR)))
        return 0;
#else
    const int created = mkdir(subdirectory, 0700);
    struct stat info;
    if (created != 0 &&
        (errno != EEXIST || stat(subdirectory, &info) != 0 || !S_ISDIR(info.st_mode)))
        return 0;
#endif
    memcpy(out, path, (size_t)count + 1u);
    return 1;
}
static void encode(uint8_t bytes[CUP_BYTES], const PortableMeshCupIdentity *identity,
                   const PortableMeshCupRecord *record)
{
    memset(bytes, 0, CUP_BYTES);
    memcpy(bytes, cup_magic, 8u);
    write_u32(bytes + 8u, 1u);
    write_u32(bytes + 12u, CUP_BYTES);
    write_u32(bytes + 16u, identity->cup_id);
    write_u32(bytes + 20u, identity->engine_class);
    write_u32(bytes + 24u, identity->driver_id);
    write_u32(bytes + 28u, record->standing_rank);
    for (unsigned i = 0; i < 4u; ++i)
        write_u32(bytes + 32u + i * 4u, identity->course_ids[i]);
    memcpy(bytes + 48u, identity->digest, 32u);
    write_u32(bytes + 80u, record->points);
    for (unsigned i = 0; i < 4u; ++i)
    {
        write_u32(bytes + 84u + i * 4u, record->finish_ticks[i]);
        write_u32(bytes + 100u + i * 4u, record->finish_ranks[i]);
    }
    const uint64_t hash = checksum(bytes, 120u);
    for (unsigned i = 0; i < 8u; ++i)
        bytes[120u + i] = (uint8_t)(hash >> (i * 8u));
}
PortableMeshCupStoreStatus portable_mesh_cup_store_load(const char *path,
                                                        const PortableMeshCupIdentity *expected,
                                                        PortableMeshCupRecord *out)
{
    if (!path || !*path || !out || !identity_valid(expected))
        return PORTABLE_MESH_CUP_STORE_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file)
        return errno == ENOENT ? PORTABLE_MESH_CUP_STORE_NOT_FOUND
                               : PORTABLE_MESH_CUP_STORE_IO_ERROR;
    uint8_t bytes[CUP_BYTES + 1u];
    const size_t size = fread(bytes, 1, sizeof bytes, file);
    int failed = ferror(file);
    if (fclose(file) != 0)
        failed = 1;
    if (failed)
        return PORTABLE_MESH_CUP_STORE_IO_ERROR;
    if (size != CUP_BYTES || memcmp(bytes, cup_magic, 8u) || read_u32(bytes + 8u) != 1u ||
        read_u32(bytes + 12u) != CUP_BYTES || read_u32(bytes + 116u) != 0u)
        return PORTABLE_MESH_CUP_STORE_INVALID;
    uint64_t saved = 0;
    for (unsigned i = 0; i < 8u; ++i)
        saved |= (uint64_t)bytes[120u + i] << (i * 8u);
    if (saved != checksum(bytes, 120u))
        return PORTABLE_MESH_CUP_STORE_INVALID;
    if (read_u32(bytes + 16u) != expected->cup_id ||
        read_u32(bytes + 20u) != expected->engine_class ||
        read_u32(bytes + 24u) != expected->driver_id || memcmp(bytes + 48u, expected->digest, 32u))
        return PORTABLE_MESH_CUP_STORE_INCOMPATIBLE;
    PortableMeshCupRecord record = {0};
    record.standing_rank = read_u32(bytes + 28u);
    record.points = read_u32(bytes + 80u);
    for (unsigned i = 0; i < 4u; ++i)
    {
        if (read_u32(bytes + 32u + i * 4u) != expected->course_ids[i])
            return PORTABLE_MESH_CUP_STORE_INCOMPATIBLE;
        record.finish_ticks[i] = read_u32(bytes + 84u + i * 4u);
        record.finish_ranks[i] = read_u32(bytes + 100u + i * 4u);
    }
    if (!record_valid(&record))
        return PORTABLE_MESH_CUP_STORE_INVALID;
    *out = record;
    return PORTABLE_MESH_CUP_STORE_OK;
}
/* Exclusive sibling temporaries never truncate another writer's staging file.
 * Only a flushed, closed candidate replaces the previous save. */
static PortableMeshCupStoreStatus publish(const char *path, const uint8_t *bytes, size_t size)
{
    if (path == NULL || path[0] == '\0' || bytes == NULL || size == 0u)
        return PORTABLE_MESH_CUP_STORE_INVALID;
    const size_t length = strlen(path);
    if (length > 4095u || length > SIZE_MAX - 48u)
        return PORTABLE_MESH_CUP_STORE_INVALID;
    char *temporary = malloc(length + 48u);
    if (temporary == NULL)
        return PORTABLE_MESH_CUP_STORE_IO_ERROR;
    int descriptor = -1;
    for (unsigned attempt = 0u; attempt < 32u; ++attempt)
    {
        (void)snprintf(temporary, length + 48u, "%s.tmp-%lu-%u", path, (unsigned long)store_pid(),
                       attempt);
#ifdef _WIN32
        descriptor =
            store_open(temporary, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        descriptor = store_open(temporary, O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
        if (descriptor >= 0 || errno != EEXIST)
            break;
    }
    if (descriptor < 0)
    {
        free(temporary);
        return PORTABLE_MESH_CUP_STORE_IO_ERROR;
    }
    FILE *file = store_fdopen(descriptor, "wb");
    if (file == NULL)
    {
        store_close(descriptor);
        remove(temporary);
        free(temporary);
        return PORTABLE_MESH_CUP_STORE_IO_ERROR;
    }
    int failed = fwrite(bytes, 1u, size, file) != size;
    if (!failed && fflush(file) != 0)
        failed = 1;
    if (fclose(file) != 0)
        failed = 1;
    if (!failed && !portable_file_replace(temporary, path))
        failed = 1;
    if (failed)
        (void)remove(temporary);
    free(temporary);
    return failed ? PORTABLE_MESH_CUP_STORE_IO_ERROR : PORTABLE_MESH_CUP_STORE_OK;
}

PortableMeshCupStoreStatus portable_mesh_cup_store_update(const char *path,
                                                          const PortableMeshCupIdentity *identity,
                                                          const PortableMeshCupRecord *candidate,
                                                          PortableMeshCupRecord *out, int *updated)
{
    if (!identity_valid(identity) || !record_valid(candidate) || !out || !updated)
        return PORTABLE_MESH_CUP_STORE_INVALID;
    PortableMeshCupRecord previous;
    const PortableMeshCupStoreStatus loaded =
        portable_mesh_cup_store_load(path, identity, &previous);
    if (loaded != PORTABLE_MESH_CUP_STORE_OK && loaded != PORTABLE_MESH_CUP_STORE_NOT_FOUND)
        return loaded;
    int better = loaded == PORTABLE_MESH_CUP_STORE_NOT_FOUND;
    if (!better)
    {
        uint64_t old_ticks = 0, new_ticks = 0;
        for (unsigned i = 0; i < 4u; ++i)
        {
            old_ticks += previous.finish_ticks[i];
            new_ticks += candidate->finish_ticks[i];
        }
        better = candidate->standing_rank < previous.standing_rank ||
                 (candidate->standing_rank == previous.standing_rank &&
                  (candidate->points > previous.points ||
                   (candidate->points == previous.points && new_ticks < old_ticks)));
    }
    if (better)
    {
        uint8_t bytes[CUP_BYTES];
        encode(bytes, identity, candidate);
        const PortableMeshCupStoreStatus saved = publish(path, bytes, sizeof bytes);
        if (saved != PORTABLE_MESH_CUP_STORE_OK)
            return saved;
    }
    *out = better ? *candidate : previous;
    *updated = better;
    return PORTABLE_MESH_CUP_STORE_OK;
}
const char *portable_mesh_cup_store_status_name(PortableMeshCupStoreStatus status)
{
    switch (status)
    {
    case PORTABLE_MESH_CUP_STORE_OK:
        return "ok";
    case PORTABLE_MESH_CUP_STORE_NOT_FOUND:
        return "not found";
    case PORTABLE_MESH_CUP_STORE_INVALID:
        return "invalid cup record";
    case PORTABLE_MESH_CUP_STORE_INCOMPATIBLE:
        return "incompatible cup record";
    case PORTABLE_MESH_CUP_STORE_IO_ERROR:
        return "cup record I/O failure";
    }
    return "unknown cup record status";
}
