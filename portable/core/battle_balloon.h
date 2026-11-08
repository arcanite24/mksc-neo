#ifndef MKSC_PORTABLE_BATTLE_BALLOON_H
#define MKSC_PORTABLE_BATTLE_BALLOON_H

#include <stdint.h>

typedef enum
{
    PORTABLE_BATTLE_BALLOON_NONE = 0,
    PORTABLE_BATTLE_BALLOON_POP,
    PORTABLE_BATTLE_BALLOON_ELIMINATED
} PortableBattleBalloonEvent;

typedef struct
{
    uint8_t remaining_ticks;
    uint8_t revenge_bomb; /* Source class4C bit8, independent of handling class. */
} PortableBattleEliminationState;

/* D214(15)/B020 enters during C444; D328/AFA4 advances after all drivers.
 * F2C50 is one frame followed by a zero-duration terminator. Presentation
 * reads this state; rendering never owns the role transition. */
static inline int portable_battle_elimination_tick(
    PortableBattleEliminationState *state, PortableBattleBalloonEvent event,
    uint8_t duration)
{
    if (state->revenge_bomb) return 1;
    if (state->remaining_ticks == 0u && event == PORTABLE_BATTLE_BALLOON_ELIMINATED)
    {
        if (duration == 0u || duration > 127u) return 0;
        state->remaining_ticks = duration;
    }
    if (state->remaining_ticks != 0u && --state->remaining_ticks == 0u)
        state->revenge_bomb = 1u;
    return 1;
}

/* CC80: source6D stores the seven-bit walking phase above contact bit0.
 * ASR23 floors negative speed; the half-period division truncates to zero. */
static inline int32_t portable_battle_bomb_walk_period(int32_t speed_q16)
{
    int32_t scaled=speed_q16/8388608;
    if (speed_q16<0 && speed_q16%8388608!=0) --scaled;
    return 20-scaled;
}

static inline uint8_t portable_battle_bomb_walk_tick(uint8_t *flags_6d,int32_t speed_q16)
{
    unsigned phase=(*flags_6d>>1u)+1u;
    if ((int32_t)phase>portable_battle_bomb_walk_period(speed_q16)) phase=0u;
    *flags_6d=(uint8_t)((phase<<1u)|(*flags_6d&1u));
    /* CC80 clears19A bits4/8 each turn, then marks the two footfalls. */
    if (phase==0u) return 8u;
    return (int32_t)phase==portable_battle_bomb_walk_period(speed_q16)/2 ? 4u : 0u;
}

static inline uint8_t portable_battle_bomb_walk_frame(
    uint8_t phase,int32_t speed_q16,uint8_t view_angle,uint16_t response_88)
{
    const unsigned angle=view_angle&63u;
    unsigned alternate=(angle==0u || angle==63u)?
        (unsigned)(response_88<0x8000u):((view_angle>>6u)&1u);
    if ((int32_t)phase<portable_battle_bomb_walk_period(speed_q16)/2)
        alternate=1u-alternate;
    return (uint8_t)alternate;
}

/* C444: called after protection/B550 and before 8200/9B38. The scene's
 * low nibble is independent of the race finish phase. Call only in Battle. */
static inline PortableBattleBalloonEvent portable_battle_balloon_tick(
    uint16_t *hit_flags, uint8_t *balloons, uint8_t scene_state)
{
    if ((*hit_flags & UINT16_C(0x8000)) != 0u)
    {
        /* A queued second hit survives the latch-clearing turn. */
        *hit_flags &= UINT16_C(0x7fff);
    }
    else if ((*hit_flags & UINT16_C(1)) != 0u)
    {
        *hit_flags &= UINT16_C(0xfffe);
        if ((scene_state & UINT8_C(0x0f)) != 4u && *balloons != 0u)
        {
            --*balloons;
            *hit_flags |= UINT16_C(0x8000);
            return *balloons == 0u ? PORTABLE_BATTLE_BALLOON_ELIMINATED :
                                    PORTABLE_BATTLE_BALLOON_POP;
        }
    }
    return PORTABLE_BATTLE_BALLOON_NONE;
}

#endif
