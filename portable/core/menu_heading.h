#ifndef PORTABLE_MENU_HEADING_H
#define PORTABLE_MENU_HEADING_H
#include <stdint.h>
#define PORTABLE_MENU_HEADING_GROUPS 4u
#define PORTABLE_MENU_HEADING_LETTERS 16u
typedef struct { uint32_t image_id; int16_t x,y; } PortableMenuHeadingLetter;
typedef struct {
    uint8_t count;
    uint32_t strip_image_id,strip_digest;
    PortableMenuHeadingLetter letters[16];
} PortableMenuHeadingGroup;
typedef struct { PortableMenuHeadingGroup groups[4]; } PortableMenuHeadingRules;
typedef struct {
    int16_t x,y,target_x,target_y,origin_y,inverse_x,inverse_y;
    uint16_t ticks;
    uint8_t mode;
} PortableMenuHeadingCell;
typedef struct {
    PortableMenuHeadingCell cells[16];
    uint8_t group,count,initialized,exiting;
} PortableMenuHeading;
typedef struct {
    uint16_t inverse[4];
    uint8_t group,cursor,grow_tick,pulse,initialized;
} PortableMenuMedallion;
static inline int32_t portable_heading_signed(uint32_t v)
{ return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v); }
static inline int32_t portable_heading_asr(int64_t v,unsigned n)
{ const int64_t d=INT64_C(1)<<n;return (int32_t)(v>=0?v/d:-((-v+d-1)/d)); }
static inline int32_t portable_heading_sine(const int32_t sine[1024],int angle)
{ angle=(int)((unsigned)angle&65535u);if(angle>=32768)angle-=65536;return sine[(unsigned)(angle/64)&1023u]; }
static inline int portable_heading_curve(int start,int end,unsigned t,unsigned d,unsigned variant,const int32_t sine[1024])
{
    if(t>=d)return end;
    const unsigned divisor=variant==5u?15u:40u;
    int angle=(int)((t*t*131072u/(divisor*d))&65535u);if(angle>=32768)angle-=65536;
    int32_t wave=sine[(unsigned)(256-angle/64)&1023u];
    if(variant==5u){
        wave=portable_heading_asr(portable_heading_signed((uint32_t)wave*(uint32_t)(end-start)),15u);
        return end-portable_heading_signed((uint32_t)wave*(d-t)*(d-t))/(int)(d*d);
    }
    wave=portable_heading_signed((uint32_t)wave*(d-t)*(d-t))/(int)(d*d);if(wave<0)wave=-wave;
    return end-portable_heading_asr((int64_t)wave*(end-start),15u);
}
static inline int portable_menu_heading_start(PortableMenuHeading *state,const PortableMenuHeadingRules *rules,unsigned group,int settled)
{
    if(!state||!rules||group>=4u||!rules->groups[group].count||rules->groups[group].count>16u)return 0;
    state->group=(uint8_t)group;state->count=rules->groups[group].count;state->initialized=1u;state->exiting=0u;
    for(unsigned i=0;i<state->count;++i){
        const PortableMenuHeadingLetter *r=&rules->groups[group].letters[i];
        state->cells[i]=(PortableMenuHeadingCell){.x=(int16_t)(settled?r->x:256),.y=r->y,
            .target_x=r->x,.target_y=r->y,.origin_y=r->y,.inverse_x=256,.inverse_y=256,.mode=(uint8_t)(settled?13u:1u)};
    }
    return 1;
}
static inline void portable_menu_heading_exit(PortableMenuHeading *s)
{
    if(!s||!s->initialized||s->exiting)return;
    s->exiting=1u;
    for(unsigned i=0;i<s->count;++i){s->cells[i].mode=17u;s->cells[i].ticks=0u;}
}
/* Source3BB8: each state transition consumes its own callback. The source
 * sine table is caller-owned content; rendering never runs this owner. */
static inline int portable_menu_heading_tick(PortableMenuHeading *s,const int32_t sine[1024])
{
    if(!s||!sine||s->count>16u)return 0;
    if(!s->initialized)return 1;
    for(unsigned i=0;i<s->count;++i){
        PortableMenuHeadingCell *c=&s->cells[i];
        const unsigned t=c->mode==0u||c->mode==20u?c->ticks:++c->ticks;
        unsigned next=c->mode;
        switch(c->mode){
        case 0:case 20:break;
        case 1:case 6:case 11:if(t>3u*i)next=c->mode+1u;break;
        case 2:case 7:
            if(t<=12u)c->x=(int16_t)(256+portable_heading_asr((int64_t)((int)i*8-(c->mode==2u?248:192))*portable_heading_sine(sine,(int)(t*16384u/12u)),15u));
            else next=c->mode+1u;
            break;
        case 3:
            if(t<=8u)c->inverse_x=(int16_t)(256+((32768-sine[(256u-t*64u)&1023u])>>10u));
            else next=4u;
            break;
        case 4:case 9:if(t>3u*(s->count-1u-i))next=c->mode+1u;break;
        case 5:case 10:
            if(t<=16u){c->x=(int16_t)portable_heading_curve((int)i*8+(c->mode==5u?8:64),c->target_x,t,16u,5u,sine);
                if(c->mode==5u)c->inverse_x=(int16_t)(384-portable_heading_asr((int64_t)portable_heading_sine(sine,(int)t*1024)*128,15u));}
            else {c->inverse_x=256;next=13u;}
            break;
        case 8:if(t>8u)next=9u;break;
        case 12:if(t<=64u)c->y=(int16_t)portable_heading_curve(-40,c->target_y,t,64u,9u,sine);else next=13u;break;
        case 13:if(t>4u*i)next=14u;break;
        case 14:c->origin_y=c->y;next=15u;break;
        case 15:
            if(t<=8u){const int32_t product=(32768-sine[(256u-t*128u)&1023u])*-4;
                c->y=(int16_t)(c->origin_y+portable_heading_asr((int64_t)product+(product<0?1:0),16u));}
            else next=16u;
            break;
        case 16:if(t>300u+4u*(s->count-1u-i))next=13u;break;
        case 17:next=18u;break;
        case 18:if(t>3u*i)next=19u;break;
        case 19:
            if(t<=32u)c->x=(int16_t)(c->x+portable_heading_asr((int64_t)(-30-c->x)*(32768-sine[(256u-t*8u)&1023u]),15u));
            else next=20u;
            break;
        default:return 0;
        }
        if(next!=c->mode){c->mode=(uint8_t)next;c->ticks=0u;}
    }
    return 1;
}
/* 4DFE resolves the matrices before input retargets the cursor. */
static inline int portable_menu_medallion_tick(PortableMenuMedallion *s,unsigned group,unsigned cursor,const int32_t sine[1024])
{
    if(!s||!sine||cursor>3u||group>3u)return 0;
    if(!s->initialized||s->group!=group){
        *s=(PortableMenuMedallion){.group=(uint8_t)group,.cursor=(uint8_t)cursor,.initialized=1u};
    }
    for(unsigned i=0;i<4u;++i)s->inverse[i]=256u;
    int value;
    if(s->grow_tick<4u){
        const unsigned n=++s->grow_tick;
        int angle=(int)(n*8192u);if(angle>=32768)angle-=65536;
        const int32_t product=(32768-sine[(unsigned)(256-angle/64)&1023u])*-64;
        value=256+portable_heading_asr((int64_t)product+(product<0?1:0),16u);
    }else{
        s->pulse=(uint8_t)((s->pulse+1u)&31u);
        value=192+portable_heading_asr((int64_t)(32768-sine[(256u-s->pulse*32u)&1023u])*20,15u);
    }
    s->inverse[cursor]=(uint16_t)value;s->cursor=(uint8_t)cursor;
    return 1;
}
static inline void portable_menu_medallion_retarget(PortableMenuMedallion *s,unsigned cursor)
{ if(s&&s->initialized&&cursor!=s->cursor){s->cursor=(uint8_t)cursor;s->grow_tick=s->pulse=0u;} }
#endif
