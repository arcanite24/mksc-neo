#ifndef PORTABLE_FINISH_LAKITU_H
#define PORTABLE_FINISH_LAKITU_H
#include <stdint.h>
enum { PORTABLE_GP_FINISH_LAKITU_MAX_FRAMES=32u };
typedef struct {
    uint8_t art_bank;
    uint16_t duration;
} PortableGpFinishLakituFrame;
typedef struct {
    uint16_t row_count, entrance_ticks, bob_ticks, exit_ticks;
    PortableGpFinishLakituFrame frames[PORTABLE_GP_FINISH_LAKITU_MAX_FRAMES];
} PortableGpFinishLakituRules;

static inline int portable_gp_finish_lakitu_rules_valid(const PortableGpFinishLakituRules *rules)
{
    if(!rules || !rules->row_count || rules->row_count>PORTABLE_GP_FINISH_LAKITU_MAX_FRAMES ||
       rules->entrance_ticks<20u || !rules->bob_ticks || !rules->exit_ticks)return 0;
    const uint32_t total=(uint32_t)rules->entrance_ticks+rules->bob_ticks+rules->exit_ticks;
    if(total>=UINT16_MAX)return 0;
    uint32_t duration=0u;
    for(unsigned i=0u;i<rules->row_count;++i){
        if(!rules->frames[i].duration || rules->frames[i].art_bank<21u ||
           rules->frames[i].art_bank>28u)return 0;
        duration+=rules->frames[i].duration;
    }
    return duration==total;
}

#endif
