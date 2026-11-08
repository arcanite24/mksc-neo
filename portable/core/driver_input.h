#ifndef MKSC_PORTABLE_DRIVER_INPUT_H
#define MKSC_PORTABLE_DRIVER_INPUT_H

#include <stdint.h>

enum
{
    PORTABLE_INPUT_NONE = 0,
    PORTABLE_INPUT_LEFT = 1u << 0,
    PORTABLE_INPUT_RIGHT = 1u << 1,
    PORTABLE_INPUT_UP = 1u << 2,
    PORTABLE_INPUT_DOWN = 1u << 3,
    PORTABLE_INPUT_ACCELERATE = 1u << 4,
    PORTABLE_INPUT_BRAKE = 1u << 5,
    PORTABLE_INPUT_HOP = 1u << 6,
    PORTABLE_INPUT_ITEM = 1u << 7,
};

enum
{
    PORTABLE_DRIVER_INPUT_OK = 0,
    PORTABLE_DRIVER_INPUT_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_INPUT_UNSUPPORTED_FLAGS = 2,
};

/*
 * This is the isolated portion of the original driver record used by
 * sub_8037FD8. The offsets in the comments are original-record offsets; the
 * native object contains no original pointer or padding contract.
 */
typedef struct
{
    int32_t base_speed_q16;       /* original +0x8c */
    uint16_t speed_parameter;     /* original +0x178 */
    uint32_t state_flags;         /* original +0x48; accepted branch is zero */
    uint16_t control_word;        /* original +0x96 */
    int32_t speed_target_q16;     /* original +0xa8 */
} PortableDriverInputState;

/*
 * Apply the source-width control word and compute the source's bounded speed
 * target. Non-zero original state flags select side-effectful branches that
 * are intentionally outside this native seam.
 */
int portable_driver_input_tick(PortableDriverInputState *state,
                               uint16_t control_word);

/* Map the proven primary GBA key word from play.c to the driver word. */
uint16_t portable_driver_control_word_from_gba_keys(uint16_t raw_keys);

/* Map the bounded native semantic buttons to the same source control word. */
uint16_t portable_driver_control_word_from_semantic_buttons(uint16_t buttons);

#endif
