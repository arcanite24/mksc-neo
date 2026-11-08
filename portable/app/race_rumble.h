#ifndef PORTABLE_RACE_RUMBLE_H
#define PORTABLE_RACE_RUMBLE_H

#include <stdint.h>
#include "driver_pilot.h"
#include "driver_engine_sound.h"

/* Optional controller rumble. The tracker reads only the immutable per-tick
 * presentation view of one local driver (the same fields that drive its sound
 * cues) and never feeds the simulation, so races, replays and digests are
 * unchanged whether rumble is on or off. */
typedef enum {
    PORTABLE_RUMBLE_NONE,
    PORTABLE_RUMBLE_BOOST,
    PORTABLE_RUMBLE_LANDING,
    PORTABLE_RUMBLE_BUMP,
    PORTABLE_RUMBLE_SPIN_OUT,
    PORTABLE_RUMBLE_ITEM_HIT,
    PORTABLE_RUMBLE_HAZARD,
    PORTABLE_RUMBLE_EVENT_COUNT
} PortableRumbleEvent;

typedef struct { uint16_t low,high,duration_ms; } PortableRumblePulse;

typedef struct {
    const PortableDriverEngineSoundInput *engine;
    const PortableDriverContactAudio *contact,*boost_voice;
} PortableRumbleView;

typedef struct {
    uint32_t contact_serial,boost_serial;
    int16_t lightning_timer;
    uint8_t response_status,flattened,jumping,primed;
} PortableRumbleTracker;

/* Highest-priority event since the previous step; the first step only primes
 * serials and edges so a race start or scene cut never rumbles. */
PortableRumbleEvent portable_rumble_step(PortableRumbleTracker *tracker,
    const PortableRumbleView *view);
PortableRumblePulse portable_rumble_pulse(PortableRumbleEvent event);

#endif
