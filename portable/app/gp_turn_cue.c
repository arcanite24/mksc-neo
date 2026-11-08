#include "gp_turn_cue.h"

#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8u);
}

static void set_map(PortableGpTurnCue *state,const uint8_t *map)
{
    /* F6F4 preserves destination cells for zero source entries. */
    for(size_t i=0u;i<12u;++i){
        const uint16_t cell=le16(map+4u+i*2u);
        if(cell!=0u)state->pending_cells[i]=cell;
    }
}

int portable_gp_turn_cue_step(PortableGpTurnCue *s,
    const uint8_t *entries,size_t count,const uint8_t maps[84],
    uint8_t route,int32_t lap,uint32_t flags,uint8_t ordinary_lakitu,
    uint16_t checkpoint_count,uint8_t final_lap)
{
    if(s==NULL||entries==NULL||maps==NULL||count==0u||count>256u||
       s->active>1u||s->hidden>1u||s->glyph>=8u||
       s->offset%4u!=0u||s->offset/4u>=count||
       lap<0||lap>127||final_lap>127u||ordinary_lakitu>1u||
       s->elapsed>1021u||s->blink>11u||s->cycles>1021u||
       s->origin<0||s->origin>255||s->lap<0||s->lap>127)return 0;
    for(size_t i=0u;i<3u;++i)
        if(le16(maps+i*28u)!=4u||le16(maps+i*28u+2u)!=3u)return 0;
    for(size_t i=0u;i<count;++i)
        if((entries[i*4u+2u]&127u)>=8u||
           (entries[i*4u+3u]&15u)>2u)return 0;
    memcpy(s->displayed_cells,s->pending_cells,sizeof s->pending_cells);
    s->displayed_glyph=s->glyph;
    if(route!=s->previous){
        s->previous=route;
        if((flags&UINT32_C(0x420))==0u){
            for(size_t i=0u;i<count;++i){
                const uint8_t *entry=entries+i*4u;
                if(entry[0]!=route)continue;
                s->active=1u;s->offset=(uint32_t)i*4u;s->elapsed=0u;
                s->origin=route;s->lap=lap;s->hidden=0u;
                s->blink=0u;s->cycles=0u;s->glyph=entry[2]&127u;
                set_map(s,maps+((entry[2]&128u)!=0u?28u:0u));
                break;
            }
        }
    }
    if(!s->active)return 1;
    const uint8_t *entry=entries+s->offset;
    const uint32_t duration=(uint32_t)entry[1]*4u;
    const uint8_t graphic=entry[2],style=entry[3];
    if((lap==final_lap&&(style&128u)!=0u)||!ordinary_lakitu)
        s->elapsed=duration;
    if(s->elapsed<duration){
        static const uint8_t visible[3]={6u,3u,10u};
        static const uint8_t hidden[3]={2u,1u,2u};
        const unsigned pattern=style&15u;
        if(!s->hidden&&s->blink>visible[pattern]){
            s->blink=0u;s->hidden=1u;set_map(s,maps+56u);
        }else if(s->hidden&&s->blink>hidden[pattern]){
            s->blink=0u;s->hidden=0u;
            set_map(s,maps+((graphic&128u)!=0u?28u:0u));
            ++s->cycles;
        }
        if(pattern!=1u||s->cycles<=2u)++s->blink;
    }else{
        set_map(s,maps+56u);s->active=0u;
    }
    if((style&64u)==0u)++s->elapsed;
    else{
        int32_t distance=(int32_t)route-s->origin;
        if(lap>s->lap)distance+=checkpoint_count;
        if(distance<0)distance=-distance;
        if(distance>(int32_t)((style&0x30u)>>4u))s->elapsed=duration;
    }
    return 1;
}
