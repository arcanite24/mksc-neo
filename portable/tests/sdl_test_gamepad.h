#ifndef PORTABLE_SDL_TEST_GAMEPAD_H
#define PORTABLE_SDL_TEST_GAMEPAD_H
#include <SDL3/SDL.h>

/* Keep a developer's attached controllers from stealing the test's first slot.
 * This hint affects only this process, not other applications or the device. */
static inline int test_gamepad_isolate(void)
{
    return SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
        "0x1209/0x4d4b",SDL_HINT_OVERRIDE);
}

static inline SDL_Joystick *test_gamepad_attach(SDL_JoystickID *id)
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id=0x1209;desc.product_id=0x4d4b;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1u;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1u;
    desc.name="MKSC test gamepad";
    *id=SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick *joystick=*id!=0u?SDL_OpenJoystick(*id):NULL;
    if(joystick!=NULL){
        SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,INT16_MIN);
        SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,INT16_MIN);
        SDL_UpdateJoysticks();
    }
    return joystick;
}
#endif
