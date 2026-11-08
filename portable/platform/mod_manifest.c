#include "mod_manifest.h"
#include "json_reader.h"
#include <stdio.h>
#include <string.h>

int portable_mod_id_valid(const char *id)
{
    if(id==NULL||id[0]=='\0')return 0;
    size_t i=0u;
    for(;id[i]!='\0';++i){
        const char c=id[i];
        if(i+1u>=PORTABLE_MOD_ID_CAPACITY||!((c>='a'&&c<='z')||(c>='0'&&c<='9')||(i>0u&&(c=='-'||c=='_'))))return 0;
    }
    return 1;
}
static int fail(char *error,const char *field,const char *reason)
{
    if(error!=NULL)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s: %s",field,reason);
    return 0;
}
static int ids(PortableJsonReader *r,char list[][PORTABLE_MOD_ID_CAPACITY],uint8_t *count)
{
    if(!portable_json_take(r,'['))return 0;
    if(portable_json_take(r,']'))return 1;
    for(;;){
        if(*count>=PORTABLE_MOD_DEPENDENCY_LIMIT||!portable_json_string(r,list[*count],PORTABLE_MOD_ID_CAPACITY)||!portable_mod_id_valid(list[*count]))return 0;
        for(unsigned i=0u;i<*count;++i)if(strcmp(list[i],list[*count])==0)return 0;
        ++*count;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return 0;
    }
}
static int pack_path_valid(const char *path)
{
    if(path[0]=='\0'||path[0]=='/')return 0;
    const char *segment=path;
    for(const char *c=path;;++c){
        if(*c=='\\'||*c==':'||((unsigned char)*c<32u&&*c!='\0'))return 0;
        if(*c=='/'||*c=='\0'){
            const size_t length=(size_t)(c-segment);
            if(length==0u||(length==1u&&segment[0]=='.')||(length==2u&&segment[0]=='.'&&segment[1]=='.'))return 0;
            if(*c=='\0')return 1;
            segment=c+1;
        }
    }
}
static int targets(PortableJsonReader *reader, uint8_t *mask)
{
    if (!portable_json_take(reader, '[') || portable_json_take(reader, ']')) return 0;
    *mask = 0u;
    for (;;) {
        char name[8];
        if (!portable_json_string(reader, name, sizeof name)) return 0;
        const uint8_t bit = strcmp(name, "2d") == 0 ? PORTABLE_MOD_TARGET_2D
            : strcmp(name, "3d") == 0 ? PORTABLE_MOD_TARGET_3D : 0u;
        if (!bit || (*mask & bit)) return 0;
        *mask = (uint8_t)(*mask | bit);
        if (portable_json_take(reader, ']')) return 1;
        if (!portable_json_take(reader, ',')) return 0;
    }
}
static int version_valid(const char *text)
{
    unsigned parts=0u;
    for(const char *c=text;;){
        if(*c<'0'||*c>'9')return 0;
        const char first=*c++;unsigned digits=1u;
        while(*c>='0'&&*c<='9'){++c;++digits;}
        if(first=='0'&&digits>1u)return 0;
        ++parts;
        if(*c=='\0')return parts==3u;
        if(*c++!='.'||parts>=3u)return 0;
    }
}
int portable_mod_manifest_parse(const char *json,size_t size,PortableModManifest *out,char *error)
{
    static const char *const fields[]={"id","name","version","api_version","pack","depends","load_after","script","targets"};
    if(error!=NULL)error[0]='\0';
    if(json==NULL||out==NULL||size>16384u)return fail(error,"manifest","invalid size");
    PortableModManifest next={0};PortableJsonReader r={json,json+size};unsigned seen=0u;
    next.targets = PORTABLE_MOD_TARGET_2D;
    if(!portable_json_take(&r,'{'))return fail(error,"manifest","expected object");
    if(!portable_json_take(&r,'}'))for(;;){
        char key[64];
        if(!portable_json_string(&r,key,sizeof key)||!portable_json_take(&r,':'))return fail(error,"manifest","expected field and colon");
        unsigned field=0u;while(field<9u&&strcmp(key,fields[field])!=0)++field;
        if(field==9u)return fail(error,key,"unknown field");
        if(seen&(1u<<field))return fail(error,key,"duplicate field");
        seen|=1u<<field;
        int ok=0;
        if(field==0u)ok=portable_json_string(&r,next.id,sizeof next.id)&&portable_mod_id_valid(next.id);
        if(field==1u)ok=portable_json_string(&r,next.name,sizeof next.name)&&next.name[0]!='\0';
        if(field==2u)ok=portable_json_string(&r,next.version,sizeof next.version)&&version_valid(next.version);
        if(field==3u){uint32_t version=0u;ok=portable_json_number(&r,&version)&&version==1u;}
        if(field==4u)ok=portable_json_string(&r,next.pack,sizeof next.pack)&&pack_path_valid(next.pack);
        if(field==5u)ok=ids(&r,next.depends,&next.dependency_count);
        if(field==6u)ok=ids(&r,next.load_after,&next.after_count);
        if(field==7u){
            ok=portable_json_string(&r,next.script,sizeof next.script)&&pack_path_valid(next.script);
            const size_t length=strlen(next.script);
            ok=ok&&length>4u&&strcmp(next.script+length-4u,".lua")==0;
        }
        if(field==8u)ok=targets(&r,&next.targets);
        if(!ok)return fail(error,key,"invalid value or unsupported API version");
        if(portable_json_take(&r,'}'))break;
        if(!portable_json_take(&r,','))return fail(error,key,"expected comma or closing brace");
    }
    portable_json_whitespace(&r);
    if(r.at!=r.end)return fail(error,"manifest","trailing data");
    if((seen&15u)!=15u||(seen&(1u<<4|1u<<7))==0u)
        return fail(error,"manifest","id, name, version, api_version and a pack or script are required");
    for(unsigned i=0u;i<next.dependency_count;++i)if(strcmp(next.depends[i],next.id)==0)return fail(error,"depends","self dependency");
    for(unsigned i=0u;i<next.after_count;++i)if(strcmp(next.load_after[i],next.id)==0)return fail(error,"load_after","self dependency");
    *out=next;return 1;
}
