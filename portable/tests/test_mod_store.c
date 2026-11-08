#undef NDEBUG /* Every check is an assert: keep them in release builds. */
#include "mod_store.h"
#include "settings.h"
#include "file_read.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_index(const char *path,const PortableStoreEntry *entry,const char *download)
{
    FILE *f=fopen(path,"wb");assert(f);
    assert(fprintf(f,"{\"format_version\":1,\"id\":\"fixture\",\"name\":\"Local fixture\",\"mods\":[{"
        "\"id\":\"%s\",\"name\":\"Example\",\"author\":\"Fixture\",\"version\":\"1.0.0\","
        "\"description\":\"Local test\",\"download_url\":\"%s\",\"sha256\":\"%s\",\"size\":%u,"
        "\"license\":\"LicenseRef-Test\",\"compatible_game_versions\":[\"1\"],\"screenshots\":[]}]}",
        entry->id,download,entry->sha256,entry->size)>0);
    assert(!fclose(f));
}
static void rejected(const char *json,PortableStore *store)
{
    char error[256];unsigned count=store->count;
    assert(!portable_store_parse(json,strlen(json),store,error));assert(store->count==count);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    PortableStore *store=calloc(1,sizeof *store),*fixture=calloc(1,sizeof *fixture);
    assert(store&&fixture);char error[256];
    assert(portable_store_load("builtin://official",NULL,store,error));assert(store->count==3u);
    assert(portable_store_compatible(&store->entries[0]));
    rejected("{\"format_version\":2,\"id\":\"x\",\"name\":\"X\",\"mods\":[]}",store);
    rejected("{\"format_version\":1,\"id\":\"x\",\"id\":\"x\",\"name\":\"X\",\"mods\":[]}",store);
    rejected("{\"format_version\":1,\"id\":\"../x\",\"name\":\"X\",\"mods\":[]}",store);
    rejected("{\"format_version\":1,\"id\":\"x\",\"name\":\"X\",\"mods\":[],\"extra\":0}",store);
    rejected("{\"format_version\":1,\"id\":\"x\",\"name\":\"X\",\"mods\":[]",store);
    assert(!portable_store_url_valid("http://example.test/index.json"));
    assert(!portable_store_url_valid("file://server/index.json"));
    assert(!portable_store_url_valid("https://example.test/\nfile"));
    char zip[4096],index[4096],url[4096],download[4096],output[4096];
    snprintf(zip,sizeof zip,"%s/fixture.zip",argv[1]);
    snprintf(index,sizeof index,"%s/index.json",argv[1]);
    assert(portable_store_file_url(index,url,sizeof url));
    assert(portable_store_file_url(zip,download,sizeof download));
    snprintf(output,sizeof output,"%s/output.zip",argv[1]);
    assert(portable_store_download(store,0,zip,error));
    write_index(index,&store->entries[0],download);
    assert(portable_store_load(url,argv[1],fixture,error));assert(!fixture->offline);
    assert(portable_store_download(fixture,0,output,error));
    uint8_t *bytes=NULL;size_t size=0;
    assert(portable_file_read_bounded(output,100000,&bytes,&size)==PORTABLE_FILE_READ_OK);
    assert(portable_store_verify(&fixture->entries[0],bytes,size));
    bytes[0]^=1u;assert(!portable_store_verify(&fixture->entries[0],bytes,size));
    assert(!portable_store_verify(&fixture->entries[0],bytes,size-1u));free(bytes);
    assert(!remove(index));assert(portable_store_load(url,argv[1],fixture,error));assert(fixture->offline);
    /* A failed verification must not replace even a preexisting download file. */
    fixture->entries[0].sha256[0]=fixture->entries[0].sha256[0]=='0'?'1':'0';
    assert(!portable_store_download(fixture,0,output,error));
    assert(portable_file_read_bounded(output,100000,&bytes,&size)==PORTABLE_FILE_READ_OK);
    assert(portable_store_verify(&store->entries[0],bytes,size));free(bytes);
    strcpy(fixture->source,"https://example.invalid/index.json");
    assert(!portable_store_download(fixture,0,output,error)); /* No network: rejects origin first. */
    strcpy(store->entries[0].compatible_game_versions[0],"999");
    assert(!portable_store_download(store,0,output,error));
    PortableSettings *settings=malloc(sizeof *settings),*loaded=malloc(sizeof *loaded);assert(settings&&loaded);
    portable_settings_defaults(settings);settings->store_url_count=1;
    strcpy(settings->store_urls[0],url);
    assert(portable_settings_save(index,settings));assert(portable_settings_load(index,loaded,error)==1);
    assert(loaded->store_url_count==1&&!strcmp(loaded->store_urls[0],url));
    const char *bad="{\"store_urls\":[\"http://insecure.test\"]}";
    assert(!portable_settings_parse(bad,strlen(bad),loaded,error));assert(!loaded->store_url_count);
    free(settings);free(loaded);free(store);free(fixture);
    puts("store: local catalog, offline cache, SHA-256, compatibility, origin and settings passed");return 0;
}
