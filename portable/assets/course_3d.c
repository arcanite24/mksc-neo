#include "course_3d.h"
#include "mesh_kart_rules_asset.h"
#include "mesh_item_rules_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG(s) PORTABLE_COURSE_3D_TAG((s)[0],(s)[1],(s)[2],(s)[3])
#define ABSENT PORTABLE_COURSE_3D_ABSENT
#define POSITION_LIMIT INT32_C(268435456)
_Static_assert(PORTABLE_COURSE_3D_TEXTURE_BUNDLE_KIND == 108 && PORTABLE_ASSET_SECTION_COURSE_3D_TEXTURE_BUNDLE == 108, "kind 108");

typedef struct { const char *name; uint32_t stride, limit; uint8_t required; } TableRule;
static const TableRule table_rules[] = {
    {"STRS",1,65536,1},{"META",128,1,1},{"VERT",32,131072,1},{"TRIS",16,262144,1},
    {"MATS",48,256,1},{"COLV",12,131072,1},{"COLT",32,262144,1},{"SURF",48,256,1},
    {"CPNT",64,1024,1},{"CPED",4,16384,1},{"RTHD",32,16384,1},{"RTND",32,16384,1},
    {"RTED",4,16384,1},{"GRID",32,64,1},{"VCOL",4,131072,0},{"UV2S",8,131072,0},
    {"ITEM",32,4096,0},{"OBJS",64,4096,0},{"RESP",48,1024,0},{"CAMR",64,1024,0},
    {"ZONE",80,4096,0},{"PROP",32,16384,0},{"TXAN",32,1024,0},{"TXSR",24,65536,0},
    {"TXPT",8,16384,0}
};

static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8u) | ((uint32_t)p[2]<<16u) | ((uint32_t)p[3]<<24u); }
static int32_t signed_word(const uint8_t *p)
{
    const uint32_t value=word(p);
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static int failure(char *error,size_t capacity,uint32_t id,const char *table,uint32_t row,const char *reason)
{
    if(error && capacity)snprintf(error,capacity,"3D course=%u table=%s row=%u: %s",
        (unsigned)id,table,(unsigned)row,reason);
    return 0;
}
const PortableCourse3DTable *portable_course_3d_table(const PortableCourse3DView *view,uint32_t tag)
{
    if(view)for(uint32_t i=0;i<view->table_count;++i)if(view->tables[i].tag==tag)return &view->tables[i];
    return NULL;
}
static const uint8_t *row_at(const PortableCourse3DView *v,const char *name,uint32_t index)
{
    const PortableCourse3DTable *t=portable_course_3d_table(v,TAG(name));
    return t && index<t->count?t->bytes+(size_t)index*t->stride:NULL;
}
static uint32_t count_at(const PortableCourse3DView *v,const char *name)
{
    const PortableCourse3DTable *t=portable_course_3d_table(v,TAG(name));
    return t?t->count:0u;
}
static int span(uint32_t first,uint32_t count,uint32_t total)
{ return first<=total && count<=total-first; }
static int index_valid(uint32_t index,uint32_t count,int optional)
{ return index<count || (optional && index==ABSENT); }
static int zeros(const uint8_t *p,unsigned words)
{ for(unsigned i=0;i<words;++i)if(word(p+i*4u))return 0;return 1; }
static int position(const uint8_t *p,unsigned components)
{
    for(unsigned i=0;i<components;++i){const int32_t n=signed_word(p+i*4u);if(n < -POSITION_LIMIT || n>POSITION_LIMIT)return 0;}
    return 1;
}
static int unit(const uint8_t *p,unsigned components)
{
    uint64_t sum=0;
    for(unsigned i=0;i<components;++i){const int64_t n=signed_word(p+i*4u);
        if(n < -INT64_C(1073741824) || n>INT64_C(1073741824))return 0;
        sum+=(uint64_t)(n*n);}
    const uint64_t one=UINT64_C(1)<<60u,tolerance=UINT64_C(1)<<50u;
    return sum>=one-tolerance && sum<=one+tolerance;
}
static int perpendicular(const uint8_t *first,const uint8_t *second)
{
    int64_t dot=0;
    for(unsigned i=0;i<3u;++i)
        dot+=(int64_t)signed_word(first+i*4u)*signed_word(second+i*4u);
    /* Both inputs passed unit(), so three Q30 products fit signed 64 bits. */
    const int64_t tolerance=INT64_C(1)<<50u;
    return dot>=-tolerance && dot<=tolerance;
}
static int utf8(const uint8_t *p,size_t length)
{
    for(size_t i=0;i<length;){uint32_t cp=p[i++],minimum=0;unsigned extra=0;
        if(cp<128u)continue;
        if(cp>=0xc2u && cp<=0xdfu){cp&=31u;extra=1;minimum=128;}
        else if(cp>=0xe0u && cp<=0xefu){cp&=15u;extra=2;minimum=2048;}
        else if(cp>=0xf0u && cp<=0xf4u){cp&=7u;extra=3;minimum=65536;}
        else return 0;
        if(extra>length-i)return 0;
        while(extra--){if((p[i]&0xc0u)!=0x80u)return 0;cp=(cp<<6u)|(p[i++]&63u);}
        if(cp<minimum || cp>0x10ffffu || (cp>=0xd800u && cp<=0xdfffu))return 0;
    }
    return 1;
}
static const char *string_at(const PortableCourse3DView *v,uint32_t offset)
{
    const PortableCourse3DTable *t=portable_course_3d_table(v,TAG("STRS"));
    if(!t || offset>=t->count || !memchr(t->bytes+offset,0,t->count-offset))return NULL;
    if(offset && (t->bytes[offset]&0xc0u)==0x80u)return NULL;
    return (const char *)t->bytes+offset;
}
static int reference(const PortableCourse3DView *v,uint32_t kind,uint32_t id,int optional)
{
    PortableAssetSectionView section;
    return (optional && id==ABSENT) || portable_asset_pack_find_section(v->pack,kind,id,&section)==PORTABLE_ASSET_PACK_OK;
}
static int image(const uint8_t *bytes,size_t size,PortableNativeImage *out)
{
    return bytes && size>=8u && !memcmp(bytes,"MKSCIMG1",8u) && portable_native_image_open(bytes,size,out) &&
        out->width<=4096u && out->height<=4096u && (uint32_t)out->width*out->height<=4000000u;
}
/* TXB1: header {magic, version 1, size, count}, count ascending 16-byte
 * entries {texture id, offset, size, 0}, then 4-byte aligned MKSCIMG1 images. */
static int bundle_header(const PortableAssetSectionView *s,uint32_t *count)
{
    if(s->version!=1u || !s->bytes || s->size<16u || word(s->bytes)!=TAG("TXB1") || word(s->bytes+4u)!=1u ||
       word(s->bytes+8u)!=s->size)return 0;
    *count=word(s->bytes+12u);
    return *count && *count<=1024u && *count<=(s->size-16u)/16u;
}
static int bundle_entry(const PortableAssetSectionView *s,uint32_t count,uint32_t index,PortableNativeImage *out)
{
    const uint8_t *p=s->bytes+16u+(size_t)index*16u;
    const uint32_t at=word(p+4u),size=word(p+8u);
    return at%4u==0 && at>=16u+count*16u && at<=s->size && size<=s->size-at && !word(p+12u) &&
        image(s->bytes+at,size,out);
}
static int bundle_find(const PortableAssetPackView *pack,uint32_t bundle,uint32_t id,PortableNativeImage *out)
{
    PortableAssetSectionView s;uint32_t count;
    if(portable_asset_pack_find_section(pack,PORTABLE_COURSE_3D_TEXTURE_BUNDLE_KIND,bundle,&s)!=PORTABLE_ASSET_PACK_OK ||
       !bundle_header(&s,&count))return 0;
    uint32_t low=0,high=count;
    while(low<high){const uint32_t middle=low+(high-low)/2u,key=word(s.bytes+16u+(size_t)middle*16u);
        if(key==id)return bundle_entry(&s,count,middle,out);
        if(key<id)low=middle+1u;else high=middle;}
    return 0;
}
/* Standalone kind-101 images first, then the course's META texture bundles. */
int portable_course_3d_texture(const PortableCourse3DView *v,uint32_t id,PortableNativeImage *out)
{
    PortableAssetSectionView section;
    if(!v || !out || id==ABSENT)return 0;
    if(portable_asset_pack_find_section(v->pack,PORTABLE_COURSE_3D_TEXTURE_KIND,id,&section)==PORTABLE_ASSET_PACK_OK)
        return section.version==1u && image(section.bytes,section.size,out);
    const uint8_t *meta=row_at(v,"META",0);
    if(!meta)return 0;
    const uint32_t first=word(meta+96u),count=word(meta+100u);
    for(uint32_t i=0;i<count && span(first,count,count_at(v,"PROP"));++i){
        PortableCourse3DProperty property;
        if(portable_course_3d_property_at(v,first+i,&property) && !strcmp(property.key,"mksc:texture-bundle") &&
           property.type==PORTABLE_COURSE_3D_PROPERTY_ASSET &&
           property.value.asset.kind==PORTABLE_COURSE_3D_TEXTURE_BUNDLE_KIND &&
           bundle_find(v->pack,property.value.asset.id,id,out))return 1;
    }
    return 0;
}
int portable_course_3d_minimap(const PortableCourse3DView *v,PortableCourse3DMinimap *out)
{
    const uint8_t *meta=v && out?row_at(v,"META",0):NULL;
    if(!meta || word(meta+40u)==ABSENT || !portable_course_3d_texture(v,word(meta+40u),&out->image) ||
       out->image.palette)return 0;
    const uint32_t first=word(meta+96u),count=word(meta+100u);
    unsigned found=0;
    for(uint32_t i=0;i<count && span(first,count,count_at(v,"PROP"));++i){
        PortableCourse3DProperty property;
        if(!portable_course_3d_property_at(v,first+i,&property) || property.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR)continue;
        const int high=!strcmp(property.key,"mksc:minimap-high");
        if(!high && strcmp(property.key,"mksc:minimap-low"))continue;
        for(unsigned axis=0;axis<2u;++axis)(high?out->high_q16:out->low_q16)[axis]=property.value.vector_q16[axis];
        found|=1u<<high;
    }
    return found==3u && out->low_q16[0]<out->high_q16[0] && out->low_q16[1]<out->high_q16[1];
}
uint32_t portable_course_3d_presence(uint32_t mode,unsigned engine_class)
{
    unsigned slot=0u;
    while(slot<5u && mode!=1u<<slot)++slot;
    return slot<5u && engine_class<4u ? UINT32_C(1)<<(slot*4u+engine_class) : PORTABLE_COURSE_3D_PRESENT_ALL;
}
int portable_course_3d_present(const PortableCourse3DView *v,uint32_t first,uint32_t count,uint32_t presence)
{
    PortableCourse3DProperty property;
    return presence==PORTABLE_COURSE_3D_PRESENT_ALL ||
           !portable_course_3d_property_find(v,first,count,"mksc:present-in",&property) ||
           property.type!=PORTABLE_COURSE_3D_PROPERTY_INTEGER ||
           ((uint32_t)property.value.integer&presence)!=0u;
}
int portable_course_3d_chase(const PortableCourse3DView *v,PortableCourse3DChase *out,char *error,size_t capacity)
{
    PortableCourse3DMeta meta;
    if(!v || !out || !portable_course_3d_meta(v,&meta))
        return failure(error,capacity,v?v->id:0u,"META",0,"invalid chase metadata");
    PortableCourse3DChase c={.kart_scale_q16=65536};
    unsigned vectors=0;
    for(uint32_t i=0;i<meta.property_count;++i){
        PortableCourse3DProperty property;
        if(!portable_course_3d_property_at(v,meta.property_first+i,&property))
            return failure(error,capacity,v->id,"META",i,"invalid chase property");
        if(!strcmp(property.key,"mksc:kart-scale")){
            if(property.type!=PORTABLE_COURSE_3D_PROPERTY_FIXED || property.value.fixed_q16<8192 ||
               property.value.fixed_q16>262144)
                return failure(error,capacity,v->id,"META",i,"mksc:kart-scale must be fixed in [0.125, 4]");
            c.kart_scale_q16=property.value.fixed_q16;
        }else if(!strcmp(property.key,"mksc:camera-fov")){
            if(property.type!=PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer<1820 ||
               property.value.integer>21846)
                return failure(error,capacity,v->id,"META",i,"mksc:camera-fov must be an integer of 1820..21846 turns/65536");
            c.fov_turns=(uint32_t)property.value.integer;
        }else{
            const int eye=!strcmp(property.key,"mksc:camera-eye");
            if(!eye && strcmp(property.key,"mksc:camera-target"))continue;
            const int32_t *q=property.value.vector_q16;
            if(property.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR || q[0] || q[1]<-16*65536 || q[1]>16*65536 ||
               q[2]<0 || q[2]>16*65536)
                return failure(error,capacity,v->id,"META",i,"chase camera vectors need x 0, |y| <= 16 m and z in [0, 16] m");
            memcpy(eye?c.eye_q16:c.target_q16,q,sizeof c.eye_q16);
            vectors|=eye?1u:2u;
        }
    }
    if(vectors==1u || vectors==2u)
        return failure(error,capacity,v->id,"META",0,"mksc:camera-eye and mksc:camera-target come together");
    /* The eye rests behind and not below its aim, at least 1 m away. */
    if(vectors && (c.eye_q16[1]>-65536 || c.target_q16[1]<0 || c.eye_q16[2]<c.target_q16[2]))
        return failure(error,capacity,v->id,"META",0,"chase eye must be 1 m or more behind the foot, not below its target");
    c.has_rig=vectors!=0u;
    *out=c;
    return 1;
}
static int texture_valid(const PortableCourse3DView *v,uint32_t id)
{ PortableNativeImage image;return id==ABSENT || portable_course_3d_texture(v,id,&image); }

static int directory(const PortableAssetSectionView *s,PortableCourse3DView *v,char *error,size_t capacity)
{
    if(!s->bytes || s->version!=1u || s->size<64u || s->size>32u*1024u*1024u ||
       word(s->bytes)!=TAG("C3D1") || word(s->bytes+4u)!=1u || word(s->bytes+8u)!=s->size ||
       word(s->bytes+12u)!=s->content_id || word(s->bytes+16u) || !zeros(s->bytes+28u,9u))
        return failure(error,capacity,s->content_id,"header",0,"invalid header/version/reserved fields");
    const uint32_t offset=word(s->bytes+20u),count=word(s->bytes+24u);
    if(!count || count>32u || offset<64u || offset%4u || offset>s->size || count>(s->size-offset)/20u)
        return failure(error,capacity,s->content_id,"directory",0,"invalid directory span");
    v->bytes=s->bytes;v->size=s->size;v->id=s->content_id;v->table_count=count;
    uint32_t offsets[32],lengths[32];
    for(uint32_t i=0;i<count;++i){
        const uint8_t *p=s->bytes+offset+i*20u;
        const uint32_t tag=word(p),at=word(p+4u),n=word(p+8u),stride=word(p+12u),flags=word(p+16u);
        const TableRule *rule=NULL;
        for(size_t r=0;r<sizeof table_rules/sizeof table_rules[0];++r)if(tag==TAG(table_rules[r].name))rule=&table_rules[r];
        if((flags&~1u) || (!rule && (flags&1u)) || !stride || at%4u || at<64u || at>s->size || n>(s->size-at)/stride ||
           (rule && (stride!=rule->stride || n>rule->limit || (rule->required && !(flags&1u)))))
            return failure(error,capacity,v->id,"directory",i,"unknown required table or invalid count/stride/span/flags");
        const uint32_t bytes=n*stride;
        if(bytes && at<offset+count*20u && offset<at+bytes)
            return failure(error,capacity,v->id,"directory",i,"table overlaps directory");
        for(uint32_t j=0;j<i;++j)if(v->tables[j].tag==tag ||
            (bytes && lengths[j] && at<offsets[j]+lengths[j] && offsets[j]<at+bytes))
            return failure(error,capacity,v->id,"directory",i,"duplicate or overlapping table");
        offsets[i]=at;lengths[i]=bytes;v->tables[i]=(PortableCourse3DTable){tag,n,stride,s->bytes+at};
    }
    for(size_t r=0;r<sizeof table_rules/sizeof table_rules[0];++r)if(table_rules[r].required){
        const PortableCourse3DTable *t=portable_course_3d_table(v,TAG(table_rules[r].name));
        if(!t || (!t->count && strcmp(table_rules[r].name,"CPNT") && strcmp(table_rules[r].name,"CPED") && strcmp(table_rules[r].name,"RTED")))
            return failure(error,capacity,v->id,table_rules[r].name,0,"required table missing or empty");
    }
    const PortableCourse3DTable *strings=portable_course_3d_table(v,TAG("STRS"));
    if(strings->bytes[strings->count-1u] || !utf8(strings->bytes,strings->count))
        return failure(error,capacity,v->id,"STRS",0,"invalid UTF-8 or unterminated strings");
    return 1;
}

static int properties(const PortableCourse3DView *v,uint32_t first,uint32_t count)
{ return span(first,count,count_at(v,"PROP")); }
/* mksc:present-in, when given, is an integer mask of the 20 race bits. */
static int presence_valid(const PortableCourse3DView *v,uint32_t first,uint32_t count)
{
    PortableCourse3DProperty property;
    return !portable_course_3d_property_find(v,first,count,"mksc:present-in",&property) ||
           (property.type==PORTABLE_COURSE_3D_PROPERTY_INTEGER && property.value.integer>0 &&
            property.value.integer<(INT32_C(1)<<20));
}
int portable_course_3d_property_at(const PortableCourse3DView *view, uint32_t index,
                                   PortableCourse3DProperty *out)
{
    const PortableCourse3DTable *table = portable_course_3d_table(view, TAG("PROP"));
    const PortableCourse3DTable *strings = portable_course_3d_table(view, TAG("STRS"));
    if (!out || !table || !table->bytes || table->stride != 32u || index >= table->count ||
        !strings || !strings->bytes || strings->stride != 1u)
        return 0;
    const uint8_t *row = table->bytes + (size_t)index * table->stride;
    const char *key = string_at(view, word(row));
    const uint32_t type = word(row + 4u);
    if (!key || !strchr(key, ':') || type > 7u || word(row + 24u) || word(row + 28u))
        return 0;
    const unsigned used = type == 4u ? 2u : type == 7u ? 3u : 1u;
    if (!zeros(row + 8u + used * 4u, 4u - used))
        return 0;
    PortableCourse3DProperty candidate = {.key = key, .type = (PortableCourse3DPropertyType)type};
    switch (candidate.type)
    {
    case PORTABLE_COURSE_3D_PROPERTY_INTEGER:
        candidate.value.integer = signed_word(row + 8u);
        break;
    case PORTABLE_COURSE_3D_PROPERTY_FIXED:
        candidate.value.fixed_q16 = signed_word(row + 8u);
        break;
    case PORTABLE_COURSE_3D_PROPERTY_BOOLEAN:
        candidate.value.boolean = word(row + 8u);
        if (candidate.value.boolean > 1u)
            return 0;
        break;
    case PORTABLE_COURSE_3D_PROPERTY_STRING:
        candidate.value.string = string_at(view, word(row + 8u));
        if (!candidate.value.string)
            return 0;
        break;
    case PORTABLE_COURSE_3D_PROPERTY_ASSET:
        candidate.value.asset.kind = word(row + 8u);
        candidate.value.asset.id = word(row + 12u);
        if (!view->pack ||
            !reference(view, candidate.value.asset.kind, candidate.value.asset.id, 0))
            return 0;
        break;
    case PORTABLE_COURSE_3D_PROPERTY_OBJECT:
        candidate.value.object = word(row + 8u);
        if (!index_valid(candidate.value.object, count_at(view, "OBJS"), 0))
            return 0;
        break;
    case PORTABLE_COURSE_3D_PROPERTY_ROUTE:
        candidate.value.route = word(row + 8u);
        if (!index_valid(candidate.value.route, count_at(view, "RTHD"), 0))
            return 0;
        break;
    case PORTABLE_COURSE_3D_PROPERTY_VECTOR:
        for (unsigned axis = 0; axis < 3u; ++axis)
            candidate.value.vector_q16[axis] = signed_word(row + 8u + axis * 4u);
        break;
    }
    *out = candidate;
    return 1;
}

int portable_course_3d_property_find(const PortableCourse3DView *view, uint32_t first,
                                     uint32_t count, const char *key, PortableCourse3DProperty *out)
{
    if (!view || !key || !out || !span(first, count, count_at(view, "PROP")))
        return 0;
    PortableCourse3DProperty candidate = {0};
    int found = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        PortableCourse3DProperty row;
        if (!portable_course_3d_property_at(view, first + i, &row))
            return 0;
        if (!strcmp(key, row.key))
        {
            candidate = row;
            found = 1;
        }
    }
    if (found)
        *out = candidate;
    return found;
}

static int validate_properties(const PortableCourse3DView *v,char *error,size_t capacity)
{
    for(uint32_t i=0;i<count_at(v,"PROP");++i){const uint8_t *p=row_at(v,"PROP",i);
        const char *key=string_at(v,word(p));const uint32_t type=word(p+4u),flags=word(p+24u);
        if(!key || !strchr(key,':') || type>7u || flags || word(p+28u))
            return failure(error,capacity,v->id,"PROP",i,"invalid key/type/reserved or unsupported required property");
        unsigned used=1;
        if(type==4u)used=2;else if(type==7u)used=3;
        if(!zeros(p+8u+used*4u,4u-used) || (type==2u && word(p+8u)>1u) ||
           (type==3u && !string_at(v,word(p+8u))) ||
           (type==4u && !reference(v,word(p+8u),word(p+12u),0)) ||
           (type==5u && !index_valid(word(p+8u),count_at(v,"OBJS"),0)) ||
           (type==6u && !index_valid(word(p+8u),count_at(v,"RTHD"),0)))
            return failure(error,capacity,v->id,"PROP",i,"invalid value or reference");
    }
    return 1;
}

static int triangle_valid(const PortableCourse3DView *v,const uint8_t *p,const char *vertices)
{
    const uint8_t *a=row_at(v,vertices,word(p)),*b=row_at(v,vertices,word(p+4u)),*c=row_at(v,vertices,word(p+8u));
    if(!a || !b || !c)return 0;
    int64_t x[3],y[3];
    for(unsigned k=0;k<3;++k){x[k]=(int64_t)signed_word(b+k*4u)-signed_word(a+k*4u);y[k]=(int64_t)signed_word(c+k*4u)-signed_word(a+k*4u);}
    return x[1]*y[2]-x[2]*y[1] || x[2]*y[0]-x[0]*y[2] || x[0]*y[1]-x[1]*y[0];
}
static int validate_geometry(const PortableCourse3DView *v,char *error,size_t capacity)
{
    if(count_at(v,"VERT")<3u || count_at(v,"COLV")<3u)return failure(error,capacity,v->id,"VERT",0,"mesh needs three vertices");
    for(unsigned pass=0;pass<2;++pass){const char *name=pass?"COLV":"VERT";
        for(uint32_t i=0;i<count_at(v,name);++i){const uint8_t *p=row_at(v,name,i);
            if(!position(p,3u) || (!pass && !unit(p+12u,3u)))return failure(error,capacity,v->id,name,i,"invalid position or unit normal");}}
    const char *optional[2]={"VCOL","UV2S"};
    for(unsigned i=0;i<2;++i){const PortableCourse3DTable *t=portable_course_3d_table(v,TAG(optional[i]));
        if(t && t->count!=count_at(v,"VERT"))return failure(error,capacity,v->id,optional[i],0,"vertex attribute count mismatch");}
    for(uint32_t i=0;i<count_at(v,"TRIS");++i){const uint8_t *p=row_at(v,"TRIS",i);
        if(!triangle_valid(v,p,"VERT") || word(p+12u)>=count_at(v,"MATS"))return failure(error,capacity,v->id,"TRIS",i,"degenerate triangle or invalid index");}
    for(uint32_t i=0;i<count_at(v,"COLT");++i){const uint8_t *p=row_at(v,"COLT",i);
        if(!triangle_valid(v,p,"COLV") || word(p+12u)>=count_at(v,"SURF") || word(p+16u) ||
           !index_valid(word(p+20u),count_at(v,"RESP"),1) || !index_valid(word(p+24u),count_at(v,"ZONE"),1) ||
           !index_valid(word(p+28u),count_at(v,"OBJS"),1))return failure(error,capacity,v->id,"COLT",i,"invalid collision triangle/reference/flags");}
    uint32_t texture_ids[128];unsigned texture_count=0;
    for(uint32_t i=0;i<count_at(v,"MATS");++i){const uint8_t *p=row_at(v,"MATS",i);
        for(unsigned j=0;j<4;++j){const uint32_t id=word(p+j*4u);if(!texture_valid(v,id))return failure(error,capacity,v->id,"MATS",i,"invalid texture");
            if(id!=ABSENT){unsigned found=0;while(found<texture_count && texture_ids[found]!=id)++found;
                if(found==texture_count){if(texture_count==128u)return failure(error,capacity,v->id,"MATS",i,"texture cap exceeded");texture_ids[texture_count++]=id;}}}
        for(unsigned j=6;j<9;++j)if(signed_word(p+j*4u)<0 || word(p+j*4u)>65536u)return failure(error,capacity,v->id,"MATS",i,"ratio outside 0..1");
        const uint32_t flags=word(p+36u);
        if((flags&~63u) || (flags&48u)==48u || !properties(v,word(p+40u),word(p+44u)) ||
           !presence_valid(v,word(p+40u),word(p+44u)))return failure(error,capacity,v->id,"MATS",i,"invalid flags/properties");}
    for(uint32_t i=0;i<count_at(v,"TXAN");++i){const uint8_t *p=row_at(v,"TXAN",i);
        const uint32_t kind=word(p+4u),frames=word(p+8u),first=word(p+12u),n=word(p+16u);
        if(word(p)>=count_at(v,"MATS") || kind>1u || !frames || frames>65535u || !n || !zeros(p+20u,3u) ||
           !span(first,n,count_at(v,kind?"TXPT":"TXSR")) || (!kind && n!=1u && n!=frames))
            return failure(error,capacity,v->id,"TXAN",i,"invalid material/kind/frames/span");
        for(uint32_t j=0;j<i;++j){const uint8_t *q=row_at(v,"TXAN",j);
            if(word(q)==word(p) && word(q+4u)==kind)return failure(error,capacity,v->id,"TXAN",i,"duplicate material animation");}
        for(uint32_t k=0;k<n;++k){const uint8_t *row=row_at(v,kind?"TXPT":"TXSR",first+k);
            if(!kind){for(unsigned j=0;j<6u;++j){const int32_t value=signed_word(row+j*4u);
                    if(value < -(INT32_C(256)<<16) || value>(INT32_C(256)<<16))
                        return failure(error,capacity,v->id,"TXSR",first+k,"texture matrix outside +-256");}
                continue;}
            const uint32_t frame=word(row),id=word(row+4u);
            if(frame>=frames || (k?frame<=word(row-8):frame!=0u) || !texture_valid(v,id) || id==ABSENT)
                return failure(error,capacity,v->id,"TXPT",first+k,"pattern keys must ascend from frame 0 to valid textures");
            unsigned found=0;while(found<texture_count && texture_ids[found]!=id)++found;
            if(found==texture_count){if(texture_count==128u)return failure(error,capacity,v->id,"TXPT",first+k,"texture cap exceeded");texture_ids[texture_count++]=id;}}
    }
    for(uint32_t i=0;i<count_at(v,"SURF");++i){const uint8_t *p=row_at(v,"SURF",i);
        if(word(p)>9u || word(p+4u) || !properties(v,word(p+40u),word(p+44u)) || !presence_valid(v,word(p+40u),word(p+44u)))
            return failure(error,capacity,v->id,"SURF",i,"invalid surface type/flags/properties");
        for(unsigned j=2;j<=8;++j)if(signed_word(p+j*4u)<0 || (j==5u && word(p+j*4u)>65536u))return failure(error,capacity,v->id,"SURF",i,"invalid physical coefficient");
        PortableCourse3DSurface decoded;
        if(!portable_course_3d_surface(v,i,&decoded))
            return failure(error,capacity,v->id,"SURF",i,"invalid jump-max-distance property/applicability");}
    return 1;
}

static int physics_profiles(const PortableCourse3DView *view, uint32_t ids[3], char *error,
                            size_t capacity)
{
    PortableCourse3DMeta meta;
    if (!portable_course_3d_meta(view, &meta))
        return failure(error, capacity, view ? view->id : 0u, "META", 0,
                       "missing physics metadata");
    const char *keys[3] = {"mksc:physics-50cc", "mksc:physics-100cc", "mksc:physics-150cc"};
    for (unsigned c = 0; c < 3u; ++c)
        ids[c] = meta.physics_profile_id;
    if (meta.property_count > 16384u || meta.property_first > UINT32_MAX - meta.property_count)
        return failure(error, capacity, view->id, "META", 0, "invalid physics property span");
    for (uint32_t i = 0; i < meta.property_count; ++i)
    {
        PortableCourse3DProperty property;
        if (!portable_course_3d_property_at(view, meta.property_first + i, &property))
            return failure(error, capacity, view->id, "META", i, "invalid physics property");
        for (unsigned c = 0; c < 3u; ++c)
            if (!strcmp(property.key, keys[c]))
            {
                if (property.type != PORTABLE_COURSE_3D_PROPERTY_ASSET ||
                    property.value.asset.kind != PORTABLE_COURSE_3D_PROFILE_KIND ||
                    !property.value.asset.id || property.value.asset.id == UINT32_MAX)
                    return failure(error, capacity, view->id, "META", c,
                                   "invalid class physics asset");
                PortableMeshKartRules rules;
                if (!portable_mesh_kart_rules_asset_load(view->pack, property.value.asset.id,
                                                         &rules))
                    return failure(error, capacity, view->id, "META", c,
                                   "missing or invalid class physics profile");
                ids[c] = property.value.asset.id;
            }
    }
    if (meta.physics_profile_id)
    {
        PortableMeshKartRules rules;
        if (!portable_mesh_kart_rules_asset_load(view->pack, meta.physics_profile_id, &rules))
            return failure(error, capacity, view->id, "META", 0,
                           "missing or invalid physics profile");
    }
    return 1;
}
static int item_rules_key(const PortableCourse3DView *view,const char *key,uint32_t *out,char *error,size_t capacity);
int portable_course_3d_item_rules(const PortableCourse3DView *view,uint32_t *out,char *error,size_t capacity)
{
    return item_rules_key(view,"mksc:item-rules",out,error,capacity);
}
int portable_course_3d_item_rules_mode(const PortableCourse3DView *view,uint32_t mode,uint32_t *out,
                                       char *error,size_t capacity)
{
    uint32_t base=0,versus=0;
    if(!item_rules_key(view,"mksc:item-rules",&base,error,capacity))return 0;
    if(base && (mode==4u || mode==8u) && !item_rules_key(view,"mksc:item-rules-vs",&versus,error,capacity))
        return 0;
    *out=versus?versus:base;
    return 1;
}
static int item_rules_key(const PortableCourse3DView *view,const char *key,uint32_t *out,char *error,size_t capacity)
{
    PortableCourse3DMeta meta;
    if(!view || !out || !portable_course_3d_meta(view,&meta))
        return failure(error,capacity,view?view->id:0u,"META",0,"invalid item rule metadata");
    uint32_t id=0,profiles[4];
    if(!physics_profiles(view,profiles,error,capacity))return 0;
    /* TT always uses META.base even when every engine class overrides it. */
    profiles[3]=meta.physics_profile_id;
    for(uint32_t i=0;i<meta.property_count;++i) {
        PortableCourse3DProperty property;
        if(!portable_course_3d_property_at(view,meta.property_first+i,&property))
            return failure(error,capacity,view->id,"META",i,"invalid item rule property");
        if(strcmp(property.key,key))continue;
        if(property.type!=PORTABLE_COURSE_3D_PROPERTY_ASSET ||
           property.value.asset.kind!=PORTABLE_MESH_ITEM_RULES_KIND ||
           !property.value.asset.id || property.value.asset.id==UINT32_MAX)
            return failure(error,capacity,view->id,"META",i,"invalid item rules asset");
        PortableMeshItemRules items;
        if(!portable_mesh_item_rules_asset_load(view->pack,property.value.asset.id,&items))
            return failure(error,capacity,view->id,"META",i,"missing or invalid MIR1 item rules");
        for(unsigned c=0;c<4u;++c)if(profiles[c]) {
            PortableMeshKartRules kart;
            if(!portable_mesh_kart_rules_asset_load(view->pack,profiles[c],&kart) ||
               !portable_mesh_item_rules_fit_profile(&items,&kart))
                return failure(error,capacity,view->id,"META",i,"item boost exceeds class physics speed bound");
        }
        id=property.value.asset.id;
    }
    *out=id;
    if(error&&capacity)error[0]=0;
    return 1;
}
int portable_course_3d_physics_profile(const PortableCourse3DView *view, unsigned engine_class,
                                       uint32_t *out, char *error, size_t capacity)
{
    uint32_t ids[3];
    if (!view || !out || engine_class > 2u || !physics_profiles(view, ids, error, capacity))
        return 0;
    PortableCourse3DMeta meta;
    if (!portable_course_3d_meta(view, &meta) || !meta.physics_profile_id)
        return failure(error, capacity, view->id, "META", engine_class,
                       "preview-only physics profile");
    *out = ids[engine_class];
    if (error && capacity)
        error[0] = 0;
    return 1;
}
int portable_course_3d_has_physics_profile(const PortableCourse3DView *view, uint32_t id)
{
    PortableCourse3DMeta meta;
    if (!view || !id || !portable_course_3d_meta(view, &meta) || !meta.physics_profile_id)
        return 0;
    if (meta.physics_profile_id == id)
        return 1;
    uint32_t ids[3];
    return physics_profiles(view, ids, NULL, 0) && (id == ids[0] || id == ids[1] || id == ids[2]);
}

static int validate_metadata(const PortableCourse3DView *v,char *error,size_t capacity)
{
    const uint8_t *p=row_at(v,"META",0);
    if(!string_at(v,word(p)) || !string_at(v,word(p+4u)) || !string_at(v,word(p+8u)) ||
       !word(p+12u) || (word(p+12u)&~31u) || word(p+16u)>99u ||
       ((word(p+12u)!=16u) && (!word(p+16u) || !count_at(v,"CPNT"))) ||
       signed_word(p+56u)<0 || signed_word(p+60u)<=signed_word(p+56u) ||
       !unit(p+68u,3u) || signed_word(p+84u)<0 || word(p+92u) ||
       !properties(v,word(p+96u),word(p+100u)) || !zeros(p+104u,6u))
        return failure(error,capacity,v->id,"META",0,"invalid metadata/units/flags/properties");
    const uint32_t music_kind=word(p+28u),music_id=word(p+32u);
    if((music_kind==ABSENT)!=(music_id==ABSENT) ||
       (music_kind!=ABSENT && !reference(v,music_kind,music_id,0)))
        return failure(error,capacity,v->id,"META",0,"missing music reference");
    for(unsigned i=9u;i<=11u;++i)if(!texture_valid(v,word(p+i*4u)))
        return failure(error,capacity,v->id,"META",0,"missing preview/minimap/sky texture");
    PortableCourse3DMinimap minimap;
    if(word(p+40u)!=ABSENT && !portable_course_3d_minimap(v,&minimap))
        return failure(error,capacity,v->id,"META",0,"minimap needs a true-colour image and mksc:minimap-low < mksc:minimap-high");
    uint32_t profiles[3],items;
    PortableCourse3DChase chase;
    return physics_profiles(v,profiles,error,capacity) && portable_course_3d_item_rules(v,&items,error,capacity) &&
           portable_course_3d_chase(v,&chase,error,capacity);
}

static int validate_placements(const PortableCourse3DView *v,char *error,size_t capacity)
{
    for(uint32_t i=0;i<count_at(v,"GRID");++i){const uint8_t *p=row_at(v,"GRID",i);
        if(!position(p,3) || !unit(p+12u,4) || word(p+28u))return failure(error,capacity,v->id,"GRID",i,"invalid spawn transform/flags");}
    for(uint32_t i=0;i<count_at(v,"ITEM");++i){const uint8_t *p=row_at(v,"ITEM",i);
        if(!position(p,3) || signed_word(p+12u)<=0 || word(p+16u)>65535u || !word(p+20u) ||
           !properties(v,word(p+24u),word(p+28u)))return failure(error,capacity,v->id,"ITEM",i,"invalid item placement");}
    for(uint32_t i=0;i<count_at(v,"OBJS");++i){const uint8_t *p=row_at(v,"OBJS",i);
        if(!reference(v,word(p+4u),word(p+8u),0) || !position(p+12u,3) || !unit(p+24u,4) ||
           !signed_word(p+40u) || !signed_word(p+44u) || !signed_word(p+48u) ||
           !index_valid(word(p+52u),count_at(v,"RTHD"),1) || !properties(v,word(p+56u),word(p+60u)))
            return failure(error,capacity,v->id,"OBJS",i,"invalid object transform/reference");}
    for(uint32_t i=0;i<count_at(v,"RESP");++i){const uint8_t *p=row_at(v,"RESP",i);
        if(!position(p,3) || !unit(p+12u,4) || !index_valid(word(p+28u),count_at(v,"CPNT"),word(row_at(v,"META",0)+12u)==16u) ||
           signed_word(p+32u)<=0 || word(p+36u) || !index_valid(word(p+40u),count_at(v,"RTHD"),1) || word(p+44u))
            return failure(error,capacity,v->id,"RESP",i,"invalid respawn transform/reference/flags");}
    for(uint32_t i=0;i<count_at(v,"CAMR");++i){const uint8_t *p=row_at(v,"CAMR",i);
        if(!position(p,3) || !unit(p+12u,4) || !word(p+28u) || word(p+28u)>=32768u ||
           signed_word(p+32u)<=0 || signed_word(p+36u)<=signed_word(p+32u) ||
           !index_valid(word(p+40u),count_at(v,"RTHD"),1) || !index_valid(word(p+44u),count_at(v,"OBJS"),1) ||
           word(p+48u) || !properties(v,word(p+56u),word(p+60u)))
            return failure(error,capacity,v->id,"CAMR",i,"invalid camera transform/frustum/reference");}
    uint8_t intro_seen[1024]={0};uint32_t intro_count=0;
    for(uint32_t i=0;i<count_at(v,"CAMR");++i){const uint8_t *p=row_at(v,"CAMR",i);uint32_t order=ABSENT;
        for(uint32_t k=0;k<word(p+60u);++k){PortableCourse3DProperty property;
            if(portable_course_3d_property_at(v,word(p+56u)+k,&property) && !strcmp(property.key,"mksc:intro-order"))
                order=property.type==PORTABLE_COURSE_3D_PROPERTY_INTEGER && property.value.integer>=0?(uint32_t)property.value.integer:ABSENT-1u;}
        if(order==ABSENT)continue;
        PortableCourse3DIntroShot shot;
        if(order>=1024u || intro_seen[order]++ || !portable_course_3d_intro_shot(v,order,&shot))
            return failure(error,capacity,v->id,"CAMR",i,"intro shot needs a unique mksc:intro-order, mksc:camera-ticks, mksc:camera-fov-end, mksc:camera-target-begin/end and a kind-2 route");
        ++intro_count;}
    for(uint32_t order=0;order<intro_count;++order)if(!intro_seen[order])
        return failure(error,capacity,v->id,"CAMR",order,"intro shot orders must run 0..count-1");
    for(uint32_t i=0;i<count_at(v,"ZONE");++i){const uint8_t *p=row_at(v,"ZONE",i);
        if(!position(p,3) || signed_word(p+12u)<=0 || signed_word(p+16u)<=0 || signed_word(p+20u)<=0 ||
           !unit(p+24u,4) || word(p+56u) || !properties(v,word(p+68u),word(p+72u)) || word(p+76u))
            return failure(error,capacity,v->id,"ZONE",i,"invalid zone transform/properties/flags");
        PortableCourse3DGravityPoint point;
        if(word(p+52u)==1u && !portable_course_3d_gravity_point(v,i,&point))
            return failure(error,capacity,v->id,"ZONE",i,"gravity-path point needs gravity, a kind-1 mksc:gravity-next and boolean mksc:anti-gravity");
        if(word(p+52u)==2u && !portable_course_3d_gravity_point(v,i,&point))
            return failure(error,capacity,v->id,"ZONE",i,"glide-path point needs gravity, a kind-2 mksc:glide-next and boolean mksc:glide-cannon");
        PortableCourse3DSoundZone sound;
        if(word(p+52u)==3u && !portable_course_3d_sound_zone(v,i,&sound))
            return failure(error,capacity,v->id,"ZONE",i,"sound emitter needs an integer mksc:sound 1..65535 and mksc:sound-period 0..65535");
        PortableCourse3DEmitterZone emitter;
        if(word(p+52u)==4u && !portable_course_3d_emitter_zone(v,i,&emitter))
            return failure(error,capacity,v->id,"ZONE",i,"particle emitter needs a positive integer mksc:emitter");}
    return 1;
}

/* Iterative traversal avoids content-controlled recursion. Scratch scales with
 * the bounded graph, never the render/collision vertex buffers. */
typedef struct { uint32_t nodes[16384], next[16384]; uint8_t color[16384]; } GraphScratch;
static int validate_partition(const PortableCourse3DView *v,const char *owners,
    unsigned first_word,unsigned count_word,const char *items,char *error,size_t capacity)
{
    const uint32_t total=count_at(v,items);
    /* All graph tables are capped at 16384 rows. Marking each owned row once
     * bounds validation work even when an attacker repeats a large span. */
    uint8_t *owned=calloc(total?total:1u,1u);
    if(!owned)return failure(error,capacity,v->id,owners,0,"cannot allocate bounded ownership scratch");
    uint32_t covered=0u;
    for(uint32_t owner=0u;owner<count_at(v,owners);++owner){
        const uint8_t *p=row_at(v,owners,owner);
        const uint32_t first=word(p+first_word*4u),count=word(p+count_word*4u);
        if(!span(first,count,total)){
            free(owned);return failure(error,capacity,v->id,owners,owner,"ownership span outside graph table");}
        for(uint32_t i=0u;i<count;++i){
            if(owned[first+i]){
                free(owned);return failure(error,capacity,v->id,owners,owner,"overlapping ownership spans");}
            owned[first+i]=1u;++covered;
        }
    }
    free(owned);
    if(covered!=total)return failure(error,capacity,v->id,items,0,"unowned graph rows");
    return 1;
}
static int validate_graphs(const PortableCourse3DView *v,char *error,size_t capacity)
{
    const uint32_t gates=count_at(v,"CPNT"),gate_edges=count_at(v,"CPED");
    for(uint32_t i=0;i<gates;++i){const uint8_t *p=row_at(v,"CPNT",i);
        if(!position(p,3) || !unit(p+12u,3) || !unit(p+24u,3) || !perpendicular(p+12u,p+24u) ||
           signed_word(p+36u)<=0 || signed_word(p+40u)<=0 ||
           !span(word(p+52u),word(p+56u),gate_edges) || (word(p+60u)&~7u))
            return failure(error,capacity,v->id,"CPNT",i,"invalid gate/edge span/flags");}
    for(uint32_t i=0;i<gate_edges;++i)if(word(row_at(v,"CPED",i))>=gates)
        return failure(error,capacity,v->id,"CPED",i,"invalid successor gate");
    for(uint32_t i=0;i<count_at(v,"RTND");++i){const uint8_t *p=row_at(v,"RTND",i);
        if(!position(p,3) || signed_word(p+12u)<=0 || signed_word(p+16u)<0 ||
           !span(word(p+20u),word(p+24u),count_at(v,"RTED")) || word(p+28u))
            return failure(error,capacity,v->id,"RTND",i,"invalid route node/edge span/flags");}
    for(uint32_t i=0;i<count_at(v,"RTED");++i)if(word(row_at(v,"RTED",i))>=count_at(v,"RTND"))
        return failure(error,capacity,v->id,"RTED",i,"invalid successor node");
    if(!validate_partition(v,"RTHD",1u,2u,"RTND",error,capacity) ||
       !validate_partition(v,"RTND",5u,6u,"RTED",error,capacity) ||
       !validate_partition(v,"CPNT",13u,14u,"CPED",error,capacity))return 0;
    GraphScratch *scratch=malloc(sizeof *scratch);
    if(!scratch)return failure(error,capacity,v->id,"graphs",0,"cannot allocate bounded graph scratch");
    memset(scratch->color,0,gates);uint32_t head=0,tail=gates?1u:0u;
    if(gates){scratch->nodes[0]=0;scratch->color[0]=1;}
    while(head<tail){const uint8_t *p=row_at(v,"CPNT",scratch->nodes[head++]);
        for(uint32_t i=0;i<word(p+56u);++i){const uint32_t to=word(row_at(v,"CPED",word(p+52u)+i));
            if(!scratch->color[to]){scratch->color[to]=1;scratch->nodes[tail++]=to;}}}
    if(tail!=gates){free(scratch);return failure(error,capacity,v->id,"CPNT",0,"unreachable checkpoint gate");}
    for(uint32_t route=0;route<count_at(v,"RTHD");++route){const uint8_t *p=row_at(v,"RTHD",route);
        const uint32_t first=word(p+4u),count=word(p+8u),start=word(p+12u),flags=word(p+16u);
        if(word(p)>2u || !count || !span(first,count,count_at(v,"RTND")) || start<first || start-first>=count ||
           (flags&~1u) || !properties(v,word(p+20u),word(p+24u)) || word(p+28u)){
            free(scratch);return failure(error,capacity,v->id,"RTHD",route,"invalid route kind/span/start/flags");}
        memset(scratch->color,0,count);uint32_t depth=1,visited=1;
        scratch->nodes[0]=start;scratch->next[0]=0;scratch->color[start-first]=1;
        while(depth){const uint32_t top=depth-1u,node=scratch->nodes[top];const uint8_t *n=row_at(v,"RTND",node);
            if(scratch->next[top]>=word(n+24u)){scratch->color[node-first]=2;--depth;continue;}
            const uint32_t edge=word(n+20u)+scratch->next[top]++;
            const uint32_t to=word(row_at(v,"RTED",edge));
            if(to<first || to-first>=count || (!(flags&1u) && scratch->color[to-first]==1)){
                free(scratch);return failure(error,capacity,v->id,"RTED",edge,"successor leaves route or undeclared cycle");}
            if(!scratch->color[to-first]){scratch->color[to-first]=1;++visited;
                scratch->nodes[depth]=to;scratch->next[depth++]=0;}
        }
        if(visited!=count){free(scratch);return failure(error,capacity,v->id,"RTHD",route,"unreachable route node");}
    }
    free(scratch);return 1;
}

static int catalog_section(const PortableAssetSectionView *s,PortableCourse3DCatalog *out,char *error,size_t capacity)
{
    if(s->version!=1u || !s->bytes || s->size!=40u || word(s->bytes)!=TAG("C3C1") ||
       word(s->bytes+4u)!=1u || word(s->bytes+8u)!=1u || word(s->bytes+12u)!=24u ||
       word(s->bytes+16u)!=s->content_id || !word(s->bytes+28u) || (word(s->bytes+28u)&~31u) || word(s->bytes+36u))
        return failure(error,capacity,s->content_id,"C3C1",0,"invalid catalog row");
    *out=(PortableCourse3DCatalog){word(s->bytes+16u),word(s->bytes+20u),word(s->bytes+24u),word(s->bytes+28u),word(s->bytes+32u)};
    return 1;
}
int portable_course_3d_open(const PortableAssetPackView *pack,uint32_t id,PortableCourse3DView *out,char *error,size_t capacity)
{
    if(out)memset(out,0,sizeof *out);
    if(error && capacity)error[0]='\0';
    PortableAssetSectionView section,catalog;PortableCourse3DView next={.pack=pack};PortableCourse3DCatalog entry;
    if(!pack || !out || portable_asset_pack_find_section(pack,99u,id,&section)!=PORTABLE_ASSET_PACK_OK)
        return failure(error,capacity,id,"header",0,"missing course or invalid arguments");
    if(!directory(&section,&next,error,capacity) || !validate_properties(&next,error,capacity) ||
       !validate_metadata(&next,error,capacity) || !validate_geometry(&next,error,capacity) ||
       !validate_placements(&next,error,capacity) || !validate_graphs(&next,error,capacity))return 0;
    if(portable_asset_pack_find_section(pack,100u,id,&catalog)!=PORTABLE_ASSET_PACK_OK ||
       !catalog_section(&catalog,&entry,error,capacity))return failure(error,capacity,id,"C3C1",0,"missing or malformed catalog row");
    const uint8_t *meta=row_at(&next,"META",0);
    if(entry.cup_id!=word(meta+20u) || entry.slot!=word(meta+24u) || entry.modes!=word(meta+12u))
        return failure(error,capacity,id,"C3C1",0,"catalog disagrees with META");
    *out=next;return 1;
}
size_t portable_course_3d_catalog_count(const PortableAssetPackView *pack)
{
    size_t count=0;if(!pack)return 0;
    for(uint32_t i=0;i<pack->section_count;++i){PortableAssetSectionView s;
        if(portable_asset_pack_section_at(pack,i,&s)==PORTABLE_ASSET_PACK_OK && s.kind==100u)++count;}
    return count;
}
int portable_course_3d_catalog_at(const PortableAssetPackView *pack,size_t index,PortableCourse3DCatalog *out,char *error,size_t capacity)
{
    if(out)memset(out,0,sizeof *out);
    if(!pack || !out)return failure(error,capacity,0,"C3C1",0,"invalid arguments");
    for(uint32_t i=0;i<pack->section_count;++i){PortableAssetSectionView s;
        if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)return failure(error,capacity,0,"C3C1",i,"invalid pack section");
        if(s.kind==100u && index--==0u)return catalog_section(&s,out,error,capacity);}
    return failure(error,capacity,0,"C3C1",0,"catalog index out of range");
}
int portable_course_3d_validate_pack(const PortableAssetPackView *pack,char *error,size_t capacity)
{
    if(error && capacity)error[0]='\0';
    if(!pack)return failure(error,capacity,0,"pack",0,"invalid arguments");
    unsigned item_profiles=0;
    for(uint32_t i=0;i<pack->section_count;++i){PortableAssetSectionView s;
        if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)return failure(error,capacity,0,"pack",i,"invalid section");
        if(s.kind==99u){PortableCourse3DView view;if(!portable_course_3d_open(pack,s.content_id,&view,error,capacity))return 0;}
        else if(s.kind==100u){PortableCourse3DCatalog a;PortableAssetSectionView course;
            if(!catalog_section(&s,&a,error,capacity))return 0;
            if(portable_asset_pack_find_section(pack,99u,a.course_id,&course)!=PORTABLE_ASSET_PACK_OK)return failure(error,capacity,a.course_id,"C3C1",0,"missing course");
            for(uint32_t j=0;j<i;++j){PortableAssetSectionView prior;PortableCourse3DCatalog b;
                if(portable_asset_pack_section_at(pack,j,&prior)!=PORTABLE_ASSET_PACK_OK)return 0;
                if(prior.kind==100u && catalog_section(&prior,&b,NULL,0) && a.cup_id==b.cup_id && a.slot==b.slot)
                    return failure(error,capacity,a.course_id,"C3C1",0,"ambiguous cup/slot");}}
        else if(s.kind==PORTABLE_MESH_ITEM_RULES_KIND){PortableMeshItemRules rules;
            if(++item_profiles>PORTABLE_MESH_ITEM_MAX_PROFILES || !portable_mesh_item_rules_asset_open(&s,&rules))
                return failure(error,capacity,0,"MIR1",s.content_id,"invalid or excessive item profiles");}
        else if(s.kind==102u){PortableMeshKartProfile profile;
            if(!portable_mesh_kart_profile_asset_open(&s,&profile))return failure(error,capacity,0,"KPH2",s.content_id,"invalid physics profile");}
        else if(s.kind==101u){PortableCourse3DView view={.pack=pack};PortableNativeImage image;
            if(!portable_course_3d_texture(&view,s.content_id,&image))return failure(error,capacity,0,"texture",s.content_id,"invalid kind101 image");}
        else if(s.kind==PORTABLE_COURSE_3D_TEXTURE_BUNDLE_KIND){uint32_t count;PortableNativeImage picture;
            if(!bundle_header(&s,&count))return failure(error,capacity,0,"TXB1",s.content_id,"invalid texture bundle header");
            for(uint32_t j=0;j<count;++j)
                if((j && word(s.bytes+16u+j*16u)<=word(s.bytes+j*16u)) || !bundle_entry(&s,count,j,&picture))
                    return failure(error,capacity,0,"TXB1",s.content_id,"bundle entries must ascend and hold valid images");}
    }
    return 1;
}

int portable_course_3d_meta(const PortableCourse3DView *v,PortableCourse3DMeta *out)
{
    const uint8_t *p=row_at(v,"META",0);if(!p || !out)return 0;
    *out=(PortableCourse3DMeta){.name=string_at(v,word(p)),.author=string_at(v,word(p+4u)),.cup_name=string_at(v,word(p+8u)),
        .modes=word(p+12u),.laps=word(p+16u),.cup_id=word(p+20u),.cup_order=word(p+24u),
        .music_kind=word(p+28u),.music_id=word(p+32u),.preview_id=word(p+36u),.minimap_id=word(p+40u),.sky_id=word(p+44u),
        .sky_rgba=word(p+48u),.fog_rgba=word(p+52u),.fog_near_q16=signed_word(p+56u),.fog_far_q16=signed_word(p+60u),
        .ambient_rgba=word(p+64u),.sun_rgba=word(p+80u),.gravity_q16=signed_word(p+84u),.physics_profile_id=word(p+88u),
        .feature_bits=word(p+92u),.property_first=word(p+96u),.property_count=word(p+100u)};
    for(unsigned i=0;i<3;++i)out->sun_q30[i]=signed_word(p+68u+i*4u);
    return 1;
}
int portable_course_3d_vertex(const PortableCourse3DView *v,uint32_t index,PortableCourse3DVertex *out)
{
    const uint8_t *p=row_at(v,"VERT",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3;++i){out->position_q16[i]=signed_word(p+i*4u);out->normal_q30[i]=signed_word(p+12u+i*4u);}
    for(unsigned i=0;i<2;++i)out->uv_q16[i]=signed_word(p+24u+i*4u);
    return 1;
}
int portable_course_3d_triangle(const PortableCourse3DView *v,uint32_t index,PortableCourse3DTriangle *out)
{
    const uint8_t *p=row_at(v,"TRIS",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3;++i)out->vertices[i]=word(p+i*4u);
    out->material=word(p+12u);return 1;
}
int portable_course_3d_material(const PortableCourse3DView *v,uint32_t index,PortableCourse3DMaterial *out)
{
    const uint8_t *p=row_at(v,"MATS",index);if(!p || !out)return 0;
    for(unsigned i=0;i<4;++i)out->textures[i]=word(p+i*4u);
    out->base_rgba=word(p+16u);out->emissive_rgba=word(p+20u);
    out->metallic_q16=signed_word(p+24u);out->roughness_q16=signed_word(p+28u);out->alpha_cutoff_q16=signed_word(p+32u);
    out->flags=word(p+36u);out->property_first=word(p+40u);out->property_count=word(p+44u);return 1;
}
int portable_course_3d_grid(const PortableCourse3DView *v,uint32_t index,PortableCourse3DGrid *out)
{
    const uint8_t *p=row_at(v,"GRID",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3;++i)out->position_q16[i]=signed_word(p+i*4u);
    for(unsigned i=0;i<4;++i)out->quaternion_q30[i]=signed_word(p+12u+i*4u);
    return 1;
}

int portable_course_3d_collision_vertex(const PortableCourse3DView *v,uint32_t index,int32_t out[3])
{
    const uint8_t *p=row_at(v,"COLV",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3u;++i)out[i]=signed_word(p+i*4u);
    return 1;
}
int portable_course_3d_collision(const PortableCourse3DView *v,uint32_t index,PortableCourse3DCollision *out)
{
    const uint8_t *p=row_at(v,"COLT",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3u;++i)out->vertices[i]=word(p+i*4u);
    out->surface=word(p+12u);out->flags=word(p+16u);out->respawn=word(p+20u);
    out->gravity_zone=word(p+24u);out->owner_object=word(p+28u);return 1;
}
int portable_course_3d_sound_zone(const PortableCourse3DView *view, uint32_t zone, PortableCourse3DSoundZone *out)
{
    const PortableCourse3DTable *zones = portable_course_3d_table(view,PORTABLE_COURSE_3D_TAG('Z','O','N','E'));
    if (!out || !zones || zones->stride != 80u || zone >= zones->count)
        return 0;
    const uint8_t *p = zones->bytes+80u*zone;
    if (word(p+52u) != 3u)
        return 0;
    PortableCourse3DSoundZone next = {{0,0,0},0,0,0};
    for (unsigned a = 0; a < 3u; ++a)
    {
        next.position_q16[a] = signed_word(p+4u*a);
        if (signed_word(p+12u+4u*a) > next.radius_q16)
            next.radius_q16 = signed_word(p+12u+4u*a);
    }
    const uint32_t first = word(p+68u), count = word(p+72u);
    PortableCourse3DProperty property;
    if (!portable_course_3d_property_find(view,first,count,"mksc:sound",&property) ||
        property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer < 1 ||
        property.value.integer > 65535)
        return 0;
    next.song = (uint16_t)property.value.integer;
    if (portable_course_3d_property_find(view,first,count,"mksc:sound-period",&property))
    {
        if (property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer < 0 ||
            property.value.integer > 65535)
            return 0;
        next.period = (uint16_t)property.value.integer;
    }
    *out = next;
    return 1;
}
int portable_course_3d_emitter_zone(const PortableCourse3DView *view, uint32_t zone, PortableCourse3DEmitterZone *out)
{
    const PortableCourse3DTable *zones = portable_course_3d_table(view,PORTABLE_COURSE_3D_TAG('Z','O','N','E'));
    if (!out || !zones || zones->stride != 80u || zone >= zones->count)
        return 0;
    const uint8_t *p = zones->bytes+80u*zone;
    PortableCourse3DProperty property;
    if (word(p+52u) != 4u ||
        !portable_course_3d_property_find(view,word(p+68u),word(p+72u),"mksc:emitter",&property) ||
        property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer < 1)
        return 0;
    for (unsigned a = 0; a < 3u; ++a)
        out->position_q16[a] = signed_word(p+4u*a);
    for (unsigned a = 0; a < 4u; ++a)
        out->quaternion_q30[a] = signed_word(p+24u+4u*a);
    out->emitter = (uint32_t)property.value.integer;
    return 1;
}
int portable_course_3d_gravity_point(const PortableCourse3DView *view, uint32_t zone,
                                     PortableCourse3DGravityPoint *out)
{
    const PortableCourse3DTable *zones = portable_course_3d_table(view,PORTABLE_COURSE_3D_TAG('Z','O','N','E'));
    if (!out || !zones || zones->stride != 80u || zone >= zones->count)
        return 0;
    const uint8_t *p = zones->bytes+80u*zone;
    const uint32_t kind = word(p+52u);
    if (kind != 1u && kind != 2u)
        return 0;
    PortableCourse3DGravityPoint next = {.kind = kind, .next = PORTABLE_COURSE_3D_ABSENT};
    for (unsigned a = 0; a < 3u; ++a)
    {
        next.position_q16[a] = signed_word(p+4u*a);
        next.gravity_q16[a] = signed_word(p+40u+4u*a);
        if (signed_word(p+12u+4u*a) > next.radius_q16)
            next.radius_q16 = signed_word(p+12u+4u*a);
    }
    if (!next.gravity_q16[0] && !next.gravity_q16[1] && !next.gravity_q16[2])
        return 0;
    const uint32_t first = word(p+68u), count = word(p+72u);
    PortableCourse3DProperty property;
    if (portable_course_3d_property_find(view,first,count,kind == 1u ? "mksc:gravity-next" : "mksc:glide-next",
                                         &property))
    {
        if (property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer < 0 ||
            (uint32_t)property.value.integer >= zones->count || (uint32_t)property.value.integer == zone ||
            word(zones->bytes+80u*(uint32_t)property.value.integer+52u) != kind)
            return 0;
        next.next = (uint32_t)property.value.integer;
    }
    if (portable_course_3d_property_find(view,first,count,kind == 1u ? "mksc:anti-gravity" : "mksc:glide-cannon",
                                         &property))
    {
        if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
            return 0;
        next.anti_gravity = property.value.boolean != 0;
    }
    *out = next;
    return 1;
}
int portable_course_3d_surface(const PortableCourse3DView *view, uint32_t index,
                                 PortableCourse3DSurface *out)
{
    const uint8_t *p = row_at(view,"SURF",index);
    if (!p || !out)
        return 0;
    PortableCourse3DSurface next = {word(p),word(p+4u),signed_word(p+8u),signed_word(p+12u),
        signed_word(p+16u),signed_word(p+20u),signed_word(p+24u),signed_word(p+28u),signed_word(p+32u),
        word(p+36u),word(p+40u),word(p+44u),0,{0,0,0},{0,0,0},0,0,0,0,0,0,0};
    unsigned cannons = 0;
    if (next.property_count > 16384u || next.property_first > UINT32_MAX-next.property_count)
        return 0;
    for (uint32_t i = 0; i < next.property_count; ++i)
    {
        PortableCourse3DProperty property;
        if (!portable_course_3d_property_at(view,next.property_first+i,&property))
            return 0;
        if (!strcmp(property.key,"mksc:jump-max-distance"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_FIXED ||
                property.value.fixed_q16 <= 0 || property.value.fixed_q16 > 1024*65536 ||
                next.type != 5u || next.gravity_ratio_q16 != 65536)
                return 0;
            next.jump_max_distance_q16 = property.value.fixed_q16;
        }
        else if (!strcmp(property.key,"mksc:one-sided") || !strcmp(property.key,"mksc:surface-gravity"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                return 0;
            *(property.key[5] == 'o' ? &next.one_sided : &next.surface_gravity) = property.value.boolean;
        }
        else if (!strcmp(property.key,"mksc:surface-class"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer < 1 ||
                property.value.integer > 10)
                return 0;
            next.surface_class = (uint32_t)property.value.integer;
        }
        else if (!strcmp(property.key,"mksc:trick"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                return 0;
            next.trick = property.value.boolean ? 1u : 2u;
        }
        else if (!strcmp(property.key,"mksc:glide"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                return 0;
            next.glide = property.value.boolean != 0;
        }
        else if (!strcmp(property.key,"mksc:splash"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                return 0;
            next.splash = property.value.boolean != 0;
        }
        else if (!strcmp(property.key,"mksc:sound-surface"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || property.value.integer < 1 ||
                property.value.integer > 63)
                return 0;
            next.sound_surface = (uint32_t)property.value.integer;
        }
        else if (!strcmp(property.key,"mksc:cannon-target") || !strcmp(property.key,"mksc:cannon-heading"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_VECTOR || next.type != 9u)
                return 0;
            const int target = property.key[12] == 't';
            memcpy(target ? next.cannon_target_q16 : next.cannon_heading_q16, property.value.vector_q16,
                   sizeof next.cannon_target_q16);
            cannons |= target ? 1u : 2u;
        }
    }
    if (next.type == 9u && cannons != 3u)
        return 0; /* a cannon needs both its target and exit heading */
    *out = next;
    return 1;
}
int portable_course_3d_checkpoint(const PortableCourse3DView *v,uint32_t index,PortableCourse3DCheckpoint *out)
{
    const uint8_t *p=row_at(v,"CPNT",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3u;++i){out->center_q16[i]=signed_word(p+i*4u);out->normal_q30[i]=signed_word(p+12u+i*4u);out->right_q30[i]=signed_word(p+24u+i*4u);}
    out->half_width_q16=signed_word(p+36u);out->half_height_q16=signed_word(p+40u);
    out->key_group=word(p+44u);out->stage=word(p+48u);out->edge_first=word(p+52u);out->edge_count=word(p+56u);out->flags=word(p+60u);return 1;
}
int portable_course_3d_checkpoint_edge(const PortableCourse3DView *v,uint32_t index,uint32_t *out)
{ const uint8_t *p=row_at(v,"CPED",index);if(!p || !out)return 0;*out=word(p);return 1; }
int portable_course_3d_respawn(const PortableCourse3DView *v,uint32_t index,PortableCourse3DRespawn *out)
{
    const uint8_t *p=row_at(v,"RESP",index);if(!p || !out)return 0;
    for(unsigned i=0;i<3u;++i)out->position_q16[i]=signed_word(p+i*4u);
    for(unsigned i=0;i<4u;++i)out->quaternion_q30[i]=signed_word(p+12u+i*4u);
    out->checkpoint=word(p+28u);out->radius_q16=signed_word(p+32u);out->flags=word(p+36u);out->route=word(p+40u);return 1;
}

int portable_course_3d_intro_shot(const PortableCourse3DView *view, uint32_t order,
                                  PortableCourse3DIntroShot *out)
{
    for (uint32_t i = 0; out && i < count_at(view, "CAMR"); ++i)
    {
        const uint8_t *p = row_at(view, "CAMR", i);
        unsigned found = 0;
        memset(out, 0, sizeof *out);
        for (uint32_t k = 0; k < word(p + 60u); ++k)
        {
            PortableCourse3DProperty property;
            if (!portable_course_3d_property_at(view, word(p + 56u) + k, &property))
                return 0;
            const int integer = property.type == PORTABLE_COURSE_3D_PROPERTY_INTEGER;
            const int vector = property.type == PORTABLE_COURSE_3D_PROPERTY_VECTOR;
            if (integer && !strcmp(property.key, "mksc:intro-order") && property.value.integer == (int32_t)order)
                found |= 1u;
            else if (integer && !strcmp(property.key, "mksc:camera-ticks") && property.value.integer > 0 &&
                     property.value.integer <= 65535)
            {
                out->ticks = (uint32_t)property.value.integer;
                found |= 2u;
            }
            else if (integer && !strcmp(property.key, "mksc:camera-fov-end") && property.value.integer > 0 &&
                     property.value.integer < 32768)
            {
                out->fov_turns[1] = (uint32_t)property.value.integer;
                found |= 4u;
            }
            else if (vector && !strcmp(property.key, "mksc:camera-target-begin"))
            {
                memcpy(out->target_q16[0], property.value.vector_q16, sizeof out->target_q16[0]);
                found |= 8u;
            }
            else if (vector && !strcmp(property.key, "mksc:camera-target-end"))
            {
                memcpy(out->target_q16[1], property.value.vector_q16, sizeof out->target_q16[1]);
                found |= 16u;
            }
        }
        if (!(found & 1u))
            continue;
        PortableCourse3DRoute route;
        out->route = word(p + 40u);
        if (found != 31u || (out->route != ABSENT && (!portable_course_3d_route(view, out->route, &route) ||
                                                      route.kind != 2u || route.flags)))
            return 0;
        for (unsigned axis = 0; axis < 3u; ++axis)
            out->position_q16[axis] = signed_word(p + axis * 4u);
        out->fov_turns[0] = word(p + 28u);
        return 1;
    }
    return 0;
}
int portable_course_3d_route(const PortableCourse3DView *view, uint32_t index,
                             PortableCourse3DRoute *out)
{
    const uint8_t *p = row_at(view, "RTHD", index);
    if (!p || !out)
        return 0;
    const PortableCourse3DRoute next = {word(p),      word(p + 4),  word(p + 8), word(p + 12),
                                        word(p + 16), word(p + 20), word(p + 24)};
    *out = next;
    return 1;
}
int portable_course_3d_route_node(const PortableCourse3DView *view, uint32_t index,
                                  PortableCourse3DRouteNode *out)
{
    const uint8_t *p = row_at(view, "RTND", index);
    if (!p || !out)
        return 0;
    const PortableCourse3DRouteNode next = {
        {signed_word(p), signed_word(p + 4), signed_word(p + 8)},
        signed_word(p + 12),
        signed_word(p + 16),
        word(p + 20),
        word(p + 24),
        word(p + 28)};
    *out = next;
    return 1;
}
int portable_course_3d_route_edge(const PortableCourse3DView *view, uint32_t index, uint32_t *out)
{
    const uint8_t *p = row_at(view, "RTED", index);
    if (!p || !out)
        return 0;
    *out = word(p);
    return 1;
}

int portable_course_3d_item(const PortableCourse3DView *view, uint32_t index,
                            PortableCourse3DItem *out)
{
    const PortableCourse3DTable *table = portable_course_3d_table(view, TAG("ITEM"));
    if (!out || !table || !table->bytes || table->stride != 32u || index >= table->count)
        return 0;
    const uint8_t *p = table->bytes + (size_t)index * 32u;
    if (!position(p, 3) || signed_word(p + 12u) <= 0 || word(p + 16u) > 65535u || !word(p + 20u) ||
        !span(word(p + 24u), word(p + 28u), count_at(view, "PROP")))
        return 0;
    const PortableCourse3DItem item = {{signed_word(p), signed_word(p + 4u), signed_word(p + 8u)},
                                       signed_word(p + 12u),
                                       word(p + 16u),
                                       word(p + 20u),
                                       word(p + 24u),
                                       word(p + 28u)};
    *out = item;
    return 1;
}

uint32_t portable_course_3d_vertex_rgba(const PortableCourse3DView *view, uint32_t index)
{
    const uint8_t *p = row_at(view, "VCOL", index);
    return p ? word(p) : UINT32_MAX;
}
int portable_course_3d_texture_animation(const PortableCourse3DView *view, uint32_t index,
                                         PortableCourse3DTextureAnimation *out)
{
    const uint8_t *p = row_at(view, "TXAN", index);
    if (!p || !out)
        return 0;
    const PortableCourse3DTextureAnimation next = {word(p), word(p + 4u), word(p + 8u), word(p + 12u),
                                                   word(p + 16u)};
    *out = next;
    return 1;
}
int portable_course_3d_texture_srt(const PortableCourse3DView *view, uint32_t index, int32_t out_q16[6])
{
    const uint8_t *p = row_at(view, "TXSR", index);
    if (!p || !out_q16)
        return 0;
    for (unsigned i = 0; i < 6u; ++i)
        out_q16[i] = signed_word(p + i * 4u);
    return 1;
}
int portable_course_3d_texture_key(const PortableCourse3DView *view, uint32_t index, uint32_t *frame,
                                   uint32_t *texture)
{
    const uint8_t *p = row_at(view, "TXPT", index);
    if (!p || !frame || !texture)
        return 0;
    *frame = word(p);
    *texture = word(p + 4u);
    return 1;
}
