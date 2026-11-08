#ifndef PORTABLE_UI_BACKGROUND_HOST_H
#define PORTABLE_UI_BACKGROUND_HOST_H

#include "asset_pack.h"

typedef struct SDL_Window SDL_Window;

/* Keep stationary while its returned view is borrowed; never merge this pack
 * into gameplay content. Only the main thread accesses this owner. */
typedef struct {
    uint8_t *bytes;
    size_t size;
    PortableAssetPackView view;
    void *request;
    uint8_t attempted;
    char notice[256];
} PortableUiBackgroundHost;

void portable_ui_background_host_init(PortableUiBackgroundHost *host);
void portable_ui_background_host_close(PortableUiBackgroundHost *host);
const PortableAssetPackView *portable_ui_background_host_load(PortableUiBackgroundHost *host);
int portable_ui_background_host_import(PortableUiBackgroundHost *host,
    const char *rom_path, char *error, size_t capacity);
int portable_ui_background_host_choose(PortableUiBackgroundHost *host, SDL_Window *window);
/* 1 imported, 0 pending/cancelled/no request, -1 failed. */
int portable_ui_background_host_poll(PortableUiBackgroundHost *host);
const char *portable_ui_background_host_notice(const PortableUiBackgroundHost *host);

#endif
