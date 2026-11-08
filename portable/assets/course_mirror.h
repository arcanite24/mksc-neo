#ifndef PORTABLE_COURSE_MIRROR_H
#define PORTABLE_COURSE_MIRROR_H

#include "recovered_pack.h"
#include "presentation_pack.h"

/* Mirror mode reflects a loaded course left-to-right in memory, before any
 * session starts: tile, sector and material maps, route rows and CPU
 * targets, start grid and headings, item boxes, coins and every course actor
 * (including direction-bearing variants). Physics, AI, laps and collisions
 * then run unchanged on the mirrored world. A pixel p maps to W-1-p (W = world
 * width), so every point keeps its mirrored tile; route points, which sit on
 * tile corners, map to W-x. Headings negate (0 is north, 0x4000 east). The
 * compatibility digest changes, so mirror ghosts and records never mix with
 * the original course. Returns 0 for inconsistent map sizes. */
int portable_course_mirror_race(PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race);

/* Presentation follows: road tiles, sky layers, Time Trial minimap, GP course
 * map and scenery positions flip to match a race mirrored above. Call after
 * portable_presentation_pack_open with the mirrored race. */
int portable_course_mirror_presentation(PortablePresentationPackStorage *storage,
    PortablePresentationPackView *view,const PortableRecoveredRaceView *race);

/* open_race / presentation_open, mirrored when mirror is nonzero. */
PortableRecoveredPackStatus portable_course_open_race(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *race,uint8_t mirror);
PortableRecoveredPackStatus portable_course_open_presentation(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,const PortableRecoveredPackStorage *race_storage,
    const PortableRecoveredRaceView *race,PortablePresentationPackStorage *storage,
    PortablePresentationPackView *view,uint8_t mirror);

#endif
