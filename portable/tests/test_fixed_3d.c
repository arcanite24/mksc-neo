#include "fixed_3d.h"
#include <stdio.h>
#include <limits.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    CHECK(portable_fixed_3d_sqrt(0) == 0);
    CHECK(portable_fixed_3d_sqrt(81) == 9);
    CHECK(portable_fixed_3d_sqrt(UINT64_MAX) == UINT32_MAX);
    CHECK(portable_fixed_3d_dt(262144) == 4389);
    CHECK(portable_fixed_3d_dt(-262144) == -4389);
    const int64_t large[3] = {INT64_MAX / 4, 0, 0}, small[3] = {1, 1, 0}, zero[3] = {0, 0, 0};
    int32_t n[3];
    CHECK(portable_fixed_3d_normalize(large, n) && n[0] == 1073741824 && n[1] == 0);
    CHECK(portable_fixed_3d_normalize(small, n));
    CHECK(n[0] >= 759250120 && n[0] <= 759250130 && n[0] == n[1]);
    CHECK(!portable_fixed_3d_normalize(zero, n));
    int32_t sine, cosine;
    portable_fixed_3d_sincos(16384, &sine, &cosine);
    CHECK(sine == 1073741824 && cosine == 0);
    portable_fixed_3d_sincos(8192, &sine, &cosine);
    CHECK(sine > 758000000 && sine < 761000000 && cosine > 758000000 && cosine < 761000000);
    portable_fixed_3d_sincos((uint16_t)-8192, &sine, &cosine);
    CHECK(sine < -758000000 && cosine > 758000000);
    return 0;
}
