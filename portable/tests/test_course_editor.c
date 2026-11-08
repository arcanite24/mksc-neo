#include "course_editor.h"
#include "course_editor_pack.h"
#include "course_3d.h"
#include "mesh_time_trial.h"
#include "mesh_kart_rules_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"editor line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    CourseEditor *e=malloc(sizeof *e);CHECK(e);course_editor_new(e);
    CourseEditorDocument original=e->document;
    CHECK(course_editor_put(e,(CourseEditorPiece){40,0,0,EDITOR_RAMP,1}));
    CHECK(course_editor_undo(e,0));CHECK(!memcmp(&e->document,&original,sizeof original));
    CHECK(course_editor_undo(e,1));CHECK(e->document.count==original.count+1);
    e->selected=course_editor_pick(e,40,0,0);CHECK(e->selected>=0);
    CHECK(course_editor_put(e,(CourseEditorPiece){44,4,1,EDITOR_RAMP,2}));
    CHECK(course_editor_delete(e));CHECK(!memcmp(&e->document,&original,sizeof original));
    CHECK(course_editor_undo(e,0));CHECK(course_editor_put(e,(CourseEditorPiece){48,0,0,EDITOR_WALL,0}));
    CHECK(!course_editor_undo(e,1));
    for(unsigned i=0;i<EDITOR_KINDS;++i){e->selected=-1;
        if(i!=EDITOR_START)CHECK(course_editor_put(e,(CourseEditorPiece){(int)(i*8),32,0,i,i%4}));}
    uint8_t *pack=NULL;size_t size=0;char error[256]={0};
    if(!course_editor_pack(&e->document,0x7e000001,"Editor test",&pack,&size,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    PortableAssetPackView view;PortableCourse3DView course;
    CHECK(portable_asset_pack_open(pack,size,&view)==PORTABLE_ASSET_PACK_OK);
    CHECK(portable_course_3d_open(&view,0x7e000001,&course,error,sizeof error));
    PortableCourse3DMeta meta;CHECK(portable_course_3d_meta(&course,&meta));
    CHECK(meta.physics_profile_id==0x7e000001);
    PortableCourse3DProperty p;CHECK(portable_course_3d_property_find(&course,meta.property_first,meta.property_count,"editor:piece:0",&p));
    CHECK(p.type==PORTABLE_COURSE_3D_PROPERTY_INTEGER && p.value.integer==0);
    CourseEditorDocument restored={0};
    CHECK(course_editor_unpack(&view,0x7e000001,&restored,error,sizeof error));
    CHECK(!memcmp(&restored,&e->document,sizeof restored));
    PortableMeshKartRules rules;PortableMeshTimeTrial trial;
    CHECK(portable_mesh_kart_rules_asset_load(&view,meta.physics_profile_id,&rules));
    CHECK(portable_mesh_time_trial_init(&trial,&course,&rules,0,error,sizeof error)==PORTABLE_MESH_OK);
    PortableMeshKartEvents events;
    for(unsigned tick=0;tick<200;++tick)
        CHECK(portable_mesh_time_trial_tick(&trial,(PortableMeshKartInput){0},&events,error,sizeof error)==PORTABLE_MESH_OK);
    free(pack);
    e->selected=-1;CHECK(course_editor_put(e,(CourseEditorPiece){0,0,0,EDITOR_START,0}));
    CHECK(!course_editor_pack(&e->document,0x7e000001,"Editor test",&pack,&size,error,sizeof error));
    CHECK(strstr(error,"one start"));
    CHECK(!course_editor_put(e,(CourseEditorPiece){401,0,0,EDITOR_ROAD,0}));
    e->selected=-1;CHECK(!course_editor_put(e,(CourseEditorPiece){0,0,0,EDITOR_ROAD,0}));
    course_editor_new(e);
    for(unsigned i=0;i<EDITOR_HISTORY+10;++i){e->selected=0;
        CHECK(course_editor_put(e,(CourseEditorPiece){(int)(i%2)*4,-20,0,EDITOR_ROAD,0}));}
    unsigned undos=0;while(course_editor_undo(e,0))++undos;
    CHECK(undos==EDITOR_HISTORY-1);
    free(e);puts("course editor history, bounds and native mod compilation passed");return 0;
}
