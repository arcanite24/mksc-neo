#include "course_editor_host.h"
#include "course_editor.h"
#include "course_editor_pack.h"
#include "course_3d.h"
#include "rom_import_host.h"
#include "file_read.h"
#include "file_replace.h"
#include "frontend_flow.h"
#include "ui_font.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CourseEditorHost {
    CourseEditor model;
    CourseEditorDocument saved;
    char root[4096],notice[256];
    unsigned slot,kind,rotation,ring,choice,exit_choice;
    int active,lobby,confirm_exit,test_requested,x,y,z,pan_x,pan_y;
    uint16_t previous;
    float scale,width,height;
    uint8_t *pack_bytes;
    PortableAssetPackView pack,composed,base;
    PortableAssetSectionView *sections,*base_sections;
    PortableCourse3DHost *test;
};
static uint32_t course_id(const CourseEditorHost *h){return 0x7e000000u+h->slot;}
static int dirty(const CourseEditorHost *h){return memcmp(&h->saved,&h->model.document,sizeof h->saved)!=0;}
static void note(CourseEditorHost *h,const char *s){snprintf(h->notice,sizeof h->notice,"%s",s);}
static int path(CourseEditorHost *h,char *out,size_t capacity,const char *file)
{
    int n=snprintf(out,capacity,"%s/workshop-%u/%s",h->root,h->slot,file);
    return n>0 && (size_t)n<capacity;
}
static int atomic_file(const char *name,const void *bytes,size_t size)
{
    char temporary[4200];int n=snprintf(temporary,sizeof temporary,"%s.tmp",name);
    if(n<=0 || (size_t)n>=sizeof temporary)return 0;
    FILE *f=fopen(temporary,"wb");if(!f)return 0;
    int okay=fwrite(bytes,1,size,f)==size;if(fclose(f))okay=0;
    if(okay)okay=portable_file_replace(temporary,name);
    if(!okay)remove(temporary);
    return okay;
}
static int compile(CourseEditorHost *h,uint8_t **bytes,size_t *size)
{
    char name[64];snprintf(name,sizeof name,"Workshop %u",h->slot);
    return course_editor_pack(&h->model.document,course_id(h),name,bytes,size,h->notice,sizeof h->notice);
}
static int save(CourseEditorHost *h)
{
    uint8_t *bytes=NULL;size_t size=0;char filename[4096],folder[4096];
    if(!compile(h,&bytes,&size))return 0;
    int okay=path(h,folder,sizeof folder,"") && SDL_CreateDirectory(folder) &&
        path(h,filename,sizeof filename,"course.pack") && atomic_file(filename,bytes,size);
    free(bytes);
    char manifest[512];int n=snprintf(manifest,sizeof manifest,
        "{\"id\":\"workshop-%u\",\"name\":\"Workshop %u\",\"version\":\"1.0.0\",\"api_version\":1,\"targets\":[\"3d\"],\"pack\":\"course.pack\"}\n",h->slot,h->slot);
    if(okay)okay=path(h,filename,sizeof filename,"mod.json") && atomic_file(filename,manifest,(size_t)n);
    if(!okay){note(h,"Save failed. Your in-memory edits are still here.");return 0;}
    h->saved=h->model.document;note(h,"Saved. Enable Workshop in Mods to race it outside the editor.");
    printf("editor saved: workshop-%u pieces=%u\n",h->slot,h->model.document.count);return 1;
}
static int load(CourseEditorHost *h)
{
    char filename[4096];if(!path(h,filename,sizeof filename,"course.pack"))return 0;
    SDL_PathInfo info;
    if(!SDL_GetPathInfo(filename,&info)){
        /* Only a genuinely absent slot creates a fresh document. */
        char folder[4096];if(!path(h,folder,sizeof folder,"") || SDL_GetPathInfo(folder,&info)){
            note(h,"Existing slot cannot be read; edits were not replaced.");return 0;}
        course_editor_new(&h->model);memset(&h->saved,0,sizeof h->saved);
        note(h,"New course. Gate numbers define lap order; gate 0 is the finish.");return 1;
    }
    uint8_t *bytes=NULL;size_t size=0;
    if(portable_file_read_bounded(filename,4*1024*1024,&bytes,&size)!=PORTABLE_FILE_READ_OK){note(h,"Cannot read this slot.");return 0;}
    PortableAssetPackView pack;
    CourseEditorDocument d={0};
    int okay=portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK &&
        course_editor_unpack(&pack,course_id(h),&d,h->notice,sizeof h->notice);
    free(bytes);
    if(!okay || !d.count || !course_editor_valid(&d)){note(h,"Not an editable Workshop course. Slot preserved.");return 0;}
    memset(&h->model,0,sizeof h->model);h->model.document=d;h->model.history[0]=d;
    h->model.history_count=1;h->model.selected=-1;h->saved=d;note(h,"Opened saved course.");
    printf("editor opened: workshop-%u pieces=%u\n",h->slot,d.count);return 1;
}
CourseEditorHost *course_editor_host_create(const char *root)
{
    if(!root || strlen(root)>=4096)return NULL;
    CourseEditorHost *h=calloc(1,sizeof *h);if(!h)return NULL;
    snprintf(h->root,sizeof h->root,"%s",root);h->active=h->lobby=1;h->slot=1;h->scale=10;
    h->model.selected=-1;note(h,"Choose a slot to create or reopen a course.");
    /* The Confirm that opened the Workshop is still held; act only after release. */
    h->previous=UINT16_MAX;return h;
}
void course_editor_host_test_return(CourseEditorHost *h)
{
    if(!h)return;
    portable_course_3d_host_close(h->test);h->test=NULL;
    free(h->pack_bytes);h->pack_bytes=NULL;free(h->sections);h->sections=NULL;free(h->base_sections);h->base_sections=NULL;
    h->previous=UINT16_MAX;
}
void course_editor_host_close(CourseEditorHost *h){if(h){course_editor_host_test_return(h);free(h);}}
int course_editor_host_active(const CourseEditorHost *h){return h && h->active;}
static void pick(CourseEditorHost *h)
{
    h->model.selected=course_editor_pick(&h->model,h->x,h->y,h->z);
    if(h->model.selected>=0){const CourseEditorPiece *p=&h->model.document.pieces[h->model.selected];
        h->kind=p->kind;h->rotation=p->rotation;note(h,"Move the cursor, then A / Enter to place the selected piece.");}
    else note(h,"No piece at this height. Use height controls to reach another level.");
}
static void place(CourseEditorHost *h)
{
    if(course_editor_put(&h->model,(CourseEditorPiece){h->x,h->y,h->z,h->kind,h->rotation})){
        h->model.selected=-1;note(h,"Piece placed. Undo is available in Actions.");}
    else note(h,"No change, or the 128-piece limit was reached.");
}
static void leave(CourseEditorHost *h)
{if(dirty(h)){h->confirm_exit=1;h->exit_choice=0;}else h->lobby=1;}
static void action(CourseEditorHost *h,unsigned a)
{
    h->ring=0;
    switch(a){case 0:save(h);break;case 1:h->test_requested=1;break;case 2:pick(h);break;
    case 3:h->rotation=(h->rotation+1)%4;break;
    case 4:if(h->model.selected<0)pick(h);course_editor_delete(&h->model);break;
    case 5:course_editor_undo(&h->model,0);break;case 6:course_editor_undo(&h->model,1);break;
    case 7:if(h->z<64)++h->z;break;case 8:if(h->z> -32)--h->z;break;case 9:leave(h);break;}
}
void course_editor_host_tick(CourseEditorHost *h,uint16_t buttons)
{
    uint16_t pressed=buttons & (uint16_t)~h->previous;h->previous=buttons;
    if(h->confirm_exit){
        if(pressed&PORTABLE_FRONTEND_BUTTON_UP)h->exit_choice=(h->exit_choice+2)%3;
        if(pressed&PORTABLE_FRONTEND_BUTTON_DOWN)h->exit_choice=(h->exit_choice+1)%3;
        if(pressed&PORTABLE_FRONTEND_BUTTON_B)h->confirm_exit=0;
        if(pressed&PORTABLE_FRONTEND_BUTTON_A){
            if(h->exit_choice==1 || (h->exit_choice==0 && save(h)))h->lobby=1;
            h->confirm_exit=0;}
        return;
    }
    if(h->lobby){
        if(pressed&PORTABLE_FRONTEND_BUTTON_UP)h->slot=(h->slot+6)%8+1;
        if(pressed&PORTABLE_FRONTEND_BUTTON_DOWN)h->slot=h->slot%8+1;
        if(pressed&PORTABLE_FRONTEND_BUTTON_B)h->active=0;
        if(pressed&PORTABLE_FRONTEND_BUTTON_A && load(h)){
            h->lobby=0;h->x=h->y=h->z=h->pan_x=h->pan_y=0;h->ring=0;}
        return;
    }
    if(h->ring){unsigned count=h->ring==1?EDITOR_KINDS:10;
        if(pressed&(PORTABLE_FRONTEND_BUTTON_UP|PORTABLE_FRONTEND_BUTTON_LEFT))h->choice=(h->choice+count-1)%count;
        if(pressed&(PORTABLE_FRONTEND_BUTTON_DOWN|PORTABLE_FRONTEND_BUTTON_RIGHT))h->choice=(h->choice+1)%count;
        if(pressed&PORTABLE_FRONTEND_BUTTON_B)h->ring=0;
        if(pressed&PORTABLE_FRONTEND_BUTTON_A){if(h->ring==1){h->kind=h->choice;h->model.selected=-1;h->ring=0;}else action(h,h->choice);}
        return;
    }
    if(pressed&PORTABLE_FRONTEND_BUTTON_SELECT){h->ring=1;h->choice=h->kind;}
    if(pressed&PORTABLE_FRONTEND_BUTTON_START){h->ring=2;h->choice=0;}
    if(pressed&PORTABLE_FRONTEND_BUTTON_UP)h->y+=4;
    if(pressed&PORTABLE_FRONTEND_BUTTON_DOWN)h->y-=4;
    if(pressed&PORTABLE_FRONTEND_BUTTON_LEFT)h->x-=4;
    if(pressed&PORTABLE_FRONTEND_BUTTON_RIGHT)h->x+=4;
    if(h->x < -400)h->x=-400;
    if(h->x>400)h->x=400;
    if(h->y < -400)h->y=-400;
    if(h->y>400)h->y=400;
    if(pressed&PORTABLE_FRONTEND_BUTTON_L)pick(h);
    if(pressed&PORTABLE_FRONTEND_BUTTON_R)h->rotation=(h->rotation+1)%4;
    if(pressed&PORTABLE_FRONTEND_BUTTON_A)place(h);
    if(pressed&PORTABLE_FRONTEND_BUTTON_B){if(h->model.selected>=0)h->model.selected=-1;else leave(h);}
    if(abs(h->x-h->pan_x)>24)h->pan_x=h->x;
    if(abs(h->y-h->pan_y)>16)h->pan_y=h->y;
}
static const char *actions[]={"Save","Test drive","Move","Rotate","Delete","Undo","Redo","Raise","Lower","Projects"};
int course_editor_host_event(CourseEditorHost *h,const SDL_Event *e,void *window)
{
    if(h->test || !h->active)return 0;
    if(e->type==SDL_EVENT_KEY_DOWN && e->key.key==SDLK_F5)return 1;
    if(e->type==SDL_EVENT_KEY_DOWN && !e->key.repeat && !h->lobby && !h->confirm_exit && !h->ring){
        if(e->key.mod&SDL_KMOD_CTRL){
            if(e->key.key==SDLK_Z){action(h,5);return 1;}
            if(e->key.key==SDLK_Y){action(h,6);return 1;}
            if(e->key.key==SDLK_S){action(h,0);return 1;}}
        if(e->key.key==SDLK_DELETE || e->key.key==SDLK_BACKSPACE){action(h,4);return 1;}
        if(e->key.key==SDLK_R){action(h,3);return 1;}
        if(e->key.key==SDLK_PAGEUP){action(h,7);return 1;}
        if(e->key.key==SDLK_PAGEDOWN){action(h,8);return 1;}
        if(e->key.key==SDLK_F6){action(h,1);return 1;}
    }
    if(e->type==SDL_EVENT_MOUSE_WHEEL && !h->lobby){h->scale+=e->wheel.y;
        if(h->scale<3)h->scale=3;
        if(h->scale>24)h->scale=24;return 1;}
    if(e->type!=SDL_EVENT_MOUSE_MOTION && e->type!=SDL_EVENT_MOUSE_BUTTON_DOWN)return 0;
    int w=1,v=1;SDL_GetWindowSize(window,&w,&v);
    float mx=e->type==SDL_EVENT_MOUSE_MOTION?e->motion.x:e->button.x;
    float my=e->type==SDL_EVENT_MOUSE_MOTION?e->motion.y:e->button.y;
    mx*=h->width/w;my*=h->height/v;
    int click=e->type==SDL_EVENT_MOUSE_BUTTON_DOWN;
    if(h->confirm_exit){
        float u=h->height/720.0f;
        int choice=(int)floorf((my-h->height*.5f+52.5f*u)/(45*u));
        if(click && e->button.button==SDL_BUTTON_LEFT && choice>=0 && choice<3){
            h->exit_choice=(unsigned)choice;
            course_editor_host_tick(h,0);course_editor_host_tick(h,PORTABLE_FRONTEND_BUTTON_A);
        }
        return 1;
    }
    if(h->lobby){if(click && my>=h->height*.22f && my<h->height*.62f){
        h->slot=1+(unsigned)((my-h->height*.22f)/(h->height*.05f));if(load(h))h->lobby=0;}return 1;}
    if(h->ring){if(click && e->button.button==SDL_BUTTON_RIGHT)h->ring=0;
        if(click && e->button.button==SDL_BUTTON_LEFT){float angle=atan2f(my-h->height*.5f,mx-h->width*.5f)+1.570796327f;
            if(angle<0)angle+=6.283185307f;
            unsigned count=h->ring==1?EDITOR_KINDS:10;
            unsigned choice=(unsigned)lroundf(angle*count/6.283185307f)%count;
            if(h->ring==1){h->kind=choice;h->model.selected=-1;h->ring=0;}else action(h,choice);}return 1;}
    if(my<h->height*(100.0f/720)){if(click){if(mx<h->width/3){h->ring=1;h->choice=h->kind;}
        else if(mx<h->width*2/3)action(h,0);else action(h,1);}return 1;}
    if(my>h->height*(620.0f/720)){if(click){h->ring=2;h->choice=0;}return 1;}
    h->x=(int)lroundf((h->pan_x+(mx-h->width*.5f)/h->scale)/4)*4;
    h->y=(int)lroundf((h->pan_y-(my-h->height*.5f)/h->scale)/4)*4;
    if(h->x < -400)h->x=-400;
    if(h->x>400)h->x=400;
    if(h->y < -400)h->y=-400;
    if(h->y>400)h->y=400;
    if(click){if(e->button.button==SDL_BUTTON_RIGHT)pick(h);else if(e->button.button==SDL_BUTTON_LEFT)place(h);}return 1;
}
static void text(UiCanvas *c,float x,float y,float size,uint32_t color,const char *s)
{UiTextStyle st=ui_text_style(UI_FONT_BOLD,size*c->height/720.0f,color);st=ui_text_fit(&st,s,c->width-40);ui_text_draw_centered(c,&st,x,y,UI_ALIGN_LEFT,s,1);}
int course_editor_host_render(ModernMenu *menu,UiCanvas *c,void *owner)
{
    (void)menu;CourseEditorHost *h=owner;h->width=(float)c->width;h->height=(float)c->height;
    ui_canvas_clear(c,0xff152932);char label[160];
    const float u=c->height/720.0f;
    if(h->lobby){text(c,40*u,55*u,32,0xfff7d779,"COURSE WORKSHOP");
        text(c,40*u,100*u,22,0xffd9e7e9,"Create or open a course  /  8 local slots");
        for(unsigned i=1;i<=8;++i){snprintf(label,sizeof label,"%s Workshop %u",i==h->slot?">":" ",i);
            text(c,60*u,h->height*.22f+(i-1)*h->height*.05f+18*u,26,i==h->slot?0xfff7d779:0xffd9e7e9,label);}
        text(c,40*u,h->height-75*u,19,0xffd9e7e9,h->notice);text(c,40*u,h->height-35*u,20,0xfff7d779,"D-pad / arrows: choose    A / Enter: open    B / Esc: Mods");return 1;}
    float cx=c->width*.5f,cy=c->height*.5f,s=h->scale;
    ui_canvas_clip(c,0,(int)(100*u),c->width,(int)(c->height-100*u));
    for(int i=-100;i<=100;++i){float x=cx+(i*4-h->pan_x)*s,y=cy-(i*4-h->pan_y)*s;
        ui_fill_rect(c,x,100*u,1,c->height-200*u,0xff29424c,1);ui_fill_rect(c,0,y,c->width,1,0xff29424c,1);}
    const uint32_t colors[]={0xff627b8a,0xffe8ad63,0xff7ba8bd,0xfff5cc43,0xff68dcf3,0xfff4f4f4,0xfff080a0,0xff6ed7a5,0xffb8a3e0};
    unsigned gate=0;
    for(unsigned i=0;i<h->model.document.count;++i){const CourseEditorPiece *p=&h->model.document.pieces[i];
        float x=cx+(p->x-h->pan_x)*s,y=cy-(p->y-h->pan_y)*s;
        float width=p->kind==EDITOR_WALL?8:p->kind<=EDITOR_BOOST?8:2,height=p->kind==EDITOR_WALL?1:width;
        if(p->rotation%2){float temp=width;width=height;height=temp;}
        float alpha=p->z==h->z?1:.35f;
        ui_fill_rect(c,x-width*s/2+1,y-height*s/2+1,width*s-2,height*s-2,colors[p->kind],alpha);
        int nx=0,ny=-1;for(unsigned r=0;r<p->rotation;++r){int temp=nx;nx=-ny;ny=temp;}
        UiPaint paint=ui_paint_solid(0xff13252c);ui_fill_circle(c,x+nx*s,y-ny*s,2,&paint,1,alpha);
        if(p->kind==EDITOR_GATE){snprintf(label,sizeof label,"%u",gate++);text(c,x-5,y-12,16,0xffffffff,label);}
        if((int)i==h->model.selected)ui_stroke_round_rect(c,x-4*s,y-4*s,8*s,8*s,0,0,3,0xfff7d779,1);
    }
    float x=cx+(h->x-h->pan_x)*s,y=cy-(h->y-h->pan_y)*s;
    ui_stroke_round_rect(c,x-4*s,y-4*s,8*s,8*s,0,0,2,0xffffffff,1);
    ui_fill_rect(c,x-9,y,18,2,0xffffffff,1);ui_fill_rect(c,x,y-9,2,18,0xffffffff,1);
    ui_canvas_unclip(c);
    snprintf(label,sizeof label,"Workshop %u%s   |   %s   |   %u / 128 pieces",h->slot,dirty(h)?" *":"",course_editor_kind_name(h->kind),h->model.document.count);
    text(c,24*u,28*u,26,0xfff7d779,label);
    text(c,24*u,72*u,20,0xffd9e7e9,"Pieces [Tab / Back]");text(c,c->width/3+20*u,72*u,20,0xffd9e7e9,"Save [Ctrl+S]");text(c,c->width*2/3+20*u,72*u,20,0xffd9e7e9,"Test drive [F6]");
    snprintf(label,sizeof label,"X %d   Y %d   Height %d m   |   Rotation %u   |   4 m snap",h->x,h->y,h->z,h->rotation*90);
    text(c,24*u,c->height-80*u,19,0xfff7d779,label);text(c,24*u,c->height-50*u,17,0xffd9e7e9,h->notice);
    text(c,24*u,c->height-22*u,17,0xffd9e7e9,"A / click place   L / right-click move   R rotate   Start / Space: Actions   B cancel / leave");
    if(h->ring){unsigned count=h->ring==1?EDITOR_KINDS:10;
        ui_fill_rect(c,0,100*u,c->width,c->height-200*u,0xff101f29,.94f);
        for(unsigned i=0;i<count;++i){float a=(float)i*6.283185307f/count-1.570796327f;
            float px=cx+cosf(a)*190*u,py=cy+sinf(a)*150*u;UiPaint paint=ui_paint_solid(i==h->choice?0xffe8c96b:0xff294652);
            ui_fill_round_rect(c,px-72*u,py-21*u,144*u,42*u,12*u,0,&paint,1,1);
            UiTextStyle st=ui_text_style(UI_FONT_BOLD,18*u,i==h->choice?0xff152932:0xffe6eff1);
            ui_text_draw_centered(c,&st,px,py,UI_ALIGN_CENTER,h->ring==1?course_editor_kind_name(i):actions[i],1);}
        text(c,cx-90*u,cy-12*u,22,0xfff7d779,h->ring==1?"Choose a piece":"Course actions");
        text(c,cx-110*u,cy+20*u,16,0xffd9e7e9,"D-pad: cycle   A: choose");}
    if(h->confirm_exit){ui_fill_rect(c,0,100*u,c->width,c->height-200*u,0xff101f29,.98f);
        text(c,60*u,cy-90*u,28,0xfff7d779,"Keep your changes?");const char *choices[]={"Save and return to projects","Discard and return to projects","Continue editing"};
        for(unsigned i=0;i<3;++i)text(c,60*u,cy-30*u+i*45*u,24,i==h->exit_choice?0xfff7d779:0xffd9e7e9,choices[i]);}
    return 1;
}
PortableCourse3DHost *course_editor_host_test(CourseEditorHost *h,const PortableAssetPackView *base,const PortableSettings *settings)
{
    if(!h->test_requested)return NULL;
    h->test_requested=0;
    size_t size=0;if(!compile(h,&h->pack_bytes,&size))return NULL;
    if(portable_asset_pack_open(h->pack_bytes,size,&h->pack)!=PORTABLE_ASSET_PACK_OK)goto failed;
    h->base_sections=calloc(base->section_count,sizeof *h->base_sections);
    h->sections=calloc(base->section_count+4,sizeof *h->sections);if(!h->base_sections || !h->sections)goto failed;
    h->base=*base;h->base.section_count=0;h->base.resolved_sections=h->base_sections;
    for(uint32_t i=0;i<base->section_count;++i){PortableAssetSectionView section;
        if(portable_asset_pack_section_at(base,i,&section)!=PORTABLE_ASSET_PACK_OK)goto failed;
        if(section.kind!=100)h->base_sections[h->base.section_count++]=section;}
    const PortableAssetPackView *layers[]={&h->base,&h->pack};
    if(portable_asset_pack_compose(layers,2,h->sections,base->section_count+4,&h->composed)!=PORTABLE_ASSET_PACK_OK)goto failed;
    h->test=portable_course_3d_host_create(&h->composed,h->notice,sizeof h->notice);if(!h->test)goto failed;
    char *directory=portable_data_directory();char saves[4096];
    int n=directory?snprintf(saves,sizeof saves,"%seditor-test",directory):0;SDL_free(directory);
    if(n<=0 || (size_t)n>=sizeof saves || !SDL_CreateDirectory(saves) ||
        !portable_course_3d_host_configure(h->test,h->composed.deterministic_digest,1,saves,settings))goto failed;
    portable_course_3d_host_show(h->test);
    /* The Test drive action admits the same neutral/Confirm transition as the
     * course browser. It creates no alternate simulation or race owner. */
    portable_course_3d_host_tick_driving(h->test,(PortableMeshKartInput){0},0);
    portable_course_3d_host_tick_driving(h->test,(PortableMeshKartInput){0},PORTABLE_FRONTEND_BUTTON_A);
    note(h,"Test drive: Pause > Change course, then B returns here.");
    printf("editor test drive: workshop-%u\n",h->slot);return h->test;
failed:
    if(!h->notice[0])note(h,"Test drive could not load this course.");
    course_editor_host_test_return(h);return NULL;
}
