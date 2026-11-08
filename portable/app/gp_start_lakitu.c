#include "gp_start_lakitu.h"

#include <stddef.h>
#include <string.h>

static int32_t signed_le32(const uint8_t *bytes)
{
    const uint32_t bits=(uint32_t)bytes[0]|(uint32_t)bytes[1]<<8u|
        (uint32_t)bytes[2]<<16u|(uint32_t)bytes[3]<<24u;
    return bits<=INT32_MAX?(int32_t)bits:
        -1-(int32_t)(UINT32_MAX-bits);
}

static int32_t bob_y(uint32_t actor_turn,const uint8_t *offsets)
{
    /* 5050C completes entrance at1469, center56. 50DB8 starts its
     * counter at1 on1470 and adds EF050 Y every eighth turn. */
    const uint32_t counter=actor_turn-1469u;
    int32_t y=56;
    for(uint32_t step=1u;step<=counter/8u;++step)
        y+=signed_le32(offsets+(step&15u)*8u+4u);
    return y;
}

static int start_lakitu_y(uint32_t intro_frame,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024],int16_t *cell_y,uint32_t last_frame)
{
    if(bob_offsets==NULL||sine==NULL||cell_y==NULL||
       intro_frame<1471u||intro_frame>last_frame)return -1;
    const uint32_t actor_turn=intro_frame-1u;
    if(actor_turn>=1731u)return 0; /* 50BEC removes the actor. */
    int32_t y;
    if(actor_turn<1710u){
        y=bob_y(actor_turn,bob_offsets);
    }else{
        /* EF310 installs50574 at1710 before its first call. It seeds
         * center from the prior bob, then decreases phase20 to19. */
        const int32_t center=bob_y(1709u,bob_offsets)+16;
        const uint32_t elapsed=actor_turn-1709u;
        const uint32_t phase=elapsed>=20u?0u:20u-elapsed;
        const int64_t product=(int64_t)sine[phase*256u/20u]*center;
        /* Source adds7fff before signed shift: division truncates
         * toward zero, explicitly avoiding host signed-right-shift. */
        y=(int32_t)(product/INT64_C(32768))-16;
    }
    /* F674 tests the signed actor Y before wrapping the lamp cells.
     * A negative cell origin is allowed while actor Y remains visible. */
    if(y<0||y>224)return 0;
    *cell_y=(int16_t)(y-11);
    return 1;
}

int portable_gp_start_lakitu_y(uint32_t intro_frame,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024],int16_t *cell_y)
{
    return start_lakitu_y(intro_frame,bob_offsets,sine,cell_y,2629u);
}

int portable_gp_start_lakitu_y_hit_continuation(uint32_t intro_frame,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024],int16_t *cell_y)
{
    return start_lakitu_y(intro_frame,bob_offsets,sine,cell_y,2749u);
}

int portable_gp_finish_lakitu_consume(PortableGpRecoveryLakituState *state,
    uint16_t *lap_markers,const PortableGpFinishLakituRules *rules)
{
    if(!state || !lap_markers)return -1;
    if(!(*lap_markers&UINT16_C(0x4000)))return 1;
    if(!rules){*lap_markers&=UINT16_C(0xbfff);return 1;}
    if(!portable_gp_finish_lakitu_rules_valid(rules))return -1;
    /* 51014 dispatches4000 before competing retrieval and warning states. */
    state->phase=PORTABLE_GP_LAKITU_FINISH;
    state->finish_turn=0u;state->inverse_q8=128u;
    state->wait=0u;state->counter=0;state->visible|=1u;
    state->cleared_requests=0u;state->retrieval_effect_started=0u;
    state->drop_started=0u;state->coin_loss_requested=0u;
    state->callback=PORTABLE_GP_RECOVERY_LAKITU_IDLE;
    state->script=PORTABLE_GP_RECOVERY_SCRIPT_NONE;
    *lap_markers&=UINT16_C(0xba8f); /* 509F4 clears4000/100,51014 clears470. */
    return 1;
}

int portable_gp_finish_lakitu_tick(PortableGpRecoveryLakituState *state,
    const PortableGpFinishLakituRules *rules,
    const uint8_t offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024])
{
    if(!state || state->phase!=PORTABLE_GP_LAKITU_FINISH || !offsets || !sine ||
       !portable_gp_finish_lakitu_rules_valid(rules))return -1;
    state->retrieval_effect_started=0u;state->coin_loss_requested=0u;
    const unsigned total=(unsigned)rules->entrance_ticks+rules->bob_ticks+rules->exit_ticks;
    if(state->finish_turn>total)return -1;
    if(state->finish_turn==total){
        /* EF898 starts one turn after the preceding script's42 jump. */
        state->phase=0u;state->visible&=UINT16_C(0xfffe);
        state->x=state->y=-128;state->inverse_q8=261u;
        return 1;
    }
    const unsigned turn=++state->finish_turn;
    unsigned frame_turn=turn-1u;
    for(unsigned i=0u;i<rules->row_count;++i){
        if(frame_turn<rules->frames[i].duration){state->art_bank=rules->frames[i].art_bank;break;}
        frame_turn-=rules->frames[i].duration;
    }
    if(turn<=rules->entrance_ticks){
        /* 5045C decrements phase20 before its first sine sample. */
        const unsigned phase=turn<20u?20u-turn:0u;
        state->x=(int16_t)(120-(int64_t)sine[phase*256u/20u]*60/32768);
        state->y=(int16_t)(4+(int64_t)sine[(20u-phase)*256u/20u]*60/32768);
        if(phase<=15u && state->inverse_q8<=255u)state->inverse_q8+=8u;
    }else if(turn<=(unsigned)rules->entrance_ticks+rules->bob_ticks){
        /* 50DF8 restarts7C when installed; bob accumulates every eighth call. */
        if(turn==(unsigned)rules->entrance_ticks+1u)state->counter=0;
        ++state->counter;
        if(((uint32_t)state->counter&7u)==0u){
            const unsigned index=((uint32_t)state->counter>>3u)&15u;
            state->x=(int16_t)(state->x+signed_le32(offsets+index*8u));
            state->y=(int16_t)(state->y+signed_le32(offsets+index*8u+4u));
        }
    }else{
        /* 50754 shrinks before placing, with both coordinates using prior X/Y. */
        const int32_t x=state->x,y=state->y;
        if(state->inverse_q8>128u)state->inverse_q8-=8u;
        state->x=(int16_t)(x+y/8);
        state->y=(int16_t)(y-(x-128)/16);
    }
    return 1;
}

int portable_gp_recovery_lakitu_consume(PortableGpRecoveryLakituState *state,
    uint16_t *requests, uint32_t driver_flags)
{
    if(state==NULL||requests==NULL||state->phase>PORTABLE_GP_LAKITU_FINISH||
       (*requests & ~UINT16_C(0x1e70))!=0u)return -1;
    if(state->phase==PORTABLE_GP_LAKITU_FINISH)return 1;
    /* 51014 handles4000 before retrieval, then holds D000 until its script
     * completes. Finished drivers may queue new recovery during that hold. */

    /* E003 exit ignores recovery requests until its script returns idle. */
    if((state->phase>=4u&&state->phase<=5u)||state->phase>=PORTABLE_GP_LAKITU_WARNING_EXIT)return 1;
    if(state->phase==PORTABLE_GP_LAKITU_WARNING && (*requests&UINT16_C(0x470))){
        /* 51014 resets a competing actor before retrying the retained request. */
        state->phase=PORTABLE_GP_LAKITU_RESET;state->exit_velocity_q16=0;
        state->warning_turn=0u;return 1;
    }
    PortableGpRecoveryLakituState next=*state;
    uint16_t pending=*requests;
    /* 51014 A48/A94 installs the script before the driver pass. */
    if(pending&UINT16_C(0x200)){
        pending&=UINT16_C(0xfbff);
        if(next.phase==1u){
            pending&=UINT16_C(0xfdff);
            next.phase=5u;next.wait=0u;next.exit_velocity_q16=0;
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_CANCEL_EXIT;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_NONE;
        }else if(next.phase==0u||next.phase==PORTABLE_GP_LAKITU_WARNING)pending&=UINT16_C(0xfdff);
    }else if(pending&UINT16_C(0x400)){
        if(next.phase!=0u)return -1;
        pending&=~UINT16_C(0x400);
        next.phase=1u;
        next.wait=0u;
        next.callback=PORTABLE_GP_RECOVERY_LAKITU_DESCENT;
        next.script=PORTABLE_GP_RECOVERY_SCRIPT_DESCENT_HOLD;
        next.art_bank=19u;
        next.visible|=1u;
        next.flags=2u;
    }else if(pending&UINT16_C(0x30)){
        if(next.phase!=0u && next.phase!=1u)return -1;
        const int bounds=(pending&UINT16_C(0x10))==0u;
        if(next.phase==0u || bounds){next.x=120;next.y=-16;}
        pending&=bounds ? UINT16_C(0xfbdf) : UINT16_C(0xffef);
        next.phase=2u;
        next.wait=0u;
        next.callback=PORTABLE_GP_RECOVERY_LAKITU_RETRIEVE;
        next.script=PORTABLE_GP_RECOVERY_SCRIPT_RETRIEVE_HOLD;
        next.art_bank=19u;
        next.visible|=1u;
        next.flags=2u;
    }else if(pending&UINT16_C(0x40)){
        if(next.phase!=0u && next.phase!=2u)return -1;
        pending&=UINT16_C(0xefbf); /* FA5C clears40 and1000, retains400. */
        next.phase=3u;
        next.x=120;next.y=-128;
        next.wait=0u;
        next.callback=PORTABLE_GP_RECOVERY_LAKITU_DROP;
        next.script=PORTABLE_GP_RECOVERY_SCRIPT_DROP_INSTALL;
        next.visible|=1u;
        next.drop_started=1u;
    }
    if(next.phase==0u && (pending&UINT16_C(0x1000)) && !(driver_flags&32u)){
        next.phase=PORTABLE_GP_LAKITU_WARNING;next.warning_turn=0u;next.counter=20;
        next.art_bank=15u;next.visible|=1u;next.flags=2u;
    }
    *state=next;
    *requests=pending;
    return 1;
}

/* EF0D0 /505D8 entrance,503B4 bob and EF1C8 /50794 exit. The
 * presentation script clock is independent of the driver's eight-turn check. */
static int warning_tick(PortableGpRecoveryLakituState *state,uint32_t driver_flags,
    const uint8_t *offsets,const int32_t *sine)
{
    if(state->phase==PORTABLE_GP_LAKITU_RESET){
        if(state->warning_turn!=0u){
            state->phase=0u;state->visible&=~1u;state->x=state->y=-128;
        }else{
            state->y=(int16_t)(state->y-state->exit_velocity_q16/65536);
            state->exit_velocity_q16+=INT32_C(0x8000);
            if(state->y<0)state->warning_turn=1u; /* EF898 decodes next turn. */
        }
        return 1;
    }
    if(state->phase==PORTABLE_GP_LAKITU_WARNING_EXIT){
        const int32_t x=state->x,y=state->y;
        state->x=(int16_t)(x+y/8);
        state->y=(int16_t)(y-(x-128)/16);
        if(++state->warning_turn==22u){
            state->phase=0u;state->visible&=~1u;state->x=state->y=-128;
        }
        return 1;
    }
    if(sine==NULL||offsets==NULL)return -1;
    const unsigned turn=state->warning_turn;
    if(turn>95u)return -1;
    state->art_bank=turn<16u?15u:turn<32u?16u:turn<48u?17u:
        turn<64u?18u:turn<80u?15u:16u;
    if(turn<20u){
        const unsigned phase=19u-turn;
        state->x=(int16_t)(120-(int64_t)sine[phase*256u/20u]*60/32768);
        state->y=(int16_t)((int64_t)sine[(20u-phase)*256u/20u]*60/32768-12);
    }else{
        if(turn==20u)state->counter=0;
        const uint32_t counter=(uint32_t)state->counter+1u;
        state->counter=counter<=INT32_MAX?(int32_t)counter:
            -1-(int32_t)(UINT32_MAX-counter);
        if(((uint32_t)state->counter&7u)==0u){
            const unsigned index=((uint32_t)state->counter>>3u)&15u;
            state->x=(int16_t)(state->x+signed_le32(offsets+index*8u));
            state->y=(int16_t)(state->y+signed_le32(offsets+index*8u+4u));
        }
        if(!(driver_flags&UINT32_C(0x400))){
            state->cleared_requests=UINT16_C(0x1000);
            state->phase=PORTABLE_GP_LAKITU_WARNING_EXIT;state->warning_turn=0u;
            return 1; /* Exit callback installs on the following actor turn. */
        }
    }
    state->warning_turn=(uint16_t)(turn==95u?32u:turn+1u);
    return 1;
}

uint16_t portable_gp_lakitu_source_state(const PortableGpRecoveryLakituState *state)
{
    if(state==NULL||state->phase==0u)return 0u;
    if(state->phase==PORTABLE_GP_LAKITU_FINISH)return UINT16_C(0xd000);
    if(state->phase==PORTABLE_GP_LAKITU_WARNING||state->phase==PORTABLE_GP_LAKITU_WARNING_EXIT)return UINT16_C(0xc000);
    if(state->phase==PORTABLE_GP_LAKITU_RESET)return UINT16_C(0x1000);
    return (uint16_t)(0xe000u+state->phase-1u);
}

uint16_t portable_gp_lakitu_scale(const PortableGpRecoveryLakituState *state)
{
    if(state==NULL)return 0u;
    if(state->phase==PORTABLE_GP_LAKITU_FINISH)
        return state->inverse_q8?(uint16_t)(65536u/state->inverse_q8):0u;
    unsigned inverse=256u;
    if(state->phase==PORTABLE_GP_LAKITU_WARNING&&state->warning_turn<=20u){
        const unsigned turn=state->warning_turn;
        inverse=turn<=4u?128u:128u+(turn-4u)*8u;
    }else if(state->phase==PORTABLE_GP_LAKITU_WARNING_EXIT){
        const unsigned turn=state->warning_turn;
        inverse=turn>=16u?128u:256u-turn*8u;
    }
    return (uint16_t)(65536u/inverse);
}

uint32_t portable_gp_lakitu_image(uint8_t art_bank)
{
    if(art_bank>=21u&&art_bank<=28u)return UINT32_C(0x27030)+art_bank-21u;
    if(art_bank>=15u&&art_bank<=18u)return UINT32_C(0x27020)+art_bank-15u;
    if(art_bank==19u||art_bank==20u)return UINT32_C(0x27010)+art_bank-19u;
    return 0u; /* Unsupported visible banks fail the native image lookup. */
}

int portable_gp_recovery_lakitu_tick(PortableGpRecoveryLakituState *state,
    uint16_t *requests, uint32_t driver_flags, int16_t prior_projected_y,
    int32_t *height_q16, uint16_t *packed_effect_72,
    const uint8_t bob_offsets[PORTABLE_GP_START_LAKITU_BOB_BYTES],
    const int32_t sine[1024])
{
    if(state==NULL||requests==NULL||height_q16==NULL||packed_effect_72==NULL||
       state->phase>PORTABLE_GP_LAKITU_FINISH||
       (*requests & ~UINT16_C(0x1e70))!=0u)return -1;
    if(state->phase==PORTABLE_GP_LAKITU_FINISH)return 1;
    PortableGpRecoveryLakituState next=*state;
    uint16_t pending=*requests,effect=*packed_effect_72;
    int32_t height=*height_q16;
    next.retrieval_effect_started=0u;
    next.coin_loss_requested=0u;
    const uint16_t preceding_requests=pending;
    if(portable_gp_recovery_lakitu_consume(&next,&pending,driver_flags)!=1)
        return -1;
    next.cleared_requests=0u;
    if(next.phase>=PORTABLE_GP_LAKITU_WARNING){
        if(warning_tick(&next,driver_flags,bob_offsets,sine)!=1)return -1;
        pending&=(uint16_t)~next.cleared_requests;
        *state=next;*requests=pending;return 1;
    }
    const int installed=pending!=preceding_requests;
    if(next.drop_started){
        if((effect&15u)==14u)
            effect&=UINT16_C(0x100); /* D950 index2 retains only packed bit8. */
        next.drop_started=0u;
    }
    if(next.script==PORTABLE_GP_RECOVERY_SCRIPT_DROP_INSTALL){
        next.art_bank=20u;
        next.script=PORTABLE_GP_RECOVERY_SCRIPT_DROP_HOLD;
    }
    if(installed){
        /* The new hold script's first decode retains zero wait. */
    }else if(next.wait!=0u){
        --next.wait;
    }else{
        /* The recovered hold scripts loop F2(-1) to F1(1), storing
         * wait0. EF810's initial F1(10) stores9 and stops decoding. */
        next.flags&=~1u;
        if(next.script==PORTABLE_GP_RECOVERY_SCRIPT_LIFT_WAIT){
            next.wait=9u;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_LIFT_INSTALL;
        }else if(next.script==PORTABLE_GP_RECOVERY_SCRIPT_LIFT_INSTALL){
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_LIFT;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_LIFT_HOLD;
            next.art_bank=20u;
            next.flags|=2u;
        }else if(next.script==PORTABLE_GP_RECOVERY_SCRIPT_EXIT_INSTALL){
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_EXIT;
            next.art_bank=19u;
            next.wait=59u; /* F1(60) stores60-1. */
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_EXIT_WAIT;
            next.flags|=2u;
        }else if(next.script==PORTABLE_GP_RECOVERY_SCRIPT_EXIT_WAIT){
            /*42 assigns EF898 and returns; do not hide in this decode pass. */
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_HIDE;
        }else if(next.script==PORTABLE_GP_RECOVERY_SCRIPT_HIDE){
            next.phase=0u;next.visible&=UINT16_C(0xfffe);
            next.x=-128;next.y=-128;
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_IDLE;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_HIDDEN_HOLD;
        }
    }
    switch(next.callback){
    case PORTABLE_GP_RECOVERY_LAKITU_IDLE:break;
    case PORTABLE_GP_RECOVERY_LAKITU_DESCENT:
        if(sine==NULL)return -1;
        if(next.flags&2u)next.counter=32;
        if(next.counter<0||next.counter>32)return -1;
        if(next.counter!=0)--next.counter;
        next.x=120;
        next.y=(int16_t)(sine[(32u-(uint32_t)next.counter)*8u]/512-16);
        if(next.counter==0)next.callback=PORTABLE_GP_RECOVERY_LAKITU_BOB;
        break;
    case PORTABLE_GP_RECOVERY_LAKITU_BOB:
        if(bob_offsets==NULL)return -1;
        if(next.flags&2u)next.counter=0;
        if(next.counter<0||next.counter==INT32_MAX)return -1;
        ++next.counter;
        if(((uint32_t)next.counter&7u)==0u){
            const int32_t y=(int32_t)next.y+signed_le32(bob_offsets+
                (((uint32_t)next.counter>>3u)&15u)*8u+4u);
            if(y<INT16_MIN||y>INT16_MAX)return -1;
            next.y=(int16_t)y;
        }
        break;
    case PORTABLE_GP_RECOVERY_LAKITU_RETRIEVE:
        if(next.y>85){
            next.y=88;
            next.wait=0u;
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_IDLE;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_LIFT_WAIT;
            if((effect&15u)==11u || (effect&15u)==13u){
                effect=(effect&UINT16_C(0xfff0))|((effect&15u)+1u);
                next.retrieval_effect_started=1u;
            }
        }else{
            if(next.y<INT16_MIN+2)return -1;
            next.y=(int16_t)(next.y+2);
        }
        break;
    case PORTABLE_GP_RECOVERY_LAKITU_LIFT:
        if(height>INT32_MAX-INT32_C(0x4000)||height<0)return -1;
        height+=INT32_C(0x4000);
        if(prior_projected_y<INT16_MIN+52)return -1;
        next.y=(int16_t)(prior_projected_y-52);
        if(height>INT32_C(0x0006ffff))
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_IDLE;
        break;
    case PORTABLE_GP_RECOVERY_LAKITU_DROP:
    {
        const int32_t y=(int32_t)prior_projected_y-52;
        next.y=(int16_t)(y < -64 ? -64 : y);
        if(height==0){
            next.phase=4u;
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_IDLE;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_EXIT_INSTALL;
            next.exit_velocity_q16=0;
            next.coin_loss_requested=2u;
        }
        break;
    }
    case PORTABLE_GP_RECOVERY_LAKITU_CANCEL_EXIT:
        next.y=(int16_t)(next.y-next.exit_velocity_q16/65536);
        next.exit_velocity_q16+=INT32_C(0x8000);
        if(next.y<0){
            next.callback=PORTABLE_GP_RECOVERY_LAKITU_IDLE;
            next.script=PORTABLE_GP_RECOVERY_SCRIPT_HIDE;
        }
        break;
    case PORTABLE_GP_RECOVERY_LAKITU_EXIT:
        if(next.y>0){
            const int32_t y=(int32_t)next.y-next.exit_velocity_q16/65536;
            if(y<INT16_MIN||y>INT16_MAX||next.exit_velocity_q16>INT32_MAX-0x8000)
                return -1;
            next.y=(int16_t)y;
            next.exit_velocity_q16+=INT32_C(0x8000);
        }
        break;
    default:return -1;
    }
    if(next.callback!=PORTABLE_GP_RECOVERY_LAKITU_IDLE)next.flags&=~2u;
    /* F9E4 also clears its first-call bit after replacing callback with0. */
    if(state->callback==PORTABLE_GP_RECOVERY_LAKITU_RETRIEVE||
       next.retrieval_effect_started)next.flags&=~2u;
    *state=next;
    *requests=pending;
    *height_q16=height;
    *packed_effect_72=effect;
    return 1;
}

static uint16_t recovery_u16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0]|(unsigned)p[1]<<8u);
}

static void recovery_store16(uint8_t *p,unsigned value)
{
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);
}

int portable_gp_recovery_lakitu_record(const PortableGpRecoveryLakituState *state,
    const uint8_t source_cells[20],uint8_t record[28])
{
    if(state==NULL||source_cells==NULL||record==NULL)return 0;
    memset(record,0,28u);
    if(!(state->visible&1u)||state->x < -64||state->x > 304||
       state->y < 0||state->y > 224)return 1;
    if(state->art_bank<19u||state->art_bank>20u||
       recovery_u16(source_cells)!=3u)return 0;
    record[0]=state->art_bank-19u;record[1]=3u;
    for(unsigned i=0u;i<3u;++i){
        const uint8_t *cell=source_cells+2u+i*6u;
        const unsigned a0=recovery_u16(cell),a1=recovery_u16(cell+2u);
        const unsigned rx=a1&511u,ry=a0&255u;
        const int sx=rx<=255u?(int)rx:-(int)(rx^511u);
        const int sy=ry<128u?(int)ry:(int)ry-256;
        /* The qualified ROM cell is two8x32 rope cells and one32x32
         * body. The source's negative X conversion differs from int9. */
        const unsigned half_x=i<2u?4u:16u,half_y=16u;
        if((a0>>14u)!=(i<2u?2u:0u)||
           (a1>>14u)!=(i<2u?1u:2u))return 0;
        const int cx=sx+(int)half_x,cy=sy+(int)half_y;
        const unsigned x=(unsigned)(state->x+(int)rx+cx*256/261-cx)&511u;
        const unsigned y=(unsigned)(state->y+(int)ry+cy*256/261-cy)&255u;
        recovery_store16(record+2u+i*6u,(a0&0xfc00u)|0x100u|y);
        recovery_store16(record+4u+i*6u,(a1&0xc000u)|x);
        recovery_store16(record+6u+i*6u,recovery_u16(cell+4u));
    }
    recovery_store16(record+20u,261u);recovery_store16(record+26u,261u);
    return 1;
}
