#ifndef MKSC_PORTABLE_SURFACE_TRAIL_H
#define MKSC_PORTABLE_SURFACE_TRAIL_H
#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_SURFACE_TRAIL_SLOTS=6, PORTABLE_SURFACE_TRAIL_LIFETIME=29 };
typedef struct {
    uint8_t material_kind[256];
    uint8_t frame_cutoffs[5][2];
    uint8_t offroad_material_min,suppressed_moving_material,spray_material;
} PortableSurfaceTrailRules;
typedef struct {
    int16_t x_q7,y_q7;
    uint16_t direction;
    uint8_t age,kind;
} PortableSurfaceTrailSlot;
typedef struct {
    PortableSurfaceTrailSlot slots[PORTABLE_SURFACE_TRAIL_SLOTS];
    uint8_t eligible_counter,side,extra_spray,visible;
} PortableSurfaceTrail;
typedef struct {
    uint16_t camera_yaw,source_angle_56,source_angle_7a,animation_flags;
    int16_t body_x,body_y;
    int32_t height_q16,speed_q16;
    uint32_t flags;
    uint8_t material,response_status,mushroom_state,parity;
} PortableSurfaceTrailInputs;
static inline int portable_surface_trail_rules_valid(const PortableSurfaceTrailRules *rules)
{
    if(!rules)return 0;
    for(unsigned i=0u;i<256u;++i)if(!rules->material_kind[i]||rules->material_kind[i]>5u)return 0;
    for(unsigned i=0u;i<5u;++i)
        if(!rules->frame_cutoffs[i][0]||rules->frame_cutoffs[i][0]>=rules->frame_cutoffs[i][1]||
           rules->frame_cutoffs[i][1]>PORTABLE_SURFACE_TRAIL_LIFETIME)return 0;
    return 1;
}
static inline int32_t portable_surface_trail_signed16(uint16_t bits)
{return bits>=0x8000u?(int32_t)bits-65536:(int32_t)bits;}
static inline int16_t portable_surface_trail_narrow16(int32_t value)
{return (int16_t)portable_surface_trail_signed16((uint16_t)value);}
static inline uint8_t portable_surface_trail_phase(uint16_t camera,uint16_t heading)
{
    /* Signed camera minus unsigned source heading, followed by arithmetic /512. */
    const int32_t delta=portable_surface_trail_signed16(camera)-(int32_t)heading;
    const int32_t shifted=delta>=0?delta/512:-((-delta+511)/512);
    return (uint8_t)((0xbfu-((uint32_t)(shifted+0x20)&127u))&127u);
}
static inline unsigned portable_surface_trail_sine_index(int32_t phase)
{
    const uint8_t bits=(uint8_t)((uint32_t)phase&127u);
    const int signed_phase=bits>=64u?(int)bits-128:(int)bits;
    return (unsigned)(signed_phase*8)&1023u;
}
static inline uint8_t portable_surface_trail_frame(const PortableSurfaceTrailRules *rules,
    const PortableSurfaceTrailSlot *slot)
{
    if(slot->kind<1u||slot->kind>5u)return 0u;
    const uint8_t *cut=rules->frame_cutoffs[slot->kind-1u];
    return (uint8_t)(slot->age<=cut[0]?0u:slot->age<=cut[1]?1u:2u);
}
/*49D78 emits before E1A0 updates: full pools cannot reuse a slot which expires
 * later in this same tick. All arithmetic narrows at the source halfwords. */
static inline int portable_surface_trail_tick(PortableSurfaceTrail *state,
    const PortableSurfaceTrailRules *rules,const PortableSurfaceTrailInputs *in,
    const int32_t *sine,size_t sine_count)
{
    if(!state||!in||!sine||sine_count<1024u||!portable_surface_trail_rules_valid(rules))return -1;
    int emitted=0;state->extra_spray=0u;state->visible=(uint8_t)((in->flags&0x20u)==0u);
    const int grounded=!(in->animation_flags&0x8000u)&&!(in->flags&0x20u)&&in->height_q16==0;
    if(grounded){
        const int main_trigger=(in->animation_flags&0xcu)!=0u ||
            (in->speed_q16!=0&&in->material>=rules->offroad_material_min)||
            (in->animation_flags&1u)!=0u;
        int eligible=(in->animation_flags&0x20u)!=0u;
        if(main_trigger){
            const uint8_t status=in->response_status;
            eligible=status!=5u&&status!=6u&&status!=7u&&status!=8u&&status!=13u&&
                (status!=15u||in->mushroom_state==3u) &&
                !(in->speed_q16!=0&&in->material==rules->suppressed_moving_material);
        }
        const uint8_t kind=rules->material_kind[in->material];
        if(eligible&&kind){
            state->eligible_counter=(uint8_t)((state->eligible_counter+1u)&3u);
            if(!state->eligible_counter){
                state->side^=1u;
                unsigned index=0u;
                while(index<PORTABLE_SURFACE_TRAIL_SLOTS&&state->slots[index].age)++index;
                if(index<PORTABLE_SURFACE_TRAIL_SLOTS){
                    const int phase=portable_surface_trail_phase(in->camera_yaw,in->source_angle_56);
                    const unsigned angle=portable_surface_trail_sine_index(phase+(state->side?24:-24));
                    const int32_t x_offset=sine[(256u-angle)&1023u]/2048;
                    const int32_t y_offset=(int32_t)((int64_t)sine[angle]*6/65536);
                    state->slots[index]=(PortableSurfaceTrailSlot){
                        portable_surface_trail_narrow16((in->body_x+x_offset)*128),
                        portable_surface_trail_narrow16((in->body_y+y_offset+8)*128),
                        (uint16_t)(phase+(state->side?4:-4)),1u,kind};
                    emitted=1;
                }
            }
        }
        state->extra_spray=(uint8_t)(in->material==rules->spray_material&&(in->parity&1u));
    }
    if(in->flags&0x20u)return emitted;
    const unsigned common=portable_surface_trail_sine_index(
        portable_surface_trail_phase(in->camera_yaw,in->source_angle_7a));
    const int32_t common_x=sine[(256u-common)&1023u]/64;
    /* Thumb multiplies with wrap32 before signed division toward zero. */
    const uint32_t product=(uint32_t)sine[common]*21845u;
    const int64_t signed_product=product>=UINT32_C(0x80000000)?(int64_t)product-INT64_C(4294967296):product;
    const int32_t common_y=(int32_t)(signed_product/INT64_C(16777216));
    for(unsigned i=0u;i<PORTABLE_SURFACE_TRAIL_SLOTS;++i){
        PortableSurfaceTrailSlot *slot=&state->slots[i];
        if(!slot->age)continue;
        const unsigned own=portable_surface_trail_sine_index(slot->direction);
        slot->x_q7=portable_surface_trail_narrow16(slot->x_q7+sine[(256u-own)&1023u]/128+common_x);
        slot->y_q7=portable_surface_trail_narrow16(slot->y_q7+common_y);
        if(++slot->age>PORTABLE_SURFACE_TRAIL_LIFETIME)slot->age=0u;
    }
    return emitted;
}
static inline uint64_t portable_surface_trail_digest(const PortableSurfaceTrail *state)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    const uint8_t owner[]={state->eligible_counter,state->side,state->extra_spray,state->visible};
    for(unsigned i=0u;i<sizeof owner;++i){hash^=owner[i];hash*=UINT64_C(1099511628211);}
    for(unsigned i=0u;i<PORTABLE_SURFACE_TRAIL_SLOTS;++i){
        const PortableSurfaceTrailSlot *slot=&state->slots[i];
        const uint8_t data[]={(uint8_t)slot->x_q7,(uint8_t)((uint16_t)slot->x_q7>>8u),
            (uint8_t)slot->y_q7,(uint8_t)((uint16_t)slot->y_q7>>8u),
            (uint8_t)slot->direction,(uint8_t)(slot->direction>>8u),slot->age,slot->kind};
        for(unsigned j=0u;j<sizeof data;++j){hash^=data[j];hash*=UINT64_C(1099511628211);}
    }
    return hash;
}
#endif
