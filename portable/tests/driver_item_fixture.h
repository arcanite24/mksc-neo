#ifndef PORTABLE_TEST_DRIVER_ITEM_FIXTURE_H
#define PORTABLE_TEST_DRIVER_ITEM_FIXTURE_H
#include "../core/driver_pilot.h"
#include "item_rules_fixture.h"

static inline void test_item_dependencies(PortableDriverPilotDependencies *d)
{
    d->mushroom_rules=test_mushroom_rules();d->item_status_rules=test_item_status_rules();
    d->boo_rules=test_boo_rules();d->green_shell_rules=test_green_shell_rules();
    d->red_shell_rules=test_red_shell_rules();d->banana_rules=test_banana_rules();
    d->shell_orbit_rules=test_shell_orbit_rules();
}
#endif
