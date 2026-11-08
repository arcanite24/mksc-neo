#include "asset_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "asset pack failed line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0]=(uint8_t)value;bytes[1]=(uint8_t)(value>>8);
    bytes[2]=(uint8_t)(value>>16);bytes[3]=(uint8_t)(value>>24);
}

static void write_u64(uint8_t *bytes, uint64_t value)
{
    write_u32(bytes,(uint32_t)value);write_u32(bytes+4,(uint32_t)(value>>32));
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value=0;
    for(unsigned index=0;index<8u;++index)value|=(uint64_t)bytes[index]<<(index*8u);
    return value;
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|
           (uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
}

static void check_composition(const PortableAssetPackView *base)
{
    PortableAssetSectionView road,route;
    CHECK(portable_asset_pack_find_section(base,PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,24u,&road)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_asset_pack_find_section(base,PORTABLE_ASSET_SECTION_ROUTE,24u,&route)==PORTABLE_ASSET_PACK_OK);
    const uint8_t art_a[]={11u,22u},art_b[]={33u,44u},route_b[]={55u,66u};
    PortableAssetSectionView a_sections[2]={road,road};
    a_sections[0].bytes=art_a;a_sections[0].size=sizeof art_a;
    a_sections[1].content_id=99u;
    PortableAssetSectionView b_sections[2]={road,route};
    b_sections[0].bytes=art_b;b_sections[0].size=sizeof art_b;
    b_sections[1].bytes=route_b;b_sections[1].size=sizeof route_b;
    const PortableAssetPackView a={.section_count=2u,.resolved_sections=a_sections};
    const PortableAssetPackView b={.section_count=2u,.resolved_sections=b_sections};
    const PortableAssetPackView *layers[]={base,&a,&b};
    PortableAssetSectionView storage[4],other_storage[4],section;
    PortableAssetPackView composed,other;
    CHECK(portable_asset_pack_compose(layers,1u,NULL,0u,&composed)==PORTABLE_ASSET_PACK_OK);
    CHECK(memcmp(&composed,base,sizeof composed)==0);
    CHECK(portable_asset_pack_compose(layers,2u,storage,4u,&composed)==PORTABLE_ASSET_PACK_OK);
    CHECK(composed.section_count==base->section_count+1u);
    CHECK(memcmp(composed.deterministic_digest,base->deterministic_digest,32u)==0);
    CHECK(memcmp(composed.pack_digest,base->pack_digest,32u)!=0);
    CHECK(portable_asset_pack_find_section(&composed,road.kind,24u,&section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.bytes==art_a);
    CHECK(portable_asset_pack_section_at(&composed,base->section_count,&section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.kind==road.kind&&section.content_id==99u);
    CHECK(portable_asset_pack_compose(layers,3u,storage,4u,&composed)==PORTABLE_ASSET_PACK_OK);
    CHECK(composed.section_count==base->section_count+1u);
    CHECK(portable_asset_pack_find_section(&composed,road.kind,24u,&section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.bytes==art_b);
    CHECK(portable_asset_pack_find_section(&composed,route.kind,24u,&section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.bytes==route_b);
    CHECK(memcmp(composed.deterministic_digest,base->deterministic_digest,32u)!=0);
    CHECK(portable_asset_pack_compose(layers,3u,other_storage,4u,&other)==PORTABLE_ASSET_PACK_OK);
    CHECK(memcmp(composed.pack_digest,other.pack_digest,32u)==0);
    CHECK(memcmp(composed.deterministic_digest,other.deterministic_digest,32u)==0);
    layers[1]=&b;layers[2]=&a;
    CHECK(portable_asset_pack_compose(layers,3u,other_storage,4u,&other)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_asset_pack_find_section(&other,road.kind,24u,&section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.bytes==art_a);
    CHECK(memcmp(composed.pack_digest,other.pack_digest,32u)!=0);
    CHECK(memcmp(composed.deterministic_digest,other.deterministic_digest,32u)==0);
    /* The base pack plus two imported games passes one pack file's 4096
     * sections: the composed view must still hold them all. */
    enum { WIDE = 4096u };
    PortableAssetSectionView *wide_sections = calloc(WIDE, sizeof *wide_sections);
    PortableAssetSectionView *wide_storage = calloc(WIDE + base->section_count, sizeof *wide_storage);
    CHECK(wide_sections != NULL && wide_storage != NULL);
    for (uint32_t i = 0; i < WIDE; ++i)
    {
        wide_sections[i] = route;
        wide_sections[i].content_id = 0x10000u + i;
    }
    const PortableAssetPackView wide = {.section_count=WIDE, .resolved_sections=wide_sections};
    const PortableAssetPackView *wide_layers[] = {base, &wide};
    CHECK(portable_asset_pack_compose(wide_layers,2u,wide_storage,WIDE+base->section_count,&composed)==
          PORTABLE_ASSET_PACK_OK);
    CHECK(composed.section_count==base->section_count+WIDE);
    free(wide_storage);
    free(wide_sections);
    /* A copied-but-identical deterministic payload retains the same legacy
     * identity; pointer equality alone must not decide this fast path. */
    uint8_t *route_copy = malloc(route.size);
    CHECK(route_copy != NULL);
    memcpy(route_copy, route.bytes, route.size);
    PortableAssetSectionView copied_route = route;
    copied_route.bytes = route_copy;
    const PortableAssetPackView copied = {.section_count=1, .resolved_sections=&copied_route};
    const PortableAssetPackView *same_layers[] = {base, &copied};
    CHECK(portable_asset_pack_compose(same_layers,2u,storage,4u,&composed)==PORTABLE_ASSET_PACK_OK);
    CHECK(memcmp(composed.deterministic_digest,base->deterministic_digest,32u)==0);
    copied_route.semantic_revision++;
    CHECK(portable_asset_pack_compose(same_layers,2u,storage,4u,&composed)==PORTABLE_ASSET_PACK_OK);
    CHECK(memcmp(composed.deterministic_digest,base->deterministic_digest,32u)!=0);
    free(route_copy);
    const PortableAssetPackView preserved=other;
    CHECK(portable_asset_pack_compose(layers,3u,other_storage,1u,&other)==PORTABLE_ASSET_PACK_INSUFFICIENT_CAPACITY);
    CHECK(memcmp(&preserved,&other,sizeof other)==0);
    CHECK(portable_asset_pack_compose(layers,0u,other_storage,4u,&other)==PORTABLE_ASSET_PACK_INVALID_ARGUMENT);
    layers[1]=NULL;
    CHECK(portable_asset_pack_compose(layers,3u,other_storage,4u,&other)==PORTABLE_ASSET_PACK_INVALID_ARGUMENT);
}

static void check_sha_blocks(void)
{
    uint8_t data[128], digest[32];
    static const uint8_t expected[32] = {0x47,0x1f,0xb9,0x43,0xaa,0x23,0xc5,0x11,0xf6,0xf7,0x2f,0x8d,0x16,0x52,0xd9,0xc8,0x80,0xcf,0xa3,0x92,0xad,0x80,0x50,0x31,0x20,0x54,0x77,0x03,0xe5,0x6a,0x2b,0xe5};
    for (unsigned i = 0; i < sizeof data; ++i) data[i] = (uint8_t)i;
    const uint8_t *whole[] = {data};
    const size_t whole_size[] = {sizeof data};
    portable_asset_pack_sha256(whole, whole_size, 1, digest);
    CHECK(memcmp(digest, expected, sizeof digest) == 0);
    const uint8_t *parts[] = {data, data + 1, data + 64, data + 127};
    const size_t sizes[] = {1, 63, 63, 1};
    portable_asset_pack_sha256(parts, sizes, 4, digest);
    CHECK(memcmp(digest, expected, sizeof digest) == 0);
}

int main(int argc,char **argv)
{
    FILE *file;
    long length;
    uint8_t *bytes;
    uint8_t *changed;
    PortableAssetPackView pack;
    PortableAssetSectionView section;

    check_sha_blocks();
    CHECK(argc==2||(argc==3&&strcmp(argv[2],"--open-only")==0));
    file=fopen(argv[1],"rb");CHECK(file!=NULL);
    CHECK(fseek(file,0,SEEK_END)==0);length=ftell(file);CHECK(length>0);
    CHECK(fseek(file,0,SEEK_SET)==0);
    bytes=malloc((size_t)length);changed=malloc((size_t)length);
    CHECK(bytes!=NULL&&changed!=NULL);
    CHECK(fread(bytes,1,(size_t)length,file)==(size_t)length);
    CHECK(fclose(file)==0);

    CHECK(portable_asset_pack_open(bytes,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OK);
    if(argc==3)
    {
        printf("portable asset pack file: sections=%u\n",pack.section_count);
        free(changed);free(bytes);return 0;
    }
    check_composition(&pack);
    CHECK(portable_asset_pack_find_section(&pack,PORTABLE_ASSET_SECTION_ROUTE,24u,
                                           &section)==PORTABLE_ASSET_PACK_OK);
    CHECK(section.semantic_revision==2u&&section.size==19u&&
          memcmp(section.bytes,"deterministic route",19u)==0);
    CHECK(portable_asset_pack_find_section(&pack,99u,1u,&section)==
          PORTABLE_ASSET_PACK_NOT_FOUND);
    CHECK(strcmp(portable_asset_pack_status_name(
                     PORTABLE_ASSET_PACK_DUPLICATE_SECTION),
                 "duplicate_section")==0);

    memcpy(changed,bytes,(size_t)length);changed[0]^=1u;
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_BAD_MAGIC);
    memcpy(changed,bytes,(size_t)length);changed[8]=2u;
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_UNSUPPORTED_VERSION);
    memcpy(changed,bytes,(size_t)length);changed[12]^=1u;
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_BAD_ENDIAN_MARKER);
    memcpy(changed,bytes,(size_t)length);write_u64(changed+112u+16u,(uint64_t)length+8u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_BAD_SECTION);
    memcpy(changed,bytes,(size_t)length);
    write_u64(changed+112u+40u+16u,read_u64(changed+112u+16u));
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_OVERLAPPING_SECTION);
    memcpy(changed,bytes,(size_t)length);
    write_u32(changed+112u+40u,read_u32(changed+112u));
    write_u32(changed+112u+40u+4u,24u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_DUPLICATE_SECTION);
    memcpy(changed,bytes,(size_t)length);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_MESH_SOURCE_ACTOR_RULES);
    write_u32(changed+112u+12u,PORTABLE_ASSET_SECTION_REQUIRED);
    /* MSA1 is a recognized required section; changed directory identity still
     * fails the original digest instead of the unknown-feature gate. */
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    memcpy(changed,bytes,(size_t)length);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_MESH_PALETTE_RULES);
    write_u32(changed+112u+12u,PORTABLE_ASSET_SECTION_REQUIRED);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    memcpy(changed,bytes,(size_t)length);write_u32(changed+112u,PORTABLE_ASSET_SECTION_FX_SET+1u);
    write_u32(changed+112u+12u,PORTABLE_ASSET_SECTION_REQUIRED);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==PORTABLE_ASSET_PACK_UNKNOWN_REQUIRED_SECTION);
    memcpy(changed,bytes,(size_t)length);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_COURSE_CATALOG);
    write_u32(changed+112u+8u,2u);
    write_u32(changed+112u+12u,PORTABLE_ASSET_SECTION_REQUIRED);
    /* Known catalog v2 reaches digest validation; future versions fail the
     * required-section gate before payload interpretation. */
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    write_u32(changed+112u+8u,3u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_UNKNOWN_REQUIRED_SECTION);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_RECOVERY_MOTION);
    write_u32(changed+112u+8u,3u);
    /* LKC1 v3 must pass the real container's required-version gate;
     * editing its descriptor invalidates the digest, not the schema. */
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    write_u32(changed+112u+8u,4u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_UNKNOWN_REQUIRED_SECTION);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_SKY_GRAPHICS);
    write_u32(changed+112u+8u,2u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    write_u32(changed+112u+8u,3u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_UNKNOWN_REQUIRED_SECTION);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_GP_SCORING);
    write_u32(changed+112u+8u,2u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    write_u32(changed+112u,PORTABLE_ASSET_SECTION_GP_CEREMONY_GRAPHICS);
    write_u32(changed+112u+8u,3u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    write_u32(changed+112u+8u,4u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    write_u32(changed+112u+8u,5u);
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_UNKNOWN_REQUIRED_SECTION);
    memcpy(changed,bytes,(size_t)length);
    changed[read_u64(changed+112u+16u)]^=1u;
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DETERMINISTIC_DIGEST_MISMATCH);
    memcpy(changed,bytes,(size_t)length);
    changed[read_u64(changed+112u+40u+16u)]^=1u;
    CHECK(portable_asset_pack_open(changed,(size_t)length,&pack)==
          PORTABLE_ASSET_PACK_DIGEST_MISMATCH);

    free(changed);free(bytes);
    puts("portable asset pack: PASS");
    return 0;
}
