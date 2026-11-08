#ifndef PORTABLE_ITEM_STATUS_RULES_H
#define PORTABLE_ITEM_STATUS_RULES_H
#include <stdint.h>
/* 485DC Star +6A;483A0 Lightning +68/C82. Signed driver timers stay positive. */
typedef struct {uint16_t star_player_ticks,star_cpu_ticks,lightning_ticks;} PortableItemStatusRules;
static inline int portable_item_status_rules_valid(const PortableItemStatusRules *rules)
{
    return rules!=0&&rules->star_player_ticks!=0u&&rules->star_player_ticks<=32767u&&
        rules->star_cpu_ticks!=0u&&rules->star_cpu_ticks<=32767u&&
        rules->lightning_ticks>=48u&&rules->lightning_ticks<=32767u;
}
/* Preserve the independent48-turn flash at any configured shrink duration. */
static inline uint16_t portable_item_status_flash_end(const PortableItemStatusRules *rules)
{return rules!=0 && rules->lightning_ticks>=48u?(uint16_t)(rules->lightning_ticks-48u):0u;}
#endif
