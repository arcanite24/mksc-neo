#include "text_data.h"
#include "native_image.h"
#include "ui_text.h"
#include "../core/text_glyph.h"
#include <stdio.h>
#include <string.h>
static int fail(char *error,const char *field,const char *reason)
{snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"text.%s: %s",field,reason);return 0;}
int portable_text_data_parse(PortableJsonReader *r,PortableTextData *out,char *error)
{
    if(!portable_json_take(r,'['))return fail(error,"records","expected array");
    if(portable_json_take(r,']'))return 1;
    for(;;){
        if(out->count>=PORTABLE_TEXT_DATA_LIMIT||!portable_json_take(r,'{'))return fail(error,"records","expected at most64 objects");
        PortableTextOverride next={0};next.color_rgb=0xffffffu;unsigned seen=0u;
        for(;;){
            char key[32];uint32_t value;
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))return fail(error,"records","expected field and colon");
            const unsigned field=strcmp(key,"id")==0?1u:strcmp(key,"value")==0?2u:strcmp(key,"color_rgb")==0?4u:0u;
            if(field==0u||(seen&field)!=0u)return fail(error,key,"unknown or duplicate field");
            seen|=field;
            if(field==2u){
                if(!portable_json_string(r,next.value,sizeof next.value)||next.value[0]=='\0'||strlen(next.value)>48u)return fail(error,"value","expected1..48 supported characters");
                for(const char *at=next.value;*at!='\0';++at)if(!portable_text_character(*at))return fail(error,"value","unsupported character; use uppercase menu glyphs");
            }else{
                if(!portable_json_number(r,&value)||(field==1u?value==0u:value>0xffffffu))return fail(error,key,"integer out of range");
                if(field==1u)next.id=value;else next.color_rgb=value;
            }
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return fail(error,"records","expected comma or brace");
        }
        if((seen&3u)!=3u)return fail(error,"records","id and value required");
        for(unsigned i=0u;i<out->count;++i)if(out->records[i].id==next.id)return fail(error,"id","duplicate graphic ID");
        out->records[out->count++]=next;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return fail(error,"records","expected comma or bracket");
    }
}
int portable_text_data_render(const PortableTextOverride *record,const PortableAssetSectionView *source,uint8_t *bytes,char *error)
{
    PortableNativeImage image;
    if(source->version!=1u||!portable_native_image_open(source->bytes,source->size,&image))return fail(error,"id","expected native frontend image v1");
    const unsigned width=(unsigned)strlen(record->value)*4u-1u;
    unsigned scale=image.width/width;
    if(scale>image.height/5u)scale=image.height/5u;
    if(scale>4u)scale=4u;
    if(scale==0u)return fail(error,"value","text does not fit image canvas");
    memcpy(bytes,source->bytes,32u);memset(bytes+32u,0,source->size-32u);
    const unsigned left=(image.width-width*scale)/2u,top=(image.height-5u*scale)/2u;
    for(unsigned i=0u;record->value[i]!='\0';++i){
        const uint16_t glyph=portable_text_glyph(record->value[i]);
        for(unsigned row=0u;row<5u;++row)for(unsigned column=0u;column<3u;++column){
            if((glyph&(1u<<(14u-row*3u-column)))==0u)continue;
            for(unsigned dy=0u;dy<scale;++dy)for(unsigned dx=0u;dx<scale;++dx){
                const size_t offset=32u+((top+row*scale+dy)*image.width+left+(i*4u+column)*scale+dx)*4u;
                const uint32_t color=0xff000000u|record->color_rgb;
                for(unsigned byte=0u;byte<4u;++byte)bytes[offset+byte]=(uint8_t)(color>>(byte*8u));
            }
        }
    }
    return 1;
}

static int ui_fail(char *error,const char *field,const char *reason)
{snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"strings.%s: %s",field,reason);return 0;}
int portable_ui_strings_parse(PortableJsonReader *r,PortableTextData *out,char *error)
{
    if(!portable_json_take(r,'['))return ui_fail(error,"records","expected array");
    if(portable_json_take(r,']'))return 1;
    for(;;){
        if(out->count>=PORTABLE_TEXT_DATA_LIMIT || !portable_json_take(r,'{'))
            return ui_fail(error,"records","expected at most64 objects");
        PortableTextOverride next={0};unsigned seen=0u;
        for(;;){
            char key[32];
            if(!portable_json_string(r,key,sizeof key)||!portable_json_take(r,':'))
                return ui_fail(error,"records","expected field and colon");
            const unsigned field=strcmp(key,"id")==0?1u:strcmp(key,"value")==0?2u:0u;
            if(!field || (seen&field))return ui_fail(error,key,"unknown or duplicate field");
            seen|=field;
            if(field==1u){
                if(!portable_json_number(r,&next.id) || portable_ui_text_default(next.id)==NULL)
                    return ui_fail(error,"id","unknown UI string ID");
            }else if(!portable_json_string(r,next.value,sizeof next.value))
                return ui_fail(error,"value","expected bounded string");
            if(portable_json_take(r,'}'))break;
            if(!portable_json_take(r,','))return ui_fail(error,"records","expected comma or brace");
        }
        if(seen!=3u)return ui_fail(error,"records","id and value required");
        if(!portable_ui_text_valid(next.id,next.value))
            return ui_fail(error,"value","unsupported glyph, empty or longer than original label");
        for(unsigned i=0u;i<out->count;++i)if(out->records[i].id==next.id)
            return ui_fail(error,"id","duplicate UI string ID");
        out->records[out->count++]=next;
        if(portable_json_take(r,']'))return 1;
        if(!portable_json_take(r,','))return ui_fail(error,"records","expected comma or bracket");
    }
}
