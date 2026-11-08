#include "settings.h"
#include "json_reader.h"
#include "mod_manifest.h"
#include "file_replace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[PORTABLE_SETTING_COUNT]={
    "window_scale","fullscreen","vsync","filter","master_volume",
    "steering_deadzone","short_transitions",
    "key_accelerate","key_brake","key_left","key_right","key_hop","key_item",
    "key_aim_up","key_aim_down","pad_accelerate","pad_brake","pad_hop","pad_item","interpolate","render_fps","internal_scale","fit_scaling","aspect_width",
    "key2_accelerate","key2_brake","key2_left","key2_right","key2_hop","key2_item","key2_aim_up","key2_aim_down",
    "rumble","input_display","mirror_mode","fix_doppler_wrap","wide_ui_backgrounds","driving_profile","classic_menus","touch_controls","artwork_lod","race_renderer",
    "smooth_3d","dynamic_camera","camera_shake","drift_sparks","boost_effects","kart_trails","immersive_camera",
    "hd2d_dof","hd2d_bloom","hd2d_ao","hd2d_grade","hd2d_fog","hd2d_light","hd2d_scale"};
#define BIT(n) (UINT64_C(1)<<(n))
_Static_assert(PORTABLE_SETTING_COUNT+4u<64u,"settings duplicate-field mask capacity");
/* SDL3's stable scancode value; keep the settings codec independent of SDL. */
enum { SETTINGS_ANDROID_BACK_SCANCODE = 282 };

static uint8_t *switch_field(PortableSettings *s,PortableSetting field)
{
    switch(field){
    case PORTABLE_SETTING_FULLSCREEN:return &s->fullscreen;
    case PORTABLE_SETTING_VSYNC:return &s->vsync;
    case PORTABLE_SETTING_FILTER:return &s->linear_filter;
    case PORTABLE_SETTING_SHORT_TRANSITIONS:return &s->short_transitions;
    case PORTABLE_SETTING_INTERPOLATE:return &s->interpolate;
    case PORTABLE_SETTING_FIT_SCALING:return &s->fit_scaling;
    case PORTABLE_SETTING_RUMBLE:return &s->rumble;
    case PORTABLE_SETTING_INPUT_DISPLAY:return &s->input_display;
    case PORTABLE_SETTING_MIRROR:return &s->mirror;
    case PORTABLE_SETTING_FIX_DOPPLER_WRAP:return &s->fix_doppler_wrap;
    case PORTABLE_SETTING_WIDE_UI_BACKGROUNDS:return &s->wide_ui_backgrounds;
    case PORTABLE_SETTING_CLASSIC_MENUS:return &s->classic_menus;
    case PORTABLE_SETTING_TOUCH_CONTROLS:return &s->touch_controls;
    case PORTABLE_SETTING_SMOOTH_3D:return &s->smooth_3d;
    case PORTABLE_SETTING_DYNAMIC_CAMERA:return &s->dynamic_camera;
    case PORTABLE_SETTING_CAMERA_SHAKE:return &s->camera_shake;
    case PORTABLE_SETTING_DRIFT_SPARKS:return &s->drift_sparks;
    case PORTABLE_SETTING_BOOST_EFFECTS:return &s->boost_effects;
    case PORTABLE_SETTING_KART_TRAILS:return &s->kart_trails;
    case PORTABLE_SETTING_IMMERSIVE_CAMERA:return &s->immersive_camera;
    case PORTABLE_SETTING_HD2D_DOF:return &s->hd2d_dof;
    case PORTABLE_SETTING_HD2D_BLOOM:return &s->hd2d_bloom;
    case PORTABLE_SETTING_HD2D_AO:return &s->hd2d_ao;
    case PORTABLE_SETTING_HD2D_GRADE:return &s->hd2d_grade;
    case PORTABLE_SETTING_HD2D_FOG:return &s->hd2d_fog;
    case PORTABLE_SETTING_HD2D_LIGHT:return &s->hd2d_light;
    case PORTABLE_SETTING_HD2D_SCALE:return &s->hd2d_scale;
    default:return NULL;
    }
}

static int key_field(PortableSetting field)
{
    if(field>=PORTABLE_SETTING_KEY_ACCELERATE&&field<=PORTABLE_SETTING_KEY_AIM_DOWN)
        return (int)(field-PORTABLE_SETTING_KEY_ACCELERATE);
    if(field>=PORTABLE_SETTING_KEY2_ACCELERATE&&field<=PORTABLE_SETTING_KEY2_AIM_DOWN)
        return 8+(int)(field-PORTABLE_SETTING_KEY2_ACCELERATE);
    return -1;
}

void portable_settings_defaults(PortableSettings *s)
{
    if(s!=NULL)*s=(PortableSettings){.window_scale=4u,.internal_scale=1u,.aspect_width=240u,.vsync=1u,
        .master_volume=100u,.steering_deadzone=8000u,
        .smooth_3d=1u,.dynamic_camera=1u,.camera_shake=1u,.drift_sparks=1u,.boost_effects=1u,.kart_trails=1u,
        .keys={29u,81u,80u,79u,225u,6u,82u,81u,26u,22u,4u,7u,20u,8u,26u,22u},.pad_buttons={0u,1u,10u,9u}};
#ifdef __ANDROID__
    /* Handhelds and phones: races fill the whole screen. */
    if(s!=NULL){s->aspect_width=0u;s->fit_scaling=1u;}
#endif
}

const char *portable_setting_name(PortableSetting field)
{
    return (unsigned)field<PORTABLE_SETTING_COUNT?names[field]:"unknown";
}

uint32_t portable_setting_get(const PortableSettings *s,PortableSetting field)
{
    if(s==NULL)return 0u;
    if(key_field(field)>=0)return s->keys[key_field(field)];
    if(field>=PORTABLE_SETTING_PAD_ACCELERATE&&field<=PORTABLE_SETTING_PAD_ITEM)
        return s->pad_buttons[field-PORTABLE_SETTING_PAD_ACCELERATE];
    const uint8_t *flag=switch_field((PortableSettings *)s,field);
    if(flag!=NULL)return *flag;
    switch(field){
    case PORTABLE_SETTING_ASPECT_WIDTH:return s->aspect_width;
    case PORTABLE_SETTING_DRIVING_PROFILE:return s->driving_profile;
    case PORTABLE_SETTING_ARTWORK_LOD:return s->artwork_lod;
    case PORTABLE_SETTING_RACE_RENDERER:return s->race_renderer;
    case PORTABLE_SETTING_INTERNAL_SCALE:return s->internal_scale;
    case PORTABLE_SETTING_RENDER_FPS:return s->render_fps;
    case PORTABLE_SETTING_WINDOW_SCALE:return s->window_scale;
    case PORTABLE_SETTING_VOLUME:return s->master_volume;
    case PORTABLE_SETTING_DEADZONE:return s->steering_deadzone;
    default:return 0u;
    }
}

int portable_setting_set(PortableSettings *s,PortableSetting field,uint32_t value)
{
    if(s==NULL)return 0;
    if(key_field(field)>=0){
        if(value<4u||value>=512u||value==19u||value==21u||value==27u||value==SETTINGS_ANDROID_BACK_SCANCODE||(value>=40u&&value<=44u))return 0; /* Fixed pause/restart/rescue/menu shortcuts. */
        s->keys[key_field(field)]=(uint16_t)value;return 1;
    }
    if(field>=PORTABLE_SETTING_PAD_ACCELERATE&&field<=PORTABLE_SETTING_PAD_ITEM){
        if(value>25u||value==2u||(value>=4u&&value<=6u)||(value>=11u&&value<=14u))return 0;
        s->pad_buttons[field-PORTABLE_SETTING_PAD_ACCELERATE]=(uint8_t)value;return 1;
    }
    uint8_t *flag=switch_field(s,field);
    if(flag!=NULL){
        if(value>1u)return 0;
        *flag=(uint8_t)value;return 1;
    }
    switch(field){
    case PORTABLE_SETTING_RACE_RENDERER:
        if(value>1u)return 0;
        s->race_renderer=(uint8_t)value;break;
    case PORTABLE_SETTING_ARTWORK_LOD:
        if(value>2u)return 0;
        s->artwork_lod=(uint8_t)value;break;
    case PORTABLE_SETTING_DRIVING_PROFILE:
        if(value>2u)return 0;
        s->driving_profile=(uint8_t)value;break;
    case PORTABLE_SETTING_ASPECT_WIDTH:
        if(value!=0u&&(value<240u||value>1024u))return 0;
        s->aspect_width=(uint16_t)value;break;
    case PORTABLE_SETTING_INTERNAL_SCALE:
        if(value<1u||value>8u)return 0;
        s->internal_scale=(uint8_t)value;break;
    case PORTABLE_SETTING_RENDER_FPS:
        if(value>1000u)return 0;
        s->render_fps=(uint16_t)value;break;
    case PORTABLE_SETTING_WINDOW_SCALE:
        if(value<1u||value>8u)return 0;
        s->window_scale=(uint8_t)value;break;
    case PORTABLE_SETTING_VOLUME:
        if(value>100u)return 0;
        s->master_volume=(uint8_t)value;break;
    case PORTABLE_SETTING_DEADZONE:
        if(value<1u||value>30000u)return 0;
        s->steering_deadzone=(uint16_t)value;break;
    default:return 0;
    }
    return 1;
}

static int fail(char *error,const char *detail)
{
    if(error!=NULL)snprintf(error,PORTABLE_SETTINGS_ERROR_CAPACITY,"%s",detail);
    return 0;
}
static int directory_valid(const char *path)
{
    const char *end=memchr(path,'\0',4096u);
    if(!end || end==path)return 0;
    for(const unsigned char *p=(const unsigned char *)path;p<(const unsigned char *)end;){
        const unsigned c=*p++;
        if(c<32u || c==127u)return 0;
        if(c<128u)continue;
        const unsigned tail=c>=0xc2u&&c<=0xdfu?1u:c>=0xe0u&&c<=0xefu?2u:c>=0xf0u&&c<=0xf4u?3u:0u;
        if(!tail || (size_t)((const unsigned char *)end-p)<tail)return 0;
        if((c==0xe0u && *p<0xa0u)||(c==0xedu && *p>=0xa0u)||
            (c==0xf0u && *p<0x90u)||(c==0xf4u && *p>=0x90u))return 0;
        for(unsigned i=0;i<tail;++i)if((p[i]&0xc0u)!=0x80u)return 0;
        p+=tail;
    }
    return 1;
}

static int store_url_valid(const char *url)
{
    if(!directory_valid(url) || strlen(url)>=1024u)return 0;
    for(const unsigned char *p=(const unsigned char *)url;*p;++p)
        if(*p>=127u || *p=='\\')return 0;
    return (!strncmp(url,"https://",8u) && url[8] && url[8]!='/') ||
        (!strncmp(url,"file:///",8u) && url[8]);
}

static int directory_json_string(PortableJsonReader *r,char *out,size_t capacity)
{
    if(!portable_json_take(r,'"'))return 0;
    size_t used=0;
    while(r->at<r->end){
        unsigned value=(unsigned char)*r->at++;
        if(value=='"'){out[used]='\0';return 1;}
        if(value=='\\'){
            if(r->at==r->end)return 0;
            value=(unsigned char)*r->at++;
            if(value=='u'){
                uint32_t code=0;
                for(unsigned i=0;i<4u;++i){
                    if(r->at==r->end)return 0;
                    const unsigned c=(unsigned char)*r->at++;
                    const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10u:c>='A'&&c<='F'?c-'A'+10u:16u;
                    if(digit==16u)return 0;
                    code=code*16u+digit;
                }
                if(code>=0xd800u && code<=0xdbffu){
                    if(r->end-r->at<6 || r->at[0]!='\\' || r->at[1]!='u')return 0;
                    r->at+=2;uint32_t low=0;
                    for(unsigned i=0;i<4u;++i){
                        const unsigned c=(unsigned char)*r->at++;
                        const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10u:c>='A'&&c<='F'?c-'A'+10u:16u;
                        if(digit==16u)return 0;
                        low=low*16u+digit;
                    }
                    if(low<0xdc00u || low>0xdfffu)return 0;
                    code=0x10000u+((code-0xd800u)<<10u)+(low-0xdc00u);
                }else if(code>=0xdc00u && code<=0xdfffu)return 0;
                unsigned char bytes[4];size_t count;
                if(code<0x80u){bytes[0]=(unsigned char)code;count=1;}
                else if(code<0x800u){bytes[0]=(unsigned char)(0xc0u|(code>>6u));bytes[1]=(unsigned char)(0x80u|(code&63u));count=2;}
                else if(code<0x10000u){bytes[0]=(unsigned char)(0xe0u|(code>>12u));bytes[1]=(unsigned char)(0x80u|((code>>6u)&63u));bytes[2]=(unsigned char)(0x80u|(code&63u));count=3;}
                else{bytes[0]=(unsigned char)(0xf0u|(code>>18u));bytes[1]=(unsigned char)(0x80u|((code>>12u)&63u));bytes[2]=(unsigned char)(0x80u|((code>>6u)&63u));bytes[3]=(unsigned char)(0x80u|(code&63u));count=4;}
                if(!code || used+count>=capacity)return 0;
                memcpy(out+used,bytes,count);used+=count;continue;
            }
            if(value!='"'&&value!='\\'&&value!='/')return 0;
        }
        if(value<32u || used+1u>=capacity)return 0;
        out[used++]=(char)value;
    }
    return 0;
}

static int write_directory(FILE *file,const char *path)
{
    if(fputc('"',file)==EOF)return 0;
    for(const unsigned char *p=(const unsigned char *)path;*p;++p){
        if((*p=='"'||*p=='\\') && fputc('\\',file)==EOF)return 0;
        if(fputc(*p,file)==EOF)return 0;
    }
    return fputc('"',file)!=EOF;
}

static int settings_parse_candidate(const char *json,size_t size,PortableSettings *settings,char *error)
{
    if(error!=NULL)error[0]='\0';
    if(json==NULL||settings==NULL||size>65536u)return fail(error,"invalid settings size");
    PortableSettings *next=settings;
    PortableJsonReader r={json,json+size};uint64_t seen=0u;
    if(!portable_json_take(&r,'{'))return fail(error,"expected settings object");
    portable_json_whitespace(&r);
    if(r.at<r.end&&*r.at=='}')++r.at;
    else for(;;){
        char key[40],text[16];uint32_t value=0u;
        if(!portable_json_string(&r,key,sizeof key)||!portable_json_take(&r,':'))return fail(error,"expected setting name and colon");
        unsigned field=0u;
        while(field<PORTABLE_SETTING_COUNT&&strcmp(key,names[field])!=0)++field;
        const int version=strcmp(key,"version")==0;
        const int mods=strcmp(key,"mods")==0;
        if(strcmp(key,"store_urls")==0){
            const uint64_t bit=BIT(PORTABLE_SETTING_COUNT+3u);
            if(seen&bit)return fail(error,"duplicate store_urls setting");
            seen|=bit;
            if(!portable_json_take(&r,'['))return fail(error,"store_urls must be an array");
            if(!portable_json_take(&r,']'))for(;;){
                if(next->store_url_count>=4u ||
                    !directory_json_string(&r,next->store_urls[next->store_url_count],1024u) ||
                    !store_url_valid(next->store_urls[next->store_url_count]))
                    return fail(error,"invalid store URL or too many stores");
                for(unsigned i=0;i<next->store_url_count;++i)
                    if(!strcmp(next->store_urls[i],next->store_urls[next->store_url_count]))
                        return fail(error,"duplicate store URL");
                ++next->store_url_count;
                if(portable_json_take(&r,']'))break;
                if(!portable_json_take(&r,','))return fail(error,"expected comma in store_urls");
            }
            if(portable_json_take(&r,'}'))break;
            if(!portable_json_take(&r,','))return fail(error,"expected comma after store_urls");
            continue;
        }
        if(strcmp(key,"asset_sources")==0){
            const uint64_t bit=BIT(PORTABLE_SETTING_COUNT+4u);
            if(seen&bit)return fail(error,"duplicate asset_sources setting");
            seen|=bit;
            if(!portable_json_take(&r,'['))return fail(error,"asset_sources must be an array");
            for(unsigned i=0;i<PORTABLE_ASSET_SOURCE_COUNT;++i){
                if(i && !portable_json_take(&r,','))return fail(error,"asset_sources needs three entries");
                if(!portable_json_string(&r,next->asset_sources[i],PORTABLE_MOD_ID_CAPACITY)||
                   (next->asset_sources[i][0] && strcmp(next->asset_sources[i],"@base") && !portable_mod_id_valid(next->asset_sources[i])))
                    return fail(error,"invalid asset source ID");
            }
            if(!portable_json_take(&r,']'))return fail(error,"asset_sources needs three entries");
            if(portable_json_take(&r,'}'))break;
            if(!portable_json_take(&r,','))return fail(error,"expected comma after asset_sources");
            continue;
        }
        if(strcmp(key,"mod_directories")==0){
            const uint64_t bit=BIT(PORTABLE_SETTING_COUNT+2u);
            if(seen&bit)return fail(error,"duplicate mod_directories setting");
            seen|=bit;
            if(!portable_json_take(&r,'['))return fail(error,"mod_directories must be an array");
            if(!portable_json_take(&r,']'))for(;;){
                if(next->mod_directory_count>=4u ||
                    !directory_json_string(&r,next->mod_directories[next->mod_directory_count],4096u) ||
                    !directory_valid(next->mod_directories[next->mod_directory_count]))
                    return fail(error,"invalid mod directory or too many directories");
                for(unsigned i=0;i<next->mod_directory_count;++i)
                    if(!strcmp(next->mod_directories[i],next->mod_directories[next->mod_directory_count]))
                        return fail(error,"duplicate mod directory");
                ++next->mod_directory_count;
                if(portable_json_take(&r,']'))break;
                if(!portable_json_take(&r,','))return fail(error,"expected comma in mod_directories array");
            }
            portable_json_whitespace(&r);
            if(r.at<r.end&&*r.at=='}'){++r.at;break;}
            if(!portable_json_take(&r,','))return fail(error,"expected comma or closing brace");
            continue;
        }
        if(mods){
            const uint64_t bit=BIT(PORTABLE_SETTING_COUNT+1u);
            if(seen&bit)return fail(error,"duplicate mods setting");
            seen|=bit;
            if(!portable_json_take(&r,'['))return fail(error,"mods must be an array");
            if(!portable_json_take(&r,']'))for(;;){
                if(next->mod_count>=PORTABLE_MOD_LIMIT||!portable_json_string(&r,next->mods[next->mod_count],PORTABLE_MOD_ID_CAPACITY)||!portable_mod_id_valid(next->mods[next->mod_count]))return fail(error,"invalid mod id or too many mods");
                for(unsigned i=0u;i<next->mod_count;++i)if(strcmp(next->mods[i],next->mods[next->mod_count])==0)return fail(error,"duplicate mod id");
                ++next->mod_count;
                if(portable_json_take(&r,']'))break;
                if(!portable_json_take(&r,','))return fail(error,"expected comma in mods array");
            }
            portable_json_whitespace(&r);
            if(r.at<r.end&&*r.at=='}'){++r.at;break;}
            if(!portable_json_take(&r,','))return fail(error,"expected comma or closing brace");
            continue;
        }
        if(field==PORTABLE_SETTING_COUNT&&!version)return fail(error,"unknown setting name");
        const uint64_t bit=BIT(version?PORTABLE_SETTING_COUNT:field);
        if(seen&bit)return fail(error,"duplicate setting name");
        seen|=bit;
        if(version||field==PORTABLE_SETTING_RACE_RENDERER||field==PORTABLE_SETTING_ARTWORK_LOD||field==PORTABLE_SETTING_DRIVING_PROFILE||field==PORTABLE_SETTING_ASPECT_WIDTH||field==PORTABLE_SETTING_INTERNAL_SCALE||field==PORTABLE_SETTING_WINDOW_SCALE||field==PORTABLE_SETTING_VOLUME||field==PORTABLE_SETTING_DEADZONE||field==PORTABLE_SETTING_RENDER_FPS||(field>=PORTABLE_SETTING_KEY_ACCELERATE&&field<=PORTABLE_SETTING_PAD_ITEM)||key_field((PortableSetting)field)>=0){
            if(!portable_json_number(&r,&value))return fail(error,"expected unsigned integer setting");
        }else if(field==PORTABLE_SETTING_TOUCH_CONTROLS){
            PortableJsonReader boolean=r;
            if(portable_json_boolean(&boolean,&value))r=boolean;
            else if(!portable_json_number(&r,&value))return fail(error,"touch_controls must be boolean or 0/1");
        }else if(field==PORTABLE_SETTING_FILTER){
            if(!portable_json_string(&r,text,sizeof text)||
               (strcmp(text,"nearest")!=0&&strcmp(text,"linear")!=0))return fail(error,"filter must be nearest or linear");
            value=(uint32_t)(strcmp(text,"linear")==0);
        }else if(!portable_json_boolean(&r,&value))return fail(error,"expected boolean setting");
        if(version?value!=1u:!portable_setting_set(next,(PortableSetting)field,value))return fail(error,"setting is out of range or version unsupported");
        portable_json_whitespace(&r);
        if(r.at<r.end&&*r.at=='}'){++r.at;break;}
        if(!portable_json_take(&r,','))return fail(error,"expected comma or closing brace");
    }
    portable_json_whitespace(&r);
    if(r.at!=r.end)return fail(error,"unexpected data after settings object");
    return 1;
}

int portable_settings_parse(const char *json,size_t size,PortableSettings *settings,char *error)
{
    if(!settings)return fail(error,"invalid settings destination");
    portable_settings_defaults(settings);
    if(!json || size>65536u)return fail(error,"invalid settings size");
    PortableSettings *next=malloc(sizeof *next);
    if(!next)return fail(error,"cannot allocate settings parser");
    portable_settings_defaults(next);
    const int valid=settings_parse_candidate(json,size,next,error);
    if(valid)*settings=*next;
    free(next);
    return valid;
}

int portable_settings_load(const char *path,PortableSettings *s,char *error)
{
    portable_settings_defaults(s);
    if(error!=NULL)error[0]='\0';
    if(path==NULL||s==NULL){fail(error,"invalid settings path");return -1;}
    FILE *file=fopen(path,"rb");
    if(file==NULL){
        if(errno==ENOENT)return 0;
        fail(error,"could not read settings file");return -1;
    }
    char *bytes=malloc(65537u);
    if(!bytes){fclose(file);fail(error,"cannot allocate settings file buffer");return -1;}
    const size_t size=fread(bytes,1u,65537u,file);
    const int read_ok=!ferror(file);
    const int close_ok=fclose(file)==0;
    if(!read_ok||!close_ok||size>65536u){free(bytes);fail(error,"settings file unreadable or too large");return -1;}
    const int valid=portable_settings_parse(bytes,size,s,error);free(bytes);return valid?1:-1;
}

int portable_settings_save(const char *path,const PortableSettings *s)
{
    if(path==NULL||s==NULL)return 0;
    PortableSettings checked;portable_settings_defaults(&checked);
    for(unsigned field=0u;field<PORTABLE_SETTING_COUNT;++field)
        if(!portable_setting_set(&checked,(PortableSetting)field,portable_setting_get(s,(PortableSetting)field)))return 0;
    if(s->store_url_count>4u)return 0;
    for(unsigned i=0;i<s->store_url_count;++i){
        if(!store_url_valid(s->store_urls[i]))return 0;
        for(unsigned j=0;j<i;++j)if(!strcmp(s->store_urls[i],s->store_urls[j]))return 0;
    }
    if(s->mod_count>PORTABLE_MOD_LIMIT || s->mod_directory_count>4u)return 0;
    for(unsigned i=0;i<s->mod_directory_count;++i){
        if(!directory_valid(s->mod_directories[i]))return 0;
        for(unsigned j=0;j<i;++j)if(!strcmp(s->mod_directories[i],s->mod_directories[j]))return 0;
    }
    for(unsigned i=0u;i<s->mod_count;++i){
        if(!portable_mod_id_valid(s->mods[i]))return 0;
        for(unsigned j=0u;j<i;++j)if(strcmp(s->mods[i],s->mods[j])==0)return 0;
    }
    for(unsigned i=0;i<PORTABLE_ASSET_SOURCE_COUNT;++i)
        if(!memchr(s->asset_sources[i],0,PORTABLE_MOD_ID_CAPACITY)||
           (s->asset_sources[i][0]&&strcmp(s->asset_sources[i],"@base")&&!portable_mod_id_valid(s->asset_sources[i])))return 0;
    char temporary[4096];const int length=snprintf(temporary,sizeof temporary,"%s.tmp",path);
    if(length<0||(size_t)length>=sizeof temporary)return 0;
    FILE *file=fopen(temporary,"wb");if(file==NULL)return 0;
    int ok=fputs("{\n  \"version\": 1",file)>=0;
    for(unsigned field=0u;ok&&field<PORTABLE_SETTING_COUNT;++field){
        /* Preserve Original settings bytes, including its prior field order. */
        if(field==PORTABLE_SETTING_DRIVING_PROFILE && s->driving_profile==0u)continue;
        if(field==PORTABLE_SETTING_CLASSIC_MENUS && s->classic_menus==0u)continue;
        if(field==PORTABLE_SETTING_TOUCH_CONTROLS && s->touch_controls==0u)continue;
        if(field==PORTABLE_SETTING_ARTWORK_LOD && s->artwork_lod==0u)continue;
        if(field==PORTABLE_SETTING_RACE_RENDERER && s->race_renderer==0u)continue;
        if(field==PORTABLE_SETTING_IMMERSIVE_CAMERA && s->immersive_camera==0u)continue;
        if(field>=PORTABLE_SETTING_SMOOTH_3D && field<=PORTABLE_SETTING_KART_TRAILS &&
           portable_setting_get(s,(PortableSetting)field)==1u)continue;
        if(field>=PORTABLE_SETTING_HD2D_DOF && !portable_setting_get(s,(PortableSetting)field))continue;
        ok=fprintf(file,",\n  \"%s\": ",names[field])>0;
        const uint32_t value=portable_setting_get(s,(PortableSetting)field);
        if(field==PORTABLE_SETTING_FILTER)
            ok=ok&&fprintf(file,"\"%s\"",value?"linear":"nearest")>0;
        else if(switch_field(&checked,(PortableSetting)field)!=NULL)
            ok=ok&&fputs(value?"true":"false",file)>=0;
        else ok=ok&&fprintf(file,"%u",value)>0;
    }
    ok=ok&&fputs(",\n  \"mods\": [",file)>=0;
    for(unsigned i=0u;ok&&i<s->mod_count;++i)ok=fprintf(file,"%s\"%s\"",i?", ":"",s->mods[i])>0;
    ok=ok&&fputs("]",file)>=0;
    if(s->asset_sources[0][0]||s->asset_sources[1][0]||s->asset_sources[2][0])
        ok=ok&&fprintf(file,",\n  \"asset_sources\": [\"%s\", \"%s\", \"%s\"]",
            s->asset_sources[0],s->asset_sources[1],s->asset_sources[2])>0;
    if(s->mod_directory_count){
        ok=ok&&fputs(",\n  \"mod_directories\": [",file)>=0;
        for(unsigned i=0;ok&&i<s->mod_directory_count;++i){
            if(i)ok=fputs(", ",file)>=0;
            ok=ok&&write_directory(file,s->mod_directories[i]);
        }
        ok=ok&&fputs("]",file)>=0;
    }
    if(s->store_url_count){
        ok=ok&&fputs(",\n  \"store_urls\": [",file)>=0;
        for(unsigned i=0;ok&&i<s->store_url_count;++i){
            if(i)ok=fputs(", ",file)>=0;
            ok=ok&&write_directory(file,s->store_urls[i]);
        }
        ok=ok&&fputs("]",file)>=0;
    }
    ok=ok&&fputs("\n}\n",file)>=0;
    if(fflush(file)!=0)ok=0;
    if(fclose(file)!=0)ok=0;
    if(ok)ok=portable_file_replace(temporary,path);
    if(!ok)remove(temporary);
    return ok;
}

void portable_settings_apply(PortableSettings *s,const PortableSettingsOverrides *overrides)
{
    if(s==NULL||overrides==NULL)return;
    if(overrides->mods_set){s->mod_count=overrides->values.mod_count;memcpy(s->mods,overrides->values.mods,sizeof s->mods);}
    for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind)
        if(overrides->asset_sources_set&(1u<<kind))
            memcpy(s->asset_sources[kind],overrides->values.asset_sources[kind],PORTABLE_MOD_ID_CAPACITY);
    for(unsigned field=0u;field<PORTABLE_SETTING_COUNT;++field)
        if(overrides->mask&BIT(field))
            (void)portable_setting_set(s,(PortableSetting)field,portable_setting_get(&overrides->values,(PortableSetting)field));
}

static void launch_option_name(char option[64], const char *name)
{
    size_t length = 2u;
    option[0] = '-';
    option[1] = '-';
    for (const char *letter = name; *letter != '\0'; ++letter)
        option[length++] = *letter == '_' ? '-' : *letter;
    option[length] = '\0';
}

static int parse_aspect_option(const char *text, uint32_t *value, char *error)
{
    if (strcmp(text, "auto") == 0)
    {
        *value = 0u;
        return 1;
    }
    uint32_t numerator, denominator;
    PortableJsonReader ratio = {text, text + strlen(text)};
    if (!portable_json_number(&ratio, &numerator) || !portable_json_take(&ratio, ':') ||
        !portable_json_number(&ratio, &denominator) || ratio.at != ratio.end ||
        numerator == 0u || numerator > 10000u || denominator == 0u || denominator > 10000u)
        return fail(error, "--aspect needs auto or WIDTH:HEIGHT");
    *value = (160u * numerator + denominator / 2u) / denominator;
    if (*value < 240u || *value > 1024u)
        return fail(error, "aspect ratio must be between 3:2 and 32:5");
    return 1;
}

/* Keep mutation order on failure: the first --mod starts a new list even if
 * that list later exceeds capacity or contains a repeated identifier. */
static int append_launch_mod(PortableSettingsOverrides *overrides, const char *id, char *error)
{
    if (!overrides->mods_set)
    {
        overrides->values.mod_count = 0u;
        overrides->mods_set = 1u;
    }
    const unsigned index = overrides->values.mod_count;
    if (index >= PORTABLE_MOD_LIMIT)
        return fail(error, "too many mods");
    for (unsigned i = 0u; i < index; ++i)
        if (strcmp(overrides->values.mods[i], id) == 0)
            return fail(error, "duplicate mod id");
    strcpy(overrides->values.mods[index], id);
    ++overrides->values.mod_count;
    return 1;
}

/* --NAME / --no-NAME for the quality-of-life switches (mirror_mode is --mirror). */
static PortableSetting switch_option(const char *arg,uint32_t *value)
{
    for(unsigned field=PORTABLE_SETTING_RUMBLE;field<PORTABLE_SETTING_COUNT;++field){
        if(field==PORTABLE_SETTING_DRIVING_PROFILE||field==PORTABLE_SETTING_ARTWORK_LOD||field==PORTABLE_SETTING_RACE_RENDERER)continue;
        char option[64];
        const char *name=field==PORTABLE_SETTING_MIRROR?"mirror":names[field];
        launch_option_name(option,name);
        if(strcmp(arg,option)==0){*value=1u;return (PortableSetting)field;}
        if(strncmp(arg,"--no-",5u)==0&&strcmp(arg+5,option+2)==0){*value=0u;return (PortableSetting)field;}
    }
    return PORTABLE_SETTING_COUNT;
}

int portable_settings_cli(int argc,char **argv,PortableSettingsOverrides *overrides,char *error)
{
    if(overrides==NULL)return fail(error,"invalid settings overrides");
    *overrides=(PortableSettingsOverrides){0};portable_settings_defaults(&overrides->values);
    if(error!=NULL)error[0]='\0';
    for(int i=0;i<argc;++i){
        const char *arg=argv[i];PortableSetting field;uint32_t value=0u;int needs_value=0;
        if(strcmp(arg,"--asset-source")==0){
            if(++i==argc)return fail(error,"--asset-source needs drivers|items|courses=ID");
            static const char *const keys[]={"drivers=","items=","courses="};
            unsigned kind=0;while(kind<3 && strncmp(argv[i],keys[kind],strlen(keys[kind])))++kind;
            if(kind==3)return fail(error,"unknown asset source category");
            const char *id=argv[i]+strlen(keys[kind]);
            if(strcmp(id,"auto")&&strcmp(id,"@base")&&!portable_mod_id_valid(id))return fail(error,"invalid asset source ID");
            snprintf(overrides->values.asset_sources[kind],PORTABLE_MOD_ID_CAPACITY,"%s",!strcmp(id,"auto")?"":id);
            overrides->asset_sources_set|=(uint8_t)(1u<<kind);continue;
        }
        if(strcmp(arg,"--race-renderer")==0){
            if(++i==argc || (strcmp(argv[i],"original")!=0 && strcmp(argv[i],"3d")!=0))
                return fail(error,"--race-renderer needs original or 3d");
            overrides->values.race_renderer=(uint8_t)(strcmp(argv[i],"3d")==0);
            overrides->mask|=BIT(PORTABLE_SETTING_RACE_RENDERER);continue;
        }
        if(strcmp(arg,"--artwork-lod")==0){
            if(++i==argc || (strcmp(argv[i],"original")!=0 && strcmp(argv[i],"high")!=0 && strcmp(argv[i],"max")!=0))
                return fail(error,"--artwork-lod needs original, high or max");
            overrides->values.artwork_lod=(uint8_t)(strcmp(argv[i],"max")==0?2u:strcmp(argv[i],"high")==0?1u:0u);
            overrides->mask|=BIT(PORTABLE_SETTING_ARTWORK_LOD);continue;
        }
        if(strcmp(arg,"--drift-extras")==0){
            if(++i==argc || (strcmp(argv[i],"off")!=0 && strcmp(argv[i],"tiers")!=0 && strcmp(argv[i],"tiers-air-boost")!=0))
                return fail(error,"--drift-extras needs off, tiers or tiers-air-boost");
            overrides->values.driving_profile=(uint8_t)(strcmp(argv[i],"tiers-air-boost")==0?2u:strcmp(argv[i],"tiers")==0?1u:0u);
            overrides->mask|=BIT(PORTABLE_SETTING_DRIVING_PROFILE);continue;
        }
        if(strcmp(arg,"--driving-profile")==0){
            if(++i==argc || (strcmp(argv[i],"original")!=0 && strcmp(argv[i],"modern")!=0 && strcmp(argv[i],"modern-air")!=0))
                return fail(error,"--driving-profile needs original, modern or modern-air");
            overrides->values.driving_profile=(uint8_t)(strcmp(argv[i],"modern-air")==0?2u:strcmp(argv[i],"modern")==0?1u:0u);
            overrides->mask|=BIT(PORTABLE_SETTING_DRIVING_PROFILE);continue;
        }
        if(strcmp(arg,"--aspect")==0){
            if(++i==argc)return fail(error,"--aspect needs auto or WIDTH:HEIGHT");
            if(!parse_aspect_option(argv[i],&value,error))return 0;
            if(!portable_setting_set(&overrides->values,PORTABLE_SETTING_ASPECT_WIDTH,value))return 0;
            overrides->mask|=BIT(PORTABLE_SETTING_ASPECT_WIDTH);continue;
        }
        if(strcmp(arg,"--watch-mods")==0 || strcmp(arg,"--no-watch-mods")==0){
            overrides->watch_mods=(uint8_t)(strcmp(arg,"--watch-mods")==0);
            overrides->watch_mods_set=1u;continue;
        }
        if(strcmp(arg,"--no-mods")==0){overrides->no_mods=1u;continue;}
        if(strcmp(arg,"--mods-dir")==0){
            if(++i==argc||argv[i][0]=='\0')return fail(error,"--mods-dir needs a directory");
            overrides->mods_dir=argv[i];continue;
        }
        if(strcmp(arg,"--mod")==0){
            if(++i==argc||!portable_mod_id_valid(argv[i]))return fail(error,"--mod needs a valid id");
            if(!append_launch_mod(overrides,argv[i],error))return 0;
            continue;
        }
        if(strcmp(arg,"--fit-scaling")==0){field=PORTABLE_SETTING_FIT_SCALING;value=1u;}
        else if(strcmp(arg,"--integer-scaling")==0)field=PORTABLE_SETTING_FIT_SCALING;
        else if(strcmp(arg,"--fullscreen")==0){field=PORTABLE_SETTING_FULLSCREEN;value=1u;}
        else if(strcmp(arg,"--windowed")==0)field=PORTABLE_SETTING_FULLSCREEN;
        else if(strcmp(arg,"--vsync")==0){field=PORTABLE_SETTING_VSYNC;value=1u;}
        else if(strcmp(arg,"--no-vsync")==0)field=PORTABLE_SETTING_VSYNC;
        else if(strcmp(arg,"--short-transitions")==0){field=PORTABLE_SETTING_SHORT_TRANSITIONS;value=1u;}
        else if(strcmp(arg,"--original-transitions")==0)field=PORTABLE_SETTING_SHORT_TRANSITIONS;
        else if(strcmp(arg,"--interpolate")==0){field=PORTABLE_SETTING_INTERPOLATE;value=1u;}
        else if(strcmp(arg,"--no-interpolate")==0)field=PORTABLE_SETTING_INTERPOLATE;
        else if((field=switch_option(arg,&value))!=PORTABLE_SETTING_COUNT){}
        else if(strcmp(arg,"--render-fps")==0){field=PORTABLE_SETTING_RENDER_FPS;needs_value=1;}
        else if(strcmp(arg,"--window-scale")==0){field=PORTABLE_SETTING_WINDOW_SCALE;needs_value=1;}
        else if(strcmp(arg,"--volume")==0){field=PORTABLE_SETTING_VOLUME;needs_value=1;}
        else if(strcmp(arg,"--steering-deadzone")==0){field=PORTABLE_SETTING_DEADZONE;needs_value=1;}
        else if(strcmp(arg,"--filter")==0){field=PORTABLE_SETTING_FILTER;needs_value=1;}
        else{
            field=PORTABLE_SETTING_COUNT;
            for(unsigned candidate=PORTABLE_SETTING_KEY_ACCELERATE;candidate<PORTABLE_SETTING_RUMBLE;++candidate){
                char option[64];
                launch_option_name(option,names[candidate]);
                if(strcmp(arg,option)==0){field=(PortableSetting)candidate;needs_value=1;break;}
            }
            if(field==PORTABLE_SETTING_COUNT)return fail(error,"unknown launch option");
        }
        if(needs_value){
            if(++i==argc)return fail(error,"launch option needs a value");
            if(field==PORTABLE_SETTING_FILTER){
                if(strcmp(argv[i],"nearest")!=0&&strcmp(argv[i],"linear")!=0)return fail(error,"filter must be nearest or linear");
                value=(uint32_t)(strcmp(argv[i],"linear")==0);
            }else{
                PortableJsonReader r={argv[i],argv[i]+strlen(argv[i])};
                if(!portable_json_number(&r,&value)||r.at!=r.end)return fail(error,"launch option needs an unsigned integer");
            }
        }
        if(!portable_setting_set(&overrides->values,field,value))return fail(error,"launch option is out of range");
        overrides->mask|=BIT((unsigned)field);
    }
    return 1;
}

unsigned portable_settings_hd2d_preset(const PortableSettings *settings)
{
    unsigned on = 0;
    for (unsigned i = PORTABLE_SETTING_HD2D_DOF; i <= PORTABLE_SETTING_HD2D_SCALE; ++i)
        on += portable_setting_get(settings, (PortableSetting)i) != 0;
    return on == 7u ? 1u : on ? 2u : 0u;
}
void portable_settings_hd2d(PortableSettings *settings, int enabled)
{
    for (unsigned i = PORTABLE_SETTING_HD2D_DOF; i <= PORTABLE_SETTING_HD2D_SCALE; ++i)
        (void)portable_setting_set(settings, (PortableSetting)i, enabled != 0);
}
