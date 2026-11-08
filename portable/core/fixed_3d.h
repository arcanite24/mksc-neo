#ifndef PORTABLE_FIXED_3D_H
#define PORTABLE_FIXED_3D_H
#include <stdint.h>
#define PORTABLE_FIXED_3D_ONE INT32_C(65536)
#define PORTABLE_FIXED_3D_UNIT INT32_C(1073741824)
#define PORTABLE_FIXED_3D_POSITION_LIMIT INT32_C(268435456)
uint64_t portable_fixed_3d_sqrt(uint64_t value);
int32_t portable_fixed_3d_clamp(int64_t value);
int32_t portable_fixed_3d_mul(int32_t a, int32_t b);
int32_t portable_fixed_3d_dt(int32_t per_second);
int64_t portable_fixed_3d_dot(const int32_t a[3], const int32_t b[3]);
/* Input magnitudes are arbitrary bounded i64; prescaling avoids cross-square overflow. */
int portable_fixed_3d_normalize(const int64_t vector[3], int32_t unit[3]);
void portable_fixed_3d_sincos(uint16_t angle, int32_t *sine, int32_t *cosine);
#endif
