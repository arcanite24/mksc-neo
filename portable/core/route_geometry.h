#ifndef PORTABLE_ROUTE_GEOMETRY_H
#define PORTABLE_ROUTE_GEOMETRY_H

#include "race_session.h"

/* Route coordinates must fit signed Q16 world units, as validated by the
 * race session. Positions retain all Q16 fraction bits. Equal distances keep
 * the first segment; a clamped far endpoint belongs to the following one. */
size_t portable_route_nearest_segment(const PortableRacePoint *route, size_t count, int32_t x_q16,
                                      int32_t y_q16);

/* Clockwise bearing from north, floored to 16-bit turns. Integer CORDIC keeps
 * 40 fractional heading bits; its error is below 2^-30 heading units. Axes,
 * diagonals and the prior atan2(+0,-0) zero-vector result are exact. */
uint16_t portable_route_bearing(int32_t delta_x, int32_t delta_y);

#endif
