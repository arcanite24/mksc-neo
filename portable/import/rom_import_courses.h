#ifndef PORTABLE_ROM_IMPORT_COURSES_H
#define PORTABLE_ROM_IMPORT_COURSES_H

/* Shared between rom_import_courses.c (group, shared art, rules, standings)
 * and rom_import_course_race.c (course_rom.course_sections). */

#include "rom_import_internal.h"

/* Offsets computed with Python ints: negative or absurd values fail like
 * course_rom.Data.span instead of wrapping. */
size_t rc_offset(RiCtx *ctx, int64_t value);
uint32_t rc_u8(RiCtx *ctx, RiData source, int64_t offset);
uint32_t rc_u16(RiCtx *ctx, RiData source, int64_t offset);
uint32_t rc_u32(RiCtx *ctx, RiData source, int64_t offset);
RiData rc_span(RiCtx *ctx, RiData source, int64_t offset, size_t size);
void rc_put_span(RiCtx *ctx, RiBuf *out, RiData source, int64_t offset, size_t size);

/* struct.pack('<I' / '<H' / '<B') of a Python int: out-of-range values fail. */
void rc_word(RiCtx *ctx, RiBuf *out, int64_t value);
void rc_half(RiCtx *ctx, RiBuf *out, int64_t value);
void rc_byte(RiCtx *ctx, RiBuf *out, int64_t value);

/* rom_course_art.rom_grid: up to eight (x, y, heading) start rows. */
typedef struct RcGridRow {
    uint32_t x, y, heading;
} RcGridRow;
uint32_t rc_rom_grid(RiCtx *ctx, uint32_t header, uint32_t slots, RcGridRow rows[8]);

/* course_rom.course_sections; content_id < 0 means None. minimap is the
 * shared_sections minimap payload, copied into each course's kind 11. */
void rc_course_sections(RiCtx *ctx, uint32_t header, int source_progress, int64_t content_id,
                        int arena, const RiBuf *minimap);

#endif
