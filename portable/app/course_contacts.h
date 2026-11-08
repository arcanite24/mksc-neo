#ifndef PORTABLE_COURSE_CONTACTS_H
#define PORTABLE_COURSE_CONTACTS_H
#include "driver_pilot.h"
#include "course_impact_sound.h"
#include "lava_bubble.h"
#include "thwomp.h"
#include "crab.h"
#include "seagull.h"
#include "cannonball.h"
#include "piranha.h"
#include "snowman.h"
#include "mouse.h"
#include "thundercloud.h"
#include "falling_star.h"
#include "penguin.h"
#include "boo.h"
#include "collision.h"

typedef struct {
    uint16_t contacts,strong_hits,flattened_hits,coin_loss;
    uint8_t coin_loss_causes[16], coin_loss_count;
    /* C12 bit0: a randomized actor was touched, so the TT ghost is unusable. */
    uint16_t ghost_invalidations;
    uint8_t boo_haunts; /* 59FAC requests for 4886C on this driver. */
    uint8_t burns; /* D214(3) lava/rock scripts started on this driver. */
} PortableCourseContactEvents;
/* Borrowed live simulation owners. The source collision pass precedes motion. */
typedef struct {
    PortableDriverPilotState *driver;
    const PortableDriverPilotDependencies *dependencies;
    PortableCourseProps *course_props;
    PortableLavaBubbleCourse *lava_bubbles;
    PortableThwompCourse *thwomps;
    PortableCrabCourse *crabs;
    PortableSeagullCourse *seagulls;
    PortableCannonballCourse *cannonballs;
    PortablePiranhaCourse *piranhas;
    PortableSnowmanCourse *snowmen;
    PortableMouseCourse *mice;
    PortableThundercloudCourse *thunderclouds;
    PortableFallingStarCourse *falling_stars;
    PortablePenguinCourse *penguins;
    PortableBooCourse *boos;
    PortablePiranhaSounds *piranha_sounds;
    PortableCourseImpactSounds *impact_sounds;
    int32_t listener_x,listener_y;
    uint32_t listener_flags;
    uint8_t driver_class;
    uint32_t scene_clock,course_id;
    const char *mode_name;
    uint16_t checkpoint_count,collision_guard;
    uint8_t is_player,player_rank_ahead;
    uint8_t kart_index; /* Roster slot a teepee's Shy Guy rides. */
    PortableCourseContactEvents events;
} PortableCourseContacts;
/* Drivers share passive owners; the native checkpoint sorter determines
 * callback order. Kart/kart responses remain owned by the GP collision pass. */
int portable_course_contacts_collide_many(PortableCourseContacts *worlds,size_t driver_count);
int portable_course_contacts_collide(PortableCourseContacts *world);
#endif
