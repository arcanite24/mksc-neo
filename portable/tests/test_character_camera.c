#include "character_camera.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "character camera failed line %d: %s\n", \
            __LINE__, #condition); exit(1); } } while (0)

int main(void)
{
    PortableCharacterCameraState state;
    PortableCharacterCameraInput input = {
        .driver_heading_7c = UINT16_C(0x4dfb),
        .response_status_7e = 1u,
        .steering_position_82 = UINT16_C(0xe1d0),
        .control_word_96 = UINT16_C(0x8100),
    };

    portable_character_camera_init(&state, UINT16_C(0x4cc9));
    state.current_adjustment = UINT16_C(0x01c8);
    state.adjustment_step = 48;
    CHECK(portable_character_camera_tick(&state, &input));
    CHECK(state.current_yaw == UINT16_C(0x4dfb));
    CHECK(state.current_adjustment == UINT16_C(0x01f8));
    CHECK(state.adjustment_step == 48);
    CHECK(portable_character_camera_yaw(&state) == UINT16_C(0x4ff3));

    state = (PortableCharacterCameraState){
        UINT16_C(0x578b), UINT16_C(0x0378), 48};
    input.driver_heading_7c = UINT16_C(0x58bd);
    input.response_status_7e = UINT8_C(0x0b);
    CHECK(portable_character_camera_tick(&state, &input));
    CHECK(state.current_yaw == UINT16_C(0x58bd));
    CHECK(state.current_adjustment == UINT16_C(0x0348));
    CHECK(portable_character_camera_yaw(&state) == UINT16_C(0x5c05));

    portable_character_camera_init(&state, 0u);
    input = (PortableCharacterCameraInput){
        .driver_heading_7c = 0u,
        .response_status_7e = 1u,
        .steering_position_82 = UINT16_C(0xe320),
        .control_word_96 = UINT16_C(0x8200),
    };
    CHECK(portable_character_camera_tick(&state, &input));
    CHECK(state.current_adjustment == UINT16_C(0xfff8));
    CHECK(state.adjustment_step == 8);
    CHECK(portable_character_camera_target_adjustment(&input) ==
          UINT16_C(0xfa00));
    CHECK(!portable_character_camera_tick(NULL, &input));
    CHECK(!portable_character_camera_tick(&state, NULL));

    puts("portable character camera: PASS");
    return 0;
}
