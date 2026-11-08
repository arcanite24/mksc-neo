#include "file_replace.h"
#include "gp_progress_store.h"
#include "mod_identity.h"
#include "track_set.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Version 1 is exactly 80 bytes. Version 2, written while mods are enabled,
 * appends the mod identity text; its checksum covers payload and text. */
enum { progress_file_size=80,progress_payload_offset=16,
       progress_max_file_size=progress_file_size+PORTABLE_MOD_IDENTITY_TEXT_CAPACITY };

static const uint8_t progress_magic[8]={'M','K','S','C','G','P','0','1'};

static void write_u16(uint8_t *data,uint16_t value)
{
    data[0]=(uint8_t)value;data[1]=(uint8_t)(value>>8);
}

static void write_u32(uint8_t *data,uint32_t value)
{
    data[0]=(uint8_t)value;data[1]=(uint8_t)(value>>8);
    data[2]=(uint8_t)(value>>16);data[3]=(uint8_t)(value>>24);
}

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(data[0]|((uint16_t)data[1]<<8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0]|((uint32_t)data[1]<<8)|
        ((uint32_t)data[2]<<16)|((uint32_t)data[3]<<24);
}

static uint32_t checksum(const uint8_t *data,size_t size)
{
    uint32_t hash=UINT32_C(2166136261);
    for(size_t index=0u;index<size;++index)
        hash=(hash^data[index])*UINT32_C(16777619);
    return hash;
}

void portable_gp_progress_init(PortableGpProgress *progress)
{
    if(progress==NULL)return;
    memset(progress->grade,0xff,sizeof progress->grade);
    memset(progress->rating,0xff,sizeof progress->rating);
    progress->unlock_flags=0u;
}

uint8_t portable_gp_progress_title_variant(const PortableGpProgress *progress)
{
    int night=1,sunset=1;
    if(progress==NULL)return 0u;
    for(unsigned cup=0u;cup<PORTABLE_GP_PROGRESS_CUP_COUNT;++cup)
        for(unsigned speed=0u;speed<PORTABLE_GP_PROGRESS_SPEED_COUNT;++speed){
            night&=progress->rating[0][cup][speed]==0u;
            sunset&=progress->grade[0][cup][speed]==0;
        }
    return night?2u:sunset?1u:0u;
}

uint8_t portable_gp_progress_cup_access(const PortableGpProgress *progress,uint8_t speed_class)
{
    if(progress==NULL||speed_class>=PORTABLE_GP_PROGRESS_SPEED_COUNT)return 0u;
    uint8_t mask=UINT8_C(0x0f);
    for(unsigned cup=0u;cup<PORTABLE_GP_PROGRESS_CUP_COUNT-1u;++cup)
        if(progress->grade[0][cup][speed_class]!=0)return mask;
    return UINT8_C(0x1f);
}

uint8_t portable_gp_progress_extra_cup_access(const PortableGpProgress *progress,uint8_t speed_class)
{
    if(portable_gp_progress_cup_access(progress,speed_class)!=UINT8_C(0x1f))return 0u;
    uint8_t mask=0u;
    for(unsigned cup=0u;cup<PORTABLE_GP_PROGRESS_CUP_COUNT;++cup)
        if(progress->unlock_flags&(1u<<(cup*3u+speed_class)))mask=(uint8_t)(mask|(1u<<cup));
    return mask;
}

PortableGpProgressStatus portable_gp_progress_apply_ceremony(
    PortableGpProgress *progress,const PortableGpCeremonySession *ceremony,
    uint8_t retro_tracks,uint8_t speed_class,
    PortableGpProgressUpdate *update)
{
    PortableGpProgressUpdate next={0};
    if(progress==NULL||ceremony==NULL||update==NULL||ceremony->performance_rating>7u||
       retro_tracks>=PORTABLE_GP_PROGRESS_TRACK_SET_COUNT||
       speed_class>=PORTABLE_GP_PROGRESS_SPEED_COUNT||
       ceremony->cup_index>=PORTABLE_GP_PROGRESS_CUP_COUNT||
       ceremony->player_standing_rank>=8u||ceremony->finishing_coin_total>396u)
        return PORTABLE_GP_PROGRESS_INVALID_ARGUMENT;
    /* 28B90 tests prior golds and >99 finishing coins before writing this medal.
     * Native flags use positive earned bits; the source stores inverted locks. */
    if(ceremony->finishing_coin_total>99u){
        int all_gold=1;
        for(unsigned cup=0u;cup<PORTABLE_GP_PROGRESS_CUP_COUNT;++cup)
            if(progress->grade[0][cup][speed_class]!=0)all_gold=0;
        const uint16_t bit=(uint16_t)(1u<<(ceremony->cup_index*3u+speed_class));
        if(all_gold&&!(progress->unlock_flags&bit)){
            progress->unlock_flags|=bit;next.unlock_changed=1u;
        }
    }
    /* 28B90 skips the medal write above third overall, then independently
     * improves the rating at save46/55 for every completed cup. */
    int8_t *medal=&progress->grade[retro_tracks][ceremony->cup_index][speed_class];
    if(ceremony->player_standing_rank<=2u&&
       (*medal<0||*medal>(int8_t)ceremony->player_standing_rank)){
        *medal=(int8_t)ceremony->player_standing_rank;next.grade_changed=1u;
    }
    uint8_t *rating=&progress->rating[retro_tracks][ceremony->cup_index][speed_class];
    if(*rating>ceremony->performance_rating){
        *rating=ceremony->performance_rating;next.rating_changed=1u;
    }
    next.file_changed=(uint8_t)(next.grade_changed||next.rating_changed||next.unlock_changed);
    *update=next;
    return PORTABLE_GP_PROGRESS_OK;
}

static size_t encode(const PortableGpProgress *progress,const PortableModIdentity *mods,
    uint8_t data[progress_max_file_size])
{
    size_t offset=progress_payload_offset;
    memset(data,0,progress_max_file_size);
    const size_t trailer=mods==NULL||mods->count==0u?0u:
        portable_mod_identity_format(mods,(char *)data+progress_file_size,PORTABLE_MOD_IDENTITY_TEXT_CAPACITY);
    const size_t size=progress_file_size+trailer;
    memcpy(data,progress_magic,sizeof progress_magic);
    write_u16(data+8u,trailer!=0u?2u:1u);write_u16(data+10u,progress_file_size);
    memcpy(data+offset,progress->grade,sizeof progress->grade);
    offset+=sizeof progress->grade;
    memcpy(data+offset,progress->rating,sizeof progress->rating);
    offset+=sizeof progress->rating;
    write_u16(data+offset,progress->unlock_flags);
    write_u32(data+12u,checksum(data+progress_payload_offset,size-progress_payload_offset));
    return size;
}

static PortableGpProgressStatus decode(
    const uint8_t *data,size_t size,PortableGpProgress *progress,PortableModIdentity *mods)
{
    PortableGpProgress next;
    PortableModIdentity found={PORTABLE_MOD_API_VERSION,0u,{{{0},{0},{0}}}};
    size_t offset=progress_payload_offset;
    const uint16_t version=size>=progress_file_size?read_u16(data+8u):0u;
    if(size<progress_file_size||memcmp(data,progress_magic,sizeof progress_magic)!=0||
       (version==1u&&size!=progress_file_size)||(version!=1u&&version!=2u)||
       read_u16(data+10u)!=progress_file_size||
       read_u32(data+12u)!=checksum(data+progress_payload_offset,size-progress_payload_offset)||
       (version==2u&&(!portable_mod_identity_parse((const char *)data+progress_file_size,
            size-progress_file_size,&found)||found.count==0u)))
        return PORTABLE_GP_PROGRESS_BAD_FORMAT;
    memcpy(next.grade,data+offset,sizeof next.grade);offset+=sizeof next.grade;
    memcpy(next.rating,data+offset,sizeof next.rating);offset+=sizeof next.rating;
    next.unlock_flags=read_u16(data+offset);
    for(size_t set=0u;set<PORTABLE_GP_PROGRESS_TRACK_SET_COUNT;++set)
        for(size_t cup=0u;cup<PORTABLE_GP_PROGRESS_CUP_COUNT;++cup)
            for(size_t speed=0u;speed<PORTABLE_GP_PROGRESS_SPEED_COUNT;++speed)
                if((next.grade[set][cup][speed]<-1||
                    next.grade[set][cup][speed]>2)||
                   (next.rating[set][cup][speed]!=UINT8_C(0xff)&&
                    next.rating[set][cup][speed]>7u))
                    return PORTABLE_GP_PROGRESS_BAD_FORMAT;
    *progress=next;
    if(mods!=NULL)*mods=found;
    return PORTABLE_GP_PROGRESS_OK;
}

PortableGpProgressStatus portable_gp_progress_load_with_mods(
    const char *path,PortableGpProgress *progress,PortableModIdentity *mods)
{
    uint8_t data[progress_max_file_size];
    FILE *file;
    int extra;
    if(path==NULL||path[0]=='\0'||progress==NULL)
        return PORTABLE_GP_PROGRESS_INVALID_ARGUMENT;
    file=fopen(path,"rb");
    if(file==NULL)return errno==ENOENT?PORTABLE_GP_PROGRESS_NOT_FOUND:
        PORTABLE_GP_PROGRESS_IO_ERROR;
    const size_t size=fread(data,1u,sizeof data,file);
    extra=fgetc(file);
    if(fclose(file)!=0)return PORTABLE_GP_PROGRESS_IO_ERROR;
    if(extra!=EOF)return PORTABLE_GP_PROGRESS_BAD_FORMAT;
    return decode(data,size,progress,mods);
}

PortableGpProgressStatus portable_gp_progress_load(
    const char *path,PortableGpProgress *progress)
{
    return portable_gp_progress_load_with_mods(path,progress,NULL);
}

PortableGpProgressStatus portable_gp_progress_save(
    const char *path,const PortableGpProgress *progress)
{
    uint8_t data[progress_max_file_size];
    char temporary[4096];
    FILE *file;
    int length;
    if(path==NULL||path[0]=='\0'||progress==NULL)
        return PORTABLE_GP_PROGRESS_INVALID_ARGUMENT;
    length=snprintf(temporary,sizeof temporary,"%s.tmp",path);
    if(length<=0||(size_t)length>=sizeof temporary)
        return PORTABLE_GP_PROGRESS_INVALID_ARGUMENT;
    const size_t size=encode(progress,portable_mod_identity_current(),data);
    file=fopen(temporary,"wb");
    if(file==NULL)return PORTABLE_GP_PROGRESS_IO_ERROR;
    {
        int failed=fwrite(data,1u,size,file)!=size;
        if(fclose(file)!=0)failed=1;
        if(failed){
        remove(temporary);return PORTABLE_GP_PROGRESS_IO_ERROR;
        }
    }
    if(!portable_file_replace(temporary,path)){remove(temporary);
        return PORTABLE_GP_PROGRESS_IO_ERROR;}
    return PORTABLE_GP_PROGRESS_OK;
}

const char *portable_gp_progress_status_name(PortableGpProgressStatus status)
{
    static const char *const names[]={"ok","not found","invalid argument",
        "bad format","I/O error"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:
        "unknown status";
}

PortableGpProgressStatus portable_gp_progress_record_ceremony(
    const char *path, PortableGpCeremonySession *ceremony)
{
    PortableGpProgress progress;
    PortableGpProgressUpdate update;
    if(ceremony==NULL)return PORTABLE_GP_PROGRESS_INVALID_ARGUMENT;
    /* Custom cups are always unlocked and never earn medals or unlocks. */
    if(ceremony->track_set==PORTABLE_TRACK_SET_CUSTOM){
        ceremony->extra_cup_unlocked=0u;
        ceremony->special_cup_unlocked=0u;
        ceremony->save_evaluation_pending=0u;
        ceremony->unlock_evaluation_pending=0u;
        return PORTABLE_GP_PROGRESS_OK;
    }
    PortableGpProgressStatus status=portable_gp_progress_load(path,&progress);
    if(status==PORTABLE_GP_PROGRESS_NOT_FOUND)portable_gp_progress_init(&progress);
    else if(status!=PORTABLE_GP_PROGRESS_OK)return status;
    const uint8_t prior_access=portable_gp_progress_cup_access(&progress,ceremony->engine_class);
    status=portable_gp_progress_apply_ceremony(&progress,ceremony,ceremony->track_set,
        ceremony->engine_class,&update);
    if(status!=PORTABLE_GP_PROGRESS_OK)return status;
    if(update.file_changed){
        status=portable_gp_progress_save(path,&progress);
        if(status!=PORTABLE_GP_PROGRESS_OK)return status;
    }
    ceremony->extra_cup_unlocked=update.unlock_changed;
    ceremony->special_cup_unlocked=(uint8_t)(prior_access!=UINT8_C(0x1f)&&
        portable_gp_progress_cup_access(&progress,ceremony->engine_class)==UINT8_C(0x1f));
    ceremony->save_evaluation_pending=0u;
    ceremony->unlock_evaluation_pending=0u;
    return PORTABLE_GP_PROGRESS_OK;
}
