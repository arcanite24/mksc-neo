#include "settings.h"
#include "controller_input.h"
#include "driver_input.h"
#include "test_temp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"settings failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
_Static_assert(PORTABLE_SETTING_ARTWORK_LOD==PORTABLE_SETTING_TOUCH_CONTROLS+1, "Settings field order must remain stable");
_Static_assert(PORTABLE_SETTING_RACE_RENDERER==PORTABLE_SETTING_ARTWORK_LOD+1, "Settings field order must remain stable");
_Static_assert(PORTABLE_SETTING_TOUCH_CONTROLS==PORTABLE_SETTING_CLASSIC_MENUS+1, "Settings field order must remain stable");
_Static_assert(PORTABLE_SETTING_SMOOTH_3D==PORTABLE_SETTING_RACE_RENDERER+1, "Settings field order must remain stable");

static int equal(const PortableSettings *a,const PortableSettings *b)
{
    for(unsigned field=0;field<PORTABLE_SETTING_COUNT;++field)
        if(portable_setting_get(a,(PortableSetting)field)!=portable_setting_get(b,(PortableSetting)field))return 0;
    return a->mod_count==b->mod_count&&memcmp(a->mods,b->mods,sizeof a->mods)==0 &&
        a->mod_directory_count==b->mod_directory_count &&
        memcmp(a->mod_directories,b->mod_directories,sizeof a->mod_directories)==0;
}

static size_t saved_settings_bytes(const char *path, char *bytes, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    const size_t size = fread(bytes, 1u, capacity, file);
    CHECK(!ferror(file) && size < capacity && feof(file));
    CHECK(fclose(file) == 0);
    return size;
}

static void drift_extras_cli(const char *path)
{
    char *canonical[] = {"off", "tiers", "tiers-air-boost"};
    char *legacy[] = {"original", "modern", "modern-air"};
    PortableSettings defaults, current, previous, loaded;
    PortableSettingsOverrides overrides;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
    portable_settings_defaults(&defaults);
    for (unsigned selection = 0; selection < 3u; ++selection)
    {
        char *new_args[] = {"--drift-extras", canonical[selection]};
        char *old_args[] = {"--driving-profile", legacy[selection]};
        CHECK(portable_settings_cli(2, new_args, &overrides, error));
        CHECK(overrides.mask == (UINT64_C(1) << PORTABLE_SETTING_DRIVING_PROFILE));
        current = defaults;
        portable_settings_apply(&current, &overrides);
        CHECK(current.driving_profile == selection);
        CHECK(portable_settings_cli(2, old_args, &overrides, error));
        previous = defaults;
        portable_settings_apply(&previous, &overrides);
        CHECK(equal(&current, &previous));

        /* Renaming the launch option must not migrate any saved field or byte. */
        char new_json[8193], old_json[8193];
        CHECK(portable_settings_save(path, &previous));
        const size_t old_size = saved_settings_bytes(path, old_json, sizeof old_json);
        CHECK(portable_settings_save(path, &current));
        const size_t new_size = saved_settings_bytes(path, new_json, sizeof new_json);
        CHECK(new_size == old_size && memcmp(new_json, old_json, new_size) == 0);
        CHECK(portable_settings_load(path, &loaded, error) == 1 && equal(&loaded, &previous));
    }

    char *missing[] = {"--drift-extras"};
    CHECK(!portable_settings_cli(1, missing, &overrides, error));
    CHECK(strstr(error, "--drift-extras") != NULL);
    char *invalid[] = {"", "modern", "original", "air", "TIERS", "3", "--volume"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
    {
        char *args[] = {"--drift-extras", invalid[i]};
        CHECK(!portable_settings_cli(2, args, &overrides, error));
        CHECK(strstr(error, "--drift-extras") != NULL);
    }
    /* Both spellings update the same setting: the last occurrence wins. */
    char *new_last[] = {"--driving-profile", "modern-air", "--drift-extras", "off"};
    CHECK(portable_settings_cli(4, new_last, &overrides, error));
    current = defaults;
    portable_settings_apply(&current, &overrides);
    CHECK(current.driving_profile == 0u);
    char *old_last[] = {"--drift-extras", "tiers", "--driving-profile", "modern-air"};
    CHECK(portable_settings_cli(4, old_last, &overrides, error));
    current = defaults;
    portable_settings_apply(&current, &overrides);
    CHECK(current.driving_profile == 2u);
    char *repeated[] = {"--drift-extras", "tiers-air-boost", "--drift-extras", "tiers"};
    CHECK(portable_settings_cli(4, repeated, &overrides, error));
    current = defaults;
    portable_settings_apply(&current, &overrides);
    CHECK(current.driving_profile == 1u);
}
static void mod_directory_settings(const char *path)
{
    PortableSettings *settings=malloc(sizeof *settings),*loaded=malloc(sizeof *loaded);
    CHECK(settings && loaded);
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
    portable_settings_defaults(settings);
    const char json[]="{\"mod_directories\":[\"C:\\\\mods\\\"quoted\",\"/home/\\u00e9/\\ud83c\\udfae\"]}";
    CHECK(portable_settings_parse(json,strlen(json),settings,error));
    CHECK(settings->mod_directory_count==2u && !strcmp(settings->mod_directories[0],"C:\\mods\"quoted"));
    CHECK(!strcmp(settings->mod_directories[1],"/home/\xc3\xa9/\xf0\x9f\x8e\xae"));
    CHECK(portable_settings_save(path,settings));
    CHECK(portable_settings_load(path,loaded,error)==1 && equal(settings,loaded));
    for(unsigned i=0;i<4u;++i){
        memset(settings->mod_directories[i],'a'+(int)i,4095u);
        settings->mod_directories[i][4095]='\0';
    }
    settings->mod_directory_count=4u;
    CHECK(portable_settings_save(path,settings));
    CHECK(portable_settings_load(path,loaded,error)==1 && equal(settings,loaded));
    const char *invalid[]={"{\"mod_directories\":[\"\"]}","{\"mod_directories\":[\"a\",\"a\"]}",
        "{\"mod_directories\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}","{\"mod_directories\":true}",
        "{\"mod_directories\":[1]}","{\"mod_directories\":[\"\\u0000\"]}",
        "{\"mod_directories\":[\"\\ud800\"]}","{\"mod_directories\":[\"\\u000a\"]}",
        "{\"mod_directories\":[],\"mod_directories\":[]}"};
    for(size_t i=0;i<sizeof invalid/sizeof invalid[0];++i){
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),loaded,error));
        CHECK(!loaded->mod_directory_count && !loaded->mod_count && error[0]);
    }
    char *bounded=malloc(65537u);CHECK(bounded);
    memset(bounded,' ',65537u);bounded[0]='{';bounded[1]='}';
    CHECK(portable_settings_parse(bounded,65536u,loaded,error));
    CHECK(!portable_settings_parse(bounded,65537u,loaded,error) && !loaded->mod_directory_count);
    free(bounded);
    settings->mod_directory_count=5u;CHECK(!portable_settings_save(path,settings));
    settings->mod_directory_count=4u;settings->mod_directories[0][0]='\n';
    CHECK(!portable_settings_save(path,settings));
    PortableSettingsOverrides *overrides=malloc(sizeof *overrides);CHECK(overrides);
    char *watch[]={"--watch-mods","--no-watch-mods","--watch-mods"};
    CHECK(portable_settings_cli(3,watch,overrides,error));
    CHECK(overrides->watch_mods_set && overrides->watch_mods && !overrides->mask);
    portable_settings_defaults(settings);portable_settings_defaults(loaded);
    portable_settings_apply(settings,overrides);CHECK(equal(settings,loaded));
    char *off[]={"--no-watch-mods"};CHECK(portable_settings_cli(1,off,overrides,error));
    CHECK(overrides->watch_mods_set && !overrides->watch_mods && !overrides->mask);
    CHECK(portable_settings_cli(0,NULL,overrides,error) && !overrides->watch_mods_set);
    free(overrides);free(loaded);free(settings);
}

static void artwork_settings(const char *path)
{
    PortableSettings defaults,settings,loaded;PortableSettingsOverrides overrides;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY];portable_settings_defaults(&defaults);
    CHECK(!defaults.artwork_lod && !strcmp(portable_setting_name(PORTABLE_SETTING_ARTWORK_LOD),"artwork_lod"));
    for(unsigned value=0u;value<3u;++value){
        settings=defaults;CHECK(portable_setting_set(&settings,PORTABLE_SETTING_ARTWORK_LOD,value));
        CHECK(portable_setting_get(&settings,PORTABLE_SETTING_ARTWORK_LOD)==value);
        CHECK(portable_settings_save(path,&settings));
        CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&settings,&loaded));
        char *args[]={"--artwork-lod",value==0u?"original":value==1u?"high":"max"};
        CHECK(portable_settings_cli(2,args,&overrides,error));
        CHECK(overrides.mask==(UINT64_C(1)<<PORTABLE_SETTING_ARTWORK_LOD));
        loaded=defaults;portable_settings_apply(&loaded,&overrides);CHECK(equal(&loaded,&settings));
    }
    const char *invalid[]={"{\"artwork_lod\":3}","{\"artwork_lod\":-1}","{\"artwork_lod\":1.0}",
        "{\"artwork_lod\":true}","{\"artwork_lod\":\"high\"}","{\"artwork_lod\":1,\"artwork_lod\":2}"};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),&settings,error));
        CHECK(equal(&settings,&defaults) && error[0]);
    }
    settings=defaults;CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_ARTWORK_LOD,3u));
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_ARTWORK_LOD,UINT32_MAX) && equal(&settings,&defaults));
    char *missing[]={"--artwork-lod"},*bad[]={"--artwork-lod","ultra"},*boolean[]={"--no-artwork-lod"};
    CHECK(!portable_settings_cli(1,missing,&overrides,error));
    CHECK(!portable_settings_cli(2,bad,&overrides,error));
    CHECK(!portable_settings_cli(1,boolean,&overrides,error));
    char *last[]={"--artwork-lod","high","--artwork-lod","max"};
    CHECK(portable_settings_cli(4,last,&overrides,error));
    CHECK(portable_settings_save(path,&defaults));
    settings=defaults;portable_settings_apply(&settings,&overrides);CHECK(settings.artwork_lod==2u);
    CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&loaded,&defaults));
    settings.artwork_lod=3u;CHECK(!portable_settings_save(path,&settings));
    CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&loaded,&defaults));
}
static void race_renderer_settings(const char *path)
{
    PortableSettings defaults,settings,loaded;PortableSettingsOverrides overrides;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY];portable_settings_defaults(&defaults);
    CHECK(!defaults.race_renderer && !strcmp(portable_setting_name(PORTABLE_SETTING_RACE_RENDERER),"race_renderer"));
    for(unsigned value=0u;value<2u;++value){
        settings=defaults;CHECK(portable_setting_set(&settings,PORTABLE_SETTING_RACE_RENDERER,value));
        CHECK(portable_setting_get(&settings,PORTABLE_SETTING_RACE_RENDERER)==value);
        CHECK(portable_settings_save(path,&settings));
        CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&settings,&loaded));
        char *args[]={"--race-renderer",value?"3d":"original"};
        CHECK(portable_settings_cli(2,args,&overrides,error));
        CHECK(overrides.mask==(UINT64_C(1)<<PORTABLE_SETTING_RACE_RENDERER));
        loaded=defaults;portable_settings_apply(&loaded,&overrides);CHECK(equal(&loaded,&settings));
    }
    const char *invalid[]={"{\"race_renderer\":2}","{\"race_renderer\":-1}","{\"race_renderer\":1.0}",
        "{\"race_renderer\":true}","{\"race_renderer\":\"3d\"}","{\"race_renderer\":1,\"race_renderer\":0}"};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),&settings,error));
        CHECK(equal(&settings,&defaults) && error[0]);
    }
    settings=defaults;CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_RACE_RENDERER,2u));
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_RACE_RENDERER,UINT32_MAX) && equal(&settings,&defaults));
    char *missing[]={"--race-renderer"},*bad[]={"--race-renderer","3D"},*boolean[]={"--no-race-renderer"};
    CHECK(!portable_settings_cli(1,missing,&overrides,error));
    CHECK(!portable_settings_cli(2,bad,&overrides,error));
    CHECK(!portable_settings_cli(1,boolean,&overrides,error));
    char *last[]={"--race-renderer","original","--race-renderer","3d"};
    CHECK(portable_settings_cli(4,last,&overrides,error));
    CHECK(portable_settings_save(path,&defaults));
    settings=defaults;portable_settings_apply(&settings,&overrides);CHECK(settings.race_renderer==1u);
    CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&loaded,&defaults));
    settings.race_renderer=2u;CHECK(!portable_settings_save(path,&settings));
    CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&loaded,&defaults));
}
/* 3D effects default on; saved files list only the switches turned off. */
static void effect_settings(const char *path)
{
    static const char *const names[6]={"smooth_3d","dynamic_camera","camera_shake",
        "drift_sparks","boost_effects","kart_trails"};
    PortableSettings defaults,settings,loaded;PortableSettingsOverrides overrides;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY],bytes[8192];portable_settings_defaults(&defaults);
    CHECK(portable_settings_save(path,&defaults));
    const size_t size=saved_settings_bytes(path,bytes,sizeof bytes);bytes[size]='\0';
    for(unsigned i=0u;i<6u;++i){
        const PortableSetting field=(PortableSetting)(PORTABLE_SETTING_SMOOTH_3D+i);
        CHECK(!strcmp(portable_setting_name(field),names[i]) && portable_setting_get(&defaults,field)==1u);
        CHECK(!strstr(bytes,names[i]));
        settings=defaults;CHECK(portable_setting_set(&settings,field,0u));
        CHECK(!portable_setting_set(&settings,field,2u) && portable_setting_get(&settings,field)==0u);
        CHECK(portable_settings_save(path,&settings));
        CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&settings,&loaded));
        char option[40],*args[1]={option};
        snprintf(option,sizeof option,"--no-%s",names[i]);
        for(char *c=option;*c;++c)if(*c=='_')*c='-';
        CHECK(portable_settings_cli(1,args,&overrides,error));
        loaded=defaults;portable_settings_apply(&loaded,&overrides);CHECK(equal(&loaded,&settings));
    }
    const char off[]="{\"camera_shake\":false,\"kart_trails\":false}";
    CHECK(portable_settings_parse(off,strlen(off),&settings,error));
    CHECK(!settings.camera_shake && !settings.kart_trails && settings.smooth_3d && settings.drift_sparks);
}
static void immersive_settings(const char *path)
{
    PortableSettings defaults,settings,loaded;
    PortableSettingsOverrides overrides;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
    portable_settings_defaults(&defaults);
    CHECK(!defaults.immersive_camera);
    const char enabled[]="{\"immersive_camera\":true}";
    CHECK(portable_settings_parse(enabled,strlen(enabled),&settings,error) && settings.immersive_camera);
    CHECK(portable_settings_save(path,&settings));
    CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&settings,&loaded));
    char *off[]={"--no-immersive-camera"},*on[]={"--immersive-camera"};
    CHECK(portable_settings_cli(1,off,&overrides,error));
    portable_settings_apply(&settings,&overrides);
    CHECK(equal(&settings,&defaults));
    CHECK(portable_settings_load(path,&loaded,error)==1 && loaded.immersive_camera);
    CHECK(portable_settings_cli(1,on,&overrides,error));
    portable_settings_apply(&settings,&overrides);
    CHECK(equal(&settings,&loaded));
    const char *invalid[]={"{\"immersive_camera\":2}","{\"immersive_camera\":null}",
        "{\"immersive_camera\":true,\"immersive_camera\":false}"};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i)
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),&settings,error) && equal(&settings,&defaults));
    CHECK(portable_settings_parse("{}",2,&settings,error) && !settings.immersive_camera);
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_IMMERSIVE_CAMERA,2));
}

static void touch_settings(const char *path)
{
    PortableSettings defaults,settings,loaded;PortableSettingsOverrides overrides;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY];portable_settings_defaults(&defaults);
    CHECK(!defaults.touch_controls && portable_setting_get(&defaults,PORTABLE_SETTING_TOUCH_CONTROLS)==0u);
    settings=defaults;
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_KEY_ACCELERATE,282u) && equal(&settings,&defaults));
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_KEY2_ITEM,282u) && equal(&settings,&defaults));
    const char back_binding[]="{\"touch_controls\":true,\"key_accelerate\":282}";
    CHECK(!portable_settings_parse(back_binding,strlen(back_binding),&settings,error) && equal(&settings,&defaults));
    CHECK(!strcmp(portable_setting_name(PORTABLE_SETTING_TOUCH_CONTROLS),"touch_controls"));
    const char *valid[]={"{\"touch_controls\":true}","{\"touch_controls\":false}",
        "{\"touch_controls\":1}","{\"touch_controls\":0}"};
    for(unsigned i=0u;i<4u;++i){
        CHECK(portable_settings_parse(valid[i],strlen(valid[i]),&settings,error));
        CHECK(settings.touch_controls==(i%2u==0u));
        CHECK(portable_settings_save(path,&settings));
        CHECK(portable_settings_load(path,&loaded,error)==1 && equal(&settings,&loaded));
    }
    const char legacy[]="{\"version\":1,\"driving_profile\":1}";
    CHECK(portable_settings_parse(legacy,strlen(legacy),&settings,error) && !settings.touch_controls && settings.driving_profile==1u);
    const char *invalid[]={"{\"touch_controls\":2}","{\"touch_controls\":-1}",
        "{\"touch_controls\":1.0}","{\"touch_controls\":null}","{\"touch_controls\":\"true\"}",
        "{\"touch_controls\":true,\"touch_controls\":false}"};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),&settings,error));
        CHECK(equal(&settings,&defaults) && error[0]);
    }
    char *enabled[]={"--touch-controls"},*disabled[]={"--no-touch-controls"};
    CHECK(portable_settings_cli(1,enabled,&overrides,error));settings=defaults;
    portable_settings_apply(&settings,&overrides);CHECK(settings.touch_controls==1u);
    for(unsigned field=0u;field<PORTABLE_SETTING_COUNT;++field)if(field!=PORTABLE_SETTING_TOUCH_CONTROLS)
        CHECK(portable_setting_get(&settings,(PortableSetting)field)==portable_setting_get(&defaults,(PortableSetting)field));
    CHECK(portable_settings_save(path,&settings));
    CHECK(portable_settings_cli(1,disabled,&overrides,error));portable_settings_apply(&settings,&overrides);
    CHECK(!settings.touch_controls);
    CHECK(portable_settings_load(path,&loaded,error)==1 && loaded.touch_controls==1u); /* CLI is temporary. */
    char *last_off[]={"--touch-controls","--no-touch-controls"},*last_on[]={"--no-touch-controls","--touch-controls"};
    CHECK(portable_settings_cli(2,last_off,&overrides,error));portable_settings_apply(&settings,&overrides);CHECK(!settings.touch_controls);
    CHECK(portable_settings_cli(2,last_on,&overrides,error));portable_settings_apply(&settings,&overrides);CHECK(settings.touch_controls);
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_TOUCH_CONTROLS,2u) && settings.touch_controls==1u);
    CHECK(!portable_setting_set(&settings,PORTABLE_SETTING_TOUCH_CONTROLS,UINT32_MAX) && settings.touch_controls==1u);
    settings.touch_controls=2u;CHECK(!portable_settings_save(path,&settings));
    CHECK(portable_settings_load(path,&loaded,error)==1 && loaded.touch_controls==1u);
    CHECK(portable_setting_set(&loaded,PORTABLE_SETTING_TOUCH_CONTROLS,0u) && portable_settings_save(path,&loaded));
    CHECK(portable_settings_load(path,&settings,error)==1 && equal(&settings,&defaults));
}

static void hd2d_settings(const char *path)
{
    PortableSettings s, loaded;
    char error[PORTABLE_SETTINGS_ERROR_CAPACITY], bytes[8193];
    portable_settings_defaults(&s);
    CHECK(portable_settings_hd2d_preset(&s) == 0);
    CHECK(portable_settings_save(path, &s));
    const size_t size = saved_settings_bytes(path, bytes, sizeof bytes);
    bytes[size] = 0;
    CHECK(strstr(bytes, "hd2d_") == NULL); /* old default files stay byte stable */
    portable_settings_hd2d(&s, 1);
    CHECK(portable_settings_hd2d_preset(&s) == 1);
    CHECK(s.internal_scale == 1 && s.race_renderer == 0); /* other renderers unchanged */
    CHECK(portable_settings_save(path, &s));
    CHECK(portable_settings_load(path, &loaded, error) == 1 && equal(&s, &loaded));
    CHECK(portable_setting_set(&s, PORTABLE_SETTING_HD2D_DOF, 0));
    CHECK(portable_settings_hd2d_preset(&s) == 2);
    CHECK(!portable_setting_set(&s, PORTABLE_SETTING_HD2D_DOF, 2));
    char *args[] = {"--hd2d-dof", "--no-hd2d-bloom"};
    PortableSettingsOverrides overrides;
    CHECK(portable_settings_cli(2, args, &overrides, error));
    portable_settings_apply(&s, &overrides);
    CHECK(s.hd2d_dof && !s.hd2d_bloom && s.hd2d_grade);
    const char *invalid = "{\"hd2d_ao\":2}";
    CHECK(!portable_settings_parse(invalid, strlen(invalid), &s, error));
    CHECK(portable_settings_hd2d_preset(&s) == 0);
    portable_settings_hd2d(&loaded, 0);
    CHECK(portable_settings_hd2d_preset(&loaded) == 0);
}

static void asset_source_settings(const char *path)
{
    PortableSettings s,loaded;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
    const char json[]={"{\"asset_sources\":[\"@base\",\"\",\"mario-kart-ds\"]}"};
    CHECK(portable_settings_parse(json,strlen(json),&s,error));
    CHECK(!strcmp(s.asset_sources[0],"@base")&&!s.asset_sources[1][0]&&!strcmp(s.asset_sources[2],"mario-kart-ds"));
    CHECK(portable_settings_save(path,&s)&&portable_settings_load(path,&loaded,error)==1);
    CHECK(!memcmp(s.asset_sources,loaded.asset_sources,sizeof s.asset_sources));
    const char *invalid[]={"{\"asset_sources\":[\"one\"]}",
        "{\"asset_sources\":[\"../bad\",\"\",\"\"]}",
        "{\"asset_sources\":[\"\",\"\",\"\",\"extra\"]}",
        "{\"asset_sources\":[\"\",\"\",\"\"],\"asset_sources\":[\"\",\"\",\"\"]}"};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i){
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),&loaded,error));
        CHECK(!loaded.asset_sources[0][0]&&!loaded.asset_sources[1][0]&&!loaded.asset_sources[2][0]);
    }
    PortableSettingsOverrides overrides;
    char *args[]={"--asset-source","drivers=mario-kart-ds","--asset-source","courses=auto"};
    CHECK(portable_settings_cli(4,args,&overrides,error));portable_settings_apply(&s,&overrides);
    CHECK(!strcmp(s.asset_sources[0],"mario-kart-ds")&&!s.asset_sources[2][0]);
    char *bad[]={"--asset-source","sound=one"};CHECK(!portable_settings_cli(2,bad,&overrides,error));
}

int main(int argc,char **argv)
{
    if(argc==4 && strcmp(argv[1],"--expect-race-renderer")==0){
        CHECK(strlen(argv[3])==1u && argv[3][0]>='0' && argv[3][0]<='1');
        PortableSettings saved,expected;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        portable_settings_defaults(&expected);expected.race_renderer=(uint8_t)(argv[3][0]-'0');
        CHECK(portable_settings_load(argv[2],&saved,error)==1 && equal(&saved,&expected));
        printf("saved race renderer: PASS value=%u scalar defaults retained\n",expected.race_renderer);
        return 0;
    }
    if(argc==4 && strcmp(argv[1],"--expect-artwork-lod")==0){
        CHECK(strlen(argv[3])==1u && argv[3][0]>='0' && argv[3][0]<='2');
        PortableSettings saved,expected;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        portable_settings_defaults(&expected);expected.artwork_lod=(uint8_t)(argv[3][0]-'0');
        CHECK(portable_settings_load(argv[2],&saved,error)==1 && equal(&saved,&expected));
        printf("saved artwork LOD: PASS value=%u scalar defaults retained\n",expected.artwork_lod);
        return 0;
    }
    if(argc>=2 && strcmp(argv[1],"--expect-first-run")==0){
        CHECK(argc==3 || argc==4);
        uint8_t expected_profile=1u;
        if(argc==4){
            CHECK(strlen(argv[3])==1u && (argv[3][0]=='1' || argv[3][0]=='2'));
            expected_profile=(uint8_t)(argv[3][0]-'0');
        }
        PortableSettings saved,defaults;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        portable_settings_defaults(&defaults);
        CHECK(portable_settings_load(argv[2],&saved,error)==1);
        CHECK(saved.driving_profile==expected_profile && saved.classic_menus==1u);
        for(unsigned field=0u;field<PORTABLE_SETTING_COUNT;++field)
            if(field!=PORTABLE_SETTING_DRIVING_PROFILE && field!=PORTABLE_SETTING_CLASSIC_MENUS)
                CHECK(portable_setting_get(&saved,(PortableSetting)field)==
                      portable_setting_get(&defaults,(PortableSetting)field));
        CHECK(saved.mod_count==0u && memcmp(saved.mods,defaults.mods,sizeof saved.mods)==0);
        puts(expected_profile==2u?
            "first run saved settings: PASS Drift Tiers + Air Boost Classic menus scalar defaults retained":
            "first run saved settings: PASS Modern driving Classic menus scalar defaults retained");
        return 0;
    }
    if(argc==3 && strcmp(argv[1],"--expect-first-run-missing")==0){
        PortableSettings saved;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        CHECK(portable_settings_load(argv[2],&saved,error)==0);
        puts("first run failed save: PASS no settings published");
        return 0;
    }
    if(argc==4 && strcmp(argv[1],"--expect-driving-profile")==0){
        CHECK(strcmp(argv[3],"0")==0 || strcmp(argv[3],"1")==0 || strcmp(argv[3],"2")==0);
        const unsigned wanted=(unsigned)(argv[3][0]-'0');
        PortableSettings saved,defaults;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        portable_settings_defaults(&defaults);
        CHECK(portable_settings_load(argv[2],&saved,error)==1);
        CHECK(saved.driving_profile==wanted && saved.mod_count==0u);
        for(unsigned field=0u;field<PORTABLE_SETTING_COUNT;++field)
            if(field!=PORTABLE_SETTING_DRIVING_PROFILE)
                CHECK(portable_setting_get(&saved,(PortableSetting)field)==
                      portable_setting_get(&defaults,(PortableSetting)field));
        CHECK(memcmp(saved.mods,defaults.mods,sizeof saved.mods)==0);
        FILE *file=fopen(argv[2],"rb");CHECK(file!=NULL);
        char json[16385];size_t length=fread(json,1u,sizeof json-1u,file);
        CHECK(!ferror(file)&&fgetc(file)==EOF&&fclose(file)==0);json[length]='\0';
        CHECK((strstr(json,"\"driving_profile\"")!=NULL)==(wanted!=0u));
        printf("saved driving profile: PASS profile=%u scalar defaults retained original omitted\n",wanted);
        return 0;
    }

    if(argc>=3 && strcmp(argv[1],"--expect-mods")==0){
        PortableSettings saved,defaults;
        char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        portable_settings_defaults(&defaults);
        CHECK(portable_settings_load(argv[2],&saved,error)==1);
        CHECK((size_t)(argc-3)<=PORTABLE_MOD_LIMIT);
        CHECK(saved.mod_count==(unsigned)(argc-3));
        for(unsigned index=0u;index<saved.mod_count;++index)
            CHECK(strcmp(saved.mods[index],argv[index+3u])==0);
        /* Applying a mod list must not persist temporary CLI video/input/audio
         * overrides. Compare each named scalar rather than structure padding. */
        for(unsigned field=0u;field<PORTABLE_SETTING_COUNT;++field)
            CHECK(portable_setting_get(&saved,(PortableSetting)field)==
                  portable_setting_get(&defaults,(PortableSetting)field));
        CHECK(saved.aspect_width==240u && saved.wide_ui_backgrounds==0u);
        printf("saved mods: PASS count=%u scalar defaults retained\n",saved.mod_count);
        return 0;
    }
    if(argc==3 && strcmp(argv[1],"--expect-wide-ui")==0){
        PortableSettings saved;
        char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
        CHECK(portable_settings_load(argv[2],&saved,error)==1);
        CHECK(saved.wide_ui_backgrounds==1u && saved.aspect_width==240u);
        puts("saved wide UI: PASS enabled=1 aspect=240");
        return 0;
    }
    CHECK(argc==1);
    PortableSettings defaults,s;char error[PORTABLE_SETTINGS_ERROR_CAPACITY];
    portable_settings_defaults(&defaults);
    const char valid[]="{\"version\":1,\"window_scale\":2,\"fullscreen\":true,\"vsync\":false,\"filter\":\"linear\",\"master_volume\":35,\"steering_deadzone\":16000,\"short_transitions\":true}";
    CHECK(portable_settings_parse(valid,strlen(valid),&s,error));
    CHECK(s.window_scale==2u&&s.fullscreen&&s.vsync==0u&&s.linear_filter&&s.master_volume==35u&&s.steering_deadzone==16000u&&s.short_transitions);
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_KEY_ACCELERATE,25u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_PAD_ACCELERATE,3u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_PAD_HOP,7u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_INTERPOLATE,1u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_RENDER_FPS,144u));
    CHECK(s.internal_scale==1u&&!s.fit_scaling&&s.aspect_width==240u);
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_ASPECT_WIDTH,373u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_INTERNAL_SCALE,8u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_FIT_SCALING,1u));
    s.mod_count=2u;strcpy(s.mods[0],"palette-demo");strcpy(s.mods[1],"kart-demo");
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_WIDE_UI_BACKGROUNDS,1u));
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_WIDE_UI_BACKGROUNDS,2u));
    const PortableSettings configured=s;
    bool keys[512]={0};keys[29]=true;keys[82]=true;keys[229]=true;
    CHECK(portable_keyboard_driving_buttons(keys,512,&defaults)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_UP|PORTABLE_INPUT_HOP));
    bool brake_keys[512]={0};brake_keys[29]=true;brake_keys[81]=true;brake_keys[225]=true;
    CHECK(portable_keyboard_driving_buttons(brake_keys,512,&defaults)==
          (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_BRAKE|PORTABLE_INPUT_DOWN|PORTABLE_INPUT_HOP));
    brake_keys[81]=false;
    CHECK(portable_keyboard_driving_buttons(brake_keys,512,&defaults)==
          (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_HOP));
    PortableSettings remapped=defaults;remapped.keys[0]=25u;remapped.keys[4]=9u;
    CHECK(portable_keyboard_driving_buttons(keys,512,&remapped)==PORTABLE_INPUT_UP);
    keys[25]=true;keys[9]=true;
    CHECK(portable_keyboard_driving_buttons(keys,512,&remapped)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_UP|PORTABLE_INPUT_HOP));
    CHECK(portable_keyboard_driving_buttons(keys,64,&remapped)==0u);
    /* Player 2 defaults to W/S/A/D, Q hop and E item, without P1 aliases. */
    bool keys2[512]={0};keys2[26]=true;keys2[4]=true;keys2[8]=true;keys2[82]=true;
    CHECK(portable_keyboard2_driving_buttons(keys2,512,&defaults)==
          (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_UP|PORTABLE_INPUT_LEFT|PORTABLE_INPUT_ITEM));
    CHECK(portable_keyboard_driving_buttons(keys2,512,&defaults)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_UP));
    CHECK(portable_setting_get(&defaults,PORTABLE_SETTING_KEY2_HOP)==20u);
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_KEY2_ITEM,44u));
    CHECK(portable_setting_set(&s,PORTABLE_SETTING_KEY2_ITEM,9u)&&s.keys[13]==9u);
    PortableSettingsOverrides p2;
    char *p2_args[]={"--key2-hop","21","--key2-hop","23","--volume","40"};
    CHECK(!portable_settings_cli(2,p2_args,&p2,error));
    CHECK(portable_settings_cli(4,p2_args+2,&p2,error)&&p2.values.keys[12]==23u&&
          p2.mask==((UINT64_C(1)<<PORTABLE_SETTING_KEY2_HOP)|(UINT64_C(1)<<PORTABLE_SETTING_VOLUME)));
    const char p2_json[]="{\"key2_aim_down\":10,\"key_hop\":11}";
    CHECK(portable_settings_parse(p2_json,strlen(p2_json),&s,error)&&s.keys[15]==10u&&s.keys[4]==11u);
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_KEY_ITEM,41u));
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_KEY_ITEM,19u));
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_KEY_ITEM,512u));
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_PAD_ITEM,6u));
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_PAD_ITEM,26u));
    const char *invalid[]={"{\"aspect_width\":239}","{\"aspect_width\":1025}","{\"internal_scale\":0}","{\"internal_scale\":9}","{\"fit_scaling\":1}","", "[]", "null", "{", "{\"master_volume\":35,}",
        "{\"window_scale\":2 \"vsync\":true}","{\"vsync\":truefalse}",
        "{\"window_scale\":2,\"master_volume\":101}","{\"window_scale\":2,\"window_scale\":3}",
        "{\"version\":2}","{\"version\":1,\"version\":1}","{\"unknown\":true}",
        "{\"master_volume\":-1}","{\"master_volume\":01}","{\"master_volume\":1.5}",
        "{\"master_volume\":42949672960}","{\"fullscreen\":1}","{\"filter\":\"smooth\"}",
        "{\"interpolate\":1}","{\"render_fps\":1001}","{\"key_accelerate\":0}","{\"key2_accelerate\":40}","{\"key2_item\":5,\"key2_item\":6}","{\"pad_hop\":6}","{\"key_item\":true}","{} {}","{\"steering_deadzone\":0}","{\"steering_deadzone\":30001}",
        "{\"window_scale\":0}","{\"window_scale\":9}","{\"vsync\":tru}",
        "{\"vsync\":false,\"master_volume\":\"5\"}"};
    for(unsigned i=0u;i<sizeof invalid/sizeof invalid[0];++i){
        s=configured;
        CHECK(!portable_settings_parse(invalid[i],strlen(invalid[i]),&s,error));
        CHECK(error[0]!='\0'&&equal(&s,&defaults));
    }
    const char *bad_mods[]={"{\"mods\":true}","{\"mods\":[\"../escape\"]}","{\"mods\":[\"a\",\"a\"]}","{\"mods\":[],\"mods\":[]}","{\"mods\":[\"a\",]}"};
    for(unsigned i=0u;i<sizeof bad_mods/sizeof bad_mods[0];++i)CHECK(!portable_settings_parse(bad_mods[i],strlen(bad_mods[i]),&s,error)&&equal(&s,&defaults));
    const char mod_json[]="{\"mods\":[\"first\",\"second\"]}";
    CHECK(portable_settings_parse(mod_json,strlen(mod_json),&s,error)&&s.mod_count==2u&&strcmp(s.mods[1],"second")==0);
    const char escaped[]=" \n{\"master_\\u0076olume\":0,\"filter\":\"nea\\u0072est\"}\t";
    CHECK(portable_settings_parse(escaped,strlen(escaped),&s,error));
    CHECK(s.master_volume==0u&&s.linear_filter==0u&&s.window_scale==4u);
    CHECK(portable_settings_parse("{}",2u,&s,error)&&equal(&s,&defaults));
    char directory[256],path[300];
    CHECK(test_temp_path(directory,sizeof directory,"settings",1)==0);
    CHECK(snprintf(path,sizeof path,"%s/settings.json",directory)>0);
    CHECK(portable_settings_load(path,&s,error)==0&&equal(&s,&defaults));
    CHECK(portable_settings_save(path,&configured));
    CHECK(portable_settings_load(path,&s,error)==1&&equal(&s,&configured));
    CHECK(portable_settings_save(path,&defaults));
    CHECK(portable_settings_load(path,&s,error)==1&&equal(&s,&defaults));
    s.master_volume=255u;
    CHECK(!portable_settings_save(path,&s));
    CHECK(portable_settings_load(path,&s,error)==1&&equal(&s,&defaults));
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    CHECK(fputs("{\"window_scale\":8,\"filter\":false}",file)>=0&&fclose(file)==0);
    CHECK(portable_settings_load(path,&s,error)==-1&&equal(&s,&defaults));
    file=fopen(path,"rb");CHECK(file!=NULL&&fgetc(file)=='{'&&fclose(file)==0);
    char *args[]={"--window-scale","3","--fullscreen","--no-vsync","--volume","0","--filter","linear","--steering-deadzone","22000","--short-transitions","--key-accelerate","25","--pad-hop","3","--interpolate","--render-fps","144","--internal-scale","7","--fit-scaling"};
    PortableSettingsOverrides overrides;
    CHECK(portable_settings_cli((int)(sizeof args/sizeof args[0]),args,&overrides,error));
    s=defaults;portable_settings_apply(&s,&overrides);
    CHECK(s.window_scale==3u&&s.fullscreen&&s.vsync==0u&&s.master_volume==0u&&s.linear_filter&&s.steering_deadzone==22000u&&s.short_transitions&&s.keys[0]==25u&&s.pad_buttons[2]==3u&&s.interpolate&&s.render_fps==144u&&s.internal_scale==7u&&s.fit_scaling);
    char *wide_args[]={"--aspect","16:9"},*auto_args[]={"--aspect","auto"};
    CHECK(portable_settings_cli(2,wide_args,&overrides,error));
    s=defaults;portable_settings_apply(&s,&overrides);CHECK(s.aspect_width==284u);
    CHECK(portable_settings_cli(2,auto_args,&overrides,error));
    portable_settings_apply(&s,&overrides);CHECK(s.aspect_width==0u);
    char *wide_ui_args[]={"--wide-ui-backgrounds","--no-wide-ui-backgrounds"};
    CHECK(portable_settings_cli(1,wide_ui_args,&overrides,error));
    s=defaults;portable_settings_apply(&s,&overrides);CHECK(s.wide_ui_backgrounds==1u);
    CHECK(portable_settings_cli(2,wide_ui_args,&overrides,error));
    portable_settings_apply(&s,&overrides);CHECK(!s.wide_ui_backgrounds);
    const char wide_ui_json[]="{\"wide_ui_backgrounds\":true}";
    CHECK(portable_settings_parse(wide_ui_json,strlen(wide_ui_json),&s,error)&&s.wide_ui_backgrounds);
    const char bad_wide_ui[]="{\"wide_ui_backgrounds\":1}";
    CHECK(!portable_settings_parse(bad_wide_ui,strlen(bad_wide_ui),&s,error)&&equal(&s,&defaults));
    const char *bad_aspects[]={"0:9","16:0","-16:9","16:9x","42949672960:9","1:1","10000:1"};
    for(size_t i=0u;i<sizeof bad_aspects/sizeof bad_aspects[0];++i){
        char *args_aspect[]={"--aspect",(char *)bad_aspects[i]};
        CHECK(!portable_settings_cli(2,args_aspect,&overrides,error));
    }
    /* Quality-of-life switches default off and accept --NAME / --no-NAME. */
    CHECK(!defaults.rumble&&!defaults.input_display&&!defaults.mirror&&!defaults.fix_doppler_wrap&&!defaults.wide_ui_backgrounds);
    char *qol_args[]={"--rumble","--input-display","--mirror","--fix-doppler-wrap","--no-input-display"};
    CHECK(portable_settings_cli(5,qol_args,&overrides,error));
    s=defaults;portable_settings_apply(&s,&overrides);
    CHECK(s.rumble&&!s.input_display&&s.mirror&&s.fix_doppler_wrap);
    const char qol_json[]="{\"rumble\":true,\"input_display\":true,\"mirror_mode\":true}";
    CHECK(portable_settings_parse(qol_json,strlen(qol_json),&s,error)&&s.rumble&&s.input_display&&s.mirror&&!s.fix_doppler_wrap);
    CHECK(portable_settings_save(path,&s));
    PortableSettings reloaded;
    CHECK(portable_settings_load(path,&reloaded,error)==1&&equal(&reloaded,&s));
    CHECK(defaults.driving_profile==0u);
    char *modern_args[]={"--driving-profile","modern"};
    CHECK(portable_settings_cli(2,modern_args,&overrides,error));
    s=defaults;portable_settings_apply(&s,&overrides);CHECK(s.driving_profile==1u);
    CHECK(portable_settings_save(path,&s));
    CHECK(portable_settings_load(path,&reloaded,error)==1&&equal(&reloaded,&s));
    CHECK(!portable_setting_set(&s,PORTABLE_SETTING_DRIVING_PROFILE,3u));
    char *air_args[]={"--driving-profile","modern-air"};
    CHECK(portable_settings_cli(2,air_args,&overrides,error));
    s=defaults;portable_settings_apply(&s,&overrides);CHECK(s.driving_profile==2u);
    CHECK(portable_settings_save(path,&s));
    CHECK(portable_settings_load(path,&reloaded,error)==1&&equal(&reloaded,&s));
    CHECK(!portable_settings_cli(1,modern_args,&overrides,error));
    char *invalid_profile[]={"--driving-profile","fast"};
    CHECK(!portable_settings_cli(2,invalid_profile,&overrides,error));
    const char modern_json[]="{\"driving_profile\":1}";
    CHECK(portable_settings_parse(modern_json,strlen(modern_json),&s,error)&&s.driving_profile==1u);
    const char invalid_json[]="{\"driving_profile\":true}";
    CHECK(!portable_settings_parse(invalid_json,strlen(invalid_json),&s,error)&&equal(&s,&defaults));
    CHECK(portable_settings_save(path,&defaults));
    FILE *stock_file=fopen(path,"rb");CHECK(stock_file!=NULL);
    char stock_json[8193];size_t stock_size=fread(stock_json,1u,sizeof stock_json-1u,stock_file);
    CHECK(fclose(stock_file)==0);stock_json[stock_size]='\0';
    CHECK(strstr(stock_json,"driving_profile")==NULL);
    CHECK(strstr(stock_json,"classic_menus")==NULL);
    CHECK(strstr(stock_json,"touch_controls")==NULL);
    CHECK(strstr(stock_json,"artwork_lod")==NULL);
    CHECK(strstr(stock_json,"race_renderer")==NULL);
    char *classic_args[]={"--classic-menus"};
    CHECK(portable_settings_cli(1,classic_args,&overrides,error));
    PortableSettings classic=defaults;portable_settings_apply(&classic,&overrides);
    CHECK(classic.classic_menus==1u&&portable_settings_save(path,&classic));
    PortableSettings classic_loaded;
    CHECK(portable_settings_load(path,&classic_loaded,error)==1&&classic_loaded.classic_menus==1u);
    CHECK(!portable_settings_parse("{\"mirror_mode\":1}",17u,&s,error));
    char *bad_switch[]={"--mirror-mode"};
    CHECK(!portable_settings_cli(1,bad_switch,&overrides,error));
    char *mod_args[]={"--mods-dir","local-mods","--mod","first","--mod","second","--no-mods"};
    PortableSettingsOverrides mod_overrides;
    CHECK(portable_settings_cli(7,mod_args,&mod_overrides,error));
    CHECK(mod_overrides.no_mods&&strcmp(mod_overrides.mods_dir,"local-mods")==0);
    portable_settings_apply(&s,&mod_overrides);
    CHECK(s.mod_count==2u&&strcmp(s.mods[0],"first")==0&&strcmp(s.mods[1],"second")==0);
    char *duplicate_mods[]={"--mod","first","--mod","first"};
    CHECK(!portable_settings_cli(4,duplicate_mods,&mod_overrides,error));
    /* Temporary command-line values never alter the persisted file. */
    CHECK(portable_settings_save(path,&configured));
    CHECK(portable_settings_load(path,&s,error)==1&&equal(&s,&configured));
    char *missing[]={"--volume"},*bad[]={"--volume","101"},*unknown[]={"--other"};
    CHECK(!portable_settings_cli(1,missing,&overrides,error));
    CHECK(!portable_settings_cli(2,bad,&overrides,error));
    CHECK(!portable_settings_cli(1,unknown,&overrides,error));
    touch_settings(path);
    drift_extras_cli(path);
    mod_directory_settings(path);
    artwork_settings(path);
    race_renderer_settings(path);
    effect_settings(path);
    immersive_settings(path);
    hd2d_settings(path);
    asset_source_settings(path);
    CHECK(remove(path)==0&&test_rmdir(directory)==0);
    puts("settings: PASS transactional fallback, replacement, temporary overrides");return 0;
}
