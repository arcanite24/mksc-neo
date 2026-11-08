#include "gp_countdown_renderer.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"countdown failed: %d\n",__LINE__);return 1;} } while(0)
static void put32(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8u*i));}
static void put16(uint8_t *p,uint16_t v)
{p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);}
int main(void)
{
    uint8_t data[1024]={0};
    uint32_t pixels[240*160]={0};
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    PortableAssetPackView pack={.bytes=data,.size=sizeof data,.section_count=7u,
        .section_table_offset=112u};
    for(unsigned i=0;i<7;++i){
        uint8_t *entry=data+112u+40u*i,*image=data+400u+40u*i;
        put32(entry,15u);put32(entry+4u,0x27000u+i);put32(entry+8u,1u);
        put32(entry+16u,400u+40u*i);put32(entry+24u,36u);
        memcpy(image,"MKSCIMG1",8u);put16(image+8u,32u);put16(image+10u,1u);
        put16(image+12u,1u);put16(image+14u,1u);put32(image+24u,32u);
        put32(image+28u,36u);put32(image+32u,0xff000001u+i);
    }
    const uint16_t remaining[]={130u,120u,110u,60u,50u};
    for(unsigned i=0;i<5;++i){
        memset(pixels,0,sizeof pixels);
        CHECK(portable_gp_countdown_render(&pack,PORTABLE_RACE_PHASE_COUNTDOWN,
            180u,remaining[i],0u,&frame));
        CHECK(pixels[46u*240u+164u]==0xff000001u+i);
    }
    memset(pixels,0,sizeof pixels);
    CHECK(portable_gp_countdown_render(&pack,PORTABLE_RACE_PHASE_RACING,180u,0u,0u,&frame));
    CHECK(pixels[46u*240u+164u]==0xff000006u);
    CHECK(portable_gp_countdown_render(&pack,PORTABLE_RACE_PHASE_RACING,180u,0u,10u,&frame));
    CHECK(pixels[46u*240u+164u]==0xff000007u);
    memset(pixels,0,sizeof pixels);
    CHECK(portable_gp_countdown_render(&pack,PORTABLE_RACE_PHASE_RACING,180u,0u,80u,&frame));
    CHECK(pixels[46u*240u+164u]==0u);
    CHECK(!portable_gp_countdown_render(&pack,PORTABLE_RACE_PHASE_COUNTDOWN,180u,181u,0u,&frame));
    puts("native GP countdown: PASS");return 0;
}
