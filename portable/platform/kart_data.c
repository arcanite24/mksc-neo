#include "kart_data.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

enum { ID_FIELD=1u, TIME_TRIAL_FIELD=512u, ENGINES_FIELD=1024u };
enum { ENGINE_ID=1u, BASE_SPEED=2u, TERRAIN_LIMITS=4u, CPU_ACCELERATION=8u, CPU_TARGETS=16u, CPU_HEADING=32u };
typedef struct {const char *name;size_t offset,count;int32_t maximum;} SignedArrayField;
static const SignedArrayField arrays[]={
    {"auto_acceleration",offsetof(PortableKartPhysicsOverride,auto_acceleration),64u,32767},
    {"brake",offsetof(PortableKartPhysicsOverride,brake),8u,32767},
    {"brake_accelerate",offsetof(PortableKartPhysicsOverride,brake_accelerate),8u,32767},
    {"forward_response",offsetof(PortableKartPhysicsOverride,forward_response),8u,32767},
    {"local_acceleration",offsetof(PortableKartPhysicsOverride,local_acceleration),256u,32767},
    {"steering_delta",offsetof(PortableKartPhysicsOverride,steering_delta),10u,32767},
    {"steering_response",offsetof(PortableKartPhysicsOverride,steering_response),16u,32767},
    {"forward_target_response",offsetof(PortableKartPhysicsOverride,forward_target_response),8u,0}
};
static int fail(char *error,const char *field,const char *reason)
{
    snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"kart_physics.%s: %s",field,reason);return 0;
}
static int integer(PortableJsonReader *r,int32_t *out)
{
    portable_json_whitespace(r);
    const int negative=r->at<r->end&&*r->at=='-';
    if(negative){++r->at;if(r->at==r->end||*r->at<'0'||*r->at>'9')return 0;}
    uint32_t magnitude;
    if(!portable_json_number(r,&magnitude)||magnitude>(negative?UINT32_C(2147483648):INT32_MAX))return 0;
    *out=negative?(magnitude==UINT32_C(2147483648)?INT32_MIN:-(int32_t)magnitude):(int32_t)magnitude;
    return 1;
}
static int array(PortableJsonReader *r,void *out,size_t count,unsigned width,int32_t low,int32_t high,const char *field,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,field,"expected fixed-size integer array");
    for(size_t i=0u;i<count;++i){
        int32_t value;
        if((i!=0u&&!portable_json_take(r,','))||!integer(r,&value)||value<low||value>high)
            return fail(error,field,"wrong array length or integer out of range");
        if(width==1u)((uint8_t *)out)[i]=(uint8_t)value;
        else ((int16_t *)out)[i]=(int16_t)value;
    }
    return portable_json_take(r,']')?1:fail(error,field,"wrong array length");
}
static int profile(PortableJsonReader *r,PortableKartEngineOverride *out,int engine,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,engine?"engines":"time_trial","expected object");
    for(;;){
        char key[32];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"profile","expected field and colon");
        const unsigned field=strcmp(key,"id")==0?ENGINE_ID:strcmp(key,"base_speed_q16")==0?BASE_SPEED:
            strcmp(key,"terrain_limits")==0?TERRAIN_LIMITS:strcmp(key,"cpu_acceleration")==0?CPU_ACCELERATION:
            strcmp(key,"cpu_target_speeds")==0?CPU_TARGETS:strcmp(key,"cpu_heading_steps")==0?CPU_HEADING:0u;
        if(field==0u||(!engine&&(field==ENGINE_ID||field>=CPU_ACCELERATION))||(out->present&field)!=0u)
            return fail(error,key,"unknown, duplicate or unavailable profile field");
        out->present|=(uint8_t)field;
        if(field==ENGINE_ID){if(!portable_json_number(r,&value)||value>2u)return fail(error,key,"expected0..2");out->id=(uint8_t)value;}
        if(field==BASE_SPEED){if(!portable_json_number(r,&value)||value==0u||value>0x07e00000u)return fail(error,key,"expected positive Q16 speed up to2016 units");out->base_speed_q16=value;}
        if(field==TERRAIN_LIMITS&&!array(r,out->terrain_limits,6u,2u,0,32767,key,error))return 0;
        if(field==CPU_ACCELERATION&&!array(r,out->cpu_acceleration,128u,1u,0,255,key,error))return 0;
        if(field==CPU_TARGETS&&!array(r,out->cpu_target_speeds,160u,1u,0,255,key,error))return 0;
        if(field==CPU_HEADING&&!array(r,out->cpu_heading_steps,40u,1u,0,255,key,error))return 0;
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,key,"expected comma or brace");
    }
    if((engine&&(out->present&ENGINE_ID)==0u)||(out->present&~ENGINE_ID)==0u)return fail(error,"profile","engine ID and at least one override required");
    return 1;
}
static int engines(PortableJsonReader *r,PortableKartPhysicsOverride *out,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,"engines","expected engine array");
    unsigned seen=0u;
    for(;;){
        if(out->engine_count>=3u)return fail(error,"engines","expected at most three engines");
        PortableKartEngineOverride *next=&out->engines[out->engine_count];
        if(!profile(r,next,1,error))return 0;
        const unsigned bit=1u<<next->id;
        if(seen&bit)return fail(error,"engines.id","duplicate engine ID");
        seen|=bit;++out->engine_count;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return fail(error,"engines","expected comma or bracket");
    }
}
const PortableKartPhysicsOverride *portable_kart_data_find(const PortableKartPhysicsData *data,uint32_t id)
{
    for(unsigned i=0u;i<data->count;++i)if(data->records[i].id==id)return &data->records[i];
    return NULL;
}
int portable_kart_data_parse(PortableJsonReader *r,PortableKartPhysicsData *out,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    if(!portable_json_take(r,'['))return fail(error,"records","expected array");
    if(portable_json_take(r,']'))return 1;
    for(;;){
        if(out->count>=PORTABLE_KART_DATA_LIMIT||!portable_json_take(r,'{'))return fail(error,"records","expected at most16 objects");
        PortableKartPhysicsOverride *next=&out->records[out->count];
        for(;;){
            char key[40];uint32_t id;
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"record","expected field and colon");
            unsigned field=strcmp(key,"id")==0?ID_FIELD:strcmp(key,"time_trial")==0?TIME_TRIAL_FIELD:strcmp(key,"engines")==0?ENGINES_FIELD:0u;
            size_t index=0u;
            if(field==0u)for(;index<sizeof arrays/sizeof arrays[0];++index)if(strcmp(key,arrays[index].name)==0){field=2u<<index;break;}
            if(field==0u||(next->present&field)!=0u)return fail(error,key,"unknown or duplicate field");
            next->present|=(uint16_t)field;
            if(field==ID_FIELD){if(!portable_json_number(r,&id)||id==0u)return fail(error,key,"expected positive motion ID");next->id=id;}
            else if(field==TIME_TRIAL_FIELD){if(!profile(r,&next->time_trial,0,error))return 0;}
            else if(field==ENGINES_FIELD){if(!engines(r,next,error))return 0;}
            else if(!array(r,(uint8_t *)next+arrays[index].offset,arrays[index].count,2u,-32768,arrays[index].maximum,key,error))return 0;
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return fail(error,key,"expected comma or brace");
        }
        if((next->present&ID_FIELD)==0u||(next->present&~ID_FIELD)==0u)return fail(error,"record","ID and at least one override required");
        if(portable_kart_data_find(out,next->id)!=NULL)return fail(error,"id","duplicate motion ID");
        ++out->count;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return fail(error,"records","expected comma or bracket");
    }
}
static void put16(uint8_t *p,uint16_t value){p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));}
static void signed_array(uint8_t *p,const int16_t *values,size_t count)
{
    for(size_t i=0u;i<count;++i)put16(p+i*2u,(uint16_t)values[i]);
}
static void apply_profile(const PortableKartEngineOverride *record,uint8_t *p,int engine)
{
    if(record->present&BASE_SPEED)put32(p+(engine?416u:0u),record->base_speed_q16);
    if(record->present&TERRAIN_LIMITS)signed_array(p+(engine?440u:24u),record->terrain_limits,6u);
    if(record->present&CPU_ACCELERATION)memcpy(p,record->cpu_acceleration,128u);
    if(record->present&CPU_TARGETS)memcpy(p+128u,record->cpu_target_speeds,160u);
    if(record->present&CPU_HEADING)memcpy(p+288u,record->cpu_heading_steps,40u);
}
void portable_kart_data_apply(const PortableKartPhysicsOverride *record,uint8_t *motion)
{
    static const size_t offsets[]={4132u,4260u,4276u,4292u,4308u,4820u,4840u,38092u};
    for(size_t index=0u;index<sizeof arrays/sizeof arrays[0];++index)
        if(record->present&(2u<<index))signed_array(motion+offsets[index],(const int16_t *)((const uint8_t *)record+arrays[index].offset),arrays[index].count);
    if(record->present&TIME_TRIAL_FIELD)apply_profile(&record->time_trial,motion+38056u,0);
    for(unsigned i=0u;i<record->engine_count;++i)apply_profile(&record->engines[i],motion+38108u+(size_t)record->engines[i].id*452u,1);
}
