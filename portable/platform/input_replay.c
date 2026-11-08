#include "input_replay.h"

void portable_input_replay_init(PortableInputReplay *replay,
                                const PortableInputFrame *frames,
                                size_t count)
{
    if (replay == NULL)
        return;
    replay->frames = frames;
    replay->count = count;
    replay->cursor = 0;
}

int portable_input_replay_next(PortableInputReplay *replay,
                               PortableInputFrame *out)
{
    if (replay == NULL || out == NULL || replay->frames == NULL ||
        replay->cursor >= replay->count)
        return 0;
    *out = replay->frames[replay->cursor++];
    return 1;
}
