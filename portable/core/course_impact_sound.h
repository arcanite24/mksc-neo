#ifndef PORTABLE_COURSE_IMPACT_SOUND_H
#define PORTABLE_COURSE_IMPACT_SOUND_H
#include <stdint.h>
#define PORTABLE_COURSE_IMPACT_CAPACITY 8u
/* FD7C/FEB8/FF80 select ECBC8 independently of the contact sound player. */
typedef struct {
    uint16_t volume;
    uint8_t character, rival, variant;
} PortableCourseImpactSound;
typedef struct {
    PortableCourseImpactSound events[PORTABLE_COURSE_IMPACT_CAPACITY];
    uint32_t serial;
    uint8_t count;
} PortableCourseImpactSounds;
#endif
