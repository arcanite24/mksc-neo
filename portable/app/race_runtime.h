#ifndef PORTABLE_RACE_RUNTIME_H
#define PORTABLE_RACE_RUNTIME_H

#include <stdint.h>

#include "driver_pilot.h"
#include "race_session.h"

typedef enum
{
    PORTABLE_HANDLING_DEMO_V1 = 1,
    PORTABLE_HANDLING_RECOVERED_V1 = 2
} PortableHandlingProfile;

typedef enum
{
    PORTABLE_RACE_RUNTIME_OK = 0,
    PORTABLE_RACE_RUNTIME_INVALID_ARGUMENT,
    PORTABLE_RACE_RUNTIME_INCOMPATIBLE_PROFILE,
    PORTABLE_RACE_RUNTIME_SESSION_ERROR,
    PORTABLE_RACE_RUNTIME_DRIVER_ERROR,
    PORTABLE_RACE_RUNTIME_MATERIAL_ERROR
} PortableRaceRuntimeStatus;

typedef struct
{
    PortableHandlingProfile profile;
    PortableRaceSession session;
    PortableDriverPilotState *recovered_driver;
    PortableDriverPilotState recovered_initial_state;
    const PortableDriverPilotDependencies *recovered_dependencies;
    PortableCourseProps *course_props;
    const PortablePiranhaCourse *piranhas;
    int last_driver_status;
    int last_material_status;
    uint8_t boundary_exit;
    uint8_t water_entry;
    const uint8_t *source_checkpoint_modes;
    size_t source_checkpoint_mode_count;
    uint8_t source_checkpoint_mode_row;
    PortableDriverFinishedInputs finished_inputs;
    uint8_t last_material_flags;
    uint8_t last_material_flags_valid;
} PortableRaceRuntime;

PortableRaceRuntimeStatus portable_race_runtime_init_recovered(
    PortableRaceRuntime *runtime, const PortableRaceRulesView *rules,
    PortableDriverPilotState *driver,
    const PortableDriverPilotDependencies *dependencies);

PortableRaceRuntimeStatus portable_race_runtime_tick(
    PortableRaceRuntime *runtime, uint16_t source_control_word,
    PortableRaceTickDisposition *disposition);

void portable_race_runtime_restart(PortableRaceRuntime *runtime);

#endif
