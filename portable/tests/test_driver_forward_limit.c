/* Executable acceptance contract for L001. Synthetic edges + bounded source
 * witness. Does not assert full sub_8038710 or pilot integration coverage. */
#include "driver_forward_limit.h"
/* Contract assertions remain enabled in optimized CTest builds. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    const int16_t table[8] = {0, -3, -16, -25, -32, -40, -48, -56};
    int32_t a = 0x00120000;
    uint8_t selector = 3;
    assert(portable_driver_forward_limit_select(-1, &selector) == 0);
    assert(selector == 3);
    assert(portable_driver_forward_limit_select(0x1a000000, &selector) == 1);
    assert(selector == 13);
    assert(portable_driver_forward_limit_select(INT32_MAX, &selector) == 1);
    assert(selector == 15);
    assert(portable_driver_forward_limit_select(0, NULL) == 2);
    assert(portable_driver_forward_limit(0x280, 0x02920000, &a, table, 8) == 1);
    assert(a == -0x00100000); /* fresh same-call original witness */
    a = 0x12345678;
    assert(portable_driver_forward_limit(0x280, 0x02810000, &a, table, 8) == 1);
    assert(a == -0x000fa988); /* bits fff05678, original low half preserved */
    a = 0x00012345;
    assert(portable_driver_forward_limit(0x280, 0x0280ffff, &a, table, 8) == 0);
    assert(a == 0x00012345);
    assert(a == 0x00012345); /* equal signed integer half: not above */
    assert(portable_driver_forward_limit(0x280, 0x027d8000, &a, table, 8) == 0);
    assert(a == 0x00012345);
    assert(portable_driver_forward_limit(-1, INT32_MAX, &a, table, 8) == 0);
    assert(a == 0x00012345);
    assert(portable_driver_forward_limit(0, -1, &a, table, 8) == 0);
    assert(a == 0x00012345);
    assert(portable_driver_forward_limit(0, INT32_MIN, &a, table, 8) == 0);
    assert(a == 0x00012345);
    assert(a == 0x00012345);
    assert(portable_driver_forward_limit(0, 0x03010000, &a, table, 8) == 1);
    assert(a == -0x0018dcbb); /* ffe72345 from synthetic table[3]=-25 */
    a = 0x12345678;
    assert(portable_driver_forward_limit(0, 0x02920000, &a, NULL, 8) == 2);
    assert(a == 0x12345678);
    assert(portable_driver_forward_limit(0, 0x02920000, &a, table, 7) == 2);
    assert(a == 0x12345678);
    assert(portable_driver_forward_limit(0, 0x02920000, NULL, table, 8) == 2);
    assert(a == 0x12345678);
    assert(a == 0x12345678);
    puts("L001 forward limit contract: PASS");
    return 0;
}
