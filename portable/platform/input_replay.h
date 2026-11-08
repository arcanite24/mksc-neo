#ifndef MKSC_PORTABLE_INPUT_REPLAY_H
#define MKSC_PORTABLE_INPUT_REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "driver_input.h"

typedef struct
{
    uint16_t buttons;
    /* Optional source-mapped word consumed by the native driver seam. */
    uint16_t source_control_word;
} PortableInputFrame;

typedef struct
{
    const PortableInputFrame *frames;
    size_t count;
    size_t cursor;
} PortableInputReplay;

void portable_input_replay_init(PortableInputReplay *replay,
                                const PortableInputFrame *frames,
                                size_t count);
int portable_input_replay_next(PortableInputReplay *replay,
                               PortableInputFrame *out);

#endif
