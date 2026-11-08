#include "mod_manager.h"
#include "mod_install.h"
#include "test_temp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#endif
#define CHECK(x) do { if(!(x)){fprintf(stderr,"mod manager line %d: %s\n",__LINE__,#x);exit(1);} } while(0)

static void write_text(const char *path,const char *text)
{
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    CHECK(fwrite(text,1u,strlen(text),file)==strlen(text));CHECK(fclose(file)==0);
}

static void make_directory(const char *path)
{
#ifdef _WIN32
    CHECK(_mkdir(path)==0);
#else
    CHECK(mkdir(path,0700)==0);
#endif
}

static void make_script(const char *directory,const char *id,const char *extra,const char *script)
{
    char path[512],json[1024];
    snprintf(path,sizeof path,"%s/%s",directory,id);
    make_directory(path);
    snprintf(path,sizeof path,"%s/%s/mod.json",directory,id);
    snprintf(json,sizeof json,"{\"id\":\"%s\",\"name\":\"Authored test\",\"version\":\"1.0.0\",\"api_version\":1,\"script\":\"main.lua\"%s}",id,extra);
    write_text(path,json);
    snprintf(path,sizeof path,"%s/%s/main.lua",directory,id);write_text(path,script);
}

static void remove_script(const char *directory,const char *id)
{
    char path[512];
    snprintf(path,sizeof path,"%s/%s/main.lua",directory,id);CHECK(remove(path)==0);
    snprintf(path,sizeof path,"%s/%s/mod.json",directory,id);CHECK(remove(path)==0);
    snprintf(path,sizeof path,"%s/%s",directory,id);CHECK(test_rmdir(path)==0);
}

static void put_u16(FILE *file,uint16_t value)
{
    CHECK(fputc((int)(value&255u),file)!=EOF&&fputc((int)(value>>8u),file)!=EOF);
}
static void put_u32(FILE *file,uint32_t value)
{
    put_u16(file,(uint16_t)value);put_u16(file,(uint16_t)(value>>16u));
}

/* Authored stored ZIP entries exercise the production archive reader without
 * external tools or game assets. Offsets and CRCs belong to these test bytes. */
static void make_archive_bytes(const char *path,const char *id,const char *version,
    const char *extra,const char *script,const char *pack,size_t pack_size)
{
    char manifest[1024];
    const int length=snprintf(manifest,sizeof manifest,
        "{\"id\":\"%s\",\"name\":\"Install fixture\",\"version\":\"%s\",\"api_version\":1,\"script\":\"main.lua\"%s%s}",
        id,version,pack?",\"pack\":\"content.pack\"":"",extra);
    CHECK(length>0&&(size_t)length<sizeof manifest);
    const char *names[]={"mod.json","main.lua","content.pack"};
    const char *contents[]={manifest,script,pack};
    const unsigned count=pack?3u:2u;
    uint32_t offsets[3],sizes[3],crcs[3];
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    for(unsigned i=0u;i<count;++i){
        const long at=ftell(file);CHECK(at>=0);
        offsets[i]=(uint32_t)at;sizes[i]=(uint32_t)(i==2u?pack_size:strlen(contents[i]));
        crcs[i]=portable_crc32((const uint8_t *)contents[i],sizes[i]);
        put_u32(file,UINT32_C(0x04034b50));put_u16(file,20u);
        put_u16(file,0u);put_u16(file,0u);put_u16(file,0u);put_u16(file,0u);
        put_u32(file,crcs[i]);put_u32(file,sizes[i]);put_u32(file,sizes[i]);
        put_u16(file,(uint16_t)strlen(names[i]));put_u16(file,0u);
        CHECK(fwrite(names[i],1u,strlen(names[i]),file)==strlen(names[i]));
        CHECK(fwrite(contents[i],1u,sizes[i],file)==sizes[i]);
    }
    const long directory=ftell(file);CHECK(directory>=0);
    for(unsigned i=0u;i<count;++i){
        put_u32(file,UINT32_C(0x02014b50));put_u16(file,20u);put_u16(file,20u);
        put_u16(file,0u);put_u16(file,0u);put_u16(file,0u);put_u16(file,0u);
        put_u32(file,crcs[i]);put_u32(file,sizes[i]);put_u32(file,sizes[i]);
        put_u16(file,(uint16_t)strlen(names[i]));put_u16(file,0u);put_u16(file,0u);
        put_u16(file,0u);put_u16(file,0u);put_u32(file,0u);put_u32(file,offsets[i]);
        CHECK(fwrite(names[i],1u,strlen(names[i]),file)==strlen(names[i]));
    }
    const long end=ftell(file);CHECK(end>=directory);
    put_u32(file,UINT32_C(0x06054b50));put_u16(file,0u);put_u16(file,0u);
    put_u16(file,(uint16_t)count);put_u16(file,(uint16_t)count);
    put_u32(file,(uint32_t)(end-directory));put_u32(file,(uint32_t)directory);put_u16(file,0u);
    CHECK(fclose(file)==0);
}
static void make_archive(const char *path,const char *id,const char *version,
    const char *extra,const char *script,const char *pack)
{
    make_archive_bytes(path,id,version,extra,script,pack,pack?strlen(pack):0u);
}

static uint8_t *read_bytes(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL&&fseek(file,0,SEEK_END)==0);
    const long length=ftell(file);CHECK(length>0&&fseek(file,0,SEEK_SET)==0);
    uint8_t *bytes=malloc((size_t)length);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1u,(size_t)length,file)==(size_t)length&&fclose(file)==0);
    *size=(size_t)length;return bytes;
}
static void write_bytes(const char *path,const uint8_t *bytes,size_t size)
{
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    CHECK(fwrite(bytes,1u,size,file)==size&&fclose(file)==0);
}
static void expect_bytes(const char *path,const uint8_t *expected,size_t size)
{
    size_t actual_size=0u;uint8_t *actual=read_bytes(path,&actual_size);
    CHECK(actual_size==size&&memcmp(actual,expected,size)==0);free(actual);
}

static void model_word(uint8_t *bytes,uint32_t value)
{
    for(unsigned i=0u;i<4u;++i)bytes[i]=(uint8_t)(value>>(8u*i));
}
static void model_binding(uint8_t *row,uint32_t target)
{
    memset(row,0,44u);model_word(row,1u);model_word(row+4u,target);model_word(row+16u,7u);
    for(unsigned i=0u;i<3u;++i)model_word(row+20u+4u*i,65536u);
}
/* One authored binary v2 registry, including a deliberate count/size mismatch
 * control, travels through the real ZIP reader and manager loader. */
static void overlay_pack(uint8_t bytes[208],unsigned declared_count)
{
    memset(bytes,0,208u);memcpy(bytes,"MKSCPACK",8u);
    bytes[8]=1u;bytes[10]=112u;model_word(bytes+12u,UINT32_C(0x12345678));
    model_word(bytes+16u,208u);model_word(bytes+24u,1u);model_word(bytes+28u,40u);
    model_word(bytes+32u,112u);
    model_word(bytes+112u,98u);model_word(bytes+116u,1u);model_word(bytes+120u,2u);
    model_word(bytes+128u,152u);model_word(bytes+136u,52u);
    memcpy(bytes+152u,"BND1",4u);model_word(bytes+156u,declared_count);
    model_binding(bytes+160u,9999u);
    const uint8_t *empty=NULL;const size_t zero=0u;
    portable_asset_pack_sha256(&empty,&zero,1u,bytes+72u);
    const uint8_t *whole=bytes;const size_t size=208u;
    portable_asset_pack_sha256(&whole,&size,1u,bytes+40u);
}

static void overlay_rejection_checks(const char *directory,const PortableAssetPackView *base,
    const PortableModManagerCandidate *active,const char *picked,const char *destination,
    const uint8_t *installed,size_t installed_size,const char *settings,const char *settings_text)
{
    uint8_t full[8u+256u*44u];memcpy(full,"BND1",4u);model_word(full+4u,256u);
    for(unsigned i=0u;i<256u;++i)model_binding(full+8u+44u*i,1000u+i);
    const PortableAssetSectionView registry={.kind=98u,.content_id=1u,.version=1u,
        .bytes=full,.size=sizeof full};
    const PortableAssetPackView layer={.resolved_sections=&registry,.section_count=1u};
    const PortableAssetPackView *layers[]={base,&layer};
    PortableAssetSectionView storage[16];PortableAssetPackView full_base;
    CHECK(portable_asset_pack_compose(layers,2u,storage,16u,&full_base)==PORTABLE_ASSET_PACK_OK);
    const PortableAssetPackView saved_base=full_base;
    const char (*ids)[PORTABLE_MOD_ID_CAPACITY]=
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])active->ids;
    const PortableModIdentity identity=*portable_mod_identity_current();
    const PortableModIdentity active_identity=*portable_mods_identity(active->mods);
    const PortableAssetPackView live=*portable_mods_pack(active->mods);
    char folder[512],manifest[512],pack_path[512],error[PORTABLE_MOD_ERROR_CAPACITY];
    snprintf(folder,sizeof folder,"%s/overlay-bad",directory);make_directory(folder);
    snprintf(manifest,sizeof manifest,"%s/overlay-bad/mod.json",directory);
    snprintf(pack_path,sizeof pack_path,"%s/overlay-bad/content.pack",directory);
    write_text(manifest,"{\"id\":\"overlay-bad\",\"name\":\"Overlay rejection\",\"version\":\"1.0.0\",\"api_version\":1,\"pack\":\"content.pack\"}");
    const char selected[][PORTABLE_MOD_ID_CAPACITY]={"base","dependent","overlay-bad"};
    uint8_t pack[208];
    for(unsigned malformed=0u;malformed<2u;++malformed){
        overlay_pack(pack,malformed?2u:1u);write_bytes(pack_path,pack,sizeof pack);
        make_archive_bytes(picked,"fresh","3.0.0","","local value = 3",
            (const char *)pack,sizeof pack);
        /* A valid review can become incompatible with a separate saved/running
         * selection. Validation must not publish the reviewed update. */
        if(!malformed){
            PortableModInstall *review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);
            CHECK(review!=NULL&&portable_mod_install_is_update(review));
            CHECK(!portable_mod_install_validate(review,&full_base,directory,ids,active->count,error)&&strstr(error,"256"));
            portable_mod_install_free(review);
        }
        CHECK(portable_mod_install_prepare(&full_base,directory,picked,ids,active->count,error)==NULL);
        CHECK(strstr(error,malformed?"invalid version-2 registry":"256"));
        PortableModManager *manager=portable_mod_manager_create(directory,selected,3u);CHECK(manager);
        CHECK(portable_mod_manager_prepare(manager,&full_base,NULL)==NULL);
        CHECK(strstr(portable_mod_manager_notice(manager),malformed?"invalid version-2 registry":"256"));
        CHECK(portable_mod_manager_count(manager)==3u&&
            strcmp(portable_mod_manager_id(manager,2u),"overlay-bad")==0);
        portable_mod_manager_free(manager);
        expect_bytes(destination,installed,installed_size);
        expect_bytes(settings,(const uint8_t *)settings_text,strlen(settings_text));
        expect_bytes(pack_path,pack,sizeof pack);
        CHECK(!memcmp(&full_base,&saved_base,sizeof full_base));
        CHECK(!memcmp(portable_mods_pack(active->mods),&live,sizeof live));
        CHECK(!memcmp(portable_mods_identity(active->mods),&active_identity,sizeof active_identity));
        CHECK(!memcmp(portable_mod_identity_current(),&identity,sizeof identity));
        CHECK(active->count==2u&&portable_script_host_active_count(active->scripts)==2u);
    }
    CHECK(remove(pack_path)==0&&remove(manifest)==0&&test_rmdir(folder)==0);
}

static void installation_checks(const char *directory,const PortableAssetPackView *base,
    const PortableModManagerCandidate *active)
{
    char picked[512],destination[512],settings[512],temporary[576],error[PORTABLE_MOD_ERROR_CAPACITY];
    snprintf(picked,sizeof picked,"%s/owner-selected-name.zip",directory);
    snprintf(destination,sizeof destination,"%s/fresh.zip",directory);
    snprintf(settings,sizeof settings,"%s/settings.json",directory);
    const char settings_text[]="{\"mods\":[\"base\",\"dependent\"]}";
    write_text(settings,settings_text);
    portable_mod_identity_set_current(portable_mods_identity(active->mods));
    char identity_before[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY],identity_after[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY];
    CHECK(portable_mod_identity_format(portable_mod_identity_current(),identity_before,sizeof identity_before));
    uint8_t active_digest[32];memcpy(active_digest,portable_mods_pack(active->mods)->deterministic_digest,32u);
    const char (*ids)[PORTABLE_MOD_ID_CAPACITY]=(const char (*)[PORTABLE_MOD_ID_CAPACITY])active->ids;
    make_archive(picked,"fresh","1.0.0","","local value = 1",NULL);
    size_t first_size=0u;uint8_t *first=read_bytes(picked,&first_size);
    PortableModInstall *review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);
    CHECK(review!=NULL&&!portable_mod_install_is_update(review)&&
        strcmp(portable_mod_install_manifest(review)->id,"fresh")==0);
    CHECK(!test_path_exists(destination)); /* Reviewing/cancelling is read-only. */
    portable_mod_install_free(review);CHECK(!test_path_exists(destination));
    review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);CHECK(review!=NULL);
    write_text(picked,"source changed after review"); /* Publish the captured, validated bytes. */
    CHECK(portable_mod_install_publish(review,error));expect_bytes(destination,first,first_size);
    CHECK(!portable_mod_install_publish(review,error));portable_mod_install_free(review);

    make_archive(picked,"fresh","2.0.0","","local value = 42",NULL);
    size_t second_size=0u;uint8_t *second=read_bytes(picked,&second_size);
    review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);
    CHECK(review!=NULL&&portable_mod_install_is_update(review));
    expect_bytes(destination,first,first_size);portable_mod_install_free(review);
    expect_bytes(destination,first,first_size); /* Declining update retains the old ZIP. */
    review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);CHECK(review!=NULL);
    CHECK(portable_mod_install_publish(review,error));portable_mod_install_free(review);
    expect_bytes(destination,second,second_size);

    overlay_rejection_checks(directory,base,active,picked,destination,second,second_size,
        settings,settings_text);

    make_archive(picked,"fresh","3.0.0","","invalid Lua !!!",NULL);
    CHECK(portable_mod_install_prepare(base,directory,picked,ids,active->count,error)==NULL&&strstr(error,"Lua"));
    expect_bytes(destination,second,second_size);
    make_archive(picked,"fresh","3.0.0",",\"targets\":[\"3d\"]","invalid Lua !!!",NULL);
    CHECK(portable_mod_install_prepare(base,directory,picked,ids,active->count,error)==NULL&&
        strstr(error,"fresh: 3D Lua validation failed"));
    expect_bytes(destination,second,second_size);
    make_archive(picked,"fresh","3.0.0",",\"targets\":[\"3d\"]","local value = 3",NULL);
    const uint8_t profile_byte=41u;
    PortableAssetSectionView profile_section={.kind=95u,.content_id=1u,.version=1u,
        .flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,.bytes=&profile_byte,.size=1u};
    PortableAssetPackView profile={.resolved_sections=&profile_section,.section_count=1u};
    PortableAssetSectionView profile_storage[8];PortableAssetPackView selected;
    const PortableAssetPackView *profile_layers[]={base,&profile};
    CHECK(portable_asset_pack_compose(profile_layers,2u,profile_storage,8u,&selected)==PORTABLE_ASSET_PACK_OK);
    review=portable_mod_install_prepare_domains(&selected,base,directory,picked,ids,active->count,error);
    CHECK(review!=NULL&&portable_mod_install_validate_domains(review,&selected,base,directory,ids,active->count,error));
    portable_mod_install_free(review);expect_bytes(destination,second,second_size);
    make_archive(picked,"fresh","3.0.0","","local value = 3","bad pack");
    CHECK(portable_mod_install_prepare(base,directory,picked,ids,active->count,error)==NULL&&error[0]);
    expect_bytes(destination,second,second_size);
    make_archive(picked,"fresh","3.0.0",",\"depends\":[\"absent\"]","local value = 3",NULL);
    CHECK(portable_mod_install_prepare(base,directory,picked,ids,active->count,error)==NULL&&strstr(error,"depends"));
    expect_bytes(destination,second,second_size);
    make_archive(picked,"base","3.0.0","","local value = 3",NULL);
    CHECK(portable_mod_install_prepare(base,directory,picked,ids,active->count,error)==NULL&&strstr(error,"folder"));
    expect_bytes(destination,second,second_size);

    /* load_after applies only when that mod is enabled. The same reviewed
     * archive must also be safe for the separately saved/running selection. */
    make_archive(picked,"fresh","3.0.0",",\"load_after\":[\"dependent\"]","local value = 3",NULL);
    const char base_only[][PORTABLE_MOD_ID_CAPACITY]={"base"};
    const char wrong_order[][PORTABLE_MOD_ID_CAPACITY]={"base","fresh","dependent"};
    review=portable_mod_install_prepare(base,directory,picked,base_only,1u,error);
    CHECK(review!=NULL&&portable_mod_install_is_update(review));
    CHECK(!portable_mod_install_validate(review,base,directory,wrong_order,3u,error)&&strstr(error,"load_after"));
    expect_bytes(destination,second,second_size);
    CHECK(portable_mod_install_validate(review,base,directory,ids,active->count,error));
    portable_mod_install_free(review);expect_bytes(destination,second,second_size);

    make_archive(picked,"fresh","3.0.0","","local value = 3",NULL);
    review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);CHECK(review!=NULL);
    write_text(destination,"external change");
    CHECK(!portable_mod_install_publish(review,error)&&strstr(error,"changed"));
    expect_bytes(destination,(const uint8_t *)"external change",strlen("external change"));
    portable_mod_install_free(review);write_bytes(destination,second,second_size);
    review=portable_mod_install_prepare(base,directory,picked,ids,active->count,error);CHECK(review!=NULL);
    for(unsigned i=0u;i<32u;++i){
        snprintf(temporary,sizeof temporary,"%s.install-%u.tmp",destination,i);write_text(temporary,"occupied");
    }
    CHECK(!portable_mod_install_publish(review,error)&&strstr(error,"occupied"));
    expect_bytes(destination,second,second_size);
    for(unsigned i=0u;i<32u;++i){
        snprintf(temporary,sizeof temporary,"%s.install-%u.tmp",destination,i);
        expect_bytes(temporary,(const uint8_t *)"occupied",8u);CHECK(remove(temporary)==0);
    }
    portable_mod_install_free(review);
    const char installed[][PORTABLE_MOD_ID_CAPACITY]={"base","dependent","fresh"};
    PortableModLoadReport report;
    PortableModSet *reopened=portable_mods_load_strict(base,directory,installed,3u,&report);
    CHECK(reopened!=NULL&&portable_mods_loaded_count(reopened)==3u&&
        strcmp(portable_mods_identity(reopened)->mods[2].version,"2.0.0")==0);
    portable_mods_free(reopened);
    CHECK(portable_script_host_active_count(active->scripts)==2u&&
        memcmp(active_digest,portable_mods_pack(active->mods)->deterministic_digest,32u)==0);
    CHECK(portable_mod_identity_format(portable_mod_identity_current(),identity_after,sizeof identity_after)&&
        strcmp(identity_before,identity_after)==0);
    expect_bytes(settings,(const uint8_t *)settings_text,strlen(settings_text));

    char full_directory[256],full_path[512];
    CHECK(test_temp_path(full_directory,sizeof full_directory,"mod-install-full",1)==0);
    for(unsigned i=0u;i<PORTABLE_MOD_CATALOG_LIMIT;++i){
        snprintf(full_path,sizeof full_path,"%s/installed-%03u",full_directory,i);make_directory(full_path);
    }
    make_archive(picked,"catalog-new","1.0.0","","local value = 1",NULL);
    CHECK(portable_mod_install_prepare(base,full_directory,picked,NULL,0u,error)==NULL&&strstr(error,"128"));
    snprintf(full_path,sizeof full_path,"%s/catalog-new.zip",full_directory);CHECK(!test_path_exists(full_path));
    snprintf(full_path,sizeof full_path,"%s/installed-127",full_directory);CHECK(test_rmdir(full_path)==0);
    review=portable_mod_install_prepare(base,full_directory,picked,NULL,0u,error);CHECK(review!=NULL);
    make_directory(full_path); /* Another install consumes the last slot during review. */
    CHECK(!portable_mod_install_publish(review,error)&&strstr(error,"128"));
    portable_mod_install_free(review);
    snprintf(full_path,sizeof full_path,"%s/catalog-new.zip",full_directory);CHECK(!test_path_exists(full_path));
    for(unsigned i=0u;i<PORTABLE_MOD_CATALOG_LIMIT;++i){
        snprintf(full_path,sizeof full_path,"%s/installed-%03u",full_directory,i);CHECK(test_rmdir(full_path)==0);
    }
    CHECK(test_rmdir(full_directory)==0);
    portable_mod_identity_set_current(NULL);
    CHECK(remove(picked)==0&&remove(destination)==0&&remove(settings)==0);
    free(second);free(first);
}

static void multiple_roots(const char *parent,const PortableAssetPackView *base)
{
    char primary[384],extra[384],relocated[384],borrowed[384],archive_path[512],installed_path[512];
    snprintf(primary,sizeof primary,"%s/primary",parent);make_directory(primary);
    snprintf(extra,sizeof extra,"%s/extra",parent);make_directory(extra);
    snprintf(relocated,sizeof relocated,"%s/relocated",parent);make_directory(relocated);
    make_script(primary,"local","","local value = 10");
    make_script(extra,"external",",\"targets\":[\"2d\",\"3d\"]","local value = 20");
    make_script(relocated,"external",",\"targets\":[\"2d\",\"3d\"]","local value = 20");
    /* Root paths do not enter either domain's identity. */
    const char external[1][PORTABLE_MOD_ID_CAPACITY]={"external"};
    PortableModSet *before=portable_mods_load_strict(base,extra,external,1u,NULL);
    PortableModSet *after=portable_mods_load_strict(base,relocated,external,1u,NULL);
    CHECK(before && after);
    CHECK(!memcmp(portable_mods_identity(before),portable_mods_identity(after),sizeof(PortableModIdentity)));
    CHECK(!memcmp(portable_mods_pack(before)->deterministic_digest,portable_mods_pack(after)->deterministic_digest,32u));
    CHECK(!memcmp(portable_mods_pack_3d(before)->deterministic_digest,portable_mods_pack_3d(after)->deterministic_digest,32u));
    portable_mods_free(before);portable_mods_free(after);

    snprintf(borrowed,sizeof borrowed,"%s",extra);
    PortableModRoots roots={{primary,borrowed},2u};
    const char selected[2][PORTABLE_MOD_ID_CAPACITY]={"local","external"};
    PortableModManager *manager=portable_mod_manager_create_roots(&roots,selected,2u);CHECK(manager);
    borrowed[0]='!'; /* Manager must not borrow dialog or stack path buffers. */
    CHECK(portable_mod_manager_refresh(manager));
    PortableModManagerCandidate *active=portable_mod_manager_prepare(manager,base,NULL);CHECK(active);
    CHECK(active->count==2u && portable_script_host_active_count(active->scripts)==2u);
    const PortableModCatalog *catalog=portable_mod_manager_catalog(manager);
    CHECK(catalog->count==2u && catalog->entries[0].source_root==1u && catalog->entries[1].source_root==0u);
    roots.directories[1]=extra;
    /* Same-root folder wins over its ZIP; it is not an ambiguous cross-root ID. */
    snprintf(installed_path,sizeof installed_path,"%s/local.zip",primary);
    make_archive(installed_path,"local","2.0.0","","local zip = 1",NULL);
    CHECK(portable_mod_manager_refresh(manager));
    CHECK(portable_mod_manager_catalog(manager)->entries[1].valid && !portable_mod_manager_catalog(manager)->entries[1].from_zip);
    CHECK(remove(installed_path)==0);

    make_script(primary,"external","","local value = 99");
    CHECK(portable_mod_manager_refresh(manager));
    catalog=portable_mod_manager_catalog(manager);
    CHECK(!catalog->entries[0].valid && strstr(catalog->entries[0].error,"duplicate ID"));
    CHECK(portable_mod_manager_prepare(manager,base,NULL)==NULL);
    CHECK(active->count==2u && portable_script_host_active_count(active->scripts)==2u);
    PortableModSet *independent=portable_mods_load_roots_domains_strict(base,base,&roots,selected,1u,NULL);
    CHECK(independent && portable_mods_loaded_count(independent)==1u);portable_mods_free(independent);
    remove_script(primary,"external");

    snprintf(archive_path,sizeof archive_path,"%s/review.zip",parent);
    make_archive(archive_path,"external","2.0.0","","local update = 1",NULL);
    char error[PORTABLE_MOD_ERROR_CAPACITY];
    CHECK(portable_mod_install_prepare_roots_domains(base,base,&roots,archive_path,selected,2u,error)==NULL);
    CHECK(strstr(error,"conflicts with root 1"));
    CHECK(remove(archive_path)==0);
    make_archive(archive_path,"installed","1.0.0","","local installed = 1",NULL);
    snprintf(borrowed,sizeof borrowed,"%s",extra);roots.directories[1]=borrowed;
    PortableModInstall *install=portable_mod_install_prepare_roots_domains(base,base,&roots,archive_path,selected,2u,error);
    CHECK(install);
    borrowed[0]='!';
    make_script(extra,"installed","","local collision = 1");
    CHECK(!portable_mod_install_publish(install,error) && strstr(error,"conflicts with root 1"));
    remove_script(extra,"installed");
    CHECK(portable_mod_install_publish(install,error));portable_mod_install_free(install);
    snprintf(installed_path,sizeof installed_path,"%s/installed.zip",primary);CHECK(remove(installed_path)==0);
    CHECK(remove(archive_path)==0);
    portable_mod_manager_candidate_free(active);portable_mod_manager_free(manager);
    remove_script(primary,"local");remove_script(extra,"external");remove_script(relocated,"external");
    CHECK(test_rmdir(primary)==0 && test_rmdir(extra)==0 && test_rmdir(relocated)==0);
}

/* The Games screen asks for card art on every frame. Loading the imported pack
 * must never happen in that call: it starts a worker, answers NULL at once and
 * the manager survives a refresh or a free while the worker is still running. */
static void preview_worker_checks(const char *directory,const PortableAssetPackView *base)
{
#ifdef PORTABLE_ENABLE_SDL3
    const char none[1][PORTABLE_MOD_ID_CAPACITY]={{0}};
    PortableModManager *manager=portable_mod_manager_create(directory,none,0u);CHECK(manager!=NULL);
    CHECK(!portable_mod_manager_preview_busy(manager));
    CHECK(portable_mod_manager_preview_image(manager,"base",base)==NULL);
    CHECK(portable_mod_manager_preview_busy(manager)||portable_mod_manager_preview_poll(manager)==0);
    for(unsigned waited=0u;portable_mod_manager_preview_busy(manager);++waited){
        CHECK(waited<5000u);
        (void)portable_mod_manager_preview_poll(manager);SDL_Delay(1);
    }
    CHECK(portable_mod_manager_preview_image(manager,"base",base)==NULL); /* No art: cached, no new worker. */
    CHECK(!portable_mod_manager_preview_busy(manager));
    /* Refresh while a worker runs discards its result; free waits for it. */
    CHECK(portable_mod_manager_preview_image(manager,"dependent",base)==NULL);
    CHECK(portable_mod_manager_refresh(manager));
    CHECK(portable_mod_manager_preview_image(manager,"dependent",base)==NULL);
    /* Free never waits: the orphaned worker is reaped by drain. */
    portable_mod_manager_free(manager);
    portable_mod_manager_drain();
#else
    (void)directory;(void)base;
#endif
}

/* One-step enable/disable keep a valid order without a manual review. */
static void one_step_checks(const char *parent,const PortableAssetPackView *base)
{
    char directory[384];
    snprintf(directory,sizeof directory,"%s/one-step",parent);make_directory(directory);
    make_script(directory,"lib","","local value = 1");
    make_script(directory,"addon",",\"depends\":[\"lib\"]","local value = 2");
    make_script(directory,"theme",",\"load_after\":[\"addon\"]","local value = 3");
    make_script(directory,"orphan",",\"depends\":[\"absent\"]","local value = 4");
    make_script(directory,"broken","","this is not valid Lua !!!");
    const char initial[1][PORTABLE_MOD_ID_CAPACITY]={"theme"};
    PortableModManager *manager=portable_mod_manager_create(directory,initial,1u);CHECK(manager!=NULL);
    /* addon pulls in lib; both land before theme, which loads after addon. */
    CHECK(portable_mod_manager_enable(manager,"addon"));
    CHECK(portable_mod_manager_count(manager)==3u);
    CHECK(!strcmp(portable_mod_manager_id(manager,0u),"lib"));
    CHECK(!strcmp(portable_mod_manager_id(manager,1u),"addon"));
    CHECK(!strcmp(portable_mod_manager_id(manager,2u),"theme"));
    PortableModManagerCandidate *candidate=portable_mod_manager_prepare(manager,base,NULL);
    CHECK(candidate!=NULL&&candidate->count==3u);
    portable_mod_manager_candidate_free(candidate);
    CHECK(portable_mod_manager_enable(manager,"addon")&&portable_mod_manager_count(manager)==3u);
    /* Failures name the mod and leave the staged order unchanged. */
    CHECK(!portable_mod_manager_enable(manager,"orphan"));
    CHECK(strstr(portable_mod_manager_notice(manager),"orphan requires absent")!=NULL);
    CHECK(!portable_mod_manager_enable(manager,"gone"));
    CHECK(strstr(portable_mod_manager_notice(manager),"gone is not installed")!=NULL);
    CHECK(portable_mod_manager_count(manager)==3u);
    /* An unrelated mod goes last, so it wins over earlier content. */
    CHECK(portable_mod_manager_enable(manager,"broken"));
    CHECK(!strcmp(portable_mod_manager_id(manager,3u),"broken"));
    /* Disabling lib also disables addon, which needs it; theme only loads
     * after addon and stays. */
    CHECK(portable_mod_manager_disable(manager,"lib")==2u);
    CHECK(portable_mod_manager_count(manager)==2u);
    CHECK(!strcmp(portable_mod_manager_id(manager,0u),"theme"));
    CHECK(portable_mod_manager_disable(manager,"lib")==0u);
    CHECK(portable_mod_manager_select(manager,initial,1u)&&portable_mod_manager_count(manager)==1u);
    CHECK(!strcmp(portable_mod_manager_id(manager,0u),"theme"));
    portable_mod_manager_free(manager);
    static const char *const ids[]={"lib","addon","theme","orphan","broken"};
    for(size_t i=0u;i<sizeof ids/sizeof ids[0];++i)remove_script(directory,ids[i]);
    CHECK(test_rmdir(directory)==0);
}

int main(void)
{
    char directory[256];CHECK(test_temp_path(directory,sizeof directory,"mod-manager",1)==0);
    make_script(directory,"base","","local value = 1");
    make_script(directory,"dependent",",\"depends\":[\"base\"]","local value = 2");
    make_script(directory,"broken","","this is not valid Lua !!!");
    const char initial[2][PORTABLE_MOD_ID_CAPACITY]={"gone","dependent"};
    PortableModManager *manager=portable_mod_manager_create(directory,initial,2u);CHECK(manager!=NULL);
    CHECK(portable_mod_manager_catalog(manager)->count==3u);
    char preview[256];CHECK(portable_mod_manager_preview(manager,0u,preview,sizeof preview));
    CHECK(strstr(preview,"Missing")!=NULL);
    CHECK(portable_mod_manager_toggle(manager,"gone"));
    CHECK(portable_mod_manager_count(manager)==1u);
    CHECK(portable_mod_manager_preview(manager,0u,preview,sizeof preview));
    CHECK(strstr(preview,"Requires base")!=NULL);
    CHECK(portable_mod_manager_toggle(manager,"base"));
    CHECK(portable_mod_manager_move(manager,1u,0u));
    CHECK(strcmp(portable_mod_manager_id(manager,0u),"base")==0);
    CHECK(!portable_mod_manager_move(manager,0u,2u));
    CHECK(!portable_mod_manager_toggle(manager,"absent"));

    /* Script-only mods need no game assets. A composed base also exercises the
     * borrowed-view contract without requiring an original pack or raw bytes. */
    const uint8_t payload=0u;
    PortableAssetSectionView section={0};section.kind=PORTABLE_ASSET_SECTION_HUD_GRAPHICS;
    section.version=1u;section.bytes=&payload;section.size=1u;
    PortableAssetPackView base={0};base.resolved_sections=&section;base.section_count=1u;
    multiple_roots(directory,&base);
    preview_worker_checks(directory,&base);
    one_step_checks(directory,&base);
    const PortableAssetPackView unchanged=base;
    PortableModManagerCandidate *active=portable_mod_manager_prepare(manager,&base,NULL);
    CHECK(active!=NULL&&active->count==2u&&portable_script_host_active_count(active->scripts)==2u);
    CHECK(memcmp(&base,&unchanged,sizeof base)==0);
    CHECK(portable_mod_manager_move(manager,0u,1u));
    CHECK(portable_mod_manager_prepare(manager,&base,NULL)==NULL);
    CHECK(portable_script_host_active_count(active->scripts)==2u&&active->count==2u);
    CHECK(strcmp(active->ids[0],"base")==0); /* Failed apply did not mutate prior owners. */
    CHECK(portable_mod_manager_move(manager,1u,0u));
    CHECK(portable_mod_manager_toggle(manager,"broken"));
    CHECK(portable_mod_manager_prepare(manager,&base,NULL)==NULL);
    CHECK(strstr(portable_mod_manager_notice(manager),"broken: Lua script failed to load")!=NULL);
    CHECK(portable_script_host_active_count(active->scripts)==2u);
    CHECK(portable_mod_manager_toggle(manager,"broken"));
    make_script(directory,"three-broken",",\"targets\":[\"3d\"]","this is invalid Lua !!!");
    make_script(directory,"three-valid",",\"targets\":[\"3d\"]","local value = 3");
    CHECK(portable_mod_manager_refresh(manager));
    CHECK(portable_mod_manager_toggle(manager,"three-broken"));
    CHECK(portable_mod_manager_prepare(manager,&base,NULL)==NULL&&
        strstr(portable_mod_manager_notice(manager),"three-broken: 3D Lua script failed to load"));
    CHECK(portable_script_host_active_count(active->scripts)==2u&&active->count==2u);
    CHECK(portable_mod_manager_toggle(manager,"three-broken"));
    CHECK(portable_mod_manager_toggle(manager,"three-valid"));
    PortableModManagerCandidate *projected=portable_mod_manager_prepare(manager,&base,NULL);
    CHECK(projected!=NULL&&projected->count==3u&&portable_script_host_active_count(projected->scripts)==2u&&
        portable_mods_identity(projected->mods)->count==2u&&portable_mods_identity_3d(projected->mods)->count==1u);
    CHECK(memcmp(portable_mods_pack(active->mods)->deterministic_digest,
        portable_mods_pack(projected->mods)->deterministic_digest,32u)==0);
    const uint8_t profile_payload=93u;
    PortableAssetSectionView profile_sections[2]={section,
        {.kind=95u,.content_id=1u,.version=1u,.flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,
         .bytes=&profile_payload,.size=1u}};
    PortableAssetPackView profile_base={.resolved_sections=profile_sections,.section_count=2u,.driving_profile=2u};
    /* Composition computes the authored profile digest without requiring an
     * original imported pack or decoding this loader-only sentinel payload. */
    PortableAssetSectionView profile_storage[2];
    const PortableAssetPackView *profile_layers[]={&base,&profile_base};
    PortableAssetPackView composed_profile;
    CHECK(portable_asset_pack_compose(profile_layers,2u,profile_storage,2u,&composed_profile)==PORTABLE_ASSET_PACK_OK);
    composed_profile.driving_profile=2u;
    PortableModManagerCandidate *profiled=portable_mod_manager_prepare_domains(manager,&composed_profile,&base,NULL);
    CHECK(profiled!=NULL&&profiled->count==projected->count&&portable_script_host_active_count(profiled->scripts)==2u);
    CHECK(memcmp(portable_mods_pack(profiled->mods)->deterministic_digest,
        portable_mods_pack(projected->mods)->deterministic_digest,32u)!=0);
    CHECK(memcmp(portable_mods_pack_3d(profiled->mods)->deterministic_digest,
        portable_mods_pack_3d(projected->mods)->deterministic_digest,32u)==0);
    CHECK(memcmp(portable_mods_identity_3d(profiled->mods),portable_mods_identity_3d(projected->mods),sizeof(PortableModIdentity))==0);
    CHECK(portable_mod_manager_toggle(manager,"three-broken"));
    CHECK(portable_mod_manager_prepare_domains(manager,&composed_profile,&base,NULL)==NULL);
    CHECK(portable_script_host_active_count(profiled->scripts)==2u&&
        memcmp(portable_mods_pack_3d(profiled->mods)->deterministic_digest,
            portable_mods_pack_3d(projected->mods)->deterministic_digest,32u)==0);
    CHECK(portable_mod_manager_toggle(manager,"three-broken"));
    portable_mod_manager_candidate_free(profiled);
    portable_mod_manager_candidate_free(projected);
    CHECK(portable_mod_manager_toggle(manager,"three-valid"));
    remove_script(directory,"three-broken");remove_script(directory,"three-valid");
    CHECK(portable_mod_manager_refresh(manager));
    CHECK(portable_mod_manager_remove(manager,1u)&&portable_mod_manager_remove(manager,0u));
    PortableModManagerCandidate *empty=portable_mod_manager_prepare(manager,&base,NULL);
    CHECK(empty!=NULL&&empty->count==0u&&empty->scripts==NULL);
    portable_mod_manager_candidate_free(empty);
    CHECK(portable_mod_manager_cycle_source(manager,0,1));
    CHECK(!strcmp(portable_mod_manager_source(manager,0),"@base"));
    CHECK(portable_mod_manager_cycle_source(manager,0,-1)&&!portable_mod_manager_source(manager,0)[0]);
    CHECK(portable_mod_manager_toggle(manager,"base"));
    CHECK(portable_mod_manager_cycle_source(manager,0,-1));
    CHECK(!strcmp(portable_mod_manager_source(manager,0),"base"));
    CHECK(portable_mod_manager_toggle(manager,"base")&&!portable_mod_manager_source(manager,0)[0]);
    char missing_sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY]={{"absent"},{0},{0}};
    portable_mod_manager_set_sources(manager,(const char (*)[PORTABLE_MOD_ID_CAPACITY])missing_sources);
    CHECK(!portable_mod_manager_prepare(manager,&base,NULL));
    CHECK(strstr(portable_mod_manager_notice(manager),"must be enabled"));
    memset(missing_sources,0,sizeof missing_sources);
    portable_mod_manager_set_sources(manager,(const char (*)[PORTABLE_MOD_ID_CAPACITY])missing_sources);
    installation_checks(directory,&base,active);
    portable_mod_manager_candidate_free(active);
    portable_mod_manager_free(manager);
    remove_script(directory,"base");remove_script(directory,"dependent");remove_script(directory,"broken");
    CHECK(test_rmdir(directory)==0);
    /* Returning to stock requires no installed files, even when discovery
     * cannot open the directory. A nonempty selection must still fail closed. */
    manager=portable_mod_manager_create(directory,NULL,0u);CHECK(manager!=NULL);
    CHECK(portable_mod_manager_notice(manager)[0]!='\0');
    empty=portable_mod_manager_prepare(manager,&base,NULL);
    CHECK(empty!=NULL&&empty->count==0u&&empty->scripts==NULL);
    portable_mod_manager_candidate_free(empty);portable_mod_manager_free(manager);
    manager=portable_mod_manager_create(directory,initial,2u);CHECK(manager!=NULL);
    CHECK(portable_mod_manager_prepare(manager,&base,NULL)==NULL);
    CHECK(portable_mod_manager_count(manager)==2u);
    portable_mod_manager_free(manager);
    puts("portable mod manager: PASS");return 0;
}
