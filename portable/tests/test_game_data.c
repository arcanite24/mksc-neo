#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui_text.h"
#include "software_frame.h"
#include "game_data.h"
#include "mod_loader.h"
#include "test_temp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"game data line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void capture_directory(const char *directory)
{
#ifdef _WIN32
    CHECK(_putenv_s("MKSC_SCREEN_DIR",directory?directory:"")==0);
#else
    CHECK(directory?setenv("MKSC_SCREEN_DIR",directory,1)==0:unsetenv("MKSC_SCREEN_DIR")==0);
#endif
}
static void capture_pixel(const char *path,uint32_t expected)
{
    unsigned char bytes[14];FILE *file=fopen(path,"rb");CHECK(file!=NULL);
    CHECK(fread(bytes,1u,sizeof bytes,file)==sizeof bytes&&fgetc(file)==EOF&&fclose(file)==0);
    CHECK(memcmp(bytes,"P6\n1 1\n255\n",11u)==0);
    CHECK(bytes[11]==(uint8_t)(expected>>16u)&&bytes[12]==(uint8_t)(expected>>8u)&&bytes[13]==(uint8_t)expected);
}
static void check_screen_capture_lifecycle(void)
{
    const char *environment=getenv("MKSC_SCREEN_DIR");char *saved=NULL;
    if(environment){saved=malloc(strlen(environment)+1u);CHECK(saved!=NULL);strcpy(saved,environment);}
    char directory[128],other[128],path[256],name[24];
    CHECK(test_temp_path(directory,sizeof directory,"screen-captures",1)==0);
    CHECK(test_temp_path(other,sizeof other,"screen-captures-other",1)==0);
    capture_directory(directory);portable_screen_capture_reset();
    uint32_t pixel=UINT32_C(0xff112233);const PortableSoftwareFrame frame={&pixel,1u,1u,NULL,1u,0u,0u,NULL};
    /* Cross both former64 and next128 growth boundaries in one application run. */
    for(unsigned i=0u;i<130u;++i){
        CHECK(snprintf(name,sizeof name,"capture-%03u",i)>0);
        CHECK(portable_screen_capture(&frame,name));
    }
    pixel=UINT32_C(0xff445566);
    for(unsigned i=0u;i<130u;++i){
        CHECK(snprintf(name,sizeof name,"capture-%03u",i)>0);
        CHECK(portable_screen_capture(&frame,name));
        CHECK(snprintf(path,sizeof path,"%s/%s.ppm",directory,name)>0);
        capture_pixel(path,UINT32_C(0xff112233));
    }
    /* Directory changes do not restart process-wide ownership mid-cup. */
    capture_directory(other);CHECK(portable_screen_capture(&frame,"capture-000"));
    CHECK(snprintf(path,sizeof path,"%s/capture-000.ppm",other)>0&&!test_path_exists(path));
    /* A subsequent shell invocation replaces existing files on its first capture. */
    capture_directory(directory);portable_screen_capture_reset();
    CHECK(portable_screen_capture(&frame,"capture-000"));
    CHECK(snprintf(path,sizeof path,"%s/capture-000.ppm",directory)>0);
    capture_pixel(path,pixel);
    pixel=UINT32_C(0xff778899);CHECK(portable_screen_capture(&frame,"capture-000"));
    capture_pixel(path,UINT32_C(0xff445566));
    portable_screen_capture_reset();portable_screen_capture_reset();
    capture_directory(saved);free(saved);
    for(unsigned i=0u;i<130u;++i){
        CHECK(snprintf(path,sizeof path,"%s/capture-%03u.ppm",directory,i)>0);
        CHECK(remove(path)==0);
    }
    CHECK(test_rmdir(directory)==0&&test_rmdir(other)==0);
}
static void check_ppm_physical_rows(void)
{
    char directory[128],path[256];
    CHECK(test_temp_path(directory,sizeof directory,"ppm-rows",1)==0);
    CHECK(snprintf(path,sizeof path,"%s/frame.ppm",directory)>0);
    for(unsigned scale=1u;scale<=4u;scale*=4u){
        const size_t width=373u*scale,height=2u*scale,stride=width+7u,count=stride*height;
        uint32_t *pixels=malloc(count*sizeof *pixels),*before=malloc(count*sizeof *before);
        CHECK(pixels&&before);
        for(size_t y=0;y<height;++y)for(size_t x=0;x<stride;++x)
            pixels[y*stride+x]=x>=width?UINT32_C(0xdeadbeef):
                ((uint32_t)((x*17u+y*3u)&255u)<<16u)|
                ((uint32_t)((x*7u+y*11u)&255u)<<8u)|(uint32_t)((x+y*31u)&255u)|
                (x&1u?UINT32_C(0xab000000):0u);
        memcpy(before,pixels,count*sizeof *pixels);
        const PortableSoftwareFrame frame={.pixels=pixels,.width=373u,.height=2u,
            .scale=(uint8_t)scale,.stride=stride};
        CHECK(portable_software_frame_write_ppm(&frame,path));
        FILE *file=fopen(path,"rb");CHECK(file!=NULL);
        char header[64],actual[64];const int n=snprintf(header,sizeof header,"P6\n%zu %zu\n255\n",width,height);
        CHECK(n>0&&(size_t)n<sizeof header);
        CHECK(fread(actual,1u,(size_t)n,file)==(size_t)n&&!memcmp(actual,header,(size_t)n));
        /* Independently inspect every output channel, including the row's
         * partial chunk after pixel1024 at4x. Padding/alpha never enter PPM. */
        for(size_t y=0;y<height;++y)for(size_t x=0;x<width;++x){
            CHECK(fgetc(file)==(int)((x*17u+y*3u)&255u));
            CHECK(fgetc(file)==(int)((x*7u+y*11u)&255u));
            CHECK(fgetc(file)==(int)((x+y*31u)&255u));
        }
        CHECK(fgetc(file)==EOF&&!ferror(file)&&fclose(file)==0);
        CHECK(!memcmp(before,pixels,count*sizeof *pixels));
        CHECK(!portable_software_frame_write_ppm(&frame,directory));
        free(before);free(pixels);
    }
    CHECK(!portable_software_frame_write_ppm(NULL,path));
    CHECK(remove(path)==0&&test_rmdir(directory)==0);
}
static uint8_t *open_pack(const char *root,const char *relative,PortableAssetPackView *pack)
{
    char path[4096];CHECK(snprintf(path,sizeof path,"%s/%s",root,relative)>0);
    FILE *file=fopen(path,"rb");CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1u,(size_t)length,file)==(size_t)length&&fclose(file)==0);
    CHECK(portable_asset_pack_open(bytes,(size_t)length,pack)==PORTABLE_ASSET_PACK_OK);return bytes;
}
static void same_overrides(const PortableAssetPackView *effective,const PortableAssetPackView *expected)
{
    for(uint32_t i=0u;i<expected->section_count;++i){
        PortableAssetSectionView wanted,actual;
        CHECK(portable_asset_pack_section_at(expected,i,&wanted)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_asset_pack_find_section(effective,wanted.kind,wanted.content_id,&actual)==PORTABLE_ASSET_PACK_OK);
        CHECK(wanted.version==actual.version&&wanted.flags==actual.flags&&wanted.size==actual.size);
        CHECK(memcmp(wanted.bytes,actual.bytes,wanted.size)==0);
    }
}
static void check_motion_override(const PortableAssetPackView *base,uint32_t version)
{
    PortableAssetSectionView source;
    CHECK(portable_asset_pack_find_section(base,PORTABLE_ASSET_SECTION_MOTION_TABLES,1u,&source)==PORTABLE_ASSET_PACK_OK);
    CHECK(source.version==6u);
    const size_t tail=version==8u?428u:16u;
    uint8_t *bytes=malloc(source.size+tail);CHECK(bytes!=NULL);
    for(size_t i=16u;i<tail;++i)bytes[source.size+i]=(uint8_t)(i*7u+19u);
    memcpy(bytes,source.bytes,source.size);
    for(unsigned i=0u;i<8u;++i){
        const uint16_t value=(uint16_t)(-3-(int)i*9);
        bytes[source.size+i*2u]=(uint8_t)value;bytes[source.size+i*2u+1u]=(uint8_t)(value>>8u);
    }
    PortableAssetSectionView *sections=calloc(base->section_count,sizeof *sections);CHECK(sections!=NULL);
    for(uint32_t i=0u;i<base->section_count;++i){
        CHECK(portable_asset_pack_section_at(base,i,&sections[i])==PORTABLE_ASSET_PACK_OK);
        if(sections[i].kind==PORTABLE_ASSET_SECTION_MOTION_TABLES && sections[i].content_id==1u){
            sections[i].version=version;sections[i].bytes=bytes;sections[i].size=source.size+tail;
        }
    }
    PortableAssetPackView layered=*base;layered.resolved_sections=sections;
    const char json[]="{\"engine_speed_percent\":{\"150\":75},\"kart_physics\":[{\"id\":1,\"time_trial\":{\"base_speed_q16\":123456}}]}";
    const PortableAssetSectionView request={.kind=PORTABLE_ASSET_SECTION_GAME_DATA_JSON,
        .content_id=1u,.version=1u,.bytes=(const uint8_t *)json,.size=sizeof json-1u};
    char error[PORTABLE_MOD_ERROR_CAPACITY];
    PortableGameDataOverlay *changed=portable_game_data_apply(&layered,&request,error);
    if(changed==NULL)fprintf(stderr,"motion v7 override: %s\n",error);
    CHECK(changed!=NULL);
    PortableAssetSectionView actual;
    CHECK(portable_asset_pack_find_section(portable_game_data_pack(changed),PORTABLE_ASSET_SECTION_MOTION_TABLES,1u,&actual)==PORTABLE_ASSET_PACK_OK);
    CHECK(actual.version==version && actual.size==source.size+tail &&
        !memcmp(actual.bytes+source.size,bytes+source.size,tail));
    CHECK(le32(actual.bytes+38056u)==123456u && le32(actual.bytes+38108u+2u*452u+416u)==825u*65536u &&
        actual.bytes[38108u+2u*452u+128u]==90u);
    portable_game_data_free(changed);
    bytes[source.size]=1u;bytes[source.size+1u]=0u;
    CHECK(portable_game_data_apply(&layered,&request,error)==NULL && strstr(error,"deceleration"));
    free(sections);free(bytes);
}

static void projection_word(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (8 * i));
}
static void projection_write(const char *path, const void *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    CHECK(file);
    CHECK(fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
}
static void json_projection_mod(const char *root, const char *id, const char *targets,
                                const char *json, int create)
{
    char path[512], manifest[512];
    snprintf(path, sizeof path, "%s/%s", root, id);
    if (create)
    {
#ifdef _WIN32
        CHECK(_mkdir(path) == 0);
#else
        CHECK(mkdir(path, 0700) == 0);
#endif
    }
    const int length =
        snprintf(manifest, sizeof manifest,
                 "{\"id\":\"%s\",\"name\":\"Projection\",\"version\":\"1.0.0\",\"api_version\":1,"
                 "\"pack\":\"content.pack\",\"script\":\"main.lua\",\"targets\":%s}",
                 id, targets);
    CHECK(length > 0 && (size_t)length < sizeof manifest);
    snprintf(path, sizeof path, "%s/%s/mod.json", root, id);
    projection_write(path, manifest, (size_t)length);
    snprintf(path, sizeof path, "%s/%s/main.lua", root, id);
    char script[128];
    const int script_size = snprintf(script, sizeof script, "--%s", id);
    CHECK(script_size > 0 && (size_t)script_size < sizeof script);
    projection_write(path, script, (size_t)script_size);
    const size_t json_size = strlen(json), total = 152 + json_size;
    uint8_t *bytes = calloc(1, total);
    CHECK(bytes);
    memcpy(bytes, "MKSCPACK", 8);
    bytes[8] = 1;
    bytes[10] = 112;
    projection_word(bytes + 12, 0x12345678);
    projection_word(bytes + 16, (uint32_t)total);
    projection_word(bytes + 24, 1);
    projection_word(bytes + 28, 40);
    projection_word(bytes + 32, 112);
    projection_word(bytes + 112, PORTABLE_ASSET_SECTION_GAME_DATA_JSON);
    projection_word(bytes + 116, 1);
    projection_word(bytes + 120, 1);
    projection_word(bytes + 124, 3);
    projection_word(bytes + 128, 152);
    projection_word(bytes + 136, (uint32_t)json_size);
    memcpy(bytes + 152, json, json_size);
    const uint8_t *parts[] = {bytes + 112, bytes + 152};
    const size_t sizes[] = {40, json_size};
    portable_asset_pack_sha256(parts, sizes, 2, bytes + 72);
    const uint8_t *whole = bytes;
    portable_asset_pack_sha256(&whole, &total, 1, bytes + 40);
    snprintf(path, sizeof path, "%s/%s/content.pack", root, id);
    projection_write(path, bytes, total);
    free(bytes);
}
static PortableAssetSectionView projection_motion(const PortableAssetPackView *pack)
{
    PortableAssetSectionView motion;
    CHECK(portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_MOTION_TABLES, 1,
                                           &motion) == PORTABLE_ASSET_PACK_OK);
    CHECK(motion.version == 6 && motion.size >= 38108 + 3 * 452);
    return motion;
}
static void projection_speeds(const PortableAssetPackView *pack, const PortableAssetPackView *base,
                              unsigned percent50, unsigned percent150)
{
    const PortableAssetSectionView actual = projection_motion(pack),
                                   source = projection_motion(base);
    CHECK(le32(actual.bytes + 38108 + 416) ==
          (uint64_t)le32(source.bytes + 38108 + 416) * percent50 / 100);
    CHECK(le32(actual.bytes + 38108 + 2 * 452 + 416) ==
          (uint64_t)le32(source.bytes + 38108 + 2 * 452 + 416) * percent150 / 100);
    CHECK(actual.bytes[38108 + 128] == (unsigned)source.bytes[38108 + 128] * percent50 / 100);
    CHECK(actual.bytes[38108 + 2 * 452 + 128] ==
          (unsigned)source.bytes[38108 + 2 * 452 + 128] * percent150 / 100);
}
static PortableGameDataOverlay *projection_base(const PortableAssetPackView *base, const char *json,
                                                PortableAssetSectionView *storage, size_t capacity,
                                                PortableAssetPackView *out)
{
    const PortableAssetSectionView request = {.kind = PORTABLE_ASSET_SECTION_GAME_DATA_JSON,
                                              .content_id = 1,
                                              .version = 1,
                                              .bytes = (const uint8_t *)json,
                                              .size = strlen(json)};
    char error[PORTABLE_MOD_ERROR_CAPACITY];
    PortableGameDataOverlay *owner = portable_game_data_apply(base, &request, error);
    CHECK(owner);
    const PortableAssetPackView *layers[] = {base, portable_game_data_pack(owner)};
    CHECK(portable_asset_pack_compose(layers, 2, storage, capacity, out) == PORTABLE_ASSET_PACK_OK);
    return owner;
}
static void independent_json_projections(const PortableAssetPackView *base)
{
    char root[128], path[512];
    CHECK(test_temp_path(root, sizeof root, "json-projections", 1) == 0);
    json_projection_mod(root, "two", "[\"2d\"]", "{\"engine_speed_percent\":{\"50\":50}}", 1);
    json_projection_mod(root, "three", "[\"3d\"]", "{\"engine_speed_percent\":{\"50\":40}}", 1);
    json_projection_mod(root, "dual", "[\"2d\",\"3d\"]", "{\"engine_speed_percent\":{\"150\":75}}",
                        1);
    json_projection_mod(root, "bad", "[\"2d\",\"3d\"]", "{\"engine_speed_percent\":{\"150\":150}}",
                        1);
    const char chain[][PORTABLE_MOD_ID_CAPACITY] = {"two", "dual"};
    const char reverse[][PORTABLE_MOD_ID_CAPACITY] = {"three", "dual"};
    PortableModLoadReport report;
    PortableModSet *a = portable_mods_load_domains_strict(base, base, root, chain, 2, &report);
    CHECK(a);
    projection_speeds(portable_mods_pack(a), base, 50, 75);
    projection_speeds(portable_mods_pack_3d(a), base, 100, 75);
    json_projection_mod(root, "two", "[\"2d\"]", "{\"engine_speed_percent\":{\"50\":60}}", 0);
    PortableModSet *b = portable_mods_load_domains_strict(base, base, root, chain, 2, &report);
    CHECK(b);
    projection_speeds(portable_mods_pack(b), base, 60, 75);
    projection_speeds(portable_mods_pack_3d(b), base, 100, 75);
    CHECK(!portable_mod_identity_equal(portable_mods_identity(a), portable_mods_identity(b)));
    CHECK(portable_mod_identity_equal(portable_mods_identity_3d(a), portable_mods_identity_3d(b)));
    CHECK(!memcmp(portable_mods_pack_3d(a)->deterministic_digest,
                  portable_mods_pack_3d(b)->deterministic_digest, 32));
    portable_mods_free(a);
    portable_mods_free(b);
    a = portable_mods_load_domains_strict(base, base, root, reverse, 2, &report);
    CHECK(a);
    projection_speeds(portable_mods_pack(a), base, 100, 75);
    projection_speeds(portable_mods_pack_3d(a), base, 40, 75);
    json_projection_mod(root, "three", "[\"3d\"]", "{\"engine_speed_percent\":{\"50\":30}}", 0);
    b = portable_mods_load_domains_strict(base, base, root, reverse, 2, &report);
    CHECK(b);
    projection_speeds(portable_mods_pack(b), base, 100, 75);
    projection_speeds(portable_mods_pack_3d(b), base, 30, 75);
    CHECK(portable_mod_identity_equal(portable_mods_identity(a), portable_mods_identity(b)));
    CHECK(!portable_mod_identity_equal(portable_mods_identity_3d(a), portable_mods_identity_3d(b)));
    CHECK(!memcmp(portable_mods_pack(a)->deterministic_digest,
                  portable_mods_pack(b)->deterministic_digest, 32));
    portable_mods_free(a);
    portable_mods_free(b);
    const size_t capacity = base->section_count + 16;
    PortableAssetSectionView *storage = calloc(capacity, sizeof *storage);
    CHECK(storage);
    PortableAssetPackView distinct;
    PortableGameDataOverlay *owner = projection_base(base, "{\"engine_speed_percent\":{\"50\":80}}",
                                                     storage, capacity, &distinct);
    a = portable_mods_load_domains_strict(base, &distinct, root, chain, 2, &report);
    CHECK(a);
    projection_speeds(portable_mods_pack(a), base, 60, 75);
    projection_speeds(portable_mods_pack_3d(a), &distinct, 100, 75);
    portable_mods_free(a);
    portable_game_data_free(owner);
    /* One domain can normalize successfully while the other's valid1500
     * source-unit coefficient exceeds its2016 cap at150percent.
     * Neither partial content nor the rejected mod's script may publish. */
    owner = projection_base(
        base,
        "{\"kart_physics\":[{\"id\":1,\"engines\":[{\"id\":2,\"base_speed_q16\":98304000}]}]}",
        storage, capacity, &distinct);
    const char failed[][PORTABLE_MOD_ID_CAPACITY] = {"two", "bad", "dual"};
    for (unsigned high = 0; high < 2; ++high)
    {
        const PortableAssetPackView *left = high ? base : &distinct,
                                    *right = high ? &distinct : base;
        a = portable_mods_load_domains_report(left, right, root, failed, 3, &report);
        CHECK(a);
        CHECK(portable_mods_loaded_count(a) == 2 && report.entries[0].accepted &&
              !report.entries[1].accepted && report.entries[2].accepted && report.first_error[0]);
        b = portable_mods_load_domains_strict(left, right, root, chain, 2, &report);
        CHECK(b);
        projection_speeds(portable_mods_pack(a), left, 60, 75);
        projection_speeds(portable_mods_pack_3d(a), right, 100, 75);
        const PortableAssetSectionView am = projection_motion(portable_mods_pack(a));
        const PortableAssetSectionView bm = projection_motion(portable_mods_pack(b));
        CHECK(am.size == bm.size && !memcmp(am.bytes, bm.bytes, am.size));
        CHECK(portable_mod_identity_equal(portable_mods_identity(a), portable_mods_identity(b)) &&
              portable_mod_identity_equal(portable_mods_identity_3d(a),
                                          portable_mods_identity_3d(b)));
        for (size_t i = 0; i < portable_mods_loaded_count(a); ++i)
        {
            const char *id, *script;
            size_t size;
            if (portable_mods_script(a, i, &id, &script, &size))
                CHECK(strcmp(id, "bad") && size == strlen(id) + 2 && !memcmp(script, "--", 2) &&
                      !memcmp(script + 2, id, size - 2));
            if (portable_mods_script_3d(a, i, &id, &script, &size))
                CHECK(strcmp(id, "bad") && size == strlen(id) + 2 && !memcmp(script, "--", 2) &&
                      !memcmp(script + 2, id, size - 2));
        }
        CHECK(!portable_mods_load_domains_strict(left, right, root, failed, 3, &report));
        portable_mods_free(a);
        portable_mods_free(b);
    }
    portable_game_data_free(owner);
    free(storage);
    const char *ids[] = {"two", "three", "dual", "bad"};
    for (unsigned i = 0; i < 4; ++i)
    {
        snprintf(path, sizeof path, "%s/%s/mod.json", root, ids[i]);
        CHECK(remove(path) == 0);
        snprintf(path, sizeof path, "%s/%s/content.pack", root, ids[i]);
        CHECK(remove(path) == 0);
        snprintf(path, sizeof path, "%s/%s/main.lua", root, ids[i]);
        CHECK(remove(path) == 0);
        snprintf(path, sizeof path, "%s/%s", root, ids[i]);
        CHECK(test_rmdir(path) == 0);
    }
    CHECK(test_rmdir(root) == 0);
}
int main(int argc,char **argv)
{
    check_screen_capture_lifecycle();
    check_ppm_physical_rows();
    CHECK(argc==2);PortableAssetPackView base,compiled,runtime;
    uint8_t *base_bytes=open_pack(argv[1],"base.pack",&base);
    independent_json_projections(&base);
    check_motion_override(&base,7u);
    check_motion_override(&base,8u);
    uint8_t *compiled_bytes=open_pack(argv[1],"compiled/content.pack",&compiled);
    uint8_t *runtime_bytes=open_pack(argv[1],"mods/a/content.pack",&runtime);
    uint8_t *preserved=malloc(base.size);CHECK(preserved!=NULL);memcpy(preserved,base_bytes,base.size);
    PortableAssetSectionView json;char error[PORTABLE_MOD_ERROR_CAPACITY];
    CHECK(portable_asset_pack_find_section(&runtime,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&json)==PORTABLE_ASSET_PACK_OK);
    PortableGameDataOverlay *data=portable_game_data_apply(&base,&json,error);CHECK(data!=NULL);
    same_overrides(portable_game_data_pack(data),&compiled);
    {
        PortableAssetPackView orbit_runtime,orbit_compiled;
        uint8_t *input=open_pack(argv[1],"orbit-runtime/content.pack",&orbit_runtime);
        uint8_t *expected=open_pack(argv[1],"orbit-compiled/content.pack",&orbit_compiled);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&orbit_runtime,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *orbit=portable_game_data_apply(&base,&request,error);CHECK(orbit!=NULL);
        same_overrides(portable_game_data_pack(orbit),&orbit_compiled);
        PortableAssetSectionView rules,material;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(orbit),PORTABLE_ASSET_SECTION_SHELL_ORBIT_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==22u && le32(rules.bytes+12u)==768u && le32(rules.bytes+16u)==4u);
        const uint8_t delays[]={0u,8u,16u,0u,10u,20u};
        for(unsigned slot=0u;slot<2u;++slot){
            CHECK(portable_asset_pack_find_section(portable_game_data_pack(orbit),PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,slot==0u?1u:27u,&material)==PORTABLE_ASSET_PACK_OK);
            CHECK(memcmp(material.bytes+1464u,delays,6u)==0);
        }
        const char partial[]="{\"shell_orbit_rules\":{\"id\":1,\"angle_step\":1024,\"green_delays\":[1,2,3]}}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(orbit),&request,error);CHECK(next!=NULL);
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_SHELL_ORBIT_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==22u && le32(rules.bytes+12u)==1024u && le32(rules.bytes+16u)==4u);
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,1u,&material)==PORTABLE_ASSET_PACK_OK);
        const uint8_t layered[]={1u,2u,3u,0u,10u,20u};CHECK(memcmp(material.bytes+1464u,layered,6u)==0);
        const char *bad[]={
            "{\"shell_orbit_rules\":{\"id\":1}}",
            "{\"shell_orbit_rules\":{\"id\":2,\"angle_step\":1}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"radius_multiplier\":0}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"radius_multiplier\":256}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"angle_step\":65536}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"animation_period\":0}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"green_delays\":[0,1]}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"red_delays\":[0,1,2,3]}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"red_delays\":[0,1,256]}}",
            "{\"shell_orbit_rules\":{\"id\":1,\"angle_step\":1,\"angle_step\":2}}"
        };
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL && strstr(error,"shell_orbit_rules")!=NULL);}
        CHECK(memcmp(base_bytes,preserved,base.size)==0);
        portable_game_data_free(next);portable_game_data_free(orbit);free(input);free(expected);
    }
    {
        PortableAssetPackView strings_runtime,strings_compiled;
        uint8_t *input=open_pack(argv[1],"strings-runtime/content.pack",&strings_runtime);
        uint8_t *expected=open_pack(argv[1],"strings-compiled/content.pack",&strings_compiled);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&strings_runtime,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *strings=portable_game_data_apply(&base,&request,error);CHECK(strings!=NULL);
        same_overrides(portable_game_data_pack(strings),&strings_compiled);
        PortableUiTextBank bank;
        CHECK(portable_ui_text_load(portable_game_data_pack(strings),&bank));
        CHECK(strcmp(portable_ui_text_resolve(&bank,"OPTIONS"),"SETUP")==0);
        CHECK(strcmp(portable_ui_text_resolve(&bank,"BATTLE"),"DUEL")==0);
        CHECK(strcmp(portable_ui_text_resolve(&bank,"RACE TIME"),"RACE TIME")==0);
        uint32_t pixels[64u*8u]={0},wanted[64u*8u]={0};
        PortableSoftwareFrame frame={pixels,64u,8u,&bank,1u,0u,0u,NULL},plain={wanted,64u,8u,NULL,1u,0u,0u,NULL};
        portable_software_frame_text(&frame,0,0,"OPTIONS",0xffffffffu);
        portable_software_frame_text(&plain,0,0,"SETUP",0xffffffffu);
        CHECK(memcmp(pixels,wanted,sizeof pixels)==0);
        const char partial[]="{\"strings\":[{\"id\":88,\"value\":\"TUNE\"}]}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(strings),&request,error);CHECK(next!=NULL);
        const PortableAssetPackView *layers[]={&base,portable_game_data_pack(strings),portable_game_data_pack(next)};
        const size_t capacity=base.section_count+16u;
        PortableAssetSectionView *sections=calloc(capacity,sizeof *sections);CHECK(sections!=NULL);
        PortableAssetPackView merged;
        CHECK(portable_asset_pack_compose(layers,3u,sections,capacity,&merged)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_ui_text_load(&merged,&bank));
        CHECK(strcmp(portable_ui_text_resolve(&bank,"OPTIONS"),"TUNE")==0 &&
            strcmp(portable_ui_text_resolve(&bank,"BATTLE"),"DUEL")==0);
        const char *bad[]={
            "{\"strings\":[{\"id\":0,\"value\":\"A\"}]}",
            "{\"strings\":[{\"id\":999,\"value\":\"A\"}]}",
            "{\"strings\":[{\"id\":88,\"value\":\"TOO LONG\"}]}",
            "{\"strings\":[{\"id\":88,\"value\":\"lower\"}]}",
            "{\"strings\":[{\"id\":88,\"value\":\"%s\"}]}",
            "{\"strings\":[{\"id\":88,\"value\":\"\"}]}",
            "{\"strings\":[{\"id\":88,\"value\":\"A\",\"color_rgb\":0}]}",
            "{\"strings\":[{\"id\":88,\"value\":\"A\"},{\"id\":88,\"value\":\"B\"}]}"
        };
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){
            request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);
            CHECK(portable_game_data_apply(&base,&request,error)==NULL && strstr(error,"strings")!=NULL);
        }
        CHECK(memcmp(base_bytes,preserved,base.size)==0);
        PortableAssetSectionView invalid;CHECK(portable_asset_pack_find_section(&merged,PORTABLE_ASSET_SECTION_UI_TEXT,88u,&invalid)==PORTABLE_ASSET_PACK_OK);
        uint8_t corrupt[67];memcpy(corrupt,invalid.bytes,invalid.size);corrupt[4]=99u;invalid.bytes=corrupt;
        PortableAssetPackView malformed={0};malformed.section_count=1u;malformed.resolved_sections=&invalid;
        CHECK(!portable_ui_text_load(&malformed,&bank));
        CHECK(strcmp(portable_ui_text_resolve(&bank,"OPTIONS"),"TUNE")==0);
        free(sections);portable_game_data_free(next);portable_game_data_free(strings);free(input);free(expected);
    }
    {
        PortableAssetPackView text_runtime,text_compiled;
        uint8_t *input=open_pack(argv[1],"text-runtime/content.pack",&text_runtime);
        uint8_t *expected=open_pack(argv[1],"text-compiled/content.pack",&text_compiled);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&text_runtime,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *text=portable_game_data_apply(&base,&request,error);CHECK(text!=NULL);
        same_overrides(portable_game_data_pack(text),&text_compiled);
        PortableAssetSectionView image,original;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(text),PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,0x21100u,&image)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_asset_pack_find_section(&base,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,0x21100u,&original)==PORTABLE_ASSET_PACK_OK);
        CHECK(image.size==original.size&&memcmp(image.bytes,original.bytes,32u)==0&&memcmp(image.bytes+32u,original.bytes+32u,image.size-32u)!=0);
        const char replace[]="{\"text\":[{\"id\":135424,\"value\":\"RACE\"}]}";
        request.bytes=(const uint8_t *)replace;request.size=sizeof replace-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(text),&request,error);CHECK(next!=NULL);
        PortableAssetSectionView changed;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,0x21100u,&changed)==PORTABLE_ASSET_PACK_OK);
        CHECK(memcmp(changed.bytes+32u,image.bytes+32u,image.size-32u)!=0);
        portable_game_data_free(next);
        const char *bad[]={
            "{\"text\":[{\"id\":1,\"value\":\"RACE\"}]}",
            "{\"text\":[{\"id\":135424,\"value\":\"lowercase\"}]}",
            "{\"text\":[{\"id\":135424,\"value\":\"ABCDEFGHIJKLMNOPQ\"}]}",
            "{\"text\":[{\"id\":135424,\"value\":\"\"}]}",
            "{\"text\":[{\"id\":135424,\"value\":\"A\",\"color_rgb\":16777216}]}",
            "{\"text\":[{\"id\":135424,\"value\":\"A\"},{\"id\":135424,\"value\":\"B\"}]}",
            "{\"text\":[{\"id\":135424,\"value\":\"A\",\"value\":\"B\"}]}"
        };
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"text")!=NULL);}
        CHECK(memcmp(base_bytes,preserved,base.size)==0);
        portable_game_data_free(text);free(input);free(expected);
    }
    {
        PortableAssetPackView runtime_status,compiled_status;
        uint8_t *input=open_pack(argv[1],"status-runtime/content.pack",&runtime_status);
        uint8_t *expected=open_pack(argv[1],"status-compiled/content.pack",&compiled_status);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&runtime_status,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *status=portable_game_data_apply(&base,&request,error);CHECK(status!=NULL);
        same_overrides(portable_game_data_pack(status),&compiled_status);
        const char partial[]="{\"item_status_rules\":{\"id\":1,\"star_cpu_ticks\":2}}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(status),&request,error);CHECK(next!=NULL);
        PortableAssetSectionView rules;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_ITEM_STATUS_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==90u&&le32(rules.bytes+12u)==2u&&le32(rules.bytes+16u)==120u);
        portable_game_data_free(next);portable_game_data_free(status);free(input);free(expected);
        const char *bad[]={"{\"item_status_rules\":{\"id\":1}}","{\"item_status_rules\":{\"id\":2,\"star_player_ticks\":1}}","{\"item_status_rules\":{\"id\":1,\"star_player_ticks\":0}}","{\"item_status_rules\":{\"id\":1,\"star_cpu_ticks\":32768}}","{\"item_status_rules\":{\"id\":1,\"lightning_ticks\":47}}","{\"item_status_rules\":{\"id\":1,\"lightning_ticks\":120,\"lightning_ticks\":48}}"};
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"item_status_rules")!=NULL);}
    }
    {
        PortableAssetPackView runtime_boo,compiled_boo;
        uint8_t *input=open_pack(argv[1],"boo-runtime/content.pack",&runtime_boo);
        uint8_t *expected=open_pack(argv[1],"boo-compiled/content.pack",&compiled_boo);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&runtime_boo,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *boo=portable_game_data_apply(&base,&request,error);CHECK(boo!=NULL);
        same_overrides(portable_game_data_pack(boo),&compiled_boo);
        const char partial[]="{\"boo_rules\":{\"id\":1,\"lock_ticks\":2}}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(boo),&request,error);CHECK(next!=NULL);
        PortableAssetSectionView rules;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_BOO_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==90u&&le32(rules.bytes+12u)==2u&&le32(rules.bytes+16u)==16u&&le32(rules.bytes+32u)==16384u);
        portable_game_data_free(next);portable_game_data_free(boo);free(input);free(expected);
        const char *bad[]={"{\"boo_rules\":{\"id\":1}}","{\"boo_rules\":{\"id\":2,\"lock_ticks\":1}}","{\"boo_rules\":{\"id\":1,\"lock_ticks\":0}}","{\"boo_rules\":{\"id\":1,\"invisibility_ticks\":32768}}","{\"boo_rules\":{\"id\":1,\"orbit_ticks\":256}}","{\"boo_rules\":{\"id\":1,\"orbit_step\":32768}}","{\"boo_rules\":{\"id\":1,\"repel_gravity_q16\":16777216}}","{\"boo_rules\":{\"id\":1,\"lock_ticks\":120,\"lock_ticks\":48}}"};
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"boo_rules")!=NULL);}
    }
    {
        PortableAssetPackView runtime_green,compiled_green;
        uint8_t *input=open_pack(argv[1],"green-runtime/content.pack",&runtime_green);
        uint8_t *expected=open_pack(argv[1],"green-compiled/content.pack",&compiled_green);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&runtime_green,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *green=portable_game_data_apply(&base,&request,error);CHECK(green!=NULL);
        same_overrides(portable_game_data_pack(green),&compiled_green);
        const char partial[]="{\"green_shell_rules\":{\"id\":1,\"collision_grace_ticks\":0,\"launch_speed_bonus\":0}}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(green),&request,error);CHECK(next!=NULL);
        PortableAssetSectionView rules;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_GREEN_SHELL_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==0u&&le32(rules.bytes+12u)==60u&&le32(rules.bytes+16u)==0u&&le32(rules.bytes+36u)==12288u);
        portable_game_data_free(next);portable_game_data_free(green);free(input);free(expected);
        const char *bad[]={"{\"green_shell_rules\":{\"id\":1}}","{\"green_shell_rules\":{\"id\":2,\"max_speed\":1}}","{\"green_shell_rules\":{\"id\":1,\"max_speed\":0}}","{\"green_shell_rules\":{\"id\":1,\"max_speed\":256}}","{\"green_shell_rules\":{\"id\":1,\"wall_contact_budget\":8}}","{\"green_shell_rules\":{\"id\":1,\"release_lock_ticks\":32768}}","{\"green_shell_rules\":{\"id\":1,\"collision_grace_ticks\":65536}}","{\"green_shell_rules\":{\"id\":1,\"destruction_gravity_q16\":16777216}}","{\"green_shell_rules\":{\"id\":1,\"cpu_hold_ticks\":120,\"cpu_hold_ticks\":48}}"};
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"green_shell_rules")!=NULL);}
    }
    {
        PortableAssetPackView runtime_red,compiled_red;
        uint8_t *input=open_pack(argv[1],"red-runtime/content.pack",&runtime_red);
        uint8_t *expected=open_pack(argv[1],"red-compiled/content.pack",&compiled_red);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&runtime_red,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *red=portable_game_data_apply(&base,&request,error);CHECK(red!=NULL);
        same_overrides(portable_game_data_pack(red),&compiled_red);
        const char partial[]="{\"red_shell_rules\":{\"id\":1,\"collision_grace_ticks\":0,\"launch_speed_bonus\":0}}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(red),&request,error);CHECK(next!=NULL);
        PortableAssetSectionView rules;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_RED_SHELL_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==0u&&le32(rules.bytes+12u)==60u&&le32(rules.bytes+16u)==0u&&le32(rules.bytes+68u)==12288u);
        portable_game_data_free(next);portable_game_data_free(red);free(input);free(expected);
        const char *bad[]={"{\"red_shell_rules\":{\"id\":1,\"trap_arm_ticks\":8}}","{\"red_shell_rules\":{\"id\":1}}","{\"red_shell_rules\":{\"id\":2,\"max_launch_speed\":1}}","{\"red_shell_rules\":{\"id\":1,\"max_launch_speed\":0}}","{\"red_shell_rules\":{\"id\":1,\"max_launch_speed\":256}}","{\"red_shell_rules\":{\"id\":1,\"route_budget\":256}}","{\"red_shell_rules\":{\"id\":1,\"release_lock_ticks\":32768}}","{\"red_shell_rules\":{\"id\":1,\"collision_grace_ticks\":65536}}","{\"red_shell_rules\":{\"id\":1,\"destruction_gravity_q16\":16777216}}","{\"red_shell_rules\":{\"id\":1,\"cpu_hold_ticks\":120,\"cpu_hold_ticks\":48}}"};
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"red_shell_rules")!=NULL);}
    }
    {
        PortableAssetPackView runtime_banana,compiled_banana;
        uint8_t *input=open_pack(argv[1],"banana-runtime/content.pack",&runtime_banana);
        uint8_t *expected=open_pack(argv[1],"banana-compiled/content.pack",&compiled_banana);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&runtime_banana,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *banana=portable_game_data_apply(&base,&request,error);CHECK(banana!=NULL);
        same_overrides(portable_game_data_pack(banana),&compiled_banana);
        const char partial[]="{\"banana_rules\": {\"id\": 1, \"collision_grace_ticks\": 0}}";
        request.bytes=(const uint8_t *)partial;request.size=sizeof partial-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(banana),&request,error);CHECK(next!=NULL);
        PortableAssetSectionView rules;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_BANANA_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(rules.bytes+8u)==262144u&&le32(rules.bytes+24u)==0u&&le32(rules.bytes+40u)==16384u);
        portable_game_data_free(next);portable_game_data_free(banana);free(input);free(expected);
        const char *bad[]={"{\"banana_rules\": {\"id\": 1, \"release_lock_ticks\": 1000, \"throw_velocity_q16\": 16777215, \"throw_gravity_q16\": 1}}","{\"banana_rules\": {\"id\": 1}}","{\"banana_rules\": {\"id\": 2, \"cpu_hold_ticks\": 60}}","{\"banana_rules\": {\"id\": 1, \"cpu_hold_ticks\": 0}}","{\"banana_rules\": {\"id\": 1, \"cpu_hold_ticks\": 32768}}","{\"banana_rules\": {\"id\": 1, \"collision_grace_ticks\": 65536}}","{\"banana_rules\": {\"id\": 1, \"throw_speed_q16\": 16777216}}","{\"banana_rules\": {\"id\": 1, \"throw_gravity_q16\": 16777215}}","{\"banana_rules\": {\"id\": 1, \"release_lock_ticks\": 32767}}","{\"banana_rules\":{\"id\":1,\"cpu_hold_ticks\":120,\"cpu_hold_ticks\":60}}"};
        for(unsigned j=0u;j<sizeof bad/sizeof bad[0];++j){request.bytes=(const uint8_t *)bad[j];request.size=strlen(bad[j]);CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"banana_rules")!=NULL);}
    }
    /* Scoring overrides preserve imported quality rules byte for byte. */
    {
        uint8_t bytes[112]={0x47,0x50,0x54,0x31,8};
        for(unsigned i=0u;i<8u;++i)bytes[8u+i]=2u;
        bytes[16]=2u;
        for(unsigned i=20u;i<sizeof bytes;++i)bytes[i]=(uint8_t)(i*3u);
        PortableAssetSectionView source={.kind=PORTABLE_ASSET_SECTION_GP_SCORING,
            .content_id=1u,.version=3u,.size=sizeof bytes,.bytes=bytes};
        PortableAssetPackView original={.section_count=1u,.resolved_sections=&source};
        const char text[]={"{\"gp_rules\":{\"id\":1,\"retry_credits\":1},\"gp_points\":{\"id\":1,\"awards\":[1,1,1,1,1,1,1,1]}}"};
        PortableAssetSectionView request=json;
        request.bytes=(const uint8_t *)text;request.size=sizeof text-1u;
        PortableGameDataOverlay *changed=portable_game_data_apply(&original,&request,error);
        CHECK(changed!=NULL);
        PortableAssetSectionView actual;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(changed),PORTABLE_ASSET_SECTION_GP_SCORING,1u,&actual)==PORTABLE_ASSET_PACK_OK);
        CHECK(actual.version==3u && actual.size==112u && actual.bytes[8]==1u &&
              le32(actual.bytes+16u)==1u && memcmp(actual.bytes+20u,bytes+20u,92u)==0);
        portable_game_data_free(changed);
    }
    /* Retry-only rules upgrade v1 without changing awards; points-only
     * layers on v2 retain credits, and malformed input never publishes. */
    {
        const char request[]="{\"gp_rules\":{\"id\":1,\"retry_credits\":0}}";
        PortableAssetSectionView retry_json=json;retry_json.bytes=(const uint8_t *)request;retry_json.size=sizeof request-1u;
        PortableGameDataOverlay *retry=portable_game_data_apply(&base,&retry_json,error);CHECK(retry!=NULL);
        PortableAssetSectionView scoring,original;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(retry),PORTABLE_ASSET_SECTION_GP_SCORING,1u,&scoring)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_asset_pack_find_section(&base,PORTABLE_ASSET_SECTION_GP_SCORING,1u,&original)==PORTABLE_ASSET_PACK_OK);
        CHECK(scoring.version==2u&&scoring.size==20u&&le32(scoring.bytes+16u)==0u&&memcmp(scoring.bytes,original.bytes,16u)==0);
        const char points[]="{\"gp_points\":{\"id\":1,\"awards\":[1,1,1,1,1,1,1,1]}}";
        retry_json.bytes=(const uint8_t *)points;retry_json.size=sizeof points-1u;
        PortableGameDataOverlay *next=portable_game_data_apply(portable_game_data_pack(retry),&retry_json,error);CHECK(next!=NULL);
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(next),PORTABLE_ASSET_SECTION_GP_SCORING,1u,&scoring)==PORTABLE_ASSET_PACK_OK);
        CHECK(scoring.bytes[8u]==1u&&le32(scoring.bytes+16u)==0u);portable_game_data_free(next);
        const char *invalid[]={"{\"gp_rules\":{\"id\":1,\"retry_credits\":4}}","{\"gp_rules\":{\"id\":2,\"retry_credits\":0}}", "{\"gp_rules\":{\"id\":1}}", "{\"gp_rules\":{\"retry_credits\":0}}", "{\"gp_rules\":{\"id\":1,\"retry_credits\":-1}}", "{\"gp_rules\":{\"id\":1,\"retry_credits\":0,\"retry_credits\":1}}"};
        for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){retry_json.bytes=(const uint8_t *)invalid[i];retry_json.size=strlen(invalid[i]);CHECK(portable_game_data_apply(&base,&retry_json,error)==NULL&&strstr(error,"gp_rules")!=NULL);}
        portable_game_data_free(retry);
    }
    {
        PortableAssetSectionView rules;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(data),PORTABLE_ASSET_SECTION_MUSHROOM_RULES,1u,&rules)==PORTABLE_ASSET_PACK_OK);
        CHECK(rules.size==28u&&le32(rules.bytes+8u)==75u&&le32(rules.bytes+12u)==12u&&le32(rules.bytes+16u)==120u&&le32(rules.bytes+20u)==40u&&le32(rules.bytes+24u)==117440512u);
        const char *invalid[]={"{\"mushroom_rules\":{\"id\":1}}","{\"mushroom_rules\":{\"id\":2,\"boost_ticks\":1}}","{\"mushroom_rules\":{\"id\":1,\"boost_ticks\":0}}","{\"mushroom_rules\":{\"id\":1,\"golden_actor_ticks\":0}}","{\"mushroom_rules\":{\"id\":1,\"single_actor_ticks\":65536}}","{\"mushroom_rules\":{\"id\":1,\"acceleration_units\":32768}}","{\"mushroom_rules\":{\"id\":1,\"max_speed_q16\":132120577}}","{\"mushroom_rules\":{\"id\":1,\"boost_ticks\":1,\"boost_ticks\":2}}"};
        for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){PortableAssetSectionView bad=json;bad.bytes=(const uint8_t *)invalid[i];bad.size=strlen(invalid[i]);CHECK(portable_game_data_apply(&base,&bad,error)==NULL&&strstr(error,"mushroom_rules")!=NULL);}
    }
    {
        PortableAssetPackView runtime_cross,compiled_cross;
        uint8_t *cross_bytes=open_pack(argv[1],"cross-runtime/content.pack",&runtime_cross);
        uint8_t *expected_bytes=open_pack(argv[1],"cross-compiled/content.pack",&compiled_cross);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&runtime_cross,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        /* A preceding within-cup reorder must not lose the art's course identity. */
        const PortableAssetPackView *layers[]={&base,portable_game_data_pack(data)};
        const size_t capacity=base.section_count+portable_game_data_pack(data)->section_count;
        PortableAssetSectionView *rows=calloc(capacity,sizeof *rows);CHECK(rows!=NULL);
        PortableAssetPackView preceding;
        CHECK(portable_asset_pack_compose(layers,2u,rows,capacity,&preceding)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *cross=portable_game_data_apply(&preceding,&request,error);CHECK(cross!=NULL);
        same_overrides(portable_game_data_pack(cross),&compiled_cross);
        PortableAssetSectionView image,catalog;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(cross),PORTABLE_ASSET_SECTION_COURSE_CATALOG,1u,&catalog)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(catalog.bytes+8u)==32u&&catalog.bytes[12u]==0u&&le32(catalog.bytes+16u)==12u);
        CHECK(le32(catalog.bytes+56u)==24u&&catalog.bytes[60u]==1u&&le32(catalog.bytes+64u)==4u);
        for(unsigned slot=0u;slot<4u;++slot){
            CHECK(portable_asset_pack_find_section(portable_game_data_pack(cross),PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,0x23300u+slot,&image)==PORTABLE_ASSET_PACK_OK);
            CHECK(image.bytes[0]==(slot==0u?5u:slot+1u));
        }
        const char restore[]="{\"cups\":[{\"catalog\":1,\"cup\":0,\"courses\":[24,25,29,27]},{\"catalog\":1,\"cup\":1,\"courses\":[32,37,38,31]},{\"catalog\":2,\"cup\":0,\"courses\":[64,65,66,67]},{\"catalog\":2,\"cup\":1,\"courses\":[68,69,70,71]}]}";
        request.bytes=(const uint8_t *)restore;request.size=sizeof restore-1u;
        PortableGameDataOverlay *restored=portable_game_data_apply(portable_game_data_pack(cross),&request,error);CHECK(restored!=NULL);
        /* Every restored table/art section must equal the original base. */
        same_overrides(&base,portable_game_data_pack(restored));
        portable_game_data_free(restored);portable_game_data_free(cross);free(rows);free(cross_bytes);free(expected_bytes);
        const char invalid[]="{\"cups\":[{\"catalog\":1,\"cup\":0,\"courses\":[32,25,29,27]}]}";
        request.bytes=(const uint8_t *)invalid;request.size=sizeof invalid-1u;
        CHECK(portable_game_data_apply(&base,&request,error)==NULL&&strstr(error,"update both cups")!=NULL);
    }
    /* CPU rows also work in the earliest v6 material layout. The final
     * variant/class/rank ends exactly at that payload's1440-byte boundary. */
    {
        PortableAssetSectionView old_material;
        CHECK(portable_asset_pack_find_section(&base,PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,27u,&old_material)==PORTABLE_ASSET_PACK_OK);
        old_material.version=6u;old_material.size=1440u;
        PortableAssetPackView old={.section_count=1u,.resolved_sections=&old_material};
        const char request[]="{\"cpu_items\":[{\"course\":27,\"engine\":2,\"variant\":2,\"driver_class\":7,\"items\":[4103,4103,4103,4103,4103,4103,4103,4103]}]}";
        PortableAssetSectionView cpu_json=json;cpu_json.bytes=(const uint8_t *)request;cpu_json.size=sizeof request-1u;
        PortableGameDataOverlay *legacy=portable_game_data_apply(&old,&cpu_json,error);CHECK(legacy!=NULL);
        PortableAssetSectionView result;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(legacy),PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,27u,&result)==PORTABLE_ASSET_PACK_OK);
        CHECK(result.version==6u&&result.size==1440u&&le32(result.bytes+1436u)==UINT32_C(0x10071007));
        CHECK(le32(result.bytes+288u)==0u);portable_game_data_free(legacy);
    }
    /* Exercise every signed table and both engine/profile boundaries against
     * the SDK's compiled output, including INT16_MIN and omitted engine1. */
    {
        PortableAssetPackView physics_runtime,physics_compiled;
        uint8_t *raw=open_pack(argv[1],"physics-runtime/content.pack",&physics_runtime);
        uint8_t *expected=open_pack(argv[1],"physics-compiled/content.pack",&physics_compiled);
        PortableAssetSectionView request;
        CHECK(portable_asset_pack_find_section(&physics_runtime,PORTABLE_ASSET_SECTION_GAME_DATA_JSON,1u,&request)==PORTABLE_ASSET_PACK_OK);
        PortableGameDataOverlay *physics=portable_game_data_apply(&base,&request,error);CHECK(physics!=NULL);
        same_overrides(portable_game_data_pack(physics),&physics_compiled);
        PortableAssetSectionView actual,original;
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(physics),PORTABLE_ASSET_SECTION_MOTION_TABLES,1u,&actual)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_asset_pack_find_section(&base,PORTABLE_ASSET_SECTION_MOTION_TABLES,1u,&original)==PORTABLE_ASSET_PACK_OK);
        CHECK(le32(actual.bytes+4132u)==UINT32_C(0x7fff8000));
        CHECK(le32(actual.bytes+38056u)==1u);
        CHECK(le32(actual.bytes+38108u+416u)==UINT32_C(2016)*65536u);
        CHECK(memcmp(actual.bytes+38108u+452u,original.bytes+38108u+452u,452u)==0);
        CHECK(portable_asset_pack_find_section(portable_game_data_pack(physics),PORTABLE_ASSET_SECTION_MOTION_TABLES,2u,&actual)==PORTABLE_ASSET_PACK_NOT_FOUND);
        portable_game_data_free(physics);free(raw);free(expected);
    }
    const char *invalid[]={
        "{\"kart_physics\":[{\"id\":1}]}",
        "{\"kart_physics\":[{\"id\":1,\"brake\":[-2147483649,0,0,0,0,0,0,0]}]}",
        "{\"kart_physics\":[{\"id\":1,\"brake\":[-01,0,0,0,0,0,0,0]}]}",
        "{\"kart_physics\":[{\"id\":999,\"time_trial\":{\"base_speed_q16\":1}}]}",
        "{\"kart_physics\":[{\"id\":1,\"time_trial\":{\"base_speed_q16\":0}}]}",
        "{\"kart_physics\":[{\"id\":1,\"engines\":[{\"id\":3,\"base_speed_q16\":1}]}]}",
        "{\"kart_physics\":[{\"id\":1,\"engines\":[{\"id\":0,\"base_speed_q16\":1},{\"id\":0,\"base_speed_q16\":2}]}]}",
        "{\"kart_physics\":[{\"id\":1,\"time_trial\":{\"base_speed_q16\":1}},{\"id\":1,\"time_trial\":{\"base_speed_q16\":2}}]}",
        "{\"kart_physics\":[{\"id\":1,\"brake\":[- 1,0,0,0,0,0,0,0]}]}",
        "{\"kart_physics\":[{\"id\":1,\"brake\":[-32769,0,0,0,0,0,0,0]}]}",
        "{\"kart_physics\":[{\"id\":1,\"brake\":[-2147483648,0,0,0,0,0,0,0]}]}",
        "{\"kart_physics\":[{\"id\":1,\"forward_target_response\":[1,0,0,0,0,0,0,0]}]}",
        "{\"cpu_items\":[{\"course\":27,\"engine\":3,\"variant\":0,\"driver_class\":0,\"items\":[4103,4103,4103,4103,4103,4103,4103,4103]}]}",
        "{\"cpu_items\":[{\"course\":27,\"engine\":2,\"variant\":0,\"driver_class\":0,\"items\":[4105,4103,4103,4103,4103,4103,4103,4103]}]}",
        "{\"cpu_items\":[{\"course\":999,\"engine\":2,\"variant\":0,\"driver_class\":0,\"items\":[0,0,0,0,0,0,0,0]}]}",
        "{\"cpu_items\":[{\"course\":27,\"engine\":2,\"variant\":0,\"driver_class\":0,\"items\":[0,0,0,0,0,0,0]}]}",
        "{\"cpu_items\":[{\"course\":27,\"engine\":2,\"variant\":0,\"driver_class\":0,\"items\":[0,0,0,0,0,0,0,0]},{\"course\":27,\"engine\":2,\"variant\":0,\"driver_class\":0,\"items\":[0,0,0,0,0,0,0,0]}]}",
        "{\"gp_points\":{\"id\":2,\"awards\":[9,6,3,1,0,0,0,0]}}",
        "{\"gp_points\":{\"id\":1,\"awards\":[64,6,3,1,0,0,0,0]}}",
        "{\"gp_points\":{\"id\":1,\"awards\":[9,6,3,1,0,0,0]}}",
        "{\"gp_items\":[{\"id\":0,\"weights\":[]}]}",
        "{\"gp_items\":[{\"id\":1,\"weights\":[]}]}",
        "{\"gp_items\":[{\"id\":1,\"weights\":[{\"item\":4105,\"weight\":1}]},{\"id\":1,\"weights\":[{\"item\":4105,\"weight\":1}]}]}","{}","[]","{\"unknown\":1}","{\"cups\":[],\"cups\":[]}",
        "{\"engine_speed_percent\":{\"150\":true}}","{\"engine_speed_percent\":{\"150\":105.5}}",
        "{\"engine_speed_percent\":{\"150\":105,\"150\":100}}","{\"engine_speed_percent\":{\"150\":200}}",
        "{\"battle_items\":[[],[],[]]}","{\"battle_items\":[[{\"item\":4098,\"weight\":1}]]}",
        "{\"battle_items\":[[{\"item\":4098,\"weight\":1}][{\"item\":4098,\"weight\":1}]]}",
        "{\"battle_items\":[[{\"item\":4098,\"weight\":32767},{\"item\":4105,\"weight\":1}]]}",
        "{\"cups\":[{\"catalog\":1,\"cup\":0,\"courses\":[24,25,29,999]}]}",
        "{\"cups\":[{\"catalog\":1,\"cup\":0,\"courses\":[24,25,29,24]}]}",
        "{\"engine_speed_percent\":{\"150\":100}} trailing"};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        PortableAssetSectionView bad=json;bad.bytes=(const uint8_t *)invalid[i];bad.size=strlen(invalid[i]);
        CHECK(portable_game_data_apply(&base,&bad,error)==NULL&&error[0]!='\0');
        CHECK(memcmp(base_bytes,preserved,base.size)==0);
    }
    /* A JSON-only override cannot invent a missing source bank. */
    {
        const char request[]="{\"gp_items\":[{\"id\":1,\"weights\":[{\"item\":4105,\"weight\":1}]}]}";
        PortableAssetSectionView *rows=calloc(base.section_count,sizeof *rows);CHECK(rows!=NULL);PortableAssetPackView old=base;
        old.resolved_sections=rows;old.section_count=0u;
        for(uint32_t i=0u;i<base.section_count;++i){
            PortableAssetSectionView row;CHECK(portable_asset_pack_section_at(&base,i,&row)==PORTABLE_ASSET_PACK_OK);
            if(row.kind!=PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS&&row.kind!=PORTABLE_ASSET_SECTION_GP_SCORING&&row.kind!=PORTABLE_ASSET_SECTION_MUSHROOM_RULES)rows[old.section_count++]=row;
        }
        PortableAssetSectionView missing=json;missing.bytes=(const uint8_t *)request;missing.size=sizeof request-1u;
        CHECK(portable_game_data_apply(&old,&missing,error)==NULL&&strstr(error,"lacks")!=NULL);
        const char points[]="{\"gp_points\":{\"id\":1,\"awards\":[9,6,3,1,0,0,0,0]}}";
        missing.bytes=(const uint8_t *)points;missing.size=sizeof points-1u;
        CHECK(portable_game_data_apply(&old,&missing,error)==NULL&&strstr(error,"lacks")!=NULL);
        const char mushroom[]="{\"mushroom_rules\":{\"id\":1,\"boost_ticks\":75}}";
        missing.bytes=(const uint8_t *)mushroom;missing.size=sizeof mushroom-1u;
        CHECK(portable_game_data_apply(&old,&missing,error)==NULL&&strstr(error,"mushroom_rules")!=NULL);
        free(rows);

    }
    char directory[4096];CHECK(snprintf(directory,sizeof directory,"%s/mods",argv[1])>0);
    char ids[3][PORTABLE_MOD_ID_CAPACITY]={"a","bad","b"};
    PortableModSet *mods=portable_mods_load(&base,directory,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,1u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==1u);
    same_overrides(portable_mods_pack(mods),&compiled);portable_mods_free(mods);
    mods=portable_mods_load(&base,directory,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==2u);
    PortableAssetSectionView motion;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_MOTION_TABLES,1u,&motion)==PORTABLE_ASSET_PACK_OK);
    CHECK(le32(motion.bytes+motion.size-36u)==UINT32_C(600)*65536u);
    CHECK(motion.bytes[motion.size-452u+128u]==63u);
    CHECK(le32(motion.bytes+38056u)==UINT32_C(1200)*65536u);
    CHECK(le32(motion.bytes+motion.size-12u)==UINT32_C(0x03e903e8));
    PortableAssetSectionView other_kart;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_MOTION_TABLES,2u,&other_kart)==PORTABLE_ASSET_PACK_OK);
    CHECK(le32(other_kart.bytes+other_kart.size-36u)==UINT32_C(1155)*65536u/2u);
    PortableAssetSectionView cpu;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,27u,&cpu)==PORTABLE_ASSET_PACK_OK);
    const uint8_t empty_rank_row[16]={0};
    CHECK(memcmp(cpu.bytes+288u+768u,empty_rank_row,16u)==0);
    CHECK(le32(cpu.bytes+288u+768u+16u)==UINT32_C(0x10071007));
    CHECK(le32(cpu.bytes+288u+768u+128u)==UINT32_C(0x10071007));
    CHECK(le32(cpu.bytes+3680u)==UINT32_C(0x00091002));
    PortableAssetSectionView mushroom;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_MUSHROOM_RULES,1u,&mushroom)==PORTABLE_ASSET_PACK_OK);
    CHECK(le32(mushroom.bytes+8u)==75u&&le32(mushroom.bytes+16u)==240u&&le32(mushroom.bytes+24u)==117440512u);
    PortableAssetSectionView scoring;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_GP_SCORING,1u,&scoring)==PORTABLE_ASSET_PACK_OK);
    CHECK(scoring.bytes[8u]==18u&&scoring.bytes[9u]==12u&&scoring.version==2u&&scoring.size==20u&&le32(scoring.bytes+16u)==3u);
    PortableAssetSectionView gp;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS,1u,&gp)==PORTABLE_ASSET_PACK_OK);
    CHECK(le32(gp.bytes+8u)==UINT32_C(0x00011002)&&le32(gp.bytes+12u)==UINT32_C(0x00ff0000));
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS,2u,&gp)==PORTABLE_ASSET_PACK_OK);
    CHECK(le32(gp.bytes+8u)==UINT32_C(0x00091007)&&le32(gp.bytes+12u)==UINT32_C(0x00011009));
    /* A second absolute cup order must move the already-remapped art back,
     * rather than borrowing the first layer's original slot identities. */
    const uint32_t kinds[]={PORTABLE_ASSET_SECTION_COURSE_CATALOG,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS};
    for(unsigned group=0u;group<2u;++group)for(unsigned image=0u;image<(group==0u?1u:12u);++image){
        const uint32_t id=group==0u?1u:0x23300u+(image/4u)*0x100u+image%4u;
        PortableAssetSectionView original,current;
        CHECK(portable_asset_pack_find_section(&base,kinds[group],id,&original)==PORTABLE_ASSET_PACK_OK);
        CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),kinds[group],id,&current)==PORTABLE_ASSET_PACK_OK);
        CHECK(original.size==current.size&&memcmp(original.bytes,current.bytes,original.size)==0);
    }
    CHECK(memcmp(base_bytes,preserved,base.size)==0);
    portable_mods_free(mods);portable_game_data_free(data);
    free(preserved);free(base_bytes);free(compiled_bytes);free(runtime_bytes);
    return 0;
}
