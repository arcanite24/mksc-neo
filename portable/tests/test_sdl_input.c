#include "sdl_gamepad.h"
#include "driver_input.h"
#include "frontend_flow.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"SDL input line %d: %s: %s\n",__LINE__,#x,SDL_GetError());exit(1);}}while(0)
int main(void)
{
    CHECK(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1"));
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1u;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1u;
    desc.name="Native remapping test";
    const SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);CHECK(id!=0u);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);CHECK(joystick!=NULL);
    SDL_Gamepad *pad=SDL_OpenGamepad(id);CHECK(pad!=NULL);
    PortableSettings settings;portable_settings_defaults(&settings);
    settings.pad_buttons[0]=SDL_GAMEPAD_BUTTON_NORTH;
    settings.pad_buttons[1]=SDL_GAMEPAD_BUTTON_LEFT_STICK;
    settings.pad_buttons[2]=SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    settings.pad_buttons[3]=SDL_GAMEPAD_BUTTON_MISC1;
    portable_sdl_input_configure(&settings);
    for(unsigned i=0u;i<4u;++i)CHECK(SDL_SetJoystickVirtualButton(joystick,settings.pad_buttons[i],true));
    SDL_UpdateGamepads();
    CHECK(portable_sdl_gamepad_driving(pad)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_BRAKE|PORTABLE_INPUT_HOP|PORTABLE_INPUT_ITEM));
    CHECK(portable_sdl_gamepad_frontend(pad)==0u);
    for(unsigned i=0u;i<4u;++i)CHECK(SDL_SetJoystickVirtualButton(joystick,settings.pad_buttons[i],false));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));
    SDL_UpdateGamepads();
    CHECK(portable_sdl_gamepad_driving(pad)==0u);
    CHECK(portable_sdl_gamepad_frontend(pad)==PORTABLE_FRONTEND_BUTTON_A);
    portable_sdl_input_configure(NULL);
    CHECK(portable_sdl_gamepad_driving(pad)==PORTABLE_INPUT_ACCELERATE);
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true));
    SDL_UpdateGamepads();
    CHECK(portable_sdl_gamepad_driving(pad)==
        (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_BRAKE|PORTABLE_INPUT_HOP));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,false));
    SDL_UpdateGamepads();
    CHECK(portable_sdl_gamepad_driving(pad)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_HOP));
    CHECK(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
    SDL_UpdateGamepads();
    CHECK(portable_sdl_gamepad_driving(pad)==
        (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_HOP|PORTABLE_INPUT_ITEM));
    SDL_CloseGamepad(pad);SDL_CloseJoystick(joystick);CHECK(SDL_DetachVirtualJoystick(id));SDL_Quit();
    puts("SDL input: PASS remapped driving, fixed menu controls, restored defaults");return 0;
}
