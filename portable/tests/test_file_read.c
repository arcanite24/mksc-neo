#include <SDL3/SDL.h>
#include "file_read.h"
#include "mod_archive.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"file read line %d: %s\n",__LINE__,#x);exit(1);}} while(0)

typedef struct {
    const uint8_t *data;
    size_t size, position, chunk, fail_at, read_bytes;
    unsigned opens, closes;
    int close_error, open_error;
} Document;
static Document document;

static size_t SDLCALL document_read(void *context, void *out, size_t maximum,
    SDL_IOStatus *status)
{
    Document *d=context;
    if(d->position>=d->fail_at){*status=SDL_IO_STATUS_ERROR;return 0u;}
    size_t count=d->size-d->position;
    if(count>maximum)count=maximum;
    if(count>d->chunk)count=d->chunk;
    if(count>d->fail_at-d->position)count=d->fail_at-d->position;
    if(count!=0u)memcpy(out,d->data+d->position,count);
    d->position+=count;d->read_bytes+=count;
    *status=count==0u?SDL_IO_STATUS_EOF:SDL_IO_STATUS_READY;
    return count;
}
static bool SDLCALL document_close(void *context)
{
    Document *d=context;
    ++d->closes;
    return d->close_error==0;
}
static SDL_IOStream *open_document(void)
{
    SDL_IOStreamInterface io;
    SDL_INIT_INTERFACE(&io);
    io.read=document_read;io.close=document_close;
    /* No size or seek operation: provider-backed streams need neither. */
    return SDL_OpenIO(&io,&document);
}
static SDL_IOStream *test_open(const char *path,const char *mode)
{
    CHECK(strcmp(path,"content://test.documents/document/opaque%3Azip")==0);
    CHECK(strcmp(mode,"rb")==0);
    ++document.opens;
    if(document.open_error){errno=ENOENT;return NULL;}
    return open_document();
}

/* Substitute only the platform opener, as file-dialog tests do. The bounded
 * reader and archive parser are production code, with real SDL IO dispatch.
 * A direct fopen on this picked URI cannot satisfy the archive regression. */
#define SDL_IOFromFile test_open
#include "../platform/file_read.c"
#undef SDL_IOFromFile

static void reset(const uint8_t *data,size_t size,size_t chunk)
{
    document=(Document){.data=data,.size=size,.chunk=chunk,.fail_at=SIZE_MAX};
}
static void failures(void)
{
    static const uint8_t data[]={1,2,3,4,5};
    uint8_t sentinel=99,*bytes=&sentinel;
    size_t size=91u;
    reset(data,sizeof data,2u);
    document.fail_at=3u;
    CHECK(portable_file_read_stream(open_document(),20u,&bytes,&size)==PORTABLE_FILE_READ_IO);
    CHECK(bytes==&sentinel&&size==91u&&document.closes==1u);
    reset(data,sizeof data,1u);document.close_error=1;
    CHECK(portable_file_read_stream(open_document(),20u,&bytes,&size)==PORTABLE_FILE_READ_IO);
    CHECK(bytes==&sentinel&&size==91u&&document.closes==1u);
    reset(data,sizeof data,2u);
    CHECK(portable_file_read_stream(open_document(),4u,&bytes,&size)==PORTABLE_FILE_READ_LIMIT);
    CHECK(bytes==&sentinel&&size==91u&&document.read_bytes==5u&&document.closes==1u);
    reset(data,sizeof data,2u);
    CHECK(portable_file_read_stream(open_document(),SIZE_MAX,&bytes,&size)==PORTABLE_FILE_READ_INVALID);
    CHECK(bytes==&sentinel&&size==91u&&document.closes==1u);
    reset(data,sizeof data,2u);document.open_error=1;
    CHECK(portable_file_read_bounded("content://test.documents/document/opaque%3Azip",8u,
        &bytes,&size)==PORTABLE_FILE_READ_OPEN);
    CHECK(bytes==&sentinel&&size==91u&&document.opens==1u&&document.closes==0u);
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    failures();
    uint8_t *input=malloc(150001u);CHECK(input!=NULL);
    for(size_t i=0;i<150001u;++i)input[i]=(uint8_t)(i*17u);
    for(size_t length=0u;length<=150000u;length+=50000u){
        reset(input,length,7u);
        uint8_t *bytes=NULL;size_t size=SIZE_MAX;
        CHECK(portable_file_read_stream(open_document(),length,&bytes,&size)==PORTABLE_FILE_READ_OK);
        CHECK(size==length&&bytes!=NULL&&memcmp(bytes,input,length)==0&&document.closes==1u);
        free(bytes);
    }
    reset(input,150001u,100000u);
    uint8_t *bytes=NULL;size_t size=0u;
    CHECK(portable_file_read_stream(open_document(),150000u,&bytes,&size)==PORTABLE_FILE_READ_LIMIT);
    CHECK(document.read_bytes==150001u&&document.closes==1u&&bytes==NULL&&size==0u);
    free(input);

    FILE *fixture=fopen(argv[1],"rb");CHECK(fixture!=NULL);
    CHECK(fseek(fixture,0,SEEK_END)==0);
    const long length=ftell(fixture);CHECK(length>0&&length<1048576);
    CHECK(fseek(fixture,0,SEEK_SET)==0);
    input=malloc((size_t)length);CHECK(input!=NULL);
    CHECK(fread(input,1u,(size_t)length,fixture)==(size_t)length&&fclose(fixture)==0);
    reset(input,(size_t)length,7u);
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
    PortableModArchive *archive=portable_mod_archive_open(
        "content://test.documents/document/opaque%3Azip",NULL,error);
    CHECK(archive!=NULL&&document.opens==1u&&document.closes==1u);
    CHECK(portable_mod_archive_validate(archive,error));
    bytes=portable_mod_archive_read(archive,"mod.json",16384u,&size,error);
    CHECK(bytes!=NULL&&size>0u);free(bytes);
    portable_mod_archive_free(archive);
    reset(input,(size_t)length,7u);document.fail_at=20u;
    CHECK(portable_mod_archive_open("content://test.documents/document/opaque%3Azip",NULL,error)==NULL);
    CHECK(document.opens==1u&&document.closes==1u&&strstr(error,"unreadable")!=NULL);
    free(input);
    puts("portable file read: PASS nonseekable short reads caps errors URI archive");
    return 0;
}
