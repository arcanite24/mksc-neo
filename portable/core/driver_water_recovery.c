#include "driver_water_recovery.h"

static void clear_components(PortableDriverPilotState *driver)
{
    driver->motion.component_x = 0;
    driver->motion.component_y = 0;
    driver->pose.component_x = 0;
    driver->pose.component_y = 0;
}

static int tick_steering(PortableDriverPilotState *driver,
                         const PortableDriverPilotDependencies *dependencies,
                         uint8_t source_mode)
{
    PortableDriverSteeringState *steering = &driver->steering;
    size_t response_count;

    if (dependencies == NULL || dependencies->steering_delta_table == NULL ||
        dependencies->steering_delta_count < 10u ||
        dependencies->steering_response_data == NULL ||
        dependencies->steering_response_data_count < 4u)
        return 0;
    response_count = dependencies->steering_response_data_count;
    if (response_count >= 16u)
        response_count = 4u;
    steering->speed_q16 = driver->motion.speed_q16;
    steering->mode = source_mode;
    steering->response_slot = 0u;
    if (portable_driver_steering_response_tick(
            steering, dependencies->steering_response_data,
            response_count) != PORTABLE_DRIVER_STEERING_OK ||
        portable_driver_steering_tick(
            steering, dependencies->steering_delta_table,
            dependencies->steering_delta_count) !=
            PORTABLE_DRIVER_STEERING_OK)
        return 0;
    driver->motion.angle = steering->angle;
    driver->forward_acceleration.response_q8 = steering->response_q8;
    return 1;
}

static int tick_heading(PortableWaterRecoveryState *recovery,
                        PortableDriverPilotState *driver,
                        const PortableDriverPilotDependencies *dependencies,
                        uint16_t *heading)
{
    if (recovery->phase == PORTABLE_WATER_RECOVERY_SINK)
    {
        if (recovery->phase_ticks == 18u)
        {
            driver->motion.speed_q16 = 0;
            driver->motion.acceleration_q16 = INT32_C(0x00010000);
            driver->steering.response_q8 = 0;
            driver->steering.response_status = 0u;
            driver->steering.response_flags = 0u;
            driver->steering.position_82 = 0u;
            driver->steering.base_angle = 0;
            driver->forward_acceleration.response_q8 = 0;
        }
        if (recovery->phase_ticks < 18u)
        {
            if (!tick_steering(driver, dependencies, 1u))
                return 0;
        }
        else
        {
            const uint16_t mode_tick =
                (uint16_t)(recovery->phase_ticks - 18u);
            if (mode_tick <= 123u)
                driver->motion.speed_q16 = (int32_t)mode_tick << 16;
            else
                driver->motion.speed_q16 = (mode_tick & 1u) != 0u
                    ? INT32_C(0x007b0000) : INT32_C(0x007c0000);
            driver->motion.acceleration_q16 =
                driver->motion.speed_q16 == INT32_C(0x007c0000)
                    ? -INT32_C(0x00010000) : INT32_C(0x00010000);
            if (!tick_steering(driver, dependencies, 4u))
                return 0;
        }
        *heading = (uint16_t)driver->steering.steering_offset;
    }
    else
    {
        if (recovery->phase == PORTABLE_WATER_RECOVERY_LIFT &&
            recovery->phase_ticks == 0u)
        {
            driver->motion.speed_q16 = 0;
            driver->motion.acceleration_q16 = 0;
            driver->steering.response_q8 = 0;
            driver->forward_acceleration.response_q8 = 0;
        }
        driver->steering.steering_offset = (int16_t)*heading;
        driver->steering.angle = (int16_t)*heading;
        driver->steering.source_angle_56 = *heading;
        driver->motion.angle = (int16_t)*heading;
    }
    return 1;
}

int portable_driver_water_recovery_tick(
    PortableWaterRecoveryState *recovery,
    PortableDriverPilotState *driver,
    const PortableDriverPilotDependencies *dependencies)
{
    uint16_t heading;
    if (recovery == NULL || driver == NULL || dependencies == NULL ||
        recovery->phase == PORTABLE_WATER_RECOVERY_NONE)
        return 0;
    heading = (uint16_t)driver->motion.angle;
    (void)portable_water_recovery_tick(
        recovery, &driver->pose.position_x, &driver->pose.position_y,
        &heading);
    if (!tick_heading(recovery, driver, dependencies, &heading))
        return 0;
    clear_components(driver);
    return 1;
}
