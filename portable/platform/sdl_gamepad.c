#include "sdl_gamepad.h"
#include "mobile_touch.h"
#include "controller_input.h"
#include "driver_input.h"
#include "frontend_flow.h"

static PortableSettings input_settings;
static int input_configured;
void portable_sdl_input_configure(const PortableSettings *settings)
{
    if(settings!=NULL)input_settings=*settings;
    else portable_settings_defaults(&input_settings);
    input_configured=1;
    portable_mobile_touch_configure(input_settings.touch_controls);
}
static void input_defaults(void)
{
    if(!input_configured)portable_sdl_input_configure(NULL);
}
uint16_t portable_sdl_keyboard_driving(void)
{
    input_defaults();
    return portable_keyboard_driving_buttons(SDL_GetKeyboardState(NULL),SDL_SCANCODE_COUNT,&input_settings) |
        portable_mobile_touch_driving();
}

uint16_t portable_sdl_keyboard2_driving(void)
{
    input_defaults();
    return portable_keyboard2_driving_buttons(SDL_GetKeyboardState(NULL),SDL_SCANCODE_COUNT,&input_settings);
}

int portable_sdl_gamepad_set_deadzone(uint16_t deadzone)
{
    if(deadzone<1u||deadzone>30000u)return 0;
    input_defaults();input_settings.steering_deadzone=deadzone;return 1;
}

SDL_Gamepad *portable_sdl_gamepad_open_first(void)
{
    int count=0;
    SDL_JoystickID *ids=SDL_GetGamepads(&count);
    SDL_Gamepad *gamepad=NULL;
    for(int index=0;index<count&&gamepad==NULL;++index)
        gamepad=SDL_OpenGamepad(ids[index]);
    SDL_free(ids);return gamepad;
}

SDL_JoystickID portable_sdl_gamepad_id(SDL_Gamepad *gamepad)
{
    return gamepad!=NULL?SDL_GetGamepadID(gamepad):0;
}

void portable_sdl_gamepad_device_event(SDL_Gamepad **gamepad,const SDL_Event *event)
{
    if(gamepad==NULL||event==NULL)return;
    if(event->type==SDL_EVENT_GAMEPAD_REMOVED&&
       event->gdevice.which==portable_sdl_gamepad_id(*gamepad)){
        SDL_CloseGamepad(*gamepad);*gamepad=NULL;
    }
    if(event->type==SDL_EVENT_GAMEPAD_ADDED&&*gamepad==NULL)
        *gamepad=SDL_OpenGamepad(event->gdevice.which);
}

uint16_t portable_sdl_gamepad_driving(SDL_Gamepad *gamepad)
{
    if(gamepad==NULL||!SDL_GamepadConnected(gamepad))return 0u;
    input_defaults();
    const PortableControllerSample sample={
        SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)input_settings.pad_buttons[0]),
        SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)input_settings.pad_buttons[1]),
        SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_LEFT),
        SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
        SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX),
        SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
        SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)};
    uint16_t buttons=portable_controller_driving_buttons_deadzone(&sample,input_settings.steering_deadzone);
    if(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_UP))buttons|=PORTABLE_INPUT_UP;
    if(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_DOWN))buttons|=PORTABLE_INPUT_DOWN;
    if(SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)input_settings.pad_buttons[2]))buttons|=PORTABLE_INPUT_HOP;
    if(SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)input_settings.pad_buttons[3]))buttons|=PORTABLE_INPUT_ITEM;
    return buttons;
}

uint16_t portable_sdl_gamepad_frontend_button(Uint8 button)
{
    switch(button){
    case SDL_GAMEPAD_BUTTON_DPAD_UP:return PORTABLE_FRONTEND_BUTTON_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:return PORTABLE_FRONTEND_BUTTON_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:return PORTABLE_FRONTEND_BUTTON_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:return PORTABLE_FRONTEND_BUTTON_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH:return PORTABLE_FRONTEND_BUTTON_A;
    case SDL_GAMEPAD_BUTTON_EAST:return PORTABLE_FRONTEND_BUTTON_B;
    case SDL_GAMEPAD_BUTTON_START:return PORTABLE_FRONTEND_BUTTON_START;
    case SDL_GAMEPAD_BUTTON_BACK:return PORTABLE_FRONTEND_BUTTON_SELECT;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return PORTABLE_FRONTEND_BUTTON_L;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return PORTABLE_FRONTEND_BUTTON_R;
    default:return 0u;
    }
}

uint16_t portable_sdl_gamepad_frontend(SDL_Gamepad *gamepad)
{
    if(gamepad==NULL||!SDL_GamepadConnected(gamepad))return 0u;
    uint16_t buttons=0u;
    for(Uint8 button=0u;button<SDL_GAMEPAD_BUTTON_COUNT;++button)
        if(SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)button))
            buttons|=portable_sdl_gamepad_frontend_button(button);
    return buttons;
}

void portable_sdl_gamepad_race_rumble(SDL_Gamepad *gamepad,PortableRumbleTracker *tracker,
    const PortableDriverEngineSoundInput *engine,const PortableDriverContactAudio *contact,
    const PortableDriverContactAudio *boost_voice,int racing)
{
    const PortableRumbleView view={engine,contact,boost_voice};
    const PortableRumbleEvent event=portable_rumble_step(tracker,&view);
    if(event==PORTABLE_RUMBLE_NONE||!racing||gamepad==NULL||!SDL_GamepadConnected(gamepad))return;
    const PortableRumblePulse pulse=portable_rumble_pulse(event);
    (void)SDL_RumbleGamepad(gamepad,pulse.low,pulse.high,pulse.duration_ms);
}

void portable_sdl_gamepad_stick(SDL_Gamepad *gamepad,int16_t *x,int16_t *y)
{
    *x=*y=0;
    if(gamepad==NULL||!SDL_GamepadConnected(gamepad))return;
    *x=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX);
    *y=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTY);
}
