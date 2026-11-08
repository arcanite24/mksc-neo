#ifndef PORTABLE_GP_FINISH_PORTRAITS_H
#define PORTABLE_GP_FINISH_PORTRAITS_H

#include <stdint.h>

enum { PORTABLE_GP_FINISH_PORTRAITS = 8 };
typedef struct {
    uint16_t x_tile, progress, delay;
    int16_t driver_class;
    uint8_t racer_index;
} PortableGpFinishPortrait;

typedef struct {
    PortableGpFinishPortrait rows[PORTABLE_GP_FINISH_PORTRAITS];
    uint8_t count, selected;
} PortableGpFinishPortraits;

static inline int portable_gp_finish_portraits_init(PortableGpFinishPortraits *state,
    uint8_t count, uint8_t selected)
{
    if (!state || count == 0u || count > 8u || selected >= count) return 0;
    *state = (PortableGpFinishPortraits){.count=count,.selected=selected};
    for (unsigned rank=0u;rank<8u;++rank) state->rows[rank].driver_class=-1;
    return 1;
}

/* 1D2A4 registers finished racers in rank order. 1D33C stops at the first
 * unfinished rank, retaining each earlier portrait's stagger and progress. */
static inline int portable_gp_finish_portraits_tick(PortableGpFinishPortraits *state,
    const uint32_t flags[8], const uint8_t classes[8], const uint8_t seats[8])
{
    if (!state || !flags || !classes || !seats || !state->count || state->count>8u) return 0;
    for (unsigned rank=0u;rank<state->count;++rank)
        if (classes[rank]>7u || seats[rank]>=state->count) return 0;
    for (unsigned rank=0u;rank<state->count;++rank) {
        PortableGpFinishPortrait *row=&state->rows[rank];
        if ((flags[rank]&UINT32_C(0x20)) && row->driver_class<0) {
            const unsigned target=rank*3u+(rank>3u?5u:0u);
            *row=(PortableGpFinishPortrait){.x_tile=(uint16_t)(target+35u),
                .delay=(uint16_t)(rank*3u),.driver_class=classes[rank],.racer_index=seats[rank]};
        }
    }
    for (unsigned rank=0u;rank<state->count;++rank) {
        PortableGpFinishPortrait *row=&state->rows[rank];
        if (row->driver_class<0) break;
        if (row->delay) --row->delay;
        else {
            if (row->progress<32u) ++row->progress;
            const int target=(int)(rank*3u+(rank>3u?5u:0u));
            /* The source adds31 before shifting a negative delta: signed
             * division expresses its truncation toward zero explicitly. */
            row->x_tile=(uint16_t)(target+35+(-35*(int)row->progress)/32);
        }
    }
    return 1;
}

#endif
