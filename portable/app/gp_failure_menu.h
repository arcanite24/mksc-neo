#ifndef PORTABLE_GP_FAILURE_MENU_H
#define PORTABLE_GP_FAILURE_MENU_H

#include <stdint.h>

typedef enum
{
    PORTABLE_GP_FAILURE_MENU_CHOOSE = 0,
    PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT,
    PORTABLE_GP_FAILURE_MENU_QUIT_OPEN,
    PORTABLE_GP_FAILURE_MENU_QUIT_NO_RETURN,
    PORTABLE_GP_FAILURE_MENU_QUIT_YES_START,
    PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER,
    PORTABLE_GP_FAILURE_MENU_RETRY_HOLD,
    PORTABLE_GP_FAILURE_MENU_RETRY_SHUTTER,
    PORTABLE_GP_FAILURE_MENU_GAME_OVER,
    PORTABLE_GP_FAILURE_MENU_COMPLETE,
    PORTABLE_GP_FAILURE_MENU_OPEN,
    PORTABLE_GP_FAILURE_MENU_DISPATCH,
    PORTABLE_GP_FAILURE_MENU_GAME_OVER_SETUP
} PortableGpFailureMenuPhase;

typedef enum
{
    PORTABLE_GP_FAILURE_ACTION_NONE = 0,
    PORTABLE_GP_FAILURE_ACTION_RETRY = 1,
    PORTABLE_GP_FAILURE_ACTION_QUIT = 4
} PortableGpFailureAction;

typedef struct
{
    PortableGpFailureMenuPhase phase;
    uint8_t choice; /* 0: TRY AGAIN, 1: QUIT */
    uint8_t confirm_yes;
    uint8_t retry_credits; /* source2C3C count, decremented by state30 */
    uint16_t phase_ticks;
    PortableGpFailureAction action;
    uint16_t cue; /* one-tick 801E7C8 song: 141 move, 142/143 choices */
} PortableGpFailureMenu;

int portable_gp_failure_menu_init(PortableGpFailureMenu *menu, uint8_t retry_credits);
int portable_gp_failure_menu_tick(PortableGpFailureMenu *menu,
                                  uint16_t triggered_keys);

#endif
