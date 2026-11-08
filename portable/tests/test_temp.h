#ifndef PORTABLE_TEST_TEMP_H
#define PORTABLE_TEST_TEMP_H

/* Tests run in CTest's build directory, inside the repository scratch budget. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define test_close _close
#define test_rmdir _rmdir
#define test_process_id _getpid
#else
#include <unistd.h>
#define test_close close
#define test_rmdir rmdir
#define test_process_id getpid
#endif

static inline int test_temp_path(char *path,size_t capacity,const char *stem,int directory)
{
    static unsigned serial;
    for(unsigned attempt=0u;attempt<100u;++attempt){
        const int length=snprintf(path,capacity,"%s-%lu-%u",stem,
            (unsigned long)test_process_id(),serial++);
        if(length<0||(size_t)length>=capacity)return -1;
        int result;
#ifdef _WIN32
        result=directory?_mkdir(path):_open(path,_O_CREAT|_O_EXCL|_O_WRONLY|_O_BINARY,
            _S_IREAD|_S_IWRITE);
#else
        result=directory?mkdir(path,0700):open(path,O_CREAT|O_EXCL|O_WRONLY,0600);
#endif
        if(result>=0)return result;
        if(errno!=EEXIST)return -1;
    }
    return -1;
}

static inline int test_path_exists(const char *path)
{
#ifdef _WIN32
    return _access(path,0)==0;
#else
    return access(path,F_OK)==0;
#endif
}
#endif
