#ifndef PORTABLE_MOD_IDENTITY_H
#define PORTABLE_MOD_IDENTITY_H
/* The enabled mod set as saves, ghosts and records store it: API version and
 * the ordered mod IDs, versions and 64-bit content fingerprints. Text form:
 * "MODS1 <api> <count>( <id> <version> <16 hex>)*", ASCII, no newline. */
#include <stddef.h>
#include <stdint.h>
#include "mod_manifest.h"

#define PORTABLE_MOD_API_VERSION 1u
#define PORTABLE_MOD_IDENTITY_TEXT_CAPACITY 4096u
typedef struct {
    char id[PORTABLE_MOD_ID_CAPACITY];
    char version[32];
    uint8_t fingerprint[8];
} PortableModIdentityEntry;
typedef struct {
    uint32_t api_version;
    uint32_t count;
    PortableModIdentityEntry mods[PORTABLE_MOD_LIMIT];
} PortableModIdentity;

/* Returns the text length (excluding NUL), or 0 when it does not fit. */
size_t portable_mod_identity_format(const PortableModIdentity *identity,char *text,size_t capacity);
int portable_mod_identity_parse(const char *text,size_t size,PortableModIdentity *out);
int portable_mod_identity_equal(const PortableModIdentity *a,const PortableModIdentity *b);
/* Human list for messages: "none" or "id version, id version". */
void portable_mod_identity_describe(const PortableModIdentity *identity,char *text,size_t capacity);
/* The process-wide enabled set, written by the game shell after loading mods
 * and read by save/ghost/record stores. Empty (stock) until set. */
const PortableModIdentity *portable_mod_identity_current(void);
void portable_mod_identity_set_current(const PortableModIdentity *identity);
/* Time Trial record files are kept per mod set so modded play never replaces
 * stock records: "" without mods, else "-mods-" plus eight hex digits. */
#define PORTABLE_RECORD_SUFFIX_CAPACITY 80u
const char *portable_mod_identity_record_suffix(void);
/* The selected gameplay rules have their own save domain. NULL is Original;
 * otherwise fingerprint is the first eight SHA-256 bytes of the rules payload.
 * User-mod identities keep their existing wire format and all 32 slots. */
void portable_mod_identity_set_driving_profile(const uint8_t fingerprint[8]);
const char *portable_mod_identity_progress_suffix(void);
/* Mirror mode is a different track: while set, records, ghosts, shared
 * ghosts and GP progress use "-mirror" names (records: "-mirror" before the
 * mod suffix) and races open mirrored. Off until the shell applies settings. */
void portable_mod_identity_set_mirror(uint8_t mirror);
uint8_t portable_mod_identity_mirror(void);
const char *portable_mod_identity_mirror_suffix(void);
#endif
