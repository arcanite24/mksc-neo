#include "gp_cup_scoring.h"
#include <string.h>
int portable_gp_cup_scoring(const PortableGpScoringRules *rules, const uint8_t finish_order[8],
                            const uint8_t previous_points[8], uint8_t points[8], uint8_t awards[8],
                            uint8_t standing_order[8], uint8_t standing_rank[8])
{
    if (!rules || !finish_order || !previous_points || !points || !awards || !standing_order ||
        !standing_rank)
        return 0;
    uint8_t next_points[8], next_awards[8], order[8], ranks[8];
    uint8_t seen = 0;
    for (unsigned rank = 0; rank < 8u; ++rank)
    {
        const unsigned racer = finish_order[rank], award = rules->finish_points[rank];
        if (racer >= 8u || (seen & (1u << racer)) || award > 63u)
            return 0;
        seen |= (uint8_t)(1u << racer);
        next_points[racer] = (uint8_t)(previous_points[racer] + award);
        next_awards[racer] = (uint8_t)award;
        order[rank] = (uint8_t)rank;
    }
    /* 801E0B8 selection-swap: player0 wins equal-score comparisons. Equal
     * CPU pairs are not swapped, though earlier swaps affect their order. */
    for (unsigned first = 0; first < 7u; ++first)
        for (unsigned second = first + 1u; second < 8u; ++second)
        {
            const uint8_t a = order[first], b = order[second];
            if (next_points[a] < next_points[b] ||
                (next_points[a] == next_points[b] && b == 0u && a != 0u))
            {
                order[first] = b;
                order[second] = a;
            }
        }
    for (unsigned i = 0; i < 8u; ++i)
    {
        ranks[i] = 0;
        for (unsigned j = 0; j < 8u; ++j)
            ranks[i] += (uint8_t)(next_points[j] > next_points[i]);
    }
    memcpy(points, next_points, 8);
    memcpy(awards, next_awards, 8);
    memcpy(standing_order, order, 8);
    memcpy(standing_rank, ranks, 8);
    return 1;
}
