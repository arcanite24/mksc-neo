#include "file_replace.h"
#include "personal_best.h"
#include "mod_identity.h"
#include "text_number.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_digit(int value)
{
    if(value>='0'&&value<='9')return value-'0';
    value=tolower((unsigned char)value);
    return value>='a'&&value<='f'?value-'a'+10:-1;
}

static int digest_parse(const char *text,uint8_t out[32])
{
    if(strlen(text)!=64u)return 0;
    for(size_t i=0;i<32u;++i){
        int high=hex_digit(text[i*2u]),low=hex_digit(text[i*2u+1u]);
        if(high<0||low<0)return 0;
        out[i]=(uint8_t)((high<<4)|low);
    }
    return 1;
}

static int compatible(const PortableRaceCompatibilityId *a,
                      const PortableRaceCompatibilityId *b)
{
    return a->course_id==b->course_id&&
        a->course_revision==b->course_revision&&
        a->rules_id==b->rules_id&&a->rules_revision==b->rules_revision&&
        a->handling_profile_id==b->handling_profile_id&&
        a->handling_revision==b->handling_revision&&
        a->state_schema_version==b->state_schema_version&&
        memcmp(a->deterministic_digest,b->deterministic_digest,
               sizeof a->deterministic_digest)==0;
}

static int read_u32(const char **cursor, const char *end, uint32_t *out)
{
    uint64_t value;
    if (!portable_text_u64(cursor, end, 10u, UINT32_MAX, &value))
        return 0;
    *out = (uint32_t)value;
    return 1;
}

static int only_whitespace(const char *cursor, const char *end)
{
    while (cursor != end && isspace((unsigned char)*cursor))
        ++cursor;
    return cursor == end;
}

static int read_digest(const char *cursor, const char *end, char digest[65])
{
    while (cursor != end && isspace((unsigned char)*cursor))
        ++cursor;
    const char *start = cursor;
    while (cursor != end && !isspace((unsigned char)*cursor))
        ++cursor;
    if ((size_t)(cursor - start) != 64u || !only_whitespace(cursor, end))
        return 0;
    memcpy(digest, start, 64u);
    digest[64] = '\0';
    return 1;
}

static PortablePersonalBestStatus read_table(FILE *file,
    const PortableRaceCompatibilityId *expected,PortablePersonalBestTable *table)
{
    char line[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY+2u],digest[65];
    PortableModIdentity mods={PORTABLE_MOD_API_VERSION,0u,{{{0},{0},{0}}}};
    uint32_t version;
    PortableRaceCompatibilityId found={0};
    uint32_t legacy_ticks;
    if(fgets(line,sizeof line,file)==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    const char *cursor = line, *end = line + strlen(line);
    if ((size_t)(end - cursor) < 7u || memcmp(cursor, "MKSC-PB", 7u) != 0)
        return PORTABLE_PERSONAL_BEST_INVALID;
    cursor += 7u;
    if (!read_u32(&cursor, end, &version))
        return PORTABLE_PERSONAL_BEST_INVALID;
    if(version==1u||version==2u)return PORTABLE_PERSONAL_BEST_INCOMPATIBLE;
    if(version==3u){
        uint32_t *fields[] = {&legacy_ticks, &found.course_id, &found.course_revision,
            &found.rules_id, &found.rules_revision, &found.handling_profile_id,
            &found.handling_revision, &found.state_schema_version, &found.character_class};
        for (size_t i = 0u; i < sizeof fields / sizeof fields[0]; ++i)
            if (!read_u32(&cursor, end, fields[i]))
                return PORTABLE_PERSONAL_BEST_INVALID;
        if (!read_digest(cursor, end, digest) || legacy_ticks == 0u || found.character_class >= 8u)
            return PORTABLE_PERSONAL_BEST_INVALID;
        table->races[0]=(PortablePersonalBestRecord){legacy_ticks,found.character_class,0u};
    }else if(version>=4u&&version<=6u){
        /* v5 adds a roster driver column; v4 tables hold stock drivers only.
         * v6 (written with mods enabled) is v5 plus a final mod identity line. */
        uint32_t *fields[] = {&found.course_id, &found.course_revision, &found.rules_id,
            &found.rules_revision, &found.handling_profile_id, &found.handling_revision,
            &found.state_schema_version};
        for (size_t i = 0u; i < sizeof fields / sizeof fields[0]; ++i)
            if (!read_u32(&cursor, end, fields[i]))
                return PORTABLE_PERSONAL_BEST_INVALID;
        if (!read_digest(cursor, end, digest))
            return PORTABLE_PERSONAL_BEST_INVALID;
        for(unsigned index=0u;index<=PORTABLE_PERSONAL_BEST_COUNT;++index){
            PortablePersonalBestRecord *record=index<PORTABLE_PERSONAL_BEST_COUNT?
                &table->races[index]:&table->best_lap;
            if (fgets(line, sizeof line, file) == NULL)
                return PORTABLE_PERSONAL_BEST_INVALID;
            cursor = line;
            end = line + strlen(line);
            if (!read_u32(&cursor, end, &record->ticks) ||
                !read_u32(&cursor, end, &record->character_class) ||
                (version >= 5u && !read_u32(&cursor, end, &record->roster_driver_id)) ||
                !only_whitespace(cursor, end) || record->character_class >= 8u ||
                record->roster_driver_id > 255u ||
                (record->ticks == 0u && (record->character_class != 0u || record->roster_driver_id != 0u)))
                return PORTABLE_PERSONAL_BEST_INVALID;
            if(index>0u&&index<PORTABLE_PERSONAL_BEST_COUNT&&record->ticks!=0u&&
               (table->races[index-1u].ticks==0u||
                table->races[index-1u].ticks>record->ticks))
                return PORTABLE_PERSONAL_BEST_INVALID;
        }
        if(table->races[0].ticks==0u)return PORTABLE_PERSONAL_BEST_INVALID;
        if(version==6u){
            if(fgets(line,sizeof line,file)==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
            size_t length=strlen(line);
            if(length==0u||line[length-1u]!='\n')return PORTABLE_PERSONAL_BEST_INVALID;
            line[--length]='\0';
            if(!portable_mod_identity_parse(line,length,&mods)||mods.count==0u)return PORTABLE_PERSONAL_BEST_INVALID;
        }
    }else return PORTABLE_PERSONAL_BEST_INVALID;
    if(!digest_parse(digest,found.deterministic_digest)||fgetc(file)!=EOF)
        return PORTABLE_PERSONAL_BEST_INVALID;
    return compatible(&found,expected)&&
        portable_mod_identity_equal(&mods,portable_mod_identity_current())?
        PORTABLE_PERSONAL_BEST_OK:PORTABLE_PERSONAL_BEST_INCOMPATIBLE;
}

PortablePersonalBestStatus portable_personal_best_load_table(
    const char *path,const PortableRaceCompatibilityId *expected,
    PortablePersonalBestTable *out_table)
{
    if(path==NULL||expected==NULL||out_table==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    memset(out_table,0,sizeof *out_table);
    FILE *file=fopen(path,"r");
    if(file==NULL)return errno==ENOENT?PORTABLE_PERSONAL_BEST_NOT_FOUND:
                                    PORTABLE_PERSONAL_BEST_IO_ERROR;
    PortablePersonalBestTable next={0};
    PortablePersonalBestStatus status=read_table(file,expected,&next);
    if(ferror(file))status=PORTABLE_PERSONAL_BEST_IO_ERROR;
    if(fclose(file)!=0)status=PORTABLE_PERSONAL_BEST_IO_ERROR;
    if(status==PORTABLE_PERSONAL_BEST_OK)*out_table=next;
    return status;
}

static PortablePersonalBestStatus write_table(const char *path,
    const PortableRaceCompatibilityId *identity,const PortablePersonalBestTable *table)
{
    const size_t length=strlen(path);
    if(length>SIZE_MAX-5u)return PORTABLE_PERSONAL_BEST_INVALID;
    char *temporary=malloc(length+5u);
    if(temporary==NULL)return PORTABLE_PERSONAL_BEST_IO_ERROR;
    memcpy(temporary,path,length);memcpy(temporary+length,".tmp",5u);
    FILE *file=fopen(temporary,"w");
    if(file==NULL){free(temporary);return PORTABLE_PERSONAL_BEST_IO_ERROR;}
    unsigned version=4u;
    for(unsigned index=0u;index<=PORTABLE_PERSONAL_BEST_COUNT;++index)
        if((index<PORTABLE_PERSONAL_BEST_COUNT?table->races[index]:table->best_lap).roster_driver_id!=0u)
            version=5u;
    const PortableModIdentity *mods=portable_mod_identity_current();
    char mod_text[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY];
    if(mods->count!=0u){
        if(portable_mod_identity_format(mods,mod_text,sizeof mod_text)==0u){
            fclose(file);remove(temporary);free(temporary);return PORTABLE_PERSONAL_BEST_INVALID;
        }
        version=6u;
    }
    int failed=fprintf(file,"MKSC-PB %u %" PRIu32 " %" PRIu32 " %" PRIu32
        " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " ",version,
        identity->course_id,identity->course_revision,identity->rules_id,
        identity->rules_revision,identity->handling_profile_id,
        identity->handling_revision,identity->state_schema_version)<0;
    for(size_t i=0u;!failed&&i<sizeof identity->deterministic_digest;++i)
        failed=fprintf(file,"%02x",identity->deterministic_digest[i])<0;
    if(!failed)failed=fputc('\n',file)==EOF;
    for(unsigned index=0u;!failed&&index<=PORTABLE_PERSONAL_BEST_COUNT;++index){
        const PortablePersonalBestRecord *record=index<PORTABLE_PERSONAL_BEST_COUNT?
            &table->races[index]:&table->best_lap;
        failed=(version>=5u?fprintf(file,"%" PRIu32 " %" PRIu32 " %" PRIu32 "\n",record->ticks,
            record->character_class,record->roster_driver_id):
            fprintf(file,"%" PRIu32 " %" PRIu32 "\n",record->ticks,record->character_class))<0;
    }
    if(!failed&&version==6u)failed=fprintf(file,"%s\n",mod_text)<0;
    if(fclose(file)!=0)failed=1;
    if(failed||!portable_file_replace(temporary,path)){remove(temporary);free(temporary);return PORTABLE_PERSONAL_BEST_IO_ERROR;}
    free(temporary);return PORTABLE_PERSONAL_BEST_OK;
}

PortablePersonalBestStatus portable_personal_best_update_table(
    const char *path,const PortableRaceCompatibilityId *identity,
    uint32_t race_ticks,uint32_t lap_ticks,PortablePersonalBestTable *out_table,
    int *out_new_best)
{
    if(path==NULL||identity==NULL||out_table==NULL||out_new_best==NULL||
       race_ticks==0u||lap_ticks>race_ticks||identity->character_class>=8u||
       identity->roster_driver_id>255u)
        return PORTABLE_PERSONAL_BEST_INVALID;
    *out_new_best=0;
    memset(out_table,0,sizeof *out_table);
    PortablePersonalBestTable next={0};
    PortablePersonalBestStatus status=portable_personal_best_load_table(path,identity,&next);
    if(status!=PORTABLE_PERSONAL_BEST_OK&&status!=PORTABLE_PERSONAL_BEST_NOT_FOUND&&
       status!=PORTABLE_PERSONAL_BEST_INCOMPATIBLE)return status;
    const uint32_t previous_best=next.races[0].ticks;
    int changed=0;
    /* The source records board has five totals and one independent lap.
     * Equal totals retain the preceding driver ahead of the new result. */
    for(unsigned index=0u;index<PORTABLE_PERSONAL_BEST_COUNT;++index){
        if(next.races[index].ticks==0u||race_ticks<next.races[index].ticks){
            for(unsigned move=PORTABLE_PERSONAL_BEST_COUNT-1u;move>index;--move)
                next.races[move]=next.races[move-1u];
            next.races[index]=(PortablePersonalBestRecord){race_ticks,identity->character_class,
                identity->roster_driver_id};
            changed=1;break;
        }
    }
    if(lap_ticks!=0u&&(next.best_lap.ticks==0u||lap_ticks<next.best_lap.ticks)){
        next.best_lap=(PortablePersonalBestRecord){lap_ticks,identity->character_class,
            identity->roster_driver_id};changed=1;
    }
    if(changed&&(status=write_table(path,identity,&next))!=PORTABLE_PERSONAL_BEST_OK)return status;
    *out_table=next;
    *out_new_best=previous_best==0u||race_ticks<previous_best;
    return PORTABLE_PERSONAL_BEST_OK;
}

PortablePersonalBestStatus portable_personal_best_load_record(
    const char *path,const PortableRaceCompatibilityId *expected,
    PortablePersonalBestRecord *out_record)
{
    if(out_record==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    *out_record=(PortablePersonalBestRecord){0};
    PortablePersonalBestTable table;
    const PortablePersonalBestStatus status=portable_personal_best_load_table(path,expected,&table);
    if(status==PORTABLE_PERSONAL_BEST_OK)*out_record=table.races[0];
    return status;
}

PortablePersonalBestStatus portable_personal_best_update_record(
    const char *path,const PortableRaceCompatibilityId *identity,
    uint32_t candidate_ticks,PortablePersonalBestRecord *out_record,int *out_updated)
{
    if(out_record==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    *out_record=(PortablePersonalBestRecord){0};
    PortablePersonalBestTable table;
    const PortablePersonalBestStatus status=portable_personal_best_update_table(
        path,identity,candidate_ticks,0u,&table,out_updated);
    if(status==PORTABLE_PERSONAL_BEST_OK)*out_record=table.races[0];
    return status;
}

PortablePersonalBestStatus portable_personal_best_load(
    const char *path,const PortableRaceCompatibilityId *expected,
    uint32_t *out_ticks)
{
    PortablePersonalBestRecord record;
    PortablePersonalBestStatus status;
    if(out_ticks==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    *out_ticks=0;
    status=portable_personal_best_load_record(path,expected,&record);
    if(status==PORTABLE_PERSONAL_BEST_OK)*out_ticks=record.ticks;
    return status;
}

PortablePersonalBestStatus portable_personal_best_update(
    const char *path,const PortableRaceCompatibilityId *identity,
    uint32_t candidate_ticks,uint32_t *out_best_ticks,int *out_updated)
{
    PortablePersonalBestRecord record;
    PortablePersonalBestStatus status;
    if(out_best_ticks==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    *out_best_ticks=0;
    status=portable_personal_best_update_record(
        path,identity,candidate_ticks,&record,out_updated);
    if(status==PORTABLE_PERSONAL_BEST_OK)*out_best_ticks=record.ticks;
    return status;
}
