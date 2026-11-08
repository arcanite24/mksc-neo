#include "mod_loader.h"
#include "content_registry.h"
#include "game_data.h"
#include "mod_archive.h"
#include "course_3d.h"
#include "model_asset.h"
#include "file_read.h"
#include "source_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <sys/stat.h>
#endif

#define MOD_PACK_MAX_BYTES (64u*1024u*1024u)
#define MOD_TOTAL_MAX_BYTES (256u*1024u*1024u)
#define MOD_SCRIPT_MAX_BYTES (256u*1024u)
#define MOD_SECTION_LIMIT 16384u /* base pack plus every mod's sections */
struct PortableModSet {
    PortableAssetPackView pack,pack_3d,views[PORTABLE_MOD_LIMIT],views_2d[PORTABLE_MOD_LIMIT];
    char *scripts[PORTABLE_MOD_LIMIT];
    size_t script_sizes[PORTABLE_MOD_LIMIT];
    size_t layer_count; /* base plus every loaded mod that carries a pack */
    PortableAssetSectionView sections[MOD_SECTION_LIMIT];
    PortableAssetSectionView sections_3d[MOD_SECTION_LIMIT];
    PortableAssetSectionView *projected_sections[PORTABLE_MOD_LIMIT];
    PortableModManifest manifests[PORTABLE_MOD_LIMIT];
    uint8_t *bytes[PORTABLE_MOD_LIMIT];
    PortableGameDataOverlay *data[PORTABLE_MOD_LIMIT][2];
    PortableAssetSectionView *normalized[PORTABLE_MOD_LIMIT][2];
    PortableModelBindingOverlay model_bindings[PORTABLE_MOD_LIMIT][2];
    PortableAssetSectionView *model_sections[PORTABLE_MOD_LIMIT][2];
    PortableModIdentity identity,identity_3d;
    size_t count;
    PortableAssetPackView raw_views[PORTABLE_MOD_LIMIT],raw_views_2d[PORTABLE_MOD_LIMIT];
    PortableModelBindingOverlay source_bindings[2];
};
static uint8_t *read_bounded(const char *path,size_t maximum,size_t *size)
{
    uint8_t *bytes=NULL;
    size_t length=0;
    if(portable_file_read_bounded(path,maximum,&bytes,&length)!=PORTABLE_FILE_READ_OK)
        return NULL;
    if(!length){free(bytes);return NULL;}
    *size=length;
    return bytes;
}
/* A mod is a folder <root>/<id>/ or, when that has no mod.json, <root>/<id>.zip.
 * Both resolve the same relative names under the same size limits. */
typedef struct {
    const char *directory,*id;
    const PortableModArchive *archive;
    PortableModArchive *owned_archive;
} ModSource;

static void source_close(ModSource *source)
{
    portable_mod_archive_free(source->owned_archive);
    source->owned_archive=NULL;
    source->archive=NULL;
}
/* Missing children are distinct from revoked/provider failures. Never let an
 * inaccessible SAF root silently resolve an enabled ID from another root. */
static int source_probe(const char *directory,const char *id,char *path,size_t capacity)
{
#ifdef PORTABLE_ENABLE_SDL3
    SDL_PathInfo info;
    if(portable_source_path_is_uri(directory) &&
        (!portable_source_path_info(directory,&info) || info.type!=SDL_PATHTYPE_DIRECTORY))
        return -1;
#endif
    for(unsigned zipped=0;zipped<2u;++zipped){
        char relative[PORTABLE_MOD_ID_CAPACITY+16];
        const int length=snprintf(relative,sizeof relative,zipped?"%s.zip":"%s/mod.json",id);
        if(length<=0 || (size_t)length>=sizeof relative)return -1;
        const int resolved=portable_source_path_resolve(directory,relative,path,capacity);
        if(resolved<0)return -1;
        if(!resolved)continue;
#ifdef PORTABLE_ENABLE_SDL3
        if(portable_source_path_info(path,&info))return zipped?2:1;
        if(portable_source_path_is_uri(path))return -1;
#else
#ifdef _WIN32
        struct _stat64 info;
        if(_stat64(path,&info)==0)return zipped?2:1;
#else
        struct stat info;
        if(stat(path,&info)==0)return zipped?2:1;
#endif
#endif
    }
    return 0;
}

static uint8_t *source_read(const ModSource *source,const char *name,size_t maximum,
    size_t *size,char *where,size_t where_capacity,char *why,size_t why_capacity)
{
    char archive_error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY]="unreadable, empty or over its size limit";
    uint8_t *bytes=NULL;
    if(source->archive!=NULL){
        snprintf(where,where_capacity,"%s.zip:%s (root %.2048s)",source->id,name,source->directory);
        bytes=portable_mod_archive_read(source->archive,name,maximum,size,archive_error);
    }else{
        char relative[4096];
        const int length=snprintf(relative,sizeof relative,"%s/%s",source->id,name);
        snprintf(where,where_capacity,"%s:%s (root %.2048s)",source->id,name,source->directory);
        const int resolved=length>0 && (size_t)length<sizeof relative ?
            portable_source_path_resolve(source->directory,relative,where,where_capacity):-1;
        if(resolved==1)bytes=read_bounded(where,maximum,size);
        else snprintf(archive_error,sizeof archive_error,resolved<0?
            "source path resolution failed":"source file is missing");
    }
    if(bytes==NULL&&why!=NULL)snprintf(why,why_capacity,"%s",archive_error);
    return bytes;
}
static int source_open(ModSource *source,char *where,size_t where_capacity)
{
    source->archive=NULL;
    source->owned_archive=NULL;
    char path[4096],error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
    const int kind=source_probe(source->directory,source->id,path,sizeof path);
    if(kind<=0){
        snprintf(where,where_capacity,"%.63s: %s in root %.2048s",source->id,
            kind<0?"cannot access mod source":"no mod folder with mod.json or ZIP",source->directory);
        return 0;
    }
    if(kind==1){snprintf(where,where_capacity,"%s",path);return 1;}
    source->owned_archive=portable_mod_archive_open(path,source->id,error);
    source->archive=source->owned_archive;
    if(source->archive==NULL){snprintf(where,where_capacity,"%.2048s: %.1024s",path,error);return 0;}
    return 1;
}

int portable_mod_roots_valid(const PortableModRoots *roots)
{
    if (!roots || !roots->count || roots->count > PORTABLE_MOD_ROOT_LIMIT) return 0;
    for (size_t i = 0; i < roots->count; ++i) {
        if (!roots->directories[i] || !roots->directories[i][0] ||
            strlen(roots->directories[i]) >= PORTABLE_MOD_DIRECTORY_CAPACITY) return 0;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(roots->directories[i], roots->directories[j])) return 0;
    }
    return 1;
}

/* Resolve the ID before opening payloads, so discovery, loading and archive
 * review cannot silently select different sources from the same root list. */
static int source_resolve(const PortableModRoots *roots, const char *id,
    ModSource *source, char *where, size_t capacity, int archive_override)
{
    size_t found = SIZE_MAX;
    for (size_t i = 0; i < roots->count; ++i) {
        char path[PORTABLE_MOD_DIRECTORY_CAPACITY];
        const int kind=source_probe(roots->directories[i],id,path,sizeof path);
        if(kind<0){
            snprintf(where,capacity,"%.63s: cannot access mod root %zu (%.2048s)",
                id,i,roots->directories[i]);
            return 0;
        }
        if(!kind)continue;
        if (found != SIZE_MAX) {
            snprintf(where, capacity, "%.63s: duplicate ID in roots %zu (%.60s) and %zu (%.60s)",
                id, found, roots->directories[found], i, roots->directories[i]); return 0;
        }
        found = i;
    }
    if (archive_override) {
        if (found != SIZE_MAX && found != 0u) {
            snprintf(where, capacity, "%.63s: installation conflicts with root %zu (%.120s)",
                id, found, roots->directories[found]); return 0;
        }
        source->directory = roots->directories[0];
        return 1;
    }
    if (found == SIZE_MAX) {
        snprintf(where, capacity, "%s: no mod folder with mod.json or ZIP in configured roots", id);
        return 0;
    }
    source->directory = roots->directories[found];
    return source_open(source, where, capacity);
}

#ifdef PORTABLE_ENABLE_SDL3
typedef struct {
    PortableModCatalog *catalog;
    int failed;
} ModDiscovery;

static SDL_EnumerationResult SDLCALL discover_entry(void *context,const char *directory,const char *name)
{
    ModDiscovery *discovery=context;
    PortableModCatalog *catalog=discovery->catalog;
    char path[4096],id[PORTABLE_MOD_ID_CAPACITY];
    const size_t length=strlen(name);
    if(strcmp(name,".")==0 || strcmp(name,"..")==0)return SDL_ENUM_CONTINUE;
    if(portable_source_path_resolve(directory,name,path,sizeof path)!=1){
        snprintf(catalog->error,sizeof catalog->error,"cannot resolve installed mod entry");
        discovery->failed=1;
        return SDL_ENUM_FAILURE;
    }
    SDL_PathInfo info;
    if(!portable_source_path_info(path,&info)){
        snprintf(catalog->error,sizeof catalog->error,"cannot inspect installed mod entry");
        discovery->failed=1;
        return SDL_ENUM_FAILURE;
    }
    const int zipped=info.type==SDL_PATHTYPE_FILE && length>4u && strcmp(name+length-4u,".zip")==0;
    if(info.type!=SDL_PATHTYPE_DIRECTORY && !zipped)return SDL_ENUM_CONTINUE;
    const size_t id_length=length-(zipped?4u:0u);
    if(id_length==0u || id_length>=sizeof id){
        snprintf(catalog->error,sizeof catalog->error,"installed mod ID exceeds 63 characters");
        discovery->failed=1;
        return SDL_ENUM_FAILURE;
    }
    memcpy(id,name,id_length);id[id_length]='\0';
    for(size_t i=0u;i<catalog->count;++i)
        if(strcmp(catalog->entries[i].id,id)==0)return SDL_ENUM_CONTINUE;
    if(catalog->count==PORTABLE_MOD_CATALOG_LIMIT){
        catalog->overflow=1u;
        snprintf(catalog->error,sizeof catalog->error,"installed mod catalog exceeds 128 entries");
        return SDL_ENUM_SUCCESS;
    }
    PortableModCatalogEntry *entry=&catalog->entries[catalog->count++];
    memcpy(entry->id,id,id_length+1u);
    entry->from_zip=(uint8_t)zipped;
    return SDL_ENUM_CONTINUE;
}

static int catalog_order(const void *left,const void *right)
{
    const PortableModCatalogEntry *a=left,*b=right;
    return strcmp(a->id,b->id);
}
#endif

int portable_mods_discover_roots(const PortableModRoots *roots,PortableModCatalog *catalog)
{
    if(catalog==NULL)return 0;
    memset(catalog,0,sizeof *catalog);
    if(!portable_mod_roots_valid(roots)){
        snprintf(catalog->error,sizeof catalog->error,"mod directory unavailable");return 0;
    }
#ifdef PORTABLE_ENABLE_SDL3
    ModDiscovery discovery={catalog,0};
    for (size_t root = 0; root < roots->count; ++root) {
        if (!portable_source_path_enumerate(roots->directories[root], discover_entry, &discovery) || discovery.failed) {
            if (!catalog->error[0]) snprintf(catalog->error, sizeof catalog->error,
                "cannot enumerate mod root %zu: %.192s", root, SDL_GetError());
            return 0;
        }
    }
    qsort(catalog->entries,catalog->count,sizeof catalog->entries[0],catalog_order);
    if(catalog->overflow)return 0;
    for(size_t i=0u;i<catalog->count;++i){
        PortableModCatalogEntry *entry=&catalog->entries[i];
        if(!portable_mod_id_valid(entry->id)){
            snprintf(entry->error,sizeof entry->error,"invalid mod ID");continue;
        }
        ModSource source={.id=entry->id};
        char where[4096],why[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
        const int opened=source_resolve(roots,entry->id,&source,where,sizeof where,0);
        for(size_t root=0;root<roots->count;++root)
            if(source.directory==roots->directories[root])entry->source_root=(uint8_t)root;
        if(!opened){
            snprintf(entry->error,sizeof entry->error,"%.255s",where);continue;
        }
        entry->from_zip=(uint8_t)(source.archive!=NULL);
        size_t size=0u;
        uint8_t *json=source_read(&source,"mod.json",16384u,&size,where,sizeof where,why,sizeof why);
        if(json==NULL)snprintf(entry->error,sizeof entry->error,"manifest: %s",why);
        else if(portable_mod_manifest_parse((const char *)json,size,&entry->manifest,entry->error)){
            if(strcmp(entry->manifest.id,entry->id)!=0)
                snprintf(entry->error,sizeof entry->error,"manifest ID must match folder or ZIP name");
            else entry->valid=1u;
        }
        free(json);
        source_close(&source);
    }
    return 1;
#else
    snprintf(catalog->error,sizeof catalog->error,"installed mod discovery requires a filesystem backend");
    return 0;
#endif
}

int portable_mods_discover(const char *directory, PortableModCatalog *catalog)
{
    const PortableModRoots roots = {{directory}, 1u};
    return portable_mods_discover_roots(&roots, catalog);
}

static void put_size(uint8_t out[8],size_t value)
{
    for(unsigned i=0u;i<8u;++i){out[i]=(uint8_t)value;value>>=8u;}
}
/* Fingerprint: SHA-256 of manifest digest, pack and script (with lengths). */
static void fingerprint(const uint8_t manifest_digest[32],const uint8_t *pack,size_t pack_size,
    const char *script,size_t script_size,uint8_t out[8])
{
    uint8_t sizes[16],digest[32];
    put_size(sizes,pack_size);put_size(sizes+8,script_size);
    const uint8_t *parts[]={manifest_digest,sizes,pack,(const uint8_t *)script};
    const size_t lengths[]={32u,16u,pack_size,script_size};
    portable_asset_pack_sha256(parts,lengths,4u,digest);
    memcpy(out,digest,8u);
}
static int selected(const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,const char *id)
{
    for(size_t i=0u;i<count;++i)if(strcmp(ids[i],id)==0)return (int)i;
    return -1;
}
static int loaded(const PortableModSet *mods,const char *id)
{
    for(size_t i=0u;i<mods->count;++i)if(strcmp(mods->manifests[i].id,id)==0)return 1;
    return 0;
}

static void report_error(PortableModLoadReport *report,size_t index,const char *message)
{
    if(report==NULL)return;
    if(report->first_error[0]=='\0')snprintf(report->first_error,sizeof report->first_error,"%s",message);
    if(index<report->count && report->entries[index].message[0]=='\0')
        snprintf(report->entries[index].message,sizeof report->entries[index].message,"%s",message);
}

/* Preserve the CLI's diagnostics while exposing the same validation result to
 * the optional manager. The first failure per request is its actionable row. */
static void reject_mod(PortableModLoadReport *report,size_t index,const char *format,...)
{
    va_list arguments,copy;
    va_start(arguments,format);
    va_copy(copy,arguments);
    vfprintf(stderr,format,arguments);
    va_end(arguments);
    if(report!=NULL){
        char message[4096u+PORTABLE_MOD_ERROR_CAPACITY+64u];
        vsnprintf(message,sizeof message,format,copy);
        const size_t size=strlen(message);
        if(size!=0u && message[size-1u]=='\n')message[size-1u]='\0';
        /* Keep the reason even when a long installation path would otherwise
         * consume the entire UI message. The row already carries its ID. */
        const size_t start=size>=PORTABLE_MOD_ERROR_CAPACITY?size-(PORTABLE_MOD_ERROR_CAPACITY-1u):0u;
        report_error(report,index,message+start);
    }
    va_end(copy);
}

/* A candidate owns normalized data until both composition and roster
 * validation succeed. Rejected candidates never become accepted layers. */
typedef struct
{
    PortableAssetPackView view;
    PortableGameDataOverlay *data;
    PortableAssetSectionView *normalized;
    size_t normalized_cost;
} ModPackCandidate;

static int dependencies_valid(const PortableModSet *mods, const PortableModManifest *manifest,
                              const char ids[][PORTABLE_MOD_ID_CAPACITY], size_t count,
                              size_t index, const char *path,PortableModLoadReport *report)
{
    int valid = 1;
    for (unsigned j = 0u; j < manifest->dependency_count; ++j)
        if (!loaded(mods, manifest->depends[j]))
        {
            reject_mod(report,index, "%s: depends: %s must be enabled and loaded earlier; disabled\n", path,
                    manifest->depends[j]);
            valid = 0;
        }
    for (unsigned j = 0u; j < manifest->after_count; ++j)
    {
        const int position = selected(ids, count, manifest->load_after[j]);
        if (position >= 0 && (size_t)position >= index)
        {
            reject_mod(report,index, "%s: load_after: %s must appear earlier; disabled\n", path,
                    manifest->load_after[j]);
            valid = 0;
        }
    }
    return valid;
}

static int find_game_data(const PortableAssetPackView *view, PortableAssetSectionView *json,
                          int *found)
{
    *found = 0;
    for (uint32_t index = 0u; index < view->section_count; ++index)
    {
        PortableAssetSectionView section;
        if (portable_asset_pack_section_at(view, index, &section) != PORTABLE_ASSET_PACK_OK)
            return 0;
        if (section.kind == PORTABLE_ASSET_SECTION_GAME_DATA_JSON)
        {
            if (section.content_id != 1u || *found)
                return 0;
            *json = section;
            *found = 1;
        }
    }
    return 1;
}

static void normalize_candidate(ModPackCandidate *pack, const PortableAssetPackView **layers,
                                size_t layer_count, const PortableAssetSectionView *json,
                                char *error)
{
    PortableAssetSectionView *candidate_sections =
        calloc(MOD_SECTION_LIMIT, sizeof *candidate_sections);
    PortableAssetPackView candidate;
    layers[layer_count] = &pack->view;
    PortableAssetPackStatus candidate_status = PORTABLE_ASSET_PACK_INSUFFICIENT_CAPACITY;
    if (candidate_sections != NULL)
        candidate_status = portable_asset_pack_compose(layers, layer_count + 1u, candidate_sections,
                                                       MOD_SECTION_LIMIT, &candidate);
    if (candidate_status == PORTABLE_ASSET_PACK_OK)
        pack->data = portable_game_data_apply(&candidate, json, error);
    else
        snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "data: %s",
                 portable_asset_pack_status_name(candidate_status));
    if (pack->data != NULL)
    {
        const PortableAssetPackView *generated = portable_game_data_pack(pack->data);
        const size_t capacity = (size_t)pack->view.section_count + generated->section_count;
        pack->normalized = calloc(capacity, sizeof *pack->normalized);
        const PortableAssetPackView *data_layers[] = {&pack->view, generated};
        PortableAssetPackView result;
        if (pack->normalized != NULL &&
            portable_asset_pack_compose(data_layers, 2u, pack->normalized, capacity, &result) ==
                PORTABLE_ASSET_PACK_OK)
        {
            pack->view = result;
            pack->normalized_cost =
                portable_game_data_bytes(pack->data) + capacity * sizeof *pack->normalized;
        }
        else
        {
            free(pack->normalized);
            pack->normalized = NULL;
            snprintf(error, PORTABLE_MOD_ERROR_CAPACITY,
                     "data: normalized section allocation/composition failed");
        }
    }
    free(candidate_sections);
}

static void discard_candidate(ModPackCandidate *pack)
{
    portable_game_data_free(pack->data);
    free(pack->normalized);
}

typedef struct
{
    PortableAssetPackView view;
    PortableModelBindingOverlay binding;
    PortableAssetSectionView *sections;
    size_t cost;
} ModModelProjection;

static void discard_model_projection(ModModelProjection *projection)
{
    portable_model_bindings_overlay_close(&projection->binding);
    free(projection->sections);
    memset(projection, 0, sizeof *projection);
}

/* Registry overlays are normalized separately against each domain's accepted
 * layers. A preceding normalized registry already owns its inherited rows;
 * reverse lookup therefore has exactly the ordinary section replacement
 * semantics without composing or inspecting any mesh/material references. */
static int project_model_bindings(const PortableAssetPackView *view,
                                  const PortableAssetPackView *const *layers, size_t layer_count,
                                  size_t available, ModModelProjection *out, char *error)
{
    out->view = *view;
    PortableAssetSectionView overlay = {0};
    int found = 0;
    for (uint32_t i = 0; i < view->section_count; ++i)
    {
        PortableAssetSectionView section;
        if (portable_asset_pack_section_at(view, i, &section) != PORTABLE_ASSET_PACK_OK)
        {
            snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings: invalid layer section");
            return 0;
        }
        if (section.kind != PORTABLE_ASSET_SECTION_MODEL_BINDINGS || section.version != 2u)
            continue;
        if (section.content_id != 1u || found)
        {
            snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings overlay requires one registry with ID1");
            return 0;
        }
        overlay = section;
        found = 1;
    }
    if (!found)
        return 1; /* Preserve every legacy registry and its fingerprints. */
    if (view->section_count > MOD_SECTION_LIMIT)
    {
        snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings: section budget exceeded");
        return 0;
    }
    const size_t section_cost = (size_t)view->section_count * sizeof *out->sections;
    if (section_cost > available || available - section_cost < 8u)
    {
        snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings: normalized memory budget exceeded");
        return 0;
    }
    PortableAssetSectionView previous;
    const PortableAssetSectionView *inherited = NULL;
    for (size_t i = layer_count; i > 0u; --i)
    {
        const PortableAssetPackStatus status = portable_asset_pack_find_section(
            layers[i - 1u], PORTABLE_ASSET_SECTION_MODEL_BINDINGS, 1u, &previous);
        if (status == PORTABLE_ASSET_PACK_OK)
        {
            inherited = &previous;
            break;
        }
        if (status != PORTABLE_ASSET_PACK_NOT_FOUND)
        {
            snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings: invalid accepted registry");
            return 0;
        }
    }
    if (!portable_model_bindings_overlay_prepare(inherited, &overlay, &out->binding, error,
                                                  PORTABLE_MOD_ERROR_CAPACITY))
        return 0;
    if (out->binding.size > available - section_cost)
    {
        snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings: normalized memory budget exceeded");
        discard_model_projection(out);
        return 0;
    }
    out->sections = calloc(view->section_count, sizeof *out->sections);
    const PortableAssetPackView replacement = {.section_count = 1u,
                                              .resolved_sections = &out->binding.section};
    const PortableAssetPackView *parts[] = {view, &replacement};
    if (!out->sections || portable_asset_pack_compose(parts, 2u, out->sections,
                                                     view->section_count, &out->view) !=
                              PORTABLE_ASSET_PACK_OK)
    {
        snprintf(error, PORTABLE_MOD_ERROR_CAPACITY, "model-bindings: normalized allocation/composition failed");
        discard_model_projection(out);
        return 0;
    }
    out->cost = section_cost + out->binding.size;
    return 1;
}

static void identify_mod(PortableModIdentityEntry *identity, const PortableModManifest *manifest,
                         const uint8_t digest[32], const uint8_t *bytes, size_t size,
                         const char *script, size_t script_size)
{
    memcpy(identity->id, manifest->id, sizeof identity->id);
    memcpy(identity->version, manifest->version, sizeof identity->version);
    fingerprint(digest, bytes, size, script, script_size, identity->fingerprint);
}
/* Domain identities describe effective content, not container offsets. Legacy
 * 2D-only manifests deliberately keep the original raw-container fingerprint. */
static int section_order(const void *left,const void *right)
{
    const PortableAssetSectionView *a=left,*b=right;
    if(a->kind!=b->kind)return a->kind<b->kind?-1:1;
    return a->content_id<b->content_id?-1:a->content_id!=b->content_id;
}
static void put_u32(uint8_t *out,uint32_t value)
{
    for(unsigned i=0u;i<4u;++i)out[i]=(uint8_t)(value>>(8u*i));
}
static void metadata_string(uint8_t *bytes,size_t *used,const char *text)
{
    const size_t length=strlen(text);
    put_size(bytes+*used,length);*used+=8u;
    memcpy(bytes+*used,text,length);*used+=length;
}
static int identify_projection(PortableModIdentityEntry *identity,
    const PortableModManifest *manifest,const PortableAssetPackView *view,
    uint8_t domain,const char *script,size_t script_size)
{
    const size_t count=view!=NULL?view->section_count:0u;
    if(count>MOD_SECTION_LIMIT)return 0;
    PortableAssetSectionView *sections=calloc(count?count:1u,sizeof *sections);
    uint8_t *descriptors=calloc(count?count:1u,28u);
    const uint8_t **parts=calloc(2u*count+3u,sizeof *parts);
    size_t *sizes=calloc(2u*count+3u,sizeof *sizes);
    if(sections==NULL||descriptors==NULL||parts==NULL||sizes==NULL){
        free(sections);free(descriptors);free(parts);free(sizes);return 0;
    }
    int valid=1;
    for(size_t i=0u;i<count;++i)
        if(portable_asset_pack_section_at(view,(uint32_t)i,&sections[i])!=PORTABLE_ASSET_PACK_OK)valid=0;
    qsort(sections,count,sizeof *sections,section_order);
    /* Fixed manifest bounds fit below 4 KiB including all length prefixes. */
    uint8_t metadata[4096],digest[32];size_t used=0u;
    metadata[used++]=domain;
    metadata_string(metadata,&used,manifest->id);
    metadata_string(metadata,&used,manifest->name);
    metadata_string(metadata,&used,manifest->version);
    metadata_string(metadata,&used,manifest->pack);
    metadata_string(metadata,&used,manifest->script);
    metadata[used++]=manifest->dependency_count;
    for(unsigned i=0u;i<manifest->dependency_count;++i)metadata_string(metadata,&used,manifest->depends[i]);
    metadata[used++]=manifest->after_count;
    for(unsigned i=0u;i<manifest->after_count;++i)metadata_string(metadata,&used,manifest->load_after[i]);
    put_size(metadata+used,count);used+=8u;
    put_size(metadata+used,script_size);used+=8u;
    static const uint8_t tag[]="MKSC mod projection v1";
    parts[0]=tag;sizes[0]=sizeof tag;parts[1]=metadata;sizes[1]=used;
    for(size_t i=0u;i<count;++i){
        const PortableAssetSectionView *section=&sections[i];uint8_t *row=descriptors+28u*i;
        put_u32(row,section->kind);put_u32(row+4u,section->content_id);
        put_u32(row+8u,section->version);put_u32(row+12u,section->flags);
        put_u32(row+16u,section->semantic_revision);put_size(row+20u,section->size);
        parts[2u+2u*i]=row;sizes[2u+2u*i]=28u;
        parts[3u+2u*i]=section->bytes;sizes[3u+2u*i]=section->size;
    }
    parts[2u+2u*count]=(const uint8_t *)script;sizes[2u+2u*count]=script_size;
    if(valid){
        portable_asset_pack_sha256(parts,sizes,2u*count+3u,digest);
        memcpy(identity->id,manifest->id,sizeof identity->id);
        memcpy(identity->version,manifest->version,sizeof identity->version);
        memcpy(identity->fingerprint,digest,sizeof identity->fingerprint);
    }
    free(sections);free(descriptors);free(parts);free(sizes);return valid;
}
static int is_3d_section(uint32_t kind)
{
    return (kind>=PORTABLE_ASSET_SECTION_COURSE_3D&&kind<=PORTABLE_ASSET_SECTION_COURSE_3D_TEXTURE_BUNDLE)||
        kind==PORTABLE_ASSET_SECTION_DRIVER_3D||kind==PORTABLE_ASSET_SECTION_KART_3D||
        kind==PORTABLE_ASSET_SECTION_NDS_SEQUENCE||kind==PORTABLE_ASSET_SECTION_DRIVER_SPRITES||
        kind==PORTABLE_ASSET_SECTION_SOUND_SET||kind==PORTABLE_ASSET_SECTION_N64_SEQUENCE||
        kind==PORTABLE_ASSET_SECTION_FX_SET;
}
static int project_2d(const PortableAssetPackView *view,uint8_t targets,
    PortableAssetPackView *out,PortableAssetSectionView **owned,size_t available,size_t *cost,char *error)
{
    uint32_t count=0u;
    *cost=0u;
    if(view->section_count>MOD_SECTION_LIMIT){
        snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"projection exceeds 4096 sections");return 0;
    }
    for(uint32_t i=0u;i<view->section_count;++i){
        PortableAssetSectionView section;
        if(portable_asset_pack_section_at(view,i,&section)!=PORTABLE_ASSET_PACK_OK)return 0;
        if(is_3d_section(section.kind)){
            if(!(targets&PORTABLE_MOD_TARGET_3D)){
                snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"section %u/%u requires target 3d",section.kind,section.content_id);return 0;
            }
        }else ++count;
    }
    *out=*view;*owned=NULL;
    if(targets!=(PORTABLE_MOD_TARGET_2D|PORTABLE_MOD_TARGET_3D))return 1;
    const size_t allocation_count=count?count:1u;
    if(allocation_count>SIZE_MAX/sizeof **owned || allocation_count*sizeof **owned>available){
        snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"projection exceeds the 256 MiB mod memory budget");return 0;
    }
    *cost=allocation_count*sizeof **owned;
    *owned=calloc(allocation_count,sizeof **owned);
    if(*owned==NULL){snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"could not allocate 2d projection");return 0;}
    uint32_t next=0u;
    for(uint32_t i=0u;i<view->section_count;++i){
        PortableAssetSectionView section;
        (void)portable_asset_pack_section_at(view,i,&section);
        if(!is_3d_section(section.kind))(*owned)[next++]=section;
    }
    out->bytes=NULL;out->size=0u;out->resolved_sections=*owned;out->section_count=count;
    return 1;
}
static void mix_identity(PortableAssetPackView *pack,const PortableModIdentity *identity)
{
    if(identity->count==0u)return;
    static const uint8_t domain[]="MKSC mod identity v1";
    char text[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY];
    const size_t length=portable_mod_identity_format(identity,text,sizeof text);
    const uint8_t *parts[]={domain,pack->deterministic_digest,(const uint8_t *)text};
    const size_t sizes[]={sizeof domain,PORTABLE_ASSET_PACK_DIGEST_SIZE,length};
    uint8_t digest[PORTABLE_ASSET_PACK_DIGEST_SIZE];
    portable_asset_pack_sha256(parts,sizes,3u,digest);
    memcpy(pack->deterministic_digest,digest,sizeof digest);
}
static PortableModSet *load_sources(const PortableAssetPackView *base,const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report,
    const char *archive_id,const PortableModArchive *archive)
{
    if(report!=NULL)memset(report,0,sizeof *report);
    if(base==NULL||base_3d==NULL||!portable_mod_roots_valid(roots)||count>PORTABLE_MOD_LIMIT||(count!=0u&&ids==NULL)){
        report_error(report,0u,"invalid mod load arguments");return NULL;
    }
    const PortableAssetPackView *bases[] = {base, base_3d};
    for(unsigned domain=0u;domain<2u;++domain){
        for(uint32_t section_index=0u;section_index<bases[domain]->section_count;++section_index){
            PortableAssetSectionView section;
            if(portable_asset_pack_section_at(bases[domain],section_index,&section)!=PORTABLE_ASSET_PACK_OK){
                report_error(report,0u,"model-bindings: invalid base section");return NULL;
            }
            if(section.kind==PORTABLE_ASSET_SECTION_MODEL_BINDINGS && section.version==2u){
                report_error(report,0u,"model-bindings overlay requires a mod layer, not a base pack");return NULL;
            }
        }
    }
#ifdef PORTABLE_ENABLE_SDL3
    for(size_t root=0;root<roots->count;++root){
        SDL_PathInfo info;
        if(portable_source_path_is_uri(roots->directories[root]) &&
            (!portable_source_path_info(roots->directories[root],&info) ||
             info.type!=SDL_PATHTYPE_DIRECTORY)){
            report_error(report,0u,"picker-backed mod root is unavailable; reselect the folder");
            return NULL;
        }
    }
#endif
    if(report!=NULL){
        report->count=count;
        for(size_t i=0u;i<count;++i)
            snprintf(report->entries[i].id,sizeof report->entries[i].id,"%s",ids[i]);
    }
    for(size_t i=0u;i<count;++i){
        if(!portable_mod_id_valid(ids[i])){report_error(report,i,"invalid mod ID");return NULL;}
        for(size_t j=0u;j<i;++j)if(strcmp(ids[i],ids[j])==0){
            report_error(report,i,"duplicate mod ID");return NULL;
        }
    }
    int composition_dirty[2] = {1, 1};
    PortableModSet *mods=calloc(1u,sizeof *mods);if(mods==NULL){
        report_error(report,0u,"could not allocate mod candidate");return NULL;
    }
    mods->layer_count=1u;mods->identity.api_version=PORTABLE_MOD_API_VERSION;
    mods->identity_3d.api_version=PORTABLE_MOD_API_VERSION;
    const PortableAssetPackView *layers[PORTABLE_ASSET_PACK_MAX_LAYERS]={base};
    const PortableAssetPackView *layers_3d[PORTABLE_ASSET_PACK_MAX_LAYERS]={base_3d};
    size_t count_2d=1u,count_3d=1u;
    size_t total=0u;
    for(size_t i=0u;i<count;++i){
        char path[4096],error[PORTABLE_MOD_ERROR_CAPACITY],why[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
        ModSource source={.id=ids[i]};
        const int overridden=archive!=NULL && strcmp(ids[i],archive_id)==0;
        if(!source_resolve(roots,ids[i],&source,path,sizeof path,overridden)){
            reject_mod(report,i,"%s; disabled\n",path[0]?path:ids[i]);continue;
        }
        if(overridden)source.archive=archive;
        size_t size=0u;uint8_t *json=source_read(&source,"mod.json",16384u,&size,path,sizeof path,why,sizeof why);
        PortableModManifest manifest;
        const int readable=json!=NULL;
        const int parsed=readable&&portable_mod_manifest_parse((const char *)json,size,&manifest,error);
        uint8_t manifest_digest[32]={0};
        if(readable){const uint8_t *part=json;portable_asset_pack_sha256(&part,&size,1u,manifest_digest);}
        free(json);
        if(!parsed){
            if(!readable)snprintf(error,sizeof error,"manifest: %s",why);
            reject_mod(report,i,"%s: %s; disabled\n",path,error);source_close(&source);continue;
        }
        if(strcmp(manifest.id,ids[i])!=0){
            reject_mod(report,i,"%s: id: must match %s %s; disabled\n",path,source.archive?"zip name":"folder",ids[i]);
            source_close(&source);continue;
        }
        if(!dependencies_valid(mods,&manifest,ids,count,i,path,report)){
            source_close(&source);continue;
        }
        char *script=NULL;size_t script_size=0u;
        if(manifest.script[0]!='\0'){
            uint8_t *text=source_read(&source,manifest.script,MOD_SCRIPT_MAX_BYTES,&script_size,path,sizeof path,why,sizeof why);
            if(text==NULL){reject_mod(report,i,"%s: script: %s (limit 256 KiB); disabled\n",path,why);source_close(&source);continue;}
            script=(char *)text;
        }
        const size_t index=mods->count;
        PortableModIdentityEntry *identity=&mods->identity.mods[mods->identity.count];
        PortableModIdentityEntry *identity_3d=&mods->identity_3d.mods[mods->identity_3d.count];
        const int from_zip=source.archive!=NULL;
        if(manifest.pack[0]=='\0'){
            source_close(&source);
            int identified=1;
            if(manifest.targets&PORTABLE_MOD_TARGET_2D){
                if(manifest.targets==PORTABLE_MOD_TARGET_2D)
                    identify_mod(identity,&manifest,manifest_digest,NULL,0u,script,script_size);
                else identified=identify_projection(identity,&manifest,NULL,PORTABLE_MOD_TARGET_2D,script,script_size);
            }
            if(identified&&(manifest.targets&PORTABLE_MOD_TARGET_3D))
                identified=identify_projection(identity_3d,&manifest,NULL,PORTABLE_MOD_TARGET_3D,script,script_size);
            if(!identified||script_size>MOD_TOTAL_MAX_BYTES-total){
                reject_mod(report,i,"%s: could not allocate script projection; disabled\n",path);free(script);continue;
            }
            mods->manifests[index]=manifest;mods->scripts[index]=script;mods->script_sizes[index]=script_size;
            if(manifest.targets&PORTABLE_MOD_TARGET_2D)++mods->identity.count;
            if(manifest.targets&PORTABLE_MOD_TARGET_3D)++mods->identity_3d.count;
            ++mods->count;total+=script_size;
            if(report!=NULL)report->entries[i].accepted=1u;
            printf("mod loaded: %s %s (%s)%s\n",manifest.id,manifest.version,manifest.name,from_zip?" from zip":"");
            continue;
        }
        uint8_t *bytes=source_read(&source,manifest.pack,MOD_PACK_MAX_BYTES,&size,path,sizeof path,why,sizeof why);
        source_close(&source);
        if(bytes==NULL||size>MOD_TOTAL_MAX_BYTES-total||script_size>MOD_TOTAL_MAX_BYTES-total-size){
            reject_mod(report,i,"%s: pack: %s; disabled\n",path,bytes==NULL?why:"exceeds the 256 MiB mod memory budget");free(bytes);free(script);continue;
        }
        PortableAssetPackView raw;
        const PortableAssetPackStatus status=portable_asset_pack_open(bytes,size,&raw);
        if(status!=PORTABLE_ASSET_PACK_OK){
            reject_mod(report,i,"%s: %s; disabled\n",path,portable_asset_pack_status_name(status));free(bytes);free(script);continue;
        }
        error[0]='\0';
        PortableAssetSectionView json_section={0};
        int has_json=0;
        if(!find_game_data(&raw,&json_section,&has_json)){
            reject_mod(report,i,"%s: game-data: expected one JSON section with ID1; disabled\n",path);free(bytes);free(script);continue;
        }
        const size_t layer=mods->layer_count-1u;
        PortableAssetSectionView *projection=NULL;
        size_t projection_cost=0u;
        ModPackCandidate pending[2]={0};
        pending[1].view=raw;
        /* Project raw content before generating inherited JSON sections. Each
         * domain must normalize only against its own accepted predecessors. */
        if(!project_2d(&raw,manifest.targets,&pending[0].view,&projection,
            MOD_TOTAL_MAX_BYTES-total-size-script_size,&projection_cost,error)){
            reject_mod(report,i,"%s: %s; disabled\n",path,error);
            free(bytes);free(script);continue;
        }
        const PortableAssetPackView projected_raw=pending[0].view;
        size_t normalized_cost=0u;
        int normalized_valid=1;
        for(unsigned domain=0u;has_json&&domain<2u;++domain){
            const uint8_t target=domain?PORTABLE_MOD_TARGET_3D:PORTABLE_MOD_TARGET_2D;
            if(!(manifest.targets&target))continue;
            normalize_candidate(&pending[domain],domain?layers_3d:layers,
                domain?count_3d:count_2d,&json_section,error);
            if(pending[domain].data==NULL||pending[domain].normalized==NULL||
                pending[domain].normalized_cost>MOD_TOTAL_MAX_BYTES-total-size-script_size-projection_cost-normalized_cost){
                if(pending[domain].data!=NULL&&pending[domain].normalized!=NULL)
                    snprintf(error,sizeof error,"normalized memory budget exceeded");
                normalized_valid=0;break;
            }
            normalized_cost+=pending[domain].normalized_cost;
        }
        if(!normalized_valid){
            reject_mod(report,i,"%s: game-data/1: %s; disabled\n",path,error);
            free(projection);discard_candidate(&pending[0]);discard_candidate(&pending[1]);free(bytes);free(script);continue;
        }
        ModModelProjection models[2]={0};
        models[0].view=pending[0].view;models[1].view=pending[1].view;
        size_t model_cost=0u;
        int model_valid=1;
        for(unsigned domain=0u;domain<2u;++domain){
            const uint8_t target=domain?PORTABLE_MOD_TARGET_3D:PORTABLE_MOD_TARGET_2D;
            if(!(manifest.targets&target))continue;
            if(!project_model_bindings(&pending[domain].view,
                domain?layers_3d:layers,domain?count_3d:count_2d,
                MOD_TOTAL_MAX_BYTES-total-size-script_size-normalized_cost-projection_cost-model_cost,
                &models[domain],error)){
                model_valid=0;break;
            }
            model_cost+=models[domain].cost;
        }
        if(!model_valid){
            reject_mod(report,i,"%s: %s; disabled\n",path,error);
            discard_model_projection(&models[0]);discard_model_projection(&models[1]);
            free(projection);discard_candidate(&pending[0]);discard_candidate(&pending[1]);free(bytes);free(script);continue;
        }
        mods->views[layer]=models[1].view;mods->views_2d[layer]=models[0].view;
        if(manifest.targets&PORTABLE_MOD_TARGET_2D)layers[count_2d]=&mods->views_2d[layer];
        if(manifest.targets&PORTABLE_MOD_TARGET_3D)layers_3d[count_3d]=&mods->views[layer];
        int valid=1;
        for(unsigned domain=0u;domain<2u;++domain){
            const uint8_t target=domain?PORTABLE_MOD_TARGET_3D:PORTABLE_MOD_TARGET_2D;
            if(!(manifest.targets&target))continue;
            PortableAssetPackView *composed=domain?&mods->pack_3d:&mods->pack;
            composition_dirty[domain] = 1;
            const PortableAssetPackStatus status_compose=portable_asset_pack_compose(
                domain?layers_3d:layers,(domain?count_3d:count_2d)+1u,
                domain?mods->sections_3d:mods->sections,MOD_SECTION_LIMIT,composed);
            if(status_compose!=PORTABLE_ASSET_PACK_OK){
                snprintf(error,sizeof error,"%s projection: %s",domain?"3d":"2d",portable_asset_pack_status_name(status_compose));valid=0;break;
            }
            PortableDriverRoster roster;
            const PortableContentStatus roster_status=portable_content_registry_driver_roster(composed,&roster);
            if(roster_status!=PORTABLE_CONTENT_OK){
                snprintf(error,sizeof error,"%s driver-roster: %s",domain?"3d":"2d",portable_content_status_name(roster_status));valid=0;break;
            }
        }
        if(valid&&(manifest.targets&PORTABLE_MOD_TARGET_2D)){
            if(manifest.targets==PORTABLE_MOD_TARGET_2D && models[0].binding.bytes==NULL)
                identify_mod(identity,&manifest,manifest_digest,bytes,size,script,script_size);
            else valid=identify_projection(identity,&manifest,&models[0].view,PORTABLE_MOD_TARGET_2D,script,script_size);
        }
        if(valid&&(manifest.targets&PORTABLE_MOD_TARGET_3D))
            valid=identify_projection(identity_3d,&manifest,&models[1].view,PORTABLE_MOD_TARGET_3D,script,script_size);
        if(!valid){
            reject_mod(report,i,"%s: %s; disabled\n",path,error[0]?error:"projection allocation failed");
            discard_model_projection(&models[0]);discard_model_projection(&models[1]);
            free(projection);discard_candidate(&pending[0]);discard_candidate(&pending[1]);free(bytes);free(script);continue;
        }
        for(unsigned domain=0u;domain<2u;++domain){
            mods->model_bindings[index][domain]=models[domain].binding;
            mods->model_sections[index][domain]=models[domain].sections;
            mods->data[index][domain]=pending[domain].data;
            mods->normalized[index][domain]=pending[domain].normalized;
        }
        for (unsigned domain = 0; domain < 2; ++domain)
            if (manifest.targets & (domain ? PORTABLE_MOD_TARGET_3D : PORTABLE_MOD_TARGET_2D))
                composition_dirty[domain] = 0;
        mods->manifests[index]=manifest;mods->bytes[index]=bytes;mods->raw_views[index]=raw;mods->raw_views_2d[index]=projected_raw;
        mods->projected_sections[index]=projection;
        mods->scripts[index]=script;mods->script_sizes[index]=script_size;
        if(manifest.targets&PORTABLE_MOD_TARGET_2D){++count_2d;++mods->identity.count;}
        if(manifest.targets&PORTABLE_MOD_TARGET_3D){++count_3d;++mods->identity_3d.count;}
        ++mods->count;++mods->layer_count;total+=size+normalized_cost+projection_cost+model_cost+script_size;
        if(report!=NULL)report->entries[i].accepted=1u;
        printf("mod loaded: %s %s (%s)%s\n",manifest.id,manifest.version,manifest.name,from_zip?" from zip":"");
    }
    /* Reuse the last accepted composition, including its hashes. Rebuild only
     * untouched domains or scratch dirtied by a subsequently rejected layer.
     * Rejected composition scratch is never published. Both domain views borrow
     * only accepted payloads, and 3D references may resolve in later layers. */
    if((composition_dirty[0] && portable_asset_pack_compose(layers,count_2d,mods->sections,MOD_SECTION_LIMIT,&mods->pack)!=PORTABLE_ASSET_PACK_OK)||
       (composition_dirty[1] && portable_asset_pack_compose(layers_3d,count_3d,mods->sections_3d,MOD_SECTION_LIMIT,&mods->pack_3d)!=PORTABLE_ASSET_PACK_OK)){
        report_error(report,0u,"could not compose mod candidate");portable_mods_free(mods);return NULL;
    }
    char validation[PORTABLE_MOD_ERROR_CAPACITY];
    if(!portable_course_3d_validate_pack(&mods->pack_3d,validation,sizeof validation)){
        fprintf(stderr,"%s\n",validation);report_error(report,0u,validation);portable_mods_free(mods);return NULL;
    }
    mix_identity(&mods->pack,&mods->identity);
    mix_identity(&mods->pack_3d,&mods->identity_3d);
    return mods;
}

int portable_mods_select_assets(PortableModSet *mods,const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,
    const char sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY],char *error,size_t capacity)
{
    if(!mods||!sources||!base_2d||!base_3d)return 0;
    for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind)
        if(!memchr(sources[kind],0,PORTABLE_MOD_ID_CAPACITY)||
           (sources[kind][0]&&strcmp(sources[kind],"@base")&&!portable_mod_id_valid(sources[kind]))){
            if(error&&capacity)snprintf(error,capacity,"Invalid asset source ID");
            return 0;
        }
    if(!sources[0][0]&&!sources[1][0]&&!sources[2][0])return 1;
    const PortableAssetPackView *bases[]={base_2d,base_3d};
    for(unsigned domain=0;domain<2;++domain){
        const PortableAssetPackView *preferred[PORTABLE_ASSET_SOURCE_COUNT]={0};
        for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind){
            if(!sources[kind][0])continue;
            if(!strcmp(sources[kind],"@base")){
                if(kind==PORTABLE_ASSET_SOURCE_DRIVERS||portable_asset_source_available(bases[domain],kind))preferred[kind]=bases[domain];
                continue;
            }
            size_t i=0;while(i<mods->count && strcmp(sources[kind],mods->manifests[i].id))++i;
            if(i==mods->count){snprintf(error,capacity,"Asset source %s must be enabled",sources[kind]);return 0;}
            const PortableAssetPackView *raw=domain?&mods->raw_views[i]:&mods->raw_views_2d[i];
            if((mods->manifests[i].targets & (domain?PORTABLE_MOD_TARGET_3D:PORTABLE_MOD_TARGET_2D))&&
               portable_asset_source_available(raw,kind))preferred[kind]=raw;
        }
        if(!preferred[0]&&!preferred[1]&&!preferred[2])continue;
        PortableAssetPackView *pack=domain?&mods->pack_3d:&mods->pack;
        PortableAssetSectionView *rows=calloc(MOD_SECTION_LIMIT,sizeof *rows);
        PortableAssetPackView next;
        int ok=rows && portable_asset_sources_compose(pack,preferred,!strcmp(sources[0],"@base"),
            rows,MOD_SECTION_LIMIT,&mods->source_bindings[domain],&next,error,capacity);
        if(ok){
            PortableAssetSectionView *owned=domain?mods->sections_3d:mods->sections;
            const int changed=memcmp(pack->deterministic_digest,next.deterministic_digest,32u)!=0;
            memcpy(owned,rows,next.section_count*sizeof *rows);next.resolved_sections=owned;*pack=next;
            if(changed)mix_identity(pack,domain?&mods->identity_3d:&mods->identity);
        }
        free(rows);if(!ok)return 0;
    }
    return portable_course_3d_validate_pack(&mods->pack_3d,error,capacity);
}

PortableModSet *portable_mods_load_report(const PortableAssetPackView *base,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report)
{
    return portable_mods_load_domains_report(base,base,directory,ids,count,report);
}

PortableModSet *portable_mods_load(const PortableAssetPackView *base,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count)
{
    return portable_mods_load_report(base,directory,ids,count,NULL);
}

PortableModSet *portable_mods_load_roots_domains_report(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report)
{
    return load_sources(base_2d,base_3d,roots,ids,count,report,NULL,NULL);
}
PortableModSet *portable_mods_load_roots_domains_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report)
{
    PortableModLoadReport local;
    if(report==NULL)report=&local;
    PortableModSet *mods=portable_mods_load_roots_domains_report(base_2d,base_3d,roots,ids,count,report);
    if(mods!=NULL && portable_mods_loaded_count(mods)!=count){
        portable_mods_free(mods);
        return NULL;
    }
    return mods;
}
PortableModSet *portable_mods_load_strict(const PortableAssetPackView *base,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report)
{
    return portable_mods_load_domains_strict(base,base,directory,ids,count,report);
}
PortableModSet *portable_mods_load_roots_domains_archive_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    const char *archive_id,const PortableModArchive *archive,PortableModLoadReport *report)
{
    PortableModLoadReport local;
    if(report==NULL)report=&local;
    memset(report,0,sizeof *report);
    if(archive==NULL || archive_id==NULL || !portable_mod_id_valid(archive_id) ||
       count>PORTABLE_MOD_LIMIT || (count!=0u && ids==NULL)){
        report_error(report,0u,"invalid archive override arguments");return NULL;
    }
    if(selected(ids,count,archive_id)<0){
        report_error(report,0u,"archive override ID must be requested");return NULL;
    }
    PortableModSet *mods=load_sources(base_2d,base_3d,roots,ids,count,report,archive_id,archive);
    if(mods!=NULL && portable_mods_loaded_count(mods)!=count){
        portable_mods_free(mods);
        return NULL;
    }
    return mods;
}
PortableModSet *portable_mods_load_archive_strict(const PortableAssetPackView *base,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    const char *archive_id,const PortableModArchive *archive,PortableModLoadReport *report)
{
    return portable_mods_load_domains_archive_strict(base,base,directory,ids,count,archive_id,archive,report);
}
PortableModSet *portable_mods_load_domains_report(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report)
{
    const PortableModRoots roots={{directory},1u};
    return portable_mods_load_roots_domains_report(base_2d,base_3d,&roots,ids,count,report);
}
PortableModSet *portable_mods_load_domains_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report)
{
    const PortableModRoots roots={{directory},1u};
    return portable_mods_load_roots_domains_strict(base_2d,base_3d,&roots,ids,count,report);
}
PortableModSet *portable_mods_load_domains_archive_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    const char *archive_id,const PortableModArchive *archive,PortableModLoadReport *report)
{
    const PortableModRoots roots={{directory},1u};
    return portable_mods_load_roots_domains_archive_strict(base_2d,base_3d,&roots,ids,count,archive_id,archive,report);
}
const PortableAssetPackView *portable_mods_pack_3d(const PortableModSet *mods){return mods==NULL?NULL:&mods->pack_3d;}
const PortableModIdentity *portable_mods_identity_3d(const PortableModSet *mods){return mods==NULL?NULL:&mods->identity_3d;}
const PortableAssetPackView *portable_mods_pack(const PortableModSet *mods){return mods==NULL?NULL:&mods->pack;}
size_t portable_mods_loaded_count(const PortableModSet *mods){return mods==NULL?0u:mods->count;}
const char *portable_mods_section_3d_provider(const PortableModSet *mods,uint32_t kind,
    uint32_t content_id,size_t *order)
{
    for(size_t i=mods==NULL?0u:mods->count;i-->0u;){
        PortableAssetSectionView section;
        if(!(mods->manifests[i].targets&PORTABLE_MOD_TARGET_3D)||
           portable_asset_pack_find_section(&mods->raw_views[i],kind,content_id,
               &section)!=PORTABLE_ASSET_PACK_OK)continue;
        if(order)*order=i;
        return mods->manifests[i].name[0]?mods->manifests[i].name:mods->manifests[i].id;
    }
    return NULL;
}
const char *portable_mods_course_3d_provider(const PortableModSet *mods,uint32_t course_id,size_t *order)
{
    return portable_mods_section_3d_provider(mods,PORTABLE_COURSE_3D_KIND,course_id,order);
}
int portable_mods_provides_3d_kind(const PortableModSet *mods,size_t order,uint32_t kind)
{
    if(mods==NULL||order>=mods->count||!(mods->manifests[order].targets&PORTABLE_MOD_TARGET_3D))return 0;
    PortableAssetSectionView section;
    for(uint32_t i=0u;i<mods->raw_views[order].section_count;++i)
        if(portable_asset_pack_section_at(&mods->raw_views[order],i,&section)==PORTABLE_ASSET_PACK_OK&&
           section.kind==kind)return 1;
    return 0;
}
const PortableModIdentity *portable_mods_identity(const PortableModSet *mods){return mods==NULL?NULL:&mods->identity;}
int portable_mods_script(const PortableModSet *mods,size_t index,const char **id,
    const char **source,size_t *size)
{
    if(mods==NULL||index>=mods->count||mods->scripts[index]==NULL||
        !(mods->manifests[index].targets&PORTABLE_MOD_TARGET_2D))return 0;
    if(id!=NULL)*id=mods->manifests[index].id;
    if(source!=NULL)*source=mods->scripts[index];
    if(size!=NULL)*size=mods->script_sizes[index];
    return 1;
}
int portable_mods_script_3d(const PortableModSet *mods,size_t index,const char **id,
    const char **source,size_t *size)
{
    if(mods==NULL||index>=mods->count||mods->scripts[index]==NULL||
        !(mods->manifests[index].targets&PORTABLE_MOD_TARGET_3D))return 0;
    if(id!=NULL)*id=mods->manifests[index].id;
    if(source!=NULL)*source=mods->scripts[index];
    if(size!=NULL)*size=mods->script_sizes[index];
    return 1;
}
void portable_mods_free(PortableModSet *mods)
{
    if(mods==NULL)return;
    for(unsigned domain=0;domain<2;++domain)portable_model_bindings_overlay_close(&mods->source_bindings[domain]);
    for(size_t i=0u;i<mods->count;++i){
        for(unsigned domain=0u;domain<2u;++domain){
            portable_model_bindings_overlay_close(&mods->model_bindings[i][domain]);
            free(mods->model_sections[i][domain]);
            portable_game_data_free(mods->data[i][domain]);
            free(mods->normalized[i][domain]);
        }
        free(mods->projected_sections[i]);free(mods->bytes[i]);free(mods->scripts[i]);
    }
    free(mods);
}
