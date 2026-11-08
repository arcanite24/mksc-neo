#include "driver_angle_target.h"

#include <stddef.h>
#include <stdint.h>

int portable_driver_angle_target_update(
    uint16_t current_angle_56,
    uint16_t target_angle_7a,
    uint16_t source_state_b4,
    PortableDriverAngleTargetResult *out)
{
    int32_t distance;

    if (out == NULL)
        return PORTABLE_DRIVER_ANGLE_TARGET_INVALID_ARGUMENT;

    *out = (PortableDriverAngleTargetResult){
        .current_angle_56 = current_angle_56,
        .target_angle_7a = target_angle_7a,
        .source_state_b4 = source_state_b4,
        .difference = (int16_t)(uint16_t)(current_angle_56 - target_angle_7a),
        .next_current_angle_56 = current_angle_56,
        .next_target_angle_7a = target_angle_7a,
    };

    if ((source_state_b4 & UINT16_C(0x0a00)) != 0)
    {
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_STATE_CALLBACK;
        return PORTABLE_DRIVER_ANGLE_TARGET_UNSUPPORTED_STATE_CALLBACK;
    }

    if (out->difference == 0)
    {
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_EQUAL_CALLBACK;
        return PORTABLE_DRIVER_ANGLE_TARGET_UNSUPPORTED_EQUAL_CALLBACK;
    }

    distance = out->difference;
    if (distance < 0)
        distance = -distance;

    if (distance <= INT32_C(0x7ff))
    {
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_SNAP_CURRENT;
        out->next_target_angle_7a = current_angle_56;
    }
    else
    {
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_RETARGET_FAR;
        out->next_target_angle_7a =
            (uint16_t)(target_angle_7a + (out->difference>0 ? UINT16_C(0x0600) : UINT16_C(0xfa00)));
    }
    return PORTABLE_DRIVER_ANGLE_TARGET_OK;
}
