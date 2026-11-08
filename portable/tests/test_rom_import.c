/* ROM-free checks of the native importer. The fixture digest below was
 * produced by the Python reference (build_driver_assets.driver_sections, the
 * lightning/finish-banner/results payloads and build_asset_pack.build_pack)
 * on the same synthetic ROM; the owner-ROM comparison is
 * portable_rom_import_matches_reference (configure with PORTABLE_ROM). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_pack.h"
#include "content_registry.h"
#include "native_image.h"
#include "rom_import_internal.h"
#ifdef PORTABLE_ENABLE_SDL3
#include "rom_import_host.h"
#include "ui_background_host.h"
#endif

#define ROM_SIZE 0x400000u

static int failures;

static void expect(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static void expect_hex(const uint8_t *digest, size_t size, const char *expected, const char *what)
{
    char hex[65];
    mksc_hex(digest, size, hex);
    if (strcmp(hex, expected) != 0) {
        fprintf(stderr, "FAIL: %s: %s != %s\n", what, hex, expected);
        ++failures;
    }
}

static uint32_t lcg_state;

static uint8_t lcg(void)
{
    lcg_state = lcg_state * 1103515245u + 12345u;
    return (uint8_t)(lcg_state >> 16);
}

static void fill(uint8_t *rom, uint32_t start, uint32_t end)
{
    for (uint32_t offset = start; offset < end; ++offset) rom[offset] = lcg();
}

static void put16(uint8_t *rom, uint32_t offset, uint32_t value)
{
    rom[offset] = (uint8_t)value;
    rom[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *rom, uint32_t offset, uint32_t value)
{
    put16(rom, offset, value & 0xFFFF);
    put16(rom, offset + 2, value >> 16);
}

/* Eight literals, then distance-8 matches of 18 bytes, then literals. */
static void lz77(uint8_t *rom, uint32_t offset, uint32_t size)
{
    uint32_t cursor = offset + 4, produced = 0;
    rom[offset] = 0x10;
    rom[offset + 1] = (uint8_t)size;
    rom[offset + 2] = (uint8_t)(size >> 8);
    rom[offset + 3] = (uint8_t)(size >> 16);
    while (produced < size) {
        uint32_t flags_at = cursor++;
        uint8_t flags = 0;
        for (int bit = 7; bit >= 0 && produced < size; --bit) {
            if (produced >= 8 && size - produced >= 18) {
                flags |= (uint8_t)(1u << bit);
                rom[cursor++] = 0xF0; /* length 15+3, distance 7+1 */
                rom[cursor++] = 0x07;
                produced += 18;
            } else {
                rom[cursor++] = lcg();
                produced += 1;
            }
        }
        rom[flags_at] = flags;
    }
}

static void pattern(uint8_t *rom, uint32_t offset, uint32_t width, uint32_t height)
{
    put16(rom, offset, width);
    put16(rom, offset + 2, height);
    fill(rom, offset + 4, offset + 4 + width * height * 2);
}

static uint8_t *fixture_rom(void)
{
    uint8_t *rom = calloc(1, ROM_SIZE);
    if (rom == NULL) return NULL;
    lcg_state = 0x4D4B5343u;
    fill(rom, 0x344600, 0x400000);  /* driver palettes, tiles, bomb frames */
    fill(rom, 0x62000, 0x70000);    /* HUD/result/effect tiles and palettes */
    fill(rom, 0x9A900, 0x9B8D4);    /* standings/results palettes */
    fill(rom, 0xAA000, 0xAD000);    /* menu palettes and followup tiles */
    fill(rom, 0xEE67E, 0xEE6A6);    /* scoreboard digits */
    fill(rom, 0xECD5C, 0xECD9C);    /* driver view slots */
    rom[0xF2C15]=2u;rom[0xF2C23]=5u;
    for(unsigned row=0u;row<8u;++row)for(unsigned i=0u;i<8u;++i)
        put32(rom,0xEDC34u+(row*8u+i)*4u,(row+i)&7u);
    for (uint32_t i = 0; i < 64; ++i) rom[0xECD9C + i] = (uint8_t)(i * 2 % 16);
    for (uint32_t i = 0; i < 8 * 16; ++i) put16(rom, 0xEBF78 + i * 2, (uint32_t)((int32_t)(i % 41) - 20) & 0xFFFF);
    for (uint32_t i = 0; i < 1024; ++i) put32(rom, 0xE645C + i * 4, (uint32_t)((int32_t)(i * 37 % 0x8000) - 0x4000));
    lz77(rom, 0x6D210, 0x800);
    lz77(rom, 0x9BE28, 0x500);
    lz77(rom, 0x9BD7C, 0x300);
    lz77(rom, 0x9B8D4, 0x800);
    pattern(rom, 0xDB7A8, 3, 2);
    pattern(rom, 0xDB798, 2, 3);
    for (uint32_t i = 0; i < 4; ++i) {
        put32(rom, 0xDB7B8 + i * 4, 0x08100000u + i * 0x40);
        pattern(rom, 0x100000 + i * 0x40, i + 1, 2);
    }
    for (uint32_t i = 0; i < 20; ++i) {
        put32(rom, 0xDB7E0 + i * 4, 0x08101000u + i * 0x20);
        fill(rom, 0x101000 + i * 0x20, 0x101000 + i * 0x20 + 18);
    }
    for (uint32_t i = 0; i < 6; ++i) {  /* five-lap compact rows, then the total divider */
        put32(rom, 0xDB7C8 + i * 4, 0x08102000u + i * 0x20);
        pattern(rom, 0x102000 + i * 0x20, i < 5 ? 2 : 14, 1);
    }
    lz77(rom, 0x9B4CC, 0x800);           /* time board map */
    return rom;
}

static void duplicate_group(RiCtx *ctx)
{
    ri_section(ctx, 5, 1, 1, ri_buf_copy(ctx, "x", 1));
    ri_section(ctx, 5, 1, 1, ri_buf_copy(ctx, "y", 1));
}

static void bad_lz77_group(RiCtx *ctx)
{
    static const uint8_t stream[] = {0x10, 4, 0, 0, 0x80, 0x00, 0x00};
    RiData data = {stream, sizeof stream};
    ri_lz77(ctx, data, 0);
}

static void ordered_group(RiCtx *ctx)
{
    ri_section(ctx, 9, 2, 1, ri_buf_copy(ctx, "late", 4));
    ri_section(ctx, 3, 7, 2, ri_buf_copy(ctx, "early!", 6));
}

static void indexed_image_group(RiCtx *ctx)
{
    const uint8_t cell[]={1,0,252,0,254,17,0,16};
    uint8_t tiles[32]={0x21}, palette[64]={0};
    palette[34]=31;palette[36]=31; /* Identical RGB, distinct bank indices. */
    RiBuf *image=ri_cell_image_indexed(ctx,(RiData){cell,sizeof cell},0,
        (RiData){tiles,sizeof tiles},(RiData){palette,sizeof palette},32);
    expect(image->size==160,"indexed image length");
    expect(memcmp(image->data,"MKSCIMG2",8)==0,"indexed image magic");
    expect(ri_le32(image->data+20)==(32u|(32u<<16)),"indexed palette range");
    expect(image->data[38]==50 && image->data[39]==49 && image->data[32]==255,
        "indexed cell flip preserves source bank and transparency");
    expect(memcmp(image->data+96,palette,sizeof palette)==0,"indexed fallback palette");
    ri_section(ctx,15,99,1,image);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    const long length = ftell(file);
    if (length <= 0 || (uintmax_t)length > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    int valid = bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file) != 0) valid = 0;
    if (!valid) { free(bytes); return NULL; }
    *size = (size_t)length;
    return bytes;
}

/* Compare the imported portrait bank with the owner's original last upload,
 * independently of either importer's ordered range declarations. */
static int standings_source_check(const char *rom_path)
{
    size_t size=0u;uint8_t *rom=read_file(rom_path,&size);
    MkscRomImportResult imported={0};char error[256];
    expect(rom!=NULL,"read owner ROM for portrait-bank oracle");
    if(rom==NULL)return 1;
    uint8_t before[20],after[20];mksc_sha1(rom,size,before);
    expect(mksc_rom_import(rom,size,&imported,error,sizeof error),error);
    PortableAssetPackView pack={0};
    if(!failures)expect(portable_asset_pack_open(imported.pack,imported.pack_size,&pack)==PORTABLE_ASSET_PACK_OK,
        "open native imported portrait banks");
    unsigned count=0u;
    if(!failures)for(uint32_t index=0u;index<pack.section_count;++index){
        PortableAssetSectionView section;
        expect(portable_asset_pack_section_at(&pack,index,&section)==PORTABLE_ASSET_PACK_OK,"read imported section");
        if(section.kind!=PORTABLE_ASSET_SECTION_GP_STANDINGS_GRAPHICS)continue;
        expect(section.size>=32u+0x3000u,"bounded standings tile bank");
        if(section.size<32u+0x3000u)continue;
        ++count;
        expect(!memcmp(section.bytes+32u+0x1000u,rom+0x9AE2Cu,0x480u),
            "all portrait interiors and A0..A3 borders match last original upload");
        expect(!memcmp(section.bytes+32u+0x1480u,rom+0xACB7Cu,0xB80u),
            "remaining name tiles retain original upload");
    }
    expect(count==44u,"all44 course and arena portrait banks checked");
    mksc_sha1(rom,size,after);expect(!memcmp(before,after,sizeof before),"import leaves owner ROM bytes immutable");
    mksc_rom_import_free(&imported);free(rom);
    if(failures)return 1;
    puts("source portrait banks: PASS all44 interiors, borders and name remainders");return 0;
}

static int title_presentation_check(const char *rom_path, const char *base_path, const char *output)
{
    size_t rom_size = 0, base_size = 0;
    uint8_t *rom = read_file(rom_path, &rom_size);
    uint8_t *base = read_file(base_path, &base_size);
    MkscRomImportResult companion = {0}, repeated = {0}, imported_base = {0};
    PortableAssetPackView source, title;
    char error[256];
    expect(rom != NULL && base != NULL, "read owner ROM and base pack");
    if (rom == NULL || base == NULL) goto done;
    expect(portable_asset_pack_open(base, base_size, &source) == PORTABLE_ASSET_PACK_OK,
        "open reference base pack");
    if (failures) goto done;
    expect(mksc_rom_import_title_presentation(rom, rom_size, &companion, error, sizeof error), error);
    if (failures) goto done;
    expect(companion.section_count == 29u && companion.pack_size <= 8u * 1024u * 1024u,
        "bounded 29-section title companion");
    expect(portable_asset_pack_open(companion.pack, companion.pack_size, &title) == PORTABLE_ASSET_PACK_OK,
        "open title companion container");
    if (failures) goto done;
    PortableAssetSectionView metadata;
    expect(portable_asset_pack_find_section(&title, 15u, 0x209ffu, &metadata) == PORTABLE_ASSET_PACK_OK,
        "title companion metadata exists");
    if (failures) goto done;
    uint8_t sha1[20];
    mksc_sha1(rom, rom_size, sha1);
    expect_hex(sha1, sizeof sha1, MKSC_ROM_IMPORT_SHA1, "title companion source ROM");
    expect(metadata.version == 1u && metadata.size == 32u &&
        memcmp(metadata.bytes, "MKSCWUI1\1\0\0\0", 12u) == 0 &&
        memcmp(metadata.bytes + 12u, sha1, sizeof sha1) == 0, "title metadata version and source SHA-1");
    unsigned identities = 0u, layers = 0u;
    for (uint32_t index = 0; index < title.section_count; ++index) {
        PortableAssetSectionView section;
        expect(portable_asset_pack_section_at(&title, index, &section) == PORTABLE_ASSET_PACK_OK,
            "read title companion section");
        if (failures) goto done;
        expect(section.kind == 15u, "companion contains only frontend data");
        if (section.content_id == 0x209ffu) continue;
        if (section.content_id >= 0x20900u && section.content_id <= 0x20905u) {
            PortableNativeImage image;
            const unsigned layer = section.content_id - 0x20900u;
            const unsigned height = (layer & 1u) ? 512u : layer == 4u ? 2048u : 256u;
            expect(portable_native_image_open(section.bytes, section.size, &image) &&
                image.width == 256u && image.height == height &&
                image.anchor_x == 0 && image.anchor_y == 0, "separated sky/cloud geometry");
            ++layers;
        } else {
            PortableAssetSectionView stock;
            expect(portable_asset_pack_find_section(&source, section.kind, section.content_id, &stock) ==
                PORTABLE_ASSET_PACK_OK && stock.version == section.version && stock.size == section.size &&
                memcmp(stock.bytes, section.bytes, section.size) == 0, "stock title identity bytes unchanged");
            ++identities;
        }
    }
    expect(identities == 22u && layers == 6u, "all stock identities and separated layers present");
    expect(mksc_rom_import_title_presentation(rom, rom_size, &repeated, error, sizeof error), error);
    expect(repeated.pack != NULL && repeated.pack_size == companion.pack_size &&
        memcmp(repeated.pack, companion.pack, companion.pack_size) == 0, "companion repeat is byte deterministic");
    mksc_rom_import_free(&repeated);
    expect(mksc_rom_import(rom, rom_size, &imported_base, error, sizeof error), error);
    expect(imported_base.pack != NULL && imported_base.pack_size == base_size &&
        memcmp(imported_base.pack, base, base_size) == 0, "full base import remains byte identical");
    if (!failures) {
        FILE *file = fopen(output, "wb");
        int written = file != NULL && fwrite(companion.pack, 1, companion.pack_size, file) == companion.pack_size;
        if (file != NULL && fclose(file) != 0) written = 0;
        expect(written, "write validated title companion fixture");
        if (!written) remove(output);
    }
done:
    mksc_rom_import_free(&imported_base);
    mksc_rom_import_free(&repeated);
    mksc_rom_import_free(&companion);
    free(base);
    free(rom);
    if (failures) return 1;
    puts("title presentation import: PASS");
    return 0;
}

static int empty_result(const MkscRomImportResult *result)
{
    const uint8_t zero[32] = {0};
    return result->pack == NULL && result->pack_size == 0u && result->section_count == 0u &&
        memcmp(result->pack_sha256, zero, sizeof zero) == 0 &&
        memcmp(result->deterministic_sha256, zero, sizeof zero) == 0;
}

#ifdef PORTABLE_ENABLE_SDL3
static int write_file(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) return 0;
    int written = fwrite(bytes, 1, size, file) == size;
    if (fclose(file) != 0) written = 0;
    return written;
}

static int cache_matches(const char *path, const uint8_t *bytes, size_t size)
{
    size_t stored_size = 0;
    uint8_t *stored = read_file(path, &stored_size);
    const int matches = stored != NULL && stored_size == size && memcmp(stored, bytes, size) == 0;
    free(stored);
    return matches;
}

static int title_cache_check(const char *rom_path, const char *fixture_path, const char *directory)
{
    PortableUiBackgroundHost host;
    portable_ui_background_host_init(&host);
    size_t size = 0;
    uint8_t *fixture = read_file(fixture_path, &size);
    char cache[4096], invalid[4096], remembered[4096], temporary[4100], error[256];
    const char *configured = getenv("MKSC_DATA_DIR");
    expect(configured != NULL && strcmp(configured, directory) == 0,
        "cache test uses its isolated data directory");
    expect(fixture != NULL, "read title companion fixture");
    const int cache_length = snprintf(cache, sizeof cache, "%s/title-backgrounds.pack", directory);
    const int invalid_length = snprintf(invalid, sizeof invalid, "%s/invalid.gba", directory);
    const int remembered_length = snprintf(remembered, sizeof remembered, "%s/rom-path.txt", directory);
    expect(cache_length > 0 && (size_t)cache_length < sizeof cache &&
        invalid_length > 0 && (size_t)invalid_length < sizeof invalid &&
        remembered_length > 0 && (size_t)remembered_length < sizeof remembered, "cache fixture path bounds");
    if (failures) goto done;
    /* CTest owns this dedicated directory; no owner's ROM or normal cache is
     * modified. Only an authored three-byte invalid ROM is written here. */
    remove(remembered);
    const char *document_uri = "content://com.android.externalstorage.documents/document/primary%3ADownload%2FMario%20Kart.gba";
    char restored_uri[4096];
    expect(portable_store_rom_path(document_uri), "remember picker document URI without native path conversion");
    expect(portable_stored_rom_path(restored_uri, sizeof restored_uri) &&
        strcmp(restored_uri, document_uri) == 0, "remembered content URI roundtrips verbatim");
    expect(cache_matches(remembered, (const uint8_t *)document_uri, strlen(document_uri)),
        "remembered URI file contains exact document identifier without cwd prefix");
    expect(!portable_store_rom_path(NULL) && !portable_store_rom_path(""),
        "invalid remembered path does not overwrite the document URI");
    expect(portable_stored_rom_path(restored_uri, sizeof restored_uri) &&
        strcmp(restored_uri, document_uri) == 0, "invalid path preserves prior remembered URI");
    remove(remembered);
    expect(write_file(cache, fixture, size), "seed title cache from validated fixture");
    const PortableAssetPackView *view = portable_ui_background_host_load(&host);
    expect(view != NULL && host.size == size && memcmp(host.bytes, fixture, size) == 0,
        "valid cache roundtrip retains exact bytes");
    if (failures) goto done;
    uint8_t *prior_bytes = host.bytes;
    uint8_t prior_digest[32];
    memcpy(prior_digest, view->pack_digest, sizeof prior_digest);
    const uint8_t bad_rom[3] = {1u, 2u, 3u};
    expect(write_file(invalid, bad_rom, sizeof bad_rom), "write authored invalid ROM");
    expect(!portable_ui_background_host_import(&host, invalid, error, sizeof error) &&
        strstr(error, "unmodified US") != NULL, "invalid owner ROM is rejected");
    expect(host.bytes == prior_bytes && portable_ui_background_host_load(&host) == view &&
        host.size == size && memcmp(host.bytes, fixture, size) == 0 &&
        memcmp(view->pack_digest, prior_digest, sizeof prior_digest) == 0 &&
        cache_matches(cache, fixture, size), "failed import preserves borrowed view and published cache");
    portable_ui_background_host_close(&host);

    fixture[size - 1u] ^= 1u;
    expect(write_file(cache, fixture, size), "write corrupted cache fixture");
    fixture[size - 1u] ^= 1u;
    expect(portable_ui_background_host_load(&host) == NULL && host.bytes == NULL && host.size == 0u,
        "corrupted cache is rejected without adopting bytes");
    portable_ui_background_host_close(&host);

    FILE *oversized = fopen(cache, "wb");
    int written = oversized != NULL && fseek(oversized, 8L * 1024L * 1024L, SEEK_SET) == 0 &&
        fputc(0, oversized) != EOF;
    if (oversized != NULL && fclose(oversized) != 0) written = 0;
    expect(written, "write oversized cache fixture");
    expect(portable_ui_background_host_load(&host) == NULL && host.bytes == NULL && host.size == 0u,
        "oversized cache is rejected without adopting bytes");
    portable_ui_background_host_close(&host);

    remove(cache);
    expect(portable_store_rom_path(rom_path), "remember original owner ROM path without copying ROM");
    view = portable_ui_background_host_load(&host);
    expect(view != NULL && host.size == size && memcmp(host.bytes, fixture, size) == 0 &&
        cache_matches(cache, fixture, size), "missing cache is lazily generated from remembered owner ROM");
    portable_ui_background_host_close(&host);
    expect(portable_ui_background_host_load(&host) != NULL && host.size == size &&
        memcmp(host.bytes, fixture, size) == 0, "published cache reopens on a fresh owner");
    snprintf(temporary, sizeof temporary, "%s.tmp", cache);
    FILE *leftover = fopen(temporary, "rb");
    expect(leftover == NULL, "successful publication leaves no temporary file");
    if (leftover != NULL) fclose(leftover);
done:
    portable_ui_background_host_close(&host);
    free(fixture);
    if (failures) return 1;
    puts("title background host: PASS");
    return 0;
}
#endif

int main(int argc, char **argv)
{
    if(argc==3 && strcmp(argv[1],"--source-portrait-banks")==0)return standings_source_check(argv[2]);
#ifdef PORTABLE_ENABLE_SDL3
    if (argc == 5 && strcmp(argv[1], "--title-cache") == 0)
        return title_cache_check(argv[2], argv[3], argv[4]);
#endif
    if (argc == 5 && strcmp(argv[1], "--title-presentation") == 0)
        return title_presentation_check(argv[2], argv[3], argv[4]);
    if (argc != 1) return 2;
    uint8_t digest[32];
    mksc_sha1("abc", 3, digest);
    expect_hex(digest, 20, "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 abc");
    mksc_sha256("abc", 3, digest);
    expect_hex(digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
    const char *two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    mksc_sha256(two_blocks, strlen(two_blocks), digest);
    expect_hex(digest, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 448-bit");

    MkscRomImportResult result;
    char error[256];
    uint8_t *rom = fixture_rom();
    expect(rom != NULL, "fixture allocation");
    if (rom == NULL) return 1;
    expect(!mksc_rom_import_supported(rom, ROM_SIZE), "fixture is not the supported ROM");
    expect(!mksc_rom_import(rom, ROM_SIZE, &result, error, sizeof error) && result.pack == NULL &&
           strstr(error, "unmodified US") != NULL, "foreign ROM is refused before importing");
    expect(!mksc_rom_import(rom, 3, &result, error, sizeof error), "truncated ROM is refused");
    memset(&result, 0xa5, sizeof result);
    expect(!mksc_rom_import_title_presentation(rom, ROM_SIZE, &result, error, sizeof error) &&
        empty_result(&result) && strstr(error, "unmodified US") != NULL,
        "foreign title ROM is refused with empty result");
    memset(&result, 0xa5, sizeof result);
    expect(!mksc_rom_import_title_presentation(rom, 3u, &result, error, sizeof error) &&
        empty_result(&result) && strstr(error, "unmodified US") != NULL,
        "truncated title ROM is refused with empty result");
    memset(&result, 0xa5, sizeof result);
    expect(!mksc_rom_import_title_presentation(NULL, 0u, &result, error, sizeof error) &&
        empty_result(&result), "empty title ROM is refused with empty result");

    const RiGroup groups[] = {ri_group_drivers, ri_group_overlays};
    expect(ri_build(rom, ROM_SIZE, 0, groups, 2, &result, error, sizeof error), error);
    if (result.pack != NULL) {
        expect(result.section_count == 28, "fixture section count");
        expect_hex(result.pack_sha256, 32,
                   "53677e14c11fd7edd9013043fd10026dddb9067f11572fd0e88e9160196ce051", "fixture pack matches the Python reference");
        expect(memcmp(result.pack + 40, result.pack_sha256, 32) == 0, "pack digest stored in header");
    }
    mksc_rom_import_free(&result);

    /* The import stamp reads back through the content registry; packs from
     * before it (here: drivers and overlays only) read as version 0. */
    const RiGroup stamped[] = {ri_group_drivers, ri_group_stamp};
    expect(ri_build(rom, ROM_SIZE, 0, stamped, 2, &result, error, sizeof error), error);
    if (result.pack != NULL) {
        PortableAssetPackView view;
        expect(portable_asset_pack_open(result.pack, result.pack_size, &view) == PORTABLE_ASSET_PACK_OK &&
               portable_content_import_version(&view) == MKSC_ROM_IMPORT_VERSION, "import stamp version");
    }
    mksc_rom_import_free(&result);
    expect(ri_build(rom, ROM_SIZE, 0, groups, 1, &result, error, sizeof error), error);
    if (result.pack != NULL) {
        PortableAssetPackView view;
        expect(portable_asset_pack_open(result.pack, result.pack_size, &view) == PORTABLE_ASSET_PACK_OK &&
               portable_content_import_version(&view) == 0u, "unstamped pack reads as version 0");
    }
    mksc_rom_import_free(&result);

    const RiGroup indexed_images[]={indexed_image_group};
    expect(ri_build(rom,ROM_SIZE,0,indexed_images,1,&result,error,sizeof error),error);
    mksc_rom_import_free(&result);

    const RiGroup ordered[] = {ordered_group};
    expect(ri_build(rom, ROM_SIZE, 0, ordered, 1, &result, error, sizeof error), "two-section pack");
    if (result.pack != NULL) {
        /* 112-byte header, sorted 40-byte entries, 8-byte aligned payloads. */
        expect(result.pack_size == 204, "two-section pack layout size");
        expect(ri_le32(result.pack + 112) == 3 && ri_le32(result.pack + 152) == 9, "sections sorted by kind");
        expect(memcmp(result.pack + 192, "early!", 6) == 0 && memcmp(result.pack + 200, "late", 4) == 0,
               "payloads aligned in table order");
        uint8_t check[32];
        uint8_t *copy = malloc(result.pack_size);
        if (copy != NULL) {
            memcpy(copy, result.pack, result.pack_size);
            memset(copy + 40, 0, 32);
            mksc_sha256(copy, result.pack_size, check);
            expect(memcmp(check, result.pack_sha256, 32) == 0, "pack digest covers the zeroed digest field");
            free(copy);
        }
    }
    mksc_rom_import_free(&result);

    const RiGroup duplicate[] = {duplicate_group};
    expect(!ri_build(rom, ROM_SIZE, 0, duplicate, 1, &result, error, sizeof error) &&
           strstr(error, "duplicate") != NULL, "duplicate sections are refused");
    const RiGroup bad[] = {bad_lz77_group};
    expect(!ri_build(rom, ROM_SIZE, 0, bad, 1, &result, error, sizeof error) &&
           strstr(error, "distance") != NULL, "LZ77 distance before output is refused");
    free(rom);
    if (failures) return 1;
    puts("rom import: PASS");
    return 0;
}
