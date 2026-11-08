#ifndef PORTABLE_LAP_HUD_H
#define PORTABLE_LAP_HUD_H
#include <stdint.h>

/* F1F8/F1A8 run before driver progress. A pending map write appears in the
 * next accepted presentation; it is not an arbitrary animation delay. */
typedef struct {
    uint8_t pending_lap,displayed_lap,initialized;
} PortableLapHud;

static inline void portable_lap_hud_init(PortableLapHud *state)
{
    if(state)*state=(PortableLapHud){1u,1u,1u};
}

static inline uint8_t portable_lap_hud_digit(uint16_t max_progress,uint32_t flags)
{
    const uint32_t base=(flags&UINT32_C(0x20))?UINT32_C(0x80):UINT32_C(0x7f);
    const uint8_t lap=(uint8_t)(((uint32_t)(max_progress>>8u)-base)&7u);
    return lap?lap:1u;
}

static inline void portable_lap_hud_step(PortableLapHud *state,
    uint16_t max_progress,uint32_t flags)
{
    if(!state)return;
    if(!state->initialized)portable_lap_hud_init(state);
    state->displayed_lap=state->pending_lap;
    state->pending_lap=portable_lap_hud_digit(max_progress,flags);
}
#endif
