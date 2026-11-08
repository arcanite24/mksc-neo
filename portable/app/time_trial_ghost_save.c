#include "time_trial_ghost_save.h"
#include "frontend_flow.h"

PortableGhostSaveAction portable_time_trial_ghost_save_tick(
    PortableGhostSaveMenu *menu,uint16_t triggered)
{
    if(menu==NULL)return PORTABLE_GHOST_SAVE_ACTION_NONE;
    if(menu->prompt){
        if(triggered&PORTABLE_FRONTEND_BUTTON_B){menu->prompt=0u;return PORTABLE_GHOST_SAVE_ACTION_NONE;}
        if(triggered&PORTABLE_FRONTEND_BUTTON_LEFT)menu->yes=0u;
        if(triggered&PORTABLE_FRONTEND_BUTTON_RIGHT)menu->yes=1u;
        if(triggered&PORTABLE_FRONTEND_BUTTON_A){
            menu->prompt=0u;
            return menu->yes?(menu->full_list?PORTABLE_GHOST_SAVE_ACTION_ERASE:
                PORTABLE_GHOST_SAVE_ACTION_WRITE):PORTABLE_GHOST_SAVE_ACTION_NONE;
        }
    }else if(menu->full_list){
        const unsigned count=menu->used_slots<PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS?
            menu->used_slots:PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS;
        if(count==0u){menu->full_list=0u;return PORTABLE_GHOST_SAVE_ACTION_NONE;}
        if(triggered&PORTABLE_FRONTEND_BUTTON_UP)
            menu->erase_cursor=(uint8_t)((menu->erase_cursor+count-1u)%count);
        if(triggered&PORTABLE_FRONTEND_BUTTON_DOWN)
            menu->erase_cursor=(uint8_t)((menu->erase_cursor+1u)%count);
        if(triggered&PORTABLE_FRONTEND_BUTTON_B)menu->full_list=0u;
        else if(triggered&PORTABLE_FRONTEND_BUTTON_A){menu->prompt=1u;menu->yes=0u;}
    }else{
        if((triggered&PORTABLE_FRONTEND_BUTTON_A)&&menu->status==PORTABLE_GHOST_SAVE_FULL){
            menu->full_list=1u;menu->erase_cursor=0u;
        }
        if(triggered&PORTABLE_FRONTEND_BUTTON_B)return PORTABLE_GHOST_SAVE_ACTION_BACK;
        if((triggered&PORTABLE_FRONTEND_BUTTON_A)&&
           (menu->status==PORTABLE_GHOST_SAVE_READY||menu->status==PORTABLE_GHOST_SAVE_FAILED)){
            menu->prompt=1u;menu->yes=0u;
        }
    }
    return PORTABLE_GHOST_SAVE_ACTION_NONE;
}
