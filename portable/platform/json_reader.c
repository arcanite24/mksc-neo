#include "json_reader.h"
#include <string.h>
void portable_json_whitespace(PortableJsonReader *r)
{
    while(r->at<r->end&&(*r->at==' '||*r->at=='\t'||*r->at=='\r'||*r->at=='\n'))++r->at;
}
int portable_json_take(PortableJsonReader *r,char c)
{
    portable_json_whitespace(r);
    if(r->at==r->end||*r->at!=c)return 0;
    ++r->at;return 1;
}
static int hex_digit(char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
/* The schema's names and enum strings are ASCII, including escaped ASCII. */
int portable_json_string(PortableJsonReader *r,char *out,size_t capacity)
{
    if(!portable_json_take(r,'"'))return 0;
    size_t length=0u;
    while(r->at<r->end){
        unsigned c=(unsigned char)*r->at++;
        if(c=='"'){out[length]='\0';return 1;}
        if(c=='\\'){
            if(r->at==r->end)return 0;
            c=(unsigned char)*r->at++;
            if(c=='u'){
                c=0u;
                for(unsigned i=0u;i<4u;++i){
                    if(r->at==r->end)return 0;
                    const int digit=hex_digit(*r->at++);
                    if(digit<0)return 0;
                    c=c*16u+(unsigned)digit;
                }
            }else if(c=='b')c='\b';
            else if(c=='f')c='\f';
            else if(c=='n')c='\n';
            else if(c=='r')c='\r';
            else if(c=='t')c='\t';
            else if(c!='"'&&c!='\\'&&c!='/')return 0;
        }else if(c<32u)return 0;
        if(c==0u||c>127u||length+1u>=capacity)return 0;
        out[length++]=(char)c;
    }
    return 0;
}
int portable_json_number(PortableJsonReader *r,uint32_t *out)
{
    portable_json_whitespace(r);
    if(r->at==r->end||*r->at<'0'||*r->at>'9')return 0;
    const char first=*r->at;
    uint32_t value=0u;unsigned digits=0u;
    while(r->at<r->end&&*r->at>='0'&&*r->at<='9'){
        const unsigned digit=(unsigned)(*r->at++-'0');
        if(value>(UINT32_MAX-digit)/10u)return 0;
        value=value*10u+digit;++digits;
    }
    if(first=='0'&&digits>1u)return 0;
    *out=value;return 1;
}
int portable_json_boolean(PortableJsonReader *r,uint32_t *out)
{
    portable_json_whitespace(r);
    if((size_t)(r->end-r->at)>=4u&&memcmp(r->at,"true",4u)==0){r->at+=4;*out=1u;return 1;}
    if((size_t)(r->end-r->at)>=5u&&memcmp(r->at,"false",5u)==0){r->at+=5;*out=0u;return 1;}
    return 0;
}
