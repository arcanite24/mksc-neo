#ifndef PORTABLE_GP_TURN_CUE_H
#define PORTABLE_GP_TURN_CUE_H

#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_GP_TURN_CUE_CELLS=12,
       PORTABLE_GP_TURN_CUE_GLYPH_BYTES=384 };

typedef struct {
    uint16_t pending_cells[12], displayed_cells[12];
    uint32_t offset, elapsed, blink, cycles;
    int32_t origin, lap;
    uint8_t previous, active, hidden, glyph, displayed_glyph;
} PortableGpTurnCue;

/* 804EDAC resets this state to zero. Call804E4CC once before motion,
 * after latching the preceding map/glyph DMA. Entries are four ROM bytes;
 * maps contain their4x3 dimensions followed by twelve little-endian cells.
 * The caller supplies native route/lap/flags and ordinary Lakitu eligibility, never guest addresses. */
int portable_gp_turn_cue_step(PortableGpTurnCue *state,
    const uint8_t *entries,size_t entry_count,const uint8_t maps[84],
    uint8_t route,int32_t lap,uint32_t flags,uint8_t ordinary_lakitu,
    uint16_t checkpoint_count,uint8_t final_lap);

#endif
