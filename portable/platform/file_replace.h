#ifndef PORTABLE_FILE_REPLACE_H
#define PORTABLE_FILE_REPLACE_H

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Publish a closed sibling temporary file, replacing an existing save without
 * deleting it first. Windows CRT rename refuses an existing destination. */
static inline int portable_file_replace(const char *temporary,const char *path)
{
    if(temporary==NULL||path==NULL)return 0;
#ifdef _WIN32
    return MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return rename(temporary,path)==0;
#endif
}
#endif
