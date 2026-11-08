#include "file_dialog.h"
#include "source_path.h"
#include <stdlib.h>
#include <string.h>
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>

struct PortableFileDialog {
    SDL_Mutex *mutex;
    unsigned references;
    int state;
    char path[4096];
    char *label,*pattern;
    SDL_DialogFileFilter filters[2];
};

static void release_request(PortableFileDialog *request)
{
    SDL_LockMutex(request->mutex);
    const int released=--request->references==0u;
    SDL_UnlockMutex(request->mutex);
    if(released){
        SDL_DestroyMutex(request->mutex);
        free(request->label);free(request->pattern);free(request);
    }
}

static void SDLCALL choose_done(void *context,const char *const *files,int filter)
{
    PortableFileDialog *request=context;
    (void)filter;
    SDL_LockMutex(request->mutex);
    if(files==NULL)request->state=3;
    else if(files[0]==NULL)request->state=2;
    else if(strlen(files[0])>=sizeof request->path)request->state=3;
    else {
        memcpy(request->path,files[0],strlen(files[0])+1u);
        request->state=1;
    }
    SDL_UnlockMutex(request->mutex);
    release_request(request);
}

static char *copy_text(const char *text)
{
    const size_t size=strlen(text)+1u;
    char *copy=malloc(size);
    if(copy!=NULL)memcpy(copy,text,size);
    return copy;
}

PortableFileDialog *portable_file_dialog_choose(SDL_Window *window,
    const char *label,const char *pattern)
{
    if(label==NULL||pattern==NULL)return NULL;
    PortableFileDialog *request=calloc(1u,sizeof *request);
    if(request==NULL)return NULL;
    request->label=copy_text(label);request->pattern=copy_text(pattern);
    request->mutex=SDL_CreateMutex();
    if(request->label==NULL||request->pattern==NULL||request->mutex==NULL){
        if(request->mutex!=NULL)SDL_DestroyMutex(request->mutex);
        free(request->label);free(request->pattern);free(request);return NULL;
    }
    /* Android pickers filter by MIME type and know none for .nds/.gba, so
     * an "All files" fallback keeps such ROMs selectable. */
    request->filters[0].name=request->label;request->filters[0].pattern=request->pattern;
    request->filters[1].name="All files";request->filters[1].pattern="*";
    /* A synchronous callback may release its reference before this returns;
     * the caller reference keeps the completed request alive in that case. */
    request->references=2u;
    SDL_ShowOpenFileDialog(choose_done,request,window,request->filters,2,NULL,false);
    return request;
}

PortableFileDialog *portable_file_dialog_choose_folder(SDL_Window *window)
{
    PortableFileDialog *request=calloc(1u,sizeof *request);
    if (request==NULL) return NULL;
    request->mutex=SDL_CreateMutex();
    if (request->mutex==NULL) {free(request);return NULL;}
    request->references=2u;
#ifdef SDL_PLATFORM_ANDROID
    (void)window;
    if (!portable_source_path_choose_folder(choose_done,request))
        choose_done(request,NULL,-1);
#else
    SDL_ShowOpenFolderDialog(choose_done,request,window,NULL,false);
#endif
    return request;
}

int portable_file_dialog_poll(PortableFileDialog *request,char *path,size_t capacity)
{
    if(request==NULL)return 3;
    SDL_LockMutex(request->mutex);
    int state=request->state;
    if(state==1){
        const size_t size=strlen(request->path)+1u;
        if(path==NULL||size>capacity)state=3;
        else memcpy(path,request->path,size);
    }
    SDL_UnlockMutex(request->mutex);
    return state;
}

void portable_file_dialog_free(PortableFileDialog *request)
{
    if(request!=NULL)release_request(request);
}
#else
PortableFileDialog *portable_file_dialog_choose_folder(SDL_Window *window)
{
    (void)window;return NULL;
}
PortableFileDialog *portable_file_dialog_choose(SDL_Window *window,
    const char *label,const char *pattern)
{
    (void)window;(void)label;(void)pattern;return NULL;
}
int portable_file_dialog_poll(PortableFileDialog *request,char *path,size_t capacity)
{
    (void)request;(void)path;(void)capacity;return 3;
}
void portable_file_dialog_free(PortableFileDialog *request)
{
    (void)request;
}
#endif
