#include "source_path.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int portable_source_path_is_uri(const char *path)
{
    return path && strncmp(path,"content://",10)==0;
}
static int safe_relative(const char *path)
{
    if(!path || !*path || strlen(path)>=4096)return 0;
    unsigned depth=0;
    const char *start=path;
    for(const char *p=path;;++p)
    {
        if(*p=='\\' || *p==':' || ((unsigned char)*p<32u && *p))return 0;
        if(*p=='/' || !*p)
        {
            const size_t n=(size_t)(p-start);
            if(!n || n>255 || (n==1 && start[0]=='.') ||
                (n==2 && start[0]=='.' && start[1]=='.') || ++depth>16)return 0;
            if(!*p)return 1;
            start=p+1;
        }
    }
}
#if defined(PORTABLE_ENABLE_SDL3) && defined(SDL_PLATFORM_ANDROID)
#include <jni.h>
typedef struct { JNIEnv *env; jobject activity; jclass cls; } AndroidSource;
static bool java_error(JNIEnv *env)
{
    jthrowable exception=(*env)->ExceptionOccurred(env);
    if(!exception)return false;
    (*env)->ExceptionClear(env);
    jclass cls=(*env)->GetObjectClass(env,exception);
    jmethodID method=cls?(*env)->GetMethodID(env,cls,"toString","()Ljava/lang/String;"):NULL;
    jstring message=method?(jstring)(*env)->CallObjectMethod(env,exception,method):NULL;
    if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
    const char *text=message?(*env)->GetStringUTFChars(env,message,NULL):NULL;
    SDL_SetError("Android source provider: %.768s",text?text:"Java operation failed");
    if(text)(*env)->ReleaseStringUTFChars(env,message,text);
    if(message)(*env)->DeleteLocalRef(env,message);
    if(cls)(*env)->DeleteLocalRef(env,cls);
    (*env)->DeleteLocalRef(env,exception);
    return true;
}
static bool android_open(AndroidSource *s)
{
    memset(s,0,sizeof *s);
    s->env=SDL_GetAndroidJNIEnv();
    s->activity=SDL_GetAndroidActivity();
    if(!s->env || !s->activity)return SDL_SetError("Android source activity unavailable");
    s->cls=(*s->env)->GetObjectClass(s->env,s->activity);
    if(java_error(s->env) || !s->cls)
    {
        (*s->env)->DeleteLocalRef(s->env,s->activity);
        return false;
    }
    return true;
}
static void android_close(AndroidSource *s)
{
    (*s->env)->DeleteLocalRef(s->env,s->cls);
    (*s->env)->DeleteLocalRef(s->env,s->activity);
}
static int android_resolve(const char *directory,const char *relative,char *out,size_t capacity)
{
    AndroidSource s;
    if(!android_open(&s))return -1;
    JNIEnv *e=s.env;
    jmethodID method=(*e)->GetStaticMethodID(e,s.cls,"sourceResolve","(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    int result=-1;
    jstring root=NULL,child=NULL,value=NULL;
    if(java_error(e) || !method)goto done;
    root=(*e)->NewStringUTF(e,directory);child=(*e)->NewStringUTF(e,relative);
    if(java_error(e) || !root || !child)goto done;
    value=(jstring)(*e)->CallStaticObjectMethod(e,s.cls,method,root,child);
    if(java_error(e))goto done;
    if(!value){result=0;goto done;}
    const char *text=(*e)->GetStringUTFChars(e,value,NULL);
    if(!text){java_error(e);goto done;}
    const size_t n=strlen(text);
    if(n>=capacity || n>=4096)SDL_SetError("Android source URI exceeds path capacity");
    else {memcpy(out,text,n+1);result=1;}
    (*e)->ReleaseStringUTFChars(e,value,text);
done:
    if(value)(*e)->DeleteLocalRef(e,value);
    if(root)(*e)->DeleteLocalRef(e,root);
    if(child)(*e)->DeleteLocalRef(e,child);
    android_close(&s);
    return result;
}
#endif
int portable_source_path_resolve(const char *directory,const char *relative,char *out,size_t capacity)
{
    if(!directory || !*directory || strlen(directory)>=4096 || !out || !capacity || !safe_relative(relative))
    {
#ifdef PORTABLE_ENABLE_SDL3
        SDL_SetError("source path: invalid root or relative name");
#endif
        return -1;
    }
    if(portable_source_path_is_uri(directory))
    {
#if defined(PORTABLE_ENABLE_SDL3) && defined(SDL_PLATFORM_ANDROID)
        return android_resolve(directory,relative,out,capacity);
#else
#ifdef PORTABLE_ENABLE_SDL3
        SDL_SetError("Android source URI unavailable on this platform");
#endif
        return -1;
#endif
    }
    char path[4096];
    const int n=snprintf(path,sizeof path,"%s/%s",directory,relative);
    if(n<0 || (size_t)n>=sizeof path || (size_t)n>=capacity)
    {
#ifdef PORTABLE_ENABLE_SDL3
        SDL_SetError("source path exceeds path capacity");
#endif
        return -1;
    }
    memcpy(out,path,(size_t)n+1);
    return 1;
}
#ifdef PORTABLE_ENABLE_SDL3
bool portable_source_path_info(const char *path,SDL_PathInfo *info)
{
    if(!path || !info)return SDL_SetError("source path: missing info output");
    if(!portable_source_path_is_uri(path))return SDL_GetPathInfo(path,info);
#if defined(SDL_PLATFORM_ANDROID)
    AndroidSource s;if(!android_open(&s))return false;
    JNIEnv *e=s.env;
    bool ok=false;
    jstring uri=NULL;jlongArray result=NULL;
    jmethodID method=(*e)->GetStaticMethodID(e,s.cls,"sourceInfo","(Ljava/lang/String;)[J");
    if(java_error(e) || !method)goto done;
    uri=(*e)->NewStringUTF(e,path);
    if(java_error(e) || !uri)goto done;
    result=(jlongArray)(*e)->CallStaticObjectMethod(e,s.cls,method,uri);
    if(java_error(e) || !result)goto done;
    if((*e)->GetArrayLength(e,result)!=3){SDL_SetError("Android source metadata invalid");goto done;}
    jlong values[3];(*e)->GetLongArrayRegion(e,result,0,3,values);
    if(java_error(e))goto done;
    if((values[0]!=1 && values[0]!=2) || values[1]<0 || values[2]<0 || values[2]>INT64_MAX/1000000)
    {SDL_SetError("Android source metadata out of bounds");goto done;}
    SDL_PathInfo staged={0};
    staged.type=values[0]==2?SDL_PATHTYPE_DIRECTORY:SDL_PATHTYPE_FILE;
    staged.size=(Uint64)values[1];staged.modify_time=(SDL_Time)(values[2]*1000000);
    *info=staged;ok=true;
done:
    if(result)(*e)->DeleteLocalRef(e,result);
    if(uri)(*e)->DeleteLocalRef(e,uri);
    android_close(&s);return ok;
#else
    return SDL_SetError("Android source URI unavailable on this platform");
#endif
}
bool portable_source_path_enumerate(const char *path,SDL_EnumerateDirectoryCallback callback,void *userdata)
{
    if(!path || !callback)return SDL_SetError("source path: missing directory callback");
    if(!portable_source_path_is_uri(path))return SDL_EnumerateDirectory(path,callback,userdata);
#if defined(SDL_PLATFORM_ANDROID)
    AndroidSource s;if(!android_open(&s))return false;
    JNIEnv *e=s.env;bool ok=false;
    jstring uri=NULL;jobjectArray names=NULL;
    jmethodID method=(*e)->GetStaticMethodID(e,s.cls,"sourceEnumerate","(Ljava/lang/String;)[Ljava/lang/String;");
    if(java_error(e) || !method)goto done;
    uri=(*e)->NewStringUTF(e,path);
    if(java_error(e) || !uri)goto done;
    names=(jobjectArray)(*e)->CallStaticObjectMethod(e,s.cls,method,uri);
    if(java_error(e) || !names)goto done;
    const jsize count=(*e)->GetArrayLength(e,names);
    if(count>4096){SDL_SetError("Android source directory exceeds4096 entries");goto done;}
    ok=true;
    for(jsize i=0;i<count;++i)
    {
        jstring name=(jstring)(*e)->GetObjectArrayElement(e,names,i);
        if(java_error(e) || !name){ok=false;break;}
        const char *text=(*e)->GetStringUTFChars(e,name,NULL);
        if(!text){java_error(e);(*e)->DeleteLocalRef(e,name);ok=false;break;}
        SDL_EnumerationResult result=callback(userdata,path,text);
        (*e)->ReleaseStringUTFChars(e,name,text);(*e)->DeleteLocalRef(e,name);
        if(result==SDL_ENUM_FAILURE){ok=false;break;}
        if(result==SDL_ENUM_SUCCESS)break;
    }
done:
    if(names)(*e)->DeleteLocalRef(e,names);
    if(uri)(*e)->DeleteLocalRef(e,uri);
    android_close(&s);return ok;
#else
    return SDL_SetError("Android source URI unavailable on this platform");
#endif
}
#if defined(SDL_PLATFORM_ANDROID)
typedef struct {SDL_DialogFileCallback callback;void *userdata;} FolderRequest;
JNIEXPORT void JNICALL Java_io_github_mkscneo_MkscActivity_nativeSourceFolderResult(
    JNIEnv *env,jclass cls,jlong token,jstring uri,jboolean failed)
{
    (void)cls;
    FolderRequest *request=(FolderRequest *)(uintptr_t)token;
    if(!request)return;
    const char *text=uri?(*env)->GetStringUTFChars(env,uri,NULL):NULL;
    const bool error=java_error(env) || (uri && !text) || failed;
    const char *paths[2]={text,NULL};
    request->callback(request->userdata,error?NULL:paths,-1);
    if(text)(*env)->ReleaseStringUTFChars(env,uri,text);
    free(request);
}
#endif
bool portable_source_path_choose_folder(SDL_DialogFileCallback callback,void *userdata)
{
#if defined(SDL_PLATFORM_ANDROID)
    if(!callback)return SDL_SetError("Android source folder callback missing");
    AndroidSource s;if(!android_open(&s))return false;
    JNIEnv *e=s.env;
    FolderRequest *request=malloc(sizeof *request);
    bool ok=false;
    if(!request){android_close(&s);return SDL_OutOfMemory();}
    *request=(FolderRequest){callback,userdata};
    jmethodID method=(*e)->GetStaticMethodID(e,s.cls,"sourceChooseFolder","(J)Z");
    if(!java_error(e) && method)
    {
        const jboolean result=(*e)->CallStaticBooleanMethod(e,s.cls,method,(jlong)(uintptr_t)request);
        ok=!java_error(e) && result;
    }
    if(!ok)free(request);
    android_close(&s);return ok;
#else
    (void)callback;(void)userdata;
    return SDL_SetError("Android source folder chooser unavailable on this platform");
#endif
}
#endif
