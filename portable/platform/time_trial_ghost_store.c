#include "file_replace.h"
#include "file_read.h"
#include "mod_identity.h"
#include "time_trial_ghost_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Version 1 is header + record. Version 2 (written while mods are enabled)
 * appends the mod identity text; header bytes 88/92 hold its length and
 * checksum, which version 1 keeps zero. */
enum
{
    GHOST_STORE_VERSION = 1,
    GHOST_STORE_MODS_VERSION = 2,
    GHOST_STORE_HEADER_SIZE = 96,
    GHOST_STORE_FILE_SIZE = GHOST_STORE_HEADER_SIZE +
                            PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE,
    GHOST_STORE_MAX_FILE_SIZE = GHOST_STORE_FILE_SIZE +
                                PORTABLE_MOD_IDENTITY_TEXT_CAPACITY,
};

static const uint8_t ghost_store_magic[8] = {
    'M', 'K', 'S', 'C', 'G', 'H', '0', '1',
};

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t bytes_checksum(const uint8_t *bytes, size_t size)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0u; index < size; ++index)
        hash = (hash ^ bytes[index]) * UINT32_C(16777619);
    return hash;
}

static uint32_t record_checksum(const uint8_t *record)
{
    return bytes_checksum(record, PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
}

static int compatible(const PortableRaceCompatibilityId *left,
                      const PortableRaceCompatibilityId *right)
{
    return left->course_id == right->course_id &&
        left->course_revision == right->course_revision &&
        left->rules_id == right->rules_id &&
        left->rules_revision == right->rules_revision &&
        left->handling_profile_id == right->handling_profile_id &&
        left->handling_revision == right->handling_revision &&
        left->state_schema_version == right->state_schema_version &&
        memcmp(left->deterministic_digest, right->deterministic_digest,
               sizeof left->deterministic_digest) == 0;
}

static int record_matches(const uint8_t *record,
                          const PortableRaceCompatibilityId *identity)
{
    return portable_time_trial_ghost_record_time(record) != 0u &&
        portable_time_trial_ghost_record_course(record) ==
            (uint8_t)identity->course_id &&
        portable_time_trial_ghost_record_driver(record) ==
            (uint8_t)(identity->character_class & 7u);
}

static PortableTimeTrialGhostStoreStatus read_record(
    const char *path,
    const PortableRaceCompatibilityId *expected,
    PortableRaceCompatibilityId *out_identity,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    PortableModIdentity *out_mods)
{
    PortableModIdentity mods = {PORTABLE_MOD_API_VERSION, 0u, {{{0}, {0}, {0}}}};
    uint32_t version = 0u;
    uint32_t trailer = 0u;
    uint8_t *bytes;
    PortableRaceCompatibilityId found = {0};
    size_t count = 0;

    PortableTimeTrialGhostStoreStatus status =
        PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;

    if (path == NULL || out_record == NULL)
        return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    memset(out_record, 0, PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    bytes = NULL;
    const PortableFileReadStatus read_status = portable_file_read_bounded(path,
        GHOST_STORE_MAX_FILE_SIZE, &bytes, &count);
    if (read_status != PORTABLE_FILE_READ_OK)
    {
        if (read_status == PORTABLE_FILE_READ_LIMIT || read_status == PORTABLE_FILE_READ_INVALID)
            return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
        if (read_status == PORTABLE_FILE_READ_NOT_FOUND)
            return PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND;
        return PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
    }
    if (count >= GHOST_STORE_HEADER_SIZE)
    {
        version = read_le32(bytes + 8);
        trailer = read_le32(bytes + 88);
    }
    if (count >= GHOST_STORE_FILE_SIZE &&
             count == GHOST_STORE_FILE_SIZE + (size_t)trailer &&
             memcmp(bytes, ghost_store_magic, sizeof ghost_store_magic) == 0 &&
             ((version == GHOST_STORE_VERSION && trailer == 0u &&
               read_le32(bytes + 92) == 0u) ||
              (version == GHOST_STORE_MODS_VERSION && trailer != 0u &&
               trailer < PORTABLE_MOD_IDENTITY_TEXT_CAPACITY &&
               read_le32(bytes + 92) ==
                   bytes_checksum(bytes + GHOST_STORE_FILE_SIZE, trailer) &&
               portable_mod_identity_parse(
                   (const char *)bytes + GHOST_STORE_FILE_SIZE, trailer, &mods))) &&
             read_le32(bytes + 12) == count &&
             read_le32(bytes + 80) == PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE)
    {
        found.course_id = read_le32(bytes + 16);
        found.course_revision = read_le32(bytes + 20);
        found.rules_id = read_le32(bytes + 24);
        found.rules_revision = read_le32(bytes + 28);
        found.handling_profile_id = read_le32(bytes + 32);
        found.handling_revision = read_le32(bytes + 36);
        found.character_class = read_le32(bytes + 40);
        found.state_schema_version = read_le32(bytes + 44);
        memcpy(found.deterministic_digest, bytes + 48,
               sizeof found.deterministic_digest);
        if (expected != NULL && (!compatible(&found, expected) ||
            !portable_mod_identity_equal(&mods, portable_mod_identity_current())))
            status = PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE;
        else if (found.character_class > 7u ||
                 read_le32(bytes + 84) !=
                     record_checksum(bytes + GHOST_STORE_HEADER_SIZE) ||
                 !record_matches(bytes + GHOST_STORE_HEADER_SIZE, &found))
            status = PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
        else
        {
            memcpy(out_record, bytes + GHOST_STORE_HEADER_SIZE,
                   PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
            found.roster_driver_id = portable_time_trial_ghost_record_roster_driver(out_record);
            if(out_identity!=NULL)*out_identity=found;
            if(out_mods!=NULL)*out_mods=mods;
            status = PORTABLE_TIME_TRIAL_GHOST_STORE_OK;
        }
    }
    free(bytes);
    return status;
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_load(
    const char *path,const PortableRaceCompatibilityId *expected,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    if(expected==NULL)return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    return read_record(path,expected,NULL,out_record,NULL);
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_mods(
    const char *path,PortableModIdentity *mods)
{
    uint8_t *record=malloc(PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    PortableRaceCompatibilityId identity;
    if(record==NULL||mods==NULL){free(record);return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;}
    const PortableTimeTrialGhostStoreStatus status=read_record(path,NULL,&identity,record,mods);
    free(record);
    return status;
}

int portable_time_trial_ghost_store_describe_refusal(const char *path,char *text,size_t capacity)
{
    PortableModIdentity recorded;
    if(text==NULL||capacity==0u)return 0;
    text[0]='\0';
    if(portable_time_trial_ghost_store_mods(path,&recorded)!=PORTABLE_TIME_TRIAL_GHOST_STORE_OK)return 0;
    const PortableModIdentity *enabled=portable_mod_identity_current();
    if(portable_mod_identity_equal(&recorded,enabled))return 0;
    char have[1024],need[1024];
    portable_mod_identity_describe(&recorded,need,sizeof need);
    portable_mod_identity_describe(enabled,have,sizeof have);
    snprintf(text,capacity,"recorded with mods [%s]; enabled mods [%s]",need,have);
    return 1;
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_inspect(
    const char *path,PortableRaceCompatibilityId *identity,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    if(identity==NULL)return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    memset(identity,0,sizeof *identity);
    return read_record(path,NULL,identity,out_record,NULL);
}

typedef enum { GHOST_WRITE_BEST, GHOST_WRITE_LOCAL, GHOST_WRITE_RECEIVED } GhostWriteMode;

static PortableTimeTrialGhostStoreStatus write_record(
    const char *path,
    const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    int *out_updated,GhostWriteMode mode)
{
    uint8_t *bytes;
    uint8_t previous[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    char *temporary;
    size_t path_length;
    FILE *file;
    int failed;
    size_t trailer;
    size_t file_size;
    PortableTimeTrialGhostStoreStatus status;

    if (path == NULL || identity == NULL || candidate == NULL ||
        out_record == NULL || out_updated == NULL ||
        identity->course_id > UINT8_MAX || identity->character_class > 7u ||
        !record_matches(candidate, identity))
        return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    *out_updated = 0;
    memset(out_record, 0, PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    status = portable_time_trial_ghost_store_load(path, identity, previous);
    if (mode == GHOST_WRITE_BEST && status == PORTABLE_TIME_TRIAL_GHOST_STORE_OK &&
        !portable_time_trial_ghost_should_replace(previous, candidate))
    {
        memcpy(out_record, previous, sizeof previous);
        return PORTABLE_TIME_TRIAL_GHOST_STORE_OK;
    }
    if (mode != GHOST_WRITE_RECEIVED && status != PORTABLE_TIME_TRIAL_GHOST_STORE_OK &&
        status != PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND)
        return status;

    bytes = calloc(1u, GHOST_STORE_MAX_FILE_SIZE);
    if (bytes == NULL)
        return PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
    {
        const PortableModIdentity *mods = portable_mod_identity_current();
        trailer = mods->count == 0u ? 0u :
            portable_mod_identity_format(mods,
                (char *)bytes + GHOST_STORE_FILE_SIZE,
                PORTABLE_MOD_IDENTITY_TEXT_CAPACITY);
        if (mods->count != 0u && trailer == 0u)
        {
            free(bytes);
            return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
        }
    }
    file_size = GHOST_STORE_FILE_SIZE + trailer;
    memcpy(bytes, ghost_store_magic, sizeof ghost_store_magic);
    write_le32(bytes + 8, trailer != 0u ? GHOST_STORE_MODS_VERSION : GHOST_STORE_VERSION);
    write_le32(bytes + 12, (uint32_t)file_size);
    write_le32(bytes + 88, (uint32_t)trailer);
    write_le32(bytes + 92, trailer != 0u ?
        bytes_checksum(bytes + GHOST_STORE_FILE_SIZE, trailer) : 0u);
    write_le32(bytes + 16, identity->course_id);
    write_le32(bytes + 20, identity->course_revision);
    write_le32(bytes + 24, identity->rules_id);
    write_le32(bytes + 28, identity->rules_revision);
    write_le32(bytes + 32, identity->handling_profile_id);
    write_le32(bytes + 36, identity->handling_revision);
    write_le32(bytes + 40, identity->character_class);
    write_le32(bytes + 44, identity->state_schema_version);
    memcpy(bytes + 48, identity->deterministic_digest,
           sizeof identity->deterministic_digest);
    write_le32(bytes + 80, PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    write_le32(bytes + 84, record_checksum(candidate));
    memcpy(bytes + GHOST_STORE_HEADER_SIZE, candidate,
           PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);

    path_length = strlen(path);
    if (path_length > SIZE_MAX - 5u)
    {
        free(bytes);
        return PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    }
    temporary = malloc(path_length + 5u);
    if (temporary == NULL)
    {
        free(bytes);
        return PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
    }
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", 5u);
    file = fopen(temporary, "wb");
    if (file == NULL)
    {
        remove(temporary);
        free(temporary);
        free(bytes);
        return PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
    }
    failed = fwrite(bytes, 1u, file_size, file) != file_size;
    if (!failed && fflush(file) != 0)
        failed = 1;
    if (fclose(file) != 0)
        failed = 1;
    if(!failed&&!portable_file_replace(temporary,path))failed=1;
    if (failed)
    {
        remove(temporary);
        free(temporary);
        free(bytes);
        return PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
    }
    free(temporary);
    free(bytes);
    memcpy(out_record, candidate, PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    *out_updated = 1;
    return PORTABLE_TIME_TRIAL_GHOST_STORE_OK;
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_update(
    const char *path,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],int *out_updated)
{
    return write_record(path,identity,candidate,out_record,out_updated,GHOST_WRITE_BEST);
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_save(
    const char *path,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    int updated=0;
    return write_record(path,identity,candidate,out_record,&updated,GHOST_WRITE_LOCAL);
}

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_replace(
    const char *path,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    int updated=0;
    return write_record(path,identity,candidate,out_record,&updated,GHOST_WRITE_RECEIVED);
}
