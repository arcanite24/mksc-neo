#include "time_trial_ghost_library.h"

#include "mod_identity.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static uint32_t course_from_name(const char *name)
{
    const char prefix[]="course-";
    char suffix[PORTABLE_RECORD_SUFFIX_CAPACITY+16u];
    snprintf(suffix,sizeof suffix,"-solo%s.ghost",portable_mod_identity_record_suffix());
    if(strncmp(name,prefix,sizeof prefix-1u)!=0)return 0u;
    const char *digits=name+sizeof prefix-1u;
    if(*digits<'1'||*digits>'9')return 0u;
    uint32_t id=0u;
    while(*digits>='0'&&*digits<='9'){
        const unsigned digit=(unsigned)(*digits++-'0');
        if(id>(UINT32_MAX-digit)/10u)return 0u;
        id=id*10u+digit;
    }
    return strcmp(digits,suffix)==0?id:0u;
}

static int joined_path(char path[4096],const char *directory,const char *name)
{
    if(directory==NULL||directory[0]=='\0')return 0;
    const size_t length=strlen(directory);
    const char last=directory[length-1u];
    const int size=snprintf(path,4096,"%s%s%s",directory,
        last=='/'||last=='\\'?"":"/",name);
    return size>0&&size<4096;
}

static void inventory_add(PortableGhostLibraryInventory *inventory,uint32_t id)
{
    ++inventory->used;
    for(unsigned slot=0u;slot<PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS;++slot){
        if(inventory->courses[slot]==0u||inventory->courses[slot]>id){
            const uint32_t displaced=inventory->courses[slot];
            inventory->courses[slot]=id;id=displaced;
            if(id==0u)break;
        }
    }
}

PortableGhostLibraryStatus portable_time_trial_ghost_library_query(
    const char *directory,uint32_t course_id,PortableGhostLibraryInventory *out)
{
    if(out==NULL)return PORTABLE_GHOST_LIBRARY_IO_ERROR;
    *out=(PortableGhostLibraryInventory){0};
    if(directory==NULL||directory[0]=='\0'||course_id==0u)
        return PORTABLE_GHOST_LIBRARY_IO_ERROR;
    PortableGhostLibraryInventory inventory={0};
    int reuse=0;
#ifdef _WIN32
    char pattern[4096];
    WIN32_FIND_DATAA entry;
    if(!joined_path(pattern,directory,"*"))return PORTABLE_GHOST_LIBRARY_IO_ERROR;
    HANDLE handle=FindFirstFileA(pattern,&entry);
    if(handle==INVALID_HANDLE_VALUE){
        const DWORD error=GetLastError();
        const DWORD attributes=GetFileAttributesA(directory);
        if(error==ERROR_FILE_NOT_FOUND&&attributes!=INVALID_FILE_ATTRIBUTES&&
           (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0u)return PORTABLE_GHOST_LIBRARY_AVAILABLE;
        return PORTABLE_GHOST_LIBRARY_IO_ERROR;
    }
    do{
        const uint32_t id=course_from_name(entry.cFileName);
        if(id!=0u){inventory_add(&inventory,id);reuse|=id==course_id;}
    }while(FindNextFileA(handle,&entry));
    const DWORD error=GetLastError();
    const int closed=FindClose(handle)!=0;
    if(error!=ERROR_NO_MORE_FILES||!closed)return PORTABLE_GHOST_LIBRARY_IO_ERROR;
#else
    DIR *handle=opendir(directory);
    if(handle==NULL)return PORTABLE_GHOST_LIBRARY_IO_ERROR;
    struct dirent *entry;
    errno=0;
    while((entry=readdir(handle))!=NULL){
        const uint32_t id=course_from_name(entry->d_name);
        if(id!=0u){inventory_add(&inventory,id);reuse|=id==course_id;}
    }
    const int error=errno;
    const int closed=closedir(handle)==0;
    if(error!=0||!closed)return PORTABLE_GHOST_LIBRARY_IO_ERROR;
#endif
    *out=inventory;
    if(reuse)return PORTABLE_GHOST_LIBRARY_REUSE;
    return inventory.used<PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS?
        PORTABLE_GHOST_LIBRARY_AVAILABLE:PORTABLE_GHOST_LIBRARY_FULL;
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_library_save(
    const char *directory,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    if(identity==NULL)return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    PortableGhostLibraryInventory inventory;
    const PortableGhostLibraryStatus status=portable_time_trial_ghost_library_query(
        directory,identity->course_id,&inventory);
    if(status!=PORTABLE_GHOST_LIBRARY_AVAILABLE&&status!=PORTABLE_GHOST_LIBRARY_REUSE)
        return PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
    char name[64],path[4096];
    const int size=snprintf(name,sizeof name,"course-%u-solo%s.ghost",identity->course_id,
        portable_mod_identity_record_suffix());
    if(size<=0||size>=(int)sizeof name||!joined_path(path,directory,name))
        return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    return portable_time_trial_ghost_store_save(path,identity,candidate,out_record);
}
