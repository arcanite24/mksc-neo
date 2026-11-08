#ifndef PORTABLE_LOCAL_PLAYERS_H
#define PORTABLE_LOCAL_PLAYERS_H

#include <stdint.h>

#define PORTABLE_LOCAL_PLAYERS_MAX 4u

/* Input sources for local multiplayer. Player i drives gamepad slot i (pads
 * in connection order); players 1 and 2 also own keyboard layouts 1 and 2. */
typedef struct {
    uint8_t keyboard; /* 0 none, 1 primary layout, 2 second layout */
    int8_t pad_slot;  /* -1 none */
} PortableLocalInput;

/* Two players always fit on one keyboard; more need a pad each. */
unsigned portable_local_players_max(unsigned pad_count);
int portable_local_players_assign(unsigned players,unsigned pad_count,
    PortableLocalInput inputs[PORTABLE_LOCAL_PLAYERS_MAX]);
/* Merge each player's keyboard and pad driving bits. */
void portable_local_players_buttons(const PortableLocalInput *inputs,unsigned players,
    uint16_t keyboard1,uint16_t keyboard2,const uint16_t pads[PORTABLE_LOCAL_PLAYERS_MAX],
    uint16_t buttons[PORTABLE_LOCAL_PLAYERS_MAX]);

#endif
