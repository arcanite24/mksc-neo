#ifndef PORTABLE_DRIVER_WATER_RECOVERY_H
#define PORTABLE_DRIVER_WATER_RECOVERY_H

#include "driver_pilot.h"
#include "water_recovery.h"

/* Advance one source-qualified Lakitu rescue frame, including the driver's
 * mode-1/mode-4 steering and speed mutations around the shared position path. */
int portable_driver_water_recovery_tick(
    PortableWaterRecoveryState *recovery,
    PortableDriverPilotState *driver,
    const PortableDriverPilotDependencies *dependencies);

#endif
