#include "game_data.h"
#include "json_reader.h"
#include "kart_data.h"
#include "text_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_SECTION_LIMIT 256u
#define DATA_MEMORY_LIMIT (16u*1024u*1024u)
typedef struct { uint16_t item,weight; } ItemWeight;
typedef struct { uint32_t courses[4]; uint8_t catalog,cup; } CupOrder;
#define CPU_ITEM_RECORD_LIMIT 128u
typedef struct {
    uint32_t course;
    uint16_t items[8];
    uint8_t engine,variant,driver_class;
} CpuItemRecord;
typedef struct {
    PortableKartPhysicsData karts;
    PortableTextData text,strings;
    uint32_t orbit_values[3],orbit_present;
    uint8_t orbit_delays[2][3];
    uint32_t banana_values[9],banana_present;
    uint32_t red_values[16],red_present;
    uint32_t green_values[8];
    uint8_t green_present;
    uint32_t boo_values[7];
    uint8_t boo_present;
    uint32_t status_values[3];
    uint8_t status_present;
    uint32_t mushroom_values[5];
    uint8_t mushroom_present;
    uint16_t speed_percent[3];
    ItemWeight items[3][10];
    ItemWeight gp_items[11][16];
    uint16_t gp_present;
    uint8_t points[8],points_present,retry_credits,retries_present;
    CpuItemRecord cpu_items[CPU_ITEM_RECORD_LIMIT];
    uint16_t cpu_count;
    CupOrder cups[10];
    uint8_t items_present,cup_count;
} GameData;
struct PortableGameDataOverlay {
    PortableAssetPackView view;
    PortableAssetSectionView sections[DATA_SECTION_LIMIT];
    uint8_t *owned[DATA_SECTION_LIMIT];
    size_t allocated;
};
static uint16_t le16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(v>>(i*8u));}
static int fail(char *error,const char *field,const char *reason)
{
    snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"%s: %s",field,reason);return 0;
}
static int number(PortableJsonReader *r,uint32_t low,uint32_t high,uint32_t *out)
{
    return portable_json_number(r,out)&&*out>=low&&*out<=high;
}
static int speeds(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"engine_speed_percent","expected object");
    if(portable_json_take(r,'}'))return 1;
    for(;;){
        char key[8];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))
            return fail(error,"engine_speed_percent","expected class and colon");
        const unsigned engine=strcmp(key,"50")==0?0u:strcmp(key,"100")==0?1u:strcmp(key,"150")==0?2u:3u;
        if(engine==3u)return fail(error,"engine_speed_percent","unknown class");
        if(out->speed_percent[engine]!=0u)return fail(error,"engine_speed_percent","duplicate class");
        if(!number(r,25u,200u,&value))return fail(error,"engine_speed_percent","expected integer25..200");
        out->speed_percent[engine]=(uint16_t)value;
        if(portable_json_take(r,'}'))return 1;
        if(!portable_json_take(r,','))return fail(error,"engine_speed_percent","expected comma or brace");
    }
}
static int item_table(PortableJsonReader *r,ItemWeight *table,unsigned capacity,const char *field_name,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,field_name,"expected weighted table");
    unsigned count=0u;uint32_t total=0u;
    for(;;){
        if(count>=capacity-1u||!portable_json_take(r,'{'))return fail(error,field_name,"too many entries or expected entry object");
        unsigned seen=0u;uint32_t item=0u,weight=0u;
        for(;;){
            char key[16];
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,field_name,"expected field and colon");
            const unsigned field=strcmp(key,"item")==0?1u:strcmp(key,"weight")==0?2u:0u;
            if(field==0u|| (seen&field)!=0u)return fail(error,field_name,"unknown or duplicate item/weight field");
            seen|=field;
            if(field==1u&&!number(r,0x1000u,0x100cu,&item))return fail(error,field_name,"expected ID4096..4108");
            if(field==2u&&!number(r,1u,32767u,&weight))return fail(error,field_name,"expected integer1..32767");
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return fail(error,field_name,"expected comma or brace");
        }
        if(seen!=3u||total+weight>32767u)return fail(error,field_name,"missing field or total weight exceeds32767");
        table[count++]=(ItemWeight){(uint16_t)item,(uint16_t)weight};total+=weight;
        if(portable_json_take(r,']'))break;
        if(!portable_json_take(r,','))return fail(error,field_name,"expected comma or bracket");
    }
    table[count]=(ItemWeight){0u,255u};return 1;
}
/* 43DE8 resolves one literal item for each rank; unavailable CPU item
 * callbacks must be rejected here instead of leaving inventory stuck in play. */
static int cpu_item_supported(uint32_t item)
{
    return item==0u||(item>=0x1000u&&item<=0x1005u)||item==0x1007u||
        (item>=0x100au&&item<=0x100cu);
}
static int cpu_tables(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,"cpu_items","expected record array");
    if(portable_json_take(r,']'))return 1;
    for(;;){
        if(out->cpu_count>=CPU_ITEM_RECORD_LIMIT||!portable_json_take(r,'{'))return fail(error,"cpu_items","expected at most128 records");
        CpuItemRecord next={0};unsigned seen=0u;
        for(;;){
            char key[32];uint32_t value;
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"cpu_items","expected field and colon");
            const unsigned field=strcmp(key,"course")==0?1u:strcmp(key,"engine")==0?2u:
                strcmp(key,"variant")==0?4u:strcmp(key,"driver_class")==0?8u:strcmp(key,"items")==0?16u:0u;
            if(field==0u||(seen&field)!=0u)return fail(error,"cpu_items","unknown or duplicate field");
            seen|=field;
            if(field==1u){if(!number(r,1u,UINT32_MAX,&value))return fail(error,"cpu_items.course","expected positive ID");next.course=value;}
            if(field==2u){if(!number(r,0u,2u,&value))return fail(error,"cpu_items.engine","expected0..2");next.engine=(uint8_t)value;}
            if(field==4u){if(!number(r,0u,2u,&value))return fail(error,"cpu_items.variant","expected0..2");next.variant=(uint8_t)value;}
            if(field==8u){if(!number(r,0u,7u,&value))return fail(error,"cpu_items.driver_class","expected0..7");next.driver_class=(uint8_t)value;}
            if(field==16u){
                if(!portable_json_take(r,'['))return fail(error,"cpu_items.items","expected eight rank items");
                for(unsigned rank=0u;rank<8u;++rank){
                    if((rank!=0u&&!portable_json_take(r,','))||!number(r,0u,0x100cu,&value)||!cpu_item_supported(value))
                        return fail(error,"cpu_items.items","expected0 or supported CPU item ID");
                    next.items[rank]=(uint16_t)value;
                }
                if(!portable_json_take(r,']'))return fail(error,"cpu_items.items","expected eight rank items");
            }
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return fail(error,"cpu_items","expected comma or brace");
        }
        if(seen!=31u)return fail(error,"cpu_items","course, engine, variant, driver_class and items required");
        for(unsigned i=0u;i<out->cpu_count;++i){
            const CpuItemRecord *prior=&out->cpu_items[i];
            if(prior->course==next.course&&prior->engine==next.engine&&prior->variant==next.variant&&prior->driver_class==next.driver_class)
                return fail(error,"cpu_items","duplicate record identity");
        }
        out->cpu_items[out->cpu_count++]=next;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return fail(error,"cpu_items","expected comma or bracket");
    }
}
static int gp_points(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"gp_points","expected id/awards object");
    unsigned seen=0u;
    for(;;){
        char key[16];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"gp_points","expected field and colon");
        const unsigned field=strcmp(key,"id")==0?1u:strcmp(key,"awards")==0?2u:0u;
        if(field==0u||(seen&field)!=0u)return fail(error,"gp_points","unknown or duplicate field");
        seen|=field;
        if(field==1u&&!number(r,1u,1u,&value))return fail(error,"gp_points.id","expected1");
        if(field==2u){
            if(!portable_json_take(r,'['))return fail(error,"gp_points.awards","expected eight rank awards");
            for(unsigned rank=0u;rank<8u;++rank){
                if((rank!=0u&&!portable_json_take(r,','))||!number(r,0u,63u,&value))return fail(error,"gp_points.awards","expected eight integers0..63");
                out->points[rank]=(uint8_t)value;
            }
            if(!portable_json_take(r,']'))return fail(error,"gp_points.awards","expected eight rank awards");
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"gp_points","expected comma or brace");
    }
    if(seen!=3u)return fail(error,"gp_points","id and awards required");
    out->points_present=1u;return 1;
}
static const char *const orbit_fields[]={"radius_multiplier","angle_step","animation_period","green_delays","red_delays"};
static int shell_orbit_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"shell_orbit_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[40];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"shell_orbit_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<5u;++index)if(strcmp(key,orbit_fields[index])==0){field=2u<<index;break;}
        if(!field || (seen&field))return fail(error,"shell_orbit_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"shell_orbit_rules.id","expected1");}
        else if(index<3u){
            if(!number(r,1u,index==0u?255u:65535u,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"shell_orbit_rules.%s",key);
                return fail(error,qualified,"positive integer out of range");
            }
            out->orbit_values[index]=value;out->orbit_present|=field;
        }else{
            char qualified[64];snprintf(qualified,sizeof qualified,"shell_orbit_rules.%s",key);
            if(!portable_json_take(r,'['))return fail(error,qualified,"expected three byte delays");
            for(unsigned i=0u;i<3u;++i){
                if(!number(r,0u,255u,&value))return fail(error,qualified,"expected byte delay0..255");
                out->orbit_delays[index-3u][i]=(uint8_t)value;
                if(i<2u&&!portable_json_take(r,','))return fail(error,qualified,"expected three byte delays");
            }
            if(!portable_json_take(r,']'))return fail(error,qualified,"expected three byte delays");
            out->orbit_present|=field;
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"shell_orbit_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->orbit_present)return fail(error,"shell_orbit_rules","ID and at least one override required");
    return 1;
}
static const uint32_t banana_limits[]={16777215u,32767u,16777215u,16777215u,65535u,32767u,32767u,16777215u,16777215u};
static const char *const banana_fields[]={"throw_speed_q16","cpu_flight_ticks","throw_velocity_q16","throw_gravity_q16","collision_grace_ticks","release_lock_ticks","cpu_hold_ticks","destruction_velocity_q16","destruction_gravity_q16"};
static int banana_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"banana_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[40];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"banana_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<9u;++index)if(strcmp(key,banana_fields[index])==0){field=2u<<index;break;}
        if(!field||(seen&field))return fail(error,"banana_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"banana_rules.id","expected1");}
        else {
            const uint32_t maximum=banana_limits[index];
            if(!number(r,index==4u?0u:1u,maximum,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"banana_rules.%s",key);
                return fail(error,qualified,"integer out of range");
            }
            out->banana_values[index]=value;out->banana_present|=1u<<index;
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"banana_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->banana_present)return fail(error,"banana_rules","ID and at least one override required");
    return 1;
}
static const uint32_t red_limits[]={32767u,255u,65535u,255u,32767u,32767u,255u,255u,255u,255u,255u,32767u,32767u,32767u,16777215u,16777215u};
static const char *const red_fields[]={"launch_speed_bonus","max_launch_speed","collision_grace_ticks","route_budget","release_lock_ticks","cpu_hold_ticks","flight_speed_50","flight_speed_100","flight_speed_150","acceleration","deceleration","trap_arm_ticks","trap_lifetime_ticks","trap_lock_ticks","destruction_velocity_q16","destruction_gravity_q16"};
static int red_shell_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"red_shell_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[40];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"red_shell_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<16u;++index)if(strcmp(key,red_fields[index])==0){field=2u<<index;break;}
        if(!field||(seen&field))return fail(error,"red_shell_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"red_shell_rules.id","expected1");}
        else {
            const uint32_t maximum=red_limits[index];
            if(!number(r,index==0u||index==2u?0u:1u,maximum,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"red_shell_rules.%s",key);
                return fail(error,qualified,"integer out of range");
            }
            out->red_values[index]=value;out->red_present|=1u<<index;
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"red_shell_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->red_present)return fail(error,"red_shell_rules","ID and at least one override required");
    return 1;
}
static const uint32_t green_limits[]={32767u,255u,65535u,7u,32767u,32767u,0xffffffu,0xffffffu};
static const char *const green_fields[]={"launch_speed_bonus","max_speed","collision_grace_ticks","wall_contact_budget","release_lock_ticks","cpu_hold_ticks","destruction_velocity_q16","destruction_gravity_q16"};
static int green_shell_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"green_shell_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[40];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"green_shell_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<8u;++index)if(strcmp(key,green_fields[index])==0){field=2u<<index;break;}
        if(!field||(seen&field))return fail(error,"green_shell_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"green_shell_rules.id","expected1");}
        else {
            const uint32_t maximum=green_limits[index];
            if(!number(r,index==0u||index==2u?0u:1u,maximum,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"green_shell_rules.%s",key);
                return fail(error,qualified,"integer out of range");
            }
            out->green_values[index]=value;out->green_present|=(uint8_t)(1u<<index);
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"green_shell_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->green_present)return fail(error,"green_shell_rules","ID and at least one override required");
    return 1;
}
static const char *const boo_fields[]={"invisibility_ticks","lock_ticks","orbit_ticks","orbit_step","bob_step","repel_velocity_q16","repel_gravity_q16"};
static int boo_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"boo_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[32];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"boo_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<7u;++index)if(strcmp(key,boo_fields[index])==0){field=2u<<index;break;}
        if(!field||(seen&field))return fail(error,"boo_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"boo_rules.id","expected1");}
        else {
            const uint32_t maximum=index>=5u?0xffffffu:index==2u?255u:32767u;
            if(!number(r,1u,maximum,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"boo_rules.%s",key);
                return fail(error,qualified,"integer out of range");
            }
            out->boo_values[index]=value;out->boo_present|=(uint8_t)(1u<<index);
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"boo_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->boo_present)return fail(error,"boo_rules","ID and at least one override required");
    return 1;
}
static const char *const status_fields[]={"star_player_ticks","star_cpu_ticks","lightning_ticks"};
static int item_status_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"item_status_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[32];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"item_status_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<3u;++index)if(strcmp(key,status_fields[index])==0){field=2u<<index;break;}
        if(!field||(seen&field))return fail(error,"item_status_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"item_status_rules.id","expected1");}
        else {
            const uint32_t maximum=32767u;
            if(!number(r,index==2u?48u:1u,maximum,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"item_status_rules.%s",key);
                return fail(error,qualified,"integer out of range");
            }
            out->status_values[index]=value;out->status_present|=(uint8_t)(1u<<index);
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"item_status_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->status_present)return fail(error,"item_status_rules","ID and at least one override required");
    return 1;
}
static const char *const mushroom_fields[]={"boost_ticks","single_actor_ticks","golden_actor_ticks","acceleration_units","max_speed_q16"};
static int mushroom_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"mushroom_rules","expected object");
    unsigned seen=0u;
    for(;;){
        char key[32];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"mushroom_rules","expected field and colon");
        unsigned field=strcmp(key,"id")==0?1u:0u,index=0u;
        if(!field)for(;index<5u;++index)if(strcmp(key,mushroom_fields[index])==0){field=2u<<index;break;}
        if(!field||(seen&field))return fail(error,"mushroom_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u){if(!number(r,1u,1u,&value))return fail(error,"mushroom_rules.id","expected1");}
        else {
            const uint32_t maximum=index==4u?UINT32_C(0x07e00000):index==3u?32767u:65535u;
            if(!number(r,index==1u?0u:1u,maximum,&value)){
                char qualified[64];snprintf(qualified,sizeof qualified,"mushroom_rules.%s",key);
                return fail(error,qualified,"integer out of range");
            }
            out->mushroom_values[index]=value;out->mushroom_present|=(uint8_t)(1u<<index);
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"mushroom_rules","expected comma or brace");
    }
    if(!(seen&1u)||!out->mushroom_present)return fail(error,"mushroom_rules","ID and at least one override required");
    return 1;
}
static int gp_rules(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'{'))return fail(error,"gp_rules","expected id/retry_credits object");
    unsigned seen=0u;
    for(;;){
        char key[24];uint32_t value;
        if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"gp_rules","expected field and colon");
        const unsigned field=strcmp(key,"id")==0?1u:strcmp(key,"retry_credits")==0?2u:0u;
        if(field==0u||(seen&field)!=0u)return fail(error,"gp_rules","unknown or duplicate field");
        seen|=field;
        if(field==1u&&!number(r,1u,1u,&value))return fail(error,"gp_rules.id","expected1");
        if(field==2u){
            if(!number(r,0u,3u,&value))return fail(error,"gp_rules.retry_credits","expected0..3");
            out->retry_credits=(uint8_t)value;
        }
        if(portable_json_take(r,'}'))break;
        if(!portable_json_take(r,','))return fail(error,"gp_rules","expected comma or brace");
    }
    if(seen!=3u)return fail(error,"gp_rules","id and retry_credits required");
    out->retries_present=1u;return 1;
}
static int gp_tables(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,"gp_items","expected table records");
    if(portable_json_take(r,']'))return 1;
    for(unsigned count=0u;;++count){
        if(count>=11u||!portable_json_take(r,'{'))return fail(error,"gp_items","expected at most11 records");
        unsigned seen=0u;uint32_t id=0u;ItemWeight table[16]={0};
        for(;;){
            char key[16];
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"gp_items","expected field and colon");
            const unsigned field=strcmp(key,"id")==0?1u:strcmp(key,"weights")==0?2u:0u;
            if(field==0u||(seen&field)!=0u)return fail(error,"gp_items","unknown or duplicate field");
            seen|=field;
            if(field==1u&&!number(r,1u,11u,&id))return fail(error,"gp_items.id","expected1..11");
            if(field==2u&&!item_table(r,table,16u,"gp_items.weights",error))return 0;
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return fail(error,"gp_items","expected comma or brace");
        }
        if(seen!=3u)return fail(error,"gp_items","id and weights required");
        const uint16_t bit=(uint16_t)(1u<<(id-1u));
        if(out->gp_present&bit)return fail(error,"gp_items.id","duplicate table ID");
        out->gp_present|=bit;memcpy(out->gp_items[id-1u],table,sizeof table);
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return fail(error,"gp_items","expected comma or bracket");
    }
}
static int cup_orders(PortableJsonReader *r,GameData *out,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,"cups","expected array");
    if(portable_json_take(r,']'))return 1;
    for(;;){
        if(out->cup_count>=10u||!portable_json_take(r,'{'))return fail(error,"cups","expected at most10 cup objects");
        CupOrder next={0};unsigned seen=0u;
        for(;;){
            char key[16];uint32_t value;
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"cups","expected field and colon");
            const unsigned field=strcmp(key,"catalog")==0?1u:strcmp(key,"cup")==0?2u:strcmp(key,"courses")==0?4u:0u;
            if(field==0u||(seen&field)!=0u)return fail(error,"cups","unknown or duplicate field");
            seen|=field;
            if(field==1u){if(!number(r,1u,2u,&value))return fail(error,"cups.catalog","expected1 or2");next.catalog=(uint8_t)value;}
            if(field==2u){if(!number(r,0u,4u,&value))return fail(error,"cups.cup","expected0..4");next.cup=(uint8_t)value;}
            if(field==4u){
                if(!portable_json_take(r,'['))return fail(error,"cups.courses","expected four distinct IDs");
                for(unsigned i=0u;i<4u;++i){
                    if((i!=0u&&!portable_json_take(r,','))||!number(r,1u,UINT32_MAX,&next.courses[i]))return fail(error,"cups.courses","expected four distinct IDs");
                    for(unsigned j=0u;j<i;++j)if(next.courses[j]==next.courses[i])return fail(error,"cups.courses","duplicate ID");
                }
                if(!portable_json_take(r,']'))return fail(error,"cups.courses","expected four distinct IDs");
            }
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return fail(error,"cups","expected comma or brace");
        }
        if(seen!=7u)return fail(error,"cups","catalog, cup and courses required");
        for(unsigned i=0u;i<out->cup_count;++i)if(out->cups[i].catalog==next.catalog&&out->cups[i].cup==next.cup)return fail(error,"cups","duplicate cup");
        out->cups[out->cup_count++]=next;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return fail(error,"cups","expected comma or bracket");
    }
}
typedef enum
{
    GAME_DATA_UNKNOWN = 0u,
    GAME_DATA_ENGINE_SPEEDS = 1u << 0u,
    GAME_DATA_BATTLE_ITEMS = 1u << 1u,
    GAME_DATA_CUPS = 1u << 2u,
    GAME_DATA_GP_ITEMS = 1u << 3u,
    GAME_DATA_GP_POINTS = 1u << 4u,
    GAME_DATA_CPU_ITEMS = 1u << 5u,
    GAME_DATA_KART_PHYSICS = 1u << 6u,
    GAME_DATA_GP_RULES = 1u << 7u,
    GAME_DATA_MUSHROOM_RULES = 1u << 8u,
    GAME_DATA_TEXT = 1u << 9u,
    GAME_DATA_ITEM_STATUS_RULES = 1u << 10u,
    GAME_DATA_BOO_RULES = 1u << 11u,
    GAME_DATA_GREEN_SHELL_RULES = 1u << 12u,
    GAME_DATA_RED_SHELL_RULES = 1u << 13u,
    GAME_DATA_BANANA_RULES = 1u << 14u,
    GAME_DATA_STRINGS = 1u << 15u,
    GAME_DATA_SHELL_ORBIT_RULES = 1u << 16u,
} GameDataField;

static GameDataField game_data_field(const char *key)
{
    static const struct
    {
        const char *name;
        GameDataField field;
    } fields[] = {
        {"engine_speed_percent", GAME_DATA_ENGINE_SPEEDS},
        {"battle_items", GAME_DATA_BATTLE_ITEMS},
        {"cups", GAME_DATA_CUPS},
        {"gp_items", GAME_DATA_GP_ITEMS},
        {"gp_points", GAME_DATA_GP_POINTS},
        {"cpu_items", GAME_DATA_CPU_ITEMS},
        {"kart_physics", GAME_DATA_KART_PHYSICS},
        {"gp_rules", GAME_DATA_GP_RULES},
        {"mushroom_rules", GAME_DATA_MUSHROOM_RULES},
        {"text", GAME_DATA_TEXT},
        {"item_status_rules", GAME_DATA_ITEM_STATUS_RULES},
        {"boo_rules", GAME_DATA_BOO_RULES},
        {"green_shell_rules", GAME_DATA_GREEN_SHELL_RULES},
        {"red_shell_rules", GAME_DATA_RED_SHELL_RULES},
        {"banana_rules", GAME_DATA_BANANA_RULES},
        {"strings", GAME_DATA_STRINGS},
        {"shell_orbit_rules", GAME_DATA_SHELL_ORBIT_RULES},
    };
    for (size_t i = 0u; i < sizeof fields / sizeof fields[0]; ++i)
        if (strcmp(key, fields[i].name) == 0)
            return fields[i].field;
    return GAME_DATA_UNKNOWN;
}

static int parse_game_data(const PortableAssetSectionView *json, GameData *out, char *error)
{
    if (json->version != 1u || json->size == 0u || json->size > 16384u)
        return fail(error, "data", "expected JSON v1 up to16384 bytes");
    PortableJsonReader r = {(const char *)json->bytes, (const char *)json->bytes + json->size};
    unsigned seen = 0u;
    if (!portable_json_take(&r, '{'))
        return fail(error, "data", "expected object");
    if (!portable_json_take(&r, '}'))
        for (;;)
        {
            char key[64];
            if (!portable_json_string(&r, key, sizeof key) || !portable_json_take(&r, ':'))
                return fail(error, "data", "expected field and colon");
            const GameDataField field = game_data_field(key);
            if (field == GAME_DATA_UNKNOWN)
                return fail(error, key, "unknown field");
            if ((seen & field) != 0u)
                return fail(error, key, "duplicate field");
            seen |= field;
            if (field == GAME_DATA_SHELL_ORBIT_RULES && !shell_orbit_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_STRINGS && !portable_ui_strings_parse(&r, &out->strings, error))
                return 0;
            if (field == GAME_DATA_BANANA_RULES && !banana_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_RED_SHELL_RULES && !red_shell_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_GREEN_SHELL_RULES && !green_shell_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_BOO_RULES && !boo_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_ITEM_STATUS_RULES && !item_status_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_TEXT && !portable_text_data_parse(&r, &out->text, error))
                return 0;
            if (field == GAME_DATA_ENGINE_SPEEDS && !speeds(&r, out, error))
                return 0;
            if (field == GAME_DATA_BATTLE_ITEMS)
            {
                if (!portable_json_take(&r, '['))
                    return fail(error, key, "expected three balloon tables");
                for (unsigned i = 0u; i < 3u; ++i)
                {
                    if (i != 0u && !portable_json_take(&r, ','))
                        return fail(error, key, "expected three balloon tables");
                    if (!item_table(&r, out->items[i], 10u, "battle_items", error))
                        return 0;
                }
                if (!portable_json_take(&r, ']'))
                    return fail(error, key, "expected three balloon tables");
                out->items_present = 1u;
            }
            if (field == GAME_DATA_CUPS && !cup_orders(&r, out, error))
                return 0;
            if (field == GAME_DATA_GP_ITEMS && !gp_tables(&r, out, error))
                return 0;
            if (field == GAME_DATA_GP_POINTS && !gp_points(&r, out, error))
                return 0;
            if (field == GAME_DATA_CPU_ITEMS && !cpu_tables(&r, out, error))
                return 0;
            if (field == GAME_DATA_KART_PHYSICS &&
                !portable_kart_data_parse(&r, &out->karts, error))
                return 0;
            if (field == GAME_DATA_GP_RULES && !gp_rules(&r, out, error))
                return 0;
            if (field == GAME_DATA_MUSHROOM_RULES && !mushroom_rules(&r, out, error))
                return 0;
            if (portable_json_take(&r, '}'))
                break;
            if (!portable_json_take(&r, ','))
                return fail(error, key, "expected comma or brace");
        }
    portable_json_whitespace(&r);
    if (r.at != r.end)
        return fail(error, "data", "trailing data");
    if (!out->speed_percent[0] && !out->speed_percent[1] && !out->speed_percent[2] &&
        !out->items_present && !out->cup_count && !out->gp_present && !out->points_present &&
        !out->cpu_count && !out->karts.count && !out->retries_present && !out->mushroom_present &&
        !out->text.count && !out->status_present && !out->boo_present && !out->green_present &&
        !out->red_present && !out->banana_present && !out->strings.count && !out->orbit_present)
        return fail(error, "data", "no overrides requested");
    return 1;
}
/* Each normalized section owns its copy; borrowed menu art keeps the effective
 * input pack alive through PortableModSet. Failure never publishes a layer. */
static uint8_t *copy_section(PortableGameDataOverlay *out,const PortableAssetSectionView *source,char *error)
{
    if(out->view.section_count>=DATA_SECTION_LIMIT||source->size>DATA_MEMORY_LIMIT-out->allocated){fail(error,"data","normalized table budget exceeded");return NULL;}
    uint8_t *bytes=malloc(source->size);
    if(bytes==NULL){fail(error,"data","allocation failed");return NULL;}
    memcpy(bytes,source->bytes,source->size);
    const uint32_t index=out->view.section_count++;
    out->owned[index]=bytes;out->sections[index]=*source;out->sections[index].bytes=bytes;
    out->sections[index].flags|=PORTABLE_ASSET_SECTION_DETERMINISTIC;
    out->allocated+=source->size;return bytes;
}
static int normalize_motion(PortableGameDataOverlay *out,const PortableAssetSectionView *source,const GameData *data,char *error)
{
    static const uint32_t header[]={0x31544f4du,1024u,64u,8u,8u,8u,256u,10u,16u};
    const size_t expected=36u+4096u+128u+48u+512u+20u+32u+32768u+416u+52u+3u*452u;
    const char *field=portable_kart_data_find(&data->karts,source->content_id)!=NULL?"kart_physics":"engine_speed_percent";
    if((source->version!=6u&&source->version!=7u&&source->version!=8u)||
       source->size!=expected+(source->version==8u?428u:source->version==7u?16u:0u))
        return fail(error,field,"reimport motion v6/v7/v8");
    if(source->version>=7u)for(unsigned i=0u;i<8u;++i){
        const uint16_t value=le16(source->bytes+expected+i*2u);
        if(value>0u&&value<0x8000u)return fail(error,field,"invalid finished-driver deceleration");
    }
    for(unsigned i=0u;i<9u;++i)if(le32(source->bytes+i*4u)!=header[i])return fail(error,field,"invalid motion header");
    uint8_t *bytes=copy_section(out,source,error);if(bytes==NULL)return 0;
    for(unsigned engine=0u;engine<3u;++engine){
        const uint32_t percent=data->speed_percent[engine];if(percent==0u)continue;
        uint8_t *profile=bytes+expected-3u*452u+engine*452u;
        for(unsigned i=128u;i<288u;++i){
            const uint32_t value=(uint32_t)profile[i]*percent/100u;
            if(value>255u)return fail(error,"engine_speed_percent","CPU target speed exceeds byte range");
            profile[i]=(uint8_t)value;
        }
        const uint64_t base=(uint64_t)le32(profile+416u)*percent/100u;
        if(base==0u||base>UINT32_C(0x07e00000))return fail(error,"engine_speed_percent","player base speed exceeds2016 Q16");
        put32(profile+416u,(uint32_t)base);
        for(unsigned offset=420u;offset<452u;offset+=2u){
            const uint16_t old=le16(profile+offset);if(old>=0x8000u)continue;
            const uint32_t value=(uint32_t)old*percent/100u;
            if(value>32767u)return fail(error,"engine_speed_percent","terrain limit exceeds halfword range");
            put16(profile+offset,(uint16_t)value);
        }
    }
    const PortableKartPhysicsOverride *kart=portable_kart_data_find(&data->karts,source->content_id);
    if(kart!=NULL)portable_kart_data_apply(kart,bytes);
    return 1;
}
static int normalize_material_items(PortableGameDataOverlay *out,const PortableAssetSectionView *source,const GameData *data,char *error)
{
    static const size_t sizes[]={1440u,1448u,1456u,1464u,1470u,1502u,3680u,3804u,3808u,3832u,3844u};
    if(source->version<6u||source->version>16u||source->size!=sizes[source->version-6u]||le32(source->bytes)!=0x3154414du||le32(source->bytes+4u)!=256u)
        return fail(error,data->items_present?"battle_items":(data->orbit_present&48u)?"shell_orbit_rules":"cpu_items","reimport material v6..16");
    if((data->orbit_present&48u)&&source->version<10u)return fail(error,"shell_orbit_rules","reimport material v10..16");
    if(data->items_present&&source->version<13u)return fail(error,"battle_items","reimport material v13..16");
    uint8_t *bytes=copy_section(out,source,error);if(bytes==NULL)return 0;
    for(unsigned family=0u;family<2u;++family)if(data->orbit_present&(16u<<family))
        memcpy(bytes+1464u+family*3u,data->orbit_delays[family],3u);
    if(data->items_present)for(unsigned table=0u;table<3u;++table)for(unsigned row=0u;row<10u;++row){
        uint8_t *entry=bytes+3680u+table*40u+row*4u;
        put16(entry,data->items[table][row].item);put16(entry+2u,data->items[table][row].weight);
    }
    for(unsigned i=0u;i<data->cpu_count;++i){
        const CpuItemRecord *record=&data->cpu_items[i];if(record->course!=source->content_id)continue;
        const size_t index=((size_t)record->engine*3u+record->variant)*64u+(size_t)record->driver_class*8u;
        for(unsigned rank=0u;rank<8u;++rank)put16(bytes+288u+(index+rank)*2u,record->items[rank]);
    }
    return 1;
}
static int copy_course_image(PortableGameDataOverlay *out,const PortableAssetPackView *base,uint32_t from,uint32_t to,char *error)
{
    if(from==to)return 1;
    static const uint32_t image_bases[]={0x23300u,0x23400u,0x23500u};
    for(unsigned i=0u;i<3u;++i){
        if(out->view.section_count>=DATA_SECTION_LIMIT)return fail(error,"cups.courses","normalized section budget exceeded");
        PortableAssetSectionView source;
        if(portable_asset_pack_find_section(base,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,image_bases[i]+from,&source)!=PORTABLE_ASSET_PACK_OK)
            return fail(error,"cups.courses","missing thumbnail/name art");
        source.content_id=image_bases[i]+to;
        out->sections[out->view.section_count++]=source;
    }
    return 1;
}
static int normalize_catalog(PortableGameDataOverlay *out,const PortableAssetPackView *base,const PortableAssetSectionView *source,const GameData *data,char *error)
{
    if(source->version!=2u||source->size<8u||le32(source->bytes)!=0x31544143u)return fail(error,"cups","reimport catalog v2");
    const uint32_t count=le32(source->bytes+4u);
    if(count==0u||count>20u||source->size!=8u+(size_t)count*12u)return fail(error,"cups","invalid catalog layout");
    uint32_t slots=0u;
    for(uint32_t index=0u;index<count;++index){
        const uint8_t *row=source->bytes+8u+index*12u;
        if(le32(row)==0u||row[4]>=5u||row[5]>=4u||le32(row+8u)>=(source->content_id==1u?44u:52u))
            return fail(error,"cups.courses","invalid original course metadata");
        const uint32_t bit=1u<<((unsigned)row[4]*4u+row[5]);
        if(slots&bit)return fail(error,"cups.courses","duplicate original cup slot");
        slots|=bit;
        for(uint32_t previous=0u;previous<index;++previous)
            if(le32(source->bytes+8u+previous*12u)==le32(row))return fail(error,"cups.courses","duplicate original course ID");
    }
    uint8_t *bytes=copy_section(out,source,error);if(bytes==NULL)return 0;
    for(unsigned c=0u;c<data->cup_count;++c){
        const CupOrder *cup=&data->cups[c];if(cup->catalog!=source->content_id)continue;
        if((slots&(15u<<((unsigned)cup->cup*4u)))!=(15u<<((unsigned)cup->cup*4u)))
            return fail(error,"cups.courses","expected four original course slots");
        for(unsigned track=0u;track<4u;++track){
            const uint8_t *original=NULL;uint8_t *target=NULL;
            for(uint32_t index=0u;index<count;++index){
                const uint8_t *row=source->bytes+8u+index*12u;
                if(le32(row)==cup->courses[track])original=row;
                if(row[4]==cup->cup&&row[5]==track)target=bytes+8u+index*12u;
            }
            if(original==NULL)return fail(error,"cups.courses","course ID is absent from the selected catalog");
            memcpy(target,original,12u);target[4]=cup->cup;target[5]=(uint8_t)track;
        }
    }
    /* The frontend maps each manifest once. Validate the entire final catalog,
     * so exchanging courses across cups requires updating both source slots. */
    for(uint32_t index=0u;index<count;++index){
        const uint8_t *row=bytes+8u+index*12u;
        for(uint32_t previous=0u;previous<index;++previous){
            const uint8_t *other=bytes+8u+previous*12u;
            if(le32(row)==le32(other))return fail(error,"cups.courses","course repeated across cup slots; update both cups");
            if(row[4]==other[4]&&le32(row+8u)==le32(other+8u))return fail(error,"cups.courses","source header repeated within cup");
        }
    }
    const uint32_t first=(source->content_id-1u)*20u;
    for(uint32_t index=0u;index<count;++index){
        const uint8_t *row=bytes+8u+index*12u;
        for(uint32_t original=0u;original<count;++original){
            const uint8_t *before=source->bytes+8u+original*12u;
            if(le32(before)!=le32(row))continue;
            if(!copy_course_image(out,base,first+(uint32_t)before[4]*4u+before[5],first+(uint32_t)row[4]*4u+row[5],error))return 0;
            break;
        }
    }
    return 1;
}
/* Keep each wire schema and its validation order together. The caller owns
 * publication and discards every copied section if any normalization fails. */
static int normalize_orbit_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                                 const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 20u || le32(section.bytes) != 0x3142524fu ||
        le32(section.bytes + 4u) != 3u)
    {
        fail(error, "shell_orbit_rules", "reimport orbit rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned i = 0u; i < 3u; ++i)
    {
        if (data->orbit_present & (2u << i))
            put32(bytes + 8u + i * 4u, data->orbit_values[i]);
        const uint32_t value = le32(bytes + 8u + i * 4u);
        if (value == 0u || value > (i == 0u ? 255u : 65535u))
        {
            fail(error, "shell_orbit_rules", "normalized parameter out of range");
            return 0;
        }
    }
    return 1;
}

static int normalize_banana_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                                  const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 44u || le32(section.bytes) != 0x314e4142u ||
        le32(section.bytes + 4u) != 9u)
    {
        fail(error, "banana_rules", "reimport banana rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned field = 0u; field < 9u; ++field)
    {
        if (data->banana_present & (1u << field))
            put32(bytes + 8u + field * 4u, data->banana_values[field]);
        const uint32_t value = le32(bytes + 8u + field * 4u);
        if ((field != 4u && value == 0u) || value > banana_limits[field])
        {
            fail(error, "banana_rules", "normalized parameter out of range");
            return 0;
        }
    }
    const int64_t ticks = le32(bytes + 28u);
    const int64_t peak = le32(bytes + 16u) / le32(bytes + 20u) + 1u,
                  sample = peak < ticks ? peak : ticks;
    if (INT64_C(65536) + sample * le32(bytes + 16u) -
            sample * (sample - 1) * le32(bytes + 20u) / 2 >
        INT32_MAX)
    {
        fail(error, "banana_rules.release_lock_ticks", "flight overflows before lock clears");
        return 0;
    }
    if (INT64_C(65536) + ticks * le32(bytes + 16u) - ticks * (ticks - 1) * le32(bytes + 20u) / 2 <=
        0)
    {
        fail(error, "banana_rules.release_lock_ticks", "must clear before landing");
        return 0;
    }
    return 1;
}

static int normalize_red_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                               const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 72u || le32(section.bytes) != 0x31485352u ||
        le32(section.bytes + 4u) != 16u)
    {
        fail(error, "red_shell_rules", "reimport red shell rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned field = 0u; field < 16u; ++field)
    {
        if (data->red_present & (1u << field))
            put32(bytes + 8u + field * 4u, data->red_values[field]);
        const uint32_t value = le32(bytes + 8u + field * 4u);
        if ((field != 0u && field != 2u && value == 0u) || value > red_limits[field])
        {
            fail(error, "red_shell_rules", "normalized parameter out of range");
            return 0;
        }
    }
    if (le32(bytes + 24u) > le32(bytes + 52u))
    {
        fail(error, "red_shell_rules.release_lock_ticks", "must not exceed trap_arm_ticks");
        return 0;
    }
    return 1;
}

static int normalize_green_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                                 const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 40u || le32(section.bytes) != 0x31485347u ||
        le32(section.bytes + 4u) != 8u)
    {
        fail(error, "green_shell_rules", "reimport green shell rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned field = 0u; field < 8u; ++field)
    {
        if (data->green_present & (1u << field))
            put32(bytes + 8u + field * 4u, data->green_values[field]);
        const uint32_t value = le32(bytes + 8u + field * 4u);
        if ((field != 0u && field != 2u && value == 0u) || value > green_limits[field])
        {
            fail(error, "green_shell_rules", "normalized parameter out of range");
            return 0;
        }
    }
    return 1;
}

static int normalize_boo_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                               const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 36u || le32(section.bytes) != 0x314f4f42u ||
        le32(section.bytes + 4u) != 7u)
    {
        fail(error, "boo_rules", "reimport Boo rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned field = 0u; field < 7u; ++field)
    {
        if (data->boo_present & (1u << field))
            put32(bytes + 8u + field * 4u, data->boo_values[field]);
        const uint32_t value = le32(bytes + 8u + field * 4u), maximum = field >= 5u   ? 0xffffffu
                                                                        : field == 2u ? 255u
                                                                                      : 32767u;
        if (value == 0u || value > maximum)
        {
            fail(error, "boo_rules", "normalized parameter out of range");
            return 0;
        }
    }
    return 1;
}

static int normalize_status_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                                  const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 20u || le32(section.bytes) != 0x31544153u ||
        le32(section.bytes + 4u) != 3u)
    {
        fail(error, "item_status_rules", "reimport item status rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned field = 0u; field < 3u; ++field)
    {
        if (data->status_present & (1u << field))
            put32(bytes + 8u + field * 4u, data->status_values[field]);
        const uint32_t value = le32(bytes + 8u + field * 4u);
        if (value < (field == 2u ? 48u : 1u) || value > 32767u)
        {
            fail(error, "item_status_rules", "normalized duration out of range");
            return 0;
        }
    }
    return 1;
}

static int normalize_mushroom_rules(PortableGameDataOverlay *out, PortableAssetSectionView section,
                                    const GameData *data, char *error)
{
    if (section.version != 1u || section.size != 28u ||
        le32(section.bytes) != UINT32_C(0x3153554d) || le32(section.bytes + 4u) != 5u)
    {
        fail(error, "mushroom_rules", "reimport mushroom rules v1");
        return 0;
    }
    uint8_t *bytes = copy_section(out, &section, error);
    if (bytes == NULL)
        return 0;
    for (unsigned field = 0u; field < 5u; ++field)
        if (data->mushroom_present & (1u << field))
            put32(bytes + 8u + field * 4u, data->mushroom_values[field]);
    for (unsigned field = 0u; field < 5u; ++field)
    {
        const uint32_t value = le32(bytes + 8u + field * 4u), maximum = field == 4u
                                                                            ? UINT32_C(0x07e00000)
                                                                        : field == 3u ? 32767u
                                                                                      : 65535u;
        if ((field != 1u && value == 0u) || value > maximum)
        {
            fail(error, "mushroom_rules", "normalized rule out of range");
            return 0;
        }
    }
    return 1;
}

PortableGameDataOverlay *portable_game_data_apply(const PortableAssetPackView *base,
                                                  const PortableAssetSectionView *json,
                                                  char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    if (error == NULL)
        return NULL;
    error[0] = '\0';
    GameData data = {0};
    if(base==NULL||json==NULL||json->bytes==NULL){fail(error,"data","invalid argument");return NULL;}
    if(json->kind!=PORTABLE_ASSET_SECTION_GAME_DATA_JSON||json->content_id!=1u){fail(error,"data","expected game-data JSON section ID1");return NULL;}
    if(!parse_game_data(json,&data,error))return NULL;
    PortableGameDataOverlay *out=calloc(1u,sizeof *out);
    if(out==NULL){fail(error,"data","allocation failed");return NULL;}
    out->view.resolved_sections=out->sections;
    unsigned found_motion=0u,found_items=0u,found_cups=0u,found_gp=0u,found_points=0u,found_cpu=0u,found_karts=0u,found_mushroom=0u,found_status=0u,found_boo=0u,found_green=0u,found_red=0u,found_banana=0u,found_orbit=0u,found_orbit_material=0u;
    for(uint32_t index=0u;index<base->section_count;++index){
        PortableAssetSectionView section;
        if(portable_asset_pack_section_at(base,index,&section)!=PORTABLE_ASSET_PACK_OK){fail(error,"data","invalid base section");goto reject;}
        if(section.kind==PORTABLE_ASSET_SECTION_MOTION_TABLES&&(data.speed_percent[0]||data.speed_percent[1]||data.speed_percent[2]||portable_kart_data_find(&data.karts,section.content_id)!=NULL)){
            if(!normalize_motion(out,&section,&data,error))goto reject;
            ++found_motion;found_karts+=portable_kart_data_find(&data.karts,section.content_id)!=NULL;
        }else if(section.kind==PORTABLE_ASSET_SECTION_MATERIAL_FLAGS){
            unsigned needed=0u;
            for(unsigned i=0u;i<data.cpu_count;++i)needed+=data.cpu_items[i].course==section.content_id;
            if(needed||data.items_present||(data.orbit_present&48u)){
                if(!normalize_material_items(out,&section,&data,error))goto reject;
                found_cpu+=needed;if(data.items_present)++found_items;
                if(data.orbit_present&48u)++found_orbit_material;
            }
        }else if(section.kind==PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS&&section.content_id>=1u&&section.content_id<=11u&&
            (data.gp_present&(1u<<(section.content_id-1u)))){
            if(section.version!=1u||section.size!=72u||le32(section.bytes)!=0x31575047u||le32(section.bytes+4u)!=16u){fail(error,"gp_items","reimport player odds v1");goto reject;}
            uint8_t *bytes=copy_section(out,&section,error);if(bytes==NULL)goto reject;
            for(unsigned row=0u;row<16u;++row){
                const ItemWeight *entry=&data.gp_items[section.content_id-1u][row];
                put16(bytes+8u+row*4u,entry->item);put16(bytes+10u+row*4u,entry->weight);
            }
            found_gp|=1u<<(section.content_id-1u);
        }else if(section.kind==PORTABLE_ASSET_SECTION_SHELL_ORBIT_RULES&&section.content_id==1u&&data.orbit_present){
            if(!normalize_orbit_rules(out,section,&data,error))goto reject;
            found_orbit=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_BANANA_RULES&&section.content_id==1u&&data.banana_present){
            if(!normalize_banana_rules(out,section,&data,error))goto reject;
            found_banana=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_RED_SHELL_RULES&&section.content_id==1u&&data.red_present){
            if(!normalize_red_rules(out,section,&data,error))goto reject;
            found_red=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_GREEN_SHELL_RULES&&section.content_id==1u&&data.green_present){
            if(!normalize_green_rules(out,section,&data,error))goto reject;
            found_green=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_BOO_RULES&&section.content_id==1u&&data.boo_present){
            if(!normalize_boo_rules(out,section,&data,error))goto reject;
            found_boo=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_ITEM_STATUS_RULES&&section.content_id==1u&&data.status_present){
            if(!normalize_status_rules(out,section,&data,error))goto reject;
            found_status=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_MUSHROOM_RULES&&section.content_id==1u&&data.mushroom_present){
            if(!normalize_mushroom_rules(out,section,&data,error))goto reject;
            found_mushroom=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_GP_SCORING&&section.content_id==1u&&(data.points_present||data.retries_present)){
            const char *field=data.retries_present?"gp_rules":"gp_points";
            if(!((section.version==1u&&section.size==16u)||(section.version==2u&&section.size==20u)||
                  (section.version==3u&&section.size==112u))||
               le32(section.bytes)!=0x31545047u||le32(section.bytes+4u)!=8u||
               (section.version>=2u&&le32(section.bytes+16u)>3u)){fail(error,field,"reimport scoring v1/v2/v3");goto reject;}
            for(unsigned rank=0u;rank<8u;++rank)if(section.bytes[8u+rank]>63u){fail(error,field,"invalid rank award");goto reject;}
            uint8_t normalized[112]={0};memcpy(normalized,section.bytes,section.size);
            if(data.retries_present){
                if(section.version==1u){section.version=2u;section.size=20u;}
                put32(normalized+16u,data.retry_credits);
            }
            if(data.points_present)memcpy(normalized+8u,data.points,8u);
            section.bytes=normalized;
            if(copy_section(out,&section,error)==NULL)goto reject;
            found_points=1u;
        }else if(section.kind==PORTABLE_ASSET_SECTION_COURSE_CATALOG){
            unsigned needed=0u;for(unsigned c=0u;c<data.cup_count;++c)needed|=data.cups[c].catalog==section.content_id;
            if(needed){if(!normalize_catalog(out,base,&section,&data,error))goto reject;found_cups|=1u<<section.content_id;}
        }
    }
    /* Text addresses final graphic slots after course art moves in this layer. */
    for(unsigned i=0u;i<data.text.count;++i){
        const PortableTextOverride *record=&data.text.records[i];
        PortableAssetSectionView source;
        if(portable_asset_pack_find_section(&out->view,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,record->id,&source)!=PORTABLE_ASSET_PACK_OK&&
           portable_asset_pack_find_section(base,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,record->id,&source)!=PORTABLE_ASSET_PACK_OK){fail(error,"text.id","frontend graphic ID absent");goto reject;}
        /* Avoid duplicate sections when a cup already moved this target. */
        unsigned slot=out->view.section_count;
        for(unsigned j=0u;j<out->view.section_count;++j)if(out->sections[j].kind==source.kind&&out->sections[j].content_id==source.content_id){slot=j;break;}
        if(slot<out->view.section_count){
            if(source.size>DATA_MEMORY_LIMIT-out->allocated){fail(error,"text","normalized table budget exceeded");goto reject;}
            uint8_t *bytes=malloc(source.size);
            if(bytes==NULL){fail(error,"text","allocation failed");goto reject;}
            if(!portable_text_data_render(record,&source,bytes,error)){free(bytes);goto reject;}
            free(out->owned[slot]);out->owned[slot]=bytes;out->sections[slot].bytes=bytes;out->allocated+=source.size;
            out->sections[slot].flags|=PORTABLE_ASSET_SECTION_DETERMINISTIC;
        }else{
            uint8_t *bytes=copy_section(out,&source,error);if(bytes==NULL)goto reject;
            if(!portable_text_data_render(record,&source,bytes,error))goto reject;
        }
    }
    for(unsigned i=0u;i<data.strings.count;++i){
        const PortableTextOverride *record=&data.strings.records[i];
        uint8_t payload[67]={0};const size_t length=strlen(record->value);
        memcpy(payload,"UIT1",4u);put32(payload+4u,(uint32_t)length);
        memcpy(payload+8u,record->value,length+1u);
        const PortableAssetSectionView source={PORTABLE_ASSET_SECTION_UI_TEXT,
            record->id,1u,PORTABLE_ASSET_SECTION_REQUIRED|PORTABLE_ASSET_SECTION_DETERMINISTIC,
            1u,payload,9u+length};
        if(copy_section(out,&source,error)==NULL)goto reject;
    }
    if(data.orbit_present&&!found_orbit){fail(error,"shell_orbit_rules","base pack lacks orbit rule record; reimport");goto reject;}
    if((data.orbit_present&48u)&&!found_orbit_material){fail(error,"shell_orbit_rules","base pack lacks material delay records; reimport");goto reject;}
    if(data.banana_present&&!found_banana){fail(error,"banana_rules","base pack lacks a banana rule record; reimport");goto reject;}
    if(data.red_present&&!found_red){fail(error,"red_shell_rules","base pack lacks a red/Spiny shell rule record; reimport");goto reject;}
    if(data.green_present&&!found_green){fail(error,"green_shell_rules","base pack lacks a green shell rule record; reimport");goto reject;}
    if(data.boo_present&&!found_boo){fail(error,"boo_rules","base pack lacks a Boo rule record; reimport");goto reject;}
    if(data.status_present&&!found_status){fail(error,"item_status_rules","base pack lacks a status rule record; reimport");goto reject;}
    if(data.mushroom_present&&!found_mushroom){fail(error,"mushroom_rules","base pack lacks a mushroom rule record; reimport");goto reject;}
    if(found_karts!=data.karts.count){fail(error,"kart_physics.id","base pack lacks a requested motion record");goto reject;}
    if(found_cpu!=data.cpu_count){fail(error,"cpu_items.course","base pack lacks a requested material record");goto reject;}
    if(data.retries_present&&!found_points){fail(error,"gp_rules","base pack lacks a scoring record; reimport");goto reject;}
    if(data.points_present&&!found_points){fail(error,"gp_points","base pack lacks a scoring record; reimport");goto reject;}
    if(found_gp!=data.gp_present){fail(error,"gp_items","base pack lacks a requested odds table; reimport");goto reject;}
    unsigned required_cups=0u;for(unsigned c=0u;c<data.cup_count;++c)required_cups|=1u<<data.cups[c].catalog;
    if(((data.speed_percent[0]||data.speed_percent[1]||data.speed_percent[2])&&!found_motion)||(data.items_present&&!found_items)||required_cups!=found_cups){fail(error,"data","base pack lacks a requested table");goto reject;}
    return out;
reject:
    portable_game_data_free(out);return NULL;
}
const PortableAssetPackView *portable_game_data_pack(const PortableGameDataOverlay *overlay){return overlay!=NULL?&overlay->view:NULL;}
size_t portable_game_data_bytes(const PortableGameDataOverlay *overlay){return overlay!=NULL?overlay->allocated+sizeof *overlay:0u;}
void portable_game_data_free(PortableGameDataOverlay *overlay)
{
    if(overlay==NULL)return;
    for(uint32_t i=0u;i<overlay->view.section_count;++i)free(overlay->owned[i]);
    free(overlay);
}
