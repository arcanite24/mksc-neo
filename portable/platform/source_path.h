#ifndef PORTABLE_SOURCE_PATH_H
#define PORTABLE_SOURCE_PATH_H
#include <stddef.h>
int portable_source_path_is_uri(const char *path);
/* 1 resolved,0 missing URI child,-1 invalid/provider failure. Native paths
 * resolve syntactically; outputs remain unchanged on failure. */
int portable_source_path_resolve(const char *directory,const char *relative,char *out,size_t capacity);
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
bool portable_source_path_info(const char *path,SDL_PathInfo *info);
bool portable_source_path_enumerate(const char *path,SDL_EnumerateDirectoryCallback callback,void *userdata);
/* Android-only chooser. Its callback owns its existing request reference. */
bool portable_source_path_choose_folder(SDL_DialogFileCallback callback,void *userdata);
#endif
#endif
