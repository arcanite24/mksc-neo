#include "driver_mushroom.h"

#include <stddef.h>

int portable_driver_mushroom_begin(uint8_t *driver_mode,
                                   uint16_t *timer,
                                   uint16_t *response_flags,const PortableMushroomRules *rules)
{
    if(!portable_mushroom_rules_valid(rules))return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (driver_mode == NULL || timer == NULL || response_flags == NULL)
        return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    *driver_mode = 8u;
    *timer = rules->boost_ticks;
    *response_flags |= UINT16_C(0x0080);
    return PORTABLE_DRIVER_MUSHROOM_OK;
}

int portable_driver_mushroom_consume(uint8_t ground_mode,
    uint16_t *request_flags, uint8_t *driver_mode, uint16_t *timer,
    uint16_t *response_flags, uint8_t *response_status, int16_t *impact_angle,const PortableMushroomRules *rules)
{
    if(!portable_mushroom_rules_valid(rules))return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (!request_flags || !driver_mode || !timer || !response_flags ||
        !response_status || !impact_angle)
        return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (ground_mode != 0u || (*request_flags & UINT16_C(0x8000)) == 0u ||
        (*response_flags & UINT16_C(0x1a00)) != 0u || *response_status >= 16u ||
        ((UINT16_C(0xde1f) >> *response_status) & 1u) == 0u)
        return PORTABLE_DRIVER_MUSHROOM_OK;
    portable_driver_mushroom_begin(driver_mode, timer, response_flags,rules);
    *request_flags &= UINT16_C(0x7fff);
    *response_status = 14u;
    *impact_angle = 0;
    if ((*response_flags & 1u) != 0u)
        *response_flags &= UINT16_C(0xffde);
    return PORTABLE_DRIVER_MUSHROOM_OK;
}

int portable_driver_start_boost_consume(uint8_t ground_mode,
    uint16_t *request_flags, uint8_t *driver_mode, uint16_t *timer,
    uint16_t *response_flags, uint8_t *started)
{
    if (!request_flags || !driver_mode || !timer || !response_flags || !started)
        return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    *started = 0u;
    /* A9AC tests B4 0x200/0x800/0x1000 and B2 0x8000/0x2000 first. */
    if (ground_mode != 0u || (*request_flags & UINT16_C(0xa001)) != UINT16_C(1) ||
        (*response_flags & UINT16_C(0x1a00)) != 0u)
        return PORTABLE_DRIVER_MUSHROOM_OK;
    *request_flags = 0u;
    *timer = UINT16_C(0x0018);
    *response_flags |= UINT16_C(0x0080);
    *driver_mode = 8u;
    if ((*response_flags & UINT16_C(1)) != 0u)
        *response_flags &= UINT16_C(0xffde);
    *started = 1u;
    return PORTABLE_DRIVER_MUSHROOM_OK;
}

int portable_driver_surface_boost(uint8_t ground_mode, uint8_t material,
                                   uint8_t *driver_mode, uint16_t *timer,
                                   uint16_t *response_flags,
                                   uint8_t *response_status, uint8_t *started)
{
    if (driver_mode == NULL || timer == NULL || response_flags == NULL ||
        response_status == NULL)
        return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (started != NULL) *started = 0u;
    /* Source A2B4 uses the same mode-8 consumer as a mushroom, but has
     * its own duration and _080EC280 status gate (ROM words 0 or -1). */
    if (ground_mode != 0u || material > UINT8_C(0x1f) ||
        (material & UINT8_C(0x0f)) != 6u || *response_status >= 16u ||
        ((UINT16_C(0xde1f) >> *response_status) & 1u) == 0u)
        return PORTABLE_DRIVER_MUSHROOM_OK;
    if (started != NULL) *started = 1u;
    *timer = UINT16_C(0x0022);
    *response_flags |= UINT16_C(0x0080);
    *driver_mode = 8u;
    *response_status = 14u;
    if ((*response_flags & UINT16_C(1)) != 0u)
        *response_flags &= UINT16_C(0xffde);
    return PORTABLE_DRIVER_MUSHROOM_OK;
}

int portable_driver_mushroom_tick(uint8_t *driver_mode,
                                  uint16_t *timer,
                                  uint16_t *response_flags,
                                  int32_t *speed_q16,
                                  int32_t *acceleration_q16,const PortableMushroomRules *rules)
{
    if(!portable_mushroom_rules_valid(rules))return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (driver_mode == NULL || timer == NULL || response_flags == NULL ||
        speed_q16 == NULL || acceleration_q16 == NULL)
        return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (*driver_mode != 8u)
        return PORTABLE_DRIVER_MUSHROOM_OK;
    /* 384F4 decrements the halfword even from zero. */
    *timer=(uint16_t)(*timer-1u);
    if (*timer == 0u)
    {
        *response_flags &= UINT16_C(0xff3f);
        *driver_mode = 0u;
        return PORTABLE_DRIVER_MUSHROOM_OK;
    }
    /* 38504 narrows C0 before both branches; at the cap only the
     * fractional low half survives, ahead of the next integration turn. */
    *acceleration_q16=(int32_t)(uint16_t)*acceleration_q16;
    if (*speed_q16 < (int32_t)rules->max_speed_q16)
        *acceleration_q16 = (int32_t)(
            ((uint32_t)*acceleration_q16 & UINT32_C(0x0000ffff)) |
            ((uint32_t)rules->acceleration_units<<16u));
    else
        *speed_q16 = (int32_t)rules->max_speed_q16;
    return PORTABLE_DRIVER_MUSHROOM_OK;
}

int portable_driver_charged_boost_tick(uint8_t *driver_mode, uint16_t *timer,
    uint16_t *response_flags, int32_t speed_target_q16,
    int32_t *speed_q16, int32_t *acceleration_q16)
{
    if (driver_mode == NULL || timer == NULL || response_flags == NULL ||
        speed_q16 == NULL || acceleration_q16 == NULL)
        return PORTABLE_DRIVER_MUSHROOM_INVALID_ARGUMENT;
    if (*driver_mode != 12u)
        return PORTABLE_DRIVER_MUSHROOM_OK;
    /* 385D8 decrements the halfword even from zero. Expiry retains C0. */
    *timer = (uint16_t)(*timer - 1u);
    if (*timer == 0u)
    {
        *response_flags &= UINT16_C(0xff3f);
        *driver_mode = 0u;
    }
    else if (*speed_q16 < (int32_t)((uint32_t)speed_target_q16 + UINT32_C(0x00e00000)))
        *acceleration_q16 = (int32_t)(
            ((uint32_t)*acceleration_q16 & UINT32_C(0xffff)) | UINT32_C(0x00030000));
    else
    {
        *speed_q16 = (int32_t)((uint32_t)speed_target_q16 + UINT32_C(0x00c00000));
        *acceleration_q16 = (uint16_t)*acceleration_q16;
    }
    return PORTABLE_DRIVER_MUSHROOM_OK;
}
