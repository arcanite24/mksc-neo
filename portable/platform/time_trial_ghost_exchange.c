#include "time_trial_ghost_exchange.h"

#include "content_registry.h"
#include "course_mirror.h"
#include "mod_identity.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *separator(const char *directory)
{
    const size_t size=strlen(directory);
    return size!=0u&&(directory[size-1u]=='/'||directory[size-1u]=='\\')?"":"/";
}

int portable_time_trial_ghost_received_path(char path[4096],const char *directory,unsigned slot)
{
    if(path==NULL||directory==NULL||directory[0]=='\0'||slot<1u||
       slot>PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS)return 0;
    const int size=snprintf(path,4096,"%s%sreceived-%u.ghost",directory,separator(directory),slot);
    return size>0&&size<4096;
}

/* Check the source RLE boundary as well as the file checksum. A nonzero first
 * input creates an empty leading run; only that first empty run is legal. */
static int valid_stream(const uint8_t *record)
{
    const uint8_t *payload=record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET;
    uint32_t ticks=0u;
    for(size_t offset=0u;offset+3u<=PORTABLE_TIME_TRIAL_GHOST_SOURCE_CAPACITY;offset+=3u){
        const uint8_t count=payload[offset+2u];
        if(count==0xffu)
            return payload[offset]==0u&&payload[offset+1u]==0u&&
                ticks>=portable_time_trial_ghost_record_time(record)&&ticks<=UINT16_MAX+1024u;
        if(count==0u&&(offset!=0u||payload[0]!=0u||payload[1]!=0u))return 0;
        ticks+=count;
    }
    return 0;
}

PortableGhostExchangeInfo portable_time_trial_ghost_exchange_inspect(
    const PortableAssetPackView *pack,const char *path,
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    PortableGhostExchangeInfo info={0};
    info.status=portable_time_trial_ghost_store_inspect(path,&info.identity,record);
    if(info.status!=PORTABLE_TIME_TRIAL_GHOST_STORE_OK)return info;
    if(!valid_stream(record)){info.status=PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;return info;}
    PortableCharacterContent character;
    PortableContentSelection selection;
    PortableRecoveredRaceView race;
    PortableRecoveredPackStorage *storage=malloc(sizeof *storage);
    if(storage==NULL){info.status=PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;return info;}
    if(portable_content_registry_saved_driver(pack,info.identity.character_class,
           portable_time_trial_ghost_record_roster_driver(record),&character)!=PORTABLE_CONTENT_OK||
       portable_content_registry_select(pack,info.identity.course_id,character.id,&selection)!=PORTABLE_CONTENT_OK||
       portable_course_open_race(pack,&selection,storage,&race,portable_mod_identity_mirror())!=PORTABLE_RECOVERED_PACK_OK)
        info.status=PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE;
    else info.status=portable_time_trial_ghost_store_load(path,&race.rules.compatibility,record);
    free(storage);
    if(info.status==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE){
        char reason[2200];
        info.mods_differ=(uint8_t)portable_time_trial_ghost_store_describe_refusal(path,reason,sizeof reason);
    }
    if(info.status==PORTABLE_TIME_TRIAL_GHOST_STORE_OK){
        if(!valid_stream(record))info.status=PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
        else{
            info.identity.character_class=portable_time_trial_ghost_record_driver(record);
            info.identity.roster_driver_id=portable_time_trial_ghost_record_roster_driver(record);
            info.ticks=portable_time_trial_ghost_record_time(record);
        }
    }
    return info;
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_receive(
    const PortableAssetPackView *pack,const char *incoming,const char *directory,unsigned slot)
{
    char destination[4096];
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],stored[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    if(!portable_time_trial_ghost_received_path(destination,directory,slot))
        return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    const PortableGhostExchangeInfo info=portable_time_trial_ghost_exchange_inspect(pack,incoming,record);
    if(info.mods_differ){
        char reason[2200];
        if(portable_time_trial_ghost_store_describe_refusal(incoming,reason,sizeof reason))
            fprintf(stderr,"ghost %s refused: %s\n",incoming,reason);
    }
    if(info.status!=PORTABLE_TIME_TRIAL_GHOST_STORE_OK)return info.status;
    return portable_time_trial_ghost_store_replace(destination,&info.identity,record,stored);
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_export(
    const PortableAssetPackView *pack,const char *directory,uint32_t course)
{
    char source[4096],destination[4096];
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],stored[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    if(directory==NULL||directory[0]=='\0'||course==0u)return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    int size=snprintf(source,sizeof source,"%s%scourse-%u-solo%s.ghost",directory,separator(directory),course,
        portable_mod_identity_progress_suffix());
    if(size<0||size>=(int)sizeof source)return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    const PortableGhostExchangeInfo info=portable_time_trial_ghost_exchange_inspect(pack,source,record);
    if(info.status!=PORTABLE_TIME_TRIAL_GHOST_STORE_OK)return info.status;
    size=snprintf(destination,sizeof destination,"%s%sshared-course-%u%s-%u.ghost",
        directory,separator(directory),course,portable_mod_identity_progress_suffix(),info.ticks);
    if(size<0||size>=(int)sizeof destination)return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    return portable_time_trial_ghost_store_replace(destination,&info.identity,record,stored);
}

int portable_time_trial_ghost_received_erase(const char *directory,unsigned slot)
{
    char path[4096];
    return portable_time_trial_ghost_received_path(path,directory,slot)&&
        (remove(path)==0||errno==ENOENT);
}
