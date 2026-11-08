#include "frontend_config.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"catalog check failed: %d\n",__LINE__); return 1; } } while (0)

static void put32(uint8_t *p,uint32_t value)
{
    for(unsigned byte=0u;byte<4u;++byte)p[byte]=(uint8_t)(value>>(byte*8u));
}

int main(void)
{
    /* A decoded pack view isolates catalog validation from pack hashing. */
    uint8_t bytes[512]={0};
    PortableAssetPackView pack={.bytes=bytes,.size=sizeof bytes,
        .section_count=4u,.section_table_offset=112u};
    const uint32_t kinds[4]={2u,1u,1u,37u},ids[4]={1u,42u,17u,1u};
    for(unsigned index=0u;index<4u;++index){
        uint8_t *entry=bytes+112u+index*40u;
        put32(entry,kinds[index]);put32(entry+4u,ids[index]);put32(entry+8u,1u);
        put32(entry+16u,320u);put32(entry+24u,24u);
    }
    uint8_t *catalog=bytes+320u;
    put32(catalog,UINT32_C(0x31544143));put32(catalog+4u,2u);
    put32(catalog+8u,42u);put32(catalog+12u,4u|(3u<<8u)|(25u<<16u));
    put32(catalog+16u,17u);put32(catalog+20u,21u<<16u);
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    uint16_t song=0u;
    CHECK(build_frontend_config(&pack,drivers,courses,&config));
    CHECK(config.course_count==2u&&courses[0].content_id==42u&&
          courses[0].cup==4u&&courses[0].track==3u&&courses[1].content_id==17u);
    CHECK(portable_frontend_course_song(&pack,42u,&song)&&song==25u);
    CHECK(portable_frontend_course_song(&pack,17u,&song)&&song==21u);
    CHECK(!portable_frontend_course_song(&pack,99u,&song));
    put32(catalog+20u,4u|(3u<<8u));
    CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* duplicate slot */
    put32(catalog+20u,5u);
    CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* invalid cup */
    put32(catalog+20u,0u);put32(catalog+16u,99u);
    CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* missing course */
    put32(catalog+16u,42u);
    CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* duplicate ID */
    put32(catalog+16u,17u);put32(catalog+4u,3u);
    CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* truncated rows */
    CHECK(!portable_frontend_course_song(&pack,42u,&song));
    pack.section_count=3u;
    CHECK(build_frontend_config(&pack,drivers,courses,&config));
    CHECK(config.course_count==2u&&courses[0].content_id==17u&&
          courses[1].content_id==42u&&courses[1].track==1u);
    CHECK(!portable_frontend_course_song(&pack,42u,&song));
    /* Version 2 supplies independent source-header keys in a shuffled cup. */
    uint8_t full[1024]={0};
    pack=(PortableAssetPackView){.bytes=full,.size=sizeof full,
        .section_count=6u,.section_table_offset=112u};
    for(unsigned index=0u;index<6u;++index){
        uint8_t *entry=full+112u+index*40u;
        put32(entry,index==0u?2u:index==5u?37u:1u);
        put32(entry+4u,index==0u||index==5u?1u:100u+index);
        put32(entry+8u,index==5u?2u:1u);
        put32(entry+16u,512u);put32(entry+24u,56u);
    }
    catalog=full+512u;
    put32(catalog,UINT32_C(0x31544143));put32(catalog+4u,4u);
    for(unsigned index=0u;index<4u;++index){
        uint8_t *row=catalog+8u+index*12u;
        put32(row,101u+index);put32(row+4u,3u|((3u-index)<<8u)|(21u<<16u));
        put32(row+8u,7u+index);
    }
    PortableGpCupDefinition definition;
    CHECK(build_frontend_config(&pack,drivers,courses,&config));
    CHECK(portable_frontend_course_song(&pack,104u,&song)&&song==21u);
    CHECK(portable_frontend_gp_cup(&pack,3u,&definition));
    CHECK(definition.cup_index==3u&&definition.course_ids[0]==104u&&
          definition.course_ids[3]==101u&&definition.source_headers[0]==10u);
    CHECK(!portable_frontend_gp_cup(&pack,0u,&definition));
    put32(catalog+16u,10u);
    CHECK(!portable_frontend_gp_cup(&pack,3u,&definition)); /* duplicate source key */
    put32(catalog+16u,256u);
    CHECK(!build_frontend_config(&pack,drivers,courses,&config));
    put32(catalog+16u,7u);put32(catalog+4u,3u);
    CHECK(!portable_frontend_gp_cup(&pack,3u,&definition)); /* incomplete/truncated */
    /* Both catalogs feed native menus; GP still reads only the base catalog. */
    put32(catalog+4u,4u);put32(catalog+16u,7u);
    pack.section_count=8u;
    uint8_t *extra_manifest=full+112u+7u*40u;
    put32(extra_manifest,1u);put32(extra_manifest+4u,64u);
    put32(extra_manifest+8u,1u);put32(extra_manifest+16u,700u);put32(extra_manifest+24u,4u);
    uint8_t *extra_entry=full+112u+6u*40u;
    put32(extra_entry,37u);put32(extra_entry+4u,2u);put32(extra_entry+8u,2u);
    put32(extra_entry+16u,640u);put32(extra_entry+24u,20u);
    put32(full+640u,UINT32_C(0x31544143));put32(full+644u,1u);
    put32(full+648u,64u);put32(full+652u,41u<<16u);put32(full+656u,32u);
    CHECK(portable_frontend_course_song(&pack,64u,&song)&&song==41u);
    CHECK(portable_frontend_course_song(&pack,104u,&song)&&song==21u);
    CHECK(build_frontend_config(&pack,drivers,courses,&config)&&config.course_count==5u&&
          courses[4].cup==5u&&courses[4].content_id==64u);
    CHECK(portable_frontend_gp_cup(&pack,3u,&definition)&&definition.course_ids[0]==104u);
    put32(full+656u,52u);
    CHECK(!build_frontend_config(&pack,drivers,courses,&config));
    put32(full+656u,32u);
    put32(extra_entry+24u,19u);
    CHECK(!portable_frontend_course_song(&pack,64u,&song));
    /* The arena catalog shares manifests/music but never GP cup membership. */
    put32(extra_entry+4u,3u);put32(extra_entry+24u,20u);
    put32(full+652u,(3u<<8u)|(41u<<16u));put32(full+656u,27u);
    CHECK(build_frontend_config(&pack,drivers,courses,&config) && config.course_count==5u &&
        courses[4].cup==PORTABLE_FRONTEND_ARENA_CUP && courses[4].track==3u);
    CHECK(portable_frontend_course_song(&pack,64u,&song) && song==41u);
    CHECK(!portable_frontend_gp_cup(&pack,PORTABLE_FRONTEND_ARENA_CUP,&definition));
    put32(full+652u,1u|(41u<<16u));
    CHECK(!build_frontend_config(&pack,drivers,courses,&config));
    /* Catalog 4 adds custom cups 11..15 from mod manifests beyond the stock
     * 64-manifest window; IDs stay byte-sized for Time Trial ghosts. */
    {
        static uint8_t custom[8192];
        enum { entries=82u, data=4096u };
        memset(custom,0,sizeof custom);
        pack=(PortableAssetPackView){.bytes=custom,.size=sizeof custom,
            .section_count=entries,.section_table_offset=112u};
        for(unsigned index=0u;index<entries;++index){
            uint8_t *entry=custom+112u+index*40u;
            uint32_t kind=1u,id=0u,version=1u,offset=data,size=4u;
            if(index==0u){kind=2u;id=1u;}
            else if(index<=4u)id=100u+index;
            else if(index==5u){kind=37u;id=1u;version=2u;offset=data+64u;size=56u;}
            else if(index<=9u)id=122u+index;
            else if(index==10u){kind=37u;id=4u;version=2u;offset=data+128u;size=56u;}
            else id=index==81u?300u:129u+index;
            put32(entry,kind);put32(entry+4u,id);put32(entry+8u,version);
            put32(entry+16u,offset);put32(entry+24u,size);
        }
        uint8_t *base=custom+data+64u,*mod=custom+data+128u;
        put32(base,UINT32_C(0x31544143));put32(base+4u,4u);
        put32(mod,UINT32_C(0x31544143));put32(mod+4u,4u);
        for(unsigned index=0u;index<4u;++index){
            put32(base+8u+index*12u,101u+index);put32(base+12u+index*12u,(index<<8u)|(21u<<16u));
            put32(base+16u+index*12u,4u+index);
            put32(mod+8u+index*12u,128u+index);put32(mod+12u+index*12u,2u|(index<<8u)|(30u<<16u));
            put32(mod+16u+index*12u,index==3u?255u:200u+index);
        }
        CHECK(build_frontend_config(&pack,drivers,courses,&config)&&config.course_count==8u);
        CHECK(courses[4].content_id==128u&&courses[4].cup==PORTABLE_FRONTEND_CUSTOM_CUP+2u&&
              courses[7].content_id==131u&&courses[7].track==3u);
        CHECK(portable_track_set_of_cup(courses[4].cup)==PORTABLE_TRACK_SET_CUSTOM);
        CHECK(portable_frontend_course_song(&pack,130u,&song)&&song==30u);
        CHECK(portable_frontend_gp_cup(&pack,PORTABLE_FRONTEND_CUSTOM_CUP+2u,&definition));
        CHECK(definition.track_set==PORTABLE_TRACK_SET_CUSTOM&&definition.cup_index==2u&&
              definition.course_ids[0]==128u&&definition.course_ids[3]==131u&&
              definition.source_headers[3]==255u);
        CHECK(!portable_frontend_gp_cup(&pack,PORTABLE_FRONTEND_CUSTOM_CUP,&definition));
        CHECK(!portable_frontend_gp_cup(&pack,PORTABLE_FRONTEND_ARENA_CUP,&definition));
        CHECK(!portable_frontend_gp_cup(&pack,PORTABLE_FRONTEND_COURSE_CUPS,&definition));
        CHECK(portable_frontend_gp_cup(&pack,0u,&definition)&&definition.track_set==PORTABLE_TRACK_SET_GBA);
        put32(mod+8u,300u);
        CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* ghost IDs are bytes */
        put32(mod+8u,128u);put32(mod+16u,256u);
        CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* byte source keys */
        put32(mod+16u,200u);put32(mod+12u,5u|(30u<<16u));
        CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* five custom cups */
        put32(mod+12u,2u|(30u<<16u));put32(mod+8u,101u);
        CHECK(!build_frontend_config(&pack,drivers,courses,&config)); /* one slot per course */
        put32(mod+8u,128u);
        pack.section_count=10u;
        CHECK(build_frontend_config(&pack,drivers,courses,&config)&&config.course_count==4u);
        CHECK(!portable_frontend_gp_cup(&pack,PORTABLE_FRONTEND_CUSTOM_CUP+2u,&definition));
    }
    puts("portable frontend catalog: PASS");
    return 0;
}
