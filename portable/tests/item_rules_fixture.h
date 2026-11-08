#ifndef PORTABLE_TEST_ITEM_RULES_FIXTURE_H
#define PORTABLE_TEST_ITEM_RULES_FIXTURE_H
#include "../core/item_roulette.h"
#include "../core/mushroom_rules.h"
#include "../core/item_status_rules.h"
#include "../core/boo_rules.h"
#include "../core/green_shell_rules.h"
#include "../core/red_shell_rules.h"
#include "../core/banana_rules.h"
#include "../core/shell_orbit_rules.h"

/* Authored values deliberately differ from the owner's game content. */
static inline PortableGpRouletteRules test_roulette_rules(void)
{
    PortableGpRouletteRules rules={0};
    for(unsigned i=0u;i<10u;++i)rules.preview[i]=(uint16_t)(0x1000u+i);
    for(unsigned i=0u;i<27u;++i)rules.timeline[i]=(uint16_t)(1u+i%3u);
    for(unsigned i=0u;i<8u;++i){
        rules.gap_upper[i]=(uint16_t)(4u+4u*i);
        rules.contact_weight[i][0]=3u+13u*i;
        rules.contact_weight[i][1]=1u+i;
    }
    for(unsigned i=0u;i<14u;++i)rules.lightning_slots[i]=i%3u==0u?-1:(int8_t)(i%4u);
    return rules;
}
static inline const PortableGpRouletteRules *test_roulette_profile(void)
{
    static PortableGpRouletteRules rules;
    rules=test_roulette_rules();
    return &rules;
}

static inline const PortableMushroomRules *test_mushroom_rules(void)
{static const PortableMushroomRules r={9u,7u,13u,17u,0x05000000u};return &r;}
static inline const PortableItemStatusRules *test_item_status_rules(void)
{static const PortableItemStatusRules r={85u,91u,96u};return &r;}
static inline const PortableBooRules *test_boo_rules(void)
{static const PortableBooRules r={72u,79u,12u,1537u,1025u,0x22000u,0x3100u};return &r;}
static inline const PortableGreenShellRules *test_green_shell_rules(void)
{static const PortableGreenShellRules r={800u,101u,7u,6u,13u,31u,0xd100u,0x1b00u};return &r;}
static inline const PortableRedShellRules *test_red_shell_rules(void)
{static const PortableRedShellRules r={801u,117u,7u,37u,13u,31u,109u,117u,127u,5u,17u,47u,135u,15u,0xd100u,0x1b00u};return &r;}
static inline const PortableBananaRules *test_banana_rules(void)
{static const PortableBananaRules r={0x81000u,29u,0x1b000u,0x1900u,7u,13u,31u,0xa100u,0x1900u};return &r;}
static inline const PortableShellOrbitRules *test_shell_orbit_rules(void)
{static const PortableShellOrbitRules r={15u,1281u,7u};return &r;}

#endif
