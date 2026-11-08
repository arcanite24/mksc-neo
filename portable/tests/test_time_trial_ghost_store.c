#include "time_trial_ghost_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_temp.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "time trial ghost store failed line %d: %s\n", \
            __LINE__, #x); \
    return 1; \
} } while (0)

static PortableRaceCompatibilityId identity(uint32_t character)
{
    PortableRaceCompatibilityId value = {
        24u, 3u, 7u, 2u, 11u, 4u, character, 5u, {0}, 0u,
    };
    for (size_t index = 0; index < sizeof value.deterministic_digest; ++index)
        value.deterministic_digest[index] = (uint8_t)(index * 7u + 3u);
    return value;
}

static int candidate(uint8_t *record, uint8_t driver, uint16_t ticks,
                     uint8_t marker)
{
    PortableTimeTrialGhostEncoder encoder;
    portable_time_trial_ghost_record_init(record, driver, 24u);
    if (!portable_time_trial_ghost_encoder_init(
            &encoder, record + PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
            PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE) ||
        !portable_time_trial_ghost_encoder_append(
            &encoder, (uint16_t)(0x8000u | marker)))
        return 0;
    portable_time_trial_ghost_record_set_time(record, ticks);
    return 1;
}

static int write_process_fixture(const char *path)
{
    PortableRaceCompatibilityId mario = identity(0u);
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t stored[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    int updated = 0;
    CHECK(candidate(record, 0u, 777u, 7u));
    CHECK(portable_time_trial_ghost_store_update(
              path, &mario, record, stored, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK && updated);
    return 0;
}

static int read_process_fixture(const char *path)
{
    PortableRaceCompatibilityId luigi = identity(1u);
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    CHECK(portable_time_trial_ghost_store_load(path, &luigi, record) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_record_time(record) == 777u &&
          portable_time_trial_ghost_record_driver(record) == 0u &&
          record[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET + 3u] == 7u);
    return 0;
}

static int read_saved_bytes(const char *path,uint8_t *bytes,size_t size)
{
    FILE *file=fopen(path,"rb");
    CHECK(file!=NULL);
    CHECK(fread(bytes,1u,size,file)==size&&fgetc(file)==EOF);
    CHECK(fclose(file)==0);return 0;
}

static int stock_modern_coexistence(void)
{
    const PortableModIdentity prior_mods=*portable_mod_identity_current();
    const uint8_t prior_mirror=portable_mod_identity_mirror();
    /* This executable starts in Original. Keep this regression independent of
     * mod-set tests and restore every global namespace selector it changes. */
    CHECK(strstr(portable_mod_identity_progress_suffix(),"modern")==NULL);
    portable_mod_identity_set_current(NULL);portable_mod_identity_set_mirror(0u);
    char directory[128],paths[2][256];
    CHECK(test_temp_path(directory,sizeof directory,"ghost-profile",1)==0);
    PortableRaceCompatibilityId ids[2]={identity(0u),identity(0u)};
    ids[1].rules_id=1234u;ids[1].rules_revision=3u;
    ids[1].deterministic_digest[0]^=0x80u;
    const uint8_t fingerprint[8]={1u,2u,3u,4u,5u,6u,7u,8u};
    uint8_t records[2][PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t loaded[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],cleared[sizeof loaded];
    memset(cleared,0,sizeof cleared);
    uint8_t before[2][96u+PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],after[sizeof before[0]];
    for(unsigned profile=0u;profile<2u;++profile){
        portable_mod_identity_set_driving_profile(profile?fingerprint:NULL);
        int length=snprintf(paths[profile],sizeof paths[profile],"%s/course-24-solo%s.ghost",
            directory,portable_mod_identity_record_suffix());
        CHECK(length>0&&(size_t)length<sizeof paths[profile]);
        CHECK(candidate(records[profile],0u,(uint16_t)(800u+profile), (uint8_t)(profile+1u)));
        CHECK(portable_time_trial_ghost_store_save(paths[profile],&ids[profile],records[profile],loaded)==
            PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
        CHECK(memcmp(loaded,records[profile],sizeof loaded)==0);
        CHECK(read_saved_bytes(paths[profile],before[profile],sizeof before[profile])==0);
    }
    CHECK(strcmp(paths[0],paths[1])!=0);
    for(unsigned profile=0u;profile<2u;++profile){
        portable_mod_identity_set_driving_profile(profile?fingerprint:NULL);
        CHECK(portable_time_trial_ghost_store_load(paths[profile],&ids[profile],loaded)==
            PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
        CHECK(memcmp(loaded,records[profile],sizeof loaded)==0);
        memset(loaded,0xa5,sizeof loaded);
        CHECK(portable_time_trial_ghost_store_load(paths[1u-profile],&ids[profile],loaded)==
            PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
        CHECK(memcmp(loaded,cleared,sizeof loaded)==0);
        memset(loaded,0xa5,sizeof loaded);
        CHECK(portable_time_trial_ghost_store_save(paths[1u-profile],&ids[profile],records[profile],loaded)==
            PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
        CHECK(memcmp(loaded,cleared,sizeof loaded)==0);
        /* Even an authored profile copying the Original rule IDs cannot
         * bypass the distinct deterministic gameplay domain. */
        PortableRaceCompatibilityId mimic=ids[1u-profile];
        mimic.deterministic_digest[0]^=0x80u;
        memset(loaded,0xa5,sizeof loaded);
        CHECK(portable_time_trial_ghost_store_load(paths[1u-profile],&mimic,loaded)==
            PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
        CHECK(memcmp(loaded,cleared,sizeof loaded)==0);
    }
    for(unsigned profile=0u;profile<2u;++profile){
        CHECK(read_saved_bytes(paths[profile],after,sizeof after)==0);
        CHECK(memcmp(before[profile],after,sizeof after)==0);
        CHECK(remove(paths[profile])==0);
    }
    CHECK(test_rmdir(directory)==0);
    portable_mod_identity_set_driving_profile(NULL);
    portable_mod_identity_set_current(&prior_mods);portable_mod_identity_set_mirror(prior_mirror);
    return 0;
}

static int picked_ghost_refusals(void)
{
    char picked[128], internal[128], directory[128];
    int fd=test_temp_path(picked,sizeof picked,"picked ghost",0);
    CHECK(fd>=0);test_close(fd);
    fd=test_temp_path(internal,sizeof internal,"received-ghost",0);
    CHECK(fd>=0);test_close(fd);CHECK(remove(internal)==0);
    CHECK(test_temp_path(directory,sizeof directory,"ghost-directory",1)==0);
    const PortableRaceCompatibilityId expected=identity(0u);
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t output[sizeof record], zero[sizeof record]={0};
    uint8_t bytes[96u+PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE], after[sizeof bytes];
    CHECK(candidate(record,0u,600u,9u));
    CHECK(portable_time_trial_ghost_store_save(internal,&expected,record,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(read_saved_bytes(internal,bytes,sizeof bytes)==0);
    FILE *file=fopen(picked,"wb");
    CHECK(file && fwrite(bytes,1,sizeof bytes,file)==sizeof bytes && fclose(file)==0);
    PortableRaceCompatibilityId found;
    CHECK(portable_time_trial_ghost_store_inspect(picked,&found,output)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK &&
        found.course_id==expected.course_id && !memcmp(output,record,sizeof record));
    PortableRaceCompatibilityId incompatible=expected;
    ++incompatible.rules_revision;
    memset(output,0xa5,sizeof output);
    CHECK(portable_time_trial_ghost_store_load(picked,&incompatible,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE && !memcmp(output,zero,sizeof output));
    CHECK(read_saved_bytes(picked,after,sizeof after)==0 && !memcmp(after,bytes,sizeof bytes));
    /* A picked envelope with a valid identity but corrupt record must not
     * publish either a record or a partially decoded mod identity. */
    bytes[sizeof bytes-1u]^=0x80u;
    file=fopen(picked,"wb");
    CHECK(file && fwrite(bytes,1,sizeof bytes,file)==sizeof bytes && fclose(file)==0);
    memset(output,0xa5,sizeof output);
    CHECK(portable_time_trial_ghost_store_inspect(picked,&found,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID && !memcmp(output,zero,sizeof output));
    PortableModIdentity mods, sentinel;
    memset(&mods,0x5a,sizeof mods);sentinel=mods;
    CHECK(portable_time_trial_ghost_store_mods(picked,&mods)==PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID &&
        !memcmp(&mods,&sentinel,sizeof mods));
    bytes[sizeof bytes-1u]^=0x80u;
    file=fopen(picked,"wb");
    CHECK(file && fwrite(bytes,1,sizeof bytes,file)==sizeof bytes && fputc(0,file)!=EOF && fclose(file)==0);
    CHECK(portable_time_trial_ghost_store_load(picked,&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID && !memcmp(output,zero,sizeof output));
    file=fopen(picked,"wb");
    CHECK(file && fseek(file,(long)(sizeof bytes+PORTABLE_MOD_IDENTITY_TEXT_CAPACITY),SEEK_SET)==0 &&
        fputc(0,file)!=EOF && fclose(file)==0);
    CHECK(portable_time_trial_ghost_store_load(picked,&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID && !memcmp(output,zero,sizeof output));
    file=fopen(picked,"wb");CHECK(file && fclose(file)==0);
    CHECK(portable_time_trial_ghost_store_load(picked,&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID && !memcmp(output,zero,sizeof output));
    CHECK(portable_time_trial_ghost_store_load(directory,&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR && !memcmp(output,zero,sizeof output));
    CHECK(portable_time_trial_ghost_store_load("content://invalid.mksc.test/document/missing",&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR && !memcmp(output,zero,sizeof output));
    CHECK(remove(picked)==0);
    CHECK(portable_time_trial_ghost_store_load(picked,&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND && !memcmp(output,zero,sizeof output));
    CHECK(read_saved_bytes(internal,after,sizeof after)==0 && !memcmp(after,bytes,sizeof bytes));
    CHECK(portable_time_trial_ghost_store_load(internal,&expected,output)==
        PORTABLE_TIME_TRIAL_GHOST_STORE_OK && !memcmp(output,record,sizeof record));
    CHECK(remove(internal)==0 && test_rmdir(directory)==0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--write") == 0)
        return write_process_fixture(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--read") == 0)
        return read_process_fixture(argv[2]);
    CHECK(argc == 1);
    CHECK(stock_modern_coexistence()==0);
    CHECK(picked_ghost_refusals()==0);

    char path[128];
    int descriptor = test_temp_path(path,sizeof path,"test_time_trial_ghost_store",0);
    PortableRaceCompatibilityId mario = identity(0u);
    PortableRaceCompatibilityId luigi = identity(1u);
    PortableRaceCompatibilityId changed;
    uint8_t first[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t next[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t loaded[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t saved_file[96u + PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    FILE *file;
    int updated = 0;

    CHECK(descriptor >= 0);
    test_close(descriptor);
    CHECK(remove(path) == 0);
    CHECK(portable_time_trial_ghost_store_load(path, &mario, loaded) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND);

    CHECK(candidate(first, 0u, 900u, 1u));
    CHECK(portable_time_trial_ghost_store_update(
              path, &mario, first, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(updated && memcmp(first, loaded, sizeof first) == 0);
    memset(loaded, 0, sizeof loaded);
    CHECK(portable_time_trial_ghost_store_load(path, &luigi, loaded) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_record_driver(loaded) == 0u &&
          portable_time_trial_ghost_record_time(loaded) == 900u);

    CHECK(candidate(next, 1u, 900u, 2u));
    CHECK(portable_time_trial_ghost_store_update(
              path, &luigi, next, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(!updated && portable_time_trial_ghost_record_driver(loaded) == 0u &&
          loaded[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET + 3u] == 1u);
    portable_time_trial_ghost_record_set_time(next, 901u);
    CHECK(portable_time_trial_ghost_store_update(
              path, &luigi, next, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK && !updated);

    portable_time_trial_ghost_record_set_time(next, 899u);
    CHECK(portable_time_trial_ghost_store_update(
              path, &luigi, next, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(updated && portable_time_trial_ghost_record_driver(loaded) == 1u &&
          portable_time_trial_ghost_record_time(loaded) == 899u &&
          loaded[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET + 3u] == 2u);

    file = fopen(path, "rb");
    CHECK(file != NULL && fread(saved_file, 1, sizeof saved_file, file) ==
          sizeof saved_file && fclose(file) == 0);
    changed = luigi;
    ++changed.course_revision;
    CHECK(portable_time_trial_ghost_store_load(path, &changed, loaded) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    CHECK(portable_time_trial_ghost_store_update(
              path, &changed, next, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    file = fopen(path, "rb");
    CHECK(file != NULL);
    {
        uint8_t after[sizeof saved_file];
        CHECK(fread(after, 1, sizeof after, file) == sizeof after &&
              fclose(file) == 0 &&
              memcmp(saved_file, after, sizeof after) == 0);
    }

    file = fopen(path, "r+b");
    CHECK(file != NULL && fseek(file, -1L, SEEK_END) == 0);
    {
        int byte = fgetc(file);
        CHECK(byte != EOF && fseek(file, -1L, SEEK_CUR) == 0 &&
              fputc(byte ^ 0xff, file) != EOF && fclose(file) == 0);
    }
    CHECK(portable_time_trial_ghost_store_load(path, &luigi, loaded) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID);
    CHECK(portable_time_trial_ghost_store_update(
              path, &luigi, next, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID);
    CHECK(remove(path) == 0);

    CHECK(candidate(next, 1u, 0u, 3u));
    CHECK(portable_time_trial_ghost_store_update(
              path, &luigi, next, loaded, &updated) ==
          PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID);
    puts("portable time trial ghost store: PASS");
    return 0;
}
