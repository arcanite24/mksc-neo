#ifndef PORTABLE_NDS_FS_H
#define PORTABLE_NDS_FS_H

/* Nintendo DS cartridge pieces for importing other games' content: the card
 * header, the FNT/FAT filesystem, NARC archives, BIOS LZ77 and BMG text. Reads
 * are bounds-checked through the importer context; a failure names the file
 * and field, then aborts the import (rom_import_internal.h). No file, OS or
 * SDL APIs. */

#include "rom_import_internal.h"

#define NDS_PATH_CAPACITY 128u
#define NDS_FILE_LIMIT 4096u
#define NDS_HEADER_BYTES 0x200u

typedef struct NdsHeader
{
    char title[13];
    char game_code[5];
    uint32_t version;
    uint32_t fnt_offset, fnt_size, fat_offset, fat_size;
} NdsHeader;

typedef struct NdsEntry
{
    char path[NDS_PATH_CAPACITY];
    RiData data;
} NdsEntry;

/* The cartridge filesystem or one NARC, entries in file-ID order. Overlays
 * and other unnamed cartridge files are not listed. */
typedef struct NdsFs
{
    const char *name;
    const NdsEntry *entries;
    size_t count;
} NdsFs;

/* CRC-16/MODBUS, as the header checksum at 0x15E uses. */
uint16_t nds_crc16(const uint8_t *data, size_t size);
/* 1 when rom has a DS header whose checksum matches. */
int nds_header_valid(const uint8_t *rom, size_t size);
void nds_header(RiCtx *ctx, RiData rom, NdsHeader *out);
NdsFs nds_rom_fs(RiCtx *ctx, RiData rom, const NdsHeader *header);
/* Opens a NARC, first decompressing an LZ77 wrapper (.carc). name is kept
 * for diagnostics and must outlive the result. */
NdsFs nds_narc(RiCtx *ctx, RiData file, const char *name);
int nds_find(const NdsFs *fs, const char *path, RiData *out);
/* Like nds_find, but a missing file fails the import naming both paths. */
RiData nds_file(RiCtx *ctx, const NdsFs *fs, const char *path);
/* BIOS LZ77 type 0x10 or 0x11 at the start of source; at most limit bytes. */
RiBuf *nds_lz77(RiCtx *ctx, RiData source, size_t limit, const char *name);

/* BMG message file ("MESGbmg1", UTF-16LE). */
typedef struct NdsBmg
{
    const char *name;
    RiData offsets, text;
    uint32_t count, entry_size;
} NdsBmg;

/* Replacement text for a private-use glyph, such as a platform badge. */
typedef struct NdsGlyph
{
    uint16_t code;
    const char *text;
} NdsGlyph;

NdsBmg nds_bmg(RiCtx *ctx, RiData file, const char *name);
/* Message index as ASCII. Control tags are dropped and mapped glyphs are
 * replaced; any other non-ASCII character fails naming the message. */
void nds_bmg_ascii(RiCtx *ctx, const NdsBmg *bmg, uint32_t index, const NdsGlyph *glyphs,
                   size_t glyph_count, char *out, size_t capacity);

#endif
