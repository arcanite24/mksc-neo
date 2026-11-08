#include "boo.h"
#include "driver_palette.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"boo line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    int32_t sine[1024];for(size_t i=0;i<1024u;++i)sine[i]=(int32_t)i;
    PortableBoo a,b;
    CHECK(!portable_boo_init(&a,0,0,0u,2u));
    CHECK(portable_boo_init(&a,100*65536,200*65536,2u,0u));
    CHECK(portable_boo_init(&b,100*65536,200*65536,2u,1u) && !memcmp(&a,&b,sizeof a));
    for(uint32_t tick=0;tick<128u;++tick){
        CHECK(portable_boo_advance(&a,sine,1024u));
        CHECK(a.state==17u && a.phase==(tick+1u)*1024u);
        CHECK(a.height_q16==0x80000+(int32_t)(tick%64u)*16*3);
    }
    a.phase=0xffff8001u;
    CHECK(portable_boo_advance(&a,sine,1024u) && a.phase==0xffff8401u && a.height_q16==0x80000+513*3);
    a.phase=0xfffffc00u;CHECK(portable_boo_advance(&a,sine,1024u) && a.phase==0u);
    CHECK(portable_boo_view_art(&a,0x4000u)==0u && portable_boo_view_art(&a,0xc000u)==1u);
    CHECK(portable_boo_view_art(&a,0xc001u)==0u && portable_boo_view_art(&a,0x4001u)==1u);
    a.state=240u;a.height_q16=0;
    CHECK(portable_boo_advance(&a,sine,1024u) && a.state==241u && a.flags==1u &&
          a.height_q16==0x30000 && a.velocity_z_q16==0x2c000 && a.affine_y_q8==-256);
    for(unsigned i=0;i<25u;++i)CHECK(portable_boo_advance(&a,sine,1024u));
    CHECK(a.state==243u && a.flags==0u && a.height_q16<0);
    b=a;CHECK(portable_boo_advance(&a,sine,1024u) && !memcmp(&a,&b,sizeof a));
    a.state=242u;a.flags=3u;CHECK(portable_boo_advance(&a,sine,1024u) && a.state==243u && a.flags==2u);
    a.state=18u;CHECK(!portable_boo_advance(&a,sine,1024u));
    CHECK(!portable_boo_advance(&a,sine,512u));
    PortableBooCourse course;CHECK(portable_boo_course_init(&course,&b,1u));
    CHECK(course.count==1u && course.initial==&b && !memcmp(&course.actors[0],&b,sizeof b));
    uint16_t requests=0xffffu;
    for(uint32_t clock=0;clock<16u;++clock){
        CHECK(portable_driver_palette_select(&requests,-1,0,0,0,clock)==1u+clock/4u);
        CHECK(requests==0xffffu);
    }
    CHECK(portable_driver_palette_select(&requests,0,296,0,0,0)==1u && requests==0xffffu);
    CHECK(portable_driver_palette_select(&requests,0,296,0,0,4)==9u && requests==0xf7ffu);
    CHECK(portable_driver_palette_select(&requests,0,0,0,0,0)==5u && requests==0x97ffu);
    CHECK(portable_driver_palette_select(&requests,0,0,0x50u,12u,0)==8u && requests==0x97ffu);
    CHECK(portable_driver_palette_select(&requests,0,0,0,0,0)==7u && requests==0x17ffu);
    CHECK(portable_driver_palette_select(&requests,0,0,0,0,0)==0u && requests==0x17ffu);
    requests=0x4000u;CHECK(portable_driver_palette_select(&requests,0,0,0,0,0)==6u && requests==0u);
    portable_driver_palette_boo_contact(0x1001u,&requests);CHECK(requests==0u);
    portable_driver_palette_boo_contact(0x2001u,&requests);CHECK(requests==0x8000u);
    return 0;
}
