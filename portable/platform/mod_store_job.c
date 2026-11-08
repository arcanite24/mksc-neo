#include "mod_store_job.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <stdio.h>
struct PortableStoreJob {
    SDL_Thread *thread;
    SDL_AtomicInt done;
    PortableStore *load;
    const PortableStore *download;
    unsigned entry;
    char url[PORTABLE_STORE_URL_CAPACITY],path[4096],error[PORTABLE_MOD_ERROR_CAPACITY];
    int result;
};
static int work(void *arg)
{
    PortableStoreJob *j=arg;
    j->result=j->load?portable_store_load(j->url,j->path,j->load,j->error):
        portable_store_download(j->download,j->entry,j->path,j->error);
    SDL_SetAtomicInt(&j->done,1);return 0;
}
static PortableStoreJob *start(PortableStoreJob *j)
{
    if(j){j->thread=SDL_CreateThread(work,"mod-store",j);if(!j->thread){free(j);j=NULL;}}
    return j;
}
PortableStoreJob *portable_store_job_load(const char *url,const char *cache,PortableStore *store)
{
    PortableStoreJob *j=calloc(1,sizeof *j);
    if(j){j->load=store;snprintf(j->url,sizeof j->url,"%s",url);snprintf(j->path,sizeof j->path,"%s",cache);}
    return start(j);
}
PortableStoreJob *portable_store_job_download(const PortableStore *store,unsigned entry,const char *destination)
{
    PortableStoreJob *j=calloc(1,sizeof *j);
    if(j){j->download=store;j->entry=entry;snprintf(j->path,sizeof j->path,"%s",destination);}
    return start(j);
}
int portable_store_job_poll(PortableStoreJob *j,char *error)
{
    if(!j||!SDL_GetAtomicInt(&j->done))return 0;
    snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s",j->error);return j->result?1:-1;
}
void portable_store_job_free(PortableStoreJob *j)
{
    if(j){SDL_WaitThread(j->thread,NULL);free(j);}
}
