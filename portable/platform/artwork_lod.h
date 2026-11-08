#ifndef PORTABLE_ARTWORK_LOD_H
#define PORTABLE_ARTWORK_LOD_H

#include <stddef.h>
#include <stdint.h>

/* Sizes describe authored textures, not animation frames. Geometry always
 * remains with the source cell; this chooses only its normalized sampler. */
static inline size_t portable_artwork_lod_select(uint8_t policy,
    const unsigned *sizes, size_t count, size_t source, unsigned footprint)
{
    const int source_valid=source<count && sizes[source]!=0u;
    size_t highest=source_valid?source:count;
    unsigned target=source_valid?sizes[source]:0u;
    if(footprint>target)target=footprint;
    /* Equal extents carry no extra detail: retain the source image rather
     * than changing art merely because another bank occurs first. */
    size_t selected=source_valid && sizes[source]>=target?source:count;
    for(size_t i=0;i<count;++i){
        if(!sizes[i])continue;
        if(highest==count || sizes[i]>sizes[highest])highest=i;
        if(sizes[i]>=target && (selected==count || sizes[i]<sizes[selected]))selected=i;
    }
    if(!policy && source<count && sizes[source])return source;
    if(policy==2u || selected==count)return highest;
    if(policy==1u){
        size_t next=count;
        for(size_t i=0;i<count;++i)
            if(sizes[i]>sizes[selected] && (next==count || sizes[i]<sizes[next]))next=i;
        if(next!=count)selected=next;
    }
    return selected;
}

#endif
