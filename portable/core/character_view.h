#ifndef MKSC_PORTABLE_CHARACTER_VIEW_H
#define MKSC_PORTABLE_CHARACTER_VIEW_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t response_limit[15], speed_limit[15];
} PortableCharacterViewRules;

typedef struct
{
    uint8_t angle, frame_override, mirror;
} PortableCharacterViewPose;

/* Source sprite direction selection shared by immutable TT/GP presentation. */
typedef struct
{
    uint16_t camera_yaw;
    uint16_t driver_angle;
    int16_t steering_response_q8;
    int32_t speed_q16;
    uint16_t position_82;
    uint16_t camera_adjustment;
    uint32_t source_flags;
    uint8_t selected;
    uint8_t contact_mode_6e, contact_phase_6f;
    uint8_t contact_frame_6c, contact_flags_6d;
} PortableCharacterViewInput;

static inline int32_t portable_character_signed_shift_right(int32_t value, unsigned shift)
{
    if (value >= 0)
        return value >> shift;
    return -(int32_t)(((uint32_t)(-(int64_t)value) +
                       (UINT32_C(1) << shift) - 1u) >> shift);
}

/* D908 tests the shared +6e art channel, not physics spin/hop state. */
static inline uint8_t portable_character_contact_art_active(
    const PortableCharacterViewInput *input)
{
    if(input==NULL)return 0;
    const unsigned mode=input->contact_mode_6e&15u;
    const uint16_t packed=(uint16_t)input->contact_mode_6e |
        (uint16_t)((uint16_t)input->contact_phase_6f<<8u);
    return (uint8_t)((mode==2u || mode==8u || mode==9u) &&
        (packed&UINT16_C(0x01f0))!=0u);
}

/* CA78's selected-driver steering correction is shared by sprite slots and
 * continuous model yaw. Keep the signed correction before byte wrapping. */
static inline int32_t portable_character_view_correction(
    const PortableCharacterViewInput *input,const PortableCharacterViewRules *rules)
{
    if(input==NULL || rules==NULL || !input->selected ||
       (input->source_flags&UINT32_C(0x20))!=0u || portable_character_contact_art_active(input))return 0;
    const uint8_t view=(uint8_t)portable_character_signed_shift_right(
        (int32_t)(int16_t)input->camera_yaw-input->driver_angle,8u);
    int32_t response=input->steering_response_q8;
    if(response<0)response=-response;
    response>>=8u;if(response>14)response=14;
    int64_t speed=input->speed_q16;
    if(speed<0)speed=-speed;
    speed>>=21u;if(speed>14)speed=14;
    int32_t limit=rules->response_limit[response];
    if(limit>rules->speed_limit[speed])limit=rules->speed_limit[speed];
    int32_t adjustment=(int8_t)(input->camera_adjustment>>8u);
    if(adjustment<0)adjustment=-adjustment;
    limit+=adjustment;
    int32_t position=(int8_t)(input->position_82>>8u);
    if(position<0)position=-position;
    if(position>18)position=18;
    if(limit>18-position)limit=18-position;
    if(view!=0u){
        if(input->speed_q16<0)limit=0;
        return limit!=0?(input->steering_response_q8<0?limit+2:-(limit+2)):0;
    }
    return (input->steering_response_q8<0)==(input->speed_q16>=0)?limit:-(limit+2);
}

static inline uint16_t portable_character_visual_yaw(
    const PortableCharacterViewInput *input,const PortableCharacterViewRules *rules)
{
    return input?(uint16_t)((int32_t)input->driver_angle-
        portable_character_view_correction(input,rules)*256):0u;
}

static inline PortableCharacterViewPose portable_character_view_pose(
    const PortableCharacterViewInput *input,const PortableCharacterViewRules *rules)
{
    PortableCharacterViewPose pose={0};
    if(input==NULL)return pose;
    const uint8_t contact=portable_character_contact_art_active(input);
    if(!input->selected || (input->source_flags&UINT32_C(0x20))!=0u || contact){
        /* CA78 CBC4: every unselected kart uses the full relative direction. */
        pose.angle=(uint8_t)(portable_character_signed_shift_right(
            (int32_t)(int16_t)input->camera_yaw-input->driver_angle,9u)&127);
    }else if(rules==NULL){
        /* Content loading rejects missing selected-driver rules. */
        return pose;
    }else{
        uint8_t view=(uint8_t)portable_character_signed_shift_right(
            (int32_t)(int16_t)input->camera_yaw-input->driver_angle,8u);
        view=(uint8_t)(view+portable_character_view_correction(input,rules));
        /* Signed byte division truncates toward zero, unlike the raw ASR9. */
        pose.angle=(uint8_t)((int8_t)view/2)&127u;
    }
    const unsigned folded=pose.angle&63u;
    if(folded!=0u && folded!=63u)pose.mirror=(uint8_t)((pose.angle>>6u)&1u);
    else{
        pose.mirror=contact?(uint8_t)(input->contact_flags_6d&1u):
            (uint8_t)(input->steering_response_q8>=0);
        if(contact)pose.frame_override=input->contact_frame_6c;
    }
    return pose;
}

#endif
