#ifndef PORTABLE_RECORDS_GHOST_ASSET_H
#define PORTABLE_RECORDS_GHOST_ASSET_H

#include "asset_pack.h"
#include <string.h>

enum { PORTABLE_RECORDS_GHOST_GROUPS=6, PORTABLE_RECORDS_GHOST_FRAMES=8 };
typedef struct
{
    uint32_t image_id;
    uint16_t duration;
} PortableRecordsGhostFrame;
typedef struct {
    PortableRecordsGhostFrame frames[PORTABLE_RECORDS_GHOST_FRAMES];
    uint8_t count;
} PortableRecordsGhostSchedule;
typedef struct {
    uint32_t header_base,local_counts[11],received_counts[3];
    PortableRecordsGhostSchedule groups[PORTABLE_RECORDS_GHOST_GROUPS];
} PortableRecordsGhostRules;

static inline uint32_t portable_records_ghost_u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static inline int portable_records_ghost_rules_open(const PortableAssetSectionView *section,
    PortableRecordsGhostRules *out)
{
    if(!section||!out||!section->bytes||section->version!=1u||section->size<100u)return 0;
    const uint8_t *p=section->bytes;
    if(portable_records_ghost_u32(p)!=UINT32_C(0x31484752)||
       portable_records_ghost_u32(p+4)!=1u||portable_records_ghost_u32(p+8)!=6u||
       portable_records_ghost_u32(p+12)!=0u)return 0;
    PortableRecordsGhostRules rules={0};
    rules.header_base=portable_records_ghost_u32(p+16);
    for (unsigned i = 0u; i < 11u; ++i)
        rules.local_counts[i] = portable_records_ghost_u32(p + 20u + 4u * i);
    for (unsigned i = 0u; i < 3u; ++i)
        rules.received_counts[i] = portable_records_ghost_u32(p + 64u + 4u * i);
    if(!rules.header_base)return 0;
    for (unsigned i = 0u; i < 11u; ++i)
        if (!rules.local_counts[i])
            return 0;
    for (unsigned i = 0u; i < 3u; ++i)
        if (!rules.received_counts[i])
            return 0;
    size_t offset=76u;
    for(unsigned group=0u;group<6u;++group){
        if(section->size-offset<4u)return 0;
        const uint32_t count=portable_records_ghost_u32(p+offset);offset+=4u;
        if(count==0u||count>8u||section->size-offset<count*8u)return 0;
        rules.groups[group].count=(uint8_t)count;
        for(unsigned i=0u;i<count;++i,offset+=8u){
            const uint32_t duration=portable_records_ghost_u32(p+offset+4u);
            rules.groups[group].frames[i]=(PortableRecordsGhostFrame){
                portable_records_ghost_u32(p+offset),(uint16_t)duration};
            if(!rules.groups[group].frames[i].image_id||duration==0u||duration>65535u)return 0;
        }
    }
    if(offset!=section->size)return 0;
    *out=rules;return 1;
}

/* Source increments before drawing; imported durations include the strict
 * greater-than boundary of D750's selected/unselected counters. */
static inline uint32_t portable_records_ghost_image(const PortableRecordsGhostSchedule *schedule,
    uint32_t age)
{
    if(!schedule||!schedule->count||schedule->count>8u)return 0u;
    uint32_t total=0u;
    for (unsigned i = 0u; i < schedule->count; ++i)
        total += schedule->frames[i].duration;
    if(!total)return 0u;
    age%=total;
    for(unsigned i=0u;i<schedule->count;++i){
        if(age<schedule->frames[i].duration)return schedule->frames[i].image_id;
        age-=schedule->frames[i].duration;
    }
    return 0u;
}

#endif
