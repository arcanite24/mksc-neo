#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "mesh_time_trial_store.h"
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

#define PB_BYTES 88u
static const uint8_t pb_magic[8] = {'M', 'K', 'S', 'C', '3', 'P', 'B', 'S'};
static const uint8_t ghost_magic[8] = {'M', 'K', 'S', 'C', '3', 'G', 'H', 'O'};
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
static int best_valid(const PortableMeshPersonalBest *best)
{
    return best != NULL && best->race_ticks != 0u &&
           best->race_ticks <= PORTABLE_MESH_REPLAY_MAX_TICKS && best->best_lap_ticks != 0u &&
           best->best_lap_ticks <= best->race_ticks;
}
int portable_mesh_store_path(const char *directory,
                             const PortableMeshTimeTrialCompatibility *identity, int ghost,
                             char *out, size_t capacity)
{
    uint8_t wire[PORTABLE_MESH_COMPATIBILITY_BYTES];
    if (directory == NULL || directory[0] == '\0' || out == NULL ||
        !portable_mesh_compatibility_write(identity, wire))
        return 0;
    char digest[65], subdirectory[4096], path[4096];
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0u; i < 32u; ++i)
    {
        digest[2u * i] = hex[identity->digest[i] >> 4u];
        digest[2u * i + 1u] = hex[identity->digest[i] & 15u];
    }
    digest[64] = '\0';
    int length = snprintf(subdirectory, sizeof subdirectory, "%s/mesh3d", directory);
    if (length < 0 || (size_t)length >= sizeof subdirectory)
        return 0;
    /* Kart-less records keep their original names. */
    char kart[24] = "";
    if (identity->kart_id)
        snprintf(kart, sizeof kart, "-kart-%u", identity->kart_id);
    length = snprintf(path, sizeof path, "%s/course-%u-driver-%u%s-profile-%u-r%u-%s.%s",
                      subdirectory, identity->course_id, identity->driver_id, kart, identity->profile_id,
                      identity->profile_revision, digest, ghost ? "ghost" : "pb");
    if (length < 0 || (size_t)length >= sizeof path || (size_t)length >= capacity)
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
    memcpy(out, path, (size_t)length + 1u);
    return 1;
}
static PortableMeshStoreStatus read_bounded(const char *path, size_t maximum, uint8_t **out,
                                            size_t *out_size)
{
    if (path == NULL || path[0] == '\0')
        return PORTABLE_MESH_STORE_INVALID;
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return errno == ENOENT ? PORTABLE_MESH_STORE_NOT_FOUND : PORTABLE_MESH_STORE_IO_ERROR;
    uint8_t *bytes = malloc(maximum + 1u);
    if (bytes == NULL)
    {
        fclose(file);
        return PORTABLE_MESH_STORE_IO_ERROR;
    }
    const size_t size = fread(bytes, 1u, maximum + 1u, file);
    const int failed = ferror(file);
    const int closed = fclose(file);
    if (failed || closed != 0)
    {
        free(bytes);
        return PORTABLE_MESH_STORE_IO_ERROR;
    }
    if (size == 0u || size > maximum)
    {
        free(bytes);
        return PORTABLE_MESH_STORE_INVALID;
    }
    *out = bytes;
    *out_size = size;
    return PORTABLE_MESH_STORE_OK;
}
/* Exclusive sibling temporaries never truncate another writer's staging file.
 * Only a flushed, closed candidate replaces the previous save. */
static PortableMeshStoreStatus publish(const char *path, const uint8_t *bytes, size_t size)
{
    if (path == NULL || path[0] == '\0' || bytes == NULL || size == 0u)
        return PORTABLE_MESH_STORE_INVALID;
    const size_t length = strlen(path);
    if (length > 4095u || length > SIZE_MAX - 48u)
        return PORTABLE_MESH_STORE_INVALID;
    char *temporary = malloc(length + 48u);
    if (temporary == NULL)
        return PORTABLE_MESH_STORE_IO_ERROR;
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
        return PORTABLE_MESH_STORE_IO_ERROR;
    }
    FILE *file = store_fdopen(descriptor, "wb");
    if (file == NULL)
    {
        store_close(descriptor);
        remove(temporary);
        free(temporary);
        return PORTABLE_MESH_STORE_IO_ERROR;
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
    return failed ? PORTABLE_MESH_STORE_IO_ERROR : PORTABLE_MESH_STORE_OK;
}
static PortableMeshStoreStatus replay_status(PortableMeshReplayStatus status)
{
    return status == PORTABLE_MESH_REPLAY_COMPLETE       ? PORTABLE_MESH_STORE_OK
           : status == PORTABLE_MESH_REPLAY_INCOMPATIBLE ? PORTABLE_MESH_STORE_INCOMPATIBLE
                                                         : PORTABLE_MESH_STORE_INVALID;
}
PortableMeshStoreStatus portable_mesh_ghost_load(const char *path,
                                                 const PortableMeshTimeTrialCompatibility *expected,
                                                 uint8_t **out_bytes, size_t *out_size)
{
    uint8_t compatibility[PORTABLE_MESH_COMPATIBILITY_BYTES];
    if (out_bytes == NULL || out_size == NULL ||
        !portable_mesh_compatibility_write(expected, compatibility))
        return PORTABLE_MESH_STORE_INVALID;
    uint8_t *bytes;
    size_t size;
    PortableMeshStoreStatus status =
        read_bounded(path, PORTABLE_MESH_REPLAY_ACTORS_MAX_BYTES, &bytes, &size);
    if (status != PORTABLE_MESH_STORE_OK)
        return status;
    if (size < 8u || memcmp(bytes, ghost_magic, 8u) != 0)
        status = PORTABLE_MESH_STORE_INVALID;
    else
        status = replay_status(portable_mesh_replay_validate(bytes, size, expected, NULL));
    if (status != PORTABLE_MESH_STORE_OK)
    {
        free(bytes);
        return status;
    }
    *out_bytes = bytes;
    *out_size = size;
    return PORTABLE_MESH_STORE_OK;
}
PortableMeshStoreStatus portable_mesh_ghost_save(const char *path,
                                                 const PortableMeshTimeTrialCompatibility *expected,
                                                 const uint8_t *bytes, size_t size)
{
    const PortableMeshStoreStatus status =
        replay_status(portable_mesh_replay_validate(bytes, size, expected, NULL));
    if (status != PORTABLE_MESH_STORE_OK)
        return status;
    uint8_t *copy = malloc(size);
    if (copy == NULL)
        return PORTABLE_MESH_STORE_IO_ERROR;
    memcpy(copy, bytes, size);
    memcpy(copy, ghost_magic, sizeof ghost_magic);
    const PortableMeshStoreStatus saved = publish(path, copy, size);
    free(copy);
    return saved;
}
PortableMeshStoreStatus
portable_mesh_personal_best_load(const char *path,
                                 const PortableMeshTimeTrialCompatibility *expected,
                                 PortableMeshPersonalBest *out)
{
    uint8_t compatibility[PORTABLE_MESH_COMPATIBILITY_BYTES];
    if (out == NULL || !portable_mesh_compatibility_write(expected, compatibility))
        return PORTABLE_MESH_STORE_INVALID;
    uint8_t *bytes;
    size_t size;
    PortableMeshStoreStatus status = read_bounded(path, PB_BYTES, &bytes, &size);
    if (status != PORTABLE_MESH_STORE_OK)
        return status;
    PortableMeshTimeTrialCompatibility identity;
    const PortableMeshPersonalBest best =
        size == PB_BYTES ? (PortableMeshPersonalBest){read_u32(bytes + 72u), read_u32(bytes + 76u)}
                         : (PortableMeshPersonalBest){0};
    if (size != PB_BYTES || memcmp(bytes, pb_magic, 8u) != 0 || read_u32(bytes + 8u) != 1u ||
        read_u32(bytes + 12u) != PB_BYTES ||
        !portable_mesh_compatibility_read(&identity, bytes + 16u) || !best_valid(&best) ||
        checksum(bytes, 80u) !=
            ((uint64_t)read_u32(bytes + 80u) | (uint64_t)read_u32(bytes + 84u) << 32u))
        status = PORTABLE_MESH_STORE_INVALID;
    else if (!portable_mesh_compatibility_equal(expected, &identity))
        status = PORTABLE_MESH_STORE_INCOMPATIBLE;
    else
        *out = best;
    free(bytes);
    return status;
}
PortableMeshStoreStatus portable_mesh_personal_best_update(
    const char *path, const PortableMeshTimeTrialCompatibility *expected,
    const PortableMeshPersonalBest *candidate, PortableMeshPersonalBest *out, int *updated)
{
    uint8_t bytes[PB_BYTES] = {0};
    if (out == NULL || updated == NULL || !best_valid(candidate) ||
        !portable_mesh_compatibility_write(expected, bytes + 16u))
        return PORTABLE_MESH_STORE_INVALID;
    PortableMeshPersonalBest best;
    PortableMeshStoreStatus status = portable_mesh_personal_best_load(path, expected, &best);
    if (status != PORTABLE_MESH_STORE_OK && status != PORTABLE_MESH_STORE_NOT_FOUND)
        return status;
    const int changed = status == PORTABLE_MESH_STORE_NOT_FOUND ||
                        candidate->race_ticks < best.race_ticks ||
                        candidate->best_lap_ticks < best.best_lap_ticks;
    if (status == PORTABLE_MESH_STORE_NOT_FOUND)
        best = *candidate;
    else
    {
        if (candidate->race_ticks < best.race_ticks)
            best.race_ticks = candidate->race_ticks;
        if (candidate->best_lap_ticks < best.best_lap_ticks)
            best.best_lap_ticks = candidate->best_lap_ticks;
    }
    if (changed)
    {
        memcpy(bytes, pb_magic, sizeof pb_magic);
        write_u32(bytes + 8u, 1u);
        write_u32(bytes + 12u, PB_BYTES);
        write_u32(bytes + 72u, best.race_ticks);
        write_u32(bytes + 76u, best.best_lap_ticks);
        const uint64_t hash = checksum(bytes, 80u);
        write_u32(bytes + 80u, (uint32_t)hash);
        write_u32(bytes + 84u, (uint32_t)(hash >> 32u));
        status = publish(path, bytes, sizeof bytes);
        if (status != PORTABLE_MESH_STORE_OK)
            return status;
    }
    *out = best;
    *updated = changed;
    return PORTABLE_MESH_STORE_OK;
}
const char *portable_mesh_store_status_name(PortableMeshStoreStatus status)
{
    switch (status)
    {
    case PORTABLE_MESH_STORE_OK:
        return "ok";
    case PORTABLE_MESH_STORE_NOT_FOUND:
        return "not_found";
    case PORTABLE_MESH_STORE_INVALID:
        return "invalid";
    case PORTABLE_MESH_STORE_INCOMPATIBLE:
        return "incompatible";
    case PORTABLE_MESH_STORE_IO_ERROR:
        return "io_error";
    }
    return "unknown";
}
