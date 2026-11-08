#include "ui_text.h"
#include "../core/text_glyph.h"
#include <string.h>
static const char *const defaults[]={
#define UI_STRING(id,value) [id-1u]=value,
#include "../core/ui_strings.inc"
#undef UI_STRING
};
const char *portable_ui_text_default(uint32_t id)
{return id>0u&&id<=PORTABLE_UI_TEXT_COUNT?defaults[id-1u]:NULL;}
int portable_ui_text_valid(uint32_t id,const char *value)
{
    const char *source=portable_ui_text_default(id);
    if(source==NULL || value==NULL || !*value || strlen(value)>strlen(source))return 0;
    for(const char *at=value;*at;++at)if(!portable_text_character(*at))return 0;
    return 1;
}
const char *portable_ui_text_resolve(const PortableUiTextBank *bank,const char *text)
{
    if(bank!=NULL && text!=NULL)for(unsigned i=0u;i<PORTABLE_UI_TEXT_COUNT;++i)
        if(bank->values[i]!=NULL && strcmp(text,defaults[i])==0)return bank->values[i];
    return text;
}
