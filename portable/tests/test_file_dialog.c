#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"file dialog line %d: %s\n",__LINE__,#x);exit(1);} } while(0)

/* Replace only the dialog launch, keeping real SDL mutexes/threads. Including
 * the implementation after SDL declarations avoids DLL-import interposition on
 * Windows and leaves the production translation unit unchanged. */
static SDL_DialogFileCallback pending_callback;
static void *pending_context;
static const SDL_DialogFileFilter *pending_filters;
static int synchronous;
static void SDLCALL test_show_open_file_dialog(SDL_DialogFileCallback callback,void *context,
    SDL_Window *window,const SDL_DialogFileFilter *filters,int count,
    const char *location,bool multiple)
{
    CHECK(window==NULL&&count==2&&strcmp(filters[1].pattern,"*")==0&&location==NULL&&!multiple);
    CHECK(pending_callback==NULL);
    pending_callback=callback;pending_context=context;pending_filters=filters;
    if(synchronous){
        const char *files[]={"sync.zip",NULL};
        pending_callback=NULL;pending_context=NULL;pending_filters=NULL;
        callback(context,files,0);
    }
}
#define SDL_ShowOpenFileDialog test_show_open_file_dialog
static void SDLCALL test_show_open_folder_dialog(SDL_DialogFileCallback callback,void *context,
    SDL_Window *window,const char *location,bool multiple)
{
    CHECK(window==NULL&&location==NULL&&!multiple&&pending_callback==NULL);
    pending_callback=callback;pending_context=context;pending_filters=NULL;
}
#define SDL_ShowOpenFolderDialog test_show_open_folder_dialog
#include "../platform/file_dialog.c"
#undef SDL_ShowOpenFileDialog
#undef SDL_ShowOpenFolderDialog

static void complete(const char *const *files)
{
    SDL_DialogFileCallback callback=pending_callback;
    void *context=pending_context;
    CHECK(callback!=NULL);
    pending_callback=NULL;pending_context=NULL;pending_filters=NULL;
    callback(context,files,0);
}

static int SDLCALL complete_thread(void *unused)
{
    (void)unused;
    const char *files[]={"thread.zip",NULL};
    complete(files);return 0;
}

int main(void)
{
    CHECK(portable_file_dialog_choose(NULL,NULL,"zip")==NULL);
    CHECK(portable_file_dialog_choose(NULL,"Zip",NULL)==NULL);
    char output[64]="untouched";
    CHECK(portable_file_dialog_poll(NULL,output,sizeof output)==3);
    portable_file_dialog_free(NULL);
    PortableFileDialog *folder=portable_file_dialog_choose_folder(NULL);CHECK(folder!=NULL);
    const char *folder_paths[]={"chosen-folder",NULL};complete(folder_paths);
    CHECK(portable_file_dialog_poll(folder,output,sizeof output)==1&&strcmp(output,"chosen-folder")==0);
    portable_file_dialog_free(folder);
    folder=portable_file_dialog_choose_folder(NULL);CHECK(folder!=NULL);
    portable_file_dialog_free(folder);complete(folder_paths);
    strcpy(output,"untouched");
    char label[]="Mod archive",pattern[]="zip";
    PortableFileDialog *request=portable_file_dialog_choose(NULL,label,pattern);CHECK(request!=NULL);
    label[0]='X';pattern[0]='X';
    CHECK(strcmp(pending_filters->name,"Mod archive")==0&&strcmp(pending_filters->pattern,"zip")==0);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==0&&strcmp(output,"untouched")==0);
    const char *selected[]={"selected.zip",NULL};complete(selected);
    CHECK(portable_file_dialog_poll(request,output,4u)==3&&strcmp(output,"untouched")==0);
    CHECK(portable_file_dialog_poll(request,NULL,64u)==3);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==1&&strcmp(output,"selected.zip")==0);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==1);
    portable_file_dialog_free(request);

    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);
    const char *cancelled[]={NULL};complete(cancelled);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==2);
    portable_file_dialog_free(request);
    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);complete(NULL);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==3);
    portable_file_dialog_free(request);

    char oversized[4097];memset(oversized,'a',sizeof oversized-1u);oversized[sizeof oversized-1u]='\0';
    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);
    const char *long_files[]={oversized,NULL};complete(long_files);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==3);
    portable_file_dialog_free(request);

    synchronous=1;
    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==1&&strcmp(output,"sync.zip")==0);
    portable_file_dialog_free(request);synchronous=0;

    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);
    portable_file_dialog_free(request); /* Callback retains filters/context after owner cancellation. */
    CHECK(strcmp(pending_filters->name,"Zip")==0&&strcmp(pending_filters->pattern,"zip")==0);
    complete(selected);

    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);
    SDL_Thread *thread=SDL_CreateThread(complete_thread,"dialog-test",NULL);CHECK(thread!=NULL);
    int result=-1;SDL_WaitThread(thread,&result);CHECK(result==0);
    CHECK(portable_file_dialog_poll(request,output,sizeof output)==1&&strcmp(output,"thread.zip")==0);
    portable_file_dialog_free(request);
    request=portable_file_dialog_choose(NULL,"Zip","zip");CHECK(request!=NULL);
    portable_file_dialog_free(request);
    thread=SDL_CreateThread(complete_thread,"late-dialog-test",NULL);CHECK(thread!=NULL);
    SDL_WaitThread(thread,&result);CHECK(result==0&&pending_callback==NULL);
    puts("portable file dialog: PASS");return 0;
}
