#include "course_editor_pack.h"
#include "course_3d.h"
#include "editor_physics.h"
#include "editor_items.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

enum { TABLES=17, TABLE_BYTES=131072 };
typedef struct { const char *tag; unsigned stride, size; uint8_t bytes[TABLE_BYTES]; } Table;
static void word(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static uint8_t *row(Table *t){uint8_t *p=t->bytes+t->size;t->size+=t->stride;return p;}
static void value(uint8_t *p,unsigned i,int32_t n){word(p+4*i,(uint32_t)n);}
static unsigned string(Table *t,const char *s)
{unsigned at=t->size;size_t n=strlen(s)+1;memcpy(t->bytes+at,s,n);t->size+=(unsigned)n;return at;}
static void turn(int *x,int *y,unsigned r)
{while(r--){int t=*x;*x=-*y;*y=t;}}
static void quaternion(uint8_t *p,unsigned r)
{
    static const int32_t z[]={0,759250125,1073741824,759250125};
    static const int32_t w[]={1073741824,759250125,0,-759250125};
    value(p,2,z[r]);value(p,3,w[r]);
}
static void position(uint8_t *p,const CourseEditorPiece *piece)
{value(p,0,piece->x*65536);value(p,1,piece->y*65536);value(p,2,piece->z*65536);}
/* Every face has independent normals; winding is counterclockwise viewed from outside. */
static void face(Table *t,const CourseEditorPiece *p,const int corners[4][3],unsigned material,int solid)
{
    unsigned first=t[2].size/32, collision=t[5].size/12;
    int32_t points[4][3];
    for(unsigned i=0;i<4;++i){int x=corners[i][0],y=corners[i][1];turn(&x,&y,p->rotation);
        points[i][0]=(p->x+x)*65536;points[i][1]=(p->y+y)*65536;points[i][2]=(p->z+corners[i][2])*65536;}
    double a[3],b[3],normal[3];
    for(unsigned j=0;j<3;++j){a[j]=(points[1][j]-points[0][j])/65536.0;b[j]=(points[2][j]-points[0][j])/65536.0;}
    normal[0]=a[1]*b[2]-a[2]*b[1];normal[1]=a[2]*b[0]-a[0]*b[2];normal[2]=a[0]*b[1]-a[1]*b[0];
    double length=sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    for(unsigned i=0;i<4;++i){uint8_t *v=row(&t[2]),*c=solid?row(&t[5]):NULL;
        for(unsigned j=0;j<3;++j){value(v,j,points[i][j]);value(v,j+3,(int32_t)llround(normal[j]/length*1073741824.0));if(c)value(c,j,points[i][j]);}}
    const unsigned indices[2][3]={{0,1,2},{0,2,3}};
    for(unsigned i=0;i<2;++i){uint8_t *v=row(&t[3]),*c=solid?row(&t[6]):NULL;
        for(unsigned j=0;j<3;++j){word(v+j*4,first+indices[i][j]);if(c)word(c+j*4,collision+indices[i][j]);}
        word(v+12,material);
        if(c){word(c+12,material);word(c+20,UINT32_MAX);word(c+24,UINT32_MAX);word(c+28,UINT32_MAX);}}
}
static void geometry(Table *t,const CourseEditorPiece *p)
{
    unsigned mat=p->kind;
    if(p->kind==EDITOR_ROAD || p->kind==EDITOR_RAMP || p->kind==EDITOR_BOOST){
        int rise=p->kind==EDITOR_RAMP?4:0;
        int q[4][3]={{-4,-4,rise},{4,-4,rise},{4,4,0},{-4,4,0}};
        face(t,p,(const int (*)[3])q,mat,1);return;
    }
    if(p->kind!=EDITOR_WALL && p->kind!=EDITOR_DECORATION)return;
    int x=p->kind==EDITOR_WALL?4:1,y=1,h=3;
    const int q[5][4][3]={{{-x,-y,h},{x,-y,h},{x,y,h},{-x,y,h}},
        {{-x,-y,0},{x,-y,0},{x,-y,h},{-x,-y,h}},
        {{x,-y,0},{x,y,0},{x,y,h},{x,-y,h}},
        {{x,y,0},{-x,y,0},{-x,y,h},{x,y,h}},
        {{-x,y,0},{-x,-y,0},{-x,-y,h},{-x,y,h}}};
    for(unsigned i=0;i<5;++i)face(t,p,q[i],mat,p->kind==EDITOR_WALL);
}
int course_editor_pack(const CourseEditorDocument *d,uint32_t id,const char *name,uint8_t **bytes,
    size_t *size,char *error,size_t capacity)
{
    if(!bytes || !size || !name || !name[0] || strlen(name)>80)return 0;
    *bytes=NULL;*size=0;
    unsigned starts=0,gates=0,roads=0;
    if(!course_editor_valid(d) || !id || id==UINT32_MAX){snprintf(error,capacity,"Invalid editor document");return 0;}
    for(unsigned i=0;i<d->count;++i){starts+=d->pieces[i].kind==EDITOR_START;
        gates+=d->pieces[i].kind==EDITOR_GATE;roads+=d->pieces[i].kind==EDITOR_ROAD;}
    if(starts!=1 || gates<2 || !roads){snprintf(error,capacity,"Need one start, at least two gates and a road");return 0;}
    Table *t=calloc(TABLES,sizeof *t);if(!t)return 0;
    const char *tags[]={"STRS","META","VERT","TRIS","MATS","COLV","COLT","SURF","CPNT","CPED","RTHD","RTND","RTED","GRID","ITEM","RESP","PROP"};
    const unsigned strides[]={1,128,32,16,48,12,32,48,64,4,32,32,4,32,32,48,32};
    for(unsigned i=0;i<TABLES;++i){t[i].tag=tags[i];t[i].stride=strides[i];}
    unsigned title=string(t,name),empty=string(t,"");
    uint8_t *meta=row(&t[1]);word(meta,title);word(meta+4,empty);word(meta+8,empty);
    word(meta+12,2);word(meta+16,3);word(meta+20,id);
    for(unsigned i=7;i<12;++i)word(meta+4*i,UINT32_MAX);
    word(meta+48,0x365267ff);word(meta+52,0x365267ff);word(meta+60,512*65536);
    word(meta+64,0xb0b0b0ff);value(meta,19,-1073741824);word(meta+80,UINT32_MAX);
    word(meta+84,1845723);word(meta+88,id);word(meta+100,1+d->count*2);
    uint8_t *prop=row(&t[16]);word(prop,string(t,"mksc:item-rules"));word(prop+4,4);word(prop+8,103);word(prop+12,id);
    const uint32_t colors[]={0x627b8aff,0xe8ad63ff,0x7ba8bdff,0xf5cc43ff,0x68dcf3ff,0xf4f4f4ff,0xf080a0ff,0x6ed7a5ff,0xb8a3e0ff};
    for(unsigned i=0;i<EDITOR_KINDS;++i){uint8_t *m=row(&t[4]),*s=row(&t[7]);
        for(unsigned j=0;j<4;++j)word(m+j*4,UINT32_MAX);
        word(m+16,colors[i]);word(m+28,65536);word(m+36,1);
        word(s,i==EDITOR_WALL?3:i==EDITOR_BOOST?4:0);word(s+8,65536);word(s+12,65536);
        word(s+24,i==EDITOR_BOOST?3*65536:0);word(s+32,65536);}
    uint8_t *route=row(&t[10]);word(route+8,gates);word(route+16,1);
    unsigned gate=0;
    for(unsigned i=0;i<d->count;++i){const CourseEditorPiece *p=&d->pieces[i];char key[48];
        snprintf(key,sizeof key,"editor:position:%u",i);prop=row(&t[16]);word(prop,string(t,key));word(prop+4,7);position(prop+8,p);
        snprintf(key,sizeof key,"editor:piece:%u",i);prop=row(&t[16]);word(prop,string(t,key));word(prop+4,0);word(prop+8,p->kind*4+p->rotation);
        /* An authored ramp or boost tile at the same grid anchor replaces the
         * starter floor, so coplanar road cannot hide its collision surface. */
        int covered=0;
        if(p->kind==EDITOR_ROAD)for(unsigned j=0;j<d->count;++j){const CourseEditorPiece *q=&d->pieces[j];
            if((q->kind==EDITOR_RAMP || q->kind==EDITOR_BOOST) && q->x==p->x && q->y==p->y && q->z==p->z)covered=1;}
        if(!covered)geometry(t,p);
        if(p->kind==EDITOR_START){uint8_t *r=row(&t[13]);position(r,p);quaternion(r+12,p->rotation);}
        if(p->kind==EDITOR_ITEM){uint8_t *r=row(&t[14]);position(r,p);word(r+12,2*65536);word(r+16,p->rotation*16384);word(r+20,180);}
        if(p->kind==EDITOR_RESPAWN){uint8_t *r=row(&t[15]);position(r,p);quaternion(r+12,p->rotation);
            word(r+28,gate?gate-1:0);word(r+32,4*65536);word(r+40,UINT32_MAX);}
        if(p->kind==EDITOR_GATE){uint8_t *r=row(&t[8]);position(r,p);
            int nx=0,ny=-1,rx=1,ry=0;turn(&nx,&ny,p->rotation);turn(&rx,&ry,p->rotation);
            value(r,3,nx*1073741824);value(r,4,ny*1073741824);value(r,6,rx*1073741824);value(r,7,ry*1073741824);
            word(r+36,6*65536);word(r+40,6*65536);word(r+44,gate);word(r+48,gate);
            word(r+52,gate);word(r+56,1);word(r+60,gate?4:5);word(row(&t[9]),(gate+1)%gates);
            uint8_t *node=row(&t[11]);position(node,p);word(node+12,2*65536);word(node+16,4*65536);
            word(node+20,gate);word(node+24,1);word(row(&t[12]),(gate+1)%gates);++gate;}
    }
    size_t course_size=64+20*TABLES;
    for(unsigned i=0;i<TABLES;++i)course_size+=(t[i].size+3u)&~3u;
    uint8_t *course=calloc(1,course_size);if(!course){free(t);return 0;}
    memcpy(course,"C3D1",4);word(course+4,1);word(course+8,(uint32_t)course_size);word(course+12,id);word(course+20,64);word(course+24,TABLES);
    size_t at=64+20*TABLES;
    for(unsigned i=0;i<TABLES;++i){uint8_t *entry=course+64+20*i;memcpy(entry,t[i].tag,4);
        word(entry+4,(uint32_t)at);word(entry+8,t[i].size/t[i].stride);word(entry+12,t[i].stride);word(entry+16,i<14);
        memcpy(course+at,t[i].bytes,t[i].size);at+=(t[i].size+3u)&~3u;}
    free(t);
    uint8_t catalog[40]={0},physics[sizeof editor_physics],items[sizeof editor_items];
    memcpy(catalog,"C3C1",4);word(catalog+4,1);word(catalog+8,1);word(catalog+12,24);word(catalog+16,id);word(catalog+20,id);word(catalog+28,2);
    for(unsigned i=0;i<sizeof editor_physics/4;++i)word(physics+i*4,editor_physics[i]);
    word(physics+12,id);
    for(unsigned i=0;i<sizeof editor_items/4;++i)word(items+i*4,editor_items[i]);
    word(items+12,id);
    const uint8_t *payloads[]={course,catalog,physics,items};
    const size_t sizes[]={course_size,sizeof catalog,sizeof physics,sizeof items};
    const uint32_t kinds[]={99,100,102,103},versions[]={1,1,2,1};
    size_t total=112+40*4;
    for(unsigned i=0;i<4;++i)total=((total+7)&~(size_t)7)+sizes[i];
    uint8_t *pack=calloc(1,total);if(!pack){free(course);return 0;}
    memcpy(pack,"MKSCPACK",8);pack[8]=1;pack[10]=112;word(pack+12,0x12345678);word(pack+16,(uint32_t)total);
    word(pack+24,4);word(pack+28,40);word(pack+32,112);
    const uint8_t *parts[8];size_t lengths[8];at=112+160;
    for(unsigned i=0;i<4;++i){at=(at+7)&~(size_t)7;uint8_t *entry=pack+112+40*i;
        word(entry,kinds[i]);word(entry+4,id);word(entry+8,versions[i]);word(entry+12,3);
        word(entry+16,(uint32_t)at);word(entry+24,(uint32_t)sizes[i]);word(entry+32,1);
        memcpy(pack+at,payloads[i],sizes[i]);parts[i*2]=entry;lengths[i*2]=40;parts[i*2+1]=pack+at;lengths[i*2+1]=sizes[i];at+=sizes[i];}
    portable_asset_pack_sha256(parts,lengths,8,pack+72);
    const uint8_t *whole=pack;portable_asset_pack_sha256(&whole,&total,1,pack+40);free(course);
    PortableAssetPackView view;PortableCourse3DView c;
    if(portable_asset_pack_open(pack,total,&view)!=PORTABLE_ASSET_PACK_OK ||
        !portable_course_3d_open(&view,id,&c,error,capacity)){free(pack);return 0;}
    *bytes=pack;*size=total;return 1;
}

int course_editor_unpack(const PortableAssetPackView *pack,uint32_t id,CourseEditorDocument *out,
    char *error,size_t capacity)
{
    if(!pack || !out)return 0;
    PortableCourse3DView course;PortableCourse3DMeta meta;CourseEditorDocument d={0};
    if(!portable_course_3d_open(pack,id,&course,error,capacity) || !portable_course_3d_meta(&course,&meta))return 0;
    for(unsigned i=0;i<EDITOR_LIMIT;++i){char key[48];PortableCourse3DProperty p,k;
        snprintf(key,sizeof key,"editor:position:%u",i);
        if(!portable_course_3d_property_find(&course,meta.property_first,meta.property_count,key,&p))break;
        snprintf(key,sizeof key,"editor:piece:%u",i);
        if(p.type!=PORTABLE_COURSE_3D_PROPERTY_VECTOR ||
            !portable_course_3d_property_find(&course,meta.property_first,meta.property_count,key,&k) ||
            k.type!=PORTABLE_COURSE_3D_PROPERTY_INTEGER || k.value.integer<0)goto invalid;
        for(unsigned a=0;a<3;++a)if(p.value.vector_q16[a]%65536)goto invalid;
        d.pieces[d.count++]=(CourseEditorPiece){p.value.vector_q16[0]/65536,p.value.vector_q16[1]/65536,p.value.vector_q16[2]/65536,
            (unsigned)k.value.integer/4,(unsigned)k.value.integer%4};
    }
    if(!d.count || meta.property_count!=1+d.count*2 || !course_editor_valid(&d))goto invalid;
    *out=d;return 1;
invalid:
    snprintf(error,capacity,"Not an editable Workshop document");return 0;
}
