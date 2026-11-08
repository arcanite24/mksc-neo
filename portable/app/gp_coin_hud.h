#ifndef PORTABLE_GP_COIN_HUD_H
#define PORTABLE_GP_COIN_HUD_H
#include <stdint.h>

enum { PORTABLE_GP_COIN_HUD_ROM_BYTES=18u };
typedef struct {
    uint16_t pending_cells[3],displayed_cells[3];
    uint16_t pending_colors[3],displayed_colors[3];
    uint32_t clock;
    uint8_t initialized,warning;
} PortableGpCoinHud;

/* DA80 uses the pre-motion coin count and current clock. The map/palette
 * upload displays the preceding pending state. Source colors are immutable
 * ROM64C94/64C9C/64CA4 bytes, not captured HUD frames. */
int portable_gp_coin_hud_init(PortableGpCoinHud *state,uint32_t preceding_clock,
    uint16_t coins,const uint8_t colors[18]);
int portable_gp_coin_hud_step(PortableGpCoinHud *state,uint32_t clock,
    uint16_t coins,uint32_t driver_flags,uint8_t phase,
    const uint8_t colors[18]);
int portable_gp_coin_hud_warning(uint32_t clock,uint16_t coins,
    uint32_t driver_flags,uint8_t phase);
#endif
