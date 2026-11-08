#ifndef PORTABLE_GP_CUP_SCORING_H
#define PORTABLE_GP_CUP_SCORING_H
#include "gp_scoring.h"
/* One semantic award operation: racer-indexed points/awards, rank-indexed
 * finish/standing orders and racer-indexed competition ranks. Failure leaves
 * every output unchanged. points may alias previous_points; other outputs
 * must be distinct. No race/session/source-layout dependencies. */
int portable_gp_cup_scoring(const PortableGpScoringRules *rules, const uint8_t finish_order[8],
                            const uint8_t previous_points[8], uint8_t points[8], uint8_t awards[8],
                            uint8_t standing_order[8], uint8_t standing_rank[8]);
#endif
