#include "ui_background_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_replace.h"
#include "rom_import_host.h"

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#endif

/* Stock flat identity frames plus six unflattened ARGB layers exceed 4 MiB. */
#define UI_BACKGROUND_MAX_BYTES (8u * 1024u * 1024u)

static int companion_valid(const uint8_t *bytes, size_t size, PortableAssetPackView *view)
{
    PortableAssetSectionView metadata;
    uint8_t source[20];
    static const char hex[] = "0123456789abcdef";
    if (size > UI_BACKGROUND_MAX_BYTES ||
        portable_asset_pack_open(bytes, size, view) != PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(view, 15u, 0x209ffu, &metadata) != PORTABLE_ASSET_PACK_OK ||
        metadata.version != 1u || metadata.size != 32u ||
        memcmp(metadata.bytes, "MKSCWUI1\1\0\0\0", 12u) != 0) return 0;
    for (size_t i = 0; i < sizeof source; ++i) {
        const char *high = strchr(hex, MKSC_ROM_IMPORT_SHA1[i * 2u]);
        const char *low = strchr(hex, MKSC_ROM_IMPORT_SHA1[i * 2u + 1u]);
        source[i] = (uint8_t)((high - hex) * 16 + (low - hex));
    }
    return memcmp(metadata.bytes + 12u, source, sizeof source) == 0;
}

static int cache_path(char *out, size_t capacity)
{
    char *directory = portable_data_directory();
    if (directory == NULL) return 0;
    const int length = snprintf(out, capacity, "%stitle-backgrounds.pack", directory);
#ifdef PORTABLE_ENABLE_SDL3
    SDL_free(directory);
#endif
    return length > 0 && (size_t)length < capacity;
}

static int cache_read(PortableUiBackgroundHost *host, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    uint8_t *bytes = malloc(UI_BACKGROUND_MAX_BYTES + 1u);
    const size_t size = bytes != NULL ? fread(bytes, 1, UI_BACKGROUND_MAX_BYTES + 1u, file) : 0u;
    int valid = bytes != NULL && !ferror(file);
    if (fclose(file) != 0) valid = 0;
    PortableAssetPackView view;
    if (!valid || !companion_valid(bytes, size, &view)) {
        free(bytes);
        return 0;
    }
    free(host->bytes);
    host->bytes = bytes;
    host->size = size;
    host->view = view;
    return 1;
}

int portable_ui_background_host_import(PortableUiBackgroundHost *host,
    const char *rom_path, char *error, size_t capacity)
{
    char local_error[256], path[4096], temporary[4100] = {0};
    if (error == NULL || capacity == 0u) {
        error = local_error;
        capacity = sizeof local_error;
    }
    error[0] = '\0';
    if (host == NULL || rom_path == NULL || !cache_path(path, sizeof path)) {
        snprintf(error, capacity, "Cannot locate title background cache");
        if (host != NULL) snprintf(host->notice, sizeof host->notice, "%s", error);
        return 0;
    }
    MkscRomImportResult result;
    if (!portable_rom_import_title_presentation_read(rom_path, &result, error, capacity)) {
        snprintf(host->notice, sizeof host->notice, "%s", error);
        return 0;
    }
    PortableAssetPackView view;
    if (!companion_valid(result.pack, result.pack_size, &view)) {
        snprintf(error, capacity, "Invalid imported title backgrounds");
        mksc_rom_import_free(&result);
        snprintf(host->notice, sizeof host->notice, "%s", error);
        return 0;
    }
    const int length = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    FILE *file = length > 0 && (size_t)length < sizeof temporary ? fopen(temporary, "wb") : NULL;
    int written = file != NULL && fwrite(result.pack, 1, result.pack_size, file) == result.pack_size;
    if (file != NULL && fclose(file) != 0) written = 0;
    if (!written || !portable_file_replace(temporary, path)) {
        if (temporary[0] != '\0') remove(temporary);
        snprintf(error, capacity, "Cannot save title background cache");
        snprintf(host->notice, sizeof host->notice, "%s", error);
        mksc_rom_import_free(&result);
        return 0;
    }
    free(host->bytes);
    host->bytes = result.pack;
    host->size = result.pack_size;
    host->view = view;
    host->attempted = 1u;
    snprintf(host->notice, sizeof host->notice, "Title backgrounds ready");
    return 1;
}

const PortableAssetPackView *portable_ui_background_host_load(PortableUiBackgroundHost *host)
{
    if (host == NULL) return NULL;
    if (host->bytes != NULL) return &host->view;
    if (host->attempted) return NULL;
    host->attempted = 1u;
    char path[4096], rom_path[4096], error[256];
    if (cache_path(path, sizeof path) && cache_read(host, path)) return &host->view;
    if (portable_stored_rom_path(rom_path, sizeof rom_path) &&
        portable_ui_background_host_import(host, rom_path, error, sizeof error)) return &host->view;
    snprintf(host->notice, sizeof host->notice, "Choose your ROM to prepare title backgrounds");
    return NULL;
}

void portable_ui_background_host_init(PortableUiBackgroundHost *host)
{
    if (host != NULL) memset(host, 0, sizeof *host);
}

const char *portable_ui_background_host_notice(const PortableUiBackgroundHost *host)
{
    return host != NULL ? host->notice : "";
}

#ifdef PORTABLE_ENABLE_SDL3
typedef struct {
    SDL_Mutex *mutex;
    unsigned references;
    int state; /* 0 pending, 1 selected, 2 cancelled, 3 failed */
    char path[4096];
} UiBackgroundRequest;

static void request_release(UiBackgroundRequest *request)
{
    SDL_LockMutex(request->mutex);
    const int released = --request->references == 0u;
    SDL_UnlockMutex(request->mutex);
    if (released) {
        SDL_DestroyMutex(request->mutex);
        free(request);
    }
}

static void SDLCALL choose_done(void *context, const char *const *files, int filter)
{
    UiBackgroundRequest *request = context;
    (void)filter;
    SDL_LockMutex(request->mutex);
    if (files == NULL) request->state = 3;
    else if (files[0] == NULL) request->state = 2;
    else if (strlen(files[0]) >= sizeof request->path) request->state = 3;
    else {
        SDL_strlcpy(request->path, files[0], sizeof request->path);
        request->state = 1;
    }
    SDL_UnlockMutex(request->mutex);
    request_release(request);
}

int portable_ui_background_host_choose(PortableUiBackgroundHost *host, SDL_Window *window)
{
    if (host == NULL || host->request != NULL) return 0;
    UiBackgroundRequest *request = calloc(1, sizeof *request);
    if (request == NULL) return 0;
    request->mutex = SDL_CreateMutex();
    if (request->mutex == NULL) { free(request); return 0; }
    request->references = 2u; /* owner and dialog callback */
    host->request = request;
    snprintf(host->notice, sizeof host->notice, "Choose your Game Boy Advance ROM");
    static const SDL_DialogFileFilter filters[] = {{"Game Boy Advance ROM", "gba"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(choose_done, request, window, filters, 2, NULL, false);
    return 1;
}

int portable_ui_background_host_poll(PortableUiBackgroundHost *host)
{
    if (host == NULL || host->request == NULL) return 0;
    UiBackgroundRequest *request = host->request;
    char path[4096];
    SDL_LockMutex(request->mutex);
    const int state = request->state;
    if (state == 1) memcpy(path, request->path, sizeof path);
    SDL_UnlockMutex(request->mutex);
    if (state == 0) return 0;
    host->request = NULL;
    request_release(request);
    if (state == 2) {
        snprintf(host->notice, sizeof host->notice, "ROM selection cancelled");
        return 0;
    }
    if (state == 3) {
        snprintf(host->notice, sizeof host->notice, "ROM selection failed");
        return -1;
    }
    char error[256];
    return portable_ui_background_host_import(host, path, error, sizeof error) ? 1 : -1;
}
#else
int portable_ui_background_host_choose(PortableUiBackgroundHost *host, SDL_Window *window)
{
    (void)host;
    (void)window;
    return 0;
}
int portable_ui_background_host_poll(PortableUiBackgroundHost *host)
{
    (void)host;
    return 0;
}
#endif

void portable_ui_background_host_close(PortableUiBackgroundHost *host)
{
    if (host == NULL) return;
#ifdef PORTABLE_ENABLE_SDL3
    if (host->request != NULL) request_release(host->request);
#endif
    free(host->bytes);
    memset(host, 0, sizeof *host);
}
