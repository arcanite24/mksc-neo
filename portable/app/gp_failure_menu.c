#include "gp_failure_menu.h"

#include <string.h>

int portable_gp_failure_menu_init(PortableGpFailureMenu *menu, uint8_t retry_credits)
{
    if (menu == NULL || retry_credits > 3u) return 0;
    memset(menu, 0, sizeof *menu);
    menu->retry_credits=retry_credits;
    menu->phase=retry_credits?PORTABLE_GP_FAILURE_MENU_OPEN:PORTABLE_GP_FAILURE_MENU_GAME_OVER_SETUP;
    return 1;
}

int portable_gp_failure_menu_tick(PortableGpFailureMenu *menu,
                                  uint16_t triggered_keys)
{
    if (menu == NULL)
        return 0;
    menu->cue = 0u;
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_COMPLETE)
        return 1;
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_OPEN){
        if(++menu->phase_ticks>=6u){menu->phase=PORTABLE_GP_FAILURE_MENU_CHOOSE;menu->phase_ticks=0u;}
        return 1;
    }
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_GAME_OVER_SETUP){
        if(++menu->phase_ticks>=3u){menu->phase=PORTABLE_GP_FAILURE_MENU_GAME_OVER;menu->phase_ticks=0u;}
        return 1;
    }
    if(menu->phase==PORTABLE_GP_FAILURE_MENU_DISPATCH){
        menu->cue=menu->choice?142u:143u;
        menu->phase_ticks=0u;
        if(menu->choice){menu->confirm_yes=0u;menu->phase=PORTABLE_GP_FAILURE_MENU_QUIT_OPEN;}
        else{
            if(!menu->retry_credits)return 0;
            --menu->retry_credits;menu->action=PORTABLE_GP_FAILURE_ACTION_RETRY;
            menu->phase=PORTABLE_GP_FAILURE_MENU_RETRY_HOLD;
        }
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_GAME_OVER)
    {
        /* Source state 32 ignores confirmation for the first 60 ticks. */
        if (menu->phase_ticks < 60u) ++menu->phase_ticks;
        else if ((triggered_keys & UINT16_C(9)) != 0u)
        {
            menu->cue = 143u;
            menu->action = PORTABLE_GP_FAILURE_ACTION_QUIT;
            menu->phase = PORTABLE_GP_FAILURE_MENU_QUIT_YES_START;
            menu->phase_ticks = 0u;
        }
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_CHOOSE)
    {
        /* sub_801E7C8 state 29 accepts vertical choice and A/Start. */
        if((triggered_keys&UINT16_C(0x00c0))!=0u){
            menu->choice=(uint8_t)(1u-menu->choice);menu->cue=141u;
        }
        if((triggered_keys&UINT16_C(9))!=0u){
            /* State29 only queues30; the next callback owns cue and credit loss. */
            menu->phase=PORTABLE_GP_FAILURE_MENU_DISPATCH;
            menu->phase_ticks=0u;
        }
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_RETRY_HOLD)
    {
        if (menu->phase_ticks++ >= 48u)
        {
            menu->phase = PORTABLE_GP_FAILURE_MENU_RETRY_SHUTTER;
            menu->phase_ticks = 0u;
        }
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_RETRY_SHUTTER)
    {
        if (++menu->phase_ticks >= 64u)
            menu->phase = PORTABLE_GP_FAILURE_MENU_COMPLETE;
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_QUIT_OPEN)
    {
        if (++menu->phase_ticks > 7u)
        {
            menu->phase = PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT;
            menu->phase_ticks = 0u;
        }
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_CONFIRM_QUIT)
    {
        /* Source state 36 defaults to NO; LEFT selects YES. */
        if((triggered_keys&UINT16_C(0x0030))!=0u){
            menu->confirm_yes=(uint8_t)(1u-menu->confirm_yes);menu->cue=141u;
        }
        if ((triggered_keys & UINT16_C(9)) != 0u)
        {
            menu->cue = menu->confirm_yes != 0u ? 143u : 142u;
            if (menu->confirm_yes != 0u)
            {
                menu->action = PORTABLE_GP_FAILURE_ACTION_QUIT;
                menu->phase = PORTABLE_GP_FAILURE_MENU_QUIT_YES_START;
                menu->phase_ticks = 0u;
            }
            else
            {
                menu->phase = PORTABLE_GP_FAILURE_MENU_QUIT_NO_RETURN;
                menu->phase_ticks = 0u;
            }
        }
        else ++menu->phase_ticks;
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_QUIT_NO_RETURN)
    {
        if (++menu->phase_ticks > 6u)
        {
            menu->choice = 1u;
            menu->phase = PORTABLE_GP_FAILURE_MENU_CHOOSE;
        }
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_QUIT_YES_START)
    {
        menu->phase = PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER;
        menu->phase_ticks = 0u;
        return 1;
    }
    if (menu->phase == PORTABLE_GP_FAILURE_MENU_QUIT_SHUTTER)
    {
        if (++menu->phase_ticks >= 64u)
            menu->phase = PORTABLE_GP_FAILURE_MENU_COMPLETE;
        return 1;
    }
    return 0;
}
