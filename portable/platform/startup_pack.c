#include "startup_pack.h"
#include "loading_screen.h"
#include "startup_trace.h"
#include "file_read.h"
#include "content_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *path;
    uint8_t *bytes;
    size_t size;
    PortableAssetPackView view;
    char error[128];
} PackJob;
static PackJob cached;
static char cached_path[4096];

static int read_pack(void *data, PortableLoadJob *job)
{
    PackJob *pack = data;
    const uint64_t since = portable_startup_clock();
    if (portable_file_read_bounded(pack->path, INT32_MAX, &pack->bytes, &pack->size) != PORTABLE_FILE_READ_OK) {
        snprintf(pack->error, sizeof pack->error, "Could not read game pack");
        return 0;
    }
    if (portable_load_cancelled(job)) return 0;
    portable_load_phase(job, 1);
    const PortableAssetPackStatus status = portable_asset_pack_open(pack->bytes, pack->size, &pack->view);
    portable_startup_phase("pack read/validate", since);
    if (status != PORTABLE_ASSET_PACK_OK) {
        snprintf(pack->error, sizeof pack->error, "Pack rejected: %s", portable_asset_pack_status_name(status));
        return 0;
    }
    return 1;
}
void portable_startup_pack_discard(void)
{
    free(cached.bytes);
    cached = (PackJob){0};
    cached_path[0] = '\0';
}
static int ensure_pack(const char *path, int hidden)
{
    if (cached.bytes && strcmp(path, cached_path) == 0) return 1;
    portable_startup_pack_discard();
    if (!portable_loading_begin(hidden)) return 0;
    PackJob pack = {.path = path};
    static const char *const phases[] = {"Reading game data", "Checking game data"};
    const int result = portable_loading_run(NULL, NULL, "Starting your game", phases, 2, read_pack, &pack);
    if (result != 1) {
        if (pack.error[0]) fprintf(stderr, "%s: %s\n", path, pack.error);
        free(pack.bytes);
        return result;
    }
    if (strlen(path) >= sizeof cached_path) { free(pack.bytes); return 0; }
    memcpy(cached_path, path, strlen(path) + 1);
    cached = pack;
    cached.path = cached_path;
    return 1;
}
int portable_startup_pack_probe(const char *path, int hidden, uint32_t *version)
{
    const int result = ensure_pack(path, hidden);
    if (result == 1) *version = portable_content_import_version(&cached.view);
    return result;
}
int portable_startup_pack_take(const char *path, int hidden, uint8_t **bytes,
                               size_t *size, PortableAssetPackView *view)
{
    const int result = ensure_pack(path, hidden);
    if (result != 1) return result;
    *bytes = cached.bytes;
    *size = cached.size;
    *view = cached.view;
    cached.bytes = NULL;
    portable_startup_pack_discard();
    return 1;
}
