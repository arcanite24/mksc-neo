#ifndef PORTABLE_TRACK_SET_H
#define PORTABLE_TRACK_SET_H

#include <stdint.h>

/* Course catalog kind37 uses content ID set+1. Menus address a cup as one
 * course_cup byte: GBA 0..4, Extra 5..9, the arena group 10 and custom
 * 11..15. Course art slots are course_cup*4+track (custom 44..63). */
typedef enum
{
    PORTABLE_TRACK_SET_GBA = 0,
    PORTABLE_TRACK_SET_EXTRA,
    PORTABLE_TRACK_SET_ARENA,
    PORTABLE_TRACK_SET_CUSTOM,
    PORTABLE_TRACK_SET_COUNT
} PortableTrackSet;

#define PORTABLE_TRACK_SET_COURSE_CUPS 16u

static inline uint8_t portable_track_set_first_cup(PortableTrackSet set)
{
    return set == PORTABLE_TRACK_SET_GBA ? 0u : set == PORTABLE_TRACK_SET_EXTRA ? 5u
         : set == PORTABLE_TRACK_SET_ARENA ? 10u : 11u;
}

/* PORTABLE_TRACK_SET_COUNT for a course_cup outside every set. */
static inline PortableTrackSet portable_track_set_of_cup(uint8_t course_cup)
{
    return course_cup < 5u ? PORTABLE_TRACK_SET_GBA
         : course_cup < 10u ? PORTABLE_TRACK_SET_EXTRA
         : course_cup == 10u ? PORTABLE_TRACK_SET_ARENA
         : course_cup < PORTABLE_TRACK_SET_COURSE_CUPS ? PORTABLE_TRACK_SET_CUSTOM
         : PORTABLE_TRACK_SET_COUNT;
}

static inline uint8_t portable_track_set_cup_count(PortableTrackSet set)
{
    return set == PORTABLE_TRACK_SET_ARENA ? 1u : 5u;
}

/* Catalog v2 source-header keys. Stock sets index the ROM track table;
 * custom rows carry any byte key that is unique within its cup. */
static inline uint32_t portable_track_set_header_limit(PortableTrackSet set)
{
    return set == PORTABLE_TRACK_SET_GBA ? 44u : set == PORTABLE_TRACK_SET_EXTRA ? 52u
         : set == PORTABLE_TRACK_SET_ARENA ? 48u : 256u;
}

#endif
