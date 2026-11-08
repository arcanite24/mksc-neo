#include "course_editor_host.h"
#include "course_editor_pack.h"
#include "frontend_flow.h"
#include "file_read.h"
#include "mod_manifest.h"
#include "test_temp.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"editor host line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void tap(CourseEditorHost *h,uint16_t button)
{course_editor_host_tick(h,button);course_editor_host_tick(h,0);}
static CourseEditorDocument read_document(const char *root)
{
    char path[512],error[256];uint8_t *bytes=NULL;size_t size=0;CourseEditorDocument document;
    snprintf(path,sizeof path,"%s/workshop-1/course.pack",root);
    CHECK(portable_file_read_bounded(path,4*1024*1024,&bytes,&size)==PORTABLE_FILE_READ_OK);
    PortableAssetPackView pack;CHECK(portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(course_editor_unpack(&pack,0x7e000001,&document,error,sizeof error));free(bytes);return document;
}
int main(void)
{
    CHECK(SDL_Init(SDL_INIT_VIDEO));char root[256];CHECK(test_temp_path(root,sizeof root,"editor",1)==0);
    /* A held Confirm from the Mods row must not also open a slot: B then
     * still leaves the slot list instead of returning to it. */
    CourseEditorHost *h=course_editor_host_create(root);CHECK(h);
    course_editor_host_tick(h,PORTABLE_FRONTEND_BUTTON_A);course_editor_host_tick(h,PORTABLE_FRONTEND_BUTTON_A);
    tap(h,PORTABLE_FRONTEND_BUTTON_B);CHECK(!course_editor_host_active(h));course_editor_host_close(h);
    h=course_editor_host_create(root);CHECK(h);course_editor_host_tick(h,0);
    tap(h,PORTABLE_FRONTEND_BUTTON_A);
    tap(h,PORTABLE_FRONTEND_BUTTON_SELECT);tap(h,PORTABLE_FRONTEND_BUTTON_DOWN);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    tap(h,PORTABLE_FRONTEND_BUTTON_RIGHT);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    tap(h,PORTABLE_FRONTEND_BUTTON_START);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    CourseEditorDocument d=read_document(root);CHECK(d.count==32);CHECK(d.pieces[31].kind==EDITOR_WALL && d.pieces[31].x==4);
    /* Move and rotate with exactly the controls used by a pad. */
    tap(h,PORTABLE_FRONTEND_BUTTON_L);tap(h,PORTABLE_FRONTEND_BUTTON_RIGHT);tap(h,PORTABLE_FRONTEND_BUTTON_R);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    tap(h,PORTABLE_FRONTEND_BUTTON_START);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    d=read_document(root);CHECK(d.pieces[31].x==8 && d.pieces[31].rotation==1);
    /* Undo through the pad wheel, redo through the desktop shortcut. */
    tap(h,PORTABLE_FRONTEND_BUTTON_START);for(unsigned i=0;i<5;++i)tap(h,PORTABLE_FRONTEND_BUTTON_DOWN);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    SDL_Event e={.key={.type=SDL_EVENT_KEY_DOWN,.key=SDLK_Y,.mod=SDL_KMOD_CTRL}};
    CHECK(course_editor_host_event(h,&e,NULL));
    e.key.key=SDLK_S;CHECK(course_editor_host_event(h,&e,NULL));
    d=read_document(root);CHECK(d.pieces[31].x==8 && d.pieces[31].rotation==1);
    /* Render and point with a mouse through the same snapped document. */
    SDL_Window *window=SDL_CreateWindow("editor test",1280,720,SDL_WINDOW_HIDDEN);CHECK(window);
    uint32_t *pixels=calloc(1280*720,sizeof *pixels);CHECK(pixels);UiCanvas canvas;
    ui_canvas_init(&canvas,pixels,1280,720,1280);CHECK(course_editor_host_render(NULL,&canvas,h));
    e=(SDL_Event){.button={.type=SDL_EVENT_MOUSE_BUTTON_DOWN,.button=SDL_BUTTON_LEFT,.x=760,.y=360}};
    CHECK(course_editor_host_event(h,&e,window));
    e=(SDL_Event){.key={.type=SDL_EVENT_KEY_DOWN,.key=SDLK_S,.mod=SDL_KMOD_CTRL}};
    CHECK(course_editor_host_event(h,&e,window));d=read_document(root);CHECK(d.count==33 && d.pieces[32].x==12);
    free(pixels);SDL_DestroyWindow(window);course_editor_host_close(h);
    h=course_editor_host_create(root);CHECK(h);course_editor_host_tick(h,0);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    tap(h,PORTABLE_FRONTEND_BUTTON_START);tap(h,PORTABLE_FRONTEND_BUTTON_A);
    CourseEditorDocument reopened=read_document(root);CHECK(!memcmp(&d,&reopened,sizeof d));course_editor_host_close(h);
    char path[512],error[256];uint8_t *bytes=NULL;size_t size=0;
    snprintf(path,sizeof path,"%s/workshop-1/mod.json",root);
    CHECK(portable_file_read_bounded(path,4096,&bytes,&size)==PORTABLE_FILE_READ_OK);
    PortableModManifest manifest;CHECK(portable_mod_manifest_parse((const char *)bytes,size,&manifest,error));
    CHECK(manifest.targets==PORTABLE_MOD_TARGET_3D);free(bytes);CHECK(remove(path)==0);
    snprintf(path,sizeof path,"%s/workshop-1/course.pack",root);CHECK(remove(path)==0);
    snprintf(path,sizeof path,"%s/workshop-1",root);CHECK(test_rmdir(path)==0);CHECK(test_rmdir(root)==0);
    SDL_Quit();puts("editor pad, mouse, history, save and reopen passed");return 0;
}
