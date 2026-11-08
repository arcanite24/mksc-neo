#include "mod_store.h"
#include "json_reader.h"
#include "file_read.h"
#include "file_replace.h"
#include "asset_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PORTABLE_STORE_CURL
#include <curl/curl.h>
#endif
#include "store_payload.h"

enum { INDEX_LIMIT = 512 * 1024, ARCHIVE_LIMIT = 96 * 1024 * 1024 };
static int fail(char *error,const char *message)
{
    if(error)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s",message);
    return 0;
}
static int prefix(const char *s,const char *p) { return strncmp(s,p,strlen(p))==0; }
int portable_store_url_valid(const char *url)
{
    if(!url || strlen(url)>=PORTABLE_STORE_URL_CAPACITY)return 0;
    for(const unsigned char *p=(const unsigned char *)url;*p;++p)
        if(*p<32u || *p>=127u || *p=='\\')return 0;
    return (prefix(url,"https://") && url[8] && url[8]!='/') ||
        (prefix(url,"file:///") && url[8]) || !strcmp(url,"builtin://official");
}
int portable_store_file_url(const char *path,char *url,size_t capacity)
{
    if(!path||!url||!capacity)return 0;
    const int drive=((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z')) && path[1]==':' && (path[2]=='/'||path[2]=='\\');
    if(path[0]!='/'&&!drive)return 0;
    const int n=snprintf(url,capacity,drive?"file:///%s":"file://%s",path);
    if(n<0||(size_t)n>=capacity)return 0;
    for(char *p=url+7;*p;++p)if(*p=='\\')*p='/';
    return portable_store_url_valid(url);
}
static int text(PortableJsonReader *r,char *out,size_t capacity)
{
    if(!portable_json_string(r,out,capacity) || !out[0])return 0;
    for(const unsigned char *p=(const unsigned char *)out;*p;++p)if(*p<32u || *p==127u)return 0;
    return 1;
}
static int strings(PortableJsonReader *r,char *out,size_t stride,unsigned limit,unsigned *count)
{
    if(!portable_json_take(r,'['))return 0;
    *count=0;
    if(portable_json_take(r,']'))return 1;
    do {
        if(*count==limit || !text(r,out+(*count)*stride,stride))return 0;
        ++*count;
        if(portable_json_take(r,']'))return 1;
    } while(portable_json_take(r,','));
    return 0;
}
static int entry_parse(PortableJsonReader *r,PortableStoreEntry *e)
{
    static const char *const names[]={"id","name","author","version","description","download_url","sha256","size","license","compatible_game_versions","screenshots"};
    char *fields[]={e->id,e->name,e->author,e->version,e->description,e->download_url,e->sha256,NULL,e->license};
    const size_t capacities[]={sizeof e->id,sizeof e->name,sizeof e->author,sizeof e->version,sizeof e->description,sizeof e->download_url,sizeof e->sha256,0,sizeof e->license};
    unsigned seen=0;
    if(!portable_json_take(r,'{'))return 0;
    do {
        char key[40];
        if(!text(r,key,sizeof key)||!portable_json_take(r,':'))return 0;
        unsigned i=0;while(i<11u && strcmp(key,names[i]))++i;
        if(i==11u || (seen&(1u<<i)))return 0;
        seen|=1u<<i;
        if(i==7u){if(!portable_json_number(r,&e->size))return 0;}
        else if(i==9u){if(!strings(r,(char *)e->compatible_game_versions,32u,8u,&e->version_count))return 0;}
        else if(i==10u){if(!strings(r,(char *)e->screenshots,PORTABLE_STORE_URL_CAPACITY,4u,&e->screenshot_count))return 0;}
        else if(!text(r,fields[i],capacities[i]))return 0;
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return 0;
    } while(1);
    if(seen!=2047u || !portable_mod_id_valid(e->id) || !e->version_count ||
       !e->size || e->size>ARCHIVE_LIMIT || strlen(e->sha256)!=64u)return 0;
    for(unsigned i=0;i<64u;++i)if(!((e->sha256[i]>='0'&&e->sha256[i]<='9')||(e->sha256[i]>='a'&&e->sha256[i]<='f')))return 0;
    /* Reuse manifest semantic-version validation. */
    for(const char *p=e->version;*p;++p)if((*p<'0'||*p>'9')&&*p!='.')return 0;
    char manifest[256],error[PORTABLE_MOD_ERROR_CAPACITY];PortableModManifest m;
    snprintf(manifest,sizeof manifest,"{\"id\":\"%s\",\"name\":\"Store\",\"version\":\"%s\",\"api_version\":1,\"script\":\"main.lua\"}",e->id,e->version);
    if(!portable_mod_manifest_parse(manifest,strlen(manifest),&m,error))return 0;
    if(!portable_store_url_valid(e->download_url) && !prefix(e->download_url,"builtin://"))return 0;
    for(unsigned i=0;i<e->screenshot_count;++i)if(!prefix(e->screenshots[i],"https://")||!portable_store_url_valid(e->screenshots[i]))return 0;
    return 1;
}
int portable_store_parse(const char *json,size_t size,PortableStore *out,char *error)
{
    if(!json || !out || size>INDEX_LIMIT)return fail(error,"Store index is too large or absent");
    PortableStore *next=calloc(1,sizeof *next);
    if(!next)return fail(error,"Cannot allocate store catalog");
    PortableJsonReader r={json,json+size};unsigned seen=0;int ok=portable_json_take(&r,'{');
    while(ok){
        char key[32];ok=text(&r,key,sizeof key)&&portable_json_take(&r,':');
        if(!ok)break;
        unsigned bit=!strcmp(key,"format_version")?1u:!strcmp(key,"id")?2u:!strcmp(key,"name")?4u:!strcmp(key,"mods")?8u:0u;
        if(!bit || (seen&bit)){ok=0;break;}seen|=bit;
        if(bit==1u){uint32_t v;ok=portable_json_number(&r,&v)&&v==1u;}
        else if(bit==2u)ok=text(&r,next->id,sizeof next->id)&&portable_mod_id_valid(next->id);
        else if(bit==4u)ok=text(&r,next->name,sizeof next->name);
        else {
            ok=portable_json_take(&r,'[');
            if(ok && !portable_json_take(&r,']'))do {
                if(next->count==PORTABLE_STORE_LIMIT || !entry_parse(&r,&next->entries[next->count])){ok=0;break;}
                for(unsigned i=0;i<next->count;++i)if(!strcmp(next->entries[i].id,next->entries[next->count].id))ok=0;
                ++next->count;
                if(portable_json_take(&r,']'))break;
                if(!portable_json_take(&r,',')){ok=0;break;}
            } while(ok);
        }
        if(!ok || portable_json_take(&r,'}'))break;
        ok=portable_json_take(&r,',');
    }
    portable_json_whitespace(&r);
    ok=ok && seen==15u && r.at==r.end;
    if(ok)*out=*next;
    free(next);
    return ok?1:fail(error,"Invalid store index: check version, required fields, limits and duplicate IDs");
}
int portable_store_compatible(const PortableStoreEntry *e)
{
    if(e)for(unsigned i=0;i<e->version_count;++i)if(!strcmp(e->compatible_game_versions[i],PORTABLE_STORE_GAME_VERSION))return 1;
    return 0;
}
static void digest(const uint8_t *bytes,size_t size,char hex[65])
{
    uint8_t hash[32];portable_asset_pack_sha256(&bytes,&size,1u,hash);
    for(unsigned i=0;i<32u;++i)snprintf(hex+i*2u,3u,"%02x",hash[i]);
}
int portable_store_verify(const PortableStoreEntry *e,const uint8_t *bytes,size_t size)
{
    if(!e || !bytes || size!=e->size)return 0;
    char hex[65];digest(bytes,size,hex);return !strcmp(hex,e->sha256);
}
#ifdef PORTABLE_STORE_CURL
typedef struct {uint8_t *bytes;size_t size,limit;} Download;
static size_t receive(void *ptr,size_t size,size_t count,void *context)
{
    Download *d=context;
    if(size && count>SIZE_MAX/size)return 0;
    const size_t n=size*count;
    if(n>d->limit-d->size)return 0;
    uint8_t *next=realloc(d->bytes,d->size+n+1u);
    if(!next)return 0;
    d->bytes=next;memcpy(next+d->size,ptr,n);d->size+=n;return n;
}
#endif
static int fetch(const char *url,size_t limit,uint8_t **bytes,size_t *size,char *error)
{
    for(size_t i=0;i<sizeof store_blobs/sizeof store_blobs[0];++i)if(!strcmp(url,store_blobs[i].url)){
        if(store_blobs[i].size>limit)return fail(error,"Store payload exceeds size limit");
        *bytes=malloc(store_blobs[i].size);
        if(!*bytes)return fail(error,"Cannot allocate store payload");
        *size=store_blobs[i].size;memcpy(*bytes,store_blobs[i].data,*size);return 1;
    }
    if(prefix(url,"file:///")){
        const char *path=url+7;
#ifdef _WIN32
        if(path[0]=='/'&&path[1]&&path[2]==':')++path;
#endif
        return portable_file_read_bounded(path,limit,bytes,size)==PORTABLE_FILE_READ_OK?1:fail(error,"Local store file unavailable or too large");
    }
    if(!portable_store_url_valid(url)||!prefix(url,"https://"))return fail(error,"Store requires HTTPS or an absolute local file URL");
#ifdef PORTABLE_STORE_CURL
    CURL *curl=curl_easy_init();
    if(!curl)return fail(error,"Cannot initialize HTTPS download");
    Download d={NULL,0,limit};
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,30L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&d);
    const CURLcode result=curl_easy_perform(curl);curl_easy_cleanup(curl);
    if(result!=CURLE_OK){free(d.bytes);return fail(error,"Download unavailable or exceeds size limit. Try again when online.");}
    *bytes=d.bytes;*size=d.size;return 1;
#else
    return fail(error,"This build has no HTTPS transport; built-in and local stores remain available");
#endif
}
static int write_bytes(const char *path,const uint8_t *bytes,size_t size)
{
    char temporary[4096];
    int n=snprintf(temporary,sizeof temporary,"%s.tmp",path);
    if(n<0||(size_t)n>=sizeof temporary)return 0;
    FILE *f=fopen(temporary,"wb");if(!f)return 0;
    int ok=fwrite(bytes,1,size,f)==size;
    if(fclose(f)!=0)ok=0;
    if(ok)ok=portable_file_replace(temporary,path);
    if(!ok)remove(temporary);
    return ok;
}
static int origin_valid(const PortableStore *s,const char *url)
{
    for(unsigned i=0;i<s->count;++i){
        const char *d=s->entries[i].download_url;
        if(prefix(url,"https://")&&!prefix(d,"https://"))return 0;
        if(prefix(url,"file:///")&&!prefix(d,"https://")&&!prefix(d,"file:///"))return 0;
    }
    return 1;
}
int portable_store_load(const char *url,const char *directory,PortableStore *out,char *error)
{
    if(!portable_store_url_valid(url)||!out)return fail(error,"Invalid store URL");
    char hash[65],cache[4096];digest((const uint8_t *)url,strlen(url),hash);
    int n=snprintf(cache,sizeof cache,"%s/store-%s.json",directory?directory:"",hash);
    if(n<0||(size_t)n>=sizeof cache)return fail(error,"Store cache path too long");
    uint8_t *bytes=NULL;size_t size=0;int offline=0;
    if(!fetch(url,INDEX_LIMIT,&bytes,&size,error)){
        if(!directory || portable_file_read_bounded(cache,INDEX_LIMIT,&bytes,&size)!=PORTABLE_FILE_READ_OK)return 0;
        offline=1;
    }
    PortableStore *next=malloc(sizeof *next);
    int ok=next && portable_store_parse((const char *)bytes,size,next,error);
    if(ok && !origin_valid(next,url))ok=fail(error,"Remote stores may only download HTTPS archives");
    if(ok){
        snprintf(next->source,sizeof next->source,"%s",url);next->offline=offline;*out=*next;
        if(!offline&&directory)(void)write_bytes(cache,bytes,size);
        if(error)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s",offline?"Offline: browsing the last saved catalog":"Catalog ready");
    }
    free(next);free(bytes);return ok;
}
int portable_store_download(const PortableStore *s,unsigned i,const char *destination,char *error)
{
    if(!s||i>=s->count||!destination)return fail(error,"No store mod selected");
    const PortableStoreEntry *e=&s->entries[i];
    if(!portable_store_compatible(e))return fail(error,"Mod does not support this game compatibility version");
    if(!origin_valid(s,s->source))return fail(error,"Store download origin is not allowed");
    uint8_t *bytes=NULL;size_t size=0;
    if(!fetch(e->download_url,e->size,&bytes,&size,error))return 0;
    int ok=portable_store_verify(e,bytes,size);
    if(!ok)fail(error,"Archive size or SHA-256 mismatch; nothing installed");
    else if(!write_bytes(destination,bytes,size))ok=fail(error,"Cannot save downloaded archive");
    free(bytes);return ok;
}
