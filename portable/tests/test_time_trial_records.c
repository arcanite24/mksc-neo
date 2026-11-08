#include "time_trial_records.h"
#include "course_menu_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include "test_temp.h"

#define CHECK(x) do{if(!(x)){fprintf(stderr,"records failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)

static void put32(uint8_t *bytes,uint32_t value)
{ for(unsigned i=0u;i<4u;++i)bytes[i]=(uint8_t)(value>>(8u*i)); }

static int ghost_rules_and_pixels(void)
{
    uint8_t payload[196]={0};
    put32(payload,UINT32_C(0x31484752));put32(payload+4u,1u);put32(payload+8u,6u);
    for(unsigned i=0u;i<15u;++i)put32(payload+16u+4u*i,500u+i);
    size_t offset=76u;
    for(unsigned group=0u;group<6u;++group){
        put32(payload+offset,2u);offset+=4u;
        for(unsigned frame=0u;frame<2u;++frame,offset+=8u){
            put32(payload+offset,515u+group*2u+frame);
            put32(payload+offset+4u,frame?5u:3u);
        }
    }
    PortableAssetSectionView section={.version=1u,.bytes=payload,.size=sizeof payload};
    PortableRecordsGhostRules rules;
    CHECK(portable_records_ghost_rules_open(&section,&rules));
    const uint8_t *valid_bytes=section.bytes;section.bytes=NULL;
    CHECK(!portable_records_ghost_rules_open(&section,&rules));section.bytes=valid_bytes;
    CHECK(portable_records_ghost_image(&rules.groups[0],2u)==515u);
    CHECK(portable_records_ghost_image(&rules.groups[0],3u)==516u);
    CHECK(portable_records_ghost_image(&rules.groups[0],7u)==516u);
    CHECK(portable_records_ghost_image(&rules.groups[0],8u)==515u);
    payload[86]=1u;CHECK(!portable_records_ghost_rules_open(&section,&rules));payload[86]=0u;
    CHECK(portable_records_ghost_rules_open(&section,&rules));
    enum { IMAGES=30 };
    uint8_t images[IMAGES][36];PortableAssetSectionView sections[IMAGES];
    for(unsigned i=0u;i<IMAGES;++i){
        uint8_t *image=images[i];memset(image,0,36u);memcpy(image,"MKSCIMG1",8u);
        image[8]=32u;image[10]=1u;image[12]=image[14]=1u;
        put32(image+24u,32u);put32(image+28u,36u);
        put32(image+32u,UINT32_C(0xff010000)+i);
        sections[i]=(PortableAssetSectionView){.kind=15u,.content_id=i<27u?500u+i:
            i==27u?PORTABLE_RECORDS_BEST:i==28u?PORTABLE_RECORDS_LAP:PORTABLE_RECORDS_GLYPHS,
            .version=1u,.bytes=image,.size=36u};
    }
    /* Authored header anchors are relative to its source origin, not a second
     * layout inferred from the image's visible bounding box. */
    for(unsigned i=1u;i<=11u;++i){images[i][16]=235u;images[i][17]=255u;images[i][18]=252u;images[i][19]=255u;}
    for(unsigned i=12u;i<15u;++i){images[i][16]=1u;images[i][18]=252u;images[i][19]=255u;}
    const PortableAssetPackView pack={.resolved_sections=sections,.section_count=IMAGES};
    const PortableFrontendCourseSlot courses[]={{128u,11u,0u,1u},{191u,11u,1u,1u},
        {129u,11u,2u,1u},{130u,11u,3u,1u}};
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_RECORDS,.course_phase=2u,
        .mode_cursor=1u,.course_cup=11u,.selected_course=128u,.page_ticks=3u,
        .cup_access={15u,15u,15u},.config={.courses=courses,.course_count=4u}};
    uint8_t listed[PORTABLE_FRONTEND_RECORDS_MAX];
    CHECK(portable_frontend_flow_records_list(&flow,listed)==4u&&listed[0]==44u&&listed[1]==45u);
    PortableTimeTrialRecords records={.course_id=128u,.ghost_rules=rules,.ghost_rules_ready=1u,
        .local_ghosts={.used=1u,.courses={128u}},.received_used=1u,
        .received={{.status=PORTABLE_TIME_TRIAL_GHOST_STORE_OK,.identity={.course_id=191u}}}};
    const PortableTimeTrialRecords saved=records;const PortableFrontendFlow saved_flow=flow;
    for(unsigned scale=1u;scale<=4u;scale*=2u){
        const size_t stride=240u*scale+3u,height=160u*scale;
        uint32_t *pixels=malloc(stride*height*sizeof *pixels);CHECK(pixels!=NULL);
        for(size_t i=0u;i<stride*height;++i)pixels[i]=UINT32_C(0xdeadbeef);
        PortableSoftwareFrame frame={pixels,240u,160u,NULL,(uint8_t)scale,stride,0u,NULL};
        CHECK(portable_time_trial_records_render(&pack,&flow,&records,&frame)==PORTABLE_FRONTEND_PRESENTATION_OK);
        CHECK(pixels[11u*scale*stride+216u*scale]==UINT32_C(0xff010000));
        CHECK(pixels[7u*scale*stride+195u*scale]==UINT32_C(0xff010002));
        CHECK(pixels[7u*scale*stride+217u*scale]==UINT32_C(0xff01000d));
        CHECK(pixels[24u*scale*stride+208u*scale]==UINT32_C(0xff010010));
        CHECK(pixels[40u*scale*stride+224u*scale]==UINT32_C(0xff01001a));
        CHECK(pixels[24u*scale*stride+224u*scale]==UINT32_C(0xdeadbeef));
        for(size_t y=0u;y<height;++y)for(size_t x=240u*scale;x<stride;++x)
            CHECK(pixels[y*stride+x]==UINT32_C(0xdeadbeef));
        CHECK(memcmp(&records,&saved,sizeof records)==0&&memcmp(&flow,&saved_flow,sizeof flow)==0);
        free(pixels);
    }
    return 0;
}

int main(void)
{
    CHECK(ghost_rules_and_pixels()==0);
    PortableTimeTrialRecords records={.course_id=24u,.ghost_status=PORTABLE_TIME_TRIAL_GHOST_STORE_OK};
    records.received[0]=(PortableGhostExchangeInfo){.status=PORTABLE_TIME_TRIAL_GHOST_STORE_OK,.identity={.course_id=24u}};
    records.received[1]=(PortableGhostExchangeInfo){.status=PORTABLE_TIME_TRIAL_GHOST_STORE_OK,.identity={.course_id=25u}};
    CHECK(portable_time_trial_records_ghost_choices(&records)==3u);
    records.ghost_status=PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID;
    CHECK(portable_time_trial_records_ghost_choices(&records)==2u);
    records.received[0].status=PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE;
    CHECK(portable_time_trial_records_ghost_choices(&records)==0u);
    CHECK(portable_time_trial_records_ghost_choices(NULL)==0u);
    char directory[128],paths[4][256];
    CHECK(test_temp_path(directory,sizeof directory,"mksc-records",1)==0);
    const char *names[]={"course-64-solo.pb","course-64-solo.ghost","course-65-solo.pb","gp-progress.bin"};
    for(unsigned i=0u;i<4u;++i){
        CHECK(snprintf(paths[i],sizeof paths[i],"%s/%s",directory,names[i])>0);
        FILE *file=fopen(paths[i],"wb");CHECK(file!=NULL&&fputs("saved",file)>=0&&fclose(file)==0);
    }
    CHECK(!portable_time_trial_records_erase(NULL,64u));
    CHECK(!portable_time_trial_records_erase(directory,0u));
    CHECK(!portable_time_trial_records_erase_ghost(NULL,64u));
    CHECK(!portable_time_trial_records_erase_ghost(directory,0u));
    CHECK(portable_time_trial_records_erase_ghost(directory,64u));
    CHECK(test_path_exists(paths[0])&&!test_path_exists(paths[1]));
    CHECK(test_path_exists(paths[2])&&test_path_exists(paths[3]));
    CHECK(portable_time_trial_records_erase_ghost(directory,64u));
    CHECK(portable_time_trial_records_erase(directory,64u));
    CHECK(!test_path_exists(paths[0])&&!test_path_exists(paths[1]));
    CHECK(test_path_exists(paths[2])&&test_path_exists(paths[3]));
    CHECK(portable_time_trial_records_erase(directory,64u));
    CHECK(remove(paths[2])==0&&remove(paths[3])==0&&test_rmdir(directory)==0);
    puts("portable Time Trial records: PASS");return 0;
}
