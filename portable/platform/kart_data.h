#ifndef PORTABLE_KART_DATA_H
#define PORTABLE_KART_DATA_H
#include "json_reader.h"
#include "mod_manifest.h"

#define PORTABLE_KART_DATA_LIMIT 16u
/* Optional fields hold absolute values in the original motion table units. */
typedef struct {
    uint32_t base_speed_q16;
    int16_t terrain_limits[6];
    uint8_t cpu_acceleration[128],cpu_target_speeds[160],cpu_heading_steps[40];
    uint8_t id,present;
} PortableKartEngineOverride;
typedef struct {
    uint32_t id;
    uint16_t present;
    int16_t auto_acceleration[64],brake[8],brake_accelerate[8],forward_response[8];
    int16_t local_acceleration[256],steering_delta[10],steering_response[16],forward_target_response[8];
    PortableKartEngineOverride time_trial,engines[3];
    uint8_t engine_count;
} PortableKartPhysicsOverride;
typedef struct {
    PortableKartPhysicsOverride records[PORTABLE_KART_DATA_LIMIT];
    uint8_t count;
} PortableKartPhysicsData;

int portable_kart_data_parse(PortableJsonReader *reader,PortableKartPhysicsData *out,char error[PORTABLE_MOD_ERROR_CAPACITY]);
const PortableKartPhysicsOverride *portable_kart_data_find(const PortableKartPhysicsData *data,uint32_t id);
/* Caller has validated and cloned a motion v6 section before applying. */
void portable_kart_data_apply(const PortableKartPhysicsOverride *record,uint8_t *motion);
#endif
