#include "controller_input.h"
#include "driver_input.h"

enum { CONTROLLER_AXIS_THRESHOLD = 8000 };

uint16_t portable_controller_driving_buttons(const PortableControllerSample *sample)
{
    return portable_controller_driving_buttons_deadzone(sample,CONTROLLER_AXIS_THRESHOLD);
}

uint16_t portable_controller_driving_buttons_deadzone(const PortableControllerSample *sample,uint16_t deadzone)
{
    if(!sample||deadzone<1u||deadzone>30000u)return 0;
    uint16_t result=0;
    if(sample->south || sample->right_trigger>=CONTROLLER_AXIS_THRESHOLD)
        result|=PORTABLE_INPUT_ACCELERATE;
    if(sample->east || sample->left_trigger>=CONTROLLER_AXIS_THRESHOLD)
        result|=PORTABLE_INPUT_BRAKE;
    if(sample->dpad_left || sample->dpad_right){
        if(sample->dpad_left&&!sample->dpad_right)result|=PORTABLE_INPUT_LEFT;
        if(sample->dpad_right&&!sample->dpad_left)result|=PORTABLE_INPUT_RIGHT;
    }else{
        if(sample->left_x<=-(int)deadzone)result|=PORTABLE_INPUT_LEFT;
        if(sample->left_x>=(int)deadzone)result|=PORTABLE_INPUT_RIGHT;
    }
    return result;
}

static uint16_t keyboard_layout_buttons(const bool *keys,size_t count,const uint16_t layout[8])
{
    static const uint16_t masks[]={PORTABLE_INPUT_ACCELERATE,PORTABLE_INPUT_BRAKE,
        PORTABLE_INPUT_LEFT,PORTABLE_INPUT_RIGHT,PORTABLE_INPUT_HOP,PORTABLE_INPUT_ITEM,
        PORTABLE_INPUT_UP,PORTABLE_INPUT_DOWN};
    uint16_t buttons=0u;
    for(unsigned action=0u;action<8u;++action){
        if(layout[action]>=count)return 0u;
        if(keys[layout[action]])buttons|=masks[action];
    }
    return buttons;
}

uint16_t portable_keyboard2_driving_buttons(const bool *keys,size_t count,const PortableSettings *settings)
{
    return keys!=NULL&&settings!=NULL?keyboard_layout_buttons(keys,count,settings->keys+8):0u;
}

uint16_t portable_keyboard_driving_buttons(const bool *keys,size_t count,const PortableSettings *settings)
{
    if(keys==NULL||settings==NULL)return 0u;
    for(unsigned action=0u;action<8u;++action)if(settings->keys[action]>=count)return 0u;
    uint16_t buttons=keyboard_layout_buttons(keys,count,settings->keys);
    /* USB scancodes: Z=29, Up=82, Left Shift=225, Right Shift=229. */
    if(settings->keys[0]==29u&&count>82u&&keys[82])buttons|=PORTABLE_INPUT_ACCELERATE;
    if(settings->keys[4]==225u&&count>229u&&keys[229])buttons|=PORTABLE_INPUT_HOP;
    return buttons;
}
