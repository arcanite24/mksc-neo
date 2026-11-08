#ifndef PORTABLE_ROM_IMPORT_FRONTEND_H
#define PORTABLE_ROM_IMPORT_FRONTEND_H

/* Helpers shared by the frontend art importers (title, menus, ceremony). */

#include "rom_import_internal.h"

/* Python `target[start:stop] = data`, including the slice clamping and the
 * length change when the replacement is longer or shorter than the slice. */
void ri_fe_splice(RiCtx *ctx, RiBuf *target, size_t start, size_t stop, RiData data);
/* target[offset:offset + len(data)] = data */
void ri_fe_place(RiCtx *ctx, RiBuf *target, size_t offset, RiData data);
/* Python rom[start:stop] for non-negative bounds (truncated at the end). */
RiData ri_fe_rom_slice(RiCtx *ctx, size_t start, size_t stop);
/* decode_lz77(rom, offset) */
RiBuf *ri_fe_lz(RiCtx *ctx, size_t offset);
/* The modules' local pointer(): u32 at address minus 0x08000000 inside the ROM. */
size_t ri_fe_pointer(RiCtx *ctx, size_t address);
/* Python floor division for a positive divisor. */
int32_t ri_fe_floordiv(int32_t value, int32_t divisor);
/* The top-left 240x160 pixels of a 256-pixel-wide tile map. */
RiBuf *ri_fe_crop_screen(RiCtx *ctx, const RiBuf *pixels, size_t width);
/* Adds (x, y) to an MKSCIMG1 anchor (character_menu_rom.placed); course
 * origin() is ri_fe_set_anchor(..., 0, 0). */
void ri_fe_move_anchor(RiCtx *ctx, RiBuf *image, int32_t x, int32_t y);
void ri_fe_set_anchor(RiCtx *ctx, RiBuf *image, int32_t x, int32_t y);
/* Payload of an already emitted section (the ceremony reuses menu art). */
const RiBuf *ri_fe_section(RiCtx *ctx, uint32_t kind, uint32_t content_id);

/* One importer per reference module, emitting kind-15 sections, version 1. */
void ri_fe_title_sections(RiCtx *ctx);     /* ui_rom.title_sections */
/* Separate optional pack, excluded from ri_group_frontend/base import. */
void ri_fe_title_presentation_sections(RiCtx *ctx);
void ri_fe_menu_sections(RiCtx *ctx);      /* menu_rom.menu_sections */
void ri_fe_countdown_sections(RiCtx *ctx); /* countdown_rom.countdown_sections */
RiBuf *ri_fe_finish_lakitu_program(RiCtx *ctx,size_t cells[8]);
void ri_fe_character_sections(RiCtx *ctx); /* character_menu_rom.character_sections */
void ri_fe_course_sections(RiCtx *ctx);    /* course_menu_rom.course_sections */
void ri_fe_pause_sections(RiCtx *ctx);     /* pause_menu_rom.pause_sections */
void ri_fe_failure_sections(RiCtx *ctx);   /* gp_failure_menu_rom.failure_sections */
/* ceremony_rom.ceremony_payload; needs the course and pause sections. */
RiBuf *ri_fe_ceremony_payload(RiCtx *ctx);

#define RI_FE_COURSE_CUP 0x23100u
#define RI_FE_COURSE_CUP_NAME 0x23200u
#define RI_FE_PAUSE_SELECTED_LABEL 0x24200u

#endif
