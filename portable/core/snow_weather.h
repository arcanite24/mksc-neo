#ifndef PORTABLE_SNOW_WEATHER_H
#define PORTABLE_SNOW_WEATHER_H
#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_SNOW_WEATHER_PARTICLES=12 };
typedef struct {
    uint8_t enabled,count,backward[8],forward[8];
} PortableSnowWeatherRules;
typedef struct {
    int32_t x_q16,y_q16;
    int16_t stage;
    uint16_t active,phase,flags;
    int32_t travel_q16;
} PortableSnowWeatherParticle;
typedef struct
{
    uint8_t x, y, art, visible;
    int32_t x_q16, y_q16;
    uint8_t active;
} PortableSnowWeatherSprite;
typedef struct
{
    PortableSnowWeatherSprite particles[PORTABLE_SNOW_WEATHER_PARTICLES];
    uint8_t count, parity, enabled, precise;
} PortableSnowWeatherSnapshot;
typedef struct
{
    PortableSnowWeatherParticle particles[PORTABLE_SNOW_WEATHER_PARTICLES];
    int32_t prior_camera_x_q16,prior_camera_y_q16;
    int16_t prior_yaw;
    uint8_t initialized,enabled;
    PortableSnowWeatherSnapshot snapshot;
} PortableSnowWeatherState;
typedef struct {
    int32_t camera_x_q16,camera_y_q16;
    uint16_t yaw;
    uint8_t paused,parity;
} PortableSnowWeatherInputs;
typedef uint32_t (*PortableSnowWeatherRandom)(void *context);

static inline int portable_snow_weather_rules_valid(const PortableSnowWeatherRules *r)
{
    if(!r || r->enabled>1u || r->count>12u || (!!r->count)!=r->enabled)return 0;
    for(unsigned i=0;i<8u;++i)if(r->backward[i]>3u || r->forward[i]>3u)return 0;
    return 1;
}
static inline int32_t portable_snow_weather_i32(uint32_t v)
{ return v<=INT32_MAX?(int32_t)v:(int32_t)((int64_t)v-INT64_C(0x100000000)); }
static inline int16_t portable_snow_weather_i16(uint32_t v)
{ return (int16_t)((v&65535u)<=32767u?(int32_t)(v&65535u):(int32_t)(v&65535u)-65536); }
static inline int32_t portable_snow_weather_add(int32_t a,int32_t b)
{ return portable_snow_weather_i32((uint32_t)a+(uint32_t)b); }
static inline int32_t portable_snow_weather_sub(int32_t a,int32_t b)
{ return portable_snow_weather_i32((uint32_t)a-(uint32_t)b); }
static inline int32_t portable_snow_weather_mul(int32_t a,int32_t b)
{ return portable_snow_weather_i32((uint32_t)a*(uint32_t)b); }
static inline int32_t portable_snow_weather_asr(int32_t a,unsigned shift)
{
    const int64_t divisor=INT64_C(1)<<shift;
    return (int32_t)(a>=0?(int64_t)a/divisor:-((-(int64_t)a+divisor-1)/divisor));
}
static inline void portable_snow_weather_snapshot(PortableSnowWeatherState *s,
                                                  const PortableSnowWeatherRules *r, uint8_t parity)
{
    s->snapshot = (PortableSnowWeatherSnapshot){
        .count = r->count, .parity = (uint8_t)(parity & 1u), .enabled = s->enabled, .precise = 1u};
    for (unsigned i = 0; i < r->count; ++i)
    {
        const PortableSnowWeatherParticle *p = &s->particles[i];
        const uint8_t x = (uint8_t)((uint32_t)p->x_q16 >> 16u);
        const uint8_t y = (uint8_t)((uint32_t)p->y_q16 >> 16u);
        /* Keep source byte wrapping while retaining the fractions discarded
         * by its draw submission. The 1x renderer still uses x/y above. */
        s->snapshot.particles[i] = (PortableSnowWeatherSprite){
            .x = x,
            .y = y,
            .x_q16 = (int32_t)x * 65536 + (int32_t)((uint32_t)p->x_q16 & 65535u),
            .y_q16 = (y >= 160u ? (int32_t)y - 256 : (int32_t)y) * 65536 +
                     (int32_t)((uint32_t)p->y_q16 & 65535u),
            .art = (uint8_t)(p->stage == 2 ? 1 : (p->stage >= 0 && p->stage < 2 ? 0 : 2)),
            .active = (uint8_t)(s->enabled && p->active == 1u && (p->flags & 1u)),
            .visible = (uint8_t)(s->enabled && (p->flags & 1u) && (i & 1u) == (parity & 1u))};
    }
}
/* 365B0: fresh staggered delays; initialization never consumes shared RNG. */
static inline int portable_snow_weather_init(PortableSnowWeatherState *s,
                                             const PortableSnowWeatherRules *r, int32_t x,
                                             int32_t y, uint16_t yaw, int mode_enabled)
{
    if(!s || (r && !portable_snow_weather_rules_valid(r)))return -1;
    *s=(PortableSnowWeatherState){.prior_camera_x_q16=x,.prior_camera_y_q16=y,
        .prior_yaw=portable_snow_weather_i16(yaw),.initialized=1,
        .enabled=(uint8_t)(r && r->enabled && mode_enabled)};
    if(!r)return 0;
    for(unsigned i=0;i<r->count;++i){s->particles[i].y_q16=-16*65536;
        s->particles[i].phase=(uint16_t)i;}
    portable_snow_weather_snapshot(s,r,0);
    return s->enabled?1:0;
}
static inline void portable_snow_weather_radial(PortableSnowWeatherParticle *p,
    int32_t forward,int set_stage)
{
    if(p->y_q16>80*65536){
        const int32_t rows=portable_snow_weather_asr(
            portable_snow_weather_sub(p->y_q16,60*65536),16);
        p->y_q16=portable_snow_weather_add(p->y_q16,
            portable_snow_weather_asr(portable_snow_weather_mul(rows,forward),6));
    }
    if(p->y_q16>70*65536){
        const int32_t dy=portable_snow_weather_sub(p->y_q16,32*65536)/32768;
        const int32_t dx=portable_snow_weather_sub(p->x_q16,120*65536)/65536;
        if(set_stage)p->stage=3;
        const int32_t correction=portable_snow_weather_mul(
            portable_snow_weather_mul(dx,forward)/32,dy)/256;
        p->x_q16=portable_snow_weather_add(p->x_q16,correction);
    }
}
/* 3670C runs after course/item owners, before camera acceptance. The caller
 * supplies the prior accepted camera and the shared main_getRandom owner
 * (source030001b8), distinct from the race/item frame-burn RNG at03002344.
 * Parity is fixed-tick presentation policy; pause freezes the whole snapshot. */
static inline int portable_snow_weather_tick(PortableSnowWeatherState *s,
    const PortableSnowWeatherRules *r,const PortableSnowWeatherInputs *in,
    const int32_t *sine,size_t sine_count,PortableSnowWeatherRandom random,void *context)
{
    if(!s || !in || !s->initialized || (r && !portable_snow_weather_rules_valid(r)))return -1;
    if(!r || !r->enabled || !s->enabled)return 0;
    if(in->paused)return 1;
    if(!sine || sine_count<1024u || !random)return -1;
    const int32_t yaw=portable_snow_weather_i16(in->yaw);
    const int32_t angle=portable_snow_weather_i16((uint32_t)(yaw-16384));
    const unsigned slot=(unsigned)(angle/64)&1023u;
    const int32_t sn=sine[slot],cs=sine[(256u-slot)&1023u];
    const int32_t dx=portable_snow_weather_sub(in->camera_x_q16,s->prior_camera_x_q16)/32768;
    const int32_t dy=portable_snow_weather_sub(in->camera_y_q16,s->prior_camera_y_q16)/32768;
    const int32_t forward=portable_snow_weather_add(portable_snow_weather_mul(sn,dy),
        portable_snow_weather_mul(cs,dx));
    const int32_t lateral=portable_snow_weather_add(portable_snow_weather_mul(cs,dy/4),
        portable_snow_weather_mul(sn,dx/4));
    for(unsigned i=0;i<r->count;++i){
        PortableSnowWeatherParticle *p=&s->particles[i];
        if(p->active==1u){
            const int stage=p->stage;
            const unsigned depth=(stage>=0 && stage<2)?0u:(stage==2?1u:2u);
            const uint16_t old_phase=p->phase;
            p->phase=(uint16_t)(p->phase+(depth==0u?256u:(depth==1u?320u:384u)));
            const int32_t wave=portable_snow_weather_sub(
                sine[(unsigned)(portable_snow_weather_i16(p->phase)/64)&1023u],
                sine[(unsigned)(portable_snow_weather_i16(old_phase)/64)&1023u]);
            p->x_q16=portable_snow_weather_add(p->x_q16,
                portable_snow_weather_mul(s->prior_yaw-yaw,1024));
            const int32_t fall=depth==0u?(forward>0?0x3000:0x4800):
                (depth==1u?(forward>0?0x6800:0x9000):(forward>0?0xc000:0x10000));
            const int32_t wobble=depth==0u?(forward>0?8:16):
                (depth==1u?(forward>0?16:32):(forward>0?24:48));
            p->y_q16=portable_snow_weather_add(p->y_q16,fall);
            p->x_q16=portable_snow_weather_add(p->x_q16,portable_snow_weather_mul(wave,wobble));
            p->x_q16=portable_snow_weather_add(p->x_q16,lateral/(depth==0u?4:(depth==1u?2:1)));
            if(depth==1u || (depth==2u && forward>0))portable_snow_weather_radial(p,forward,depth==1u);
            if(depth<2u){
                const int32_t span=(depth==0u?72:80)*65536;
                p->travel_q16=portable_snow_weather_add(p->travel_q16,forward);
                if(p->travel_q16>=span){
                    p->travel_q16=portable_snow_weather_sub(p->travel_q16,span);
                    p->stage=portable_snow_weather_i16((uint32_t)p->stage+1u);
                }
            }
            if((uint32_t)p->x_q16>248u*65536u){p->active=0;p->flags&=65534u;
                p->phase=(uint16_t)(random(context)&31u);}
            if(p->y_q16>(depth==0u?84:(depth==1u?110:160))*65536){
                p->active=0;p->flags&=65534u;p->phase=(uint16_t)((random(context)&63u)+20u);}
        }else{
            p->phase=(uint16_t)(p->phase-1u);
            if(portable_snow_weather_i16(p->phase)<0){
                if(i>9u && (forward || lateral)){p->phase=(uint16_t)(random(context)&31u);}
                else{
                    const uint8_t *choices=forward<0?r->backward:r->forward;
                    p->stage=choices[random(context)&7u];p->flags=1;p->active=1;
                    p->phase=(uint16_t)random(context);p->travel_q16=0;
                    p->x_q16=(int32_t)(random(context)&63u)*5*65536;
                    const uint32_t roll=random(context);
                    const int32_t y=p->stage<2?(int32_t)(roll&31u)+8:
                        (p->stage==2?(int32_t)(roll&31u)-8:(int32_t)(roll&15u)-8);
                    p->y_q16=y*65536;
                }
            }
        }
    }
    s->prior_camera_x_q16=in->camera_x_q16;s->prior_camera_y_q16=in->camera_y_q16;
    s->prior_yaw=(int16_t)yaw;
    portable_snow_weather_snapshot(s,r,in->parity);
    return 1;
}
static inline uint64_t portable_snow_weather_digest(const PortableSnowWeatherState *s)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    if(!s)return hash;
    /* Hash typed values explicitly; never hash padding or host byte order. */
    uint32_t words[12*7+7];unsigned n=0;
    for(unsigned i=0;i<12u;++i){const PortableSnowWeatherParticle *p=&s->particles[i];
        words[n++]=(uint32_t)p->x_q16;words[n++]=(uint32_t)p->y_q16;
        words[n++]=(uint16_t)p->stage;words[n++]=p->active;words[n++]=p->phase;
        words[n++]=p->flags;words[n++]=(uint32_t)p->travel_q16;}
    words[n++]=(uint32_t)s->prior_camera_x_q16;words[n++]=(uint32_t)s->prior_camera_y_q16;
    words[n++]=(uint16_t)s->prior_yaw;words[n++]=s->initialized;words[n++]=s->enabled;
    words[n++]=s->snapshot.count;words[n++]=s->snapshot.parity;
    for(unsigned i=0;i<n;++i)for(unsigned b=0;b<4u;++b){hash^=(words[i]>>(b*8u))&255u;
        hash*=UINT64_C(1099511628211);}
    return hash;
}
#endif
