#include "mod_install.h"
#include "mod_archive.h"
#include "script_host.h"
#include "file_replace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct PortableModInstall {
    PortableModArchive *archive;
    PortableModManifest manifest;
    char destination[4096],folder[4096];
    PortableModRoots roots;
    char *directories[PORTABLE_MOD_ROOT_LIMIT];
    uint8_t *previous;
    size_t previous_size;
    int update,published;
};

/* Refuse directories and unreadable paths, rather than treating them as absent. */
static int path_kind(const char *path)
{
#ifdef _WIN32
    struct _stat64 info;
    if(_stat64(path,&info)==0)return (info.st_mode&_S_IFREG)?1:2;
#else
    struct stat info;
    if(stat(path,&info)==0)return S_ISREG(info.st_mode)?1:2;
#endif
    return errno==ENOENT?0:-1;
}

static int fail(char *error,const char *message)
{
    if(error!=NULL)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s",message);
    return 0;
}

static uint8_t *read_previous(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb");
    if(file==NULL)return NULL;
    long length=-1;
    if(fseek(file,0,SEEK_END)==0)length=ftell(file);
    uint8_t *bytes=NULL;
    if(length>=0 && (unsigned long)length<=PORTABLE_MOD_ARCHIVE_MAX_BYTES && fseek(file,0,SEEK_SET)==0){
        bytes=malloc(length?(size_t)length:1u);
        if(bytes!=NULL && (fread(bytes,1u,(size_t)length,file)!=(size_t)length || ferror(file))){
            free(bytes);bytes=NULL;
        }
    }
    if(fclose(file)!=0){free(bytes);bytes=NULL;}
    if(bytes!=NULL)*size=(size_t)length;
    return bytes;
}

void portable_mod_install_free(PortableModInstall *install)
{
    if(install==NULL)return;
    portable_mod_archive_free(install->archive);
    free(install->previous);
    for(size_t i=0;i<install->roots.count;++i)free(install->directories[i]);
    free(install);
}

int portable_mod_install_validate_roots_domains_with(const PortableModInstall *install,const PortableAssetPackView *base,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    PortableModInstallValidator validator,void *context,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    char local_error[PORTABLE_MOD_ERROR_CAPACITY];
    if(error==NULL)error=local_error;
    error[0]='\0';
    if(install==NULL || base==NULL || base_3d==NULL || !portable_mod_roots_valid(roots) || count>PORTABLE_MOD_LIMIT || (count && ids==NULL))
        return fail(error,"Invalid archive validation request");
    char selected[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    memset(selected,0,sizeof selected);
    size_t selected_count=count;int included=0;
    for(size_t i=0u;i<count;++i){
        if(memchr(ids[i],'\0',PORTABLE_MOD_ID_CAPACITY)==NULL || !portable_mod_id_valid(ids[i])){
            fail(error,"Invalid staged mod ID");return 0;
        }
        memcpy(selected[i],ids[i],sizeof selected[i]);
        included|=strcmp(ids[i],install->manifest.id)==0;
    }
    if(!included){
        if(count==PORTABLE_MOD_LIMIT){fail(error,"Remove a staged mod before validating this archive (32 mod limit)");return 0;}
        snprintf(selected[selected_count++],PORTABLE_MOD_ID_CAPACITY,"%s",install->manifest.id);
    }
    PortableModLoadReport *report=calloc(1u,sizeof *report);
    if(report==NULL){fail(error,"Cannot allocate archive validation report");return 0;}
    PortableModSet *mods=portable_mods_load_roots_domains_archive_strict(base,base_3d,roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])selected,selected_count,install->manifest.id,install->archive,report);
    if(mods==NULL){fail(error,report->first_error);free(report);return 0;}
    free(report);
    int valid=1;
    for(unsigned domain=0u;domain<2u&&valid;++domain){
        PortableScriptSource sources[PORTABLE_MOD_LIMIT];size_t scripts_count=0u;
        for(size_t i=0u;i<selected_count;++i){
            PortableScriptSource *source=&sources[scripts_count];
            const int present=domain?
                portable_mods_script_3d(mods,i,&source->id,&source->source,&source->size):
                portable_mods_script(mods,i,&source->id,&source->source,&source->size);
            if(present)++scripts_count;
        }
        PortableScriptHost *scripts=scripts_count?portable_script_host_create(sources,scripts_count):NULL;
        valid=scripts_count==0u || (scripts!=NULL && portable_script_host_active_count(scripts)==scripts_count);
        if(!valid){
            const char *id=scripts?portable_script_host_failed_id(scripts):NULL;
            snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s: %s Lua validation failed; archive was not installed",
                id?id:install->manifest.id,domain?"3D":"2D");
        }
        portable_script_host_free(scripts);
    }
    if(valid && validator!=NULL)valid=validator(portable_mods_pack(mods),context,error);
    portable_mods_free(mods);
    return valid;
}

int portable_mod_install_validate_with(const PortableModInstall *install,const PortableAssetPackView *base,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    PortableModInstallValidator validator,void *context,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    return portable_mod_install_validate_domains_with(install,base,base,directory,ids,count,validator,context,error);
}
int portable_mod_install_validate_domains(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    return portable_mod_install_validate_domains_with(install,base_2d,base_3d,directory,ids,count,NULL,NULL,error);
}
int portable_mod_install_validate(const PortableModInstall *install,const PortableAssetPackView *base,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    return portable_mod_install_validate_with(install,base,directory,ids,count,NULL,NULL,error);
}

static int catalog_available(const PortableModRoots *roots,const char *id,char *error)
{
    PortableModCatalog *catalog=calloc(1u,sizeof *catalog);
    if(catalog==NULL)return fail(error,"Cannot allocate installed mod catalog");
    const int discovered=portable_mods_discover_roots(roots,catalog);
    int listed=0,conflict=0;
    for(size_t i=0u;i<catalog->count;++i){
        const PortableModCatalogEntry *entry=&catalog->entries[i];
        if(strcmp(entry->id,id)!=0)continue;
        listed=1;
        if(strstr(entry->error,"duplicate ID")!=NULL){fail(error,entry->error);conflict=1;}
        else if(entry->source_root!=0u){
            if(error!=NULL) {
                snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%.63s: installation conflicts with root %u",
                    id,(unsigned)entry->source_root);
            }
            conflict=1;
        }
    }
    const int full=catalog->count==PORTABLE_MOD_CATALOG_LIMIT && !listed;
    if(!discovered || full)
        fail(error,full?"Installed mod catalog is full (128 entries)":catalog->error);
    free(catalog);
    return discovered && !full && !conflict;
}

PortableModInstall *portable_mod_install_prepare_roots_domains(const PortableAssetPackView *base,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,const char *path,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    char local_error[PORTABLE_MOD_ERROR_CAPACITY];
    if(error==NULL)error=local_error;
    error[0]='\0';
    if(base==NULL || base_3d==NULL || !portable_mod_roots_valid(roots) || path==NULL || count>PORTABLE_MOD_LIMIT || (count && ids==NULL)){
        fail(error,"Invalid archive installation request");return NULL;
    }
    const char *directory=roots->directories[0];
    PortableModInstall *install=calloc(1u,sizeof *install);
    if(install==NULL){fail(error,"Cannot allocate archive review");return NULL;}
    install->roots.count=roots->count;
    for(size_t i=0;i<roots->count;++i){
        const size_t length=strlen(roots->directories[i]);
        install->directories[i]=malloc(length+1u);
        if(!install->directories[i]){fail(error,"Cannot preserve archive source roots");goto failed;}
        memcpy(install->directories[i],roots->directories[i],length+1u);
        install->roots.directories[i]=install->directories[i];
    }
    char archive_error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
    install->archive=portable_mod_archive_open(path,NULL,archive_error);
    if(install->archive==NULL || !portable_mod_archive_validate(install->archive,archive_error)){
        fail(error,archive_error);goto failed;
    }
    size_t manifest_size=0u;
    uint8_t *json=portable_mod_archive_read(install->archive,"mod.json",16384u,&manifest_size,archive_error);
    if(json==NULL){fail(error,archive_error);goto failed;}
    const int parsed=portable_mod_manifest_parse((const char *)json,manifest_size,&install->manifest,error);
    free(json);
    if(!parsed)goto failed;
    const char *root=portable_mod_archive_root(install->archive);
    if(root[0]!='\0' && strcmp(root,install->manifest.id)!=0){
        fail(error,"Containing folder must match the manifest ID");goto failed;
    }
    int length=snprintf(install->destination,sizeof install->destination,"%s/%s.zip",directory,install->manifest.id);
    int folder_length=snprintf(install->folder,sizeof install->folder,"%s/%s",directory,install->manifest.id);
    if(length<=0 || (size_t)length>=sizeof install->destination || folder_length<=0 ||
        (size_t)folder_length>=sizeof install->folder){fail(error,"Installation path is too long");goto failed;}
    if(path_kind(install->folder)!=0){fail(error,"A mod folder already uses this ID; archive installation cannot replace it");goto failed;}
    if(!catalog_available(roots,install->manifest.id,error))goto failed;
    const int kind=path_kind(install->destination);
    if(kind<0 || kind==2){fail(error,"Cannot inspect the installed archive");goto failed;}
    if(kind==1){
        install->update=1;
        install->previous=read_previous(install->destination,&install->previous_size);
        if(install->previous==NULL){fail(error,"Cannot preserve the previous archive for review");goto failed;}
    }
    if(!portable_mod_install_validate_roots_domains(install,base,base_3d,roots,ids,count,error))goto failed;
    return install;
failed:
    portable_mod_install_free(install);return NULL;
}

int portable_mod_install_validate_roots_domains(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const PortableModRoots *roots,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    return portable_mod_install_validate_roots_domains_with(install,base_2d,base_3d,roots,ids,count,NULL,NULL,error);
}
int portable_mod_install_validate_domains_with(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    PortableModInstallValidator validator,void *context,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    const PortableModRoots roots={{directory},1u};
    return portable_mod_install_validate_roots_domains_with(install,base_2d,base_3d,&roots,ids,count,validator,context,error);
}
PortableModInstall *portable_mod_install_prepare_domains(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,const char *path,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    const PortableModRoots roots={{directory},1u};
    return portable_mod_install_prepare_roots_domains(base_2d,base_3d,&roots,path,ids,count,error);
}

PortableModInstall *portable_mod_install_prepare(const PortableAssetPackView *base,
    const char *directory,const char *path,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    return portable_mod_install_prepare_domains(base,base,directory,path,ids,count,error);
}

const PortableModManifest *portable_mod_install_manifest(const PortableModInstall *install)
{return install?&install->manifest:NULL;}
int portable_mod_install_is_update(const PortableModInstall *install)
{return install!=NULL && install->update;}

static int destination_unchanged(const PortableModInstall *install)
{
    if(path_kind(install->folder)!=0)return 0;
    const int kind=path_kind(install->destination);
    if(!install->update)return kind==0;
    if(kind!=1)return 0;
    FILE *file=fopen(install->destination,"rb");
    if(file==NULL)return 0;
    uint8_t buffer[4096];size_t offset=0u;int same=1;
    while(offset<install->previous_size){
        size_t chunk=install->previous_size-offset;
        if(chunk>sizeof buffer)chunk=sizeof buffer;
        if(fread(buffer,1u,chunk,file)!=chunk || memcmp(buffer,install->previous+offset,chunk)!=0){same=0;break;}
        offset+=chunk;
    }
    if(same && (fgetc(file)!=EOF || ferror(file)))same=0;
    if(fclose(file)!=0)same=0;
    return same;
}

int portable_mod_install_publish(PortableModInstall *install,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    char local_error[PORTABLE_MOD_ERROR_CAPACITY];
    if(error==NULL)error=local_error;
    error[0]='\0';
    if(install==NULL || install->published)return fail(error,"No pending archive installation");
    if(!destination_unchanged(install))return fail(error,"Installed files changed during review; choose the archive again");
    if(!catalog_available(&install->roots,install->manifest.id,error))return 0;
    char temporary[4160];FILE *file=NULL;
    for(unsigned attempt=0u;attempt<32u;++attempt){
        const int length=snprintf(temporary,sizeof temporary,"%s.install-%u.tmp",install->destination,attempt);
        if(length<=0 || (size_t)length>=sizeof temporary)return fail(error,"Installation path is too long");
        file=fopen(temporary,"wbx");
        if(file!=NULL)break;
        if(errno!=EEXIST)return fail(error,"Cannot create an installation temporary file; previous archive remains intact");
    }
    if(file==NULL)return fail(error,"Installation temporary names are occupied; previous archive remains intact");
    size_t size=0u;const uint8_t *bytes=portable_mod_archive_bytes(install->archive,&size);
    int written=bytes!=NULL && fwrite(bytes,1u,size,file)==size;
    if(fclose(file)!=0)written=0;
    if(!written || !destination_unchanged(install) ||
        !catalog_available(&install->roots,install->manifest.id,error) || !portable_file_replace(temporary,install->destination)){
        remove(temporary);
        return fail(error,"Cannot publish the archive; previous installation remains intact");
    }
    install->published=1;
    return 1;
}
