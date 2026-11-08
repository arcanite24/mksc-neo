#ifndef PORTABLE_ROM_IMPORT_INTERNAL_H
#define PORTABLE_ROM_IMPORT_INTERNAL_H

/* Shared helpers for the importer sections. Every failure longjmps to the
 * build entry point, which releases all importer allocations, so section code
 * reads like the Python reference: validate, fail loudly, never clean up. */

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "rom_import.h"

#if defined(__GNUC__) || defined(__clang__)
#define RI_NORETURN __attribute__((noreturn))
#define RI_PRINTF(a, b) __attribute__((format(printf, a, b)))
#elif defined(_MSC_VER)
#define RI_NORETURN __declspec(noreturn)
#define RI_PRINTF(a, b)
#else
#define RI_NORETURN
#define RI_PRINTF(a, b)
#endif

/* Growable byte buffer owned by the importer context. */
typedef struct RiBuf {
    uint8_t *data;
    size_t size;
    size_t capacity;
    struct RiBuf *previous;
    struct RiBuf *next;
} RiBuf;

/* Read-only bounds-checked view (ROM, a container slice, a decoded block). */
typedef struct RiData {
    const uint8_t *data;
    size_t size;
} RiData;

typedef struct RiSection {
    uint32_t kind;
    uint32_t content_id;
    uint32_t version;
    RiBuf *data;
} RiSection;

typedef struct RiCtx {
    RiData rom;
    jmp_buf failure;
    char *error;
    size_t error_size;
    RiBuf *buffers;
    RiSection *sections;
    size_t section_count;
    size_t section_capacity;
    MkscImportProgress progress;
    void *progress_context;
    void *job; /* importer-specific state shared by its groups */
    int cancelled;
} RiCtx;

typedef void (*RiGroup)(RiCtx *ctx);

RI_NORETURN void ri_fail(RiCtx *ctx, const char *format, ...) RI_PRINTF(2, 3);

/* Buffers. ri_buf_free is optional: everything is released at the end. */
RiBuf *ri_buf(RiCtx *ctx);
RiBuf *ri_buf_zero(RiCtx *ctx, size_t size);
RiBuf *ri_buf_copy(RiCtx *ctx, const void *data, size_t size);
void ri_buf_free(RiCtx *ctx, RiBuf *buffer);
void ri_reserve(RiCtx *ctx, RiBuf *buffer, size_t capacity);
void ri_resize(RiCtx *ctx, RiBuf *buffer, size_t size); /* new bytes are zero */
void ri_put(RiCtx *ctx, RiBuf *buffer, const void *data, size_t size);
void ri_put_buf(RiCtx *ctx, RiBuf *buffer, const RiBuf *other);
void ri_put_zero(RiCtx *ctx, RiBuf *buffer, size_t size);
void ri_u8(RiCtx *ctx, RiBuf *buffer, uint32_t value);
void ri_u16(RiCtx *ctx, RiBuf *buffer, uint32_t value);
void ri_u32(RiCtx *ctx, RiBuf *buffer, uint32_t value);
void ri_i16(RiCtx *ctx, RiBuf *buffer, int32_t value);
void ri_i32(RiCtx *ctx, RiBuf *buffer, int32_t value);
/* struct.pack('<nI', ...) */
void ri_words(RiCtx *ctx, RiBuf *buffer, size_t count, ...);
void ri_set_u16(RiBuf *buffer, size_t offset, uint32_t value);
void ri_set_u32(RiBuf *buffer, size_t offset, uint32_t value);
RiData ri_view(const RiBuf *buffer);

/* Bounds-checked reads; offsets are relative to the view. */
RiData ri_span(RiCtx *ctx, RiData source, size_t offset, size_t size);
const uint8_t *ri_at(RiCtx *ctx, RiData source, size_t offset, size_t size);
uint32_t ri_get_u8(RiCtx *ctx, RiData source, size_t offset);
uint32_t ri_get_u16(RiCtx *ctx, RiData source, size_t offset);
uint32_t ri_get_u32(RiCtx *ctx, RiData source, size_t offset);
int32_t ri_get_i16(RiCtx *ctx, RiData source, size_t offset);
int32_t ri_get_i32(RiCtx *ctx, RiData source, size_t offset);
/* GBA ROM pointer at offset, minus 0x08000000, checked inside source. */
size_t ri_pointer(RiCtx *ctx, RiData source, size_t offset);
/* Plain little-endian reads without checks, for already validated spans. */
uint32_t ri_le16(const uint8_t *bytes);
uint32_t ri_le32(const uint8_t *bytes);

/* GBA BIOS LZ77 (type 0x10) at offset. */
RiBuf *ri_lz77(RiCtx *ctx, RiData source, size_t offset);

/* Appends a section; ownership of data moves to the section list. */
void ri_section(RiCtx *ctx, uint32_t kind, uint32_t content_id, uint32_t version, RiBuf *data);

/* Native image helpers matching tools/portable/ui_rom.py. */
uint32_t ri_color(uint32_t bgr555);
/* depth 4 or 8; returns 0 for transparent index 0. */
uint32_t ri_tile_pixel(RiCtx *ctx, RiData tiles, RiData palette, uint32_t tile,
                       uint32_t x, uint32_t y, uint32_t palette_bank, int depth);
/* Screen-block tile map (width, height in cells, multiples of 32) into ARGB
 * pixels; returns width*8 x height*8 little-endian words (see ri_pixel). */
RiBuf *ri_tile_map(RiCtx *ctx, RiData tiles, RiData palette, RiData cells,
                   uint32_t width, uint32_t height, int depth);
/* MKSCIMG1 asset from a buffer of width*height little-endian ARGB words. */
RiBuf *ri_image_asset(RiCtx *ctx, uint32_t width, uint32_t height, const RiBuf *pixels,
                      int32_t anchor_x, int32_t anchor_y);
/* Source CellData list at address in rom (ui_rom.cell_image). */
RiBuf *ri_cell_image_indexed(RiCtx *ctx, RiData rom, size_t address, RiData tiles, RiData palette,
                             uint32_t palette_base);
RiBuf *ri_cell_image(RiCtx *ctx, RiData rom, size_t address, RiData tiles, RiData palette);
uint32_t ri_pixel(const RiBuf *pixels, size_t index);
void ri_set_pixel(RiBuf *pixels, size_t index, uint32_t value);

RiBuf *ri_ambient_payload(RiCtx *ctx,uint32_t behavior,uint32_t animation,uint32_t material,RiData object_palette);

/* Runs groups over rom into a pack. verify_sha1 refuses any other ROM. */
int ri_build(const uint8_t *rom, size_t size, int verify_sha1, const RiGroup *groups,
             size_t group_count, MkscRomImportResult *result, char *error, size_t error_size);
/* ri_build for other games: no Super Circuit check, job is shared with the
 * groups as ctx->job, and ri_step reports progress. Returns 1 on success, 0
 * on failure and -1 when the progress callback cancelled. */
int ri_build_steps(const uint8_t *rom, size_t size, const RiGroup *groups, size_t group_count,
                   void *job, MkscImportProgress progress, void *context,
                   MkscRomImportResult *result, char *error, size_t error_size);
/* Runs attempt, catching its failure: returns 1 when it completed, else 0
 * with the failure text in reason. Cancellation still ends the import.
 * Buffers stay owned by the context; attempt should add sections last. */
typedef void (*RiAttempt)(RiCtx *ctx, void *argument);
int ri_try(RiCtx *ctx, RiAttempt attempt, void *argument, char *reason, size_t reason_size);
/* Reports done of total steps; a cancelling callback fails the import. */
void ri_step(RiCtx *ctx, uint32_t done, uint32_t total);

/* Section groups, one per reference module family. */
void ri_group_drivers(RiCtx *ctx);  /* build_driver_assets.py */
void ri_group_overlays(RiCtx *ctx); /* lightning, finish banner, GP results */
void ri_group_courses(RiCtx *ctx);  /* course_rom.py, standings, grids, catalogs, rules */
void ri_group_audio(RiCtx *ctx);    /* audio_rom.py and export_audio_*.py */
void ri_group_frontend(RiCtx *ctx); /* ui_rom/menus/countdown/pause/ceremony/ui_text_mod */
void ri_group_stamp(RiCtx *ctx);    /* kind80 "IMP1" + MKSC_ROM_IMPORT_VERSION */

#endif
