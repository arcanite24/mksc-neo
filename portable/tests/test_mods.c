#include "mod_archive.h"
#include "mod_loader.h"
#include "model_asset.h"
#include "test_temp.h"
#include "mesh_item_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"mods line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    const char *name;
    const uint8_t *data;
    size_t size;
    uint16_t method;
} TestZipEntry;

static void zip_u16(uint8_t *p,uint16_t value)
{p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);}
static void zip_u32(uint8_t *p,uint32_t value)
{for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(value>>(i*8u));}

/* Authored containers make each malformed field independent of disk fixtures. */
static uint8_t *test_zip(const TestZipEntry *entries,size_t count,size_t *size,size_t *directory)
{
    uint8_t *bytes=calloc(1u,4096u);CHECK(bytes!=NULL&&count<=4u);
    size_t at=0u,offsets[4];
    for(size_t i=0u;i<count;++i){
        const TestZipEntry *entry=&entries[i];
        const size_t name=strlen(entry->name);
        offsets[i]=at;zip_u32(bytes+at,UINT32_C(0x04034b50));
        zip_u16(bytes+at+4u,20u);zip_u16(bytes+at+8u,entry->method);
        zip_u32(bytes+at+14u,entry->method?0u:portable_crc32(entry->data,entry->size));
        zip_u32(bytes+at+18u,(uint32_t)entry->size);
        zip_u32(bytes+at+22u,entry->method?0u:(uint32_t)entry->size);
        zip_u16(bytes+at+26u,(uint16_t)name);
        memcpy(bytes+at+30u,entry->name,name);
        if(entry->size!=0u)memcpy(bytes+at+30u+name,entry->data,entry->size);
        at+=30u+name+entry->size;
    }
    *directory=at;
    for(size_t i=0u;i<count;++i){
        const TestZipEntry *entry=&entries[i];const size_t name=strlen(entry->name);
        zip_u32(bytes+at,UINT32_C(0x02014b50));zip_u16(bytes+at+6u,20u);
        zip_u16(bytes+at+10u,entry->method);
        zip_u32(bytes+at+16u,entry->method?0u:portable_crc32(entry->data,entry->size));
        zip_u32(bytes+at+20u,(uint32_t)entry->size);
        zip_u32(bytes+at+24u,entry->method?0u:(uint32_t)entry->size);
        zip_u16(bytes+at+28u,(uint16_t)name);zip_u32(bytes+at+42u,(uint32_t)offsets[i]);
        memcpy(bytes+at+46u,entry->name,name);at+=46u+name;
    }
    zip_u32(bytes+at,UINT32_C(0x06054b50));zip_u16(bytes+at+8u,(uint16_t)count);
    zip_u16(bytes+at+10u,(uint16_t)count);zip_u32(bytes+at+12u,(uint32_t)(at-*directory));
    zip_u32(bytes+at+16u,(uint32_t)*directory);*size=at+22u;CHECK(*size<=4096u);
    return bytes;
}

static void archive_install_checks(void)
{
    static const uint8_t manifest_bytes[]={'{','}'},empty_deflate[]={3u,0u};
    TestZipEntry entries[3]={{"nested/mod.json",manifest_bytes,2u,0u},
                            {"nested/empty",NULL,0u,0u},
                            {"nested/empty.deflate",empty_deflate,2u,8u}};
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];size_t size,directory,raw_size;
    uint8_t *bytes=test_zip(entries,3u,&size,&directory),saved[4096];memcpy(saved,bytes,size);
    PortableModArchive *archive=portable_mod_archive_open_memory(bytes,size,NULL,error);
    CHECK(archive!=NULL&&strcmp(portable_mod_archive_root(archive),"nested")==0);
    CHECK(portable_mod_archive_validate(archive,error));
    CHECK(portable_mod_archive_bytes(archive,&raw_size)==bytes&&raw_size==size&&memcmp(bytes,saved,size)==0);
    uint8_t *read=portable_mod_archive_read(archive,"mod.json",16u,&raw_size,error);
    CHECK(read!=NULL&&raw_size==2u&&memcmp(read,manifest_bytes,2u)==0);free(read);
    CHECK(portable_mod_archive_read(archive,"empty",16u,&raw_size,error)==NULL);
    portable_mod_archive_free(archive);
    /* A root manifest wins even when nested candidates would be ambiguous. */
    entries[1]=(TestZipEntry){"other/mod.json",manifest_bytes,2u,0u};
    entries[2]=(TestZipEntry){"mod.json",manifest_bytes,2u,0u};
    bytes=test_zip(entries,3u,&size,&directory);
    archive=portable_mod_archive_open_memory(bytes,size,NULL,error);
    CHECK(archive!=NULL&&portable_mod_archive_root(archive)[0]=='\0');portable_mod_archive_free(archive);
    bytes=test_zip(entries,2u,&size,&directory);
    CHECK(portable_mod_archive_open_memory(bytes,size,NULL,error)==NULL&&strstr(error,"multiple"));
    bytes=test_zip(entries,2u,&size,&directory);
    archive=portable_mod_archive_open_memory(bytes,size,"nested",error);
    CHECK(archive!=NULL&&strcmp(portable_mod_archive_root(archive),"nested")==0);portable_mod_archive_free(archive);
    entries[0].name="Bad/mod.json";bytes=test_zip(entries,1u,&size,&directory);
    CHECK(portable_mod_archive_open_memory(bytes,size,NULL,error)==NULL&&strstr(error,"valid mod ID"));
    entries[0].name="deep/nested/mod.json";bytes=test_zip(entries,1u,&size,&directory);
    CHECK(portable_mod_archive_open_memory(bytes,size,NULL,error)==NULL&&strstr(error,"manifest"));
    entries[0].name="mod.json";
    entries[1]=(TestZipEntry){"unused",manifest_bytes,2u,0u};
    bytes=test_zip(entries,2u,&size,&directory);
    /* Corrupt an unrequested entry: opening/read still work, full validation fails. */
    bytes[directory+46u+strlen(entries[0].name)+16u]^=1u;
    archive=portable_mod_archive_open_memory(bytes,size,NULL,error);CHECK(archive!=NULL);
    read=portable_mod_archive_read(archive,"mod.json",16u,&raw_size,error);CHECK(read!=NULL);free(read);
    CHECK(!portable_mod_archive_validate(archive,error)&&strstr(error,"unused")&&strstr(error,"CRC"));portable_mod_archive_free(archive);
    for(unsigned folder=0u;folder<2u;++folder){
        entries[1]=(TestZipEntry){folder?"empty/":"empty",NULL,0u,0u};
        bytes=test_zip(entries,2u,&size,&directory);
        zip_u32(bytes+directory+46u+strlen(entries[0].name)+16u,1u);
        archive=portable_mod_archive_open_memory(bytes,size,NULL,error);CHECK(archive!=NULL);
        CHECK(!portable_mod_archive_validate(archive,error)&&strstr(error,"CRC"));portable_mod_archive_free(archive);
    }
    entries[1]=(TestZipEntry){"bad-deflate",manifest_bytes,2u,8u};
    bytes=test_zip(entries,2u,&size,&directory);archive=portable_mod_archive_open_memory(bytes,size,NULL,error);CHECK(archive!=NULL);
    CHECK(!portable_mod_archive_validate(archive,error)&&strstr(error,"deflate"));portable_mod_archive_free(archive);
    bytes=test_zip(entries,1u,&size,&directory);bytes[directory+46u+3u]=0u;
    CHECK(portable_mod_archive_open_memory(bytes,size,NULL,error)==NULL&&strstr(error,"NUL"));
    bytes=test_zip(entries,1u,&size,&directory);zip_u32(bytes+directory+42u,UINT32_MAX);
    CHECK(portable_mod_archive_open_memory(bytes,size,NULL,error)==NULL&&strstr(error,"local header"));
    bytes=test_zip(entries,1u,&size,&directory);zip_u16(bytes+28u,UINT16_MAX);
    CHECK(portable_mod_archive_open_memory(bytes,size,NULL,error)==NULL&&strstr(error,"local header"));
}

static uint8_t *read_bytes(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    long length=ftell(file);CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1u,(size_t)length,file)==(size_t)length&&fclose(file)==0);
    *size=(size_t)length;return bytes;
}
static void write_bytes(const char *path,const void *bytes,size_t size)
{
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    CHECK(fwrite(bytes,1u,size,file)==size&&fclose(file)==0);
}
static void manifest(const char *root,const char *id,const char *extra)
{
    char path[512],json[1024];snprintf(path,sizeof path,"%s/%s/mod.json",root,id);
    int size=snprintf(json,sizeof json,"{\"id\":\"%s\",\"name\":\"Example\",\"version\":\"1.0.0\",\"api_version\":1,\"pack\":\"content.pack\"%s}",id,extra);
    CHECK(size>0&&(size_t)size<sizeof json);write_bytes(path,json,(size_t)size);
}
static void strict_checks(const PortableAssetPackView *base,const char *root,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],const uint8_t expected_digest[32])
{
    PortableModLoadReport report;
    PortableModSet *candidate=portable_mods_load_strict(base,root,ids,3u,&report);
    CHECK(candidate!=NULL&&report.count==3u&&report.first_error[0]=='\0');
    for(size_t i=0u;i<3u;++i)CHECK(report.entries[i].accepted&&report.entries[i].message[0]=='\0');
    CHECK(memcmp(expected_digest,portable_mods_pack(candidate)->deterministic_digest,32u)==0);
    portable_mods_free(candidate);
    const char wrong[][PORTABLE_MOD_ID_CAPACITY]={"c","b","a"};
    candidate=portable_mods_load_report(base,root,wrong,3u,&report);
    CHECK(candidate!=NULL&&portable_mods_loaded_count(candidate)==1u&&
        !report.entries[0].accepted&&!report.entries[1].accepted&&report.entries[2].accepted&&
        strstr(report.entries[0].message,"load_after")&&strstr(report.entries[1].message,"depends"));
    portable_mods_free(candidate);
    CHECK(portable_mods_load_strict(base,root,wrong,3u,&report)==NULL&&
        strstr(report.first_error,"load_after"));
    const char missing[][PORTABLE_MOD_ID_CAPACITY]={"a","absent"};
    CHECK(portable_mods_load_strict(base,root,missing,2u,&report)==NULL&&
        report.entries[0].accepted&&!report.entries[1].accepted&&report.first_error[0]);
    candidate=portable_mods_load_strict(base,root,NULL,0u,&report);
    CHECK(candidate!=NULL&&memcmp(base->deterministic_digest,
        portable_mods_pack(candidate)->deterministic_digest,32u)==0);
    portable_mods_free(candidate);
}

static void projection_pack(const char *path,unsigned padding,uint8_t road_color,uint8_t texture_color,int broken)
{
    uint8_t bytes[320]={0};
    const size_t road_at=192u+padding,texture_at=road_at+8u,total=texture_at+36u;
    CHECK(total<=sizeof bytes&&padding%8u==0u);
    memcpy(bytes,"MKSCPACK",8u);zip_u16(bytes+8u,1u);zip_u16(bytes+10u,112u);
    zip_u32(bytes+12u,UINT32_C(0x12345678));zip_u32(bytes+16u,(uint32_t)total);
    zip_u32(bytes+24u,2u);zip_u32(bytes+28u,40u);zip_u32(bytes+32u,112u);
    zip_u32(bytes+112u,8u);zip_u32(bytes+116u,24u);zip_u32(bytes+120u,1u);
    zip_u32(bytes+128u,(uint32_t)road_at);zip_u32(bytes+136u,1u);bytes[road_at]=road_color;
    zip_u32(bytes+152u,broken?99u:101u);zip_u32(bytes+156u,7u);zip_u32(bytes+160u,1u);
    zip_u32(bytes+168u,(uint32_t)texture_at);zip_u32(bytes+176u,36u);
    uint8_t *image=bytes+texture_at;
    memcpy(image,"MKSCIMG1",8u);zip_u16(image+8u,32u);zip_u16(image+10u,1u);
    zip_u16(image+12u,1u);zip_u16(image+14u,1u);zip_u32(image+24u,32u);zip_u32(image+28u,36u);
    image[32u]=texture_color;image[35u]=255u;
    const uint8_t *empty=NULL;const size_t zero=0u;
    portable_asset_pack_sha256(&empty,&zero,1u,bytes+72u);
    const uint8_t *whole=bytes;
    portable_asset_pack_sha256(&whole,&total,1u,bytes+40u);
    write_bytes(path,bytes,total);
}
static void item_projection_pack(const char *path,uint32_t capacity)
{
    uint8_t bytes[152u+PORTABLE_MESH_ITEM_RULES_BYTES]={0};
    const size_t total=sizeof bytes;
    memcpy(bytes,"MKSCPACK",8u);zip_u16(bytes+8u,1u);zip_u16(bytes+10u,112u);
    zip_u32(bytes+12u,UINT32_C(0x12345678));zip_u32(bytes+16u,(uint32_t)total);
    zip_u32(bytes+24u,1u);zip_u32(bytes+28u,40u);zip_u32(bytes+32u,112u);
    zip_u32(bytes+112u,103u);zip_u32(bytes+116u,41u);zip_u32(bytes+120u,1u);
    zip_u32(bytes+124u,3u);zip_u32(bytes+128u,152u);
    zip_u32(bytes+136u,PORTABLE_MESH_ITEM_RULES_BYTES);
    test_mir_payload(bytes+152u);zip_u32(bytes+184u,capacity);
    const uint8_t *parts[]={bytes+112u,bytes+152u};
    const size_t sizes[]={40u,PORTABLE_MESH_ITEM_RULES_BYTES};
    portable_asset_pack_sha256(parts,sizes,2u,bytes+72u);
    const uint8_t *whole=bytes;
    portable_asset_pack_sha256(&whole,&total,1u,bytes+40u);
    write_bytes(path,bytes,total);
}
static void coin_projection_pack(const char *path, uint32_t hit_loss)
{
    uint8_t bytes[152u + 452u] = {0};
    const size_t total = sizeof bytes;
    memcpy(bytes, "MKSCPACK", 8u);
    zip_u16(bytes + 8u, 1u); zip_u16(bytes + 10u, 112u);
    zip_u32(bytes + 12u, UINT32_C(0x12345678));
    zip_u32(bytes + 16u, (uint32_t)total);
    zip_u32(bytes + 24u, 1u); zip_u32(bytes + 28u, 40u); zip_u32(bytes + 32u, 112u);
    zip_u32(bytes + 112u, PORTABLE_ASSET_SECTION_MESH_COIN_RULES);
    zip_u32(bytes + 116u, 41u); zip_u32(bytes + 120u, 1u); zip_u32(bytes + 124u, 3u);
    zip_u32(bytes + 128u, 152u); zip_u32(bytes + 136u, 452u);
    uint8_t *rules = bytes + 152u;
    memcpy(rules, "MCR1", 4u);
    zip_u32(rules + 4u, 1u); zip_u32(rules + 8u, 452u);
    zip_u32(rules + 12u, 41u); zip_u32(rules + 16u, 1u);
    zip_u32(rules + 20u, 13u); zip_u32(rules + 24u, 99u); zip_u32(rules + 28u, 5u);
    for (unsigned i = 0; i < 100u; ++i) zip_u32(rules + 32u + 4u * i, 65536u);
    zip_u32(rules + 432u, hit_loss);
    const uint8_t *parts[] = {bytes + 112u, rules};
    const size_t sizes[] = {40u, 452u};
    portable_asset_pack_sha256(parts, sizes, 2u, bytes + 72u);
    const uint8_t *whole = bytes;
    portable_asset_pack_sha256(&whole, &total, 1u, bytes + 40u);
    write_bytes(path, bytes, total);
}
static void sky_projection_pack(const char *path, uint32_t revision)
{
    uint8_t bytes[152u + 4244u] = {0};
    const size_t total = sizeof bytes;
    memcpy(bytes, "MKSCPACK", 8u);
    zip_u16(bytes + 8u, 1u); zip_u16(bytes + 10u, 112u);
    zip_u32(bytes + 12u, UINT32_C(0x12345678));
    zip_u32(bytes + 16u, (uint32_t)total);
    zip_u32(bytes + 24u, 1u); zip_u32(bytes + 28u, 40u); zip_u32(bytes + 32u, 112u);
    zip_u32(bytes + 112u, PORTABLE_ASSET_SECTION_MESH_SKY_RULES);
    zip_u32(bytes + 116u, 41u); zip_u32(bytes + 120u, 1u); zip_u32(bytes + 124u, 3u);
    zip_u32(bytes + 128u, 152u); zip_u32(bytes + 136u, 4244u);
    uint8_t *rules = bytes + 152u;
    memcpy(rules, "MSK1", 4u);
    zip_u32(rules + 4u, 1u); zip_u32(rules + 8u, 4244u);
    zip_u32(rules + 12u, 41u); zip_u32(rules + 16u, revision);
    zip_u32(rules + 20u, 1u); zip_u32(rules + 28u, 1024u);
    zip_u32(rules + 32u + 4u * 4u, 1u); /* Logical layer0 enabled. */
    zip_u32(rules + 32u + 14u * 4u, UINT32_MAX); /* No split row. */
    zip_u32(rules + 124u, 256u); zip_u32(rules + 128u, 1024u * 65536u);
    const uint8_t *parts[] = {bytes + 112u, rules};
    const size_t sizes[] = {40u, 4244u};
    portable_asset_pack_sha256(parts, sizes, 2u, bytes + 72u);
    const uint8_t *whole = bytes;
    portable_asset_pack_sha256(&whole, &total, 1u, bytes + 40u);
    write_bytes(path, bytes, total);
}
static void projection_manifest(const char *root,const char *id,const char *targets)
{
    char path[512],json[1024];
    snprintf(path,sizeof path,"%s/%s/mod.json",root,id);
    const int length=snprintf(json,sizeof json,
        "{\"id\":\"%s\",\"name\":\"Projection\",\"version\":\"1.0.0\",\"api_version\":1,"
        "\"pack\":\"content.pack\",\"script\":\"main.lua\"%s}",id,targets);
    CHECK(length>0&&(size_t)length<sizeof json);write_bytes(path,json,(size_t)length);
}
static void equal_projection(const PortableModSet *legacy,const PortableModSet *domains)
{
    const PortableAssetPackView *a=portable_mods_pack(legacy),*b=portable_mods_pack(domains);
    CHECK(a->section_count==b->section_count&&a->driving_profile==b->driving_profile);
    CHECK(memcmp(a->deterministic_digest,b->deterministic_digest,32u)==0&&memcmp(a->pack_digest,b->pack_digest,32u)==0);
    CHECK(memcmp(portable_mods_identity(legacy),portable_mods_identity(domains),sizeof(PortableModIdentity))==0);
    for(uint32_t i=0u;i<a->section_count;++i){
        PortableAssetSectionView left,right;
        CHECK(portable_asset_pack_section_at(a,i,&left)==PORTABLE_ASSET_PACK_OK&&
            portable_asset_pack_section_at(b,i,&right)==PORTABLE_ASSET_PACK_OK);
        CHECK(left.kind==right.kind&&left.content_id==right.content_id&&left.version==right.version&&
            left.flags==right.flags&&left.semantic_revision==right.semantic_revision&&left.size==right.size&&
            memcmp(left.bytes,right.bytes,left.size)==0);
    }
    for(size_t i=0u;i<portable_mods_loaded_count(legacy);++i){
        const char *left_id=NULL,*left=NULL,*right_id=NULL,*right=NULL;size_t left_size=0u,right_size=0u;
        const int present=portable_mods_script(legacy,i,&left_id,&left,&left_size);
        CHECK(present==portable_mods_script(domains,i,&right_id,&right,&right_size));
        if(present)CHECK(strcmp(left_id,right_id)==0&&left_size==right_size&&memcmp(left,right,left_size)==0);
    }
}
static void dual_base_checks(const PortableAssetPackView *original,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY])
{
    /* Deliberately tiny authored profile payloads isolate the loader's identity
     * contract; profile decoding belongs to its dedicated tests. */
    const uint8_t payloads[2]={17u,29u};
    PortableAssetSectionView profile_sections[2]={{0},{0}},storage[2][64];
    PortableAssetPackView profiles[2],bases[2];
    CHECK(original->section_count<64u);
    for(unsigned i=0u;i<2u;++i){
        profile_sections[i]=(PortableAssetSectionView){.kind=95u,.content_id=1u,.version=1u,
            .flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,.semantic_revision=1u,.bytes=&payloads[i],.size=1u};
        profiles[i]=(PortableAssetPackView){.section_count=1u,.resolved_sections=&profile_sections[i]};
        const PortableAssetPackView *layers[]={original,&profiles[i]};
        CHECK(portable_asset_pack_compose(layers,2u,storage[i],64u,&bases[i])==PORTABLE_ASSET_PACK_OK);
        bases[i].driving_profile=(uint8_t)(i+1u);
    }
    PortableModLoadReport report;
    PortableModSet *a=portable_mods_load_domains_strict(&bases[0],original,directory,ids,3u,&report);
    PortableModSet *b=portable_mods_load_domains_strict(&bases[1],original,directory,ids,3u,&report);
    CHECK(a!=NULL&&b!=NULL);
    CHECK(memcmp(portable_mods_pack(a)->deterministic_digest,portable_mods_pack(b)->deterministic_digest,32u)!=0);
    CHECK(memcmp(portable_mods_pack_3d(a)->deterministic_digest,portable_mods_pack_3d(b)->deterministic_digest,32u)==0);
    CHECK(memcmp(portable_mods_identity_3d(a),portable_mods_identity_3d(b),sizeof(PortableModIdentity))==0);
    CHECK(portable_mods_pack_3d(a)->driving_profile==original->driving_profile&&portable_mods_pack_3d(b)->driving_profile==original->driving_profile);
    for(unsigned i=0u;i<2u;++i){
        PortableModSet *legacy=portable_mods_load_strict(&bases[i],directory,ids,3u,&report);
        CHECK(legacy!=NULL);equal_projection(legacy,i?b:a);portable_mods_free(legacy);
    }
    portable_mods_free(a);portable_mods_free(b);
    a=portable_mods_load_domains_report(&bases[0],original,directory,NULL,0u,&report);
    CHECK(a!=NULL&&memcmp(portable_mods_pack(a)->deterministic_digest,bases[0].deterministic_digest,32u)==0&&
        memcmp(portable_mods_pack_3d(a)->deterministic_digest,original->deterministic_digest,32u)==0);
    portable_mods_free(a);
    CHECK(portable_mods_load_domains_strict(original,NULL,directory,ids,3u,&report)==NULL&&strstr(report.first_error,"invalid mod load arguments"));
}
static void projection_checks(const PortableAssetPackView *base)
{
    char root[256],path[512];CHECK(test_temp_path(root,sizeof root,"mod-projections",1)==0);
    const char *names[]={"legacy","three","dual"};
    for(unsigned i=0u;i<3u;++i){
        snprintf(path,sizeof path,"%s/%s",root,names[i]);
#ifdef _WIN32
        CHECK(_mkdir(path)==0);
#else
        CHECK(mkdir(path,0700)==0);
#endif
        snprintf(path,sizeof path,"%s/%s/main.lua",root,names[i]);write_bytes(path,"local x=1",9u);
    }
    projection_manifest(root,"legacy","");
    projection_manifest(root,"three",",\"targets\":[\"3d\"]");
    projection_manifest(root,"dual",",\"targets\":[\"2d\",\"3d\"]");
    /* Reuse a real legacy container, then compare the original fingerprint
     * recipe directly instead of merely comparing two executions. */
    PortableAssetSectionView base_road;
    CHECK(portable_asset_pack_find_section(base,8u,24u,&base_road)==PORTABLE_ASSET_PACK_OK);
    /* The legacy pack contains no 3D sections. Borrow the caller's raw pack. */
    snprintf(path,sizeof path,"%s/legacy/content.pack",root);write_bytes(path,base->bytes,base->size);
    snprintf(path,sizeof path,"%s/three/content.pack",root);projection_pack(path,0u,77u,12u,0);
    snprintf(path,sizeof path,"%s/dual/content.pack",root);projection_pack(path,0u,88u,22u,0);
    const char ids[][PORTABLE_MOD_ID_CAPACITY]={"legacy","three","dual"};
    PortableModLoadReport report;
    PortableModSet *legacy=portable_mods_load_strict(base,root,ids,1u,&report);CHECK(legacy!=NULL);
    snprintf(path,sizeof path,"%s/legacy/mod.json",root);size_t json_size;uint8_t *json=read_bytes(path,&json_size);
    uint8_t manifest_hash[32],sizes[16]={0},expected[32];const uint8_t *part=json;
    portable_asset_pack_sha256(&part,&json_size,1u,manifest_hash);free(json);
    for(unsigned i=0u;i<8u;++i)sizes[i]=(uint8_t)((uint64_t)base->size>>(8u*i));
    sizes[8u]=9u;
    const uint8_t *parts[]={manifest_hash,sizes,base->bytes,(const uint8_t *)"local x=1"};
    const size_t lengths[]={32u,16u,base->size,9u};
    portable_asset_pack_sha256(parts,lengths,4u,expected);
    CHECK(memcmp(expected,portable_mods_identity(legacy)->mods[0].fingerprint,8u)==0);
    PortableModSet *three=portable_mods_load_strict(base,root,ids,2u,&report);CHECK(three!=NULL);
    CHECK(portable_mods_loaded_count(three)==2u&&portable_mods_identity(three)->count==1u&&portable_mods_identity_3d(three)->count==1u);
    CHECK(memcmp(portable_mods_pack(legacy)->deterministic_digest,portable_mods_pack(three)->deterministic_digest,32u)==0);
    CHECK(memcmp(portable_mods_identity(legacy),portable_mods_identity(three),sizeof(PortableModIdentity))==0);
    const char *id=NULL,*source=NULL;size_t script_size=0u;
    CHECK(!portable_mods_script(three,1u,&id,&source,&script_size));
    CHECK(portable_mods_script_3d(three,1u,&id,&source,&script_size)&&strcmp(id,"three")==0&&script_size==9u);
    CHECK(!portable_mods_script_3d(three,0u,&id,&source,&script_size));
    PortableAssetSectionView section;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(three),8u,24u,&section)==PORTABLE_ASSET_PACK_OK&&section.size==base_road.size&&memcmp(section.bytes,base_road.bytes,section.size)==0);
    CHECK(portable_asset_pack_find_section(portable_mods_pack_3d(three),8u,24u,&section)==PORTABLE_ASSET_PACK_OK&&section.size==1u&&section.bytes[0]==77u);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(three),101u,7u,&section)==PORTABLE_ASSET_PACK_NOT_FOUND);
    portable_mods_free(three);portable_mods_free(legacy);
    PortableModSet *dual=portable_mods_load_strict(base,root,ids,3u,&report);CHECK(dual!=NULL);
    uint8_t before_2d[32],before_3d[32];
    memcpy(before_2d,portable_mods_pack(dual)->deterministic_digest,32u);
    memcpy(before_3d,portable_mods_pack_3d(dual)->deterministic_digest,32u);
    CHECK(portable_mods_identity(dual)->count==2u&&portable_mods_identity_3d(dual)->count==2u);
    CHECK(portable_mods_script(dual,2u,NULL,NULL,NULL)&&portable_mods_script_3d(dual,2u,NULL,NULL,NULL));
    portable_mods_free(dual);
    dual_base_checks(base,root,ids);
    snprintf(path,sizeof path,"%s/dual/content.pack",root);projection_pack(path,16u,88u,22u,0);
    dual=portable_mods_load_strict(base,root,ids,3u,&report);CHECK(dual!=NULL);
    CHECK(memcmp(before_2d,portable_mods_pack(dual)->deterministic_digest,32u)==0&&memcmp(before_3d,portable_mods_pack_3d(dual)->deterministic_digest,32u)==0);
    portable_mods_free(dual);
    projection_pack(path,8u,88u,33u,0);
    dual=portable_mods_load_strict(base,root,ids,3u,&report);CHECK(dual!=NULL);
    CHECK(memcmp(before_2d,portable_mods_pack(dual)->deterministic_digest,32u)==0&&memcmp(before_3d,portable_mods_pack_3d(dual)->deterministic_digest,32u)!=0);
    uint8_t before_shared[32];memcpy(before_shared,portable_mods_pack_3d(dual)->deterministic_digest,32u);
    portable_mods_free(dual);
    projection_pack(path,8u,99u,33u,0);
    dual=portable_mods_load_strict(base,root,ids,3u,&report);CHECK(dual!=NULL);
    CHECK(memcmp(before_2d,portable_mods_pack(dual)->deterministic_digest,32u)!=0&&
        memcmp(before_shared,portable_mods_pack_3d(dual)->deterministic_digest,32u)!=0);portable_mods_free(dual);
    projection_pack(path,0u,88u,22u,1);
    CHECK(portable_mods_load_strict(base,root,ids,3u,&report)==NULL&&strstr(report.first_error,"3D course"));
    projection_manifest(root,"dual",",\"targets\":[\"2d\"]");projection_pack(path,0u,88u,22u,0);
    CHECK(portable_mods_load_strict(base,root,ids,3u,&report)==NULL&&strstr(report.first_error,"requires target 3d"));
    /* MIR1 is a 3D-only deterministic projection even in a dual-target mod.
     * Changing its valid scalar must not perturb any 2D bytes/identity/scripts. */
    projection_manifest(root,"dual",",\"targets\":[\"2d\",\"3d\"]");
    item_projection_pack(path,64u);
    PortableModSet *items_before=portable_mods_load_strict(base,root,ids,3u,&report);CHECK(items_before!=NULL);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(items_before),103u,41u,&section)==PORTABLE_ASSET_PACK_NOT_FOUND);
    CHECK(portable_asset_pack_find_section(portable_mods_pack_3d(items_before),103u,41u,&section)==PORTABLE_ASSET_PACK_OK);
    item_projection_pack(path,32u);
    PortableModSet *items_after=portable_mods_load_strict(base,root,ids,3u,&report);CHECK(items_after!=NULL);
    equal_projection(items_before,items_after);
    CHECK(memcmp(portable_mods_pack_3d(items_before)->deterministic_digest,
                 portable_mods_pack_3d(items_after)->deterministic_digest,32u)!=0);
    CHECK(!portable_mod_identity_equal(portable_mods_identity_3d(items_before),
                                       portable_mods_identity_3d(items_after)));
    portable_mods_free(items_before);portable_mods_free(items_after);
    item_projection_pack(path,65u);
    CHECK(portable_mods_load_strict(base,root,ids,3u,&report)==NULL&&strstr(report.first_error,"MIR1"));
    item_projection_pack(path,64u);
    projection_manifest(root,"dual",",\"targets\":[\"2d\"]");
    CHECK(portable_mods_load_strict(base,root,ids,3u,&report)==NULL&&strstr(report.first_error,"requires target 3d"));
    /* MCR1 is also exclusively 3D: changing a coin loss leaves the complete
     * 2D projection (including scripts/identity) byte-identical. */
    projection_manifest(root,"dual",",\"targets\":[\"2d\",\"3d\"]");
    coin_projection_pack(path,4u);
    PortableModSet *coins_before=portable_mods_load_strict(base,root,ids,3u,&report);
    CHECK(coins_before!=NULL);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(coins_before),105u,41u,&section)==PORTABLE_ASSET_PACK_NOT_FOUND);
    CHECK(portable_asset_pack_find_section(portable_mods_pack_3d(coins_before),105u,41u,&section)==PORTABLE_ASSET_PACK_OK);
    coin_projection_pack(path,8u);
    PortableModSet *coins_after=portable_mods_load_strict(base,root,ids,3u,&report);
    CHECK(coins_after!=NULL);
    equal_projection(coins_before,coins_after);
    CHECK(memcmp(portable_mods_pack_3d(coins_before)->deterministic_digest,
                 portable_mods_pack_3d(coins_after)->deterministic_digest,32u)!=0);
    CHECK(!portable_mod_identity_equal(portable_mods_identity_3d(coins_before),
                                      portable_mods_identity_3d(coins_after)));
    portable_mods_free(coins_before);portable_mods_free(coins_after);
    projection_manifest(root,"dual",",\"targets\":[\"2d\"]");
    CHECK(portable_mods_load_strict(base,root,ids,3u,&report)==NULL &&
          strstr(report.first_error,"requires target 3d"));
    /* MSK1 revision changes affect only the 3D projection, including identity. */
    projection_manifest(root,"dual",",\"targets\":[\"2d\",\"3d\"]");
    sky_projection_pack(path,1u);
    PortableModSet *sky_before=portable_mods_load_strict(base,root,ids,3u,&report);
    CHECK(sky_before!=NULL);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(sky_before),106u,41u,&section)==PORTABLE_ASSET_PACK_NOT_FOUND);
    CHECK(portable_asset_pack_find_section(portable_mods_pack_3d(sky_before),106u,41u,&section)==PORTABLE_ASSET_PACK_OK);
    sky_projection_pack(path,2u);
    PortableModSet *sky_after=portable_mods_load_strict(base,root,ids,3u,&report);
    CHECK(sky_after!=NULL);
    equal_projection(sky_before,sky_after);
    CHECK(memcmp(portable_mods_pack_3d(sky_before)->deterministic_digest,
                 portable_mods_pack_3d(sky_after)->deterministic_digest,32u)!=0);
    CHECK(!portable_mod_identity_equal(portable_mods_identity_3d(sky_before),
                                      portable_mods_identity_3d(sky_after)));
    portable_mods_free(sky_before);portable_mods_free(sky_after);
    projection_manifest(root,"dual",",\"targets\":[\"2d\"]");
    CHECK(portable_mods_load_strict(base,root,ids,3u,&report)==NULL &&
          strstr(report.first_error,"requires target 3d"));
    for(unsigned i=0u;i<3u;++i){
        snprintf(path,sizeof path,"%s/%s/mod.json",root,names[i]);CHECK(remove(path)==0);
        snprintf(path,sizeof path,"%s/%s/main.lua",root,names[i]);CHECK(remove(path)==0);
        snprintf(path,sizeof path,"%s/%s/content.pack",root,names[i]);CHECK(remove(path)==0);
        snprintf(path,sizeof path,"%s/%s",root,names[i]);CHECK(test_rmdir(path)==0);
    }
    CHECK(test_rmdir(root)==0);
}

static void make_directory(const char *path)
{
#ifdef _WIN32
    CHECK(_mkdir(path)==0);
#else
    CHECK(mkdir(path,0700)==0);
#endif
}
#ifdef PORTABLE_ENABLE_SDL3
static void discovery_checks(const char *root,const char *zip_root)
{
    PortableModCatalog *catalog=calloc(1u,sizeof *catalog);CHECK(catalog!=NULL);
    char path[512],bad[512],missing[512];
    snprintf(bad,sizeof bad,"%s/z-bad",root);make_directory(bad);
    snprintf(missing,sizeof missing,"%s/z-missing",root);make_directory(missing);
    snprintf(path,sizeof path,"%s/z-bad/mod.json",root);write_bytes(path,"{}",2u);
    CHECK(portable_mods_discover(root,catalog)&&catalog->count==5u&&!catalog->overflow);
    CHECK(strcmp(catalog->entries[0].id,"a")==0&&catalog->entries[0].valid&&
        strcmp(catalog->entries[1].id,"b")==0&&catalog->entries[1].valid&&
        strcmp(catalog->entries[2].id,"c")==0&&catalog->entries[2].valid);
    CHECK(strcmp(catalog->entries[3].id,"z-bad")==0&&!catalog->entries[3].valid&&catalog->entries[3].error[0]);
    CHECK(strcmp(catalog->entries[4].id,"z-missing")==0&&!catalog->entries[4].valid&&catalog->entries[4].error[0]);
    CHECK(remove(path)==0&&test_rmdir(bad)==0&&test_rmdir(missing)==0);
    CHECK(portable_mods_discover(zip_root,catalog));
    size_t both=0u;int zipped=0;
    for(size_t i=0u;i<catalog->count;++i){
        if(i)CHECK(strcmp(catalog->entries[i-1u].id,catalog->entries[i].id)<0);
        if(strcmp(catalog->entries[i].id,"both")==0){
            ++both;CHECK(catalog->entries[i].valid&&!catalog->entries[i].from_zip);
        }
        if(strcmp(catalog->entries[i].id,"zipped")==0)
            zipped=catalog->entries[i].valid&&catalog->entries[i].from_zip;
    }
    CHECK(both==1u&&zipped);
    char overflow_root[128];CHECK(test_temp_path(overflow_root,sizeof overflow_root,"catalog-overflow",1)==0);
    for(unsigned i=0u;i<129u;++i){snprintf(path,sizeof path,"%s/mod-%03u",overflow_root,i);make_directory(path);}
    CHECK(!portable_mods_discover(overflow_root,catalog)&&catalog->overflow&&
        catalog->count==128u&&strstr(catalog->error,"128"));
    for(unsigned i=0u;i<129u;++i){snprintf(path,sizeof path,"%s/mod-%03u",overflow_root,i);CHECK(test_rmdir(path)==0);}
    CHECK(test_rmdir(overflow_root)==0);free(catalog);
}
#endif

/* Serialized BND1 packs exercise the production loader, not the raw merge helper. */
static void model_row(uint8_t *row, uint32_t target, uint32_t mesh)
{
    memset(row, 0, 44);
    zip_u32(row, PORTABLE_MODEL_DRIVER);
    zip_u32(row + 4, target);
    zip_u32(row + 16, mesh);
    for (unsigned i = 0; i < 3; ++i)
        zip_u32(row + 20 + 4 * i, 65536);
}
static PortableAssetSectionView model_registry(uint8_t *bytes, unsigned count, unsigned version)
{
    memcpy(bytes, "BND1", 4);
    zip_u32(bytes + 4, count);
    return (PortableAssetSectionView){
        .kind = 98, .content_id = 1, .version = version, .bytes = bytes, .size = 8u + 44u * count};
}
static void model_pack(const char *path, const PortableAssetSectionView *sections, unsigned count)
{
    size_t total = 112u + 40u * count;
    for (unsigned i = 0; i < count; ++i)
        total += (sections[i].size + 7u) & ~(size_t)7u;
    uint8_t *bytes = calloc(1, total);
    CHECK(bytes != NULL);
    memcpy(bytes, "MKSCPACK", 8);
    zip_u16(bytes + 8, 1);
    zip_u16(bytes + 10, 112);
    zip_u32(bytes + 12, 0x12345678);
    zip_u32(bytes + 16, (uint32_t)total);
    zip_u32(bytes + 24, count);
    zip_u32(bytes + 28, 40);
    zip_u32(bytes + 32, 112);
    size_t offset = 112u + 40u * count;
    for (unsigned i = 0; i < count; ++i)
    {
        const PortableAssetSectionView *section = &sections[i];
        uint8_t *row = bytes + 112 + 40 * i;
        zip_u32(row, section->kind);
        zip_u32(row + 4, section->content_id);
        zip_u32(row + 8, section->version);
        zip_u32(row + 12, section->flags);
        zip_u32(row + 16, (uint32_t)offset);
        zip_u32(row + 24, (uint32_t)section->size);
        memcpy(bytes + offset, section->bytes, section->size);
        offset += (section->size + 7u) & ~(size_t)7u;
    }
    const uint8_t *empty = NULL;
    const size_t zero = 0;
    portable_asset_pack_sha256(&empty, &zero, 1, bytes + 72);
    const uint8_t *whole = bytes;
    portable_asset_pack_sha256(&whole, &total, 1, bytes + 40);
    write_bytes(path, bytes, total);
    free(bytes);
}
static void model_mod(const char *root, const char *id, const char *targets,
                      const PortableAssetSectionView *sections, unsigned count)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, id);
    make_directory(path);
    manifest(root, id, targets);
    snprintf(path, sizeof path, "%s/%s/content.pack", root, id);
    model_pack(path, sections, count);
}
static void model_count(const PortableAssetPackView *pack, size_t expected, uint32_t present,
                        uint32_t absent)
{
    PortableModelCatalog catalog = {0};
    CHECK(portable_model_catalog_open(&catalog, pack));
    CHECK(catalog.count == expected);
    if (present)
        CHECK(portable_model_catalog_find(&catalog, 1, present, 0, 0) != NULL);
    if (absent)
        CHECK(portable_model_catalog_find(&catalog, 1, absent, 0, 0) == NULL);
    portable_model_catalog_close(&catalog);
}
static void model_overlay_checks(const PortableAssetPackView *original)
{
    char root[256], path[512];
    CHECK(test_temp_path(root, sizeof root, "model-overlays", 1) == 0);
    static const char both[] = ",\"targets\":[\"2d\",\"3d\"]";
    uint8_t mesh[88] = {0}, material[16] = {0}, base_rows[52] = {0}, rows[8 + 256 * 44] = {0};
    memcpy(mesh, "MSH1", 4);
    zip_u32(mesh + 4, 3);
    zip_u32(mesh + 8, 1);
    zip_u32(mesh + 12, 1);
    zip_u32(mesh + 16, 9);
    mesh[82] = 1;
    mesh[84] = 2;
    memcpy(material, "MTL1", 4);
    zip_u32(material + 4, 0xff123456);
    model_row(base_rows + 8, 16, 7);
    PortableAssetSectionView seed_sections[] = {
        {.kind = 96, .content_id = 7, .version = 1, .bytes = mesh, .size = sizeof mesh},
        {.kind = 97, .content_id = 9, .version = 1, .bytes = material, .size = sizeof material},
        model_registry(base_rows, 1, 1)};
    PortableAssetPackView seed = {.section_count = 3, .resolved_sections = seed_sections}, base;
    const PortableAssetPackView *base_layers[] = {original, &seed};
    PortableAssetSectionView *composed = calloc(original->section_count + 3, sizeof *composed);
    CHECK(composed);
    CHECK(portable_asset_pack_compose(base_layers, 2, composed, original->section_count + 3,
                                      &base) == PORTABLE_ASSET_PACK_OK);
    model_count(&base, 1, 16, 0);
    model_row(rows + 8, 17, 7);
    PortableAssetSectionView registry = model_registry(rows, 1, 2);
    model_mod(root, "a", both, &registry, 1);
    model_row(rows + 8, 18, 7);
    model_mod(root, "b", both, &registry, 1);
    PortableModLoadReport report;
    const char disjoint[][PORTABLE_MOD_ID_CAPACITY] = {"a", "b"};
    const char reverse[][PORTABLE_MOD_ID_CAPACITY] = {"b", "a"};
    PortableModSet *first = portable_mods_load_strict(&base, root, disjoint, 2, &report);
    PortableModSet *second = portable_mods_load_strict(&base, root, reverse, 2, &report);
    CHECK(first && second);
    model_count(portable_mods_pack(first), 3, 16, 0);
    model_count(portable_mods_pack_3d(first), 3, 18, 0);
    PortableAssetSectionView left, right;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(first), 98, 1, &left) ==
          PORTABLE_ASSET_PACK_OK);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(second), 98, 1, &right) ==
          PORTABLE_ASSET_PACK_OK);
    CHECK(left.version == 1 && right.version == 1 && left.size == right.size &&
          !memcmp(left.bytes, right.bytes, left.size));
    portable_mods_free(second);
    portable_mods_free(first);
    /* Later invalid winner suppresses its valid predecessor; duplicates remain ambiguous. */
    model_row(rows + 8, 17, 999);
    model_mod(root, "invalid", both, &registry, 1);
    const char invalid[][PORTABLE_MOD_ID_CAPACITY] = {"a", "b", "invalid"};
    first = portable_mods_load_strict(&base, root, invalid, 3, &report);
    CHECK(first);
    model_count(portable_mods_pack(first), 2, 18, 17);
    portable_mods_free(first);
    model_row(rows + 8, 17, 7);
    model_row(rows + 52, 17, 999);
    registry = model_registry(rows, 2, 2);
    model_mod(root, "duplicates", both, &registry, 1);
    const char duplicates[][PORTABLE_MOD_ID_CAPACITY] = {"a", "duplicates"};
    first = portable_mods_load_strict(&base, root, duplicates, 2, &report);
    CHECK(first);
    model_count(portable_mods_pack(first), 1, 16, 17);
    portable_mods_free(first);
    const char repaired[][PORTABLE_MOD_ID_CAPACITY] = {"duplicates", "a"};
    first = portable_mods_load_strict(&base, root, repaired, 2, &report);
    CHECK(first);
    model_count(portable_mods_pack(first), 2, 17, 0);
    portable_mods_free(first);
    registry = model_registry(rows, 1, 1);
    model_row(rows + 8, 19, 7);
    model_mod(root, "reset", both, &registry, 1);
    const char reset[][PORTABLE_MOD_ID_CAPACITY] = {"a", "b", "reset"};
    first = portable_mods_load_strict(&base, root, reset, 3, &report);
    CHECK(first);
    model_count(portable_mods_pack(first), 1, 19, 16);
    portable_mods_free(first);
    /* Binding references are resolved from the final pack, including later assets. */
    registry = model_registry(rows, 1, 2);
    model_row(rows + 8, 20, 70);
    model_mod(root, "forward", both, &registry, 1);
    PortableAssetSectionView late = seed_sections[0];
    late.content_id = 70;
    model_mod(root, "assets", both, &late, 1);
    const char forward[][PORTABLE_MOD_ID_CAPACITY] = {"forward", "assets"};
    first = portable_mods_load_strict(&base, root, forward, 2, &report);
    CHECK(first);
    model_count(portable_mods_pack(first), 2, 20, 0);
    portable_mods_free(first);
    /* 256 raw rows admitted; the 257th candidate is rejected, then a valid replacement still works. */
    for (unsigned i = 0; i < 255; ++i)
        model_row(rows + 8 + 44 * i, 100 + i, 7);
    registry = model_registry(rows, 255, 2);
    model_mod(root, "full", both, &registry, 1);
    registry = model_registry(rows, 1, 2);
    model_row(rows + 8, 400, 7);
    model_mod(root, "overflow", both, &registry, 1);
    model_row(rows + 8, 100, 7);
    model_mod(root, "repair", both, &registry, 1);
    const char capacity[][PORTABLE_MOD_ID_CAPACITY] = {"full", "overflow", "repair"};
    first = portable_mods_load_report(&base, root, capacity, 3, &report);
    CHECK(first);
    CHECK(portable_mods_loaded_count(first) == 2 && report.entries[0].accepted &&
          !report.entries[1].accepted && report.entries[2].accepted);
    model_count(portable_mods_pack(first), 256, 100, 400);
    portable_mods_free(first);
    CHECK(!portable_mods_load_strict(&base, root, capacity, 3, &report));
    /* Malformed v2 does not erase preceding accepted registry state. */
    rows[0] = 0;
    model_mod(root, "malformed", both, &registry, 1);
    const char malformed[][PORTABLE_MOD_ID_CAPACITY] = {"a", "malformed", "b"};
    first = portable_mods_load_report(&base, root, malformed, 3, &report);
    CHECK(first);
    CHECK(portable_mods_loaded_count(first) == 2 && !report.entries[1].accepted);
    model_count(portable_mods_pack(first), 3, 17, 0);
    portable_mods_free(first);
    CHECK(!portable_mods_load_strict(&base, root, malformed, 3, &report));
    /* Each domain inherits only its own accepted predecessor, including identities. */
    registry = model_registry(rows, 1, 2);
    model_row(rows + 8, 17, 7);
    model_mod(root, "two", ",\"targets\":[\"2d\"]", &registry, 1);
    model_row(rows + 8, 18, 7);
    model_mod(root, "three", ",\"targets\":[\"3d\"]", &registry, 1);
    model_row(rows + 8, 19, 7);
    model_mod(root, "both", both, &registry, 1);
    const char domains[][PORTABLE_MOD_ID_CAPACITY] = {"two", "three", "both"};
    first = portable_mods_load_domains_strict(&base, &base, root, domains, 3, &report);
    CHECK(first);
    model_count(portable_mods_pack(first), 3, 17, 18);
    model_count(portable_mods_pack_3d(first), 3, 18, 17);
    model_row(rows + 8, 17, 999);
    snprintf(path, sizeof path, "%s/two/content.pack", root);
    model_pack(path, &registry, 1);
    second = portable_mods_load_domains_strict(&base, &base, root, domains, 3, &report);
    CHECK(second);
    CHECK(!portable_mod_identity_equal(portable_mods_identity(first),
                                       portable_mods_identity(second)));
    CHECK(portable_mod_identity_equal(portable_mods_identity_3d(first),
                                      portable_mods_identity_3d(second)));
    model_count(portable_mods_pack(second), 2, 19, 17);
    model_count(portable_mods_pack_3d(second), 3, 18, 17);
    portable_mods_free(second);
    portable_mods_free(first);
    /* Failure in only the second projection rolls back BOTH candidate views. */
    model_row(rows + 8, 16, 7);
    for (unsigned i = 1; i < 256; ++i)
        model_row(rows + 8 + 44 * i, 99 + i, 7);
    registry = model_registry(rows, 256, 1);
    const PortableAssetPackView full_registry = {.section_count = 1,
                                                 .resolved_sections = &registry};
    const PortableAssetPackView *full_layers[] = {&base, &full_registry};
    PortableAssetSectionView *domain_sections =
        calloc(original->section_count + 3, sizeof *domain_sections);
    CHECK(domain_sections);
    PortableAssetPackView full_3d;
    CHECK(portable_asset_pack_compose(full_layers, 2, domain_sections, original->section_count + 3,
                                      &full_3d) == PORTABLE_ASSET_PACK_OK);
    const char partial[][PORTABLE_MOD_ID_CAPACITY] = {"both", "repair"};
    first = portable_mods_load_domains_report(&base, &full_3d, root, partial, 2, &report);
    CHECK(first && portable_mods_loaded_count(first) == 1 && !report.entries[0].accepted &&
          report.entries[1].accepted);
    model_count(portable_mods_pack(first), 2, 100, 19);
    model_count(portable_mods_pack_3d(first), 256, 100, 19);
    portable_mods_free(first);
    free(domain_sections);
    /* Uninstalled archive review uses the same strict production composition. */
    snprintf(path, sizeof path, "%s/a/mod.json", root);
    size_t manifest_size, pack_size, zip_size, directory;
    uint8_t *json = read_bytes(path, &manifest_size);
    snprintf(path, sizeof path, "%s/a/content.pack", root);
    uint8_t *pack_bytes = read_bytes(path, &pack_size);
    TestZipEntry entries[] = {{"mod.json", json, manifest_size, 0},
                              {"content.pack", pack_bytes, pack_size, 0}};
    uint8_t *zip = test_zip(entries, 2, &zip_size, &directory);
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
    PortableModArchive *archive = portable_mod_archive_open_memory(zip, zip_size, NULL, error);
    CHECK(archive);
    first = portable_mods_load_domains_archive_strict(&base, &base, root, disjoint, 2, "a", archive,
                                                      &report);
    CHECK(first);
    portable_mod_archive_free(archive); /* Loaded owners retain no archive borrow. */
    model_count(portable_mods_pack(first), 3, 17, 0);
    model_count(portable_mods_pack_3d(first), 3, 18, 0);
    portable_mods_free(first);
    free(json);
    free(pack_bytes);
    /* Raw v2 bases cannot be silently interpreted as empty legacy registries. */
    PortableAssetPackView bad_base = base;
    registry = model_registry(rows, 1, 2);
    const PortableAssetPackView raw = {.section_count = 1, .resolved_sections = &registry};
    const PortableAssetPackView *bad_layers[] = {&base, &raw};
    PortableAssetSectionView *bad_sections =
        calloc(original->section_count + 3, sizeof *bad_sections);
    CHECK(bad_sections);
    CHECK(portable_asset_pack_compose(bad_layers, 2, bad_sections, original->section_count + 3,
                                      &bad_base) == PORTABLE_ASSET_PACK_OK);
    CHECK(!portable_mods_load_domains_strict(&bad_base, &base, root, NULL, 0, &report));
    free(bad_sections);
    free(composed);
    const char *created[] = {"a",         "b",      "invalid", "duplicates", "reset",
                             "forward",   "assets", "full",    "overflow",   "repair",
                             "malformed", "two",    "three",   "both"};
    for (unsigned i = 0; i < sizeof created / sizeof created[0]; ++i)
    {
        snprintf(path, sizeof path, "%s/%s/content.pack", root, created[i]);
        CHECK(remove(path) == 0);
        snprintf(path, sizeof path, "%s/%s/mod.json", root, created[i]);
        CHECK(remove(path) == 0);
        snprintf(path, sizeof path, "%s/%s", root, created[i]);
        CHECK(test_rmdir(path) == 0);
    }
    CHECK(test_rmdir(root) == 0);
}
int main(int argc, char **argv)
{
    archive_install_checks();
    CHECK(argc==4);
    const char valid[]="{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.2.3\",\"api_version\":1,\"pack\":\"pack/content.pack\",\"depends\":[\"base-mod\"],\"load_after\":[\"colors\"]}";
    PortableModManifest parsed;char error[PORTABLE_MOD_ERROR_CAPACITY];
    CHECK(portable_mod_manifest_parse(valid,strlen(valid),&parsed,error));
    CHECK(strcmp(parsed.id,"demo")==0&&parsed.dependency_count==1u&&parsed.after_count==1u&&parsed.targets==PORTABLE_MOD_TARGET_2D);
    const char *invalid_targets[]={"[]","[\"2d\",\"2d\"]","[\"4d\"]","\"3d\"","[3]"};
    for(unsigned i=0u;i<sizeof invalid_targets/sizeof invalid_targets[0];++i){
        char text[512];snprintf(text,sizeof text,"{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"main.lua\",\"targets\":%s}",invalid_targets[i]);
        CHECK(!portable_mod_manifest_parse(text,strlen(text),&parsed,error));
    }
    const char scripted[]="{\"id\":\"lua-only\",\"name\":\"Lua\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"lua/main.lua\"}";
    CHECK(portable_mod_manifest_parse(scripted,strlen(scripted),&parsed,error));
    CHECK(strcmp(parsed.script,"lua/main.lua")==0&&parsed.pack[0]=='\0');
    const char *bad_scripts[]={
        "{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1}",
        "{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"main.txt\"}",
        "{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"../main.lua\"}",
        "{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"/main.lua\"}",
        "{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\".lua\"}",
        "{\"id\":\"x\",\"name\":\"X\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":7}"};
    for(unsigned i=0u;i<sizeof bad_scripts/sizeof bad_scripts[0];++i)
        CHECK(!portable_mod_manifest_parse(bad_scripts[i],strlen(bad_scripts[i]),&parsed,error)&&error[0]!='\0');
    CHECK(portable_mod_manifest_parse(valid,strlen(valid),&parsed,error));
    const PortableModManifest preserved=parsed;
    const char *bad[]={"{}","[]","{\"id\":\"a\",\"id\":\"b\"}","{\"unknown\":1}",
        "{\"api_version\":2}","{\"api_version\":true}","{\"id\":\"../escape\"}",
        "{\"pack\":\"../content.pack\"}","{\"pack\":\"/content.pack\"}","{\"pack\":\"a/./b\"}",
        "{\"pack\":\"C:/content.pack\"}","{\"version\":\"1.2\"}","{\"version\":\"01.2.3\"}",
        "{\"depends\":[\"a\",\"a\"]}","{\"load_after\":[3]}","{} trailing","{\"name\":\"x\",}"};
    for(unsigned i=0u;i<sizeof bad/sizeof bad[0];++i){
        CHECK(!portable_mod_manifest_parse(bad[i],strlen(bad[i]),&parsed,error)&&error[0]!='\0');
        CHECK(memcmp(&parsed,&preserved,sizeof parsed)==0);
    }
    char root[256],path[512];CHECK(test_temp_path(root,sizeof root,"mods",1)==0);
    const char *names[]={"a","b","c"};
    for(unsigned i=0u;i<3u;++i){
        snprintf(path,sizeof path,"%s/%s",root,names[i]);
#ifdef _WIN32
        CHECK(_mkdir(path)==0);
#else
        CHECK(mkdir(path,0700)==0);
#endif
    }
    size_t base_size,mod_size;uint8_t *base_bytes=read_bytes(argv[1],&base_size),*mod_bytes=read_bytes(argv[2],&mod_size);
    PortableAssetPackView base;CHECK(portable_asset_pack_open(base_bytes,base_size,&base)==PORTABLE_ASSET_PACK_OK);
    projection_checks(&base);
    model_overlay_checks(&base);
    for(unsigned i=0u;i<3u;++i){snprintf(path,sizeof path,"%s/%s/content.pack",root,names[i]);write_bytes(path,i==0u?base_bytes:mod_bytes,i==0u?base_size:mod_size);}
    manifest(root,"a","");manifest(root,"b",",\"depends\":[\"a\"]");manifest(root,"c",",\"load_after\":[\"b\"]");
    char ids[3][PORTABLE_MOD_ID_CAPACITY]={"a","b","c"};
    PortableModSet *mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==3u);
    PortableAssetSectionView road;
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),8u,24u,&road)==PORTABLE_ASSET_PACK_OK);
    CHECK(road.size==16u&&memcmp(road.bytes,"mod presentation",16u)==0);
    /* Even a presentation-only set changes the digest ghosts and records use,
     * reproducibly for the same ordered set. */
    uint8_t first_digest[32];
    memcpy(first_digest,portable_mods_pack(mods)->deterministic_digest,32u);
    CHECK(memcmp(base.deterministic_digest,first_digest,32u)!=0);
    const PortableModIdentity *identity=portable_mods_identity(mods);
    CHECK(identity->count==3u&&strcmp(identity->mods[1].id,"b")==0&&strcmp(identity->mods[1].version,"1.0.0")==0);
    CHECK(memcmp(identity->mods[1].fingerprint,identity->mods[2].fingerprint,8u)!=0); /* manifests differ */
    strict_checks(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,first_digest);
    CHECK(memcmp(first_digest,portable_mods_pack(mods)->deterministic_digest,32u)==0);
#ifdef PORTABLE_ENABLE_SDL3
    discovery_checks(root,argv[3]);
#else
    PortableModCatalog *catalog=calloc(1u,sizeof *catalog);CHECK(catalog!=NULL);
    CHECK(!portable_mods_discover(root,catalog)&&strstr(catalog->error,"filesystem backend"));
    free(catalog);
#endif
    portable_mods_free(mods);
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&memcmp(first_digest,portable_mods_pack(mods)->deterministic_digest,32u)==0);
    portable_mods_free(mods);
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,0u);
    CHECK(mods!=NULL&&memcmp(base.deterministic_digest,portable_mods_pack(mods)->deterministic_digest,32u)==0);
    portable_mods_free(mods);
    strcpy(ids[0],"c");strcpy(ids[1],"b");strcpy(ids[2],"a");
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==1u);portable_mods_free(mods);
    strcpy(ids[0],"a");strcpy(ids[1],"b");strcpy(ids[2],"c");
    snprintf(path,sizeof path,"%s/a/mod.json",root);write_bytes(path,"{}",2u);
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==1u);portable_mods_free(mods);
    manifest(root,"a","");snprintf(path,sizeof path,"%s/b/content.pack",root);write_bytes(path,"bad",3u);
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==2u);portable_mods_free(mods);
    PortableModLoadReport rejected;
    CHECK(portable_mods_load_strict(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u,&rejected)==NULL&&
        rejected.entries[0].accepted&&!rejected.entries[1].accepted&&rejected.entries[2].accepted&&
        rejected.entries[1].message[0]&&rejected.first_error[0]);
    /* A script-only mod sits between pack layers without a layer of its own. */
    snprintf(path,sizeof path,"%s/b/mod.json",root);
    const char script_manifest[]="{\"id\":\"b\",\"name\":\"Lua\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"main.lua\",\"depends\":[\"a\"]}";
    write_bytes(path,script_manifest,strlen(script_manifest));
    snprintf(path,sizeof path,"%s/c/content.pack",root);write_bytes(path,mod_bytes,mod_size);
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==2u); /* main.lua missing: b and dependent-free c */
    portable_mods_free(mods);
    snprintf(path,sizeof path,"%s/b/main.lua",root);write_bytes(path,"x = 1\n",6u);
    mods=portable_mods_load(&base,root,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,3u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==3u);
    const char *script_id=NULL,*source=NULL;size_t script_size=0u;
    CHECK(!portable_mods_script(mods,0u,&script_id,&source,&script_size));
    CHECK(portable_mods_script(mods,1u,&script_id,&source,&script_size)&&strcmp(script_id,"b")==0&&
          script_size==6u&&memcmp(source,"x = 1\n",6u)==0);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),8u,24u,&road)==PORTABLE_ASSET_PACK_OK&&
          road.size==16u&&memcmp(road.bytes,"mod presentation",16u)==0);
    portable_mods_free(mods);
    CHECK(remove(path)==0);
    for(unsigned i=0u;i<3u;++i){
        snprintf(path,sizeof path,"%s/%s/mod.json",root,names[i]);CHECK(remove(path)==0);
        snprintf(path,sizeof path,"%s/%s/content.pack",root,names[i]);CHECK(remove(path)==0);
        snprintf(path,sizeof path,"%s/%s",root,names[i]);CHECK(test_rmdir(path)==0);
    }
    CHECK(test_rmdir(root)==0);
    /* Zip mods: same validation as folders; unsafe archives are refused. */
    char zip_ids[][PORTABLE_MOD_ID_CAPACITY]={"zipped","stored","nested","both"};
    mods=portable_mods_load(&base,argv[3],(const char (*)[PORTABLE_MOD_ID_CAPACITY])zip_ids,4u);
    CHECK(mods!=NULL&&portable_mods_loaded_count(mods)==4u);
    CHECK(portable_asset_pack_find_section(portable_mods_pack(mods),8u,24u,&road)==PORTABLE_ASSET_PACK_OK&&
          road.size==16u&&memcmp(road.bytes,"mod presentation",16u)==0);
    CHECK(portable_mods_script(mods,0u,&script_id,&source,&script_size)&&strcmp(script_id,"zipped")==0&&
          script_size==27u*64u&&memcmp(source,"function on_tick(race) end\n",27u)==0);
    CHECK(portable_mods_script(mods,1u,&script_id,&source,&script_size)&&strcmp(script_id,"stored")==0);
    CHECK(!portable_mods_script(mods,2u,&script_id,&source,&script_size));
    CHECK(portable_mods_script(mods,3u,&script_id,&source,&script_size)&&strcmp(script_id,"both")==0);
    CHECK(strcmp(portable_mods_identity(mods)->mods[2].id,"nested")==0);
    portable_mods_free(mods);
    const char *refused[]={"traversal","backslash","wrong-id","duplicate","bomb","lying","bad-crc","encrypted","missing"};
    for(unsigned i=0u;i<sizeof refused/sizeof refused[0];++i){
        char one[1][PORTABLE_MOD_ID_CAPACITY];snprintf(one[0],sizeof one[0],"%s",refused[i]);
        mods=portable_mods_load(&base,argv[3],(const char (*)[PORTABLE_MOD_ID_CAPACITY])one,1u);
        if(mods==NULL||portable_mods_loaded_count(mods)!=0u){fprintf(stderr,"zip %s was not refused\n",refused[i]);exit(1);}
        portable_mods_free(mods);
    }
    /* Inflate: stored, fixed and dynamic blocks round-trip through the loader
     * above; truncated or overlong streams fail without writing past output. */
    static const uint8_t fixed_hello[]={0xcb,0x48,0xcd,0xc9,0xc9,0x07,0x00}; /* "hello" */
    uint8_t out[8]={0};
    CHECK(portable_inflate(fixed_hello,sizeof fixed_hello,out,5u)&&memcmp(out,"hello",5u)==0);
    CHECK(!portable_inflate(fixed_hello,sizeof fixed_hello,out,4u));
    CHECK(!portable_inflate(fixed_hello,sizeof fixed_hello-2u,out,5u));
    CHECK(!portable_inflate(fixed_hello,sizeof fixed_hello,out,6u));
    CHECK(portable_crc32((const uint8_t *)"123456789",9u)==UINT32_C(0xcbf43926));
    free(base_bytes);free(mod_bytes);
    puts("mods: PASS strict manifests, explicit order, dependency failures, pack rejection, effective lookup, script-only mods, mod identity, zip mods");return 0;
}
