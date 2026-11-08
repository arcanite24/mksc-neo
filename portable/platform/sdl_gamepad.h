#ifndef PORTABLE_SDL_GAMEPAD_H
#define PORTABLE_SDL_GAMEPAD_H

#include <SDL3/SDL.h>
#include <stdint.h>
#include "settings.h"
#include "race_rumble.h"

SDL_Gamepad *portable_sdl_gamepad_open_first(void);
void portable_sdl_input_configure(const PortableSettings *settings);
uint16_t portable_sdl_keyboard_driving(void);
uint16_t portable_sdl_keyboard2_driving(void); /* Player 2's layout. */
int portable_sdl_gamepad_set_deadzone(uint16_t deadzone);
SDL_JoystickID portable_sdl_gamepad_id(SDL_Gamepad *gamepad);
/* Each owner has one SDL reference; disconnected devices are replaced on add. */
void portable_sdl_gamepad_device_event(SDL_Gamepad **gamepad,const SDL_Event *event);
uint16_t portable_sdl_gamepad_driving(SDL_Gamepad *gamepad);
uint16_t portable_sdl_gamepad_frontend(SDL_Gamepad *gamepad);
uint16_t portable_sdl_gamepad_frontend_button(Uint8 button);
/* Optional rumble from one local driver's immutable tick view. The tracker
 * always steps; pulses play only while racing on a connected pad. */
void portable_sdl_gamepad_race_rumble(SDL_Gamepad *gamepad,PortableRumbleTracker *tracker,
    const PortableDriverEngineSoundInput *engine,const PortableDriverContactAudio *contact,
    const PortableDriverContactAudio *boost_voice,int racing);
/* Left stick for the input display; zero without a pad. */
void portable_sdl_gamepad_stick(SDL_Gamepad *gamepad,int16_t *x,int16_t *y);

#endif
