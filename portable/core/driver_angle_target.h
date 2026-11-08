#ifndef MKSC_PORTABLE_DRIVER_ANGLE_TARGET_H
#define MKSC_PORTABLE_DRIVER_ANGLE_TARGET_H

#include <stdint.h>

enum
{
    PORTABLE_DRIVER_ANGLE_TARGET_OK = 0,
    PORTABLE_DRIVER_ANGLE_TARGET_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_ANGLE_TARGET_UNSUPPORTED_STATE_CALLBACK = 2,
    PORTABLE_DRIVER_ANGLE_TARGET_UNSUPPORTED_EQUAL_CALLBACK = 3,
};

enum
{
    PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_STATE_CALLBACK = 1,
    PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_EQUAL_CALLBACK = 2,
    PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_SNAP_CURRENT = 3,
    PORTABLE_DRIVER_ANGLE_TARGET_BRANCH_RETARGET_FAR = 4,
};

typedef struct
{
    uint16_t current_angle_56;
    uint16_t target_angle_7a;
    uint16_t source_state_b4;
    int16_t difference;
    uint16_t next_current_angle_56;
    uint16_t next_target_angle_7a;
    uint8_t branch;
} PortableDriverAngleTargetResult;

/* Reproduce the non-callback branches of sub_803BC00. The source stores
 * current/target angles as halfwords, so subtraction and signed0x600 target steps wrap
 * at 16 bits. BA98 and BB48 remain explicit unsupported callback branches. */
int portable_driver_angle_target_update(
    uint16_t current_angle_56,
    uint16_t target_angle_7a,
    uint16_t source_state_b4,
    PortableDriverAngleTargetResult *out);

#endif
