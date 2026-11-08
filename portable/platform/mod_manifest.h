#ifndef PORTABLE_MOD_MANIFEST_H
#define PORTABLE_MOD_MANIFEST_H
#include <stddef.h>
#include <stdint.h>
#define PORTABLE_MOD_LIMIT 32u
#define PORTABLE_MOD_ID_CAPACITY 64u
#define PORTABLE_MOD_ERROR_CAPACITY 256u
#define PORTABLE_MOD_DEPENDENCY_LIMIT 16u
enum { PORTABLE_MOD_TARGET_2D = 1u, PORTABLE_MOD_TARGET_3D = 2u };
typedef struct {
    char id[PORTABLE_MOD_ID_CAPACITY],name[96],version[32],pack[256],script[256];
    char depends[PORTABLE_MOD_DEPENDENCY_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    char load_after[PORTABLE_MOD_DEPENDENCY_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    uint8_t dependency_count,after_count;
    uint8_t targets; /* Omitted manifest field means the legacy 2D domain. */
} PortableModManifest;
int portable_mod_id_valid(const char *id);
int portable_mod_manifest_parse(const char *json,size_t size,PortableModManifest *out,char *error);
#endif
