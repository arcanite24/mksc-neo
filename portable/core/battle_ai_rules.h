#ifndef PORTABLE_BATTLE_AI_RULES_H
#define PORTABLE_BATTLE_AI_RULES_H

#include <stdint.h>
#include <stddef.h>

/* Native Battle CPU policy. The original Battle participants were human;
 * navigation supplies goals to the recovered BB48/BD24 driver, never poses. */
typedef struct {
    uint16_t cruise_speed,turn_speed;
    uint16_t turn_angle,stop_angle;
    uint16_t clearance,lookahead;
} PortableBattleAiRules;

static inline int portable_battle_ai_rules_valid(const PortableBattleAiRules *r)
{
    return r!=NULL && r->cruise_speed>0u && r->cruise_speed<=2016u &&
        r->turn_speed>0u && r->turn_speed<=r->cruise_speed &&
        r->turn_angle>0u && r->turn_angle<r->stop_angle && r->stop_angle<=32768u &&
        r->clearance>0u && r->clearance<8u && r->lookahead>=16u && r->lookahead<=256u;
}
/* Intent only; shared item callbacks retain their original timing/effects. */
typedef struct {
    uint16_t range,aim_angle,interval;
} PortableBattleCombatRules;

static inline int portable_battle_combat_rules_valid(const PortableBattleCombatRules *r)
{
    return r!=NULL && r->range>=16u && r->range<=256u &&
        r->aim_angle>0u && r->aim_angle<=16384u && r->interval>0u && r->interval<=600u;
}
#endif
