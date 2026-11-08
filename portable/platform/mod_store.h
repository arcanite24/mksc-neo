#ifndef PORTABLE_MOD_STORE_H
#define PORTABLE_MOD_STORE_H
#include "mod_manifest.h"
#define PORTABLE_STORE_URL_CAPACITY 1024u
#define PORTABLE_STORE_LIMIT 64u
#define PORTABLE_STORE_GAME_VERSION "1"
typedef struct {
    char id[64], name[96], author[96], version[32], description[512];
    char download_url[PORTABLE_STORE_URL_CAPACITY], sha256[65], license[96];
    char compatible_game_versions[8][32], screenshots[4][PORTABLE_STORE_URL_CAPACITY];
    uint32_t size;
    unsigned version_count, screenshot_count;
} PortableStoreEntry;
typedef struct {
    char id[64], name[96], source[PORTABLE_STORE_URL_CAPACITY];
    PortableStoreEntry entries[PORTABLE_STORE_LIMIT];
    unsigned count;
    int offline;
} PortableStore;
/* Strict, bounded ASCII JSON; failed parsing leaves out unchanged. */
int portable_store_parse(const char *json,size_t size,PortableStore *out,char *error);
int portable_store_url_valid(const char *url);
int portable_store_file_url(const char *path,char *url,size_t capacity);
int portable_store_compatible(const PortableStoreEntry *entry);
/* Platform I/O only. HTTPS when built with libcurl, file:/// for local catalogs,
 * builtin://official always available. cache_directory must already exist.
 * A remote index may never request file or builtin downloads. */
int portable_store_load(const char *url,const char *cache_directory,PortableStore *out,char *error);
int portable_store_download(const PortableStore *store,unsigned entry,const char *destination,char *error);
int portable_store_verify(const PortableStoreEntry *entry,const uint8_t *bytes,size_t size);
#endif
