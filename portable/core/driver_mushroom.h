#ifndef MKSC_PORTABLE_DRIVER_MUSHROOM_H
#define MKSC_PORTABLE_DRIVER_MUSHROOM_H

#include <stdint.h>
#include "mushroom_rules.h"

enum
{
    PORTABLE_DRIVER_MUSHROOM_OK = 0,
    PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT = 1,
};

/* Start A9AC's mode-8 mushroom response. The fields map to
 * driver +0x84, +0xcc and +0xb4 respectively. */
int portable_driver_mushroom_begin(uint8_t *driver_mode,
                                   uint16_t *timer,
                                   uint16_t *response_flags,const PortableMushroomRules *rules);

/* A9AC consumes B2's queued item boost only on eligible ground responses.
 * Damage and airborne states retain the request for a later update. */
int portable_driver_mushroom_consume(uint8_t ground_mode,
    uint16_t *request_flags, uint8_t *driver_mode, uint16_t *timer,
    uint16_t *response_flags, uint8_t *response_status, int16_t *impact_angle,const PortableMushroomRules *rules);

/* B2 bit 0 is 40DD8's start-boost request: held A with launch revs in
 * 0x1c0..0x3bf on the final countdown turn. A9AC consumes it after the
 * mushroom request with a 24-tick mode-8 boost, clears every B2 request and
 * cancels an over-rev launch. started reports an accepted request. */
int portable_driver_start_boost_consume(uint8_t ground_mode,
    uint16_t *request_flags, uint8_t *driver_mode, uint16_t *timer,
    uint16_t *response_flags, uint8_t *started);

/* Ground material nibble 6 starts/refreshes A2B4's 34-tick boost when
 * _080EC280 permits the current response. Airborne material is inert. Optional started reports each
 * accepted refresh, even if the boost was already active. */
int portable_driver_surface_boost(uint8_t ground_mode, uint8_t material,
                                   uint8_t *driver_mode, uint16_t *timer,
                                   uint16_t *response_flags,
                                   uint8_t *response_status, uint8_t *started);

/* Advance one source mode-8 update over +0xbc/+0xc0. */
int portable_driver_mushroom_tick(uint8_t *driver_mode,
                                  uint16_t *timer,
                                  uint16_t *response_flags,
                                  int32_t *speed_q16,
                                  int32_t *acceleration_q16,const PortableMushroomRules *rules);

/* 8298 mode12: charged-release boost relative to the current A8 target. */
int portable_driver_charged_boost_tick(uint8_t *driver_mode, uint16_t *timer,
    uint16_t *response_flags, int32_t speed_target_q16,
    int32_t *speed_q16, int32_t *acceleration_q16);

#endif
