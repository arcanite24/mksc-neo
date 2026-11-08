#include "mod_identity.h"
#include "text_number.h"
#include <stdio.h>
#include <string.h>

static PortableModIdentity current={PORTABLE_MOD_API_VERSION,0u,{{{0},{0},{0}}}};
static char record_suffix[PORTABLE_RECORD_SUFFIX_CAPACITY];
static char profile_suffix[32],progress_suffix[48];
static uint8_t mirror_mode;

size_t portable_mod_identity_format(const PortableModIdentity *identity,char *text,size_t capacity)
{
    if(identity==NULL||text==NULL||capacity==0u||identity->count>PORTABLE_MOD_LIMIT)return 0u;
    int written=snprintf(text,capacity,"MODS1 %u %u",(unsigned)identity->api_version,(unsigned)identity->count);
    if(written<0||(size_t)written>=capacity)return 0u;
    size_t length=(size_t)written;
    for(uint32_t i=0u;i<identity->count;++i){
        const PortableModIdentityEntry *mod=&identity->mods[i];
        const uint8_t *f=mod->fingerprint;
        written=snprintf(text+length,capacity-length," %s %s %02x%02x%02x%02x%02x%02x%02x%02x",
            mod->id,mod->version,f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7]);
        if(written<0||(size_t)written>=capacity-length)return 0u;
        length+=(size_t)written;
    }
    return length;
}

static int token(const char **at,const char *end,char *out,size_t capacity)
{
    if(*at>=end||**at!=' ')return 0;
    ++*at;
    size_t length=0u;
    while(*at<end&&**at!=' '){
        const char c=**at;
        if(c<'!'||c>'~'||length+1u>=capacity)return 0;
        out[length++]=c;++*at;
    }
    out[length]='\0';
    return length!=0u;
}

static int id_valid(const char *id)
{
    for(size_t i=0u;id[i]!='\0';++i){
        const char c=id[i];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||(i>0u&&(c=='-'||c=='_'))))return 0;
    }
    return id[0]!='\0';
}

static int hex_value(char c)
{
    return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
}

int portable_mod_identity_parse(const char *text,size_t size,PortableModIdentity *out)
{
    if (text == NULL || out == NULL || size < 5u || memcmp(text, "MODS1", 5u) != 0)
        return 0;
    PortableModIdentity next = {0};
    const char *at = text + 5, *end = text + size;
    char field[64];
    uint64_t value;
    const char *tail;
    if (!token(&at, end, field, sizeof field))
        return 0;
    tail = field;
    if (!portable_text_u64(&tail, field + strlen(field), 10u, UINT32_MAX, &value) ||
        *tail != '\0' || value != PORTABLE_MOD_API_VERSION)
        return 0;
    next.api_version = (uint32_t)value;
    if (!token(&at, end, field, sizeof field))
        return 0;
    tail = field;
    if (!portable_text_u64(&tail, field + strlen(field), 10u, PORTABLE_MOD_LIMIT, &value) ||
        *tail != '\0')
        return 0;
    next.count = (uint32_t)value;
    for (uint32_t i = 0u; i < next.count; ++i)
    {
        PortableModIdentityEntry *mod = &next.mods[i];
        char fingerprint[17];
        if (!token(&at, end, mod->id, sizeof mod->id) || !id_valid(mod->id) ||
            !token(&at, end, mod->version, sizeof mod->version) ||
            !token(&at, end, fingerprint, sizeof fingerprint) || strlen(fingerprint) != 16u)
            return 0;
        for (unsigned b = 0u; b < 8u; ++b)
        {
            const int high = hex_value(fingerprint[b * 2u]),
                      low = hex_value(fingerprint[b * 2u + 1u]);
            if (high < 0 || low < 0)
                return 0;
            mod->fingerprint[b]=(uint8_t)(high<<4|low);
        }
    }
    if(at!=end)return 0;
    *out=next;
    return 1;
}

int portable_mod_identity_equal(const PortableModIdentity *a,const PortableModIdentity *b)
{
    if(a==NULL||b==NULL||a->api_version!=b->api_version||a->count!=b->count)return 0;
    for(uint32_t i=0u;i<a->count;++i)
        if(strcmp(a->mods[i].id,b->mods[i].id)!=0||strcmp(a->mods[i].version,b->mods[i].version)!=0||
           memcmp(a->mods[i].fingerprint,b->mods[i].fingerprint,8u)!=0)return 0;
    return 1;
}

void portable_mod_identity_describe(const PortableModIdentity *identity,char *text,size_t capacity)
{
    if(text==NULL||capacity==0u)return;
    text[0]='\0';
    if(identity==NULL||identity->count==0u){snprintf(text,capacity,"none");return;}
    size_t length=0u;
    for(uint32_t i=0u;i<identity->count&&length<capacity;++i){
        const int written=snprintf(text+length,capacity-length,"%s%s %s",i?", ":"",
            identity->mods[i].id,identity->mods[i].version);
        if(written<0)return;
        length+=(size_t)written;
    }
}

const PortableModIdentity *portable_mod_identity_current(void){return &current;}

void portable_mod_identity_set_current(const PortableModIdentity *identity)
{
    if(identity==NULL||identity->count>PORTABLE_MOD_LIMIT){
        memset(&current,0,sizeof current);current.api_version=PORTABLE_MOD_API_VERSION;return;
    }
    current=*identity;
}

const char *portable_mod_identity_record_suffix(void)
{
    char text[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY];
    const size_t length=current.count==0u?0u:portable_mod_identity_format(&current,text,sizeof text);
    if(length==0u){snprintf(record_suffix,sizeof record_suffix,"%s",portable_mod_identity_progress_suffix());return record_suffix;}
    uint32_t hash=UINT32_C(2166136261);
    for(size_t i=0u;i<length;++i)hash=(hash^(uint8_t)text[i])*UINT32_C(16777619);
    snprintf(record_suffix,sizeof record_suffix,"%s-mods-%08x",portable_mod_identity_progress_suffix(),(unsigned)hash);
    return record_suffix;
}

void portable_mod_identity_set_mirror(uint8_t mirror){mirror_mode=(uint8_t)(mirror!=0u);}
uint8_t portable_mod_identity_mirror(void){return mirror_mode;}
const char *portable_mod_identity_mirror_suffix(void){return mirror_mode?"-mirror":"";}

void portable_mod_identity_set_driving_profile(const uint8_t fingerprint[8])
{
    if(fingerprint==NULL){profile_suffix[0]='\0';return;}
    static const char hex[]="0123456789abcdef";
    memcpy(profile_suffix,"-modern-",8u);
    for(size_t i=0u;i<8u;++i){
        profile_suffix[8u+i*2u]=hex[fingerprint[i]>>4];
        profile_suffix[9u+i*2u]=hex[fingerprint[i]&15u];
    }
    profile_suffix[24]='\0';
}

const char *portable_mod_identity_progress_suffix(void)
{
    snprintf(progress_suffix,sizeof progress_suffix,"%s%s",portable_mod_identity_mirror_suffix(),profile_suffix);
    return progress_suffix;
}
