#include "mod_manager.h"
#include "course_3d.h"
#ifdef PORTABLE_ENABLE_SDL3
#include "load_job.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PortableModManager {
    PortableModCatalog catalog;
    PortableModRoots roots;
    char *directories[PORTABLE_MOD_ROOT_LIMIT];
    char ids[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    size_t count;
    uint8_t catalog_ready;
    char asset_sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY];
    PortableModSet *preview;
    char preview_id[PORTABLE_MOD_ID_CAPACITY];
    PortableNativeImage preview_image;
#ifdef PORTABLE_ENABLE_SDL3
    /* A worker loads a library entry's art off the main thread (see PreviewRequest). */
    struct PreviewRequest *request;
    uint32_t generation;
#endif
    char notice[PORTABLE_MOD_ERROR_CAPACITY];
};

static int notice(PortableModManager *manager,const char *message)
{
    snprintf(manager->notice,sizeof manager->notice,"%s",message);
    return 0;
}

static const PortableModCatalogEntry *find_entry(const PortableModManager *manager,const char *id)
{
    for(size_t i=0u;i<manager->catalog.count;++i)
        if(strcmp(manager->catalog.entries[i].id,id)==0)return &manager->catalog.entries[i];
    return NULL;
}

PortableModManager *portable_mod_manager_create_roots(const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count)
{
    if(!portable_mod_roots_valid(roots)||count>PORTABLE_MOD_LIMIT||(count!=0u&&ids==NULL))return NULL;
    for(size_t i=0u;i<count;++i){
        if(memchr(ids[i],'\0',PORTABLE_MOD_ID_CAPACITY)==NULL||!portable_mod_id_valid(ids[i]))return NULL;
        for(size_t j=0u;j<i;++j)if(strcmp(ids[i],ids[j])==0)return NULL;
    }
    PortableModManager *manager=calloc(1u,sizeof *manager);
    if(manager==NULL)return NULL;
    manager->roots.count=roots->count;
    for(size_t i=0u;i<roots->count;++i){
        const size_t length=strlen(roots->directories[i]);
        manager->directories[i]=malloc(length+1u);
        if(manager->directories[i]==NULL){portable_mod_manager_free(manager);return NULL;}
        memcpy(manager->directories[i],roots->directories[i],length+1u);
        manager->roots.directories[i]=manager->directories[i];
    }
    for(size_t i=0u;i<count;++i)memcpy(manager->ids[i],ids[i],PORTABLE_MOD_ID_CAPACITY);
    manager->count=count;
    portable_mod_manager_refresh(manager);
    return manager;
}

PortableModManager *portable_mod_manager_create(const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count)
{
    const PortableModRoots roots={{directory},1u};
    return portable_mod_manager_create_roots(&roots,ids,count);
}

#ifdef PORTABLE_ENABLE_SDL3
static void preview_abandon(PortableModManager *manager);
#endif
void portable_mod_manager_free(PortableModManager *manager)
{
    if(manager!=NULL){
#ifdef PORTABLE_ENABLE_SDL3
        preview_abandon(manager);
#endif
        portable_mods_free(manager->preview);
        for(size_t i=0u;i<manager->roots.count;++i)free(manager->directories[i]);
        free(manager);
    }
}

int portable_mod_manager_refresh(PortableModManager *manager)
{
    if(manager==NULL)return 0;
    portable_mods_free(manager->preview);manager->preview=NULL;manager->preview_id[0]=0;memset(&manager->preview_image,0,sizeof manager->preview_image);
#ifdef PORTABLE_ENABLE_SDL3
    ++manager->generation; /* A load still running belongs to the old catalog; its result is dropped. */
#endif
    manager->catalog_ready=(uint8_t)portable_mods_discover_roots(&manager->roots,&manager->catalog);
    if(!manager->catalog_ready)
        return notice(manager,manager->catalog.error);
    manager->notice[0]='\0';
    return 1;
}

const PortableModCatalog *portable_mod_manager_catalog(const PortableModManager *manager)
{return manager!=NULL?&manager->catalog:NULL;}
size_t portable_mod_manager_count(const PortableModManager *manager)
{return manager!=NULL?manager->count:0u;}
const char *portable_mod_manager_id(const PortableModManager *manager,size_t index)
{return manager!=NULL&&index<manager->count?manager->ids[index]:NULL;}
const char *portable_mod_manager_notice(const PortableModManager *manager)
{return manager!=NULL?manager->notice:"Mod manager unavailable";}
int portable_mod_manager_index(const PortableModManager *manager,const char *id)
{
    if(manager!=NULL&&id!=NULL)
        for(size_t i=0u;i<manager->count;++i)if(strcmp(manager->ids[i],id)==0)return (int)i;
    return -1;
}

int portable_mod_manager_remove(PortableModManager *manager,size_t index)
{
    if(manager==NULL||index>=manager->count)return 0;
    for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind)
        if(!strcmp(manager->asset_sources[kind],manager->ids[index]))manager->asset_sources[kind][0]=0;
    memmove(manager->ids+index,manager->ids+index+1u,
        (manager->count-index-1u)*sizeof manager->ids[0]);
    memset(manager->ids[--manager->count],0,sizeof manager->ids[0]);
    manager->notice[0]='\0';
    return 1;
}

int portable_mod_manager_toggle(PortableModManager *manager,const char *id)
{
    if(manager==NULL||id==NULL)return 0;
    int index=portable_mod_manager_index(manager,id);
    if(index>=0)return portable_mod_manager_remove(manager,(size_t)index);
    if(manager->catalog.overflow)return notice(manager,"Installed mod catalog exceeds 128 entries");
    const PortableModCatalogEntry *entry=find_entry(manager,id);
    if(entry==NULL)return notice(manager,"Mod is no longer installed");
    if(!entry->valid)return notice(manager,entry->error);
    if(manager->count==PORTABLE_MOD_LIMIT)return notice(manager,"At most 32 mods may be enabled");
    snprintf(manager->ids[manager->count++],PORTABLE_MOD_ID_CAPACITY,"%s",entry->id);
    manager->notice[0]='\0';
    return 1;
}

static int lists_id(const char (*ids)[PORTABLE_MOD_ID_CAPACITY],unsigned count,const char *id)
{
    for(unsigned i=0u;i<count;++i)if(strcmp(ids[i],id)==0)return 1;
    return 0;
}

/* Returns the mod's index, or -1 with a notice. ceiling is the latest index a
 * requesting dependent allows. */
static int enable_at_depth(PortableModManager *manager,const char *id,size_t ceiling,unsigned depth)
{
    const int existing=portable_mod_manager_index(manager,id);
    if(existing>=0)return existing;
    if(manager->catalog.overflow){notice(manager,"Installed mod catalog exceeds 128 entries");return -1;}
    const PortableModCatalogEntry *entry=find_entry(manager,id);
    if(entry==NULL){
        snprintf(manager->notice,sizeof manager->notice,"%s is not installed",id);
        return -1;
    }
    if(!entry->valid){
        snprintf(manager->notice,sizeof manager->notice,"%s: %s",id,entry->error);
        return -1;
    }
    const PortableModManifest *manifest=&entry->manifest;
    size_t high=ceiling<manager->count?ceiling:manager->count;
    for(size_t i=0u;i<high;++i){
        const PortableModCatalogEntry *other=find_entry(manager,manager->ids[i]);
        if(other!=NULL && other->valid &&
           (lists_id((const char (*)[PORTABLE_MOD_ID_CAPACITY])other->manifest.depends,other->manifest.dependency_count,id)||
            lists_id((const char (*)[PORTABLE_MOD_ID_CAPACITY])other->manifest.load_after,other->manifest.after_count,id)))
            high=i;
    }
    /* Missing dependencies go just before this mod; a cycle stops at the mod
     * limit and strict validation then names the problem. */
    for(unsigned i=0u;i<manifest->dependency_count;++i){
        const char *need=manifest->depends[i];
        if(portable_mod_manager_index(manager,need)>=0)continue;
        if(depth>=PORTABLE_MOD_LIMIT || find_entry(manager,need)==NULL){
            snprintf(manager->notice,sizeof manager->notice,"%s requires %s, which is not installed",id,need);
            return -1;
        }
        const int placed=enable_at_depth(manager,need,high,depth+1u);
        if(placed<0)return -1;
        if((size_t)placed<=high)++high;
        const int cycle=portable_mod_manager_index(manager,id);
        if(cycle>=0)return cycle;
    }
    if(manager->count==PORTABLE_MOD_LIMIT){notice(manager,"At most 32 mods may be enabled");return -1;}
    size_t low=0u;
    for(size_t i=0u;i<manager->count;++i)
        if(lists_id((const char (*)[PORTABLE_MOD_ID_CAPACITY])manifest->depends,manifest->dependency_count,manager->ids[i])||
           lists_id((const char (*)[PORTABLE_MOD_ID_CAPACITY])manifest->load_after,manifest->after_count,manager->ids[i]))
            low=i+1u;
    /* Later mods win, so a new mod goes as late as its constraints allow.
     * Contradictory constraints keep it last for validation to explain. */
    const size_t at=low<=high?high:manager->count;
    memmove(manager->ids+at+1u,manager->ids+at,(manager->count-at)*sizeof manager->ids[0]);
    snprintf(manager->ids[at],PORTABLE_MOD_ID_CAPACITY,"%s",entry->id);
    ++manager->count;
    manager->notice[0]='\0';
    return (int)at;
}

int portable_mod_manager_enable(PortableModManager *manager,const char *id)
{
    if(manager==NULL||id==NULL)return 0;
    char saved[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    const size_t count=manager->count;
    memcpy(saved,manager->ids,sizeof saved);
    if(enable_at_depth(manager,id,manager->count,0u)>=0)return 1;
    char reason[PORTABLE_MOD_ERROR_CAPACITY];
    memcpy(reason,manager->notice,sizeof reason);
    (void)portable_mod_manager_select(manager,(const char (*)[PORTABLE_MOD_ID_CAPACITY])saved,count);
    memcpy(manager->notice,reason,sizeof reason);
    return 0;
}

size_t portable_mod_manager_disable(PortableModManager *manager,const char *id)
{
    const int index=portable_mod_manager_index(manager,id);
    if(index<0)return 0u;
    char removed[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    size_t removed_count=1u;
    snprintf(removed[0],sizeof removed[0],"%s",id);
    (void)portable_mod_manager_remove(manager,(size_t)index);
    for(int changed=1;changed;){
        changed=0;
        for(size_t i=0u;i<manager->count && !changed;++i){
            const PortableModCatalogEntry *entry=find_entry(manager,manager->ids[i]);
            if(entry==NULL || !entry->valid)continue;
            for(size_t r=0u;r<removed_count && !changed;++r)
                if(lists_id((const char (*)[PORTABLE_MOD_ID_CAPACITY])entry->manifest.depends,
                            entry->manifest.dependency_count,removed[r])){
                    memcpy(removed[removed_count++],manager->ids[i],sizeof removed[0]);
                    (void)portable_mod_manager_remove(manager,i);
                    changed=1;
                }
        }
    }
    return removed_count;
}

int portable_mod_manager_select(PortableModManager *manager,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count)
{
    if(manager==NULL||count>PORTABLE_MOD_LIMIT||(count!=0u&&ids==NULL))return 0;
    memset(manager->ids,0,sizeof manager->ids);
    for(size_t i=0u;i<count;++i)snprintf(manager->ids[i],PORTABLE_MOD_ID_CAPACITY,"%s",ids[i]);
    manager->count=count;
    manager->notice[0]='\0';
    return 1;
}

int portable_mod_manager_move(PortableModManager *manager,size_t from,size_t to)
{
    if(manager==NULL||from>=manager->count||to>=manager->count)return 0;
    char id[PORTABLE_MOD_ID_CAPACITY];
    memcpy(id,manager->ids[from],sizeof id);
    if(from<to)memmove(manager->ids[from],manager->ids[from+1u],(to-from)*sizeof id);
    else if(from>to)memmove(manager->ids[to+1u],manager->ids[to],(from-to)*sizeof id);
    memcpy(manager->ids[to],id,sizeof id);
    manager->notice[0]='\0';
    return 1;
}

int portable_mod_manager_preview(const PortableModManager *manager,size_t index,char *text,size_t capacity)
{
    if(manager==NULL||index>=manager->count||text==NULL||capacity==0u)return 0;
    const PortableModCatalogEntry *entry=find_entry(manager,manager->ids[index]);
    const char *message="Later mods override earlier matching content";
    if(entry==NULL)message="Missing installed mod; remove it before applying";
    else if(!entry->valid)message=entry->error;
    else {
        const PortableModManifest *manifest=&entry->manifest;
        for(unsigned i=0u;i<manifest->dependency_count;++i){
            int prior=portable_mod_manager_index(manager,manifest->depends[i]);
            if(prior<0||(size_t)prior>=index){
                snprintf(text,capacity,"Requires %s earlier in the list",manifest->depends[i]);return 1;
            }
        }
        for(unsigned i=0u;i<manifest->after_count;++i){
            int prior=portable_mod_manager_index(manager,manifest->load_after[i]);
            if(prior>=0&&(size_t)prior>=index){
                snprintf(text,capacity,"Move after %s",manifest->load_after[i]);return 1;
            }
        }
    }
    snprintf(text,capacity,"%s",message);
    return 1;
}

void portable_mod_manager_candidate_free(PortableModManagerCandidate *candidate)
{
    if(candidate!=NULL){
        portable_script_host_free(candidate->scripts);
        portable_mods_free(candidate->mods);
        free(candidate);
    }
}

PortableModManagerCandidate *portable_mod_manager_prepare_domains(PortableModManager *manager,
    const PortableAssetPackView *base,const PortableAssetPackView *base_3d,PortableModLoadReport *report)
{
    if(manager==NULL||base==NULL||base_3d==NULL)return NULL;
    if(!manager->catalog_ready&&manager->count!=0u){
        notice(manager,manager->catalog.error[0]?manager->catalog.error:"Installed mod discovery failed");
        return NULL;
    }
    PortableModManagerCandidate *candidate=calloc(1u,sizeof *candidate);
    if(candidate==NULL){notice(manager,"Cannot allocate mod candidate");return NULL;}
    /* Keep the larger diagnostic table off small backend stacks. */
    PortableModLoadReport *owned_report=NULL;
    if(report==NULL){owned_report=calloc(1u,sizeof *owned_report);report=owned_report;}
    if(report==NULL){notice(manager,"Cannot allocate validation report");goto failed;}
    candidate->mods=portable_mods_load_roots_domains_strict(base,base_3d,&manager->roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])manager->ids,manager->count,report);
    if(candidate->mods==NULL){notice(manager,report->first_error[0]?report->first_error:"Mod validation failed");goto failed;}
    if(!portable_mods_select_assets(candidate->mods,base,base_3d,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])manager->asset_sources,manager->notice,sizeof manager->notice))goto failed;
    memcpy(candidate->asset_sources,manager->asset_sources,sizeof candidate->asset_sources);
    PortableScriptSource sources[PORTABLE_MOD_LIMIT];
    size_t count=0u;
    for(size_t i=0u;i<manager->count;++i){
        PortableScriptSource *source=&sources[count];
        if(portable_mods_script(candidate->mods,i,&source->id,&source->source,&source->size))++count;
    }
    if(count!=0u){
        candidate->scripts=portable_script_host_create(sources,count);
        if(candidate->scripts==NULL){
            notice(manager,"Cannot allocate Lua script host; previous mods remain active");goto failed;
        }
        if(portable_script_host_active_count(candidate->scripts)!=count){
            const char *id=portable_script_host_failed_id(candidate->scripts);
            snprintf(manager->notice,sizeof manager->notice,
                "%s: Lua script failed to load; previous mods remain active",id!=NULL?id:"Unknown mod");
            goto failed;
        }
    }
    /* The preview has no script hooks yet, but its declared scripts must still
     * compile before the full selection can replace the current one. */
    count=0u;
    for(size_t i=0u;i<manager->count;++i){
        PortableScriptSource *source=&sources[count];
        if(portable_mods_script_3d(candidate->mods,i,&source->id,&source->source,&source->size))++count;
    }
    if(count!=0u){
        PortableScriptHost *preview_scripts=portable_script_host_create(sources,count);
        if(preview_scripts==NULL){
            notice(manager,"Cannot allocate 3D Lua script host; previous mods remain active");goto failed;
        }
        if(portable_script_host_active_count(preview_scripts)!=count){
            const char *id=portable_script_host_failed_id(preview_scripts);
            snprintf(manager->notice,sizeof manager->notice,
                "%s: 3D Lua script failed to load; previous mods remain active",id!=NULL?id:"Unknown mod");
            portable_script_host_free(preview_scripts);goto failed;
        }
        portable_script_host_free(preview_scripts);
    }
    candidate->count=manager->count;
    memcpy(candidate->ids,manager->ids,sizeof candidate->ids);
    notice(manager,"Candidate validated; later mods override earlier matching content");
    free(owned_report);
    return candidate;
failed:
    free(owned_report);
    portable_mod_manager_candidate_free(candidate);
    return NULL;
}

PortableModManagerCandidate *portable_mod_manager_prepare(PortableModManager *manager,
    const PortableAssetPackView *base,PortableModLoadReport *report)
{
    return portable_mod_manager_prepare_domains(manager,base,base,report);
}

void portable_mod_manager_set_sources(PortableModManager *manager,
    const char sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY])
{
    if(!manager||!sources)return;
    for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind)
        if(!memchr(sources[kind],0,PORTABLE_MOD_ID_CAPACITY)||
           (sources[kind][0]&&strcmp(sources[kind],"@base")&&!portable_mod_id_valid(sources[kind])))return;
    memcpy(manager->asset_sources,sources,sizeof manager->asset_sources);
}
const char *portable_mod_manager_source(const PortableModManager *manager,unsigned kind)
{return manager&&kind<PORTABLE_ASSET_SOURCE_COUNT?manager->asset_sources[kind]:"";}
int portable_mod_manager_cycle_source(PortableModManager *manager,unsigned kind,int direction)
{
    if(!manager||kind>=PORTABLE_ASSET_SOURCE_COUNT)return 0;
    unsigned at=0;
    if(!strcmp(manager->asset_sources[kind],"@base"))at=1;
    for(size_t i=0;i<manager->count;++i)
        if(!strcmp(manager->asset_sources[kind],manager->ids[i]))at=(unsigned)i+2u;
    const unsigned count=(unsigned)manager->count+2u;
    at=(at+count+(direction<0?count-1u:1u))%count;
    snprintf(manager->asset_sources[kind],PORTABLE_MOD_ID_CAPACITY,"%s",
        at==0?"":at==1?"@base":manager->ids[at-2u]);
    return 1;
}
static PortableModSet *load_preview(const PortableModRoots *roots,const char *id,
    const PortableAssetPackView *base,PortableNativeImage *image)
{
    char selected[1][PORTABLE_MOD_ID_CAPACITY]={{0}};
    snprintf(selected[0],sizeof selected[0],"%s",id);
    memset(image,0,sizeof *image);
    PortableModSet *set=portable_mods_load_roots_domains_strict(base,base,roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])selected,1,NULL);
    const PortableAssetPackView *pack=portable_mods_pack_3d(set);
    PortableCourse3DCatalog row;PortableCourse3DView course;PortableCourse3DMeta meta;
    if(pack && portable_course_3d_catalog_at(pack,0,&row,NULL,0) &&
       portable_course_3d_open(pack,row.course_id,&course,NULL,0) && portable_course_3d_meta(&course,&meta))
        (void)portable_course_3d_texture(&course,meta.preview_id,image);
    return set;
}

#ifdef PORTABLE_ENABLE_SDL3
/* The worker owns this request outright (own copies of the roots), so a manager
 * that is freed or refreshed mid-load never waits for it: the request moves to a
 * main-thread list of orphans, reaped once finished. Results of a stale
 * generation are dropped. portable_mod_manager_drain waits for orphans at exit. */
typedef struct PreviewRequest {
    PortableLoadJob job;
    PortableModRoots roots;
    char *directories[PORTABLE_MOD_ROOT_LIMIT];
    char id[PORTABLE_MOD_ID_CAPACITY];
    const PortableAssetPackView *base;
    uint32_t generation;
    PortableModSet *set;
    PortableNativeImage image;
    struct PreviewRequest *next;
} PreviewRequest;
static PreviewRequest *orphans;

static void request_free(PreviewRequest *request)
{
    for(size_t i=0u;i<request->roots.count;++i)free(request->directories[i]);
    portable_mods_free(request->set);
    free(request);
}
static int preview_work(void *user,PortableLoadJob *job)
{
    (void)job;
    PreviewRequest *request=user;
    request->set=load_preview(&request->roots,request->id,request->base,&request->image);
    return 1;
}
static void reap_orphans(void)
{
    for(PreviewRequest **at=&orphans;*at!=NULL;){
        PreviewRequest *request=*at;
        if(portable_load_finish(&request->job)==PORTABLE_LOAD_IDLE){at=&request->next;continue;}
        *at=request->next;request_free(request);
    }
}
void portable_mod_manager_drain(void)
{
    while(orphans!=NULL){reap_orphans();if(orphans!=NULL)SDL_Delay(1);}
}
static void preview_abandon(PortableModManager *manager)
{
    PreviewRequest *request=manager->request;
    if(request==NULL)return;
    manager->request=NULL;
    request->next=orphans;orphans=request;
    reap_orphans();
}
int portable_mod_manager_preview_poll(PortableModManager *manager)
{
    reap_orphans();
    PreviewRequest *request=manager?manager->request:NULL;
    if(request==NULL||portable_load_finish(&request->job)==PORTABLE_LOAD_IDLE)return 0;
    manager->request=NULL;
    if(request->generation==manager->generation){
        portable_mods_free(manager->preview);
        manager->preview=request->set;request->set=NULL;manager->preview_image=request->image;
        memcpy(manager->preview_id,request->id,sizeof manager->preview_id);
    }
    request_free(request);
    return 1;
}
int portable_mod_manager_preview_busy(const PortableModManager *manager)
{return manager&&manager->request!=NULL;}
static void preview_start(PortableModManager *manager,const char *id,const PortableAssetPackView *base)
{
    PreviewRequest *request=calloc(1u,sizeof *request);
    int started=0;
    if(request!=NULL){
        request->roots.count=manager->roots.count;
        started=1;
        for(size_t i=0u;i<manager->roots.count;++i){
            const size_t length=strlen(manager->directories[i]);
            request->directories[i]=malloc(length+1u);
            if(request->directories[i]==NULL){started=0;request->roots.count=i;break;}
            memcpy(request->directories[i],manager->directories[i],length+1u);
            request->roots.directories[i]=request->directories[i];
        }
        snprintf(request->id,sizeof request->id,"%s",id);
        request->base=base;request->generation=manager->generation;
        if(started)started=portable_load_start(&request->job,preview_work,request);
    }
    if(started){manager->request=request;return;}
    if(request!=NULL){(void)portable_load_finish(&request->job);request_free(request);}
    /* No worker could start: record an empty result rather than block the
     * frame or retry every frame. */
    snprintf(manager->preview_id,sizeof manager->preview_id,"%s",id);
    memset(&manager->preview_image,0,sizeof manager->preview_image);
}
#else
void portable_mod_manager_drain(void){}
int portable_mod_manager_preview_poll(PortableModManager *manager){(void)manager;return 0;}
int portable_mod_manager_preview_busy(const PortableModManager *manager){(void)manager;return 0;}
#endif

const PortableNativeImage *portable_mod_manager_preview_image(PortableModManager *manager,
    const char *id,const PortableAssetPackView *base)
{
    if(!manager||!id||!base)return NULL;
    (void)portable_mod_manager_preview_poll(manager);
    if(strcmp(manager->preview_id,id)){
#ifdef PORTABLE_ENABLE_SDL3
        if(portable_mod_manager_preview_busy(manager))return NULL;
        preview_start(manager,id,base);
        return NULL;
#else
        portable_mods_free(manager->preview);
        snprintf(manager->preview_id,sizeof manager->preview_id,"%s",id);
        manager->preview=load_preview(&manager->roots,id,base,&manager->preview_image);
#endif
    }
    return manager->preview_image.pixels?&manager->preview_image:NULL;
}
