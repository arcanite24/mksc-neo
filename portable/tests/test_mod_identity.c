/* Mod identity in ghosts, Time Trial records and GP progress: stock files keep
 * their old formats and still load; files written with mods record the set
 * and are refused with a clear reason under a different set. */
#include "gp_progress_store.h"
#include "mod_identity.h"
#include "personal_best.h"
#include "time_trial_ghost_store.h"
#include "test_temp.h"
#include "text_number.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"mod identity line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

static void unsigned_number(const char *text, unsigned base, uint64_t maximum, int valid,
                            uint64_t expected, const char *remaining)
{
    const char *cursor = text;
    uint64_t value = 51u;
    CHECK(portable_text_u64(&cursor, text + strlen(text), base, maximum, &value) == valid);
    if (valid)
        CHECK(value == expected && strcmp(cursor, remaining) == 0);
    else
        CHECK(cursor == text && value == 51u);
}

static void signed_number(const char *text, unsigned base, int64_t minimum, int64_t maximum,
                          int valid, int64_t expected)
{
    const char *cursor = text;
    int64_t value = 51;
    CHECK(portable_text_i64(&cursor, text + strlen(text), base, minimum, maximum, &value) == valid);
    if (valid)
        CHECK(value == expected && *cursor == '\0');
    else
        CHECK(cursor == text && value == 51);
}

static void text_numbers(void)
{
    unsigned_number(" \t\r\n\v\f+0xffffffff!", 0u, UINT32_MAX, 1, UINT32_MAX, "!");
    unsigned_number("4294967295", 10u, UINT32_MAX, 1, UINT32_MAX, "");
    unsigned_number("4294967296", 10u, UINT32_MAX, 0, 0u, "");
    unsigned_number("-1", 0u, UINT32_MAX, 0, 0u, "");
    unsigned_number(" -4294967295", 0u, UINT32_MAX, 0, 0u, "");
    unsigned_number("-0", 10u, UINT64_MAX, 0, 0u, "");
    unsigned_number("18446744073709551615", 10u, UINT64_MAX, 1, UINT64_MAX, "");
    unsigned_number("18446744073709551616", 10u, UINT64_MAX, 0, 0u, "");
    unsigned_number("1844674407370955161500000000000000000000", 10u, UINT64_MAX, 0, 0u, "");
    unsigned_number("010", 0u, UINT64_MAX, 1, 8u, "");
    unsigned_number("09", 0u, UINT64_MAX, 1, 0u, "9");
    unsigned_number("0xFF", 16u, 255u, 1, 255u, "");
    unsigned_number("1012", 2u, 10u, 1, 5u, "2");
    unsigned_number("Z!", 36u, 35u, 1, 35u, "!");
    unsigned_number("1", 1u, UINT64_MAX, 0, 0u, "");
    unsigned_number("", 10u, UINT64_MAX, 0, 0u, "");
    unsigned_number("+", 10u, UINT64_MAX, 0, 0u, "");
    unsigned_number("0x", 0u, UINT64_MAX, 0, 0u, "");
    unsigned_number("0", 10u, 0u, 1, 0u, "");
    unsigned_number("1", 10u, 0u, 0, 0u, "");
    signed_number("-9223372036854775808", 10u, INT64_MIN, INT64_MAX, 1, INT64_MIN);
    signed_number("9223372036854775807", 10u, INT64_MIN, INT64_MAX, 1, INT64_MAX);
    signed_number("-9223372036854775809", 10u, INT64_MIN, INT64_MAX, 0, 0);
    signed_number("9223372036854775808", 10u, INT64_MIN, INT64_MAX, 0, 0);
    signed_number("-0x8000000000000000", 0u, INT64_MIN, INT64_MAX, 1, INT64_MIN);
    signed_number("+0x7fffffffffffffff", 16u, INT64_MIN, INT64_MAX, 1, INT64_MAX);
    signed_number("-5", 10u, -5, 5, 1, -5);
    signed_number("-6", 10u, -5, 5, 0, 0);
    signed_number("6", 10u, -5, 5, 0, 0);
    signed_number("0", 10u, -5, -1, 0, 0);
    signed_number("-0", 10u, 0, 0, 1, 0);
    const char bytes[3] = {'2', '5', '5'};
    const char *cursor = bytes;
    uint64_t value;
    CHECK(portable_text_u64(&cursor, bytes + sizeof bytes, 10u, 255u, &value));
    CHECK(cursor == bytes + sizeof bytes && value == 255u);
}

static PortableModIdentity set_of(const char *first, const char *second)
{
    PortableModIdentity identity = {PORTABLE_MOD_API_VERSION, 0u, {{{0}, {0}, {0}}}};
    const char *ids[] = {first, second};
    for (unsigned i = 0u; i < 2u; ++i)
    {
        if (ids[i] == NULL)
            continue;
        PortableModIdentityEntry *mod = &identity.mods[identity.count++];
        snprintf(mod->id, sizeof mod->id, "%s", ids[i]);
        snprintf(mod->version, sizeof mod->version, "1.%u.0", i);
        for (unsigned b = 0u; b < 8u; ++b)
            mod->fingerprint[b] = (uint8_t)(b * 17u + i);
    }
    return identity;
}

static PortableRaceCompatibilityId race(void)
{
    PortableRaceCompatibilityId value={24u,3u,7u,2u,11u,4u,1u,5u,{0},0u};
    for(size_t i=0u;i<sizeof value.deterministic_digest;++i)value.deterministic_digest[i]=(uint8_t)i;
    return value;
}

static void ghost_record(uint8_t *record)
{
    PortableTimeTrialGhostEncoder encoder;
    portable_time_trial_ghost_record_init(record,1u,24u);
    CHECK(portable_time_trial_ghost_encoder_init(&encoder,record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
        PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE)&&portable_time_trial_ghost_encoder_append(&encoder,0x8001u));
    portable_time_trial_ghost_record_set_time(record,900u);
}

static long file_size(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long size=ftell(file);fclose(file);return size;
}

static void identity_text(void)
{
    const PortableModIdentity two=set_of("lap-bonus","my_track");
    char text[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY],human[256];
    const size_t length=portable_mod_identity_format(&two,text,sizeof text);
    CHECK(length!=0u&&strcmp(text,"MODS1 1 2 lap-bonus 1.0.0 0011223344556677 my_track 1.1.0 0112233445566778")==0);
    PortableModIdentity parsed;
    CHECK(portable_mod_identity_parse(text,length,&parsed)&&portable_mod_identity_equal(&parsed,&two));
    const char *bad[]={"MODS1 2 0","MODS1 1 1","MODS1 1 1 Bad 1.0.0 0011223344556677",
        "MODS1 1 1 a 1.0.0 00112233445566","MODS1 1 0 extra","MODS2 1 0","MODS1 1 33",
        "MODS1 -4294967295 0", "MODS1 1 -4294967295 a 1.0.0 0011223344556677",
        "MODS1 4294967297 0", "MODS1 1 4294967296",
        "MODS1 18446744073709551616 0"};
    for(unsigned i=0u;i<sizeof bad/sizeof bad[0];++i)CHECK(!portable_mod_identity_parse(bad[i],strlen(bad[i]),&parsed));
    portable_mod_identity_describe(&two,human,sizeof human);
    CHECK(strcmp(human, "lap-bonus 1.0.0, my_track 1.1.0") == 0);
    portable_mod_identity_set_current(NULL);
    CHECK(portable_mod_identity_current()->count == 0u &&
          portable_mod_identity_record_suffix()[0] == '\0');
    portable_mod_identity_describe(portable_mod_identity_current(), human, sizeof human);
    CHECK(strcmp(human, "none") == 0);
    portable_mod_identity_set_current(&two);
    const char *suffix = portable_mod_identity_record_suffix();
    CHECK(strncmp(suffix, "-mods-", 6u) == 0 && strlen(suffix) == 14u);
    portable_mod_identity_set_current(NULL);
}

static void ghosts(void)
{
    char path[128];
    const int descriptor=test_temp_path(path,sizeof path,"mod-identity-ghost",0);
    CHECK(descriptor>=0);test_close(descriptor);CHECK(remove(path)==0);
    const PortableRaceCompatibilityId id=race();
    uint8_t *record=malloc(PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE),*out=malloc(PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    CHECK(record!=NULL&&out!=NULL);ghost_record(record);
    char reason[512];
    PortableModIdentity found;
    /* Stock: version 1, exactly header + record, loads only without mods. */
    portable_mod_identity_set_current(NULL);
    CHECK(portable_time_trial_ghost_store_save(path,&id,record,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(file_size(path)==96+PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    CHECK(portable_time_trial_ghost_store_load(path,&id,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_store_mods(path,&found)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&found.count==0u);
    const PortableModIdentity lua=set_of("lap-bonus",NULL),other=set_of("item-swap",NULL);
    portable_mod_identity_set_current(&lua);
    CHECK(portable_time_trial_ghost_store_load(path,&id,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    CHECK(portable_time_trial_ghost_store_describe_refusal(path,reason,sizeof reason));
    CHECK(strcmp(reason,"recorded with mods [none]; enabled mods [lap-bonus 1.0.0]")==0);
    /* A different set's file stays until erased, as for other content. */
    CHECK(portable_time_trial_ghost_store_save(path,&id,record,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    CHECK(remove(path)==0);
    /* With mods: version 2 appends the set and reloads under the same set. */
    CHECK(portable_time_trial_ghost_store_save(path,&id,record,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(file_size(path)>96+PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE);
    CHECK(portable_time_trial_ghost_store_load(path,&id,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&
          portable_time_trial_ghost_record_time(out)==900u);
    CHECK(!portable_time_trial_ghost_store_describe_refusal(path,reason,sizeof reason));
    CHECK(portable_time_trial_ghost_store_mods(path,&found)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&
          portable_mod_identity_equal(&found,&lua));
    portable_mod_identity_set_current(&other);
    CHECK(portable_time_trial_ghost_store_load(path,&id,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    CHECK(portable_time_trial_ghost_store_describe_refusal(path,reason,sizeof reason)&&
          strcmp(reason,"recorded with mods [lap-bonus 1.0.0]; enabled mods [item-swap 1.0.0]")==0);
    portable_mod_identity_set_current(NULL);
    CHECK(portable_time_trial_ghost_store_load(path,&id,out)==PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
    /* A damaged identity trailer is an invalid file, not a different set. */
    FILE *file=fopen(path,"r+b");CHECK(file!=NULL);
    CHECK(fseek(file,96+PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE+2,SEEK_SET)==0&&fputc('X',file)!=EOF&&fclose(file)==0);
    CHECK(portable_time_trial_ghost_store_mods(path,&found)==PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID);
    CHECK(remove(path)==0);free(record);free(out);
}

static void records(void)
{
    char path[128];
    const int descriptor=test_temp_path(path,sizeof path,"mod-identity-pb",0);
    CHECK(descriptor>=0);test_close(descriptor);CHECK(remove(path)==0);
    const PortableRaceCompatibilityId id=race();
    PortablePersonalBestTable table;
    int best=0;
    portable_mod_identity_set_current(NULL);
    CHECK(portable_personal_best_update_table(path,&id,1000u,300u,&table,&best)==PORTABLE_PERSONAL_BEST_OK&&best);
    char line[64];
    FILE *file=fopen(path,"r");CHECK(file!=NULL&&fgets(line,sizeof line,file)!=NULL);fclose(file);
    CHECK(strncmp(line,"MKSC-PB 4 ",10u)==0);
    const PortableModIdentity lua=set_of("lap-bonus",NULL);
    portable_mod_identity_set_current(&lua);
    CHECK(portable_personal_best_load_table(path,&id,&table)==PORTABLE_PERSONAL_BEST_INCOMPATIBLE);
    CHECK(remove(path)==0);
    CHECK(portable_personal_best_update_table(path,&id,1100u,320u,&table,&best)==PORTABLE_PERSONAL_BEST_OK&&best);
    file=fopen(path,"r");CHECK(file!=NULL&&fgets(line,sizeof line,file)!=NULL);fclose(file);
    CHECK(strncmp(line,"MKSC-PB 6 ",10u)==0);
    CHECK(portable_personal_best_load_table(path,&id,&table)==PORTABLE_PERSONAL_BEST_OK&&table.races[0].ticks==1100u);
    portable_mod_identity_set_current(NULL);
    CHECK(portable_personal_best_load_table(path,&id,&table)==PORTABLE_PERSONAL_BEST_INCOMPATIBLE);
    CHECK(remove(path)==0);
}

static void progress(void)
{
    char path[128];
    const int descriptor=test_temp_path(path,sizeof path,"mod-identity-gp",0);
    CHECK(descriptor>=0);test_close(descriptor);CHECK(remove(path)==0);
    PortableGpProgress saved,loaded;
    PortableModIdentity found;
    portable_gp_progress_init(&saved);saved.grade[0][1][2]=0;saved.unlock_flags=5u;
    portable_mod_identity_set_current(NULL);
    CHECK(portable_gp_progress_save(path,&saved)==PORTABLE_GP_PROGRESS_OK&&file_size(path)==80);
    const PortableModIdentity two=set_of("custom-cup","lap-bonus");
    portable_mod_identity_set_current(&two);
    /* Old files load under any mod set; progress is shared, the set is noted. */
    CHECK(portable_gp_progress_load_with_mods(path,&loaded,&found)==PORTABLE_GP_PROGRESS_OK&&found.count==0u);
    CHECK(portable_gp_progress_save(path,&saved)==PORTABLE_GP_PROGRESS_OK&&file_size(path)>80);
    portable_mod_identity_set_current(NULL);
    CHECK(portable_gp_progress_load_with_mods(path,&loaded,&found)==PORTABLE_GP_PROGRESS_OK&&
          portable_mod_identity_equal(&found,&two)&&memcmp(&loaded,&saved,sizeof saved)==0);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK);
    FILE *file=fopen(path,"r+b");CHECK(file!=NULL);
    CHECK(fseek(file,85,SEEK_SET)==0&&fputc('Z',file)!=EOF&&fclose(file)==0);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_BAD_FORMAT);
    CHECK(remove(path)==0);
}

int main(void)
{
    text_numbers();
    identity_text();
    ghosts();
    records();
    progress();
    puts("mod identity: PASS text form, ghost v1/v2 refusal, records v6, GP progress v2");
    return 0;
}
