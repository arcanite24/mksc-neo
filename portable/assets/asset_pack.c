#include "asset_pack.h"

#include <string.h>

#define PACK_ENDIAN_MARKER UINT32_C(0x12345678)
#define PACK_MAX_SECTIONS UINT32_C(4096)
/* A composed view stacks the base pack and every mod, so it may hold more
 * sections than one pack file. */
#define PACK_MAX_COMPOSED_SECTIONS UINT32_C(16384)
#define PACK_DIGEST_OFFSET 40u
#define PACK_DETERMINISTIC_DIGEST_OFFSET 72u

typedef struct
{
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t used;
} Sha256;

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) | (uint64_t)read_u32(bytes + 4) << 32;
}

static uint32_t rotate_right(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32u - count));
}

static void sha256_transform(Sha256 *hash, const uint8_t *block)
{
    static const uint32_t constants[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };
    uint32_t words[64];
    uint32_t a,b,c,d,e,f,g,h;

    for (size_t index = 0; index < 16u; ++index)
        words[index] = (uint32_t)block[index*4u] << 24 |
                       (uint32_t)block[index*4u+1u] << 16 |
                       (uint32_t)block[index*4u+2u] << 8 |
                       block[index*4u+3u];
    for (size_t index = 16u; index < 64u; ++index)
    {
        const uint32_t left = words[index-15u];
        const uint32_t right = words[index-2u];
        const uint32_t s0 = rotate_right(left,7)^rotate_right(left,18)^(left>>3);
        const uint32_t s1 = rotate_right(right,17)^rotate_right(right,19)^(right>>10);
        words[index] = words[index-16u] + s0 + words[index-7u] + s1;
    }
    a = hash->state[0];
    b = hash->state[1];
    c = hash->state[2];
    d = hash->state[3];
    e = hash->state[4];
    f = hash->state[5];
    g = hash->state[6];
    h = hash->state[7];
    for (size_t index = 0; index < 64u; ++index)
    {
        const uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
        const uint32_t choice=(e&f)^((~e)&g);
        const uint32_t temp1=h+s1+choice+constants[index]+words[index];
        const uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
        const uint32_t majority=(a&b)^(a&c)^(b&c);
        const uint32_t temp2=s0+majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    hash->state[0] += a;
    hash->state[1] += b;
    hash->state[2] += c;
    hash->state[3] += d;
    hash->state[4] += e;
    hash->state[5] += f;
    hash->state[6] += g;
    hash->state[7] += h;
}

static void sha256_init(Sha256 *hash)
{
    *hash = (Sha256){.state={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
}

static void sha256_update(Sha256 *hash, const uint8_t *bytes, size_t size)
{
    hash->bit_count += (uint64_t)size * 8u;
    if (hash->used == 0u) {
        while (size >= 64u) { sha256_transform(hash, bytes); bytes += 64u; size -= 64u; }
    }
    while (size != 0u)
    {
        size_t amount = 64u - hash->used;
        if (amount > size) amount = size;
        memcpy(hash->block + hash->used, bytes, amount);
        hash->used += amount;
        bytes += amount;
        size -= amount;
        if (hash->used == 64u)
        {
            sha256_transform(hash, hash->block);
            hash->used = 0u;
        }
    }
}

static void sha256_finish(Sha256 *hash, uint8_t output[32])
{
    const uint64_t bit_count = hash->bit_count;
    hash->block[hash->used++] = 0x80u;
    if (hash->used > 56u)
    {
        memset(hash->block + hash->used, 0, 64u - hash->used);
        sha256_transform(hash, hash->block);
        hash->used = 0u;
    }
    memset(hash->block + hash->used, 0, 56u - hash->used);
    for (unsigned index = 0; index < 8u; ++index)
        hash->block[63u-index] = (uint8_t)(bit_count >> (index*8u));
    sha256_transform(hash, hash->block);
    for (size_t index = 0; index < 8u; ++index)
    {
        output[index*4u]=(uint8_t)(hash->state[index]>>24);
        output[index*4u+1u]=(uint8_t)(hash->state[index]>>16);
        output[index*4u+2u]=(uint8_t)(hash->state[index]>>8);
        output[index*4u+3u]=(uint8_t)hash->state[index];
    }
}

static const uint8_t *section_entry(const PortableAssetPackView *pack,
                                    uint32_t index)
{
    return pack->bytes + pack->section_table_offset +
           (uint64_t)index * PORTABLE_ASSET_PACK_SECTION_SIZE;
}

static int known_section(uint32_t kind, uint32_t version)
{
    return kind >= PORTABLE_ASSET_SECTION_COURSE_MANIFEST &&
           kind <= PORTABLE_ASSET_SECTION_FX_SET &&
           (version == 1u || (kind == PORTABLE_ASSET_SECTION_NDS_SEQUENCE && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_DRIVER_SPRITES && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_SOUND_SET && (version == 2u || version == 3u)) || (kind == PORTABLE_ASSET_SECTION_HUD_GRAPHICS && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_MODEL_BINDINGS && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS && version == 2u) ||
            ((kind == PORTABLE_ASSET_SECTION_AUDIO_PLAYER_RULES ||
              kind == PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE) &&
             version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_FRONTEND_MOTION && (version == 2u || version == 3u)) ||
            (kind == PORTABLE_ASSET_SECTION_RECOVERY_MOTION && (version == 2u || version == 3u)) ||
            (kind == PORTABLE_ASSET_SECTION_SKY_GRAPHICS && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_COURSE_3D_PROFILE && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_ENGINE_SOUND_RULES &&
             (version == 2u || version == 3u)) ||
            (kind == PORTABLE_ASSET_SECTION_SURFACE_SOUND_RULES && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_RACE_RULES && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_GP_CEREMONY_GRAPHICS &&
             (version == 2u || version == 3u || version == 4u)) ||
            (kind == PORTABLE_ASSET_SECTION_GP_SCORING && (version == 2u || version == 3u)) ||
            (kind == PORTABLE_ASSET_SECTION_COURSE_CATALOG && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_COURSE_SCENERY &&
             (version == 2u || version == 3u || version == 4u)) ||
            ((kind == PORTABLE_ASSET_SECTION_LAVA_BUBBLES ||
              kind == PORTABLE_ASSET_SECTION_THWOMPS) &&
             version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_MATERIAL_FLAGS && (version >= 2u && version <= 16u)) ||
            (kind == PORTABLE_ASSET_SECTION_ROUTE &&
             (version == 2u || version == 3u || version == 4u)) ||
            (kind == PORTABLE_ASSET_SECTION_MOTION_TABLES && (version >= 2u && version <= 8u)) ||
            (kind == PORTABLE_ASSET_SECTION_GP_MINIMAP_MARKERS && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS &&
             (version == 2u || version == 3u || version == 4u)) ||
            (kind == PORTABLE_ASSET_SECTION_GP_OPENING_PALETTE && version == 2u) ||
            (kind == PORTABLE_ASSET_SECTION_GP_OPENING_TITLE &&
             (version == 2u || version == 3u || version == 4u || version == 5u || version == 6u ||
              version == 7u || version == 8u || version == 9u || version == 10u || version == 11u ||
              version == 12u || version == 13u || version == 14u || version == 15u ||
              version == 16u || version == 17u || version == 18u || version == 19u ||
              version == 20u || version == 21u || version == 22u || version == 23u ||
              version == 24u || version == 25u || version == 26u || version == 27u)) ||
            (kind == PORTABLE_ASSET_SECTION_GP_RESULTS_GRAPHICS &&
             (version == 2u || version == 3u || version == 4u || version == 5u || version == 6u ||
              version == 7u || version == 8u || version == 9u)) ||
            (kind == PORTABLE_ASSET_SECTION_GP_RACE_HUD_BG0 &&
             (version == 2u || version == 3u || version == 4u || version == 5u || version == 6u ||
              version == 7u || version == 8u || version == 9u || version == 10u || version == 11u ||
              version == 12u || version == 13u || version == 14u || version == 15u ||
              version == 16u || version == 17u || version == 18u || version == 19u ||
              version == 20u || version == 21u || version == 22u || version == 23u ||
              version == 24u || version == 25u || version == 26u || version == 27u ||
              version == 28u || version == 29u || version == 30u || version == 31u ||
              version == 32u || version == 33u || version == 34u || version == 35u ||
              version == 36u)));
}

static void pack_digest(const uint8_t *bytes, size_t size, uint8_t output[32])
{
    static const uint8_t zeros[32] = {0};
    Sha256 hash;
    sha256_init(&hash);
    sha256_update(&hash, bytes, PACK_DIGEST_OFFSET);
    sha256_update(&hash, zeros, sizeof zeros);
    sha256_update(&hash, bytes + PACK_DIGEST_OFFSET + sizeof zeros,
                  size - PACK_DIGEST_OFFSET - sizeof zeros);
    sha256_finish(&hash, output);
}

static void deterministic_digest(const PortableAssetPackView *pack,
                                 uint8_t output[32])
{
    Sha256 hash;
    sha256_init(&hash);
    for (uint32_t index = 0; index < pack->section_count; ++index)
    {
        const uint8_t *entry = section_entry(pack, index);
        if ((read_u32(entry + 12) & PORTABLE_ASSET_SECTION_DETERMINISTIC) != 0u)
        {
            const uint64_t offset = read_u64(entry + 16);
            const uint64_t size = read_u64(entry + 24);
            sha256_update(&hash, entry, PORTABLE_ASSET_PACK_SECTION_SIZE);
            sha256_update(&hash, pack->bytes + offset, (size_t)size);
        }
    }
    sha256_finish(&hash, output);
}

PortableAssetPackStatus portable_asset_pack_open(
    const uint8_t *bytes, size_t size, PortableAssetPackView *out)
{
    static const uint8_t magic[8] = {'M','K','S','C','P','A','C','K'};
    PortableAssetPackView next = {0};
    uint8_t digest[32];
    uint64_t table_size;
    uint64_t table_end;

    if (bytes == NULL || out == NULL) return PORTABLE_ASSET_PACK_INVALID_ARGUMENT;
    if (size < PORTABLE_ASSET_PACK_HEADER_SIZE) return PORTABLE_ASSET_PACK_TOO_SMALL;
    if (memcmp(bytes, magic, sizeof magic) != 0) return PORTABLE_ASSET_PACK_BAD_MAGIC;
    if (read_u16(bytes+8) != PORTABLE_ASSET_PACK_FORMAT_VERSION ||
        read_u16(bytes+10) != PORTABLE_ASSET_PACK_HEADER_SIZE)
        return PORTABLE_ASSET_PACK_UNSUPPORTED_VERSION;
    if (read_u32(bytes+12) != PACK_ENDIAN_MARKER)
        return PORTABLE_ASSET_PACK_BAD_ENDIAN_MARKER;
    if (read_u64(bytes+16) != size) return PORTABLE_ASSET_PACK_SIZE_MISMATCH;
    next.section_count = read_u32(bytes+24);
    if (next.section_count > PACK_MAX_SECTIONS ||
        read_u32(bytes+28) != PORTABLE_ASSET_PACK_SECTION_SIZE)
        return PORTABLE_ASSET_PACK_BAD_SECTION_TABLE;
    next.section_table_offset = read_u64(bytes+32);
    table_size = (uint64_t)next.section_count * PORTABLE_ASSET_PACK_SECTION_SIZE;
    table_end = next.section_table_offset + table_size;
    if (next.section_table_offset < PORTABLE_ASSET_PACK_HEADER_SIZE ||
        (next.section_table_offset & 7u) != 0u ||
        table_end < next.section_table_offset || table_end > size)
        return PORTABLE_ASSET_PACK_BAD_SECTION_TABLE;
    for (size_t index = 104u; index < PORTABLE_ASSET_PACK_HEADER_SIZE; ++index)
        if (bytes[index] != 0u)
            return PORTABLE_ASSET_PACK_BAD_SECTION_TABLE;
    next.bytes=bytes;next.size=size;
    memcpy(next.pack_digest,bytes+PACK_DIGEST_OFFSET,32);
    memcpy(next.deterministic_digest,bytes+PACK_DETERMINISTIC_DIGEST_OFFSET,32);

    uint64_t prior_end = 0u;
    uint32_t highest_kind = 0u, highest_content = 0u;
    for (uint32_t index=0;index<next.section_count;++index)
    {
        const uint8_t *entry=section_entry(&next,index);
        const uint32_t kind=read_u32(entry);
        const uint32_t content=read_u32(entry+4);
        const uint32_t version=read_u32(entry+8);
        const uint32_t flags=read_u32(entry+12);
        const uint64_t offset=read_u64(entry+16);
        const uint64_t section_size=read_u64(entry+24);
        if (kind==0u || content==0u || section_size==0u ||
            (flags & ~(PORTABLE_ASSET_SECTION_REQUIRED |
                       PORTABLE_ASSET_SECTION_DETERMINISTIC)) != 0u ||
            read_u32(entry+36) != 0u || offset<table_end ||
            offset>size || section_size>size-offset || (offset&7u)!=0u)
            return PORTABLE_ASSET_PACK_BAD_SECTION;
        if (!known_section(kind,version) &&
            (flags&PORTABLE_ASSET_SECTION_REQUIRED)!=0u)
            return PORTABLE_ASSET_PACK_UNKNOWN_REQUIRED_SECTION;
        /* Ordered, disjoint import tables need one pass. Unordered mods still
         * take the exact duplicate/overlap checks, in their original order. */
        const int possible_duplicate = kind < highest_kind ||
            (kind == highest_kind && content <= highest_content);
        const int possible_overlap = offset < prior_end;
        for (uint32_t prior = 0u; (possible_duplicate || possible_overlap) && prior < index; ++prior)
        {
            const uint8_t *other = section_entry(&next, prior);
            if (possible_duplicate && kind == read_u32(other) && content == read_u32(other + 4))
                return PORTABLE_ASSET_PACK_DUPLICATE_SECTION;
            if (possible_overlap) {
                const uint64_t other_offset = read_u64(other + 16);
                const uint64_t other_size = read_u64(other + 24);
                if (offset < other_offset + other_size && other_offset < offset + section_size)
                    return PORTABLE_ASSET_PACK_OVERLAPPING_SECTION;
            }
        }
        if (offset + section_size > prior_end) prior_end = offset + section_size;
        if (!possible_duplicate) { highest_kind = kind; highest_content = content; }
    }
    deterministic_digest(&next,digest);
    if(memcmp(digest,next.deterministic_digest,32)!=0)
        return PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH;
    pack_digest(bytes,size,digest);
    if(memcmp(digest,next.pack_digest,32)!=0)
        return PORTABLE_ASSET_PACK_DIGEST_MISMATCH;
    *out=next;
    return PORTABLE_ASSET_PACK_OK;
}

PortableAssetPackStatus portable_asset_pack_find_section(
    const PortableAssetPackView *pack,uint32_t kind,uint32_t content_id,
    PortableAssetSectionView *out)
{
    if(pack==NULL||out==NULL||(pack->bytes==NULL&&pack->resolved_sections==NULL)||kind==0u||content_id==0u)
        return PORTABLE_ASSET_PACK_INVALID_ARGUMENT;
    for(uint32_t index=0;index<pack->section_count;++index)
    {
        PortableAssetSectionView section;
        const PortableAssetPackStatus status=portable_asset_pack_section_at(pack,index,&section);
        if(status!=PORTABLE_ASSET_PACK_OK)return status;
        if(section.kind==kind&&section.content_id==content_id){*out=section;return PORTABLE_ASSET_PACK_OK;}
    }
    return PORTABLE_ASSET_PACK_NOT_FOUND;
}

PortableAssetPackStatus portable_asset_pack_section_at(
    const PortableAssetPackView *pack,uint32_t index,PortableAssetSectionView *out)
{
    const uint8_t *entry;
    uint64_t offset,size;
    if(pack==NULL||out==NULL||(pack->bytes==NULL&&pack->resolved_sections==NULL))
        return PORTABLE_ASSET_PACK_INVALID_ARGUMENT;
    if(index>=pack->section_count)return PORTABLE_ASSET_PACK_NOT_FOUND;
    if(pack->resolved_sections!=NULL){*out=pack->resolved_sections[index];return PORTABLE_ASSET_PACK_OK;}
    entry = section_entry(pack, index);
    offset = read_u64(entry + 16);
    size = read_u64(entry + 24);
    *out=(PortableAssetSectionView){read_u32(entry),read_u32(entry+4),
        read_u32(entry+8),read_u32(entry+12),read_u32(entry+32),
        pack->bytes+offset,(size_t)size};
    return PORTABLE_ASSET_PACK_OK;
}

/* Canonical effective-content hashes contain typed metadata and payload bytes,
 * never host addresses or the physical offsets of an individual pack file. */
static void section_digest_word(Sha256 *hash,uint64_t value)
{
    uint8_t bytes[8];
    for (unsigned i = 0u; i < 8u; ++i)
    {
        bytes[i] = (uint8_t)value;
        value >>= 8u;
    }
    sha256_update(hash,bytes,sizeof bytes);
}

static PortableAssetPackStatus effective_digest(const PortableAssetPackView *pack,
    int deterministic_only,uint8_t output[PORTABLE_ASSET_PACK_DIGEST_SIZE])
{
    static const uint8_t domain[]="MKSC effective sections v1";
    Sha256 hash;
    sha256_init(&hash);sha256_update(&hash,domain,sizeof domain);
    for(uint32_t i=0u;i<pack->section_count;++i){
        PortableAssetSectionView section;
        const PortableAssetPackStatus status=portable_asset_pack_section_at(pack,i,&section);
        if(status!=PORTABLE_ASSET_PACK_OK)return status;
        if(deterministic_only&&!(section.flags&PORTABLE_ASSET_SECTION_DETERMINISTIC))continue;
        section_digest_word(&hash,section.kind);
        section_digest_word(&hash,section.content_id);
        section_digest_word(&hash,section.version);
        section_digest_word(&hash,section.flags);
        section_digest_word(&hash,section.semantic_revision);
        section_digest_word(&hash,section.size);
        sha256_update(&hash,section.bytes,section.size);
    }
    sha256_finish(&hash,output);
    return PORTABLE_ASSET_PACK_OK;
}

/* Compare the canonical deterministic section stream directly. Most composed
 * sections still borrow the exact same immutable bytes, so unchanged content
 * needs no second full hash of the base and no speculative hash of the result. */
static int next_deterministic(const PortableAssetPackView *pack, uint32_t *index,
                              PortableAssetSectionView *section, int *found)
{
    *found = 0;
    while (*index < pack->section_count) {
        if (portable_asset_pack_section_at(pack, (*index)++, section) != PORTABLE_ASSET_PACK_OK) return 0;
        if (section->flags & PORTABLE_ASSET_SECTION_DETERMINISTIC) { *found = 1; break; }
    }
    return 1;
}
static int same_deterministic_sections(const PortableAssetPackView *a, const PortableAssetPackView *b)
{
    uint32_t ai = 0, bi = 0;
    for (;;) {
        PortableAssetSectionView left, right;
        int left_found, right_found;
        if (!next_deterministic(a, &ai, &left, &left_found) ||
            !next_deterministic(b, &bi, &right, &right_found)) return 0;
        if (!left_found || !right_found) return left_found == right_found;
        if (left.kind != right.kind || left.content_id != right.content_id || left.version != right.version ||
            left.flags != right.flags || left.semantic_revision != right.semantic_revision || left.size != right.size ||
            (left.bytes != right.bytes && left.size && memcmp(left.bytes, right.bytes, left.size))) return 0;
    }
}

PortableAssetPackStatus portable_asset_pack_compose(
    const PortableAssetPackView *const *layers,size_t layer_count,
    PortableAssetSectionView *storage,size_t capacity,PortableAssetPackView *out)
{
    if(layers==NULL||out==NULL||layer_count==0u||layer_count>PORTABLE_ASSET_PACK_MAX_LAYERS)
        return PORTABLE_ASSET_PACK_INVALID_ARGUMENT;
    for(size_t i=0u;i<layer_count;++i)
        if(layers[i]==NULL||(layers[i]->bytes==NULL&&layers[i]->resolved_sections==NULL))
            return PORTABLE_ASSET_PACK_INVALID_ARGUMENT;
    if(layer_count==1u){*out=*layers[0];return PORTABLE_ASSET_PACK_OK;}
    if(storage==NULL)return PORTABLE_ASSET_PACK_INVALID_ARGUMENT;
    PortableAssetPackView next={0};
    next.resolved_sections=storage;
    next.driving_profile=layers[0]->driving_profile;
    for(size_t layer=0u;layer<layer_count;++layer){
        for(uint32_t index=0u;index<layers[layer]->section_count;++index){
            PortableAssetSectionView section;
            const PortableAssetPackStatus status=portable_asset_pack_section_at(layers[layer],index,&section);
            if(status!=PORTABLE_ASSET_PACK_OK)return status;
            uint32_t slot=0u;
            while(slot<next.section_count&&
                  (storage[slot].kind!=section.kind||storage[slot].content_id!=section.content_id))++slot;
            if(slot==next.section_count){
                if(slot>=capacity||slot>=PACK_MAX_COMPOSED_SECTIONS)return PORTABLE_ASSET_PACK_INSUFFICIENT_CAPACITY;
                ++next.section_count;
            }
            storage[slot]=section;
        }
    }
    PortableAssetPackStatus status=effective_digest(&next,0,next.pack_digest);
    if(status!=PORTABLE_ASSET_PACK_OK)return status;
    if(same_deterministic_sections(layers[0], &next))
        memcpy(next.deterministic_digest,layers[0]->deterministic_digest,sizeof next.deterministic_digest);
    else {
        status=effective_digest(&next,1,next.deterministic_digest);
        if(status!=PORTABLE_ASSET_PACK_OK)return status;
    }
    *out=next;
    return PORTABLE_ASSET_PACK_OK;
}

void portable_asset_pack_sha256(const uint8_t *const *parts,const size_t *sizes,
    size_t count,uint8_t output[PORTABLE_ASSET_PACK_DIGEST_SIZE])
{
    Sha256 hash;
    sha256_init(&hash);
    for (size_t i = 0u; i < count; ++i)
        if (sizes[i] != 0u)
            sha256_update(&hash, parts[i], sizes[i]);
    sha256_finish(&hash,output);
}

const char *portable_asset_pack_status_name(PortableAssetPackStatus status)
{
    static const char *const names[]={"ok","invalid_argument","too_small",
        "bad_magic","unsupported_version","bad_endian_marker","size_mismatch",
        "bad_section_table","bad_section","overlapping_section",
        "duplicate_section","unknown_required_section","digest_mismatch",
        "deterministic_digest_mismatch","not_found","insufficient_capacity"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:"invalid_status";
}
