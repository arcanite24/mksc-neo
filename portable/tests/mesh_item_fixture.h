#ifndef PORTABLE_TEST_MESH_ITEM_FIXTURE_H
#define PORTABLE_TEST_MESH_ITEM_FIXTURE_H
#include "mesh_item_rules.h"
#include <string.h>
static inline void test_mir_word(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8u));
}
/* Authored SI fixture, with no imported game data or runtime item behavior. */
static inline void test_mir_payload(uint8_t bytes[PORTABLE_MESH_ITEM_RULES_BYTES])
{
    memset(bytes, 0, PORTABLE_MESH_ITEM_RULES_BYTES);
    const uint32_t header[8] = {UINT32_C(0x3152494d), 1u, 1396u, 41u, 7u, 48u, 8u, 16u};
    for (unsigned i = 0; i < 8u; ++i)
        test_mir_word(bytes + i * 4u, header[i]);
    PortableMeshItemRules rules = {0};
#define TEST_MIR_MINIMUM(name, minimum, maximum) rules.name = minimum;
    PORTABLE_MESH_ITEM_FIELDS(TEST_MIR_MINIMUM)
#undef TEST_MIR_MINIMUM
    rules.actors_capacity = 64;
    rules.actors_lifetime_ticks = 600;
    rules.shell_speed_q16 = 12 * 65536;
    rules.shell_ground_probe_q16 = 65536;
    rules.shell_ground_snap_q16 = 16384;
    rules.shell_trap_arm_ticks = 10;
    rules.shell_trap_lifetime_ticks = 600;
    rules.shell_trap_lock_ticks = 10;
    size_t at = 32;
#define TEST_MIR_SCALAR(name, minimum, maximum)                                                    \
    test_mir_word(bytes + at, (uint32_t)rules.name);                                               \
    at += 4u;
    PORTABLE_MESH_ITEM_FIELDS(TEST_MIR_SCALAR)
#undef TEST_MIR_SCALAR
    static const uint16_t preview[10] = {0x1000, 0x1001, 0x1002, 0x1003, 0x1004,
                                         0x1005, 0x1007, 0x1008, 0x1009, 0x100a};
    for (unsigned i = 0; i < 10u; ++i)
    {
        test_mir_word(bytes + at, preview[i]);
        at += 4u;
    }
    for (unsigned i = 0; i < 27u; ++i)
    {
        test_mir_word(bytes + at, 2u);
        at += 4u;
    }
    for (unsigned rank = 0; rank < 16u; ++rank)
    {
        bytes[at] = 7u;
        bytes[at + 1u] = 0x10u;
        bytes[at + 2u] = 255u;
        bytes[at + 3u] = 255u;
        at += 16u * 4u;
    }
}
#endif
