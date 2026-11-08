#ifndef MKSC_PORTABLE_DRIVER_STEERING_H
#define MKSC_PORTABLE_DRIVER_STEERING_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_DRIVER_STEERING_OK = 0,
    PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_STEERING_TABLE_TOO_SMALL = 2,
    PORTABLE_DRIVER_STEERING_RESPONSE_TABLE_TOO_SMALL = 3,
};

typedef struct
{
    int32_t speed_q16;       /* original +0xbc */
    uint8_t time_trial_mode; /* source scene mode2 changes the case0 speed gate */
    int16_t response_q8;     /* original +0x88 */
    uint16_t control_word;   /* original +0x96 */
    uint32_t state_flags;    /* original +0x48 */
    uint8_t mode;            /* original +0x78 */
    int16_t angle;           /* original +0x7a */
    int16_t steering_offset; /* original +0x7c */
    int16_t base_angle;      /* original +0x80 */
    uint16_t position_82;    /* original +0x82 */
    uint16_t source_angle_56; /* original +0x56 */
    uint8_t response_status; /* original +0x7e */
    uint8_t response_slot;   /* original +0xb0 */
    uint16_t response_flags; /* original +0xb4 */
    uint16_t spin_delay;     /* original +0xb8, signed countdown */
    uint16_t spin_delay_cue; /* one-turn 8BF4 song: 165 braked out, 162 expired */
    uint16_t drift_angle;    /* original +0xc8 */
    uint16_t exit_timer;     /* original +0xcc */
    uint8_t driver_mode;     /* original +0x84 */
    uint8_t response_selector_index; /* original +0x86 */
    uint8_t response_selector_flags; /* original +0x4c */
    uint8_t response_row;    /* original +0x54 */
} PortableDriverSteeringState;

typedef struct
{
    uint8_t audio_cue_140;
    uint8_t particle_effect;
    uint8_t strong_exit;
} PortableDriverChargedExitEvent;

enum
{
    PORTABLE_DRIVER_RESPONSE_ROW_COUNT = 8,
    PORTABLE_DRIVER_RESPONSE_ROW_WIDTH = 8,
};

/* MOT1v8: B550/8E34 selectors and response coefficients, owned by content. */
typedef struct {
    int16_t rows[PORTABLE_DRIVER_RESPONSE_ROW_COUNT][PORTABLE_DRIVER_RESPONSE_ROW_WIDTH];
    int32_t selector_rows[16];
    int32_t character_offsets[8];
} PortableDriverResponseRules;
static inline int portable_driver_response_rules_valid(const PortableDriverResponseRules *rules)
{
    if(!rules)return 0;
    for(unsigned row=0u;row<8u;++row){
        if(rules->rows[row][0]>0)return 0;
        for(unsigned column=1u;column<8u;++column)if(rules->rows[row][column]<0)return 0;
    }
    for(unsigned i=0u;i<16u;++i){
        if(rules->selector_rows[i]<0||rules->selector_rows[i]>7)return 0;
        for(unsigned c=0u;c<8u;++c){
            if(rules->character_offsets[c]<-7||rules->character_offsets[c]>7||
               rules->selector_rows[i]+rules->character_offsets[c]>7)return 0;
        }
    }
    return 1;
}

/* Apply sub_8038AFC's bounded signed response-state update. */
int portable_driver_steering_response_tick(
    PortableDriverSteeringState *state,
    const int16_t *response_data,
    size_t response_data_count);

/* Apply the source-shaped heading delta selected by left/right controls. */
int portable_driver_steering_tick(
    PortableDriverSteeringState *state,
    const int16_t *steering_delta_table,
    size_t steering_delta_count);

/* A9AC queued strong or ordinary hit, consumed only from grounded 9B38. */
int portable_driver_queued_hit(PortableDriverSteeringState *state);

/* Complete 8E34 case 13. spin_rate is the signed halfword at original +0xb6.
 * The caller controls whether contact hold permits this response update. */
int portable_driver_strong_spin_tick(PortableDriverSteeringState *state,
                                     uint16_t *spin_rate);

/* Ground A374/A3A0 slippery material entry, before the response consumer. */
int portable_driver_surface_spin(PortableDriverSteeringState *state,
                                uint8_t ground_mode, uint8_t material);

/* Source sub_8038E34 status 0->1 and held-direction status-1 response. */
/* Player turn/relaxation dispatch, including charged-release presentation. */
int portable_driver_source_turn_response_tick(
    PortableDriverSteeringState *state, int32_t speed_target_q16,
    const PortableDriverResponseRules *rules,PortableDriverChargedExitEvent *event);

int portable_driver_source_left_turn_response_tick(
    PortableDriverSteeringState *state, int32_t speed_target_q16,
    const PortableDriverResponseRules *rules);

/* Complete 8E34 case 11 rebound response (A-held and released controls). */
int portable_driver_collision_response_tick(PortableDriverSteeringState *state);

/* Complete 8E34 case14: row-zero signed approach/charged exit, zero reset,
 * and the halfword large-angle transfer into response/driver mode11. */
int portable_driver_source_status14_tick(PortableDriverSteeringState *state,
    int32_t speed_target_q16, int is_player, int is_primary_effect_driver,
    const PortableDriverResponseRules *rules,PortableDriverChargedExitEvent *event);

/* Apply sub_8037B30's bounded +0x54/+0xb0 response-table selector. */
int portable_driver_response_row_select(
    PortableDriverSteeringState *state,
    uint16_t control_word,const PortableDriverResponseRules *rules);

/* Apply the neutral status-1 branch of sub_8038E34 and its sub_8039690
 * charged-exit consumer. response_row_data is one 16-byte _080EBE28 row.
 * The two ownership booleans represent the source's player and primary
 * effect-object pointer comparisons; they never affect authoritative motion. */
int portable_driver_charged_steering_exit_tick(
    PortableDriverSteeringState *state,
    int32_t speed_target_q16,
    const int16_t response_row_data[8],
    int is_player,
    int is_primary_effect_driver,
    PortableDriverChargedExitEvent *event);

#endif
