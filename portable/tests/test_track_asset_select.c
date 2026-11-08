#include "track_asset_select.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static void put(uint8_t *p, uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8));
}
int main(void) {
    uint8_t container[196+49*3], owned[3], saved[3];
    uint32_t indices[]={UINT32_MAX,25,24,25};
    for(unsigned i=0;i<49;i++) {
        put(container+i*4,196+i*3);
        memset(container+196+i*3,(int)i,3);
    }
    PortableTrackAsset out={0}, old;
    for(unsigned h=1;h<4;h++) {
        assert(portable_track_asset_select(indices,4,h,container,sizeof container,owned,3,&out)==0);
        assert(out.header_index==h && out.course_index==indices[h]);
        assert(out.source_offset==196+indices[h]*3 && out.size==3);
        for(unsigned i=0;i<3;i++) assert(owned[i]==indices[h]);
    }
    memcpy(saved,owned,3);old=out;
    for(unsigned fault=0;fault<6;fault++) {
        const uint32_t *map=indices; size_t count=4,capacity=3,size=sizeof container;
        uint32_t header=1;
        switch(fault) {
        case 0: map=NULL; break;
        case 1: header=0; break;
        case 2: header=4; break;
        case 3: count=257; break;
        case 4: capacity=2; break;
        case 5: size=10; break;
        }
        assert(portable_track_asset_select(map,count,header,container,size,owned,capacity,&out)!=0);
        assert(!memcmp(&out,&old,sizeof out) && !memcmp(owned,saved,3));
    }
    put(container+48*4,195); /* Full directory is validated before exposing selected slice. */
    assert(portable_track_asset_select(indices,4,1,container,sizeof container,owned,3,&out)!=0);
    assert(!memcmp(&out,&old,sizeof out) && !memcmp(owned,saved,3));
    return 0;
}
